// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include "backend/config.h"

#include <glib/gstdio.h>

const KartonAccent karton_accents[] = {
    { "accent-violet", "Aurora Violet", "#7c5cff" },
    { "accent-cyan", "Sky Cyan", "#36b7ff" },
    { "accent-teal", "Mint Teal", "#30c7b5" },
    { "accent-green", "Leaf Green", "#76bf57" },
    { "accent-amber", "Amber Glow", "#f0ad2e" },
    { "accent-orange", "Sunset Orange", "#ff8a3d" },
    { "accent-rose", "Rose Pink", "#f4658a" },
    { "accent-red", "Signal Red", "#ec5a5a" },
};

const guint karton_accents_count = G_N_ELEMENTS(karton_accents);

static gboolean
ensure_config_dir(gchar **error_msg)
{
    gchar *config_dir = g_build_filename(g_get_user_config_dir(), "karton", NULL);
    gboolean ok = g_mkdir_with_parents(config_dir, 0755) == 0;
    if (!ok && error_msg) {
        *error_msg = g_strdup("Cannot create ~/.config/karton");
    }
    g_free(config_dir);
    return ok;
}

static gboolean
write_file(const char *path, const char *content, gchar **error_msg)
{
    if (g_file_set_contents(path, content, -1, NULL)) {
        return TRUE;
    }
    if (error_msg) {
        *error_msg = g_strdup_printf("Cannot write %s", path);
    }
    return FALSE;
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
    gchar *out = NULL;
    gchar *err = NULL;
    gint status = 0;

    if (!run_command_capture(command, &out, &err, &status)) {
        if (error_msg && !*error_msg) {
            *error_msg = g_strdup("Cannot execute command");
        }
        return FALSE;
    }

    g_free(out);
    if (status == 0) {
        g_free(err);
        return TRUE;
    }

    if (error_msg) {
        *error_msg = g_strdup(err && *err ? err : "Command returned non-zero status");
    }
    g_free(err);
    return FALSE;
}

static gchar *
read_gtk_setting(const char *key)
{
    const char *dirs[] = { "gtk-4.0", "gtk-3.0" };

    for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
        gchar *path = g_build_filename(g_get_user_config_dir(), dirs[i], "settings.ini", NULL);
        GKeyFile *key_file = g_key_file_new();

        if (g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)
            && g_key_file_has_key(key_file, "Settings", key, NULL)) {
            gchar *value = g_key_file_get_string(key_file, "Settings", key, NULL);
            g_key_file_free(key_file);
            g_free(path);
            return value;
        }

        g_key_file_free(key_file);
        g_free(path);
    }

    return NULL;
}

static gboolean
write_gtk_setting(const char *key, const char *value, gchar **error_msg)
{
    const char *dirs[] = { "gtk-3.0", "gtk-4.0" };

    for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
        gchar *dir = g_build_filename(g_get_user_config_dir(), dirs[i], NULL);
        if (g_mkdir_with_parents(dir, 0755) != 0) {
            if (error_msg) {
                *error_msg = g_strdup("Cannot create GTK config directory");
            }
            g_free(dir);
            return FALSE;
        }
        g_free(dir);

        gchar *path = g_build_filename(g_get_user_config_dir(), dirs[i], "settings.ini", NULL);
        GKeyFile *key_file = g_key_file_new();
        g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);
        g_key_file_set_string(key_file, "Settings", key, value);

        gsize data_len = 0;
        gchar *data = g_key_file_to_data(key_file, &data_len, NULL);
        gboolean ok = g_file_set_contents(path, data, (gssize)data_len, NULL);

        g_free(data);
        g_key_file_free(key_file);
        g_free(path);

        if (!ok) {
            if (error_msg) {
                *error_msg = g_strdup("Cannot write GTK settings.ini");
            }
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
karton_command_exists(const char *name)
{
    gchar *path = g_find_program_in_path(name);
    if (!path) {
        return FALSE;
    }
    g_free(path);
    return TRUE;
}

char *
karton_config_path(const char *leaf)
{
    return g_build_filename(g_get_user_config_dir(), "karton", leaf, NULL);
}

KartonThemeMode
karton_theme_mode_load(void)
{
    gchar *mode_path = karton_config_path("theme-mode");
    gchar *content = NULL;
    KartonThemeMode mode = KARTON_THEME_AUTO;

    if (g_file_get_contents(mode_path, &content, NULL, NULL) && content) {
        g_strstrip(content);
        if (g_ascii_strcasecmp(content, "light") == 0) {
            mode = KARTON_THEME_LIGHT;
        } else if (g_ascii_strcasecmp(content, "dark") == 0) {
            mode = KARTON_THEME_DARK;
        }
    }

    g_free(content);
    g_free(mode_path);
    return mode;
}

const char *
karton_theme_mode_name(KartonThemeMode mode)
{
    switch (mode) {
    case KARTON_THEME_LIGHT:
        return "light";
    case KARTON_THEME_DARK:
        return "dark";
    case KARTON_THEME_AUTO:
    default:
        return "auto";
    }
}

gboolean
karton_theme_mode_apply(KartonThemeMode mode, gchar **error_msg)
{
    if (!ensure_config_dir(error_msg)) {
        return FALSE;
    }

    gchar *mode_path = karton_config_path("theme-mode");
    const char *mode_name = karton_theme_mode_name(mode);
    gboolean ok = write_file(mode_path, mode_name, error_msg);
    g_free(mode_path);
    if (!ok) {
        return FALSE;
    }

    gchar *local_apply = g_build_filename(g_get_home_dir(), ".local-karton", "bin", "karton-apply-theme", NULL);
    gchar *cmd = NULL;
    if (g_file_test(local_apply, G_FILE_TEST_IS_EXECUTABLE)) {
        cmd = g_strdup_printf("%s %s", local_apply, mode_name);
    } else {
        cmd = g_strdup_printf("karton-apply-theme %s", mode_name);
    }
    g_free(local_apply);

    ok = run_command_ok(cmd, error_msg);
    g_free(cmd);
    return ok;
}

char *
karton_accent_load(void)
{
    gchar *accent_path = karton_config_path("accent-color");
    gchar *content = NULL;

    if (g_file_get_contents(accent_path, &content, NULL, NULL) && content) {
        g_strstrip(content);
        if (g_regex_match_simple("^#[0-9A-Fa-f]{6}$", content, 0, 0)) {
            g_free(accent_path);
            return content;
        }
    }

    g_free(content);
    g_free(accent_path);
    return g_strdup(karton_accents[0].hex);
}

const KartonAccent *
karton_accent_lookup(const char *hex)
{
    if (!hex) {
        return &karton_accents[0];
    }

    for (guint i = 0; i < karton_accents_count; i++) {
        if (g_ascii_strcasecmp(karton_accents[i].hex, hex) == 0) {
            return &karton_accents[i];
        }
    }

    return &karton_accents[0];
}

gboolean
karton_accent_apply(const char *hex, gchar **error_msg)
{
    if (!hex || !g_regex_match_simple("^#[0-9A-Fa-f]{6}$", hex, 0, 0)) {
        if (error_msg) {
            *error_msg = g_strdup("Accent color must be a hex RGB value");
        }
        return FALSE;
    }

    if (!ensure_config_dir(error_msg)) {
        return FALSE;
    }

    gchar *accent_path = karton_config_path("accent-color");
    gboolean ok = write_file(accent_path, hex, error_msg);
    g_free(accent_path);
    if (!ok) {
        return FALSE;
    }

    return karton_theme_mode_apply(karton_theme_mode_load(), error_msg);
}

char *
karton_font_load(void)
{
    gchar *font = read_gtk_setting("gtk-font-name");
    if (font && *font) {
        return font;
    }

    g_free(font);
    return g_strdup("Sans 11");
}

gboolean
karton_font_apply(const char *font_name, gchar **error_msg)
{
    if (!font_name || !*font_name) {
        if (error_msg) {
            *error_msg = g_strdup("Font name cannot be empty");
        }
        return FALSE;
    }

    return write_gtk_setting("gtk-font-name", font_name, error_msg);
}