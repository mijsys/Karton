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
    GtkWidget *text_view;
    GtkWidget *status_label;
    GSettings *interface_settings;
    GtkTextBuffer *buffer;
    GFile *current_file;
} KartonText;

static void on_color_scheme_changed(GSettings *settings, gchar *key, gpointer user_data) {
    (void)settings;
    (void)key;
    (void)user_data;

    karton_theme_mode_apply();
}

static void set_status(KartonText *state, const char *text) {
    if (!state || !state->status_label) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(state->status_label), text ? text : "");
}

static void update_title(KartonText *state) {
    char *base = NULL;
    char *title = NULL;

    if (!state || !state->window) {
        return;
    }

    if (state->current_file) {
        base = g_file_get_basename(state->current_file);
        title = g_strdup_printf(_("Karton Text - %s"), base ? base : _("(unknown)"));
    } else {
        title = g_strdup(_("Karton Text"));
    }

    gtk_window_set_title(GTK_WINDOW(state->window), title);
    g_free(title);
    g_free(base);
}

static void new_document(KartonText *state) {
    if (!state || !state->buffer) {
        return;
    }

    gtk_text_buffer_set_text(state->buffer, "", -1);
    g_clear_object(&state->current_file);
    set_status(state, _("New document"));
    update_title(state);
}

static void load_text_file(KartonText *state, GFile *file) {
    GError *error = NULL;
    char *contents = NULL;
    gsize len = 0;
    char *base;
    char *status;

    if (!state || !file) {
        return;
    }

    if (!g_file_load_contents(file, NULL, &contents, &len, NULL, &error)) {
        char *message = g_strdup_printf(_("Open failed: %s"), error ? error->message : _("unknown error"));
        set_status(state, message);
        g_free(message);
        if (error) {
            g_error_free(error);
        }
        return;
    }

    gtk_text_buffer_set_text(state->buffer, contents, (int)len);
    g_free(contents);

    g_clear_object(&state->current_file);
    state->current_file = g_object_ref(file);

    base = g_file_get_basename(file);
    status = g_strdup_printf(_("Opened: %s"), base ? base : _("(unknown)"));
    set_status(state, status);
    g_free(status);
    g_free(base);
    update_title(state);
}

static gboolean save_to_file(KartonText *state, GFile *file) {
    GtkTextIter start;
    GtkTextIter end;
    char *text;
    GError *error = NULL;
    gboolean ok;
    char *base;
    char *status;

    if (!state || !state->buffer || !file) {
        return FALSE;
    }

    gtk_text_buffer_get_start_iter(state->buffer, &start);
    gtk_text_buffer_get_end_iter(state->buffer, &end);
    text = gtk_text_buffer_get_text(state->buffer, &start, &end, FALSE);

    ok = g_file_replace_contents(
        file,
        text,
        strlen(text),
        NULL,
        FALSE,
        G_FILE_CREATE_NONE,
        NULL,
        NULL,
        &error
    );
    g_free(text);

    if (!ok) {
        char *message = g_strdup_printf(_("Save failed: %s"), error ? error->message : _("unknown error"));
        set_status(state, message);
        g_free(message);
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    g_clear_object(&state->current_file);
    state->current_file = g_object_ref(file);

    base = g_file_get_basename(file);
    status = g_strdup_printf(_("Saved: %s"), base ? base : _("(unknown)"));
    set_status(state, status);
    g_free(status);
    g_free(base);
    update_title(state);

    return TRUE;
}

static void on_open_response(GtkNativeDialog *native, int response, gpointer user_data) {
    KartonText *state = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file) {
            load_text_file(state, file);
            g_object_unref(file);
        }
    }

    g_object_unref(native);
}

static void on_save_as_response(GtkNativeDialog *native, int response, gpointer user_data) {
    KartonText *state = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file) {
            save_to_file(state, file);
            g_object_unref(file);
        }
    }

    g_object_unref(native);
}

static void open_dialog(KartonText *state) {
    GtkFileChooserNative *dialog;

    dialog = gtk_file_chooser_native_new(
        _("Open text file"),
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Open"),
        _("Cancel")
    );

    g_signal_connect(dialog, "response", G_CALLBACK(on_open_response), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void save_as_dialog(KartonText *state) {
    GtkFileChooserNative *dialog;

    dialog = gtk_file_chooser_native_new(
        _("Save text file"),
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("Save"),
        _("Cancel")
    );

    g_signal_connect(dialog, "response", G_CALLBACK(on_save_as_response), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void save_current(KartonText *state) {
    if (state->current_file) {
        save_to_file(state, state->current_file);
    } else {
        save_as_dialog(state);
    }
}

static void activate(GtkApplication *app, gpointer user_data) {
    KartonText *state = user_data;
    GtkWidget *root;
    GtkWidget *toolbar;
    GtkWidget *scroller;
    GtkWidget *new_btn;
    GtkWidget *open_btn;
    GtkWidget *save_btn;
    GtkWidget *save_as_btn;

    state->window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(state->window), 900, 640);

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(state->window), root);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_widget_set_margin_start(toolbar, 8);
    gtk_widget_set_margin_end(toolbar, 8);
    gtk_widget_set_margin_top(toolbar, 8);
    gtk_widget_set_margin_bottom(toolbar, 8);
    gtk_box_append(GTK_BOX(root), toolbar);

    new_btn = gtk_button_new_with_label(_("New"));
    g_signal_connect_swapped(new_btn, "clicked", G_CALLBACK(new_document), state);
    gtk_box_append(GTK_BOX(toolbar), new_btn);

    open_btn = gtk_button_new_with_label(_("Open"));
    g_signal_connect_swapped(open_btn, "clicked", G_CALLBACK(open_dialog), state);
    gtk_box_append(GTK_BOX(toolbar), open_btn);

    save_as_btn = gtk_button_new_with_label(_("Save As"));
    g_signal_connect_swapped(save_as_btn, "clicked", G_CALLBACK(save_as_dialog), state);
    gtk_box_append(GTK_BOX(toolbar), save_as_btn);

    save_btn = gtk_button_new_with_label(_("Save"));
    g_signal_connect_swapped(save_btn, "clicked", G_CALLBACK(save_current), state);
    gtk_box_append(GTK_BOX(toolbar), save_btn);

    scroller = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(root), scroller);

    state->text_view = gtk_text_view_new();
    state->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->text_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), state->text_view);

    state->status_label = gtk_label_new(_("Ready"));
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(state->status_label, 12);
    gtk_widget_set_margin_end(state->status_label, 12);
    gtk_widget_set_margin_top(state->status_label, 8);
    gtk_widget_set_margin_bottom(state->status_label, 8);
    gtk_box_append(GTK_BOX(root), state->status_label);

    update_title(state);
    karton_theme_mode_apply();
    state->interface_settings = karton_theme_open_interface_settings();
    if (state->interface_settings) {
        g_signal_connect(state->interface_settings, "changed::color-scheme", G_CALLBACK(on_color_scheme_changed), state);
    }
    gtk_window_present(GTK_WINDOW(state->window));
}

static int command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    KartonText *state = user_data;
    int argc = 0;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);

    g_application_activate(app);

    if (argc > 1) {
        GFile *file = g_file_new_for_commandline_arg(argv[1]);
        load_text_file(state, file);
        g_object_unref(file);
    }

    g_strfreev(argv);
    return 0;
}

int main(int argc, char **argv) {
    KartonText state = {0};
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain("karton-session", LOCALEDIR);
    bind_textdomain_codeset("karton-session", "UTF-8");
    textdomain("karton-session");

    state.app = GTK_APPLICATION(gtk_application_new("io.karton.Text", G_APPLICATION_HANDLES_COMMAND_LINE));
    g_signal_connect(state.app, "activate", G_CALLBACK(activate), &state);
    g_signal_connect(state.app, "command-line", G_CALLBACK(command_line), &state);

    status = g_application_run(G_APPLICATION(state.app), argc, argv);

    g_clear_object(&state.interface_settings);
    g_clear_object(&state.current_file);
    g_clear_object(&state.app);

    return status;
}
