#pragma once

#include <gtk/gtk.h>

GtkWidget *karton_terminal_window_new(GtkApplication *app,
	const char *initial_command,
	const char *working_directory);
