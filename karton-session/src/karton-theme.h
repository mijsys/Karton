// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <gtk/gtk.h>

typedef enum {
    KARTON_THEME_LIGHT = 0,
    KARTON_THEME_DARK = 1,
    KARTON_THEME_AUTO = 2,
} KartonThemeMode;

KartonThemeMode karton_theme_mode_read(void);
gboolean karton_theme_mode_effective_dark(KartonThemeMode mode);
void karton_theme_mode_apply(void);
GSettings *karton_theme_open_interface_settings(void);
gboolean karton_theme_mode_write(KartonThemeMode mode);
KartonThemeMode karton_theme_mode_toggle_dark_light(void);
const char *karton_theme_mode_label(KartonThemeMode mode);
