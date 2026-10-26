/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gtk/gtk.h>

void ephy_global_menu_setup (GtkApplication *application);
void ephy_global_menu_attach (GtkApplicationWindow *window);
const char *ephy_global_menu_get_dbusmenu_path (GtkApplicationWindow *window);
