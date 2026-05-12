#include "window.h"
#include "page-appearance.h"
#include "page-display.h"
#include "page-audio.h"
#include "page-input.h"
#include "page-network.h"
#include "page-bluetooth.h"
#include "page-power.h"
#include "page-users.h"
#include "page-security.h"
#include "page-notifications.h"
#include "page-default-apps.h"
#include "page-updates.h"
#include "page-region.h"
#include "page-accessibility.h"
#include "page-files-disks.h"
#include "page-session-startup.h"
#include "page-system-info.h"
#include "page-advanced.h"
#include <libintl.h>
#include <gio/gio.h>
#include <stdio.h>

#define _(s) gettext(s)
#define N_(s) s

struct page_spec {
    const char *id;
    const char *title;
    const char *icon;
};

static const struct page_spec g_pages[] = {
    { "appearance", N_("Appearance and personalization"), "sidebar-appearance" },
    { "display", N_("Display and monitors"), "sidebar-display" },
    { "audio", N_("Sound"), "sidebar-sound" },
    { "devices", N_("Input devices"), "sidebar-devices" },
    { "network", N_("Network"), "sidebar-network" },
    { "bluetooth", N_("Bluetooth"), "sidebar-devices" },
    { "power", N_("Power"), "sidebar-power" },
    { "users", N_("Users and accounts"), "sidebar-users" },
    { "security", N_("Security and privacy"), "sidebar-security" },
    { "notifications", N_("Notifications"), "sidebar-notifications" },
    { "default-apps", N_("Default applications"), "sidebar-default-apps" },
    { "updates", N_("Updates and software"), "sidebar-updates" },
    { "region", N_("Date, time and region"), "preferences-system-time-symbolic" },
    { "accessibility", N_("Accessibility"), "preferences-desktop-accessibility-symbolic" },
    { "files-disks", N_("File manager and disks"), "drive-harddisk-symbolic" },
    { "session-startup", N_("Session and startup"), "system-run-symbolic" },
    { "system-info", N_("System information"), "computer-symbolic" },
    { "advanced", N_("Advanced / developer"), "applications-engineering-symbolic" },
};

static GtkWidget *create_sidebar_icon(const char *icon_name) {
    char *resource_path = g_strdup_printf("/io/karton/Settings/icons/%s.svg", icon_name);
    gsize size = 0;
    guint32 flags = 0;

    GtkWidget *icon = NULL;
    if (g_resources_get_info(resource_path, G_RESOURCE_LOOKUP_FLAGS_NONE, &size, &flags, NULL)) {
        icon = gtk_image_new_from_resource(resource_path);
    } else {
        icon = gtk_image_new_from_icon_name(icon_name);
    }

    gtk_image_set_pixel_size(GTK_IMAGE(icon), 19);
    g_free(resource_path);
    return icon;
}

static GtkWidget *create_sidebar_row(const char *title, const char *icon_name) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    GtkWidget *icon = create_sidebar_icon(icon_name);
    gtk_widget_add_css_class(icon, "sidebar-icon");
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_add_css_class(label, "sidebar-label");
    gtk_box_append(GTK_BOX(box), label);

    return box;
}

static int page_index_from_id(const char *page_id) {
    if (!page_id || !*page_id) {
        return 0;
    }

    if (g_ascii_strcasecmp(page_id, "appearance") == 0
        || g_ascii_strcasecmp(page_id, "theme") == 0
        || g_ascii_strcasecmp(page_id, "personalization") == 0) {
        return 0;
    }
    if (g_ascii_strcasecmp(page_id, "desktop") == 0
        || g_ascii_strcasecmp(page_id, "layout") == 0
        || g_ascii_strcasecmp(page_id, "dock") == 0
        || g_ascii_strcasecmp(page_id, "panel") == 0) {
        return 0;
    }
    if (g_ascii_strcasecmp(page_id, "display") == 0
        || g_ascii_strcasecmp(page_id, "screen") == 0
        || g_ascii_strcasecmp(page_id, "monitor") == 0
        || g_ascii_strcasecmp(page_id, "monitors") == 0) {
        return 1;
    }
    if (g_ascii_strcasecmp(page_id, "audio") == 0
        || g_ascii_strcasecmp(page_id, "sound") == 0) {
        return 2;
    }
    if (g_ascii_strcasecmp(page_id, "devices") == 0
        || g_ascii_strcasecmp(page_id, "device") == 0
        || g_ascii_strcasecmp(page_id, "input") == 0
        || g_ascii_strcasecmp(page_id, "inputs") == 0
        || g_ascii_strcasecmp(page_id, "keyboard") == 0
        || g_ascii_strcasecmp(page_id, "mouse") == 0
        || g_ascii_strcasecmp(page_id, "touchpad") == 0
        || g_ascii_strcasecmp(page_id, "controller") == 0) {
        return 3;
    }
    if (g_ascii_strcasecmp(page_id, "network") == 0
        || g_ascii_strcasecmp(page_id, "wifi") == 0
        || g_ascii_strcasecmp(page_id, "internet") == 0) {
        return 4;
    }
    if (g_ascii_strcasecmp(page_id, "bluetooth") == 0
        || g_ascii_strcasecmp(page_id, "bt") == 0) {
        return 5;
    }
    if (g_ascii_strcasecmp(page_id, "power") == 0
        || g_ascii_strcasecmp(page_id, "battery") == 0) {
        return 6;
    }
    if (g_ascii_strcasecmp(page_id, "users") == 0
        || g_ascii_strcasecmp(page_id, "user") == 0
        || g_ascii_strcasecmp(page_id, "account") == 0
        || g_ascii_strcasecmp(page_id, "accounts") == 0
        || g_ascii_strcasecmp(page_id, "login") == 0) {
        return 7;
    }
    if (g_ascii_strcasecmp(page_id, "security") == 0
        || g_ascii_strcasecmp(page_id, "privacy") == 0
        || g_ascii_strcasecmp(page_id, "secure") == 0
        || g_ascii_strcasecmp(page_id, "firewall") == 0) {
        return 8;
    }
    if (g_ascii_strcasecmp(page_id, "notifications") == 0
        || g_ascii_strcasecmp(page_id, "notification") == 0
        || g_ascii_strcasecmp(page_id, "notify") == 0
        || g_ascii_strcasecmp(page_id, "alerts") == 0
        || g_ascii_strcasecmp(page_id, "dnd") == 0) {
        return 9;
    }
    if (g_ascii_strcasecmp(page_id, "default-apps") == 0
        || g_ascii_strcasecmp(page_id, "defaultapps") == 0
        || g_ascii_strcasecmp(page_id, "defaults") == 0
        || g_ascii_strcasecmp(page_id, "apps") == 0) {
        return 10;
    }
    if (g_ascii_strcasecmp(page_id, "updates") == 0
        || g_ascii_strcasecmp(page_id, "update") == 0
        || g_ascii_strcasecmp(page_id, "software") == 0
        || g_ascii_strcasecmp(page_id, "packages") == 0
        || g_ascii_strcasecmp(page_id, "repo") == 0
        || g_ascii_strcasecmp(page_id, "repositories") == 0) {
        return 11;
    }
    if (g_ascii_strcasecmp(page_id, "region") == 0
        || g_ascii_strcasecmp(page_id, "locale") == 0
        || g_ascii_strcasecmp(page_id, "language") == 0
        || g_ascii_strcasecmp(page_id, "date") == 0
        || g_ascii_strcasecmp(page_id, "time") == 0
        || g_ascii_strcasecmp(page_id, "datetime") == 0
        || g_ascii_strcasecmp(page_id, "timezone") == 0) {
        return 12;
    }
    if (g_ascii_strcasecmp(page_id, "accessibility") == 0
        || g_ascii_strcasecmp(page_id, "a11y") == 0
        || g_ascii_strcasecmp(page_id, "screen-reader") == 0
        || g_ascii_strcasecmp(page_id, "magnifier") == 0
        || g_ascii_strcasecmp(page_id, "high-contrast") == 0
        || g_ascii_strcasecmp(page_id, "captions") == 0
        || g_ascii_strcasecmp(page_id, "osk") == 0
        || g_ascii_strcasecmp(page_id, "sticky-keys") == 0) {
        return 13;
    }
    if (g_ascii_strcasecmp(page_id, "files-disks") == 0
        || g_ascii_strcasecmp(page_id, "files") == 0
        || g_ascii_strcasecmp(page_id, "file-manager") == 0
        || g_ascii_strcasecmp(page_id, "storage") == 0
        || g_ascii_strcasecmp(page_id, "disks") == 0
        || g_ascii_strcasecmp(page_id, "mount") == 0
        || g_ascii_strcasecmp(page_id, "automount") == 0) {
        return 14;
    }
    if (g_ascii_strcasecmp(page_id, "session-startup") == 0
        || g_ascii_strcasecmp(page_id, "session") == 0
        || g_ascii_strcasecmp(page_id, "startup") == 0
        || g_ascii_strcasecmp(page_id, "autostart") == 0
        || g_ascii_strcasecmp(page_id, "login-manager") == 0
        || g_ascii_strcasecmp(page_id, "logout") == 0) {
        return 15;
    }
    if (g_ascii_strcasecmp(page_id, "system-info") == 0
        || g_ascii_strcasecmp(page_id, "system") == 0
        || g_ascii_strcasecmp(page_id, "hardware") == 0
        || g_ascii_strcasecmp(page_id, "diagnostics") == 0
        || g_ascii_strcasecmp(page_id, "logs") == 0
        || g_ascii_strcasecmp(page_id, "kernel") == 0) {
        return 16;
    }
    if (g_ascii_strcasecmp(page_id, "advanced") == 0
        || g_ascii_strcasecmp(page_id, "developer") == 0
        || g_ascii_strcasecmp(page_id, "debug") == 0
        || g_ascii_strcasecmp(page_id, "wayland") == 0
        || g_ascii_strcasecmp(page_id, "x11") == 0
        || g_ascii_strcasecmp(page_id, "renderer") == 0
        || g_ascii_strcasecmp(page_id, "experimental") == 0) {
        return 17;
    }

    return 0;
}

static GtkWidget *create_placeholder_page(const char *title) {
    GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page_box, 32);
    gtk_widget_set_margin_top(page_box, 32);
    gtk_widget_set_margin_end(page_box, 32);

    GtkWidget *header = gtk_label_new(title);
    gtk_widget_add_css_class(header, "title-1");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page_box), header);

    GtkWidget *desc = gtk_label_new(_("This module is prepared for expansion in future iterations."));
    gtk_widget_set_halign(desc, GTK_ALIGN_START);
    gtk_widget_add_css_class(desc, "placeholder-description");
    gtk_box_append(GTK_BOX(page_box), desc);
    return page_box;
}

static GtkWidget *create_page_by_index(guint index)
{
    if (index == 0) {
        return page_appearance_new();
    }
    if (index == 1) {
        return page_display_new();
    }
    if (index == 2) {
        return page_audio_new();
    }
    if (index == 3) {
        return page_input_new();
    }
    if (index == 4) {
        return page_network_new();
    }
    if (index == 5) {
        return page_bluetooth_new();
    }
    if (index == 6) {
        return page_power_new();
    }
    if (index == 7) {
        return page_users_new();
    }
    if (index == 8) {
        return page_security_new();
    }
    if (index == 9) {
        return page_notifications_new();
    }
    if (index == 10) {
        return page_default_apps_new();
    }
    if (index == 11) {
        return page_updates_new();
    }
    if (index == 12) {
        return page_region_new();
    }
    if (index == 13) {
        return page_accessibility_new();
    }
    if (index == 14) {
        return page_files_disks_new();
    }
    if (index == 15) {
        return page_session_startup_new();
    }
    if (index == 16) {
        return page_system_info_new();
    }
    if (index == 17) {
        return page_advanced_new();
    }

    return create_placeholder_page(_("Unknown module"));
}

static void ensure_page_loaded(GtkStack *stack, guint index)
{
    GPtrArray *cache = g_object_get_data(G_OBJECT(stack), "karton-pages-cache");
    if (!cache || index >= cache->len) {
        return;
    }

    if (g_ptr_array_index(cache, index) != NULL) {
        return;
    }

    GtkWidget *page = create_page_by_index(index);
    g_ptr_array_index(cache, index) = page;

    char page_name[32];
    snprintf(page_name, sizeof(page_name), "page_%u", index);

    GtkWidget *old = gtk_stack_get_child_by_name(stack, page_name);
    if (old) {
        gtk_stack_remove(stack, old);
    }

    gtk_stack_add_named(stack, page, page_name);
}

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    GtkStack *stack = GTK_STACK(user_data);
    if (!row) return;
    int index = gtk_list_box_row_get_index(row);

    ensure_page_loaded(stack, (guint)index);

    char page_name[32];
    snprintf(page_name, sizeof(page_name), "page_%d", index);
    gtk_stack_set_visible_child_name(stack, page_name);
}

void karton_settings_window_select_page(GtkWidget *window, const char *page_id) {
    if (!window || !GTK_IS_WINDOW(window)) {
        return;
    }

    GtkWidget *sidebar_list = g_object_get_data(G_OBJECT(window), "karton-sidebar-list");
    if (!sidebar_list || !GTK_IS_LIST_BOX(sidebar_list)) {
        return;
    }

    int index = page_index_from_id(page_id);
    GtkListBoxRow *target_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar_list), index);
    if (target_row) {
        gtk_list_box_select_row(GTK_LIST_BOX(sidebar_list), target_row);
    }
}

GtkWidget *karton_settings_window_new(GtkApplication *app, const char *initial_page) {
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), _("Karton System Settings"));
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    gtk_widget_add_css_class(window, "settings-window");

    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(root_box, "settings-root");
    gtk_window_set_child(GTK_WINDOW(window), root_box);

    GtkWidget *sidebar_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sidebar_scroll, 260, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(sidebar_scroll, "settings-sidebar");
    gtk_box_append(GTK_BOX(root_box), sidebar_scroll);

    GtkWidget *sidebar_list = gtk_list_box_new();
    gtk_widget_add_css_class(sidebar_list, "navigation-sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scroll), sidebar_list);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(root_box), separator);

    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(content_box, TRUE);
    gtk_widget_set_vexpand(content_box, TRUE);
    gtk_widget_add_css_class(content_box, "settings-content");
    gtk_box_append(GTK_BOX(root_box), content_box);

    GtkWidget *stack = gtk_stack_new();
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    gtk_widget_add_css_class(stack, "content-stack");
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_box_append(GTK_BOX(content_box), stack);

    GPtrArray *pages_cache = g_ptr_array_sized_new(G_N_ELEMENTS(g_pages));
    g_ptr_array_set_size(pages_cache, G_N_ELEMENTS(g_pages));
    g_object_set_data_full(G_OBJECT(stack), "karton-pages-cache", pages_cache, (GDestroyNotify)g_ptr_array_unref);

    g_signal_connect(sidebar_list, "row-selected", G_CALLBACK(on_row_selected), stack);

    for (guint i = 0; i < G_N_ELEMENTS(g_pages); i++) {
        GtkWidget *row = create_sidebar_row(_(g_pages[i].title), g_pages[i].icon);
        gtk_list_box_append(GTK_LIST_BOX(sidebar_list), row);

        GtkWidget *page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        char page_name[32];
        snprintf(page_name, sizeof(page_name), "page_%d", i);
        gtk_stack_add_named(GTK_STACK(stack), page_box, page_name);
    }

    g_object_set_data(G_OBJECT(window), "karton-sidebar-list", sidebar_list);
    karton_settings_window_select_page(window, initial_page);

    return window;
}
