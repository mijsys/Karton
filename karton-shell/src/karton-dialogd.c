#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s) gettext(s)
#define N_(s) s

#define DIALOG_BUS_NAME "org.karton.Dialog"
#define DIALOG_OBJECT_PATH "/org/karton/Dialog"
#define DIALOG_IFACE "org.karton.Dialog1"
#define DIALOGD_IDLE_TIMEOUT_SECONDS 120

enum theme_mode {
    THEME_AUTO = 0,
    THEME_LIGHT = 1,
    THEME_DARK = 2,
};

struct dialogd_state {
    enum theme_mode configured_theme;
    bool configured_dark;
    char panel_bg[64];
    char text_color[64];
    char accent_color[64];
    int dialog_radius;
    int dialog_spacing;

    GFileMonitor *theme_monitor;
    GMainLoop *loop;
    GDBusConnection *conn;
    gint64 last_activity_us;

    bool have_previous_theme;
    enum theme_mode previous_mode;
    bool previous_dark;
};

static const char introspection_xml[] =
    "<node>"
    "  <interface name='org.karton.Dialog1'>"
    "    <method name='PromptPassword'>"
    "      <arg name='title' type='s' direction='in'/>"
    "      <arg name='text' type='s' direction='in'/>"
    "      <arg name='accepted' type='b' direction='out'/>"
    "      <arg name='password' type='s' direction='out'/>"
    "    </method>"
    "    <method name='PromptQuestion'>"
    "      <arg name='title' type='s' direction='in'/>"
    "      <arg name='text' type='s' direction='in'/>"
    "      <arg name='accepted' type='b' direction='out'/>"
    "    </method>"
    "    <method name='GetTheme'>"
    "      <arg name='theme' type='s' direction='out'/>"
    "    </method>"
    "    <signal name='ThemeChanged'>"
    "      <arg name='mode' type='s'/>"
    "      <arg name='effective' type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static void
mark_activity(struct dialogd_state *state)
{
    if (!state) {
        return;
    }
    state->last_activity_us = g_get_monotonic_time();
}

static void
karton_get_config_path(char *out, size_t out_size, const char *name)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) {
        snprintf(out, out_size, "%s/karton/%s", xdg, name);
        return;
    }

    if (!home || !home[0]) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s/.config/karton/%s", home, name);
}

static enum theme_mode
theme_mode_parse(const char *value)
{
    if (!value || !value[0]) {
        return THEME_AUTO;
    }
    if (g_ascii_strcasecmp(value, "light") == 0) {
        return THEME_LIGHT;
    }
    if (g_ascii_strcasecmp(value, "dark") == 0) {
        return THEME_DARK;
    }
    return THEME_AUTO;
}

static const char *
theme_mode_name(enum theme_mode mode)
{
    switch (mode) {
    case THEME_LIGHT:
        return "light";
    case THEME_DARK:
        return "dark";
    default:
        return "auto";
    }
}

static bool
theme_is_dark(enum theme_mode mode)
{
    if (mode == THEME_DARK) {
        return true;
    }
    if (mode == THEME_LIGHT) {
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now = { 0 };
    if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
        return true;
    }
    return tm_now.tm_hour < 7 || tm_now.tm_hour >= 19;
}

static const char *
theme_name(bool dark)
{
    return dark ? "dark" : "light";
}

static void
trim_ascii(char *s)
{
    if (!s) {
        return;
    }

    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    char *p = s;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

static bool
read_key_value_file(const char *path, const char *key, char *out, size_t out_size)
{
    if (!path || !key || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }

    char line[256] = { 0 };
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        trim_ascii(line);
        if (!line[0] || line[0] == '#') {
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        trim_ascii(line);
        char *value = eq + 1;
        trim_ascii(value);
        if (strcmp(line, key) != 0) {
            continue;
        }

        snprintf(out, out_size, "%s", value);
        found = true;
        break;
    }

    fclose(f);
    return found;
}

static void
load_theme_mode_from_files(struct dialogd_state *state)
{
    char dialog_conf[1024] = { 0 };
    char mode_file[1024] = { 0 };
    char value[128] = { 0 };

    karton_get_config_path(dialog_conf, sizeof(dialog_conf), "dialogd.conf");
    karton_get_config_path(mode_file, sizeof(mode_file), "theme-mode");

    state->configured_theme = THEME_AUTO;

    if (dialog_conf[0] && read_key_value_file(dialog_conf, "theme_mode", value, sizeof(value))) {
        state->configured_theme = theme_mode_parse(value);
    } else if (mode_file[0] && read_key_value_file(mode_file, "", value, sizeof(value))) {
        state->configured_theme = theme_mode_parse(value);
    } else if (mode_file[0]) {
        FILE *f = fopen(mode_file, "r");
        if (f) {
            if (fgets(value, sizeof(value), f)) {
                trim_ascii(value);
                state->configured_theme = theme_mode_parse(value);
            }
            fclose(f);
        }
    }

    state->configured_dark = theme_is_dark(state->configured_theme);

    snprintf(state->panel_bg, sizeof(state->panel_bg), "%s", "#fffdff");
    snprintf(state->text_color, sizeof(state->text_color), "%s", "#2d2550");
    snprintf(state->accent_color, sizeof(state->accent_color), "%s", "#8b78f6");
    state->dialog_radius = 14;
    state->dialog_spacing = 8;

    if (dialog_conf[0]) {
        if (read_key_value_file(dialog_conf, "panel_bg", value, sizeof(value))) {
            g_strlcpy(state->panel_bg, value, sizeof(state->panel_bg));
        }
        if (read_key_value_file(dialog_conf, "text_color", value, sizeof(value))) {
            g_strlcpy(state->text_color, value, sizeof(state->text_color));
        }
        if (read_key_value_file(dialog_conf, "accent_color", value, sizeof(value))) {
            g_strlcpy(state->accent_color, value, sizeof(state->accent_color));
        }
        if (read_key_value_file(dialog_conf, "dialog_radius", value, sizeof(value))) {
            state->dialog_radius = atoi(value);
        }
        if (read_key_value_file(dialog_conf, "dialog_spacing", value, sizeof(value))) {
            state->dialog_spacing = atoi(value);
        }
    }
}

static bool
str_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) {
        return false;
    }
    char *h = g_ascii_strdown(haystack, -1);
    char *n = g_ascii_strdown(needle, -1);
    bool match = (h && n && strstr(h, n) != NULL);
    g_free(h);
    g_free(n);
    return match;
}

static const char *
prompt_context_hint(const char *title, const char *text)
{
    if (str_contains_ci(title, "wi-fi") || str_contains_ci(title, "wifi")
        || str_contains_ci(text, "wi-fi") || str_contains_ci(text, "wifi")
        || str_contains_ci(text, "ssid")) {
        return "Reason: Wi-Fi network password";
    }

    if (str_contains_ci(title, "sudo") || str_contains_ci(text, "sudo")
        || str_contains_ci(title, "pkexec") || str_contains_ci(text, "pkexec")
        || str_contains_ci(title, "polkit") || str_contains_ci(text, "polkit")
        || str_contains_ci(title, "administrator") || str_contains_ci(text, "administrator")
        || str_contains_ci(title, "root") || str_contains_ci(text, "root")) {
        return "Reason: Administrator authentication";
    }

    return "Reason: Application authentication";
}

struct gtk_prompt_state {
    GMainLoop *loop;
    GtkWidget *entry;
    gboolean accepted;
    char *value;
};

static void
gtk_prompt_finish(struct gtk_prompt_state *state, gboolean accepted)
{
    if (!state) {
        return;
    }
    state->accepted = accepted;
    if (state->loop && g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
}

static void
on_gtk_prompt_accept(GtkButton *button, gpointer user_data)
{
    (void)button;
    struct gtk_prompt_state *state = user_data;
    if (!state) {
        return;
    }

    if (state->entry) {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(state->entry));
        g_free(state->value);
        state->value = g_strdup(text ? text : "");
    }

    gtk_prompt_finish(state, TRUE);
}

static void
on_gtk_prompt_cancel(GtkButton *button, gpointer user_data)
{
    (void)button;
    gtk_prompt_finish((struct gtk_prompt_state *)user_data, FALSE);
}

static gboolean
on_gtk_prompt_close(GtkWindow *window, gpointer user_data)
{
    (void)window;
    gtk_prompt_finish((struct gtk_prompt_state *)user_data, FALSE);
    return FALSE;
}

static bool
prompt_with_gtk(const char *title,
                const char *text,
                bool dark,
                bool password,
                char **out_value)
{
    if (out_value) {
        *out_value = NULL;
    }

    if (!gtk_init_check()) {
        return false;
    }

    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", dark, NULL);
    }

    struct gtk_prompt_state state = { 0 };
    state.loop = g_main_loop_new(NULL, FALSE);

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title && *title ? title : (password ? _("Password") : _("Question")));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 420, -1);

    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);

    GtkWidget *label = gtk_label_new(text && *text ? text : (password ? _("Enter password") : _("Proceed?")));
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(root), label);

    if (password) {
        GtkWidget *hint = gtk_label_new(_(prompt_context_hint(title, text)));
        gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
        gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
        gtk_box_append(GTK_BOX(root), hint);
    }

    if (password) {
        state.entry = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(state.entry), FALSE);
        gtk_entry_set_input_purpose(GTK_ENTRY(state.entry), GTK_INPUT_PURPOSE_PASSWORD);
        gtk_entry_set_activates_default(GTK_ENTRY(state.entry), TRUE);
        gtk_box_append(GTK_BOX(root), state.entry);
    }

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
    GtkWidget *ok = gtk_button_new_with_label(password ? _("OK") : _("Yes"));

    gtk_box_append(GTK_BOX(actions), cancel);
    gtk_box_append(GTK_BOX(actions), ok);
    gtk_box_append(GTK_BOX(root), actions);

    gtk_window_set_child(GTK_WINDOW(window), root);
    gtk_window_set_default_widget(GTK_WINDOW(window), ok);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_gtk_prompt_cancel), &state);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_gtk_prompt_accept), &state);
    g_signal_connect(window, "close-request", G_CALLBACK(on_gtk_prompt_close), &state);

    gtk_window_present(GTK_WINDOW(window));
    if (state.entry) {
        gtk_widget_grab_focus(state.entry);
    }
    g_main_loop_run(state.loop);

    gtk_window_destroy(GTK_WINDOW(window));
    g_main_loop_unref(state.loop);

    if (state.accepted && out_value) {
        *out_value = state.value;
        state.value = NULL;
    }
    g_free(state.value);
    return state.accepted;
}

static bool
prompt_password_backend(const char *title, const char *text, bool dark, char **out_password)
{
    *out_password = NULL;

    if (!prompt_with_gtk(title, text, dark, true, out_password)) {
        return false;
    }

    if (!out_password || !*out_password || !(*out_password)[0]) {
        g_free(out_password ? *out_password : NULL);
        if (out_password) {
            *out_password = NULL;
        }
        return false;
    }

    return true;
}

static bool
prompt_question_backend(const char *title, const char *text, bool dark)
{
    return prompt_with_gtk(title, text, dark, false, NULL);
}

static void
state_reload_theme(struct dialogd_state *state, bool emit_signal)
{
    if (!state) {
        return;
    }

    load_theme_mode_from_files(state);

    if (emit_signal && state->conn) {
        bool changed = !state->have_previous_theme
            || state->previous_mode != state->configured_theme
            || state->previous_dark != state->configured_dark;
        if (changed) {
            g_dbus_connection_emit_signal(state->conn,
                                          NULL,
                                          DIALOG_OBJECT_PATH,
                                          DIALOG_IFACE,
                                          "ThemeChanged",
                                          g_variant_new("(ss)",
                                                        theme_mode_name(state->configured_theme),
                                                        theme_name(state->configured_dark)),
                                          NULL);
        }
    }

    state->have_previous_theme = true;
    state->previous_mode = state->configured_theme;
    state->previous_dark = state->configured_dark;
}

static void
handle_method_call(GDBusConnection *connection,
                   const char *sender,
                   const char *object_path,
                   const char *interface_name,
                   const char *method_name,
                   GVariant *parameters,
                   GDBusMethodInvocation *invocation,
                   void *user_data)
{
    struct dialogd_state *state = user_data;
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;

    mark_activity(state);

    if (g_strcmp0(method_name, "GetTheme") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", theme_name(state->configured_dark)));
        return;
    }

    if (g_strcmp0(method_name, "PromptPassword") == 0) {
        const char *title = NULL;
        const char *text = NULL;
        g_variant_get(parameters, "(&s&s)", &title, &text);

        char *password = NULL;
        bool accepted = prompt_password_backend(title, text, state->configured_dark, &password);
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(bs)", accepted, accepted ? password : ""));
        g_free(password);
        return;
    }

    if (g_strcmp0(method_name, "PromptQuestion") == 0) {
        const char *title = NULL;
        const char *text = NULL;
        g_variant_get(parameters, "(&s&s)", &title, &text);

        bool accepted = prompt_question_backend(title, text, state->configured_dark);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", accepted));
        return;
    }

    g_dbus_method_invocation_return_error_literal(invocation,
                                                  G_DBUS_ERROR,
                                                  G_DBUS_ERROR_UNKNOWN_METHOD,
                                                  "Unknown method");
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

static bool
is_theme_related_file(GFile *file)
{
    if (!file) {
        return false;
    }

    bool match = false;
    char *base = g_file_get_basename(file);
    if (base) {
        if (strcmp(base, "theme-mode") == 0
            || strcmp(base, "theme.toml") == 0
            || strcmp(base, "dialogd.conf") == 0) {
            match = true;
        }
    }
    g_free(base);
    return match;
}

static void
theme_monitor_changed(GFileMonitor *monitor,
                      GFile *file,
                      GFile *other_file,
                      GFileMonitorEvent event_type,
                      void *user_data)
{
    struct dialogd_state *state = user_data;
    (void)monitor;

    bool related = is_theme_related_file(file) || is_theme_related_file(other_file);
    if (!related) {
        return;
    }

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED
        || event_type == G_FILE_MONITOR_EVENT_CREATED
        || event_type == G_FILE_MONITOR_EVENT_DELETED
        || event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
        || event_type == G_FILE_MONITOR_EVENT_MOVED_IN
        || event_type == G_FILE_MONITOR_EVENT_MOVED_OUT
        || event_type == G_FILE_MONITOR_EVENT_RENAMED) {
        state_reload_theme(state, true);
        mark_activity(state);
    }
}

static GFileMonitor *
create_theme_dir_monitor(void)
{
    char theme_path[1024] = { 0 };
    karton_get_config_path(theme_path, sizeof(theme_path), "theme-mode");
    if (!theme_path[0]) {
        return NULL;
    }

    char *dir = g_path_get_dirname(theme_path);
    if (!dir || !dir[0]) {
        g_free(dir);
        return NULL;
    }
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        g_free(dir);
        return NULL;
    }

    GFile *dir_file = g_file_new_for_path(dir);
    g_free(dir);

    GFileMonitor *mon = g_file_monitor_directory(dir_file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_unref(dir_file);
    return mon;
}

static gboolean
dialogd_idle_check(void *user_data)
{
    struct dialogd_state *state = user_data;
    if (!state || !state->loop) {
        return G_SOURCE_REMOVE;
    }

    gint64 now = g_get_monotonic_time();
    gint64 idle_us = now - state->last_activity_us;
    if (idle_us >= (gint64)DIALOGD_IDLE_TIMEOUT_SECONDS * G_USEC_PER_SEC) {
        g_main_loop_quit(state->loop);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

int
main(void)
{
    setlocale(LC_ALL, "");
    bindtextdomain("karton-shell", LOCALEDIR);
    textdomain("karton-shell");

    struct dialogd_state state = { 0 };
    mark_activity(&state);

    GError *error = NULL;
    GDBusNodeInfo *introspection = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (!introspection) {
        fprintf(stderr, "karton-dialogd: cannot parse introspection XML: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }

    GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!conn) {
        fprintf(stderr, "karton-dialogd: cannot connect to session bus: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        g_dbus_node_info_unref(introspection);
        return 1;
    }
    state.conn = conn;

    guint owner_id = g_bus_own_name_on_connection(conn,
                                                  DIALOG_BUS_NAME,
                                                  G_BUS_NAME_OWNER_FLAGS_NONE,
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  NULL);

    guint reg_id = g_dbus_connection_register_object(conn,
                                                     DIALOG_OBJECT_PATH,
                                                     introspection->interfaces[0],
                                                     &interface_vtable,
                                                     &state,
                                                     NULL,
                                                     &error);
    if (reg_id == 0) {
        fprintf(stderr, "karton-dialogd: cannot register object: %s\n", error ? error->message : "unknown");
        g_clear_error(&error);
        g_bus_unown_name(owner_id);
        g_object_unref(conn);
        g_dbus_node_info_unref(introspection);
        return 1;
    }

    state_reload_theme(&state, false);

    state.theme_monitor = create_theme_dir_monitor();
    if (state.theme_monitor) {
        g_signal_connect(state.theme_monitor, "changed", G_CALLBACK(theme_monitor_changed), &state);
    }

    state.loop = g_main_loop_new(NULL, false);
    g_timeout_add_seconds(15, dialogd_idle_check, &state);
    g_main_loop_run(state.loop);

    g_main_loop_unref(state.loop);
    state.loop = NULL;
    if (state.theme_monitor) {
        g_object_unref(state.theme_monitor);
    }
    g_dbus_connection_unregister_object(conn, reg_id);
    g_bus_unown_name(owner_id);
    g_object_unref(conn);
    g_dbus_node_info_unref(introspection);
    return 0;
}
