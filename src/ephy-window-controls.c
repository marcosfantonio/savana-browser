/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "ephy-window-controls.h"

#include <librsvg/rsvg.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RsvgHandle, g_object_unref)

static cairo_status_t
append_png (void                *closure,
            const unsigned char *data,
            unsigned int         length)
{
  g_byte_array_append (closure, data, length);
  return CAIRO_STATUS_SUCCESS;
}

/* Keep GtkWindowControls in charge of actions, accessibility and placement.
 * Only replace their artwork; Aurorae SVGs are sprite sheets, not plain icons. */
static char *
render_button (RsvgHandle *handle,
               const char *contents,
               gsize       length,
               const char *state,
               int         width,
               int         height)
{
  g_autofree char *id = g_strdup_printf ("#%s-center", state);
  g_autofree char *key = NULL;
  g_autofree char *digest = NULL;
  g_autofree char *directory = NULL;
  g_autofree char *path = NULL;
  g_autoptr (GChecksum) checksum = g_checksum_new (G_CHECKSUM_SHA256);
  g_autoptr (GByteArray) png = g_byte_array_new ();
  cairo_surface_t *surface;
  cairo_t *cr;
  RsvgRectangle viewport = { 0, 0, width * 2, height * 2 };
  gboolean rendered;
  cairo_status_t status;

  if (!rsvg_handle_has_sub (handle, id)) {
    g_free (id);
    id = g_strdup ("#active-center");
  }
  key = g_strdup_printf ("v1:%s:%d:%d", id, width, height);
  g_checksum_update (checksum, (const guchar *)contents, length);
  g_checksum_update (checksum, (const guchar *)key, strlen (key));
  digest = g_strdup_printf ("%s.png", g_checksum_get_string (checksum));
  directory = g_build_filename (g_get_user_cache_dir (), "savana", "window-buttons", NULL);
  if (g_mkdir_with_parents (directory, 0700) < 0)
    return NULL;
  path = g_build_filename (directory, digest, NULL);
  if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
    return g_filename_to_uri (path, NULL, NULL);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width * 2, height * 2);
  cr = cairo_create (surface);
  rendered = rsvg_handle_render_element (handle, cr, id, &viewport, NULL);
  cairo_destroy (cr);
  status = rendered ? cairo_surface_write_to_png_stream (surface, append_png, png) : CAIRO_STATUS_WRITE_ERROR;
  cairo_surface_destroy (surface);
  if (status != CAIRO_STATUS_SUCCESS ||
      !g_file_set_contents_full (path, (const char *)png->data, png->len,
                                 G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL))
    return NULL;
  return g_filename_to_uri (path, NULL, NULL);
}

GtkCssProvider *
ephy_window_controls_load_theme (const char *directory)
{
  const char *buttons[] = { "close", "minimize", "maximize", "restore" };
  const char *states[] = { "active", "inactive", "hover", "pressed", "hover-inactive", "pressed-inactive" };
  const char *pseudos[] = { "", ":backdrop", ":hover", ":active", ":backdrop:hover", ":backdrop:active" };
  g_autoptr (GString) css = g_string_new (NULL);
  g_autoptr (GKeyFile) config = g_key_file_new ();
  g_autofree char *name = g_path_get_basename (directory);
  g_autofree char *rc_name = g_strconcat (name, "rc", NULL);
  g_autofree char *rc_path = g_build_filename (directory, rc_name, NULL);
  GtkCssProvider *provider;
  int width = 16;
  int height = 16;

  if (g_key_file_load_from_file (config, rc_path, G_KEY_FILE_NONE, NULL)) {
    if (g_key_file_has_key (config, "Layout", "ButtonWidth", NULL))
      width = CLAMP (g_key_file_get_integer (config, "Layout", "ButtonWidth", NULL), 8, 64);
    if (g_key_file_has_key (config, "Layout", "ButtonHeight", NULL))
      height = CLAMP (g_key_file_get_integer (config, "Layout", "ButtonHeight", NULL), 8, 64);
  }

  for (guint b = 0; b < G_N_ELEMENTS (buttons); b++) {
    g_autofree char *filename = g_strdup_printf ("%s.svg", buttons[b]);
    g_autofree char *path = g_build_filename (directory, filename, NULL);
    g_autofree char *contents = NULL;
    g_autoptr (RsvgHandle) handle = NULL;
    gsize length;
    const char *button = b == 3 ? "maximize" : buttons[b];
    const char *window = b == 3 ? "window.maximized" : "window";

    if (!g_file_get_contents (path, &contents, &length, NULL)) {
      g_free (path);
      path = g_strconcat (directory, G_DIR_SEPARATOR_S, buttons[b], ".svgz", NULL);
      if (!g_file_get_contents (path, &contents, &length, NULL))
        return NULL;
    }
    handle = rsvg_handle_new_from_data ((const guint8 *)contents, length, NULL);
    if (!handle || !rsvg_handle_has_sub (handle, "#active-center"))
      return NULL;

    for (guint s = 0; s < G_N_ELEMENTS (states); s++) {
      g_autofree char *uri = render_button (handle, contents, length, states[s], width, height);

      if (!uri)
        return NULL;
      g_string_append_printf (css,
                              "%s .savana-header-bar windowcontrols button.%s%s {"
                              "background: transparent; background-image: url(\"%s\");"
                              "background-size: %dpx %dpx; background-position: center;"
                              "background-repeat: no-repeat; border: none; box-shadow: none; }\n"
                              "%s .savana-header-bar windowcontrols button.%s%s > image {"
                              "-gtk-icon-source: none; min-width: %dpx; min-height: %dpx; }\n",
                              window, button, pseudos[s], uri, width, height,
                              window, button, pseudos[s], width, height);
    }
  }

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider, css->str);
  return provider;
}

GtkCssProvider *
ephy_window_controls_load_active_theme (void)
{
  g_autoptr (GKeyFile) config = g_key_file_new ();
  g_autoptr (GString) decoration_config = g_string_new (NULL);
  g_autofree char *contents = NULL;
  g_auto (GStrv) lines = NULL;
  g_autofree char *path = g_build_filename (g_get_user_config_dir (), "kwinrc", NULL);
  g_autofree char *library = NULL;
  g_autofree char *theme = NULL;
  const char *prefix = "__aurorae__svg__";
  const char *name;
  const char * const *system_dirs;
  const char *desktop = g_getenv ("XDG_CURRENT_DESKTOP");
  gboolean in_decoration_group = FALSE;

  if (!desktop || !g_strrstr (desktop, "KDE") ||
      !g_file_get_contents (path, &contents, NULL, NULL))
    return NULL;
  /* KConfig's nested [Group][Subgroup] headers are not valid GKeyFile
   * groups. Isolate the plain decoration group before parsing its keys. */
  lines = g_strsplit (contents, "\n", -1);
  for (guint i = 0; lines[i]; i++) {
    const char *line = g_strstrip (lines[i]);

    if (*line == '[')
      in_decoration_group = g_str_equal (line, "[org.kde.kdecoration2]");
    if (in_decoration_group) {
      g_string_append (decoration_config, line);
      g_string_append_c (decoration_config, '\n');
    }
  }
  if (!g_key_file_load_from_data (config, decoration_config->str, decoration_config->len,
                                   G_KEY_FILE_NONE, NULL))
    return NULL;
  library = g_key_file_get_string (config, "org.kde.kdecoration2", "library", NULL);
  theme = g_key_file_get_string (config, "org.kde.kdecoration2", "theme", NULL);
  if (g_strcmp0 (library, "org.kde.kwin.aurorae.v2") ||
      !theme || !g_str_has_prefix (theme, prefix))
    return NULL;
  name = theme + strlen (prefix);

  if (!*name || strchr (name, '/') || g_str_equal (name, ".") || g_str_equal (name, ".."))
    return NULL;
  g_free (path);
  path = g_build_filename (g_get_user_data_dir (), "aurorae", "themes", name, NULL);
  if (g_file_test (path, G_FILE_TEST_IS_DIR))
    return ephy_window_controls_load_theme (path);
  system_dirs = g_get_system_data_dirs ();
  for (guint i = 0; system_dirs[i]; i++) {
    g_free (path);
    path = g_build_filename (system_dirs[i], "aurorae", "themes", name, NULL);
    if (g_file_test (path, G_FILE_TEST_IS_DIR))
      return ephy_window_controls_load_theme (path);
  }
  return NULL;
}
