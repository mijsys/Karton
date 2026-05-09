// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include "backend/system.h"

#include <ctype.h>
#include <libintl.h>
#include <pwd.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "backend/config.h"

#define _(s) gettext(s)

typedef struct {
    gchar *desktop_id;
    gchar *display_name;
    gchar *exec_line;
    gchar *comment;
    gchar *system_path;
    gchar *user_path;
    gboolean enabled;
} KartonAutostartEntry;

static void
autostart_entry_free(KartonAutostartEntry *entry)
{
    if (!entry) {
        return;
    }
    g_free(entry->desktop_id);
    g_free(entry->display_name);
    g_free(entry->exec_line);
    g_free(entry->comment);
    g_free(entry->system_path);
    g_free(entry->user_path);
    g_free(entry);
}

static gint
autostart_entry_compare(gconstpointer a, gconstpointer b)
{
    const KartonAutostartEntry *entry_a = *(const KartonAutostartEntry * const *)a;
    const KartonAutostartEntry *entry_b = *(const KartonAutostartEntry * const *)b;
    const char *name_a = entry_a->display_name ? entry_a->display_name : entry_a->desktop_id;
    const char *name_b = entry_b->display_name ? entry_b->display_name : entry_b->desktop_id;
    return g_ascii_strcasecmp(name_a, name_b);
}

static gboolean
desktop_file_read_metadata(const char *path,
    gchar **name_out,
    gchar **exec_out,
    gchar **comment_out,
    gboolean *hidden_out)
{
    if (name_out) {
        *name_out = NULL;
    }
    if (exec_out) {
        *exec_out = NULL;
    }
    if (comment_out) {
        *comment_out = NULL;
    }
    if (hidden_out) {
        *hidden_out = FALSE;
    }
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return FALSE;
    }

    GKeyFile *key_file = g_key_file_new();
    gboolean ok = g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);
    if (!ok) {
        g_key_file_free(key_file);
        return FALSE;
    }

    if (name_out && g_key_file_has_key(key_file, "Desktop Entry", "Name", NULL)) {
        *name_out = g_key_file_get_locale_string(key_file, "Desktop Entry", "Name", NULL, NULL);
    }
    if (exec_out && g_key_file_has_key(key_file, "Desktop Entry", "Exec", NULL)) {
        *exec_out = g_key_file_get_string(key_file, "Desktop Entry", "Exec", NULL);
    }
    if (comment_out && g_key_file_has_key(key_file, "Desktop Entry", "Comment", NULL)) {
        *comment_out = g_key_file_get_locale_string(key_file, "Desktop Entry", "Comment", NULL, NULL);
    }
    if (hidden_out && g_key_file_has_key(key_file, "Desktop Entry", "Hidden", NULL)) {
        *hidden_out = g_key_file_get_boolean(key_file, "Desktop Entry", "Hidden", NULL);
    }

    g_key_file_free(key_file);
    return TRUE;
}

static GPtrArray *
collect_autostart_entries(void)
{
    GHashTable *entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)autostart_entry_free);
    const char *system_dirs[] = { "/etc/xdg/autostart", NULL };

    for (guint dir_index = 0; system_dirs[dir_index]; dir_index++) {
        GDir *dir = g_dir_open(system_dirs[dir_index], 0, NULL);
        if (!dir) {
            continue;
        }
        const gchar *name = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (!g_str_has_suffix(name, ".desktop")) {
                continue;
            }
            KartonAutostartEntry *entry = g_hash_table_lookup(entries, name);
            if (!entry) {
                entry = g_new0(KartonAutostartEntry, 1);
                entry->desktop_id = g_strdup(name);
                g_hash_table_insert(entries, g_strdup(name), entry);
            }
            g_free(entry->system_path);
            entry->system_path = g_build_filename(system_dirs[dir_index], name, NULL);
        }
        g_dir_close(dir);
    }

    gchar *user_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    GDir *dir = g_dir_open(user_dir, 0, NULL);
    if (dir) {
        const gchar *name = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (!g_str_has_suffix(name, ".desktop")) {
                continue;
            }
            KartonAutostartEntry *entry = g_hash_table_lookup(entries, name);
            if (!entry) {
                entry = g_new0(KartonAutostartEntry, 1);
                entry->desktop_id = g_strdup(name);
                g_hash_table_insert(entries, g_strdup(name), entry);
            }
            g_free(entry->user_path);
            entry->user_path = g_build_filename(user_dir, name, NULL);
        }
        g_dir_close(dir);
    }
    g_free(user_dir);

    GPtrArray *list = g_ptr_array_new_with_free_func((GDestroyNotify)autostart_entry_free);
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, entries);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        KartonAutostartEntry *entry = value;
        gboolean hidden = FALSE;
        gchar *name = NULL;
        gchar *exec_line = NULL;
        gchar *comment = NULL;

        if (entry->user_path) {
            desktop_file_read_metadata(entry->user_path, &name, &exec_line, &comment, &hidden);
            entry->enabled = !hidden;
        } else if (entry->system_path) {
            desktop_file_read_metadata(entry->system_path, &name, &exec_line, &comment, &hidden);
            entry->enabled = !hidden;
        }

        entry->display_name = name ? name : g_strdup(entry->desktop_id);
        entry->exec_line = exec_line;
        entry->comment = comment;
        g_ptr_array_add(list, entry);
        g_hash_table_iter_steal(&iter);
    }

    g_hash_table_destroy(entries);
    g_ptr_array_sort(list, autostart_entry_compare);
    return list;
}

static KartonAutostartEntry *
find_autostart_entry(const char *desktop_id)
{
    GPtrArray *entries = collect_autostart_entries();
    KartonAutostartEntry *found = NULL;
    for (guint i = 0; i < entries->len; i++) {
        KartonAutostartEntry *entry = g_ptr_array_index(entries, i);
        if (g_strcmp0(entry->desktop_id, desktop_id) == 0) {
            found = g_new0(KartonAutostartEntry, 1);
            found->desktop_id = g_strdup(entry->desktop_id);
            found->display_name = g_strdup(entry->display_name);
            found->exec_line = g_strdup(entry->exec_line);
            found->comment = g_strdup(entry->comment);
            found->system_path = g_strdup(entry->system_path);
            found->user_path = g_strdup(entry->user_path);
            found->enabled = entry->enabled;
            break;
        }
    }
    g_ptr_array_free(entries, TRUE);
    return found;
}

static gboolean
run_command_capture(const char *command, gchar **stdout_out, gchar **stderr_out, gint *status_out)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gint status = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync(command, &out, &err, &status, &error);
    if (!ok) {
        if (stderr_out) {
            *stderr_out = g_strdup(error ? error->message : "Command failed");
        }
        g_clear_error(&error);
        g_free(out);
        g_free(err);
        return FALSE;
    }

    if (stdout_out) {
        *stdout_out = out;
    } else {
        g_free(out);
    }
    if (stderr_out) {
        *stderr_out = err;
    } else {
        g_free(err);
    }
    if (status_out) {
        *status_out = status;
    }
    return TRUE;
}

static gboolean
run_command_ok(const char *command, gchar **error_msg)
{
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint status = 0;

    gboolean ok = run_command_capture(command, &stdout_text, &stderr_text, &status);
    g_free(stdout_text);
    if (!ok || status != 0) {
        if (error_msg) {
            *error_msg = g_strdup(stderr_text && *stderr_text ? stderr_text : "Command returned non-zero status");
        }
        g_free(stderr_text);
        return FALSE;
    }

    g_free(stderr_text);
    return TRUE;
}

static gchar *
first_line_trimmed(const gchar *text)
{
    if (!text) {
        return g_strdup("");
    }

    gchar *copy = g_strdup(text);
    g_strstrip(copy);
    gchar *newline = strpbrk(copy, "\r\n");
    if (newline) {
        *newline = '\0';
    }
    return copy;
}

static gboolean
text_has_true_value(const gchar *text)
{
    if (!text) {
        return FALSE;
    }
    return strstr(text, "true") || strstr(text, "enabled") || strstr(text, "on") || strstr(text, "yes");
}

static double
first_number_in_text(const gchar *text)
{
    if (!text) {
        return -1.0;
    }

    const unsigned char *ptr = (const unsigned char *)text;
    while (*ptr && !isdigit(*ptr)) {
        ptr++;
    }
    if (!*ptr) {
        return -1.0;
    }

    return g_ascii_strtod((const gchar *)ptr, NULL);
}

static gboolean
gsettings_get_bool(const char *schema, const char *key, gboolean *out)
{
    gchar *cmd = g_strdup_printf("gsettings get %s %s", schema, key);
    gchar *stdout_text = NULL;
    gint status = 0;
    gboolean ok = run_command_capture(cmd, &stdout_text, NULL, &status);
    g_free(cmd);
    if (!ok || status != 0) {
        g_free(stdout_text);
        return FALSE;
    }

    gchar *line = first_line_trimmed(stdout_text);
    *out = text_has_true_value(line);
    g_free(line);
    g_free(stdout_text);
    return TRUE;
}

static gboolean
gsettings_set_bool(const char *schema, const char *key, gboolean value, gchar **error_msg)
{
    gchar *cmd = g_strdup_printf("gsettings set %s %s %s", schema, key, value ? "true" : "false");
    gboolean ok = run_command_ok(cmd, error_msg);
    g_free(cmd);
    return ok;
}

static gboolean
gsettings_get_double(const char *schema, const char *key, double *out)
{
    gchar *cmd = g_strdup_printf("gsettings get %s %s", schema, key);
    gchar *stdout_text = NULL;
    gint status = 0;
    gboolean ok = run_command_capture(cmd, &stdout_text, NULL, &status);
    g_free(cmd);
    if (!ok || status != 0) {
        g_free(stdout_text);
        return FALSE;
    }

    gchar *line = first_line_trimmed(stdout_text);
    *out = g_ascii_strtod(line, NULL);
    g_free(line);
    g_free(stdout_text);
    return TRUE;
}

static gboolean
gsettings_set_double(const char *schema, const char *key, double value, gchar **error_msg)
{
    gchar *cmd = g_strdup_printf("gsettings set %s %s %.2f", schema, key, value);
    gboolean ok = run_command_ok(cmd, error_msg);
    g_free(cmd);
    return ok;
}

static gboolean
read_ull_file(const char *path, guint64 *value_out)
{
    if (!path || !value_out) {
        return FALSE;
    }

    gchar *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL) || !content) {
        g_free(content);
        return FALSE;
    }

    g_strstrip(content);
    *value_out = g_ascii_strtoull(content, NULL, 10);
    g_free(content);
    return TRUE;
}

static gboolean
backlight_find_device(gchar **device_path_out)
{
    if (!device_path_out) {
        return FALSE;
    }
    *device_path_out = NULL;

    GDir *dir = g_dir_open("/sys/class/backlight", 0, NULL);
    if (!dir) {
        return FALSE;
    }

    const gchar *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (name[0] == '.') {
            continue;
        }
        *device_path_out = g_build_filename("/sys/class/backlight", name, NULL);
        g_dir_close(dir);
        return TRUE;
    }

    g_dir_close(dir);
    return FALSE;
}

static gboolean
backlight_get_percent(double *percent_out)
{
    if (!percent_out) {
        return FALSE;
    }

    gchar *device_path = NULL;
    if (!backlight_find_device(&device_path)) {
        return FALSE;
    }

    gchar *brightness_path = g_build_filename(device_path, "brightness", NULL);
    gchar *max_path = g_build_filename(device_path, "max_brightness", NULL);
    guint64 current = 0;
    guint64 max = 0;
    gboolean ok = FALSE;

    if (read_ull_file(brightness_path, &current) && read_ull_file(max_path, &max) && max > 0) {
        *percent_out = CLAMP(((double)current * 100.0) / (double)max, 0.0, 100.0);
        ok = TRUE;
    }

    g_free(brightness_path);
    g_free(max_path);
    g_free(device_path);
    return ok;
}

static gboolean
backlight_set_percent(double percent)
{
    gchar *device_path = NULL;
    if (!backlight_find_device(&device_path)) {
        return FALSE;
    }

    gchar *brightness_path = g_build_filename(device_path, "brightness", NULL);
    gchar *max_path = g_build_filename(device_path, "max_brightness", NULL);
    guint64 max = 0;
    gboolean ok = FALSE;

    if (read_ull_file(max_path, &max) && max > 0) {
        double clamped = CLAMP(percent, 1.0, 100.0);
        guint64 raw = (guint64)((clamped * (double)max) / 100.0 + 0.5);
        if (raw == 0 && clamped > 0.0) {
            raw = 1;
        }
        gchar *content = g_strdup_printf("%llu\n", (unsigned long long)MIN(raw, max));
        ok = g_file_set_contents(brightness_path, content, -1, NULL);
        g_free(content);
    }

    g_free(brightness_path);
    g_free(max_path);
    g_free(device_path);
    return ok;
}

static gboolean
brightnessctl_get_percent(double *percent_out)
{
    gchar *current_text = NULL;
    gchar *max_text = NULL;
    gint current_status = 0;
    gint max_status = 0;

    gboolean ok = run_command_capture("brightnessctl g", &current_text, NULL, &current_status)
        && run_command_capture("brightnessctl m", &max_text, NULL, &max_status)
        && current_status == 0
        && max_status == 0;
    if (!ok) {
        g_free(current_text);
        g_free(max_text);
        return FALSE;
    }

    double current = first_number_in_text(current_text);
    double max = first_number_in_text(max_text);
    g_free(current_text);
    g_free(max_text);
    if (current < 0.0 || max <= 0.0) {
        return FALSE;
    }

    *percent_out = CLAMP(current * 100.0 / max, 0.0, 100.0);
    return TRUE;
}

static gboolean
light_get_percent(double *percent_out)
{
    gchar *stdout_text = NULL;
    gint status = 0;
    if (!run_command_capture("light -G", &stdout_text, NULL, &status) || status != 0) {
        g_free(stdout_text);
        return FALSE;
    }

    gchar *line = first_line_trimmed(stdout_text);
    g_free(stdout_text);
    *percent_out = CLAMP(g_ascii_strtod(line, NULL), 0.0, 100.0);
    g_free(line);
    return TRUE;
}

static gboolean
read_brightness_state(double *percent_out, gchar **backend_out)
{
    if (percent_out) {
        *percent_out = 0.0;
    }
    if (backend_out) {
        *backend_out = NULL;
    }

    if (karton_command_exists("brightnessctl") && brightnessctl_get_percent(percent_out)) {
        if (backend_out) {
            *backend_out = g_strdup("brightnessctl");
        }
        return TRUE;
    }
    if (karton_command_exists("light") && light_get_percent(percent_out)) {
        if (backend_out) {
            *backend_out = g_strdup("light");
        }
        return TRUE;
    }
    if (backlight_get_percent(percent_out)) {
        if (backend_out) {
            *backend_out = g_strdup("sysfs");
        }
        return TRUE;
    }

    return FALSE;
}

void
karton_toggle_state_clear(KartonToggleState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_accessibility_state_clear(KartonAccessibilityState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_advanced_state_clear(KartonAdvancedState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_audio_state_clear(KartonAudioState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_power_state_clear(KartonPowerState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->current_profile, g_free);
    g_clear_pointer(&state->summary, g_free);
}

void
karton_display_state_clear(KartonDisplayState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->brightness_backend, g_free);
    g_clear_pointer(&state->backend, g_free);
    g_clear_pointer(&state->current_output, g_free);
    g_clear_pointer(&state->current_mode, g_free);
    g_clear_pointer(&state->current_orientation, g_free);
    g_clear_pointer(&state->available_outputs, g_free);
    g_clear_pointer(&state->available_modes, g_free);
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_input_state_clear(KartonInputState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_user_state_clear(KartonUserState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->username, g_free);
    g_clear_pointer(&state->full_name, g_free);
    g_clear_pointer(&state->shell, g_free);
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_session_state_clear(KartonSessionState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->session_type, g_free);
    g_clear_pointer(&state->session_name, g_free);
    g_clear_pointer(&state->desktop_name, g_free);
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
    g_clear_pointer(&state->autostart_entries, g_free);
    g_clear_pointer(&state->autostart_selected, g_free);
    g_clear_pointer(&state->autostart_preview, g_free);
}

void
karton_default_apps_state_clear(KartonDefaultAppsState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->browser, g_free);
    g_clear_pointer(&state->file_manager, g_free);
    g_clear_pointer(&state->text_editor, g_free);
    g_clear_pointer(&state->mail_app, g_free);
    g_clear_pointer(&state->audio_app, g_free);
    g_clear_pointer(&state->video_app, g_free);
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_updates_state_clear(KartonUpdatesState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->backend, g_free);
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_storage_state_clear(KartonStorageState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_privacy_state_clear(KartonPrivacyState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

void
karton_region_state_clear(KartonRegionState *state)
{
    if (!state) {
        return;
    }
    g_clear_pointer(&state->locale, g_free);
    g_clear_pointer(&state->timezone, g_free);
    g_clear_pointer(&state->vc_keymap, g_free);
    g_clear_pointer(&state->x11_layout, g_free);
    g_clear_pointer(&state->summary, g_free);
    g_clear_pointer(&state->details, g_free);
}

static gchar *
query_command_first_line(const char *command)
{
    gchar *stdout_text = NULL;
    gint status = 0;
    if (!run_command_capture(command, &stdout_text, NULL, &status) || status != 0) {
        g_free(stdout_text);
        return NULL;
    }

    gchar *line = first_line_trimmed(stdout_text);
    g_free(stdout_text);
    if (!line || !*line) {
        g_free(line);
        return NULL;
    }
    return line;
}

static gboolean
run_command_async(const char *command, gchar **error_msg)
{
    GError *error = NULL;
    gboolean ok = g_spawn_command_line_async(command, &error);
    if (!ok && error_msg) {
        *error_msg = g_strdup(error ? error->message : "Cannot launch command.");
    }
    g_clear_error(&error);
    return ok;
}

gboolean
karton_wifi_get_state(KartonToggleState *state)
{
    memset(state, 0, sizeof(*state));
    if (!karton_command_exists("nmcli")) {
        state->summary = g_strdup(_("NetworkManager CLI is not available."));
        return FALSE;
    }

    gchar *stdout_text = NULL;
    gint status = 0;
    if (!run_command_capture("nmcli -t -f WIFI general status", &stdout_text, NULL, &status) || status != 0) {
        g_free(stdout_text);
        state->summary = g_strdup(_("Cannot read Wi-Fi status."));
        return FALSE;
    }

    gchar *line = first_line_trimmed(stdout_text);
    state->available = TRUE;
    state->enabled = g_ascii_strcasecmp(line, "enabled") == 0;
    g_free(line);
    g_free(stdout_text);

    stdout_text = NULL;
    if (run_command_capture("nmcli -t -f ACTIVE,SSID dev wifi", &stdout_text, NULL, &status) && status == 0) {
        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "yes:")) {
                const char *ssid = lines[i] + 4;
                state->summary = g_strdup_printf(_("Connected to %s"), ssid && *ssid ? ssid : _("Wi-Fi network"));
                break;
            }
        }
        g_strfreev(lines);
    }
    g_free(stdout_text);

    if (!state->summary) {
        state->summary = g_strdup(state->enabled ? _("Wi-Fi is enabled.") : _("Wi-Fi is disabled."));
    }

    stdout_text = NULL;
    if (run_command_capture("nmcli -t -f ACTIVE,SSID,SIGNAL,SECURITY dev wifi list | head -n 8", &stdout_text, NULL, &status) && status == 0 && stdout_text && *stdout_text) {
        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        GString *details = g_string_new("");
        guint count = 0;
        for (guint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (!*line) {
                continue;
            }
            gchar **parts = g_strsplit(line, ":", 4);
            const char *active = parts[0] ? parts[0] : "";
            const char *ssid = parts[1] && *parts[1] ? parts[1] : _("Hidden network");
            const char *signal = parts[2] && *parts[2] ? parts[2] : "?";
            const char *security = parts[3] && *parts[3] ? parts[3] : _("Open");
            g_string_append_printf(details,
                "%s%s %s (%s%%, %s)",
                count > 0 ? "\n" : "",
                g_strcmp0(active, "yes") == 0 ? "•" : "○",
                ssid,
                signal,
                security);
            count++;
            g_strfreev(parts);
        }
        if (count > 0) {
            state->details = g_string_free(details, FALSE);
        } else {
            g_string_free(details, TRUE);
        }
        g_strfreev(lines);
    }
    g_free(stdout_text);

    if (!state->details) {
        state->details = g_strdup(_("No Wi-Fi networks were reported."));
    }
    return TRUE;
}

gboolean
karton_wifi_set_enabled(gboolean enabled, gchar **error_msg)
{
    if (!karton_command_exists("nmcli")) {
        if (error_msg) {
            *error_msg = g_strdup(_("nmcli is not available"));
        }
        return FALSE;
    }
    return run_command_ok(enabled ? "nmcli radio wifi on" : "nmcli radio wifi off", error_msg);
}

gboolean
karton_bluetooth_get_state(KartonToggleState *state)
{
    memset(state, 0, sizeof(*state));

    if (karton_command_exists("nmcli")) {
        gchar *stdout_text = NULL;
        gint status = 0;
        if (run_command_capture("nmcli -t -f BLUETOOTH general status", &stdout_text, NULL, &status) && status == 0) {
            gchar *line = first_line_trimmed(stdout_text);
            state->available = TRUE;
            state->enabled = g_ascii_strcasecmp(line, "enabled") == 0;
            state->summary = g_strdup(state->enabled ? "Bluetooth is enabled." : "Bluetooth is disabled.");
            g_free(line);
            g_free(stdout_text);
            return TRUE;
        }
        g_free(stdout_text);
    }

    if (!karton_command_exists("rfkill")) {
        state->summary = g_strdup(_("Bluetooth backend is not available."));
        return FALSE;
    }

    gchar *stdout_text = NULL;
    gint status = 0;
    if (!run_command_capture("rfkill list bluetooth", &stdout_text, NULL, &status) || status != 0) {
        g_free(stdout_text);
        state->summary = g_strdup(_("Cannot read Bluetooth state."));
        return FALSE;
    }

    state->available = TRUE;
    state->enabled = stdout_text && !g_strrstr(stdout_text, "Soft blocked: yes") && !g_strrstr(stdout_text, "Hard blocked: yes");
    state->summary = g_strdup(state->enabled ? _("Bluetooth is enabled.") : _("Bluetooth is disabled."));
    g_free(stdout_text);

    stdout_text = NULL;
    if (karton_command_exists("bluetoothctl")
        && run_command_capture("bluetoothctl devices | head -n 8", &stdout_text, NULL, &status)
        && status == 0
        && stdout_text
        && *stdout_text) {
        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        GString *details = g_string_new("");
        guint count = 0;
        for (guint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (!*line) {
                continue;
            }
            const char *label = g_str_has_prefix(line, "Device ") ? line + 7 : line;
            const char *name = strchr(label, ' ');
            g_string_append_printf(details, "%s%s", count > 0 ? "\n" : "", name && *(name + 1) ? name + 1 : label);
            count++;
        }
        if (count > 0) {
            state->details = g_string_free(details, FALSE);
        } else {
            g_string_free(details, TRUE);
        }
        g_strfreev(lines);
    }
    g_free(stdout_text);

    if (!state->details) {
        state->details = g_strdup(_("No Bluetooth devices were reported."));
    }
    return TRUE;
}

gboolean
karton_bluetooth_set_enabled(gboolean enabled, gchar **error_msg)
{
    if (karton_command_exists("nmcli")) {
        return run_command_ok(enabled ? "nmcli radio bluetooth on" : "nmcli radio bluetooth off", error_msg);
    }
    if (karton_command_exists("rfkill")) {
        return run_command_ok(enabled ? "rfkill unblock bluetooth" : "rfkill block bluetooth", error_msg);
    }
    if (error_msg) {
        *error_msg = g_strdup(_("No Bluetooth backend is available"));
    }
    return FALSE;
}

gboolean
karton_notifications_get_state(KartonToggleState *state)
{
    memset(state, 0, sizeof(*state));
    if (!karton_command_exists("gsettings")) {
        state->summary = g_strdup(_("gsettings is not available."));
        return FALSE;
    }

    gboolean show_banners = TRUE;
    if (!gsettings_get_bool("org.gnome.desktop.notifications", "show-banners", &show_banners)) {
        state->summary = g_strdup(_("Notification schema is unavailable."));
        return FALSE;
    }

    state->available = TRUE;
    state->enabled = !show_banners;
    state->summary = g_strdup(state->enabled ? _("Do not disturb is enabled.") : _("Notifications are enabled."));
    return TRUE;
}

gboolean
karton_notifications_set_dnd(gboolean enabled, gchar **error_msg)
{
    return gsettings_set_bool("org.gnome.desktop.notifications", "show-banners", !enabled, error_msg);
}

gboolean
karton_accessibility_get_state(KartonAccessibilityState *state)
{
    memset(state, 0, sizeof(*state));
    if (!karton_command_exists("gsettings")) {
        state->summary = g_strdup(_("Accessibility settings backend is unavailable."));
        state->details = g_strdup(_("Install gsettings support to manage accessibility options here."));
        return FALSE;
    }

    state->available = TRUE;
    if (!gsettings_get_bool("org.gnome.desktop.a11y.applications", "screen-reader-enabled", &state->screen_reader)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.a11y.applications", "screen-keyboard-enabled", &state->screen_keyboard)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.a11y.applications", "screen-magnifier-enabled", &state->screen_magnifier)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.a11y.keyboard", "stickykeys-enable", &state->sticky_keys)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.a11y.keyboard", "mousekeys-enable", &state->mouse_keys)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.a11y.keyboard", "slowkeys-enable", &state->slow_keys)) {
        state->available = FALSE;
    }
    if (!gsettings_get_double("org.gnome.desktop.interface", "text-scaling-factor", &state->text_scale)) {
        state->available = FALSE;
    }

    if (!state->available) {
        state->summary = g_strdup(_("Accessibility schemas are only partially available."));
        state->details = g_strdup(_("Some accessibility controls could not be read from the current desktop settings."));
        return FALSE;
    }

    state->summary = g_strdup_printf(_("Screen reader %s, text scale %.2fx"),
        state->screen_reader ? _("enabled") : _("disabled"),
        state->text_scale);
    state->details = g_strdup_printf(
        _("On-screen keyboard: %s\nScreen magnifier: %s\nSticky keys: %s\nMouse keys: %s\nSlow keys: %s\nText scaling: %.2fx"),
        state->screen_keyboard ? _("enabled") : _("disabled"),
        state->screen_magnifier ? _("enabled") : _("disabled"),
        state->sticky_keys ? _("enabled") : _("disabled"),
        state->mouse_keys ? _("enabled") : _("disabled"),
        state->slow_keys ? _("enabled") : _("disabled"),
        state->text_scale);
    return TRUE;
}

gboolean
karton_accessibility_apply(gboolean screen_reader,
    gboolean screen_keyboard,
    gboolean screen_magnifier,
    gboolean sticky_keys,
    gboolean mouse_keys,
    gboolean slow_keys,
    double text_scale,
    gchar **error_msg)
{
    if (!karton_command_exists("gsettings")) {
        if (error_msg) {
            *error_msg = g_strdup(_("gsettings is not available."));
        }
        return FALSE;
    }

    if (text_scale < 0.50 || text_scale > 3.00) {
        if (error_msg) {
            *error_msg = g_strdup(_("Text scaling is out of range."));
        }
        return FALSE;
    }

    if (!gsettings_set_bool("org.gnome.desktop.a11y.applications", "screen-reader-enabled", screen_reader, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.a11y.applications", "screen-keyboard-enabled", screen_keyboard, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.a11y.applications", "screen-magnifier-enabled", screen_magnifier, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.a11y.keyboard", "stickykeys-enable", sticky_keys, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.a11y.keyboard", "mousekeys-enable", mouse_keys, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.a11y.keyboard", "slowkeys-enable", slow_keys, error_msg)) {
        return FALSE;
    }
    return gsettings_set_double("org.gnome.desktop.interface", "text-scaling-factor", text_scale, error_msg);
}

gboolean
karton_audio_get_state(KartonAudioState *state)
{
    memset(state, 0, sizeof(*state));
    if (!karton_command_exists("wpctl")) {
        state->summary = g_strdup(_("PipeWire wpctl is not available."));
        return FALSE;
    }

    gchar *stdout_text = NULL;
    gint status = 0;
    if (!run_command_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@", &stdout_text, NULL, &status) || status != 0) {
        g_free(stdout_text);
        state->summary = g_strdup(_("Cannot read output volume."));
        return FALSE;
    }

    double value = first_number_in_text(stdout_text);
    state->available = value >= 0.0;
    state->muted = stdout_text && strstr(stdout_text, "MUTED") != NULL;
    state->level = value >= 0.0 ? CLAMP(value * 100.0, 0.0, 150.0) : 0.0;
    state->summary = g_strdup_printf(_("Output volume: %.0f%%%s"), state->level, state->muted ? _(" (muted)") : "");
    g_free(stdout_text);

    stdout_text = NULL;
    if (run_command_capture("wpctl get-volume @DEFAULT_AUDIO_SOURCE@", &stdout_text, NULL, &status) && status == 0) {
        double input_value = first_number_in_text(stdout_text);
        state->input_available = input_value >= 0.0;
        state->input_muted = stdout_text && strstr(stdout_text, "MUTED") != NULL;
        state->input_level = input_value >= 0.0 ? CLAMP(input_value * 100.0, 0.0, 150.0) : 0.0;
    }
    g_free(stdout_text);

    stdout_text = NULL;
    if (run_command_capture("wpctl status | sed -n '/Audio/,/Video/p' | head -n 24", &stdout_text, NULL, &status) && status == 0 && stdout_text && *stdout_text) {
        state->details = first_line_trimmed(stdout_text);
        g_free(state->details);
        state->details = g_strdup(stdout_text);
    }
    g_free(stdout_text);

    if (!state->details) {
        state->details = g_strdup_printf(_("Input volume: %.0f%%%s"),
            state->input_level,
            state->input_muted ? _(" (muted)") : "");
    }
    return TRUE;
}

gboolean
karton_audio_apply(double output_level,
    gboolean output_muted,
    double input_level,
    gboolean input_muted,
    gchar **error_msg)
{
    if (!karton_command_exists("wpctl")) {
        if (error_msg) {
            *error_msg = g_strdup(_("wpctl is not available"));
        }
        return FALSE;
    }

    gchar *volume_cmd = g_strdup_printf("wpctl set-volume @DEFAULT_AUDIO_SINK@ %.0f%%", CLAMP(output_level, 0.0, 150.0));
    gboolean ok = run_command_ok(volume_cmd, error_msg);
    g_free(volume_cmd);
    if (!ok) {
        return FALSE;
    }

    if (!run_command_ok(output_muted ? "wpctl set-mute @DEFAULT_AUDIO_SINK@ 1" : "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0", error_msg)) {
        return FALSE;
    }

    volume_cmd = g_strdup_printf("wpctl set-volume @DEFAULT_AUDIO_SOURCE@ %.0f%%", CLAMP(input_level, 0.0, 150.0));
    ok = run_command_ok(volume_cmd, error_msg);
    g_free(volume_cmd);
    if (!ok) {
        return FALSE;
    }

    return run_command_ok(input_muted ? "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 1" : "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 0", error_msg);
}

gboolean
karton_power_get_state(KartonPowerState *state)
{
    memset(state, 0, sizeof(*state));
    if (!karton_command_exists("powerprofilesctl")) {
        state->summary = g_strdup(_("powerprofilesctl is not available."));
        return FALSE;
    }

    gchar *stdout_text = NULL;
    gint status = 0;
    if (!run_command_capture("powerprofilesctl get", &stdout_text, NULL, &status) || status != 0) {
        g_free(stdout_text);
        state->summary = g_strdup(_("Cannot read the current power profile."));
        return FALSE;
    }

    gchar *line = first_line_trimmed(stdout_text);
    state->available = TRUE;
    state->current_profile = g_strdup(line);
    state->summary = g_strdup_printf(_("Active profile: %s"), line);
    g_free(line);
    g_free(stdout_text);
    return TRUE;
}

gboolean
karton_power_set_profile(const char *profile, gchar **error_msg)
{
    if (!profile || !*profile) {
        if (error_msg) {
            *error_msg = g_strdup(_("Power profile is required"));
        }
        return FALSE;
    }

    gchar *cmd = g_strdup_printf("powerprofilesctl set %s", profile);
    gboolean ok = run_command_ok(cmd, error_msg);
    g_free(cmd);
    return ok;
}

gboolean
karton_display_get_state(KartonDisplayState *state)
{
    memset(state, 0, sizeof(*state));
    state->interface_scale = 1.0;
    state->current_orientation = g_strdup("normal");
    state->brightness_available = read_brightness_state(&state->brightness_level, &state->brightness_backend);

    if (karton_command_exists("gsettings")) {
        double scale = 1.0;
        if (gsettings_get_double("org.gnome.desktop.interface", "text-scaling-factor", &scale) && scale > 0.0) {
            state->interface_scale = scale;
        }
    }

    gchar *stdout_text = NULL;
    gint status = 0;
    if (karton_command_exists("xrandr")
        && run_command_capture("xrandr --query 2>/dev/null", &stdout_text, NULL, &status)
        && status == 0
        && stdout_text
        && *stdout_text) {
        state->available = TRUE;
        state->backend = g_strdup("xrandr");
        state->can_configure = g_strcmp0(g_getenv("XDG_SESSION_TYPE"), "x11") == 0;

        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        guint outputs = 0;
        GString *detail = g_string_new("");
        GString *output_list = g_string_new("");
        GString *mode_list = g_string_new("");
        gboolean collect_modes = FALSE;

        for (guint i = 0; lines[i]; i++) {
            gchar *raw = lines[i];
            gchar *line = g_strstrip(raw);
            if (!*line) {
                continue;
            }

            if (!g_ascii_isspace((guchar)raw[0]) && strstr(line, " connected")) {
                collect_modes = FALSE;
                gchar **parts = g_strsplit(line, " ", 0);
                const char *output_name = parts[0] ? parts[0] : "";
                gchar *mode_token = NULL;
                const char *orientation = "normal";

                for (guint p = 0; parts[p]; p++) {
                    if (strchr(parts[p], '+') && strchr(parts[p], 'x')) {
                        gchar **mode_parts = g_strsplit(parts[p], "+", 2);
                        mode_token = g_strdup(mode_parts[0] ? mode_parts[0] : parts[p]);
                        g_strfreev(mode_parts);
                    }
                    if (parts[p][0] == '(') {
                        if (strstr(parts[p], "left")) {
                            orientation = "left";
                        } else if (strstr(parts[p], "right")) {
                            orientation = "right";
                        } else if (strstr(parts[p], "inverted")) {
                            orientation = "inverted";
                        }
                    }
                }

                outputs++;
                g_string_append_printf(output_list, "%s%s", outputs > 1 ? "\n" : "", output_name);
                g_string_append_printf(detail,
                    "%s%s: %s (%s)",
                    outputs > 1 ? "\n" : "",
                    output_name,
                    mode_token ? mode_token : _("mode unknown"),
                    orientation);

                if (!state->current_output) {
                    state->current_output = g_strdup(output_name);
                    state->current_mode = g_strdup(mode_token ? mode_token : _("mode unknown"));
                    g_free(state->current_orientation);
                    state->current_orientation = g_strdup(orientation);
                    collect_modes = TRUE;
                }

                g_free(mode_token);
                g_strfreev(parts);
                continue;
            }

            if (collect_modes && g_ascii_isdigit((guchar)line[0])) {
                gchar **parts = g_strsplit_set(line, " \t", 0);
                if (parts[0] && *parts[0] && strchr(parts[0], 'x')) {
                    if (mode_list->len > 0) {
                        g_string_append_c(mode_list, '\n');
                    }
                    g_string_append(mode_list, parts[0]);
                }
                g_strfreev(parts);
            }
        }

        state->summary = g_strdup_printf(_("Detected %u display output(s). Interface scale: %.2fx"), outputs, state->interface_scale);
        state->details = g_string_free(detail, FALSE);
        state->available_outputs = g_string_free(output_list, FALSE);
        state->available_modes = g_string_free(mode_list, FALSE);
        g_strfreev(lines);
    }
    g_free(stdout_text);

    if (!state->summary) {
        state->summary = g_strdup_printf(_("Display backend not detected. Interface scale: %.2fx"), state->interface_scale);
        state->details = g_strdup(_("Display information will appear when a display backend is detected."));
    }

    return state->available;
}

gboolean
karton_display_set_brightness(double percent, gchar **error_msg)
{
    if (percent < 0.0 || percent > 100.0) {
        if (error_msg) {
            *error_msg = g_strdup(_("Brightness value is out of range."));
        }
        return FALSE;
    }

    double clamped = CLAMP(percent, 1.0, 100.0);
    if (karton_command_exists("brightnessctl")) {
        gchar *cmd = g_strdup_printf("brightnessctl set %.0f%%", clamped);
        gboolean ok = run_command_ok(cmd, error_msg);
        g_free(cmd);
        return ok;
    }
    if (karton_command_exists("light")) {
        gchar *cmd = g_strdup_printf("light -S %.0f", clamped);
        gboolean ok = run_command_ok(cmd, error_msg);
        g_free(cmd);
        return ok;
    }
    if (backlight_set_percent(clamped)) {
        return TRUE;
    }

    if (error_msg) {
        *error_msg = g_strdup(_("Brightness control backend is unavailable."));
    }
    return FALSE;
}

gboolean
karton_display_apply_mode(const char *output,
    const char *mode,
    const char *orientation,
    gchar **error_msg)
{
    if (!karton_command_exists("xrandr")) {
        if (error_msg) {
            *error_msg = g_strdup(_("xrandr is not available."));
        }
        return FALSE;
    }
    if (g_strcmp0(g_getenv("XDG_SESSION_TYPE"), "x11") != 0) {
        if (error_msg) {
            *error_msg = g_strdup(_("Display mode changes are available only in X11 sessions."));
        }
        return FALSE;
    }
    if (!output || !*output || !mode || !*mode) {
        if (error_msg) {
            *error_msg = g_strdup(_("Display output and mode are required."));
        }
        return FALSE;
    }

    const char *rotation = orientation && *orientation ? orientation : "normal";
    gchar *cmd = g_strdup_printf("xrandr --output %s --mode %s --rotate %s", output, mode, rotation);
    gboolean ok = run_command_ok(cmd, error_msg);
    g_free(cmd);
    return ok;
}

gboolean
karton_display_set_interface_scale(double scale, gchar **error_msg)
{
    if (!karton_command_exists("gsettings")) {
        if (error_msg) {
            *error_msg = g_strdup(_("gsettings is not available."));
        }
        return FALSE;
    }
    if (scale < 0.5 || scale > 3.0) {
        if (error_msg) {
            *error_msg = g_strdup(_("Interface scale is out of range."));
        }
        return FALSE;
    }
    return gsettings_set_double("org.gnome.desktop.interface", "text-scaling-factor", scale, error_msg);
}

gboolean
karton_input_get_state(KartonInputState *state)
{
    memset(state, 0, sizeof(*state));
    if (!karton_command_exists("gsettings")) {
        state->summary = g_strdup(_("gsettings is not available."));
        state->details = g_strdup(_("Input settings backend is unavailable."));
        return FALSE;
    }

    gboolean got_any = FALSE;
    double mouse_speed = 0.0;
    if (gsettings_get_double("org.gnome.desktop.peripherals.mouse", "accel-speed", &mouse_speed)) {
        state->mouse_speed = CLAMP(mouse_speed, -1.0, 1.0);
        got_any = TRUE;
    }
    if (gsettings_get_bool("org.gnome.desktop.peripherals.touchpad", "natural-scroll", &state->natural_scroll)) {
        got_any = TRUE;
    }
    if (gsettings_get_bool("org.gnome.desktop.peripherals.touchpad", "tap-to-click", &state->tap_to_click)) {
        got_any = TRUE;
    }
    if (gsettings_get_bool("org.gnome.desktop.peripherals.mouse", "left-handed", &state->left_handed)) {
        got_any = TRUE;
    }

    state->available = got_any;
    if (!got_any) {
        state->summary = g_strdup(_("Input settings are unavailable."));
        state->details = g_strdup(_("Mouse and touchpad keys could not be read from gsettings."));
        return FALSE;
    }

    state->summary = g_strdup_printf(_("Pointer speed %.0f%%, tap-to-click %s"),
        ((state->mouse_speed + 1.0) * 50.0),
        state->tap_to_click ? _("enabled") : _("disabled"));
    state->details = g_strdup_printf(_("Natural scroll: %s\nLeft-handed mouse: %s\nTouchpad tap-to-click: %s"),
        state->natural_scroll ? _("enabled") : _("disabled"),
        state->left_handed ? _("enabled") : _("disabled"),
        state->tap_to_click ? _("enabled") : _("disabled"));
    return TRUE;
}

gboolean
karton_input_apply(double mouse_speed,
    gboolean natural_scroll,
    gboolean tap_to_click,
    gboolean left_handed,
    gchar **error_msg)
{
    if (!karton_command_exists("gsettings")) {
        if (error_msg) {
            *error_msg = g_strdup(_("gsettings is not available."));
        }
        return FALSE;
    }

    if (mouse_speed < -1.0 || mouse_speed > 1.0) {
        if (error_msg) {
            *error_msg = g_strdup(_("Pointer speed is out of range."));
        }
        return FALSE;
    }

    if (!gsettings_set_double("org.gnome.desktop.peripherals.mouse", "accel-speed", mouse_speed, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.peripherals.touchpad", "natural-scroll", natural_scroll, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.peripherals.touchpad", "tap-to-click", tap_to_click, error_msg)) {
        return FALSE;
    }
    return gsettings_set_bool("org.gnome.desktop.peripherals.mouse", "left-handed", left_handed, error_msg);
}

gboolean
karton_user_get_state(KartonUserState *state)
{
    memset(state, 0, sizeof(*state));

    struct passwd *pwd = getpwuid(getuid());
    const char *username = g_get_user_name();
    const char *real_name = g_get_real_name();

    state->username = g_strdup(username && *username ? username : (pwd && pwd->pw_name ? pwd->pw_name : _("Unknown user")));
    state->full_name = g_strdup((real_name && *real_name && strcmp(real_name, "Unknown") != 0)
        ? real_name
        : (pwd && pwd->pw_gecos && *pwd->pw_gecos ? pwd->pw_gecos : state->username));
    state->shell = g_strdup(pwd && pwd->pw_shell && *pwd->pw_shell ? pwd->pw_shell : _("Unknown shell"));

    gchar *groups = NULL;
    gint status = 0;
    if (run_command_capture("id -nG", &groups, NULL, &status) && status == 0 && groups) {
        state->admin = strstr(groups, "wheel") || strstr(groups, "sudo");
    }
    g_free(groups);

    state->summary = g_strdup_printf(_("Signed in as %s"), state->full_name);
    state->details = g_strdup_printf(_("Login: %s\nShell: %s\nAdministrator privileges: %s"),
        state->username,
        state->shell,
        state->admin ? _("yes") : _("no"));
    return TRUE;
}

gboolean
karton_session_get_state(KartonSessionState *state)
{
    memset(state, 0, sizeof(*state));

    const char *session_type = g_getenv("XDG_SESSION_TYPE");
    const char *desktop_name = g_getenv("XDG_CURRENT_DESKTOP");
    state->session_type = g_strdup(session_type && *session_type ? session_type : _("unknown"));
    state->desktop_name = g_strdup(desktop_name && *desktop_name ? desktop_name : _("Karton"));

    gchar *stdout_text = NULL;
    gint status = 0;
    if (run_command_capture("loginctl show-session self -p Name -p State -p Type 2>/dev/null", &stdout_text, NULL, &status) && status == 0 && stdout_text) {
        gchar **lines = g_strsplit(stdout_text, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            if (g_str_has_prefix(lines[i], "Name=")) {
                state->session_name = g_strdup(lines[i] + 5);
            }
            if (g_str_has_prefix(lines[i], "Type=") && (!session_type || !*session_type)) {
                g_free(state->session_type);
                state->session_type = g_strdup(lines[i] + 5);
            }
        }
        g_strfreev(lines);
    }
    g_free(stdout_text);

    gchar *autostart_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    GDir *dir = g_dir_open(autostart_dir, 0, NULL);
    if (dir) {
        const gchar *name = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_suffix(name, ".desktop")) {
                state->autostart_count++;
            }
        }
        g_dir_close(dir);
    }
    g_free(autostart_dir);

    GPtrArray *entries = collect_autostart_entries();
    GString *entry_names = g_string_new("");
    GString *preview = g_string_new("");
    state->autostart_count = (gint)entries->len;
    for (guint i = 0; i < entries->len; i++) {
        KartonAutostartEntry *entry = g_ptr_array_index(entries, i);
        g_string_append_printf(entry_names,
            "%s%s\t%s",
            i > 0 ? "\n" : "",
            entry->display_name ? entry->display_name : entry->desktop_id,
            entry->desktop_id);
        if (i < 8) {
            g_string_append_printf(preview,
                "%s%s%s%s (%s)",
                i > 0 ? "\n" : "",
                entry->display_name,
                entry->exec_line && *entry->exec_line ? " - " : "",
                entry->exec_line && *entry->exec_line ? entry->exec_line : "",
                entry->enabled ? _("enabled") : _("disabled"));
        }
        if (!state->autostart_selected) {
            state->autostart_selected = g_strdup(entry->desktop_id);
            state->autostart_selected_enabled = entry->enabled;
        }
    }
    state->autostart_entries = g_string_free(entry_names, FALSE);
    state->autostart_preview = preview->len > 0
        ? g_string_free(preview, FALSE)
        : (g_string_free(preview, TRUE), g_strdup(_("No autostart entries detected.")));
    g_ptr_array_free(entries, TRUE);

    state->summary = g_strdup_printf(_("%s session on %s"), state->session_type, state->desktop_name);
    state->details = g_strdup_printf(_("Session name: %s\nAutostart entries: %d"),
        state->session_name && *state->session_name ? state->session_name : _("self"),
        state->autostart_count);
    return TRUE;
}

gboolean
karton_session_run_action(const char *action, gchar **error_msg)
{
    const char *command = NULL;
    if (g_strcmp0(action, "logout") == 0) {
        command = "sh -lc 'if command -v karton >/dev/null 2>&1; then karton --exit; elif command -v labwc >/dev/null 2>&1; then labwc --exit; else loginctl terminate-session self; fi'";
    } else if (g_strcmp0(action, "restart") == 0) {
        command = "systemctl reboot";
    } else if (g_strcmp0(action, "poweroff") == 0) {
        command = "systemctl poweroff";
    }

    if (!command) {
        if (error_msg) {
            *error_msg = g_strdup(_("Unknown session action."));
        }
        return FALSE;
    }

    GError *error = NULL;
    if (g_spawn_command_line_async(command, &error)) {
        return TRUE;
    }

    if (error_msg) {
        *error_msg = g_strdup(error ? error->message : _("Cannot execute session action."));
    }
    g_clear_error(&error);
    return FALSE;
}

gboolean
karton_session_autostart_lookup(const char *desktop_id, gboolean *enabled_out, gchar **details_out)
{
    if (details_out) {
        *details_out = NULL;
    }
    if (enabled_out) {
        *enabled_out = FALSE;
    }
    if (!desktop_id || !*desktop_id) {
        return FALSE;
    }

    KartonAutostartEntry *entry = find_autostart_entry(desktop_id);
    if (!entry) {
        return FALSE;
    }

    if (enabled_out) {
        *enabled_out = entry->enabled;
    }
    if (details_out) {
        *details_out = g_strdup_printf(_("Entry: %s\nDesktop file: %s\nSource: %s\nState: %s\nCommand: %s\nDescription: %s"),
            entry->display_name ? entry->display_name : entry->desktop_id,
            entry->desktop_id,
            entry->user_path ? _("user override") : _("system default"),
            entry->enabled ? _("enabled") : _("disabled"),
            entry->exec_line && *entry->exec_line ? entry->exec_line : _("not provided"),
            entry->comment && *entry->comment ? entry->comment : _("not provided"));
    }

    autostart_entry_free(entry);
    return TRUE;
}

gboolean
karton_session_autostart_set_enabled(const char *desktop_id, gboolean enabled, gchar **error_msg)
{
    KartonAutostartEntry *entry = find_autostart_entry(desktop_id);
    if (!entry) {
        if (error_msg) {
            *error_msg = g_strdup(_("Autostart entry was not found."));
        }
        return FALSE;
    }

    gchar *user_dir = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    if (g_mkdir_with_parents(user_dir, 0755) != 0) {
        if (error_msg) {
            *error_msg = g_strdup(_("Cannot create the user autostart directory."));
        }
        g_free(user_dir);
        autostart_entry_free(entry);
        return FALSE;
    }

    gchar *target = g_build_filename(user_dir, desktop_id, NULL);
    g_free(user_dir);
    gboolean ok = FALSE;

    if (enabled) {
        if (entry->system_path) {
            gchar *contents = NULL;
            if (g_file_get_contents(entry->system_path, &contents, NULL, NULL) && contents) {
                GKeyFile *key_file = g_key_file_new();
                if (g_key_file_load_from_data(key_file, contents, -1, G_KEY_FILE_NONE, NULL)) {
                    g_key_file_set_boolean(key_file, "Desktop Entry", "Hidden", FALSE);
                    gsize len = 0;
                    gchar *data = g_key_file_to_data(key_file, &len, NULL);
                    ok = g_file_set_contents(target, data, (gssize)len, NULL);
                    g_free(data);
                }
                g_key_file_free(key_file);
            }
            g_free(contents);
        } else if (entry->user_path) {
            GKeyFile *key_file = g_key_file_new();
            if (g_key_file_load_from_file(key_file, entry->user_path, G_KEY_FILE_NONE, NULL)) {
                g_key_file_set_boolean(key_file, "Desktop Entry", "Hidden", FALSE);
                gsize len = 0;
                gchar *data = g_key_file_to_data(key_file, &len, NULL);
                ok = g_file_set_contents(target, data, (gssize)len, NULL);
                g_free(data);
            }
            g_key_file_free(key_file);
        }
    } else {
        GKeyFile *key_file = g_key_file_new();
        g_key_file_set_string(key_file, "Desktop Entry", "Type", "Application");
        g_key_file_set_string(key_file, "Desktop Entry", "Name", entry->display_name ? entry->display_name : desktop_id);
        g_key_file_set_boolean(key_file, "Desktop Entry", "Hidden", TRUE);
        gsize len = 0;
        gchar *data = g_key_file_to_data(key_file, &len, NULL);
        ok = g_file_set_contents(target, data, (gssize)len, NULL);
        g_free(data);
        g_key_file_free(key_file);
    }

    if (!ok && error_msg) {
        *error_msg = g_strdup(_("Cannot update the autostart override."));
    }

    g_free(target);
    autostart_entry_free(entry);
    return ok;
}

gboolean
karton_default_apps_get_state(KartonDefaultAppsState *state)
{
    memset(state, 0, sizeof(*state));

    state->browser = query_command_first_line("xdg-settings get default-web-browser 2>/dev/null");
    state->file_manager = query_command_first_line("xdg-mime query default inode/directory 2>/dev/null");
    state->text_editor = query_command_first_line("xdg-mime query default text/plain 2>/dev/null");
    state->mail_app = query_command_first_line("xdg-mime query default x-scheme-handler/mailto 2>/dev/null");
    state->audio_app = query_command_first_line("xdg-mime query default audio/mpeg 2>/dev/null");
    state->video_app = query_command_first_line("xdg-mime query default video/mp4 2>/dev/null");

    state->summary = g_strdup_printf(_("Browser: %s"), state->browser ? state->browser : _("not set"));
    state->details = g_strdup_printf(
        _("File manager: %s\nText editor: %s\nMail handler: %s\nMusic: %s\nVideo: %s"),
        state->file_manager ? state->file_manager : _("not set"),
        state->text_editor ? state->text_editor : _("not set"),
        state->mail_app ? state->mail_app : _("not set"),
        state->audio_app ? state->audio_app : _("not set"),
        state->video_app ? state->video_app : _("not set"));
    return TRUE;
}

gboolean
karton_updates_get_state(KartonUpdatesState *state)
{
    memset(state, 0, sizeof(*state));

    gchar *stdout_text = NULL;
    gint status = 0;
    if (karton_command_exists("checkupdates")
        && run_command_capture("checkupdates 2>/dev/null | head -n 12", &stdout_text, NULL, &status)) {
        state->available = TRUE;
        state->backend = g_strdup("pacman");
    } else if (karton_command_exists("apt")
        && run_command_capture("apt list --upgradable 2>/dev/null | sed '1d' | head -n 12", &stdout_text, NULL, &status)) {
        state->available = TRUE;
        state->backend = g_strdup("apt");
    } else if (karton_command_exists("dnf")
        && run_command_capture("dnf check-update -q 2>/dev/null | head -n 12", &stdout_text, NULL, &status)) {
        state->available = TRUE;
        state->backend = g_strdup("dnf");
    }

    if (!state->available) {
        state->summary = g_strdup(_("No supported updates backend detected."));
        state->details = g_strdup(_("Install a supported package manager CLI to see pending updates here."));
        return FALSE;
    }

    if (!stdout_text || !*stdout_text) {
        state->pending_count = 0;
        state->summary = g_strdup_printf(_("System is up to date according to %s."), state->backend);
        state->details = g_strdup(_("No pending package updates were reported."));
        g_free(stdout_text);
        return TRUE;
    }

    gchar **lines = g_strsplit(stdout_text, "\n", -1);
    GString *details = g_string_new("");
    for (guint i = 0; lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (!*line) {
            continue;
        }
        state->pending_count++;
        g_string_append_printf(details, "%s%s", state->pending_count > 1 ? "\n" : "", line);
    }
    g_strfreev(lines);
    g_free(stdout_text);

    state->summary = g_strdup_printf(_("%d package update(s) pending via %s."), state->pending_count, state->backend);
    state->details = g_string_free(details, FALSE);
    return TRUE;
}

gboolean
karton_storage_get_state(KartonStorageState *state)
{
    memset(state, 0, sizeof(*state));

    gchar *df_text = NULL;
    gchar *lsblk_text = NULL;
    gint status = 0;
    gboolean have_df = run_command_capture("df -h --output=source,size,used,avail,pcent,target | head -n 10", &df_text, NULL, &status) && status == 0;
    gboolean have_lsblk = run_command_capture("lsblk -o NAME,TYPE,SIZE,MOUNTPOINTS | head -n 12", &lsblk_text, NULL, &status) && status == 0;

    state->available = have_df || have_lsblk;
    if (!state->available) {
        g_free(df_text);
        g_free(lsblk_text);
        state->summary = g_strdup(_("Storage information is unavailable."));
        state->details = g_strdup(_("Neither df nor lsblk returned usable data."));
        return FALSE;
    }

    state->summary = g_strdup(_("Mounted volumes and block devices detected."));
    state->details = g_strdup_printf(_("Usage snapshot:\n%s\n\nDevices:\n%s"),
        df_text ? df_text : _("No filesystem usage data."),
        lsblk_text ? lsblk_text : _("No block device data."));
    g_free(df_text);
    g_free(lsblk_text);
    return TRUE;
}

gboolean
karton_privacy_get_state(KartonPrivacyState *state)
{
    memset(state, 0, sizeof(*state));

    if (!karton_command_exists("gsettings")) {
        state->summary = g_strdup(_("Privacy settings backend is unavailable."));
        state->details = g_strdup(_("Install gsettings support to manage privacy options here."));
        return FALSE;
    }

    state->available = TRUE;
    if (!gsettings_get_bool("org.gnome.desktop.screensaver", "lock-enabled", &state->lock_screen)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "privacy-screen", &state->privacy_screen)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "remember-recent-files", &state->remember_recent_files)) {
        state->available = FALSE;
    }

    gboolean disable_camera = FALSE;
    gboolean disable_microphone = FALSE;
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "disable-camera", &disable_camera)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "disable-microphone", &disable_microphone)) {
        state->available = FALSE;
    }
    state->camera_access = !disable_camera;
    state->microphone_access = !disable_microphone;
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "usb-protection", &state->usb_protection)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "hide-identity", &state->hide_identity)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "send-software-usage-stats", &state->send_usage_stats)) {
        state->available = FALSE;
    }
    if (!gsettings_get_bool("org.gnome.desktop.privacy", "report-technical-problems", &state->report_technical_problems)) {
        state->available = FALSE;
    }

    if (!state->available) {
        state->summary = g_strdup(_("Privacy schemas are only partially available."));
        state->details = g_strdup(_("Some privacy controls could not be read from the current desktop settings."));
        return FALSE;
    }

    state->summary = g_strdup_printf(_("Lock screen %s, recent files %s"),
        state->lock_screen ? _("enabled") : _("disabled"),
        state->remember_recent_files ? _("enabled") : _("disabled"));
    state->details = g_strdup_printf(
        _("Privacy screen: %s\nCamera access: %s\nMicrophone access: %s\nRecent files history: %s\nUSB protection: %s\nHide identity: %s\nUsage stats: %s\nTechnical reports: %s"),
        state->privacy_screen ? _("enabled") : _("disabled"),
        state->camera_access ? _("enabled") : _("disabled"),
        state->microphone_access ? _("enabled") : _("disabled"),
        state->remember_recent_files ? _("enabled") : _("disabled"),
        state->usb_protection ? _("enabled") : _("disabled"),
        state->hide_identity ? _("enabled") : _("disabled"),
        state->send_usage_stats ? _("enabled") : _("disabled"),
        state->report_technical_problems ? _("enabled") : _("disabled"));
    return TRUE;
}

gboolean
karton_privacy_apply(gboolean lock_screen,
    gboolean privacy_screen,
    gboolean remember_recent_files,
    gboolean camera_access,
    gboolean microphone_access,
    gboolean usb_protection,
    gboolean hide_identity,
    gboolean send_usage_stats,
    gboolean report_technical_problems,
    gchar **error_msg)
{
    if (!karton_command_exists("gsettings")) {
        if (error_msg) {
            *error_msg = g_strdup(_("gsettings is not available."));
        }
        return FALSE;
    }

    if (!gsettings_set_bool("org.gnome.desktop.screensaver", "lock-enabled", lock_screen, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "privacy-screen", privacy_screen, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "remember-recent-files", remember_recent_files, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "disable-camera", !camera_access, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "disable-microphone", !microphone_access, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "usb-protection", usb_protection, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "hide-identity", hide_identity, error_msg)) {
        return FALSE;
    }
    if (!gsettings_set_bool("org.gnome.desktop.privacy", "send-software-usage-stats", send_usage_stats, error_msg)) {
        return FALSE;
    }
    return gsettings_set_bool("org.gnome.desktop.privacy", "report-technical-problems", report_technical_problems, error_msg);
}

gboolean
karton_region_get_state(KartonRegionState *state)
{
    memset(state, 0, sizeof(*state));

    const char *lang_env = g_getenv("LANG");
    state->locale = g_strdup(lang_env && *lang_env ? lang_env : _("unknown"));

    if (karton_command_exists("timedatectl")) {
        state->timezone = query_command_first_line("timedatectl show --property=Timezone --value 2>/dev/null");
    }

    gchar *localectl_text = NULL;
    gint status = 0;
    if (karton_command_exists("localectl")
        && run_command_capture("localectl status 2>/dev/null | head -n 12", &localectl_text, NULL, &status)
        && status == 0
        && localectl_text) {
        gchar **lines = g_strsplit(localectl_text, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (g_str_has_prefix(line, "System Locale:")) {
                const char *value = strstr(line, "LANG=");
                if (value && *(value + 5)) {
                    g_free(state->locale);
                    state->locale = g_strdup(value + 5);
                }
            } else if (g_str_has_prefix(line, "VC Keymap:")) {
                const char *value = line + strlen("VC Keymap:");
                state->vc_keymap = g_strdup(g_strstrip((gchar *)value));
            } else if (g_str_has_prefix(line, "X11 Layout:")) {
                const char *value = line + strlen("X11 Layout:");
                state->x11_layout = g_strdup(g_strstrip((gchar *)value));
            }
        }
        g_strfreev(lines);
    }
    g_free(localectl_text);

    state->available = TRUE;
    state->summary = g_strdup_printf(_("Locale %s, timezone %s"),
        state->locale ? state->locale : _("unknown"),
        state->timezone ? state->timezone : _("unknown"));
    state->details = g_strdup_printf(
        _("System locale: %s\nTimezone: %s\nVC keymap: %s\nX11 layout: %s"),
        state->locale ? state->locale : _("unknown"),
        state->timezone ? state->timezone : _("unknown"),
        state->vc_keymap ? state->vc_keymap : _("not provided"),
        state->x11_layout ? state->x11_layout : _("not provided"));
    return TRUE;
}

gboolean
karton_region_open_tool(const char *tool, gchar **error_msg)
{
    if (g_strcmp0(tool, "time") == 0) {
        if (karton_command_exists("systemsettings")) {
            return run_command_async("systemsettings kcm_clock", error_msg);
        }
        if (karton_command_exists("gnome-control-center")) {
            return run_command_async("gnome-control-center datetime", error_msg);
        }
    } else if (g_strcmp0(tool, "language") == 0) {
        if (karton_command_exists("systemsettings")) {
            return run_command_async("systemsettings kcm_regionandlang", error_msg);
        }
        if (karton_command_exists("gnome-control-center")) {
            return run_command_async("gnome-control-center region", error_msg);
        }
    }

    if (error_msg) {
        *error_msg = g_strdup(_("No supported regional settings tool is available."));
    }
    return FALSE;
}

gboolean
karton_advanced_get_state(KartonAdvancedState *state)
{
    memset(state, 0, sizeof(*state));

    const char *session_type = g_getenv("XDG_SESSION_TYPE");
    const char *wayland_display = g_getenv("WAYLAND_DISPLAY");
    const char *x_display = g_getenv("DISPLAY");
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");

    state->available = TRUE;
    state->summary = g_strdup_printf(_("%s session, desktop %s"),
        session_type && *session_type ? session_type : _("unknown"),
        desktop && *desktop ? desktop : _("unknown"));
    state->details = g_strdup_printf(
        _("WAYLAND_DISPLAY: %s\nDISPLAY: %s\nwayland-info: %s\nxrandr: %s\nwlr-randr: %s\nloginctl: %s\ngsettings: %s\njournalctl: %s"),
        wayland_display && *wayland_display ? wayland_display : _("not set"),
        x_display && *x_display ? x_display : _("not set"),
        karton_command_exists("wayland-info") ? _("available") : _("missing"),
        karton_command_exists("xrandr") ? _("available") : _("missing"),
        karton_command_exists("wlr-randr") ? _("available") : _("missing"),
        karton_command_exists("loginctl") ? _("available") : _("missing"),
        karton_command_exists("gsettings") ? _("available") : _("missing"),
        karton_command_exists("journalctl") ? _("available") : _("missing"));
    return TRUE;
}

gboolean
karton_advanced_open_report(const char *tool, gchar **error_msg)
{
    const char *prefix = NULL;
    const char *capture_command = NULL;

    if (!karton_command_exists("xdg-open")) {
        if (error_msg) {
            *error_msg = g_strdup(_("No supported diagnostics command is available."));
        }
        return FALSE;
    }

    if (g_strcmp0(tool, "session") == 0) {
        if (karton_command_exists("journalctl")) {
            prefix = "session-journal";
            capture_command = "journalctl --user -b --no-pager";
        }
    } else if (g_strcmp0(tool, "display") == 0) {
        if (karton_command_exists("wayland-info")) {
            prefix = "display-report";
            capture_command = "wayland-info";
        } else if (karton_command_exists("wlr-randr")) {
            prefix = "display-report";
            capture_command = "wlr-randr";
        } else if (karton_command_exists("xrandr")) {
            prefix = "display-report";
            capture_command = "xrandr --query";
        }
    }

    if (!prefix || !capture_command) {
        if (error_msg) {
            *error_msg = g_strdup(_("No supported diagnostics command is available."));
        }
        return FALSE;
    }

    gchar *script = g_strdup_printf(
        "tmp=$(mktemp /tmp/karton-%s.XXXXXX.txt) && %s > \"$tmp\" 2>&1 && xdg-open \"$tmp\" >/dev/null 2>&1",
        prefix,
        capture_command);
    gchar *quoted_script = g_shell_quote(script);
    gchar *command = g_strdup_printf("sh -lc %s", quoted_script);
    gboolean ok = run_command_async(command, error_msg);
    g_free(command);
    g_free(quoted_script);
    g_free(script);
    return ok;
}

gchar *
karton_display_summary(void)
{
    gchar *stdout_text = NULL;
    gint status = 0;

    if (karton_command_exists("wlr-randr")
        && run_command_capture("wlr-randr | awk '/^[^[:space:]]/ {print $1}' | paste -sd ', ' -", &stdout_text, NULL, &status)
        && status == 0) {
        gchar *line = first_line_trimmed(stdout_text);
        g_free(stdout_text);
        if (*line) {
            gchar *summary = g_strdup_printf("Active outputs: %s", line);
            g_free(line);
            return summary;
        }
        g_free(line);
    }
    g_free(stdout_text);

    stdout_text = NULL;
    if (karton_command_exists("xrandr")
        && run_command_capture("xrandr --query | awk '/ connected/{print $1}' | paste -sd ', ' -", &stdout_text, NULL, &status)
        && status == 0) {
        gchar *line = first_line_trimmed(stdout_text);
        g_free(stdout_text);
        if (*line) {
            gchar *summary = g_strdup_printf("Active outputs: %s", line);
            g_free(line);
            return summary;
        }
        g_free(line);
    }
    g_free(stdout_text);
    return g_strdup(_("Display information will appear when a display backend is detected."));
}

gchar *
karton_input_summary(void)
{
    return g_strdup(_("Use this section for pointer speed, touchpad gestures and keyboard layout shortcuts."));
}

gchar *
karton_system_summary(void)
{
    const gchar *pretty_name = g_get_os_info(G_OS_INFO_KEY_PRETTY_NAME);
    if (!pretty_name || !*pretty_name) {
        pretty_name = "Linux";
    }

    gchar *stdout_text = NULL;
    gint status = 0;
    if (run_command_capture("uname -srmo", &stdout_text, NULL, &status) && status == 0) {
        gchar *line = first_line_trimmed(stdout_text);
        gchar *summary = g_strdup_printf("%s\n%s", pretty_name, *line ? line : _("Kernel information unavailable"));
        g_free(line);
        g_free(stdout_text);
        return summary;
    }

    g_free(stdout_text);
    return g_strdup(pretty_name);
}