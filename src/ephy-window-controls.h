/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <gtk/gtk.h>

GtkCssProvider *ephy_window_controls_load_theme (const char *directory);
GtkCssProvider *ephy_window_controls_load_active_theme (void);
