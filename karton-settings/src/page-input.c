#include "page-input.h"

#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <glib.h>
#include <libintl.h>
#include <stdio.h>
#include <string.h>

#define _(s) gettext(s)
#define N_(s) s

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_layout_options[] = {
    { N_("Polish"), "pl" },
    { N_("English (US)"), "us" },
    { N_("German"), "de" },
    { N_("French"), "fr" },
    { N_("Spanish"), "es" },
    { N_("Czech"), "cz" },
};

static const struct option_value g_shortcut_options[] = {
    { N_("Default"), "default" },
    { N_("Developer"), "developer" },
    { N_("Compact"), "compact" },
};

static const struct option_value g_tablet_mode_options[] = {
    { N_("Absolute mapping"), "absolute" },
    { N_("Relative mapping"), "relative" },
};

struct shortcut_binding {
    const char *id;
    const char *label;
    const char *default_default;
    const char *default_developer;
    const char *default_compact;
    const char *action;
    const char *arg_name;
    const char *arg_value;
};

static const struct shortcut_binding g_shortcut_bindings[] = {
    {
        "launch_terminal",
        N_("Launch terminal"),
        "W-Return",
        "W-t",
        "W-Return",
        "Execute",
        "command",
        "lab-sensible-terminal",
    },
    {
        "close_window",
        N_("Close window"),
        "A-F4",
        "W-q",
        "A-F4",
        "Close",
        NULL,
        NULL,
    },
    {
        "next_window",
        N_("Next window"),
        "A-Tab",
        "A-Tab",
        "A-Tab",
        "NextWindow",
        NULL,
        NULL,
    },
    {
        "previous_window",
        N_("Previous window"),
        "A-S-Tab",
        "A-S-Tab",
        "A-S-Tab",
        "PreviousWindow",
        NULL,
        NULL,
    },
    {
        "window_menu",
        N_("Window menu"),
        "A-Space",
        "A-Space",
        "A-Space",
        "ShowMenu",
        "menu",
        "client-menu",
    },
    {
        "toggle_maximize",
        N_("Toggle maximize"),
        "W-a",
        "W-a",
        "W-a",
        "ToggleMaximize",
        NULL,
        NULL,
    },
    {
        "snap_left",
        N_("Snap window left"),
        "W-Left",
        "W-Left",
        "",
        "SnapToEdge",
        "direction",
        "left",
    },
    {
        "snap_right",
        N_("Snap window right"),
        "W-Right",
        "W-Right",
        "",
        "SnapToEdge",
        "direction",
        "right",
    },
    {
        "snap_up",
        N_("Snap window up"),
        "W-Up",
        "W-Up",
        "",
        "SnapToEdge",
        "direction",
        "up",
    },
    {
        "snap_down",
        N_("Snap window down"),
        "W-Down",
        "W-Down",
        "",
        "SnapToEdge",
        "direction",
        "down",
    },
    {
        "screenshot_full",
        N_("Screenshot full"),
        "Print",
        "Print",
        "Print",
        "Execute",
        "command",
        "sh -lc '$HOME/.local-karton/bin/karton-screenshot full'",
    },
    {
        "screenshot_area",
        N_("Screenshot area"),
        "S-Print",
        "S-Print",
        "S-Print",
        "Execute",
        "command",
        "sh -lc '$HOME/.local-karton/bin/karton-screenshot area'",
    },
    {
        "screenshot_full_copy",
        N_("Screenshot full (copy)"),
        "C-Print",
        "C-Print",
        "C-Print",
        "Execute",
        "command",
        "sh -lc '$HOME/.local-karton/bin/karton-screenshot full-copy'",
    },
    {
        "screenshot_area_copy",
        N_("Screenshot area (copy)"),
        "C-S-Print",
        "C-S-Print",
        "C-S-Print",
        "Execute",
        "command",
        "sh -lc '$HOME/.local-karton/bin/karton-screenshot area-copy'",
    },
};

static GtkWidget *g_layout_dropdown = NULL;
static GtkWidget *g_shortcuts_dropdown = NULL;
static GtkWidget *g_shortcuts_configure_button = NULL;
static GtkWidget *g_cursor_speed_scale = NULL;
static GtkWidget *g_touchpad_switch = NULL;
static GtkWidget *g_gestures_switch = NULL;
static GtkWidget *g_natural_scroll_switch = NULL;
static GtkWidget *g_multimedia_switch = NULL;
static GtkWidget *g_tablet_switch = NULL;
static GtkWidget *g_tablet_mode_dropdown = NULL;
static GtkWidget *g_controller_switch = NULL;
static GtkWidget *g_detected_devices_box = NULL;
static GtkWidget *g_status_label = NULL;
static GtkStringList *g_layout_model = NULL;
static GPtrArray *g_layout_values = NULL;
static GHashTable *g_layout_name_map = NULL;
static char *g_shortcut_values[G_N_ELEMENTS(g_shortcut_bindings)] = { 0 };

#define RCXML_INPUT_BEGIN_MARKER "<!-- BEGIN KartON managed input -->"
#define RCXML_INPUT_END_MARKER "<!-- END KartON managed input -->"

static void append_keybind_action(GString *xml,
                                  const char *key,
                                  const char *action,
                                  const char *arg_name,
                                  const char *arg_value);
static void append_managed_shortcut_resets(GString *block);
static gboolean apply_keyboard_environment(void);
static gboolean apply_compositor_input_config(void);
static void save_input_config(void);
static char *apply_runtime_input(void);
static void refresh_detected_input_devices(void);
static const char *selected_layout_value(void);
static void rebuild_layout_model_from_system(void);
static char *normalize_layout_value(const char *raw);
static void ensure_layout_name_map(void);

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

static char *input_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "input.conf", NULL);
}

static char *session_environment_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "environment", NULL);
}

static char *karton_rcxml_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "rc.xml", NULL);
}

static char *labwc_rcxml_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "labwc", "rc.xml", NULL);
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

static GtkWidget *create_slider_row(const char *title, const char *subtitle, GtkWidget *scale)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), title_lbl);

    if (subtitle && *subtitle) {
        GtkWidget *subtitle_lbl = gtk_label_new(subtitle);
        gtk_widget_set_halign(subtitle_lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(subtitle_lbl, "row-subtitle");
        gtk_box_append(GTK_BOX(row), subtitle_lbl);
    }

    gtk_box_append(GTK_BOX(row), scale);
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

static guint find_layout_value_index(const char *value)
{
    if (!g_layout_values || g_layout_values->len == 0 || !value) {
        return 0;
    }

    for (guint i = 0; i < g_layout_values->len; i++) {
        const char *item = g_ptr_array_index(g_layout_values, i);
        if (g_strcmp0(item, value) == 0) {
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

static const char *selected_layout_value(void)
{
    if (!g_layout_dropdown || !g_layout_values || g_layout_values->len == 0) {
        return "us";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_layout_dropdown));
    if (idx >= g_layout_values->len) {
        idx = 0;
    }

    const char *value = g_ptr_array_index(g_layout_values, idx);
    return (value && *value) ? value : "us";
}

static const char *friendly_layout_label(const char *layout)
{
    ensure_layout_name_map();
    if (g_layout_name_map) {
        const char *mapped = g_hash_table_lookup(g_layout_name_map, layout);
        if (mapped && *mapped) {
            return mapped;
        }
    }

    for (guint i = 0; i < G_N_ELEMENTS(g_layout_options); i++) {
        if (g_strcmp0(layout, g_layout_options[i].value) == 0) {
            return _(g_layout_options[i].label);
        }
    }

    return layout;
}

static void parse_layout_names_from_rules_file(const char *path)
{
    if (!path || !g_layout_name_map || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return;
    }

    char *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL) || !content) {
        g_free(content);
        return;
    }

    gboolean in_layout_section = FALSE;
    gchar **lines = g_strsplit(content, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        char *line = g_strdup(lines[i]);
        g_strstrip(line);

        if (!*line) {
            g_free(line);
            continue;
        }

        if (line[0] == '!') {
            in_layout_section = g_str_has_prefix(line, "! layout");
            g_free(line);
            continue;
        }

        if (!in_layout_section) {
            g_free(line);
            continue;
        }

        char **parts = g_strsplit_set(line, " \t", 2);
        if (parts[0] && parts[0][0] && parts[1] && parts[1][0]) {
            char *layout_id = g_strdup(parts[0]);
            char *layout_name = g_strdup(parts[1]);
            g_strstrip(layout_id);
            g_strstrip(layout_name);

            if (*layout_id && *layout_name && !g_hash_table_contains(g_layout_name_map, layout_id)) {
                g_hash_table_insert(g_layout_name_map, layout_id, layout_name);
            } else {
                g_free(layout_id);
                g_free(layout_name);
            }
        }

        g_strfreev(parts);
        g_free(line);
    }

    g_strfreev(lines);
    g_free(content);
}

static void ensure_layout_name_map(void)
{
    if (g_layout_name_map) {
        return;
    }

    g_layout_name_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    /* Prefer XKB rules files shipped by xkeyboard-config. */
    parse_layout_names_from_rules_file("/usr/share/X11/xkb/rules/base.lst");
    parse_layout_names_from_rules_file("/usr/share/X11/xkb/rules/evdev.lst");
}

static void append_layout_choice(const char *layout)
{
    if (!layout || !*layout || !g_layout_model || !g_layout_values) {
        return;
    }

    for (guint i = 0; i < g_layout_values->len; i++) {
        const char *existing = g_ptr_array_index(g_layout_values, i);
        if (g_strcmp0(existing, layout) == 0) {
            return;
        }
    }

    const char *label = friendly_layout_label(layout);
    char *display = NULL;
    if (g_strcmp0(label, layout) == 0) {
        display = g_strdup(layout);
    } else {
        display = g_strdup_printf("%s (%s)", label, layout);
    }

    gtk_string_list_append(g_layout_model, display);
    g_ptr_array_add(g_layout_values, g_strdup(layout));
    g_free(display);
}

static void rebuild_layout_model_from_system(void)
{
    g_layout_model = gtk_string_list_new(NULL);
    g_layout_values = g_ptr_array_new_with_free_func(g_free);

    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean have_system_layouts = FALSE;

    if (command_is_available("localectl")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture("sh -lc 'localectl list-x11-keymap-layouts 2>/dev/null'",
                                          &stdout_data,
                                          NULL,
                                          NULL);

        if (ok && stdout_data && *stdout_data) {
            gchar **lines = g_strsplit(stdout_data, "\n", -1);
            for (guint i = 0; lines[i] != NULL; i++) {
                char *layout = normalize_layout_value(lines[i]);
                if (!layout) {
                    continue;
                }

                if (!g_hash_table_contains(seen, layout)) {
                    append_layout_choice(layout);
                    g_hash_table_add(seen, g_strdup(layout));
                    have_system_layouts = TRUE;
                }

                g_free(layout);
            }
            g_strfreev(lines);
        }

        g_free(stdout_data);
    }

    if (!have_system_layouts) {
        for (guint i = 0; i < G_N_ELEMENTS(g_layout_options); i++) {
            append_layout_choice(g_layout_options[i].value);
        }
    }

    if (g_layout_values->len == 0) {
        append_layout_choice("us");
    }

    g_hash_table_destroy(seen);
}

static char *normalize_layout_value(const char *raw)
{
    if (!raw) {
        return NULL;
    }

    char *copy = g_strdup(raw);
    g_strstrip(copy);

    if (!*copy) {
        g_free(copy);
        return NULL;
    }

    gsize len = strlen(copy);
    if (len >= 2 && ((copy[0] == '"' && copy[len - 1] == '"') ||
                     (copy[0] == '\'' && copy[len - 1] == '\''))) {
        copy[len - 1] = '\0';
        memmove(copy, copy + 1, len - 1);
    }

    char *comma = strchr(copy, ',');
    if (comma) {
        *comma = '\0';
    }

    char *space = strpbrk(copy, " \t;");
    if (space) {
        *space = '\0';
    }

    char *paren = strchr(copy, '(');
    if (paren) {
        *paren = '\0';
    }

    g_strstrip(copy);

    if (!*copy) {
        g_free(copy);
        return NULL;
    }

    return copy;
}

static char *normalize_device_name_value(const char *raw)
{
    if (!raw) {
        return NULL;
    }

    char *copy = g_strdup(raw);
    g_strstrip(copy);

    if (!*copy) {
        g_free(copy);
        return NULL;
    }

    gsize len = strlen(copy);
    if (len >= 2 && copy[0] == '"' && copy[len - 1] == '"') {
        copy[len - 1] = '\0';
        memmove(copy, copy + 1, len - 1);
        g_strstrip(copy);
    }

    if (!*copy) {
        g_free(copy);
        return NULL;
    }

    return copy;
}

static void add_unique_device_name(GPtrArray *devices, GHashTable *seen, const char *name)
{
    char *normalized = normalize_device_name_value(name);
    if (!normalized) {
        return;
    }

    if (g_hash_table_contains(seen, normalized)) {
        g_free(normalized);
        return;
    }

    g_hash_table_add(seen, g_strdup(normalized));
    g_ptr_array_add(devices, normalized);
}

static void collect_input_devices_from_libinput(GPtrArray *devices, GHashTable *seen)
{
    if (!command_is_available("libinput")) {
        return;
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture("sh -lc 'timeout 2s libinput list-devices 2>/dev/null'",
                                      &stdout_data,
                                      NULL,
                                      NULL);
    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return;
    }

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        char *line = g_strdup(lines[i]);
        g_strstrip(line);

        if (g_str_has_prefix(line, "Device:")) {
            const char *name = line + strlen("Device:");
            add_unique_device_name(devices, seen, name);
        }

        g_free(line);
    }

    g_strfreev(lines);
    g_free(stdout_data);
}

static void collect_input_devices_from_procfs(GPtrArray *devices, GHashTable *seen)
{
    char *content = NULL;
    if (!g_file_get_contents("/proc/bus/input/devices", &content, NULL, NULL) || !content) {
        g_free(content);
        return;
    }

    gchar **lines = g_strsplit(content, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];
        if (!g_str_has_prefix(line, "N: Name=")) {
            continue;
        }

        const char *name = line + strlen("N: Name=");
        add_unique_device_name(devices, seen, name);
    }

    g_strfreev(lines);
    g_free(content);
}

static void refresh_detected_input_devices(void)
{
    if (!g_detected_devices_box) {
        return;
    }

    GtkWidget *child = gtk_widget_get_first_child(g_detected_devices_box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(g_detected_devices_box), child);
        child = next;
    }

    GPtrArray *devices = g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    collect_input_devices_from_libinput(devices, seen);
    if (devices->len == 0) {
        collect_input_devices_from_procfs(devices, seen);
    }

    if (devices->len == 0) {
        GtkWidget *empty = gtk_label_new(_("No input devices detected right now."));
        gtk_widget_set_halign(empty, GTK_ALIGN_START);
        gtk_widget_add_css_class(empty, "row-subtitle");
        gtk_box_append(GTK_BOX(g_detected_devices_box), empty);
    } else {
        for (guint i = 0; i < devices->len; i++) {
            const char *name = g_ptr_array_index(devices, i);
            GtkWidget *label = gtk_label_new(name);
            gtk_widget_set_halign(label, GTK_ALIGN_START);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_box_append(GTK_BOX(g_detected_devices_box), label);
        }
    }

    g_hash_table_destroy(seen);
    g_ptr_array_unref(devices);
}

static void on_refresh_detected_devices_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_detected_input_devices();
    status_set(_("Detected input devices list refreshed"), FALSE);
}

static char *extract_assignment_value(const char *line, const char *key)
{
    if (!line || !key) {
        return NULL;
    }

    const char *p = line;
    while (g_ascii_isspace(*p)) {
        p++;
    }

    if (*p == '#' || *p == '\0') {
        return NULL;
    }

    if (g_str_has_prefix(p, "export ")) {
        p += strlen("export ");
        while (g_ascii_isspace(*p)) {
            p++;
        }
    }

    if (!g_str_has_prefix(p, key)) {
        return NULL;
    }

    p += strlen(key);
    while (g_ascii_isspace(*p)) {
        p++;
    }

    if (*p != '=') {
        return NULL;
    }
    p++;

    while (g_ascii_isspace(*p)) {
        p++;
    }

    return normalize_layout_value(p);
}

static char *layout_from_assignment_file(const char *file_path, const char *key)
{
    char *content = NULL;
    if (!g_file_get_contents(file_path, &content, NULL, NULL)) {
        return NULL;
    }

    gchar **lines = g_strsplit(content, "\n", -1);
    char *found = NULL;

    for (guint i = 0; lines[i] != NULL; i++) {
        found = extract_assignment_value(lines[i], key);
        if (found) {
            break;
        }
    }

    g_strfreev(lines);
    g_free(content);
    return found;
}

static char *layout_from_localectl(void)
{
    if (!command_is_available("localectl")) {
        return NULL;
    }

    char *stdout_data = NULL;
    if (!run_command_capture("sh -lc 'localectl status 2>/dev/null'", &stdout_data, NULL, NULL)) {
        g_free(stdout_data);
        return NULL;
    }

    char *result = NULL;
    gchar **lines = g_strsplit(stdout_data ? stdout_data : "", "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        char *trimmed = g_strdup(lines[i]);
        g_strstrip(trimmed);

        if (g_str_has_prefix(trimmed, "X11 Layout:")) {
            const char *value = strchr(trimmed, ':');
            if (value) {
                value++;
                result = normalize_layout_value(value);
            }
            g_free(trimmed);
            break;
        }

        g_free(trimmed);
    }

    g_strfreev(lines);
    g_free(stdout_data);
    return result;
}

static char *detected_system_layout(void)
{
    char *layout = layout_from_localectl();
    if (layout) {
        return layout;
    }

    layout = layout_from_assignment_file("/etc/default/keyboard", "XKBLAYOUT");
    if (layout) {
        return layout;
    }

    char *env_path = session_environment_path();
    layout = layout_from_assignment_file(env_path, "XKB_DEFAULT_LAYOUT");
    g_free(env_path);
    if (layout) {
        return layout;
    }

    return normalize_layout_value(g_getenv("XKB_DEFAULT_LAYOUT"));
}

static guint default_layout_index_from_system(void)
{
    char *layout = detected_system_layout();
    if (!layout) {
        return 0;
    }

    guint idx = find_layout_value_index(layout);
    g_free(layout);
    return idx;
}

static void append_keybind_none(GString *xml, const char *key)
{
    char *escaped_key = g_markup_escape_text(key, -1);
    g_string_append_printf(xml,
                           "    <keybind key=\"%s\">\n"
                           "      <action name=\"None\" />\n"
                           "    </keybind>\n",
                           escaped_key ? escaped_key : "");
    g_free(escaped_key);
}

static gboolean write_managed_xml_block(const char *file_path,
                                        const char *begin_marker,
                                        const char *end_marker,
                                        const char *xml_block)
{
    char *existing = NULL;
    if (!g_file_get_contents(file_path, &existing, NULL, NULL)) {
        existing = g_strdup("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<labwc_config>\n</labwc_config>\n");
    }

    gchar **lines = g_strsplit(existing, "\n", -1);
    GString *filtered = g_string_new(NULL);
    gboolean skip = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        char *trim = g_strdup(lines[i]);
        g_strstrip(trim);

        if (g_strcmp0(trim, begin_marker) == 0) {
            skip = TRUE;
            g_free(trim);
            continue;
        }

        if (g_strcmp0(trim, end_marker) == 0) {
            skip = FALSE;
            g_free(trim);
            continue;
        }

        g_free(trim);

        if (skip) {
            continue;
        }

        g_string_append(filtered, lines[i]);
        if (lines[i + 1] != NULL) {
            g_string_append_c(filtered, '\n');
        }
    }

    GString *managed = g_string_new(NULL);
    g_string_append_printf(managed, "  %s\n", begin_marker);
    if (xml_block && *xml_block) {
        g_string_append(managed, xml_block);
        if (managed->str[managed->len - 1] != '\n') {
            g_string_append_c(managed, '\n');
        }
    }
    g_string_append_printf(managed, "  %s\n", end_marker);

    const char *close_tag = g_strrstr(filtered->str, "</labwc_config>");
    if (!close_tag) {
        close_tag = g_strrstr(filtered->str, "</openbox_config>");
    }

    GString *out = g_string_new(NULL);
    if (!close_tag) {
        g_string_append(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<labwc_config>\n");
        g_string_append(out, managed->str);
        g_string_append(out, "</labwc_config>\n");
    } else {
        gsize prefix_len = (gsize)(close_tag - filtered->str);
        g_string_append_len(out, filtered->str, prefix_len);
        if (out->len > 0 && out->str[out->len - 1] != '\n') {
            g_string_append_c(out, '\n');
        }
        g_string_append(out, managed->str);
        g_string_append(out, close_tag);
        if (out->len > 0 && out->str[out->len - 1] != '\n') {
            g_string_append_c(out, '\n');
        }
    }

    gboolean ok = FALSE;
    char *dir = g_path_get_dirname(file_path);
    if (g_mkdir_with_parents(dir, 0700) == 0) {
        ok = g_file_set_contents(file_path, out->str, -1, NULL);
    }

    g_free(dir);
    g_string_free(out, TRUE);
    g_string_free(managed, TRUE);
    g_string_free(filtered, TRUE);
    g_strfreev(lines);
    g_free(existing);

    return ok;
}

static gboolean apply_compositor_input_config(void)
{
    const char *profile = dropdown_selected_value(g_shortcuts_dropdown,
                                                  g_shortcut_options,
                                                  G_N_ELEMENTS(g_shortcut_options));

    gboolean touchpad_enabled = gtk_switch_get_active(GTK_SWITCH(g_touchpad_switch));
    gboolean gestures_enabled = gtk_switch_get_active(GTK_SWITCH(g_gestures_switch));
    gboolean natural_scroll = gtk_switch_get_active(GTK_SWITCH(g_natural_scroll_switch));
    gboolean multimedia_enabled = gtk_switch_get_active(GTK_SWITCH(g_multimedia_switch));

    double slider = gtk_range_get_value(GTK_RANGE(g_cursor_speed_scale));
    double pointer_speed = (slider / 50.0) - 1.0;

    GString *block = g_string_new(NULL);
    g_string_append(block, "  <keyboard>\n");

    append_managed_shortcut_resets(block);

    if (g_strcmp0(profile, "compact") == 0) {
        append_keybind_none(block, "W-Left");
        append_keybind_none(block, "W-Right");
        append_keybind_none(block, "W-Up");
        append_keybind_none(block, "W-Down");
    }

    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        const char *binding = g_shortcut_values[i];
        if (!binding || !*binding) {
            continue;
        }

        append_keybind_action(block,
                              binding,
                              g_shortcut_bindings[i].action,
                              g_shortcut_bindings[i].arg_name,
                              g_shortcut_bindings[i].arg_value);
    }

    if (!multimedia_enabled) {
        append_keybind_none(block, "XF86AudioLowerVolume");
        append_keybind_none(block, "XF86AudioRaiseVolume");
        append_keybind_none(block, "XF86AudioMute");
        append_keybind_none(block, "XF86MonBrightnessUp");
        append_keybind_none(block, "XF86MonBrightnessDown");
    }

    g_string_append(block, "  </keyboard>\n");
    g_string_append(block, "  <libinput>\n");
    g_string_append_printf(block,
                           "    <device category=\"default\">\n"
                           "      <pointerSpeed>%.2f</pointerSpeed>\n"
                           "    </device>\n",
                           pointer_speed);

    g_string_append_printf(block,
                           "    <device category=\"touchpad\">\n"
                           "      <sendEventsMode>%s</sendEventsMode>\n"
                           "      <naturalScroll>%s</naturalScroll>\n"
                           "      <tap>%s</tap>\n"
                           "      <threeFingerDrag>%s</threeFingerDrag>\n"
                           "      <scrollMethod>twofinger</scrollMethod>\n"
                           "    </device>\n",
                           touchpad_enabled ? "yes" : "no",
                           natural_scroll ? "yes" : "no",
                           touchpad_enabled ? "yes" : "no",
                           (touchpad_enabled && gestures_enabled) ? "yes" : "no");
    g_string_append(block, "  </libinput>\n");

    char *karton_path = karton_rcxml_path();
    gboolean ok = write_managed_xml_block(karton_path,
                                          RCXML_INPUT_BEGIN_MARKER,
                                          RCXML_INPUT_END_MARKER,
                                          block->str);
    g_free(karton_path);

    char *labwc_path = labwc_rcxml_path();
    if (g_file_test(labwc_path, G_FILE_TEST_EXISTS)) {
        ok = write_managed_xml_block(labwc_path,
                                     RCXML_INPUT_BEGIN_MARKER,
                                     RCXML_INPUT_END_MARKER,
                                     block->str) && ok;
    }
    g_free(labwc_path);

    g_string_free(block, TRUE);
    return ok;
}

static void set_shortcut_value(guint idx, const char *value)
{
    if (idx >= G_N_ELEMENTS(g_shortcut_bindings)) {
        return;
    }

    g_free(g_shortcut_values[idx]);

    char *copy = g_strdup(value ? value : "");
    g_strstrip(copy);
    g_shortcut_values[idx] = copy;
}

static const char *shortcut_default_for_profile(guint idx, const char *profile)
{
    if (idx >= G_N_ELEMENTS(g_shortcut_bindings)) {
        return "";
    }

    if (g_strcmp0(profile, "developer") == 0) {
        return g_shortcut_bindings[idx].default_developer;
    }

    if (g_strcmp0(profile, "compact") == 0) {
        return g_shortcut_bindings[idx].default_compact;
    }

    return g_shortcut_bindings[idx].default_default;
}

static void set_shortcut_defaults_for_profile(const char *profile)
{
    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        set_shortcut_value(i, shortcut_default_for_profile(i, profile));
    }
}

static void append_keybind_action(GString *xml,
                                  const char *key,
                                  const char *action,
                                  const char *arg_name,
                                  const char *arg_value)
{
    char *escaped_key = g_markup_escape_text(key ? key : "", -1);
    char *escaped_action = g_markup_escape_text(action ? action : "", -1);

    if (!arg_name || !arg_value) {
        g_string_append_printf(xml,
                               "    <keybind key=\"%s\">\n"
                               "      <action name=\"%s\" />\n"
                               "    </keybind>\n",
                               escaped_key ? escaped_key : "",
                               escaped_action ? escaped_action : "");
    } else {
        char *escaped_arg_name = g_markup_escape_text(arg_name, -1);
        char *escaped_arg_value = g_markup_escape_text(arg_value, -1);
        g_string_append_printf(xml,
                               "    <keybind key=\"%s\">\n"
                               "      <action name=\"%s\" %s=\"%s\" />\n"
                               "    </keybind>\n",
                               escaped_key ? escaped_key : "",
                               escaped_action ? escaped_action : "",
                               escaped_arg_name ? escaped_arg_name : "",
                               escaped_arg_value ? escaped_arg_value : "");
        g_free(escaped_arg_name);
        g_free(escaped_arg_value);
    }

    g_free(escaped_key);
    g_free(escaped_action);
}

static void append_managed_shortcut_resets(GString *block)
{
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        const char *candidates[] = {
            g_shortcut_bindings[i].default_default,
            g_shortcut_bindings[i].default_developer,
            g_shortcut_bindings[i].default_compact,
            NULL,
        };

        for (guint j = 0; candidates[j] != NULL; j++) {
            const char *key = candidates[j];
            if (!key || !*key) {
                continue;
            }

            if (!g_hash_table_contains(seen, key)) {
                append_keybind_none(block, key);
                g_hash_table_add(seen, g_strdup(key));
            }
        }
    }

    g_hash_table_destroy(seen);
}

static void on_shortcut_profile_selected_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    const char *profile = dropdown_selected_value(g_shortcuts_dropdown,
                                                  g_shortcut_options,
                                                  G_N_ELEMENTS(g_shortcut_options));
    set_shortcut_defaults_for_profile(profile);
}

struct shortcut_dialog_ctx {
    GtkWidget *window;
    GtkWidget *entries[G_N_ELEMENTS(g_shortcut_bindings)];
};

static void on_shortcuts_dialog_cancel(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    struct shortcut_dialog_ctx *ctx = user_data;
    gtk_window_destroy(GTK_WINDOW(ctx->window));
}

static const char *shortcut_token_from_keyval(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
        return "Return";
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab:
        return "Tab";
    case GDK_KEY_space:
        return "Space";
    default:
        break;
    }

    const char *name = gdk_keyval_name(keyval);
    if (!name || !*name) {
        return NULL;
    }

    if (!g_strcmp0(name, "Shift_L") || !g_strcmp0(name, "Shift_R")
            || !g_strcmp0(name, "Control_L") || !g_strcmp0(name, "Control_R")
            || !g_strcmp0(name, "Alt_L") || !g_strcmp0(name, "Alt_R")
            || !g_strcmp0(name, "Super_L") || !g_strcmp0(name, "Super_R")
            || !g_strcmp0(name, "Meta_L") || !g_strcmp0(name, "Meta_R")
            || !g_strcmp0(name, "ISO_Level3_Shift")) {
        return NULL;
    }

    return name;
}

static gboolean on_shortcut_entry_key_pressed(GtkEventControllerKey *controller,
                                              guint keyval,
                                              guint keycode,
                                              GdkModifierType state,
                                              gpointer user_data)
{
    (void)controller;
    (void)keycode;

    GtkWidget *entry = GTK_WIDGET(user_data);
    if (!GTK_IS_EDITABLE(entry)) {
        return FALSE;
    }

    state &= gtk_accelerator_get_default_mod_mask();

    if ((keyval == GDK_KEY_BackSpace || keyval == GDK_KEY_Delete) && state == 0) {
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
        return TRUE;
    }

    const char *token = shortcut_token_from_keyval(keyval);
    if (!token) {
        return TRUE;
    }

    GString *binding = g_string_new(NULL);
    if (state & (GDK_SUPER_MASK | GDK_META_MASK)) {
        g_string_append(binding, "W-");
    }
    if (state & GDK_CONTROL_MASK) {
        g_string_append(binding, "C-");
    }
    if (state & GDK_ALT_MASK) {
        g_string_append(binding, "A-");
    }
    if (state & GDK_SHIFT_MASK) {
        g_string_append(binding, "S-");
    }
    g_string_append(binding, token);

    gtk_editable_set_text(GTK_EDITABLE(entry), binding->str);
    g_string_free(binding, TRUE);
    return TRUE;
}

static void on_shortcuts_dialog_save(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    struct shortcut_dialog_ctx *ctx = user_data;

    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(ctx->entries[i]));
        set_shortcut_value(i, text);
    }

    save_input_config();

    if (!apply_keyboard_environment()) {
        status_set(_("Could not update session keyboard environment file."), TRUE);
        gtk_window_destroy(GTK_WINDOW(ctx->window));
        return;
    }

    if (!apply_compositor_input_config()) {
        status_set(_("Could not update compositor input configuration."), TRUE);
        gtk_window_destroy(GTK_WINDOW(ctx->window));
        return;
    }

    char *issues = apply_runtime_input();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        gtk_window_destroy(GTK_WINDOW(ctx->window));
        return;
    }

    status_set(_("Shortcuts saved and applied globally"), FALSE);
    gtk_window_destroy(GTK_WINDOW(ctx->window));
}

static GtkWidget *create_shortcut_editor_row(const char *title, const char *value, GtkWidget **entry_out)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), value ? value : "");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Press a key combination"));
    gtk_widget_set_size_request(entry, 180, -1);
    gtk_widget_set_hexpand(entry, FALSE);
    gtk_widget_set_halign(entry, GTK_ALIGN_END);

    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller,
                     "key-pressed",
                     G_CALLBACK(on_shortcut_entry_key_pressed),
                     entry);
    gtk_widget_add_controller(entry, key_controller);

    gtk_box_append(GTK_BOX(row), entry);

    if (entry_out) {
        *entry_out = entry;
    }

    return row;
}

static void apply_theme_classes_from_root(GtkWidget *dialog, GtkWidget *root)
{
    if (!dialog) {
        return;
    }

    gtk_widget_add_css_class(dialog, "settings-window");

    if (root && gtk_widget_has_css_class(root, "theme-dark")) {
        gtk_widget_add_css_class(dialog, "theme-dark");
        gtk_widget_remove_css_class(dialog, "theme-light");
    } else {
        gtk_widget_add_css_class(dialog, "theme-light");
        gtk_widget_remove_css_class(dialog, "theme-dark");
    }
}

static void on_configure_shortcuts_clicked(GtkButton *btn, gpointer data)
{
    (void)data;

    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(btn)));

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Configure keyboard shortcuts"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 430);
    apply_theme_classes_from_root(dialog, root);
    if (root && GTK_IS_WINDOW(root)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root));
    }

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(content, "input-shortcuts-dialog");
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 16);
    gtk_window_set_child(GTK_WINDOW(dialog), content);

    GtkWidget *title = gtk_label_new(_("Edit shortcut bindings used by the compositor."));
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(title), TRUE);
    gtk_box_append(GTK_BOX(content), title);

    GtkWidget *preview = gtk_frame_new(NULL);
    gtk_widget_add_css_class(preview, "input-shortcuts-preview");
    GtkWidget *preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(preview_box, 12);
    gtk_widget_set_margin_end(preview_box, 12);
    gtk_widget_set_margin_top(preview_box, 12);
    gtk_widget_set_margin_bottom(preview_box, 12);
    gtk_frame_set_child(GTK_FRAME(preview), preview_box);



    GtkWidget *mock_window = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(mock_window, "input-shortcuts-preview-window");



    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *left = gtk_label_new(_("Shortcut"));
    gtk_widget_set_hexpand(left, TRUE);
    gtk_widget_set_halign(left, GTK_ALIGN_START);
    gtk_widget_add_css_class(left, "heading");
    gtk_box_append(GTK_BOX(header), left);

    GtkWidget *right = gtk_label_new(_("Key combination"));
    gtk_widget_set_halign(right, GTK_ALIGN_END);
    gtk_widget_add_css_class(right, "heading");
    gtk_box_append(GTK_BOX(header), right);
    gtk_box_append(GTK_BOX(content), header);

    GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(content), list);

    struct shortcut_dialog_ctx *ctx = g_new0(struct shortcut_dialog_ctx, 1);
    ctx->window = dialog;

    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        GtkWidget *row = create_shortcut_editor_row(_(g_shortcut_bindings[i].label),
                                                     g_shortcut_values[i],
                                                     &ctx->entries[i]);
        gtk_box_append(GTK_BOX(list), row);
    }

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    gtk_widget_set_margin_top(actions, 6);
    gtk_box_append(GTK_BOX(content), actions);

    GtkWidget *cancel_btn = gtk_button_new_with_label(_("Cancel"));
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_shortcuts_dialog_cancel), ctx);
    gtk_box_append(GTK_BOX(actions), cancel_btn);

    GtkWidget *save_btn = gtk_button_new_with_label(_("Save shortcuts"));
    gtk_widget_add_css_class(save_btn, "suggested-action");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_shortcuts_dialog_save), ctx);
    gtk_box_append(GTK_BOX(actions), save_btn);

    g_signal_connect_swapped(dialog, "destroy", G_CALLBACK(g_free), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
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

static gboolean apply_keyboard_environment(void)
{
    const char *layout = selected_layout_value();

    if (!layout || !*layout) {
        layout = "us";
    }

    char *escaped = g_strescape(layout, NULL);
    GString *block = g_string_new(NULL);
    g_string_append_printf(block, "XKB_DEFAULT_LAYOUT=%s", escaped ? escaped : "us");

    char *path = session_environment_path();
    gboolean ok = write_managed_env_block(path,
                                          "# BEGIN KartON managed keyboard env",
                                          "# END KartON managed keyboard env",
                                          block->str);

    g_free(path);
    g_free(escaped);
    g_string_free(block, TRUE);
    return ok;
}

static void save_input_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_integer(kf, "input", "layout_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_layout_dropdown)));
    g_key_file_set_string(kf, "input", "layout_value", selected_layout_value());
    g_key_file_set_integer(kf, "input", "shortcut_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_shortcuts_dropdown)));
    g_key_file_set_integer(kf, "input", "cursor_speed", (int)gtk_range_get_value(GTK_RANGE(g_cursor_speed_scale)));

    g_key_file_set_boolean(kf, "input", "touchpad", gtk_switch_get_active(GTK_SWITCH(g_touchpad_switch)));
    g_key_file_set_boolean(kf, "input", "gestures", gtk_switch_get_active(GTK_SWITCH(g_gestures_switch)));
    g_key_file_set_boolean(kf, "input", "natural_scroll", gtk_switch_get_active(GTK_SWITCH(g_natural_scroll_switch)));
    g_key_file_set_boolean(kf, "input", "multimedia", gtk_switch_get_active(GTK_SWITCH(g_multimedia_switch)));
    g_key_file_set_boolean(kf, "input", "tablet", gtk_switch_get_active(GTK_SWITCH(g_tablet_switch)));
    g_key_file_set_integer(kf, "input", "tablet_mode_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_tablet_mode_dropdown)));
    g_key_file_set_boolean(kf, "input", "controller", gtk_switch_get_active(GTK_SWITCH(g_controller_switch)));

    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        g_key_file_set_string(kf,
                              "shortcuts",
                              g_shortcut_bindings[i].id,
                              g_shortcut_values[i] ? g_shortcut_values[i] : "");
    }

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = input_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_input_config(void)
{
    char *path = input_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    int layout_idx = g_key_file_get_integer(kf, "input", "layout_idx", &error);
    if (error) {
        g_clear_error(&error);
        layout_idx = (int)default_layout_index_from_system();
    }

    char *layout_value = g_key_file_get_string(kf, "input", "layout_value", &error);
    if (error) {
        g_clear_error(&error);
        g_free(layout_value);
        layout_value = NULL;
    }

    if (layout_value && *layout_value) {
        layout_idx = (int)find_layout_value_index(layout_value);
    }

    int shortcut_idx = g_key_file_get_integer(kf, "input", "shortcut_idx", &error);
    if (error) {
        g_clear_error(&error);
        shortcut_idx = 0;
    }

    shortcut_idx = clamp_int(shortcut_idx, 0, (int)G_N_ELEMENTS(g_shortcut_options) - 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_shortcuts_dropdown), (guint)shortcut_idx);

    const char *profile = dropdown_selected_value(g_shortcuts_dropdown,
                                                  g_shortcut_options,
                                                  G_N_ELEMENTS(g_shortcut_options));
    set_shortcut_defaults_for_profile(profile);

    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_bindings); i++) {
        char *binding = g_key_file_get_string(kf, "shortcuts", g_shortcut_bindings[i].id, &error);
        if (error) {
            g_clear_error(&error);
            continue;
        }

        set_shortcut_value(i, binding);
        g_free(binding);
    }

    int cursor_speed = g_key_file_get_integer(kf, "input", "cursor_speed", &error);
    if (error) {
        g_clear_error(&error);
        cursor_speed = 50;
    }

    gboolean touchpad = g_key_file_get_boolean(kf, "input", "touchpad", &error);
    if (error) {
        g_clear_error(&error);
        touchpad = TRUE;
    }

    gboolean gestures = g_key_file_get_boolean(kf, "input", "gestures", &error);
    if (error) {
        g_clear_error(&error);
        gestures = TRUE;
    }

    gboolean natural_scroll = g_key_file_get_boolean(kf, "input", "natural_scroll", &error);
    if (error) {
        g_clear_error(&error);
        natural_scroll = FALSE;
    }

    gboolean multimedia = g_key_file_get_boolean(kf, "input", "multimedia", &error);
    if (error) {
        g_clear_error(&error);
        multimedia = TRUE;
    }

    gboolean tablet = g_key_file_get_boolean(kf, "input", "tablet", &error);
    if (error) {
        g_clear_error(&error);
        tablet = TRUE;
    }

    int tablet_mode_idx = g_key_file_get_integer(kf, "input", "tablet_mode_idx", &error);
    if (error) {
        g_clear_error(&error);
        tablet_mode_idx = 0;
    }

    gboolean controller = g_key_file_get_boolean(kf, "input", "controller", &error);
    if (error) {
        g_clear_error(&error);
        controller = TRUE;
    }

    int layout_max = (g_layout_values && g_layout_values->len > 0)
        ? (int)g_layout_values->len - 1
        : 0;
    layout_idx = clamp_int(layout_idx, 0, layout_max);
    cursor_speed = clamp_int(cursor_speed, 0, 100);
    tablet_mode_idx = clamp_int(tablet_mode_idx, 0, (int)G_N_ELEMENTS(g_tablet_mode_options) - 1);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_layout_dropdown), (guint)layout_idx);
    gtk_range_set_value(GTK_RANGE(g_cursor_speed_scale), cursor_speed);

    gtk_switch_set_active(GTK_SWITCH(g_touchpad_switch), touchpad);
    gtk_switch_set_active(GTK_SWITCH(g_gestures_switch), gestures);
    gtk_switch_set_active(GTK_SWITCH(g_natural_scroll_switch), natural_scroll);
    gtk_switch_set_active(GTK_SWITCH(g_multimedia_switch), multimedia);
    gtk_switch_set_active(GTK_SWITCH(g_tablet_switch), tablet);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_tablet_mode_dropdown), (guint)tablet_mode_idx);
    gtk_switch_set_active(GTK_SWITCH(g_controller_switch), controller);

    g_free(layout_value);
    g_key_file_unref(kf);
    g_free(path);
}

static char *apply_runtime_input(void)
{
    gboolean reconfigured = FALSE;

    if (command_is_available("karton")) {
        reconfigured = run_command_success("sh -lc 'timeout 2s karton --reconfigure >/dev/null 2>&1'");
    }

    if (!reconfigured && command_is_available("tektura")) {
        reconfigured = run_command_success("sh -lc 'timeout 2s tektura --reconfigure >/dev/null 2>&1'");
    }

    if (!reconfigured && command_is_available("labwc")) {
        reconfigured = run_command_success("sh -lc 'timeout 2s labwc --reconfigure >/dev/null 2>&1'");
    }

    if (!reconfigured) {
        reconfigured = run_command_success("sh -lc 'pkill -HUP -x karton >/dev/null 2>&1 || pkill -HUP -x tektura >/dev/null 2>&1'");
    }

    if (!reconfigured) {
        return g_strdup(_("Settings were saved. Some changes may require logging out and back in."));
    }

    return NULL;
}

static void on_reload_input_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_input_config();
    refresh_detected_input_devices();
    status_set(_("Input settings reloaded"), FALSE);
}

static void on_apply_input_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_input_config();

    if (!apply_keyboard_environment()) {
        status_set(_("Could not update session keyboard environment file."), TRUE);
        return;
    }

    if (!apply_compositor_input_config()) {
        status_set(_("Could not update compositor input configuration."), TRUE);
        return;
    }

    char *issues = apply_runtime_input();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Input device settings applied"), FALSE);
}

GtkWidget *page_input_new(void)
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

    GtkWidget *title = gtk_label_new(_("Input devices"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Configure mouse, keyboard, touchpad, gestures and other input devices."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *keyboard_frame = create_section(_("Keyboard and shortcuts"),
                                               _("Set keyboard layout and shortcut profile for everyday workflows."));
    GtkWidget *keyboard_box = gtk_frame_get_child(GTK_FRAME(keyboard_frame));

    rebuild_layout_model_from_system();
    g_layout_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_layout_model), NULL);
    g_object_unref(g_layout_model);

    GtkStringList *shortcuts_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_shortcut_options); i++) {
        gtk_string_list_append(shortcuts_model, _(g_shortcut_options[i].label));
    }
    g_shortcuts_dropdown = gtk_drop_down_new(G_LIST_MODEL(shortcuts_model), NULL);
    g_object_unref(shortcuts_model);

    g_signal_connect(g_shortcuts_dropdown,
                     "notify::selected",
                     G_CALLBACK(on_shortcut_profile_selected_changed),
                     NULL);

    g_shortcuts_configure_button = gtk_button_new_with_label(_("Configure"));
    g_signal_connect(g_shortcuts_configure_button,
                     "clicked",
                     G_CALLBACK(on_configure_shortcuts_clicked),
                     NULL);

    GtkWidget *shortcuts_control = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(shortcuts_control), g_shortcuts_dropdown);
    gtk_box_append(GTK_BOX(shortcuts_control), g_shortcuts_configure_button);

    gtk_box_append(GTK_BOX(keyboard_box), create_row(_("Keyboard layout"), g_layout_dropdown));
    gtk_box_append(GTK_BOX(keyboard_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(keyboard_box), create_row(_("Keyboard shortcuts"), shortcuts_control));

    gtk_box_append(GTK_BOX(box), keyboard_frame);

    GtkWidget *pointer_frame = create_section(_("Pointer and touchpad"),
                                              _("Tune cursor speed and touchpad behavior, including gestures and natural scrolling."));
    GtkWidget *pointer_box = gtk_frame_get_child(GTK_FRAME(pointer_frame));

    g_cursor_speed_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_cursor_speed_scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g_cursor_speed_scale), 0);

    g_touchpad_switch = gtk_switch_new();
    g_gestures_switch = gtk_switch_new();
    g_natural_scroll_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(pointer_box), create_slider_row(_("Cursor speed"), _("0 = precise, 100 = fast"), g_cursor_speed_scale));
    gtk_box_append(GTK_BOX(pointer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(pointer_box), create_row(_("Touchpad"), g_touchpad_switch));
    gtk_box_append(GTK_BOX(pointer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(pointer_box), create_row(_("Gestures"), g_gestures_switch));
    gtk_box_append(GTK_BOX(pointer_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(pointer_box), create_row(_("Natural scrolling"), g_natural_scroll_switch));

    gtk_box_append(GTK_BOX(box), pointer_frame);

    GtkWidget *devices_frame = create_section(_("Additional input devices"),
                                              _("Configure multimedia keys, graphics tablets and controllers."));
    GtkWidget *devices_box = gtk_frame_get_child(GTK_FRAME(devices_frame));

    GtkStringList *tablet_mode_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_tablet_mode_options); i++) {
        gtk_string_list_append(tablet_mode_model, _(g_tablet_mode_options[i].label));
    }
    g_tablet_mode_dropdown = gtk_drop_down_new(G_LIST_MODEL(tablet_mode_model), NULL);
    g_object_unref(tablet_mode_model);

    g_multimedia_switch = gtk_switch_new();
    g_tablet_switch = gtk_switch_new();
    g_controller_switch = gtk_switch_new();
    g_detected_devices_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *detected_header = gtk_label_new(_("Detected devices"));
    gtk_widget_set_halign(detected_header, GTK_ALIGN_START);
    gtk_widget_add_css_class(detected_header, "card-subtitle");

    GtkWidget *refresh_devices_btn = gtk_button_new_with_label(_("Refresh device list"));
    g_signal_connect(refresh_devices_btn,
                     "clicked",
                     G_CALLBACK(on_refresh_detected_devices_clicked),
                     NULL);

    GtkWidget *detected_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(detected_actions, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(detected_actions), refresh_devices_btn);

    gtk_box_append(GTK_BOX(devices_box), create_row(_("Multimedia keys"), g_multimedia_switch));
    gtk_box_append(GTK_BOX(devices_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(devices_box), create_row(_("Graphics tablets"), g_tablet_switch));
    gtk_box_append(GTK_BOX(devices_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(devices_box), create_row(_("Tablet mode"), g_tablet_mode_dropdown));
    gtk_box_append(GTK_BOX(devices_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(devices_box), create_row(_("Controllers"), g_controller_switch));
    gtk_box_append(GTK_BOX(devices_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(devices_box), detected_header);
    gtk_box_append(GTK_BOX(devices_box), g_detected_devices_box);
    gtk_box_append(GTK_BOX(devices_box), detected_actions);

    gtk_box_append(GTK_BOX(box), devices_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_input_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply input settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_input_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_layout_dropdown), default_layout_index_from_system());
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_shortcuts_dropdown), 0);
    set_shortcut_defaults_for_profile("default");
    gtk_range_set_value(GTK_RANGE(g_cursor_speed_scale), 50);
    gtk_switch_set_active(GTK_SWITCH(g_touchpad_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_gestures_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_natural_scroll_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_multimedia_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_tablet_switch), TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_tablet_mode_dropdown), 0);
    gtk_switch_set_active(GTK_SWITCH(g_controller_switch), TRUE);

    load_input_config();
    refresh_detected_input_devices();

    return outer_scroll;
}
