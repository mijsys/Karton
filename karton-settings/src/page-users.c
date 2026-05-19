#include "page-users.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <string.h>
#include <unistd.h>

#define _(s) gettext(s)

typedef struct {
    char *label;
    char *value;
} SessionOption;

typedef struct {
    GtkWidget *entry;
} AvatarPickContext;

static GtkWidget *g_users_dropdown = NULL;
static GtkWidget *g_new_username_entry = NULL;
static GtkWidget *g_new_fullname_entry = NULL;
static GtkWidget *g_password_entry = NULL;
static GtkWidget *g_autologin_switch = NULL;
static GtkWidget *g_session_dropdown = NULL;
static GtkWidget *g_avatar_path_entry = NULL;
static GtkWidget *g_groups_entry = NULL;
static GtkWidget *g_admin_switch = NULL;
static GtkWidget *g_status_label = NULL;
static GPtrArray *g_session_options = NULL;

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

static void on_modal_window_response(GtkWidget *widget, gpointer user_data)
{
    gint response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "response-id"));
    PasswordDialogState *state = (PasswordDialogState *)user_data;
    state->response_id = response_id;
    if (state->loop) {
        g_main_loop_quit(state->loop);
    }
}

static gboolean on_modal_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    PasswordDialogState *state = (PasswordDialogState *)user_data;
    state->response_id = GTK_RESPONSE_CANCEL;
    if (state->loop) {
        g_main_loop_quit(state->loop);
    }
    return TRUE;
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

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *help = gtk_label_new(_("Type your administrator password to apply user and account settings."));
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
    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel_button = gtk_button_new_with_label(_("Cancel"));
    GtkWidget *apply_button = gtk_button_new_with_label(_("Apply"));
    g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
    g_object_set_data(G_OBJECT(apply_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
    gtk_box_append(GTK_BOX(buttons), cancel_button);
    gtk_box_append(GTK_BOX(buttons), apply_button);

    gtk_box_append(GTK_BOX(content), box);
    gtk_box_append(GTK_BOX(content), buttons);
    gtk_window_set_child(GTK_WINDOW(dialog), content);
    gtk_window_set_default_widget(GTK_WINDOW(dialog), apply_button);

    PasswordDialogState state = {0};
    state.loop = g_main_loop_new(NULL, FALSE);
    state.response_id = GTK_RESPONSE_NONE;

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_modal_window_response), &state);
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_modal_window_response), &state);
    g_signal_connect(dialog, "close-request", G_CALLBACK(on_modal_window_close_request), &state);

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

static gboolean prompt_confirm_delete_dialog(GtkWidget *parent,
                                             const char *username,
                                             gboolean *cancelled)
{
    if (cancelled) {
        *cancelled = FALSE;
    }

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Delete user account"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    if (parent && GTK_IS_WINDOW(parent)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    }

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);

    char *msg = g_strdup_printf(_("Do you want to permanently delete user '%s' and home files?"), username ? username : "");
    GtkWidget *help = gtk_label_new(msg);
    gtk_widget_set_halign(help, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(help), TRUE);
    g_free(msg);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel_button = gtk_button_new_with_label(_("Cancel"));
    GtkWidget *delete_button = gtk_button_new_with_label(_("Delete user"));
    gtk_widget_add_css_class(delete_button, "destructive-action");
    g_object_set_data(G_OBJECT(cancel_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
    g_object_set_data(G_OBJECT(delete_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_OK));
    gtk_box_append(GTK_BOX(buttons), cancel_button);
    gtk_box_append(GTK_BOX(buttons), delete_button);

    gtk_box_append(GTK_BOX(content), help);
    gtk_box_append(GTK_BOX(content), buttons);
    gtk_window_set_child(GTK_WINDOW(dialog), content);
    gtk_window_set_default_widget(GTK_WINDOW(dialog), cancel_button);

    PasswordDialogState state = {0};
    state.loop = g_main_loop_new(NULL, FALSE);
    state.response_id = GTK_RESPONSE_NONE;

    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_modal_window_response), &state);
    g_signal_connect(delete_button, "clicked", G_CALLBACK(on_modal_window_response), &state);
    g_signal_connect(dialog, "close-request", G_CALLBACK(on_modal_window_close_request), &state);

    gtk_window_present(GTK_WINDOW(dialog));
    g_main_loop_run(state.loop);

    gboolean confirmed = state.response_id == GTK_RESPONSE_OK;
    if (!confirmed && cancelled) {
        *cancelled = TRUE;
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_main_loop_unref(state.loop);
    return confirmed;
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
    gtk_widget_set_size_request(entry, 320, -1);

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

static void session_option_free(gpointer data)
{
    SessionOption *option = data;
    if (!option) {
        return;
    }

    g_free(option->label);
    g_free(option->value);
    g_free(option);
}

static void ensure_session_store(void)
{
    if (!g_session_options) {
        g_session_options = g_ptr_array_new_with_free_func(session_option_free);
    }
}

static guint session_option_count(void)
{
    return g_session_options ? g_session_options->len : 0;
}

static gboolean session_value_exists(const char *value)
{
    if (!value || !*value || !g_session_options) {
        return FALSE;
    }

    for (guint i = 0; i < g_session_options->len; i++) {
        SessionOption *option = g_ptr_array_index(g_session_options, i);
        if (option && g_strcmp0(option->value, value) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static guint find_session_index(const char *value)
{
    if (!value || !*value || !g_session_options || g_session_options->len == 0) {
        return 0;
    }

    for (guint i = 0; i < g_session_options->len; i++) {
        SessionOption *option = g_ptr_array_index(g_session_options, i);
        if (option && g_strcmp0(option->value, value) == 0) {
            return i;
        }
    }

    return 0;
}

static const char *dropdown_selected_session_value(GtkWidget *dropdown)
{
    if (!dropdown || !g_session_options || g_session_options->len == 0) {
        return "default";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (idx >= g_session_options->len) {
        idx = 0;
    }

    SessionOption *option = g_ptr_array_index(g_session_options, idx);
    if (!option || !option->value || !*option->value) {
        return "default";
    }

    return option->value;
}

static char *desktop_id_from_filename(const char *name)
{
    if (!name || !*name) {
        return g_strdup("");
    }

    if (g_str_has_suffix(name, ".desktop")) {
        gsize len = strlen(name);
        return g_strndup(name, len - strlen(".desktop"));
    }

    return g_strdup(name);
}

static gboolean is_login_shell(const char *shell)
{
    if (!shell || !*shell) {
        return FALSE;
    }

    return !(g_str_has_suffix(shell, "/nologin")
             || g_str_has_suffix(shell, "/false")
             || g_str_has_suffix(shell, "nologin")
             || g_str_has_suffix(shell, "false"));
}

static void append_session_option(GtkStringList *model,
                                  GHashTable *seen_values,
                                  const char *label,
                                  const char *value)
{
    if (!model || !seen_values || !label || !*label || !value || !*value) {
        return;
    }

    if (g_hash_table_contains(seen_values, value)) {
        return;
    }

    SessionOption *option = g_new0(SessionOption, 1);
    option->label = g_strdup(label);
    option->value = g_strdup(value);
    g_ptr_array_add(g_session_options, option);

    g_hash_table_add(seen_values, g_strdup(value));
    gtk_string_list_append(model, label);
}

static void append_installed_sessions_from_dir(GtkStringList *model,
                                               GHashTable *seen_values,
                                               const char *dir_path,
                                               const char *source_token,
                                               const char *source_label)
{
    if (!dir_path || !g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
        return;
    }

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) {
        return;
    }

    const char *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_suffix(name, ".desktop")) {
            continue;
        }

        char *id = desktop_id_from_filename(name);
        if (!id || !*id) {
            g_free(id);
            continue;
        }

        char *desktop_path = g_build_filename(dir_path, name, NULL);
        GKeyFile *kf = g_key_file_new();
        gboolean loaded = g_key_file_load_from_file(kf, desktop_path, G_KEY_FILE_NONE, NULL);

        char *display_name = NULL;
        gboolean hidden = FALSE;
        gboolean nodisplay = FALSE;
        if (loaded) {
            hidden = g_key_file_get_boolean(kf, "Desktop Entry", "Hidden", NULL);
            nodisplay = g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", NULL);
            display_name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
            if (!display_name || !*display_name) {
                g_free(display_name);
                display_name = g_key_file_get_string(kf, "Desktop Entry", "Name", NULL);
            }
        }

        if (!display_name || !*display_name) {
            g_free(display_name);
            display_name = g_strdup(id);
        }

        if (!hidden && !nodisplay) {
            char *label = g_strdup_printf("%s (%s)", display_name, source_label);
            char *value = g_strdup_printf("%s:%s", source_token, id);
            append_session_option(model, seen_values, label, value);
            g_free(value);
            g_free(label);
        }

        g_free(display_name);
        g_key_file_unref(kf);
        g_free(desktop_path);
        g_free(id);
    }

    g_dir_close(dir);
}

static void refresh_session_options(const char *preferred_value)
{
    if (!g_session_dropdown) {
        return;
    }

    ensure_session_store();
    g_ptr_array_set_size(g_session_options, 0);

    GtkStringList *model = gtk_string_list_new(NULL);
    GHashTable *seen_values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    append_session_option(model, seen_values, _("System default"), "default");

    char *user_wayland_sessions = g_build_filename(g_get_home_dir(), ".local", "share", "wayland-sessions", NULL);
    char *user_x11_sessions = g_build_filename(g_get_home_dir(), ".local", "share", "xsessions", NULL);

    append_installed_sessions_from_dir(model,
                                       seen_values,
                                       user_wayland_sessions,
                                       "wayland",
                                       _("Wayland"));
    append_installed_sessions_from_dir(model,
                                       seen_values,
                                       user_x11_sessions,
                                       "x11",
                                       _("X11"));

    append_installed_sessions_from_dir(model,
                                       seen_values,
                                       "/usr/share/wayland-sessions",
                                       "wayland",
                                       _("Wayland"));
    append_installed_sessions_from_dir(model,
                                       seen_values,
                                       "/usr/local/share/wayland-sessions",
                                       "wayland",
                                       _("Wayland"));
    append_installed_sessions_from_dir(model,
                                       seen_values,
                                       "/usr/share/xsessions",
                                       "x11",
                                       _("X11"));
    append_installed_sessions_from_dir(model,
                                       seen_values,
                                       "/usr/local/share/xsessions",
                                       "x11",
                                       _("X11"));

    if (session_option_count() == 1) {
        append_session_option(model, seen_values, _("Wayland session"), "wayland");
        append_session_option(model, seen_values, _("X11 session"), "x11");
    }
    append_session_option(model, seen_values, _("Safe session"), "safe");

    g_free(user_x11_sessions);
    g_free(user_wayland_sessions);

    gtk_drop_down_set_model(GTK_DROP_DOWN(g_session_dropdown), G_LIST_MODEL(model));

    guint index = 0;
    if (preferred_value && *preferred_value && session_value_exists(preferred_value)) {
        index = find_session_index(preferred_value);
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_session_dropdown), index);

    g_hash_table_unref(seen_values);
    g_object_unref(model);
}

static void on_avatar_file_dialog_response(GObject *source, GAsyncResult *res, gpointer user_data)
{
    AvatarPickContext *ctx = user_data;
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);

    if (file) {
        char *path = g_file_get_path(file);
        if (path && GTK_IS_EDITABLE(ctx->entry)) {
            gtk_editable_set_text(GTK_EDITABLE(ctx->entry), path);
        }
        g_free(path);
        g_object_unref(file);
    }

    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        status_set(_("Could not open profile image picker"), TRUE);
    }
    g_clear_error(&error);

    g_object_unref(ctx->entry);
    g_free(ctx);
}

static void open_avatar_file_dialog_for_entry(GtkWidget *entry)
{
    if (!entry) {
        return;
    }

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Choose profile image"));
    gtk_file_dialog_set_accept_label(dialog, _("Select"));

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, _("Image files"));
    gtk_file_filter_add_suffix(images, "png");
    gtk_file_filter_add_suffix(images, "jpg");
    gtk_file_filter_add_suffix(images, "jpeg");
    gtk_file_filter_add_suffix(images, "webp");
    gtk_file_filter_add_suffix(images, "bmp");
    gtk_file_filter_add_suffix(images, "gif");
    g_list_store_append(filters, images);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, images);

    GFile *home = g_file_new_for_path(g_get_home_dir());
    gtk_file_dialog_set_initial_folder(dialog, home);
    g_object_unref(home);

    AvatarPickContext *ctx = g_new0(AvatarPickContext, 1);
    ctx->entry = g_object_ref(entry);

    GtkRoot *root = gtk_widget_get_root(entry);
    GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
    gtk_file_dialog_open(dialog, parent, NULL, on_avatar_file_dialog_response, ctx);

    g_object_unref(images);
    g_object_unref(filters);
    g_object_unref(dialog);
}

static void on_choose_avatar_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    open_avatar_file_dialog_for_entry(GTK_WIDGET(data));
}

static GtkWidget *create_avatar_picker_row(const char *title, GtkWidget **entry_out)
{
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("No profile image selected"));
    gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
    gtk_widget_set_size_request(entry, 320, -1);

    GtkWidget *pick_btn = gtk_button_new_with_label(_("Choose from disk"));
    gtk_button_set_icon_name(GTK_BUTTON(pick_btn), "document-open-symbolic");
    g_signal_connect(pick_btn, "clicked", G_CALLBACK(on_choose_avatar_clicked), entry);

    GtkWidget *control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_hexpand(control, TRUE);
    gtk_box_append(GTK_BOX(control), entry);
    gtk_box_append(GTK_BOX(control), pick_btn);

    if (entry_out) {
        *entry_out = entry;
    }

    return create_row(title, control);
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

static char *users_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "users.conf", NULL);
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

static char *selected_user_name(void)
{
    char *selected = dropdown_selected_text(g_users_dropdown);
    if (!selected || !*selected) {
        g_free(selected);
        return NULL;
    }

    if (g_strcmp0(selected, _("No local users found")) == 0) {
        g_free(selected);
        return NULL;
    }

    return selected;
}

static gboolean is_valid_username(const char *name)
{
    if (!name || !*name) {
        return FALSE;
    }

    if (!(g_ascii_isalpha(name[0]) || name[0] == '_')) {
        return FALSE;
    }

    for (guint i = 1; name[i] != '\0'; i++) {
        if (!(g_ascii_isalnum(name[i]) || name[i] == '_' || name[i] == '-')) {
            return FALSE;
        }
    }

    return TRUE;
}

static char *normalize_groups_list(const char *input)
{
    if (!input || !*input) {
        return g_strdup("");
    }

    gchar **parts = g_strsplit(input, ",", -1);
    GPtrArray *clean = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; parts[i] != NULL; i++) {
        char *item = g_strdup(parts[i]);
        g_strstrip(item);
        if (!*item) {
            g_free(item);
            continue;
        }
        g_ptr_array_add(clean, item);
    }

    GString *out = g_string_new(NULL);
    for (guint i = 0; i < clean->len; i++) {
        const char *item = g_ptr_array_index(clean, i);
        if (i > 0) {
            g_string_append_c(out, ',');
        }
        g_string_append(out, item);
    }

    char *result = g_string_free(out, FALSE);
    g_ptr_array_free(clean, TRUE);
    g_strfreev(parts);
    return result;
}

static gboolean user_exists(const char *username)
{
    if (!username || !*username) {
        return FALSE;
    }

    char *q_user = g_shell_quote(username);
    char *cmd = g_strdup_printf("sh -lc 'id -u %s >/dev/null 2>&1'", q_user);
    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(q_user);
    return ok;
}

static void refresh_users_list(void)
{
    char *selected_before = selected_user_name();
    GtkStringList *model = gtk_string_list_new(NULL);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean appended = FALSE;

    char *stdout_data = NULL;
    gboolean ok = run_command_capture(
        "sh -lc 'if command -v getent >/dev/null 2>&1; then getent passwd 2>/dev/null; elif [ -r /etc/passwd ]; then cat /etc/passwd 2>/dev/null; fi'",
        &stdout_data,
        NULL,
        NULL);

    if (ok && stdout_data && *stdout_data) {
        gchar **lines = g_strsplit(stdout_data, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            if (!lines[i][0]) {
                continue;
            }

            gchar **parts = g_strsplit(lines[i], ":", 7);
            const char *name = (parts[0] && parts[0][0]) ? parts[0] : NULL;
            long uid = (parts[2] && parts[2][0]) ? g_ascii_strtoll(parts[2], NULL, 10) : -1;
            const char *shell = (parts[6] && parts[6][0]) ? parts[6] : "";

            gboolean include = TRUE;
            if (!name || g_strcmp0(name, "nobody") == 0) {
                include = FALSE;
            }
            if (include && uid != 0 && uid < 1000) {
                include = FALSE;
            }
            if (include && !is_login_shell(shell)) {
                include = FALSE;
            }

            if (include && !g_hash_table_contains(seen, name)) {
                g_hash_table_add(seen, g_strdup(name));
                gtk_string_list_append(model, name);
                appended = TRUE;
            }

            g_strfreev(parts);

            if (g_hash_table_size(seen) > 250) {
                break;
            }
        }
        g_strfreev(lines);
    }

    g_free(stdout_data);

    if (!appended) {
        gtk_string_list_append(model, _("No local users found"));
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(g_users_dropdown), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_users_dropdown), 0);

    if (selected_before && *selected_before) {
        GListModel *active_model = gtk_drop_down_get_model(GTK_DROP_DOWN(g_users_dropdown));
        guint n_items = active_model ? g_list_model_get_n_items(active_model) : 0;
        for (guint i = 0; i < n_items; i++) {
            GObject *item = g_list_model_get_item(active_model, i);
            if (!item || !GTK_IS_STRING_OBJECT(item)) {
                g_clear_object(&item);
                continue;
            }

            const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
            if (g_strcmp0(text, selected_before) == 0) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(g_users_dropdown), i);
                g_object_unref(item);
                break;
            }
            g_object_unref(item);
        }
    }

    g_object_unref(model);
    g_hash_table_unref(seen);
    g_free(selected_before);
}

static void save_users_config(void)
{
    GKeyFile *kf = g_key_file_new();

    char *selected_user = selected_user_name();
    const char *session = dropdown_selected_session_value(g_session_dropdown);

    g_key_file_set_boolean(kf, "users", "autologin", gtk_switch_get_active(GTK_SWITCH(g_autologin_switch)));
    g_key_file_set_boolean(kf, "users", "admin", gtk_switch_get_active(GTK_SWITCH(g_admin_switch)));
    g_key_file_set_integer(kf, "users", "session_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_session_dropdown)));

    g_key_file_set_string(kf, "users", "selected_user", selected_user ? selected_user : "");
    g_key_file_set_string(kf, "users", "session", session ? session : "default");
    g_key_file_set_string(kf, "users", "avatar_path", gtk_editable_get_text(GTK_EDITABLE(g_avatar_path_entry)));
    g_key_file_set_string(kf, "users", "groups", gtk_editable_get_text(GTK_EDITABLE(g_groups_entry)));
    g_key_file_set_string(kf, "users", "new_username", gtk_editable_get_text(GTK_EDITABLE(g_new_username_entry)));
    g_key_file_set_string(kf, "users", "new_fullname", gtk_editable_get_text(GTK_EDITABLE(g_new_fullname_entry)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = users_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_free(selected_user);
    g_key_file_unref(kf);
}

static void load_users_config(void)
{
    char *path = users_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean autologin = g_key_file_get_boolean(kf, "users", "autologin", &error);
    if (error) {
        g_clear_error(&error);
        autologin = FALSE;
    }

    gboolean admin = g_key_file_get_boolean(kf, "users", "admin", &error);
    if (error) {
        g_clear_error(&error);
        admin = FALSE;
    }

    int session_idx = g_key_file_get_integer(kf, "users", "session_idx", &error);
    if (error) {
        g_clear_error(&error);
        session_idx = 0;
    }

    char *session = g_key_file_get_string(kf, "users", "session", &error);
    if (error) {
        g_clear_error(&error);
        session = g_strdup("");
    }

    char *selected_user = g_key_file_get_string(kf, "users", "selected_user", &error);
    if (error) {
        g_clear_error(&error);
        selected_user = g_strdup("");
    }

    char *avatar_path = g_key_file_get_string(kf, "users", "avatar_path", &error);
    if (error) {
        g_clear_error(&error);
        avatar_path = g_strdup("");
    }

    char *groups = g_key_file_get_string(kf, "users", "groups", &error);
    if (error) {
        g_clear_error(&error);
        groups = g_strdup("");
    }

    char *new_username = g_key_file_get_string(kf, "users", "new_username", &error);
    if (error) {
        g_clear_error(&error);
        new_username = g_strdup("");
    }

    char *new_fullname = g_key_file_get_string(kf, "users", "new_fullname", &error);
    if (error) {
        g_clear_error(&error);
        new_fullname = g_strdup("");
    }

    session_idx = clamp_int(session_idx, 0, (int)session_option_count() - 1);

    gtk_switch_set_active(GTK_SWITCH(g_autologin_switch), autologin);
    gtk_switch_set_active(GTK_SWITCH(g_admin_switch), admin);

    if (session && *session && session_value_exists(session)) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_session_dropdown), find_session_index(session));
    } else {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_session_dropdown), (guint)session_idx);
    }

    gtk_editable_set_text(GTK_EDITABLE(g_avatar_path_entry), avatar_path);
    gtk_editable_set_text(GTK_EDITABLE(g_groups_entry), groups);
    gtk_editable_set_text(GTK_EDITABLE(g_new_username_entry), new_username);
    gtk_editable_set_text(GTK_EDITABLE(g_new_fullname_entry), new_fullname);

    if (selected_user && *selected_user) {
        GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(g_users_dropdown));
        guint n_items = model ? g_list_model_get_n_items(model) : 0;
        for (guint i = 0; i < n_items; i++) {
            GObject *item = g_list_model_get_item(model, i);
            if (item && GTK_IS_STRING_OBJECT(item)) {
                const char *txt = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
                if (g_strcmp0(txt, selected_user) == 0) {
                    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_users_dropdown), i);
                    g_object_unref(item);
                    break;
                }
            }
            g_clear_object(&item);
        }
    }

    g_free(new_fullname);
    g_free(new_username);
    g_free(groups);
    g_free(avatar_path);
    g_free(session);
    g_free(selected_user);
    g_key_file_unref(kf);
    g_free(path);
}

static gboolean apply_avatar_for_user(const char *username, const char *avatar_path, GString *issues)
{
    if (!username || !*username || !avatar_path || !*avatar_path) {
        return TRUE;
    }

    if (!g_file_test(avatar_path, G_FILE_TEST_EXISTS)) {
        g_string_append(issues, _("Avatar file path does not exist. "));
        return FALSE;
    }

    char *dir = g_build_filename(g_get_home_dir(), ".config", "karton", "users", username, NULL);
    char *dest_path = g_build_filename(dir, "avatar", NULL);

    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_string_append(issues, _("Could not create user avatar directory. "));
        g_free(dest_path);
        g_free(dir);
        return FALSE;
    }

    GFile *src = g_file_new_for_path(avatar_path);
    GFile *dst = g_file_new_for_path(dest_path);
    GError *error = NULL;

    gboolean ok = g_file_copy(src,
                              dst,
                              G_FILE_COPY_OVERWRITE,
                              NULL,
                              NULL,
                              NULL,
                              &error);

    if (!ok) {
        g_string_append(issues, _("Could not copy selected avatar image. "));
        g_clear_error(&error);
    }

    g_object_unref(dst);
    g_object_unref(src);
    g_free(dest_path);
    g_free(dir);

    return ok;
}

static char *apply_runtime_users(const char *password)
{
    GString *issues = g_string_new(NULL);

    char *selected_user = selected_user_name();
    const char *new_username_text = gtk_editable_get_text(GTK_EDITABLE(g_new_username_entry));
    const char *new_fullname_text = gtk_editable_get_text(GTK_EDITABLE(g_new_fullname_entry));
    const char *password_text = gtk_editable_get_text(GTK_EDITABLE(g_password_entry));
    const char *groups_text = gtk_editable_get_text(GTK_EDITABLE(g_groups_entry));
    const char *avatar_path = gtk_editable_get_text(GTK_EDITABLE(g_avatar_path_entry));
    gboolean admin_enabled = gtk_switch_get_active(GTK_SWITCH(g_admin_switch));
    gboolean autologin_enabled = gtk_switch_get_active(GTK_SWITCH(g_autologin_switch));

    char *new_username = g_strdup(new_username_text ? new_username_text : "");
    char *new_fullname = g_strdup(new_fullname_text ? new_fullname_text : "");
    g_strstrip(new_username);
    g_strstrip(new_fullname);

    if (new_username[0] != '\0') {
        if (!is_valid_username(new_username)) {
            g_string_append(issues, _("Invalid username for new account. "));
        } else if (user_exists(new_username)) {
            g_string_append(issues, _("User already exists. "));
        } else {
            char *q_user = g_shell_quote(new_username);
            char *script = NULL;

            if (new_fullname[0] != '\0') {
                char *q_fullname = g_shell_quote(new_fullname);
                script = g_strdup_printf("useradd -m -s /bin/bash -c %s %s", q_fullname, q_user);
                g_free(q_fullname);
            } else {
                script = g_strdup_printf("useradd -m -s /bin/bash %s", q_user);
            }

            if (!run_privileged_script_with_password(script, password)) {
                g_string_append(issues, _("Could not create new user account (missing privileges). "));
            } else {
                refresh_users_list();
                g_free(selected_user);
                selected_user = g_strdup(new_username);
            }

            g_free(script);
            g_free(q_user);
        }
    }

    if (selected_user && selected_user[0] != '\0' && password_text && password_text[0] != '\0') {
        char *pair = g_strdup_printf("%s:%s", selected_user, password_text);
        char *q_pair = g_shell_quote(pair);
        char *script = g_strdup_printf("echo %s | chpasswd", q_pair);

        if (!run_privileged_script_with_password(script, password)) {
            g_string_append(issues, _("Could not set user password (missing privileges). "));
        }

        g_free(script);
        g_free(q_pair);
        g_free(pair);
    }

    if (selected_user && selected_user[0] != '\0') {
        char *normalized_groups = normalize_groups_list(groups_text);
        if (normalized_groups[0] != '\0') {
            char *q_user = g_shell_quote(selected_user);
            char *q_groups = g_shell_quote(normalized_groups);
            char *script = g_strdup_printf("usermod -aG %s %s", q_groups, q_user);

            if (!run_privileged_script_with_password(script, password)) {
                g_string_append(issues, _("Could not apply additional user groups (missing privileges). "));
            }

            g_free(script);
            g_free(q_groups);
            g_free(q_user);
        }

        if (admin_enabled) {
            char *q_user = g_shell_quote(selected_user);
            char *script = g_strdup_printf("if getent group wheel >/dev/null 2>&1; then usermod -aG wheel %s; fi; if getent group sudo >/dev/null 2>&1; then usermod -aG sudo %s; fi", q_user, q_user);

            if (!run_privileged_script_with_password(script, password)) {
                g_string_append(issues, _("Could not grant administrator rights (missing privileges). "));
            }

            g_free(script);
            g_free(q_user);
        }

        g_free(normalized_groups);

        (void)apply_avatar_for_user(selected_user, avatar_path, issues);
    }

    const char *session = dropdown_selected_session_value(g_session_dropdown);

    if (autologin_enabled && (!selected_user || !selected_user[0])) {
        g_string_append(issues, _("Select a user before enabling autologin. "));
    }

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_AUTOLOGIN_ENABLED=%s\n"
                           "KARTON_AUTOLOGIN_USER=%s\n"
                           "KARTON_DEFAULT_SESSION=%s",
                           autologin_enabled ? "1" : "0",
                           selected_user ? selected_user : "",
                           session ? session : "default");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed users env",
                                              "# END KartON managed users env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist users/session environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *q_user = g_shell_quote(selected_user ? selected_user : "");
        char *q_session = g_shell_quote(session ? session : "default");
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_AUTOLOGIN_ENABLED=%s KARTON_AUTOLOGIN_USER=%s KARTON_DEFAULT_SESSION=%s >/dev/null 2>&1 || true'",
            autologin_enabled ? "1" : "0",
            q_user,
            q_session);
        (void)run_command_success(cmd);
        g_free(cmd);
        g_free(q_session);
        g_free(q_user);
    }

    g_free(env_path);
    g_string_free(env_block, TRUE);

    gtk_editable_set_text(GTK_EDITABLE(g_password_entry), "");

    g_free(new_fullname);
    g_free(new_username);
    g_free(selected_user);

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

static void on_refresh_users_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    char *session_before = g_strdup(dropdown_selected_session_value(g_session_dropdown));
    refresh_users_list();
    refresh_session_options(session_before);
    g_free(session_before);
    status_set(_("Users list refreshed"), FALSE);
}

static void on_reload_users_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_users_list();
    refresh_session_options(NULL);
    load_users_config();
    status_set(_("Users settings reloaded"), FALSE);
}

static void on_delete_user_clicked(GtkButton *btn, gpointer data)
{
    (void)data;

    char *selected_user = selected_user_name();
    if (!selected_user || !*selected_user) {
        status_set(_("Select a user to delete."), TRUE);
        g_free(selected_user);
        return;
    }

    if (g_strcmp0(selected_user, "root") == 0) {
        status_set(_("Deleting the root user is not allowed."), TRUE);
        g_free(selected_user);
        return;
    }

    const char *current_user = g_get_user_name();
    if (current_user && g_strcmp0(selected_user, current_user) == 0) {
        status_set(_("Deleting the currently logged in user is not allowed."), TRUE);
        g_free(selected_user);
        return;
    }

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
    GtkWidget *parent = GTK_IS_WINDOW(root) ? GTK_WIDGET(root) : NULL;

    gboolean cancelled = FALSE;
    if (!prompt_confirm_delete_dialog(parent, selected_user, &cancelled)) {
        if (cancelled) {
            status_set(_("Deleting user account was canceled."), TRUE);
        }
        g_free(selected_user);
        return;
    }

    char *password = NULL;
    cancelled = FALSE;

    if (geteuid() != 0) {
        if (!prompt_password_dialog(parent, &password, &cancelled)) {
            if (cancelled) {
                status_set(_("Deleting user account was canceled."), TRUE);
            } else {
                status_set(_("Administrator password is required to delete users."), TRUE);
            }
            g_free(selected_user);
            return;
        }

        if (!verify_password_with_sudo(password)) {
            status_set(_("Authentication failed: invalid administrator password."), TRUE);
            g_free(password);
            g_free(selected_user);
            return;
        }
    }

    char *q_user = g_shell_quote(selected_user);
    char *script = g_strdup_printf("userdel -r %s >/dev/null 2>&1 || userdel %s >/dev/null 2>&1", q_user, q_user);
    gboolean ok = run_privileged_script_with_password(script, password);

    g_free(script);
    g_free(q_user);
    g_free(password);

    if (!ok) {
        status_set(_("Could not delete user account (missing privileges or user is currently in use)."), TRUE);
        g_free(selected_user);
        return;
    }

    if (gtk_switch_get_active(GTK_SWITCH(g_autologin_switch))) {
        gtk_switch_set_active(GTK_SWITCH(g_autologin_switch), FALSE);
    }

    refresh_users_list();
    save_users_config();
    refresh_shell_and_top_panel();

    char *msg = g_strdup_printf(_("User '%s' was deleted."), selected_user);
    status_set(msg, FALSE);
    g_free(msg);
    g_free(selected_user);
}

static void on_apply_users_clicked(GtkButton *btn, gpointer data)
{
    (void)data;

    char *password = NULL;
    gboolean cancelled = FALSE;

    if (geteuid() != 0) {
        GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
        GtkWidget *parent = GTK_IS_WINDOW(root) ? GTK_WIDGET(root) : NULL;

        if (!prompt_password_dialog(parent, &password, &cancelled)) {
            if (cancelled) {
                status_set(_("Applying users and accounts settings was canceled."), TRUE);
            } else {
                status_set(_("Administrator password is required to apply users and accounts settings."), TRUE);
            }
            return;
        }

        if (!verify_password_with_sudo(password)) {
            status_set(_("Authentication failed: invalid administrator password."), TRUE);
            g_free(password);
            return;
        }
    }

    save_users_config();

    char *issues = apply_runtime_users(password);
    g_free(password);
    refresh_shell_and_top_panel();

    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Users and accounts settings applied"), FALSE);
}

GtkWidget *page_users_new(void)
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

    GtkWidget *title = gtk_label_new(_("Users and accounts"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Manage accounts, passwords, autologin, profile image, user groups, administrator rights and default sessions."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *accounts_frame = create_section(_("Accounts"),
                                               _("Select existing users, create new user accounts and remove accounts you no longer need."));
    GtkWidget *accounts_box = gtk_frame_get_child(GTK_FRAME(accounts_frame));

    g_users_dropdown = gtk_drop_down_new(NULL, NULL);
    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh"));
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_users_clicked), NULL);

    GtkWidget *delete_btn = gtk_button_new_with_label(_("Delete selected user"));
    gtk_widget_add_css_class(delete_btn, "destructive-action");
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_delete_user_clicked), NULL);

    GtkWidget *users_control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(users_control), g_users_dropdown);
    gtk_box_append(GTK_BOX(users_control), refresh_btn);
    gtk_box_append(GTK_BOX(users_control), delete_btn);

    gtk_box_append(GTK_BOX(accounts_box), create_row(_("Existing users"), users_control));
    gtk_box_append(GTK_BOX(accounts_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(accounts_box), create_entry_row(_("New account username"), _("newuser"), &g_new_username_entry));
    gtk_box_append(GTK_BOX(accounts_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(accounts_box), create_entry_row(_("New account full name"), _("New User"), &g_new_fullname_entry));

    gtk_box_append(GTK_BOX(box), accounts_frame);

    GtkWidget *auth_frame = create_section(_("Authentication and sessions"),
                                           _("Set user password, autologin and default session."));
    GtkWidget *auth_box = gtk_frame_get_child(GTK_FRAME(auth_frame));

    g_session_dropdown = gtk_drop_down_new(NULL, NULL);

    g_password_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_password_entry), _("Leave empty to keep current password"));
    gtk_entry_set_visibility(GTK_ENTRY(g_password_entry), FALSE);
    gtk_widget_set_size_request(g_password_entry, 320, -1);

    g_autologin_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(auth_box), create_row(_("Set password"), g_password_entry));
    gtk_box_append(GTK_BOX(auth_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(auth_box), create_row(_("Autologin"), g_autologin_switch));
    gtk_box_append(GTK_BOX(auth_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(auth_box), create_row(_("Default session"), g_session_dropdown));

    gtk_box_append(GTK_BOX(box), auth_frame);

    GtkWidget *profile_frame = create_section(_("Profile and permissions"),
                                              _("Configure profile image, user groups and administrator rights."));
    GtkWidget *profile_box = gtk_frame_get_child(GTK_FRAME(profile_frame));

    g_admin_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(profile_box), create_avatar_picker_row(_("Profile image"), &g_avatar_path_entry));
    gtk_box_append(GTK_BOX(profile_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(profile_box), create_entry_row(_("Additional groups"), _("audio,video,network"), &g_groups_entry));
    gtk_box_append(GTK_BOX(profile_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(profile_box), create_row(_("Administrator rights"), g_admin_switch));

    gtk_box_append(GTK_BOX(box), profile_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_users_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply users settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_users_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_autologin_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_admin_switch), FALSE);

    refresh_users_list();
    refresh_session_options("default");
    load_users_config();

    return outer_scroll;
}
