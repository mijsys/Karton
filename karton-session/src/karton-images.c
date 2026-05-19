// SPDX-License-Identifier: GPL-2.0-only

#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>

#include "karton-theme.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s) gettext(s)

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *picture;
    GtkWidget *status_label;
    GSettings *interface_settings;
    GFile *current_file;
    GdkTexture *texture;
    double zoom;
    gboolean fit_mode;
} KartonImages;

static void on_color_scheme_changed(GSettings *settings, gchar *key, gpointer user_data) {
    (void)settings;
    (void)key;
    (void)user_data;

    karton_theme_mode_apply();
}

static void update_picture_view(KartonImages *state) {
    int w;
    int h;

    if (!state || !state->picture) {
        return;
    }

    if (!state->texture) {
        gtk_picture_set_paintable(GTK_PICTURE(state->picture), NULL);
        gtk_label_set_text(GTK_LABEL(state->status_label), _("No image loaded"));
        return;
    }

    gtk_picture_set_paintable(GTK_PICTURE(state->picture), GDK_PAINTABLE(state->texture));

    if (state->fit_mode) {
        gtk_picture_set_content_fit(GTK_PICTURE(state->picture), GTK_CONTENT_FIT_CONTAIN);
        gtk_picture_set_can_shrink(GTK_PICTURE(state->picture), TRUE);
        gtk_widget_set_size_request(state->picture, -1, -1);
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Fit to window"));
        return;
    }

    gtk_picture_set_content_fit(GTK_PICTURE(state->picture), GTK_CONTENT_FIT_FILL);
    gtk_picture_set_can_shrink(GTK_PICTURE(state->picture), FALSE);

    w = gdk_texture_get_width(state->texture);
    h = gdk_texture_get_height(state->texture);

    if (w > 0 && h > 0) {
        int zoom_w = (int)(w * state->zoom);
        int zoom_h = (int)(h * state->zoom);
        gtk_widget_set_size_request(state->picture, MAX(1, zoom_w), MAX(1, zoom_h));
    }
}

static void set_status_with_filename(KartonImages *state, const char *prefix) {
    char *base = NULL;
    char *text = NULL;

    if (!state || !state->current_file) {
        return;
    }

    base = g_file_get_basename(state->current_file);
    text = g_strdup_printf("%s: %s", prefix, base ? base : _("(unknown)"));
    gtk_label_set_text(GTK_LABEL(state->status_label), text);
    g_free(text);
    g_free(base);
}

static void load_image_file(KartonImages *state, GFile *file) {
    GError *error = NULL;
    GdkTexture *new_texture = NULL;

    if (!state || !file) {
        return;
    }

    new_texture = gdk_texture_new_from_file(file, &error);
    if (!new_texture) {
        char *message = g_strdup_printf(_("Could not open image: %s"), error ? error->message : _("unknown error"));
        gtk_label_set_text(GTK_LABEL(state->status_label), message);
        g_free(message);
        if (error) {
            g_error_free(error);
        }
        return;
    }

    g_clear_object(&state->texture);
    g_clear_object(&state->current_file);

    state->texture = new_texture;
    state->current_file = g_object_ref(file);
    state->zoom = 1.0;
    state->fit_mode = TRUE;

    update_picture_view(state);
    set_status_with_filename(state, _("Loaded"));
}

static void on_open_dialog_response(GtkNativeDialog *native, int response, gpointer user_data) {
    KartonImages *state = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file) {
            load_image_file(state, file);
            g_object_unref(file);
        }
    }

    g_object_unref(native);
}

static void open_image_dialog(KartonImages *state) {
    GtkFileChooserNative *dialog;

    if (!state || !state->window) {
        return;
    }

    dialog = gtk_file_chooser_native_new(
        _("Open image"),
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Open"),
        _("Cancel")
    );

    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), TRUE);
    g_signal_connect(dialog, "response", G_CALLBACK(on_open_dialog_response), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void zoom_in(KartonImages *state) {

    if (!state || !state->texture) {
        return;
    }

    state->fit_mode = FALSE;
    state->zoom = MIN(state->zoom * 1.2, 10.0);
    update_picture_view(state);
}

static void zoom_out(KartonImages *state) {

    if (!state || !state->texture) {
        return;
    }

    state->fit_mode = FALSE;
    state->zoom = MAX(state->zoom / 1.2, 0.1);
    update_picture_view(state);
}

static void fit_to_window(KartonImages *state) {

    if (!state || !state->texture) {
        return;
    }

    state->fit_mode = TRUE;
    update_picture_view(state);
}

static void activate(GtkApplication *app, gpointer user_data) {
    KartonImages *state = user_data;
    GtkWidget *toolbar;
    GtkWidget *open_btn;
    GtkWidget *zoom_in_btn;
    GtkWidget *zoom_out_btn;
    GtkWidget *fit_btn;
    GtkWidget *root;
    GtkWidget *scroll;

    state->window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1000, 700);
    gtk_window_set_title(GTK_WINDOW(state->window), _("Karton Images"));

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(state->window), root);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_widget_set_margin_start(toolbar, 8);
    gtk_widget_set_margin_end(toolbar, 8);
    gtk_widget_set_margin_top(toolbar, 8);
    gtk_widget_set_margin_bottom(toolbar, 8);
    gtk_box_append(GTK_BOX(root), toolbar);

    open_btn = gtk_button_new_with_label(_("Open"));
    g_signal_connect_swapped(open_btn, "clicked", G_CALLBACK(open_image_dialog), state);
    gtk_box_append(GTK_BOX(toolbar), open_btn);

    fit_btn = gtk_button_new_with_label(_("Fit"));
    g_signal_connect_swapped(fit_btn, "clicked", G_CALLBACK(fit_to_window), state);
    gtk_box_append(GTK_BOX(toolbar), fit_btn);

    zoom_out_btn = gtk_button_new_with_label("-");
    g_signal_connect_swapped(zoom_out_btn, "clicked", G_CALLBACK(zoom_out), state);
    gtk_box_append(GTK_BOX(toolbar), zoom_out_btn);

    zoom_in_btn = gtk_button_new_with_label("+");
    g_signal_connect_swapped(zoom_in_btn, "clicked", G_CALLBACK(zoom_in), state);
    gtk_box_append(GTK_BOX(toolbar), zoom_in_btn);

    scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(root), scroll);

    state->picture = gtk_picture_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), state->picture);

    state->status_label = gtk_label_new(_("No image loaded"));
    gtk_widget_set_margin_start(state->status_label, 12);
    gtk_widget_set_margin_end(state->status_label, 12);
    gtk_widget_set_margin_top(state->status_label, 8);
    gtk_widget_set_margin_bottom(state->status_label, 8);
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(root), state->status_label);

    karton_theme_mode_apply();
    state->interface_settings = karton_theme_open_interface_settings();
    if (state->interface_settings) {
        g_signal_connect(state->interface_settings, "changed::color-scheme", G_CALLBACK(on_color_scheme_changed), state);
    }
    gtk_window_present(GTK_WINDOW(state->window));
}

static int command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    KartonImages *state = user_data;
    int argc = 0;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);

    g_application_activate(app);

    if (argc > 1) {
        GFile *file = g_file_new_for_commandline_arg(argv[1]);
        load_image_file(state, file);
        g_object_unref(file);
    }

    g_strfreev(argv);
    return 0;
}

int main(int argc, char **argv) {
    KartonImages state = {0};
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain("karton-session", LOCALEDIR);
    bind_textdomain_codeset("karton-session", "UTF-8");
    textdomain("karton-session");

    state.zoom = 1.0;
    state.fit_mode = TRUE;

    state.app = GTK_APPLICATION(gtk_application_new("io.karton.Images", G_APPLICATION_HANDLES_COMMAND_LINE));
    g_signal_connect(state.app, "activate", G_CALLBACK(activate), &state);
    g_signal_connect(state.app, "command-line", G_CALLBACK(command_line), &state);

    status = g_application_run(G_APPLICATION(state.app), argc, argv);

    g_clear_object(&state.texture);
    g_clear_object(&state.interface_settings);
    g_clear_object(&state.current_file);
    g_clear_object(&state.app);

    return status;
}
