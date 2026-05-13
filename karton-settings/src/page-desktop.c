#include "page-desktop.h"

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

static const struct option_value g_hot_corner_action_options[] = {
    { N_("Disabled"), "disabled" },
    { N_("Workspace overview"), "workspace-overview" },
    { N_("Switch workspace left"), "workspace-left" },
    { N_("Switch workspace right"), "workspace-right" },
    { N_("Toggle show desktop"), "show-desktop" },
    { N_("Open launcher"), "launcher" },
};

static GtkWidget *g_desktop_icons_switch = NULL;
static GtkWidget *g_hot_corners_switch = NULL;
static GtkWidget *g_remember_layout_switch = NULL;
static GtkWidget *g_workspace_wrap_switch = NULL;
static GtkWidget *g_workspace_count_spin = NULL;
static GtkWidget *g_hot_corner_top_left_dropdown = NULL;
static GtkWidget *g_hot_corner_top_right_dropdown = NULL;
static GtkWidget *g_hot_corner_bottom_left_dropdown = NULL;
static GtkWidget *g_hot_corner_bottom_right_dropdown = NULL;
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

static GtkWidget *dropdown_from_options(const struct option_value *options, guint count)
{
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint i = 0; i < count; i++) {
        gtk_string_list_append(model, _(options[i].label));
    }

    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    g_object_unref(model);
    return dropdown;
}

static void update_hot_corner_controls_state(void)
{
    gboolean enabled = g_hot_corners_switch
                       && gtk_switch_get_active(GTK_SWITCH(g_hot_corners_switch));

    if (g_hot_corner_top_left_dropdown) {
        gtk_widget_set_sensitive(g_hot_corner_top_left_dropdown, enabled);
    }
    if (g_hot_corner_top_right_dropdown) {
        gtk_widget_set_sensitive(g_hot_corner_top_right_dropdown, enabled);
    }
    if (g_hot_corner_bottom_left_dropdown) {
        gtk_widget_set_sensitive(g_hot_corner_bottom_left_dropdown, enabled);
    }
    if (g_hot_corner_bottom_right_dropdown) {
        gtk_widget_set_sensitive(g_hot_corner_bottom_right_dropdown, enabled);
    }
}

static void on_hot_corners_switch_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    (void)user_data;
    update_hot_corner_controls_state();
}

static char *desktop_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "desktop.conf", NULL);
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

static void save_desktop_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "desktop", "desktop_icons", gtk_switch_get_active(GTK_SWITCH(g_desktop_icons_switch)));
    g_key_file_set_boolean(kf, "desktop", "hot_corners", gtk_switch_get_active(GTK_SWITCH(g_hot_corners_switch)));
    g_key_file_set_boolean(kf, "desktop", "remember_layout", gtk_switch_get_active(GTK_SWITCH(g_remember_layout_switch)));
    g_key_file_set_boolean(kf, "desktop", "workspace_wrap", gtk_switch_get_active(GTK_SWITCH(g_workspace_wrap_switch)));
    g_key_file_set_integer(kf, "desktop", "workspace_count", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_workspace_count_spin)));
    g_key_file_set_string(kf, "desktop", "corner_top_left",
                          dropdown_selected_value(g_hot_corner_top_left_dropdown,
                                                  g_hot_corner_action_options,
                                                  G_N_ELEMENTS(g_hot_corner_action_options)));
    g_key_file_set_string(kf, "desktop", "corner_top_right",
                          dropdown_selected_value(g_hot_corner_top_right_dropdown,
                                                  g_hot_corner_action_options,
                                                  G_N_ELEMENTS(g_hot_corner_action_options)));
    g_key_file_set_string(kf, "desktop", "corner_bottom_left",
                          dropdown_selected_value(g_hot_corner_bottom_left_dropdown,
                                                  g_hot_corner_action_options,
                                                  G_N_ELEMENTS(g_hot_corner_action_options)));
    g_key_file_set_string(kf, "desktop", "corner_bottom_right",
                          dropdown_selected_value(g_hot_corner_bottom_right_dropdown,
                                                  g_hot_corner_action_options,
                                                  G_N_ELEMENTS(g_hot_corner_action_options)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = desktop_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_desktop_config(void)
{
    char *path = desktop_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean desktop_icons = g_key_file_get_boolean(kf, "desktop", "desktop_icons", &error);
    if (error) {
        g_clear_error(&error);
        desktop_icons = TRUE;
    }

    gboolean hot_corners = g_key_file_get_boolean(kf, "desktop", "hot_corners", &error);
    if (error) {
        g_clear_error(&error);
        hot_corners = FALSE;
    }

    gboolean remember_layout = g_key_file_get_boolean(kf, "desktop", "remember_layout", &error);
    if (error) {
        g_clear_error(&error);
        remember_layout = TRUE;
    }

    gboolean workspace_wrap = g_key_file_get_boolean(kf, "desktop", "workspace_wrap", &error);
    if (error) {
        g_clear_error(&error);
        workspace_wrap = TRUE;
    }

    int workspace_count = g_key_file_get_integer(kf, "desktop", "workspace_count", &error);
    if (error) {
        g_clear_error(&error);
        workspace_count = 4;
    }

    char *corner_top_left = g_key_file_get_string(kf, "desktop", "corner_top_left", &error);
    if (error) {
        g_clear_error(&error);
        corner_top_left = g_strdup("show-desktop");
    }

    char *corner_top_right = g_key_file_get_string(kf, "desktop", "corner_top_right", &error);
    if (error) {
        g_clear_error(&error);
        corner_top_right = g_strdup("workspace-overview");
    }

    char *corner_bottom_left = g_key_file_get_string(kf, "desktop", "corner_bottom_left", &error);
    if (error) {
        g_clear_error(&error);
        corner_bottom_left = g_strdup("workspace-left");
    }

    char *corner_bottom_right = g_key_file_get_string(kf, "desktop", "corner_bottom_right", &error);
    if (error) {
        g_clear_error(&error);
        corner_bottom_right = g_strdup("workspace-right");
    }

    gtk_switch_set_active(GTK_SWITCH(g_desktop_icons_switch), desktop_icons);
    gtk_switch_set_active(GTK_SWITCH(g_hot_corners_switch), hot_corners);
    gtk_switch_set_active(GTK_SWITCH(g_remember_layout_switch), remember_layout);
    gtk_switch_set_active(GTK_SWITCH(g_workspace_wrap_switch), workspace_wrap);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_workspace_count_spin), CLAMP(workspace_count, 1, 12));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_top_left_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 corner_top_left));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_top_right_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 corner_top_right));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_bottom_left_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 corner_bottom_left));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_bottom_right_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 corner_bottom_right));
    update_hot_corner_controls_state();

    g_free(corner_bottom_right);
    g_free(corner_bottom_left);
    g_free(corner_top_right);
    g_free(corner_top_left);
    g_key_file_unref(kf);
    g_free(path);
}

static char *apply_runtime_desktop(void)
{
    gboolean desktop_icons = gtk_switch_get_active(GTK_SWITCH(g_desktop_icons_switch));
    gboolean hot_corners = gtk_switch_get_active(GTK_SWITCH(g_hot_corners_switch));
    gboolean remember_layout = gtk_switch_get_active(GTK_SWITCH(g_remember_layout_switch));
    gboolean workspace_wrap = gtk_switch_get_active(GTK_SWITCH(g_workspace_wrap_switch));
    int workspace_count = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_workspace_count_spin));
    const char *corner_top_left = dropdown_selected_value(g_hot_corner_top_left_dropdown,
                                                           g_hot_corner_action_options,
                                                           G_N_ELEMENTS(g_hot_corner_action_options));
    const char *corner_top_right = dropdown_selected_value(g_hot_corner_top_right_dropdown,
                                                            g_hot_corner_action_options,
                                                            G_N_ELEMENTS(g_hot_corner_action_options));
    const char *corner_bottom_left = dropdown_selected_value(g_hot_corner_bottom_left_dropdown,
                                                              g_hot_corner_action_options,
                                                              G_N_ELEMENTS(g_hot_corner_action_options));
    const char *corner_bottom_right = dropdown_selected_value(g_hot_corner_bottom_right_dropdown,
                                                               g_hot_corner_action_options,
                                                               G_N_ELEMENTS(g_hot_corner_action_options));

    GString *issues = g_string_new(NULL);

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_DESKTOP_ICONS=%s\n"
                           "KARTON_DESKTOP_HOT_CORNERS=%s\n"
                           "KARTON_DESKTOP_CORNER_TOP_LEFT=%s\n"
                           "KARTON_DESKTOP_CORNER_TOP_RIGHT=%s\n"
                           "KARTON_DESKTOP_CORNER_BOTTOM_LEFT=%s\n"
                           "KARTON_DESKTOP_CORNER_BOTTOM_RIGHT=%s\n"
                           "KARTON_DESKTOP_REMEMBER_LAYOUT=%s\n"
                           "KARTON_DESKTOP_WORKSPACE_WRAP=%s\n"
                           "KARTON_DESKTOP_WORKSPACE_COUNT=%d",
                           desktop_icons ? "1" : "0",
                           hot_corners ? "1" : "0",
                           corner_top_left ? corner_top_left : "disabled",
                           corner_top_right ? corner_top_right : "disabled",
                           corner_bottom_left ? corner_bottom_left : "disabled",
                           corner_bottom_right ? corner_bottom_right : "disabled",
                           remember_layout ? "1" : "0",
                           workspace_wrap ? "1" : "0",
                           CLAMP(workspace_count, 1, 12));

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed desktop env",
                                              "# END KartON managed desktop env",
                                              env_block->str);

    if (!env_ok) {
        g_string_append(issues, _("Could not persist desktop environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_DESKTOP_ICONS=%s KARTON_DESKTOP_HOT_CORNERS=%s KARTON_DESKTOP_CORNER_TOP_LEFT=%s KARTON_DESKTOP_CORNER_TOP_RIGHT=%s KARTON_DESKTOP_CORNER_BOTTOM_LEFT=%s KARTON_DESKTOP_CORNER_BOTTOM_RIGHT=%s KARTON_DESKTOP_REMEMBER_LAYOUT=%s KARTON_DESKTOP_WORKSPACE_WRAP=%s KARTON_DESKTOP_WORKSPACE_COUNT=%d >/dev/null 2>&1 || true'",
            desktop_icons ? "1" : "0",
            hot_corners ? "1" : "0",
            corner_top_left ? corner_top_left : "disabled",
            corner_top_right ? corner_top_right : "disabled",
            corner_bottom_left ? corner_bottom_left : "disabled",
            corner_bottom_right ? corner_bottom_right : "disabled",
            remember_layout ? "1" : "0",
            workspace_wrap ? "1" : "0",
            CLAMP(workspace_count, 1, 12));
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    (void)run_command_success(
        "sh -lc 'if pkill -USR1 -x karton-settingsd >/dev/null 2>&1; then :; "
        "elif command -v karton-settingsd >/dev/null 2>&1; then "
        "karton-settingsd --reload-desktop >/dev/null 2>&1 & "
        "elif [ -x \"$HOME/.local-karton/bin/karton-settingsd\" ]; then "
        "\"$HOME/.local-karton/bin/karton-settingsd\" --reload-desktop >/dev/null 2>&1 & "
        "fi'"
    );

    (void)run_command_success("sh -lc 'pkill -HUP -x karton >/dev/null 2>&1 || pkill -HUP -x labwc >/dev/null 2>&1 || true'");

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_reload_desktop_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_desktop_config();
    status_set(_("Desktop settings reloaded"), FALSE);
}

static void on_apply_desktop_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_desktop_config();
    char *issues = apply_runtime_desktop();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Desktop settings applied"), FALSE);
}

GtkWidget *page_desktop_new(void)
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

    GtkWidget *title = gtk_label_new(_("Desktop and layout"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Configure workspace behavior, desktop icons and hot corners for everyday workflow."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *behavior_frame = create_section(_("Desktop behavior"),
                                               _("Control how the desktop reacts during daily work and login."));
    GtkWidget *behavior_box = gtk_frame_get_child(GTK_FRAME(behavior_frame));

    g_desktop_icons_switch = gtk_switch_new();
    g_hot_corners_switch = gtk_switch_new();
    g_remember_layout_switch = gtk_switch_new();
    g_signal_connect(g_hot_corners_switch, "notify::active",
                     G_CALLBACK(on_hot_corners_switch_changed), NULL);

    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Desktop icons"), g_desktop_icons_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Hot corners"), g_hot_corners_switch));
    gtk_box_append(GTK_BOX(behavior_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(behavior_box), create_row(_("Remember layout on login"), g_remember_layout_switch));

    gtk_box_append(GTK_BOX(box), behavior_frame);

    GtkWidget *workspace_frame = create_section(_("Workspaces"),
                                                _("Tune the number and navigation behavior of virtual desktops."));
    GtkWidget *workspace_box = gtk_frame_get_child(GTK_FRAME(workspace_frame));

    g_workspace_count_spin = gtk_spin_button_new_with_range(1.0, 12.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_workspace_count_spin), 4.0);
    g_workspace_wrap_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(workspace_box), create_row(_("Workspace count"), g_workspace_count_spin));
    gtk_box_append(GTK_BOX(workspace_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(workspace_box), create_row(_("Workspace wrap-around"), g_workspace_wrap_switch));

    gtk_box_append(GTK_BOX(box), workspace_frame);

    GtkWidget *corners_frame = create_section(_("Hot corners"),
                                              _("Choose what happens when the pointer reaches each screen corner."));
    GtkWidget *corners_box = gtk_frame_get_child(GTK_FRAME(corners_frame));

    g_hot_corner_top_left_dropdown = dropdown_from_options(g_hot_corner_action_options,
                                                            G_N_ELEMENTS(g_hot_corner_action_options));
    g_hot_corner_top_right_dropdown = dropdown_from_options(g_hot_corner_action_options,
                                                             G_N_ELEMENTS(g_hot_corner_action_options));
    g_hot_corner_bottom_left_dropdown = dropdown_from_options(g_hot_corner_action_options,
                                                               G_N_ELEMENTS(g_hot_corner_action_options));
    g_hot_corner_bottom_right_dropdown = dropdown_from_options(g_hot_corner_action_options,
                                                                G_N_ELEMENTS(g_hot_corner_action_options));

    gtk_box_append(GTK_BOX(corners_box), create_row(_("Top-left corner"), g_hot_corner_top_left_dropdown));
    gtk_box_append(GTK_BOX(corners_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(corners_box), create_row(_("Top-right corner"), g_hot_corner_top_right_dropdown));
    gtk_box_append(GTK_BOX(corners_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(corners_box), create_row(_("Bottom-left corner"), g_hot_corner_bottom_left_dropdown));
    gtk_box_append(GTK_BOX(corners_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(corners_box), create_row(_("Bottom-right corner"), g_hot_corner_bottom_right_dropdown));

    gtk_box_append(GTK_BOX(box), corners_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_desktop_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply desktop settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_desktop_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_desktop_icons_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_hot_corners_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_remember_layout_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_workspace_wrap_switch), TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_top_left_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 "show-desktop"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_top_right_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 "workspace-overview"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_bottom_left_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 "workspace-left"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_hot_corner_bottom_right_dropdown),
                               find_option_index(g_hot_corner_action_options,
                                                 G_N_ELEMENTS(g_hot_corner_action_options),
                                                 "workspace-right"));
    update_hot_corner_controls_state();

    load_desktop_config();

    return outer_scroll;
}