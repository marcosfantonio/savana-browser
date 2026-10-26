/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "ephy-savanna-sidebar.h"

#include <glib/gi18n.h>

#include "ephy-bookmarks-manager.h"
#include "ephy-embed.h"
#include "ephy-embed-container.h"
#include "ephy-embed-shell.h"
#include "ephy-file-helpers.h"
#include "ephy-shell.h"
#include "ephy-tab-view.h"
#include "ephy-web-view.h"
#include "synced-tabs-dialog.h"

#define READING_TAG "Reading List"
#define GROUP_KEY "savanna-tab-group"

typedef struct {
  EphyWindow *window;
  GtkStack *stack;
  GtkWidget *bookmarks;
  GtkWidget *root;
  GtkWidget *groups;
  GtkWidget *profile;
  GFileMonitor *monitor;
} Sidebar;

static char *
groups_path (void)
{
  return g_build_filename (ephy_profile_dir (), "savanna-tab-groups.ini", NULL);
}

static GKeyFile *
groups_load (void)
{
  GKeyFile *file = g_key_file_new ();
  g_autofree char *path = groups_path ();

  g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, NULL);
  return file;
}

static gboolean
can_save_groups (void)
{
  EphyEmbedShellMode mode = ephy_embed_shell_get_mode (ephy_embed_shell_get_default ());

  return mode != EPHY_EMBED_SHELL_MODE_INCOGNITO;
}

static void
groups_write (GKeyFile *file)
{
  g_autofree char *path = groups_path ();
  g_autofree char *data = g_key_file_to_data (file, NULL, NULL);
  g_autoptr (GError) error = NULL;

  if (!can_save_groups ())
    return;
  if (!g_file_set_contents_full (path, data, -1,
                                  G_FILE_SET_CONTENTS_CONSISTENT,
                                  0600, &error))
    g_warning ("Could not save tab groups: %s", error->message);
}

static void
save_window_group (EphyWindow *window)
{
  const char *id = g_object_get_data (G_OBJECT (window), GROUP_KEY);
  AdwTabView *view = ephy_tab_view_get_tab_view (ephy_window_get_tab_view (window));
  g_autoptr (GKeyFile) file = NULL;
  g_autoptr (GPtrArray) urls = g_ptr_array_new_with_free_func (g_free);

  if (!id || gtk_widget_in_destruction (GTK_WIDGET (window)) ||
      g_object_get_data (G_OBJECT (window), "savanna-loading-group") || !can_save_groups ())
    return;
  file = groups_load ();
  if (!g_key_file_has_group (file, id))
    return;
  for (int i = 0; i < adw_tab_view_get_n_pages (view); i++) {
    EphyEmbed *embed = EPHY_EMBED (adw_tab_page_get_child (adw_tab_view_get_nth_page (view, i)));
    const char *url = ephy_web_view_get_address (ephy_embed_get_web_view (embed));
    g_ptr_array_add (urls, g_strdup (url && *url ? url : "ephy-about:overview"));
  }
  /* Closing the window must not erase its saved group. */
  if (!urls->len)
    return;
  g_key_file_set_string_list (file, id, "urls", (const char * const *)urls->pdata, urls->len);
  groups_write (file);
}

char *
ephy_savanna_tab_group_save (EphyWindow *window,
                            const char *name)
{
  g_autoptr (GKeyFile) file = NULL;
  char *id;

  if (!name || !*name || !can_save_groups ())
    return NULL;
  save_window_group (window);
  file = groups_load ();
  id = g_uuid_string_random ();
  g_key_file_set_string (file, id, "name", name);
  groups_write (file);
  g_object_set_data_full (G_OBJECT (window), GROUP_KEY, g_strdup (id), g_free);
  save_window_group (window);
  return id;
}

void
ephy_savanna_tab_group_remove (const char *id)
{
  g_autoptr (GKeyFile) file = groups_load ();

  g_key_file_remove_group (file, id, NULL);
  groups_write (file);
  for (GList *l = gtk_application_get_windows (GTK_APPLICATION (ephy_shell_get_default ())); l; l = l->next) {
    if (g_strcmp0 (g_object_get_data (l->data, GROUP_KEY), id) == 0)
      g_object_set_data (l->data, GROUP_KEY, NULL);
  }
}

void
ephy_savanna_tab_group_open (EphyWindow *window,
                            const char *id)
{
  EphyShell *shell = ephy_shell_get_default ();
  g_autoptr (GKeyFile) file = groups_load ();
  g_auto (GStrv) urls = NULL;
  EphyWindow *target;

  if (!g_key_file_has_group (file, id) || !can_save_groups ())
    return;
  for (GList *l = gtk_application_get_windows (GTK_APPLICATION (shell)); l; l = l->next) {
    if (EPHY_IS_WINDOW (l->data) && g_strcmp0 (g_object_get_data (l->data, GROUP_KEY), id) == 0) {
      gtk_window_present (l->data);
      return;
    }
  }
  /* A group owns a window: switching never discards forms, media or tabs in
   * another group. Already-open groups are focused instead of duplicated. */
  urls = g_key_file_get_string_list (file, id, "urls", NULL, NULL);
  target = ephy_window_new ();
  g_object_set_data_full (G_OBJECT (target), GROUP_KEY, g_strdup (id), g_free);
  g_object_set_data (G_OBJECT (target), "savanna-loading-group", GINT_TO_POINTER (TRUE));
  for (guint i = 0; urls && urls[i]; i++) {
    EphyEmbed *embed = ephy_shell_new_tab (shell, target, NULL, EPHY_NEW_TAB_APPEND_LAST);
    ephy_web_view_load_url (ephy_embed_get_web_view (embed), urls[i]);
  }
  if (!urls || !urls[0]) {
    EphyEmbed *embed = ephy_shell_new_tab (shell, target, NULL, EPHY_NEW_TAB_JUMP);
    ephy_web_view_load_url (ephy_embed_get_web_view (embed), "ephy-about:overview");
  }
  g_object_set_data (G_OBJECT (target), "savanna-loading-group", NULL);
  gtk_window_present (GTK_WINDOW (target));
}

static GtkWidget *
sidebar_button (const char *icon,
                const char *title)
{
  GtkWidget *button = gtk_button_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
  GtkWidget *label = gtk_label_new (title);

  gtk_widget_add_css_class (button, "flat");
  gtk_widget_add_css_class (button, "savanna-sidebar-item");
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_box_append (GTK_BOX (box), gtk_image_new_from_icon_name (icon));
  gtk_box_append (GTK_BOX (box), label);
  gtk_button_set_child (GTK_BUTTON (button), box);
  return button;
}

static void
heading (GtkWidget *box,
         const char *text)
{
  GtkWidget *label = gtk_label_new (text);

  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_widget_add_css_class (label, "sidebar-heading");
  gtk_box_append (GTK_BOX (box), label);
}

static void
group_dialog_response (AdwAlertDialog *dialog,
                       const char *response,
                       GtkWidget *root)
{
  Sidebar *sidebar = g_object_get_data (G_OBJECT (root), "savanna-sidebar");
  GtkWidget *entry = adw_alert_dialog_get_extra_child (dialog);
  const char *id = g_object_get_data (G_OBJECT (dialog), "group-id");
  g_autofree char *name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (entry)));

  g_strstrip (name);
  if (g_strcmp0 (response, "save") != 0 || !*name)
    return;
  if (id) {
    g_autoptr (GKeyFile) file = groups_load ();
    if (!g_key_file_has_group (file, id))
      return;
    g_key_file_set_string (file, id, "name", name);
    groups_write (file);
  } else {
    g_autofree char *new_id = ephy_savanna_tab_group_save (sidebar->window, name);
  }
}

static void
group_name_changed (GtkEditable *entry,
                    AdwAlertDialog *dialog)
{
  g_autofree char *name = g_strdup (gtk_editable_get_text (entry));

  adw_alert_dialog_set_response_enabled (dialog, "save", *g_strstrip (name) != '\0');
}

static void
group_edit_clicked (GtkButton *button,
                    Sidebar *sidebar)
{
  const char *id = g_object_get_data (G_OBJECT (button), "group-id");
  g_autoptr (GKeyFile) file = groups_load ();
  g_autofree char *name = id ? g_key_file_get_string (file, id, "name", NULL) : NULL;
  AdwDialog *dialog = adw_alert_dialog_new (id ? _("Rename Tab Group") : _("New Tab Group"),
                                            id ? NULL : _("Save the tabs in this window as a group. Groups reopen in their own window without closing your other tabs."));
  GtkWidget *entry = gtk_entry_new ();

  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("Name"));
  gtk_editable_set_text (GTK_EDITABLE (entry), name ? name : "");
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), entry);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", _("Cancel"), "save", _("Save"), NULL);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "save");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "save", ADW_RESPONSE_SUGGESTED);
  g_object_set_data_full (G_OBJECT (dialog), "group-id", g_strdup (id), g_free);
  g_signal_connect_object (entry, "changed", G_CALLBACK (group_name_changed), dialog, 0);
  group_name_changed (GTK_EDITABLE (entry), ADW_ALERT_DIALOG (dialog));
  g_signal_connect_object (dialog, "response", G_CALLBACK (group_dialog_response), sidebar->root, 0);
  adw_dialog_present (dialog, GTK_WIDGET (sidebar->window));
}

static void
group_open_clicked (GtkButton *button,
                    Sidebar *sidebar)
{
  ephy_savanna_tab_group_open (sidebar->window, g_object_get_data (G_OBJECT (button), "group-id"));
}

static void
group_delete_response (AdwAlertDialog *dialog,
                       const char *response,
                       gpointer user_data)
{
  if (g_strcmp0 (response, "delete") == 0)
    ephy_savanna_tab_group_remove (g_object_get_data (G_OBJECT (dialog), "group-id"));
}

static void
group_delete_clicked (GtkButton *button,
                      Sidebar *sidebar)
{
  AdwDialog *dialog = adw_alert_dialog_new (_("Delete Tab Group?"), _("The saved group will be removed. Its open tabs will not be closed."));

  g_object_set_data_full (G_OBJECT (dialog), "group-id", g_strdup (g_object_get_data (G_OBJECT (button), "group-id")), g_free);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog), "cancel", _("Cancel"), "delete", _("Delete"), NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "delete", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");
  g_signal_connect (dialog, "response", G_CALLBACK (group_delete_response), NULL);
  adw_dialog_present (dialog, GTK_WIDGET (sidebar->window));
}

static void
refresh_groups (Sidebar *sidebar)
{
  g_autoptr (GKeyFile) file = groups_load ();
  g_auto (GStrv) ids = g_key_file_get_groups (file, NULL);

  while (gtk_widget_get_first_child (sidebar->groups))
    gtk_box_remove (GTK_BOX (sidebar->groups), gtk_widget_get_first_child (sidebar->groups));
  for (guint i = 0; ids[i]; i++) {
    g_autofree char *name = g_key_file_get_string (file, ids[i], "name", NULL);
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *button = sidebar_button ("savanna-tabs-symbolic", name ? name : _("Tab Group"));
    GtkWidget *menu = gtk_menu_button_new ();
    GtkWidget *popover = gtk_popover_new ();
    GtkWidget *actions = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *rename = gtk_button_new_with_label (_("Rename"));
    GtkWidget *remove = gtk_button_new_with_label (_("Delete"));

    gtk_widget_set_hexpand (button, TRUE);
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu), "view-more-symbolic");
    gtk_widget_add_css_class (menu, "flat");
    gtk_widget_set_tooltip_text (menu, _("Manage Tab Group"));
    gtk_box_append (GTK_BOX (actions), rename);
    gtk_box_append (GTK_BOX (actions), remove);
    gtk_popover_set_child (GTK_POPOVER (popover), actions);
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (menu), popover);
    g_object_set_data_full (G_OBJECT (button), "group-id", g_strdup (ids[i]), g_free);
    g_object_set_data_full (G_OBJECT (rename), "group-id", g_strdup (ids[i]), g_free);
    g_object_set_data_full (G_OBJECT (remove), "group-id", g_strdup (ids[i]), g_free);
    g_signal_connect (button, "clicked", G_CALLBACK (group_open_clicked), sidebar);
    g_signal_connect (rename, "clicked", G_CALLBACK (group_edit_clicked), sidebar);
    g_signal_connect (remove, "clicked", G_CALLBACK (group_delete_clicked), sidebar);
    if (g_strcmp0 (g_object_get_data (G_OBJECT (sidebar->window), GROUP_KEY), ids[i]) == 0)
      gtk_widget_add_css_class (button, "active-group");
    gtk_box_append (GTK_BOX (row), button);
    gtk_box_append (GTK_BOX (row), menu);
    gtk_box_append (GTK_BOX (sidebar->groups), row);
  }
}

static void
groups_changed (GFileMonitor *monitor,
                GFile *file,
                GFile *other,
                GFileMonitorEvent event,
                Sidebar *sidebar)
{
  refresh_groups (sidebar);
}

static void
page_uri_changed (EphyWebView *view,
                  GParamSpec *pspec,
                  GtkWidget *root)
{
  Sidebar *sidebar = g_object_get_data (G_OBJECT (root), "savanna-sidebar");

  if (gtk_widget_get_root (GTK_WIDGET (view)) != GTK_ROOT (sidebar->window))
    return;
  save_window_group (sidebar->window);
}

static void
pages_changed (AdwTabView *view,
               GParamSpec *pspec,
               GtkWidget *root)
{
  Sidebar *sidebar = g_object_get_data (G_OBJECT (root), "savanna-sidebar");
  int count = adw_tab_view_get_n_pages (view);
  g_autofree char *label = g_strdup_printf (ngettext ("Personal — %d tab", "Personal — %d tabs", count), count);

  gtk_label_set_text (GTK_LABEL (sidebar->profile), label);
  save_window_group (sidebar->window);
}

static void
page_attached (AdwTabView *view,
               AdwTabPage *page,
               int position,
               GtkWidget *root)
{
  EphyWebView *web_view = ephy_embed_get_web_view (EPHY_EMBED (adw_tab_page_get_child (page)));

  /* Reconnected when a tab moves to a different window. */
  g_signal_handlers_disconnect_by_func (web_view, page_uri_changed, root);
  g_signal_connect_object (web_view, "notify::address", G_CALLBACK (page_uri_changed), root, 0);
}

static void
connected_clicked (GtkButton *button,
                   Sidebar *sidebar)
{
  SyncedTabsDialog *dialog = synced_tabs_dialog_new (ephy_shell_get_open_tabs_manager (ephy_shell_get_default ()));

  gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (ephy_shell_get_default ()));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (sidebar->window));
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
sync_clicked (GtkButton *button,
              Sidebar *sidebar)
{
  GtkWindow *dialog = GTK_WINDOW (ephy_shell_get_firefox_sync_dialog (ephy_shell_get_default ()));

  gtk_window_set_transient_for (dialog, GTK_WINDOW (sidebar->window));
  gtk_window_present (dialog);
}

static void
open_bookmark (GtkButton *button,
               EphyWindow *window)
{
  EphyEmbed *embed = ephy_shell_new_tab (ephy_shell_get_default (), window, NULL, EPHY_NEW_TAB_JUMP);

  ephy_web_view_load_url (ephy_embed_get_web_view (embed), g_object_get_data (G_OBJECT (button), "url"));
  {
    GtkWidget *dialog = gtk_widget_get_ancestor (GTK_WIDGET (button), ADW_TYPE_DIALOG);
    if (dialog)
      adw_dialog_close (ADW_DIALOG (dialog));
  }
}

static void
mark_read (GtkButton *button,
           EphyBookmark *bookmark)
{
  ephy_bookmark_remove_tag (bookmark, READING_TAG);
  ephy_bookmarks_manager_save (ephy_shell_get_bookmarks_manager (ephy_shell_get_default ()), FALSE, FALSE, NULL, NULL, NULL);
  gtk_widget_set_visible (gtk_widget_get_parent (GTK_WIDGET (button)), FALSE);
}

static void
save_reading_clicked (GtkButton *button,
                      GtkWidget *root)
{
  Sidebar *sidebar = g_object_get_data (G_OBJECT (root), "savanna-sidebar");
  EphyEmbed *embed = ephy_embed_container_get_active_child (EPHY_EMBED_CONTAINER (sidebar->window));
  EphyBookmarksManager *manager = ephy_shell_get_bookmarks_manager (ephy_shell_get_default ());
  EphyWebView *view;
  EphyBookmark *bookmark;
  const char *url;

  if (!embed)
    return;
  view = ephy_embed_get_web_view (embed);
  url = ephy_web_view_get_address (view);
  if (!url || !(g_str_has_prefix (url, "https://") || g_str_has_prefix (url, "http://")))
    return;
  bookmark = ephy_bookmarks_manager_get_bookmark_by_url (manager, url);
  ephy_bookmarks_manager_create_tag (manager, READING_TAG);
  if (!bookmark) {
    g_autofree char *id = ephy_bookmark_generate_random_id ();
    const char *title = webkit_web_view_get_title (WEBKIT_WEB_VIEW (view));
    g_autoptr (EphyBookmark) created = ephy_bookmark_new (url, title && *title ? title : url, NULL, id);
    ephy_bookmark_add_tag (created, READING_TAG);
    ephy_bookmarks_manager_add_bookmark (manager, created);
  } else {
    ephy_bookmark_add_tag (bookmark, READING_TAG);
  }
  ephy_bookmarks_manager_save (manager, FALSE, FALSE, NULL, NULL, NULL);
  gtk_button_set_label (button, _("Saved to Reading List"));
  gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
}

static int
recent_first (gconstpointer a,
              gconstpointer b)
{
  gint64 ta = ephy_bookmark_get_time_added (*(EphyBookmark * const *)a);
  gint64 tb = ephy_bookmark_get_time_added (*(EphyBookmark * const *)b);

  return ta < tb ? 1 : ta > tb ? -1 : 0;
}

static void
collection_clicked (GtkButton *button,
                    Sidebar *sidebar)
{
  const char *kind = g_object_get_data (G_OBJECT (button), "collection");
  gboolean reading = g_str_equal (kind, "reading");
  gboolean shared = g_str_equal (kind, "shared");
  EphyBookmarksManager *manager = ephy_shell_get_bookmarks_manager (ephy_shell_get_default ());
  GSequence *bookmarks = ephy_bookmarks_manager_get_bookmarks (manager);
  g_autoptr (GPtrArray) items = g_ptr_array_new ();
  AdwDialog *dialog = adw_dialog_new ();
  GtkWidget *toolbar = adw_toolbar_view_new ();
  GtkWidget *header = adw_header_bar_new ();
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

  adw_dialog_set_title (dialog, reading ? _("Reading List") : shared ? _("Shared with You") : _("Recently Saved"));
  adw_dialog_set_content_width (dialog, 480);
  adw_dialog_set_content_height (dialog, 560);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), header);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 12);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), box);
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), scroll);
  adw_dialog_set_child (dialog, toolbar);
  if (reading) {
    GtkWidget *save = gtk_button_new_with_label (_("Save Current Page to Reading List"));
    EphyEmbed *embed = ephy_embed_container_get_active_child (EPHY_EMBED_CONTAINER (sidebar->window));
    const char *url = embed ? ephy_web_view_get_address (ephy_embed_get_web_view (embed)) : NULL;
    gtk_widget_set_sensitive (save, can_save_groups () && url && (g_str_has_prefix (url, "https://") || g_str_has_prefix (url, "http://")));
    g_signal_connect_object (save, "clicked", G_CALLBACK (save_reading_clicked), sidebar->root, 0);
    gtk_box_append (GTK_BOX (box), save);
  }
  for (GSequenceIter *iter = g_sequence_get_begin_iter (bookmarks); !g_sequence_iter_is_end (iter); iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);
    if (!shared && (!reading || ephy_bookmark_has_tag (bookmark, READING_TAG)))
      g_ptr_array_add (items, bookmark);
  }
  g_ptr_array_sort (items, recent_first);
  for (guint i = 0; i < items->len; i++) {
    EphyBookmark *bookmark = g_ptr_array_index (items, i);
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *open = sidebar_button ("user-bookmarks-symbolic", ephy_bookmark_get_title (bookmark));
    gtk_widget_set_hexpand (open, TRUE);
    gtk_widget_set_tooltip_text (open, ephy_bookmark_get_url (bookmark));
    g_object_set_data_full (G_OBJECT (open), "url", g_strdup (ephy_bookmark_get_url (bookmark)), g_free);
    g_signal_connect_object (open, "clicked", G_CALLBACK (open_bookmark), sidebar->window, 0);
    gtk_box_append (GTK_BOX (row), open);
    if (reading) {
      GtkWidget *done = gtk_button_new_from_icon_name ("object-select-symbolic");
      gtk_widget_set_tooltip_text (done, _("Mark as Read"));
      g_signal_connect_object (done, "clicked", G_CALLBACK (mark_read), bookmark, 0);
      gtk_box_append (GTK_BOX (row), done);
    }
    gtk_box_append (GTK_BOX (box), row);
  }
  if (!items->len) {
    GtkWidget *status = adw_status_page_new ();
    adw_status_page_set_icon_name (ADW_STATUS_PAGE (status), shared ? "send-to-symbolic" : "user-bookmarks-symbolic");
    adw_status_page_set_title (ADW_STATUS_PAGE (status), shared ? _("No Shared Links") : _("Nothing Saved Yet"));
    adw_status_page_set_description (ADW_STATUS_PAGE (status), shared ? _("Receiving links from other devices is not supported yet. No shared content is available.") : reading ? _("Save a page to read it later. Saved pages remain here after restarting Savanna.") : _("Your most recently saved bookmarks will appear here."));
    gtk_box_append (GTK_BOX (box), status);
  }
  adw_dialog_present (dialog, GTK_WIDGET (sidebar->window));
}

void
ephy_savanna_sidebar_show_bookmarks (EphyWindow *window,
                                     gboolean    show)
{
  GtkStack *stack = g_object_get_data (G_OBJECT (window), "savanna-sidebar-stack");

  if (!stack)
    return;
  gtk_stack_set_visible_child_name (stack, show ? "bookmarks" : "navigation");
}

static void
sidebar_free (gpointer data)
{
  Sidebar *sidebar = data;

  g_clear_object (&sidebar->monitor);
  g_free (sidebar);
}

GtkWidget *
ephy_savanna_sidebar_new (EphyWindow *window,
                          GtkWidget  *bookmarks_view)
{
  Sidebar *sidebar = g_new0 (Sidebar, 1);
  GtkWidget *stack = gtk_stack_new ();
  GtkWidget *scroll = gtk_scrolled_window_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *profile = sidebar_button ("avatar-default-symbolic", "");
  GtkWidget *create = sidebar_button ("list-add-symbolic", _("New Tab Group"));
  GtkWidget *devices = sidebar_button ("phone-symbolic", _("iPhone & Android"));
  GtkWidget *sync = sidebar_button ("view-refresh-symbolic", _("Connect Devices…"));
  GtkWidget *bookmarks = sidebar_button ("savanna-bookmarks-symbolic", _("Bookmarks"));
  GtkWidget *history = sidebar_button ("document-open-recent-symbolic", _("History"));
  AdwTabView *view = ephy_tab_view_get_tab_view (ephy_window_get_tab_view (window));
  g_autofree char *path = groups_path ();
  g_autoptr (GFile) file = g_file_new_for_path (path);
  const char *titles[] = { _("Recently Saved"), _("Reading List"), _("Shared with You") };
  const char *icons[] = { "savanna-recent-symbolic", "savanna-reading-list-symbolic", "send-to-symbolic" };
  const char *kinds[] = { "recent", "reading", "shared" };

  sidebar->window = window;
  sidebar->stack = GTK_STACK (stack);
  sidebar->bookmarks = bookmarks_view;
  sidebar->root = stack;
  sidebar->groups = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  sidebar->profile = gtk_widget_get_last_child (gtk_button_get_child (GTK_BUTTON (profile)));
  g_object_set_data_full (G_OBJECT (stack), "savanna-sidebar", sidebar, sidebar_free);
  g_object_set_data (G_OBJECT (scroll), "savanna-sidebar", sidebar);
  g_object_set_data (G_OBJECT (window), "savanna-sidebar-stack", stack);
  gtk_widget_add_css_class (scroll, "savanna-sidebar");
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), box);
  gtk_widget_add_css_class (profile, "sidebar-profile");
  gtk_actionable_set_action_name (GTK_ACTIONABLE (profile), "win.tabs-view");
  gtk_box_append (GTK_BOX (box), profile);
  heading (box, _("Personal Tab Groups"));
  gtk_box_append (GTK_BOX (box), sidebar->groups);
  gtk_box_append (GTK_BOX (box), create);
  gtk_widget_set_sensitive (create, can_save_groups ());
  g_signal_connect (create, "clicked", G_CALLBACK (group_edit_clicked), sidebar);
  heading (box, _("Devices"));
  gtk_box_append (GTK_BOX (box), devices);
  gtk_box_append (GTK_BOX (box), sync);
  gtk_widget_set_tooltip_text (sync, _("Connect Devices…"));
  g_signal_connect (devices, "clicked", G_CALLBACK (connected_clicked), sidebar);
  g_signal_connect (sync, "clicked", G_CALLBACK (sync_clicked), sidebar);
  heading (box, _("Saved"));
  for (guint i = 0; i < G_N_ELEMENTS (titles); i++) {
    GtkWidget *item = sidebar_button (icons[i], titles[i]);
    g_object_set_data (G_OBJECT (item), "collection", (gpointer)kinds[i]);
    g_signal_connect (item, "clicked", G_CALLBACK (collection_clicked), sidebar);
    gtk_box_append (GTK_BOX (box), item);
    if (i == 0) {
      gtk_actionable_set_action_name (GTK_ACTIONABLE (bookmarks), "win.bookmarks");
      gtk_box_append (GTK_BOX (box), bookmarks);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (history), "app.history");
      gtk_widget_set_tooltip_text (history, _("History"));
      gtk_box_append (GTK_BOX (box), history);
    }
  }
  sidebar->monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, NULL);
  if (sidebar->monitor)
    g_signal_connect (sidebar->monitor, "changed", G_CALLBACK (groups_changed), sidebar);
  g_signal_connect_object (view, "notify::n-pages", G_CALLBACK (pages_changed), scroll, 0);
  g_signal_connect_object (view, "page-attached", G_CALLBACK (page_attached), scroll, 0);
  pages_changed (view, NULL, scroll);
  refresh_groups (sidebar);
  gtk_stack_add_named (sidebar->stack, scroll, "navigation");
  gtk_stack_add_named (sidebar->stack, bookmarks_view, "bookmarks");
  return stack;
}
