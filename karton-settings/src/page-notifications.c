#include "page-notifications.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <string.h>

#define _(s) gettext(s)
#define N_(s) s

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_position_options[] = {
    { N_("Top right"), "top-right" },
    { N_("Top left"), "top-left" },
    { N_("Bottom right"), "bottom-right" },
    { N_("Bottom left"), "bottom-left" },
};

static const struct option_value g_priority_options[] = {
    { N_("Low"), "low" },
    { N_("Normal"), "normal" },
    { N_("High"), "high" },
    { N_("Urgent"), "urgent" },
};

static GtkWidget *g_dnd_switch = NULL;
static GtkWidget *g_position_dropdown = NULL;
static GtkWidget *g_history_switch = NULL;
static GtkWidget *g_alert_sounds_switch = NULL;

static GtkWidget *g_priority_chat_dropdown = NULL;
static GtkWidget *g_priority_system_dropdown = NULL;
static GtkWidget *g_priority_updates_dropdown = NULL;

static GtkWidget *g_status_label = NULL;
static GtkWidget *g_reload_btn = NULL;
static GtkWidget *g_test_btn = NULL;
static GtkWidget *g_apply_btn = NULL;
static GtkWidget *g_loading_box = NULL;
static GtkWidget *g_loading_spinner = NULL;
static GtkWidget *g_loading_label = NULL;

static gboolean write_managed_env_block(const char *file_path,
                                        const char *begin_marker,
                                        const char *end_marker,
                                        const char *block);

static gboolean run_command_capture(const char *command, char **stdout_out, char **stderr_out, int *wait_status_out)
{
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    int wait_status = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync(command,
                                            stdout_out ? &stdout_data : NULL,
                                            stderr_out ? &stderr_data : NULL,
                                            &wait_status,
                                            &error);
    if (!ok) {
        if (stderr_out) {
            *stderr_out = g_strdup(error ? error->message : "spawn failed");
        }
        g_clear_error(&error);
        g_free(stdout_data);
        g_free(stderr_data);
        return FALSE;
    }

    if (stdout_out) {
        *stdout_out = stdout_data;
    } else {
        g_free(stdout_data);
    }

    if (stderr_out) {
        *stderr_out = stderr_data;
    } else {
        g_free(stderr_data);
    }

    if (wait_status_out) {
        *wait_status_out = wait_status;
    }

    return g_spawn_check_wait_status(wait_status, NULL);
}

static gboolean run_command_success(const char *command)
{
    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;
    gboolean ok = run_command_capture(command, &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;
    g_free(stdout_data);
    g_free(stderr_data);
    return ok;
}

static gboolean command_is_available(const char *name)
{
    char *tool = g_find_program_in_path(name);
    if (!tool) {
        return FALSE;
    }

    g_free(tool);
    return TRUE;
}

static GtkWidget *create_row(const char *title, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), label);

    if (control) {
        gtk_widget_set_halign(control, GTK_ALIGN_END);
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), control);
    }

    return row;
}

static GtkWidget *create_section(const char *title, const char *description)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "appearance-card");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);

    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(title_lbl, "card-title");
    gtk_box_append(GTK_BOX(box), title_lbl);

    if (description && *description) {
        GtkWidget *desc_lbl = gtk_label_new(description);
        gtk_widget_set_halign(desc_lbl, GTK_ALIGN_START);
        gtk_label_set_wrap(GTK_LABEL(desc_lbl), TRUE);
        gtk_widget_add_css_class(desc_lbl, "card-subtitle");
        gtk_box_append(GTK_BOX(box), desc_lbl);
    }

    gtk_frame_set_child(GTK_FRAME(frame), box);
    return frame;
}

static void status_set(const char *text, gboolean is_error)
{
    if (!g_status_label) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(g_status_label), text ? text : "");
    gtk_widget_remove_css_class(g_status_label, "error");
    gtk_widget_remove_css_class(g_status_label, "success");
    gtk_widget_add_css_class(g_status_label, is_error ? "error" : "success");
}

static void notifications_set_controls_sensitive(gboolean sensitive)
{
    if (g_dnd_switch) {
        gtk_widget_set_sensitive(g_dnd_switch, sensitive);
    }
    if (g_position_dropdown) {
        gtk_widget_set_sensitive(g_position_dropdown, sensitive);
    }
    if (g_history_switch) {
        gtk_widget_set_sensitive(g_history_switch, sensitive);
    }
    if (g_alert_sounds_switch) {
        gtk_widget_set_sensitive(g_alert_sounds_switch, sensitive);
    }
    if (g_priority_chat_dropdown) {
        gtk_widget_set_sensitive(g_priority_chat_dropdown, sensitive);
    }
    if (g_priority_system_dropdown) {
        gtk_widget_set_sensitive(g_priority_system_dropdown, sensitive);
    }
    if (g_priority_updates_dropdown) {
        gtk_widget_set_sensitive(g_priority_updates_dropdown, sensitive);
    }
    if (g_reload_btn) {
        gtk_widget_set_sensitive(g_reload_btn, sensitive);
    }
    if (g_test_btn) {
        gtk_widget_set_sensitive(g_test_btn, sensitive);
    }
    if (g_apply_btn) {
        gtk_widget_set_sensitive(g_apply_btn, sensitive);
    }
}

static void notifications_set_loading(gboolean loading, const char *message)
{
    notifications_set_controls_sensitive(!loading);

    if (g_loading_label && message) {
        gtk_label_set_text(GTK_LABEL(g_loading_label), message);
    }

    if (g_loading_box) {
        gtk_widget_set_visible(g_loading_box, loading);
    }

    if (g_loading_spinner) {
        if (loading) {
            gtk_spinner_start(GTK_SPINNER(g_loading_spinner));
        } else {
            gtk_spinner_stop(GTK_SPINNER(g_loading_spinner));
        }
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static guint find_option_index(const struct option_value *options, guint count, const char *value)
{
    if (!value) {
        return 0;
    }

    for (guint i = 0; i < count; i++) {
        if (g_strcmp0(options[i].value, value) == 0) {
            return i;
        }
    }

    return 0;
}

static const char *dropdown_selected_value(GtkWidget *dropdown, const struct option_value *options, guint count)
{
    if (!dropdown || !options || count == 0) {
        return "";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (idx >= count) {
        idx = 0;
    }

    return options[idx].value;
}

static char *notifications_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "notifications.conf", NULL);
}

static char *session_environment_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "environment", NULL);
}

static char *mako_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "mako", "config", NULL);
}

static char *mako_default_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "mako", "config", NULL);
}

static const char *normalize_notification_position(const char *position)
{
    if (!position || !*position) {
        return "top-right";
    }

    if (g_strcmp0(position, "top-right") == 0
        || g_strcmp0(position, "top-left") == 0
        || g_strcmp0(position, "bottom-right") == 0
        || g_strcmp0(position, "bottom-left") == 0) {
        return position;
    }

    return "top-right";
}

static gboolean write_mako_anchor_config(const char *config_path, const char *position)
{
    if (!config_path || !*config_path) {
        return FALSE;
    }

    const char *anchor = normalize_notification_position(position);
    char *existing = NULL;
    gsize len = 0;
    (void)g_file_get_contents(config_path, &existing, &len, NULL);
    if (!existing) {
        existing = g_strdup("");
    }

    gchar **lines = g_strsplit(existing, "\n", -1);
    GString *filtered = g_string_new(NULL);
    gboolean skip = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        if (g_strcmp0(lines[i], "# BEGIN KartON managed notifications anchor") == 0) {
            skip = TRUE;
            continue;
        }
        if (g_strcmp0(lines[i], "# END KartON managed notifications anchor") == 0) {
            skip = FALSE;
            continue;
        }
        if (skip) {
            continue;
        }

        char *trimmed = g_strdup(lines[i]);
        g_strstrip(trimmed);
        gboolean is_anchor = g_str_has_prefix(trimmed, "anchor=") || g_str_has_prefix(trimmed, "anchor =");
        g_free(trimmed);
        if (is_anchor) {
            continue;
        }

        g_string_append(filtered, lines[i]);
        if (lines[i + 1] != NULL) {
            g_string_append_c(filtered, '\n');
        }
    }

    while (filtered->len > 0 && filtered->str[0] == '\n') {
        g_string_erase(filtered, 0, 1);
    }

    GString *out = g_string_new(NULL);
    if (filtered->len > 0) {
        g_string_append(out, filtered->str);
        if (out->str[out->len - 1] != '\n') {
            g_string_append_c(out, '\n');
        }
        g_string_append_c(out, '\n');
    }

    g_string_append_printf(out,
                           "# BEGIN KartON managed notifications anchor\n"
                           "anchor=%s\n"
                           "# END KartON managed notifications anchor\n",
                           anchor);

    char *dir = g_path_get_dirname(config_path);
    gboolean ok = FALSE;
    if (g_mkdir_with_parents(dir, 0700) == 0) {
        ok = g_file_set_contents(config_path, out->str, -1, NULL);
    }

    g_free(dir);
    g_string_free(out, TRUE);
    g_string_free(filtered, TRUE);
    g_strfreev(lines);
    g_free(existing);
    return ok;
}

static gboolean parse_truthy(const char *value)
{
    if (!value) {
        return FALSE;
    }

    while (*value && g_ascii_isspace(*value)) {
        value++;
    }

    const char *end = value + strlen(value);
    while (end > value && g_ascii_isspace(*(end - 1))) {
        end--;
    }

    if (end <= value) {
        return FALSE;
    }

    char *trimmed = g_strndup(value, (gsize)(end - value));
    gboolean result = g_ascii_strcasecmp(trimmed, "1") == 0
        || g_ascii_strcasecmp(trimmed, "yes") == 0
        || g_ascii_strcasecmp(trimmed, "true") == 0
        || g_ascii_strcasecmp(trimmed, "on") == 0;
    g_free(trimmed);

    return result;
}

static gboolean notifications_dnd_from_environment(gboolean *dnd_out)
{
    if (!dnd_out) {
        return FALSE;
    }

    char *env_path = session_environment_path();
    char *contents = NULL;
    gboolean found = FALSE;

    if (g_file_get_contents(env_path, &contents, NULL, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            if (g_str_has_prefix(lines[i], "KARTON_NOTIFICATIONS_DND=")) {
                const char *value = lines[i] + strlen("KARTON_NOTIFICATIONS_DND=");
                *dnd_out = parse_truthy(value);
                found = TRUE;
                break;
            }
        }
        g_strfreev(lines);
    }

    g_free(contents);
    g_free(env_path);
    return found;
}

static gboolean notifications_runtime_dnd_enabled(gboolean *dnd_out)
{
    if (!dnd_out) {
        return FALSE;
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture(
        "sh -lc 'if command -v makoctl >/dev/null 2>&1 && makoctl mode 2>/dev/null | tr \" \" \"\\n\" | grep -Fxq do-not-disturb; then echo 1; else echo 0; fi'",
        &stdout_data,
        NULL,
        NULL);

    if (ok && stdout_data) {
        *dnd_out = parse_truthy(stdout_data);
        g_free(stdout_data);
        return TRUE;
    }

    g_free(stdout_data);
    return notifications_dnd_from_environment(dnd_out);
}

static gboolean write_managed_env_block(const char *file_path,
                                        const char *begin_marker,
                                        const char *end_marker,
                                        const char *block)
{
    char *existing = NULL;
    gsize len = 0;
    (void)g_file_get_contents(file_path, &existing, &len, NULL);
    if (!existing) {
        existing = g_strdup("");
    }

    gchar **lines = g_strsplit(existing, "\n", -1);
    GString *filtered = g_string_new(NULL);
    gboolean skip = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        if (g_strcmp0(lines[i], begin_marker) == 0) {
            skip = TRUE;
            continue;
        }
        if (g_strcmp0(lines[i], end_marker) == 0) {
            skip = FALSE;
            continue;
        }
        if (skip) {
            continue;
        }

        g_string_append(filtered, lines[i]);
        if (lines[i + 1] != NULL) {
            g_string_append_c(filtered, '\n');
        }
    }

    while (filtered->len > 0 && filtered->str[0] == '\n') {
        g_string_erase(filtered, 0, 1);
    }

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "%s\n%s\n%s\n", begin_marker, block ? block : "", end_marker);

    if (filtered->len > 0) {
        g_string_append_c(out, '\n');
        g_string_append(out, filtered->str);
        if (out->str[out->len - 1] != '\n') {
            g_string_append_c(out, '\n');
        }
    }

    char *dir = g_path_get_dirname(file_path);
    gboolean ok = FALSE;

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        ok = g_file_set_contents(file_path, out->str, -1, NULL);
    }

    g_free(dir);
    g_string_free(out, TRUE);
    g_string_free(filtered, TRUE);
    g_strfreev(lines);
    g_free(existing);

    return ok;
}

static gboolean gsettings_set_bool(const char *schema, const char *key, gboolean value)
{
    if (!command_is_available("gsettings")) {
        return FALSE;
    }

    char *q_schema = g_shell_quote(schema);
    char *q_key = g_shell_quote(key);

    char *cmd = g_strdup_printf(
        "sh -lc 'gsettings set %s %s %s >/dev/null 2>&1'",
        q_schema,
        q_key,
        value ? "true" : "false");

    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_key);
    g_free(q_schema);

    return ok;
}

static void save_notifications_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "notifications", "dnd", gtk_switch_get_active(GTK_SWITCH(g_dnd_switch)));
    g_key_file_set_boolean(kf, "notifications", "history", gtk_switch_get_active(GTK_SWITCH(g_history_switch)));
    g_key_file_set_boolean(kf, "notifications", "alert_sounds", gtk_switch_get_active(GTK_SWITCH(g_alert_sounds_switch)));

    guint position_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_position_dropdown));
    guint chat_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_priority_chat_dropdown));
    guint system_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_priority_system_dropdown));
    guint updates_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_priority_updates_dropdown));

    if (position_idx >= G_N_ELEMENTS(g_position_options)) {
        position_idx = 0;
    }
    if (chat_idx >= G_N_ELEMENTS(g_priority_options)) {
        chat_idx = 1;
    }
    if (system_idx >= G_N_ELEMENTS(g_priority_options)) {
        system_idx = 2;
    }
    if (updates_idx >= G_N_ELEMENTS(g_priority_options)) {
        updates_idx = 2;
    }

    g_key_file_set_string(kf, "notifications", "position", g_position_options[position_idx].value);
    g_key_file_set_string(kf, "notifications", "priority_chat", g_priority_options[chat_idx].value);
    g_key_file_set_string(kf, "notifications", "priority_system", g_priority_options[system_idx].value);
    g_key_file_set_string(kf, "notifications", "priority_updates", g_priority_options[updates_idx].value);

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = notifications_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_notifications_config(void)
{
    notifications_set_loading(TRUE, _("Loading notification settings..."));

    char *path = notifications_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        gboolean runtime_dnd = FALSE;
        if (notifications_runtime_dnd_enabled(&runtime_dnd)) {
            gtk_switch_set_active(GTK_SWITCH(g_dnd_switch), runtime_dnd);
        }
        g_key_file_unref(kf);
        g_free(path);
        notifications_set_loading(FALSE, NULL);
        return;
    }

    GError *error = NULL;

    gboolean dnd = g_key_file_get_boolean(kf, "notifications", "dnd", &error);
    if (error) {
        g_clear_error(&error);
        dnd = FALSE;
    }

    gboolean history = g_key_file_get_boolean(kf, "notifications", "history", &error);
    if (error) {
        g_clear_error(&error);
        history = TRUE;
    }

    gboolean alert_sounds = g_key_file_get_boolean(kf, "notifications", "alert_sounds", &error);
    if (error) {
        g_clear_error(&error);
        alert_sounds = TRUE;
    }

    char *position = g_key_file_get_string(kf, "notifications", "position", &error);
    if (error) {
        g_clear_error(&error);
    }

    char *priority_chat = g_key_file_get_string(kf, "notifications", "priority_chat", &error);
    if (error) {
        g_clear_error(&error);
    }

    char *priority_system = g_key_file_get_string(kf, "notifications", "priority_system", &error);
    if (error) {
        g_clear_error(&error);
    }

    char *priority_updates = g_key_file_get_string(kf, "notifications", "priority_updates", &error);
    if (error) {
        g_clear_error(&error);
    }

    gboolean runtime_dnd = FALSE;
    if (notifications_runtime_dnd_enabled(&runtime_dnd)) {
        dnd = runtime_dnd;
    }

    gtk_switch_set_active(GTK_SWITCH(g_dnd_switch), dnd);
    gtk_switch_set_active(GTK_SWITCH(g_history_switch), history);
    gtk_switch_set_active(GTK_SWITCH(g_alert_sounds_switch), alert_sounds);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_position_dropdown),
                               find_option_index(g_position_options, G_N_ELEMENTS(g_position_options), position));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_chat_dropdown),
                               find_option_index(g_priority_options, G_N_ELEMENTS(g_priority_options), priority_chat));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_system_dropdown),
                               find_option_index(g_priority_options, G_N_ELEMENTS(g_priority_options), priority_system));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_updates_dropdown),
                               find_option_index(g_priority_options, G_N_ELEMENTS(g_priority_options), priority_updates));

    g_free(priority_updates);
    g_free(priority_system);
    g_free(priority_chat);
    g_free(position);
    g_key_file_unref(kf);
    g_free(path);
    notifications_set_loading(FALSE, NULL);
}

static char *apply_runtime_notifications(void)
{
    gboolean dnd = gtk_switch_get_active(GTK_SWITCH(g_dnd_switch));
    gboolean history = gtk_switch_get_active(GTK_SWITCH(g_history_switch));
    gboolean alert_sounds = gtk_switch_get_active(GTK_SWITCH(g_alert_sounds_switch));

    const char *position = dropdown_selected_value(g_position_dropdown,
                                                   g_position_options,
                                                   G_N_ELEMENTS(g_position_options));
    const char *normalized_position = normalize_notification_position(position);
    const char *priority_chat = dropdown_selected_value(g_priority_chat_dropdown,
                                                        g_priority_options,
                                                        G_N_ELEMENTS(g_priority_options));
    const char *priority_system = dropdown_selected_value(g_priority_system_dropdown,
                                                          g_priority_options,
                                                          G_N_ELEMENTS(g_priority_options));
    const char *priority_updates = dropdown_selected_value(g_priority_updates_dropdown,
                                                           g_priority_options,
                                                           G_N_ELEMENTS(g_priority_options));

    GString *issues = g_string_new(NULL);

    if (!gsettings_set_bool("org.gnome.desktop.notifications", "show-banners", !dnd)) {
        g_string_append(issues, _("Could not update Do Not Disturb state in desktop notifications. "));
    }

    if (!gsettings_set_bool("org.gnome.desktop.notifications", "show-in-lock-screen", history)) {
        g_string_append(issues, _("Could not update notification history visibility. "));
    }

    if (!gsettings_set_bool("org.gnome.desktop.sound", "event-sounds", alert_sounds)) {
        g_string_append(issues, _("Could not update alert sound policy. "));
    }

    if (command_is_available("makoctl")) {
        const char *cmd = dnd
                              ? "sh -lc 'makoctl mode -a do-not-disturb >/dev/null 2>&1 || true'"
                              : "sh -lc 'makoctl mode -r do-not-disturb >/dev/null 2>&1 || true'";
        (void)run_command_success(cmd);
    }

    if (command_is_available("mako")) {
        gboolean mako_cfg_ok = FALSE;
        char *cfg_path = mako_config_path();
        char *default_cfg_path = mako_default_config_path();

        mako_cfg_ok = write_mako_anchor_config(cfg_path, normalized_position) || mako_cfg_ok;
        mako_cfg_ok = write_mako_anchor_config(default_cfg_path, normalized_position) || mako_cfg_ok;

        if (!mako_cfg_ok) {
            g_string_append(issues, _("Could not persist notifications environment settings. "));
        }

        if (command_is_available("makoctl")) {
            (void)run_command_success("sh -lc 'makoctl reload >/dev/null 2>&1 || true'");
        } else if (command_is_available("pkill")) {
            (void)run_command_success("sh -lc 'pkill -HUP -x mako >/dev/null 2>&1 || true'");
        }

        g_free(default_cfg_path);
        g_free(cfg_path);
    }

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_NOTIFICATIONS_DND=%s\n"
                           "KARTON_NOTIFICATIONS_POSITION=%s\n"
                           "KARTON_NOTIFICATIONS_HISTORY=%s\n"
                           "KARTON_NOTIFICATIONS_ALERT_SOUNDS=%s\n"
                           "KARTON_NOTIFICATIONS_PRIORITY_CHAT=%s\n"
                           "KARTON_NOTIFICATIONS_PRIORITY_SYSTEM=%s\n"
                           "KARTON_NOTIFICATIONS_PRIORITY_UPDATES=%s",
                           dnd ? "1" : "0",
                           normalized_position,
                           history ? "1" : "0",
                           alert_sounds ? "1" : "0",
                           priority_chat,
                           priority_system,
                           priority_updates);

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed notifications env",
                                              "# END KartON managed notifications env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist notifications environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_NOTIFICATIONS_DND=%s KARTON_NOTIFICATIONS_POSITION=%s KARTON_NOTIFICATIONS_HISTORY=%s KARTON_NOTIFICATIONS_ALERT_SOUNDS=%s KARTON_NOTIFICATIONS_PRIORITY_CHAT=%s KARTON_NOTIFICATIONS_PRIORITY_SYSTEM=%s KARTON_NOTIFICATIONS_PRIORITY_UPDATES=%s >/dev/null 2>&1 || true'",
            dnd ? "1" : "0",
            normalized_position,
            history ? "1" : "0",
            alert_sounds ? "1" : "0",
            priority_chat,
            priority_system,
            priority_updates);
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static gboolean send_test_notification(void)
{
    if (!command_is_available("notify-send")) {
        return FALSE;
    }

    return run_command_success(
        "sh -lc 'notify-send --app-name=KartonSettings --icon=preferences-system-notifications \"Karton\" \"Test notification from Settings\" >/dev/null 2>&1'");
}

static void on_reload_notifications_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_notifications_config();
    status_set(_("Notification settings reloaded"), FALSE);
}

static void on_apply_notifications_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    notifications_set_loading(TRUE, _("Applying notification settings..."));

    save_notifications_config();

    char *issues = apply_runtime_notifications();
    notifications_set_loading(FALSE, NULL);
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Notification settings applied"), FALSE);
}

static void on_test_notifications_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    notifications_set_loading(TRUE, _("Preparing test notification..."));

    save_notifications_config();

    char *issues = apply_runtime_notifications();
    if (issues) {
        notifications_set_loading(FALSE, NULL);
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    if (!command_is_available("notify-send")) {
        notifications_set_loading(FALSE, NULL);
        status_set(_("Could not send test notification because notify-send is not installed."), TRUE);
        return;
    }

    if (!send_test_notification()) {
        notifications_set_loading(FALSE, NULL);
        status_set(_("Could not send test notification."), TRUE);
        return;
    }

    notifications_set_loading(FALSE, NULL);
    status_set(_("Test notification sent."), FALSE);
}

GtkWidget *page_notifications_new(void)
{
    GtkWidget *outer_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outer_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(box, 28);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_end(box, 28);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_add_css_class(box, "appearance-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(outer_scroll), box);

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(hero, "appearance-hero");

    GtkWidget *title = gtk_label_new(_("Notifications"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Manage system notification behavior: Do Not Disturb, popup position, history, alert sounds and app priorities."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *behavior_frame = create_section(_("Notification behavior"),
                                               _("Control Do Not Disturb mode, position of popups, history and alert sounds."));
    GtkWidget *behavior_box = gtk_frame_get_child(GTK_FRAME(behavior_frame));

    g_dnd_switch = gtk_switch_new();
    g_position_dropdown = gtk_drop_down_new_from_strings((const char *const[]){
        _(g_position_options[0].label),
        _(g_position_options[1].label),
        _(g_position_options[2].label),
        _(g_position_options[3].label),
        NULL
    });
    g_history_switch = gtk_switch_new();
    g_alert_sounds_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Do Not Disturb"), g_dnd_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Notification popup position"), g_position_dropdown));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Notification history"), g_history_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Alert sounds"), g_alert_sounds_switch));

    gtk_box_append(GTK_BOX(box), behavior_frame);

    GtkWidget *priority_frame = create_section(_("Application priorities"),
                                               _("Choose priority groups used for sorting and prominence of notifications."));
    GtkWidget *priority_box = gtk_frame_get_child(GTK_FRAME(priority_frame));

    g_priority_chat_dropdown = gtk_drop_down_new_from_strings((const char *const[]){
        _(g_priority_options[0].label),
        _(g_priority_options[1].label),
        _(g_priority_options[2].label),
        _(g_priority_options[3].label),
        NULL
    });

    g_priority_system_dropdown = gtk_drop_down_new_from_strings((const char *const[]){
        _(g_priority_options[0].label),
        _(g_priority_options[1].label),
        _(g_priority_options[2].label),
        _(g_priority_options[3].label),
        NULL
    });

    g_priority_updates_dropdown = gtk_drop_down_new_from_strings((const char *const[]){
        _(g_priority_options[0].label),
        _(g_priority_options[1].label),
        _(g_priority_options[2].label),
        _(g_priority_options[3].label),
        NULL
    });

    gtk_box_append(GTK_BOX(priority_box), create_row(_("Chat applications"), g_priority_chat_dropdown));
    gtk_box_append(GTK_BOX(priority_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(priority_box), create_row(_("System alerts"), g_priority_system_dropdown));
    gtk_box_append(GTK_BOX(priority_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(priority_box), create_row(_("Update notifications"), g_priority_updates_dropdown));

    gtk_box_append(GTK_BOX(box), priority_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    g_reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(g_reload_btn, "clicked", G_CALLBACK(on_reload_notifications_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_reload_btn);

    g_test_btn = gtk_button_new();
    GtkWidget *test_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *test_icon = gtk_image_new_from_icon_name("preferences-system-notifications-symbolic");
    GtkWidget *test_label = gtk_label_new(_("Test"));
    gtk_box_append(GTK_BOX(test_box), test_icon);
    gtk_box_append(GTK_BOX(test_box), test_label);
    gtk_button_set_child(GTK_BUTTON(g_test_btn), test_box);
    g_signal_connect(g_test_btn, "clicked", G_CALLBACK(on_test_notifications_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_test_btn);

    g_apply_btn = gtk_button_new_with_label(_("Apply notification settings"));
    gtk_widget_add_css_class(g_apply_btn, "suggested-action");
    g_signal_connect(g_apply_btn, "clicked", G_CALLBACK(on_apply_notifications_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_loading_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(g_loading_box, GTK_ALIGN_START);
    gtk_widget_set_visible(g_loading_box, FALSE);

    g_loading_spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(g_loading_box), g_loading_spinner);

    g_loading_label = gtk_label_new(_("Loading notification settings..."));
    gtk_widget_add_css_class(g_loading_label, "row-subtitle");
    gtk_box_append(GTK_BOX(g_loading_box), g_loading_label);

    gtk_box_append(GTK_BOX(box), g_loading_box);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_dnd_switch), FALSE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_position_dropdown), 0);
    gtk_switch_set_active(GTK_SWITCH(g_history_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_alert_sounds_switch), TRUE);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_chat_dropdown), 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_system_dropdown), 2);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_updates_dropdown), 2);

    load_notifications_config();

    return outer_scroll;
}
