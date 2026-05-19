#include "page-bluetooth.h"

#include <gio/gio.h>
#include <glib.h>
#include <libintl.h>
#include <string.h>

#define _(s) gettext(s)
#define N_(s) s

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_bt_profile_options[] = {
    { N_("Balanced quality"), "balanced" },
    { N_("Low latency"), "low-latency" },
    { N_("Voice and calls"), "voice" },
};

static const struct option_value g_headphones_profile_options[] = {
    { N_("A2DP (high quality)"), "a2dp" },
    { N_("Hands-free (HFP/HSP)"), "hfp" },
    { N_("Off"), "off" },
};

static const struct option_value g_controller_profile_options[] = {
    { N_("XInput"), "xinput" },
    { N_("DirectInput"), "dinput" },
    { N_("Switch mode"), "switch" },
};

static GtkWidget *g_bt_power_switch = NULL;
static GtkWidget *g_bt_visibility_switch = NULL;
static GtkWidget *g_devices_dropdown = NULL;
static GtkWidget *g_pair_mac_entry = NULL;
static GtkWidget *g_bt_profile_dropdown = NULL;
static GtkWidget *g_headphones_profile_dropdown = NULL;
static GtkWidget *g_controller_profile_dropdown = NULL;
static GtkWidget *g_file_transfer_switch = NULL;
static GtkWidget *g_file_transfer_dir_entry = NULL;
static GtkWidget *g_status_label = NULL;
static GtkWidget *g_refresh_devices_btn = NULL;
static GtkWidget *g_pair_btn = NULL;
static GtkWidget *g_forget_btn = NULL;
static GtkWidget *g_reload_btn = NULL;
static GtkWidget *g_apply_btn = NULL;
static GtkWidget *g_loading_box = NULL;
static GtkWidget *g_loading_spinner = NULL;
static GtkWidget *g_loading_label = NULL;

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

static GtkWidget *create_entry_row(const char *title, const char *placeholder, GtkWidget **entry_out)
{
    GtkWidget *entry = gtk_entry_new();
    if (placeholder && *placeholder) {
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    }
    gtk_widget_set_size_request(entry, 280, -1);

    if (entry_out) {
        *entry_out = entry;
    }

    return create_row(title, entry);
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

static void bluetooth_set_controls_sensitive(gboolean sensitive)
{
    if (g_bt_power_switch) {
        gtk_widget_set_sensitive(g_bt_power_switch, sensitive);
    }
    if (g_bt_visibility_switch) {
        gtk_widget_set_sensitive(g_bt_visibility_switch, sensitive);
    }
    if (g_devices_dropdown) {
        gtk_widget_set_sensitive(g_devices_dropdown, sensitive);
    }
    if (g_pair_mac_entry) {
        gtk_widget_set_sensitive(g_pair_mac_entry, sensitive);
    }
    if (g_bt_profile_dropdown) {
        gtk_widget_set_sensitive(g_bt_profile_dropdown, sensitive);
    }
    if (g_headphones_profile_dropdown) {
        gtk_widget_set_sensitive(g_headphones_profile_dropdown, sensitive);
    }
    if (g_controller_profile_dropdown) {
        gtk_widget_set_sensitive(g_controller_profile_dropdown, sensitive);
    }
    if (g_file_transfer_switch) {
        gtk_widget_set_sensitive(g_file_transfer_switch, sensitive);
    }
    if (g_file_transfer_dir_entry) {
        gtk_widget_set_sensitive(g_file_transfer_dir_entry, sensitive);
    }
    if (g_refresh_devices_btn) {
        gtk_widget_set_sensitive(g_refresh_devices_btn, sensitive);
    }
    if (g_pair_btn) {
        gtk_widget_set_sensitive(g_pair_btn, sensitive);
    }
    if (g_forget_btn) {
        gtk_widget_set_sensitive(g_forget_btn, sensitive);
    }
    if (g_reload_btn) {
        gtk_widget_set_sensitive(g_reload_btn, sensitive);
    }
    if (g_apply_btn) {
        gtk_widget_set_sensitive(g_apply_btn, sensitive);
    }
}

static void bluetooth_set_loading(gboolean loading, const char *message)
{
    bluetooth_set_controls_sensitive(!loading);

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

static char *dropdown_selected_text(GtkWidget *dropdown)
{
    if (!dropdown) {
        return g_strdup("");
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(dropdown));
    if (!model || idx >= g_list_model_get_n_items(model)) {
        return g_strdup("");
    }

    GObject *item = g_list_model_get_item(model, idx);
    if (!item || !GTK_IS_STRING_OBJECT(item)) {
        g_clear_object(&item);
        return g_strdup("");
    }

    const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    char *out = g_strdup(text ? text : "");
    g_object_unref(item);
    return out;
}

static char *bluetooth_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "bluetooth.conf", NULL);
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

static char *expand_home_path(const char *input)
{
    if (!input || !*input) {
        return g_build_filename(g_get_home_dir(), "Downloads", NULL);
    }

    if (g_str_has_prefix(input, "~/")) {
        return g_build_filename(g_get_home_dir(), input + 2, NULL);
    }

    if (g_strcmp0(input, "$HOME") == 0) {
        return g_strdup(g_get_home_dir());
    }

    if (g_str_has_prefix(input, "$HOME/")) {
        return g_build_filename(g_get_home_dir(), input + 6, NULL);
    }

    return g_strdup(input);
}

static gboolean pactl_set_card_profile(const char *card_name, const char *profile_name)
{
    if (!card_name || !*card_name || !profile_name || !*profile_name || !command_is_available("pactl")) {
        return FALSE;
    }

    char *q_card = g_shell_quote(card_name);
    char *q_profile = g_shell_quote(profile_name);
    char *cmd = g_strdup_printf(
        "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 3s pactl set-card-profile %s %s >/dev/null 2>&1; else pactl set-card-profile %s %s >/dev/null 2>&1; fi'",
        q_card, q_profile, q_card, q_profile);

    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_profile);
    g_free(q_card);
    return ok;
}

static char *bluetooth_card_for_mac(const char *mac)
{
    if (!mac || !*mac) {
        return NULL;
    }

    char *normalized = g_ascii_strdown(mac, -1);
    for (char *p = normalized; p && *p; p++) {
        if (*p == ':') {
            *p = '_';
        }
    }

    char *card = g_strdup_printf("bluez_card.%s", normalized);
    g_free(normalized);
    return card;
}

static char *first_bluez_card(void)
{
    if (!command_is_available("pactl")) {
        return NULL;
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture(
        "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 3s pactl list cards short 2>/dev/null; else pactl list cards short 2>/dev/null; fi | awk '\''/bluez_card/{print $1; exit}'\'''",
        &stdout_data,
        NULL,
        NULL);

    if (!ok || !stdout_data) {
        g_free(stdout_data);
        return NULL;
    }

    g_strstrip(stdout_data);
    if (!*stdout_data) {
        g_free(stdout_data);
        return NULL;
    }

    return stdout_data;
}

static void map_audio_profile(const char *profile,
                              const char **primary,
                              const char **secondary)
{
    *primary = NULL;
    *secondary = NULL;

    if (!profile || !*profile) {
        return;
    }

    if (g_strcmp0(profile, "balanced") == 0 || g_strcmp0(profile, "a2dp") == 0) {
        *primary = "a2dp_sink";
        return;
    }

    if (g_strcmp0(profile, "voice") == 0 || g_strcmp0(profile, "hfp") == 0 || g_strcmp0(profile, "low-latency") == 0) {
        *primary = "handsfree_head_unit";
        *secondary = "headset_head_unit";
        return;
    }

    if (g_strcmp0(profile, "off") == 0) {
        *primary = "off";
    }
}

static gboolean apply_audio_profile_runtime(const char *mac,
                                            const char *profile,
                                            GString *issues)
{
    const char *primary = NULL;
    const char *secondary = NULL;
    map_audio_profile(profile, &primary, &secondary);

    if (!primary) {
        return TRUE;
    }

    char *card = bluetooth_card_for_mac(mac);
    if (!card) {
        card = first_bluez_card();
    }

    if (!card) {
        g_string_append(issues, _("No active Bluetooth audio card found to apply profile. "));
        return FALSE;
    }

    gboolean ok = pactl_set_card_profile(card, primary);
    if (!ok && secondary) {
        ok = pactl_set_card_profile(card, secondary);
    }

    if (!ok) {
        g_string_append(issues, _("Could not apply Bluetooth audio profile on active card. "));
    }

    g_free(card);
    return ok;
}

static gboolean apply_controller_profile_runtime(const char *controller_profile,
                                                 GString *issues)
{
    if (!controller_profile || !*controller_profile) {
        return TRUE;
    }

    const char *xbox = "0";
    const char *ps = "0";
    const char *sw = "0";

    if (g_strcmp0(controller_profile, "xinput") == 0) {
        xbox = "1";
    } else if (g_strcmp0(controller_profile, "switch") == 0) {
        sw = "1";
    } else {
        ps = "1";
    }

    GString *block = g_string_new(NULL);
    g_string_append_printf(block,
                           "KARTON_BT_CONTROLLER_PROFILE=%s\n"
                           "SDL_JOYSTICK_HIDAPI_XBOX=%s\n"
                           "SDL_JOYSTICK_HIDAPI_PS4=%s\n"
                           "SDL_JOYSTICK_HIDAPI_PS5=%s\n"
                           "SDL_JOYSTICK_HIDAPI_SWITCH=%s",
                           controller_profile,
                           xbox,
                           ps,
                           ps,
                           sw);

    char *env_path = session_environment_path();
    gboolean ok = write_managed_env_block(env_path,
                                          "# BEGIN KartON managed bluetooth controller env",
                                          "# END KartON managed bluetooth controller env",
                                          block->str);
    g_free(env_path);

    if (!ok) {
        g_string_append(issues, _("Could not persist controller profile environment settings. "));
        g_string_free(block, TRUE);
        return FALSE;
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_BT_CONTROLLER_PROFILE=%s SDL_JOYSTICK_HIDAPI_XBOX=%s SDL_JOYSTICK_HIDAPI_PS4=%s SDL_JOYSTICK_HIDAPI_PS5=%s SDL_JOYSTICK_HIDAPI_SWITCH=%s >/dev/null 2>&1 || true'",
            controller_profile,
            xbox,
            ps,
            ps,
            sw);
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    g_string_free(block, TRUE);
    return TRUE;
}

static gboolean apply_file_transfer_runtime(gboolean enabled,
                                            const char *transfer_dir,
                                            GString *issues)
{
    char *expanded = expand_home_path(transfer_dir);
    if (!expanded || !*expanded) {
        g_free(expanded);
        expanded = g_build_filename(g_get_home_dir(), "Downloads", NULL);
    }

    if (g_mkdir_with_parents(expanded, 0700) != 0) {
        g_string_append(issues, _("Could not create Bluetooth transfer directory. "));
    }

    char *obex_dir = g_build_filename(g_get_home_dir(), ".config", "obexd", NULL);
    char *obex_conf = g_build_filename(obex_dir, "obexd.conf", NULL);
    (void)g_mkdir_with_parents(obex_dir, 0700);

    GString *cfg = g_string_new(NULL);
    g_string_append(cfg, "[General]\n");
    g_string_append_printf(cfg, "Root=%s\n", expanded);
    g_string_append_printf(cfg, "AutoAccept=%s\n", enabled ? "true" : "false");

    gboolean ok_write = g_file_set_contents(obex_conf, cfg->str, -1, NULL);
    if (!ok_write) {
        g_string_append(issues, _("Could not write obexd file transfer configuration. "));
    }

    if (command_is_available("systemctl")) {
        gboolean service_ok = run_command_success(enabled
            ? "sh -lc 'systemctl --user restart obex.service >/dev/null 2>&1 || systemctl --user restart obexd.service >/dev/null 2>&1 || true'"
            : "sh -lc 'systemctl --user stop obex.service >/dev/null 2>&1 || systemctl --user stop obexd.service >/dev/null 2>&1 || true'");
        if (!service_ok) {
            g_string_append(issues, _("Could not restart user OBEX service. "));
        }
    }

    g_string_free(cfg, TRUE);
    g_free(obex_conf);
    g_free(obex_dir);
    g_free(expanded);

    return ok_write;
}

static void refresh_shell_and_top_panel(void)
{
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -USR1 -x karton-top-panel >/dev/null 2>&1 || true; pkill -USR1 -x karton-side-dock >/dev/null 2>&1 || true'");
}

static void refresh_bt_devices(void)
{
    GtkStringList *model = gtk_string_list_new(NULL);
    gboolean appended = FALSE;

    if (command_is_available("bluetoothctl")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc \"if command -v timeout >/dev/null 2>&1; then timeout 2s bluetoothctl devices 2>/dev/null; else bluetoothctl devices 2>/dev/null; fi | sed -n 's/^Device //p' | head -n 40\"",
            &stdout_data,
            NULL,
            NULL);

        if (ok && stdout_data && *stdout_data) {
            gchar **lines = g_strsplit(stdout_data, "\n", -1);
            for (guint i = 0; lines[i] != NULL; i++) {
                if (!lines[i][0]) {
                    continue;
                }
                gtk_string_list_append(model, lines[i]);
                appended = TRUE;
            }
            g_strfreev(lines);
        }

        g_free(stdout_data);
    }

    if (!appended) {
        gtk_string_list_append(model, _("No Bluetooth devices found"));
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(g_devices_dropdown), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_devices_dropdown), 0);
    g_object_unref(model);
}

static char *selected_device_mac(void)
{
    char *selected = dropdown_selected_text(g_devices_dropdown);
    if (!selected || !*selected) {
        g_free(selected);
        return NULL;
    }

    gchar **parts = g_strsplit(selected, " ", 2);
    char *mac = NULL;
    if (parts[0] && strchr(parts[0], ':')) {
        mac = g_strdup(parts[0]);
    }

    g_strfreev(parts);
    g_free(selected);
    return mac;
}

static void save_bluetooth_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "bluetooth", "enabled", gtk_switch_get_active(GTK_SWITCH(g_bt_power_switch)));
    g_key_file_set_boolean(kf, "bluetooth", "visible", gtk_switch_get_active(GTK_SWITCH(g_bt_visibility_switch)));
    g_key_file_set_boolean(kf, "bluetooth", "file_transfer", gtk_switch_get_active(GTK_SWITCH(g_file_transfer_switch)));

    g_key_file_set_integer(kf, "bluetooth", "bt_profile_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_bt_profile_dropdown)));
    g_key_file_set_integer(kf, "bluetooth", "headphones_profile_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_headphones_profile_dropdown)));
    g_key_file_set_integer(kf, "bluetooth", "controller_profile_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_controller_profile_dropdown)));

    const char *pair_mac = gtk_editable_get_text(GTK_EDITABLE(g_pair_mac_entry));
    const char *transfer_dir = gtk_editable_get_text(GTK_EDITABLE(g_file_transfer_dir_entry));

    g_key_file_set_string(kf, "bluetooth", "pair_mac", pair_mac ? pair_mac : "");
    g_key_file_set_string(kf, "bluetooth", "transfer_dir", transfer_dir ? transfer_dir : "");

    char *selected_device = dropdown_selected_text(g_devices_dropdown);
    g_key_file_set_string(kf, "bluetooth", "selected_device", selected_device ? selected_device : "");
    g_free(selected_device);

    const char *bt_profile = dropdown_selected_value(g_bt_profile_dropdown,
                                                     g_bt_profile_options,
                                                     G_N_ELEMENTS(g_bt_profile_options));
    const char *headphones_profile = dropdown_selected_value(g_headphones_profile_dropdown,
                                                             g_headphones_profile_options,
                                                             G_N_ELEMENTS(g_headphones_profile_options));
    const char *controller_profile = dropdown_selected_value(g_controller_profile_dropdown,
                                                             g_controller_profile_options,
                                                             G_N_ELEMENTS(g_controller_profile_options));

    g_key_file_set_string(kf, "bluetooth", "bt_profile", bt_profile ? bt_profile : "balanced");
    g_key_file_set_string(kf, "bluetooth", "headphones_profile", headphones_profile ? headphones_profile : "a2dp");
    g_key_file_set_string(kf, "bluetooth", "controller_profile", controller_profile ? controller_profile : "xinput");

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = bluetooth_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_bluetooth_config(void)
{
    bluetooth_set_loading(TRUE, _("Loading Bluetooth settings..."));

    char *path = bluetooth_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        bluetooth_set_loading(FALSE, NULL);
        return;
    }

    GError *error = NULL;

    gboolean enabled = g_key_file_get_boolean(kf, "bluetooth", "enabled", &error);
    if (error) {
        g_clear_error(&error);
        enabled = TRUE;
    }

    gboolean visible = g_key_file_get_boolean(kf, "bluetooth", "visible", &error);
    if (error) {
        g_clear_error(&error);
        visible = TRUE;
    }

    gboolean file_transfer = g_key_file_get_boolean(kf, "bluetooth", "file_transfer", &error);
    if (error) {
        g_clear_error(&error);
        file_transfer = TRUE;
    }

    int bt_profile_idx = g_key_file_get_integer(kf, "bluetooth", "bt_profile_idx", &error);
    if (error) {
        g_clear_error(&error);
        bt_profile_idx = 0;
    }

    int headphones_profile_idx = g_key_file_get_integer(kf, "bluetooth", "headphones_profile_idx", &error);
    if (error) {
        g_clear_error(&error);
        headphones_profile_idx = 0;
    }

    int controller_profile_idx = g_key_file_get_integer(kf, "bluetooth", "controller_profile_idx", &error);
    if (error) {
        g_clear_error(&error);
        controller_profile_idx = 0;
    }

    char *pair_mac = g_key_file_get_string(kf, "bluetooth", "pair_mac", &error);
    if (error) {
        g_clear_error(&error);
        pair_mac = g_strdup("");
    }

    char *transfer_dir = g_key_file_get_string(kf, "bluetooth", "transfer_dir", &error);
    if (error) {
        g_clear_error(&error);
        transfer_dir = g_strdup("$HOME/Downloads");
    }

    bt_profile_idx = clamp_int(bt_profile_idx, 0, (int)G_N_ELEMENTS(g_bt_profile_options) - 1);
    headphones_profile_idx = clamp_int(headphones_profile_idx, 0, (int)G_N_ELEMENTS(g_headphones_profile_options) - 1);
    controller_profile_idx = clamp_int(controller_profile_idx, 0, (int)G_N_ELEMENTS(g_controller_profile_options) - 1);

    gtk_switch_set_active(GTK_SWITCH(g_bt_power_switch), enabled);
    gtk_switch_set_active(GTK_SWITCH(g_bt_visibility_switch), visible);
    gtk_switch_set_active(GTK_SWITCH(g_file_transfer_switch), file_transfer);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_bt_profile_dropdown), (guint)bt_profile_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_headphones_profile_dropdown), (guint)headphones_profile_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_controller_profile_dropdown), (guint)controller_profile_idx);

    gtk_editable_set_text(GTK_EDITABLE(g_pair_mac_entry), pair_mac);
    gtk_editable_set_text(GTK_EDITABLE(g_file_transfer_dir_entry), transfer_dir);

    g_free(pair_mac);
    g_free(transfer_dir);
    g_key_file_unref(kf);
    g_free(path);
    bluetooth_set_loading(FALSE, NULL);
}

static char *apply_runtime_bluetooth(void)
{
    GString *issues = g_string_new(NULL);

    if (!command_is_available("bluetoothctl")) {
        g_string_append(issues, _("bluetoothctl not found. Runtime Bluetooth apply is limited. "));
        return g_string_free(issues, FALSE);
    }

    gboolean enabled = gtk_switch_get_active(GTK_SWITCH(g_bt_power_switch));
    gboolean visible = gtk_switch_get_active(GTK_SWITCH(g_bt_visibility_switch));

    if (!run_command_success(enabled
                             ? "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 3s bluetoothctl power on >/dev/null 2>&1; else bluetoothctl power on >/dev/null 2>&1; fi'"
                             : "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 3s bluetoothctl power off >/dev/null 2>&1; else bluetoothctl power off >/dev/null 2>&1; fi'")) {
        g_string_append(issues, _("Could not change Bluetooth power state. "));
    }

    if (!run_command_success(visible
                             ? "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 3s bluetoothctl discoverable on >/dev/null 2>&1; timeout 3s bluetoothctl pairable on >/dev/null 2>&1; else bluetoothctl discoverable on >/dev/null 2>&1; bluetoothctl pairable on >/dev/null 2>&1; fi'"
                             : "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 3s bluetoothctl discoverable off >/dev/null 2>&1; timeout 3s bluetoothctl pairable off >/dev/null 2>&1; else bluetoothctl discoverable off >/dev/null 2>&1; bluetoothctl pairable off >/dev/null 2>&1; fi'")) {
        g_string_append(issues, _("Could not change Bluetooth visibility. "));
    }

    char *selected_mac = selected_device_mac();
    const char *bt_profile = dropdown_selected_value(g_bt_profile_dropdown,
                                                     g_bt_profile_options,
                                                     G_N_ELEMENTS(g_bt_profile_options));
    const char *headphones_profile = dropdown_selected_value(g_headphones_profile_dropdown,
                                                             g_headphones_profile_options,
                                                             G_N_ELEMENTS(g_headphones_profile_options));
    const char *controller_profile = dropdown_selected_value(g_controller_profile_dropdown,
                                                             g_controller_profile_options,
                                                             G_N_ELEMENTS(g_controller_profile_options));

    (void)apply_audio_profile_runtime(selected_mac, bt_profile, issues);
    (void)apply_audio_profile_runtime(selected_mac, headphones_profile, issues);
    (void)apply_controller_profile_runtime(controller_profile, issues);

    gboolean file_transfer_enabled = gtk_switch_get_active(GTK_SWITCH(g_file_transfer_switch));
    const char *transfer_dir = gtk_editable_get_text(GTK_EDITABLE(g_file_transfer_dir_entry));
    (void)apply_file_transfer_runtime(file_transfer_enabled, transfer_dir, issues);

    g_free(selected_mac);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_refresh_devices_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    bluetooth_set_loading(TRUE, _("Refreshing Bluetooth devices..."));
    refresh_bt_devices();
    bluetooth_set_loading(FALSE, NULL);
    status_set(_("Bluetooth device list refreshed"), FALSE);
}

static void on_pair_device_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    const char *mac_text = gtk_editable_get_text(GTK_EDITABLE(g_pair_mac_entry));
    if (!mac_text || !*mac_text) {
        status_set(_("Enter device MAC address to pair."), TRUE);
        return;
    }

    bluetooth_set_loading(TRUE, _("Pairing Bluetooth device..."));

    char *quoted = g_shell_quote(mac_text);
    char *cmd = g_strdup_printf(
        "sh -lc 'mac=%s; printf \"pair %%s\\ntrust %%s\\nconnect %%s\\nquit\\n\" \"$mac\" \"$mac\" \"$mac\" | (if command -v timeout >/dev/null 2>&1; then timeout 6s bluetoothctl >/dev/null 2>&1; else bluetoothctl >/dev/null 2>&1; fi)'",
        quoted);

    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(quoted);

    if (!ok) {
        bluetooth_set_loading(FALSE, NULL);
        status_set(_("Could not pair/connect device with provided MAC."), TRUE);
        return;
    }

    refresh_bt_devices();
    bluetooth_set_loading(FALSE, NULL);
    status_set(_("Pairing command sent."), FALSE);
}

static void on_forget_selected_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    char *mac = selected_device_mac();
    if (!mac) {
        status_set(_("Select a Bluetooth device to remove."), TRUE);
        return;
    }

    bluetooth_set_loading(TRUE, _("Removing selected Bluetooth device..."));

    char *quoted = g_shell_quote(mac);
    char *cmd = g_strdup_printf("sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 4s bluetoothctl remove %s >/dev/null 2>&1; else bluetoothctl remove %s >/dev/null 2>&1; fi'", quoted, quoted);
    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(quoted);
    g_free(mac);

    if (!ok) {
        bluetooth_set_loading(FALSE, NULL);
        status_set(_("Could not remove selected Bluetooth device."), TRUE);
        return;
    }

    refresh_bt_devices();
    bluetooth_set_loading(FALSE, NULL);
    status_set(_("Selected Bluetooth device removed."), FALSE);
}

static void on_reload_bluetooth_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    bluetooth_set_loading(TRUE, _("Reloading Bluetooth settings..."));
    refresh_bt_devices();
    load_bluetooth_config();
    bluetooth_set_loading(FALSE, NULL);
    status_set(_("Bluetooth settings reloaded"), FALSE);
}

static void on_apply_bluetooth_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    bluetooth_set_loading(TRUE, _("Applying Bluetooth settings..."));

    save_bluetooth_config();

    char *issues = apply_runtime_bluetooth();
    refresh_shell_and_top_panel();
    bluetooth_set_loading(FALSE, NULL);
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Bluetooth settings applied"), FALSE);
}

GtkWidget *page_bluetooth_new(void)
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

    GtkWidget *title = gtk_label_new(_("Bluetooth and wireless devices"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Manage pairing, headphones, controllers, file transfer, Bluetooth profiles and device visibility."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *pairing_frame = create_section(_("Pairing and visibility"),
                                              _("Control Bluetooth radio, discoverability and pairing."));
    GtkWidget *pairing_box = gtk_frame_get_child(GTK_FRAME(pairing_frame));

    g_bt_power_switch = gtk_switch_new();
    g_bt_visibility_switch = gtk_switch_new();

    g_devices_dropdown = gtk_drop_down_new(NULL, NULL);
    g_refresh_devices_btn = gtk_button_new_with_label(_("Refresh"));
    g_signal_connect(g_refresh_devices_btn, "clicked", G_CALLBACK(on_refresh_devices_clicked), NULL);

    GtkWidget *devices_control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(devices_control), g_devices_dropdown);
    gtk_box_append(GTK_BOX(devices_control), g_refresh_devices_btn);

    gtk_box_append(GTK_BOX(pairing_box), create_row(_("Bluetooth"), g_bt_power_switch));
    gtk_box_append(GTK_BOX(pairing_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(pairing_box), create_row(_("Device visibility"), g_bt_visibility_switch));
    gtk_box_append(GTK_BOX(pairing_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(pairing_box), create_row(_("Detected devices"), devices_control));

    GtkWidget *pair_control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    g_pair_mac_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_pair_mac_entry), _("AA:BB:CC:DD:EE:FF"));
    gtk_widget_set_size_request(g_pair_mac_entry, 220, -1);
    g_pair_btn = gtk_button_new_with_label(_("Pair"));
    g_signal_connect(g_pair_btn, "clicked", G_CALLBACK(on_pair_device_clicked), NULL);
    g_forget_btn = gtk_button_new_with_label(_("Forget selected"));
    g_signal_connect(g_forget_btn, "clicked", G_CALLBACK(on_forget_selected_clicked), NULL);

    gtk_box_append(GTK_BOX(pair_control), g_pair_mac_entry);
    gtk_box_append(GTK_BOX(pair_control), g_pair_btn);
    gtk_box_append(GTK_BOX(pair_control), g_forget_btn);

    gtk_box_append(GTK_BOX(pairing_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(pairing_box), create_row(_("Device pairing"), pair_control));

    gtk_box_append(GTK_BOX(box), pairing_frame);

    GtkWidget *profiles_frame = create_section(_("Bluetooth profiles"),
                                               _("Configure profiles for headphones and controllers."));
    GtkWidget *profiles_box = gtk_frame_get_child(GTK_FRAME(profiles_frame));

    GtkStringList *bt_profile_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_bt_profile_options); i++) {
        gtk_string_list_append(bt_profile_model, _(g_bt_profile_options[i].label));
    }
    g_bt_profile_dropdown = gtk_drop_down_new(G_LIST_MODEL(bt_profile_model), NULL);
    g_object_unref(bt_profile_model);

    GtkStringList *headphones_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_headphones_profile_options); i++) {
        gtk_string_list_append(headphones_model, _(g_headphones_profile_options[i].label));
    }
    g_headphones_profile_dropdown = gtk_drop_down_new(G_LIST_MODEL(headphones_model), NULL);
    g_object_unref(headphones_model);

    GtkStringList *controller_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_controller_profile_options); i++) {
        gtk_string_list_append(controller_model, _(g_controller_profile_options[i].label));
    }
    g_controller_profile_dropdown = gtk_drop_down_new(G_LIST_MODEL(controller_model), NULL);
    g_object_unref(controller_model);

    gtk_box_append(GTK_BOX(profiles_box), create_row(_("Bluetooth profile"), g_bt_profile_dropdown));
    gtk_box_append(GTK_BOX(profiles_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(profiles_box), create_row(_("Headphones"), g_headphones_profile_dropdown));
    gtk_box_append(GTK_BOX(profiles_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(profiles_box), create_row(_("Controllers"), g_controller_profile_dropdown));

    gtk_box_append(GTK_BOX(box), profiles_frame);

    GtkWidget *transfer_frame = create_section(_("File transfer"),
                                               _("Enable Bluetooth file transfer and set default target directory."));
    GtkWidget *transfer_box = gtk_frame_get_child(GTK_FRAME(transfer_frame));

    g_file_transfer_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(transfer_box), create_row(_("Allow file transfer"), g_file_transfer_switch));
    gtk_box_append(GTK_BOX(transfer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(transfer_box), create_entry_row(_("Transfer directory"), _("$HOME/Downloads"), &g_file_transfer_dir_entry));

    gtk_box_append(GTK_BOX(box), transfer_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    g_reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(g_reload_btn, "clicked", G_CALLBACK(on_reload_bluetooth_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_reload_btn);

    g_apply_btn = gtk_button_new_with_label(_("Apply Bluetooth settings"));
    gtk_widget_add_css_class(g_apply_btn, "suggested-action");
    g_signal_connect(g_apply_btn, "clicked", G_CALLBACK(on_apply_bluetooth_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_loading_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(g_loading_box, GTK_ALIGN_START);
    gtk_widget_set_visible(g_loading_box, FALSE);

    g_loading_spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(g_loading_box), g_loading_spinner);

    g_loading_label = gtk_label_new(_("Loading Bluetooth settings..."));
    gtk_widget_add_css_class(g_loading_label, "row-subtitle");
    gtk_box_append(GTK_BOX(g_loading_box), g_loading_label);

    gtk_box_append(GTK_BOX(box), g_loading_box);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_bt_power_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_bt_visibility_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_file_transfer_switch), TRUE);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_bt_profile_dropdown), find_option_index(g_bt_profile_options, G_N_ELEMENTS(g_bt_profile_options), "balanced"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_headphones_profile_dropdown), find_option_index(g_headphones_profile_options, G_N_ELEMENTS(g_headphones_profile_options), "a2dp"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_controller_profile_dropdown), find_option_index(g_controller_profile_options, G_N_ELEMENTS(g_controller_profile_options), "xinput"));

    gtk_editable_set_text(GTK_EDITABLE(g_file_transfer_dir_entry), "$HOME/Downloads");

    refresh_bt_devices();
    load_bluetooth_config();

    return outer_scroll;
}
