# Savanna architecture and interface guide

Savanna is a branded Epiphany-derived browser. Most browser internals keep the
upstream `Ephy*` names; the app identity and the user-visible browser executable
are Savanna-specific. This guide records the project-specific UI behavior and
where to make changes without breaking persistence or desktop integration.

## Identity, executable, and install layout

- Meson target and installed browser command: `savanna` (`src/meson.build`).
- Desktop launcher: `data/org.gnome.Epiphany.desktop.in.in`, whose visible name
  is Savanna and whose `Exec` actions invoke `savanna`.
- Application ID: `com.savanna.Browser` in the root `meson.build`.
- Savanna-private data, library and helper directories use `savanna` rather
  than Epiphany's `epiphany` directory names.
- Search and web-app provider binaries are installed as
  `savanna-search-provider` and `savanna-webapp-provider`.
- The Savanna web-app provider uses the D-Bus name/interface
  `com.savanna.Browser.WebAppProvider`; do not re-use Epiphany's provider name.

Do not restore an `epiphany` executable target or launcher command: a local
install at a shared prefix must not replace the distribution's browser. Keep
the Meson build target, desktop Exec fields, and provider service Exec fields in
sync. Upstream compatibility identifiers (including many `Ephy*` C symbols,
resource names and the existing GSettings schema) remain inherited unless a
separate migration is designed; do not casually rename those identifiers.
The GSettings schema is currently inherited as `org.gnome.Epiphany`; changing
it safely requires a coordinated schema/data migration, not just a Meson path
change.

## Main UI ownership

- `src/ephy-window.c`: browser-window setup, action registration, toolbar and
  window-level behavior. Window actions generally use the `win.` namespace.
- `src/ephy-shell.c`: application/window lifecycle, application actions, and
  the point where Savanna's global menu and per-browser-window behavior are
  attached. It filters auxiliary windows where browser-only logic is required.
- `src/ephy-global-menu.c`: declares the global menu hierarchy and maps menu
  labels to application (`app.`) or window (`win.`) actions. Keep this menu
  intentional and distinct from the website's own page content.
- `src/ephy-dbus-menu.c`: exports the GTK menu model as per-window DBusMenu and
  associates it with KDE Plasma's Wayland surface or X11 window. It mirrors
  enabled/check state and invokes the corresponding GAction. A model item
  intentionally omitted from the global menu must not be reintroduced here.
- `src/resources/savanna.css`: application chrome styling, transparency,
  sidebar and popup appearance. All browser popover outer surfaces are cleared
  here to avoid opaque rectangular shadows behind the rounded content. The
  bookmarks view receives `savanna-bookmarks` from `ephy-window.c` so its
  pane can share the navigation sidebar surface.
- GTK templates/Blueprints and their C controllers live under
  `src/resources/gtk/` and `src/`; change both sides consistently.

The GTK app menu is not itself sufficient for Plasma: DBusMenu exports the
actual hierarchical menu and the native registration ties that object to each
window. Read `docs/global-menu.md` for backend behavior, packaging dependencies
and diagnostics. Keep new actionable menu entries backed by real GActions.

## Sidebar, tab groups, and saved content

`src/ephy-savanna-sidebar.c` owns Savanna's sidebar-specific behavior. It
intentionally displays tab **groups**, not a duplicate list of every open tab.
Groups persist in `savanna-tab-groups.ini` under the active browser profile;
group entries retain URLs and reopen in their own window so restoring a group
does not replace unrelated windows or tabs. Incognito mode must not write this
file. Saved/reading content is separate from tab-group data. Device tabs use
the existing Firefox Sync integration; do not invent iCloud/device content.
“Shared with You” remains an explicit empty state until link receiving exists.

When altering group persistence, preserve atomic/consistent writes, profile
scoping, incognito exclusion, and the rule that closing a group window does not
delete the saved group. History is linked into the sidebar from the existing
history implementation; avoid creating a second independent history store.
The Bookmarks button switches the left sidebar stack to the existing
`EphyBookmarksDialog` view; it must not create a second, trailing/right overlay.
Bookmark rows are implemented by `src/bookmarks/ephy-bookmark-row.c` and show
their 48px favicon, title, 16px favicon beside the URL, and a two-line site
description when supplied by the page. The row owns its explicit content and
editing controls rather than depending on libadwaita's private child ordering.
Descriptions are read from `meta[name=description]` or `og:description` only
from matching open/loaded bookmark pages, capped at 512 characters; opening the
sidebar does not fetch bookmarked pages. They are cached as UTF-8 files under
`ephy_cache_dir()/savanna-bookmark-descriptions/<SHA256(full URL)>`, atomically
replaced with mode 0600, excluded from Firefox Sync and never written incognito.
Missing descriptions remain hidden rather than showing invented summaries.
Start-page tiles embed cached WebKit favicons as PNG data URIs and refresh when
the asynchronous icon becomes available; a local globe replaces missing icons,
never a third-party favicon service. Both panel icon sizes share that cache.
The bookmarks view and its nested toolbar/lists are transparent: only the parent
sidebar pane paints the glass, matching navigation on the start page and the
solid sidebar surface on regular pages.
The bookmarks pane uses a plain GTK box for its compact back/title/options
header (`src/resources/gtk/bookmarks-dialog.blp`), never a nested window header
bar: window controls and sidebar collapse belong only to the main toolbar.
Back returns to sidebar navigation; editing is available in the options menu.
Search sits directly below the header and the bookmark list scrolls within the
remaining pane height, without a second oversized title.

## Downloads and popovers

`src/ephy-downloads-popover.c` controls download-list construction and popup
size. The popup is deliberately wider, height-bounded, and internally
scrollable: new rows should not grow it without limit. Global popover surface
styling is in `src/resources/savanna.css`; keep the readable inner content
surface separate from the translucent outer shell.

## Tests and validation

- `tests/savanna-ui-test.c` covers the app chrome, global menu model/actions,
  auxiliary windows/session behavior, and UI regression cases.
- `tests/meson.build` registers the focused test as “Savanna interface test”.
- Run the focused UI test after changes to menu, sidebar, window, or popup code.
- Other test failures caused by missing external platform resources/plugins
  should be reported separately; do not hide them or modify unrelated tests to
  make the environment look healthy.
- Always compile and install via `meson install -C build --quiet` after source
  edits (see `AGENTS.md`).
