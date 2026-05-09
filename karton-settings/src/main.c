// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>

#include "ui/window.h"

static GtkWindow *
find_existing_window(GtkApplication *app)
{
    GList *windows = gtk_application_get_windows(app);
    return windows ? GTK_WINDOW(windows->data) : NULL;
}

static void
present_settings_window(GtkApplication *app, const char *page)
{
    GtkWindow *window = find_existing_window(app);

    if (!window) {
        window = karton_settings_window_new(app);
    }

    karton_settings_window_select_page(window, page);
    gtk_window_present(window);
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    present_settings_window(app, NULL);
}

static int
on_command_line(GApplication *app, GApplicationCommandLine *command_line, gpointer user_data)
{
    (void)user_data;

    GVariantDict *options = g_application_command_line_get_options_dict(command_line);
    const char *page = NULL;
    g_variant_dict_lookup(options, "page", "&s", &page);

    present_settings_window(GTK_APPLICATION(app), page);
    return 0;
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain("karton-settings", LOCALEDIR);
    bind_textdomain_codeset("karton-settings", "UTF-8");
    textdomain("karton-settings");

    const GOptionEntry option_entries[] = {
        { "page", 0, 0, G_OPTION_ARG_STRING, NULL, "Open a specific settings page", "PAGE" },
        { NULL }
    };

    GtkApplication *app = gtk_application_new(
        "io.karton.Settings",
        G_APPLICATION_NON_UNIQUE | G_APPLICATION_HANDLES_COMMAND_LINE);
    g_application_add_main_option_entries(G_APPLICATION(app), option_entries);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}