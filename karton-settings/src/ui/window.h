// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#pragma once

#include <gtk/gtk.h>

GtkWindow *karton_settings_window_new(GtkApplication *app);
void karton_settings_window_select_page(GtkWindow *window, const char *page);
