# Instructions for coding agents

Savana is a separately branded browser based on GNOME Epiphany/WebKitGTK. Keep
upstream `ephy-*` symbol names where they are part of the inherited browser
architecture, but do not make the installed browser executable, application
launcher, or Savana-owned private data/library directories collide with the
system Epiphany installation.

## Before editing

- Read `docs/architecture.md` for ownership and UI behavior, especially before
  changing the sidebar, global menu, window actions, or persistence.
- Inspect the surrounding code and existing changes first. Preserve user edits.
- Follow the existing C/GTK/GLib conventions and keep the GTK4/libadwaita
  implementation; do not introduce GTK3-only menu integrations.

## Build and install

After **every source change**, build and install the configured local build:

```sh
meson install -C build --quiet
```

This runs Ninja as needed and installs to the prefix configured in `build`.
Report failures rather than claiming installation succeeded. Run relevant tests
as well; the focused interface test is:

```sh
meson test -C build 'Savana interface test' --print-errorlogs
```

The main installed executable and desktop launch command are `savana`, not
`epiphany`. Never add an installed `epiphany` alias or change the desktop Exec
back to `epiphany`. The existing build directory may retain historical target
files; the installed launcher is the authoritative name.

## Documentation discipline

When UI behavior or ownership changes, update `docs/architecture.md` in the
same change. Document non-obvious persistence formats and desktop protocols so
future maintainers can safely extend them.
