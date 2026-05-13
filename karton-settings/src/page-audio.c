#include "page-audio.h"

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

static const struct option_value g_profile_options[] = {
    { N_("Pro audio"), "pro-audio" },
    { N_("Analog stereo"), "analog-stereo" },
    { N_("Digital stereo"), "digital-stereo" },
    { N_("Headset / hands-free"), "headset-head-unit" },
    { N_("Disabled"), "off" },
};

static const struct option_value g_mixing_options[] = {
    { N_("Automatic"), "auto" },
    { N_("Software mixing"), "software" },
    { N_("Hardware mixing"), "hardware" },
};

static const struct option_value g_routing_options[] = {
    { N_("Automatic"), "auto" },
    { N_("Speakers"), "speakers" },
    { N_("Headphones"), "headphones" },
    { N_("Bluetooth"), "bluetooth" },
};

static GtkWidget *g_output_dropdown = NULL;
static GtkWidget *g_input_dropdown = NULL;
static GtkWidget *g_profile_dropdown = NULL;
static GtkWidget *g_mixing_dropdown = NULL;
static GtkWidget *g_routing_dropdown = NULL;
static GtkWidget *g_volume_scale = NULL;
static GtkWidget *g_mic_scale = NULL;
static GtkWidget *g_mic_activity_bar = NULL;
static GtkWidget *g_mic_activity_label = NULL;
static GtkWidget *g_balance_scale = NULL;
static GtkWidget *g_bluetooth_switch = NULL;
static GtkWidget *g_effects_switch = NULL;
static GtkWidget *g_status_label = NULL;
static guint g_mic_activity_timer_id = 0;
static guint g_auto_apply_timeout_id = 0;
static gboolean g_block_runtime_handlers = FALSE;
static guint g_pending_apply_mask = 0;

#define AUDIO_APPLY_LEVELS_ONLY (1u << 0)
#define AUDIO_APPLY_FULL        (1u << 1)

static GtkStringList *g_output_model = NULL;
static GtkStringList *g_input_model = NULL;

static GPtrArray *g_output_refs = NULL;
static GPtrArray *g_input_refs = NULL;

static gboolean auto_apply_audio_timeout(gpointer data);

static void schedule_auto_apply_audio(guint mask)
{
    if (g_block_runtime_handlers) {
        return;
    }

    g_pending_apply_mask |= mask;

    if (g_auto_apply_timeout_id) {
        g_source_remove(g_auto_apply_timeout_id);
        g_auto_apply_timeout_id = 0;
    }

    g_auto_apply_timeout_id = g_timeout_add(220, auto_apply_audio_timeout, NULL);
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

static gboolean run_command_async(const char *command)
{
    GError *error = NULL;
    gboolean ok = g_spawn_command_line_async(command, &error);
    if (!ok) {
        g_clear_error(&error);
    }
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

static int round_to_int(double value)
{
    return (int)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 150) {
        return 150;
    }
    return value;
}

static gboolean parse_first_percent(const char *text, int *value_out)
{
    if (!text || !value_out) {
        return FALSE;
    }

    for (const char *p = text; *p; p++) {
        if (!g_ascii_isdigit(*p)) {
            continue;
        }

        char *end = NULL;
        long value = g_ascii_strtoll(p, &end, 10);
        if (end && *end == '%') {
            *value_out = clamp_percent((int)value);
            return TRUE;
        }

        if (end && end > p) {
            p = end - 1;
        }
    }

    return FALSE;
}

static char *audio_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "audio.conf", NULL);
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

static void clear_string_list(GtkStringList *list)
{
    if (!list) {
        return;
    }

    while (g_list_model_get_n_items(G_LIST_MODEL(list)) > 0) {
        gtk_string_list_remove(list, g_list_model_get_n_items(G_LIST_MODEL(list)) - 1);
    }
}

static const char *selected_output_name(void)
{
    if (!g_output_dropdown || !g_output_refs) {
        return NULL;
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_output_dropdown));
    if (idx >= g_output_refs->len) {
        return NULL;
    }

    return g_ptr_array_index(g_output_refs, idx);
}

static const char *selected_input_name(void)
{
    if (!g_input_dropdown || !g_input_refs) {
        return NULL;
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_input_dropdown));
    if (idx >= g_input_refs->len) {
        return NULL;
    }

    return g_ptr_array_index(g_input_refs, idx);
}

static char *get_default_from_pactl_info(const char *key_prefix)
{
    if (g_strcmp0(key_prefix, "Default Source:") == 0) {
        char *stdout_data = NULL;
        char *stderr_data = NULL;
        int wait_status = 0;
        gboolean ok = run_command_capture("sh -lc 'timeout 2s LC_ALL=C pactl get-default-source 2>/dev/null'",
                                          &stdout_data,
                                          &stderr_data,
                                          &wait_status);
        (void)wait_status;
        g_free(stderr_data);

        if (ok && stdout_data) {
            g_strstrip(stdout_data);
            if (*stdout_data) {
                return stdout_data;
            }
        }
        g_free(stdout_data);
    } else if (g_strcmp0(key_prefix, "Default Sink:") == 0) {
        char *stdout_data = NULL;
        char *stderr_data = NULL;
        int wait_status = 0;
        gboolean ok = run_command_capture("sh -lc 'timeout 2s LC_ALL=C pactl get-default-sink 2>/dev/null'",
                                          &stdout_data,
                                          &stderr_data,
                                          &wait_status);
        (void)wait_status;
        g_free(stderr_data);

        if (ok && stdout_data) {
            g_strstrip(stdout_data);
            if (*stdout_data) {
                return stdout_data;
            }
        }
        g_free(stdout_data);
    }

    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;

    gboolean ok = run_command_capture("sh -lc 'timeout 2s LC_ALL=C pactl info 2>/dev/null'", &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;
    g_free(stderr_data);

    if (!ok || !stdout_data) {
        g_free(stdout_data);
        return NULL;
    }

    char *result = NULL;
    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (!g_str_has_prefix(lines[i], key_prefix)) {
            continue;
        }

        const char *value = lines[i] + strlen(key_prefix);
        while (*value == ' ') {
            value++;
        }

        if (*value) {
            result = g_strdup(value);
            break;
        }
    }

    g_strfreev(lines);
    g_free(stdout_data);
    return result;
}

static GHashTable *read_pactl_description_map(const char *command)
{
    GHashTable *map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;

    gboolean ok = run_command_capture(command, &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;
    g_free(stderr_data);

    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return map;
    }

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    char *current_name = NULL;

    for (guint i = 0; lines[i] != NULL; i++) {
        char *trimmed = g_strdup(lines[i]);
        g_strstrip(trimmed);

        if (g_str_has_prefix(trimmed, "Name:")) {
            g_free(current_name);
            current_name = g_strdup(trimmed + strlen("Name:"));
            g_strstrip(current_name);
        } else if (current_name && g_str_has_prefix(trimmed, "Description:")) {
            char *description = g_strdup(trimmed + strlen("Description:"));
            g_strstrip(description);

            if (*current_name && *description && !g_hash_table_contains(map, current_name)) {
                g_hash_table_insert(map, g_strdup(current_name), description);
                description = NULL;
            }

            g_free(description);
        }

        g_free(trimmed);
    }

    g_free(current_name);
    g_strfreev(lines);
    g_free(stdout_data);
    return map;
}

static char *shorten_device_label(const char *text)
{
    if (!text || !*text) {
        return g_strdup("");
    }

    const guint max_chars = 44;
    glong chars = g_utf8_strlen(text, -1);
    if (chars <= (glong)max_chars) {
        return g_strdup(text);
    }

    const char *end = g_utf8_offset_to_pointer(text, (glong)max_chars - 3);
    gsize bytes = (gsize)(end - text);
    return g_strdup_printf("%.*s...", (int)bytes, text);
}

static char *build_device_label(const char *name, const char *description)
{
    if (description && *description && g_strcmp0(name, description) != 0) {
        char *trimmed = g_strdup(description);
        g_strstrip(trimmed);
        char *shortened = shorten_device_label(trimmed);
        g_free(trimmed);
        return shortened;
    }

    return shorten_device_label(name);
}

static void append_device_name(GtkStringList *model, GPtrArray *refs, const char *name, const char *display_name)
{
    if (!model || !refs || !name || !*name) {
        return;
    }

    for (guint i = 0; i < refs->len; i++) {
        const char *existing = g_ptr_array_index(refs, i);
        if (g_strcmp0(existing, name) == 0) {
            return;
        }
    }

    gtk_string_list_append(model, (display_name && *display_name) ? display_name : name);
    g_ptr_array_add(refs, g_strdup(name));
}

static void populate_device_model(GtkStringList *model,
                                  GPtrArray *refs,
                                  const char *command,
                                  GHashTable *description_map,
                                  gboolean filter_monitor_sources)
{
    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;

    gboolean ok = run_command_capture(command, &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;
    g_free(stderr_data);

    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return;
    }

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (!lines[i][0]) {
            continue;
        }

        gchar **cols = g_strsplit(lines[i], "\t", -1);
        const char *name = NULL;

        if (cols[1] && cols[1][0]) {
            name = cols[1];
        } else {
            static char parsed_name[256];
            parsed_name[0] = '\0';
            if (sscanf(lines[i], "%*s %255s", parsed_name) == 1) {
                name = parsed_name;
            }
        }

        if (name && *name) {
            if (filter_monitor_sources && g_str_has_suffix(name, ".monitor")) {
                g_strfreev(cols);
                continue;
            }

            const char *description = NULL;
            if (description_map) {
                description = g_hash_table_lookup(description_map, name);
            }
            char *label = build_device_label(name, description);
            append_device_name(model, refs, name, label);
            g_free(label);
        }

        g_strfreev(cols);
    }

    g_strfreev(lines);
    g_free(stdout_data);
}

static void refresh_audio_devices(void)
{
    clear_string_list(g_output_model);
    clear_string_list(g_input_model);
    g_ptr_array_set_size(g_output_refs, 0);
    g_ptr_array_set_size(g_input_refs, 0);

    GHashTable *sink_descriptions = NULL;
    GHashTable *source_descriptions = NULL;

    if (command_is_available("pactl")) {
        sink_descriptions = read_pactl_description_map("sh -lc 'timeout 2s pactl list sinks 2>/dev/null'");
        source_descriptions = read_pactl_description_map("sh -lc 'timeout 2s pactl list sources 2>/dev/null'");

        populate_device_model(g_output_model,
                              g_output_refs,
                              "sh -lc 'timeout 2s pactl list short sinks 2>/dev/null'",
                              sink_descriptions,
                              FALSE);
        populate_device_model(g_input_model,
                              g_input_refs,
                              "sh -lc 'timeout 2s pactl list short sources 2>/dev/null'",
                              source_descriptions,
                              TRUE);
    }

    if (g_output_refs->len == 0) {
        append_device_name(g_output_model, g_output_refs, "@DEFAULT_SINK@", _("Default output"));
    }
    if (g_input_refs->len == 0) {
        append_device_name(g_input_model, g_input_refs, "@DEFAULT_SOURCE@", _("Default input"));
    }

    char *default_sink = get_default_from_pactl_info("Default Sink:");
    char *default_source = get_default_from_pactl_info("Default Source:");

    guint output_sel = 0;
    guint input_sel = 0;

    if (default_sink) {
        for (guint i = 0; i < g_output_refs->len; i++) {
            if (g_strcmp0(default_sink, g_ptr_array_index(g_output_refs, i)) == 0) {
                output_sel = i;
                break;
            }
        }
    }

    if (default_source) {
        for (guint i = 0; i < g_input_refs->len; i++) {
            if (g_strcmp0(default_source, g_ptr_array_index(g_input_refs, i)) == 0) {
                input_sel = i;
                break;
            }
        }
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_output_dropdown), output_sel);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_input_dropdown), input_sel);

    if (sink_descriptions) {
        g_hash_table_unref(sink_descriptions);
    }
    if (source_descriptions) {
        g_hash_table_unref(source_descriptions);
    }

    g_free(default_sink);
    g_free(default_source);
}

static int read_runtime_volume_percent(const char *wpctl_target, const char *pactl_command, int fallback)
{
    int value = fallback;

    if (command_is_available("wpctl")) {
        char *stdout_data = NULL;
        char *stderr_data = NULL;
        int wait_status = 0;
        char *cmd = g_strdup_printf("sh -lc 'timeout 2s wpctl get-volume %s 2>/dev/null'", wpctl_target);

        gboolean ok = run_command_capture(cmd, &stdout_data, &stderr_data, &wait_status);
        (void)wait_status;
        g_free(cmd);
        g_free(stderr_data);

        if (ok && stdout_data && *stdout_data) {
            const char *marker = strstr(stdout_data, "Volume:");
            if (marker) {
                marker += strlen("Volume:");
                while (*marker == ' ') {
                    marker++;
                }
                char *end = NULL;
                double raw = g_ascii_strtod(marker, &end);
                if (end != marker) {
                    value = clamp_percent(round_to_int(raw * 100.0));
                    g_free(stdout_data);
                    return value;
                }
            }
        }
        g_free(stdout_data);
    }

    if (command_is_available("pactl")) {
        char *stdout_data = NULL;
        char *stderr_data = NULL;
        int wait_status = 0;
        gboolean ok = run_command_capture(pactl_command, &stdout_data, &stderr_data, &wait_status);
        (void)wait_status;
        g_free(stderr_data);

        if (ok && stdout_data && parse_first_percent(stdout_data, &value)) {
            g_free(stdout_data);
            return value;
        }
        g_free(stdout_data);
    }

    return value;
}

static gboolean has_real_microphone(void)
{
    if (command_is_available("pactl")) {
        char *stdout_data = NULL;
        char *stderr_data = NULL;
        int wait_status = 0;

        gboolean ok = run_command_capture("sh -lc 'timeout 2s pactl list short sources 2>/dev/null'",
                                          &stdout_data,
                                          &stderr_data,
                                          &wait_status);
        (void)wait_status;
        g_free(stderr_data);

        if (!ok || !stdout_data || !*stdout_data) {
            g_free(stdout_data);
            return FALSE;
        }

        gboolean found = FALSE;
        gchar **lines = g_strsplit(stdout_data, "\n", -1);
        for (guint i = 0; lines[i] != NULL; i++) {
            if (!lines[i][0]) {
                continue;
            }

            gchar **cols = g_strsplit(lines[i], "\t", -1);
            const char *name = (cols[1] && cols[1][0]) ? cols[1] : NULL;

            if (name && *name && !g_str_has_suffix(name, ".monitor")) {
                found = TRUE;
                g_strfreev(cols);
                break;
            }

            g_strfreev(cols);
        }

        g_strfreev(lines);
        g_free(stdout_data);
        return found;
    }

    if (!g_input_refs) {
        return FALSE;
    }

    for (guint i = 0; i < g_input_refs->len; i++) {
        const char *name = g_ptr_array_index(g_input_refs, i);
        if (!name || !*name) {
            continue;
        }

        if (g_strcmp0(name, "@DEFAULT_SOURCE@") != 0 && !g_str_has_suffix(name, ".monitor")) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean is_default_source_muted(void)
{
    if (!command_is_available("pactl")) {
        return FALSE;
    }

    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;

    gboolean ok = run_command_capture("sh -lc 'timeout 2s LC_ALL=C pactl get-source-mute @DEFAULT_SOURCE@ 2>/dev/null'",
                                      &stdout_data,
                                      &stderr_data,
                                      &wait_status);
    (void)wait_status;
    g_free(stderr_data);

    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return FALSE;
    }

    gboolean muted = (strstr(stdout_data, "yes") != NULL || strstr(stdout_data, "Yes") != NULL);
    g_free(stdout_data);
    return muted;
}

static int read_default_source_activity_percent(int fallback)
{
    if (!command_is_available("pactl")) {
        return clamp_percent(fallback);
    }

    char *default_source = get_default_from_pactl_info("Default Source:");
    if (!default_source || !*default_source) {
        g_free(default_source);
        return clamp_percent(fallback);
    }

    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;
    gboolean ok = run_command_capture("sh -lc 'timeout 0.6s LC_ALL=C pactl list sources 2>/dev/null'",
                                      &stdout_data,
                                      &stderr_data,
                                      &wait_status);
    (void)wait_status;
    g_free(stderr_data);

    if (!ok || !stdout_data || !*stdout_data) {
        g_free(default_source);
        g_free(stdout_data);
        return clamp_percent(fallback);
    }

    int activity = 0;
    int configured_volume = clamp_percent(fallback);
    gboolean in_target = FALSE;
    gboolean running = FALSE;
    gboolean have_peak = FALSE;

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        char *line = g_strdup(lines[i]);
        g_strstrip(line);

        if (g_str_has_prefix(line, "Source #")) {
            in_target = FALSE;
            running = FALSE;
            g_free(line);
            continue;
        }

        if (g_str_has_prefix(line, "Name:")) {
            const char *name = line + strlen("Name:");
            while (*name == ' ') {
                name++;
            }
            in_target = (g_strcmp0(name, default_source) == 0);
            g_free(line);
            continue;
        }

        if (!in_target) {
            g_free(line);
            continue;
        }

        if (g_str_has_prefix(line, "State:")) {
            const char *state = line + strlen("State:");
            while (*state == ' ') {
                state++;
            }
            running = g_ascii_strcasecmp(state, "RUNNING") == 0;
            g_free(line);
            continue;
        }

        if (g_str_has_prefix(line, "Volume:") && !have_peak) {
            int parsed = configured_volume;
            if (parse_first_percent(line, &parsed)) {
                configured_volume = clamp_percent(parsed);
            }
            g_free(line);
            continue;
        }

        if (strstr(line, "Peak") != NULL || strstr(line, "peak") != NULL) {
            int parsed = 0;
            if (parse_first_percent(line, &parsed)) {
                activity = clamp_percent(parsed);
                have_peak = TRUE;
            }
            g_free(line);
            continue;
        }

        g_free(line);
    }

    g_strfreev(lines);
    g_free(stdout_data);
    g_free(default_source);

    if (have_peak) {
        return activity;
    }

    if (running) {
        int scaled = configured_volume / 2;
        if (scaled < 6) {
            scaled = 6;
        }
        return clamp_percent(scaled);
    }

    return 0;
}

static void refresh_microphone_activity(void)
{
    if (!g_mic_activity_bar || !g_mic_activity_label) {
        return;
    }

    if (!has_real_microphone()) {
        gtk_level_bar_set_value(GTK_LEVEL_BAR(g_mic_activity_bar), 0);
        gtk_label_set_text(GTK_LABEL(g_mic_activity_label), _("No microphone detected"));
        return;
    }

    if (is_default_source_muted()) {
        gtk_level_bar_set_value(GTK_LEVEL_BAR(g_mic_activity_bar), 0);
        gtk_label_set_text(GTK_LABEL(g_mic_activity_label), _("Microphone detected (muted)"));
        return;
    }

    int fallback = g_mic_scale ? (int)gtk_range_get_value(GTK_RANGE(g_mic_scale)) : 0;
    int level = read_default_source_activity_percent(fallback);

    gtk_level_bar_set_value(GTK_LEVEL_BAR(g_mic_activity_bar), level);

    char *status = g_strdup_printf(_("Microphone detected (%d%%)"), level);
    gtk_label_set_text(GTK_LABEL(g_mic_activity_label), status);
    g_free(status);
}

static gboolean on_microphone_activity_tick(gpointer data)
{
    (void)data;
    refresh_microphone_activity();
    return G_SOURCE_CONTINUE;
}

static void save_audio_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_integer(kf, "audio", "volume", (int)gtk_range_get_value(GTK_RANGE(g_volume_scale)));
    g_key_file_set_integer(kf, "audio", "microphone", (int)gtk_range_get_value(GTK_RANGE(g_mic_scale)));
    g_key_file_set_integer(kf, "audio", "balance", (int)gtk_range_get_value(GTK_RANGE(g_balance_scale)));
    g_key_file_set_integer(kf, "audio", "profile_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_profile_dropdown)));
    g_key_file_set_integer(kf, "audio", "mixing_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_mixing_dropdown)));
    g_key_file_set_integer(kf, "audio", "routing_idx", (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(g_routing_dropdown)));
    g_key_file_set_boolean(kf, "audio", "bluetooth_audio", gtk_switch_get_active(GTK_SWITCH(g_bluetooth_switch)));
    g_key_file_set_boolean(kf, "audio", "effects", gtk_switch_get_active(GTK_SWITCH(g_effects_switch)));

    const char *output = selected_output_name();
    if (output) {
        g_key_file_set_string(kf, "audio", "output_device", output);
    }

    const char *input = selected_input_name();
    if (input) {
        g_key_file_set_string(kf, "audio", "input_device", input);
    }

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = audio_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_audio_config(void)
{
    char *path = audio_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    int volume = g_key_file_get_integer(kf, "audio", "volume", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        gtk_range_set_value(GTK_RANGE(g_volume_scale), clamp_percent(volume));
    }

    int microphone = g_key_file_get_integer(kf, "audio", "microphone", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        gtk_range_set_value(GTK_RANGE(g_mic_scale), clamp_percent(microphone));
    }

    int balance = g_key_file_get_integer(kf, "audio", "balance", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        if (balance < -100) {
            balance = -100;
        }
        if (balance > 100) {
            balance = 100;
        }
        gtk_range_set_value(GTK_RANGE(g_balance_scale), balance);
    }

    int profile_idx = g_key_file_get_integer(kf, "audio", "profile_idx", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        if (profile_idx < 0 || profile_idx >= (int)G_N_ELEMENTS(g_profile_options)) {
            profile_idx = 1;
        }
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_profile_dropdown), (guint)profile_idx);
    }

    int mixing_idx = g_key_file_get_integer(kf, "audio", "mixing_idx", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        if (mixing_idx < 0 || mixing_idx >= (int)G_N_ELEMENTS(g_mixing_options)) {
            mixing_idx = 0;
        }
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mixing_dropdown), (guint)mixing_idx);
    }

    int routing_idx = g_key_file_get_integer(kf, "audio", "routing_idx", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        if (routing_idx < 0 || routing_idx >= (int)G_N_ELEMENTS(g_routing_options)) {
            routing_idx = 0;
        }
        gtk_drop_down_set_selected(GTK_DROP_DOWN(g_routing_dropdown), (guint)routing_idx);
    }

    gboolean bt_audio = g_key_file_get_boolean(kf, "audio", "bluetooth_audio", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        gtk_switch_set_active(GTK_SWITCH(g_bluetooth_switch), bt_audio);
    }

    gboolean effects = g_key_file_get_boolean(kf, "audio", "effects", &error);
    if (error) {
        g_clear_error(&error);
    } else {
        gtk_switch_set_active(GTK_SWITCH(g_effects_switch), effects);
    }

    char *output = g_key_file_get_string(kf, "audio", "output_device", &error);
    if (!error && output) {
        for (guint i = 0; i < g_output_refs->len; i++) {
            if (g_strcmp0(output, g_ptr_array_index(g_output_refs, i)) == 0) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(g_output_dropdown), i);
                break;
            }
        }
    }
    g_free(output);
    g_clear_error(&error);

    char *input = g_key_file_get_string(kf, "audio", "input_device", &error);
    if (!error && input) {
        for (guint i = 0; i < g_input_refs->len; i++) {
            if (g_strcmp0(input, g_ptr_array_index(g_input_refs, i)) == 0) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(g_input_dropdown), i);
                break;
            }
        }
    }
    g_free(input);
    g_clear_error(&error);

    g_key_file_unref(kf);
    g_free(path);
}

static char *read_first_card_name(void)
{
    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;

    gboolean ok = run_command_capture("sh -lc 'timeout 2s pactl list short cards 2>/dev/null'",
                                      &stdout_data,
                                      &stderr_data,
                                      &wait_status);
    (void)wait_status;
    g_free(stderr_data);

    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return NULL;
    }

    char *result = NULL;
    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (!lines[i][0]) {
            continue;
        }

        gchar **cols = g_strsplit(lines[i], "\t", -1);
        if (cols[1] && cols[1][0]) {
            result = g_strdup(cols[1]);
            g_strfreev(cols);
            break;
        }

        g_strfreev(cols);
    }

    g_strfreev(lines);
    g_free(stdout_data);
    return result;
}

static gboolean set_default_sink_by_pattern(const char *pattern)
{
    if (!pattern || !*pattern || !g_output_refs || !command_is_available("pactl")) {
        return FALSE;
    }

    for (guint i = 0; i < g_output_refs->len; i++) {
        const char *name = g_ptr_array_index(g_output_refs, i);
        if (!name || !g_strrstr(name, pattern)) {
            continue;
        }

        char *quoted = g_shell_quote(name);
        char *cmd = g_strdup_printf("sh -lc 'timeout 2s pactl set-default-sink %s >/dev/null 2>&1 || true'", quoted);
        gboolean ok = run_command_async(cmd);
        g_free(cmd);
        g_free(quoted);
        return ok;
    }

    return FALSE;
}

static char *apply_runtime_audio(void)
{
    int volume = clamp_percent((int)gtk_range_get_value(GTK_RANGE(g_volume_scale)));
    int mic = clamp_percent((int)gtk_range_get_value(GTK_RANGE(g_mic_scale)));
    int balance = (int)gtk_range_get_value(GTK_RANGE(g_balance_scale));

    const char *output = selected_output_name();
    const char *input = selected_input_name();

    guint profile_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_profile_dropdown));
    if (profile_idx >= G_N_ELEMENTS(g_profile_options)) {
        profile_idx = 1;
    }

    guint routing_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_routing_dropdown));
    if (routing_idx >= G_N_ELEMENTS(g_routing_options)) {
        routing_idx = 0;
    }

    gboolean bluetooth_audio = gtk_switch_get_active(GTK_SWITCH(g_bluetooth_switch));
    gboolean effects = gtk_switch_get_active(GTK_SWITCH(g_effects_switch));

    gboolean volume_ok = FALSE;
    gboolean mic_ok = FALSE;

    if (command_is_available("wpctl")) {
        char *set_volume = g_strdup_printf("sh -lc 'timeout 2s wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%% >/dev/null 2>&1'", volume);
        char *set_mic = g_strdup_printf("sh -lc 'timeout 2s wpctl set-volume @DEFAULT_AUDIO_SOURCE@ %d%% >/dev/null 2>&1'", mic);
        volume_ok = run_command_success(set_volume);
        mic_ok = run_command_success(set_mic);
        g_free(set_volume);
        g_free(set_mic);
    } else if (command_is_available("pactl")) {
        char *set_volume = g_strdup_printf("sh -lc 'timeout 2s pactl set-sink-volume @DEFAULT_SINK@ %d%% >/dev/null 2>&1'", volume);
        char *set_mic = g_strdup_printf("sh -lc 'timeout 2s pactl set-source-volume @DEFAULT_SOURCE@ %d%% >/dev/null 2>&1'", mic);
        volume_ok = run_command_success(set_volume);
        mic_ok = run_command_success(set_mic);
        g_free(set_volume);
        g_free(set_mic);
    }

    gboolean balance_ok = TRUE;
    if (command_is_available("pactl")) {
        double b = (double)balance / 100.0;
        double left_factor = (b > 0.0) ? (1.0 - b) : 1.0;
        double right_factor = (b < 0.0) ? (1.0 + b) : 1.0;

        int left = clamp_percent(round_to_int((double)volume * left_factor));
        int right = clamp_percent(round_to_int((double)volume * right_factor));

        char *balance_cmd = g_strdup_printf(
            "sh -lc 'timeout 2s pactl set-sink-volume @DEFAULT_SINK@ %d%% %d%% >/dev/null 2>&1'",
            left,
            right);
        balance_ok = run_command_success(balance_cmd);
        g_free(balance_cmd);
    }

    gboolean output_ok = TRUE;
    if (output && command_is_available("pactl")) {
        char *quoted = g_shell_quote(output);
        char *cmd = g_strdup_printf("sh -lc 'timeout 2s pactl set-default-sink %s >/dev/null 2>&1 || true'", quoted);
        output_ok = run_command_async(cmd);
        g_free(cmd);
        g_free(quoted);
    }

    gboolean input_ok = TRUE;
    if (input && command_is_available("pactl")) {
        char *quoted = g_shell_quote(input);
        char *cmd = g_strdup_printf("sh -lc 'timeout 2s pactl set-default-source %s >/dev/null 2>&1 || true'", quoted);
        input_ok = run_command_async(cmd);
        g_free(cmd);
        g_free(quoted);
    }

    gboolean profile_ok = TRUE;
    if (command_is_available("pactl")) {
        char *card_name = read_first_card_name();
        if (card_name) {
            char *quoted = g_shell_quote(card_name);
            char *cmd = g_strdup_printf("sh -lc 'timeout 2s pactl set-card-profile %s %s >/dev/null 2>&1 || true'",
                                        quoted,
                                        g_profile_options[profile_idx].value);
            profile_ok = run_command_async(cmd);
            g_free(cmd);
            g_free(quoted);
            g_free(card_name);
        }
    }

    gboolean routing_ok = TRUE;
    if (routing_idx > 0 && command_is_available("pactl")) {
        const char *pattern = NULL;

        if (g_strcmp0(g_routing_options[routing_idx].value, "speakers") == 0) {
            pattern = "analog-stereo";
        } else if (g_strcmp0(g_routing_options[routing_idx].value, "headphones") == 0) {
            pattern = "headphones";
        } else if (g_strcmp0(g_routing_options[routing_idx].value, "bluetooth") == 0) {
            pattern = "bluez";
        }

        if (pattern) {
            routing_ok = set_default_sink_by_pattern(pattern);
        }
    }

    gboolean bluetooth_ok = TRUE;
    if (bluetooth_audio && command_is_available("pactl")) {
        bluetooth_ok = set_default_sink_by_pattern("bluez");
    }

    gboolean effects_ok = TRUE;
    if (command_is_available("gsettings")) {
        const char *value = effects ? "true" : "false";
        char *cmd = g_strdup_printf(
            "sh -lc 'timeout 2s gsettings set org.gnome.desktop.sound event-sounds %s >/dev/null 2>&1 || true'",
            value);
        effects_ok = run_command_async(cmd);
        g_free(cmd);
    }

    GString *issues = g_string_new(NULL);

    if (!volume_ok || !mic_ok) {
        g_string_append(issues, _("Could not apply volume or microphone level."));
    }

    if (!balance_ok) {
        if (issues->len > 0) {
            g_string_append(issues, " ");
        }
        g_string_append(issues, _("Could not apply audio balance."));
    }

    if (!output_ok || !input_ok || !profile_ok || !routing_ok || !bluetooth_ok || !effects_ok) {
        if (issues->len > 0) {
            g_string_append(issues, " ");
        }
        g_string_append(issues, _("Some advanced audio options were queued with limited backend support."));
    }

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static char *apply_runtime_audio_levels_only(void)
{
    int volume = clamp_percent((int)gtk_range_get_value(GTK_RANGE(g_volume_scale)));
    int mic = clamp_percent((int)gtk_range_get_value(GTK_RANGE(g_mic_scale)));
    int balance = (int)gtk_range_get_value(GTK_RANGE(g_balance_scale));

    gboolean volume_ok = FALSE;
    gboolean mic_ok = FALSE;

    if (command_is_available("wpctl")) {
        char *set_volume = g_strdup_printf("sh -lc 'timeout 2s wpctl set-volume @DEFAULT_AUDIO_SINK@ %d%% >/dev/null 2>&1'", volume);
        char *set_mic = g_strdup_printf("sh -lc 'timeout 2s wpctl set-volume @DEFAULT_AUDIO_SOURCE@ %d%% >/dev/null 2>&1'", mic);
        volume_ok = run_command_success(set_volume);
        mic_ok = run_command_success(set_mic);
        g_free(set_volume);
        g_free(set_mic);
    } else if (command_is_available("pactl")) {
        char *set_volume = g_strdup_printf("sh -lc 'timeout 2s pactl set-sink-volume @DEFAULT_SINK@ %d%% >/dev/null 2>&1'", volume);
        char *set_mic = g_strdup_printf("sh -lc 'timeout 2s pactl set-source-volume @DEFAULT_SOURCE@ %d%% >/dev/null 2>&1'", mic);
        volume_ok = run_command_success(set_volume);
        mic_ok = run_command_success(set_mic);
        g_free(set_volume);
        g_free(set_mic);
    }

    gboolean balance_ok = TRUE;
    if (command_is_available("pactl")) {
        double b = (double)balance / 100.0;
        double left_factor = (b > 0.0) ? (1.0 - b) : 1.0;
        double right_factor = (b < 0.0) ? (1.0 + b) : 1.0;

        int left = clamp_percent(round_to_int((double)volume * left_factor));
        int right = clamp_percent(round_to_int((double)volume * right_factor));

        char *balance_cmd = g_strdup_printf(
            "sh -lc 'timeout 2s pactl set-sink-volume @DEFAULT_SINK@ %d%% %d%% >/dev/null 2>&1'",
            left,
            right);
        balance_ok = run_command_success(balance_cmd);
        g_free(balance_cmd);
    }

    GString *issues = g_string_new(NULL);
    if (!volume_ok || !mic_ok) {
        g_string_append(issues, _("Could not apply volume or microphone level."));
    }
    if (!balance_ok) {
        if (issues->len > 0) {
            g_string_append(issues, " ");
        }
        g_string_append(issues, _("Could not apply audio balance."));
    }

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_refresh_devices_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    refresh_audio_devices();
    refresh_microphone_activity();
    status_set(_("Audio devices refreshed"), FALSE);
}

static void on_apply_audio_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    char *issues = apply_runtime_audio();
    save_audio_config();

    if (issues && *issues) {
        char *message = g_strdup_printf(_("Audio settings saved. %s"), issues);
        status_set(message, TRUE);
        g_free(message);
    } else {
        status_set(_("Audio settings applied"), FALSE);
    }

    refresh_microphone_activity();

    g_free(issues);
}

static void on_audio_control_changed(GObject *obj, GParamSpec *pspec, gpointer data)
{
    (void)obj;
    (void)pspec;
    (void)data;
    schedule_auto_apply_audio(AUDIO_APPLY_FULL);
}

static void on_audio_scale_changed(GtkRange *range, gpointer data)
{
    (void)range;
    (void)data;
    schedule_auto_apply_audio(AUDIO_APPLY_LEVELS_ONLY);
}

static gboolean auto_apply_audio_timeout(gpointer data)
{
    (void)data;
    g_auto_apply_timeout_id = 0;

    guint apply_mask = g_pending_apply_mask;
    g_pending_apply_mask = 0;

    char *issues = NULL;
    if (apply_mask & AUDIO_APPLY_FULL) {
        issues = apply_runtime_audio();
    } else {
        issues = apply_runtime_audio_levels_only();
    }
    save_audio_config();

    if (issues && *issues) {
        char *message = g_strdup_printf(_("Audio auto-saved. %s"), issues);
        status_set(message, TRUE);
        g_free(message);
    } else {
        status_set(_("Audio settings auto-saved"), FALSE);
    }

    refresh_microphone_activity();
    g_free(issues);
    return G_SOURCE_REMOVE;
}

static void on_audio_page_destroy(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    if (g_mic_activity_timer_id) {
        g_source_remove(g_mic_activity_timer_id);
        g_mic_activity_timer_id = 0;
    }
    if (g_auto_apply_timeout_id) {
        g_source_remove(g_auto_apply_timeout_id);
        g_auto_apply_timeout_id = 0;
    }
    g_pending_apply_mask = 0;
}

GtkWidget *page_audio_new(void)
{
    if (!g_output_refs) {
        g_output_refs = g_ptr_array_new_with_free_func(g_free);
    }
    if (!g_input_refs) {
        g_input_refs = g_ptr_array_new_with_free_func(g_free);
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

    GtkWidget *title = gtk_label_new(_("Sound and multimedia"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Configure volume, devices, microphone, balance, profiles, bluetooth audio and routing."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *levels_frame = create_section(_("Volume and capture"),
                                             _("Set master volume, microphone level and left/right balance."));
    GtkWidget *levels_box = gtk_frame_get_child(GTK_FRAME(levels_frame));

    g_volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_volume_scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g_volume_scale), 0);
    g_signal_connect(g_volume_scale, "value-changed", G_CALLBACK(on_audio_scale_changed), NULL);

    g_mic_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_mic_scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g_mic_scale), 0);
    g_signal_connect(g_mic_scale, "value-changed", G_CALLBACK(on_audio_scale_changed), NULL);

    g_mic_activity_bar = gtk_level_bar_new_for_interval(0, 150);
    gtk_widget_set_hexpand(g_mic_activity_bar, TRUE);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(g_mic_activity_bar), 0);

    g_mic_activity_label = gtk_label_new(_("No microphone detected"));
    gtk_widget_set_halign(g_mic_activity_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_mic_activity_label, "row-subtitle");

    g_balance_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -100, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(g_balance_scale), TRUE);
    gtk_scale_set_digits(GTK_SCALE(g_balance_scale), 0);
    g_signal_connect(g_balance_scale, "value-changed", G_CALLBACK(on_audio_scale_changed), NULL);

    gtk_box_append(GTK_BOX(levels_box), create_slider_row(_("Volume"), _("Master output level"), g_volume_scale));
    gtk_box_append(GTK_BOX(levels_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(levels_box), create_slider_row(_("Microphone"), _("Input gain level"), g_mic_scale));
    gtk_box_append(GTK_BOX(levels_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(levels_box), create_slider_row(_("Microphone activity"), _("Live microphone input detection"), g_mic_activity_bar));
    gtk_box_append(GTK_BOX(levels_box), g_mic_activity_label);
    gtk_box_append(GTK_BOX(levels_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(levels_box), create_slider_row(_("Audio balance"), _("-100 left / +100 right"), g_balance_scale));

    gtk_box_append(GTK_BOX(box), levels_frame);

    GtkWidget *devices_frame = create_section(_("Audio devices"),
                                              _("Choose default output and input devices."));
    GtkWidget *devices_box = gtk_frame_get_child(GTK_FRAME(devices_frame));

    g_output_model = gtk_string_list_new(NULL);
    g_input_model = gtk_string_list_new(NULL);

    g_output_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_output_model), NULL);
    g_input_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_input_model), NULL);
    g_signal_connect(g_output_dropdown, "notify::selected", G_CALLBACK(on_audio_control_changed), NULL);
    g_signal_connect(g_input_dropdown, "notify::selected", G_CALLBACK(on_audio_control_changed), NULL);

    gtk_box_append(GTK_BOX(devices_box), create_row(_("Output device"), g_output_dropdown));
    gtk_box_append(GTK_BOX(devices_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(devices_box), create_row(_("Input device"), g_input_dropdown));

    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh audio devices"));
    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_devices_clicked), NULL);
    gtk_box_append(GTK_BOX(devices_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(devices_box), refresh_btn);

    gtk_box_append(GTK_BOX(box), devices_frame);

    GtkWidget *advanced_frame = create_section(_("Profiles, effects and routing"),
                                               _("Tune profile, bluetooth behavior, system effects, mixing and routing policy."));
    GtkWidget *advanced_box = gtk_frame_get_child(GTK_FRAME(advanced_frame));

    GtkStringList *profile_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_profile_options); i++) {
        gtk_string_list_append(profile_model, _(g_profile_options[i].label));
    }
    g_profile_dropdown = gtk_drop_down_new(G_LIST_MODEL(profile_model), NULL);
    g_signal_connect(g_profile_dropdown, "notify::selected", G_CALLBACK(on_audio_control_changed), NULL);
    g_object_unref(profile_model);

    GtkStringList *mixing_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_mixing_options); i++) {
        gtk_string_list_append(mixing_model, _(g_mixing_options[i].label));
    }
    g_mixing_dropdown = gtk_drop_down_new(G_LIST_MODEL(mixing_model), NULL);
    g_signal_connect(g_mixing_dropdown, "notify::selected", G_CALLBACK(on_audio_control_changed), NULL);
    g_object_unref(mixing_model);

    GtkStringList *routing_model = gtk_string_list_new(NULL);
    for (guint i = 0; i < G_N_ELEMENTS(g_routing_options); i++) {
        gtk_string_list_append(routing_model, _(g_routing_options[i].label));
    }
    g_routing_dropdown = gtk_drop_down_new(G_LIST_MODEL(routing_model), NULL);
    g_signal_connect(g_routing_dropdown, "notify::selected", G_CALLBACK(on_audio_control_changed), NULL);
    g_object_unref(routing_model);

    g_bluetooth_switch = gtk_switch_new();
    g_effects_switch = gtk_switch_new();
    g_signal_connect(g_bluetooth_switch, "notify::active", G_CALLBACK(on_audio_control_changed), NULL);
    g_signal_connect(g_effects_switch, "notify::active", G_CALLBACK(on_audio_control_changed), NULL);

    gtk_box_append(GTK_BOX(advanced_box), create_row(_("Audio profile"), g_profile_dropdown));
    gtk_box_append(GTK_BOX(advanced_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(advanced_box), create_row(_("Bluetooth audio"), g_bluetooth_switch));
    gtk_box_append(GTK_BOX(advanced_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(advanced_box), create_row(_("System sound effects"), g_effects_switch));
    gtk_box_append(GTK_BOX(advanced_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(advanced_box), create_row(_("Mixing"), g_mixing_dropdown));
    gtk_box_append(GTK_BOX(advanced_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(advanced_box), create_row(_("Routing"), g_routing_dropdown));

    gtk_box_append(GTK_BOX(box), advanced_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply audio settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_audio_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);
    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    g_block_runtime_handlers = TRUE;
    gtk_range_set_value(GTK_RANGE(g_volume_scale), read_runtime_volume_percent("@DEFAULT_AUDIO_SINK@",
                                                                               "sh -lc 'timeout 2s pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null'",
                                                                               70));
    gtk_range_set_value(GTK_RANGE(g_mic_scale), read_runtime_volume_percent("@DEFAULT_AUDIO_SOURCE@",
                                                                            "sh -lc 'timeout 2s pactl get-source-volume @DEFAULT_SOURCE@ 2>/dev/null'",
                                                                            70));
    gtk_range_set_value(GTK_RANGE(g_balance_scale), 0);
    gtk_switch_set_active(GTK_SWITCH(g_bluetooth_switch), FALSE);
    gtk_switch_set_active(GTK_SWITCH(g_effects_switch), TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_profile_dropdown), 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_mixing_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_routing_dropdown), 0);
    g_block_runtime_handlers = FALSE;

    g_block_runtime_handlers = TRUE;
    refresh_audio_devices();
    load_audio_config();
    g_block_runtime_handlers = FALSE;
    refresh_microphone_activity();

    if (g_mic_activity_timer_id == 0) {
        g_mic_activity_timer_id = g_timeout_add_seconds(1, on_microphone_activity_tick, NULL);
    }

    g_signal_connect(box, "destroy", G_CALLBACK(on_audio_page_destroy), NULL);

    return outer_scroll;
}
