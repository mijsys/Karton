// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#pragma once

#include <gtk/gtk.h>

typedef enum {
    KARTON_THEME_AUTO = 0,
    KARTON_THEME_LIGHT,
    KARTON_THEME_DARK,
} KartonThemeMode;

typedef struct {
    const char *id;
    const char *name;
    const char *hex;
} KartonAccent;

extern const KartonAccent karton_accents[];
extern const guint karton_accents_count;

gboolean karton_command_exists(const char *name);
char *karton_config_path(const char *leaf);

KartonThemeMode karton_theme_mode_load(void);
const char *karton_theme_mode_name(KartonThemeMode mode);
gboolean karton_theme_mode_apply(KartonThemeMode mode, gchar **error_msg);

char *karton_accent_load(void);
const KartonAccent *karton_accent_lookup(const char *hex);
gboolean karton_accent_apply(const char *hex, gchar **error_msg);

char *karton_font_load(void);
gboolean karton_font_apply(const char *font_name, gchar **error_msg);