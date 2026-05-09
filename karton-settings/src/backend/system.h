// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#pragma once

#include <gtk/gtk.h>

typedef struct {
    gboolean available;
    gboolean enabled;
    gchar *summary;
    gchar *details;
} KartonToggleState;

typedef struct {
    gboolean available;
    gboolean screen_reader;
    gboolean screen_keyboard;
    gboolean screen_magnifier;
    gboolean sticky_keys;
    gboolean mouse_keys;
    gboolean slow_keys;
    double text_scale;
    gchar *summary;
    gchar *details;
} KartonAccessibilityState;

typedef struct {
    gboolean available;
    gboolean muted;
    double level;
    gboolean input_available;
    gboolean input_muted;
    double input_level;
    gchar *summary;
    gchar *details;
} KartonAudioState;

typedef struct {
    gboolean available;
    gchar *current_profile;
    gchar *summary;
} KartonPowerState;

typedef struct {
    gboolean available;
    double interface_scale;
    gboolean brightness_available;
    double brightness_level;
    gchar *brightness_backend;
    gchar *backend;
    gchar *current_output;
    gchar *current_mode;
    gchar *current_orientation;
    gchar *available_outputs;
    gchar *available_modes;
    gboolean can_configure;
    gchar *summary;
    gchar *details;
} KartonDisplayState;

typedef struct {
    gboolean available;
    double mouse_speed;
    gboolean natural_scroll;
    gboolean tap_to_click;
    gboolean left_handed;
    gchar *summary;
    gchar *details;
} KartonInputState;

typedef struct {
    gchar *username;
    gchar *full_name;
    gchar *shell;
    gboolean admin;
    gchar *summary;
    gchar *details;
} KartonUserState;

typedef struct {
    gchar *session_type;
    gchar *session_name;
    gchar *desktop_name;
    gint autostart_count;
    gchar *summary;
    gchar *details;
    gchar *autostart_entries;
    gchar *autostart_selected;
    gboolean autostart_selected_enabled;
    gchar *autostart_preview;
} KartonSessionState;

typedef struct {
    gchar *browser;
    gchar *file_manager;
    gchar *text_editor;
    gchar *mail_app;
    gchar *audio_app;
    gchar *video_app;
    gchar *summary;
    gchar *details;
} KartonDefaultAppsState;

typedef struct {
    gboolean available;
    gchar *backend;
    gint pending_count;
    gchar *summary;
    gchar *details;
} KartonUpdatesState;

typedef struct {
    gboolean available;
    gchar *summary;
    gchar *details;
} KartonStorageState;

typedef struct {
    gboolean available;
    gboolean lock_screen;
    gboolean privacy_screen;
    gboolean remember_recent_files;
    gboolean camera_access;
    gboolean microphone_access;
    gboolean usb_protection;
    gboolean hide_identity;
    gboolean send_usage_stats;
    gboolean report_technical_problems;
    gchar *summary;
    gchar *details;
} KartonPrivacyState;

typedef struct {
    gboolean available;
    gchar *locale;
    gchar *timezone;
    gchar *vc_keymap;
    gchar *x11_layout;
    gchar *summary;
    gchar *details;
} KartonRegionState;

typedef struct {
    gboolean available;
    gchar *summary;
    gchar *details;
} KartonAdvancedState;

void karton_toggle_state_clear(KartonToggleState *state);
void karton_accessibility_state_clear(KartonAccessibilityState *state);
void karton_audio_state_clear(KartonAudioState *state);
void karton_power_state_clear(KartonPowerState *state);
void karton_display_state_clear(KartonDisplayState *state);
void karton_input_state_clear(KartonInputState *state);
void karton_user_state_clear(KartonUserState *state);
void karton_session_state_clear(KartonSessionState *state);
void karton_default_apps_state_clear(KartonDefaultAppsState *state);
void karton_updates_state_clear(KartonUpdatesState *state);
void karton_storage_state_clear(KartonStorageState *state);
void karton_privacy_state_clear(KartonPrivacyState *state);
void karton_region_state_clear(KartonRegionState *state);
void karton_advanced_state_clear(KartonAdvancedState *state);

gboolean karton_wifi_get_state(KartonToggleState *state);
gboolean karton_wifi_set_enabled(gboolean enabled, gchar **error_msg);

gboolean karton_bluetooth_get_state(KartonToggleState *state);
gboolean karton_bluetooth_set_enabled(gboolean enabled, gchar **error_msg);

gboolean karton_notifications_get_state(KartonToggleState *state);
gboolean karton_notifications_set_dnd(gboolean enabled, gchar **error_msg);

gboolean karton_accessibility_get_state(KartonAccessibilityState *state);
gboolean karton_accessibility_apply(gboolean screen_reader,
    gboolean screen_keyboard,
    gboolean screen_magnifier,
    gboolean sticky_keys,
    gboolean mouse_keys,
    gboolean slow_keys,
    double text_scale,
    gchar **error_msg);

gboolean karton_audio_get_state(KartonAudioState *state);
gboolean karton_audio_apply(double output_level,
    gboolean output_muted,
    double input_level,
    gboolean input_muted,
    gchar **error_msg);

gboolean karton_power_get_state(KartonPowerState *state);
gboolean karton_power_set_profile(const char *profile, gchar **error_msg);

gboolean karton_display_get_state(KartonDisplayState *state);
gboolean karton_display_set_interface_scale(double scale, gchar **error_msg);
gboolean karton_display_set_brightness(double percent, gchar **error_msg);
gboolean karton_display_apply_mode(const char *output,
    const char *mode,
    const char *orientation,
    gchar **error_msg);

gboolean karton_input_get_state(KartonInputState *state);
gboolean karton_input_apply(double mouse_speed,
    gboolean natural_scroll,
    gboolean tap_to_click,
    gboolean left_handed,
    gchar **error_msg);

gboolean karton_user_get_state(KartonUserState *state);

gboolean karton_session_get_state(KartonSessionState *state);
gboolean karton_session_run_action(const char *action, gchar **error_msg);
gboolean karton_session_autostart_lookup(const char *desktop_id, gboolean *enabled_out, gchar **details_out);
gboolean karton_session_autostart_set_enabled(const char *desktop_id, gboolean enabled, gchar **error_msg);

gboolean karton_default_apps_get_state(KartonDefaultAppsState *state);
gboolean karton_updates_get_state(KartonUpdatesState *state);
gboolean karton_storage_get_state(KartonStorageState *state);
gboolean karton_privacy_get_state(KartonPrivacyState *state);
gboolean karton_privacy_apply(gboolean lock_screen,
    gboolean privacy_screen,
    gboolean remember_recent_files,
    gboolean camera_access,
    gboolean microphone_access,
    gboolean usb_protection,
    gboolean hide_identity,
    gboolean send_usage_stats,
    gboolean report_technical_problems,
    gchar **error_msg);
gboolean karton_region_get_state(KartonRegionState *state);
gboolean karton_region_open_tool(const char *tool, gchar **error_msg);
gboolean karton_advanced_get_state(KartonAdvancedState *state);
gboolean karton_advanced_open_report(const char *tool, gchar **error_msg);

gchar *karton_display_summary(void);
gchar *karton_input_summary(void);
gchar *karton_system_summary(void);