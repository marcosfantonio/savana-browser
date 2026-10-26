#pragma once

#include "ephy-window.h"

GtkWidget *ephy_savana_sidebar_new          (EphyWindow *window,
                                               GtkWidget  *bookmarks);
void       ephy_savana_sidebar_show_bookmarks (EphyWindow *window,
                                                 gboolean    show);
char      *ephy_savana_tab_group_save       (EphyWindow *window,
                                             const char *name);
void       ephy_savana_tab_group_open       (EphyWindow *window,
                                             const char *id);
void       ephy_savana_tab_group_remove     (const char *id);
