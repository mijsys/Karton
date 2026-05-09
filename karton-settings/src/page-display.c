#include "page-display.h"

#include <gio/gio.h>
#include <glib.h>
#include <libintl.h>
#include <math.h>
#include <stdio.h>

#define _(s) gettext(s)
#define N_(s) s

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
    gboolean primary;
    int width;
    int height;
    double refresh_hz;
    int pos_x;
    int pos_y;
    double scale;
    gboolean adaptive_sync;
    char *transform;
    GPtrArray *modes;

    int staged_width;
    int staged_height;
    double staged_refresh_hz;
    int staged_pos_x;
    int staged_pos_y;
    double staged_scale;
    gboolean staged_adaptive_sync;
    char *staged_transform;
};

struct orientation_option {
    const char *label;
    const char *arg;
};

struct layout_option {
    const char *label;
};

enum monitor_backend {
    MONITOR_BACKEND_NONE = 0,
    MONITOR_BACKEND_WLR_RANDR,
};

static const double g_scale_values[] = { 1.00, 1.25, 1.50, 1.75, 2.00 };

static const struct orientation_option g_orientation_options[] = {
    { N_("Landscape"), "normal" },
    { N_("Portrait left"), "90" },
    { N_("Portrait right"), "270" },
    { N_("Inverted"), "180" },
};

enum monitor_layout {
    MONITOR_LAYOUT_KEEP = 0,
    MONITOR_LAYOUT_RIGHT,
    MONITOR_LAYOUT_LEFT,
    MONITOR_LAYOUT_DOWN,
    MONITOR_LAYOUT_UP,
    MONITOR_LAYOUT_MIRROR,
};

static const struct layout_option g_layout_options[] = {
    { N_("Keep current layout") },
    { N_("Arrange to the right") },
    { N_("Arrange to the left") },
    { N_("Arrange downward") },
    { N_("Arrange upward") },
    { N_("Mirror all monitors") },
};

static GtkWidget *g_preview_area = NULL;
static GtkWidget *g_preview_overlay = NULL;
static GtkWidget *g_loading_overlay = NULL;
static GtkWidget *g_loading_spinner = NULL;
static GtkWidget *g_loading_label = NULL;
static GtkWidget *g_monitor_dropdown = NULL;
static GtkWidget *g_mode_dropdown = NULL;
static GtkWidget *g_primary_dropdown = NULL;
static GtkWidget *g_layout_dropdown = NULL;
static GtkWidget *g_scale_dropdown = NULL;
static GtkWidget *g_orientation_dropdown = NULL;
static GtkWidget *g_brightness_scale = NULL;
static GtkWidget *g_night_light_switch = NULL;
static GtkWidget *g_vrr_switch = NULL;
static GtkWidget *g_status_label = NULL;
static GtkWidget *g_monitor_info_label = NULL;

static GtkStringList *g_monitor_model = NULL;
static GtkStringList *g_mode_model = NULL;
static GtkStringList *g_primary_model = NULL;

static GPtrArray *g_monitors = NULL;
static GPtrArray *g_mode_refs = NULL;
static enum monitor_backend g_monitor_backend = MONITOR_BACKEND_NONE;

static gboolean g_block_handlers = FALSE;
static int g_busy_depth = 0;
static gboolean g_preview_drag_active = FALSE;
static gboolean g_preview_drag_moved = FALSE;
static int g_preview_drag_monitor_idx = -1;
static double g_preview_drag_offset_x = 0.0;
static double g_preview_drag_offset_y = 0.0;

static void clear_monitor_data(void);
static void apply_layout_to_staged_positions(enum monitor_layout layout, int primary_idx);
static void rebuild_monitor_dropdown(void);
static void rebuild_primary_dropdown(void);
static enum monitor_layout selected_layout_mode(void);
static struct monitor_info *selected_monitor(void);
static struct monitor_mode *selected_mode(void);
static gboolean monitor_backend_can_apply_outputs(void);
static void queue_preview_redraw(void);
static void update_monitor_info_label(void);
static void status_set(const char *text, gboolean is_error);
static void save_current_config(const struct monitor_info *monitor, const struct monitor_mode *mode);
static void load_saved_config(void);
static void refresh_monitor_data(gboolean with_status);
static gboolean apply_outputs_configuration(char **error_out);

static int round_to_int(double value)
{
    return (int)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static gboolean monitor_name_is_unknown(const char *name)
{
    if (!name || !*name) {
        return TRUE;
    }

    char *copy = g_strdup(name);
    g_strstrip(copy);
    gboolean unknown = (!*copy)
        || g_ascii_strcasecmp(copy, "unknown") == 0
        || g_ascii_strcasecmp(copy, "unknown monitor") == 0
        || g_ascii_strcasecmp(copy, "default") == 0;

    if (!unknown) {
        char *lower = g_ascii_strdown(copy, -1);
        char **tokens = g_strsplit_set(lower, " _-\t", -1);
        gboolean all_unknownish = TRUE;

        for (guint i = 0; tokens[i] != NULL; i++) {
            if (!tokens[i][0]) {
                continue;
            }
            if (g_strcmp0(tokens[i], "unknown") == 0
                || g_strcmp0(tokens[i], "monitor") == 0
                || g_strcmp0(tokens[i], "display") == 0) {
                continue;
            }
            all_unknownish = FALSE;
            break;
        }

        if (all_unknownish) {
            unknown = TRUE;
        }

        g_strfreev(tokens);
        g_free(lower);
    }

    g_free(copy);
    return unknown;
}

static void set_monitor_fallback_name(struct monitor_info *monitor, guint index)
{
    if (!monitor) {
        return;
    }

    if (!monitor_name_is_unknown(monitor->name)) {
        return;
    }

    g_free(monitor->name);
    monitor->name = g_strdup_printf("Monitor %u", index + 1);
}

static void set_busy_state(gboolean busy, const char *message)
{
    if (!g_loading_overlay) {
        return;
    }

    if (busy) {
        g_busy_depth++;
        if (message && g_loading_label) {
            gtk_label_set_text(GTK_LABEL(g_loading_label), message);
        }

        gtk_widget_set_visible(g_loading_overlay, TRUE);
        if (g_loading_spinner) {
            gtk_spinner_start(GTK_SPINNER(g_loading_spinner));
        }
    } else {
        if (g_busy_depth > 0) {
            g_busy_depth--;
        }

        if (g_busy_depth == 0) {
            gtk_widget_set_visible(g_loading_overlay, FALSE);
            if (g_loading_spinner) {
                gtk_spinner_stop(GTK_SPINNER(g_loading_spinner));
            }
        }
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static gboolean compute_preview_transform(int width,
                                         int height,
                                         int *min_x_out,
                                         int *min_y_out,
                                         double *factor_out,
                                         double *pad_out)
{
    if (!g_monitors || g_monitors->len == 0) {
        return FALSE;
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

        int w = m->staged_width > 0 ? m->staged_width : 800;
        int h = m->staged_height > 0 ? m->staged_height : 600;

        if (m->staged_pos_x < min_x) min_x = m->staged_pos_x;
        if (m->staged_pos_y < min_y) min_y = m->staged_pos_y;
        if (m->staged_pos_x + w > max_x) max_x = m->staged_pos_x + w;
        if (m->staged_pos_y + h > max_y) max_y = m->staged_pos_y + h;
    }

    if (min_x == G_MAXINT || min_y == G_MAXINT) {
        return FALSE;
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

    *min_x_out = min_x;
    *min_y_out = min_y;
    *factor_out = factor;
    *pad_out = pad;
    return TRUE;
}

static int monitor_index_at_preview_point(double px,
                                          double py,
                                          int width,
                                          int height,
                                          double *rect_x_out,
                                          double *rect_y_out)
{
    int min_x = 0;
    int min_y = 0;
    double factor = 0.0;
    double pad = 0.0;

    if (!compute_preview_transform(width, height, &min_x, &min_y, &factor, &pad)) {
        return -1;
    }

    for (int i = (int)g_monitors->len - 1; i >= 0; i--) {
        struct monitor_info *m = g_ptr_array_index(g_monitors, (guint)i);
        if (!m->enabled) {
            continue;
        }

        double w = (double)(m->staged_width > 0 ? m->staged_width : 800) * factor;
        double h = (double)(m->staged_height > 0 ? m->staged_height : 600) * factor;
        double x = pad + ((double)m->staged_pos_x - (double)min_x) * factor;
        double y = pad + ((double)m->staged_pos_y - (double)min_y) * factor;

        if (px >= x && px <= x + w && py >= y && py <= y + h) {
            if (rect_x_out) {
                *rect_x_out = x;
            }
            if (rect_y_out) {
                *rect_y_out = y;
            }
            return i;
        }
    }

    return -1;
}

static void on_preview_click_pressed(GtkGestureClick *gesture,
                                     int n_press,
                                     double x,
                                     double y,
                                     gpointer user_data)
{
    (void)user_data;
    if (n_press != 1 || !g_preview_area || !g_monitors || g_monitors->len == 0) {
        return;
    }

    int width = gtk_widget_get_width(g_preview_area);
    int height = gtk_widget_get_height(g_preview_area);
    if (width <= 0 || height <= 0) {
        return;
    }

    double rect_x = 0.0;
    double rect_y = 0.0;
    int idx = monitor_index_at_preview_point(x, y, width, height, &rect_x, &rect_y);
    if (idx < 0) {
        g_preview_drag_active = FALSE;
        g_preview_drag_monitor_idx = -1;
        return;
    }

    g_preview_drag_active = TRUE;
    g_preview_drag_moved = FALSE;
    g_preview_drag_monitor_idx = idx;
    g_preview_drag_offset_x = x - rect_x;
    g_preview_drag_offset_y = y - rect_y;

    g_block_handlers = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_monitor_dropdown), (guint)idx);
    g_block_handlers = FALSE;
    update_monitor_info_label();
    queue_preview_redraw();

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void on_preview_click_released(GtkGestureClick *gesture,
                                      int n_press,
                                      double x,
                                      double y,
                                      gpointer user_data)
{
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    (void)user_data;

    gboolean should_apply = g_preview_drag_active && g_preview_drag_moved;

    g_preview_drag_active = FALSE;
    g_preview_drag_moved = FALSE;
    g_preview_drag_monitor_idx = -1;

    if (!should_apply) {
        return;
    }

    set_busy_state(TRUE, _("Applying display settings..."));

    if (!monitor_backend_can_apply_outputs()) {
        struct monitor_info *monitor = selected_monitor();
        struct monitor_mode *mode = selected_mode();
        save_current_config(monitor, mode);
        status_set(_("Monitor controls are in limited mode (missing wlr-randr). Layout changes are saved locally only."), FALSE);
        set_busy_state(FALSE, NULL);
        return;
    }

    char *apply_error = NULL;
    gboolean ok = apply_outputs_configuration(&apply_error);

    if (!ok) {
        status_set(apply_error ? apply_error : _("Could not apply monitor settings"), TRUE);
        g_free(apply_error);
        set_busy_state(FALSE, NULL);
        return;
    }

    g_free(apply_error);

    struct monitor_info *monitor = selected_monitor();
    struct monitor_mode *mode = selected_mode();
    save_current_config(monitor, mode);
    status_set(_("Display settings applied"), FALSE);

    refresh_monitor_data(FALSE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();

    set_busy_state(FALSE, NULL);
}

static void on_preview_motion(GtkEventControllerMotion *controller,
                              double x,
                              double y,
                              gpointer user_data)
{
    (void)controller;
    (void)user_data;

    if (!g_preview_drag_active || g_preview_drag_monitor_idx < 0 || !g_monitors) {
        return;
    }
    if ((guint)g_preview_drag_monitor_idx >= g_monitors->len) {
        return;
    }

    int width = gtk_widget_get_width(g_preview_area);
    int height = gtk_widget_get_height(g_preview_area);
    int min_x = 0;
    int min_y = 0;
    double factor = 0.0;
    double pad = 0.0;
    if (width <= 0 || height <= 0
        || !compute_preview_transform(width, height, &min_x, &min_y, &factor, &pad)) {
        return;
    }

    struct monitor_info *monitor = g_ptr_array_index(g_monitors, (guint)g_preview_drag_monitor_idx);
    int old_x = monitor->staged_pos_x;
    int old_y = monitor->staged_pos_y;
    double logical_x = ((x - g_preview_drag_offset_x - pad) / factor) + (double)min_x;
    double logical_y = ((y - g_preview_drag_offset_y - pad) / factor) + (double)min_y;

    monitor->staged_pos_x = round_to_int(logical_x);
    monitor->staged_pos_y = round_to_int(logical_y);

    if (old_x != monitor->staged_pos_x || old_y != monitor->staged_pos_y) {
        g_preview_drag_moved = TRUE;
    }

    if (g_layout_dropdown && selected_layout_mode() != MONITOR_LAYOUT_KEEP) {
        g_block_handlers = TRUE;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_layout_dropdown), MONITOR_LAYOUT_KEEP);
        g_block_handlers = FALSE;
    }

    status_set(_("Monitor moved in preview. Click Apply display settings to save."), FALSE);
    update_monitor_info_label();
    queue_preview_redraw();
}

static void on_preview_motion_leave(GtkEventControllerMotion *controller, gpointer user_data)
{
    (void)controller;
    (void)user_data;
    g_preview_drag_active = FALSE;
    g_preview_drag_monitor_idx = -1;
}

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
    g_free(monitor->staged_transform);
    if (monitor->modes) {
        g_ptr_array_unref(monitor->modes);
    }
    g_free(monitor);
}

static void monitor_sync_staged_from_current(struct monitor_info *monitor)
{
    if (!monitor) {
        return;
    }

    monitor->staged_width = monitor->width;
    monitor->staged_height = monitor->height;
    monitor->staged_refresh_hz = monitor->refresh_hz;
    monitor->staged_pos_x = monitor->pos_x;
    monitor->staged_pos_y = monitor->pos_y;
    monitor->staged_scale = monitor->scale > 0.0 ? monitor->scale : 1.0;
    monitor->staged_adaptive_sync = monitor->adaptive_sync;

    g_free(monitor->staged_transform);
    monitor->staged_transform = g_strdup((monitor->transform && *monitor->transform)
                                         ? monitor->transform
                                         : "normal");
}

static int monitor_logical_width(const struct monitor_info *monitor)
{
    if (!monitor) {
        return 1;
    }

    double scale = monitor->staged_scale > 0.0 ? monitor->staged_scale : 1.0;
    int width = monitor->staged_width > 0 ? monitor->staged_width : monitor->width;
    int logical = (int)(((double)width / scale) + 0.5);
    return logical > 0 ? logical : 1;
}

static int monitor_logical_height(const struct monitor_info *monitor)
{
    if (!monitor) {
        return 1;
    }

    double scale = monitor->staged_scale > 0.0 ? monitor->staged_scale : 1.0;
    int height = monitor->staged_height > 0 ? monitor->staged_height : monitor->height;
    int logical = (int)(((double)height / scale) + 0.5);
    return logical > 0 ? logical : 1;
}

static int selected_primary_index(void)
{
    if (!g_primary_dropdown) {
        return 0;
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_primary_dropdown));
    if (!g_monitors || idx >= g_monitors->len) {
        return 0;
    }
    return (int)idx;
}

static enum monitor_layout selected_layout_mode(void)
{
    if (!g_layout_dropdown) {
        return MONITOR_LAYOUT_KEEP;
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_layout_dropdown));
    if (idx >= G_N_ELEMENTS(g_layout_options)) {
        return MONITOR_LAYOUT_KEEP;
    }
    return (enum monitor_layout)idx;
}

static void set_primary_monitor_index(int primary_idx)
{
    if (!g_monitors || g_monitors->len == 0) {
        return;
    }

    if (primary_idx < 0 || primary_idx >= (int)g_monitors->len) {
        primary_idx = 0;
    }

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        monitor->primary = ((int)i == primary_idx);
    }
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

static void ensure_monitor_has_current_mode(struct monitor_info *monitor)
{
    if (!monitor) {
        return;
    }

    if (monitor->scale <= 0.0) {
        monitor->scale = 1.0;
    }

    if (!monitor->transform || !*monitor->transform) {
        g_free(monitor->transform);
        monitor->transform = g_strdup("normal");
    }

    if (monitor->width <= 0 || monitor->height <= 0) {
        if (monitor->modes && monitor->modes->len > 0) {
            struct monitor_mode *mode = g_ptr_array_index(monitor->modes, 0);
            monitor->width = mode->width;
            monitor->height = mode->height;
            monitor->refresh_hz = mode->refresh_hz;
        }
    }

    if (monitor->refresh_hz <= 0.0) {
        monitor->refresh_hz = 60.0;
    }

    if (!monitor->modes) {
        monitor->modes = g_ptr_array_new_with_free_func(free_monitor_mode);
    }

    if (monitor->modes->len == 0 && monitor->width > 0 && monitor->height > 0) {
        struct monitor_mode *mode = g_new0(struct monitor_mode, 1);
        mode->width = monitor->width;
        mode->height = monitor->height;
        mode->refresh_hz = monitor->refresh_hz;
        mode->preferred = TRUE;
        mode->current = TRUE;
        g_ptr_array_add(monitor->modes, mode);
    }

    monitor_sync_staged_from_current(monitor);
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

static gint compare_monitor_mode_desc(gconstpointer a, gconstpointer b)
{
    const struct monitor_mode *left = *((const struct monitor_mode * const *)a);
    const struct monitor_mode *right = *((const struct monitor_mode * const *)b);

    long left_pixels = (long)left->width * (long)left->height;
    long right_pixels = (long)right->width * (long)right->height;
    if (left_pixels != right_pixels) {
        return left_pixels > right_pixels ? -1 : 1;
    }

    if (fabs(left->refresh_hz - right->refresh_hz) > 0.01) {
        return left->refresh_hz > right->refresh_hz ? -1 : 1;
    }

    if (left->preferred != right->preferred) {
        return left->preferred ? -1 : 1;
    }
    if (left->current != right->current) {
        return left->current ? -1 : 1;
    }

    return 0;
}

static void normalize_monitor_modes(struct monitor_info *monitor)
{
    if (!monitor || !monitor->modes || monitor->modes->len < 2) {
        return;
    }

    g_ptr_array_sort(monitor->modes, compare_monitor_mode_desc);

    guint i = 1;
    while (i < monitor->modes->len) {
        struct monitor_mode *prev = g_ptr_array_index(monitor->modes, i - 1);
        struct monitor_mode *cur = g_ptr_array_index(monitor->modes, i);
        gboolean same = prev->width == cur->width
            && prev->height == cur->height
            && fabs(prev->refresh_hz - cur->refresh_hz) < 0.05;
        if (!same) {
            i++;
            continue;
        }

        prev->preferred = prev->preferred || cur->preferred;
        prev->current = prev->current || cur->current;
        g_ptr_array_remove_index(monitor->modes, i);
    }
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
    if (g_str_has_prefix(line, "Output ")) {
        const char *start = line + strlen("Output ");
        while (*start == ' ' ) {
            start++;
        }
        if (!*start) {
            return FALSE;
        }
        const char *end = start;
        while (*end && *end != ' ') {
            end++;
        }
        if (end == start) {
            return FALSE;
        }
        *name_out = g_strndup(start, (gsize)(end - start));
        return TRUE;
    }

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

    if (g_find_program_in_path("wlr-randr")) {
        g_monitor_backend = MONITOR_BACKEND_WLR_RANDR;
        run_command_capture("sh -lc 'wlr-randr 2>/dev/null || true'",
                            &stdout_data,
                            &stderr_data,
                            &wait_status);
    } else {
        g_monitor_backend = MONITOR_BACKEND_NONE;
    }
    (void)wait_status;

    if (!stdout_data || !*stdout_data) {
        if (err_out) {
            *err_out = g_strdup(_("No monitor backend available (wlr-randr) or no outputs"));
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

        if (strstr(line, "Scale factor:")) {
            double scale = 1.0;
            if (sscanf(line, " Scale factor: %lf", &scale) == 1 || sscanf(line, "Scale factor: %lf", &scale) == 1) {
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

        if (strstr(line, "Adaptive Sync:") || strstr(line, "Adaptive sync:")) {
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

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        set_monitor_fallback_name(monitor, i);
        normalize_monitor_modes(monitor);
        ensure_monitor_has_current_mode(monitor);
    }

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

static gboolean query_monitors(char **err_out)
{
    return query_monitors_from_wlr_randr(err_out);
}

static int detect_primary_monitor_index(void)
{
    if (!g_monitors || g_monitors->len == 0) {
        return 0;
    }

    int best_idx = 0;
    int best_score = G_MAXINT;

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        if (!monitor->enabled) {
            continue;
        }

        int score = abs(monitor->pos_x) + abs(monitor->pos_y);
        if (monitor->pos_x == 0 && monitor->pos_y == 0) {
            return (int)i;
        }

        if (score < best_score) {
            best_score = score;
            best_idx = (int)i;
        }
    }

    return best_idx;
}

static const char *monitor_backend_label(enum monitor_backend backend)
{
    switch (backend) {
    case MONITOR_BACKEND_WLR_RANDR:
        return "wlr-randr";
    case MONITOR_BACKEND_NONE:
    default:
        return "none";
    }
}

static gboolean monitor_backend_can_apply_outputs(void)
{
    return g_monitor_backend == MONITOR_BACKEND_WLR_RANDR;
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
             "%s%s: %dx%d @ %.2f Hz, %s %.2fx, %s (%d,%d), backend: %s",
             monitor->name,
             monitor->primary ? " [main]" : "",
             monitor->staged_width,
             monitor->staged_height,
             monitor->staged_refresh_hz,
             _("scale"),
             monitor->staged_scale,
             _("position"),
             monitor->staged_pos_x,
             monitor->staged_pos_y,
             monitor_backend_label(g_monitor_backend));
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
        if (mode->width == monitor->staged_width
            && mode->height == monitor->staged_height
            && fabs(mode->refresh_hz - monitor->staged_refresh_hz) < 0.15) {
            selected_idx = i;
        }
    }

    if (selected_idx == GTK_INVALID_LIST_POSITION) {
        selected_idx = 0;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mode_dropdown), selected_idx);
}

static void rebuild_primary_dropdown(void)
{
    clear_string_list(g_primary_model);

    if (!g_monitors || g_monitors->len == 0) {
        gtk_string_list_append(g_primary_model, _("No connected monitors"));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_primary_dropdown), 0);
        return;
    }

    guint selected_idx = 0;
    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        gtk_string_list_append(g_primary_model, monitor->name);
        if (monitor->primary) {
            selected_idx = i;
        }
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_primary_dropdown), selected_idx);
}

static void on_mode_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    struct monitor_info *monitor = selected_monitor();
    struct monitor_mode *mode = selected_mode();
    if (!monitor || !mode) {
        return;
    }

    monitor->staged_width = mode->width;
    monitor->staged_height = mode->height;
    monitor->staged_refresh_hz = mode->refresh_hz;
    g_block_handlers = TRUE;
    rebuild_monitor_dropdown();
    g_block_handlers = FALSE;
    queue_preview_redraw();
    update_monitor_info_label();
}

static void on_scale_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    struct monitor_info *monitor = selected_monitor();
    if (!monitor) {
        return;
    }

    guint scale_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_scale_dropdown));
    if (scale_idx >= G_N_ELEMENTS(g_scale_values)) {
        scale_idx = 0;
    }

    monitor->staged_scale = g_scale_values[scale_idx];
    g_block_handlers = TRUE;
    rebuild_monitor_dropdown();
    g_block_handlers = FALSE;
    queue_preview_redraw();
    update_monitor_info_label();
}

static void on_orientation_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    struct monitor_info *monitor = selected_monitor();
    if (!monitor) {
        return;
    }

    guint orient_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_orientation_dropdown));
    if (orient_idx >= G_N_ELEMENTS(g_orientation_options)) {
        orient_idx = 0;
    }

    g_free(monitor->staged_transform);
    monitor->staged_transform = g_strdup(g_orientation_options[orient_idx].arg);
    update_monitor_info_label();
}

static void on_vrr_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    struct monitor_info *monitor = selected_monitor();
    if (!monitor) {
        return;
    }

    monitor->staged_adaptive_sync = gtk_switch_get_active(GTK_SWITCH(g_vrr_switch));
}

static void on_primary_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    set_busy_state(TRUE, _("Rebuilding display layout..."));
    apply_layout_to_staged_positions(selected_layout_mode(), selected_primary_index());
    g_block_handlers = TRUE;
    rebuild_monitor_dropdown();
    rebuild_primary_dropdown();
    g_block_handlers = FALSE;
    update_monitor_info_label();
    queue_preview_redraw();
    set_busy_state(FALSE, NULL);
}

static void on_layout_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;
    if (g_block_handlers) {
        return;
    }

    set_busy_state(TRUE, _("Rebuilding display layout..."));
    apply_layout_to_staged_positions(selected_layout_mode(), selected_primary_index());
    g_block_handlers = TRUE;
    rebuild_monitor_dropdown();
    rebuild_primary_dropdown();
    g_block_handlers = FALSE;
    update_monitor_info_label();
    queue_preview_redraw();
    set_busy_state(FALSE, NULL);
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
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), nearest_scale_index(monitor->staged_scale));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), orientation_index_from_arg(monitor->staged_transform));
        gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), monitor->staged_adaptive_sync);
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

        int w = m->staged_width > 0 ? m->staged_width : 800;
        int h = m->staged_height > 0 ? m->staged_height : 600;

        if (m->staged_pos_x < min_x) min_x = m->staged_pos_x;
        if (m->staged_pos_y < min_y) min_y = m->staged_pos_y;
        if (m->staged_pos_x + w > max_x) max_x = m->staged_pos_x + w;
        if (m->staged_pos_y + h > max_y) max_y = m->staged_pos_y + h;
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

        double w = (double)(m->staged_width > 0 ? m->staged_width : 800) * factor;
        double h = (double)(m->staged_height > 0 ? m->staged_height : 600) * factor;
        double x = pad + ((double)m->staged_pos_x - (double)min_x) * factor;
        double y = pad + ((double)m->staged_pos_y - (double)min_y) * factor;

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

        if (m->primary) {
            cairo_set_source_rgba(cairo, 0.98, 0.84, 0.32, 0.95);
            cairo_arc(cairo, x + w - 12.0, y + 12.0, 4.0, 0.0, 2.0 * G_PI);
            cairo_fill(cairo);
        }

        char mode_text[64] = { 0 };
        snprintf(mode_text, sizeof(mode_text), "%dx%d @ %.1f", m->staged_width, m->staged_height, m->staged_refresh_hz);
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

static void apply_layout_to_staged_positions(enum monitor_layout layout, int primary_idx)
{
    if (!g_monitors || g_monitors->len == 0) {
        return;
    }

    if (primary_idx < 0 || primary_idx >= (int)g_monitors->len) {
        primary_idx = 0;
    }

    set_primary_monitor_index(primary_idx);

    if (layout == MONITOR_LAYOUT_KEEP) {
        for (guint i = 0; i < g_monitors->len; i++) {
            struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
            monitor->staged_pos_x = monitor->pos_x;
            monitor->staged_pos_y = monitor->pos_y;
        }
        return;
    }

    struct monitor_info *primary = g_ptr_array_index(g_monitors, (guint)primary_idx);
    primary->staged_pos_x = 0;
    primary->staged_pos_y = 0;

    if (layout == MONITOR_LAYOUT_MIRROR) {
        for (guint i = 0; i < g_monitors->len; i++) {
            struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
            monitor->staged_pos_x = 0;
            monitor->staged_pos_y = 0;
        }
        return;
    }

    int cursor_x = 0;
    int cursor_y = 0;
    int primary_w = monitor_logical_width(primary);
    int primary_h = monitor_logical_height(primary);

    if (layout == MONITOR_LAYOUT_RIGHT) {
        cursor_x = primary_w;
    } else if (layout == MONITOR_LAYOUT_LEFT) {
        cursor_x = 0;
    } else if (layout == MONITOR_LAYOUT_DOWN) {
        cursor_y = primary_h;
    } else if (layout == MONITOR_LAYOUT_UP) {
        cursor_y = 0;
    }

    for (guint turn = 0; turn < g_monitors->len; turn++) {
        guint idx = turn == 0 ? (guint)primary_idx : ((turn <= (guint)primary_idx) ? turn - 1 : turn);
        if ((int)idx == primary_idx) {
            continue;
        }

        struct monitor_info *monitor = g_ptr_array_index(g_monitors, idx);
        int w = monitor_logical_width(monitor);
        int h = monitor_logical_height(monitor);

        if (layout == MONITOR_LAYOUT_RIGHT) {
            monitor->staged_pos_x = cursor_x;
            monitor->staged_pos_y = 0;
            cursor_x += w;
        } else if (layout == MONITOR_LAYOUT_LEFT) {
            cursor_x -= w;
            monitor->staged_pos_x = cursor_x;
            monitor->staged_pos_y = 0;
        } else if (layout == MONITOR_LAYOUT_DOWN) {
            monitor->staged_pos_x = 0;
            monitor->staged_pos_y = cursor_y;
            cursor_y += h;
        } else if (layout == MONITOR_LAYOUT_UP) {
            cursor_y -= h;
            monitor->staged_pos_x = 0;
            monitor->staged_pos_y = cursor_y;
        }
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
    g_key_file_set_integer(kf, "display", "layout_idx", (int)selected_layout_mode());
    g_key_file_set_boolean(kf, "display", "vrr", gtk_switch_get_active(GTK_SWITCH(g_vrr_switch)));
    g_key_file_set_integer(kf, "display", "brightness", (int)gtk_range_get_value(GTK_RANGE(g_brightness_scale)));
    g_key_file_set_boolean(kf, "display", "night_light", gtk_switch_get_active(GTK_SWITCH(g_night_light_switch)));

    int primary_idx = selected_primary_index();
    g_key_file_set_integer(kf, "display", "primary_idx", primary_idx);
    if (g_monitors && primary_idx >= 0 && primary_idx < (int)g_monitors->len) {
        struct monitor_info *primary = g_ptr_array_index(g_monitors, (guint)primary_idx);
        if (primary && primary->name) {
            g_key_file_set_string(kf, "display", "primary_monitor", primary->name);
        }
    }

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

    int layout_idx = g_key_file_get_integer(kf, "display", "layout_idx", &error);
    if (error) {
        g_clear_error(&error);
        layout_idx = 0;
    }

    int primary_idx = g_key_file_get_integer(kf, "display", "primary_idx", &error);
    if (error) {
        g_clear_error(&error);
        primary_idx = -1;
    }

    char *primary_monitor = g_key_file_get_string(kf, "display", "primary_monitor", &error);
    if (error) {
        g_clear_error(&error);
        primary_monitor = NULL;
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
    if (layout_idx < 0 || layout_idx >= (int)G_N_ELEMENTS(g_layout_options)) {
        layout_idx = 0;
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), (guint)scale_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), (guint)orient_idx);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_layout_dropdown), (guint)layout_idx);
    gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), vrr);
    gtk_range_set_value(GTK_RANGE(g_brightness_scale), brightness < 10 ? 10 : (brightness > 100 ? 100 : brightness));
    gtk_switch_set_active(GTK_SWITCH(g_night_light_switch), night_light);

    if (g_monitors && g_monitors->len > 0) {
        if (primary_monitor && *primary_monitor) {
            for (guint i = 0; i < g_monitors->len; i++) {
                struct monitor_info *m = g_ptr_array_index(g_monitors, i);
                if (g_strcmp0(m->name, primary_monitor) == 0) {
                    primary_idx = (int)i;
                    break;
                }
            }
        }
        if (primary_idx < 0 || primary_idx >= (int)g_monitors->len) {
            primary_idx = detect_primary_monitor_index();
        }
        set_primary_monitor_index(primary_idx);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_primary_dropdown), (guint)primary_idx);
        apply_layout_to_staged_positions((enum monitor_layout)layout_idx, primary_idx);
    }

    g_block_handlers = FALSE;

    g_free(monitor_name);
    g_free(primary_monitor);
    g_key_file_unref(kf);
    g_free(path);
}

static void rebuild_monitor_dropdown(void)
{
    char *selected_name = NULL;
    if (g_monitors && g_monitor_dropdown) {
        guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_monitor_dropdown));
        if (selected < g_monitors->len) {
            struct monitor_info *selected_monitor = g_ptr_array_index(g_monitors, selected);
            selected_name = g_strdup(selected_monitor->name);
        }
    }

    clear_string_list(g_monitor_model);

    if (!g_monitors || g_monitors->len == 0) {
        gtk_string_list_append(g_monitor_model, _("No connected monitors"));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_monitor_dropdown), 0);
        return;
    }

    guint selected_idx = 0;
    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        char line[256] = { 0 };
        snprintf(line,
                 sizeof(line),
                 "%s%s (%dx%d @ %.2f Hz)%s",
                 monitor->name,
                 monitor->primary ? _(" [main]") : "",
                 monitor->staged_width,
                 monitor->staged_height,
                 monitor->staged_refresh_hz,
                 monitor->enabled ? "" : _(" [disabled]"));
        gtk_string_list_append(g_monitor_model, line);

        if (selected_name && g_strcmp0(selected_name, monitor->name) == 0) {
            selected_idx = i;
        }
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_monitor_dropdown), selected_idx);
    g_free(selected_name);
}

static void refresh_monitor_data(gboolean with_status)
{
    char *error = NULL;
    gboolean ok = query_monitors(&error);

    if (ok && g_monitors && g_monitors->len > 0) {
        set_primary_monitor_index(detect_primary_monitor_index());
    }

    g_block_handlers = TRUE;
    rebuild_monitor_dropdown();
    rebuild_primary_dropdown();
    rebuild_mode_dropdown_for_selected();

    struct monitor_info *monitor = selected_monitor();
    if (monitor) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), nearest_scale_index(monitor->staged_scale));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), orientation_index_from_arg(monitor->staged_transform));
        gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), monitor->staged_adaptive_sync);
    }

    apply_layout_to_staged_positions(selected_layout_mode(), selected_primary_index());

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

    set_busy_state(TRUE, _("Refreshing monitor list..."));
    refresh_monitor_data(TRUE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();
    set_busy_state(FALSE, NULL);
}

static gboolean apply_outputs_configuration(char **error_out)
{
    if (!g_monitors || g_monitors->len == 0) {
        if (error_out) {
            *error_out = g_strdup(_("No connected monitors"));
        }
        return FALSE;
    }

    if (g_monitor_backend != MONITOR_BACKEND_WLR_RANDR) {
        if (error_out) {
            *error_out = g_strdup(_("Detected monitors, but no output-management backend is available for mode/layout apply"));
        }
        return FALSE;
    }

    for (guint i = 0; i < g_monitors->len; i++) {
        struct monitor_info *monitor = g_ptr_array_index(g_monitors, i);
        if (!monitor || !monitor->enabled) {
            continue;
        }

        if (monitor->staged_width <= 0 || monitor->staged_height <= 0 || monitor->staged_refresh_hz <= 0.0) {
            continue;
        }

        char *quoted_name = g_shell_quote(monitor->name ? monitor->name : "");
        const char *transform = (monitor->staged_transform && *monitor->staged_transform)
            ? monitor->staged_transform
            : "normal";
        double scale = monitor->staged_scale > 0.0 ? monitor->staged_scale : 1.0;

        char *cmd = g_strdup_printf(
            "sh -lc 'wlr-randr --output %s --mode %dx%d@%.6fHz --scale %.2f --transform %s --pos %d,%d --adaptive-sync %s'",
            quoted_name,
            monitor->staged_width,
            monitor->staged_height,
            monitor->staged_refresh_hz,
            scale,
            transform,
            monitor->staged_pos_x,
            monitor->staged_pos_y,
            monitor->staged_adaptive_sync ? "enabled" : "disabled");

        char *stdout_data = NULL;
        char *stderr_data = NULL;
        int wait_status = 0;
        gboolean ok = run_command_capture(cmd, &stdout_data, &stderr_data, &wait_status);
        (void)wait_status;

        g_free(cmd);
        g_free(quoted_name);

        if (!ok) {
            if (error_out) {
                if (stderr_data && *stderr_data) {
                    g_strstrip(stderr_data);
                    *error_out = g_strdup(stderr_data);
                } else {
                    *error_out = g_strdup(_("Could not apply monitor settings"));
                }
            }
            g_free(stdout_data);
            g_free(stderr_data);
            return FALSE;
        }

        g_free(stdout_data);
        g_free(stderr_data);
    }

    return TRUE;
}

static void on_apply_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    if (g_block_handlers) {
        return;
    }

    if (!g_monitors || g_monitors->len == 0) {
        status_set(_("No connected monitors"), TRUE);
        return;
    }

    set_busy_state(TRUE, _("Applying display settings..."));
    apply_layout_to_staged_positions(selected_layout_mode(), selected_primary_index());

    apply_runtime_tweaks();

    if (!monitor_backend_can_apply_outputs()) {
        struct monitor_info *monitor = selected_monitor();
        struct monitor_mode *mode = selected_mode();
        save_current_config(monitor, mode);
        status_set(_("Monitor controls are in limited mode (missing wlr-randr). Layout changes are saved locally only."), FALSE);
        set_busy_state(FALSE, NULL);
        return;
    }

    char *apply_error = NULL;
    gboolean ok = apply_outputs_configuration(&apply_error);

    if (!ok) {
        status_set(apply_error ? apply_error : _("Could not apply monitor settings"), TRUE);
        g_free(apply_error);
        set_busy_state(FALSE, NULL);
        return;
    }

    g_free(apply_error);

    struct monitor_info *monitor = selected_monitor();
    struct monitor_mode *mode = selected_mode();
    save_current_config(monitor, mode);
    status_set(_("Display settings applied"), FALSE);

    refresh_monitor_data(FALSE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();
    set_busy_state(FALSE, NULL);
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
    gtk_widget_add_css_class(preview_frame, "display-preview-card");
    GtkWidget *preview_box = gtk_frame_get_child(GTK_FRAME(preview_frame));
    gtk_widget_add_css_class(preview_box, "display-preview-box");

    g_preview_overlay = gtk_overlay_new();
    gtk_widget_add_css_class(g_preview_overlay, "display-preview-overlay");
    gtk_box_append(GTK_BOX(preview_box), g_preview_overlay);

    g_preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(g_preview_area, -1, 220);
    gtk_widget_add_css_class(g_preview_area, "display-preview-area");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(g_preview_area), draw_monitor_preview, NULL, NULL);
    gtk_overlay_set_child(GTK_OVERLAY(g_preview_overlay), g_preview_area);

    GtkGesture *preview_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(preview_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(preview_click, "pressed", G_CALLBACK(on_preview_click_pressed), NULL);
    g_signal_connect(preview_click, "released", G_CALLBACK(on_preview_click_released), NULL);
    gtk_widget_add_controller(g_preview_area, GTK_EVENT_CONTROLLER(preview_click));

    GtkEventController *preview_motion = gtk_event_controller_motion_new();
    g_signal_connect(preview_motion, "motion", G_CALLBACK(on_preview_motion), NULL);
    g_signal_connect(preview_motion, "leave", G_CALLBACK(on_preview_motion_leave), NULL);
    gtk_widget_add_controller(g_preview_area, preview_motion);

    g_loading_overlay = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(g_loading_overlay, "display-loading-overlay");
    gtk_widget_set_halign(g_loading_overlay, GTK_ALIGN_FILL);
    gtk_widget_set_valign(g_loading_overlay, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(g_loading_overlay, TRUE);
    gtk_widget_set_vexpand(g_loading_overlay, TRUE);
    gtk_widget_set_visible(g_loading_overlay, FALSE);

    GtkWidget *loading_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(loading_panel, "display-loading-panel");
    gtk_widget_set_halign(loading_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(loading_panel, GTK_ALIGN_CENTER);

    g_loading_spinner = gtk_spinner_new();
    gtk_widget_set_size_request(g_loading_spinner, 28, 28);
    gtk_box_append(GTK_BOX(loading_panel), g_loading_spinner);

    g_loading_label = gtk_label_new(_("Working..."));
    gtk_widget_add_css_class(g_loading_label, "display-loading-label");
    gtk_box_append(GTK_BOX(loading_panel), g_loading_label);

    gtk_box_append(GTK_BOX(g_loading_overlay), loading_panel);
    gtk_overlay_add_overlay(GTK_OVERLAY(g_preview_overlay), g_loading_overlay);

    g_monitor_info_label = gtk_label_new("");
    gtk_widget_set_halign(g_monitor_info_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_monitor_info_label, "row-subtitle");
    gtk_widget_add_css_class(g_monitor_info_label, "display-monitor-meta");
    gtk_box_append(GTK_BOX(preview_box), g_monitor_info_label);

    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh monitor list"));
    gtk_widget_add_css_class(refresh_btn, "display-secondary-btn");
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_monitors_clicked), NULL);
    gtk_box_append(GTK_BOX(preview_box), refresh_btn);

    gtk_box_append(GTK_BOX(box), preview_frame);

    GtkWidget *screen_frame = create_section(_("Screen configuration"), _("Apply mode, scaling, orientation and adaptive sync to the selected monitor."));
    gtk_widget_add_css_class(screen_frame, "display-config-card");
    GtkWidget *screen_box = gtk_frame_get_child(GTK_FRAME(screen_frame));
    gtk_widget_add_css_class(screen_box, "display-config-box");

    g_monitor_model = gtk_string_list_new(NULL);
    g_monitor_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_monitor_model), NULL);
    g_signal_connect(g_monitor_dropdown, "notify::selected", G_CALLBACK(on_monitor_changed), NULL);

    g_primary_model = gtk_string_list_new(NULL);
    g_primary_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_primary_model), NULL);
    g_signal_connect(g_primary_dropdown, "notify::selected", G_CALLBACK(on_primary_changed), NULL);

    g_mode_model = gtk_string_list_new(NULL);
    g_mode_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_mode_model), NULL);
    g_signal_connect(g_mode_dropdown, "notify::selected", G_CALLBACK(on_mode_changed), NULL);

    GtkStringList *layout_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_layout_options); i++) {
        gtk_string_list_append(layout_model, _(g_layout_options[i].label));
    }
    g_layout_dropdown = gtk_drop_down_new(G_LIST_MODEL(layout_model), NULL);
    g_signal_connect(g_layout_dropdown, "notify::selected", G_CALLBACK(on_layout_changed), NULL);
    g_object_unref(layout_model);

    GtkStringList *scale_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_scale_values); i++) {
        char entry[16] = { 0 };
        snprintf(entry, sizeof(entry), "%.0f%%", g_scale_values[i] * 100.0);
        gtk_string_list_append(scale_model, entry);
    }
    g_scale_dropdown = gtk_drop_down_new(G_LIST_MODEL(scale_model), NULL);
    g_signal_connect(g_scale_dropdown, "notify::selected", G_CALLBACK(on_scale_changed), NULL);
    g_object_unref(scale_model);

    GtkStringList *orientation_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_orientation_options); i++) {
        gtk_string_list_append(orientation_model, _(g_orientation_options[i].label));
    }
    g_orientation_dropdown = gtk_drop_down_new(G_LIST_MODEL(orientation_model), NULL);
    g_signal_connect(g_orientation_dropdown, "notify::selected", G_CALLBACK(on_orientation_changed), NULL);
    g_object_unref(orientation_model);

    g_vrr_switch = gtk_switch_new();
    g_signal_connect(g_vrr_switch, "notify::active", G_CALLBACK(on_vrr_changed), NULL);

    gtk_box_append(GTK_BOX(screen_box), create_row(_("Monitor"), g_monitor_dropdown));
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("Main monitor"), g_primary_dropdown));
    gtk_box_append(GTK_BOX(screen_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(screen_box), create_row(_("Monitor layout"), g_layout_dropdown));
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
    gtk_widget_add_css_class(actions, "display-actions");

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply display settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    gtk_widget_add_css_class(apply_btn, "display-apply-btn");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);
    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_widget_add_css_class(g_status_label, "display-status");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_scale_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_orientation_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_layout_dropdown), MONITOR_LAYOUT_KEEP);
    gtk_range_set_value(GTK_RANGE(g_brightness_scale), 70.0);
    gtk_switch_set_active(GTK_SWITCH(g_night_light_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_vrr_switch), FALSE);

    refresh_monitor_data(FALSE);
    load_saved_config();
    update_monitor_info_label();
    queue_preview_redraw();

    return outer_scroll;
}
