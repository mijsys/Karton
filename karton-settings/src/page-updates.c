#include "page-updates.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define _(s) gettext(s)
#define N_(s) s

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_channel_options[] = {
    { N_("Stable"), "stable" },
    { N_("Testing"), "testing" },
    { N_("Nightly"), "nightly" },
};

static GtkWidget *g_system_updates_switch = NULL;
static GtkWidget *g_repositories_switch = NULL;
static GtkWidget *g_drivers_switch = NULL;
static GtkWidget *g_app_store_switch = NULL;
static GtkWidget *g_flatpak_switch = NULL;
static GtkWidget *g_snap_switch = NULL;
static GtkWidget *g_appimage_switch = NULL;
static GtkWidget *g_auto_updates_switch = NULL;
static GtkWidget *g_channel_dropdown = NULL;
static GtkWidget *g_repositories_text_view = NULL;
static GtkWidget *g_status_label = NULL;
static GtkWidget *g_check_btn = NULL;
static GtkWidget *g_install_btn = NULL;
static GtkWidget *g_reload_btn = NULL;
static GtkWidget *g_apply_btn = NULL;
static GtkWidget *g_edit_repos_btn = NULL;
static GtkWidget *g_gate_hint_label = NULL;
static guint g_updates_gate_source_id = 0;

static void status_set(const char *text, gboolean is_error);
static char *session_environment_path(void);

static const char *detect_package_backend(void);
static void repositories_list_set_text(const char *text);

static gboolean session_is_graphical(void)
{
    return (g_getenv("WAYLAND_DISPLAY") && *g_getenv("WAYLAND_DISPLAY"))
        || (g_getenv("DISPLAY") && *g_getenv("DISPLAY"));
}

static gboolean parse_truthy(const char *value)
{
    if (!value) {
        return FALSE;
    }

    while (*value && g_ascii_isspace(*value)) {
        value++;
    }

    return g_ascii_strcasecmp(value, "1") == 0
        || g_ascii_strcasecmp(value, "yes") == 0
        || g_ascii_strcasecmp(value, "true") == 0
        || g_ascii_strcasecmp(value, "on") == 0;
}

static char *advanced_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "advanced.conf", NULL);
}

static gboolean read_experimental_from_environment_file(gboolean *out_enabled)
{
    if (!out_enabled) {
        return FALSE;
    }

    char *env_path = session_environment_path();
    char *contents = NULL;
    gboolean found = FALSE;

    if (g_file_get_contents(env_path, &contents, NULL, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            if (!g_str_has_prefix(lines[i], "KARTON_ADV_EXPERIMENTAL=")) {
                continue;
            }

            const char *value = lines[i] + sizeof("KARTON_ADV_EXPERIMENTAL=") - 1;
            *out_enabled = parse_truthy(value);
            found = TRUE;
            break;
        }
        g_strfreev(lines);
    }

    g_free(contents);
    g_free(env_path);
    return found;
}

static gboolean updates_experimental_enabled(void)
{
    char *adv_path = advanced_config_path();
    GKeyFile *kf = g_key_file_new();
    gboolean enabled = FALSE;
    gboolean has_config_value = FALSE;

    if (g_key_file_load_from_file(kf, adv_path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        enabled = g_key_file_get_boolean(kf, "advanced", "experimental", &error);
        if (error) {
            g_clear_error(&error);
            enabled = FALSE;
        } else {
            has_config_value = TRUE;
        }
    }

    g_key_file_unref(kf);
    g_free(adv_path);

    if (has_config_value) {
        return enabled;
    }

    if (read_experimental_from_environment_file(&enabled)) {
        return enabled;
    }

    const char *env_direct = g_getenv("KARTON_ADV_EXPERIMENTAL");
    if (env_direct && *env_direct) {
        return parse_truthy(env_direct);
    }

    return enabled;
}

static void updates_apply_gate_state(gboolean enabled)
{
    if (g_system_updates_switch) gtk_widget_set_sensitive(g_system_updates_switch, enabled);
    if (g_repositories_switch) gtk_widget_set_sensitive(g_repositories_switch, enabled);
    if (g_drivers_switch) gtk_widget_set_sensitive(g_drivers_switch, enabled);
    if (g_auto_updates_switch) gtk_widget_set_sensitive(g_auto_updates_switch, enabled);
    if (g_channel_dropdown) gtk_widget_set_sensitive(g_channel_dropdown, enabled);
    if (g_repositories_text_view) gtk_widget_set_sensitive(g_repositories_text_view, enabled);
    if (g_app_store_switch) gtk_widget_set_sensitive(g_app_store_switch, enabled);
    if (g_flatpak_switch) gtk_widget_set_sensitive(g_flatpak_switch, enabled);
    if (g_snap_switch) gtk_widget_set_sensitive(g_snap_switch, enabled);
    if (g_appimage_switch) gtk_widget_set_sensitive(g_appimage_switch, enabled);
    if (g_check_btn) gtk_widget_set_sensitive(g_check_btn, enabled);
    if (g_install_btn) gtk_widget_set_sensitive(g_install_btn, enabled);
    if (g_apply_btn) gtk_widget_set_sensitive(g_apply_btn, enabled);
    if (g_edit_repos_btn) gtk_widget_set_sensitive(g_edit_repos_btn, enabled);

    if (g_reload_btn) {
        gtk_widget_set_sensitive(g_reload_btn, TRUE);
    }

    if (g_gate_hint_label) {
        gtk_widget_set_visible(g_gate_hint_label, !enabled);
    }
}

static gboolean ensure_updates_feature_enabled(void)
{
    gboolean enabled = updates_experimental_enabled();
    updates_apply_gate_state(enabled);

    if (!enabled) {
        status_set(_("Updates and software are disabled. Enable Experimental features in Advanced and apply settings first."), TRUE);
    }

    return enabled;
}

static gboolean updates_gate_tick(gpointer data)
{
    (void)data;
    updates_apply_gate_state(updates_experimental_enabled());
    return G_SOURCE_CONTINUE;
}

static char *deferred_desktop_update_command_for_backend(const char *backend)
{
    if (g_strcmp0(backend, "pacman") == 0) {
        return g_strdup("sudo pacman -Syu tektura kartonde karton-shell karton-session karton-settings karton-files karton-terminal karton-idle karton-lock");
    }

    if (g_strcmp0(backend, "dnf") == 0) {
        return g_strdup("sudo dnf upgrade 'tektura*' 'kartonde*' 'karton-shell*' 'karton-session*' 'karton-settings*' 'karton-files*' 'karton-terminal*' 'karton-idle*' 'karton-lock*'");
    }

    if (g_strcmp0(backend, "zypper") == 0) {
        return g_strdup("sudo zypper update tektura kartonde karton-shell karton-session karton-settings karton-files karton-terminal karton-idle karton-lock");
    }

    if (g_strcmp0(backend, "apt") == 0) {
        return g_strdup("sudo apt-get update && sudo apt-get install --only-upgrade tektura kartonde karton-shell karton-session karton-settings karton-files karton-terminal karton-idle karton-lock");
    }

    return NULL;
}

typedef struct {
    GMainLoop *loop;
    gint response_id;
} PasswordDialogState;

typedef struct {
    GtkTextBuffer *buffer;
    GMainLoop *loop;
    GPid child_pid;
    gint wait_status;
    gboolean child_exited;
} UpdateInstallRunState;

typedef struct {
    UpdateInstallRunState *run;
    gboolean is_stderr;
} UpdateInstallChannelCtx;

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

static char *updates_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "updates.conf", NULL);
}

static char *session_environment_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "environment", NULL);
}

static char *repositories_list_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "repositories.list", NULL);
}

static char *repositories_system_path(void)
{
    const char *backend = detect_package_backend();

    if (g_strcmp0(backend, "pacman") == 0) {
        return g_strdup("/etc/pacman.conf");
    }

    if (g_strcmp0(backend, "apt") == 0) {
        return g_strdup("/etc/apt/sources.list");
    }

    if (g_strcmp0(backend, "dnf") == 0) {
        return g_strdup("/etc/yum.repos.d/karton-settings.repo");
    }

    if (g_strcmp0(backend, "zypper") == 0) {
        return g_strdup("/etc/zypp/repos.d/karton-settings.repo");
    }

    return NULL;
}

static gboolean load_repositories_from_file(const char *path)
{
    if (!path || !*path) {
        return FALSE;
    }

    char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, NULL)) {
        g_free(contents);
        return FALSE;
    }

    repositories_list_set_text(contents ? contents : "");
    g_free(contents);
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

static gboolean write_repositories_system_file(const char *path,
                                               const char *text,
                                               const char *password,
                                               gboolean *auth_failed)
{
    if (auth_failed) {
        *auth_failed = FALSE;
    }

    if (!path || !*path) {
        return FALSE;
    }

    if (geteuid() == 0) {
        return g_file_set_contents(path, text ? text : "", -1, NULL);
    }

    if (!command_is_available("sudo") || !password || !*password) {
        if (auth_failed) {
            *auth_failed = TRUE;
        }
        return FALSE;
    }

    if (!verify_password_with_sudo(password)) {
        if (auth_failed) {
            *auth_failed = TRUE;
        }
        return FALSE;
    }

    char *q_path = g_shell_quote(path);
    char *script = g_strdup_printf("cat > %s", q_path);
    char *q_script = g_shell_quote(script);
    char *cmd = g_strdup_printf("sudo -S -k -p '' sh -lc %s", q_script);
    char *stdin_payload = g_strdup_printf("%s\n%s", password, text ? text : "");

    gboolean ok = run_command_with_stdin_capture(cmd, stdin_payload, NULL, NULL);

    g_free(stdin_payload);
    g_free(cmd);
    g_free(q_script);
    g_free(script);
    g_free(q_path);

    return ok;
}

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

    GtkWidget *help = gtk_label_new(_("Type your administrator password to continue."));
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

static char *default_repositories_template(void)
{
    const char *backend = detect_package_backend();

    if (g_strcmp0(backend, "apt") == 0) {
        return g_strdup(
            "# APT repositories list\n"
            "# Example:\n"
            "# deb http://archive.ubuntu.com/ubuntu/ noble main restricted universe multiverse\n"
            "# deb http://security.ubuntu.com/ubuntu/ noble-security main restricted universe multiverse\n");
    }

    if (g_strcmp0(backend, "dnf") == 0) {
        return g_strdup(
            "# DNF repositories list\n"
            "# Example:\n"
            "# [fedora]\n"
            "# name=Fedora\n"
            "# baseurl=https://download.example.org/fedora/$releasever/$basearch/os/\n"
            "# enabled=1\n"
            "# gpgcheck=1\n");
    }

    if (g_strcmp0(backend, "pacman") == 0) {
        return g_strdup(
            "# pacman repositories list\n"
            "# Example:\n"
            "# [core]\n"
            "# Include = /etc/pacman.d/mirrorlist\n"
            "# [extra]\n"
            "# Include = /etc/pacman.d/mirrorlist\n");
    }

    if (g_strcmp0(backend, "zypper") == 0) {
        return g_strdup(
            "# zypper repositories list\n"
            "# Example:\n"
            "# [repo-oss]\n"
            "# name=openSUSE OSS\n"
            "# baseurl=http://download.opensuse.org/distribution/leap/$releasever/repo/oss/\n"
            "# enabled=1\n"
            "# gpgcheck=1\n");
    }

    return g_strdup(
        "# Repositories list\n"
        "# Add entries in the format used by your package manager.\n");
}

static char *update_command_for_backend(const char *backend, gboolean protect_session)
{
    if (g_strcmp0(backend, "apt") == 0) {
        if (protect_session) {
            return g_strdup(
                "DEBIAN_FRONTEND=noninteractive apt-get update && "
                "DEBIAN_FRONTEND=noninteractive apt-get -y upgrade && "
                "printf '\\nNote: KartON packages are not excluded on APT backend. "
                "If you update karton/tektura packages, log out first to avoid session disruption.\\n'");
        }
        return g_strdup("DEBIAN_FRONTEND=noninteractive apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -y upgrade");
    }

    if (g_strcmp0(backend, "dnf") == 0) {
        if (protect_session) {
            return g_strdup(
                "dnf -y upgrade --refresh "
                "--exclude=tektura* --exclude=kartonde* --exclude=karton-shell* "
                "--exclude=karton-session* --exclude=karton-settings* --exclude=karton-files* "
                "--exclude=karton-terminal* --exclude=karton-idle* --exclude=karton-lock*");
        }
        return g_strdup("dnf -y upgrade --refresh");
    }

    if (g_strcmp0(backend, "pacman") == 0) {
        if (protect_session) {
            return g_strdup(
                "pacman -Syu --noconfirm "
                "--ignore tektura,kartonde,karton-shell,karton-session,karton-settings,"
                "karton-files,karton-terminal,karton-idle,karton-lock");
        }
        return g_strdup("pacman -Syu --noconfirm");
    }

    if (g_strcmp0(backend, "zypper") == 0) {
        if (protect_session) {
            return g_strdup(
                "zypper --non-interactive refresh && "
                "zypper --non-interactive update "
                "--exclude tektura --exclude kartonde --exclude karton-shell --exclude karton-session "
                "--exclude karton-settings --exclude karton-files --exclude karton-terminal "
                "--exclude karton-idle --exclude karton-lock");
        }
        return g_strdup("zypper --non-interactive refresh && zypper --non-interactive update");
    }

    return NULL;
}

static void append_update_log(GtkTextBuffer *buffer, const char *text)
{
    if (!buffer || !text || !*text) {
        return;
    }

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
}

static gboolean on_update_log_io(GIOChannel *source, GIOCondition condition, gpointer user_data)
{
    UpdateInstallChannelCtx *ctx = (UpdateInstallChannelCtx *)user_data;
    if (!ctx || !ctx->run || !ctx->run->buffer || !source) {
        return FALSE;
    }

    while (TRUE) {
        gchar chunk[1024];
        gsize bytes_read = 0;
        GError *error = NULL;
        GIOStatus status = g_io_channel_read_chars(source,
                                                   chunk,
                                                   sizeof(chunk) - 1,
                                                   &bytes_read,
                                                   &error);

        if (status == G_IO_STATUS_ERROR) {
            if (error && error->message) {
                char *msg = g_strdup_printf("[io] %s\n", error->message);
                append_update_log(ctx->run->buffer, msg);
                g_free(msg);
            }
            g_clear_error(&error);
            return FALSE;
        }

        if (bytes_read > 0) {
            chunk[bytes_read] = '\0';
            if (ctx->is_stderr) {
                append_update_log(ctx->run->buffer, "[stderr] ");
            }
            append_update_log(ctx->run->buffer, chunk);
        }

        if (status == G_IO_STATUS_AGAIN) {
            break;
        }

        if (status == G_IO_STATUS_EOF) {
            return FALSE;
        }

        if (bytes_read == 0) {
            break;
        }
    }

    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        return FALSE;
    }

    return TRUE;
}

static void on_update_child_exit(GPid pid, gint status, gpointer user_data)
{
    UpdateInstallRunState *run = (UpdateInstallRunState *)user_data;
    if (!run) {
        return;
    }

    run->child_pid = pid;
    run->wait_status = status;
    run->child_exited = TRUE;
    g_spawn_close_pid(pid);

    if (run->loop) {
        g_main_loop_quit(run->loop);
    }
}

static char *install_updates_now(GtkWidget *parent,
                                 const char *password,
                                 gboolean *auth_failed)
{
    if (auth_failed) {
        *auth_failed = FALSE;
    }

    const char *backend = detect_package_backend();
    gboolean protect_session = session_is_graphical();
    char *update_cmd = update_command_for_backend(backend, protect_session);
    if (!update_cmd) {
        return g_strdup(_("No supported package manager backend found."));
    }

    if (geteuid() != 0) {
        if (!command_is_available("sudo")) {
            return g_strdup(_("Could not start system update: sudo is not available."));
        }

        if (!password || !*password) {
            if (auth_failed) {
                *auth_failed = TRUE;
            }
            return g_strdup(_("Administrator password is required to install updates."));
        }

        if (!verify_password_with_sudo(password)) {
            if (auth_failed) {
                *auth_failed = TRUE;
            }
            return g_strdup(_("Authentication failed: invalid administrator password."));
        }
    }

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Installing system updates"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 460);
    gtk_window_set_resizable(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);

    if (parent && GTK_IS_WINDOW(parent)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    }

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 16);
    gtk_widget_set_margin_end(content, 16);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    const char *info_text = protect_session
        ? _("Update process is running in session-safe mode. KartON session packages are deferred to avoid breaking the current desktop session.")
        : _("Update process is running. Live output is shown below.");
    GtkWidget *info = gtk_label_new(info_text);
    gtk_widget_set_halign(info, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(info), TRUE);

    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_set_halign(spinner, GTK_ALIGN_START);
    gtk_spinner_start(GTK_SPINNER(spinner));

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), log_view);
    GtkTextBuffer *log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));

    gtk_box_append(GTK_BOX(box), info);
    gtk_box_append(GTK_BOX(box), spinner);
    gtk_box_append(GTK_BOX(box), scrolled);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *close_button = gtk_button_new_with_label(_("Close"));
    gtk_widget_set_sensitive(close_button, FALSE);
    g_object_set_data(G_OBJECT(close_button), "response-id", GINT_TO_POINTER(GTK_RESPONSE_CLOSE));
    gtk_box_append(GTK_BOX(buttons), close_button);

    gtk_box_append(GTK_BOX(content), box);
    gtk_box_append(GTK_BOX(content), buttons);
    gtk_window_set_child(GTK_WINDOW(dialog), content);

    gtk_window_present(GTK_WINDOW(dialog));

    append_update_log(log_buffer, _("Starting package manager update...\n"));
        if (protect_session) {
                append_update_log(log_buffer,
                                                    _("Session-safe mode is enabled: desktop environment packages are deferred.\n"
                                                        "Run deferred desktop update after logout from TTY to avoid session crash.\n"));
        }

    GPid child_pid = 0;
    gint stdin_fd = -1;
    gint stdout_fd = -1;
    gint stderr_fd = -1;
    GError *spawn_error = NULL;
    gboolean spawned = FALSE;

    if (geteuid() == 0) {
        gchar *argv_root[] = { "sh", "-lc", (gchar *)update_cmd, NULL };
        spawned = g_spawn_async_with_pipes(NULL,
                                           argv_root,
                                           NULL,
                                           G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                           NULL,
                                           NULL,
                                           &child_pid,
                                           &stdin_fd,
                                           &stdout_fd,
                                           &stderr_fd,
                                           &spawn_error);
    } else {
        gchar *argv_sudo[] = { "sudo", "-S", "-k", "-p", "", "sh", "-lc", (gchar *)update_cmd, NULL };
        spawned = g_spawn_async_with_pipes(NULL,
                                           argv_sudo,
                                           NULL,
                                           G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                           NULL,
                                           NULL,
                                           &child_pid,
                                           &stdin_fd,
                                           &stdout_fd,
                                           &stderr_fd,
                                           &spawn_error);
    }

    if (!spawned) {
        char *msg = g_strdup_printf(_("Could not start update process: %s"),
                                    (spawn_error && spawn_error->message) ? spawn_error->message : "spawn failed");
        g_clear_error(&spawn_error);
        gtk_spinner_stop(GTK_SPINNER(spinner));
        append_update_log(log_buffer, msg);
        append_update_log(log_buffer, "\n");
        g_free(msg);

        gtk_widget_set_sensitive(close_button, TRUE);
        gtk_window_set_deletable(GTK_WINDOW(dialog), TRUE);

        PasswordDialogState close_state = {0};
        close_state.loop = g_main_loop_new(NULL, FALSE);
        close_state.response_id = GTK_RESPONSE_NONE;
        g_signal_connect(close_button, "clicked", G_CALLBACK(on_modal_window_response), &close_state);
        g_signal_connect(dialog, "close-request", G_CALLBACK(on_modal_window_close_request), &close_state);
        g_main_loop_run(close_state.loop);
        g_main_loop_unref(close_state.loop);

        gtk_window_destroy(GTK_WINDOW(dialog));
        g_free(update_cmd);
        return g_strdup(_("System update command failed. Please check package manager logs."));
    }

    if (stdin_fd >= 0) {
        if (geteuid() != 0 && password && *password) {
            char *stdin_payload = g_strdup_printf("%s\n", password);
            size_t payload_len = strlen(stdin_payload);
            ssize_t bytes_written = write(stdin_fd, stdin_payload, payload_len);
            if (bytes_written < 0 || (size_t)bytes_written != payload_len) {
                append_update_log(log_buffer, _("[stderr] Warning: failed to write complete sudo password payload.\n"));
            }
            g_free(stdin_payload);
        }
        close(stdin_fd);
        stdin_fd = -1;
    }

    UpdateInstallRunState run = {0};
    run.buffer = log_buffer;
    run.loop = g_main_loop_new(NULL, FALSE);
    run.child_pid = child_pid;
    run.wait_status = 0;
    run.child_exited = FALSE;

    GIOChannel *stdout_channel = NULL;
    GIOChannel *stderr_channel = NULL;
    guint stdout_watch = 0;
    guint stderr_watch = 0;
    guint child_watch = 0;

    UpdateInstallChannelCtx stdout_ctx = { .run = &run, .is_stderr = FALSE };
    UpdateInstallChannelCtx stderr_ctx = { .run = &run, .is_stderr = TRUE };

    if (stdout_fd >= 0) {
        stdout_channel = g_io_channel_unix_new(stdout_fd);
        g_io_channel_set_encoding(stdout_channel, NULL, NULL);
        g_io_channel_set_flags(stdout_channel,
                               g_io_channel_get_flags(stdout_channel) | G_IO_FLAG_NONBLOCK,
                               NULL);
        stdout_watch = g_io_add_watch(stdout_channel,
                                      G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                      on_update_log_io,
                                      &stdout_ctx);
    }

    if (stderr_fd >= 0) {
        stderr_channel = g_io_channel_unix_new(stderr_fd);
        g_io_channel_set_encoding(stderr_channel, NULL, NULL);
        g_io_channel_set_flags(stderr_channel,
                               g_io_channel_get_flags(stderr_channel) | G_IO_FLAG_NONBLOCK,
                               NULL);
        stderr_watch = g_io_add_watch(stderr_channel,
                                      G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                      on_update_log_io,
                                      &stderr_ctx);
    }

    child_watch = g_child_watch_add(child_pid, on_update_child_exit, &run);

    g_main_loop_run(run.loop);

    if (stdout_channel) {
        gchar *rest = NULL;
        gsize rest_len = 0;
        if (g_io_channel_read_to_end(stdout_channel, &rest, &rest_len, NULL) == G_IO_STATUS_NORMAL && rest) {
            append_update_log(log_buffer, rest);
        }
        g_free(rest);
    }

    if (stderr_channel) {
        gchar *rest = NULL;
        gsize rest_len = 0;
        if (g_io_channel_read_to_end(stderr_channel, &rest, &rest_len, NULL) == G_IO_STATUS_NORMAL && rest) {
            append_update_log(log_buffer, "[stderr] ");
            append_update_log(log_buffer, rest);
        }
        g_free(rest);
    }

    if (stdout_watch > 0) {
        g_source_remove(stdout_watch);
    }
    if (stderr_watch > 0) {
        g_source_remove(stderr_watch);
    }
    if (child_watch > 0) {
        g_source_remove(child_watch);
    }

    if (stdout_channel) {
        g_io_channel_shutdown(stdout_channel, TRUE, NULL);
        g_io_channel_unref(stdout_channel);
    }
    if (stderr_channel) {
        g_io_channel_shutdown(stderr_channel, TRUE, NULL);
        g_io_channel_unref(stderr_channel);
    }

    g_main_loop_unref(run.loop);
    gtk_spinner_stop(GTK_SPINNER(spinner));

    gboolean ok = WIFEXITED(run.wait_status) && WEXITSTATUS(run.wait_status) == 0;
    if (ok) {
        append_update_log(log_buffer, _("\nUpdate process finished successfully.\n"));
        if (protect_session) {
            char *deferred_cmd = deferred_desktop_update_command_for_backend(backend);
            append_update_log(log_buffer,
                              _("Desktop environment update (deferred):\n"
                                "1. Log out from graphical session\n"
                                "2. Switch to TTY (Ctrl+Alt+F3)\n"
                                "3. Run command:\n"));
            append_update_log(log_buffer, deferred_cmd ? deferred_cmd : _("(no backend-specific command available)"));
            append_update_log(log_buffer, "\n");
            g_free(deferred_cmd);
        }
    } else {
        append_update_log(log_buffer, _("\nUpdate process failed.\n"));
    }

    gtk_widget_set_sensitive(close_button, TRUE);
    gtk_window_set_deletable(GTK_WINDOW(dialog), TRUE);

    PasswordDialogState close_state = {0};
    close_state.loop = g_main_loop_new(NULL, FALSE);
    close_state.response_id = GTK_RESPONSE_NONE;
    g_signal_connect(close_button, "clicked", G_CALLBACK(on_modal_window_response), &close_state);
    g_signal_connect(dialog, "close-request", G_CALLBACK(on_modal_window_close_request), &close_state);
    g_main_loop_run(close_state.loop);
    g_main_loop_unref(close_state.loop);

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_free(update_cmd);

    if (ok) {
        return NULL;
    }

    return g_strdup(_("System update command failed. Please check package manager logs."));
}

static char *repositories_list_get_text(void)
{
    if (!g_repositories_text_view) {
        return g_strdup("");
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_repositories_text_view));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void repositories_list_set_text(const char *text)
{
    if (!g_repositories_text_view) {
        return;
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_repositories_text_view));
    gtk_text_buffer_set_text(buffer, text ? text : "", -1);
}

static gboolean save_repositories_list_file(void)
{
    char *path = repositories_list_path();
    char *dir = g_path_get_dirname(path);
    char *text = repositories_list_get_text();
    gboolean ok = FALSE;

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        ok = g_file_set_contents(path, text ? text : "", -1, NULL);
    }

    g_free(text);
    g_free(dir);
    g_free(path);
    return ok;
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

static char *normalize_os_release_value(const char *raw)
{
    if (!raw) {
        return g_strdup("");
    }

    char *value = g_strdup(raw);
    g_strstrip(value);

    gsize len = strlen(value);
    if (len >= 2 && (value[0] == '"' || value[0] == '\'')) {
        char quote = value[0];
        if (value[len - 1] == quote) {
            value[len - 1] = '\0';
            memmove(value, value + 1, len - 1);
        }
    }

    char *lower = g_ascii_strdown(value, -1);
    g_free(value);
    return lower;
}

static gboolean os_release_contains(const char *value, const char *needle)
{
    return value && needle && strstr(value, needle) != NULL;
}

static const char *detect_package_backend(void)
{
    char *contents = NULL;
    char *id = NULL;
    char *id_like = NULL;

    if (g_file_get_contents("/etc/os-release", &contents, NULL, NULL)) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            if (!id && g_str_has_prefix(lines[i], "ID=")) {
                id = normalize_os_release_value(lines[i] + 3);
                continue;
            }

            if (!id_like && g_str_has_prefix(lines[i], "ID_LIKE=")) {
                id_like = normalize_os_release_value(lines[i] + 8);
            }
        }
        g_strfreev(lines);
        g_free(contents);
    }

    if ((os_release_contains(id, "arch")
         || os_release_contains(id, "manjaro")
         || os_release_contains(id, "artix")
         || os_release_contains(id_like, "arch"))
        && command_is_available("pacman")) {
        g_free(id);
        g_free(id_like);
        return "pacman";
    }

    if ((os_release_contains(id, "debian")
         || os_release_contains(id, "ubuntu")
         || os_release_contains(id, "mint")
         || os_release_contains(id, "elementary")
         || os_release_contains(id, "pop")
         || os_release_contains(id_like, "debian")
         || os_release_contains(id_like, "ubuntu"))
        && command_is_available("apt")) {
        g_free(id);
        g_free(id_like);
        return "apt";
    }

    if ((os_release_contains(id, "fedora")
         || os_release_contains(id, "rhel")
         || os_release_contains(id, "centos")
         || os_release_contains(id, "rocky")
         || os_release_contains(id, "alma")
         || os_release_contains(id_like, "fedora")
         || os_release_contains(id_like, "rhel"))
        && command_is_available("dnf")) {
        g_free(id);
        g_free(id_like);
        return "dnf";
    }

    if ((os_release_contains(id, "opensuse")
         || os_release_contains(id, "suse")
         || os_release_contains(id_like, "suse"))
        && command_is_available("zypper")) {
        g_free(id);
        g_free(id_like);
        return "zypper";
    }

    g_free(id);
    g_free(id_like);

    if (command_is_available("pacman")
        && (g_file_test("/var/lib/pacman/local", G_FILE_TEST_IS_DIR)
            || g_file_test("/etc/pacman.conf", G_FILE_TEST_EXISTS))) {
        return "pacman";
    }

    if (command_is_available("apt")
        && (g_file_test("/var/lib/dpkg/status", G_FILE_TEST_EXISTS)
            || g_file_test("/etc/apt/sources.list", G_FILE_TEST_EXISTS))) {
        return "apt";
    }

    if (command_is_available("dnf")) {
        if (g_file_test("/etc/dnf/dnf.conf", G_FILE_TEST_EXISTS)
            || g_file_test("/etc/yum.repos.d", G_FILE_TEST_IS_DIR)) {
            return "dnf";
        }
    }

    if (command_is_available("zypper")) {
        if (g_file_test("/etc/zypp/zypp.conf", G_FILE_TEST_EXISTS)
            || g_file_test("/etc/zypp/repos.d", G_FILE_TEST_IS_DIR)) {
            return "zypper";
        }
    }

    if (command_is_available("pacman")) {
        return "pacman";
    }
    if (command_is_available("apt")) {
        return "apt";
    }
    if (command_is_available("dnf")) {
        return "dnf";
    }
    if (command_is_available("zypper")) {
        return "zypper";
    }

    return "none";
}

static char *check_updates_now(gboolean *is_error)
{
    if (is_error) {
        *is_error = FALSE;
    }

    const char *backend = detect_package_backend();

    if (g_strcmp0(backend, "apt") == 0) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 8s apt list --upgradable 2>/dev/null | sed 1d | head -n 40; else apt list --upgradable 2>/dev/null | sed 1d | head -n 40; fi'",
            &stdout_data,
            NULL,
            NULL);

        if (!ok) {
            g_free(stdout_data);
            if (is_error) {
                *is_error = TRUE;
            }
            return g_strdup(_("Could not check updates via APT."));
        }

        if (!stdout_data || !*stdout_data) {
            g_free(stdout_data);
            return g_strdup(_("No pending updates were found."));
        }

        gchar **lines = g_strsplit(stdout_data, "\n", -1);
        guint count = 0;
        for (guint i = 0; lines[i] != NULL; i++) {
            if (lines[i][0]) {
                count++;
            }
        }

        char *result = g_strdup_printf(_("Pending updates detected: %u packages (APT)."), count);
        g_strfreev(lines);
        g_free(stdout_data);
        return result;
    }

    if (g_strcmp0(backend, "dnf") == 0) {
        int wait_status = 0;
        gboolean ok = run_command_capture(
            "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 10s dnf -q check-update >/dev/null 2>&1; else dnf -q check-update >/dev/null 2>&1; fi'",
            NULL,
            NULL,
            &wait_status);

        if (ok) {
            return g_strdup(_("No pending updates were found."));
        }

        if (WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 100) {
            return g_strdup(_("Pending updates detected (DNF)."));
        }

        if (is_error) {
            *is_error = TRUE;
        }
        return g_strdup(_("Could not check updates via DNF."));
    }

    if (g_strcmp0(backend, "pacman") == 0) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 8s pacman -Qu 2>/dev/null | head -n 40; else pacman -Qu 2>/dev/null | head -n 40; fi'",
            &stdout_data,
            NULL,
            NULL);

        if (!ok) {
            g_free(stdout_data);
            if (is_error) {
                *is_error = TRUE;
            }
            return g_strdup(_("Could not check updates via pacman."));
        }

        if (!stdout_data || !*stdout_data) {
            g_free(stdout_data);
            return g_strdup(_("No pending updates were found."));
        }

        gchar **lines = g_strsplit(stdout_data, "\n", -1);
        guint count = 0;
        for (guint i = 0; lines[i] != NULL; i++) {
            if (lines[i][0]) {
                count++;
            }
        }

        char *result = g_strdup_printf(_("Pending updates detected: %u packages (pacman)."), count);
        g_strfreev(lines);
        g_free(stdout_data);
        return result;
    }

    if (g_strcmp0(backend, "zypper") == 0) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'if command -v timeout >/dev/null 2>&1; then timeout 10s zypper -q lu 2>/dev/null | grep \"^v |\" | head -n 40; else zypper -q lu 2>/dev/null | grep \"^v |\" | head -n 40; fi'",
            &stdout_data,
            NULL,
            NULL);

        if (!ok) {
            g_free(stdout_data);
            if (is_error) {
                *is_error = TRUE;
            }
            return g_strdup(_("Could not check updates via zypper."));
        }

        if (!stdout_data || !*stdout_data) {
            g_free(stdout_data);
            return g_strdup(_("No pending updates were found."));
        }

        gchar **lines = g_strsplit(stdout_data, "\n", -1);
        guint count = 0;
        for (guint i = 0; lines[i] != NULL; i++) {
            if (lines[i][0]) {
                count++;
            }
        }

        char *result = g_strdup_printf(_("Pending updates detected: %u packages (zypper)."), count);
        g_strfreev(lines);
        g_free(stdout_data);
        return result;
    }

    if (is_error) {
        *is_error = TRUE;
    }
    return g_strdup(_("No supported package manager backend found."));
}

static void save_updates_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "updates", "system_updates", gtk_switch_get_active(GTK_SWITCH(g_system_updates_switch)));
    g_key_file_set_boolean(kf, "updates", "repositories", gtk_switch_get_active(GTK_SWITCH(g_repositories_switch)));
    g_key_file_set_boolean(kf, "updates", "drivers", gtk_switch_get_active(GTK_SWITCH(g_drivers_switch)));
    g_key_file_set_boolean(kf, "updates", "app_store", gtk_switch_get_active(GTK_SWITCH(g_app_store_switch)));
    g_key_file_set_boolean(kf, "updates", "flatpak", gtk_switch_get_active(GTK_SWITCH(g_flatpak_switch)));
    g_key_file_set_boolean(kf, "updates", "snap", gtk_switch_get_active(GTK_SWITCH(g_snap_switch)));
    g_key_file_set_boolean(kf, "updates", "appimage", gtk_switch_get_active(GTK_SWITCH(g_appimage_switch)));
    g_key_file_set_boolean(kf, "updates", "auto_updates", gtk_switch_get_active(GTK_SWITCH(g_auto_updates_switch)));

    const char *channel = dropdown_selected_value(g_channel_dropdown, g_channel_options, G_N_ELEMENTS(g_channel_options));
    g_key_file_set_string(kf, "updates", "channel", channel ? channel : "stable");

    char *repositories_list = repositories_list_get_text();
    g_key_file_set_string(kf, "updates", "repositories_list", repositories_list ? repositories_list : "");

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = updates_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_free(repositories_list);
    g_key_file_unref(kf);
}

static void load_updates_config(void)
{
    char *path = updates_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean system_updates = g_key_file_get_boolean(kf, "updates", "system_updates", &error);
    if (error) {
        g_clear_error(&error);
        system_updates = TRUE;
    }

    gboolean repositories = g_key_file_get_boolean(kf, "updates", "repositories", &error);
    if (error) {
        g_clear_error(&error);
        repositories = TRUE;
    }

    gboolean drivers = g_key_file_get_boolean(kf, "updates", "drivers", &error);
    if (error) {
        g_clear_error(&error);
        drivers = TRUE;
    }

    gboolean app_store = g_key_file_get_boolean(kf, "updates", "app_store", &error);
    if (error) {
        g_clear_error(&error);
        app_store = TRUE;
    }

    gboolean flatpak = g_key_file_get_boolean(kf, "updates", "flatpak", &error);
    if (error) {
        g_clear_error(&error);
        flatpak = TRUE;
    }

    gboolean snap = g_key_file_get_boolean(kf, "updates", "snap", &error);
    if (error) {
        g_clear_error(&error);
        snap = FALSE;
    }

    gboolean appimage = g_key_file_get_boolean(kf, "updates", "appimage", &error);
    if (error) {
        g_clear_error(&error);
        appimage = TRUE;
    }

    gboolean auto_updates = g_key_file_get_boolean(kf, "updates", "auto_updates", &error);
    if (error) {
        g_clear_error(&error);
        auto_updates = FALSE;
    }

    char *channel = g_key_file_get_string(kf, "updates", "channel", &error);
    if (error) {
        g_clear_error(&error);
        channel = g_strdup("stable");
    }

    char *repositories_list = g_key_file_get_string(kf, "updates", "repositories_list", &error);
    if (error) {
        g_clear_error(&error);
        repositories_list = NULL;
    }

    gtk_switch_set_active(GTK_SWITCH(g_system_updates_switch), system_updates);
    gtk_switch_set_active(GTK_SWITCH(g_repositories_switch), repositories);
    gtk_switch_set_active(GTK_SWITCH(g_drivers_switch), drivers);
    gtk_switch_set_active(GTK_SWITCH(g_app_store_switch), app_store);
    gtk_switch_set_active(GTK_SWITCH(g_flatpak_switch), flatpak);
    gtk_switch_set_active(GTK_SWITCH(g_snap_switch), snap);
    gtk_switch_set_active(GTK_SWITCH(g_appimage_switch), appimage);
    gtk_switch_set_active(GTK_SWITCH(g_auto_updates_switch), auto_updates);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_channel_dropdown),
                               find_option_index(g_channel_options, G_N_ELEMENTS(g_channel_options), channel));

    char *repo_system_path = repositories_system_path();
    gboolean loaded_system_repos = FALSE;
    if (repo_system_path) {
        loaded_system_repos = load_repositories_from_file(repo_system_path);
    }

    if (!loaded_system_repos && repositories_list && *repositories_list) {
        repositories_list_set_text(repositories_list);
    } else if (!loaded_system_repos) {
        char *defaults = default_repositories_template();
        repositories_list_set_text(defaults);
        g_free(defaults);
    }

    g_free(repo_system_path);

    g_free(channel);
    g_free(repositories_list);
    g_key_file_unref(kf);
    g_free(path);
}

static char *apply_runtime_updates(const char *password, gboolean *auth_failed)
{
    if (auth_failed) {
        *auth_failed = FALSE;
    }

    gboolean system_updates = gtk_switch_get_active(GTK_SWITCH(g_system_updates_switch));
    gboolean repositories = gtk_switch_get_active(GTK_SWITCH(g_repositories_switch));
    gboolean drivers = gtk_switch_get_active(GTK_SWITCH(g_drivers_switch));
    gboolean app_store = gtk_switch_get_active(GTK_SWITCH(g_app_store_switch));
    gboolean flatpak = gtk_switch_get_active(GTK_SWITCH(g_flatpak_switch));
    gboolean snap = gtk_switch_get_active(GTK_SWITCH(g_snap_switch));
    gboolean appimage = gtk_switch_get_active(GTK_SWITCH(g_appimage_switch));
    gboolean auto_updates = gtk_switch_get_active(GTK_SWITCH(g_auto_updates_switch));
    const char *channel = dropdown_selected_value(g_channel_dropdown, g_channel_options, G_N_ELEMENTS(g_channel_options));

    GString *issues = g_string_new(NULL);

    if (flatpak && !command_is_available("flatpak")) {
        g_string_append(issues, _("Flatpak support was requested but flatpak is not installed. "));
    }

    if (snap && !command_is_available("snap")) {
        g_string_append(issues, _("Snap support was requested but snap is not installed. "));
    }

    if (repositories) {
        char *repositories_text = repositories_list_get_text();
        char *repo_system_path = repositories_system_path();

        if (!save_repositories_list_file()) {
            g_string_append(issues, _("Could not persist repositories list. "));
        }

        if (repo_system_path && *repo_system_path) {
            gboolean repo_auth_failed = FALSE;
            if (!write_repositories_system_file(repo_system_path,
                                                repositories_text,
                                                password,
                                                &repo_auth_failed)) {
                if (repo_auth_failed) {
                    g_string_append(issues, _("Authentication failed: administrator password is required to edit repositories. "));
                    if (auth_failed) {
                        *auth_failed = TRUE;
                    }
                } else {
                    char *msg = g_strdup_printf(_("Could not write repositories configuration file: %s. "), repo_system_path);
                    g_string_append(issues, msg);
                    g_free(msg);
                }
            }
        } else {
            g_string_append(issues, _("No supported repositories configuration file for this package backend. "));
        }

        g_free(repo_system_path);
        g_free(repositories_text);
    }

    if (system_updates && auto_updates && command_is_available("systemctl")) {
        gboolean timer_ok = FALSE;
        timer_ok = run_command_success("sh -lc 'systemctl --user enable --now flatpak-system-update.timer >/dev/null 2>&1'") || timer_ok;
        timer_ok = run_command_success("sh -lc 'systemctl enable --now apt-daily-upgrade.timer >/dev/null 2>&1'") || timer_ok;
        timer_ok = run_command_success("sh -lc 'systemctl enable --now dnf-automatic.timer >/dev/null 2>&1'") || timer_ok;

        if (!timer_ok) {
            g_string_append(issues, _("Could not enable automatic update timer on this system. "));
        }
    }

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_UPDATES_SYSTEM=%s\n"
                           "KARTON_UPDATES_REPOSITORIES=%s\n"
                           "KARTON_UPDATES_DRIVERS=%s\n"
                           "KARTON_UPDATES_APP_STORE=%s\n"
                           "KARTON_UPDATES_FLATPAK=%s\n"
                           "KARTON_UPDATES_SNAP=%s\n"
                           "KARTON_UPDATES_APPIMAGE=%s\n"
                           "KARTON_UPDATES_AUTOMATIC=%s\n"
                           "KARTON_UPDATES_CHANNEL=%s",
                           system_updates ? "1" : "0",
                           repositories ? "1" : "0",
                           drivers ? "1" : "0",
                           app_store ? "1" : "0",
                           flatpak ? "1" : "0",
                           snap ? "1" : "0",
                           appimage ? "1" : "0",
                           auto_updates ? "1" : "0",
                           channel ? channel : "stable");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed updates env",
                                              "# END KartON managed updates env",
                                              env_block->str);

    if (!env_ok) {
        g_string_append(issues, _("Could not persist updates environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_UPDATES_SYSTEM=%s KARTON_UPDATES_REPOSITORIES=%s KARTON_UPDATES_DRIVERS=%s KARTON_UPDATES_APP_STORE=%s KARTON_UPDATES_FLATPAK=%s KARTON_UPDATES_SNAP=%s KARTON_UPDATES_APPIMAGE=%s KARTON_UPDATES_AUTOMATIC=%s KARTON_UPDATES_CHANNEL=%s >/dev/null 2>&1 || true'",
            system_updates ? "1" : "0",
            repositories ? "1" : "0",
            drivers ? "1" : "0",
            app_store ? "1" : "0",
            flatpak ? "1" : "0",
            snap ? "1" : "0",
            appimage ? "1" : "0",
            auto_updates ? "1" : "0",
            channel ? channel : "stable");
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

static void on_check_updates_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (!ensure_updates_feature_enabled()) {
        return;
    }

    gboolean is_error = FALSE;
    char *message = check_updates_now(&is_error);
    status_set(message, is_error);
    g_free(message);
}

static void on_install_updates_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (!ensure_updates_feature_enabled()) {
        return;
    }

    GtkRoot *root = gtk_widget_get_root(g_status_label ? g_status_label : g_repositories_text_view);
    GtkWidget *parent = root ? GTK_WIDGET(root) : NULL;

    if (!gtk_switch_get_active(GTK_SWITCH(g_system_updates_switch))) {
        status_set(_("Enable System updates first, then run update installation."), TRUE);
        return;
    }

    char *password = NULL;
    if (geteuid() != 0) {
        if (!command_is_available("sudo")) {
            status_set(_("Could not start update installation: sudo is not available."), TRUE);
            return;
        }

        gboolean cancelled = FALSE;

        if (!prompt_password_dialog(parent, &password, &cancelled)) {
            if (cancelled) {
                status_set(_("System update was cancelled."), TRUE);
            } else {
                status_set(_("Administrator password is required to install updates."), TRUE);
            }
            return;
        }
    }

    gboolean auth_failed = FALSE;
    char *issues = install_updates_now(parent, password, &auth_failed);
    g_free(password);

    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    if (session_is_graphical()) {
        const char *backend = detect_package_backend();
        char *deferred_cmd = deferred_desktop_update_command_for_backend(backend);
        if (deferred_cmd) {
            char *msg = g_strdup_printf(
                _("System update finished in session-safe mode. Desktop environment update is deferred; run after logout from TTY:\n%s"),
                deferred_cmd);
            status_set(msg, FALSE);
            g_free(msg);
            g_free(deferred_cmd);
            return;
        }
    }

    status_set(_("System update finished successfully."), FALSE);
}

static gboolean check_updates_on_open_idle(gpointer data)
{
    (void)data;

    gboolean is_error = FALSE;
    char *message = check_updates_now(&is_error);
    status_set(message, is_error);
    g_free(message);

    return G_SOURCE_REMOVE;
}

static void on_reload_updates_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_updates_config();
    updates_apply_gate_state(updates_experimental_enabled());
    status_set(_("Updates and software settings reloaded"), FALSE);
}

static void on_apply_updates_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (!ensure_updates_feature_enabled()) {
        return;
    }

    save_updates_config();

    char *password = NULL;
    if (gtk_switch_get_active(GTK_SWITCH(g_repositories_switch))) {
        char *repo_system_path = repositories_system_path();
        if (repo_system_path && geteuid() != 0) {
            if (!command_is_available("sudo")) {
                status_set(_("Could not apply repositories changes: sudo is not available."), TRUE);
                g_free(repo_system_path);
                return;
            }

            GtkRoot *root = gtk_widget_get_root(g_status_label ? g_status_label : g_repositories_text_view);
            GtkWidget *parent = root ? GTK_WIDGET(root) : NULL;
            gboolean cancelled = FALSE;

            if (!prompt_password_dialog(parent, &password, &cancelled)) {
                if (cancelled) {
                    status_set(_("Repositories update was cancelled."), TRUE);
                } else {
                    status_set(_("Administrator password is required to apply repositories changes."), TRUE);
                }
                g_free(repo_system_path);
                return;
            }
        }
        g_free(repo_system_path);
    }

    gboolean auth_failed = FALSE;
    char *issues = apply_runtime_updates(password, &auth_failed);
    g_free(password);

    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Updates and software settings applied"), FALSE);
}

static void on_edit_repositories_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (!ensure_updates_feature_enabled()) {
        return;
    }

    char *repo_system_path = repositories_system_path();
    if (repo_system_path && load_repositories_from_file(repo_system_path)) {
        char *msg = g_strdup_printf(_("Loaded repositories from %s. Edit and click Apply updates settings."), repo_system_path);
        status_set(msg, FALSE);
        g_free(msg);
    } else {
        status_set(_("Could not read repositories from system config file; using current editor text."), TRUE);
    }
    g_free(repo_system_path);

    if (g_repositories_text_view) {
        gtk_widget_grab_focus(g_repositories_text_view);
    }
}

GtkWidget *page_updates_new(void)
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

    GtkWidget *title = gtk_label_new(_("Updates and software"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Manage system updates, repositories, drivers, application store and package formats like Flatpak, Snap and AppImage."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *updates_frame = create_section(_("Update sources"),
                                              _("Control system updates, repositories, drivers and software channels."));
    GtkWidget *updates_box = gtk_frame_get_child(GTK_FRAME(updates_frame));

    g_system_updates_switch = gtk_switch_new();
    g_repositories_switch = gtk_switch_new();
    g_drivers_switch = gtk_switch_new();
    g_auto_updates_switch = gtk_switch_new();

    GtkStringList *channel_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_channel_options); i++) {
        gtk_string_list_append(channel_model, _(g_channel_options[i].label));
    }
    g_channel_dropdown = gtk_drop_down_new(G_LIST_MODEL(channel_model), NULL);
    g_object_unref(channel_model);

    gtk_box_append(GTK_BOX(updates_box), create_row(_("System updates"), g_system_updates_switch));
    gtk_box_append(GTK_BOX(updates_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(updates_box), create_row(_("Repositories"), g_repositories_switch));
    gtk_box_append(GTK_BOX(updates_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(updates_box), create_row(_("Drivers"), g_drivers_switch));
    gtk_box_append(GTK_BOX(updates_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(updates_box), create_row(_("Automatic updates"), g_auto_updates_switch));
    gtk_box_append(GTK_BOX(updates_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(updates_box), create_row(_("Update channel"), g_channel_dropdown));

    gtk_box_append(GTK_BOX(updates_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *repos_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *repos_label = gtk_label_new(_("Repositories list"));
    gtk_widget_set_hexpand(repos_label, TRUE);
    gtk_widget_set_halign(repos_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(repos_header), repos_label);

    g_edit_repos_btn = gtk_button_new_with_label(_("Edit repositories"));
    g_signal_connect(g_edit_repos_btn, "clicked", G_CALLBACK(on_edit_repositories_clicked), NULL);
    gtk_box_append(GTK_BOX(repos_header), g_edit_repos_btn);
    gtk_box_append(GTK_BOX(updates_box), repos_header);

    GtkWidget *repos_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(repos_scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(repos_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(repos_scrolled), 170);
    g_repositories_text_view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(g_repositories_text_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_repositories_text_view), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(repos_scrolled), g_repositories_text_view);
    gtk_box_append(GTK_BOX(updates_box), repos_scrolled);

    gtk_box_append(GTK_BOX(box), updates_frame);

    GtkWidget *software_frame = create_section(_("Software formats and store"),
                                               _("Enable software store integration and package formats used by desktop applications."));
    GtkWidget *software_box = gtk_frame_get_child(GTK_FRAME(software_frame));

    g_app_store_switch = gtk_switch_new();
    g_flatpak_switch = gtk_switch_new();
    g_snap_switch = gtk_switch_new();
    g_appimage_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(software_box), create_row(_("Application store"), g_app_store_switch));
    gtk_box_append(GTK_BOX(software_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(software_box), create_row(_("Flatpak"), g_flatpak_switch));
    gtk_box_append(GTK_BOX(software_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(software_box), create_row(_("Snap"), g_snap_switch));
    gtk_box_append(GTK_BOX(software_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(software_box), create_row(_("AppImage"), g_appimage_switch));

    gtk_box_append(GTK_BOX(box), software_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    g_check_btn = gtk_button_new_with_label(_("Check for updates now"));
    g_signal_connect(g_check_btn, "clicked", G_CALLBACK(on_check_updates_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_check_btn);

    g_install_btn = gtk_button_new_with_label(_("Install updates now"));
    g_signal_connect(g_install_btn, "clicked", G_CALLBACK(on_install_updates_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_install_btn);

    g_reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(g_reload_btn, "clicked", G_CALLBACK(on_reload_updates_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_reload_btn);

    g_apply_btn = gtk_button_new_with_label(_("Apply updates settings"));
    gtk_widget_add_css_class(g_apply_btn, "suggested-action");
    g_signal_connect(g_apply_btn, "clicked", G_CALLBACK(on_apply_updates_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_gate_hint_label = gtk_label_new(_("This section is disabled. Enable Experimental features in Advanced to activate Updates and software."));
    gtk_widget_set_halign(g_gate_hint_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_gate_hint_label), TRUE);
    gtk_widget_add_css_class(g_gate_hint_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_gate_hint_label);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_system_updates_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_repositories_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_drivers_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_auto_updates_switch), FALSE);

    gtk_switch_set_active(GTK_SWITCH(g_app_store_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_flatpak_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_snap_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_appimage_switch), TRUE);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_channel_dropdown), find_option_index(g_channel_options, G_N_ELEMENTS(g_channel_options), "stable"));

    char *repositories_defaults = default_repositories_template();
    repositories_list_set_text(repositories_defaults);
    g_free(repositories_defaults);

    load_updates_config();
    updates_apply_gate_state(updates_experimental_enabled());

    if (g_updates_gate_source_id == 0) {
        g_updates_gate_source_id = g_timeout_add_seconds(1, updates_gate_tick, NULL);
    }

    if (gtk_switch_get_active(GTK_SWITCH(g_system_updates_switch)) && updates_experimental_enabled()) {
        g_idle_add(check_updates_on_open_idle, NULL);
    }

    return outer_scroll;
}
