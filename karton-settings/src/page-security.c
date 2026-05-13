#include "page-security.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <string.h>
#include <unistd.h>

#define _(s) gettext(s)

static GtkWidget *g_lock_screen_switch = NULL;
static GtkWidget *g_encryption_switch = NULL;
static GtkWidget *g_firewall_switch = NULL;
static GtkWidget *g_app_permissions_switch = NULL;
static GtkWidget *g_activity_history_switch = NULL;
static GtkWidget *g_location_switch = NULL;
static GtkWidget *g_camera_switch = NULL;
static GtkWidget *g_microphone_switch = NULL;
static GtkWidget *g_sandboxing_switch = NULL;
static GtkWidget *g_mac_switch = NULL;
static GtkWidget *g_secure_boot_switch = NULL;

static GtkWidget *g_encryption_state_label = NULL;
static GtkWidget *g_secure_boot_state_label = NULL;
static GtkWidget *g_mac_state_label = NULL;
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

static gboolean run_command_with_stdin_capture(const char *command,
                                               const char *stdin_data,
                                               char **stdout_out,
                                               char **stderr_out)
{
    GError *error = NULL;
    GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE
                                             | G_SUBPROCESS_FLAGS_STDOUT_PIPE
                                             | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                         &error,
                                         "sh",
                                         "-lc",
                                         command,
                                         NULL);
    if (!proc) {
        if (stderr_out) {
            *stderr_out = g_strdup(error ? error->message : "spawn failed");
        }
        g_clear_error(&error);
        return FALSE;
    }

    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gboolean communicated = g_subprocess_communicate_utf8(proc,
                                                           stdin_data ? stdin_data : "",
                                                           NULL,
                                                           stdout_out ? &stdout_data : NULL,
                                                           stderr_out ? &stderr_data : NULL,
                                                           &error);
    if (!communicated) {
        if (stderr_out) {
            *stderr_out = g_strdup(error ? error->message : "communicate failed");
        }
        g_clear_error(&error);
        g_object_unref(proc);
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

    gboolean ok = g_subprocess_get_successful(proc);
    g_object_unref(proc);
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

static char *security_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "security.conf", NULL);
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

static gboolean verify_password_with_sudo(const char *password)
{
    if (!password || !*password || !command_is_available("sudo")) {
        return FALSE;
    }

    char *stdin_payload = g_strdup_printf("%s\n", password);
    gboolean ok = run_command_with_stdin_capture("sudo -S -k -p '' true", stdin_payload, NULL, NULL);
    g_free(stdin_payload);
    return ok;
}

static gboolean run_privileged_script_with_password(const char *script, const char *password)
{
    if (!script || !*script) {
        return FALSE;
    }

    if (geteuid() == 0) {
        char *q_script_root = g_shell_quote(script);
        char *cmd_root = g_strdup_printf("sh -lc %s", q_script_root);
        gboolean ok_root = run_command_success(cmd_root);
        g_free(cmd_root);
        g_free(q_script_root);
        return ok_root;
    }

    if (!password || !*password || !command_is_available("sudo")) {
        return FALSE;
    }

    char *stdin_payload = g_strdup_printf("%s\n", password);
    char *q_script = g_shell_quote(script);
    char *cmd = g_strdup_printf("sudo -S -k -p '' sh -lc %s", q_script);

    gboolean ok = run_command_with_stdin_capture(cmd, stdin_payload, NULL, NULL);
    g_free(stdin_payload);
    g_free(cmd);
    g_free(q_script);
    return ok;
}

typedef struct {
    GMainLoop *loop;
    gint response_id;
} PasswordDialogState;

static void on_password_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
    (void)dialog;
    PasswordDialogState *state = (PasswordDialogState *)user_data;
    state->response_id = response_id;
    if (state->loop) {
        g_main_loop_quit(state->loop);
    }
}

static gboolean prompt_password_dialog(GtkWidget *parent, char **password_out, gboolean *cancelled)
{
    if (password_out) {
        *password_out = NULL;
    }
    if (cancelled) {
        *cancelled = FALSE;
    }

    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Enter password"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    if (parent && GTK_IS_WINDOW(parent)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    }

    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Apply"), GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);

    GtkWidget *help = gtk_label_new(_("Type your administrator password to apply security settings."));
    gtk_widget_set_halign(help, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(help), TRUE);

    GtkWidget *password_label = gtk_label_new(_("Password"));
    gtk_widget_set_halign(password_label, GTK_ALIGN_START);

    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(password_entry), 0x2022);
    gtk_entry_set_activates_default(GTK_ENTRY(password_entry), TRUE);

    gtk_box_append(GTK_BOX(box), help);
    gtk_box_append(GTK_BOX(box), password_label);
    gtk_box_append(GTK_BOX(box), password_entry);
    gtk_box_append(GTK_BOX(content), box);

    PasswordDialogState state = {0};
    state.loop = g_main_loop_new(NULL, FALSE);
    state.response_id = GTK_RESPONSE_NONE;

    g_signal_connect(dialog, "response", G_CALLBACK(on_password_dialog_response), &state);

    gtk_window_present(GTK_WINDOW(dialog));
    gtk_widget_grab_focus(password_entry);
    g_main_loop_run(state.loop);

    gboolean accepted = state.response_id == GTK_RESPONSE_OK;

    if (accepted) {
        const char *entered = gtk_editable_get_text(GTK_EDITABLE(password_entry));
        if (!entered || !*entered) {
            accepted = FALSE;
        } else if (password_out) {
            *password_out = g_strdup(entered);
        }
    } else if (cancelled) {
        *cancelled = TRUE;
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_unref(state.loop);
    return accepted;
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

static gboolean detect_root_encrypted(void)
{
    char *stdout_data = NULL;
    gboolean ok = run_command_capture(
        "sh -lc 'src=$(findmnt -no SOURCE / 2>/dev/null); if [ -n \"$src\" ]; then t=$(lsblk -no TYPE \"$src\" 2>/dev/null | head -n1); printf %s \"$t\"; fi'",
        &stdout_data,
        NULL,
        NULL);

    gboolean encrypted = FALSE;
    if (ok && stdout_data && *stdout_data) {
        g_strstrip(stdout_data);
        if (g_strcmp0(stdout_data, "crypt") == 0) {
            encrypted = TRUE;
        }
    }

    g_free(stdout_data);
    return encrypted;
}

static int detect_secure_boot_state(void)
{
    char *stdout_data = NULL;
    gboolean ok = run_command_capture(
        "sh -lc 'f=$(ls /sys/firmware/efi/efivars/SecureBoot-* 2>/dev/null | head -n1); if [ -n \"$f\" ]; then od -An -t u1 -j 4 -N 1 \"$f\" 2>/dev/null | tr -d \"[:space:]\"; fi'",
        &stdout_data,
        NULL,
        NULL);

    int state = -1;
    if (ok && stdout_data && *stdout_data) {
        g_strstrip(stdout_data);
        if (g_strcmp0(stdout_data, "1") == 0) {
            state = 1;
        } else if (g_strcmp0(stdout_data, "0") == 0) {
            state = 0;
        }
    }

    g_free(stdout_data);
    return state;
}

static char *detect_selinux_state(void)
{
    if (!command_is_available("getenforce")) {
        return g_strdup(_("SELinux: unavailable"));
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture("sh -lc 'getenforce 2>/dev/null'", &stdout_data, NULL, NULL);
    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return g_strdup(_("SELinux: unknown"));
    }

    g_strstrip(stdout_data);
    char *state = g_strdup_printf(_("SELinux: %s"), stdout_data);
    g_free(stdout_data);
    return state;
}

static char *detect_apparmor_state(void)
{
    if (!command_is_available("aa-status")) {
        return g_strdup(_("AppArmor: unavailable"));
    }

    gboolean enabled = run_command_success("sh -lc 'aa-status --enabled >/dev/null 2>&1'");
    return g_strdup(enabled ? _("AppArmor: enabled") : _("AppArmor: disabled"));
}

static void refresh_runtime_status(void)
{
    gboolean encrypted = detect_root_encrypted();
    gtk_switch_set_active(GTK_SWITCH(g_encryption_switch), encrypted);

    char *enc_text = g_strdup(encrypted
                                  ? _("Root filesystem encryption: enabled")
                                  : _("Root filesystem encryption: not detected"));
    gtk_label_set_text(GTK_LABEL(g_encryption_state_label), enc_text);
    g_free(enc_text);

    int secure_boot = detect_secure_boot_state();
    if (secure_boot == 1) {
        gtk_switch_set_active(GTK_SWITCH(g_secure_boot_switch), TRUE);
        gtk_label_set_text(GTK_LABEL(g_secure_boot_state_label), _("Secure Boot: enabled"));
    } else if (secure_boot == 0) {
        gtk_switch_set_active(GTK_SWITCH(g_secure_boot_switch), FALSE);
        gtk_label_set_text(GTK_LABEL(g_secure_boot_state_label), _("Secure Boot: disabled"));
    } else {
        gtk_switch_set_active(GTK_SWITCH(g_secure_boot_switch), FALSE);
        gtk_label_set_text(GTK_LABEL(g_secure_boot_state_label), _("Secure Boot: unavailable or unknown"));
    }

    char *selinux = detect_selinux_state();
    char *apparmor = detect_apparmor_state();
    char *mac = g_strdup_printf("%s\n%s", selinux, apparmor);
    gtk_label_set_text(GTK_LABEL(g_mac_state_label), mac);

    g_free(mac);
    g_free(apparmor);
    g_free(selinux);
}

static void save_security_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "security", "lock_screen", gtk_switch_get_active(GTK_SWITCH(g_lock_screen_switch)));
    g_key_file_set_boolean(kf, "security", "firewall", gtk_switch_get_active(GTK_SWITCH(g_firewall_switch)));
    g_key_file_set_boolean(kf, "security", "app_permissions", gtk_switch_get_active(GTK_SWITCH(g_app_permissions_switch)));
    g_key_file_set_boolean(kf, "security", "activity_history", gtk_switch_get_active(GTK_SWITCH(g_activity_history_switch)));
    g_key_file_set_boolean(kf, "security", "location", gtk_switch_get_active(GTK_SWITCH(g_location_switch)));
    g_key_file_set_boolean(kf, "security", "camera", gtk_switch_get_active(GTK_SWITCH(g_camera_switch)));
    g_key_file_set_boolean(kf, "security", "microphone", gtk_switch_get_active(GTK_SWITCH(g_microphone_switch)));
    g_key_file_set_boolean(kf, "security", "sandboxing", gtk_switch_get_active(GTK_SWITCH(g_sandboxing_switch)));
    g_key_file_set_boolean(kf, "security", "mac_hardening", gtk_switch_get_active(GTK_SWITCH(g_mac_switch)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = security_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_security_config(void)
{
    char *path = security_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean lock_screen = g_key_file_get_boolean(kf, "security", "lock_screen", &error);
    if (error) {
        g_clear_error(&error);
        lock_screen = TRUE;
    }

    gboolean firewall = g_key_file_get_boolean(kf, "security", "firewall", &error);
    if (error) {
        g_clear_error(&error);
        firewall = FALSE;
    }

    gboolean app_permissions = g_key_file_get_boolean(kf, "security", "app_permissions", &error);
    if (error) {
        g_clear_error(&error);
        app_permissions = TRUE;
    }

    gboolean activity_history = g_key_file_get_boolean(kf, "security", "activity_history", &error);
    if (error) {
        g_clear_error(&error);
        activity_history = FALSE;
    }

    gboolean location = g_key_file_get_boolean(kf, "security", "location", &error);
    if (error) {
        g_clear_error(&error);
        location = FALSE;
    }

    gboolean camera = g_key_file_get_boolean(kf, "security", "camera", &error);
    if (error) {
        g_clear_error(&error);
        camera = FALSE;
    }

    gboolean microphone = g_key_file_get_boolean(kf, "security", "microphone", &error);
    if (error) {
        g_clear_error(&error);
        microphone = FALSE;
    }

    gboolean sandboxing = g_key_file_get_boolean(kf, "security", "sandboxing", &error);
    if (error) {
        g_clear_error(&error);
        sandboxing = TRUE;
    }

    gboolean mac_hardening = g_key_file_get_boolean(kf, "security", "mac_hardening", &error);
    if (error) {
        g_clear_error(&error);
        mac_hardening = TRUE;
    }

    gtk_switch_set_active(GTK_SWITCH(g_lock_screen_switch), lock_screen);
    gtk_switch_set_active(GTK_SWITCH(g_firewall_switch), firewall);
    gtk_switch_set_active(GTK_SWITCH(g_app_permissions_switch), app_permissions);
    gtk_switch_set_active(GTK_SWITCH(g_activity_history_switch), activity_history);
    gtk_switch_set_active(GTK_SWITCH(g_location_switch), location);
    gtk_switch_set_active(GTK_SWITCH(g_camera_switch), camera);
    gtk_switch_set_active(GTK_SWITCH(g_microphone_switch), microphone);
    gtk_switch_set_active(GTK_SWITCH(g_sandboxing_switch), sandboxing);
    gtk_switch_set_active(GTK_SWITCH(g_mac_switch), mac_hardening);

    g_key_file_unref(kf);
    g_free(path);
}

static gboolean ensure_password_authentication(const char *password, GString *issues)
{
    if (geteuid() == 0) {
        return TRUE;
    }

    if (!command_is_available("sudo")) {
        g_string_append(issues, _("Authentication backend unavailable: sudo not found. "));
        return FALSE;
    }

    if (!password || !*password) {
        g_string_append(issues, _("Password is required. "));
        return FALSE;
    }

    if (verify_password_with_sudo(password)) {
        return TRUE;
    }

    g_string_append(issues, _("Authentication failed: password is required to apply security changes. "));
    return FALSE;
}

static char *apply_runtime_security(const char *password, gboolean *auth_failed)
{
    GString *issues = g_string_new(NULL);

    if (auth_failed) {
        *auth_failed = FALSE;
    }

    gboolean lock_screen = gtk_switch_get_active(GTK_SWITCH(g_lock_screen_switch));
    gboolean firewall = gtk_switch_get_active(GTK_SWITCH(g_firewall_switch));
    gboolean app_permissions = gtk_switch_get_active(GTK_SWITCH(g_app_permissions_switch));
    gboolean activity_history = gtk_switch_get_active(GTK_SWITCH(g_activity_history_switch));
    gboolean location = gtk_switch_get_active(GTK_SWITCH(g_location_switch));
    gboolean camera = gtk_switch_get_active(GTK_SWITCH(g_camera_switch));
    gboolean microphone = gtk_switch_get_active(GTK_SWITCH(g_microphone_switch));
    gboolean sandboxing = gtk_switch_get_active(GTK_SWITCH(g_sandboxing_switch));
    gboolean mac_hardening = gtk_switch_get_active(GTK_SWITCH(g_mac_switch));
    gboolean encryption_required = gtk_switch_get_active(GTK_SWITCH(g_encryption_switch));
    gboolean secure_boot_required = gtk_switch_get_active(GTK_SWITCH(g_secure_boot_switch));

    if (!ensure_password_authentication(password, issues)) {
        if (auth_failed) {
            *auth_failed = TRUE;
        }
        return g_string_free(issues, FALSE);
    }

    if (!gsettings_set_bool("org.gnome.desktop.screensaver", "lock-enabled", lock_screen)) {
        g_string_append(issues, _("Could not update screen lock policy. "));
    }

    if (!gsettings_set_bool("org.gnome.desktop.privacy", "remember-recent-files", activity_history)) {
        g_string_append(issues, _("Could not update activity history policy. "));
    }

    if (!gsettings_set_bool("org.gnome.system.location", "enabled", location)) {
        g_string_append(issues, _("Could not update location policy. "));
    }

    if (!gsettings_set_bool("org.gnome.desktop.privacy", "disable-camera", !camera)) {
        g_string_append(issues, _("Could not update camera privacy policy. "));
    }

    if (!gsettings_set_bool("org.gnome.desktop.privacy", "disable-microphone", !microphone)) {
        g_string_append(issues, _("Could not update microphone privacy policy on this desktop. "));
    }

    if (command_is_available("ufw")) {
        char *script = g_strdup_printf("ufw --force %s >/dev/null 2>&1", firewall ? "enable" : "disable");
        if (!run_privileged_script_with_password(script, password)) {
            g_string_append(issues, _("Could not apply firewall state via ufw. "));
        }
        g_free(script);
    } else if (command_is_available("systemctl") && command_is_available("firewall-cmd")) {
        const char *script = firewall
                                 ? "systemctl enable --now firewalld >/dev/null 2>&1"
                                 : "systemctl disable --now firewalld >/dev/null 2>&1";
        if (!run_privileged_script_with_password(script, password)) {
            g_string_append(issues, _("Could not apply firewall state via firewalld. "));
        }
    } else {
        g_string_append(issues, _("No supported firewall backend found (ufw/firewalld). "));
    }

    if (command_is_available("getenforce")) {
        const char *script = mac_hardening ? "setenforce 1 >/dev/null 2>&1" : "setenforce 0 >/dev/null 2>&1";
        if (!run_privileged_script_with_password(script, password)) {
            g_string_append(issues, _("Could not switch SELinux enforcement mode. "));
        }
    } else if (command_is_available("aa-status") && command_is_available("systemctl")) {
        const char *script = mac_hardening
                                 ? "systemctl enable --now apparmor >/dev/null 2>&1"
                                 : "systemctl disable --now apparmor >/dev/null 2>&1";
        if (!run_privileged_script_with_password(script, password)) {
            g_string_append(issues, _("Could not switch AppArmor service state. "));
        }
    }

    if (sandboxing && !command_is_available("flatpak") && !command_is_available("bwrap")) {
        g_string_append(issues, _("Application isolation tools not found (flatpak/bwrap). "));
    }

    gboolean encrypted_now = detect_root_encrypted();
    if (encryption_required != encrypted_now) {
        g_string_append(issues, _("Disk encryption cannot be toggled live from settings. "));
    }

    int secure_boot_state = detect_secure_boot_state();
    if (secure_boot_state >= 0 && secure_boot_required != (secure_boot_state == 1)) {
        g_string_append(issues, _("Secure Boot cannot be changed from userspace. Use firmware setup. "));
    }

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_SECURITY_LOCK_SCREEN=%s\n"
                           "KARTON_SECURITY_FIREWALL=%s\n"
                           "KARTON_SECURITY_APP_PERMISSIONS=%s\n"
                           "KARTON_SECURITY_ACTIVITY_HISTORY=%s\n"
                           "KARTON_SECURITY_LOCATION=%s\n"
                           "KARTON_SECURITY_CAMERA=%s\n"
                           "KARTON_SECURITY_MICROPHONE=%s\n"
                           "KARTON_SECURITY_SANDBOXING=%s\n"
                           "KARTON_SECURITY_MAC_HARDENING=%s\n"
                           "KARTON_SECURITY_ENCRYPTION_REQUIRED=%s\n"
                           "KARTON_SECURITY_SECURE_BOOT_REQUIRED=%s",
                           lock_screen ? "1" : "0",
                           firewall ? "1" : "0",
                           app_permissions ? "1" : "0",
                           activity_history ? "1" : "0",
                           location ? "1" : "0",
                           camera ? "1" : "0",
                           microphone ? "1" : "0",
                           sandboxing ? "1" : "0",
                           mac_hardening ? "1" : "0",
                           encryption_required ? "1" : "0",
                           secure_boot_required ? "1" : "0");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed security env",
                                              "# END KartON managed security env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist security environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_SECURITY_LOCK_SCREEN=%s KARTON_SECURITY_FIREWALL=%s KARTON_SECURITY_APP_PERMISSIONS=%s KARTON_SECURITY_ACTIVITY_HISTORY=%s KARTON_SECURITY_LOCATION=%s KARTON_SECURITY_CAMERA=%s KARTON_SECURITY_MICROPHONE=%s KARTON_SECURITY_SANDBOXING=%s KARTON_SECURITY_MAC_HARDENING=%s KARTON_SECURITY_ENCRYPTION_REQUIRED=%s KARTON_SECURITY_SECURE_BOOT_REQUIRED=%s >/dev/null 2>&1 || true'",
            lock_screen ? "1" : "0",
            firewall ? "1" : "0",
            app_permissions ? "1" : "0",
            activity_history ? "1" : "0",
            location ? "1" : "0",
            camera ? "1" : "0",
            microphone ? "1" : "0",
            sandboxing ? "1" : "0",
            mac_hardening ? "1" : "0",
            encryption_required ? "1" : "0",
            secure_boot_required ? "1" : "0");
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

static void on_refresh_status_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_runtime_status();
    status_set(_("Security status refreshed"), FALSE);
}

static void on_reload_security_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_security_config();
    refresh_runtime_status();
    status_set(_("Security settings reloaded"), FALSE);
}

static void on_apply_security_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    gboolean cancelled = FALSE;
    char *password = NULL;
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(btn)));

    if (!prompt_password_dialog(root, &password, &cancelled)) {
        if (cancelled) {
            status_set(_("Applying security settings was canceled."), TRUE);
        } else {
            status_set(_("Password is required."), TRUE);
        }
        return;
    }

    gboolean auth_failed = FALSE;
    char *issues = apply_runtime_security(password, &auth_failed);

    if (!auth_failed) {
        save_security_config();
    }

    refresh_runtime_status();
    g_free(password);

    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Security and privacy settings applied"), FALSE);
}

GtkWidget *page_security_new(void)
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

    GtkWidget *title = gtk_label_new(_("Security and privacy"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Protect the session and personal data: lock screen, encryption state, firewall, app permissions, activity history, location, camera, microphone, application isolation and SELinux/AppArmor."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *protection_frame = create_section(_("System protection"),
                                                 _("Configure lock screen, firewall, encryption requirement, Secure Boot expectation and SELinux/AppArmor policy."));
    GtkWidget *protection_box = gtk_frame_get_child(GTK_FRAME(protection_frame));

    g_lock_screen_switch = gtk_switch_new();
    g_firewall_switch = gtk_switch_new();
    g_encryption_switch = gtk_switch_new();
    gtk_widget_set_sensitive(g_encryption_switch, FALSE);
    g_secure_boot_switch = gtk_switch_new();
    gtk_widget_set_sensitive(g_secure_boot_switch, FALSE);
    g_mac_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(protection_box), create_row(_("Screen lock"), g_lock_screen_switch));
    gtk_box_append(GTK_BOX(protection_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(protection_box), create_row(_("Firewall"), g_firewall_switch));
    gtk_box_append(GTK_BOX(protection_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(protection_box), create_row(_("Encryption (detected state)"), g_encryption_switch));
    gtk_box_append(GTK_BOX(protection_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(protection_box), create_row(_("Secure Boot (detected state)"), g_secure_boot_switch));
    gtk_box_append(GTK_BOX(protection_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(protection_box), create_row(_("SELinux/AppArmor hardening"), g_mac_switch));

    gtk_box_append(GTK_BOX(box), protection_frame);

    GtkWidget *privacy_frame = create_section(_("Privacy controls"),
                                              _("Control app permissions, activity history, location, camera, microphone and application isolation policy."));
    GtkWidget *privacy_box = gtk_frame_get_child(GTK_FRAME(privacy_frame));

    g_app_permissions_switch = gtk_switch_new();
    g_activity_history_switch = gtk_switch_new();
    g_location_switch = gtk_switch_new();
    g_camera_switch = gtk_switch_new();
    g_microphone_switch = gtk_switch_new();
    g_sandboxing_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(privacy_box), create_row(_("Application permissions"), g_app_permissions_switch));
    gtk_box_append(GTK_BOX(privacy_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(privacy_box), create_row(_("Activity history"), g_activity_history_switch));
    gtk_box_append(GTK_BOX(privacy_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(privacy_box), create_row(_("Location"), g_location_switch));
    gtk_box_append(GTK_BOX(privacy_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(privacy_box), create_row(_("Camera"), g_camera_switch));
    gtk_box_append(GTK_BOX(privacy_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(privacy_box), create_row(_("Microphone"), g_microphone_switch));
    gtk_box_append(GTK_BOX(privacy_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(privacy_box), create_row(_("Application isolation"), g_sandboxing_switch));

    gtk_box_append(GTK_BOX(box), privacy_frame);

    GtkWidget *status_frame = create_section(_("Security runtime status"),
                                             _("Live status from the current system. Some options (encryption and Secure Boot) are read-only in userspace."));
    GtkWidget *status_box = gtk_frame_get_child(GTK_FRAME(status_frame));

    g_encryption_state_label = gtk_label_new("");
    gtk_widget_set_halign(g_encryption_state_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_encryption_state_label, "card-subtitle");

    g_secure_boot_state_label = gtk_label_new("");
    gtk_widget_set_halign(g_secure_boot_state_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_secure_boot_state_label, "card-subtitle");

    g_mac_state_label = gtk_label_new("");
    gtk_widget_set_halign(g_mac_state_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_mac_state_label), TRUE);
    gtk_widget_add_css_class(g_mac_state_label, "card-subtitle");

    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh status"));
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_status_clicked), NULL);

    gtk_box_append(GTK_BOX(status_box), g_encryption_state_label);
    gtk_box_append(GTK_BOX(status_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(status_box), g_secure_boot_state_label);
    gtk_box_append(GTK_BOX(status_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(status_box), g_mac_state_label);
    gtk_box_append(GTK_BOX(status_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(status_box), create_row(_("Status refresh"), refresh_btn));

    gtk_box_append(GTK_BOX(box), status_frame);

    GtkWidget *auth_hint = gtk_label_new(_("Password authentication is required when applying security changes."));
    gtk_widget_set_halign(auth_hint, GTK_ALIGN_START);
    gtk_widget_add_css_class(auth_hint, "row-subtitle");
    gtk_box_append(GTK_BOX(box), auth_hint);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_security_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply security settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_security_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_lock_screen_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_firewall_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_app_permissions_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_activity_history_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_location_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_camera_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_microphone_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_sandboxing_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_mac_switch), TRUE);

    load_security_config();
    refresh_runtime_status();

    return outer_scroll;
}
