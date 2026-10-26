/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "ephy-global-menu.h"
#include <string.h>

#if HAVE_DBUSMENU
#include <libdbusmenu-glib/menuitem.h>
#include <libdbusmenu-glib/server.h>

#if HAVE_GLOBAL_MENU_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#include <wayland-client.h>
#include "appmenu-client-protocol.h"
#endif
#if HAVE_GLOBAL_MENU_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#endif

typedef struct {
  GWeakRef group;
  char *action;
  GVariant *target;
} ItemAction;

typedef struct {
  GDBusConnection *connection;
  DbusmenuServer *server;
  char *path;
  guint registration_idle;
  GCancellable *registration_cancellable;
  guint registration_attempts;
  GdkDisplay *display;
#if HAVE_GLOBAL_MENU_WAYLAND
  struct wl_event_queue *queue;
  struct org_kde_kwin_appmenu_manager *manager;
  struct org_kde_kwin_appmenu *appmenu;
#endif
#if HAVE_GLOBAL_MENU_X11
  guint registrar_watch;
  Window xid;
#endif
} WindowMenu;

static void
item_action_free (gpointer data)
{
  ItemAction *action = data;

  g_weak_ref_clear (&action->group);
  g_free (action->action);
  g_clear_pointer (&action->target, g_variant_unref);
  g_free (action);
}

static void
update_item (DbusmenuMenuitem *item)
{
  ItemAction *action = g_object_get_data (G_OBJECT (item), "action");
  g_autoptr (GObject) object = g_weak_ref_get (&action->group);
  g_autoptr (GVariant) state = NULL;
  gboolean exists = object && g_action_group_has_action (G_ACTION_GROUP (object), action->action);

  dbusmenu_menuitem_property_set_bool (item, DBUSMENU_MENUITEM_PROP_ENABLED,
                                      exists && g_action_group_get_action_enabled (G_ACTION_GROUP (object), action->action));
  if (exists)
    state = g_action_group_get_action_state (G_ACTION_GROUP (object), action->action);
  if (state && g_variant_is_of_type (state, G_VARIANT_TYPE_BOOLEAN)) {
    dbusmenu_menuitem_property_set (item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE, DBUSMENU_MENUITEM_TOGGLE_CHECK);
    dbusmenu_menuitem_property_set_int (item, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE, g_variant_get_boolean (state));
  } else if (state && action->target && g_variant_is_of_type (state, g_variant_get_type (action->target))) {
    dbusmenu_menuitem_property_set (item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE, DBUSMENU_MENUITEM_TOGGLE_RADIO);
    dbusmenu_menuitem_property_set_int (item, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE, g_variant_equal (state, action->target));
  } else {
    dbusmenu_menuitem_property_remove (item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE);
    dbusmenu_menuitem_property_remove (item, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE);
  }
}

static void
action_membership_changed (GActionGroup *group,
                           const char *name,
                           DbusmenuMenuitem *item)
{
  ItemAction *action = g_object_get_data (G_OBJECT (item), "action");

  if (g_str_equal (name, action->action))
    update_item (item);
}

static void
action_enabled_changed (GActionGroup *group,
                        const char *name,
                        gboolean enabled,
                        DbusmenuMenuitem *item)
{
  action_membership_changed (group, name, item);
}

static void
action_state_changed (GActionGroup *group,
                      const char *name,
                      GVariant *state,
                      DbusmenuMenuitem *item)
{
  action_membership_changed (group, name, item);
}

static void
item_activated (DbusmenuMenuitem *item,
                guint timestamp,
                gpointer data)
{
  ItemAction *action = g_object_get_data (G_OBJECT (item), "action");
  g_autoptr (GObject) object = g_weak_ref_get (&action->group);

  if (object && g_action_group_has_action (G_ACTION_GROUP (object), action->action) &&
      g_action_group_get_action_enabled (G_ACTION_GROUP (object), action->action))
    g_action_group_activate_action (G_ACTION_GROUP (object), action->action, action->target);
}

static void
set_shortcut (DbusmenuMenuitem *item,
              GtkApplication *application,
              const char *detailed_action)
{
  g_autofree char *original = NULL;
  g_auto (GStrv) accels = NULL;
  GdkModifierType modifiers;
  guint key;
  GVariantBuilder shortcuts;
  GVariantBuilder shortcut;

  /* Window aliases preserve the original toolbar/tab accelerator names. */
  if (g_str_has_prefix (detailed_action, "win.global-toolbar-"))
    original = g_strconcat ("toolbar.", detailed_action + strlen ("win.global-toolbar-"), NULL);
  else if (g_str_has_prefix (detailed_action, "win.global-tab-"))
    original = g_strconcat ("tab.", detailed_action + strlen ("win.global-tab-"), NULL);
  accels = gtk_application_get_accels_for_action (application, original ? original : detailed_action);
  if (!accels[0] || !gtk_accelerator_parse (accels[0], &key, &modifiers) || !gdk_keyval_name (key))
    return;
  g_variant_builder_init (&shortcuts, G_VARIANT_TYPE ("aas"));
  g_variant_builder_init (&shortcut, G_VARIANT_TYPE ("as"));
  if (modifiers & GDK_CONTROL_MASK)
    g_variant_builder_add (&shortcut, "s", "Control");
  if (modifiers & GDK_ALT_MASK)
    g_variant_builder_add (&shortcut, "s", "Alt");
  if (modifiers & GDK_SHIFT_MASK)
    g_variant_builder_add (&shortcut, "s", "Shift");
  if (modifiers & GDK_SUPER_MASK)
    g_variant_builder_add (&shortcut, "s", "Super");
  if (modifiers & GDK_META_MASK)
    g_variant_builder_add (&shortcut, "s", "Meta");
  g_variant_builder_add (&shortcut, "s", gdk_keyval_name (gdk_keyval_to_upper (key)));
  g_variant_builder_add_value (&shortcuts, g_variant_builder_end (&shortcut));
  dbusmenu_menuitem_property_set_variant (item, DBUSMENU_MENUITEM_PROP_SHORTCUT, g_variant_builder_end (&shortcuts));
}

static void
append_model (DbusmenuMenuitem *parent,
              GMenuModel *model,
              GtkApplicationWindow *window)
{
  GtkApplication *application = gtk_window_get_application (GTK_WINDOW (window));

  for (int i = 0; i < g_menu_model_get_n_items (model); i++) {
    g_autoptr (GMenuModel) section = g_menu_model_get_item_link (model, i, G_MENU_LINK_SECTION);
    g_autoptr (GMenuModel) submenu = g_menu_model_get_item_link (model, i, G_MENU_LINK_SUBMENU);
    g_autofree char *label = NULL;
    g_autofree char *name = NULL;
    DbusmenuMenuitem *item;

    if (section) {
      if (dbusmenu_menuitem_get_children (parent)) {
        item = dbusmenu_menuitem_new ();
        dbusmenu_menuitem_property_set (item, DBUSMENU_MENUITEM_PROP_TYPE, "separator");
        dbusmenu_menuitem_child_append (parent, item);
        g_object_unref (item);
      }
      append_model (parent, section, window);
      continue;
    }
    item = dbusmenu_menuitem_new ();
    g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label);
    dbusmenu_menuitem_property_set (item, DBUSMENU_MENUITEM_PROP_LABEL, label ? label : "");
    dbusmenu_menuitem_property_set_bool (item, DBUSMENU_MENUITEM_PROP_VISIBLE, TRUE);
    if (submenu) {
      dbusmenu_menuitem_property_set (item, DBUSMENU_MENUITEM_PROP_CHILD_DISPLAY, DBUSMENU_MENUITEM_CHILD_DISPLAY_SUBMENU);
      append_model (item, submenu, window);
    } else if (g_menu_model_get_item_attribute (model, i, G_MENU_ATTRIBUTE_ACTION, "s", &name)) {
      GActionGroup *group = NULL;
      const char *local_name = NULL;

      if (g_str_has_prefix (name, "win.")) {
        group = G_ACTION_GROUP (window);
        local_name = name + 4;
      } else if (g_str_has_prefix (name, "app.")) {
        group = G_ACTION_GROUP (application);
        local_name = name + 4;
      }
      if (group) {
        ItemAction *action = g_new0 (ItemAction, 1);

        g_weak_ref_init (&action->group, G_OBJECT (group));
        action->action = g_strdup (local_name);
        action->target = g_menu_model_get_item_attribute_value (model, i, G_MENU_ATTRIBUTE_TARGET, NULL);
        g_object_set_data_full (G_OBJECT (item), "action", action, item_action_free);
        g_signal_connect (item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK (item_activated), NULL);
        g_signal_connect_object (group, "action-enabled-changed", G_CALLBACK (action_enabled_changed), item, 0);
        g_signal_connect_object (group, "action-state-changed", G_CALLBACK (action_state_changed), item, 0);
        g_signal_connect_object (group, "action-added", G_CALLBACK (action_membership_changed), item, 0);
        g_signal_connect_object (group, "action-removed", G_CALLBACK (action_membership_changed), item, 0);
        update_item (item);
        set_shortcut (item, application, name);
      } else {
        dbusmenu_menuitem_property_set_bool (item, DBUSMENU_MENUITEM_PROP_ENABLED, FALSE);
      }
    }
    dbusmenu_menuitem_child_append (parent, item);
    g_object_unref (item);
  }
}

#if HAVE_GLOBAL_MENU_WAYLAND
static void
registry_global (void *data,
                 struct wl_registry *registry,
                 uint32_t name,
                 const char *interface,
                 uint32_t version)
{
  WindowMenu *menu = data;

  if (!menu->manager && g_str_equal (interface, org_kde_kwin_appmenu_manager_interface.name))
    menu->manager = wl_registry_bind (registry, name, &org_kde_kwin_appmenu_manager_interface, MIN (version, 2));
}

static void
registry_global_remove (void *data,
                        struct wl_registry *registry,
                        uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
  registry_global,
  registry_global_remove,
};
#endif

#if HAVE_GLOBAL_MENU_X11
static void
registrar_appeared (GDBusConnection *connection,
                    const char *name,
                    const char *owner,
                    gpointer data)
{
  WindowMenu *menu = data;

  /* Name watching also re-registers after a panel/registrar restart. */
  g_dbus_connection_call (connection, name, "/com/canonical/AppMenu/Registrar",
                          "com.canonical.AppMenu.Registrar", "RegisterWindow",
                          g_variant_new ("(uo)", (guint32)menu->xid, menu->path),
                          NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, NULL, NULL);
}
#endif

static void
unregister_surface (WindowMenu *menu)
{
#if HAVE_GLOBAL_MENU_WAYLAND
  if (menu->appmenu) {
    org_kde_kwin_appmenu_release (menu->appmenu);
    menu->appmenu = NULL;
  }
  if (menu->manager) {
    if (wl_proxy_get_version ((struct wl_proxy *)menu->manager) >= 2)
      org_kde_kwin_appmenu_manager_release (menu->manager);
    else
      wl_proxy_destroy ((struct wl_proxy *)menu->manager);
    menu->manager = NULL;
  }
  if (menu->queue) {
    wl_event_queue_destroy (menu->queue);
    menu->queue = NULL;
  }
#endif
#if HAVE_GLOBAL_MENU_X11
  if (menu->registrar_watch) {
    GDBusConnection *connection = menu->connection;

    g_bus_unwatch_name (menu->registrar_watch);
    menu->registrar_watch = 0;
    if (connection)
      g_dbus_connection_call (connection, "com.canonical.AppMenu.Registrar", "/com/canonical/AppMenu/Registrar",
                              "com.canonical.AppMenu.Registrar", "UnregisterWindow", g_variant_new ("(u)", (guint32)menu->xid),
                              NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, NULL, NULL, NULL);
    menu->xid = 0;
  }
#endif
  g_clear_object (&menu->display);
}

static void
register_surface (GtkApplicationWindow *window)
{
  WindowMenu *menu = g_object_get_data (G_OBJECT (window), "savanna-dbusmenu");
  GtkApplication *application = gtk_window_get_application (GTK_WINDOW (window));
  GDBusConnection *connection = g_application_get_dbus_connection (G_APPLICATION (application));
  GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));
  const char *service;

  if (!surface || !connection || menu->display)
    return;
  service = g_dbus_connection_get_unique_name (connection);
  menu->display = g_object_ref (gdk_surface_get_display (surface));
#if HAVE_GLOBAL_MENU_WAYLAND
  if (GDK_IS_WAYLAND_SURFACE (surface)) {
    struct wl_display *display = gdk_wayland_display_get_wl_display (menu->display);
    struct wl_registry *registry = wl_display_get_registry (display);

    /* Never dispatch GTK's Wayland event queue from our registry discovery. */
    menu->queue = wl_display_create_queue (display);
    wl_proxy_set_queue ((struct wl_proxy *)registry, menu->queue);
    wl_registry_add_listener (registry, &registry_listener, menu);
    if (wl_display_roundtrip_queue (display, menu->queue) >= 0 && menu->manager) {
      menu->appmenu = org_kde_kwin_appmenu_manager_create (menu->manager, gdk_wayland_surface_get_wl_surface (surface));
      org_kde_kwin_appmenu_set_address (menu->appmenu, service, menu->path);
      wl_display_flush (display);
    }
    wl_registry_destroy (registry);
  }
#endif
#if HAVE_GLOBAL_MENU_X11
  /* GTK's X11 backend is deprecated, but remains necessary for X11 panels. */
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  if (GDK_IS_X11_SURFACE (surface)) {
    Display *display = gdk_x11_display_get_xdisplay (menu->display);

    menu->xid = gdk_x11_surface_get_xid (surface);
    gdk_x11_display_error_trap_push (menu->display);
    XChangeProperty (display, menu->xid, XInternAtom (display, "_KDE_NET_WM_APPMENU_SERVICE_NAME", False),
                     XA_STRING, 8, PropModeReplace, (const unsigned char *)service, strlen (service));
    XChangeProperty (display, menu->xid, XInternAtom (display, "_KDE_NET_WM_APPMENU_OBJECT_PATH", False),
                     XA_STRING, 8, PropModeReplace, (const unsigned char *)menu->path, strlen (menu->path));
    XFlush (display);
    gdk_x11_display_error_trap_pop_ignored (menu->display);
    menu->registrar_watch = g_bus_watch_name_on_connection (connection, "com.canonical.AppMenu.Registrar",
                                                           G_BUS_NAME_WATCHER_FLAGS_NONE, registrar_appeared, NULL, menu, NULL);
  }
  G_GNUC_END_IGNORE_DEPRECATIONS
#endif
}

static gboolean register_surface_idle (gpointer data);

static void
export_checked (GObject *source,
                 GAsyncResult *result,
                 gpointer data)
{
  g_autoptr (GtkApplicationWindow) window = data;
  WindowMenu *menu = g_object_get_data (G_OBJECT (window), "savanna-dbusmenu");
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  const char *xml = NULL;

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) || !gtk_widget_get_mapped (GTK_WIDGET (window)))
    return;
  if (reply)
    g_variant_get (reply, "(&s)", &xml);
  if (xml && strstr (xml, "com.canonical.dbusmenu")) {
    register_surface (window);
  } else if (++menu->registration_attempts < 100) {
    menu->registration_idle = g_timeout_add_full (G_PRIORITY_DEFAULT, 25, register_surface_idle, g_object_ref (window), g_object_unref);
  } else {
    g_warning ("Global menu D-Bus object was not registered: %s", menu->path);
  }
}

static gboolean
register_surface_idle (gpointer data)
{
  GtkApplicationWindow *window = data;
  WindowMenu *menu = g_object_get_data (G_OBJECT (window), "savanna-dbusmenu");

  menu->registration_idle = 0;
  if (!gtk_widget_get_mapped (GTK_WIDGET (window)))
    return G_SOURCE_REMOVE;
  /* libdbusmenu exports asynchronously. Check the actual object rather than
   * advertising a path which a fast panel could query before it exists. */
  g_dbus_connection_call (menu->connection, g_dbus_connection_get_unique_name (menu->connection), menu->path,
                          "org.freedesktop.DBus.Introspectable", "Introspect", NULL, G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE, 3000, menu->registration_cancellable,
                          export_checked, g_object_ref (window));
  return G_SOURCE_REMOVE;
}

static void
window_mapped (GtkWidget *window,
               WindowMenu *menu)
{
  if (!menu->registration_idle && !menu->display && !menu->registration_cancellable) {
    menu->registration_attempts = 0;
    menu->registration_cancellable = g_cancellable_new ();
    menu->registration_idle = g_idle_add_full (G_PRIORITY_LOW, register_surface_idle, g_object_ref (window), g_object_unref);
  }
}

static void
window_unrealized (GtkWidget *window,
                   WindowMenu *menu)
{
  g_clear_handle_id (&menu->registration_idle, g_source_remove);
  if (menu->registration_cancellable)
    g_cancellable_cancel (menu->registration_cancellable);
  g_clear_object (&menu->registration_cancellable);
  unregister_surface (menu);
}

static void
window_menu_free (gpointer data)
{
  WindowMenu *menu = data;

  g_clear_handle_id (&menu->registration_idle, g_source_remove);
  if (menu->registration_cancellable)
    g_cancellable_cancel (menu->registration_cancellable);
  g_clear_object (&menu->registration_cancellable);
  unregister_surface (menu);
  g_clear_object (&menu->server);
  g_clear_object (&menu->connection);
  g_free (menu->path);
  g_free (menu);
}
#endif

void
ephy_global_menu_attach (GtkApplicationWindow *window)
{
#if HAVE_DBUSMENU
  GtkApplication *application = gtk_window_get_application (GTK_WINDOW (window));
  GMenuModel *model;
  DbusmenuMenuitem *root;
  WindowMenu *menu;

  if (!application)
    return;
  model = gtk_application_get_menubar (application);
  if (!model || !g_application_get_is_registered (G_APPLICATION (application)) ||
      !g_application_get_dbus_connection (G_APPLICATION (application)) ||
      g_object_get_data (G_OBJECT (window), "savanna-dbusmenu"))
    return;
  menu = g_new0 (WindowMenu, 1);
  menu->connection = g_object_ref (g_application_get_dbus_connection (G_APPLICATION (application)));
  menu->path = g_strdup_printf ("%s/dbusmenu/%u", g_application_get_dbus_object_path (G_APPLICATION (application)),
                                gtk_application_window_get_id (window));
  root = dbusmenu_menuitem_new ();
  append_model (root, model, window);
  menu->server = dbusmenu_server_new (menu->path);
  dbusmenu_server_set_root (menu->server, root);
  g_object_unref (root);
  g_object_set_data_full (G_OBJECT (window), "savanna-dbusmenu", menu, window_menu_free);
  g_signal_connect (window, "map", G_CALLBACK (window_mapped), menu);
  g_signal_connect (window, "unrealize", G_CALLBACK (window_unrealized), menu);
  if (gtk_widget_get_mapped (GTK_WIDGET (window)))
    window_mapped (GTK_WIDGET (window), menu);
#endif
}

const char *
ephy_global_menu_get_dbusmenu_path (GtkApplicationWindow *window)
{
#if HAVE_DBUSMENU
  WindowMenu *menu = g_object_get_data (G_OBJECT (window), "savanna-dbusmenu");

  return menu ? menu->path : NULL;
#else
  return NULL;
#endif
}
