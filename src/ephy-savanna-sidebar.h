#pragma once

#include "ephy-window.h"

GtkWidget *ephy_savanna_sidebar_new          (EphyWindow *window);
char      *ephy_savanna_tab_group_save       (EphyWindow *window,
                                             const char *name);
void       ephy_savanna_tab_group_open       (EphyWindow *window,
                                             const char *id);
void       ephy_savanna_tab_group_remove     (const char *id);
