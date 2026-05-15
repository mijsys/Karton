#include "page-power.h"

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

static const struct option_value g_power_profile_options[] = {
    { N_("Performance"), "performance" },
    { N_("Balanced"), "balanced" },
    { N_("Power saver"), "power-saver" },
};

static const struct option_value g_lid_action_options[] = {
    { N_("Suspend"), "suspend" },
    { N_("Hibernate"), "hibernate" },
    { N_("Do nothing"), "nothing" },
};

static const struct option_value g_blank_delay_options[] = {
    { N_("1 minute"), "60" },
    { N_("2 minutes"), "120" },
    { N_("3 minutes"), "180" },
    { N_("5 minutes"), "300" },
    { N_("10 minutes"), "600" },
    { N_("15 minutes"), "900" },
    { N_("Never"), "0" },
};

static const struct option_value g_lock_delay_options[] = {
    { N_("Screen turns off"), "0" },
    { N_("30 seconds"), "30" },
    { N_("1 minute"), "60" },
    { N_("3 minutes"), "180" },
    { N_("5 minutes"), "300" },
    { N_("10 minutes"), "600" },
    { N_("30 minutes"), "1800" },
    { N_("1 hour"), "3600" },
};

static GtkWidget *g_power_profile_dropdown = NULL;
static GtkWidget *g_power_saver_switch = NULL;
static GtkWidget *g_auto_brightness_switch = NULL;
static GtkWidget *g_blank_delay_dropdown = NULL;
static GtkWidget *g_auto_lock_switch = NULL;
static GtkWidget *g_lock_delay_dropdown = NULL;
static GtkWidget *g_lock_delay_row = NULL;
static GtkWidget *g_allow_suspend_switch = NULL;
static GtkWidget *g_allow_hibernate_switch = NULL;
static GtkWidget *g_lid_action_dropdown = NULL;
static GtkWidget *g_charge_limit_switch = NULL;
static GtkWidget *g_charge_start_spin = NULL;
static GtkWidget *g_charge_end_spin = NULL;
static GtkWidget *g_battery_stats_label = NULL;
static GtkWidget *g_status_label = NULL;

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

static gboolean gsettings_key_supported(const char *schema_name, const char *key)
{
    if (!schema_name || !key) {
        return FALSE;
    }

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return FALSE;
    }

    GSettingsSchema *schema = g_settings_schema_source_lookup(source, schema_name, TRUE);
    if (!schema) {
        return FALSE;
    }

    gboolean has_key = g_settings_schema_has_key(schema, key);
    g_settings_schema_unref(schema);
    return has_key;
}

static int clamp_int(int value, int min, int max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static GtkWidget *create_row(const char *title, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 42);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
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

static guint find_option_index(const struct option_value *options, guint count, const char *value)
{
    if (!value) {
        return 0;
    }

    for (guint i = 0; i < count; i++) {
        if (g_strcmp0(value, options[i].value) == 0) {
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

static char *power_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "power.conf", NULL);
}

static char *session_environment_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "environment", NULL);
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

static void refresh_shell_and_top_panel(void)
{
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -USR1 -x karton-top-panel >/dev/null 2>&1 || true; pkill -USR1 -x karton-side-dock >/dev/null 2>&1 || true'");
}

static gboolean trigger_lock_screen_now(void)
{
    if (command_is_available("karton-settingsd")) {
        return run_command_success("sh -lc 'karton-settingsd --lock-now >/dev/null 2>&1'");
    }

    char *local_settingsd = g_build_filename(g_get_home_dir(), ".local-karton", "bin", "karton-settingsd", NULL);
    gboolean local_exists = g_file_test(local_settingsd, G_FILE_TEST_IS_EXECUTABLE);
    if (local_exists) {
        char *q_local = g_shell_quote(local_settingsd);
        char *cmd = g_strdup_printf("sh -lc \"%s --lock-now >/dev/null 2>&1\"", q_local);
        gboolean ok = run_command_success(cmd);
        g_free(cmd);
        g_free(q_local);
        g_free(local_settingsd);
        return ok;
    }
    g_free(local_settingsd);

    if (command_is_available("karton-lock")) {
        return run_command_success("sh -lc 'karton-lock >/dev/null 2>&1 &'");
    }

    char *local_lock = g_build_filename(g_get_home_dir(), ".local-karton", "bin", "karton-lock", NULL);
    gboolean local_lock_exists = g_file_test(local_lock, G_FILE_TEST_IS_EXECUTABLE);
    if (local_lock_exists) {
        char *q_local = g_shell_quote(local_lock);
        char *cmd = g_strdup_printf("sh -lc \"%s >/dev/null 2>&1 &\"", q_local);
        gboolean ok = run_command_success(cmd);
        g_free(cmd);
        g_free(q_local);
        g_free(local_lock);
        return ok;
    }
    g_free(local_lock);

    return FALSE;
}

static char *first_battery_path(void)
{
    GDir *dir = g_dir_open("/sys/class/power_supply", 0, NULL);
    if (!dir) {
        return NULL;
    }

    const char *name = NULL;
    char *out = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_prefix(name, "BAT")) {
            continue;
        }

        out = g_build_filename("/sys/class/power_supply", name, NULL);
        break;
    }

    g_dir_close(dir);
    return out;
}

static char *read_sysfs_value(const char *path)
{
    char *content = NULL;
    if (!path || !g_file_get_contents(path, &content, NULL, NULL)) {
        g_free(content);
        return NULL;
    }

    g_strstrip(content);
    return content;
}

static gboolean write_first_existing_int(const char *battery_path,
                                         const char **file_candidates,
                                         guint candidates_count,
                                         int value)
{
    if (!battery_path || !file_candidates || candidates_count == 0) {
        return FALSE;
    }

    char buf[16];
    g_snprintf(buf, sizeof(buf), "%d\n", value);

    for (guint i = 0; i < candidates_count; i++) {
        char *path = g_build_filename(battery_path, file_candidates[i], NULL);
        gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);
        gboolean ok = FALSE;

        if (exists) {
            ok = g_file_set_contents(path, buf, -1, NULL);
            g_free(path);
            return ok;
        }

        g_free(path);
    }

    return FALSE;
}

static void refresh_battery_stats(void)
{
    if (!g_battery_stats_label) {
        return;
    }

    char *state = NULL;
    char *percent = NULL;
    char *time_to_empty = NULL;
    char *time_to_full = NULL;

    if (command_is_available("upower")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'if command -v timeout >/dev/null 2>&1; then dev=$(timeout 2s upower -e 2>/dev/null | grep -m1 -E \"battery|DisplayDevice\" || true); if [ -n \"$dev\" ]; then timeout 2s upower -i \"$dev\" 2>/dev/null; fi; else dev=$(upower -e 2>/dev/null | grep -m1 -E \"battery|DisplayDevice\" || true); if [ -n \"$dev\" ]; then upower -i \"$dev\" 2>/dev/null; fi; fi'",
            &stdout_data,
            NULL,
            NULL);

        if (ok && stdout_data && *stdout_data) {
            gchar **lines = g_strsplit(stdout_data, "\n", -1);
            for (guint i = 0; lines[i] != NULL; i++) {
                char *line = g_strdup(lines[i]);
                g_strstrip(line);

                if (g_str_has_prefix(line, "state:")) {
                    g_free(state);
                    state = g_strdup(g_strstrip(line + strlen("state:")));
                } else if (g_str_has_prefix(line, "percentage:")) {
                    g_free(percent);
                    percent = g_strdup(g_strstrip(line + strlen("percentage:")));
                } else if (g_str_has_prefix(line, "time to empty:")) {
                    g_free(time_to_empty);
                    time_to_empty = g_strdup(g_strstrip(line + strlen("time to empty:")));
                } else if (g_str_has_prefix(line, "time to full:")) {
                    g_free(time_to_full);
                    time_to_full = g_strdup(g_strstrip(line + strlen("time to full:")));
                }

                g_free(line);
            }
            g_strfreev(lines);
        }

        g_free(stdout_data);
    }

    if (!percent || !*percent) {
        char *bat = first_battery_path();
        if (bat) {
            char *capacity_path = g_build_filename(bat, "capacity", NULL);
            char *status_path = g_build_filename(bat, "status", NULL);
            char *capacity = read_sysfs_value(capacity_path);
            char *status = read_sysfs_value(status_path);

            if (capacity && *capacity) {
                g_free(percent);
                percent = g_strdup_printf("%s%%", capacity);
            }
            if (status && *status) {
                g_free(state);
                state = g_strdup(status);
            }

            g_free(status);
            g_free(capacity);
            g_free(status_path);
            g_free(capacity_path);
            g_free(bat);
        }
    }

    if (!state) {
        state = g_strdup(_("unknown"));
    }
    if (!percent) {
        percent = g_strdup(_("unknown"));
    }

    const char *time_line = "";
    if (time_to_empty && *time_to_empty) {
        time_line = time_to_empty;
    } else if (time_to_full && *time_to_full) {
        time_line = time_to_full;
    }

    char *text = NULL;
    if (time_line && *time_line) {
        text = g_strdup_printf(_("State: %s\nCharge: %s\nEstimated time: %s"), state, percent, time_line);
    } else {
        text = g_strdup_printf(_("State: %s\nCharge: %s"), state, percent);
    }

    gtk_label_set_text(GTK_LABEL(g_battery_stats_label), text);

    g_free(text);
    g_free(time_to_full);
    g_free(time_to_empty);
    g_free(percent);
    g_free(state);
}

static void save_power_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "power", "power_saver", gtk_switch_get_active(GTK_SWITCH(g_power_saver_switch)));
    g_key_file_set_boolean(kf, "power", "auto_brightness", gtk_switch_get_active(GTK_SWITCH(g_auto_brightness_switch)));
    g_key_file_set_boolean(kf, "power", "allow_suspend", gtk_switch_get_active(GTK_SWITCH(g_allow_suspend_switch)));
    g_key_file_set_boolean(kf, "power", "allow_hibernate", gtk_switch_get_active(GTK_SWITCH(g_allow_hibernate_switch)));
    g_key_file_set_boolean(kf, "power", "charge_limit_enabled", gtk_switch_get_active(GTK_SWITCH(g_charge_limit_switch)));
    g_key_file_set_boolean(kf, "power", "auto_lock", gtk_switch_get_active(GTK_SWITCH(g_auto_lock_switch)));

    g_key_file_set_integer(kf, "power", "power_profile_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_power_profile_dropdown)));
    g_key_file_set_integer(kf, "power", "lid_action_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_lid_action_dropdown)));
    g_key_file_set_integer(kf, "power", "blank_delay_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_blank_delay_dropdown)));
    g_key_file_set_integer(kf, "power", "lock_delay_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_lock_delay_dropdown)));
    g_key_file_set_integer(kf, "power", "charge_start", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_charge_start_spin)));
    g_key_file_set_integer(kf, "power", "charge_end", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_charge_end_spin)));

    const char *profile = dropdown_selected_value(g_power_profile_dropdown,
                                                  g_power_profile_options,
                                                  G_N_ELEMENTS(g_power_profile_options));
    const char *lid_action = dropdown_selected_value(g_lid_action_dropdown,
                                                     g_lid_action_options,
                                                     G_N_ELEMENTS(g_lid_action_options));

    g_key_file_set_string(kf, "power", "power_profile", profile ? profile : "balanced");
    g_key_file_set_string(kf, "power", "lid_action", lid_action ? lid_action : "suspend");

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = power_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_power_config(void)
{
    char *path = power_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean power_saver = g_key_file_get_boolean(kf, "power", "power_saver", &error);
    if (error) {
        g_clear_error(&error);
        power_saver = FALSE;
    }

    gboolean auto_brightness = g_key_file_get_boolean(kf, "power", "auto_brightness", &error);
    if (error) {
        g_clear_error(&error);
        auto_brightness = FALSE;
    }

    gboolean allow_suspend = g_key_file_get_boolean(kf, "power", "allow_suspend", &error);
    if (error) {
        g_clear_error(&error);
        allow_suspend = TRUE;
    }

    gboolean allow_hibernate = g_key_file_get_boolean(kf, "power", "allow_hibernate", &error);
    if (error) {
        g_clear_error(&error);
        allow_hibernate = FALSE;
    }

    gboolean charge_limit_enabled = g_key_file_get_boolean(kf, "power", "charge_limit_enabled", &error);
    if (error) {
        g_clear_error(&error);
        charge_limit_enabled = FALSE;
    }

    gboolean auto_lock = g_key_file_get_boolean(kf, "power", "auto_lock", &error);
    if (error) {
        g_clear_error(&error);
        auto_lock = TRUE;
    }

    int power_profile_idx = g_key_file_get_integer(kf, "power", "power_profile_idx", &error);
    if (error) {
        g_clear_error(&error);
        power_profile_idx = 1;
    }

    int lid_action_idx = g_key_file_get_integer(kf, "power", "lid_action_idx", &error);
    if (error) {
        g_clear_error(&error);
        lid_action_idx = 0;
    }

    int blank_delay_idx = g_key_file_get_integer(kf, "power", "blank_delay_idx", &error);
    if (error) {
        g_clear_error(&error);
        blank_delay_idx = 3; // 5 minutes
    }

    int lock_delay_idx = g_key_file_get_integer(kf, "power", "lock_delay_idx", &error);
    if (error) {
        g_clear_error(&error);
        lock_delay_idx = 0; // Screen turns off
    }

    int charge_start = g_key_file_get_integer(kf, "power", "charge_start", &error);
    if (error) {
        g_clear_error(&error);
        charge_start = 40;
    }

    int charge_end = g_key_file_get_integer(kf, "power", "charge_end", &error);
    if (error) {
        g_clear_error(&error);
        charge_end = 80;
    }

    power_profile_idx = clamp_int(power_profile_idx, 0, (int)G_N_ELEMENTS(g_power_profile_options) - 1);
    lid_action_idx = clamp_int(lid_action_idx, 0, (int)G_N_ELEMENTS(g_lid_action_options) - 1);
    blank_delay_idx = clamp_int(blank_delay_idx, 0, (int)G_N_ELEMENTS(g_blank_delay_options) - 1);
    lock_delay_idx = clamp_int(lock_delay_idx, 0, (int)G_N_ELEMENTS(g_lock_delay_options) - 1);
    charge_start = clamp_int(charge_start, 0, 99);
    charge_end = clamp_int(charge_end, 1, 100);

    gtk_switch_set_active(GTK_SWITCH(g_power_saver_switch), power_saver);
    gtk_switch_set_active(GTK_SWITCH(g_auto_brightness_switch), auto_brightness);
    gtk_switch_set_active(GTK_SWITCH(g_allow_suspend_switch), allow_suspend);
    gtk_switch_set_active(GTK_SWITCH(g_allow_hibernate_switch), allow_hibernate);
    gtk_switch_set_active(GTK_SWITCH(g_charge_limit_switch), charge_limit_enabled);
    gtk_switch_set_active(GTK_SWITCH(g_auto_lock_switch), auto_lock);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_power_profile_dropdown), (guint)power_profile_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_lid_action_dropdown), (guint)lid_action_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_blank_delay_dropdown), (guint)blank_delay_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_lock_delay_dropdown), (guint)lock_delay_idx);

    gtk_widget_set_sensitive(g_lock_delay_row, auto_lock);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_charge_start_spin), (double)charge_start);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_charge_end_spin), (double)charge_end);

    g_key_file_unref(kf);
    g_free(path);
}

static gboolean gsettings_set_bool(const char *schema, const char *key, gboolean value)
{
    if (!command_is_available("gsettings")) {
        return FALSE;
    }

    if (!gsettings_key_supported(schema, key)) {
        return TRUE;
    }

    char *q_schema = g_shell_quote(schema);
    char *q_key = g_shell_quote(key);

    char *cmd = g_strdup_printf(
        "sh -lc \"gsettings set %s %s %s >/dev/null 2>&1\"",
        q_schema,
        q_key,
        value ? "true" : "false");

    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_key);
    g_free(q_schema);

    return ok;
}

static gboolean gsettings_set_string(const char *schema, const char *key, const char *value)
{
    if (!command_is_available("gsettings")) {
        return FALSE;
    }

    if (!gsettings_key_supported(schema, key)) {
        return TRUE;
    }

    char *q_schema = g_shell_quote(schema);
    char *q_key = g_shell_quote(key);
    char *q_value = g_shell_quote(value ? value : "");

    char *cmd = g_strdup_printf(
        "sh -lc \"gsettings set %s %s %s >/dev/null 2>&1\"",
        q_schema,
        q_key,
        q_value);

    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_value);
    g_free(q_key);
    g_free(q_schema);

    return ok;
}

static gboolean gsettings_set_uint(const char *schema, const char *key, guint value)
{
    if (!command_is_available("gsettings")) {
        return FALSE;
    }

    if (!gsettings_key_supported(schema, key)) {
        return TRUE;
    }

    char *q_schema = g_shell_quote(schema);
    char *q_key = g_shell_quote(key);

    char *cmd = g_strdup_printf(
        "sh -lc \"gsettings set %s %s uint32 %u >/dev/null 2>&1\"",
        q_schema, q_key, value);

    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_key);
    g_free(q_schema);

    return ok;
}

static gboolean apply_charge_limits_runtime(gboolean enabled, int start, int end, GString *issues)
{
    if (!enabled) {
        return TRUE;
    }

    char *battery_path = first_battery_path();
    if (!battery_path) {
        g_string_append(issues, _("No battery device found for charge limit control. "));
        return FALSE;
    }

    static const char *start_candidates[] = {
        "charge_control_start_threshold",
        "start_charge_thresh",
        "charge_start_threshold",
    };

    static const char *end_candidates[] = {
        "charge_control_end_threshold",
        "end_charge_thresh",
        "charge_stop_threshold",
    };

    gboolean ok_start = write_first_existing_int(battery_path,
                                                 start_candidates,
                                                 G_N_ELEMENTS(start_candidates),
                                                 start);
    gboolean ok_end = write_first_existing_int(battery_path,
                                               end_candidates,
                                               G_N_ELEMENTS(end_candidates),
                                               end);

    g_free(battery_path);

    if (!ok_start || !ok_end) {
        g_string_append(issues, _("Could not apply battery charge limits on this device. "));
        return FALSE;
    }

    return TRUE;
}

static char *apply_runtime_power(void)
{
    GString *issues = g_string_new(NULL);

    gboolean power_saver = gtk_switch_get_active(GTK_SWITCH(g_power_saver_switch));
    gboolean auto_brightness = gtk_switch_get_active(GTK_SWITCH(g_auto_brightness_switch));
    gboolean allow_suspend = gtk_switch_get_active(GTK_SWITCH(g_allow_suspend_switch));
    gboolean allow_hibernate = gtk_switch_get_active(GTK_SWITCH(g_allow_hibernate_switch));
    gboolean charge_limit_enabled = gtk_switch_get_active(GTK_SWITCH(g_charge_limit_switch));
    gboolean auto_lock = gtk_switch_get_active(GTK_SWITCH(g_auto_lock_switch));

    int charge_start = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_charge_start_spin));
    int charge_end = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_charge_end_spin));

    const char *profile = dropdown_selected_value(g_power_profile_dropdown,
                                                  g_power_profile_options,
                                                  G_N_ELEMENTS(g_power_profile_options));
    const char *lid_action = dropdown_selected_value(g_lid_action_dropdown,
                                                     g_lid_action_options,
                                                     G_N_ELEMENTS(g_lid_action_options));
    const char *blank_delay_str = dropdown_selected_value(g_blank_delay_dropdown,
                                                          g_blank_delay_options,
                                                          G_N_ELEMENTS(g_blank_delay_options));
    const char *lock_delay_str = dropdown_selected_value(g_lock_delay_dropdown,
                                                         g_lock_delay_options,
                                                         G_N_ELEMENTS(g_lock_delay_options));

    guint blank_delay = blank_delay_str ? (guint)g_ascii_strtoll(blank_delay_str, NULL, 10) : 300;
    guint lock_delay = lock_delay_str ? (guint)g_ascii_strtoll(lock_delay_str, NULL, 10) : 0;

    const char *effective_profile = power_saver ? "power-saver" : profile;

    if (command_is_available("powerprofilesctl")) {
        char *q_profile = g_shell_quote(effective_profile);
        char *cmd = g_strdup_printf("sh -lc \"powerprofilesctl set %s >/dev/null 2>&1\"", q_profile);
        if (!run_command_success(cmd)) {
            g_string_append(issues, _("Could not apply selected power profile. "));
        }
        g_free(cmd);
        g_free(q_profile);
    } else {
        g_string_append(issues, _("powerprofilesctl not found. Runtime power profile apply is limited. "));
    }

    if (command_is_available("gsettings")) {
        const char *inactive_action = "nothing";
        if (allow_hibernate && !allow_suspend) {
            inactive_action = "hibernate";
        } else if (allow_suspend) {
            inactive_action = "suspend";
        }

        gboolean ok_gsettings = TRUE;
        ok_gsettings &= gsettings_set_bool("org.gnome.settings-daemon.plugins.power", "ambient-enabled", auto_brightness);
        ok_gsettings &= gsettings_set_string("org.gnome.settings-daemon.plugins.power", "sleep-inactive-ac-type", inactive_action);
        ok_gsettings &= gsettings_set_string("org.gnome.settings-daemon.plugins.power", "sleep-inactive-battery-type", inactive_action);
        ok_gsettings &= gsettings_set_string("org.gnome.settings-daemon.plugins.power", "lid-close-ac-action", lid_action);
        ok_gsettings &= gsettings_set_string("org.gnome.settings-daemon.plugins.power", "lid-close-battery-action", lid_action);

        ok_gsettings &= gsettings_set_uint("org.gnome.desktop.session", "idle-delay", blank_delay);
        ok_gsettings &= gsettings_set_bool("org.gnome.desktop.screensaver", "lock-enabled", auto_lock);
        ok_gsettings &= gsettings_set_uint("org.gnome.desktop.screensaver", "lock-delay", lock_delay);

        if (!ok_gsettings) {
            g_string_append(issues, _("Could not apply some desktop power settings via gsettings. "));
        }
    }

    if (charge_limit_enabled && charge_start >= charge_end) {
        g_string_append(issues, _("Charge start limit must be lower than charge end limit. "));
    } else {
        (void)apply_charge_limits_runtime(charge_limit_enabled, charge_start, charge_end, issues);
    }

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_POWER_PROFILE=%s\n"
                           "KARTON_POWER_SAVER=%s\n"
                           "KARTON_AUTO_BRIGHTNESS=%s\n"
                           "KARTON_ALLOW_SUSPEND=%s\n"
                           "KARTON_ALLOW_HIBERNATE=%s\n"
                           "KARTON_LID_ACTION=%s\n"
                           "KARTON_IDLE_DELAY=%u\n"
                           "KARTON_LOCK_ENABLED=%s\n"
                           "KARTON_LOCK_DELAY=%u\n"
                           "KARTON_LOCK_BEFORE_SLEEP=1\n"
                           "KARTON_CHARGE_LIMIT_ENABLED=%s\n"
                           "KARTON_CHARGE_LIMIT_START=%d\n"
                           "KARTON_CHARGE_LIMIT_END=%d",
                           effective_profile,
                           power_saver ? "1" : "0",
                           auto_brightness ? "1" : "0",
                           allow_suspend ? "1" : "0",
                           allow_hibernate ? "1" : "0",
                           lid_action,
                           blank_delay,
                           auto_lock ? "1" : "0",
                           lock_delay,
                           charge_limit_enabled ? "1" : "0",
                           charge_start,
                           charge_end);

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed power env",
                                              "# END KartON managed power env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist power environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *q_profile = g_shell_quote(effective_profile);
        char *q_lid = g_shell_quote(lid_action);
        char *cmd = g_strdup_printf(
            "sh -lc \"dbus-update-activation-environment --systemd KARTON_POWER_PROFILE=%s KARTON_POWER_SAVER=%s KARTON_AUTO_BRIGHTNESS=%s KARTON_ALLOW_SUSPEND=%s KARTON_ALLOW_HIBERNATE=%s KARTON_LID_ACTION=%s KARTON_IDLE_DELAY=%u KARTON_LOCK_ENABLED=%s KARTON_LOCK_DELAY=%u KARTON_LOCK_BEFORE_SLEEP=1 KARTON_CHARGE_LIMIT_ENABLED=%s KARTON_CHARGE_LIMIT_START=%d KARTON_CHARGE_LIMIT_END=%d >/dev/null 2>&1 || true\"",
            q_profile,
            power_saver ? "1" : "0",
            auto_brightness ? "1" : "0",
            allow_suspend ? "1" : "0",
            allow_hibernate ? "1" : "0",
            q_lid,
            blank_delay,
            auto_lock ? "1" : "0",
            lock_delay,
            charge_limit_enabled ? "1" : "0",
            charge_start,
            charge_end);
        (void)run_command_success(cmd);
        g_free(cmd);
        g_free(q_lid);
        g_free(q_profile);

        (void)run_command_success("sh -lc 'killall karton-idle 2>/dev/null; karton-idle >/dev/null 2>&1 & true'");
    }

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_refresh_battery_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_battery_stats();
    status_set(_("Battery statistics refreshed"), FALSE);
}

static void on_reload_power_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_power_config();
    refresh_battery_stats();
    status_set(_("Power settings reloaded"), FALSE);
}

static void on_lock_now_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (trigger_lock_screen_now()) {
        status_set(_("Screen lock triggered"), FALSE);
        return;
    }

    status_set(_("Could not start lock screen (karton-lock/karton-settingsd missing)."), TRUE);
}

static void on_apply_power_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    gboolean has_gsettings = command_is_available("gsettings");

    gboolean charge_limit_enabled = gtk_switch_get_active(GTK_SWITCH(g_charge_limit_switch));
    int charge_start = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_charge_start_spin));
    int charge_end = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_charge_end_spin));

    if (charge_limit_enabled && charge_start >= charge_end) {
        status_set(_("Charge start limit must be lower than charge end limit."), TRUE);
        return;
    }

    save_power_config();

    char *issues = apply_runtime_power();
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-settingsd >/dev/null 2>&1 || true'");
    refresh_shell_and_top_panel();
    refresh_battery_stats();

    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    if (!has_gsettings) {
        status_set(_("Power settings applied (KartON backend mode, gsettings not installed)"), FALSE);
        return;
    }

    status_set(_("Power settings applied"), FALSE);
}

static void on_auto_lock_switch_notify(GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    gboolean active = gtk_switch_get_active(GTK_SWITCH(gobject));
    gtk_widget_set_sensitive(g_lock_delay_row, active);
}

GtkWidget *page_power_new(void)
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

    GtkWidget *title = gtk_label_new(_("Power and battery"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Manage performance profile, power saving, suspend and hibernation behavior, charging limits and battery statistics."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *perf_frame = create_section(_("Performance and saving"),
                                           _("Choose performance profile and enable battery saving features."));
    GtkWidget *perf_box = gtk_frame_get_child(GTK_FRAME(perf_frame));

    GtkStringList *profile_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_power_profile_options); i++) {
        gtk_string_list_append(profile_model, _(g_power_profile_options[i].label));
    }
    g_power_profile_dropdown = gtk_drop_down_new(G_LIST_MODEL(profile_model), NULL);
    g_object_unref(profile_model);

    g_power_saver_switch = gtk_switch_new();
    g_auto_brightness_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(perf_box), create_row(_("Performance profile"), g_power_profile_dropdown));
    gtk_box_append(GTK_BOX(perf_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(perf_box), create_row(_("Power saving"), g_power_saver_switch));
    gtk_box_append(GTK_BOX(perf_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(perf_box), create_row(_("Automatic brightness"), g_auto_brightness_switch));

    gtk_box_append(GTK_BOX(box), perf_frame);

    GtkWidget *sleep_frame = create_section(_("Sleep and lid"),
                                            _("Configure suspend, hibernation and behavior after closing the lid."));
    GtkWidget *sleep_box = gtk_frame_get_child(GTK_FRAME(sleep_frame));

    GtkStringList *lid_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_lid_action_options); i++) {
        gtk_string_list_append(lid_model, _(g_lid_action_options[i].label));
    }
    g_lid_action_dropdown = gtk_drop_down_new(G_LIST_MODEL(lid_model), NULL);
    g_object_unref(lid_model);

    g_allow_suspend_switch = gtk_switch_new();
    g_allow_hibernate_switch = gtk_switch_new();

    GtkStringList *blank_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_blank_delay_options); i++) {
        gtk_string_list_append(blank_model, _(g_blank_delay_options[i].label));
    }
    g_blank_delay_dropdown = gtk_drop_down_new(G_LIST_MODEL(blank_model), NULL);
    g_object_unref(blank_model);

    g_auto_lock_switch = gtk_switch_new();
    g_signal_connect(g_auto_lock_switch, "notify::active", G_CALLBACK(on_auto_lock_switch_notify), NULL);

    GtkStringList *lock_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_lock_delay_options); i++) {
        gtk_string_list_append(lock_model, _(g_lock_delay_options[i].label));
    }
    g_lock_delay_dropdown = gtk_drop_down_new(G_LIST_MODEL(lock_model), NULL);
    g_object_unref(lock_model);

    gtk_box_append(GTK_BOX(sleep_box), create_row(_("Blank screen"), g_blank_delay_dropdown));
    gtk_box_append(GTK_BOX(sleep_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(sleep_box), create_row(_("Automatic screen lock"), g_auto_lock_switch));
    gtk_box_append(GTK_BOX(sleep_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    g_lock_delay_row = create_row(_("Lock screen after blank for"), g_lock_delay_dropdown);
    gtk_box_append(GTK_BOX(sleep_box), g_lock_delay_row);
    gtk_box_append(GTK_BOX(sleep_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(sleep_box), create_row(_("Allow suspend"), g_allow_suspend_switch));
    gtk_box_append(GTK_BOX(sleep_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(sleep_box), create_row(_("Allow hibernation"), g_allow_hibernate_switch));
    gtk_box_append(GTK_BOX(sleep_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(sleep_box), create_row(_("Lid close action"), g_lid_action_dropdown));
    gtk_box_append(GTK_BOX(sleep_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *lock_now_btn = gtk_button_new_with_label(_("Lock screen now"));
    g_signal_connect(lock_now_btn, "clicked", G_CALLBACK(on_lock_now_clicked), NULL);
    gtk_box_append(GTK_BOX(sleep_box), create_row(_("Manual lock"), lock_now_btn));

    gtk_box_append(GTK_BOX(box), sleep_frame);

    GtkWidget *charge_frame = create_section(_("Charging limits"),
                                             _("Set battery charging thresholds if hardware/driver supports them."));
    GtkWidget *charge_box = gtk_frame_get_child(GTK_FRAME(charge_frame));

    g_charge_limit_switch = gtk_switch_new();
    g_charge_start_spin = gtk_spin_button_new_with_range(0, 99, 1);
    g_charge_end_spin = gtk_spin_button_new_with_range(1, 100, 1);

    gtk_box_append(GTK_BOX(charge_box), create_row(_("Enable charge limits"), g_charge_limit_switch));
    gtk_box_append(GTK_BOX(charge_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(charge_box), create_row(_("Charge start (%)"), g_charge_start_spin));
    gtk_box_append(GTK_BOX(charge_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(charge_box), create_row(_("Charge end (%)"), g_charge_end_spin));

    gtk_box_append(GTK_BOX(box), charge_frame);

    GtkWidget *stats_frame = create_section(_("Battery statistics"),
                                            _("Current battery status and estimated remaining time."));
    GtkWidget *stats_box = gtk_frame_get_child(GTK_FRAME(stats_frame));

    g_battery_stats_label = gtk_label_new("");
    gtk_widget_set_halign(g_battery_stats_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_battery_stats_label), TRUE);
    gtk_widget_add_css_class(g_battery_stats_label, "card-subtitle");

    GtkWidget *refresh_stats_btn = gtk_button_new_with_label(_("Refresh battery stats"));
    g_signal_connect(refresh_stats_btn, "clicked", G_CALLBACK(on_refresh_battery_clicked), NULL);

    gtk_box_append(GTK_BOX(stats_box), g_battery_stats_label);
    gtk_box_append(GTK_BOX(stats_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(stats_box), create_row(_("Statistics update"), refresh_stats_btn));

    gtk_box_append(GTK_BOX(box), stats_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_power_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply power settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_power_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(g_status_label), 72);
    gtk_widget_set_hexpand(g_status_label, TRUE);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_power_profile_dropdown), find_option_index(g_power_profile_options, G_N_ELEMENTS(g_power_profile_options), "balanced"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_lid_action_dropdown), find_option_index(g_lid_action_options, G_N_ELEMENTS(g_lid_action_options), "suspend"));

    gtk_switch_set_active(GTK_SWITCH(g_power_saver_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_auto_brightness_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_allow_suspend_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_allow_hibernate_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_charge_limit_switch), FALSE);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_charge_start_spin), 40);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_charge_end_spin), 80);

    load_power_config();
    refresh_battery_stats();

    return outer_scroll;
}
