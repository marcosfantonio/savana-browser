# Savanna

**Savanna** is a Linux web browser based on GNOME Web (Epiphany) and WebKitGTK. It builds on Epiphany's browsing engine while providing its own interface, with tab groups, a sidebar, saved content, and KDE Plasma global menu support.

The installed application is called **`savanna`**. It has its own application ID (`com.savanna.Browser`) and does not install an executable named `epiphany`.

## Screenshots

![Savanna browser](screenshots/screen2.png)

![Savanna screenshot](screenshots/screen3.png)

![Savanna screenshot](screenshots/screen4.png)

![Savanna screenshot](screenshots/screen1.png)

![Savanna screenshot](screenshots/screen5.png)

![Savanna screenshot](screenshots/screen6.png)

![Savanna screenshot](screenshots/screen7.png)

![Savanna screenshot](screenshots/screen8.png)

## Easy Linux installation

### CachyOS and Arch Linux

Install the build tools and the dependencies required by the project (including
the separate GTK 4 portal backend and code-generation/documentation tools):

```sh
sudo pacman -S --needed base-devel git meson ninja pkgconf \
  gtk4 libadwaita webkitgtk-6.0 dbusmenu-glib wayland libx11 \
  libportal libportal-gtk4 libpwquality glib2-devel itstool blueprint-compiler
```

Clone, build, and install for your user (installation does not require `sudo`):

```sh
git clone https://github.com/marcosfantonio/savanna.git savanna-browser
cd savanna-browser
meson setup build --prefix="$HOME/.local" -Ddeveloper_mode=true
meson install -C build --quiet
```

`meson setup` configures the build and checks dependencies; `meson install`
compiles and installs it. The installation is per-user and does not require
`sudo`. In particular, Arch/CachyOS package `libportal-gtk4` separately from
`libportal`; both are needed. `glib2-devel` provides GLib code-generation tools,
while `itstool` and `blueprint-compiler` are needed for help and UI generation.
Meson lists the global-menu integrations enabled in its summary.

Launch the browser from a terminal:

```sh
~/.local/bin/savanna
```

The installed desktop file is
`~/.local/share/applications/com.savanna.Browser.desktop` and launches
`savanna` (not `epiphany`). If `savanna` is not found, either run
`~/.local/bin/savanna` or add `~/.local/bin` to your `PATH`.

### Other distributions

Install the equivalent development packages for GTK 4, libadwaita, WebKitGTK
6, libportal's GTK 4 backend, libpwquality, GLib, Meson/Ninja, Blueprint,
itstool, and the other libraries required by the project; package names vary
by distribution. Then use the same clone, setup, and installation commands
above. If Meson reports a missing dependency or program, install its
distribution-specific development package and rerun setup and installation:

```sh
meson setup build --prefix="$HOME/.local" -Ddeveloper_mode=true
meson install -C build --quiet
```

If configuration was interrupted and the existing `build` directory is left
in an unusable state, preserve anything you need from it before removing it;
then rerun the setup command above. A clean build directory is only needed when
Meson cannot reconfigure the existing one.

## Rebuild

From the repository directory, run:

```sh
meson install -C build --quiet
```

This recompiles changed files and installs the application again in `~/.local`. To run the interface tests:

```sh
meson test -C build 'Savanna interface test' --print-errorlogs
```

To verify the installed command, run `savanna --version` (or
`~/.local/bin/savanna --version` if `~/.local/bin` is not on your `PATH`).
Warnings about deprecated inherited `Gtk.TreeView` usage or individual
translation merge conflicts may appear during the build; they do not by
themselves mean compilation failed.

## Features

- Persistent tab groups in the sidebar.
- History, bookmarks, and saved content accessible from the interface.
- Device tab integration through Firefox Sync.
- Plasma global menu integration via DBusMenu on Wayland and X11/XWayland, when the global-menu panel is enabled.
- A height-limited downloads popover with its own scrolling.

See [`docs/global-menu.md`](docs/global-menu.md) for global menu compatibility and troubleshooting, and [`docs/architecture.md`](docs/architecture.md) for UI organization and guidance for future changes. Coding agent instructions are in [`AGENTS.md`](AGENTS.md).

## Development

Savanna is free software licensed under GPL-3.0-or-later. Most modules still use the upstream Epiphany `ephy-*` internal names. Changes to Savanna's interface and project-specific integrations should follow the architecture and behaviors documented above.
