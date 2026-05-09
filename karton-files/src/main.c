#include <gtk/gtk.h>
#include <gio/gio.h>
#include <locale.h>
#include <libintl.h>
#include "window.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

static GFileMonitor *g_theme_mode_monitor = NULL;
static GtkWidget *g_main_window = NULL;

static void update_window_theme_class(gboolean prefer_dark) {
    if (!g_main_window) {
        return;
    }

    gtk_widget_remove_css_class(g_main_window, "theme-dark");
    gtk_widget_remove_css_class(g_main_window, "theme-light");

    if (prefer_dark) {
        gtk_widget_add_css_class(g_main_window, "theme-dark");
    } else {
        gtk_widget_add_css_class(g_main_window, "theme-light");
    }
}

static gboolean is_effective_dark_mode(const char *mode) {
    if (g_strcmp0(mode, "dark") == 0) {
        return TRUE;
    }
    if (g_strcmp0(mode, "light") == 0) {
        return FALSE;
    }

    GDateTime *now = g_date_time_new_now_local();
    gint hour = g_date_time_get_hour(now);
    g_date_time_unref(now);

    return (hour >= 19 || hour < 7);
}

static char *theme_mode_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "theme-mode", NULL);
}

static char *read_theme_mode(void) {
    char *path = theme_mode_path();
    char *content = NULL;

    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        g_free(path);
        return g_strdup("auto");
    }

    g_free(path);
    g_strstrip(content);
    if (g_strcmp0(content, "light") != 0
        && g_strcmp0(content, "dark") != 0
        && g_strcmp0(content, "auto") != 0) {
        g_free(content);
        return g_strdup("auto");
    }

    return content;
}

static void apply_runtime_theme_mode(void) {
    char *mode = read_theme_mode();
    gboolean prefer_dark = is_effective_dark_mode(mode);
    g_free(mode);

    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) {
        return;
    }

    g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark, NULL);
    update_window_theme_class(prefer_dark);
}

static void on_theme_mode_file_changed(GFileMonitor *monitor,
                                       GFile *file,
                                       GFile *other_file,
                                       GFileMonitorEvent event_type,
                                       gpointer user_data) {
    (void)monitor;
    (void)user_data;
    (void)other_file;

    if (event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
        && event_type != G_FILE_MONITOR_EVENT_CREATED
        && event_type != G_FILE_MONITOR_EVENT_MOVED_IN
        && event_type != G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED) {
        return;
    }

    if (file) {
        char *base = g_file_get_basename(file);
        gboolean is_theme_mode = g_strcmp0(base, "theme-mode") == 0;
        g_free(base);
        if (!is_theme_mode) {
            return;
        }
    }

    apply_runtime_theme_mode();
}

static void setup_theme_mode_monitor(void) {
    char *dir_path = g_build_filename(g_get_home_dir(), ".config", "karton", NULL);
    GFile *dir = g_file_new_for_path(dir_path);

    if (g_theme_mode_monitor) {
        g_object_unref(g_theme_mode_monitor);
        g_theme_mode_monitor = NULL;
    }

    g_theme_mode_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE, NULL, NULL);
    if (g_theme_mode_monitor) {
        g_signal_connect(g_theme_mode_monitor, "changed", G_CALLBACK(on_theme_mode_file_changed), NULL);
    }

    g_object_unref(dir);
    g_free(dir_path);
}

static void on_shutdown(GApplication *app, gpointer user_data) {
    (void)app;
    (void)user_data;

    if (g_theme_mode_monitor) {
        g_object_unref(g_theme_mode_monitor);
        g_theme_mode_monitor = NULL;
    }

    g_main_window = NULL;
}

static void init_locale(void) {
    setlocale(LC_ALL, "");

    const char *locale_dir = g_getenv("KARTON_LOCALEDIR");
    if (!locale_dir || !*locale_dir) {
        locale_dir = LOCALEDIR;
    }

    bindtextdomain("karton-files", locale_dir);
    bind_textdomain_codeset("karton-files", "UTF-8");
    textdomain("karton-files");
}

static void load_app_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/io/karton/Files/styles/files.css");

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    g_object_unref(provider);
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    load_app_css();
    GtkWidget *window = karton_files_window_new(app);
    g_main_window = window;
    apply_runtime_theme_mode();
    setup_theme_mode_monitor();
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    init_locale();

    GtkApplication *app = gtk_application_new("io.karton.Files", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
