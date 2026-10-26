# Linux global menus

Savana exports both the native GTK `org.gtk.Menus` model and a per-window
`com.canonical.dbusmenu` tree. Exporting the GTK model alone is **not** sufficient
for KDE Plasma's global-menu widget.

## Desktop/session support

| Session/panel | How the window advertises its menu |
| --- | --- |
| Plasma Wayland | `org_kde_kwin_appmenu_manager`, with the D-Bus service and object path attached to the actual `wl_surface` |
| Plasma X11/XWayland | `_KDE_NET_WM_APPMENU_SERVICE_NAME` and `_KDE_NET_WM_APPMENU_OBJECT_PATH` window properties |
| Other X11 DBusMenu panels | `com.canonical.AppMenu.Registrar.RegisterWindow`; registration is repeated when the registrar restarts |
| GTK/GMenu consumers | The existing `GtkApplication` menubar and exported window actions |

Wayland support is discovered from compositor protocols, not distro or desktop
environment names. Other Wayland compositors must implement the KDE appmenu
protocol (or consume the native GTK menu); Wayland has no universal global-menu
registration protocol. This does not add a menu widget to a user's panel or
enable an extension on a desktop without global-menu support.

Each browser window has its own DBusMenu object. Actions target that window;
disabled actions, checkmarks and keyboard shortcuts are reflected in the menu.
Surface registration is recreated when a window is re-realized. Native GTK menus
remain available even when the desktop has no DBusMenu consumer.

## Build/package requirements

The `global_menu` Meson feature defaults to `enabled`, so missing exporter
dependencies fail configuration rather than silently shipping a nonworking
Plasma integration:

- `dbusmenu-glib-0.4` (GLib-only; no GTK 3 dependency).
- For GTK's Wayland backend: `wayland-client` and `wayland-scanner`.
- For GTK's X11 backend: Xlib (`x11`).

The small MIT-licensed KDE appmenu protocol definition is bundled in
`src/protocols/appmenu.xml`; installing plasma-wayland-protocols is not necessary.
Meson's **Global menu** summary lists which backends are built.

Typical development packages (names vary by distro release):

- Arch/CachyOS: `libdbusmenu-glib`, `wayland`, `libx11`.
- Debian/Ubuntu: `libdbusmenu-glib-dev`, `libwayland-dev`, `libwayland-bin`, `libx11-dev`.
- Fedora: `libdbusmenu-devel`, `wayland-devel`, `libX11-devel`.

Packagers intentionally building without DBusMenu support can use
`-Dglobal_menu=disabled`. This keeps native GTK menus, but does not provide the
Plasma/DBusMenu bridge. `auto` is also available for optional builds.

## Diagnostics

Use a freshly started binary after installing; an existing process retains the
old libraries and registrations. In Plasma, add/enable a compatible Global Menu
widget if one is not already present. No `GTK_MODULES=appmenu-gtk-module` setting
is required: that GTK 2/3 module does not provide this GTK 4 integration.

The exporter lives at the application's D-Bus object path followed by
`/dbusmenu/<window-id>`. For the default build and first window:

```sh
gdbus call --session --dest com.savana.Browser \
  --object-path /com/savana/Browser/dbusmenu/1 \
  --method com.canonical.dbusmenu.GetLayout 0 -1 '[]'
```

The layout should contain the seven top-level menus (File, Edit, View, History,
Bookmarks, Window and Help). The application name is supplied by the desktop
integration and is not repeated as a Savana menu. On Wayland,
`WAYLAND_DEBUG=client` shows `org_kde_kwin_appmenu.set_address` for the window.
On X11, `xprop` shows the two `_KDE_NET_WM_APPMENU_*` properties. D-Bus layout
export and native surface registration must **both** work; a successful
`org.gtk.Menus.Start` call alone does not establish Plasma compatibility.
