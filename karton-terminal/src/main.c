#include <gtk/gtk.h>
#include <locale.h>
#include <libintl.h>

#include "window.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

struct startup_options {
    char *command;
    char *working_directory;
};

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
    struct startup_options *options = user_data;

    GtkWidget *window = karton_terminal_window_new(app,
        options ? options->command : NULL,
        options ? options->working_directory : NULL);
    gtk_widget_show_all(window);
    gtk_window_present(GTK_WINDOW(window));
}

static void free_startup_options(struct startup_options *options) {
    if (!options) {
        return;
    }

    g_free(options->command);
    g_free(options->working_directory);
}

static gboolean parse_startup_options(int argc, char **argv,
    struct startup_options *options,
    int *filtered_argc,
    char ***filtered_argv) {
    int out_argc = 0;
    char **out_argv = g_new0(char *, (gsize)argc + 1);

    for (int i = 0; i < argc; i++) {
        if (!g_strcmp0(argv[i], "-e") || !g_strcmp0(argv[i], "--execute")) {
            if (i + 1 >= argc) {
                g_printerr("karton-terminal: missing argument for %s\n", argv[i]);
                g_free(out_argv);
                return FALSE;
            }

            g_free(options->command);
            options->command = g_strdup(argv[++i]);
            continue;
        }

        if (!g_strcmp0(argv[i], "--working-directory")) {
            if (i + 1 >= argc) {
                g_printerr("karton-terminal: missing argument for %s\n", argv[i]);
                g_free(out_argv);
                return FALSE;
            }

            g_free(options->working_directory);
            options->working_directory = g_strdup(argv[++i]);
            continue;
        }

        out_argv[out_argc++] = argv[i];
    }

    out_argv[out_argc] = NULL;
    *filtered_argc = out_argc;
    *filtered_argv = out_argv;
    return TRUE;
}

int main(int argc, char **argv) {
    init_locale();

    struct startup_options options = { 0 };
    int filtered_argc = 0;
    char **filtered_argv = NULL;
    if (!parse_startup_options(argc, argv, &options, &filtered_argc, &filtered_argv)) {
        free_startup_options(&options);
        return 2;
    }

    GtkApplication *app = gtk_application_new("io.karton.Terminal", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &options);

    int status = g_application_run(G_APPLICATION(app), filtered_argc, filtered_argv);
    g_free(filtered_argv);
    free_startup_options(&options);
    g_object_unref(app);
    return status;
}
