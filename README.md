# Savana

**Savana** is a Linux web browser based on GNOME Web (Epiphany) and WebKitGTK. It builds on Epiphany's browsing engine while providing its own interface, with tab groups, a sidebar, saved content, and KDE Plasma global menu support.

The installed application is called **`savana`**. It has its own application ID (`com.savana.Browser`) and does not install an executable named `epiphany`.

## Screenshots

![Savana browser](screenshots/screen2.png)

![Savana screenshot](screenshots/screen3.png)

![Savana screenshot](screenshots/screen4.png)

![Savana screenshot](screenshots/screen1.png)

![Savana screenshot](screenshots/screen5.png)

![Savana screenshot](screenshots/screen6.png)

![Savana screenshot](screenshots/screen7.png)

![Savana screenshot](screenshots/screen8.png)

## Easy Linux installation

### CachyOS and Arch Linux

Install the build tools and all dependencies required to build and run the
project, including the GTK 4 portal backend, code-generation/documentation
tools, and GStreamer plugins for media playback:

```sh
sudo pacman -S --needed \
  base-devel git meson ninja pkgconf \
  gtk4 libadwaita webkitgtk-6.0 \
  libdbusmenu-glib wayland libx11 \
  libportal libportal-gtk4 libpwquality glib2-devel \
  itstool blueprint-compiler \
  gst-plugins-good gst-plugin-pipewire
```

Clone, build, and install for your user (installation does not require `sudo`):

```sh
git clone https://github.com/marcosfantonio/savana-browser.git savana-browser
cd savana-browser
meson setup build --prefix="$HOME/.local" -Ddeveloper_mode=true
meson install -C build --quiet
```

`meson setup` configures the build and checks dependencies; `meson install`
compiles and installs it. The installation is per-user and does not require
`sudo`. In particular, Arch/CachyOS package `libportal-gtk4` separately from
`libportal`; both are needed. `glib2-devel` provides GLib code-generation tools,
while `itstool` and `blueprint-compiler` are needed for help and UI generation.
Meson lists the global-menu integrations enabled in its summary.

For media playback (including YouTube audio), `gst-plugins-good` provides the
`autoaudiosink` and `pulsesink` elements required for audio output;
`gst-plugin-pipewire` supports systems using PipeWire. These runtime packages
are included in the initial installation so users do not need to install
additional dependencies afterward. Without `gst-plugins-good`, WebKit may fail
while initializing media playback and the page process can terminate when
opening sites such as YouTube.

Launch the browser from a terminal:

```sh
~/.local/bin/savana
```

The installed desktop file is
`~/.local/share/applications/com.savana.Browser.desktop` and launches
`savana` (not `epiphany`). If `savana` is not found, either run
`~/.local/bin/savana` or add `~/.local/bin` to your `PATH`.

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
meson test -C build 'Savana interface test' --print-errorlogs
```

To verify the installed command, run `savana --version` (or
`~/.local/bin/savana --version` if `~/.local/bin` is not on your `PATH`).
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

Savana is free software licensed under GPL-3.0-or-later. Most modules still use the upstream Epiphany `ephy-*` internal names. Changes to Savana's interface and project-specific integrations should follow the architecture and behaviors documented above.
