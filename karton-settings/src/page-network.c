#include "page-network.h"

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

static const struct option_value g_ipv4_mode_options[] = {
    { N_("Automatic (DHCP)"), "auto" },
    { N_("Manual"), "manual" },
    { N_("Disabled"), "off" },
};

static const struct option_value g_ipv6_mode_options[] = {
    { N_("Automatic"), "auto" },
    { N_("Manual"), "manual" },
    { N_("Link-local only"), "link-local" },
    { N_("Disabled"), "off" },
};

static GtkWidget *g_wifi_switch = NULL;
static GtkWidget *g_wifi_network_dropdown = NULL;
static GPtrArray *g_wifi_network_ssids = NULL;
static GPtrArray *g_wifi_network_security = NULL;
static GtkWidget *g_ethernet_switch = NULL;
static GtkWidget *g_vpn_switch = NULL;
static GtkWidget *g_proxy_entry = NULL;
static GtkWidget *g_hotspot_switch = NULL;
static GtkWidget *g_hotspot_ssid_entry = NULL;
static GtkWidget *g_dns_entry = NULL;
static GtkWidget *g_ipv4_mode_dropdown = NULL;
static GtkWidget *g_ipv6_mode_dropdown = NULL;
static GtkWidget *g_hotspot_row = NULL;
static GtkWidget *g_vpn_row = NULL;
static GtkWidget *g_vpn_name_entry = NULL;
static GtkWidget *g_firewall_switch = NULL;
static GtkWidget *g_sharing_switch = NULL;
static GtkWidget *g_sharing_row = NULL;
static GtkWidget *g_bt_tether_switch = NULL;
static GtkWidget *g_bt_tether_row = NULL;
static GtkWidget *g_virtual_nat_switch = NULL;
static GtkWidget *g_status_label = NULL;

static void save_network_config(void);
static void refresh_shell_and_top_panel(void);

struct wifi_password_dialog_state {
    GtkWidget *window;
    GtkWidget *entry;
    char *ssid;
};

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

static gboolean run_command_success(const char *command);

static gboolean run_sudo_command(const char *cmd) {
    char *password = NULL;
    char *prompt_cmd = g_strdup_printf("karton-password-dialog --title=\"%s\" --text=\"%s\"",
        _("Authentication Required"), _("Wymagane hasło administratora (sudo) do zatwierdzenia ustawień"));
    run_command_capture(prompt_cmd, &password, NULL, NULL);
    g_free(prompt_cmd);
    
    if (!password || !*password) {
        g_free(password);
        return FALSE; // Anulowano
    }
    g_strstrip(password);

    char *escaped_pwd = g_shell_quote(password);
    g_free(password);
    
    char *full_cmd = g_strdup_printf("sh -c 'echo %s | sudo -S %s'", escaped_pwd, cmd);
    gboolean ok = run_command_success(full_cmd);
    
    g_free(full_cmd);
    g_free(escaped_pwd);
    return ok;
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

static char *network_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "network.conf", NULL);
}

static void clear_wifi_network_cache(void)
{
    if (!g_wifi_network_ssids) {
        g_wifi_network_ssids = g_ptr_array_new_with_free_func(g_free);
    }
    if (!g_wifi_network_security) {
        g_wifi_network_security = g_ptr_array_new_with_free_func(g_free);
    }

    g_ptr_array_set_size(g_wifi_network_ssids, 0);
    g_ptr_array_set_size(g_wifi_network_security, 0);
}

static void append_wifi_network_choice(GtkStringList *model,
                                       const char *ssid,
                                       const char *signal,
                                       const char *security)
{
    const char *safe_ssid = (ssid && *ssid) ? ssid : _("Hidden network");
    const char *safe_signal = (signal && *signal) ? signal : "?";
    const char *safe_security = (security && *security) ? security : _("open");

    char *label = g_strdup_printf("%s (%s%%, %s)", safe_ssid, safe_signal, safe_security);
    gtk_string_list_append(model, label);
    g_free(label);

    g_ptr_array_add(g_wifi_network_ssids, g_strdup(safe_ssid));
    g_ptr_array_add(g_wifi_network_security, g_strdup(safe_security));
}

static int selected_wifi_network_index(void)
{
    if (!g_wifi_network_dropdown || !g_wifi_network_ssids) {
        return -1;
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_wifi_network_dropdown));
    if (idx >= g_wifi_network_ssids->len) {
        return -1;
    }

    return (int)idx;
}

static const char *selected_wifi_ssid(void)
{
    int idx = selected_wifi_network_index();
    if (idx < 0) {
        return NULL;
    }

    return g_ptr_array_index(g_wifi_network_ssids, (guint)idx);
}

static const char *selected_wifi_security(void)
{
    int idx = selected_wifi_network_index();
    if (idx < 0) {
        return NULL;
    }

    return g_ptr_array_index(g_wifi_network_security, (guint)idx);
}

static gboolean wifi_security_requires_password(const char *security)
{
    if (!security || !*security) {
        return FALSE;
    }

    return g_strcmp0(security, "--") != 0
        && g_ascii_strcasecmp(security, "NONE") != 0
        && g_ascii_strcasecmp(security, _("open")) != 0;
}

static gboolean connect_wifi_with_password(const char *ssid,
                                           const char *password,
                                           char **error_out)
{
    if (error_out) {
        *error_out = NULL;
    }

    if (!ssid || !*ssid || !command_is_available("nmcli")) {
        if (error_out) {
            *error_out = g_strdup(_("NetworkManager CLI (nmcli) is not available"));
        }
        return FALSE;
    }

    char *ssid_quoted = g_shell_quote(ssid);
    char *cmd = NULL;

    if (password && *password) {
        char *password_quoted = g_shell_quote(password);
        cmd = g_strdup_printf(
            "sh -lc 'nmcli --wait 8 dev wifi connect %s password %s 2>&1'",
            ssid_quoted,
            password_quoted);
        g_free(password_quoted);
    } else {
        cmd = g_strdup_printf(
            "sh -lc 'nmcli --wait 8 connection up id %s 2>&1 || nmcli --wait 8 dev wifi connect %s 2>&1'",
            ssid_quoted,
            ssid_quoted);
    }

    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;
    gboolean ok = run_command_capture(cmd, &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;

    if (!ok && error_out) {
        const char *msg = NULL;
        if (stderr_data && *stderr_data) {
            msg = stderr_data;
        } else if (stdout_data && *stdout_data) {
            msg = stdout_data;
        } else {
            msg = _("Could not connect to selected network");
        }

        *error_out = g_strdup(msg);
        g_strstrip(*error_out);
    }

    g_free(stdout_data);
    g_free(stderr_data);
    g_free(cmd);
    g_free(ssid_quoted);
    return ok;
}

static void refresh_wifi_networks(gboolean force_rescan)
{
    GtkStringList *model = gtk_string_list_new(NULL);
    gboolean appended = FALSE;
    guint connected_idx = 0;
    clear_wifi_network_cache();

    if (command_is_available("nmcli")) {
        char *stdout_data = NULL;
        if (force_rescan) {
            run_command_success("sh -lc 'nmcli dev wifi rescan >/dev/null 2>&1'");
        }
        char *cmd = g_strdup_printf(
            "sh -lc 'nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY dev wifi list --rescan no 2>/dev/null | sort -t: -k3 -nr | head -n 30'");
        gboolean ok = run_command_capture(
            cmd,
            &stdout_data,
            NULL,
            NULL);
        g_free(cmd);

        guint valid_count = 0;

        if (ok && stdout_data && *stdout_data) {
            gchar **lines = g_strsplit(stdout_data, "\n", -1);
            for (guint i = 0; lines[i] != NULL; i++) {
                if (!lines[i][0]) {
                    continue;
                }

                gchar **parts = g_strsplit(lines[i], ":", 4);
                const char *inuse = parts[0] ? parts[0] : "";
                const char *ssid = parts[1] && *parts[1] ? parts[1] : "";
                const char *signal = parts[2] && *parts[2] ? parts[2] : "?";
                const char *security = parts[3] && *parts[3] ? parts[3] : _("open");

                if (!ssid[0]) {
                    g_strfreev(parts);
                    continue;
                }

                if (g_strrstr(inuse, "*")) {
                    connected_idx = valid_count;
                }

                append_wifi_network_choice(model, ssid, signal, security);

                valid_count++;
                appended = TRUE;
                g_strfreev(parts);
            }
            g_strfreev(lines);
        }

        g_free(stdout_data);
    }

    if (!appended) {
        gtk_string_list_append(model, _("No networks found"));
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(g_wifi_network_dropdown), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_wifi_network_dropdown), connected_idx);
    g_object_unref(model);
}

struct wifi_connect_task_data {
    char *ssid;
    char *password;
    gboolean success;
    char *error_text;
};

static void wifi_connect_task_data_free(gpointer user_data)
{
    struct wifi_connect_task_data *data = user_data;
    if (data) {
        g_free(data->ssid);
        g_free(data->password);
        g_free(data->error_text);
        g_free(data);
    }
}

static void connect_wifi_thread_func(GTask *task,
                                       gpointer source_object,
                                       gpointer task_data,
                                       GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;

    struct wifi_connect_task_data *data = task_data;
    data->success = connect_wifi_with_password(data->ssid, data->password, &data->error_text);
    g_task_return_boolean(task, data->success);
}

static void on_wifi_connected(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)source_object;
    (void)user_data;
    GTask *task = G_TASK(res);
    struct wifi_connect_task_data *data = g_task_get_task_data(task);
    
    if (!data->success) {
        status_set(data->error_text && *data->error_text ? data->error_text : _("Could not connect to selected network"), TRUE);
    } else {
        save_network_config();
        refresh_wifi_networks(TRUE);
        refresh_shell_and_top_panel();

        char *msg = g_strdup_printf(_("Connected to %s"), data->ssid);
        status_set(msg, FALSE);
        g_free(msg);
    }
    
    g_object_unref(task);
}

static void connect_selected_wifi_with_optional_password(const char *password)
{
    const char *ssid = selected_wifi_ssid();
    if (!ssid || !*ssid) {
        status_set(_("No Wi-Fi network selected"), TRUE);
        return;
    }

    if (!gtk_switch_get_active(GTK_SWITCH(g_wifi_switch))) {
        if (!run_command_success("sh -lc 'nmcli radio wifi on >/dev/null 2>&1'")) {
            status_set(_("Could not enable Wi-Fi radio"), TRUE);
            return;
        }
        gtk_switch_set_active(GTK_SWITCH(g_wifi_switch), TRUE);
    }

    status_set(_("Connecting..."), FALSE);

    struct wifi_connect_task_data *data = g_new0(struct wifi_connect_task_data, 1);
    data->ssid = g_strdup(ssid);
    data->password = g_strdup(password);

    GTask *task = g_task_new(NULL, NULL, on_wifi_connected, NULL);
    g_task_set_task_data(task, data, wifi_connect_task_data_free);
    g_task_run_in_thread(task, connect_wifi_thread_func);
}

static void on_wifi_password_dialog_cancel(GtkButton *btn, gpointer user_data)
{
    (void)btn;

    struct wifi_password_dialog_state *state = user_data;
    if (!state) {
        return;
    }

    gtk_window_destroy(GTK_WINDOW(state->window));
}

static void on_wifi_password_dialog_connect(GtkButton *btn, gpointer user_data)
{
    (void)btn;

    struct wifi_password_dialog_state *state = user_data;
    if (!state) {
        return;
    }

    const char *password = gtk_editable_get_text(GTK_EDITABLE(state->entry));
    if (!password || !*password) {
        status_set(_("Enter Wi-Fi password"), TRUE);
        return;
    }

    gtk_window_destroy(GTK_WINDOW(state->window));
    connect_selected_wifi_with_optional_password(password);
}

static void on_connect_wifi_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (!command_is_available("nmcli")) {
        status_set(_("NetworkManager CLI (nmcli) not found"), TRUE);
        return;
    }

    const char *ssid = selected_wifi_ssid();
    const char *security = selected_wifi_security();
    if (!ssid || !*ssid) {
        status_set(_("No Wi-Fi network selected"), TRUE);
        return;
    }

    gboolean needs_password = wifi_security_requires_password(security);
    if (!needs_password) {
        connect_selected_wifi_with_optional_password(NULL);
        return;
    }

    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(g_wifi_network_dropdown)));
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Connect to Wi-Fi"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 170);
    if (root && GTK_IS_WINDOW(root)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root));
    }

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), content);

    char *prompt = g_strdup_printf(_("Enter password for %s"), ssid);
    GtkWidget *label = gtk_label_new(prompt);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    g_free(prompt);
    gtk_box_append(GTK_BOX(content), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Wi-Fi password"));
    gtk_box_append(GTK_BOX(content), entry);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(content), actions);

    GtkWidget *cancel_btn = gtk_button_new_with_label(_("Cancel"));
    gtk_box_append(GTK_BOX(actions), cancel_btn);

    GtkWidget *connect_btn = gtk_button_new_with_label(_("Connect"));
    gtk_widget_add_css_class(connect_btn, "suggested-action");
    gtk_box_append(GTK_BOX(actions), connect_btn);

    struct wifi_password_dialog_state *state = g_new0(struct wifi_password_dialog_state, 1);
    state->window = dialog;
    state->entry = entry;
    state->ssid = g_strdup(ssid);

    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_wifi_password_dialog_cancel), state);
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_wifi_password_dialog_connect), state);
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_free), state->ssid);
    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_free), state);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void update_dynamic_visibility(void) {
    if (g_hotspot_row) {
        gtk_widget_set_visible(g_hotspot_row, gtk_switch_get_active(GTK_SWITCH(g_hotspot_switch)));
    }
    if (g_vpn_row) {
        gtk_widget_set_visible(g_vpn_row, gtk_switch_get_active(GTK_SWITCH(g_vpn_switch)));
    }
    if (g_sharing_row) {
        gboolean has_ethernet = command_is_available("nmcli") && run_command_success("sh -lc \"nmcli -t -f TYPE device | grep -q '^ethernet$'\"");
        gtk_widget_set_visible(g_sharing_row, has_ethernet);
    }
    if (g_bt_tether_row) {
        gboolean has_bt = command_is_available("nmcli") && run_command_success("sh -lc \"nmcli -t -f TYPE device | grep -q '^bt$'\"");
        gtk_widget_set_visible(g_bt_tether_row, has_bt);
    }
}

static void on_dynamic_switch_changed(GObject *gobject, GParamSpec *pspec, gpointer user_data) {
    (void)gobject; (void)pspec; (void)user_data;
    update_dynamic_visibility();
}

static void save_network_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "network", "wifi", gtk_switch_get_active(GTK_SWITCH(g_wifi_switch)));
    g_key_file_set_boolean(kf, "network", "ethernet", gtk_switch_get_active(GTK_SWITCH(g_ethernet_switch)));
    g_key_file_set_boolean(kf, "network", "vpn", gtk_switch_get_active(GTK_SWITCH(g_vpn_switch)));
    g_key_file_set_boolean(kf, "network", "virtual_nat", gtk_switch_get_active(GTK_SWITCH(g_virtual_nat_switch)));
    g_key_file_set_boolean(kf, "network", "hotspot", gtk_switch_get_active(GTK_SWITCH(g_hotspot_switch)));
    g_key_file_set_boolean(kf, "network", "firewall", gtk_switch_get_active(GTK_SWITCH(g_firewall_switch)));
    g_key_file_set_boolean(kf, "network", "sharing", gtk_switch_get_active(GTK_SWITCH(g_sharing_switch)));
    g_key_file_set_boolean(kf, "network", "bt_tether", gtk_switch_get_active(GTK_SWITCH(g_bt_tether_switch)));

    g_key_file_set_integer(kf, "network", "ipv4_mode_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_ipv4_mode_dropdown)));
    g_key_file_set_integer(kf, "network", "ipv6_mode_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_ipv6_mode_dropdown)));

    const char *proxy = gtk_editable_get_text(GTK_EDITABLE(g_proxy_entry));
    const char *hotspot_ssid = gtk_editable_get_text(GTK_EDITABLE(g_hotspot_ssid_entry));
    const char *dns = gtk_editable_get_text(GTK_EDITABLE(g_dns_entry));
    const char *vpn_name = gtk_editable_get_text(GTK_EDITABLE(g_vpn_name_entry));

    g_key_file_set_string(kf, "network", "proxy", proxy ? proxy : "");
    g_key_file_set_string(kf, "network", "hotspot_ssid", hotspot_ssid ? hotspot_ssid : "");
    g_key_file_set_string(kf, "network", "dns", dns ? dns : "");
    g_key_file_set_string(kf, "network", "vpn_name", vpn_name ? vpn_name : "");

    char *network_name = dropdown_selected_text(g_wifi_network_dropdown);
    g_key_file_set_string(kf, "network", "wifi_network", network_name ? network_name : "");
    g_free(network_name);

    const char *ipv4 = dropdown_selected_value(g_ipv4_mode_dropdown,
                                               g_ipv4_mode_options,
                                               G_N_ELEMENTS(g_ipv4_mode_options));
    const char *ipv6 = dropdown_selected_value(g_ipv6_mode_dropdown,
                                               g_ipv6_mode_options,
                                               G_N_ELEMENTS(g_ipv6_mode_options));
    g_key_file_set_string(kf, "network", "ipv4_mode", ipv4 ? ipv4 : "auto");
    g_key_file_set_string(kf, "network", "ipv6_mode", ipv6 ? ipv6 : "auto");

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = network_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_network_config(void)
{
    char *path = network_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean wifi = g_key_file_get_boolean(kf, "network", "wifi", &error);
    if (error) {
        g_clear_error(&error);
        wifi = TRUE;
    }

    gboolean ethernet = g_key_file_get_boolean(kf, "network", "ethernet", &error);
    if (error) {
        g_clear_error(&error);
        ethernet = TRUE;
    }

    gboolean vpn = g_key_file_get_boolean(kf, "network", "vpn", &error);
    if (error) {
        g_clear_error(&error);
        vpn = FALSE;
    }

    gboolean virtual_nat = g_key_file_get_boolean(kf, "network", "virtual_nat", &error);
    if (error) {
        g_clear_error(&error);
        virtual_nat = FALSE;
    }

    gboolean hotspot = g_key_file_get_boolean(kf, "network", "hotspot", &error);
    if (error) {
        g_clear_error(&error);
        hotspot = FALSE;
    }

    gboolean firewall = g_key_file_get_boolean(kf, "network", "firewall", &error);
    if (error) {
        g_clear_error(&error);
        firewall = TRUE;
    }

    gboolean sharing = g_key_file_get_boolean(kf, "network", "sharing", &error);
    if (error) {
        g_clear_error(&error);
        sharing = FALSE;
    }

    gboolean bt_tether = g_key_file_get_boolean(kf, "network", "bt_tether", &error);
    if (error) {
        g_clear_error(&error);
        bt_tether = FALSE;
    }

    int ipv4_idx = g_key_file_get_integer(kf, "network", "ipv4_mode_idx", &error);
    if (error) {
        g_clear_error(&error);
        ipv4_idx = 0;
    }

    int ipv6_idx = g_key_file_get_integer(kf, "network", "ipv6_mode_idx", &error);
    if (error) {
        g_clear_error(&error);
        ipv6_idx = 0;
    }

    char *proxy = g_key_file_get_string(kf, "network", "proxy", &error);
    if (error) {
        g_clear_error(&error);
        proxy = g_strdup("");
    }

    char *hotspot_ssid = g_key_file_get_string(kf, "network", "hotspot_ssid", &error);
    if (error) {
        g_clear_error(&error);
        hotspot_ssid = g_strdup("Karton Hotspot");
    }

    char *vpn_name = g_key_file_get_string(kf, "network", "vpn_name", &error);
    if (error) {
        g_clear_error(&error);
        vpn_name = g_strdup("my-vpn-config");
    }

    char *dns = g_key_file_get_string(kf, "network", "dns", &error);
    if (error) {
        g_clear_error(&error);
        dns = g_strdup("");
    }

    ipv4_idx = clamp_int(ipv4_idx, 0, (int)G_N_ELEMENTS(g_ipv4_mode_options) - 1);
    ipv6_idx = clamp_int(ipv6_idx, 0, (int)G_N_ELEMENTS(g_ipv6_mode_options) - 1);

    gtk_switch_set_active(GTK_SWITCH(g_wifi_switch), wifi);
    gtk_switch_set_active(GTK_SWITCH(g_ethernet_switch), ethernet);
    gtk_switch_set_active(GTK_SWITCH(g_vpn_switch), vpn);
    gtk_switch_set_active(GTK_SWITCH(g_virtual_nat_switch), virtual_nat);
    gtk_switch_set_active(GTK_SWITCH(g_hotspot_switch), hotspot);
    gtk_switch_set_active(GTK_SWITCH(g_firewall_switch), firewall);
    gtk_switch_set_active(GTK_SWITCH(g_sharing_switch), sharing);
    gtk_switch_set_active(GTK_SWITCH(g_bt_tether_switch), bt_tether);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_ipv4_mode_dropdown), (guint)ipv4_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_ipv6_mode_dropdown), (guint)ipv6_idx);

    gtk_editable_set_text(GTK_EDITABLE(g_proxy_entry), proxy);
    gtk_editable_set_text(GTK_EDITABLE(g_hotspot_ssid_entry), hotspot_ssid);
    gtk_editable_set_text(GTK_EDITABLE(g_dns_entry), dns);
    gtk_editable_set_text(GTK_EDITABLE(g_vpn_name_entry), vpn_name);

    g_free(proxy);
    g_free(hotspot_ssid);
    g_free(vpn_name);
    g_free(dns);
    g_key_file_unref(kf);
    g_free(path);

    update_dynamic_visibility();
}

static char *apply_runtime_network(void)
{
    GString *issues = g_string_new(NULL);

    if (command_is_available("nmcli")) {
        gboolean wifi_enabled = gtk_switch_get_active(GTK_SWITCH(g_wifi_switch));
        if (!run_command_success(wifi_enabled
                                 ? "sh -lc 'nmcli radio wifi on >/dev/null 2>&1'"
                                 : "sh -lc 'nmcli radio wifi off >/dev/null 2>&1'")) {
            g_string_append(issues, _("Could not switch Wi-Fi radio state. "));
        }

        if (gtk_switch_get_active(GTK_SWITCH(g_hotspot_switch))) {
            const char *ssid_text = gtk_editable_get_text(GTK_EDITABLE(g_hotspot_ssid_entry));
            const char *ssid = (ssid_text && *ssid_text) ? ssid_text : "Karton Hotspot";
            char *quoted = g_shell_quote(ssid);
            char *cmd = g_strdup_printf("sh -lc 'nmcli dev wifi hotspot ssid %s >/dev/null 2>&1'", quoted);
            if (!run_command_success(cmd)) {
                g_string_append(issues, _("Could not start hotspot with current settings. "));
            }
            g_free(cmd);
            g_free(quoted);
        } else {
            run_command_success("sh -lc 'nmcli connection down Hotspot >/dev/null 2>&1 || true'");
            run_command_success("sh -lc 'nmcli connection down \"Karton Hotspot\" >/dev/null 2>&1 || true'");
        }

        if (gtk_switch_get_active(GTK_SWITCH(g_vpn_switch))) {
            const char *vpn_text = gtk_editable_get_text(GTK_EDITABLE(g_vpn_name_entry));
            if (vpn_text && *vpn_text) {
                char *quoted = g_shell_quote(vpn_text);
                char *cmd = g_strdup_printf("sh -lc 'nmcli connection up %s >/dev/null 2>&1'", quoted);
                if (!run_command_success(cmd)) {
                    g_string_append(issues, _("Could not connect to the specified VPN. "));
                }
                g_free(cmd);
                g_free(quoted);
            }
        } else {
            const char *vpn_text = gtk_editable_get_text(GTK_EDITABLE(g_vpn_name_entry));
            if (vpn_text && *vpn_text) {
                char *quoted = g_shell_quote(vpn_text);
                char *cmd = g_strdup_printf("sh -lc 'nmcli connection down %s >/dev/null 2>&1'", quoted);
                run_command_success(cmd);
                g_free(cmd);
                g_free(quoted);
            }
        }
    } else {
        g_string_append(issues, _("NetworkManager CLI (nmcli) not found. Runtime network apply is limited. "));
    }

    if (command_is_available("ufw")) {
        gboolean firewall_enabled = gtk_switch_get_active(GTK_SWITCH(g_firewall_switch));
        if (firewall_enabled) {
            run_sudo_command("ufw enable >/dev/null 2>&1");
        } else {
            run_sudo_command("ufw disable >/dev/null 2>&1");
        }
    }

    if (command_is_available("virsh")) {
        gboolean virtual_nat_enabled = gtk_switch_get_active(GTK_SWITCH(g_virtual_nat_switch));
        if (virtual_nat_enabled) {
            run_sudo_command("systemctl start libvirtd >/dev/null 2>&1");
            if (!run_command_success("sh -lc 'virsh net-start default >/dev/null 2>&1'")) {
                // If it fails, it might already be started, so we don't strictly report as error, or we could.
                run_command_success("sh -lc 'virsh net-autostart default >/dev/null 2>&1'");
            } else {
                run_command_success("sh -lc 'virsh net-autostart default >/dev/null 2>&1'");
            }
        } else {
            run_command_success("sh -lc 'virsh net-autostart default --disable >/dev/null 2>&1'");
            run_command_success("sh -lc 'virsh net-destroy default >/dev/null 2>&1'");
        }
    }

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void refresh_shell_and_top_panel(void)
{
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -USR1 -x karton-top-panel >/dev/null 2>&1 || true; pkill -USR1 -x karton-side-dock >/dev/null 2>&1 || true'");
}

static void on_refresh_wifi_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_wifi_networks(TRUE);
    status_set(_("Wi-Fi networks refreshed"), FALSE);
}

static void on_reload_network_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_wifi_networks(FALSE);
    load_network_config();
    status_set(_("Network settings reloaded"), FALSE);
}

static void on_apply_network_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_network_config();

    char *issues = apply_runtime_network();
    refresh_shell_and_top_panel();
    if (issues) {
        status_set(issues, FALSE);
        g_free(issues);
        return;
    }

    status_set(_("Network settings applied"), FALSE);
}

GtkWidget *page_network_new(void)
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

    GtkWidget *title = gtk_label_new(_("Network and internet"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Configure Wi-Fi, Ethernet, VPN, proxy, hotspot, DNS and connection sharing."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *connections_frame = create_section(_("Connections"),
                                                  _("Manage Wi-Fi, Ethernet, VPN and hotspot connectivity."));
    GtkWidget *connections_box = gtk_frame_get_child(GTK_FRAME(connections_frame));

    g_wifi_switch = gtk_switch_new();
    g_ethernet_switch = gtk_switch_new();
    g_vpn_switch = gtk_switch_new();
    g_hotspot_switch = gtk_switch_new();
    g_virtual_nat_switch = gtk_switch_new();

    g_wifi_network_dropdown = gtk_drop_down_new(NULL, NULL);
    GtkWidget *scan_btn = gtk_button_new_with_label(_("Scan"));
    GtkWidget *connect_btn = gtk_button_new_with_label(_("Connect"));
    g_signal_connect(scan_btn, "clicked", G_CALLBACK(on_refresh_wifi_clicked), NULL);
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_connect_wifi_clicked), NULL);

    g_signal_connect(g_hotspot_switch, "notify::active", G_CALLBACK(on_dynamic_switch_changed), NULL);
    g_signal_connect(g_vpn_switch, "notify::active", G_CALLBACK(on_dynamic_switch_changed), NULL);

    GtkWidget *wifi_control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(wifi_control), g_wifi_network_dropdown);
    gtk_box_append(GTK_BOX(wifi_control), scan_btn);
    gtk_box_append(GTK_BOX(wifi_control), connect_btn);

    gtk_box_append(GTK_BOX(connections_box), create_row(_("Wi-Fi"), g_wifi_switch));
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(connections_box), create_row(_("Available networks"), wifi_control));
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(connections_box), create_row(_("Ethernet"), g_ethernet_switch));
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(connections_box), create_row(_("VPN"), g_vpn_switch));
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(connections_box), create_row(_("Virtual NAT (QEMU/libvirt)"), g_virtual_nat_switch));
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    g_vpn_row = create_entry_row(_("VPN config name"), _("my-vpn-config"), &g_vpn_name_entry);
    gtk_box_append(GTK_BOX(connections_box), g_vpn_row);
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    gtk_box_append(GTK_BOX(connections_box), create_row(_("Hotspot"), g_hotspot_switch));

    g_hotspot_row = create_entry_row(_("Hotspot name"), _("Karton Hotspot"), &g_hotspot_ssid_entry);
    gtk_box_append(GTK_BOX(connections_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(connections_box), g_hotspot_row);

    gtk_box_append(GTK_BOX(box), connections_frame);

    GtkWidget *routing_frame = create_section(_("Routing and addressing"),
                                              _("Set proxy, DNS and IPv4/IPv6 behavior."));
    GtkWidget *routing_box = gtk_frame_get_child(GTK_FRAME(routing_frame));

    GtkStringList *ipv4_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_ipv4_mode_options); i++) {
        gtk_string_list_append(ipv4_model, _(g_ipv4_mode_options[i].label));
    }
    g_ipv4_mode_dropdown = gtk_drop_down_new(G_LIST_MODEL(ipv4_model), NULL);
    g_object_unref(ipv4_model);

    GtkStringList *ipv6_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_ipv6_mode_options); i++) {
        gtk_string_list_append(ipv6_model, _(g_ipv6_mode_options[i].label));
    }
    g_ipv6_mode_dropdown = gtk_drop_down_new(G_LIST_MODEL(ipv6_model), NULL);
    g_object_unref(ipv6_model);

    gtk_box_append(GTK_BOX(routing_box), create_entry_row(_("Proxy"), _("http://proxy.local:8080"), &g_proxy_entry));
    gtk_box_append(GTK_BOX(routing_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(routing_box), create_entry_row(_("DNS servers"), _("1.1.1.1, 8.8.8.8"), &g_dns_entry));
    gtk_box_append(GTK_BOX(routing_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(routing_box), create_row(_("IPv4"), g_ipv4_mode_dropdown));
    gtk_box_append(GTK_BOX(routing_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(routing_box), create_row(_("IPv6"), g_ipv6_mode_dropdown));

    gtk_box_append(GTK_BOX(box), routing_frame);

    GtkWidget *security_frame = create_section(_("Security and sharing"),
                                               _("Control firewall, network sharing and Bluetooth tethering."));
    GtkWidget *security_box = gtk_frame_get_child(GTK_FRAME(security_frame));

    g_firewall_switch = gtk_switch_new();
    g_sharing_switch = gtk_switch_new();
    g_bt_tether_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(security_box), create_row(_("Firewall"), g_firewall_switch));
    gtk_box_append(GTK_BOX(security_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    g_sharing_row = create_row(_("Network sharing (Cable)"), g_sharing_switch);
    gtk_box_append(GTK_BOX(security_box), g_sharing_row);
    gtk_box_append(GTK_BOX(security_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    g_bt_tether_row = create_row(_("Bluetooth tethering"), g_bt_tether_switch);
    gtk_box_append(GTK_BOX(security_box), g_bt_tether_row);

    gtk_box_append(GTK_BOX(box), security_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_network_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply network settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_network_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_wifi_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_ethernet_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_vpn_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_virtual_nat_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_hotspot_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_firewall_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_sharing_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_bt_tether_switch), FALSE);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_ipv4_mode_dropdown), find_option_index(g_ipv4_mode_options, G_N_ELEMENTS(g_ipv4_mode_options), "auto"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_ipv6_mode_dropdown), find_option_index(g_ipv6_mode_options, G_N_ELEMENTS(g_ipv6_mode_options), "auto"));
    gtk_editable_set_text(GTK_EDITABLE(g_hotspot_ssid_entry), "Karton Hotspot");
    gtk_editable_set_text(GTK_EDITABLE(g_vpn_name_entry), "my-vpn-config");

    refresh_wifi_networks(FALSE);
    load_network_config();

    return outer_scroll;
}
