#include <gtk/gtk.h>
#include <locale.h>
#include <libintl.h>

#include "window.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

static void init_locale(void) {
    setlocale(LC_ALL, "");

    const char *locale_dir = g_getenv("KARTON_LOCALEDIR");
    if (!locale_dir || !*locale_dir) {
        locale_dir = LOCALEDIR;
    }

    bindtextdomain("karton-terminal", locale_dir);
    bind_textdomain_codeset("karton-terminal", "UTF-8");
    textdomain("karton-terminal");
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    GtkWidget *window = karton_terminal_window_new(app);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    init_locale();

    GtkApplication *app = gtk_application_new("io.karton.Terminal", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
