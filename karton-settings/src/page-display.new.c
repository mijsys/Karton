#include "page-display.h"

#include <gio/gio.h>
#include <glib.h>
#include <libintl.h>
#include <math.h>
#include <stdio.h>

#define _(s) gettext(s)

struct monitor_mode {
    int width;
    int height;
    double refresh_hz;
    gboolean preferred;
    gboolean current;
};

struct monitor_info {
    char *name;
    gboolean enabled;
    int width;
    int height;
    double refresh_hz;
    int pos_x;
    int pos_y;
    double scale;
    gboolean adaptive_sync;
    char *transform;
    GPtrArray *modes;
};

struct orientation_option {
    const char *label;
    const char *arg;
};

static const double g_scale_values[] = { 1.00, 1.25, 1.50, 1.75, 2.00 };

static const struct orientation_option g_orientation_options[] = {
    { "Landscape", "normal" },
    { "Portrait left", "90" },
    { "Portrait right", "270" },
    { "Inverted", "180" },
};

static GtkWidget *g_preview_area = NULL;
static GtkWidget *g_monitor_dropdown = NULL;
static GtkWidget *g_mode_dropdown = NULL;
static GtkWidget *g_scale_dropdown = NULL;
static GtkWidget *g_orientation_dropdown = NULL;
static GtkWidget *g_brightness_scale = NULL;
static GtkWidget *g_night_light_switch = NULL;
static GtkWidget *g_vrr_switch = NULL;
static GtkWidget *g_status_label = NULL;
static GtkWidget *g_monitor_info_label = NULL;

static GtkStringList *g_monitor_model = NULL;
static GtkStringList *g_mode_model = NULL;

static GPtrArray *g_monitors = NULL;
static GPtrArray *g_mode_refs = NULL;

static gboolean g_block_handlers = FALSE;

static char *display_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "display.conf", NULL);
}

static void free_monitor_mode(gpointer data)
{
    struct monitor_mode *mode = data;
    g_free(mode);
}

static void free_monitor_info(gpointer data)
{
    struct monitor_info *monitor = data;
    if (!monitor) {
        return;
    }

    g_free(monitor->name);
    g_free(monitor->transform);
    if (monitor->modes) {
        g_ptr_array_unref(monitor->modes);
    }
    g_free(monitor);
}

static void clear_string_list(GtkStringList *list)
{
    if (!list) {
        return;
    }

    while (g_list_model_get_n_items(G_LIST_MODEL(list)) > 0) {
        gtk_string_list_remove(list, g_list_model_get_n_items(G_LIST_MODEL(list)) - 1);
    }
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

static struct monitor_mode *parse_mode_line(const char *line)
{
    char *trimmed = g_strdup(line);
    g_strstrip(trimmed);

    int w = 0;
    int h = 0;
    double hz = 0.0;

    gboolean parsed = (sscanf(trimmed, "%dx%d px, %lf Hz", &w, &h, &hz) == 3)
        || (sscanf(trimmed, "%dx%d@%lfHz", &w, &h, &hz) == 3)
        || (sscanf(trimmed, "%dx%d @ %lf Hz", &w, &h, &hz) == 3);

    if (!parsed || w <= 0 || h <= 0 || hz <= 0.0) {
        g_free(trimmed);
        return NULL;
    }

    struct monitor_mode *mode = g_new0(struct monitor_mode, 1);
    mode->width = w;
    mode->height = h;
    mode->refresh_hz = hz;
    mode->preferred = strstr(trimmed, "preferred") != NULL;
    mode->current = strstr(trimmed, "current") != NULL;

    g_free(trimmed);
    return mode;
}

static gboolean parse_current_mode_value(const char *line, int *w, int *h, double *hz)
{
    int mw = 0;
    int mh = 0;
    double mhz = 0.0;

    gboolean ok = (sscanf(line, " Current mode: %dx%d @ %lf Hz", &mw, &mh, &mhz) == 3)
        || (sscanf(line, " Current mode: %dx%d px, %lf Hz", &mw, &mh, &mhz) == 3)
        || (sscanf(line, "Current mode: %dx%d @ %lf Hz", &mw, &mh, &mhz) == 3)
        || (sscanf(line, "Current mode: %dx%d px, %lf Hz", &mw, &mh, &mhz) == 3);

    if (!ok || mw <= 0 || mh <= 0 || mhz <= 0.0) {
        return FALSE;
    }

    *w = mw;
    *h = mh;
    *hz = mhz;
    return TRUE;
}

static gboolean is_output_header_line(const char *line, char **name_out)
{
    if (!line || !*line || g_ascii_isspace(line[0])) {
        return FALSE;
    }

    const char *space = strchr(line, ' ');
    gsize len = space ? (gsize)(space - line) : strlen(line);
    if (len == 0) {
        return FALSE;
    }

    char *name = g_strndup(line, len);
    gboolean looks_like_output = (strchr(name, '-') != NULL) || g_str_has_prefix(name, "WL-");
    if (!looks_like_output) {
        g_free(name);
        return FALSE;
    }

    *name_out = name;
    return TRUE;
}

static void clear_monitor_data(void)
{
    if (g_monitors) {
        g_ptr_array_set_size(g_monitors, 0);
    }
    if (g_mode_refs) {
        g_ptr_array_set_size(g_mode_refs, 0);
    }
}

static gboolean query_monitors_from_wlr_randr(char **err_out)
{
    int wait_status = 0;
    char *stdout_data = NULL;
    char *stderr_data = NULL;

    run_command_capture("sh -lc 'command -v wlr-randr >/dev/null 2>&1 && wlr-randr 2>/dev/null || true'",
                        &stdout_data,
                        &stderr_data,
                        &wait_status);
    (void)wait_status;

    if (!stdout_data || !*stdout_data) {
        if (err_out) {
            *err_out = g_strdup(_("wlr-randr is not available or returned no outputs"));
        }
        g_free(stdout_data);
        g_free(stderr_data);
        return FALSE;
    }

    clear_monitor_data();

    struct monitor_info *current = NULL;
    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];
        if (!line || !*line) {
            continue;
        }

        char *header_name = NULL;
        if (is_output_header_line(line, &header_name)) {
            current = g_new0(struct monitor_info, 1);
            current->name = header_name;
            current->enabled = TRUE;
            current->scale = 1.0;
            current->transform = g_strdup("normal");
            current->modes = g_ptr_array_new_with_free_func(free_monitor_mode);
            g_ptr_array_add(g_monitors, current);
            continue;
        }

        if (!current) {
            continue;
        }

        if (strstr(line, "Enabled:")) {
            current->enabled = strstr(line, "yes") != NULL || strstr(line, "true") != NULL;
            continue;
        }

        int mw = 0;
        int mh = 0;
        double mhz = 0.0;
        if (parse_current_mode_value(line, &mw, &mh, &mhz)) {
            current->width = mw;
            current->height = mh;
            current->refresh_hz = mhz;
            continue;
        }

        if (strstr(line, "Position:")) {
            int x = 0;
            int y = 0;
            if (sscanf(line, " Position: %d,%d", &x, &y) == 2 || sscanf(line, "Position: %d,%d", &x, &y) == 2) {
                current->pos_x = x;
                current->pos_y = y;
            }
            continue;
        }

        if (strstr(line, "Scale:")) {
            double scale = 1.0;
            if (sscanf(line, " Scale: %lf", &scale) == 1 || sscanf(line, "Scale: %lf", &scale) == 1) {
                if (scale > 0.0) {
                    current->scale = scale;
                }
            }
            continue;
        }

        if (strstr(line, "Transform:")) {
            char value[64] = { 0 };
            if (sscanf(line, " Transform: %63s", value) == 1 || sscanf(line, "Transform: %63s", value) == 1) {
                g_free(current->transform);
                current->transform = g_strdup(value);
            }
            continue;
        }

        if (strstr(line, "Adaptive Sync:")) {
            current->adaptive_sync = strstr(line, "enabled") != NULL || strstr(line, "on") != NULL;
            continue;
        }

        struct monitor_mode *mode = parse_mode_line(line);
        if (mode) {
            if (mode->current) {
                current->width = mode->width;
                current->height = mode->height;
                current->refresh_hz = mode->refresh_hz;
            }
            g_ptr_array_add(current->modes, mode);
        }
    }

    g_strfreev(lines);
    g_free(stderr_data);

    if (g_monitors->len == 0) {
        if (err_out) {
            *err_out = g_strdup(_("No monitors were detected"));
        }
        g_free(stdout_data);
        return FALSE;
    }

    g_free(stdout_data);
    return TRUE;
}

static struct monitor_info *selected_monitor(void)
{
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_monitor_dropdown));
    if (!g_monitors || idx >= g_monitors->len) {
        return NULL;
    }
    return g_ptr_array_index(g_monitors, idx);
}

static struct monitor_mode *selected_mode(void)
{
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_mode_dropdown));
    if (!g_mode_refs || idx >= g_mode_refs->len) {
        return NULL;
    }
    return g_ptr_array_index(g_mode_refs, idx);
}

static guint nearest_scale_index(double value)
{
    guint best = 0;
    double best_delta = fabs(value - g_scale_values[0]);
    for (guint i = 1; i < G_N_ELEMENTS(g_scale_values); i++) {
        double delta = fabs(value - g_scale_values[i]);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static guint orientation_index_from_arg(const char *arg)
{
    if (!arg || !*arg) {
        return 0;
    }

    for (guint i = 0; i < G_N_ELEMENTS(g_orientation_options); i++) {
        if (g_ascii_strcasecmp(arg, g_orientation_options[i].arg) == 0) {
            return i;
        }
    }

    return 0;
}

static void queue_preview_redraw(void)
{
    if (g_preview_area) {
        gtk_widget_queue_draw(g_preview_area);
    }
}

static void update_monitor_info_label(void)
{
    if (!g_monitor_info_label) {
        return;
    }

    struct monitor_info *monitor = selected_monitor();
    if (!monitor) {
        gtk_label_set_text(GTK_LABEL(g_monitor_info_label), _("No monitor selected"));
        return;
    }

    char text[256] = { 0 };
    snprintf(text,
             sizeof(text),
             "%s: %dx%d @ %.2f Hz, %s %.2fx, %s (%d,%d)",
             monitor->name,
             monitor->width,
             monitor->height,
             monitor->refresh_hz,
             _("scale"),
             monitor->scale,
             _("position"),
             monitor->pos_x,
             monitor->pos_y);
    gtk_label_set_text(GTK_LABEL(g_monitor_info_label), text);
}

static void rebuild_mode_dropdown_for_selected(void)
{
    clear_string_list(g_mode_model);
    g_ptr_array_set_size(g_mode_refs, 0);

    struct monitor_info *monitor = selected_monitor();
    if (!monitor || monitor->modes->len == 0) {
        gtk_string_list_append(g_mode_model, _("No modes available"));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mode_dropdown), 0);
        return;
    }

    guint selected_idx = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; i < monitor->modes->len; i++) {
        struct monitor_mode *mode = g_ptr_array_index(monitor->modes, i);
        char line[160] = { 0 };
        if (mode->preferred && mode->current) {
            snprintf(line, sizeof(line), "%dx%d @ %.2f Hz (%s, %s)", mode->width, mode->height, mode->refresh_hz, _("preferred"), _("current"));
        } else if (mode->preferred) {
            snprintf(line, sizeof(line), "%dx%d @ %.2f Hz (%s)", mode->width, mode->height, mode->refresh_hz, _("preferred"));
        } else if (mode->current) {
            snprintf(line, sizeof(line), "%dx%d @ %.2f Hz (%s)", mode->width, mode->height, mode->refresh_hz, _("current"));
        } else {
            snprintf(line, sizeof(line), "%dx%d @ %.2f Hz", mode->width, mode->height, mode->refresh_hz);
        }

        gtk_string_list_append(g_mode_model, line);
        g_ptr_array_add(g_mode_refs, mode);
        if (mode->current) {
            selected_idx = i;
        }
    }

    if (selected_idx == GTK_INVALID_LIST_POSITION) {
        selected_idx = 0;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mode_dropdown), selected_idx);
}

static void on_monitor_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    g_block_handlers = TRUE;
    rebuild_mode_dropdown_for_selected();

    struct monitor_info *monitor = selected_monitor();
    if (monitor) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), nearest_scale_index(monitor->scale));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), orientation_index_from_arg(monitor->transform));
        gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), monitor->adaptive_sync);
    }

    g_block_handlers = FALSE;
    update_monitor_info_label();
    queue_preview_redraw();
}

static void draw_monitor_preview(GtkDrawingArea *area,
                                 cairo_t *cairo,
                                 int width,
                                 int height,
                                 gpointer user_data)
{
    (void)area;
    (void)user_data;

    cairo_set_source_rgba(cairo, 0.08, 0.12, 0.18, 0.14);
    cairo_rectangle(cairo, 0, 0, width, height);
    cairo_fill(cairo);

    if (!g_monitors || g_monitors->len == 0) {
        cairo_set_source_rgba(cairo, 0.35, 0.45, 0.60, 0.90);
        cairo_select_font_face(cairo, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cairo, 14.0);
        cairo_move_to(cairo, 16.0, 28.0);
        cairo_show_text(cairo, _("No monitors detected"));
        return;
    }

    int min_x = G_MAXINT;
    int min_y = G_MAXINT;
    int max_x = G_MININT;
    int max_y = G_MININT;

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *m = g_ptr_array_index(g_monitors, i);
        if (!m->enabled) {
            continue;
        }

        int w = m->width > 0 ? m->width : 800;
        int h = m->height > 0 ? m->height : 600;

        if (m->pos_x < min_x) min_x = m->pos_x;
        if (m->pos_y < min_y) min_y = m->pos_y;
        if (m->pos_x + w > max_x) max_x = m->pos_x + w;
        if (m->pos_y + h > max_y) max_y = m->pos_y + h;
    }

    if (min_x == G_MAXINT || min_y == G_MAXINT) {
        cairo_set_source_rgba(cairo, 0.35, 0.45, 0.60, 0.90);
        cairo_select_font_face(cairo, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cairo, 14.0);
        cairo_move_to(cairo, 16.0, 28.0);
        cairo_show_text(cairo, _("All outputs are disabled"));
        return;
    }

    double bbox_w = (double)(max_x - min_x);
    double bbox_h = (double)(max_y - min_y);
    if (bbox_w < 1.0) bbox_w = 1.0;
    if (bbox_h < 1.0) bbox_h = 1.0;

    double pad = 18.0;
    double sx = (width - pad * 2.0) / bbox_w;
    double sy = (height - pad * 2.0) / bbox_h;
    double factor = sx < sy ? sx : sy;

    if (factor > 0.22) {
        factor = 0.22;
    }
    if (factor < 0.04) {
        factor = 0.04;
    }

    guint selected_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_monitor_dropdown));

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *m = g_ptr_array_index(g_monitors, i);
        if (!m->enabled) {
            continue;
        }

        double w = (double)(m->width > 0 ? m->width : 800) * factor;
        double h = (double)(m->height > 0 ? m->height : 600) * factor;
        double x = pad + ((double)m->pos_x - (double)min_x) * factor;
        double y = pad + ((double)m->pos_y - (double)min_y) * factor;

        gboolean selected = i == selected_idx;

        if (selected) {
            cairo_set_source_rgba(cairo, 0.39, 0.56, 0.93, 0.32);
        } else {
            cairo_set_source_rgba(cairo, 0.17, 0.28, 0.45, 0.24);
        }
        cairo_rectangle(cairo, x, y, w, h);
        cairo_fill(cairo);

        cairo_set_source_rgba(cairo, selected ? 0.45 : 0.30, selected ? 0.65 : 0.42, selected ? 0.97 : 0.55, 0.95);
        cairo_set_line_width(cairo, selected ? 2.4 : 1.4);
        cairo_rectangle(cairo, x, y, w, h);
        cairo_stroke(cairo);

        cairo_set_source_rgba(cairo, 0.95, 0.97, 1.00, 0.95);
        cairo_select_font_face(cairo, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cairo, 11.0);
        cairo_move_to(cairo, x + 8.0, y + 16.0);
        cairo_show_text(cairo, m->name ? m->name : "?");

        char mode_text[64] = { 0 };
        snprintf(mode_text, sizeof(mode_text), "%dx%d @ %.1f", m->width, m->height, m->refresh_hz);
        cairo_select_font_face(cairo, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cairo, 10.0);
        cairo_move_to(cairo, x + 8.0, y + 31.0);
        cairo_show_text(cairo, mode_text);
    }
}

static void apply_runtime_tweaks(void)
{
    int brightness = (int)gtk_range_get_value(GTK_RANGE(g_brightness_scale));
    gboolean night_light = gtk_switch_get_active(GTK_SWITCH(g_night_light_switch));

    if (g_find_program_in_path("brightnessctl")) {
        char *cmd = g_strdup_printf("sh -lc 'brightnessctl set %d%% >/dev/null 2>&1 || true'", brightness);
        g_spawn_command_line_async(cmd, NULL);
        g_free(cmd);
    }

    if (g_find_program_in_path("gsettings")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'gsettings set org.gnome.settings-daemon.plugins.color night-light-enabled %s >/dev/null 2>&1 || true'",
            night_light ? "true" : "false");
        g_spawn_command_line_async(cmd, NULL);
        g_free(cmd);
    }
}

static void save_current_config(const struct monitor_info *monitor, const struct monitor_mode *mode)
{
    GKeyFile *kf = g_key_file_new();

    if (monitor && monitor->name) {
        g_key_file_set_string(kf, "display", "monitor", monitor->name);
    }
    if (mode) {
        g_key_file_set_integer(kf, "display", "mode_width", mode->width);
        g_key_file_set_integer(kf, "display", "mode_height", mode->height);
        g_key_file_set_double(kf, "display", "mode_hz", mode->refresh_hz);
    }

    g_key_file_set_integer(kf, "display", "scale_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_scale_dropdown)));
    g_key_file_set_integer(kf, "display", "orientation_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_orientation_dropdown)));
    g_key_file_set_boolean(kf, "display", "vrr", gtk_switch_get_active(GTK_SWITCH(g_vrr_switch)));
    g_key_file_set_integer(kf, "display", "brightness", (int)gtk_range_get_value(GTK_RANGE(g_brightness_scale)));
    g_key_file_set_boolean(kf, "display", "night_light", gtk_switch_get_active(GTK_SWITCH(g_night_light_switch)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = display_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_saved_config(void)
{
    char *path = display_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;
    char *monitor_name = g_key_file_get_string(kf, "display", "monitor", &error);
    if (error) {
        g_clear_error(&error);
        g_free(monitor_name);
        monitor_name = NULL;
    }

    int mode_w = g_key_file_get_integer(kf, "display", "mode_width", &error);
    if (error) {
        g_clear_error(&error);
        mode_w = 0;
    }

    int mode_h = g_key_file_get_integer(kf, "display", "mode_height", &error);
    if (error) {
        g_clear_error(&error);
        mode_h = 0;
    }

    double mode_hz = g_key_file_get_double(kf, "display", "mode_hz", &error);
    if (error) {
        g_clear_error(&error);
        mode_hz = 0.0;
    }

    int scale_idx = g_key_file_get_integer(kf, "display", "scale_idx", &error);
    if (error) {
        g_clear_error(&error);
        scale_idx = 0;
    }

    int orient_idx = g_key_file_get_integer(kf, "display", "orientation_idx", &error);
    if (error) {
        g_clear_error(&error);
        orient_idx = 0;
    }

    gboolean vrr = g_key_file_get_boolean(kf, "display", "vrr", &error);
    if (error) {
        g_clear_error(&error);
        vrr = FALSE;
    }

    int brightness = g_key_file_get_integer(kf, "display", "brightness", &error);
    if (error) {
        g_clear_error(&error);
        brightness = 70;
    }

    gboolean night_light = g_key_file_get_boolean(kf, "display", "night_light", &error);
    if (error) {
        g_clear_error(&error);
        night_light = FALSE;
    }

    g_block_handlers = TRUE;

    if (monitor_name && g_monitors) {
        for (guint i = 0; i < g_monitors->len; i++) {
            struct monitor_info *m = g_ptr_array_index(g_monitors, i);
            if (g_strcmp0(m->name, monitor_name) == 0) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(g_monitor_dropdown), i);
                break;
            }
        }
    }

    rebuild_mode_dropdown_for_selected();

    for (guint i = 0; g_mode_refs && i < g_mode_refs->len; i++) {
        struct monitor_mode *mode = g_ptr_array_index(g_mode_refs, i);
        if (mode->width == mode_w && mode->height == mode_h && fabs(mode->refresh_hz - mode_hz) < 0.15) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mode_dropdown), i);
            break;
        }
    }

    if (scale_idx < 0 || scale_idx >= (int)G_N_ELEMENTS(g_scale_values)) {
        scale_idx = 0;
    }
    if (orient_idx < 0 || orient_idx >= (int)G_N_ELEMENTS(g_orientation_options)) {
        orient_idx = 0;
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), (guint)scale_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), (guint)orient_idx);
    gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), vrr);
    gtk_range_set_value(GTK_RANGE(g_brightness_scale), brightness < 10 ? 10 : (brightness > 100 ? 100 : brightness));
    gtk_switch_set_active(GTK_SWITCH(g_night_light_switch), night_light);

    g_block_handlers = FALSE;

    g_free(monitor_name);
    g_key_file_unref(kf);
    g_free(path);
}

static void rebuild_monitor_dropdown(void)
{
    clear_string_list(g_monitor_model);

    if (!g_monitors || g_monitors->len == 0) {
        gtk_string_list_append(g_monitor_model, _("No connected monitors"));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_monitor_dropdown), 0);
        return;
    }

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        char line[256] = { 0 };
        snprintf(line,
                 sizeof(line),
                 "%s (%dx%d @ %.2f Hz)%s",
                 monitor->name,
                 monitor->width,
                 monitor->height,
                 monitor->refresh_hz,
                 monitor->enabled ? "" : " [disabled]");
        gtk_string_list_append(g_monitor_model, line);
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_monitor_dropdown), 0);
}

static void refresh_monitor_data(gboolean with_status)
{
    char *error = NULL;
    gboolean ok = query_monitors_from_wlr_randr(&error);

    g_block_handlers = TRUE;
    rebuild_monitor_dropdown();
    rebuild_mode_dropdown_for_selected();

    struct monitor_info *monitor = selected_monitor();
    if (monitor) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), nearest_scale_index(monitor->scale));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), orientation_index_from_arg(monitor->transform));
        gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), monitor->adaptive_sync);
    }

    g_block_handlers = FALSE;

    update_monitor_info_label();
    queue_preview_redraw();

    if (with_status) {
        if (!ok) {
            status_set(error ? error : _("Could not detect monitors"), TRUE);
        } else {
            status_set(_("Monitor list refreshed"), FALSE);
        }
    }

    g_free(error);
}

static void on_refresh_monitors_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_monitor_data(TRUE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();
}

static void on_apply_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    struct monitor_info *monitor = selected_monitor();
    struct monitor_mode *mode = selected_mode();
    if (!monitor || !mode) {
        status_set(_("No monitor mode selected"), TRUE);
        return;
    }

    guint scale_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_scale_dropdown));
    if (scale_idx >= G_N_ELEMENTS(g_scale_values)) {
        scale_idx = 0;
    }

    guint orient_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_orientation_dropdown));
    if (orient_idx >= G_N_ELEMENTS(g_orientation_options)) {
        orient_idx = 0;
    }

    gboolean vrr = gtk_switch_get_active(GTK_SWITCH(g_vrr_switch));

    char *quoted_name = g_shell_quote(monitor->name);
    char *command = g_strdup_printf(
        "sh -lc 'wlr-randr --output %s --mode %dx%d@%.6fHz --scale %.2f --transform %s --adaptive-sync %s'",
        quoted_name,
        mode->width,
        mode->height,
        mode->refresh_hz,
        g_scale_values[scale_idx],
        g_orientation_options[orient_idx].arg,
        vrr ? "enabled" : "disabled");

    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;
    gboolean ok = run_command_capture(command, &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;

    g_free(command);
    g_free(quoted_name);

    apply_runtime_tweaks();

    if (!ok) {
        if (stderr_data && *stderr_data) {
            g_strstrip(stderr_data);
            status_set(stderr_data, TRUE);
        } else {
            status_set(_("Could not apply monitor settings"), TRUE);
        }
        g_free(stdout_data);
        g_free(stderr_data);
        return;
    }

    save_current_config(monitor, mode);
    status_set(_("Display settings applied"), FALSE);

    g_free(stdout_data);
    g_free(stderr_data);

    refresh_monitor_data(FALSE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();
}

GtkWidget *page_display_new(void)
{
    if (!g_monitors) {
        g_monitors = g_ptr_array_new_with_free_func(free_monitor_info);
    }
    if (!g_mode_refs) {
        g_mode_refs = g_ptr_array_new();
    }

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

    GtkWidget *title = gtk_label_new(_("Display and monitors"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Detected outputs are shown graphically with live name, resolution and refresh rate."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *preview_frame = create_section(_("Connected monitors"), _("Preview and detect all currently connected outputs."));
    GtkWidget *preview_box = gtk_frame_get_child(GTK_FRAME(preview_frame));

    g_preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_preview_area, -1, 220);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_preview_area), draw_monitor_preview, NULL, NULL);
    gtk_box_append(GTK_BOX(preview_box), g_preview_area);

    g_monitor_info_label = gtk_label_new("");
    gtk_widget_set_halign(g_monitor_info_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_monitor_info_label, "row-subtitle");
    gtk_box_append(GTK_BOX(preview_box), g_monitor_info_label);

    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh monitor list"));
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_monitors_clicked), NULL);
    gtk_box_append(GTK_BOX(preview_box), refresh_btn);

    gtk_box_append(GTK_BOX(box), preview_frame);

    GtkWidget *screen_frame = create_section(_("Screen configuration"), _("Apply mode, scaling, orientation and adaptive sync to the selected monitor."));
    GtkWidget *screen_box = gtk_frame_get_child(GTK_FRAME(screen_frame));

    g_monitor_model = gtk_string_list_new(NULL);
    g_monitor_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_monitor_model), NULL);
    g_signal_connect(g_monitor_dropdown, "notify::selected", G_CALLBACK(on_monitor_changed), NULL);

    g_mode_model = gtk_string_list_new(NULL);
    g_mode_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_mode_model), NULL);

    GtkStringList *scale_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_scale_values); i++) {
        char entry[16] = { 0 };
        snprintf(entry, sizeof(entry), "%.0f%%", g_scale_values[i] * 100.0);
        gtk_string_list_append(scale_model, entry);
    }
    g_scale_dropdown = gtk_drop_down_new(G_LIST_MODEL(scale_model), NULL);
    g_object_unref(scale_model);

    GtkStringList *orientation_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_orientation_options); i++) {
        gtk_string_list_append(orientation_model, _(g_orientation_options[i].label));
    }
    g_orientation_dropdown = gtk_drop_down_new(G_LIST_MODEL(orientation_model), NULL);
    g_object_unref(orientation_model);

    g_vrr_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(screen_box), create_row(_("Monitor"), g_monitor_dropdown));
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("Resolution and refresh rate"), g_mode_dropdown));
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("DPI scaling"), g_scale_dropdown));
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("Orientation"), g_orientation_dropdown));
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("VRR / Adaptive Sync"), g_vrr_switch));

    g_brightness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_brightness_scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g_brightness_scale), 0);
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_slider_row(_("Brightness"), _("Adjust display brightness (10-100%)"), g_brightness_scale));

    g_night_light_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("Night light"), g_night_light_switch));

    gtk_box_append(GTK_BOX(box), screen_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply display settings"));
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);
    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), 0);
    gtk_range_set_value(GTK_RANGE(g_brightness_scale), 70.0);
    gtk_switch_set_active(GTK_SWITCH(g_night_light_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), FALSE);

    refresh_monitor_data(FALSE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();

    return outer_scroll;
}
