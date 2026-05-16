#include "page-session-startup.h"

#include <gio/gio.h>
#ifdef G_OS_UNIX
#include <gio/gdesktopappinfo.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <string.h>
#include <unistd.h>

#define _(s) gettext(s)

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_login_manager_options[] = {
    { "Auto", "auto" },
    { "greetd", "greetd" },
    { "lightdm", "lightdm" },
    { "sddm", "sddm" },
    { "gdm", "gdm" },
    { "ly", "ly" },
};

static void status_set(const char *text, gboolean is_error);
static GtkWidget *g_autostart_switch = NULL;
static GtkWidget *g_services_switch = NULL;
static GtkWidget *g_login_manager_dropdown = NULL;
static GtkWidget *g_session_selection_switch = NULL;
static GtkWidget *g_restore_session_switch = NULL;
static GtkWidget *g_status_label = NULL;
static gboolean g_loading_session_startup = FALSE;
static guint g_live_apply_source_id = 0;
static gboolean g_admin_password_verified = FALSE;
static char *g_admin_password_cache = NULL;
static GtkWidget *g_login_hint_label = NULL;

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

static void clear_admin_password_cache(void)
{
    if (!g_admin_password_cache) {
        g_admin_password_verified = FALSE;
        return;
    }

    size_t len = strlen(g_admin_password_cache);
    if (len > 0) {
        memset(g_admin_password_cache, 0, len);
    }

    g_free(g_admin_password_cache);
    g_admin_password_cache = NULL;
    g_admin_password_verified = FALSE;
}

typedef struct {
    GMainLoop *loop;
    gint response_id;
} PasswordDialogState;

static void on_password_dialog_finish(PasswordDialogState *state, gint response_id)
{
    if (!state) {
        return;
    }

    state->response_id = response_id;
    if (state->loop) {
        g_main_loop_quit(state->loop);
    }
}

static void on_password_dialog_button_clicked(GtkButton *button, gpointer user_data)
{
    PasswordDialogState *state = (PasswordDialogState *)user_data;
    gpointer response_ptr = g_object_get_data(G_OBJECT(button), "karton-response-id");
    gint response_id = GPOINTER_TO_INT(response_ptr);
    on_password_dialog_finish(state, response_id);
}

static gboolean on_password_dialog_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    PasswordDialogState *state = (PasswordDialogState *)user_data;
    on_password_dialog_finish(state, GTK_RESPONSE_CANCEL);
    return FALSE;
}

static gboolean prompt_password_dialog(GtkWidget *parent, char **password_out, gboolean *cancelled)
{
    if (password_out) {
        *password_out = NULL;
    }
    if (cancelled) {
        *cancelled = FALSE;
    }

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Enter password"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    if (parent && GTK_IS_WINDOW(parent)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    }

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root_box, 16);
    gtk_widget_set_margin_end(root_box, 16);
    gtk_widget_set_margin_top(root_box, 16);
    gtk_widget_set_margin_bottom(root_box, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), root_box);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *help = gtk_label_new(_("Type your administrator password to apply session and startup settings."));
    gtk_widget_set_halign(help, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(help), TRUE);

    GtkWidget *password_label = gtk_label_new(_("Password"));
    gtk_widget_set_halign(password_label, GTK_ALIGN_START);

    GtkWidget *password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(password_entry), 0x2022);
    gtk_entry_set_activates_default(GTK_ENTRY(password_entry), TRUE);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *cancel_btn = gtk_button_new_with_label(_("Cancel"));
    g_object_set_data(G_OBJECT(cancel_btn), "karton-response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_object_set_data(G_OBJECT(apply_btn), "karton-response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));

    gtk_box_append(GTK_BOX(content), help);
    gtk_box_append(GTK_BOX(content), password_label);
    gtk_box_append(GTK_BOX(content), password_entry);

    gtk_box_append(GTK_BOX(actions), cancel_btn);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(root_box), content);
    gtk_box_append(GTK_BOX(root_box), actions);
    gtk_window_set_default_widget(GTK_WINDOW(dialog), apply_btn);

    PasswordDialogState state = {0};
    state.loop = g_main_loop_new(NULL, FALSE);
    state.response_id = GTK_RESPONSE_NONE;

    g_signal_connect(dialog, "close-request", G_CALLBACK(on_password_dialog_close_request), &state);
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_password_dialog_button_clicked), &state);
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_password_dialog_button_clicked), &state);

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

static gboolean ensure_admin_password_for_session_changes(GtkWidget *parent, gboolean *cancelled)
{
    if (cancelled) {
        *cancelled = FALSE;
    }

    if (geteuid() == 0) {
        return TRUE;
    }

    if (g_admin_password_verified
        && g_admin_password_cache
        && *g_admin_password_cache
        && verify_password_with_sudo(g_admin_password_cache)) {
        return TRUE;
    }

    clear_admin_password_cache();

    char *password = NULL;
    if (!prompt_password_dialog(parent, &password, cancelled)) {
        return FALSE;
    }

    if (!verify_password_with_sudo(password)) {
        if (password) {
            size_t len = strlen(password);
            if (len > 0) {
                memset(password, 0, len);
            }
        }
        g_free(password);
        return FALSE;
    }

    g_admin_password_cache = password;
    g_admin_password_verified = TRUE;
    return TRUE;
}


static GtkWidget *g_autostart_app_dropdown = NULL;
static GPtrArray *g_autostart_paths = NULL;

static void on_add_autostart_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    if (!g_autostart_app_dropdown || !g_autostart_paths) return;
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_autostart_app_dropdown));
    if (idx >= g_autostart_paths->len) return;
    const char *path = g_ptr_array_index(g_autostart_paths, idx);
    if (!path || !*path) return;

    char *basename = g_path_get_basename(path);
    char *autostart_dir = g_build_filename(g_get_home_dir(), ".config", "autostart", NULL);
    g_mkdir_with_parents(autostart_dir, 0700);
    char *dest = g_build_filename(autostart_dir, basename, NULL);

    char *q_path = g_shell_quote(path);
    char *q_dest = g_shell_quote(dest);
    char *cmd = g_strdup_printf("cp %s %s", q_path, q_dest);
    run_command_success(cmd);
    g_free(cmd);
    g_free(q_path);
    g_free(q_dest);

    g_free(basename);
    g_free(autostart_dir);
    g_free(dest);

    status_set(_("Application added to autostart."), FALSE);
}

static void on_open_autostart_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    char *autostart_dir = g_build_filename(g_get_home_dir(), ".config", "autostart", NULL);
    g_mkdir_with_parents(autostart_dir, 0700);

    char *q_dir = g_shell_quote(autostart_dir);
    char *cmd = g_strdup_printf("xdg-open %s >/dev/null 2>&1 || true", q_dir);
    run_command_success(cmd);
    g_free(cmd);
    g_free(q_dir);
    g_free(autostart_dir);
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

static GtkWidget *dropdown_from_options(const struct option_value *options, guint count)
{
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint i = 0; i < count; i++) {
        gtk_string_list_append(model, options[i].label);
    }

    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    g_object_unref(model);
    return dropdown;
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
        return "auto";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (idx >= count) {
        idx = 0;
    }

    return options[idx].value;
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

static char *session_startup_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "session-startup.conf", NULL);
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

static void save_session_startup_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "session_startup", "autostart_apps", gtk_switch_get_active(GTK_SWITCH(g_autostart_switch)));
    g_key_file_set_boolean(kf, "session_startup", "session_services", gtk_switch_get_active(GTK_SWITCH(g_services_switch)));
    g_key_file_set_string(kf,
                          "session_startup",
                          "login_manager",
                          dropdown_selected_value(g_login_manager_dropdown,
                                                  g_login_manager_options,
                                                  G_N_ELEMENTS(g_login_manager_options)));
    g_key_file_set_boolean(kf, "session_startup", "session_selection", gtk_switch_get_active(GTK_SWITCH(g_session_selection_switch)));
    g_key_file_set_boolean(kf, "session_startup", "restore_session", gtk_switch_get_active(GTK_SWITCH(g_restore_session_switch)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = session_startup_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_session_startup_config(void)
{
    char *path = session_startup_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean autostart_apps = g_key_file_get_boolean(kf, "session_startup", "autostart_apps", &error);
    if (error) {
        g_clear_error(&error);
        autostart_apps = TRUE;
    }

    gboolean session_services = g_key_file_get_boolean(kf, "session_startup", "session_services", &error);
    if (error) {
        g_clear_error(&error);
        session_services = TRUE;
    }

    char *login_manager = g_key_file_get_string(kf, "session_startup", "login_manager", &error);
    if (error) {
        g_clear_error(&error);
        login_manager = g_strdup("auto");
    }

    gboolean session_selection = g_key_file_get_boolean(kf, "session_startup", "session_selection", &error);
    if (error) {
        g_clear_error(&error);
        session_selection = TRUE;
    }

    gboolean restore_session = g_key_file_get_boolean(kf, "session_startup", "restore_session", &error);
    if (error) {
        g_clear_error(&error);
        restore_session = TRUE;
    }

    g_loading_session_startup = TRUE;

    gtk_switch_set_active(GTK_SWITCH(g_autostart_switch), autostart_apps);
    gtk_switch_set_active(GTK_SWITCH(g_services_switch), session_services);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_login_manager_dropdown),
                               find_option_index(g_login_manager_options,
                                                 G_N_ELEMENTS(g_login_manager_options),
                                                 login_manager));
    gtk_switch_set_active(GTK_SWITCH(g_session_selection_switch), session_selection);
    gtk_switch_set_active(GTK_SWITCH(g_restore_session_switch), restore_session);

    g_loading_session_startup = FALSE;

    g_free(login_manager);
    g_key_file_unref(kf);
    g_free(path);
}

static char *detect_login_manager_via_backend(void)
{
    const char *cmd = NULL;
    if (command_is_available("karton-login-manager")) {
        cmd = "karton-login-manager --status";
    } else {
        cmd = "sh -lc '$HOME/.local-karton/bin/karton-login-manager --status'";
    }

    char *stdout_data = NULL;
    if (!run_command_capture(cmd, &stdout_data) || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return g_strdup("unknown");
    }

    g_strstrip(stdout_data);
    return stdout_data;
}

static void refresh_login_manager_label(void)
{
    if (!g_login_hint_label) {
        return;
    }

    char *manager = detect_login_manager_via_backend();
    char *text = g_strdup_printf(_("Detected login manager: %s"), manager ? manager : "unknown");
    gtk_label_set_text(GTK_LABEL(g_login_hint_label), text);
    g_free(text);
    g_free(manager);
}

static gboolean apply_login_manager_selection(const char *login_manager, const char *password, GString *issues)
{
    if (!login_manager || g_strcmp0(login_manager, "auto") == 0) {
        return TRUE;
    }

    if (geteuid() != 0) {
        if (!command_is_available("sudo")) {
            g_string_append(issues, _("Could not apply login manager: sudo is not available. "));
            return FALSE;
        }
        if (!password || !*password || !verify_password_with_sudo(password)) {
            g_string_append(issues, _("Authentication failed: administrator password is required to switch login manager. "));
            return FALSE;
        }
    }

    const char *backend = command_is_available("karton-login-manager")
                              ? "karton-login-manager"
                              : "$HOME/.local-karton/bin/karton-login-manager";

    char *script = g_strdup_printf("KARTON_PROJECT_ROOT=\"%s\" %s --set %s",
                                   g_get_current_dir(),
                                   backend,
                                   login_manager);
    gboolean ok = run_privileged_script_with_password(script, password);
    g_free(script);

    if (!ok) {
        g_string_append(issues, _("Could not apply login manager backend flow. "));
        return FALSE;
    }

    return TRUE;
}

static char *apply_runtime_session_startup(const char *password)
{
    gboolean autostart_apps = gtk_switch_get_active(GTK_SWITCH(g_autostart_switch));
    gboolean session_services = gtk_switch_get_active(GTK_SWITCH(g_services_switch));
    const char *login_manager = dropdown_selected_value(g_login_manager_dropdown,
                                                        g_login_manager_options,
                                                        G_N_ELEMENTS(g_login_manager_options));
    gboolean session_selection = gtk_switch_get_active(GTK_SWITCH(g_session_selection_switch));
    gboolean restore_session = gtk_switch_get_active(GTK_SWITCH(g_restore_session_switch));

    GString *issues = g_string_new(NULL);
    GString *env_block = g_string_new(NULL);

    g_string_append_printf(env_block,
                           "KARTON_SESSION_AUTOSTART_APPS=%s\n"
                           "KARTON_SESSION_SERVICES=%s\n"
                           "KARTON_SESSION_LOGIN_MANAGER=%s\n"
                           "KARTON_SESSION_SELECTION=%s\n"
                           "KARTON_SESSION_RESTORE=%s",
                           autostart_apps ? "1" : "0",
                           session_services ? "1" : "0",
                           login_manager ? login_manager : "auto",
                           session_selection ? "1" : "0",
                           restore_session ? "1" : "0");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed session/startup env",
                                              "# END KartON managed session/startup env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist session and startup environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_SESSION_AUTOSTART_APPS=%s KARTON_SESSION_SERVICES=%s KARTON_SESSION_LOGIN_MANAGER=%s KARTON_SESSION_SELECTION=%s KARTON_SESSION_RESTORE=%s >/dev/null 2>&1 || true'",
            autostart_apps ? "1" : "0",
            session_services ? "1" : "0",
            login_manager ? login_manager : "auto",
            session_selection ? "1" : "0",
            restore_session ? "1" : "0");
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    (void)apply_login_manager_selection(login_manager, password, issues);
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-settingsd >/dev/null 2>&1 || true'");

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static gboolean apply_session_startup_changes(GtkWidget *origin_widget, gboolean dynamic)
{
    GtkRoot *root = origin_widget ? gtk_widget_get_root(origin_widget) : NULL;
    GtkWidget *parent = GTK_IS_WINDOW(root) ? GTK_WIDGET(root) : NULL;

    gboolean cancelled = FALSE;
    if (!ensure_admin_password_for_session_changes(parent, &cancelled)) {
        if (cancelled) {
            status_set(_("Applying session and startup settings was canceled."), TRUE);
        } else {
            status_set(_("Administrator authentication failed. Session changes were not applied."), TRUE);
        }
        return FALSE;
    }

    save_session_startup_config();
    char *issues = apply_runtime_session_startup(g_admin_password_cache);
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return FALSE;
    }

    refresh_login_manager_label();
    status_set(dynamic ? _("Session and startup settings applied dynamically")
                       : _("Session and startup settings applied"),
               FALSE);
    return TRUE;
}

static gboolean on_live_apply_timeout(gpointer user_data)
{
    (void)user_data;
    g_live_apply_source_id = 0;

    if (g_loading_session_startup) {
        return G_SOURCE_REMOVE;
    }

    (void)apply_session_startup_changes(g_status_label, TRUE);
    return G_SOURCE_REMOVE;
}

static void schedule_live_apply(void)
{
    if (g_loading_session_startup) {
        return;
    }

    if (g_live_apply_source_id != 0) {
        g_source_remove(g_live_apply_source_id);
        g_live_apply_source_id = 0;
    }

    g_live_apply_source_id = g_timeout_add(250, on_live_apply_timeout, NULL);
}

static void on_live_setting_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    (void)user_data;
    schedule_live_apply();
}

static void on_reload_session_startup_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (g_live_apply_source_id != 0) {
        g_source_remove(g_live_apply_source_id);
        g_live_apply_source_id = 0;
    }

    load_session_startup_config();
    refresh_login_manager_label();
    status_set(_("Session and startup settings reloaded"), FALSE);
}

static void on_apply_session_startup_clicked(GtkButton *btn, gpointer data)
{
    (void)data;
    (void)apply_session_startup_changes(GTK_WIDGET(btn), FALSE);
}

GtkWidget *page_session_startup_new(void)
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

    GtkWidget *title = gtk_label_new(_("Session and startup"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Controls for system startup, login flow and desktop session lifecycle."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *features_frame = create_section(_("Startup and session controls"),
                                               _("Configure how your desktop session starts and restores."));
    GtkWidget *features_box = gtk_frame_get_child(GTK_FRAME(features_frame));

    g_autostart_switch = gtk_switch_new();
    g_services_switch = gtk_switch_new();
    g_login_manager_dropdown = dropdown_from_options(g_login_manager_options,
                                                     G_N_ELEMENTS(g_login_manager_options));
    g_session_selection_switch = gtk_switch_new();
    g_restore_session_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(features_box), create_row(_("Application autostart"), g_autostart_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkStringList *apps_model = gtk_string_list_new(NULL);
    g_autostart_paths = g_ptr_array_new_with_free_func(g_free);
    gtk_string_list_append(apps_model, _("Select application..."));
    g_ptr_array_add(g_autostart_paths, g_strdup(""));

    GList *apps = g_app_info_get_all();
    for (GList *l = apps; l != NULL; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (g_app_info_should_show(app) && G_IS_DESKTOP_APP_INFO(app)) {
            const char *path = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app));
            if (path) {
                const char *name = g_app_info_get_display_name(app);
                if (!name) name = g_app_info_get_name(app);
                gtk_string_list_append(apps_model, name ? name : "Unknown");
                g_ptr_array_add(g_autostart_paths, g_strdup(path));
            }
        }
    }
    g_list_free_full(apps, g_object_unref);

    g_autostart_app_dropdown = gtk_drop_down_new(G_LIST_MODEL(apps_model), NULL);
    g_object_unref(apps_model);

    GtkWidget *add_btn = gtk_button_new_with_label(_("Add"));
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_autostart_clicked), NULL);

    GtkWidget *autostart_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(autostart_box), g_autostart_app_dropdown);
    gtk_box_append(GTK_BOX(autostart_box), add_btn);

    GtkWidget *open_autostart_btn = gtk_button_new_with_label(_("Open Autostart Folder"));
    g_signal_connect(open_autostart_btn, "clicked", G_CALLBACK(on_open_autostart_clicked), NULL);

    gtk_box_append(GTK_BOX(features_box), create_row(_("Add to autostart"), autostart_box));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Manage autostart apps"), open_autostart_btn));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Session services"), g_services_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Login manager"), g_login_manager_dropdown));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Session selection"), g_session_selection_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Session restore"), g_restore_session_switch));

    gtk_box_append(GTK_BOX(box), features_frame);

    g_login_hint_label = gtk_label_new(_("Detected login manager: unknown"));
    gtk_widget_set_halign(g_login_hint_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_login_hint_label), TRUE);
    gtk_widget_add_css_class(g_login_hint_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_login_hint_label);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_session_startup_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply session and startup settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_session_startup_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new(_("Changes are saved in user config and exported to the session environment."));
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_status_label), TRUE);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_autostart_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_services_switch), TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_login_manager_dropdown),
                               find_option_index(g_login_manager_options,
                                                 G_N_ELEMENTS(g_login_manager_options),
                                                 "auto"));
    gtk_switch_set_active(GTK_SWITCH(g_session_selection_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_restore_session_switch), TRUE);

    g_signal_connect(g_autostart_switch, "notify::active", G_CALLBACK(on_live_setting_notify), NULL);
    g_signal_connect(g_services_switch, "notify::active", G_CALLBACK(on_live_setting_notify), NULL);
    g_signal_connect(g_login_manager_dropdown, "notify::selected", G_CALLBACK(on_live_setting_notify), NULL);
    g_signal_connect(g_session_selection_switch, "notify::active", G_CALLBACK(on_live_setting_notify), NULL);
    g_signal_connect(g_restore_session_switch, "notify::active", G_CALLBACK(on_live_setting_notify), NULL);

    load_session_startup_config();
    refresh_login_manager_label();

    return outer_scroll;
}
