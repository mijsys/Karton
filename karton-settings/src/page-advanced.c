#include "page-advanced.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>

#define _(s) gettext(s)

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_session_backend_options[] = {
    { "Auto", "auto" },
    { "Wayland", "wayland" },
    { "X11", "x11" },
};

static const struct option_value g_performance_options[] = {
    { "Balanced", "balanced" },
    { "Power saving", "power-save" },
    { "Performance", "performance" },
};

static const struct option_value g_renderer_options[] = {
    { "Auto", "auto" },
    { "Vulkan", "vulkan" },
    { "OpenGL", "opengl" },
    { "Software", "software" },
};

static GtkWidget *g_debug_switch = NULL;
static GtkWidget *g_experimental_switch = NULL;
static GtkWidget *g_session_backend_dropdown = NULL;
static GtkWidget *g_performance_dropdown = NULL;
static GtkWidget *g_renderer_dropdown = NULL;
static GtkWidget *g_status_label = NULL;

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

static char *advanced_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "advanced.conf", NULL);
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

static void save_advanced_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "advanced", "debugging", gtk_switch_get_active(GTK_SWITCH(g_debug_switch)));
    g_key_file_set_boolean(kf, "advanced", "experimental", gtk_switch_get_active(GTK_SWITCH(g_experimental_switch)));
    g_key_file_set_string(kf,
                          "advanced",
                          "session_backend",
                          dropdown_selected_value(g_session_backend_dropdown,
                                                  g_session_backend_options,
                                                  G_N_ELEMENTS(g_session_backend_options)));
    g_key_file_set_string(kf,
                          "advanced",
                          "performance_profile",
                          dropdown_selected_value(g_performance_dropdown,
                                                  g_performance_options,
                                                  G_N_ELEMENTS(g_performance_options)));
    g_key_file_set_string(kf,
                          "advanced",
                          "renderer",
                          dropdown_selected_value(g_renderer_dropdown,
                                                  g_renderer_options,
                                                  G_N_ELEMENTS(g_renderer_options)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = advanced_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_advanced_config(void)
{
    char *path = advanced_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean debugging = g_key_file_get_boolean(kf, "advanced", "debugging", &error);
    if (error) {
        g_clear_error(&error);
        debugging = FALSE;
    }

    gboolean experimental = g_key_file_get_boolean(kf, "advanced", "experimental", &error);
    if (error) {
        g_clear_error(&error);
        experimental = FALSE;
    }

    char *session_backend = g_key_file_get_string(kf, "advanced", "session_backend", &error);
    if (error) {
        g_clear_error(&error);
        session_backend = g_strdup("auto");
    }

    char *performance_profile = g_key_file_get_string(kf, "advanced", "performance_profile", &error);
    if (error) {
        g_clear_error(&error);
        performance_profile = g_strdup("balanced");
    }

    char *renderer = g_key_file_get_string(kf, "advanced", "renderer", &error);
    if (error) {
        g_clear_error(&error);
        renderer = g_strdup("auto");
    }

    gtk_switch_set_active(GTK_SWITCH(g_debug_switch), debugging);
    gtk_switch_set_active(GTK_SWITCH(g_experimental_switch), experimental);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_session_backend_dropdown),
                               find_option_index(g_session_backend_options,
                                                 G_N_ELEMENTS(g_session_backend_options),
                                                 session_backend));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_performance_dropdown),
                               find_option_index(g_performance_options,
                                                 G_N_ELEMENTS(g_performance_options),
                                                 performance_profile));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_renderer_dropdown),
                               find_option_index(g_renderer_options,
                                                 G_N_ELEMENTS(g_renderer_options),
                                                 renderer));

    g_free(renderer);
    g_free(performance_profile);
    g_free(session_backend);
    g_key_file_unref(kf);
    g_free(path);
}

static char *apply_runtime_advanced(void)
{
    gboolean debugging = gtk_switch_get_active(GTK_SWITCH(g_debug_switch));
    gboolean experimental = gtk_switch_get_active(GTK_SWITCH(g_experimental_switch));
    const char *session_backend = dropdown_selected_value(g_session_backend_dropdown,
                                                          g_session_backend_options,
                                                          G_N_ELEMENTS(g_session_backend_options));
    const char *performance_profile = dropdown_selected_value(g_performance_dropdown,
                                                              g_performance_options,
                                                              G_N_ELEMENTS(g_performance_options));
    const char *renderer = dropdown_selected_value(g_renderer_dropdown,
                                                   g_renderer_options,
                                                   G_N_ELEMENTS(g_renderer_options));

    GString *issues = g_string_new(NULL);
    GString *env_block = g_string_new(NULL);

    g_string_append_printf(env_block,
                           "KARTON_ADV_DEBUGGING=%s\n"
                           "KARTON_ADV_EXPERIMENTAL=%s\n"
                           "KARTON_ADV_SESSION_BACKEND=%s\n"
                           "KARTON_ADV_PERFORMANCE_PROFILE=%s\n"
                           "KARTON_ADV_RENDERER=%s",
                           debugging ? "1" : "0",
                           experimental ? "1" : "0",
                           session_backend ? session_backend : "auto",
                           performance_profile ? performance_profile : "balanced",
                           renderer ? renderer : "auto");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed advanced env",
                                              "# END KartON managed advanced env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist advanced environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_ADV_DEBUGGING=%s KARTON_ADV_EXPERIMENTAL=%s KARTON_ADV_SESSION_BACKEND=%s KARTON_ADV_PERFORMANCE_PROFILE=%s KARTON_ADV_RENDERER=%s >/dev/null 2>&1 || true'",
            debugging ? "1" : "0",
            experimental ? "1" : "0",
            session_backend ? session_backend : "auto",
            performance_profile ? performance_profile : "balanced",
            renderer ? renderer : "auto");
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -HUP -x karton >/dev/null 2>&1 || pkill -HUP -x labwc >/dev/null 2>&1 || true'");

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_reload_advanced_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_advanced_config();
    status_set(_("Advanced settings reloaded"), FALSE);
}

static void on_apply_advanced_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_advanced_config();
    char *issues = apply_runtime_advanced();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Advanced settings applied"), FALSE);
}

GtkWidget *page_advanced_new(void)
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

    GtkWidget *title = gtk_label_new(_("Advanced / developer"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Options for advanced users, diagnostics and low-level graphics/session tuning."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *features_frame = create_section(_("Developer controls"),
                                               _("Tune backend, renderer and experimental flags used by the desktop."));
    GtkWidget *features_box = gtk_frame_get_child(GTK_FRAME(features_frame));

    g_debug_switch = gtk_switch_new();
    g_experimental_switch = gtk_switch_new();
    g_session_backend_dropdown = dropdown_from_options(g_session_backend_options,
                                                       G_N_ELEMENTS(g_session_backend_options));
    g_performance_dropdown = dropdown_from_options(g_performance_options,
                                                   G_N_ELEMENTS(g_performance_options));
    g_renderer_dropdown = dropdown_from_options(g_renderer_options,
                                                G_N_ELEMENTS(g_renderer_options));

    gtk_box_append(GTK_BOX(features_box), create_row(_("Debugging"), g_debug_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Experimental features"), g_experimental_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Wayland/X11"), g_session_backend_dropdown));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Performance profiles"), g_performance_dropdown));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Renderer configuration"), g_renderer_dropdown));

    gtk_box_append(GTK_BOX(box), features_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_advanced_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply advanced settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_advanced_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new(_("Advanced options are persisted and synchronized with running session components."));
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_status_label), TRUE);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_debug_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_experimental_switch), FALSE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_session_backend_dropdown),
                               find_option_index(g_session_backend_options,
                                                 G_N_ELEMENTS(g_session_backend_options),
                                                 "auto"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_performance_dropdown),
                               find_option_index(g_performance_options,
                                                 G_N_ELEMENTS(g_performance_options),
                                                 "balanced"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_renderer_dropdown),
                               find_option_index(g_renderer_options,
                                                 G_N_ELEMENTS(g_renderer_options),
                                                 "auto"));

    load_advanced_config();

    return outer_scroll;
}
