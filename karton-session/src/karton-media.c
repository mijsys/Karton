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
    GtkWidget *video;
    GtkWidget *status_label;
    GSettings *interface_settings;
    GFile *current_file;
} KartonMedia;

static void on_color_scheme_changed(GSettings *settings, gchar *key, gpointer user_data) {
    (void)settings;
    (void)key;
    (void)user_data;

    karton_theme_mode_apply();
}

static void set_status(KartonMedia *state, const char *text) {
    if (!state || !state->status_label) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(state->status_label), text ? text : "");
}

static GtkMediaStream *get_stream(KartonMedia *state) {
    if (!state || !state->video) {
        return NULL;
    }

    return gtk_video_get_media_stream(GTK_VIDEO(state->video));
}

static void load_media_file(KartonMedia *state, GFile *file) {
    GtkMediaStream *media;
    char *base;
    char *status;

    if (!state || !file) {
        return;
    }

    media = gtk_media_file_new_for_file(file);
    gtk_video_set_media_stream(GTK_VIDEO(state->video), GTK_MEDIA_STREAM(media));
    g_object_unref(media);

    g_clear_object(&state->current_file);
    state->current_file = g_object_ref(file);

    base = g_file_get_basename(file);
    status = g_strdup_printf(_("Loaded: %s"), base ? base : _("(unknown)"));
    set_status(state, status);
    g_free(status);
    g_free(base);
}

static void on_open_dialog_response(GtkNativeDialog *native, int response, gpointer user_data) {
    KartonMedia *state = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file) {
            load_media_file(state, file);
            g_object_unref(file);
        }
    }

    g_object_unref(native);
}

static void open_media_dialog(KartonMedia *state) {
    GtkFileChooserNative *dialog;

    if (!state || !state->window) {
        return;
    }

    dialog = gtk_file_chooser_native_new(
        _("Open media"),
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Open"),
        _("Cancel")
    );

    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), TRUE);
    g_signal_connect(dialog, "response", G_CALLBACK(on_open_dialog_response), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void play_pause(KartonMedia *state) {
    GtkMediaStream *stream;

    stream = get_stream(state);
    if (!stream) {
        return;
    }

    if (gtk_media_stream_get_playing(stream)) {
        gtk_media_stream_pause(stream);
        set_status(state, _("Paused"));
    } else {
        gtk_media_stream_play(stream);
        set_status(state, _("Playing"));
    }
}

static void stop_playback(KartonMedia *state) {
    GtkMediaStream *stream;

    stream = get_stream(state);
    if (!stream) {
        return;
    }

    gtk_media_stream_pause(stream);
    gtk_media_stream_seek(stream, 0);
    set_status(state, _("Stopped"));
}

static void activate(GtkApplication *app, gpointer user_data) {
    KartonMedia *state = user_data;
    GtkWidget *root;
    GtkWidget *toolbar;
    GtkWidget *open_btn;
    GtkWidget *play_btn;
    GtkWidget *stop_btn;

    state->window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(state->window), 960, 640);
    gtk_window_set_title(GTK_WINDOW(state->window), _("Karton Media"));

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
    g_signal_connect_swapped(open_btn, "clicked", G_CALLBACK(open_media_dialog), state);
    gtk_box_append(GTK_BOX(toolbar), open_btn);

    play_btn = gtk_button_new_with_label(_("Play/Pause"));
    g_signal_connect_swapped(play_btn, "clicked", G_CALLBACK(play_pause), state);
    gtk_box_append(GTK_BOX(toolbar), play_btn);

    stop_btn = gtk_button_new_with_label(_("Stop"));
    g_signal_connect_swapped(stop_btn, "clicked", G_CALLBACK(stop_playback), state);
    gtk_box_append(GTK_BOX(toolbar), stop_btn);

    state->video = gtk_video_new();
    gtk_widget_set_hexpand(state->video, TRUE);
    gtk_widget_set_vexpand(state->video, TRUE);
    gtk_box_append(GTK_BOX(root), state->video);

    state->status_label = gtk_label_new(_("No media loaded"));
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
    KartonMedia *state = user_data;
    int argc = 0;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);

    g_application_activate(app);

    if (argc > 1) {
        GFile *file = g_file_new_for_commandline_arg(argv[1]);
        load_media_file(state, file);
        g_object_unref(file);
    }

    g_strfreev(argv);
    return 0;
}

int main(int argc, char **argv) {
    KartonMedia state = {0};
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain("karton-session", LOCALEDIR);
    bind_textdomain_codeset("karton-session", "UTF-8");
    textdomain("karton-session");

    state.app = GTK_APPLICATION(gtk_application_new("io.karton.Media", G_APPLICATION_HANDLES_COMMAND_LINE));
    g_signal_connect(state.app, "activate", G_CALLBACK(activate), &state);
    g_signal_connect(state.app, "command-line", G_CALLBACK(command_line), &state);

    status = g_application_run(G_APPLICATION(state.app), argc, argv);

    g_clear_object(&state.interface_settings);
    g_clear_object(&state.current_file);
    g_clear_object(&state.app);

    return status;
}
