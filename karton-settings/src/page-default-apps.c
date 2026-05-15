#include "page-default-apps.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <string.h>

#define _(s) gettext(s)

typedef struct {
    GtkStringList *labels;
    GPtrArray *values;
} AppChoiceList;

static GtkWidget *g_browser_dropdown = NULL;
static GtkWidget *g_file_manager_dropdown = NULL;
static GtkWidget *g_terminal_dropdown = NULL;
static GtkWidget *g_mail_dropdown = NULL;
static GtkWidget *g_music_dropdown = NULL;
static GtkWidget *g_video_dropdown = NULL;
static GtkWidget *g_text_editor_dropdown = NULL;
static GtkWidget *g_status_label = NULL;

static AppChoiceList g_browser_choices = { 0 };
static AppChoiceList g_file_manager_choices = { 0 };
static AppChoiceList g_terminal_choices = { 0 };
static AppChoiceList g_mail_choices = { 0 };
static AppChoiceList g_music_choices = { 0 };
static AppChoiceList g_video_choices = { 0 };
static AppChoiceList g_text_editor_choices = { 0 };

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
        gtk_widget_set_size_request(control, 320, -1);
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

static void app_choice_list_clear(AppChoiceList *list)
{
    if (!list) {
        return;
    }

    if (list->labels) {
        g_object_unref(list->labels);
        list->labels = NULL;
    }

    if (list->values) {
        g_ptr_array_unref(list->values);
        list->values = NULL;
    }
}

static gboolean app_choice_list_contains_value(const AppChoiceList *list, const char *value)
{
    if (!list || !list->values || !value) {
        return FALSE;
    }

    for (guint i = 0; i < list->values->len; i++) {
        const char *existing = g_ptr_array_index(list->values, i);
        if (g_strcmp0(existing, value) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void app_choice_list_begin(AppChoiceList *list)
{
    if (!list) {
        return;
    }

    app_choice_list_clear(list);
    list->labels = gtk_string_list_new(NULL);
    list->values = g_ptr_array_new_with_free_func(g_free);

    gtk_string_list_append(list->labels, _("System default"));
    g_ptr_array_add(list->values, g_strdup(""));
}

static void app_choice_list_add(AppChoiceList *list, const char *label, const char *value)
{
    if (!list || !list->labels || !list->values || !label || !*label || !value || !*value) {
        return;
    }

    if (app_choice_list_contains_value(list, value)) {
        return;
    }

    gtk_string_list_append(list->labels, label);
    g_ptr_array_add(list->values, g_strdup(value));
}

static void app_choice_list_add_from_app_info(AppChoiceList *list, GAppInfo *app)
{
    if (!list || !G_IS_APP_INFO(app)) {
        return;
    }

    if (!g_app_info_should_show(app)) {
        return;
    }

    const char *desktop_id = g_app_info_get_id(app);
    if (!desktop_id || !*desktop_id) {
        return;
    }

    const char *label = g_app_info_get_display_name(app);
    if (!label || !*label) {
        label = g_app_info_get_name(app);
    }
    if (!label || !*label) {
        return;
    }

    app_choice_list_add(list, label, desktop_id);
}

static void app_choice_list_collect_mime_type(AppChoiceList *list, const char *mime_type)
{
    if (!list || !mime_type || !*mime_type) {
        return;
    }

    GList *apps = g_app_info_get_all_for_type(mime_type);
    for (GList *it = apps; it != NULL; it = it->next) {
        app_choice_list_add_from_app_info(list, G_APP_INFO(it->data));
    }

    g_list_free_full(apps, g_object_unref);
}

static void app_choice_list_collect_mime_types(AppChoiceList *list, const char *const *mime_types, guint count)
{
    if (!list || !mime_types || count == 0) {
        return;
    }

    for (guint i = 0; i < count; i++) {
        app_choice_list_collect_mime_type(list, mime_types[i]);
    }
}

static gboolean app_name_has_terminal_keyword(const char *text)
{
    static const char *const keywords[] = {
        "terminal",
        "console",
        "konsole",
        "xterm",
        "alacritty",
        "kitty",
        "wezterm",
        "tilix",
        "terminator",
        "kgx",
        "ptyxis",
    };

    if (!text || !*text) {
        return FALSE;
    }

    char *lower = g_ascii_strdown(text, -1);
    gboolean found = FALSE;

    for (guint i = 0; i < G_N_ELEMENTS(keywords); i++) {
        if (strstr(lower, keywords[i]) != NULL) {
            found = TRUE;
            break;
        }
    }

    g_free(lower);
    return found;
}

static gboolean app_name_has_file_manager_keyword(const char *text)
{
    static const char *const keywords[] = {
        "file",
        "files",
        "filemanager",
        "file-manager",
        "nautilus",
        "thunar",
        "dolphin",
        "nemo",
        "pcmanfm",
        "krusader",
        "doublecmd",
        "double commander",
    };

    if (!text || !*text) {
        return FALSE;
    }

    char *lower = g_ascii_strdown(text, -1);
    gboolean found = FALSE;

    for (guint i = 0; i < G_N_ELEMENTS(keywords); i++) {
        if (strstr(lower, keywords[i]) != NULL) {
            found = TRUE;
            break;
        }
    }

    g_free(lower);
    return found;
}

static void app_choice_list_collect_terminal_apps(AppChoiceList *list)
{
    if (!list) {
        return;
    }

    GList *apps = g_app_info_get_all();
    for (GList *it = apps; it != NULL; it = it->next) {
        GAppInfo *app = G_APP_INFO(it->data);
        if (!G_IS_APP_INFO(app) || !g_app_info_should_show(app)) {
            continue;
        }

        const char *desktop_id = g_app_info_get_id(app);
        const char *name = g_app_info_get_display_name(app);
        if (!name || !*name) {
            name = g_app_info_get_name(app);
        }

        if (!desktop_id || !*desktop_id || !name || !*name) {
            continue;
        }

        if (app_name_has_terminal_keyword(desktop_id) || app_name_has_terminal_keyword(name)) {
            app_choice_list_add(list, name, desktop_id);
        }
    }

    g_list_free_full(apps, g_object_unref);
}

static void app_choice_list_collect_file_manager_apps(AppChoiceList *list)
{
    if (!list) {
        return;
    }

    GList *apps = g_app_info_get_all();
    for (GList *it = apps; it != NULL; it = it->next) {
        GAppInfo *app = G_APP_INFO(it->data);
        if (!G_IS_APP_INFO(app) || !g_app_info_should_show(app)) {
            continue;
        }

        const char *desktop_id = g_app_info_get_id(app);
        const char *name = g_app_info_get_display_name(app);
        if (!name || !*name) {
            name = g_app_info_get_name(app);
        }

        if (!desktop_id || !*desktop_id || !name || !*name) {
            continue;
        }

        if (app_name_has_file_manager_keyword(desktop_id) || app_name_has_file_manager_keyword(name)) {
            app_choice_list_add(list, name, desktop_id);
        }
    }

    g_list_free_full(apps, g_object_unref);
}

static void app_choice_list_collect_known_desktop_ids(AppChoiceList *list,
                                                       const char *const *desktop_ids,
                                                       guint count)
{
    if (!list || !desktop_ids || count == 0) {
        return;
    }

    GList *apps = g_app_info_get_all();
    for (GList *it = apps; it != NULL; it = it->next) {
        GAppInfo *app = G_APP_INFO(it->data);
        if (!G_IS_APP_INFO(app) || !g_app_info_should_show(app)) {
            continue;
        }

        const char *desktop_id = g_app_info_get_id(app);
        if (!desktop_id || !*desktop_id) {
            continue;
        }

        for (guint i = 0; i < count; i++) {
            if (g_strcmp0(desktop_id, desktop_ids[i]) == 0) {
                app_choice_list_add_from_app_info(list, app);
                break;
            }
        }
    }

    g_list_free_full(apps, g_object_unref);
}

static guint app_choice_list_find_index(const AppChoiceList *list, const char *value)
{
    if (!list || !list->values || !value) {
        return 0;
    }

    for (guint i = 0; i < list->values->len; i++) {
        const char *existing = g_ptr_array_index(list->values, i);
        if (g_strcmp0(existing, value) == 0) {
            return i;
        }
    }

    return 0;
}

static const char *dropdown_selected_value(GtkWidget *dropdown, const AppChoiceList *list)
{
    if (!dropdown || !list || !list->values || list->values->len == 0) {
        return "";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (idx >= list->values->len) {
        idx = 0;
    }

    return g_ptr_array_index(list->values, idx);
}

static GtkWidget *create_dropdown_from_choices(const AppChoiceList *list)
{
    if (!list || !list->labels) {
        return gtk_drop_down_new(NULL, NULL);
    }

    return gtk_drop_down_new(G_LIST_MODEL(list->labels), NULL);
}

static void load_installed_default_app_choices(void)
{
    static const char *const known_file_manager_ids[] = {
        "io.karton.Files.desktop",
        "org.gnome.Nautilus.desktop",
        "thunar.desktop",
        "org.kde.dolphin.desktop",
        "nemo.desktop",
        "pcmanfm.desktop",
    };

    static const char *const known_terminal_ids[] = {
        "io.karton.Terminal.desktop",
        "org.gnome.Console.desktop",
        "org.gnome.Terminal.desktop",
        "org.kde.konsole.desktop",
        "Alacritty.desktop",
    };

    static const char *const browser_mimes[] = {
        "text/html",
        "x-scheme-handler/http",
        "x-scheme-handler/https",
    };

    static const char *const file_manager_mimes[] = {
        "inode/directory",
        "application/x-gnome-saved-search",
    };

    static const char *const terminal_mimes[] = {
        "x-scheme-handler/terminal",
    };

    static const char *const mail_mimes[] = {
        "x-scheme-handler/mailto",
        "message/rfc822",
    };

    static const char *const music_mimes[] = {
        "audio/mpeg",
        "audio/flac",
        "audio/ogg",
        "application/ogg",
    };

    static const char *const video_mimes[] = {
        "video/mp4",
        "video/webm",
        "video/x-matroska",
        "video/x-msvideo",
    };

    static const char *const text_mimes[] = {
        "text/plain",
        "text/markdown",
        "application/json",
        "text/x-python",
    };

    app_choice_list_begin(&g_browser_choices);
    app_choice_list_begin(&g_file_manager_choices);
    app_choice_list_begin(&g_terminal_choices);
    app_choice_list_begin(&g_mail_choices);
    app_choice_list_begin(&g_music_choices);
    app_choice_list_begin(&g_video_choices);
    app_choice_list_begin(&g_text_editor_choices);

    app_choice_list_collect_mime_types(&g_browser_choices, browser_mimes, G_N_ELEMENTS(browser_mimes));
    app_choice_list_collect_mime_types(&g_file_manager_choices, file_manager_mimes, G_N_ELEMENTS(file_manager_mimes));
    app_choice_list_collect_file_manager_apps(&g_file_manager_choices);
    app_choice_list_collect_known_desktop_ids(&g_file_manager_choices,
                                              known_file_manager_ids,
                                              G_N_ELEMENTS(known_file_manager_ids));
    app_choice_list_collect_mime_types(&g_terminal_choices, terminal_mimes, G_N_ELEMENTS(terminal_mimes));
    app_choice_list_collect_terminal_apps(&g_terminal_choices);
    app_choice_list_collect_known_desktop_ids(&g_terminal_choices,
                                              known_terminal_ids,
                                              G_N_ELEMENTS(known_terminal_ids));
    app_choice_list_collect_mime_types(&g_mail_choices, mail_mimes, G_N_ELEMENTS(mail_mimes));
    app_choice_list_collect_mime_types(&g_music_choices, music_mimes, G_N_ELEMENTS(music_mimes));
    app_choice_list_collect_mime_types(&g_video_choices, video_mimes, G_N_ELEMENTS(video_mimes));
    app_choice_list_collect_mime_types(&g_text_editor_choices, text_mimes, G_N_ELEMENTS(text_mimes));
}

static char *default_apps_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "default-apps.conf", NULL);
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

static gboolean set_xdg_mime_default(const char *desktop_id, const char *mime_type)
{
    if (!desktop_id || !*desktop_id || !mime_type || !*mime_type || !command_is_available("xdg-mime")) {
        return FALSE;
    }

    char *q_desktop = g_shell_quote(desktop_id);
    char *q_mime = g_shell_quote(mime_type);
    char *cmd = g_strdup_printf("sh -lc 'xdg-mime default %s %s >/dev/null 2>&1'", q_desktop, q_mime);
    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_mime);
    g_free(q_desktop);
    return ok;
}

static gboolean set_xdg_settings_default_browser(const char *desktop_id)
{
    if (!desktop_id || !*desktop_id || !command_is_available("xdg-settings")) {
        return FALSE;
    }

    char *q_desktop = g_shell_quote(desktop_id);
    char *cmd = g_strdup_printf("sh -lc 'xdg-settings set default-web-browser %s >/dev/null 2>&1'", q_desktop);
    gboolean ok = run_command_success(cmd);

    g_free(cmd);
    g_free(q_desktop);
    return ok;
}

static char *query_xdg_settings_default_browser(void)
{
    if (!command_is_available("xdg-settings")) {
        return g_strdup("");
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture(
        "sh -lc 'xdg-settings get default-web-browser 2>/dev/null'",
        &stdout_data,
        NULL,
        NULL);

    if (!ok || !stdout_data) {
        g_free(stdout_data);
        return g_strdup("");
    }

    g_strstrip(stdout_data);
    if (!stdout_data[0]) {
        g_free(stdout_data);
        return g_strdup("");
    }

    return stdout_data;
}

static char *query_xdg_mime_default(const char *mime_type)
{
    if (!mime_type || !*mime_type || !command_is_available("xdg-mime")) {
        return g_strdup("");
    }

    char *q_mime = g_shell_quote(mime_type);
    char *cmd = g_strdup_printf("sh -lc 'xdg-mime query default %s 2>/dev/null'", q_mime);

    char *stdout_data = NULL;
    gboolean ok = run_command_capture(cmd, &stdout_data, NULL, NULL);

    g_free(cmd);
    g_free(q_mime);

    if (!ok || !stdout_data) {
        g_free(stdout_data);
        return g_strdup("");
    }

    g_strstrip(stdout_data);
    if (!stdout_data[0]) {
        g_free(stdout_data);
        return g_strdup("");
    }

    return stdout_data;
}

static void load_default_apps_from_system(void)
{
    char *browser = query_xdg_settings_default_browser();
    char *file_manager = query_xdg_mime_default("inode/directory");
    char *terminal = query_xdg_mime_default("x-scheme-handler/terminal");
    char *mail = query_xdg_mime_default("x-scheme-handler/mailto");
    char *music = query_xdg_mime_default("audio/mpeg");
    char *video = query_xdg_mime_default("video/mp4");
    char *text_editor = query_xdg_mime_default("text/plain");

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_browser_dropdown),
                               app_choice_list_find_index(&g_browser_choices, browser));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_file_manager_dropdown),
                               app_choice_list_find_index(&g_file_manager_choices, file_manager));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_terminal_dropdown),
                               app_choice_list_find_index(&g_terminal_choices, terminal));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mail_dropdown),
                               app_choice_list_find_index(&g_mail_choices, mail));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_music_dropdown),
                               app_choice_list_find_index(&g_music_choices, music));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_video_dropdown),
                               app_choice_list_find_index(&g_video_choices, video));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_text_editor_dropdown),
                               app_choice_list_find_index(&g_text_editor_choices, text_editor));

    g_free(text_editor);
    g_free(video);
    g_free(music);
    g_free(mail);
    g_free(terminal);
    g_free(file_manager);
    g_free(browser);
}

static void save_default_apps_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_string(kf,
                          "default-apps",
                          "browser",
                          dropdown_selected_value(g_browser_dropdown, &g_browser_choices));
    g_key_file_set_string(kf,
                          "default-apps",
                          "file_manager",
                          dropdown_selected_value(g_file_manager_dropdown, &g_file_manager_choices));
    g_key_file_set_string(kf,
                          "default-apps",
                          "terminal",
                          dropdown_selected_value(g_terminal_dropdown, &g_terminal_choices));
    g_key_file_set_string(kf,
                          "default-apps",
                          "mail",
                          dropdown_selected_value(g_mail_dropdown, &g_mail_choices));
    g_key_file_set_string(kf,
                          "default-apps",
                          "music",
                          dropdown_selected_value(g_music_dropdown, &g_music_choices));
    g_key_file_set_string(kf,
                          "default-apps",
                          "video",
                          dropdown_selected_value(g_video_dropdown, &g_video_choices));
    g_key_file_set_string(kf,
                          "default-apps",
                          "text_editor",
                          dropdown_selected_value(g_text_editor_dropdown, &g_text_editor_choices));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = default_apps_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_default_apps_config(void)
{
    load_default_apps_from_system();

    char *path = default_apps_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    char *browser = g_key_file_get_string(kf, "default-apps", "browser", &error);
    if (error) {
        g_clear_error(&error);
        browser = g_strdup("");
    }

    char *file_manager = g_key_file_get_string(kf, "default-apps", "file_manager", &error);
    if (error) {
        g_clear_error(&error);
        file_manager = g_strdup("");
    }

    char *terminal = g_key_file_get_string(kf, "default-apps", "terminal", &error);
    if (error) {
        g_clear_error(&error);
        terminal = g_strdup("");
    }

    char *mail = g_key_file_get_string(kf, "default-apps", "mail", &error);
    if (error) {
        g_clear_error(&error);
        mail = g_strdup("");
    }

    char *music = g_key_file_get_string(kf, "default-apps", "music", &error);
    if (error) {
        g_clear_error(&error);
        music = g_strdup("");
    }

    char *video = g_key_file_get_string(kf, "default-apps", "video", &error);
    if (error) {
        g_clear_error(&error);
        video = g_strdup("");
    }

    char *text_editor = g_key_file_get_string(kf, "default-apps", "text_editor", &error);
    if (error) {
        g_clear_error(&error);
        text_editor = g_strdup("");
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_browser_dropdown),
                               app_choice_list_find_index(&g_browser_choices, browser));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_file_manager_dropdown),
                               app_choice_list_find_index(&g_file_manager_choices, file_manager));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_terminal_dropdown),
                               app_choice_list_find_index(&g_terminal_choices, terminal));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mail_dropdown),
                               app_choice_list_find_index(&g_mail_choices, mail));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_music_dropdown),
                               app_choice_list_find_index(&g_music_choices, music));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_video_dropdown),
                               app_choice_list_find_index(&g_video_choices, video));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_text_editor_dropdown),
                               app_choice_list_find_index(&g_text_editor_choices, text_editor));

    g_free(text_editor);
    g_free(video);
    g_free(music);
    g_free(mail);
    g_free(terminal);
    g_free(file_manager);
    g_free(browser);
    g_key_file_unref(kf);
    g_free(path);
}

static void apply_default_for_mimes(const char *desktop_id,
                                    const char *const *mime_types,
                                    guint mime_count,
                                    GString *issues,
                                    const char *issue_text)
{
    if (!desktop_id || !*desktop_id) {
        return;
    }

    gboolean all_ok = TRUE;
    for (guint i = 0; i < mime_count; i++) {
        if (!set_xdg_mime_default(desktop_id, mime_types[i])) {
            all_ok = FALSE;
        }
    }

    if (!all_ok && issues && issue_text) {
        g_string_append(issues, issue_text);
    }
}

static char *apply_runtime_default_apps(void)
{
    const char *browser = dropdown_selected_value(g_browser_dropdown, &g_browser_choices);
    const char *file_manager = dropdown_selected_value(g_file_manager_dropdown, &g_file_manager_choices);
    const char *terminal = dropdown_selected_value(g_terminal_dropdown, &g_terminal_choices);
    const char *mail = dropdown_selected_value(g_mail_dropdown, &g_mail_choices);
    const char *music = dropdown_selected_value(g_music_dropdown, &g_music_choices);
    const char *video = dropdown_selected_value(g_video_dropdown, &g_video_choices);
    const char *text_editor = dropdown_selected_value(g_text_editor_dropdown, &g_text_editor_choices);

    GString *issues = g_string_new(NULL);

    if (browser && *browser) {
        if (!set_xdg_settings_default_browser(browser)) {
            g_string_append(issues, _("Could not update default browser via xdg-settings. "));
        }

        const char *const browser_mimes[] = {
            "text/html",
            "x-scheme-handler/http",
            "x-scheme-handler/https",
        };
        apply_default_for_mimes(browser,
                                browser_mimes,
                                G_N_ELEMENTS(browser_mimes),
                                issues,
                                _("Could not update browser MIME associations. "));
    }

    const char *const file_manager_mimes[] = {
        "inode/directory",
        "application/x-gnome-saved-search",
    };
    apply_default_for_mimes(file_manager,
                            file_manager_mimes,
                            G_N_ELEMENTS(file_manager_mimes),
                            issues,
                            _("Could not update file manager default association. "));

    const char *const terminal_mimes[] = {
        "x-scheme-handler/terminal",
    };
    apply_default_for_mimes(terminal,
                            terminal_mimes,
                            G_N_ELEMENTS(terminal_mimes),
                            issues,
                            _("Could not update terminal default association. "));

    const char *const mail_mimes[] = {
        "x-scheme-handler/mailto",
        "message/rfc822",
    };
    apply_default_for_mimes(mail,
                            mail_mimes,
                            G_N_ELEMENTS(mail_mimes),
                            issues,
                            _("Could not update mail client association. "));

    const char *const music_mimes[] = {
        "audio/mpeg",
        "audio/flac",
        "audio/ogg",
        "application/ogg",
    };
    apply_default_for_mimes(music,
                            music_mimes,
                            G_N_ELEMENTS(music_mimes),
                            issues,
                            _("Could not update music player associations. "));

    const char *const video_mimes[] = {
        "video/mp4",
        "video/webm",
        "video/x-matroska",
        "video/x-msvideo",
    };
    apply_default_for_mimes(video,
                            video_mimes,
                            G_N_ELEMENTS(video_mimes),
                            issues,
                            _("Could not update video player associations. "));

    const char *const text_mimes[] = {
        "text/plain",
        "text/markdown",
        "application/json",
        "text/x-python",
    };
    apply_default_for_mimes(text_editor,
                            text_mimes,
                            G_N_ELEMENTS(text_mimes),
                            issues,
                            _("Could not update text editor associations. "));

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_DEFAULT_BROWSER=%s\n"
                           "KARTON_DEFAULT_FILE_MANAGER=%s\n"
                           "KARTON_DEFAULT_TERMINAL=%s\n"
                           "KARTON_DEFAULT_MAIL=%s\n"
                           "KARTON_DEFAULT_MUSIC=%s\n"
                           "KARTON_DEFAULT_VIDEO=%s\n"
                           "KARTON_DEFAULT_TEXT_EDITOR=%s",
                           browser ? browser : "",
                           file_manager ? file_manager : "",
                           terminal ? terminal : "",
                           mail ? mail : "",
                           music ? music : "",
                           video ? video : "",
                           text_editor ? text_editor : "");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed default apps env",
                                              "# END KartON managed default apps env",
                                              env_block->str);

    if (!env_ok) {
        g_string_append(issues, _("Could not persist default applications environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_DEFAULT_BROWSER=%s KARTON_DEFAULT_FILE_MANAGER=%s KARTON_DEFAULT_TERMINAL=%s KARTON_DEFAULT_MAIL=%s KARTON_DEFAULT_MUSIC=%s KARTON_DEFAULT_VIDEO=%s KARTON_DEFAULT_TEXT_EDITOR=%s >/dev/null 2>&1 || true'",
            browser ? browser : "",
            file_manager ? file_manager : "",
            terminal ? terminal : "",
            mail ? mail : "",
            music ? music : "",
            video ? video : "",
            text_editor ? text_editor : "");
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

static gboolean launch_xdg_open_test(const char *target)
{
    if (!target || !*target || !command_is_available("xdg-open")) {
        return FALSE;
    }

    char *q_target = g_shell_quote(target);
    char *cmd = g_strdup_printf("sh -lc 'xdg-open %s >/dev/null 2>&1 &'", q_target);
    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(q_target);

    return ok;
}

static void on_reload_default_apps_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_default_apps_config();
    status_set(_("Default applications settings reloaded"), FALSE);
}

static void on_apply_default_apps_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_default_apps_config();

    char *issues = apply_runtime_default_apps();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Default applications settings applied"), FALSE);
}

static void on_test_default_apps_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_default_apps_config();

    char *issues = apply_runtime_default_apps();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    gboolean opened = FALSE;
    opened = launch_xdg_open_test("https://example.com") || opened;
    opened = launch_xdg_open_test("mailto:test@example.com?subject=Karton%20default%20apps%20test") || opened;

    if (!opened) {
        status_set(_("Could not launch default application test targets."), TRUE);
        return;
    }

    status_set(_("Default application tests launched."), FALSE);
}

GtkWidget *page_default_apps_new(void)
{
    load_installed_default_app_choices();

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

    GtkWidget *title = gtk_label_new(_("Default applications"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Choose which applications should open links, folders, terminal sessions, mail, music, video and text files."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *defaults_frame = create_section(_("Application defaults"),
                                               _("Set default handlers for the most common tasks and file types."));
    GtkWidget *defaults_box = gtk_frame_get_child(GTK_FRAME(defaults_frame));

    g_browser_dropdown = create_dropdown_from_choices(&g_browser_choices);
    g_file_manager_dropdown = create_dropdown_from_choices(&g_file_manager_choices);
    g_terminal_dropdown = create_dropdown_from_choices(&g_terminal_choices);
    g_mail_dropdown = create_dropdown_from_choices(&g_mail_choices);
    g_music_dropdown = create_dropdown_from_choices(&g_music_choices);
    g_video_dropdown = create_dropdown_from_choices(&g_video_choices);
    g_text_editor_dropdown = create_dropdown_from_choices(&g_text_editor_choices);

    gtk_box_append(GTK_BOX(defaults_box), create_row(_("Web browser"), g_browser_dropdown));
    gtk_box_append(GTK_BOX(defaults_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(defaults_box), create_row(_("File manager"), g_file_manager_dropdown));
    gtk_box_append(GTK_BOX(defaults_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(defaults_box), create_row(_("Terminal"), g_terminal_dropdown));
    gtk_box_append(GTK_BOX(defaults_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(defaults_box), create_row(_("Mail client"), g_mail_dropdown));
    gtk_box_append(GTK_BOX(defaults_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(defaults_box), create_row(_("Music player"), g_music_dropdown));
    gtk_box_append(GTK_BOX(defaults_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(defaults_box), create_row(_("Video player"), g_video_dropdown));
    gtk_box_append(GTK_BOX(defaults_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(defaults_box), create_row(_("Text editor"), g_text_editor_dropdown));

    gtk_box_append(GTK_BOX(box), defaults_frame);

    GtkWidget *note = gtk_label_new(_("Some desktop environments may ignore selected handlers for specific categories. In that case values are still saved for KartON services."));
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_add_css_class(note, "row-subtitle");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_default_apps_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *test_btn = gtk_button_new();
    GtkWidget *test_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *test_icon = gtk_image_new_from_icon_name("applications-system-symbolic");
    GtkWidget *test_label = gtk_label_new(_("Test"));
    gtk_box_append(GTK_BOX(test_box), test_icon);
    gtk_box_append(GTK_BOX(test_box), test_label);
    gtk_button_set_child(GTK_BUTTON(test_btn), test_box);
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_test_default_apps_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), test_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply default applications"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_default_apps_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_browser_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_file_manager_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_terminal_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mail_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_music_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_video_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_text_editor_dropdown), 0);

    load_default_apps_config();

    return outer_scroll;
}
