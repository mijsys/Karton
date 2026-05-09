// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include "ui/window.h"

#include <math.h>
#include <libintl.h>
#include <string.h>

#include "backend/config.h"
#include "backend/system.h"

#define _(s) gettext(s)
#define N_(s) s

typedef struct {
    const char *id;
    const char *title;
    const char *subtitle;
    const char *icon_name;
    const char *purpose;
    const char *const *items;
} KartonCategory;

typedef struct {
    GtkApplicationWindow *window;
    GtkListBox *sidebar_list;
    GtkLabel *page_title;
    GtkLabel *page_subtitle;
    GtkLabel *status_label;
    GtkStack *stack;

    GtkWidget *theme_buttons[3];
    GtkEntry *font_entry;
    GPtrArray *accent_buttons;
    gchar *selected_accent;

    GtkSwitch *wifi_switch;
    GtkLabel *wifi_summary;
    GtkLabel *wifi_details;
    GtkSwitch *bluetooth_switch;
    GtkLabel *bluetooth_summary;
    GtkLabel *bluetooth_details;
    GtkScale *volume_scale;
    GtkSwitch *mute_switch;
    GtkScale *mic_scale;
    GtkSwitch *mic_mute_switch;
    GtkLabel *audio_summary;
    GtkLabel *audio_details;
    GtkDropDown *power_profile_drop;
    GtkLabel *power_summary;
    GtkSwitch *dnd_switch;
    GtkLabel *notifications_summary;
    GtkLabel *display_summary;
    GtkLabel *display_details;
    GtkLabel *display_output_label;
    GtkScale *display_brightness_scale;
    GtkLabel *display_brightness_summary;
    GtkDropDown *display_scale_drop;
    GtkDropDown *display_mode_drop;
    GtkDropDown *display_orientation_drop;
    GtkScale *mouse_speed_scale;
    GtkSwitch *natural_scroll_switch;
    GtkSwitch *tap_to_click_switch;
    GtkSwitch *left_handed_switch;
    GtkLabel *input_summary;
    GtkLabel *input_details;
    GtkSwitch *accessibility_reader_switch;
    GtkSwitch *accessibility_keyboard_switch;
    GtkSwitch *accessibility_magnifier_switch;
    GtkSwitch *accessibility_sticky_keys_switch;
    GtkSwitch *accessibility_mouse_keys_switch;
    GtkSwitch *accessibility_slow_keys_switch;
    GtkScale *accessibility_text_scale;
    GtkLabel *accessibility_summary;
    GtkLabel *accessibility_details;
    GtkSwitch *privacy_lock_switch;
    GtkSwitch *privacy_screen_switch;
    GtkSwitch *privacy_recent_files_switch;
    GtkSwitch *privacy_camera_switch;
    GtkSwitch *privacy_microphone_switch;
    GtkSwitch *privacy_usb_switch;
    GtkSwitch *privacy_identity_switch;
    GtkSwitch *privacy_usage_stats_switch;
    GtkSwitch *privacy_reports_switch;
    GtkLabel *privacy_summary;
    GtkLabel *privacy_details;
    GtkLabel *user_summary;
    GtkLabel *user_details;
    GtkLabel *session_summary;
    GtkLabel *session_details;
    GtkSearchEntry *autostart_search;
    GtkDropDown *autostart_drop;
    GtkSwitch *autostart_switch;
    GtkLabel *autostart_details;
    gchar *autostart_entries_raw;
    gchar **autostart_ids;
    GtkLabel *defaults_summary;
    GtkLabel *defaults_details;
    GtkLabel *updates_summary;
    GtkLabel *updates_details;
    GtkLabel *region_summary;
    GtkLabel *region_details;
    GtkLabel *advanced_summary;
    GtkLabel *advanced_details;
    GtkLabel *storage_summary;
    GtkLabel *storage_details;
    GtkLabel *system_summary;

    gboolean syncing_appearance;
    gboolean syncing_display;
    gboolean syncing_input;
    gboolean syncing_wifi;
    gboolean syncing_bluetooth;
    gboolean syncing_audio;
    gboolean syncing_power;
    gboolean syncing_notifications;
    gboolean syncing_accessibility;
    gboolean syncing_privacy;
    gboolean syncing_session;
} KartonSettingsWindow;

static const char *const appearance_items[] = {
    N_("Light, dark and automatic global theme modes"),
    N_("Accent color shared across shell surfaces"),
    N_("System font and interface density preparation"),
    N_("Wallpaper, lock screen and panel styling roadmap"),
    NULL,
};

static const char *const desktop_items[] = {
    N_("Panel placement and auto-hide behavior"),
    N_("Application menu and launcher layout"),
    N_("Widgets, workspaces and desktop shortcuts"),
    N_("Desktop icon and folder visibility"),
    NULL,
};

static const char *const windows_items[] = {
    N_("Window snapping and tiling rules"),
    N_("Focus behavior and Alt-Tab flow"),
    N_("Open and close animations"),
    N_("Virtual desktop and decoration rules"),
    NULL,
};

static const char *const display_items[] = {
    N_("Resolution, refresh rate and orientation"),
    N_("Multiple displays and scaling"),
    N_("Night light, color profiles and HDR roadmap"),
    N_("Adaptive sync and image quality controls"),
    NULL,
};

static const char *const sound_items[] = {
    N_("Output volume and mute state"),
    N_("Input and output device routing"),
    N_("Microphone level and audio profiles"),
    N_("System effects and multimedia handling"),
    NULL,
};

static const char *const input_items[] = {
    N_("Mouse speed and touchpad behavior"),
    N_("Keyboard layout and shortcut profiles"),
    N_("Gesture configuration and natural scrolling"),
    N_("Extended controller and tablet support roadmap"),
    NULL,
};

static const char *const network_items[] = {
    N_("Wi-Fi, Ethernet and DNS controls"),
    N_("VPN, proxy and hotspot management"),
    N_("IPv4 and IPv6 configuration"),
    N_("Firewall and network sharing roadmap"),
    NULL,
};

static const char *const bluetooth_items[] = {
    N_("Device pairing and visibility"),
    N_("Headsets, controllers and transfers"),
    N_("Bluetooth profiles and wireless accessories"),
    NULL,
};

static const char *const power_items[] = {
    N_("Performance and battery saver profiles"),
    N_("Sleep, hibernate and lid behavior"),
    N_("Charging limits and battery statistics roadmap"),
    NULL,
};

static const char *const users_items[] = {
    N_("Accounts, passwords and profile pictures"),
    N_("Administrator rights and session defaults"),
    N_("Autologin and user group management"),
    NULL,
};

static const char *const privacy_items[] = {
    N_("Screen lock and encryption"),
    N_("App permissions, camera and microphone access"),
    N_("Location, firewall and sandbox policies"),
    N_("Secure boot and security framework roadmap"),
    NULL,
};

static const char *const notifications_items[] = {
    N_("Do not disturb and banner behavior"),
    N_("History, alert sounds and app priorities"),
    N_("Placement and notification lifecycle"),
    NULL,
};

static const char *const defaults_items[] = {
    N_("Preferred browser, terminal and file manager"),
    N_("Mail, music, video and text editor defaults"),
    N_("File associations and launch actions roadmap"),
    NULL,
};

static const char *const updates_items[] = {
    N_("System updates and repositories"),
    N_("Drivers, application stores and runtime formats"),
    N_("Automatic update policy"),
    NULL,
};

static const char *const region_items[] = {
    N_("Language, locale and keyboard format"),
    N_("Timezone, date format and regional units"),
    N_("System internationalization"),
    NULL,
};

static const char *const accessibility_items[] = {
    N_("Screen reader and zoom controls"),
    N_("High contrast, captions and color filters"),
    N_("On-screen keyboard and slow keys"),
    NULL,
};

static const char *const storage_items[] = {
    N_("Disk mounting and automount policy"),
    N_("Permissions, trash and thumbnails"),
    N_("Network shares and disk encryption roadmap"),
    NULL,
};

static const char *const session_items[] = {
    N_("Autostart applications and services"),
    N_("Login manager and session selection"),
    N_("Restart, logout and restore behavior"),
    NULL,
};

static const char *const system_items[] = {
    N_("Operating system, kernel and hardware overview"),
    N_("Driver, memory and storage diagnostics"),
    N_("Logs and runtime health information"),
    NULL,
};

static const char *const advanced_items[] = {
    N_("Debugging and environment variables"),
    N_("Wayland or X11 session controls"),
    N_("Renderer, compositor and experimental flags"),
    NULL,
};

static const KartonCategory categories[] = {
    { "appearance", N_("Appearance"), N_("Global theme, accent and desktop style"), "preferences-desktop-theme-symbolic", N_("Tune the visual identity of Karton and keep it consistent across shell surfaces."), appearance_items },
    { "desktop", N_("Desktop"), N_("Panels, widgets and workspace layout"), "user-desktop-symbolic", N_("Organize how the main workspace is arranged and navigated."), desktop_items },
    { "windows", N_("Windows"), N_("Tiling, focus and decoration rules"), "preferences-system-windows-symbolic", N_("Control multitasking and window management behavior."), windows_items },
    { "display", N_("Displays"), N_("Resolution, scaling and connected outputs"), "video-display-symbolic", N_("Configure display hardware and image quality."), display_items },
    { "sound", N_("Sound"), N_("Audio output, input and media behavior"), "audio-speakers-symbolic", N_("Manage playback, microphones and multimedia routing."), sound_items },
    { "input", N_("Mouse and Touchpad"), N_("Pointer, touchpad and keyboard interaction"), "input-mouse-symbolic", N_("Adjust the way input devices behave."), input_items },
    { "network", N_("Network"), N_("Internet access and connectivity"), "network-wireless-symbolic", N_("Connect the desktop to networks and online services."), network_items },
    { "bluetooth", N_("Bluetooth"), N_("Wireless accessories and pairing"), "bluetooth-active-symbolic", N_("Manage wireless devices and profiles."), bluetooth_items },
    { "power", N_("Power"), N_("Profiles, battery and sleep behavior"), "battery-good-symbolic", N_("Balance performance, thermals and battery life."), power_items },
    { "users", N_("Users"), N_("Accounts and permissions"), "avatar-default-symbolic", N_("Create and manage local user accounts."), users_items },
    { "privacy", N_("Security and Privacy"), N_("Protection, permissions and policies"), "changes-prevent-symbolic", N_("Protect system data and control what apps can access."), privacy_items },
    { "notifications", N_("Notifications"), N_("Alerts, history and interruptions"), "preferences-system-notifications-symbolic", N_("Choose how the system communicates with you."), notifications_items },
    { "defaults", N_("Default Apps"), N_("Preferred handlers for common tasks"), "preferences-desktop-apps-symbolic", N_("Set the applications Karton should prefer by default."), defaults_items },
    { "updates", N_("Updates"), N_("Software sources and maintenance"), "software-update-available-symbolic", N_("Install and maintain system software."), updates_items },
    { "region", N_("Date, Time and Region"), N_("Language, locale and regional formats"), "preferences-system-time-symbolic", N_("Adapt the desktop to your language and region."), region_items },
    { "accessibility", N_("Accessibility"), N_("Visibility, reading and input assistance"), "preferences-desktop-accessibility-symbolic", N_("Make the desktop usable in a wider range of situations."), accessibility_items },
    { "storage", N_("Files and Storage"), N_("Disks, mounts and file handling"), "drive-harddisk-symbolic", N_("Manage storage devices and file-system behavior."), storage_items },
    { "session", N_("Session and Startup"), N_("Login flow and session services"), "system-run-symbolic", N_("Control what starts with the session and how it restarts."), session_items },
    { "system", N_("System"), N_("Device information and diagnostics"), "dialog-information-symbolic", N_("Review the current state of the machine and runtime."), system_items },
    { "advanced", N_("Advanced"), N_("Developer and experimental options"), "applications-engineering-symbolic", N_("Expose lower-level controls for advanced users."), advanced_items },
};

static const char *const power_profile_values[] = { "power-saver", "balanced", "performance", NULL };
static const double display_scale_values[] = { 1.0, 1.10, 1.25, 1.50, 1.75, 2.0 };

static void refresh_all(KartonSettingsWindow *self);
static void set_status(KartonSettingsWindow *self, const char *text, gboolean success);

static gboolean
refresh_all_idle(gpointer user_data)
{
    KartonSettingsWindow *self = user_data;

    if (!self || !self->window) {
        return G_SOURCE_REMOVE;
    }

    refresh_all(self);
    set_status(self, _("Settings shell initialized."), TRUE);
    return G_SOURCE_REMOVE;
}
static void apply_css(void);

static const KartonCategory *
lookup_category(const char *id)
{
    for (guint i = 0; i < G_N_ELEMENTS(categories); i++) {
        if (g_strcmp0(categories[i].id, id) == 0) {
            return &categories[i];
        }
    }
    return &categories[0];
}

static void
set_status(KartonSettingsWindow *self, const char *text, gboolean success)
{
    gtk_label_set_text(self->status_label, text ? text : "");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-ok");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-err");
    gtk_widget_add_css_class(GTK_WIDGET(self->status_label), success ? "status-ok" : "status-err");
}

static GtkWidget *
create_card(const char *title, const char *subtitle)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(card, "card");

    if (title) {
        GtkWidget *title_label = gtk_label_new(title);
        gtk_widget_add_css_class(title_label, "card-title");
        gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
        gtk_box_append(GTK_BOX(card), title_label);
    }

    if (subtitle && *subtitle) {
        GtkWidget *subtitle_label = gtk_label_new(subtitle);
        gtk_widget_add_css_class(subtitle_label, "card-subtitle");
        gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(subtitle_label), TRUE);
        gtk_box_append(GTK_BOX(card), subtitle_label);
    }

    return card;
}

static GtkWidget *
create_label_block(const char *text, const char *css_class)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    if (css_class) {
        gtk_widget_add_css_class(label, css_class);
    }
    return label;
}

static GtkWidget *
create_scope_card(const KartonCategory *category)
{
    GtkWidget *card = create_card(_("Included scope"), _(category->purpose));
    GString *text = g_string_new("");
    for (guint i = 0; category->items[i]; i++) {
        g_string_append_printf(text, "- %s", _(category->items[i]));
        if (category->items[i + 1]) {
            g_string_append_c(text, '\n');
        }
    }

    GtkWidget *label = create_label_block(text->str, NULL);
    gtk_box_append(GTK_BOX(card), label);
    g_string_free(text, TRUE);
    return card;
}

static void
apply_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    GString *css = g_string_new(
        "window.settings-window {"
        "  background: radial-gradient(circle at top left, rgba(151,188,255,0.28), transparent 26%),"
        "              linear-gradient(180deg, #f2f6ff 0%, #fbfdff 100%);"
        "}"
        ".layout { padding: 18px; }"
        ".sidebar {"
        "  min-width: 292px;"
        "  padding: 20px;"
        "  border-radius: 30px;"
        "  background: linear-gradient(180deg, rgba(255,255,255,0.90), rgba(248,251,255,0.82));"
        "  border: 1px solid rgba(113,132,169,0.16);"
        "  box-shadow: 0 18px 60px rgba(83,107,151,0.12);"
        "}"
        ".content {"
        "  margin-left: 18px;"
        "  padding: 22px 24px;"
        "  border-radius: 34px;"
        "  background: linear-gradient(180deg, rgba(255,255,255,0.93), rgba(252,253,255,0.88));"
        "  border: 1px solid rgba(113,132,169,0.16);"
        "  box-shadow: 0 24px 70px rgba(83,107,151,0.14);"
        "}"
        ".app-title { font-size: 22px; font-weight: 800; color: #202b44; letter-spacing: 0.02em; }"
        ".app-subtitle { font-size: 12px; color: #6b7590; }"
        ".page-title { font-size: 30px; font-weight: 850; color: #1e273f; letter-spacing: -0.02em; }"
        ".page-subtitle { font-size: 13px; color: #667390; }"
        ".card {"
        "  padding: 18px;"
        "  border-radius: 24px;"
        "  background: linear-gradient(180deg, rgba(255,255,255,0.98), rgba(248,250,255,0.94));"
        "  border: 1px solid rgba(112,132,170,0.14);"
        "  box-shadow: 0 8px 28px rgba(99,118,156,0.08);"
        "}"
        ".hero-card { background: linear-gradient(135deg, rgba(124,92,255,0.10), rgba(74,189,255,0.10) 52%, rgba(255,255,255,0.96)); }"
        ".card-title { font-size: 16px; font-weight: 750; color: #202b44; }"
        ".card-subtitle { font-size: 12px; color: #6e7894; }"
        ".mini-note { font-size: 12px; color: #6e7894; }"
        ".detail-text { font-size: 12px; color: #50607f; }"
        ".emphasis-text { font-size: 20px; font-weight: 780; color: #1f2c49; }"
        ".sidebar-row { padding: 11px 12px; border-radius: 18px; }"
        ".sidebar-row:selected { background: linear-gradient(90deg, rgba(124,92,255,0.16), rgba(124,92,255,0.06)); }"
        ".sidebar-row:hover { background: rgba(124,92,255,0.08); }"
        ".sidebar-title { font-weight: 700; color: #22304d; }"
        ".sidebar-subtitle { font-size: 11px; color: #6f7890; }"
        ".theme-choice, .action-button, .danger-button {"
        "  border-radius: 18px;"
        "  padding: 10px 12px;"
        "}"
        ".theme-choice.selected-card {"
        "  border: 2px solid #7c5cff;"
        "  background: rgba(124,92,255,0.08);"
        "}"
        ".action-button { background: linear-gradient(135deg, #7c5cff, #55bbff); color: white; }"
        ".danger-button { background: linear-gradient(135deg, #eb6388, #ff8a50); color: white; }"
        ".accent-swatch {"
        "  min-width: 34px; min-height: 34px;"
        "  border-radius: 999px;"
        "  padding: 0;"
        "  border: 2px solid rgba(17,24,39,0.08);"
        "}"
        ".accent-swatch.accent-selected {"
        "  border: 3px solid white;"
        "  outline: 2px solid #202b44;"
        "  outline-offset: 1px;"
        "}"
        ".status-ok { color: #25624a; font-weight: 700; }"
        ".status-err { color: #a13b58; font-weight: 700; }"
    );

    for (guint i = 0; i < karton_accents_count; i++) {
        g_string_append_printf(css,
            "button#%s { background: %s; color: transparent; }",
            karton_accents[i].id,
            karton_accents[i].hex);
    }

    gtk_css_provider_load_from_string(provider, css->str);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_string_free(css, TRUE);
    g_object_unref(provider);
}

static GtkWidget *
create_theme_button(KartonSettingsWindow *self, KartonThemeMode mode, const char *title, const char *subtitle)
{
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "theme-choice");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(box), create_label_block(title, "card-title"));
    gtk_box_append(GTK_BOX(box), create_label_block(subtitle, "mini-note"));
    gtk_button_set_child(GTK_BUTTON(button), box);

    g_object_set_data(G_OBJECT(button), "theme-mode", GINT_TO_POINTER(mode));
    self->theme_buttons[mode] = button;
    return button;
}

static void
update_theme_buttons(KartonSettingsWindow *self, KartonThemeMode active_mode)
{
    for (guint i = 0; i < 3; i++) {
        if (!self->theme_buttons[i]) {
            continue;
        }
        gtk_widget_remove_css_class(self->theme_buttons[i], "selected-card");
    }

    if (self->theme_buttons[active_mode]) {
        gtk_widget_add_css_class(self->theme_buttons[active_mode], "selected-card");
    }
}

static void
update_accent_buttons(KartonSettingsWindow *self)
{
    for (guint i = 0; i < self->accent_buttons->len; i++) {
        GtkWidget *button = g_ptr_array_index(self->accent_buttons, i);
        const char *hex = g_object_get_data(G_OBJECT(button), "accent-hex");
        gtk_widget_remove_css_class(button, "accent-selected");
        if (g_strcmp0(hex, self->selected_accent) == 0) {
            gtk_widget_add_css_class(button, "accent-selected");
        }
    }
}

static guint
display_scale_index_from_value(double scale)
{
    guint best = 0;
    double best_delta = G_MAXDOUBLE;
    for (guint i = 0; i < G_N_ELEMENTS(display_scale_values); i++) {
        double delta = fabs(display_scale_values[i] - scale);
        if (delta < best_delta) {
            best = i;
            best_delta = delta;
        }
    }
    return best;
}

static GtkWidget *
create_summary_row(const char *title, GtkWidget *suffix)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = create_label_block(title, NULL);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    if (suffix) {
        gtk_box_append(GTK_BOX(row), suffix);
    }
    return row;
}

static const char *
get_dropdown_selected_text(GtkDropDown *drop_down)
{
    GObject *item = gtk_drop_down_get_selected_item(drop_down);
    if (!item) {
        return NULL;
    }
    const char *text = GTK_IS_STRING_OBJECT(item) ? gtk_string_object_get_string(GTK_STRING_OBJECT(item)) : NULL;
    if (item) {
        g_object_unref(item);
    }
    return text;
}

static gboolean
autostart_entry_matches_filter(const char *label, const char *desktop_id, const char *filter)
{
    if (!filter || !*filter) {
        return TRUE;
    }

    gchar *needle = g_utf8_casefold(filter, -1);
    gchar *haystack_label = g_utf8_casefold(label ? label : "", -1);
    gchar *haystack_id = g_utf8_casefold(desktop_id ? desktop_id : "", -1);
    gboolean matches = strstr(haystack_label, needle) != NULL || strstr(haystack_id, needle) != NULL;
    g_free(needle);
    g_free(haystack_label);
    g_free(haystack_id);
    return matches;
}

static void
set_autostart_dropdown_entries(KartonSettingsWindow *self, const char *values)
{
    g_clear_pointer(&self->autostart_entries_raw, g_free);
    self->autostart_entries_raw = g_strdup(values);
}

static const char *
get_autostart_selected_id(KartonSettingsWindow *self)
{
    guint selected = gtk_drop_down_get_selected(self->autostart_drop);
    if (!self->autostart_ids || selected == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }
    for (guint i = 0; self->autostart_ids[i]; i++) {
        if (i == selected) {
            return self->autostart_ids[i];
        }
    }
    return NULL;
}

static void
select_autostart_id(KartonSettingsWindow *self, const char *desktop_id)
{
    if (!desktop_id || !*desktop_id || !self->autostart_ids) {
        gtk_drop_down_set_selected(self->autostart_drop, GTK_INVALID_LIST_POSITION);
        return;
    }
    for (guint i = 0; self->autostart_ids[i]; i++) {
        if (g_strcmp0(self->autostart_ids[i], desktop_id) == 0) {
            gtk_drop_down_set_selected(self->autostart_drop, i);
            return;
        }
    }
    gtk_drop_down_set_selected(self->autostart_drop, 0);
}

static void
rebuild_autostart_dropdown(KartonSettingsWindow *self, const char *preferred_id)
{
    GtkStringList *list = gtk_string_list_new(NULL);
    const char *filter = self->autostart_search
        ? gtk_editable_get_text(GTK_EDITABLE(self->autostart_search))
        : NULL;

    g_clear_pointer(&self->autostart_ids, g_strfreev);

    if (self->autostart_entries_raw && *self->autostart_entries_raw) {
        gchar **lines = g_strsplit(self->autostart_entries_raw, "\n", -1);
        GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (!*line) {
                continue;
            }

            gchar **parts = g_strsplit(line, "\t", 2);
            const char *label = parts[0] && *parts[0] ? parts[0] : line;
            const char *desktop_id = parts[1] && *parts[1] ? parts[1] : label;
            if (autostart_entry_matches_filter(label, desktop_id, filter)) {
                gtk_string_list_append(list, label);
                g_ptr_array_add(ids, g_strdup(desktop_id));
            }
            g_strfreev(parts);
        }
        g_ptr_array_add(ids, NULL);
        self->autostart_ids = (gchar **)g_ptr_array_free(ids, FALSE);
        g_strfreev(lines);
    }

    gtk_drop_down_set_model(self->autostart_drop, G_LIST_MODEL(list));
    g_object_unref(list);
    select_autostart_id(self, preferred_id);
}

static void
set_dropdown_strings(GtkDropDown *drop_down, const char *values)
{
    GtkStringList *list = gtk_string_list_new(NULL);
    if (values && *values) {
        gchar **lines = g_strsplit(values, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            gchar *line = g_strstrip(lines[i]);
            if (*line) {
                gtk_string_list_append(list, line);
            }
        }
        g_strfreev(lines);
    }
    gtk_drop_down_set_model(drop_down, G_LIST_MODEL(list));
    g_object_unref(list);
}

static void
select_dropdown_text(GtkDropDown *drop_down, const char *value)
{
    if (!value || !*value) {
        gtk_drop_down_set_selected(drop_down, GTK_INVALID_LIST_POSITION);
        return;
    }
    GListModel *model = gtk_drop_down_get_model(drop_down);
    if (!model) {
        return;
    }
    guint n_items = g_list_model_get_n_items(model);
    for (guint i = 0; i < n_items; i++) {
        GObject *item = g_list_model_get_item(model, i);
        if (GTK_IS_STRING_OBJECT(item)) {
            const char *text = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
            if (g_strcmp0(text, value) == 0) {
                gtk_drop_down_set_selected(drop_down, i);
                g_object_unref(item);
                return;
            }
        }
        g_object_unref(item);
    }
    gtk_drop_down_set_selected(drop_down, 0);
}

static void
refresh_appearance(KartonSettingsWindow *self)
{
    self->syncing_appearance = TRUE;
    update_theme_buttons(self, karton_theme_mode_load());

    gchar *font = karton_font_load();
    gtk_editable_set_text(GTK_EDITABLE(self->font_entry), font);
    g_free(font);

    g_clear_pointer(&self->selected_accent, g_free);
    self->selected_accent = karton_accent_load();
    update_accent_buttons(self);
    self->syncing_appearance = FALSE;
}

static void
refresh_wifi(KartonSettingsWindow *self)
{
    KartonToggleState state = { 0 };
    self->syncing_wifi = TRUE;
    karton_wifi_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->wifi_switch), state.available);
    gtk_switch_set_active(self->wifi_switch, state.enabled);
    gtk_label_set_text(self->wifi_summary, state.summary ? state.summary : _("Wi-Fi state unavailable."));
    gtk_label_set_text(self->wifi_details, state.details ? state.details : _("No Wi-Fi details available."));
    self->syncing_wifi = FALSE;
    karton_toggle_state_clear(&state);
}

static void
refresh_bluetooth(KartonSettingsWindow *self)
{
    KartonToggleState state = { 0 };
    self->syncing_bluetooth = TRUE;
    karton_bluetooth_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->bluetooth_switch), state.available);
    gtk_switch_set_active(self->bluetooth_switch, state.enabled);
    gtk_label_set_text(self->bluetooth_summary, state.summary ? state.summary : _("Bluetooth state unavailable."));
    gtk_label_set_text(self->bluetooth_details, state.details ? state.details : _("No Bluetooth details available."));
    self->syncing_bluetooth = FALSE;
    karton_toggle_state_clear(&state);
}

static void
refresh_audio(KartonSettingsWindow *self)
{
    KartonAudioState state = { 0 };
    self->syncing_audio = TRUE;
    karton_audio_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->volume_scale), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->mute_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->mic_scale), state.input_available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->mic_mute_switch), state.input_available);
    gtk_range_set_value(GTK_RANGE(self->volume_scale), state.level);
    gtk_switch_set_active(self->mute_switch, state.muted);
    gtk_range_set_value(GTK_RANGE(self->mic_scale), state.input_level);
    gtk_switch_set_active(self->mic_mute_switch, state.input_muted);
    gtk_label_set_text(self->audio_summary, state.summary ? state.summary : _("Audio state unavailable."));
    gtk_label_set_text(self->audio_details, state.details ? state.details : _("No audio device details available."));
    self->syncing_audio = FALSE;
    karton_audio_state_clear(&state);
}

static void
refresh_power(KartonSettingsWindow *self)
{
    KartonPowerState state = { 0 };
    self->syncing_power = TRUE;
    karton_power_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->power_profile_drop), state.available);
    if (state.current_profile) {
        for (guint i = 0; power_profile_values[i]; i++) {
            if (g_strcmp0(power_profile_values[i], state.current_profile) == 0) {
                gtk_drop_down_set_selected(self->power_profile_drop, i);
                break;
            }
        }
    }
    gtk_label_set_text(self->power_summary, state.summary ? state.summary : _("Power profile state unavailable."));
    self->syncing_power = FALSE;
    karton_power_state_clear(&state);
}

static void
refresh_notifications(KartonSettingsWindow *self)
{
    KartonToggleState state = { 0 };
    self->syncing_notifications = TRUE;
    karton_notifications_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->dnd_switch), state.available);
    gtk_switch_set_active(self->dnd_switch, state.enabled);
    gtk_label_set_text(self->notifications_summary, state.summary ? state.summary : _("Notification state unavailable."));
    self->syncing_notifications = FALSE;
    karton_toggle_state_clear(&state);
}

static void
refresh_display(KartonSettingsWindow *self)
{
    KartonDisplayState state = { 0 };
    self->syncing_display = TRUE;
    karton_display_get_state(&state);
    gtk_label_set_text(self->display_summary, state.summary ? state.summary : _("Display state unavailable."));
    gtk_label_set_text(self->display_details, state.details ? state.details : _("No display details available."));
    gtk_label_set_text(self->display_output_label,
        state.current_output ? state.current_output : _("No active output"));
    set_dropdown_strings(self->display_mode_drop, state.available_modes);
    select_dropdown_text(self->display_mode_drop, state.current_mode);
    select_dropdown_text(self->display_orientation_drop, state.current_orientation);
    gtk_widget_set_sensitive(GTK_WIDGET(self->display_mode_drop), state.can_configure);
    gtk_widget_set_sensitive(GTK_WIDGET(self->display_orientation_drop), state.can_configure);
    gtk_drop_down_set_selected(self->display_scale_drop, display_scale_index_from_value(state.interface_scale));
    gtk_widget_set_sensitive(GTK_WIDGET(self->display_brightness_scale), state.brightness_available);
    if (state.brightness_available) {
        gtk_range_set_value(GTK_RANGE(self->display_brightness_scale), state.brightness_level);
        gchar *brightness_text = g_strdup_printf(_("Brightness backend: %s. Current level: %.0f%%"),
            state.brightness_backend ? state.brightness_backend : _("unknown"),
            state.brightness_level);
        gtk_label_set_text(self->display_brightness_summary, brightness_text);
        g_free(brightness_text);
    } else {
        gtk_label_set_text(self->display_brightness_summary, _("Brightness control unavailable."));
    }
    self->syncing_display = FALSE;
    karton_display_state_clear(&state);
}

static void
refresh_input(KartonSettingsWindow *self)
{
    KartonInputState state = { 0 };
    self->syncing_input = TRUE;
    karton_input_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->mouse_speed_scale), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->natural_scroll_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->tap_to_click_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->left_handed_switch), state.available);
    gtk_range_set_value(GTK_RANGE(self->mouse_speed_scale), state.mouse_speed);
    gtk_switch_set_active(self->natural_scroll_switch, state.natural_scroll);
    gtk_switch_set_active(self->tap_to_click_switch, state.tap_to_click);
    gtk_switch_set_active(self->left_handed_switch, state.left_handed);
    gtk_label_set_text(self->input_summary, state.summary ? state.summary : _("Input settings are unavailable."));
    gtk_label_set_text(self->input_details, state.details ? state.details : _("No input details available."));
    self->syncing_input = FALSE;
    karton_input_state_clear(&state);
}

static void
refresh_users(KartonSettingsWindow *self)
{
    KartonUserState state = { 0 };
    karton_user_get_state(&state);
    gtk_label_set_text(self->user_summary, state.summary ? state.summary : _("User information unavailable."));
    gtk_label_set_text(self->user_details, state.details ? state.details : _("No user details available."));
    karton_user_state_clear(&state);
}

static void
refresh_accessibility(KartonSettingsWindow *self)
{
    KartonAccessibilityState state = { 0 };
    self->syncing_accessibility = TRUE;
    karton_accessibility_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_reader_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_keyboard_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_magnifier_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_sticky_keys_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_mouse_keys_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_slow_keys_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->accessibility_text_scale), state.available);
    gtk_switch_set_active(self->accessibility_reader_switch, state.screen_reader);
    gtk_switch_set_active(self->accessibility_keyboard_switch, state.screen_keyboard);
    gtk_switch_set_active(self->accessibility_magnifier_switch, state.screen_magnifier);
    gtk_switch_set_active(self->accessibility_sticky_keys_switch, state.sticky_keys);
    gtk_switch_set_active(self->accessibility_mouse_keys_switch, state.mouse_keys);
    gtk_switch_set_active(self->accessibility_slow_keys_switch, state.slow_keys);
    gtk_range_set_value(GTK_RANGE(self->accessibility_text_scale), state.text_scale);
    gtk_label_set_text(self->accessibility_summary,
        state.summary ? state.summary : _("Accessibility information unavailable."));
    gtk_label_set_text(self->accessibility_details,
        state.details ? state.details : _("No accessibility details available."));
    self->syncing_accessibility = FALSE;
    karton_accessibility_state_clear(&state);
}

static void
refresh_advanced(KartonSettingsWindow *self)
{
    KartonAdvancedState state = { 0 };
    karton_advanced_get_state(&state);
    gtk_label_set_text(self->advanced_summary,
        state.summary ? state.summary : _("Advanced diagnostics unavailable."));
    gtk_label_set_text(self->advanced_details,
        state.details ? state.details : _("No advanced diagnostics available."));
    karton_advanced_state_clear(&state);
}

static void
refresh_privacy(KartonSettingsWindow *self)
{
    KartonPrivacyState state = { 0 };
    self->syncing_privacy = TRUE;
    karton_privacy_get_state(&state);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_lock_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_screen_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_recent_files_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_camera_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_microphone_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_usb_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_identity_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_usage_stats_switch), state.available);
    gtk_widget_set_sensitive(GTK_WIDGET(self->privacy_reports_switch), state.available);
    gtk_switch_set_active(self->privacy_lock_switch, state.lock_screen);
    gtk_switch_set_active(self->privacy_screen_switch, state.privacy_screen);
    gtk_switch_set_active(self->privacy_recent_files_switch, state.remember_recent_files);
    gtk_switch_set_active(self->privacy_camera_switch, state.camera_access);
    gtk_switch_set_active(self->privacy_microphone_switch, state.microphone_access);
    gtk_switch_set_active(self->privacy_usb_switch, state.usb_protection);
    gtk_switch_set_active(self->privacy_identity_switch, state.hide_identity);
    gtk_switch_set_active(self->privacy_usage_stats_switch, state.send_usage_stats);
    gtk_switch_set_active(self->privacy_reports_switch, state.report_technical_problems);
    gtk_label_set_text(self->privacy_summary, state.summary ? state.summary : _("Privacy information unavailable."));
    gtk_label_set_text(self->privacy_details, state.details ? state.details : _("No privacy details available."));
    self->syncing_privacy = FALSE;
    karton_privacy_state_clear(&state);
}

static void
refresh_session(KartonSettingsWindow *self)
{
    KartonSessionState state = { 0 };
    self->syncing_session = TRUE;
    karton_session_get_state(&state);
    gtk_label_set_text(self->session_summary, state.summary ? state.summary : _("Session information unavailable."));
    gtk_label_set_text(self->session_details, state.details ? state.details : _("No session details available."));
    set_autostart_dropdown_entries(self, state.autostart_entries);
    rebuild_autostart_dropdown(self, state.autostart_selected);
    gtk_widget_set_sensitive(GTK_WIDGET(self->autostart_drop), state.autostart_count > 0);
    gtk_widget_set_sensitive(GTK_WIDGET(self->autostart_switch), state.autostart_count > 0);
    gtk_switch_set_active(self->autostart_switch, state.autostart_selected_enabled);
    gtk_label_set_text(self->autostart_details,
        state.autostart_preview ? state.autostart_preview : _("No autostart details available."));
    self->syncing_session = FALSE;
    karton_session_state_clear(&state);
}

static void
refresh_defaults(KartonSettingsWindow *self)
{
    KartonDefaultAppsState state = { 0 };
    karton_default_apps_get_state(&state);
    gtk_label_set_text(self->defaults_summary, state.summary ? state.summary : _("Default application state unavailable."));
    gtk_label_set_text(self->defaults_details, state.details ? state.details : _("No default application details available."));
    karton_default_apps_state_clear(&state);
}

static void
refresh_updates(KartonSettingsWindow *self)
{
    KartonUpdatesState state = { 0 };
    karton_updates_get_state(&state);
    gtk_label_set_text(self->updates_summary, state.summary ? state.summary : _("Update information unavailable."));
    gtk_label_set_text(self->updates_details, state.details ? state.details : _("No update details available."));
    karton_updates_state_clear(&state);
}

static void
refresh_storage(KartonSettingsWindow *self)
{
    KartonStorageState state = { 0 };
    karton_storage_get_state(&state);
    gtk_label_set_text(self->storage_summary, state.summary ? state.summary : _("Storage information unavailable."));
    gtk_label_set_text(self->storage_details, state.details ? state.details : _("No storage details available."));
    karton_storage_state_clear(&state);
}

static void
refresh_region(KartonSettingsWindow *self)
{
    KartonRegionState state = { 0 };
    karton_region_get_state(&state);
    gtk_label_set_text(self->region_summary, state.summary ? state.summary : _("Region information unavailable."));
    gtk_label_set_text(self->region_details, state.details ? state.details : _("No region details available."));
    karton_region_state_clear(&state);
}

static void
refresh_all(KartonSettingsWindow *self)
{
    refresh_appearance(self);
    refresh_display(self);
    refresh_input(self);
    refresh_wifi(self);
    refresh_bluetooth(self);
    refresh_audio(self);
    refresh_power(self);
    refresh_notifications(self);
    refresh_accessibility(self);
    refresh_privacy(self);
    refresh_users(self);
    refresh_session(self);
    refresh_defaults(self);
    refresh_updates(self);
    refresh_region(self);
    refresh_advanced(self);
    refresh_storage(self);

    gchar *system_summary = karton_system_summary();
    gtk_label_set_text(self->system_summary, system_summary);
    g_free(system_summary);
}

static void
on_refresh_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    refresh_all(self);
    set_status(self, _("Settings state refreshed."), TRUE);
}

static void
on_theme_clicked(GtkWidget *widget, gpointer user_data)
{
    KartonSettingsWindow *self = user_data;
    KartonThemeMode mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "theme-mode"));
    gchar *error_msg = NULL;

    if (karton_theme_mode_apply(mode, &error_msg)) {
        refresh_appearance(self);
        set_status(self, _("Global theme updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the global theme."), FALSE);
    }

    g_free(error_msg);
}

static void
on_accent_clicked(GtkWidget *widget, gpointer user_data)
{
    KartonSettingsWindow *self = user_data;
    const char *hex = g_object_get_data(G_OBJECT(button), "accent-hex");
    gchar *error_msg = NULL;

    if (karton_accent_apply(hex, &error_msg)) {
        g_free(self->selected_accent);
        self->selected_accent = g_strdup(hex);
        update_accent_buttons(self);
        set_status(self, _("Accent color updated for Karton."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the accent color."), FALSE);
    }

    g_free(error_msg);
}

static void
on_apply_font_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    const char *font_name = gtk_editable_get_text(GTK_EDITABLE(self->font_entry));
    gchar *error_msg = NULL;

    if (karton_font_apply(font_name, &error_msg)) {
        set_status(self, _("GTK interface font updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the GTK font."), FALSE);
    }

    g_free(error_msg);
}

static gboolean
on_wifi_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
    (void)sw;
    KartonSettingsWindow *self = user_data;
    if (self->syncing_wifi) {
        return FALSE;
    }

    gchar *error_msg = NULL;
    if (karton_wifi_set_enabled(state, &error_msg)) {
        set_status(self, state ? _("Wi-Fi enabled.") : _("Wi-Fi disabled."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot change Wi-Fi state."), FALSE);
    }
    g_free(error_msg);
    refresh_wifi(self);
    return FALSE;
}

static gboolean
on_bluetooth_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
    (void)sw;
    KartonSettingsWindow *self = user_data;
    if (self->syncing_bluetooth) {
        return FALSE;
    }

    gchar *error_msg = NULL;
    if (karton_bluetooth_set_enabled(state, &error_msg)) {
        set_status(self, state ? _("Bluetooth enabled.") : _("Bluetooth disabled."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot change Bluetooth state."), FALSE);
    }
    g_free(error_msg);
    refresh_bluetooth(self);
    return FALSE;
}

static gboolean
on_dnd_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
    (void)sw;
    KartonSettingsWindow *self = user_data;
    if (self->syncing_notifications) {
        return FALSE;
    }

    gchar *error_msg = NULL;
    if (karton_notifications_set_dnd(state, &error_msg)) {
        set_status(self, state ? _("Do not disturb enabled.") : _("Notifications enabled."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot change notification mode."), FALSE);
    }
    g_free(error_msg);
    refresh_notifications(self);
    return FALSE;
}

static void
on_apply_audio_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    if (self->syncing_audio) return;
    gchar *error_msg = NULL;

    if (karton_audio_apply(
            gtk_range_get_value(GTK_RANGE(self->volume_scale)),
            gtk_switch_get_active(self->mute_switch),
            gtk_range_get_value(GTK_RANGE(self->mic_scale)),
            gtk_switch_get_active(self->mic_mute_switch),
            &error_msg)) {
        set_status(self, _("Sound output updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the sound output."), FALSE);
    }
    g_free(error_msg);
    refresh_audio(self);
}

static void
on_apply_power_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    if (self->syncing_power) return;
    guint selected = gtk_drop_down_get_selected(self->power_profile_drop);
    gchar *error_msg = NULL;

    if (!power_profile_values[selected]) {
        set_status(self, _("Selected power profile is invalid."), FALSE);
        return;
    }

    if (karton_power_set_profile(power_profile_values[selected], &error_msg)) {
        set_status(self, _("Power profile updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot change the power profile."), FALSE);
    }
    g_free(error_msg);
    refresh_power(self);
}

static void
on_apply_display_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    guint index = gtk_drop_down_get_selected(self->display_scale_drop);
    gchar *error_msg = NULL;

    if (index >= G_N_ELEMENTS(display_scale_values)) {
        set_status(self, _("Selected display scale is invalid."), FALSE);
        return;
    }

    if (karton_display_set_interface_scale(display_scale_values[index], &error_msg)) {
        set_status(self, _("Interface scale updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the interface scale."), FALSE);
    }
    g_free(error_msg);
    refresh_display(self);
}

static void
on_apply_display_brightness_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    gchar *error_msg = NULL;

    if (karton_display_set_brightness(gtk_range_get_value(GTK_RANGE(self->display_brightness_scale)), &error_msg)) {
        set_status(self, _("Brightness updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update brightness."), FALSE);
    }

    g_free(error_msg);
    refresh_display(self);
}

static void
on_apply_display_mode_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    const char *mode = get_dropdown_selected_text(self->display_mode_drop);
    const char *orientation = get_dropdown_selected_text(self->display_orientation_drop);
    const char *output = gtk_label_get_text(self->display_output_label);
    gchar *error_msg = NULL;

    if (karton_display_apply_mode(output, mode, orientation, &error_msg)) {
        set_status(self, _("Display mode updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the display mode."), FALSE);
    }
    g_free(error_msg);
    refresh_display(self);
}

static void
on_apply_input_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    gchar *error_msg = NULL;

    if (karton_input_apply(
            gtk_range_get_value(GTK_RANGE(self->mouse_speed_scale)),
            gtk_switch_get_active(self->natural_scroll_switch),
            gtk_switch_get_active(self->tap_to_click_switch),
            gtk_switch_get_active(self->left_handed_switch),
            &error_msg)) {
        set_status(self, _("Input settings updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update input settings."), FALSE);
    }
    g_free(error_msg);
    refresh_input(self);
}

static void
on_apply_accessibility_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    gchar *error_msg = NULL;

    if (karton_accessibility_apply(
            gtk_switch_get_active(self->accessibility_reader_switch),
            gtk_switch_get_active(self->accessibility_keyboard_switch),
            gtk_switch_get_active(self->accessibility_magnifier_switch),
            gtk_switch_get_active(self->accessibility_sticky_keys_switch),
            gtk_switch_get_active(self->accessibility_mouse_keys_switch),
            gtk_switch_get_active(self->accessibility_slow_keys_switch),
            gtk_range_get_value(GTK_RANGE(self->accessibility_text_scale)),
            &error_msg)) {
        set_status(self, _("Accessibility settings updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update accessibility settings."), FALSE);
    }
    g_free(error_msg);
    refresh_accessibility(self);
}

static void
on_apply_privacy_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    gchar *error_msg = NULL;

    if (karton_privacy_apply(
            gtk_switch_get_active(self->privacy_lock_switch),
            gtk_switch_get_active(self->privacy_screen_switch),
            gtk_switch_get_active(self->privacy_recent_files_switch),
            gtk_switch_get_active(self->privacy_camera_switch),
            gtk_switch_get_active(self->privacy_microphone_switch),
            gtk_switch_get_active(self->privacy_usb_switch),
            gtk_switch_get_active(self->privacy_identity_switch),
            gtk_switch_get_active(self->privacy_usage_stats_switch),
            gtk_switch_get_active(self->privacy_reports_switch),
            &error_msg)) {
        set_status(self, _("Privacy settings updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update privacy settings."), FALSE);
    }
    g_free(error_msg);
    refresh_privacy(self);
}

static void
on_region_tool_clicked(GtkWidget *widget, gpointer user_data)
{
    KartonSettingsWindow *self = user_data;
    const char *tool = g_object_get_data(G_OBJECT(button), "region-tool");
    gchar *error_msg = NULL;

    if (karton_region_open_tool(tool, &error_msg)) {
        if (g_strcmp0(tool, "time") == 0) {
            set_status(self, _("Time settings tool opened."), TRUE);
        } else {
            set_status(self, _("Language and formats tool opened."), TRUE);
        }
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot open the regional settings tool."), FALSE);
    }
    g_free(error_msg);
}

static void
on_advanced_report_clicked(GtkWidget *widget, gpointer user_data)
{
    KartonSettingsWindow *self = user_data;
    const char *tool = g_object_get_data(G_OBJECT(button), "advanced-tool");
    gchar *error_msg = NULL;

    if (karton_advanced_open_report(tool, &error_msg)) {
        if (g_strcmp0(tool, "session") == 0) {
            set_status(self, _("Session journal report opened."), TRUE);
        } else {
            set_status(self, _("Display report opened."), TRUE);
        }
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot open the diagnostics report."), FALSE);
    }

    g_free(error_msg);
}

static void
on_session_action_clicked(GtkWidget *widget, gpointer user_data)
{
    KartonSettingsWindow *self = user_data;
    const char *action = g_object_get_data(G_OBJECT(button), "session-action");
    gchar *error_msg = NULL;

    if (karton_session_run_action(action, &error_msg)) {
        if (g_strcmp0(action, "logout") == 0) {
            set_status(self, _("Logout requested."), TRUE);
        } else if (g_strcmp0(action, "restart") == 0) {
            set_status(self, _("Restart requested."), TRUE);
        } else {
            set_status(self, _("Shutdown requested."), TRUE);
        }
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot run the requested session action."), FALSE);
    }

    g_free(error_msg);
}

static void
update_autostart_selection(KartonSettingsWindow *self)
{
    const char *desktop_id = get_autostart_selected_id(self);
    gboolean enabled = FALSE;
    gchar *details = NULL;

    if (!desktop_id || !*desktop_id) {
        gtk_widget_set_sensitive(GTK_WIDGET(self->autostart_switch), FALSE);
        const char *filter = self->autostart_search
            ? gtk_editable_get_text(GTK_EDITABLE(self->autostart_search))
            : NULL;
        gtk_label_set_text(self->autostart_details,
            (filter && *filter) ? _("No autostart entries match the current filter.") : _("No autostart entries detected."));
        return;
    }

    if (karton_session_autostart_lookup(desktop_id, &enabled, &details)) {
        self->syncing_session = TRUE;
        gtk_widget_set_sensitive(GTK_WIDGET(self->autostart_switch), TRUE);
        gtk_switch_set_active(self->autostart_switch, enabled);
        self->syncing_session = FALSE;
        gtk_label_set_text(self->autostart_details, details ? details : _("No autostart details available."));
    } else {
        gtk_widget_set_sensitive(GTK_WIDGET(self->autostart_switch), FALSE);
        gtk_label_set_text(self->autostart_details, _("Cannot read the selected autostart entry."));
    }

    g_free(details);
}

static void
on_autostart_selection_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    KartonSettingsWindow *self = user_data;
    if (self->syncing_session) {
        return;
    }
    update_autostart_selection(self);
}

static void
on_autostart_search_changed(GtkEditable *editable, gpointer user_data)
{
    (void)editable;
    KartonSettingsWindow *self = user_data;
    const char *selected_id = get_autostart_selected_id(self);
    rebuild_autostart_dropdown(self, selected_id);
    update_autostart_selection(self);
}

static void
on_apply_autostart_clicked(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    KartonSettingsWindow *self = user_data;
    const char *desktop_id = get_autostart_selected_id(self);
    gchar *error_msg = NULL;

    if (!desktop_id || !*desktop_id) {
        set_status(self, _("Select an autostart entry first."), FALSE);
        return;
    }

    if (karton_session_autostart_set_enabled(desktop_id,
            gtk_switch_get_active(self->autostart_switch),
            &error_msg)) {
        set_status(self, _("Autostart override updated."), TRUE);
    } else {
        set_status(self, error_msg ? error_msg : _("Cannot update the autostart override."), FALSE);
    }

    g_free(error_msg);
    refresh_session(self);
    update_autostart_selection(self);
}

static void
karton_settings_window_free(KartonSettingsWindow *self)
{
    if (!self) {
        return;
    }
    g_clear_pointer(&self->selected_accent, g_free);
    g_clear_pointer(&self->autostart_entries_raw, g_free);
    g_clear_pointer(&self->autostart_ids, g_strfreev);
    g_free(self);
}

static void
on_sidebar_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    KartonSettingsWindow *self = user_data;
    if (!row) {
        return;
    }

    const char *category_id = g_object_get_data(G_OBJECT(row), "category-id");
    const KartonCategory *category = lookup_category(category_id);
    gtk_stack_set_visible_child_name(self->stack, category->id);
    gtk_label_set_text(self->page_title, _(category->title));
    gtk_label_set_text(self->page_subtitle, _(category->subtitle));
}

void
karton_settings_window_select_page(GtkWindow *window, const char *page)
{
    KartonSettingsWindow *self = g_object_get_data(G_OBJECT(window), "karton-settings-window");
    const char *target = page && *page ? page : "appearance";
    const KartonCategory *category = lookup_category(target);

    if (!self || !self->sidebar_list || !category) {
        return;
    }

    for (guint i = 0; i < G_N_ELEMENTS(categories); i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(self->sidebar_list, i);
        const char *category_id;

        if (!row) {
            continue;
        }

        category_id = g_object_get_data(G_OBJECT(row), "category-id");
        if (g_strcmp0(category_id, category->id) == 0) {
            gtk_list_box_select_row(self->sidebar_list, row);
            return;
        }
    }
}
static GtkWidget *
build_appearance_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *hero = create_card(_("Global styling"), _(category->purpose));
    gtk_widget_add_css_class(hero, "hero-card");
    gtk_box_append(GTK_BOX(hero), create_label_block(_("Karton applies theme changes globally through the shared session tooling, so light, dark and automatic mode stay aligned between the shell and the settings app."), NULL));
    gtk_box_append(GTK_BOX(page), hero);

    GtkWidget *theme_card = create_card(_("Theme mode"), _("Choose how the desktop resolves its global theme."));
    GtkWidget *theme_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *light = create_theme_button(self, KARTON_THEME_LIGHT, _("Light"), _("Bright surfaces for daytime layouts"));
    GtkWidget *dark = create_theme_button(self, KARTON_THEME_DARK, _("Dark"), _("Low-glare surfaces for night sessions"));
    GtkWidget *auto_mode = create_theme_button(self, KARTON_THEME_AUTO, _("Auto"), _("Follow the Karton session mode"));
    g_signal_connect(light, "clicked", G_CALLBACK(on_theme_clicked), self);
    g_signal_connect(dark, "clicked", G_CALLBACK(on_theme_clicked), self);
    g_signal_connect(auto_mode, "clicked", G_CALLBACK(on_theme_clicked), self);
    gtk_box_append(GTK_BOX(theme_row), light);
    gtk_box_append(GTK_BOX(theme_row), dark);
    gtk_box_append(GTK_BOX(theme_row), auto_mode);
    gtk_box_append(GTK_BOX(theme_card), theme_row);
    gtk_box_append(GTK_BOX(page), theme_card);

    GtkWidget *accent_card = create_card(_("Accent color"), _("This value is persisted for shell surfaces and future compositor hooks."));
    GtkWidget *accent_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    self->accent_buttons = g_ptr_array_new();
    for (guint i = 0; i < karton_accents_count; i++) {
        GtkWidget *button = gtk_button_new_with_label(" ");
        gtk_widget_add_css_class(button, "accent-swatch");
        gtk_widget_set_name(button, karton_accents[i].id);
        gtk_widget_set_tooltip_text(button, karton_accents[i].name);
        g_object_set_data(G_OBJECT(button), "accent-hex", (gpointer)karton_accents[i].hex);
        g_signal_connect(button, "clicked", G_CALLBACK(on_accent_clicked), self);
        g_ptr_array_add(self->accent_buttons, button);
        gtk_box_append(GTK_BOX(accent_row), button);
    }
    gtk_box_append(GTK_BOX(accent_card), accent_row);
    gtk_box_append(GTK_BOX(page), accent_card);

    GtkWidget *font_card = create_card(_("Interface font"), _("Applied directly to GTK settings for Karton apps."));
    GtkWidget *font_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    self->font_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(self->font_entry), TRUE);
    gtk_entry_set_placeholder_text(self->font_entry, _("Example: Sans 11"));
    GtkWidget *font_button = gtk_button_new_with_label(_("Apply font"));
    gtk_widget_add_css_class(font_button, "action-button");
    g_signal_connect(font_button, "clicked", G_CALLBACK(on_apply_font_clicked), self);
    gtk_box_append(GTK_BOX(font_row), GTK_WIDGET(self->font_entry));
    gtk_box_append(GTK_BOX(font_row), font_button);
    gtk_box_append(GTK_BOX(font_card), font_row);
    gtk_box_append(GTK_BOX(page), font_card);

    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_network_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *wifi_card = create_card(_("Wi-Fi"), _("Quick toggle for the NetworkManager radio state."));
    self->wifi_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(self->wifi_switch, "state-set", G_CALLBACK(on_wifi_state_set), self);
    gtk_box_append(GTK_BOX(wifi_card), create_summary_row(_("Wireless networking"), GTK_WIDGET(self->wifi_switch)));
    self->wifi_summary = GTK_LABEL(create_label_block("", "mini-note"));
    self->wifi_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(wifi_card), GTK_WIDGET(self->wifi_summary));
    gtk_box_append(GTK_BOX(wifi_card), GTK_WIDGET(self->wifi_details));
    gtk_box_append(GTK_BOX(page), wifi_card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_bluetooth_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Bluetooth"), _("Toggle the Bluetooth radio through the available system backend."));
    self->bluetooth_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(self->bluetooth_switch, "state-set", G_CALLBACK(on_bluetooth_state_set), self);
    gtk_box_append(GTK_BOX(card), create_summary_row(_("Wireless accessories"), GTK_WIDGET(self->bluetooth_switch)));
    self->bluetooth_summary = GTK_LABEL(create_label_block("", "mini-note"));
    self->bluetooth_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->bluetooth_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->bluetooth_details));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_sound_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Output level"), _("PipeWire output volume and mute state."));
    self->volume_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 150.0, 1.0));
    gtk_scale_set_draw_value(self->volume_scale, TRUE);
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->volume_scale));
    self->mute_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(card), create_summary_row(_("Mute output"), GTK_WIDGET(self->mute_switch)));

    GtkWidget *mic_card = create_card(_("Input level"), _("Microphone level and mute state for the default capture device."));
    self->mic_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 150.0, 1.0));
    gtk_scale_set_draw_value(self->mic_scale, TRUE);
    gtk_box_append(GTK_BOX(mic_card), GTK_WIDGET(self->mic_scale));
    self->mic_mute_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(mic_card), create_summary_row(_("Mute microphone"), GTK_WIDGET(self->mic_mute_switch)));
    gtk_box_append(GTK_BOX(page), mic_card);

    g_signal_connect(self->volume_scale, "value-changed", G_CALLBACK(on_apply_audio_clicked), self);
    g_signal_connect(self->mute_switch, "notify::active", G_CALLBACK(on_apply_audio_clicked), self);
    g_signal_connect(self->mic_scale, "value-changed", G_CALLBACK(on_apply_audio_clicked), self);
    g_signal_connect(self->mic_mute_switch, "notify::active", G_CALLBACK(on_apply_audio_clicked), self);
    self->audio_summary = GTK_LABEL(create_label_block("", "mini-note"));
    self->audio_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->audio_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->audio_details));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_power_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    const char *profile_labels[] = { "Power Saver", "Balanced", "Performance", NULL };
    GtkWidget *card = create_card(_("Power profile"), _("Switch between available power policy modes."));
    self->power_profile_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(profile_labels));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->power_profile_drop));
    g_signal_connect(self->power_profile_drop, "notify::selected", G_CALLBACK(on_apply_power_clicked), self);
    self->power_summary = GTK_LABEL(create_label_block("", "mini-note"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->power_summary));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_notifications_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Do not disturb"), _("Use gsettings to suppress notification banners when available."));
    self->dnd_switch = GTK_SWITCH(gtk_switch_new());
    g_signal_connect(self->dnd_switch, "state-set", G_CALLBACK(on_dnd_state_set), self);
    gtk_box_append(GTK_BOX(card), create_summary_row(_("Silence banners"), GTK_WIDGET(self->dnd_switch)));
    self->notifications_summary = GTK_LABEL(create_label_block("", "mini-note"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->notifications_summary));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_display_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Connected outputs"), _("A live summary is shown when a supported display backend is present."));
    self->display_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->display_summary));
    self->display_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->display_details));
    gtk_box_append(GTK_BOX(page), card);

    GtkWidget *mode_card = create_card(_("Active output"), _("Current output, mode and orientation reported by the display backend."));
    self->display_output_label = GTK_LABEL(create_label_block("", "emphasis-text"));
    gtk_box_append(GTK_BOX(mode_card), GTK_WIDGET(self->display_output_label));
    const char *orientation_labels[] = { "normal", "left", "right", "inverted", NULL };
    self->display_mode_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(NULL));
    self->display_orientation_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(orientation_labels));
    gtk_box_append(GTK_BOX(mode_card), create_summary_row(_("Resolution"), GTK_WIDGET(self->display_mode_drop)));
    gtk_box_append(GTK_BOX(mode_card), create_summary_row(_("Orientation"), GTK_WIDGET(self->display_orientation_drop)));
    GtkWidget *apply_mode_button = gtk_button_new_with_label(_("Apply display mode"));
    gtk_widget_add_css_class(apply_mode_button, "action-button");
    g_signal_connect(apply_mode_button, "clicked", G_CALLBACK(on_apply_display_mode_clicked), self);
    gtk_box_append(GTK_BOX(mode_card), apply_mode_button);
    gtk_box_append(GTK_BOX(mode_card), create_label_block(_("Mode changes are enabled only when Karton runs in an X11 session with direct xrandr access."), "mini-note"));
    gtk_box_append(GTK_BOX(page), mode_card);

    GtkWidget *brightness_card = create_card(_("Brightness"), _("Adjust the internal display backlight when a supported backend is available."));
    self->display_brightness_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1.0, 100.0, 1.0));
    gtk_scale_set_draw_value(self->display_brightness_scale, TRUE);
    gtk_box_append(GTK_BOX(brightness_card), GTK_WIDGET(self->display_brightness_scale));
    GtkWidget *brightness_button = gtk_button_new_with_label(_("Apply brightness"));
    gtk_widget_add_css_class(brightness_button, "action-button");
    g_signal_connect(brightness_button, "clicked", G_CALLBACK(on_apply_display_brightness_clicked), self);
    gtk_box_append(GTK_BOX(brightness_card), brightness_button);
    self->display_brightness_summary = GTK_LABEL(create_label_block("", "mini-note"));
    gtk_box_append(GTK_BOX(brightness_card), GTK_WIDGET(self->display_brightness_summary));
    gtk_box_append(GTK_BOX(page), brightness_card);

    const char *scale_labels[] = { "100%", "110%", "125%", "150%", "175%", "200%", NULL };
    GtkWidget *scale_card = create_card(_("Interface scale"), _("Apply a GTK interface scaling factor for Karton apps."));
    self->display_scale_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(scale_labels));
    gtk_box_append(GTK_BOX(scale_card), GTK_WIDGET(self->display_scale_drop));
    GtkWidget *apply_button = gtk_button_new_with_label(_("Apply scale"));
    gtk_widget_add_css_class(apply_button, "action-button");
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_display_clicked), self);
    gtk_box_append(GTK_BOX(scale_card), apply_button);
    gtk_box_append(GTK_BOX(page), scale_card);

    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_input_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *pointer_card = create_card(_("Pointer speed"), _("Acceleration speed for the default mouse device."));
    self->mouse_speed_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -1.0, 1.0, 0.05));
    gtk_scale_set_draw_value(self->mouse_speed_scale, TRUE);
    gtk_box_append(GTK_BOX(pointer_card), GTK_WIDGET(self->mouse_speed_scale));
    self->left_handed_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(pointer_card), create_summary_row(_("Left-handed mode"), GTK_WIDGET(self->left_handed_switch)));
    gtk_box_append(GTK_BOX(page), pointer_card);

    GtkWidget *touchpad_card = create_card(_("Touchpad"), _("Natural scrolling and tap gestures for the default touchpad."));
    self->natural_scroll_switch = GTK_SWITCH(gtk_switch_new());
    self->tap_to_click_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(touchpad_card), create_summary_row(_("Natural scroll"), GTK_WIDGET(self->natural_scroll_switch)));
    gtk_box_append(GTK_BOX(touchpad_card), create_summary_row(_("Tap to click"), GTK_WIDGET(self->tap_to_click_switch)));
    gtk_box_append(GTK_BOX(page), touchpad_card);

    GtkWidget *status_card = create_card(_("Input overview"), _("Mouse and touchpad state resolved from the current desktop settings."));
    self->input_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->input_details = GTK_LABEL(create_label_block("", "detail-text"));
    GtkWidget *apply_button = gtk_button_new_with_label(_("Apply input"));
    gtk_widget_add_css_class(apply_button, "action-button");
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_input_clicked), self);
    gtk_box_append(GTK_BOX(status_card), GTK_WIDGET(self->input_summary));
    gtk_box_append(GTK_BOX(status_card), GTK_WIDGET(self->input_details));
    gtk_box_append(GTK_BOX(status_card), apply_button);
    gtk_box_append(GTK_BOX(page), status_card);

    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_system_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("System overview"), _("Basic runtime details collected from the local machine."));
    self->system_summary = GTK_LABEL(create_label_block("", NULL));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->system_summary));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_advanced_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Advanced diagnostics"), _("Session environment and low-level tools detected in the current runtime."));
    self->advanced_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->advanced_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->advanced_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->advanced_details));

    GtkWidget *actions_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *session_button = gtk_button_new_with_label(_("Open session journal"));
    GtkWidget *display_button = gtk_button_new_with_label(_("Open display report"));
    gtk_widget_add_css_class(session_button, "action-button");
    gtk_widget_add_css_class(display_button, "action-button");
    g_object_set_data(G_OBJECT(session_button), "advanced-tool", (gpointer)"session");
    g_object_set_data(G_OBJECT(display_button), "advanced-tool", (gpointer)"display");
    g_signal_connect(session_button, "clicked", G_CALLBACK(on_advanced_report_clicked), self);
    g_signal_connect(display_button, "clicked", G_CALLBACK(on_advanced_report_clicked), self);
    gtk_box_append(GTK_BOX(actions_row), session_button);
    gtk_box_append(GTK_BOX(actions_row), display_button);
    gtk_box_append(GTK_BOX(card), actions_row);

    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_accessibility_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *controls_card = create_card(_("Accessibility controls"), _("Reading, magnification and input assistance resolved from the active desktop settings."));
    self->accessibility_reader_switch = GTK_SWITCH(gtk_switch_new());
    self->accessibility_keyboard_switch = GTK_SWITCH(gtk_switch_new());
    self->accessibility_magnifier_switch = GTK_SWITCH(gtk_switch_new());
    self->accessibility_sticky_keys_switch = GTK_SWITCH(gtk_switch_new());
    self->accessibility_mouse_keys_switch = GTK_SWITCH(gtk_switch_new());
    self->accessibility_slow_keys_switch = GTK_SWITCH(gtk_switch_new());
    self->accessibility_text_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.75, 2.00, 0.05));
    gtk_scale_set_digits(self->accessibility_text_scale, 2);
    gtk_widget_set_hexpand(GTK_WIDGET(self->accessibility_text_scale), TRUE);
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Screen reader"), GTK_WIDGET(self->accessibility_reader_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("On-screen keyboard"), GTK_WIDGET(self->accessibility_keyboard_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Screen magnifier"), GTK_WIDGET(self->accessibility_magnifier_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Sticky keys"), GTK_WIDGET(self->accessibility_sticky_keys_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Mouse keys"), GTK_WIDGET(self->accessibility_mouse_keys_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Slow keys"), GTK_WIDGET(self->accessibility_slow_keys_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Text scaling"), GTK_WIDGET(self->accessibility_text_scale)));
    GtkWidget *apply_button = gtk_button_new_with_label(_("Apply accessibility settings"));
    gtk_widget_add_css_class(apply_button, "action-button");
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_accessibility_clicked), self);
    gtk_box_append(GTK_BOX(controls_card), apply_button);
    gtk_box_append(GTK_BOX(page), controls_card);

    GtkWidget *status_card = create_card(_("Accessibility overview"), _("Current accessibility state collected from the running desktop session."));
    self->accessibility_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->accessibility_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(status_card), GTK_WIDGET(self->accessibility_summary));
    gtk_box_append(GTK_BOX(status_card), GTK_WIDGET(self->accessibility_details));
    gtk_box_append(GTK_BOX(page), status_card);

    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_privacy_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);

    GtkWidget *controls_card = create_card(_("Privacy controls"), _("Protect local data, device access and the recent files history."));
    self->privacy_lock_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_screen_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_recent_files_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_camera_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_microphone_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_usb_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_identity_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_usage_stats_switch = GTK_SWITCH(gtk_switch_new());
    self->privacy_reports_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Lock screen"), GTK_WIDGET(self->privacy_lock_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Privacy screen"), GTK_WIDGET(self->privacy_screen_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Recent files history"), GTK_WIDGET(self->privacy_recent_files_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Camera access"), GTK_WIDGET(self->privacy_camera_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Microphone access"), GTK_WIDGET(self->privacy_microphone_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("USB protection"), GTK_WIDGET(self->privacy_usb_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Hide identity"), GTK_WIDGET(self->privacy_identity_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Usage statistics"), GTK_WIDGET(self->privacy_usage_stats_switch)));
    gtk_box_append(GTK_BOX(controls_card), create_summary_row(_("Technical problem reports"), GTK_WIDGET(self->privacy_reports_switch)));
    GtkWidget *apply_button = gtk_button_new_with_label(_("Apply privacy settings"));
    gtk_widget_add_css_class(apply_button, "action-button");
    g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply_privacy_clicked), self);
    gtk_box_append(GTK_BOX(controls_card), apply_button);
    gtk_box_append(GTK_BOX(page), controls_card);

    GtkWidget *status_card = create_card(_("Privacy overview"), _("Current privacy state resolved from the active desktop settings."));
    self->privacy_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->privacy_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(status_card), GTK_WIDGET(self->privacy_summary));
    gtk_box_append(GTK_BOX(status_card), GTK_WIDGET(self->privacy_details));
    gtk_box_append(GTK_BOX(page), status_card);

    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_defaults_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Preferred applications"), _("Handlers currently registered in the XDG desktop stack."));
    self->defaults_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->defaults_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->defaults_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->defaults_details));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_updates_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Package updates"), _("Pending updates detected through the native package manager CLI."));
    self->updates_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->updates_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->updates_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->updates_details));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_region_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Regional settings"), _("Locale, timezone and keyboard layout detected from the current system."));
    self->region_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->region_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->region_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->region_details));

    GtkWidget *actions_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *time_button = gtk_button_new_with_label(_("Open time settings"));
    GtkWidget *language_button = gtk_button_new_with_label(_("Open language and formats"));
    gtk_widget_add_css_class(time_button, "action-button");
    gtk_widget_add_css_class(language_button, "action-button");
    g_object_set_data(G_OBJECT(time_button), "region-tool", (gpointer)"time");
    g_object_set_data(G_OBJECT(language_button), "region-tool", (gpointer)"language");
    g_signal_connect(time_button, "clicked", G_CALLBACK(on_region_tool_clicked), self);
    g_signal_connect(language_button, "clicked", G_CALLBACK(on_region_tool_clicked), self);
    gtk_box_append(GTK_BOX(actions_row), time_button);
    gtk_box_append(GTK_BOX(actions_row), language_button);
    gtk_box_append(GTK_BOX(card), actions_row);

    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_storage_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Storage overview"), _("Mounted filesystems and block devices reported by the local system."));
    self->storage_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->storage_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->storage_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->storage_details));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_users_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *card = create_card(_("Current account"), _("Local user details resolved from the active session."));
    self->user_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->user_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->user_summary));
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(self->user_details));
    gtk_box_append(GTK_BOX(page), card);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_session_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *runtime_card = create_card(_("Session runtime"), _("Live session data and autostart snapshot."));
    self->session_summary = GTK_LABEL(create_label_block("", "emphasis-text"));
    self->session_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(runtime_card), GTK_WIDGET(self->session_summary));
    gtk_box_append(GTK_BOX(runtime_card), GTK_WIDGET(self->session_details));
    gtk_box_append(GTK_BOX(page), runtime_card);

    GtkWidget *autostart_card = create_card(_("Autostart"), _("Inspect system startup entries and apply a user override."));
    self->autostart_search = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_search_entry_set_placeholder_text(self->autostart_search, _("Filter autostart entries"));
    g_signal_connect(self->autostart_search, "changed", G_CALLBACK(on_autostart_search_changed), self);
    gtk_box_append(GTK_BOX(autostart_card), GTK_WIDGET(self->autostart_search));

    self->autostart_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(NULL));
    gtk_widget_set_hexpand(GTK_WIDGET(self->autostart_drop), TRUE);
    g_signal_connect(self->autostart_drop, "notify::selected", G_CALLBACK(on_autostart_selection_changed), self);
    gtk_box_append(GTK_BOX(autostart_card), create_summary_row(_("Entry"), GTK_WIDGET(self->autostart_drop)));

    self->autostart_switch = GTK_SWITCH(gtk_switch_new());
    gtk_box_append(GTK_BOX(autostart_card), create_summary_row(_("Enabled"), GTK_WIDGET(self->autostart_switch)));

    GtkWidget *autostart_apply = gtk_button_new_with_label(_("Apply autostart state"));
    gtk_widget_add_css_class(autostart_apply, "action-button");
    g_signal_connect(autostart_apply, "clicked", G_CALLBACK(on_apply_autostart_clicked), self);
    gtk_box_append(GTK_BOX(autostart_card), autostart_apply);

    self->autostart_details = GTK_LABEL(create_label_block("", "detail-text"));
    gtk_box_append(GTK_BOX(autostart_card), GTK_WIDGET(self->autostart_details));
    gtk_box_append(GTK_BOX(autostart_card), create_label_block(_("Disabling writes a user override in ~/.config/autostart so the system entry stays installed but does not start in your session."), "mini-note"));
    gtk_box_append(GTK_BOX(page), autostart_card);

    GtkWidget *actions_card = create_card(_("Session actions"), _("Immediate actions forwarded to the current system session."));
    GtkWidget *actions_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *logout_button = gtk_button_new_with_label(_("Log out"));
    GtkWidget *restart_button = gtk_button_new_with_label(_("Restart device"));
    GtkWidget *poweroff_button = gtk_button_new_with_label(_("Power off device"));
    gtk_widget_add_css_class(logout_button, "action-button");
    gtk_widget_add_css_class(restart_button, "action-button");
    gtk_widget_add_css_class(poweroff_button, "danger-button");
    g_object_set_data(G_OBJECT(logout_button), "session-action", (gpointer)"logout");
    g_object_set_data(G_OBJECT(restart_button), "session-action", (gpointer)"restart");
    g_object_set_data(G_OBJECT(poweroff_button), "session-action", (gpointer)"poweroff");
    g_signal_connect(logout_button, "clicked", G_CALLBACK(on_session_action_clicked), self);
    g_signal_connect(restart_button, "clicked", G_CALLBACK(on_session_action_clicked), self);
    g_signal_connect(poweroff_button, "clicked", G_CALLBACK(on_session_action_clicked), self);
    gtk_box_append(GTK_BOX(actions_row), logout_button);
    gtk_box_append(GTK_BOX(actions_row), restart_button);
    gtk_box_append(GTK_BOX(actions_row), poweroff_button);
    gtk_box_append(GTK_BOX(actions_card), actions_row);
    gtk_box_append(GTK_BOX(actions_card), create_label_block(_("These actions are executed immediately, so use them only from an interactive session."), "mini-note"));
    gtk_box_append(GTK_BOX(page), actions_card);

    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_generic_page(const KartonCategory *category)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    GtkWidget *hero = create_card(_("Module overview"), _(category->purpose));
    gtk_box_append(GTK_BOX(hero), create_label_block(_("This module is already modeled in the navigation and content architecture, so functional controls can be added without reshaping the app again."), NULL));
    gtk_box_append(GTK_BOX(page), hero);
    gtk_box_append(GTK_BOX(page), create_scope_card(category));
    return page;
}

static GtkWidget *
build_page(KartonSettingsWindow *self, const KartonCategory *category)
{
    if (g_strcmp0(category->id, "appearance") == 0) {
        return build_appearance_page(self, category);
    }
    if (g_strcmp0(category->id, "network") == 0) {
        return build_network_page(self, category);
    }
    if (g_strcmp0(category->id, "bluetooth") == 0) {
        return build_bluetooth_page(self, category);
    }
    if (g_strcmp0(category->id, "sound") == 0) {
        return build_sound_page(self, category);
    }
    if (g_strcmp0(category->id, "power") == 0) {
        return build_power_page(self, category);
    }
    if (g_strcmp0(category->id, "notifications") == 0) {
        return build_notifications_page(self, category);
    }
    if (g_strcmp0(category->id, "display") == 0) {
        return build_display_page(self, category);
    }
    if (g_strcmp0(category->id, "input") == 0) {
        return build_input_page(self, category);
    }
    if (g_strcmp0(category->id, "accessibility") == 0) {
        return build_accessibility_page(self, category);
    }
    if (g_strcmp0(category->id, "privacy") == 0) {
        return build_privacy_page(self, category);
    }
    if (g_strcmp0(category->id, "users") == 0) {
        return build_users_page(self, category);
    }
    if (g_strcmp0(category->id, "defaults") == 0) {
        return build_defaults_page(self, category);
    }
    if (g_strcmp0(category->id, "updates") == 0) {
        return build_updates_page(self, category);
    }
    if (g_strcmp0(category->id, "region") == 0) {
        return build_region_page(self, category);
    }
    if (g_strcmp0(category->id, "storage") == 0) {
        return build_storage_page(self, category);
    }
    if (g_strcmp0(category->id, "session") == 0) {
        return build_session_page(self, category);
    }
    if (g_strcmp0(category->id, "system") == 0) {
        return build_system_page(self, category);
    }
    if (g_strcmp0(category->id, "advanced") == 0) {
        return build_advanced_page(self, category);
    }
    return build_generic_page(category);
}

static GtkWidget *
create_sidebar_row(const KartonCategory *category)
{
    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_css_class(row, "sidebar-row");
    g_object_set_data(G_OBJECT(row), "category-id", (gpointer)category->id);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = gtk_image_new_from_icon_name(category->icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(labels), create_label_block(_(category->title), "sidebar-title"));
    gtk_box_append(GTK_BOX(labels), create_label_block(_(category->subtitle), "sidebar-subtitle"));
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), labels);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    return row;
}

GtkWindow *
karton_settings_window_new(GtkApplication *app)
{
    apply_css();

    KartonSettingsWindow *self = g_new0(KartonSettingsWindow, 1);
    self->window = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(self->window), _("Settings"));
    gtk_window_set_default_size(GTK_WINDOW(self->window), 1320, 840);
    gtk_widget_add_css_class(GTK_WIDGET(self->window), "settings-window");

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(root, "layout");
    gtk_window_set_child(GTK_WINDOW(self->window), root);

    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(sidebar, "sidebar");
    gtk_box_append(GTK_BOX(root), sidebar);

    GtkWidget *brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(brand), create_label_block(_("Karton"), "app-title"));
    gtk_box_append(GTK_BOX(brand), create_label_block(_("System settings"), "app-subtitle"));
    gtk_box_append(GTK_BOX(sidebar), brand);

    GtkWidget *sidebar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar_list), GTK_SELECTION_SINGLE);
    g_signal_connect(sidebar_list, "row-selected", G_CALLBACK(on_sidebar_selected), self);
    self->sidebar_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(self->sidebar_list, GTK_SELECTION_SINGLE);
    g_signal_connect(self->sidebar_list, "row-selected", G_CALLBACK(on_sidebar_selected), self);
    gtk_box_append(GTK_BOX(sidebar), GTK_WIDGET(self->sidebar_list));

    for (guint i = 0; i < G_N_ELEMENTS(categories); i++) {
        gtk_list_box_append(self->sidebar_list, create_sidebar_row(&categories[i]));
    }

    self->status_label = GTK_LABEL(create_label_block(_("Ready"), "mini-note"));
    gtk_box_append(GTK_BOX(sidebar), GTK_WIDGET(self->status_label));

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_add_css_class(content, "content");
    gtk_widget_set_hexpand(content, TRUE);
    gtk_box_append(GTK_BOX(root), content);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *header_labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    self->page_title = GTK_LABEL(create_label_block(_("Appearance"), "page-title"));
    self->page_subtitle = GTK_LABEL(create_label_block(_("Global theme, accent and desktop style"), "page-subtitle"));
    gtk_box_append(GTK_BOX(header_labels), GTK_WIDGET(self->page_title));
    gtk_box_append(GTK_BOX(header_labels), GTK_WIDGET(self->page_subtitle));
    gtk_box_append(GTK_BOX(header), header_labels);

    GtkWidget *refresh_button = gtk_button_new_with_label(_("Refresh"));
    gtk_widget_add_css_class(refresh_button, "action-button");
    gtk_widget_set_halign(refresh_button, GTK_ALIGN_END);
    gtk_widget_set_hexpand(refresh_button, TRUE);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), self);
    gtk_box_append(GTK_BOX(header), refresh_button);
    gtk_box_append(GTK_BOX(content), header);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(content), scroller);

    self->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(self->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), GTK_WIDGET(self->stack));

    for (guint i = 0; i < G_N_ELEMENTS(categories); i++) {
        GtkWidget *page = build_page(self, &categories[i]);
        gtk_widget_set_margin_bottom(page, 8);
        gtk_stack_add_named(self->stack, page, categories[i].id);
    }

    GtkListBoxRow *first_row = gtk_list_box_get_row_at_index(self->sidebar_list, 0);
    gtk_list_box_select_row(self->sidebar_list, first_row);

    g_object_set_data_full(G_OBJECT(self->window), "karton-settings-window", self, (GDestroyNotify)karton_settings_window_free);
    g_idle_add(refresh_all_idle, self);
    return GTK_WINDOW(self->window);
}