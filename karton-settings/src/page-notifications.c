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

static const struct option_value g_sound_mode_options[] = {
    { N_("System sound"), "system" },
    { N_("Custom sound"), "custom" },
};

static GtkWidget *g_dnd_switch = NULL;
static GtkWidget *g_position_dropdown = NULL;
static GtkWidget *g_history_switch = NULL;
static GtkWidget *g_alert_sounds_switch = NULL;
static GtkWidget *g_sound_mode_dropdown = NULL;
static GtkWidget *g_custom_sound_entry = NULL;
static GtkWidget *g_choose_sound_btn = NULL;

static GtkWidget *g_priority_chat_dropdown = NULL;
static GtkWidget *g_priority_system_dropdown = NULL;
static GtkWidget *g_priority_updates_dropdown = NULL;

static GtkWidget *g_status_label = NULL;
static GtkWidget *g_reload_btn = NULL;
static GtkWidget *g_test_btn = NULL;
static GtkWidget *g_apply_btn = NULL;
static GtkWidget *g_open_list_btn = NULL;
static GtkWidget *g_loading_box = NULL;
static GtkWidget *g_loading_spinner = NULL;
static GtkWidget *g_loading_label = NULL;
static guint g_live_sync_source_id = 0;

static gboolean notifications_position_supported(void)
{
    return FALSE;
}

static gboolean write_managed_env_block(const char *file_path,
                                        const char *begin_marker,
                                        const char *end_marker,
                                        const char *block);
static void notifications_update_sound_controls(void);

typedef struct {
    GtkWidget *entry;
} SoundPickContext;

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
        gtk_widget_set_sensitive(g_position_dropdown, sensitive && notifications_position_supported());
    }
    if (g_history_switch) {
        gtk_widget_set_sensitive(g_history_switch, sensitive);
    }
    if (g_alert_sounds_switch) {
        gtk_widget_set_sensitive(g_alert_sounds_switch, sensitive);
    }
    if (g_sound_mode_dropdown) {
        gtk_widget_set_sensitive(g_sound_mode_dropdown, sensitive);
    }
    if (g_custom_sound_entry) {
        gtk_widget_set_sensitive(g_custom_sound_entry, sensitive);
    }
    if (g_choose_sound_btn) {
        gtk_widget_set_sensitive(g_choose_sound_btn, sensitive);
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
    if (g_open_list_btn) {
        gtk_widget_set_sensitive(g_open_list_btn, sensitive);
    }
    if (g_apply_btn) {
        gtk_widget_set_sensitive(g_apply_btn, sensitive);
    }

    if (sensitive) {
        notifications_update_sound_controls();
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

static void notifications_update_sound_controls(void)
{
    gboolean sounds_on = g_alert_sounds_switch && gtk_switch_get_active(GTK_SWITCH(g_alert_sounds_switch));
    const char *mode = dropdown_selected_value(g_sound_mode_dropdown,
                                               g_sound_mode_options,
                                               G_N_ELEMENTS(g_sound_mode_options));
    gboolean custom_mode = g_strcmp0(mode, "custom") == 0;
    gboolean allow_custom = sounds_on && custom_mode;

    if (g_sound_mode_dropdown) {
        gtk_widget_set_sensitive(g_sound_mode_dropdown, sounds_on);
    }
    if (g_custom_sound_entry) {
        gtk_widget_set_sensitive(g_custom_sound_entry, allow_custom);
    }
    if (g_choose_sound_btn) {
        gtk_widget_set_sensitive(g_choose_sound_btn, allow_custom);
    }
}

static void on_sound_mode_selected_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;
    notifications_update_sound_controls();
}

static void on_alert_sounds_switch_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;
    notifications_update_sound_controls();
}

static char *notifications_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "notifications.conf", NULL);
}

static char *session_environment_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "environment", NULL);
}

static char *notifications_log_path(void)
{
    const char *cache_home = g_getenv("XDG_CACHE_HOME");
    if (cache_home && *cache_home) {
        return g_build_filename(cache_home, "karton", "notifications.log", NULL);
    }

    return g_build_filename(g_get_home_dir(), ".cache", "karton", "notifications.log", NULL);
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
            }
        }
        g_strfreev(lines);
    }

    g_free(contents);
    g_free(env_path);
    return found;
}

static gboolean notifications_env_get_value(const char *key, char **value_out)
{
    if (!key || !*key || !value_out) {
        return FALSE;
    }

    *value_out = NULL;
    char *env_path = session_environment_path();
    char *contents = NULL;
    gboolean found = FALSE;

    if (g_file_get_contents(env_path, &contents, NULL, NULL) && contents) {
        gchar **lines = g_strsplit(contents, "\n", -1);
        char *prefix = g_strdup_printf("%s=", key);

        for (guint i = 0; lines[i] != NULL; i++) {
            if (!g_str_has_prefix(lines[i], prefix)) {
                continue;
            }

            char *value = g_strdup(lines[i] + strlen(prefix));
            g_strstrip(value);
            *value_out = value;
            found = TRUE;
        }

        g_free(prefix);
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

    if (notifications_dnd_from_environment(dnd_out)) {
        return TRUE;
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
    return FALSE;
}

static gboolean notifications_live_sync_tick(gpointer user_data)
{
    (void)user_data;

    if (!g_dnd_switch || !g_loading_box) {
        return G_SOURCE_CONTINUE;
    }

    if (gtk_widget_get_visible(g_loading_box)) {
        return G_SOURCE_CONTINUE;
    }

    gboolean env_dnd = FALSE;
    if (notifications_dnd_from_environment(&env_dnd)) {
        gboolean current = gtk_switch_get_active(GTK_SWITCH(g_dnd_switch));
        if (current != env_dnd) {
            gtk_switch_set_active(GTK_SWITCH(g_dnd_switch), env_dnd);
        }
    }

    if (notifications_position_supported() && g_position_dropdown) {
        char *env_position = NULL;
        if (notifications_env_get_value("KARTON_NOTIFICATIONS_POSITION", &env_position) && env_position && *env_position) {
            const char *normalized = normalize_notification_position(env_position);
            guint idx = find_option_index(g_position_options, G_N_ELEMENTS(g_position_options), normalized);
            guint current_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_position_dropdown));
            if (current_idx != idx) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(g_position_dropdown), idx);
            }
        }
        g_free(env_position);
    }

    return G_SOURCE_CONTINUE;
}

static void on_sound_file_dialog_response(GObject *source, GAsyncResult *res, gpointer user_data)
{
    SoundPickContext *ctx = user_data;
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
        status_set(_("Could not open custom notification sound picker"), TRUE);
    }
    g_clear_error(&error);

    g_object_unref(ctx->entry);
    g_free(ctx);
}

static void on_choose_sound_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    GtkWidget *entry = GTK_WIDGET(data);
    if (!entry) {
        return;
    }

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Choose custom notification sound"));
    gtk_file_dialog_set_accept_label(dialog, _("Select"));

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *audio = gtk_file_filter_new();
    gtk_file_filter_set_name(audio, _("Audio files"));
    gtk_file_filter_add_suffix(audio, "wav");
    gtk_file_filter_add_suffix(audio, "oga");
    gtk_file_filter_add_suffix(audio, "ogg");
    gtk_file_filter_add_suffix(audio, "mp3");
    gtk_file_filter_add_suffix(audio, "flac");
    g_list_store_append(filters, audio);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, audio);

    GFile *home = g_file_new_for_path(g_get_home_dir());
    gtk_file_dialog_set_initial_folder(dialog, home);
    g_object_unref(home);

    SoundPickContext *ctx = g_new0(SoundPickContext, 1);
    ctx->entry = g_object_ref(entry);

    GtkRoot *root = gtk_widget_get_root(entry);
    GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
    gtk_file_dialog_open(dialog, parent, NULL, on_sound_file_dialog_response, ctx);

    g_object_unref(audio);
    g_object_unref(filters);
    g_object_unref(dialog);
}

static void open_notification_history_window(GtkWidget *parent)
{
    char *log_path = notifications_log_path();
    char *contents = NULL;
    gboolean loaded = FALSE;

    if (g_file_get_contents(log_path, &contents, NULL, NULL) && contents && *contents) {
        loaded = TRUE;
    } else {
        g_free(contents);
        contents = NULL;

        char *stdout_data = NULL;
        if (run_command_capture(
                "sh -lc 'if command -v timeout >/dev/null 2>&1 && command -v makoctl >/dev/null 2>&1; then timeout 1s makoctl list 2>/dev/null; fi'",
                &stdout_data,
                NULL,
                NULL)
            && stdout_data && *stdout_data) {
            contents = stdout_data;
            loaded = TRUE;
        } else {
            g_free(stdout_data);
        }
    }

    if (!loaded) {
        contents = g_strdup(_("No notification history entries found."));
    }

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Notification history list"));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 420);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    if (parent && GTK_IS_WINDOW(parent)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent));
    }

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(root_box, 12);
    gtk_widget_set_margin_end(root_box, 12);
    gtk_widget_set_margin_top(root_box, 12);
    gtk_widget_set_margin_bottom(root_box, 12);
    gtk_window_set_child(GTK_WINDOW(dialog), root_box);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text), TRUE);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    gtk_text_buffer_set_text(buffer, contents ? contents : "", -1);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text);
    gtk_box_append(GTK_BOX(root_box), scroll);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    GtkWidget *close_btn = gtk_button_new_with_label(_("Close"));
    g_signal_connect_swapped(close_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(actions), close_btn);
    gtk_box_append(GTK_BOX(root_box), actions);

    gtk_window_present(GTK_WINDOW(dialog));

    g_free(contents);
    g_free(log_path);
}

static void on_open_notification_list_clicked(GtkButton *btn, gpointer data)
{
    (void)data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
    GtkWidget *parent = GTK_IS_WINDOW(root) ? GTK_WIDGET(root) : NULL;
    open_notification_history_window(parent);
}

static gboolean play_custom_notification_sound(const char *sound_path)
{
    if (!sound_path || !*sound_path || !g_file_test(sound_path, G_FILE_TEST_EXISTS)) {
        return FALSE;
    }

    char *q_path = g_shell_quote(sound_path);
    char *cmd = g_strdup_printf(
        "sh -lc 'if command -v pw-play >/dev/null 2>&1; then pw-play %s >/dev/null 2>&1; "
        "elif command -v paplay >/dev/null 2>&1; then paplay %s >/dev/null 2>&1; "
        "elif command -v aplay >/dev/null 2>&1; then aplay %s >/dev/null 2>&1; "
        "else exit 1; fi'",
        q_path,
        q_path,
        q_path);

    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(q_path);
    return ok;
}

static gboolean play_system_notification_sound(void)
{
    return run_command_success(
        "sh -lc 'if command -v canberra-gtk-play >/dev/null 2>&1; then canberra-gtk-play -i message >/dev/null 2>&1; "
        "elif command -v paplay >/dev/null 2>&1; then paplay /usr/share/sounds/freedesktop/stereo/message.oga >/dev/null 2>&1 || true; "
        "else exit 1; fi'"
    );
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
    if (!schema || !*schema || !key || !*key) {
        return FALSE;
    }

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return FALSE;
    }

    GSettingsSchema *schema_obj = g_settings_schema_source_lookup(source, schema, TRUE);
    if (!schema_obj) {
        return TRUE;
    }

    if (!g_settings_schema_has_key(schema_obj, key)) {
        g_settings_schema_unref(schema_obj);
        return TRUE;
    }

    g_settings_schema_unref(schema_obj);

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
    g_key_file_set_string(kf,
                          "notifications",
                          "sound_mode",
                          dropdown_selected_value(g_sound_mode_dropdown,
                                                  g_sound_mode_options,
                                                  G_N_ELEMENTS(g_sound_mode_options)));
    g_key_file_set_string(kf,
                          "notifications",
                          "custom_sound",
                          gtk_editable_get_text(GTK_EDITABLE(g_custom_sound_entry)));

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

    char *sound_mode = g_key_file_get_string(kf, "notifications", "sound_mode", &error);
    if (error) {
        g_clear_error(&error);
    }

    char *custom_sound = g_key_file_get_string(kf, "notifications", "custom_sound", &error);
    if (error) {
        g_clear_error(&error);
    }

    gtk_switch_set_active(GTK_SWITCH(g_dnd_switch), dnd);
    gtk_switch_set_active(GTK_SWITCH(g_history_switch), history);
    gtk_switch_set_active(GTK_SWITCH(g_alert_sounds_switch), alert_sounds);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_position_dropdown),
                               find_option_index(g_position_options, G_N_ELEMENTS(g_position_options), position));
    gtk_widget_set_sensitive(g_position_dropdown, notifications_position_supported());
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_chat_dropdown),
                               find_option_index(g_priority_options, G_N_ELEMENTS(g_priority_options), priority_chat));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_system_dropdown),
                               find_option_index(g_priority_options, G_N_ELEMENTS(g_priority_options), priority_system));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_updates_dropdown),
                               find_option_index(g_priority_options, G_N_ELEMENTS(g_priority_options), priority_updates));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_sound_mode_dropdown),
                               find_option_index(g_sound_mode_options, G_N_ELEMENTS(g_sound_mode_options), sound_mode));

    gtk_editable_set_text(GTK_EDITABLE(g_custom_sound_entry), custom_sound ? custom_sound : "");

    notifications_update_sound_controls();

    g_free(custom_sound);
    g_free(sound_mode);
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
    const char *sound_mode = dropdown_selected_value(g_sound_mode_dropdown,
                                                     g_sound_mode_options,
                                                     G_N_ELEMENTS(g_sound_mode_options));
    const char *custom_sound = gtk_editable_get_text(GTK_EDITABLE(g_custom_sound_entry));

    const char *normalized_position = "top-right";
    if (notifications_position_supported()) {
        const char *position = dropdown_selected_value(g_position_dropdown,
                                                       g_position_options,
                                                       G_N_ELEMENTS(g_position_options));
        normalized_position = normalize_notification_position(position);
    }
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

    gboolean use_custom_sound = g_strcmp0(sound_mode, "custom") == 0;
    gboolean system_sound_enabled = alert_sounds && !use_custom_sound;
    if (!gsettings_set_bool("org.gnome.desktop.sound", "event-sounds", system_sound_enabled)) {
        g_string_append(issues, _("Could not update alert sound policy. "));
    }

    if (alert_sounds && use_custom_sound) {
        if (!custom_sound || !*custom_sound || !g_file_test(custom_sound, G_FILE_TEST_EXISTS)) {
            g_string_append(issues, _("Custom notification sound file is missing or invalid. "));
        }
    }

    if (command_is_available("makoctl")) {
        const char *cmd = dnd
                              ? "sh -lc 'makoctl mode -a do-not-disturb >/dev/null 2>&1 || true'"
                              : "sh -lc 'makoctl mode -r do-not-disturb >/dev/null 2>&1 || true'";
        (void)run_command_success(cmd);
    }

    if (notifications_position_supported()) {
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
                           "KARTON_NOTIFICATIONS_SOUND_MODE=%s\n"
                           "KARTON_NOTIFICATIONS_CUSTOM_SOUND=%s\n"
                           "KARTON_NOTIFICATIONS_PRIORITY_CHAT=%s\n"
                           "KARTON_NOTIFICATIONS_PRIORITY_SYSTEM=%s\n"
                           "KARTON_NOTIFICATIONS_PRIORITY_UPDATES=%s",
                           dnd ? "1" : "0",
                           normalized_position,
                           history ? "1" : "0",
                           alert_sounds ? "1" : "0",
                           use_custom_sound ? "custom" : "system",
                           custom_sound ? custom_sound : "",
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
        char *q_custom_sound = g_shell_quote(custom_sound ? custom_sound : "");
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_NOTIFICATIONS_DND=%s KARTON_NOTIFICATIONS_POSITION=%s KARTON_NOTIFICATIONS_HISTORY=%s KARTON_NOTIFICATIONS_ALERT_SOUNDS=%s KARTON_NOTIFICATIONS_SOUND_MODE=%s KARTON_NOTIFICATIONS_CUSTOM_SOUND=%s KARTON_NOTIFICATIONS_PRIORITY_CHAT=%s KARTON_NOTIFICATIONS_PRIORITY_SYSTEM=%s KARTON_NOTIFICATIONS_PRIORITY_UPDATES=%s >/dev/null 2>&1 || true'",
            dnd ? "1" : "0",
            normalized_position,
            history ? "1" : "0",
            alert_sounds ? "1" : "0",
            use_custom_sound ? "custom" : "system",
            q_custom_sound,
            priority_chat,
            priority_system,
            priority_updates);
        (void)run_command_success(cmd);
        g_free(cmd);
        g_free(q_custom_sound);
    }

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void refresh_notifications_in_shell(void)
{
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -USR1 -x karton-top-panel >/dev/null 2>&1 || true; pkill -USR1 -x karton-side-dock >/dev/null 2>&1 || true'");
}

static const char *priority_to_notify_urgency(const char *priority)
{
    if (!priority || !*priority) {
        return "normal";
    }

    if (g_strcmp0(priority, "low") == 0) {
        return "low";
    }
    if (g_strcmp0(priority, "urgent") == 0 || g_strcmp0(priority, "high") == 0) {
        return "critical";
    }

    return "normal";
}

static gboolean send_test_notification(const char *summary, const char *body, const char *urgency)
{
    if (!command_is_available("notify-send")) {
        return FALSE;
    }

    char *q_summary = g_shell_quote(summary ? summary : "Karton");
    char *q_body = g_shell_quote(body ? body : "Test notification from Settings");
    const char *urg = urgency && *urgency ? urgency : "normal";

    char *cmd = g_strdup_printf(
        "sh -lc 'notify-send --urgency=%s --app-name=KartonSettings --icon=preferences-system-notifications %s %s >/dev/null 2>&1'",
        urg,
        q_summary,
        q_body);

    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(q_body);
    g_free(q_summary);
    return ok;
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

    refresh_notifications_in_shell();

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

    refresh_notifications_in_shell();

    if (!command_is_available("notify-send")) {
        notifications_set_loading(FALSE, NULL);
        status_set(_("Could not send test notification because notify-send is not installed."), TRUE);
        return;
    }

    gboolean alert_sounds = gtk_switch_get_active(GTK_SWITCH(g_alert_sounds_switch));
    const char *sound_mode = dropdown_selected_value(g_sound_mode_dropdown,
                                                     g_sound_mode_options,
                                                     G_N_ELEMENTS(g_sound_mode_options));
    const char *custom_sound = gtk_editable_get_text(GTK_EDITABLE(g_custom_sound_entry));

    if (alert_sounds) {
        gboolean played = FALSE;
        if (g_strcmp0(sound_mode, "custom") == 0) {
            played = play_custom_notification_sound(custom_sound);
        } else {
            played = play_system_notification_sound();
        }

        if (!played) {
            notifications_set_loading(FALSE, NULL);
            status_set(_("Could not play selected notification sound."), TRUE);
            return;
        }
    }

    const char *priority_chat = dropdown_selected_value(g_priority_chat_dropdown,
                                                        g_priority_options,
                                                        G_N_ELEMENTS(g_priority_options));
    const char *priority_system = dropdown_selected_value(g_priority_system_dropdown,
                                                          g_priority_options,
                                                          G_N_ELEMENTS(g_priority_options));
    const char *priority_updates = dropdown_selected_value(g_priority_updates_dropdown,
                                                           g_priority_options,
                                                           G_N_ELEMENTS(g_priority_options));

    gboolean sent_chat = send_test_notification(_("Chat"),
                                                _("Test notification from chat applications"),
                                                priority_to_notify_urgency(priority_chat));
    gboolean sent_system = send_test_notification(_("System"),
                                                  _("Test notification from system alerts"),
                                                  priority_to_notify_urgency(priority_system));
    gboolean sent_updates = send_test_notification(_("Updates"),
                                                   _("Test notification from updates"),
                                                   priority_to_notify_urgency(priority_updates));

    if (!(sent_chat && sent_system && sent_updates)) {
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
    g_sound_mode_dropdown = gtk_drop_down_new_from_strings((const char *const[]){
        _(g_sound_mode_options[0].label),
        _(g_sound_mode_options[1].label),
        NULL
    });

    g_custom_sound_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_custom_sound_entry), _("No custom notification sound selected"));
    gtk_editable_set_editable(GTK_EDITABLE(g_custom_sound_entry), FALSE);
    gtk_widget_set_size_request(g_custom_sound_entry, 320, -1);

    g_choose_sound_btn = gtk_button_new_with_label(_("Choose custom sound"));
    gtk_button_set_icon_name(GTK_BUTTON(g_choose_sound_btn), "audio-x-generic-symbolic");
    g_signal_connect(g_choose_sound_btn, "clicked", G_CALLBACK(on_choose_sound_clicked), g_custom_sound_entry);

    GtkWidget *sound_picker = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(sound_picker), g_custom_sound_entry);
    gtk_box_append(GTK_BOX(sound_picker), g_choose_sound_btn);

    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Do Not Disturb"), g_dnd_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Notification popup position"), g_position_dropdown));
    gtk_widget_set_sensitive(g_position_dropdown, notifications_position_supported());
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Notification history"), g_history_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Alert sounds"), g_alert_sounds_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Sound source"), g_sound_mode_dropdown));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Custom sound file"), sound_picker));

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

    g_open_list_btn = gtk_button_new_with_label(_("Open notification list"));
    g_signal_connect(g_open_list_btn, "clicked", G_CALLBACK(on_open_notification_list_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_open_list_btn);

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
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_sound_mode_dropdown), 0);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_chat_dropdown), 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_system_dropdown), 2);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_priority_updates_dropdown), 2);

    g_signal_connect(g_sound_mode_dropdown, "notify::selected", G_CALLBACK(on_sound_mode_selected_changed), NULL);
    g_signal_connect(g_alert_sounds_switch, "notify::active", G_CALLBACK(on_alert_sounds_switch_changed), NULL);

    notifications_update_sound_controls();

    load_notifications_config();

    if (g_live_sync_source_id == 0) {
        g_live_sync_source_id = g_timeout_add_seconds(1, notifications_live_sync_tick, NULL);
    }

    return outer_scroll;
}
