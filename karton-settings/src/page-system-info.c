#include "page-system-info.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>

#define _(s) gettext(s)

static GtkWidget *g_temperature_switch = NULL;
static GtkWidget *g_disk_usage_switch = NULL;
static GtkWidget *g_logs_switch = NULL;
static GtkWidget *g_cpu_value = NULL;
static GtkWidget *g_ram_value = NULL;
static GtkWidget *g_gpu_value = NULL;
static GtkWidget *g_kernel_value = NULL;
static GtkWidget *g_temperature_value = NULL;
static GtkWidget *g_disk_usage_value = NULL;
static GtkWidget *g_logs_view = NULL;
static GtkTextBuffer *g_logs_buffer = NULL;
static GtkWidget *g_status_label = NULL;
static guint g_metrics_timer_id = 0;

static gboolean command_is_available(const char *name)
{
    char *tool = g_find_program_in_path(name);
    if (!tool) {
        return FALSE;
    }

    g_free(tool);
    return TRUE;
}

static gboolean run_command_success(const char *command)
{
    int wait_status = 0;
    gboolean ok = g_spawn_command_line_sync(command, NULL, NULL, &wait_status, NULL);
    if (!ok) {
        return FALSE;
    }

    return g_spawn_check_wait_status(wait_status, NULL);
}

static gboolean run_command_capture(const char *command, char **stdout_out)
{
    gchar *stdout_data = NULL;
    int wait_status = 0;
    gboolean ok = g_spawn_command_line_sync(command,
                                            stdout_out ? &stdout_data : NULL,
                                            NULL,
                                            &wait_status,
                                            NULL);
    if (!ok) {
        g_free(stdout_data);
        return FALSE;
    }

    if (stdout_out) {
        *stdout_out = stdout_data;
    } else {
        g_free(stdout_data);
    }

    return g_spawn_check_wait_status(wait_status, NULL);
}

static GtkWidget *create_row(const char *title, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(title);
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

static char *system_info_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "system-info.conf", NULL);
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

static void save_system_info_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "system_info", "temperature", gtk_switch_get_active(GTK_SWITCH(g_temperature_switch)));
    g_key_file_set_boolean(kf, "system_info", "disk_usage", gtk_switch_get_active(GTK_SWITCH(g_disk_usage_switch)));
    g_key_file_set_boolean(kf, "system_info", "system_logs", gtk_switch_get_active(GTK_SWITCH(g_logs_switch)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = system_info_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_system_info_config(void)
{
    char *path = system_info_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean temperature = g_key_file_get_boolean(kf, "system_info", "temperature", &error);
    if (error) {
        g_clear_error(&error);
        temperature = TRUE;
    }

    gboolean disk_usage = g_key_file_get_boolean(kf, "system_info", "disk_usage", &error);
    if (error) {
        g_clear_error(&error);
        disk_usage = TRUE;
    }

    gboolean system_logs = g_key_file_get_boolean(kf, "system_info", "system_logs", &error);
    if (error) {
        g_clear_error(&error);
        system_logs = TRUE;
    }

    gtk_switch_set_active(GTK_SWITCH(g_temperature_switch), temperature);
    gtk_switch_set_active(GTK_SWITCH(g_disk_usage_switch), disk_usage);
    gtk_switch_set_active(GTK_SWITCH(g_logs_switch), system_logs);

    g_key_file_unref(kf);
    g_free(path);
}

static char *capture_line(const char *command, const char *fallback)
{
    char *out = NULL;
    if (!run_command_capture(command, &out) || !out || !*out) {
        g_free(out);
        return g_strdup(fallback);
    }

    g_strstrip(out);
    if (!*out) {
        g_free(out);
        return g_strdup(fallback);
    }

    return out;
}

static void refresh_system_metrics(void)
{
    char *cpu = capture_line("sh -lc \"awk -F: '/model name/{sub(/^ +/,\\\"\\\",$2); print $2; exit}' /proc/cpuinfo\"", "n/a");
    char *ram = capture_line("sh -lc \"free -h | awk '/Mem:/{print $3 \\\" / \\\" $2}'\"", "n/a");

    char *gpu = NULL;
    if (command_is_available("lspci")) {
        gpu = capture_line("sh -lc \"lspci | grep -Ei 'vga|3d|display' | head -n1\"", "n/a");
    } else {
        gpu = g_strdup("n/a");
    }

    char *kernel = capture_line("sh -lc 'uname -r'", "n/a");
    char *temp = capture_line("sh -lc 'if [ -r /sys/class/thermal/thermal_zone0/temp ]; then v=$(cat /sys/class/thermal/thermal_zone0/temp); printf \"%s mC\" \"$v\"; else echo n/a; fi'", "n/a");
    char *disk = capture_line("sh -lc \"df -h / | awk 'NR==2{print $3 \\\" / \\\" $2 \\\" (\\\" $5 \\\")\\\"}'\"", "n/a");

    gtk_label_set_text(GTK_LABEL(g_cpu_value), cpu);
    gtk_label_set_text(GTK_LABEL(g_ram_value), ram);
    gtk_label_set_text(GTK_LABEL(g_gpu_value), gpu);
    gtk_label_set_text(GTK_LABEL(g_kernel_value), kernel);

    if (gtk_switch_get_active(GTK_SWITCH(g_temperature_switch))) {
        gtk_label_set_text(GTK_LABEL(g_temperature_value), temp);
    } else {
        gtk_label_set_text(GTK_LABEL(g_temperature_value), _("Disabled"));
    }

    if (gtk_switch_get_active(GTK_SWITCH(g_disk_usage_switch))) {
        gtk_label_set_text(GTK_LABEL(g_disk_usage_value), disk);
    } else {
        gtk_label_set_text(GTK_LABEL(g_disk_usage_value), _("Disabled"));
    }

    if (gtk_switch_get_active(GTK_SWITCH(g_logs_switch))) {
        char *logs = NULL;
        if (command_is_available("journalctl")) {
            (void)run_command_capture("sh -lc 'journalctl -n 20 --no-pager 2>/dev/null'", &logs);
        }
        if (!logs || !*logs) {
            g_free(logs);
            logs = capture_line("sh -lc 'dmesg | tail -n 20 2>/dev/null'", _("No logs available"));
        }
        gtk_text_buffer_set_text(g_logs_buffer, logs ? logs : _("No logs available"), -1);
        g_free(logs);
    } else {
        gtk_text_buffer_set_text(g_logs_buffer, _("Logs view disabled"), -1);
    }

    g_free(cpu);
    g_free(ram);
    g_free(gpu);
    g_free(kernel);
    g_free(temp);
    g_free(disk);
}

static gboolean on_metrics_timer(gpointer data)
{
    (void)data;
    refresh_system_metrics();
    return G_SOURCE_CONTINUE;
}

static char *apply_runtime_system_info(void)
{
    gboolean temperature = gtk_switch_get_active(GTK_SWITCH(g_temperature_switch));
    gboolean disk_usage = gtk_switch_get_active(GTK_SWITCH(g_disk_usage_switch));
    gboolean system_logs = gtk_switch_get_active(GTK_SWITCH(g_logs_switch));

    GString *issues = g_string_new(NULL);
    GString *env_block = g_string_new(NULL);

    g_string_append_printf(env_block,
                           "KARTON_SYSTEM_INFO_TEMPERATURE=%s\n"
                           "KARTON_SYSTEM_INFO_DISK_USAGE=%s\n"
                           "KARTON_SYSTEM_INFO_LOGS=%s",
                           temperature ? "1" : "0",
                           disk_usage ? "1" : "0",
                           system_logs ? "1" : "0");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed system info env",
                                              "# END KartON managed system info env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist system information environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_SYSTEM_INFO_TEMPERATURE=%s KARTON_SYSTEM_INFO_DISK_USAGE=%s KARTON_SYSTEM_INFO_LOGS=%s >/dev/null 2>&1 || true'",
            temperature ? "1" : "0",
            disk_usage ? "1" : "0",
            system_logs ? "1" : "0");
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-settingsd >/dev/null 2>&1 || true'");

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_refresh_metrics_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    refresh_system_metrics();
    status_set(_("System metrics refreshed"), FALSE);
}

static void on_reload_system_info_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_system_info_config();
    refresh_system_metrics();
    status_set(_("System information settings reloaded"), FALSE);
}

static void on_apply_system_info_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_system_info_config();
    char *issues = apply_runtime_system_info();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    refresh_system_metrics();
    status_set(_("System information settings applied"), FALSE);
}

GtkWidget *page_system_info_new(void)
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

    GtkWidget *title = gtk_label_new(_("System information"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Diagnostics and hardware details for monitoring system health."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *toggle_frame = create_section(_("Live diagnostics"),
                                             _("Enable selected data streams in system diagnostics views."));
    GtkWidget *toggle_box = gtk_frame_get_child(GTK_FRAME(toggle_frame));

    g_temperature_switch = gtk_switch_new();
    g_disk_usage_switch = gtk_switch_new();
    g_logs_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(toggle_box), create_row(_("Temperature"), g_temperature_switch));
    gtk_box_append(GTK_BOX(toggle_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(toggle_box), create_row(_("Disk usage"), g_disk_usage_switch));
    gtk_box_append(GTK_BOX(toggle_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(toggle_box), create_row(_("System logs"), g_logs_switch));

    gtk_box_append(GTK_BOX(box), toggle_frame);

    GtkWidget *metrics_frame = create_section(_("Live metrics"),
                                              _("Hardware and software metrics are refreshed every 5 seconds."));
    GtkWidget *metrics_box = gtk_frame_get_child(GTK_FRAME(metrics_frame));

    g_cpu_value = gtk_label_new("-");
    g_ram_value = gtk_label_new("-");
    g_gpu_value = gtk_label_new("-");
    g_kernel_value = gtk_label_new("-");
    g_temperature_value = gtk_label_new("-");
    g_disk_usage_value = gtk_label_new("-");

    gtk_box_append(GTK_BOX(metrics_box), create_row(_("CPU"), g_cpu_value));
    gtk_box_append(GTK_BOX(metrics_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(metrics_box), create_row(_("RAM"), g_ram_value));
    gtk_box_append(GTK_BOX(metrics_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(metrics_box), create_row(_("GPU"), g_gpu_value));
    gtk_box_append(GTK_BOX(metrics_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(metrics_box), create_row(_("Kernel"), g_kernel_value));
    gtk_box_append(GTK_BOX(metrics_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(metrics_box), create_row(_("Temperature"), g_temperature_value));
    gtk_box_append(GTK_BOX(metrics_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(metrics_box), create_row(_("Disk usage"), g_disk_usage_value));

    gtk_box_append(GTK_BOX(box), metrics_frame);

    GtkWidget *logs_frame = create_section(_("Recent logs"),
                                           _("Shows the last system log lines (journalctl or dmesg fallback)."));
    GtkWidget *logs_box = gtk_frame_get_child(GTK_FRAME(logs_frame));

    g_logs_buffer = gtk_text_buffer_new(NULL);
    g_logs_view = gtk_text_view_new_with_buffer(g_logs_buffer);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_logs_view), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_logs_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_logs_view), FALSE);
    gtk_widget_set_size_request(g_logs_view, -1, 220);
    gtk_box_append(GTK_BOX(logs_box), g_logs_view);

    gtk_box_append(GTK_BOX(box), logs_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh metrics now"));
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_metrics_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), refresh_btn);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_system_info_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply system information settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_system_info_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new(_("Monitoring preferences are saved and exported to the active session."));
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_status_label), TRUE);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_temperature_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_disk_usage_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_logs_switch), TRUE);

    load_system_info_config();
    refresh_system_metrics();

    if (g_metrics_timer_id == 0) {
        g_metrics_timer_id = g_timeout_add_seconds(5, on_metrics_timer, NULL);
    }

    return outer_scroll;
}
