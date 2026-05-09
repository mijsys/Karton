#pragma once
#include <gtk/gtk.h>

GtkWidget *karton_settings_window_new(GtkApplication *app, const char *initial_page);
void karton_settings_window_select_page(GtkWidget *window, const char *page_id);
