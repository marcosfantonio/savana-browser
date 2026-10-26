/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib/gi18n.h>

#include "ephy-memory-guard.h"
#include "ephy-prefs.h"
#include "ephy-settings.h"

static gboolean
write_limit (const char *path,
             guint64     limit)
{
  g_autofree char *value = g_strdup_printf ("%" G_GUINT64_FORMAT, limit);
  int fd = open (path, O_WRONLY | O_CLOEXEC);
  ssize_t written;

  if (fd < 0)
    return FALSE;
  written = write (fd, value, strlen (value));
  close (fd);
  return written == (ssize_t)strlen (value);
}

static gboolean
set_systemd_limits (const char *cgroup_path,
                    guint64     memory_limit,
                    guint64     swap_limit)
{
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree char *unit = g_path_get_basename (cgroup_path);
  GVariantBuilder properties;
  g_autoptr (GVariant) reply = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (!connection) {
    g_debug ("Memory guard: no user session bus: %s", error->message);
    return FALSE;
  }

  g_variant_builder_init (&properties, G_VARIANT_TYPE ("a(sv)"));
  g_variant_builder_add (&properties, "(sv)", "MemoryMax", g_variant_new_uint64 (memory_limit));
  g_variant_builder_add (&properties, "(sv)", "MemorySwapMax", g_variant_new_uint64 (swap_limit));
  reply = g_dbus_connection_call_sync (connection,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "SetUnitProperties",
                                       g_variant_new ("(sba(sv))", unit, TRUE, &properties),
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       3000,
                                       NULL,
                                       &error);
  if (!reply) {
    g_debug ("Memory guard: systemd could not set limits for %s: %s", unit, error->message);
    return FALSE;
  }

  g_debug ("Memory guard: systemd set runtime limits for %s", unit);
  return TRUE;
}

void
ephy_memory_guard_configure (void)
{
  g_autofree char *contents = NULL;
  g_autofree char *cgroup_path = NULL;
  g_autofree char *cgroup_dir = NULL;
  g_autofree char *memory_path = NULL;
  g_autofree char *swap_path = NULL;
  g_autofree char *procs_path = NULL;
  g_autofree char *memory_current_limit = NULL;
  g_autofree char *swap_current_limit = NULL;
  g_autofree char *procs = NULL;
  g_auto (GStrv) lines = NULL;
  g_auto (GStrv) pids = NULL;
  guint count = 0;
  long page_size;
  long page_count;
  guint64 ram;
  guint64 memory_limit;
  guint64 swap_limit;

  if (!g_settings_get_boolean (EPHY_SETTINGS_WEB, EPHY_PREFS_WEB_ENABLE_MEMORY_GUARD) ||
      !g_file_get_contents ("/proc/self/cgroup", &contents, NULL, NULL))
    return;

  lines = g_strsplit (contents, "\n", -1);
  for (guint i = 0; lines[i]; i++) {
    if (g_str_has_prefix (lines[i], "0::")) {
      cgroup_path = g_strdup (lines[i] + 3);
      break;
    }
  }

  /* Never change a cgroup shared with a shell, session, or unrelated app. */
  if (!cgroup_path || !g_str_has_prefix (cgroup_path, "/user.slice/") ||
      !strstr (cgroup_path, "app-com.savana.Browser@"))
    return;

  cgroup_dir = g_build_filename ("/sys/fs/cgroup", cgroup_path, NULL);
  memory_path = g_build_filename (cgroup_dir, "memory.max", NULL);
  swap_path = g_build_filename (cgroup_dir, "memory.swap.max", NULL);
  procs_path = g_build_filename (cgroup_dir, "cgroup.procs", NULL);
  if (!g_file_get_contents (procs_path, &procs, NULL, NULL))
    return;
  pids = g_strsplit (procs, "\n", -1);
  for (guint i = 0; pids[i]; i++)
    count += pids[i][0] != '\0';
  if (count != 1 ||
      !g_file_get_contents (memory_path, &memory_current_limit, NULL, NULL) ||
      !g_file_get_contents (swap_path, &swap_current_limit, NULL, NULL) ||
      !g_str_has_prefix (g_strstrip (memory_current_limit), "max") ||
      !g_str_has_prefix (g_strstrip (swap_current_limit), "max"))
    return;

  page_size = sysconf (_SC_PAGESIZE);
  page_count = sysconf (_SC_PHYS_PAGES);
  if (page_size <= 0 || page_count <= 0)
    return;
  ram = (guint64)page_size * page_count;
  memory_limit = CLAMP (ram / 2, 1536ULL * 1024 * 1024, 8ULL * 1024 * 1024 * 1024);
  swap_limit = CLAMP (ram / 4, 512ULL * 1024 * 1024, 4ULL * 1024 * 1024 * 1024);

  /* Runtime unit properties survive systemd manager reloads. Direct cgroup
   * writes remain a fallback for delegated user cgroups. */
  if (set_systemd_limits (cgroup_path, memory_limit, swap_limit))
    return;

  if (!write_limit (memory_path, memory_limit) || !write_limit (swap_path, swap_limit)) {
    g_warning ("Could not apply Savana memory limits in %s", cgroup_path);
    return;
  }
  g_debug ("Applied Savana memory guard (memory=%" G_GUINT64_FORMAT
           ", swap=%" G_GUINT64_FORMAT ")", memory_limit, swap_limit);
}
