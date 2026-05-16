#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <gio/gio.h>
#include "window.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

static GFileMonitor *g_theme_mode_monitor = NULL;
static GtkWidget *g_main_window = NULL;
static char *g_requested_page = NULL;

static void apply_runtime_theme_mode(void);

static gboolean portal_inhibit_interface_available(void) {
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!bus) {
        g_clear_error(&error);
        return FALSE;
    }

    GVariant *reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.DBus.Introspectable",
        "Introspect",
        NULL,
        G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE,
        800,
        NULL,
        &error);

    g_object_unref(bus);

    if (!reply) {
        g_clear_error(&error);
        return FALSE;
    }

    const char *xml = NULL;
    g_variant_get(reply, "(&s)", &xml);
    gboolean has_iface = (xml && g_strstr_len(xml, -1, "org.freedesktop.portal.Inhibit") != NULL);
    g_variant_unref(reply);
    return has_iface;
}

static void configure_runtime_environment(void) {
    if (!g_getenv("GSK_RENDERER")) {
        g_setenv("GSK_RENDERER", "gl", FALSE);
    }

    if (!portal_inhibit_interface_available()) {
        g_setenv("GTK_USE_PORTAL", "0", TRUE);
    }
}

static const GOptionEntry g_option_entries[] = {
    { "page", 0, 0, G_OPTION_ARG_STRING, &g_requested_page, "Open a specific settings page", "PAGE" },
    { NULL }
};

static const char *find_page_argument(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) {
            continue;
        }

        if (g_str_has_prefix(argv[i], "--page=")) {
            const char *value = argv[i] + strlen("--page=");
            if (*value) {
                return value;
            }
        }

        if (g_strcmp0(argv[i], "--page") == 0 && i + 1 < argc && argv[i + 1] && argv[i + 1][0]) {
            return argv[i + 1];
        }
    }

    return NULL;
}

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

static void on_theme_mode_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
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
    g_clear_pointer(&g_requested_page, g_free);
}

static void init_locale(void) {
    setlocale(LC_ALL, "");

    const char *locale_dir = g_getenv("KARTON_LOCALEDIR");
    if (!locale_dir || !*locale_dir) {
        locale_dir = LOCALEDIR;
    }

    bindtextdomain("karton-settings", locale_dir);
    bind_textdomain_codeset("karton-settings", "UTF-8");
    textdomain("karton-settings");
}

static void load_app_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/io/karton/Settings/styles/settings.css");
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

    if (g_main_window && GTK_IS_WINDOW(g_main_window)) {
        karton_settings_window_select_page(g_main_window, g_requested_page);
        apply_runtime_theme_mode();
        gtk_window_present(GTK_WINDOW(g_main_window));
        return;
    }

    GtkWidget *window = karton_settings_window_new(app, g_requested_page);
    g_main_window = window;
    apply_runtime_theme_mode();
    setup_theme_mode_monitor();
    gtk_window_present(GTK_WINDOW(window));
}

static int on_command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    (void)user_data;

    GVariantDict *options = g_application_command_line_get_options_dict(cmdline);
    const char *page = NULL;
    
    g_free(g_requested_page);
    g_requested_page = NULL;

    if (g_variant_dict_lookup(options, "page", "&s", &page)) {
        g_requested_page = g_strdup(page);
    } else {
        int argc = 0;
        char **argv = g_application_command_line_get_arguments(cmdline, &argc);
        page = find_page_argument(argc, argv);
        if (page && *page) {
            g_requested_page = g_strdup(page);
        }
        g_strfreev(argv);
    }

    g_application_activate(app);
    return 0;
}

int main(int argc, char **argv) {
    init_locale();
    configure_runtime_environment();

    GtkApplication *app = gtk_application_new("io.karton.Settings", G_APPLICATION_HANDLES_COMMAND_LINE);
    g_application_add_main_option_entries(G_APPLICATION(app), g_option_entries);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
