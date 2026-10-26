#include "config.h"

#include <gtk/gtk.h>

#include "ephy-bookmark.h"
#include "ephy-bookmark-row.h"
#include "ephy-bookmarks-dialog.h"
#include "ephy-bookmarks-manager.h"
#include "ephy-embed-container.h"
#include "ephy-embed.h"
#include "ephy-downloads-manager.h"
#include "ephy-download.h"
#include "ephy-file-helpers.h"
#include "ephy-header-bar.h"
#include "ephy-history-service.h"
#include "ephy-fullscreen-box.h"
#include "ephy-global-menu.h"
#include "ephy-savanna-sidebar.h"
#include "ephy-location-entry.h"
#include "ephy-settings.h"
#include "ephy-shell.h"
#include "ephy-session.h"
#include "ephy-tab-view.h"
#include "ephy-window.h"
#include "prefs-appearance-page.h"

#if HAVE_DBUSMENU
typedef struct {
  GMainLoop *loop;
  GVariant *reply;
  GError *error;
} MenuCall;

static void
menu_call_finished (GObject *source,
                    GAsyncResult *result,
                    gpointer data)
{
  MenuCall *call = data;

  call->reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &call->error);
  g_main_loop_quit (call->loop);
}

static GVariant *
call_dbusmenu (GtkApplicationWindow *window,
               const char *method,
               GVariant *parameters)
{
  GtkApplication *application = gtk_window_get_application (GTK_WINDOW (window));
  GDBusConnection *connection = g_application_get_dbus_connection (G_APPLICATION (application));
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GVariant) arguments = g_variant_ref_sink (parameters);
  MenuCall call = { .loop = loop };
  const char *path = ephy_global_menu_get_dbusmenu_path (window);

  g_assert_nonnull (path);
  for (guint attempt = 0; attempt < 100; attempt++) {
    g_dbus_connection_call (connection, g_dbus_connection_get_unique_name (connection), path,
                            "com.canonical.dbusmenu", method, arguments, NULL,
                            G_DBUS_CALL_FLAGS_NONE, 5000, NULL, menu_call_finished, &call);
    g_main_loop_run (loop);
    if (!g_error_matches (call.error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) ||
        !g_str_equal (method, "GetLayout") || attempt == 99)
      break;
    /* The server registers asynchronously when a new window is constructed. */
    g_clear_error (&call.error);
    g_timeout_add_once (25, (GSourceOnceFunc)g_main_loop_quit, loop);
    g_main_loop_run (loop);
  }
  g_assert_no_error (call.error);
  g_assert_nonnull (call.reply);
  return call.reply;
}

static GVariant *
find_menu_layout (GVariant *layout,
                  const char *label)
{
  g_autoptr (GVariant) properties = g_variant_get_child_value (layout, 1);
  g_autoptr (GVariant) children = g_variant_get_child_value (layout, 2);
  const char *item_label = NULL;

  if (g_variant_lookup (properties, "label", "&s", &item_label) && g_str_equal (item_label, label))
    return g_variant_ref (layout);
  for (gsize i = 0; i < g_variant_n_children (children); i++) {
    g_autoptr (GVariant) child = g_variant_get_child_value (children, i);
    g_autoptr (GVariant) unboxed = g_variant_get_variant (child);
    GVariant *found = find_menu_layout (unboxed, label);

    if (found)
      return found;
  }
  return NULL;
}

static void
assert_dbusmenu (GtkApplicationWindow *window,
                 AdwOverlaySplitView *split)
{
  g_autoptr (GVariant) reply = call_dbusmenu (window, "GetLayout", g_variant_new ("(ii@as)", 0, -1, g_variant_new_strv (NULL, 0)));
  g_autoptr (GVariant) root = g_variant_get_child_value (reply, 1);
  g_autoptr (GVariant) children = g_variant_get_child_value (root, 2);
  g_autoptr (GVariant) item = find_menu_layout (root, "Show Sidebar");
  g_autoptr (GVariant) find_item = find_menu_layout (root, "Find…");
  g_autoptr (GVariant) search_item = find_menu_layout (root, "Pesquisar");
  g_autoptr (GVariant) id = NULL;
  g_autoptr (GVariant) event = NULL;
  GSimpleAction *action = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (window), "toggle-sidebar"));
  gboolean initial = adw_overlay_split_view_get_show_sidebar (split);
  gint32 item_id;

  g_assert_cmpuint (g_variant_n_children (children), ==, 7);
  g_assert_null (find_item);
  g_assert_null (search_item);
  g_assert_nonnull (item);
  id = g_variant_get_child_value (item, 0);
  item_id = g_variant_get_int32 (id);
  event = call_dbusmenu (window, "Event", g_variant_new ("(isvu)", item_id, "clicked", g_variant_new_int32 (0), 0));
  g_assert_cmpint (adw_overlay_split_view_get_show_sidebar (split), ==, !initial);
  g_clear_pointer (&event, g_variant_unref);
  event = call_dbusmenu (window, "Event", g_variant_new ("(isvu)", item_id, "clicked", g_variant_new_int32 (0), 0));
  g_assert_cmpint (adw_overlay_split_view_get_show_sidebar (split), ==, initial);

  g_simple_action_set_enabled (action, FALSE);
  g_clear_pointer (&reply, g_variant_unref);
  reply = call_dbusmenu (window, "GetLayout", g_variant_new ("(ii@as)", item_id, 0, g_variant_new_strv (NULL, 0)));
  {
    g_autoptr (GVariant) disabled = g_variant_get_child_value (reply, 1);
    g_autoptr (GVariant) properties = g_variant_get_child_value (disabled, 1);
    gboolean enabled = TRUE;

    g_assert_true (g_variant_lookup (properties, "enabled", "b", &enabled));
    g_assert_false (enabled);
  }
  g_clear_pointer (&event, g_variant_unref);
  event = call_dbusmenu (window, "Event", g_variant_new ("(isvu)", item_id, "clicked", g_variant_new_int32 (0), 0));
  g_assert_cmpint (adw_overlay_split_view_get_show_sidebar (split), ==, initial);
  g_simple_action_set_enabled (action, TRUE);
}
#endif

static GskRenderNode *
background_node (GtkWidget *widget)
{
  GtkSnapshot *snapshot = gtk_snapshot_new ();

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_snapshot_render_background (snapshot, gtk_widget_get_style_context (widget), 0, 0, 100, 100);
  G_GNUC_END_IGNORE_DEPRECATIONS
  return gtk_snapshot_free_to_node (snapshot);
}

static GtkWidget *
find_sidebar_button (GtkWidget *widget,
                     const char *name)
{
  if (GTK_IS_BUTTON (widget) &&
      (g_strcmp0 (gtk_button_get_label (GTK_BUTTON (widget)), name) == 0 ||
       g_strcmp0 (gtk_widget_get_tooltip_text (widget), name) == 0 ||
       g_strcmp0 (g_object_get_data (G_OBJECT (widget), "collection"), name) == 0))
    return widget;
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child)) {
    GtkWidget *found = find_sidebar_button (child, name);
    if (found)
      return found;
  }
  return NULL;
}

static GskRenderNode *
find_background_color (GskRenderNode *node)
{
  switch (gsk_render_node_get_node_type (node)) {
    case GSK_COLOR_NODE:
      return node;
    case GSK_ROUNDED_CLIP_NODE:
      return find_background_color (gsk_rounded_clip_node_get_child (node));
    case GSK_CLIP_NODE:
      return find_background_color (gsk_clip_node_get_child (node));
    case GSK_CONTAINER_NODE:
      for (guint i = 0; i < gsk_container_node_get_n_children (node); i++) {
        GskRenderNode *color = find_background_color (gsk_container_node_get_child (node, i));
        if (color)
          return color;
      }
      return NULL;
    default:
      return NULL;
  }
}

static GtkWidget *
find_widget_with_class (GtkWidget  *widget,
                        const char *css_class)
{
  if (gtk_widget_has_css_class (widget, css_class))
    return widget;
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child)) {
    GtkWidget *found = find_widget_with_class (child, css_class);

    if (found)
      return found;
  }
  return NULL;
}

static void
assert_bookmarks_chrome (GtkWidget *widget)
{
  g_assert_false (ADW_IS_HEADER_BAR (widget));
  g_assert_false (GTK_IS_WINDOW_CONTROLS (widget));
  g_assert_false (gtk_widget_has_css_class (widget, "title-1"));
  if (GTK_IS_LIST_BOX (widget) || GTK_IS_SCROLLED_WINDOW (widget) ||
      gtk_widget_has_css_class (widget, "savanna-bookmarks") ||
      gtk_widget_has_css_class (widget, "savanna-bookmark-row")) {
    g_autoptr (GskRenderNode) background = background_node (widget);

    g_assert_null (background);
  }
  if (GTK_IS_IMAGE (widget)) {
    g_assert_cmpstr (gtk_image_get_icon_name (GTK_IMAGE (widget)), !=, "view-sidebar-end-symbolic");
    g_assert_cmpstr (gtk_image_get_icon_name (GTK_IMAGE (widget)), !=, "view-sidebar-start-symbolic");
  }
  for (GtkWidget *child = gtk_widget_get_first_child (widget); child; child = gtk_widget_get_next_sibling (child))
    assert_bookmarks_chrome (child);
}

static void
assert_glass_popover (GtkWidget *popover)
{
  GtkWidget *contents = gtk_widget_get_first_child (popover);
  g_autoptr (GskRenderNode) outer = background_node (popover);
  g_autoptr (GskRenderNode) inner = NULL;
  GskRenderNode *color;

  g_assert_null (outer);
  while (contents && g_strcmp0 (gtk_widget_get_css_name (contents), "contents") != 0)
    contents = gtk_widget_get_next_sibling (contents);
  g_assert_nonnull (contents);
  inner = background_node (contents);
  g_assert_nonnull (inner);
  color = find_background_color (inner);
  g_assert_nonnull (color);
  g_assert_cmpfloat (gsk_color_node_get_color (color)->alpha, >, 0.0);
  g_assert_cmpfloat (gsk_color_node_get_color (color)->alpha, <, 1.0);
}

static void
assert_chrome_transparency (GtkWidget *header,
                            gboolean solid)
{
  GtkWidget *native_header = GTK_WIDGET (gtk_widget_get_template_child (header, EPHY_TYPE_HEADER_BAR, "header_bar"));
  GtkWidget *top_bar = native_header;
  g_autoptr (GskRenderNode) node = background_node (native_header);

  /* The palette provider must not paint an opaque layer over the glass. */
  g_assert_null (node);
  while (top_bar && !gtk_widget_has_css_class (top_bar, "top-bar"))
    top_bar = gtk_widget_get_parent (top_bar);
  g_assert_nonnull (top_bar);
  node = background_node (top_bar);
  g_assert_nonnull (node);
  g_assert_cmpint (gsk_render_node_get_node_type (node), ==, GSK_COLOR_NODE);
  if (solid)
    g_assert_cmpfloat (gsk_color_node_get_color (node)->alpha, ==, 1.0);
  else
    g_assert_cmpfloat (gsk_color_node_get_color (node)->alpha, <, 1.0);

  {
    GtkWidget *window = GTK_WIDGET (gtk_widget_get_root (header));
    AdwOverlaySplitView *split = ADW_OVERLAY_SPLIT_VIEW (gtk_widget_get_template_child (window, EPHY_TYPE_WINDOW, "navigation_split_view"));
    GtkWidget *sidebar = adw_overlay_split_view_get_sidebar (split);
    GtkWidget *tools = gtk_widget_get_last_child (sidebar);
    GtkWidget *extensions = gtk_widget_get_first_child (tools);
    GtkWidget *preferences = gtk_widget_get_next_sibling (extensions);
    GtkWidget *entry = GTK_WIDGET (ephy_header_bar_get_title_widget (EPHY_HEADER_BAR (header)));
    GtkWidget *suggestions = GTK_WIDGET (gtk_widget_get_template_child (entry, EPHY_TYPE_LOCATION_ENTRY, "suggestions_popover"));
    g_autoptr (GskRenderNode) sidebar_background = background_node (sidebar);

    g_assert_nonnull (sidebar_background);
    g_assert_cmpint (gsk_render_node_get_node_type (sidebar_background), ==, GSK_COLOR_NODE);
    g_assert_true (gdk_rgba_equal (gsk_color_node_get_color (node), gsk_color_node_get_color (sidebar_background)));
    g_assert_true (GTK_IS_OVERLAY (sidebar));
    g_assert_true (gtk_overlay_get_child (GTK_OVERLAY (sidebar)) == g_object_get_data (G_OBJECT (window), "savanna-sidebar-stack"));
    g_assert_cmpint (gtk_widget_get_valign (tools), ==, GTK_ALIGN_END);
    g_assert_true (GTK_IS_BUTTON (extensions));
    g_assert_cmpstr (gtk_button_get_icon_name (GTK_BUTTON (extensions)), ==, "application-x-addon-symbolic");
    g_assert_false (gtk_widget_get_sensitive (extensions));
    g_assert_null (find_sidebar_button (sidebar, "Downloads"));
    g_assert_cmpstr (gtk_actionable_get_action_name (GTK_ACTIONABLE (preferences)), ==, "app.preferences");
    g_assert_null (gtk_widget_get_next_sibling (preferences));
    assert_glass_popover (suggestions);
  }
}

static void
session_save_wait_cb (gpointer data)
{
  g_main_loop_quit (data);
}

static void
javascript_checked_cb (GObject *source,
                        GAsyncResult *result,
                        gpointer data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (JSCValue) value = webkit_web_view_evaluate_javascript_finish (WEBKIT_WEB_VIEW (source), result, &error);
  g_autofree char *text = NULL;

  g_assert_no_error (error);
  text = jsc_value_to_string (value);
  g_assert_cmpstr (text, ==, "PASS");
  g_main_loop_quit (data);
}

static void
page_loaded_cb (WebKitWebView *view,
                 WebKitLoadEvent event,
                 gpointer data)
{
  const char *script =
    "(() => {"
    "const links = [...document.querySelectorAll('.favorite')];"
    "if (!links.some(a => a.href === 'https://savanna.test/' && a.textContent.includes('<Real Bookmark>'))) return 'bookmarks';"
    "if (document.querySelector('.favorites script')) return 'escaping';"
    "const icon = document.querySelector('.favorite .favorite-icon');"
    "if (!icon || !icon.complete || !icon.naturalWidth) return 'favorite icon';"
    "if (document.querySelector('.user-favorite').textContent.trim()) return 'letter tile';"
    "if (document.documentElement.dataset.theme !== 'dark') return 'theme';"
    "if (!getComputedStyle(document.body).backgroundImage.includes('savanna-wallpaper-dark.svg')) return 'dark wallpaper';"
    "if (!document.querySelector('.start-logo').src.endsWith('logo.svg')) return 'logo';"
    "if (document.querySelector('.suggestion-card')) return 'fake suggestions';"
    "if (document.querySelector('.overview-item[href]')) return 'fake history';"
    "return 'PASS';"
    "})()";

  if (event != WEBKIT_LOAD_FINISHED)
    return;
  g_signal_handlers_disconnect_by_func (view, page_loaded_cb, data);
  webkit_web_view_evaluate_javascript (view, script, -1, NULL, NULL, NULL, javascript_checked_cb, data);
}

static void
compact_address_cb (EphyWebView *view,
                     GParamSpec *pspec,
                     gpointer data)
{
  if (g_strcmp0 (ephy_web_view_get_address (view), "https://compact.test/") == 0) {
    g_signal_handlers_disconnect_by_func (view, compact_address_cb, data);
    g_main_loop_quit (data);
  }
}

static void
history_visit_added_cb (EphyHistoryService *service,
                        GAsyncResult *result,
                        gpointer data)
{
  g_autoptr (GError) error = NULL;

  g_assert_true (ephy_history_service_add_visit_finish (service, result, &error));
  g_assert_no_error (error);
  g_main_loop_quit (data);
}

static void
populated_page_loaded_cb (WebKitWebView *view,
                           WebKitLoadEvent event,
                           gpointer data)
{
  const char *script =
    "(() => {"
    "if (!document.querySelector('.user-suggestion[href=\"http://127.0.0.1:9/visited\"]')) return 'suggestions';"
    "if (!document.querySelector('.overview-item[href=\"http://127.0.0.1:9/visited\"]')) return 'history';"
    "if (document.querySelectorAll('.favorite[href=\"https://savanna.test/\"]').length !== 1) return 'bookmarks';"
    "return 'PASS';"
    "})()";

  if (event != WEBKIT_LOAD_FINISHED)
    return;
  g_signal_handlers_disconnect_by_func (view, populated_page_loaded_cb, data);
  webkit_web_view_evaluate_javascript (view, script, -1, NULL, NULL, NULL, javascript_checked_cb, data);
}

static void
preview_loaded_cb (WebKitWebView *view,
                    WebKitLoadEvent event,
                    EphyBookmark *bookmark)
{
  if (event == WEBKIT_LOAD_FINISHED)
    ephy_bookmark_update_from_view (bookmark, view);
}

static void
preview_description_cb (EphyBookmark *bookmark,
                        GParamSpec *pspec,
                        GMainLoop *loop)
{
  g_main_loop_quit (loop);
}

static void
test_bookmark_preview (void)
{
  g_autoptr (EphyBookmark) bookmark = ephy_bookmark_new ("https://preview.test/", "Preview", NULL, "preview");
  g_autoptr (EphyBookmark) restored = NULL;
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GtkWidget) view = g_object_ref_sink (ephy_web_view_new ());
  g_autoptr (GtkWidget) row = g_object_ref_sink (ephy_bookmark_row_new (bookmark));
  GtkWidget *large = GTK_WIDGET (gtk_widget_get_template_child (row, EPHY_TYPE_BOOKMARK_ROW, "favicon_image"));
  GtkWidget *small = GTK_WIDGET (gtk_widget_get_template_child (row, EPHY_TYPE_BOOKMARK_ROW, "url_favicon_image"));
  GtkWidget *url = GTK_WIDGET (gtk_widget_get_template_child (row, EPHY_TYPE_BOOKMARK_ROW, "url_label"));
  GtkWidget *description = GTK_WIDGET (gtk_widget_get_template_child (row, EPHY_TYPE_BOOKMARK_ROW, "description_label"));
  GtkWidget *controls = GTK_WIDGET (gtk_widget_get_template_child (row, EPHY_TYPE_BOOKMARK_ROW, "edit_controls"));

  g_assert_cmpint (gtk_image_get_pixel_size (GTK_IMAGE (large)), ==, 48);
  g_assert_cmpint (gtk_image_get_pixel_size (GTK_IMAGE (small)), ==, 16);
  g_assert_nonnull (gtk_image_get_gicon (GTK_IMAGE (large)));
  g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (url)), ==, "preview.test/");
  ephy_bookmark_row_set_is_editing (EPHY_BOOKMARK_ROW (row), FALSE);
  g_assert_false (gtk_widget_get_visible (controls));
  g_assert_true (gtk_widget_get_visible (large));
  g_signal_connect (bookmark, "notify::description", G_CALLBACK (preview_description_cb), loop);
  g_signal_connect (view, "load-changed", G_CALLBACK (preview_loaded_cb), bookmark);
  webkit_web_view_load_html (WEBKIT_WEB_VIEW (view),
                             "<meta name='description' content='A &lt;real&gt; site summary.'><title>Preview</title>",
                             "https://preview.test/");
  g_main_loop_run (loop);
  g_assert_cmpstr (ephy_bookmark_get_description (bookmark), ==, "A <real> site summary.");
  g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (description)), ==, "A <real> site summary.");
  g_assert_true (gtk_widget_get_visible (description));
  restored = ephy_bookmark_new ("https://preview.test/", "Preview", NULL, "restored");
  g_assert_cmpstr (ephy_bookmark_get_description (restored), ==, "A <real> site summary.");
  g_signal_handlers_disconnect_by_data (bookmark, loop);
  g_signal_handlers_disconnect_by_data (view, bookmark);
}

static void
test_interface (void)
{
  EphyShell *shell = ephy_shell_get_default ();
  EphyWindow *window;
  EphyEmbed *embed;
  AdwOverlaySplitView *split;
  AdwTabView *tabs;
  EphyBookmarksManager *manager = ephy_shell_get_bookmarks_manager (shell);
  g_autoptr (EphyBookmark) bookmark = ephy_bookmark_new ("https://savanna.test/", "<Real Bookmark>", NULL, "savanna-test");
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  EphyDownloadsManager *downloads;
  g_autoptr (EphyDownload) download = NULL;
  GtkWidget *header;
  EphyActionBarEnd *end;
  GtkWidget *revealer;
  GtkWidget *appearance;
  AdwComboRow *theme_row;
  GdkRGBA header_color;
  g_autoptr (EphyHistoryPageVisit) visit = NULL;
  int home_height;
  int page_height;
  int download_width;
  int home_control_height;
  int page_control_height;
  GtkWidget *share_button;
  GtkWidget *downloads_icon;
  GtkWidget *dialog;
  GtkWidget *url_button;
  int home_url_height;
  int page_url_height;
  GdkRGBA palette_color;
  g_autofree char *download_path = g_build_filename (g_get_tmp_dir (), "savanna-completed-test", NULL);

  g_settings_set_uint (EPHY_SETTINGS_MAIN, "browser-color-scheme", 1);
  g_assert_false (adw_style_manager_get_dark (adw_style_manager_get_default ()));
  g_settings_set_uint (EPHY_SETTINGS_MAIN, "browser-color-scheme", 2);
  g_assert_true (adw_style_manager_get_dark (adw_style_manager_get_default ()));
  appearance = g_object_ref_sink (g_object_new (EPHY_TYPE_PREFS_APPEARANCE_PAGE, NULL));
  theme_row = ADW_COMBO_ROW (gtk_widget_get_template_child (appearance, EPHY_TYPE_PREFS_APPEARANCE_PAGE, "browser_color_scheme"));
  g_assert_cmpuint (adw_combo_row_get_selected (theme_row), ==, 2);
  adw_combo_row_set_selected (theme_row, 1);
  g_assert_false (adw_style_manager_get_dark (adw_style_manager_get_default ()));
  adw_combo_row_set_selected (theme_row, 2);
  g_assert_true (adw_style_manager_get_dark (adw_style_manager_get_default ()));
  g_object_unref (appearance);

  ephy_bookmarks_manager_add_bookmark (manager, bookmark);
  window = ephy_window_new ();
  g_assert_nonnull (gtk_application_get_menubar (GTK_APPLICATION (shell)));
  g_assert_cmpint (g_menu_model_get_n_items (gtk_application_get_menubar (GTK_APPLICATION (shell))), ==, 7);
  g_assert_true (g_action_group_has_action (G_ACTION_GROUP (window), "new-tab"));
  g_assert_true (g_action_group_has_action (G_ACTION_GROUP (window), "global-tab-close"));
  g_assert_true (g_action_group_has_action (G_ACTION_GROUP (window), "global-toolbar-reload"));
  dialog = gtk_window_new ();
  gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (shell));
  g_assert_true (gtk_widget_has_css_class (dialog, "savanna-surface"));
  g_settings_set_uint (EPHY_SETTINGS_MAIN, "browser-color-scheme", 1);
  gtk_widget_get_color (dialog, &palette_color);
  g_assert_cmpfloat (palette_color.red, <, 0.4);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_assert_true (gtk_style_context_lookup_color (gtk_widget_get_style_context (dialog), "window_bg_color", &palette_color));
  G_GNUC_END_IGNORE_DEPRECATIONS
  g_assert_cmpfloat (palette_color.red, >, 0.8);
  g_settings_set_uint (EPHY_SETTINGS_MAIN, "browser-color-scheme", 2);
  gtk_widget_get_color (dialog, &palette_color);
  g_assert_cmpfloat (palette_color.red, >, 0.6);
  gtk_window_destroy (GTK_WINDOW (dialog));
  header = ephy_window_get_header_bar (window);
  gtk_widget_get_color (GTK_WIDGET (gtk_widget_get_template_child (header, EPHY_TYPE_HEADER_BAR, "header_bar")), &header_color);
  g_assert_cmpfloat (header_color.red, >, 0.6);
  embed = ephy_shell_new_tab_full (shell, NULL, NULL, window, NULL, EPHY_NEW_TAB_DONT_SHOW_WINDOW);
  split = ADW_OVERLAY_SPLIT_VIEW (gtk_widget_get_template_child (GTK_WIDGET (window), EPHY_TYPE_WINDOW, "navigation_split_view"));
  g_assert_true (gtk_widget_is_ancestor (GTK_WIDGET (split),
                  GTK_WIDGET (gtk_widget_get_template_child (GTK_WIDGET (window), EPHY_TYPE_WINDOW, "fullscreen_box"))));
  g_assert_false (gtk_widget_is_ancestor (header, GTK_WIDGET (split)));
  /* The sidebar starts below the header, alongside (not below) the tabs. */
  g_assert_true (EPHY_IS_FULLSCREEN_BOX (adw_overlay_split_view_get_content (split)));
  g_assert_true (gtk_widget_is_ancestor (GTK_WIDGET (embed), adw_overlay_split_view_get_content (split)));
  g_assert_nonnull (adw_overlay_split_view_get_sidebar (split));
  g_assert_false (adw_overlay_split_view_get_show_sidebar (split));
  {
    GtkWidget *history = find_sidebar_button (adw_overlay_split_view_get_sidebar (split), "History");
    g_assert_nonnull (history);
    g_assert_cmpstr (gtk_actionable_get_action_name (GTK_ACTIONABLE (history)), ==, "app.history");
    g_assert_cmpstr (gtk_actionable_get_action_name (GTK_ACTIONABLE (gtk_widget_get_prev_sibling (history))), ==, "win.bookmarks");
    g_action_group_activate_action (G_ACTION_GROUP (window), "toggle-sidebar", NULL);
    g_assert_true (adw_overlay_split_view_get_show_sidebar (split));
    g_action_group_activate_action (G_ACTION_GROUP (window), "toggle-sidebar", NULL);
    g_assert_false (adw_overlay_split_view_get_show_sidebar (split));
  }
  gtk_widget_activate_action (GTK_WIDGET (window), "win.toggle-sidebar", NULL);
  g_assert_true (adw_overlay_split_view_get_show_sidebar (split));
  gtk_widget_activate_action (GTK_WIDGET (window), "win.toggle-sidebar", NULL);
  g_assert_false (adw_overlay_split_view_get_show_sidebar (split));
  g_assert_null (gtk_widget_get_template_child (GTK_WIDGET (window), EPHY_TYPE_WINDOW, "overlay_split_view"));
  g_action_group_activate_action (G_ACTION_GROUP (window), "bookmarks", NULL);
  g_assert_true (adw_overlay_split_view_get_show_sidebar (split));
  g_assert_cmpstr (gtk_stack_get_visible_child_name (GTK_STACK (
    g_object_get_data (G_OBJECT (window), "savanna-sidebar-stack"))), ==, "bookmarks");
  {
    GtkStack *stack = g_object_get_data (G_OBJECT (window), "savanna-sidebar-stack");
    GtkWidget *pane = gtk_stack_get_visible_child (stack);
    GtkWidget *back = find_sidebar_button (pane, "Back");
    GtkWidget *done = GTK_WIDGET (gtk_widget_get_template_child (pane, EPHY_TYPE_BOOKMARKS_DIALOG, "done_button"));

    assert_bookmarks_chrome (pane);
    g_assert_nonnull (back);
    g_assert_false (gtk_widget_get_visible (done));
    g_assert_true (gtk_widget_activate_action (pane, "dialog.edit", NULL));
    g_assert_true (gtk_widget_get_visible (done));
    g_signal_emit_by_name (done, "clicked");
    g_assert_false (gtk_widget_get_visible (done));
    g_signal_emit_by_name (back, "clicked");
    g_assert_cmpstr (gtk_stack_get_visible_child_name (stack), ==, "navigation");
    g_assert_true (adw_overlay_split_view_get_show_sidebar (split));
    g_action_group_activate_action (G_ACTION_GROUP (window), "bookmarks", NULL);
  }
  g_action_group_activate_action (G_ACTION_GROUP (window), "bookmarks", NULL);
  g_assert_false (adw_overlay_split_view_get_show_sidebar (split));

  tabs = ephy_tab_view_get_tab_view (ephy_window_get_tab_view (window));
  ephy_shell_new_tab_full (shell, NULL, NULL, window, NULL, EPHY_NEW_TAB_DONT_SHOW_WINDOW);
  g_assert_cmpint (adw_tab_view_get_n_pages (tabs), ==, 2);
  adw_tab_view_set_selected_page (tabs, adw_tab_view_get_nth_page (tabs, 0));

  g_signal_connect (ephy_embed_get_web_view (embed), "load-changed", G_CALLBACK (page_loaded_cb), loop);
  ephy_web_view_load_url (ephy_embed_get_web_view (embed), "ephy-about:overview");
  g_main_loop_run (loop);
  g_assert_false (gtk_widget_has_css_class (GTK_WIDGET (window), "savanna-compact"));
  assert_chrome_transparency (header, FALSE);
#if HAVE_DBUSMENU
  assert_dbusmenu (GTK_APPLICATION_WINDOW (window), split);
#endif
  g_settings_set_uint (EPHY_SETTINGS_MAIN, "browser-color-scheme", 1);
  assert_chrome_transparency (header, FALSE);
  g_settings_set_uint (EPHY_SETTINGS_MAIN, "browser-color-scheme", 2);
  gtk_widget_measure (header, GTK_ORIENTATION_VERTICAL, -1, &home_height, NULL, NULL, NULL);
  share_button = GTK_WIDGET (gtk_widget_get_template_child (header, EPHY_TYPE_HEADER_BAR, "share_button"));
  gtk_widget_measure (share_button, GTK_ORIENTATION_VERTICAL, -1, &home_control_height, NULL, NULL, NULL);
  url_button = GTK_WIDGET (gtk_widget_get_template_child (GTK_WIDGET (ephy_header_bar_get_title_widget (EPHY_HEADER_BAR (header))), EPHY_TYPE_LOCATION_ENTRY, "url_button"));
  gtk_widget_measure (url_button, GTK_ORIENTATION_VERTICAL, -1, &home_url_height, NULL, NULL, NULL);

  /* Only header whitespace collapses; controls retain their dimensions. */
  g_signal_connect (ephy_embed_get_web_view (embed), "notify::address", G_CALLBACK (compact_address_cb), loop);
  webkit_web_view_load_html (WEBKIT_WEB_VIEW (ephy_embed_get_web_view (embed)), "<title>Compact page</title>", "https://compact.test/");
  if (g_strcmp0 (ephy_web_view_get_address (ephy_embed_get_web_view (embed)), "https://compact.test/") != 0)
    g_main_loop_run (loop);
  g_assert_true (gtk_widget_has_css_class (GTK_WIDGET (window), "savanna-compact"));
  assert_chrome_transparency (header, TRUE);
  gtk_widget_measure (header, GTK_ORIENTATION_VERTICAL, -1, &page_height, NULL, NULL, NULL);
  gtk_widget_measure (share_button, GTK_ORIENTATION_VERTICAL, -1, &page_control_height, NULL, NULL, NULL);
  gtk_widget_measure (url_button, GTK_ORIENTATION_VERTICAL, -1, &page_url_height, NULL, NULL, NULL);
  {
    GtkWidget *tab_bar = find_widget_with_class (GTK_WIDGET (window), "savanna-tab-bar");
    GtkBorder padding;

    g_assert_nonnull (tab_bar);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_style_context_get_padding (gtk_widget_get_style_context (tab_bar), &padding);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_assert_cmpint (padding.bottom, ==, 8);
  }
  g_assert_cmpint (page_height, <, home_height);
  g_assert_cmpint (page_control_height, ==, home_control_height);
  g_assert_cmpint (page_url_height, ==, home_url_height);
  adw_tab_view_set_selected_page (tabs, adw_tab_view_get_nth_page (tabs, 1));
  g_assert_false (gtk_widget_has_css_class (GTK_WIDGET (window), "savanna-compact"));
  adw_tab_view_set_selected_page (tabs, adw_tab_view_get_nth_page (tabs, 0));
  g_assert_true (gtk_widget_has_css_class (GTK_WIDGET (window), "savanna-compact"));
  webkit_web_view_stop_loading (WEBKIT_WEB_VIEW (ephy_embed_get_web_view (embed)));

  {
    GtkWidget *sidebar = adw_overlay_split_view_get_sidebar (split);
    GtkWidget *reading = find_sidebar_button (sidebar, "reading");
    AdwDialog *dialog;
    GtkWidget *save;
    EphyBookmark *saved;

    g_assert_nonnull (reading);
    g_assert_nonnull (find_sidebar_button (sidebar, "recent"));
    g_assert_nonnull (find_sidebar_button (sidebar, "shared"));
    g_signal_emit_by_name (reading, "clicked");
    dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));
    g_assert_nonnull (dialog);
    save = find_sidebar_button (GTK_WIDGET (dialog), "Save Current Page to Reading List");
    g_assert_nonnull (save);
    g_assert_true (gtk_widget_get_sensitive (save));
    g_signal_emit_by_name (save, "clicked");
    saved = ephy_bookmarks_manager_get_bookmark_by_url (manager, "https://compact.test/");
    g_assert_nonnull (saved);
    g_assert_true (ephy_bookmark_has_tag (saved, "Reading List"));
    adw_dialog_force_close (dialog);
    g_signal_emit_by_name (reading, "clicked");
    dialog = adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (window));
    save = find_sidebar_button (GTK_WIDGET (dialog), "Mark as Read");
    g_assert_nonnull (save);
    g_signal_emit_by_name (save, "clicked");
    g_assert_false (ephy_bookmark_has_tag (saved, "Reading List"));
    g_assert_nonnull (ephy_bookmarks_manager_get_bookmark_by_url (manager, "https://compact.test/"));
    adw_dialog_force_close (dialog);
  }

  header = GTK_WIDGET (ephy_window_get_header_bar (window));
  end = ephy_header_bar_get_action_bar_end (EPHY_HEADER_BAR (header));
  revealer = ephy_action_bar_end_get_downloads_revealer (end);
  g_assert_false (gtk_revealer_get_reveal_child (GTK_REVEALER (revealer)));
  g_assert_false (gtk_widget_get_visible (revealer));
  gtk_widget_measure (GTK_WIDGET (end), GTK_ORIENTATION_HORIZONTAL, -1, &download_width, NULL, NULL, NULL);
  g_assert_cmpint (download_width, ==, 0);
  downloads = ephy_embed_shell_get_downloads_manager (EPHY_EMBED_SHELL (shell));
  download = ephy_download_new_for_uri_internal ("http://127.0.0.1:9/test-download");
  ephy_download_disable_desktop_notification (download);
  ephy_downloads_manager_add_download (downloads, download);
  g_assert_true (gtk_revealer_get_reveal_child (GTK_REVEALER (revealer)));
  g_assert_true (gtk_widget_get_visible (revealer));
  ephy_download_set_destination (download, download_path);
  /* A completed download intentionally closes the last hidden window.
   * Keep this test window visible while exercising completion. */
  gtk_window_present (GTK_WINDOW (window));
  g_signal_emit_by_name (ephy_download_get_webkit_download (download), "finished");
  g_assert_true (ephy_download_succeeded (download));
  g_assert_true (gtk_widget_get_visible (revealer));
  downloads_icon = GTK_WIDGET (gtk_widget_get_template_child (GTK_WIDGET (end), EPHY_TYPE_ACTION_BAR_END, "downloads_icon"));
  g_assert_true (gtk_widget_has_css_class (downloads_icon, "downloads-complete"));
  ephy_download_mark_opened (download);
  g_assert_false (gtk_widget_get_visible (revealer));
  g_assert_false (gtk_widget_has_css_class (downloads_icon, "downloads-complete"));
  g_assert_nonnull (ephy_downloads_manager_get_downloads (downloads));
  /* Completion is synthetic. Stop the underlying request before yielding to
   * map the toolbar popup, or its real failure would complete it a second time. */
  g_signal_handlers_disconnect_by_data (download, downloads);
  g_signal_handlers_disconnect_by_data (ephy_download_get_webkit_download (download), downloads);
  ephy_download_cancel (download);
  gtk_widget_activate_action (GTK_WIDGET (window), "win.show-downloads", NULL);
  g_assert_false (adw_overlay_split_view_get_show_sidebar (split));
  {
    GtkWidget *button = GTK_WIDGET (gtk_widget_get_template_child (GTK_WIDGET (end), EPHY_TYPE_ACTION_BAR_END, "downloads_button"));
    GtkWidget *popover = GTK_WIDGET (gtk_menu_button_get_popover (GTK_MENU_BUTTON (button)));
    GtkWidget *scroll = gtk_widget_get_first_child (gtk_popover_get_child (GTK_POPOVER (popover)));

    g_signal_connect_swapped (popover, "map", G_CALLBACK (g_main_loop_quit), loop);
    if (!gtk_widget_get_mapped (popover))
      g_main_loop_run (loop);
    g_signal_handlers_disconnect_by_data (popover, loop);
    g_assert_true (gtk_widget_get_visible (revealer));
    assert_glass_popover (popover);
    g_assert_cmpint (gtk_scrolled_window_get_min_content_height (GTK_SCROLLED_WINDOW (scroll)), ==, 132);
    g_assert_cmpint (gtk_scrolled_window_get_max_content_height (GTK_SCROLLED_WINDOW (scroll)), ==, 132);
    g_assert_false (gtk_scrolled_window_get_propagate_natural_height (GTK_SCROLLED_WINDOW (scroll)));
    g_assert_false (gtk_scrolled_window_get_overlay_scrolling (GTK_SCROLLED_WINDOW (scroll)));
    gtk_popover_popdown (GTK_POPOVER (popover));
  }
  ephy_downloads_manager_remove_download (downloads, download);
  g_assert_false (gtk_revealer_get_reveal_child (GTK_REVEALER (revealer)));
  g_assert_false (gtk_widget_get_visible (revealer));
  gtk_widget_measure (GTK_WIDGET (end), GTK_ORIENTATION_HORIZONTAL, -1, &download_width, NULL, NULL, NULL);
  g_assert_cmpint (download_width, ==, 0);

  visit = ephy_history_page_visit_new ("http://127.0.0.1:9/visited", g_get_real_time (), EPHY_PAGE_VISIT_LINK);
  ephy_history_service_add_visit (ephy_embed_shell_get_global_history_service (EPHY_EMBED_SHELL (shell)),
                                  visit, NULL, (GAsyncReadyCallback)history_visit_added_cb, loop);
  g_main_loop_run (loop);
  g_signal_connect (ephy_embed_get_web_view (embed), "load-changed", G_CALLBACK (populated_page_loaded_cb), loop);
  ephy_web_view_load_url (ephy_embed_get_web_view (embed), "ephy-about:overview");
  g_main_loop_run (loop);
  {
    GtkWidget *sidebar = adw_overlay_split_view_get_sidebar (split);
    GtkStack *stack = g_object_get_data (G_OBJECT (window), "savanna-sidebar-stack");
    GtkWidget *pane;
    GtkWidget *toolbar;
    GtkWidget *toast;
    GtkWidget *content;

    g_action_group_activate_action (G_ACTION_GROUP (window), "bookmarks", NULL);
    g_timeout_add_once (100, session_save_wait_cb, loop);
    g_main_loop_run (loop);
    pane = gtk_stack_get_visible_child (stack);
    toolbar = GTK_WIDGET (gtk_widget_get_template_child (pane, EPHY_TYPE_BOOKMARKS_DIALOG, "toolbar_view"));
    toast = GTK_WIDGET (gtk_widget_get_template_child (pane, EPHY_TYPE_BOOKMARKS_DIALOG, "toast_overlay"));
    content = gtk_widget_get_first_child (adw_toast_overlay_get_child (ADW_TOAST_OVERLAY (toast)));
    g_assert_cmpint (gtk_widget_get_height (pane), >, 0);
    g_assert_cmpint (gtk_widget_get_height (pane), ==, gtk_widget_get_height (sidebar));
    g_assert_cmpint (gtk_widget_get_height (toolbar), ==, gtk_widget_get_height (pane));
    g_assert_cmpint (gtk_widget_get_valign (content), ==, GTK_ALIGN_FILL);
    g_action_group_activate_action (G_ACTION_GROUP (window), "bookmarks", NULL);
  }
  {
    GtkWidget *sync = find_sidebar_button (adw_overlay_split_view_get_sidebar (split), "Connect Devices…");
    GtkWidget *dialog;
    g_autofree char *session_path = g_build_filename (ephy_profile_dir (), "session_state.xml", NULL);

    g_assert_nonnull (sync);
    g_application_hold (G_APPLICATION (shell));
    gtk_window_present (GTK_WINDOW (window));
    g_signal_emit_by_name (sync, "clicked");
    dialog = ephy_shell_get_firefox_sync_dialog (shell);
    g_assert_true (GTK_IS_WINDOW (dialog));
    g_assert_true (gtk_window_get_transient_for (GTK_WINDOW (dialog)) == GTK_WINDOW (window));
    gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (shell));
    ephy_session_save (ephy_shell_get_session (shell));
    g_timeout_add_seconds_once (2, session_save_wait_cb, loop);
    g_main_loop_run (loop);
    g_assert_true (g_file_test (session_path, G_FILE_TEST_EXISTS));
    gtk_window_destroy (GTK_WINDOW (dialog));
    g_application_release (G_APPLICATION (shell));
  }
  g_assert_false (gtk_widget_has_css_class (GTK_WIDGET (window), "savanna-compact"));
  {
    g_autofree char *id = ephy_savanna_tab_group_save (window, "Research");
    g_autofree char *path = g_build_filename (ephy_profile_dir (), "savanna-tab-groups.ini", NULL);
    g_autoptr (GKeyFile) groups = g_key_file_new ();
    g_auto (GStrv) urls = NULL;
    gsize length;
    guint windows = ephy_shell_get_n_windows (shell);
    EphyWindow *reopened = NULL;
#if HAVE_DBUSMENU
    g_autofree char *menu_path = g_strdup (ephy_global_menu_get_dbusmenu_path (GTK_APPLICATION_WINDOW (window)));
#endif

    g_assert_nonnull (id);
    g_assert_true (g_key_file_load_from_file (groups, path, G_KEY_FILE_NONE, NULL));
    urls = g_key_file_get_string_list (groups, id, "urls", &length, NULL);
    g_assert_cmpuint (length, ==, 2);
    ephy_savanna_tab_group_open (window, id);
    g_assert_cmpuint (ephy_shell_get_n_windows (shell), ==, windows);
    gtk_window_destroy (GTK_WINDOW (window));
    g_assert_true (g_key_file_load_from_file (groups, path, G_KEY_FILE_NONE, NULL));
    g_clear_pointer (&urls, g_strfreev);
    urls = g_key_file_get_string_list (groups, id, "urls", &length, NULL);
    g_assert_cmpuint (length, ==, 2);
    ephy_savanna_tab_group_open (NULL, id);
    for (GList *l = gtk_application_get_windows (GTK_APPLICATION (shell)); l; l = l->next)
      if (EPHY_IS_WINDOW (l->data) && g_strcmp0 (g_object_get_data (l->data, "savanna-tab-group"), id) == 0)
        reopened = l->data;
    g_assert_nonnull (reopened);
#if HAVE_DBUSMENU
    g_assert_cmpstr (ephy_global_menu_get_dbusmenu_path (GTK_APPLICATION_WINDOW (reopened)), !=, menu_path);
    assert_dbusmenu (GTK_APPLICATION_WINDOW (reopened), ADW_OVERLAY_SPLIT_VIEW (gtk_widget_get_template_child (GTK_WIDGET (reopened), EPHY_TYPE_WINDOW, "navigation_split_view")));
#endif
    g_assert_cmpint (adw_tab_view_get_n_pages (ephy_tab_view_get_tab_view (ephy_window_get_tab_view (reopened))), ==, 2);
    ephy_savanna_tab_group_remove (id);
    g_assert_true (g_key_file_load_from_file (groups, path, G_KEY_FILE_NONE, NULL));
    g_assert_false (g_key_file_has_group (groups, id));
    g_assert_cmpint (adw_tab_view_get_n_pages (ephy_tab_view_get_tab_view (ephy_window_get_tab_view (reopened))), ==, 2);
    gtk_window_destroy (GTK_WINDOW (reopened));
  }
}

int
main (int argc, char **argv)
{
  int result;

  g_setenv ("WEBKIT_DISABLE_COMPOSITING_MODE", "1", FALSE);
  gtk_test_init (&argc, &argv);
  /* Optional system spell-check plugins can warn; GTK criticals stay fatal. */
  g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  g_assert_true (ephy_file_helpers_init (NULL, EPHY_FILE_HELPERS_TESTING_MODE | EPHY_FILE_HELPERS_ENSURE_EXISTS, NULL));
  _ephy_shell_create_instance (EPHY_EMBED_SHELL_MODE_TEST);
  g_assert_true (g_application_register (G_APPLICATION (ephy_shell_get_default ()), NULL, NULL));
  g_test_add_func ("/savanna/interface", test_interface);
  g_test_add_func ("/savanna/bookmark-preview", test_bookmark_preview);
  result = g_test_run ();
  g_object_unref (ephy_shell_get_default ());
  ephy_file_helpers_shutdown ();
  return result;
}
