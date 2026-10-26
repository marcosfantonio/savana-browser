/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h"
#include "ephy-global-menu.h"
#include <glib/gi18n.h>

void
ephy_global_menu_setup (GtkApplication *application)
{
  static const struct {
    const char *menu;
    const char *label;
    const char *action;
  } entries[] = {
    { N_("_File"), N_("New _Tab"), "win.new-tab" },
    { NULL, N_("_New Window"), "app.new-window" },
    { NULL, N_("New _Incognito Window"), "app.new-incognito" },
    { NULL, N_("_Open…"), "win.open" },
    { NULL, N_("Save Page _As…"), "win.save-as" },
    { NULL, N_("_Print…"), "win.print" },
    { NULL, N_("_Close Tab"), "win.global-tab-close" },
    { N_("_Edit"), N_("_Undo"), "win.undo" },
    { NULL, N_("_Redo"), "win.redo" },
    { NULL, N_("Cu_t"), "win.cut" },
    { NULL, N_("_Copy"), "win.copy" },
    { NULL, N_("_Paste"), "win.paste" },
    { NULL, N_("Select _All"), "win.select-all" },
    { N_("_View"), N_("_Reload"), "win.global-toolbar-reload" },
    { NULL, N_("_Stop"), "win.global-toolbar-stop" },
    { NULL, N_("Zoom _In"), "win.zoom-in" },
    { NULL, N_("Zoom _Out"), "win.zoom-out" },
    { NULL, N_("_Normal Size"), "win.zoom-normal" },
    { NULL, N_("Show Sidebar"), "win.toggle-sidebar" },
    { NULL, N_("Downloads"), "win.show-downloads" },
    { NULL, N_("_Fullscreen"), "win.fullscreen" },
    { N_("_History"), N_("_Back"), "win.global-toolbar-navigation-back" },
    { NULL, N_("_Forward"), "win.global-toolbar-navigation-forward" },
    { NULL, N_("_History"), "app.history" },
    { NULL, N_("Reopen Closed _Tab"), "app.reopen-closed-tab" },
    { NULL, N_("Clear Personal _Data…"), "app.clear-data-view" },
    { N_("_Bookmarks"), N_("_Bookmarks"), "win.bookmarks" },
    { NULL, N_("Add _Bookmark"), "win.bookmark-page" },
    { NULL, N_("_Import Bookmarks…"), "app.import-bookmarks" },
    { NULL, N_("_Export Bookmarks…"), "app.export-bookmarks" },
    { N_("_Window"), N_("_New Window"), "app.new-window" },
    { NULL, N_("Tab _Overview"), "win.tabs-view" },
    { NULL, N_("_Duplicate Tab"), "win.global-tab-duplicate" },
    { NULL, N_("Close _Other Tabs"), "win.global-tab-close-others" },
    { N_("_Help"), N_("_Help"), "app.help" },
    { NULL, N_("_Keyboard Shortcuts"), "app.shortcuts" },
    { NULL, N_("_About Savana"), "app.about" },
  };
  g_autoptr (GMenu) menubar = g_menu_new ();
  g_autoptr (GMenu) menu = NULL;

  for (guint i = 0; i < G_N_ELEMENTS (entries); i++) {
    if (entries[i].menu) {
      g_clear_object (&menu);
      menu = g_menu_new ();
      g_menu_append_submenu (menubar, _(entries[i].menu), G_MENU_MODEL (menu));
    }
    g_menu_append (menu, _(entries[i].label), entries[i].action);
  }
  /* Export the native GTK menu model over D-Bus for global-menu consumers. */
  gtk_application_set_menubar (application, G_MENU_MODEL (menubar));
}
