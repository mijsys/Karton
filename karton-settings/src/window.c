#include "window.h"
#include "page-appearance.h"
#include "page-display.h"
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
    { "devices", N_("Devices"), "sidebar-devices" },
    { "network", N_("Network"), "sidebar-network" },
    { "bluetooth", N_("Bluetooth"), "sidebar-devices" },
    { "power", N_("Power"), "sidebar-power" },
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
        || g_ascii_strcasecmp(page_id, "input") == 0) {
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

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    GtkStack *stack = GTK_STACK(user_data);
    if (!row) return;
    int index = gtk_list_box_row_get_index(row);

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

    g_signal_connect(sidebar_list, "row-selected", G_CALLBACK(on_row_selected), stack);

    for (guint i = 0; i < G_N_ELEMENTS(g_pages); i++) {
        GtkWidget *row = create_sidebar_row(_(g_pages[i].title), g_pages[i].icon);
        gtk_list_box_append(GTK_LIST_BOX(sidebar_list), row);

        GtkWidget *page_box;
        if (i == 0) {
            page_box = page_appearance_new();
        } else if (i == 1) {
            page_box = page_display_new();
        } else {
            page_box = create_placeholder_page(_(g_pages[i].title));
        }

        char page_name[32];
        snprintf(page_name, sizeof(page_name), "page_%d", i);
        gtk_stack_add_named(GTK_STACK(stack), page_box, page_name);
    }

    g_object_set_data(G_OBJECT(window), "karton-sidebar-list", sidebar_list);
    karton_settings_window_select_page(window, initial_page);

    return window;
}
