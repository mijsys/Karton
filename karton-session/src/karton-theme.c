// SPDX-License-Identifier: GPL-2.0-only

#include "karton-theme.h"

#include <gio/gio.h>

static char *theme_mode_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "theme-mode", NULL);
}

static const char *theme_mode_to_text(KartonThemeMode mode) {
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

static KartonThemeMode theme_mode_from_text(const char *text) {
    if (g_strcmp0(text, "light") == 0) {
        return KARTON_THEME_LIGHT;
    }
    if (g_strcmp0(text, "dark") == 0) {
        return KARTON_THEME_DARK;
    }
    return KARTON_THEME_AUTO;
}

GSettings *karton_theme_open_interface_settings(void) {
    GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = NULL;
    GSettings *interface_settings = NULL;

    if (schema_source) {
        schema = g_settings_schema_source_lookup(schema_source, "org.gnome.desktop.interface", TRUE);
    }

    if (!schema) {
        return NULL;
    }

    interface_settings = g_settings_new_full(schema, NULL, NULL);
    g_settings_schema_unref(schema);
    return interface_settings;
}

KartonThemeMode karton_theme_mode_read(void) {
    char *path = theme_mode_path();
    char *content = NULL;
    KartonThemeMode mode = KARTON_THEME_AUTO;

    if (g_file_get_contents(path, &content, NULL, NULL)) {
        g_strstrip(content);
        mode = theme_mode_from_text(content);
    }

    g_free(content);
    g_free(path);
    return mode;
}

gboolean karton_theme_mode_effective_dark(KartonThemeMode mode) {
    if (mode == KARTON_THEME_DARK) {
        return TRUE;
    }

    if (mode == KARTON_THEME_LIGHT) {
        return FALSE;
    }

    /* AUTO: prefer system color-scheme if available. */
    GSettings *interface_settings = karton_theme_open_interface_settings();

    if (interface_settings) {
        gchar *scheme = g_settings_get_string(interface_settings, "color-scheme");
        if (scheme) {
            if (g_strcmp0(scheme, "prefer-dark") == 0) {
                g_free(scheme);
                g_object_unref(interface_settings);
                return TRUE;
            }

            if (g_strcmp0(scheme, "prefer-light") == 0 || g_strcmp0(scheme, "default") == 0) {
                g_free(scheme);
                g_object_unref(interface_settings);
                return FALSE;
            }

            g_free(scheme);
        }

        g_object_unref(interface_settings);
    }

    GDateTime *now = g_date_time_new_now_local();
    gint hour = g_date_time_get_hour(now);
    g_date_time_unref(now);

    return (hour >= 19 || hour < 7);
}

void karton_theme_mode_apply(void) {
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) {
        return;
    }

    g_object_set(
        settings,
        "gtk-application-prefer-dark-theme",
        karton_theme_mode_effective_dark(KARTON_THEME_AUTO),
        NULL
    );
}

gboolean karton_theme_mode_write(KartonThemeMode mode) {
    char *path = theme_mode_path();
    char *dir = g_path_get_dirname(path);
    gboolean ok;

    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_free(dir);
        g_free(path);
        return FALSE;
    }

    ok = g_file_set_contents(path, theme_mode_to_text(mode), -1, NULL);

    g_free(dir);
    g_free(path);
    return ok;
}

KartonThemeMode karton_theme_mode_toggle_dark_light(void) {
    KartonThemeMode current = karton_theme_mode_read();
    KartonThemeMode next = (current == KARTON_THEME_DARK) ? KARTON_THEME_LIGHT : KARTON_THEME_DARK;

    if (!karton_theme_mode_write(next)) {
        return current;
    }

    karton_theme_mode_apply();
    return next;
}

const char *karton_theme_mode_label(KartonThemeMode mode) {
    if (karton_theme_mode_effective_dark(mode)) {
        return "Dark";
    }

    return "Light";
}
