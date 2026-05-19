#include "page-accessibility.h"

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

static const struct option_value g_color_filter_options[] = {
    { N_("Off"), "off" },
    { N_("Grayscale"), "grayscale" },
    { N_("Protanopia filter"), "protanopia" },
    { N_("Deuteranopia filter"), "deuteranopia" },
    { N_("Tritanopia filter"), "tritanopia" },
};

static GtkWidget *g_screen_reader_switch = NULL;
static GtkWidget *g_magnifier_switch = NULL;
static GtkWidget *g_high_contrast_switch = NULL;
static GtkWidget *g_captions_switch = NULL;
static GtkWidget *g_onscreen_keyboard_switch = NULL;
static GtkWidget *g_color_filter_dropdown = NULL;
static GtkWidget *g_sticky_keys_switch = NULL;
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

static char *accessibility_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "accessibility.conf", NULL);
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

static void save_accessibility_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "accessibility", "screen_reader", gtk_switch_get_active(GTK_SWITCH(g_screen_reader_switch)));
    g_key_file_set_boolean(kf, "accessibility", "magnifier", gtk_switch_get_active(GTK_SWITCH(g_magnifier_switch)));
    g_key_file_set_boolean(kf, "accessibility", "high_contrast", gtk_switch_get_active(GTK_SWITCH(g_high_contrast_switch)));
    g_key_file_set_boolean(kf, "accessibility", "captions", gtk_switch_get_active(GTK_SWITCH(g_captions_switch)));
    g_key_file_set_boolean(kf, "accessibility", "onscreen_keyboard", gtk_switch_get_active(GTK_SWITCH(g_onscreen_keyboard_switch)));
    g_key_file_set_boolean(kf, "accessibility", "sticky_keys", gtk_switch_get_active(GTK_SWITCH(g_sticky_keys_switch)));
    g_key_file_set_string(kf,
                          "accessibility",
                          "color_filter",
                          dropdown_selected_value(g_color_filter_dropdown,
                                                  g_color_filter_options,
                                                  G_N_ELEMENTS(g_color_filter_options)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = accessibility_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_accessibility_config(void)
{
    char *path = accessibility_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean screen_reader = g_key_file_get_boolean(kf, "accessibility", "screen_reader", &error);
    if (error) {
        g_clear_error(&error);
        screen_reader = FALSE;
    }

    gboolean magnifier = g_key_file_get_boolean(kf, "accessibility", "magnifier", &error);
    if (error) {
        g_clear_error(&error);
        magnifier = FALSE;
    }

    gboolean high_contrast = g_key_file_get_boolean(kf, "accessibility", "high_contrast", &error);
    if (error) {
        g_clear_error(&error);
        high_contrast = FALSE;
    }

    gboolean captions = g_key_file_get_boolean(kf, "accessibility", "captions", &error);
    if (error) {
        g_clear_error(&error);
        captions = FALSE;
    }

    gboolean onscreen_keyboard = g_key_file_get_boolean(kf, "accessibility", "onscreen_keyboard", &error);
    if (error) {
        g_clear_error(&error);
        onscreen_keyboard = FALSE;
    }

    gboolean sticky_keys = g_key_file_get_boolean(kf, "accessibility", "sticky_keys", &error);
    if (error) {
        g_clear_error(&error);
        sticky_keys = FALSE;
    }

    char *color_filter = g_key_file_get_string(kf, "accessibility", "color_filter", &error);
    if (error) {
        g_clear_error(&error);
        color_filter = g_strdup("off");
    }

    gtk_switch_set_active(GTK_SWITCH(g_screen_reader_switch), screen_reader);
    gtk_switch_set_active(GTK_SWITCH(g_magnifier_switch), magnifier);
    gtk_switch_set_active(GTK_SWITCH(g_high_contrast_switch), high_contrast);
    gtk_switch_set_active(GTK_SWITCH(g_captions_switch), captions);
    gtk_switch_set_active(GTK_SWITCH(g_onscreen_keyboard_switch), onscreen_keyboard);
    gtk_switch_set_active(GTK_SWITCH(g_sticky_keys_switch), sticky_keys);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_color_filter_dropdown),
                               find_option_index(g_color_filter_options,
                                                 G_N_ELEMENTS(g_color_filter_options),
                                                 color_filter));

    g_free(color_filter);
    g_key_file_unref(kf);
    g_free(path);
}

static char *apply_runtime_accessibility(void)
{
    gboolean screen_reader = gtk_switch_get_active(GTK_SWITCH(g_screen_reader_switch));
    gboolean magnifier = gtk_switch_get_active(GTK_SWITCH(g_magnifier_switch));
    gboolean high_contrast = gtk_switch_get_active(GTK_SWITCH(g_high_contrast_switch));
    gboolean captions = gtk_switch_get_active(GTK_SWITCH(g_captions_switch));
    gboolean onscreen_keyboard = gtk_switch_get_active(GTK_SWITCH(g_onscreen_keyboard_switch));
    gboolean sticky_keys = gtk_switch_get_active(GTK_SWITCH(g_sticky_keys_switch));
    const char *color_filter = dropdown_selected_value(g_color_filter_dropdown,
                                                        g_color_filter_options,
                                                        G_N_ELEMENTS(g_color_filter_options));

    GString *issues = g_string_new(NULL);

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "KARTON_A11Y_SCREEN_READER=%s\n"
                           "KARTON_A11Y_MAGNIFIER=%s\n"
                           "KARTON_A11Y_HIGH_CONTRAST=%s\n"
                           "KARTON_A11Y_CAPTIONS=%s\n"
                           "KARTON_A11Y_ONSCREEN_KEYBOARD=%s\n"
                           "KARTON_A11Y_COLOR_FILTER=%s\n"
                           "KARTON_A11Y_STICKY_KEYS=%s",
                           screen_reader ? "1" : "0",
                           magnifier ? "1" : "0",
                           high_contrast ? "1" : "0",
                           captions ? "1" : "0",
                           onscreen_keyboard ? "1" : "0",
                           color_filter ? color_filter : "off",
                           sticky_keys ? "1" : "0");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed accessibility env",
                                              "# END KartON managed accessibility env",
                                              env_block->str);

    if (!env_ok) {
        g_string_append(issues, _("Could not persist accessibility environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_A11Y_SCREEN_READER=%s KARTON_A11Y_MAGNIFIER=%s KARTON_A11Y_HIGH_CONTRAST=%s KARTON_A11Y_CAPTIONS=%s KARTON_A11Y_ONSCREEN_KEYBOARD=%s KARTON_A11Y_COLOR_FILTER=%s KARTON_A11Y_STICKY_KEYS=%s >/dev/null 2>&1 || true'",
            screen_reader ? "1" : "0",
            magnifier ? "1" : "0",
            high_contrast ? "1" : "0",
            captions ? "1" : "0",
            onscreen_keyboard ? "1" : "0",
            color_filter ? color_filter : "off",
            sticky_keys ? "1" : "0");
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    (void)run_command_success(
        "sh -lc 'if pkill -USR1 -x karton-settingsd >/dev/null 2>&1; then :; "
        "elif command -v karton-settingsd >/dev/null 2>&1; then "
        "karton-settingsd --reload-runtime >/dev/null 2>&1 & "
        "elif [ -x \"$HOME/.local-karton/bin/karton-settingsd\" ]; then "
        "\"$HOME/.local-karton/bin/karton-settingsd\" --reload-runtime >/dev/null 2>&1 & "
        "fi'"
    );

    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -USR1 -x karton-top-panel >/dev/null 2>&1 || true; pkill -USR1 -x karton-side-dock >/dev/null 2>&1 || true'");
    (void)run_command_success("sh -lc 'pkill -HUP -x karton >/dev/null 2>&1 || pkill -HUP -x labwc >/dev/null 2>&1 || true'");

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_reload_accessibility_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_accessibility_config();
    status_set(_("Accessibility settings reloaded"), FALSE);
}

static void on_apply_accessibility_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_accessibility_config();
    char *issues = apply_runtime_accessibility();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Accessibility settings applied"), FALSE);
}

GtkWidget *page_accessibility_new(void)
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

    GtkWidget *title = gtk_label_new(_("Accessibility"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Configure assistive technologies and sync them with KartON session components and compositor."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *assistive_frame = create_section(_("Assistive technologies"),
                                                _("Turn on the screen reader, captions and on-screen keyboard for daily accessibility needs."));
    GtkWidget *assistive_box = gtk_frame_get_child(GTK_FRAME(assistive_frame));

    g_screen_reader_switch = gtk_switch_new();
    g_captions_switch = gtk_switch_new();
    g_onscreen_keyboard_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(assistive_box), create_row(_("Screen reader"), g_screen_reader_switch));
    gtk_box_append(GTK_BOX(assistive_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(assistive_box), create_row(_("Captions"), g_captions_switch));
    gtk_box_append(GTK_BOX(assistive_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(assistive_box), create_row(_("On-screen keyboard"), g_onscreen_keyboard_switch));

    gtk_box_append(GTK_BOX(box), assistive_frame);

    GtkWidget *vision_frame = create_section(_("Vision"),
                                             _("Tune magnification, contrast and color filters for better readability."));
    GtkWidget *vision_box = gtk_frame_get_child(GTK_FRAME(vision_frame));

    g_magnifier_switch = gtk_switch_new();
    g_high_contrast_switch = gtk_switch_new();
    g_color_filter_dropdown = dropdown_from_options(g_color_filter_options,
                                                     G_N_ELEMENTS(g_color_filter_options));

    gtk_box_append(GTK_BOX(vision_box), create_row(_("Magnifier"), g_magnifier_switch));
    gtk_box_append(GTK_BOX(vision_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(vision_box), create_row(_("High contrast"), g_high_contrast_switch));
    gtk_box_append(GTK_BOX(vision_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(vision_box), create_row(_("Color filters"), g_color_filter_dropdown));

    gtk_box_append(GTK_BOX(box), vision_frame);

    GtkWidget *keyboard_frame = create_section(_("Keyboard accessibility"),
                                               _("Use sticky keys when keyboard shortcuts are hard to press simultaneously."));
    GtkWidget *keyboard_box = gtk_frame_get_child(GTK_FRAME(keyboard_frame));

    g_sticky_keys_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(keyboard_box), create_row(_("Sticky keys"), g_sticky_keys_switch));

    gtk_box_append(GTK_BOX(box), keyboard_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_accessibility_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply accessibility settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_accessibility_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new(_("Changes are propagated to KartON session daemons and compositor."));
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_status_label), TRUE);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_screen_reader_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_magnifier_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_high_contrast_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_captions_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_onscreen_keyboard_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_sticky_keys_switch), FALSE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_color_filter_dropdown),
                               find_option_index(g_color_filter_options,
                                                 G_N_ELEMENTS(g_color_filter_options),
                                                 "off"));

    load_accessibility_config();

    return outer_scroll;
}
