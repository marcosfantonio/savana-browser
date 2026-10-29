# Savana architecture and interface guide

Savana is a branded Epiphany-derived browser. Most browser internals keep the
upstream `Ephy*` names; the app identity and the user-visible browser executable
are Savana-specific. This guide records the project-specific UI behavior and
where to make changes without breaking persistence or desktop integration.

## Identity, executable, and install layout

- Meson target and installed browser command: `savana` (`src/meson.build`).
- Desktop launcher: `data/org.gnome.Epiphany.desktop.in.in`, whose visible name
  is Savana and whose `Exec` actions invoke `savana`.
- Application ID: `com.savana.Browser` in the root `meson.build`.
- Savana-private data, library and helper directories use `savana` rather
  than Epiphany's `epiphany` directory names.
- Search and web-app provider binaries are installed as
  `savana-search-provider` and `savana-webapp-provider`.
- The Savana web-app provider uses the D-Bus name/interface
  `com.savana.Browser.WebAppProvider`; do not re-use Epiphany's provider name.

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
- `src/ephy-main.c`: process-start graphics workarounds, enabled only by the
  user's “Fix para placas NVIDIA” preference in General → Web Content. The
  preference selects TextureMapper composition, shared-memory page-frame
  transport, disables WebKit's GL video sink and NVIDIA explicit sync, and
  excludes AV1 decoder plugins and `nvh264dec` from GStreamer selection.
  `nvh264dec` failed to instantiate in WebKitGTK 2.54.2's
  `ImageDecoderGStreamer` MP4-image-preview path; the unchecked null decoder
  crashed in `configureVideoDecoderForHarnessing`. A synthetic H.264 MP4 loaded
  through an HTML Image reproduced the crash independently of WhatsApp, and
  excluding only `nvh264dec` made the preview load successfully. H.264 falls
  back to the installed software decoder; other non-AV1 decoders are unchanged.
  The workaround is scoped to Savana; GTK/WebKit page acceleration remains
  enabled. It takes effect on restart. Do not turn off WebKit hardware
  acceleration globally.
  GTK must be initialized before constructing the shell: shell construction
  creates the WebKit context, whose acceleration probe needs a default display.
  In particular, `WEBKIT_DMABUF_RENDERER_FORCE_SHM=1` bypasses the normal DMABUF
  display setup and requires GTK initialization before WebKit context
  construction. SHM was verified to stop page-content flicker on the affected
  NVIDIA setup; disabling the GL video sink was verified to fix green H.264
  video output. This compatibility option is not automatically hardware
  detected; it is enabled by default and can be changed in General → Web
  Content. It takes effect on restart.
  `src/ephy-memory-guard.c` applies cgroup v2 memory and swap limits at startup
  when the default-on “Proteção contra falta de memória” preference is enabled.
  It only touches an otherwise-unlimited, dedicated Savana systemd user-app
  cgroup containing only the startup process; shared cgroups and existing
  limits are left unchanged. Limits are half of physical RAM (1.5–8 GiB) and a
  quarter of physical RAM (512 MiB–4 GiB) for swap. The limit converts a
  system-wide memory-pressure kill into a cgroup OOM where the WebKit process
  can be selected, but cannot guarantee the tab survives. The setting takes
  effect on restart.
- `src/ephy-shell.c`: application/window lifecycle, application actions, and
  the point where Savana's global menu and per-browser-window behavior are
  attached. It filters auxiliary windows where browser-only logic is required.
  Normal browser launches restore the previous session before opening the
  Start Page as the active tab, preserving restored tabs and windows.
  Launches without arguments (including subsequent launcher activations) open
  `about:overview`, independently of the custom home-URL preference. An active
  empty/start-page tab can be reused, but regular restored pages are not replaced. Explicit
  URLs/session files and non-browser application modes retain their own flow.
  Session serialization, saved groups and explicit session loading are unchanged.
  Automatic session resume skips saved `about:overview`/`about:newtab` tabs
  (including the `ephy-about:` variants), even when pinned, so only the fresh
  Start Page remains. It matches internal URLs, never titles or external
  homepages. Active indices account for skipped tabs; windows containing only
  old Start Pages are omitted. Explicit session imports retain their contents.
- `src/ephy-global-menu.c`: declares the global menu hierarchy and maps menu
  labels to application (`app.`) or window (`win.`) actions. Keep this menu
  intentional and distinct from the website's own page content.
- `src/ephy-dbus-menu.c`: exports the GTK menu model as per-window DBusMenu and
  associates it with KDE Plasma's Wayland surface or X11 window. It mirrors
  enabled/check state and invokes the corresponding GAction. A model item
  intentionally omitted from the global menu must not be reintroduced here.
- `src/resources/savana.css`: application chrome styling, transparency,
  sidebar and popup appearance. Light-mode chrome uses white outlines and soft
  shadows on toolbar button groups, the URL field and open tabs; these do not
  change dark-mode styling or system window-control artwork. Regular pages use
  solid surfaces with the corresponding light/dark palette;
  the start page retains the glass treatment. All browser popover outer surfaces are cleared
  here to avoid opaque rectangular shadows behind the rounded content. The
  bookmarks view receives `savana-bookmarks` from `ephy-window.c` so its
  pane can share the navigation sidebar surface. The preferences dialog is
  marked `savana-preferences` by `src/preferences/ephy-prefs-dialog.c`; its
  light palette uses a solid darker page surface, off-white navigation tabs,
  and explicit switch/checkbox contrast without changing dark mode or the
  dialog's dimmed parent-window backdrop. Its dark-mode marker follows the
  current `AdwStyleManager` setting.
  The sidebar toggle has a dedicated `savana-sidebar-toggle` CSS class so its
  pill border outranks generic borderless toolbar-button rules; its light-mode
  white outline and shadow match the neighboring navigation controls.
- GTK templates/Blueprints and their C controllers live under
  `src/resources/gtk/` and `src/`; change both sides consistently.
- `src/ephy-window-controls.c` reads Plasma's `kwinrc` decoration selection
  (`org.kde.kdecoration2`) on KDE desktops. It parses
  only that group's keys (other KConfig groups can use nested headers that
  GLib's INI parser does not accept). For Aurorae SVG themes it renders
  the close/minimize/maximize/restore sprite elements with librsvg and applies
  only their artwork to the existing GTK window controls. Header layout,
  accessibility, actions and system GTK button placement remain unchanged.
  Active, inactive, hover and pressed states and maximized restore artwork
  follow the theme. Missing sprite states use the active artwork. Themes are
  resolved under XDG data directories; unsupported native KDecoration plugins
  or incomplete themes retain the system GTK controls, not a simulated KWin
  theme. The selected theme is reloaded when `kwinrc` changes. Rendered 2x PNGs
  are cached in `$XDG_CACHE_HOME/savana/window-buttons/` using a SHA-256 of the
  source SVG, state and dimensions; deleting this cache is safe. Only button
  artwork uses a scoped CSS override above user CSS priority so existing GTK
  theme sprites do not mask the selected KWin artwork.

The GTK app menu is not itself sufficient for Plasma: DBusMenu exports the
actual hierarchical menu and the native registration ties that object to each
window. Read `docs/global-menu.md` for backend behavior, packaging dependencies
and diagnostics. Keep new actionable menu entries backed by real GActions.

## Optional Liquid Glass Toolbar

Appearance → Theme exposes “Liquid Glass Toolbar”, persisted by the main
`org.gnome.Epiphany` boolean `liquid-glass-toolbar` (default false). Changes
apply immediately to existing/new browser windows, independently of color
scheme. The window samples the first visible pixel row of the active WebView.
`src/ephy-toolbar-color.c` selects that row's dominant 4-bit RGB histogram bucket
and averages its original RGB values with alpha weighting; the rest of the
viewport never contributes. Transparent rows keep the page/palette fallback.
Both fullscreen boxes paint only the resulting solid RGBA behind their top bars;
the sidebar uses the same per-window CSS color and translucent glass surfaces.
Page layout, scroll position, fixed site headers and pointer coordinates remain
unchanged. No page elements are hidden under browser chrome, and no DOM/script
injection or KWin protocol is used.
`savana-liquid-glass` scopes translucent light/dark controls, URL field and
tab capsule in `savana.css`; native KWin control artwork remains unchanged.
The effect uses translucent highlights, not macOS optical refraction or a copy
of the website. A `GtkWidgetPaintable` observes invalidation only and is never
rendered into the chrome. Invalidations coalesce into asynchronous WebKit
visible-region shared-memory snapshots, at most two requests/second and one
in flight. Only memory textures may be sampled; GPU/DMA-BUF readback is never
attempted. Snapshot pixels are discarded immediately, never persisted. This
replaces the old mirrored/blurred render nodes, which caused a fatal GTK
“Failed to download … dmabuf texture” when the sidebar resized the WebView.
Disabling the option, switching tabs or closing a window cancels queued/in-flight
work; weak callbacks and request identity prevent stale colors or use-after-free.
Popovers, preferences and fullscreen autohide retain their layout/behavior.

## Sidebar, tab groups, and saved content

`src/ephy-savana-sidebar.c` owns Savana's sidebar-specific behavior. It
intentionally displays tab **groups**, not a duplicate list of every open tab.
Groups persist in `savana-tab-groups.ini` under the active browser profile;
group entries retain URLs and reopen in their own window so restoring a group
does not replace unrelated windows or tabs. Incognito mode must not write this
file. The former `savanna-tab-groups.ini` filename is imported on first load so
existing groups survive the branding change. Saved/reading content is separate
from tab-group data. Bookmark descriptions similarly read the previous
`savanna-bookmark-descriptions` cache path when the new cache entry is absent.
Device tabs use the existing Firefox Sync integration; do not invent
iCloud/device content.
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
`ephy_cache_dir()/savana-bookmark-descriptions/<SHA256(full URL)>`, atomically
replaced with mode 0600, excluded from Firefox Sync and never written incognito.
Missing descriptions remain hidden rather than showing invented summaries.
Start-page tiles embed cached WebKit favicons as PNG data URIs and refresh when
the asynchronous icon becomes available; a local globe replaces missing icons,
never a third-party favicon service. Both panel icon sizes share that cache.
`lib/ephy-favicon-helpers.c` builds the same filled icon tile for the start page
and the large sidebar icon (the small URL icon remains unchanged). It samples
the first visible pixels entering each row/column from all four sides, ignoring
transparent margins and preferring alpha >= 192 over antialiasing/shadows. An
alpha-weighted 4-bit/channel histogram selects the dominant exterior color,
not an average of the logo interior. Translucent-only icons retry at alpha 32;
fully transparent icons keep their fallback. The icon is centered, aspect-ratio
preserved, on a square of that color (100px on the start page, 48px in the
sidebar). Small favicons are never enlarged; larger images are downsampled
with the high-quality filter. Nearly monochromatic icons (95% alpha-weighted
visible pixels within 32 RGB levels of the exterior color) are rendered white
over their original color, preserving their alpha/silhouette. Near-white icons
use a dark glyph instead so they remain visible. Multicolor icons are unchanged.
Rounded corners (20% radius) are clipped into the actual tile alpha with
antialiased edges, not just styled on GtkImage. Tiles are cached per target size
on the source texture, not persisted separately, and update with favicon changes.
The bookmarks view and its nested toolbar/lists are transparent: only the parent
sidebar pane paints the glass, matching navigation on the start page and the
solid sidebar surface on regular pages.
The bookmarks pane uses a plain GTK box for its compact back/title/options
header (`src/resources/gtk/bookmarks-dialog.blp`), never a nested window header
bar: window controls and sidebar collapse belong only to the main toolbar.
Back returns to sidebar navigation; editing is available in the options menu.
Search sits directly below the header and the bookmark list scrolls within the
remaining pane height, without a second oversized title.
The pane fills the sidebar height. Its stack is the main child of a GTK overlay;
Extensions (a disabled placeholder) and Preferences float at the bottom over
the same surface, rather than shortening the bookmarks container. Scrolling
content reserves bottom space so its last row remains reachable above the tools.
Outside the start page, compact tab chrome keeps 6px of padding below the tabs;
only surrounding whitespace changes, not button or tab sizes.
The tab bar uses libadwaita's inverted layout to put its native close buttons
on the left; do not reparent private tab widgets to move them.
When enabled, the strip stays visible even with one tab and expands tabs across a single
rounded gray capsule, as in `startpage.png`. Inactive tabs are transparent;
the selected tab is an inset white pill in light mode (dark surfaces remain
dark). Native selection, close, reorder and overflow behavior remain owned by
AdwTabBar. The explicit Never visibility preference and tab-width preference
remain supported. Overview/new-tab pages have the localized tab title “Start Page”
(“Página Inicial” in Portuguese) and Savana's outlined star icon, overriding
internal document titles/favicons only for these pages.

## Downloads and popovers

`src/ephy-downloads-popover.c` controls download-list construction and popup
size. The popup is deliberately wider, height-bounded, and internally
scrollable: new rows should not grow it without limit. Global popover surface
styling is in `src/resources/savana.css`; keep the readable inner content
surface separate from the translucent outer shell.
Downloads are available from the toolbar and the existing menu/shortcut actions,
not the sidebar footer. Explicitly opening downloads temporarily reveals the
toolbar button, including when only previously opened downloads remain; closing
the popup restores the normal activity-based button visibility.
The toolbar download glyph is 18px (the button's hit area is unchanged), using
a thin arrow inside an inset circular progress outline, following `abas.png`.
The URL's site-menu default artwork is an outlined rectangle with two lines
below, matching that reference; its existing notification animations and menu
actions remain intact. The URL loading/stop button uses Savana's own plain X
symbolic asset so system icon themes cannot add a circle around it.
Completion keeps its normal inherited toolbar icon color, including during
the completion animation, and clears any temporary blue new-download highlight.
“Show in folder” opens a download's containing directory (including for an
in-progress download whose file is not yet present). Savana passes a directory
URI to the desktop portal's `OpenDirectory` operation, which cannot select a
file within it. A directory passed directly to this action is opened as-is.

## Tests and validation

- `tests/savana-ui-test.c` covers the app chrome, global menu model/actions,
  auxiliary windows/session behavior, and UI regression cases.
- `tests/meson.build` registers the focused test as “Savana interface test”.
- Run the focused UI test after changes to menu, sidebar, window, or popup code.
- Other test failures caused by missing external platform resources/plugins
  should be reported separately; do not hide them or modify unrelated tests to
  make the environment look healthy.
- Always compile and install via `meson install -C build --quiet` after source
  edits (see `AGENTS.md`).
