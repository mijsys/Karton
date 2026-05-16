// SPDX-License-Identifier: GPL-2.0-only

#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <poppler.h>

#include "karton-theme.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s) gettext(s)

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *status_label;
    GtkWidget *drawing_area;
    GtkWidget *prev_btn;
    GtkWidget *next_btn;
    GSettings *interface_settings;
    GFile *current_file;
    PopplerDocument *document;
    int page_index;
    int page_count;
    double zoom;
} KartonPdf;

static void on_color_scheme_changed(GSettings *settings, gchar *key, gpointer user_data) {
    (void)settings;
    (void)key;
    (void)user_data;

    karton_theme_mode_apply();
}

static void set_status(KartonPdf *state, const char *text) {
    if (!state || !state->status_label) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(state->status_label), text ? text : "");
}

static void update_navigation(KartonPdf *state) {
    if (!state || !state->document) {
        if (state && state->prev_btn) {
            gtk_widget_set_sensitive(state->prev_btn, FALSE);
        }
        if (state && state->next_btn) {
            gtk_widget_set_sensitive(state->next_btn, FALSE);
        }
        return;
    }

    if (state->prev_btn) {
        gtk_widget_set_sensitive(state->prev_btn, state->page_index > 0);
    }

    if (state->next_btn) {
        gtk_widget_set_sensitive(state->next_btn, state->page_index + 1 < state->page_count);
    }
}

static void update_canvas_size(KartonPdf *state) {
    PopplerPage *page;
    double page_w;
    double page_h;
    int draw_w;
    int draw_h;

    if (!state || !state->document || !state->drawing_area) {
        return;
    }

    page = poppler_document_get_page(state->document, state->page_index);
    if (!page) {
        return;
    }

    poppler_page_get_size(page, &page_w, &page_h);
    g_object_unref(page);

    draw_w = MAX(1, (int)(page_w * state->zoom));
    draw_h = MAX(1, (int)(page_h * state->zoom));
    gtk_widget_set_size_request(state->drawing_area, draw_w, draw_h);
}

static void update_page_status(KartonPdf *state) {
    char *text;

    if (!state || !state->document) {
        set_status(state, _("No PDF loaded"));
        return;
    }

    text = g_strdup_printf(_("Page %d of %d"), state->page_index + 1, state->page_count);
    set_status(state, text);
    g_free(text);
}

static void update_title(KartonPdf *state) {
    char *base = NULL;
    char *title = NULL;

    if (!state || !state->window) {
        return;
    }

    if (state->current_file) {
        base = g_file_get_basename(state->current_file);
        title = g_strdup_printf(_("Karton PDF - %s"), base ? base : _("(unknown)"));
    } else {
        title = g_strdup(_("Karton PDF"));
    }

    gtk_window_set_title(GTK_WINDOW(state->window), title);
    g_free(title);
    g_free(base);
}

static void render_page(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    KartonPdf *state = user_data;
    PopplerPage *page;
    (void)area;
    (void)width;
    (void)height;

    if (!state || !state->document) {
        return;
    }

    page = poppler_document_get_page(state->document, state->page_index);
    if (!page) {
        return;
    }

    cairo_save(cr);
    cairo_scale(cr, state->zoom, state->zoom);
    poppler_page_render(page, cr);
    cairo_restore(cr);
    g_object_unref(page);
}

static void refresh_view(KartonPdf *state) {
    if (!state) {
        return;
    }

    update_canvas_size(state);
    update_navigation(state);
    update_page_status(state);

    if (state->drawing_area) {
        gtk_widget_queue_draw(state->drawing_area);
    }
}

static void clear_document(KartonPdf *state) {
    if (!state) {
        return;
    }

    g_clear_object(&state->document);
    state->page_index = 0;
    state->page_count = 0;
}

static void load_pdf_file(KartonPdf *state, GFile *file) {
    GError *error = NULL;
    char *uri;
    PopplerDocument *doc;

    if (!state || !file) {
        return;
    }

    uri = g_file_get_uri(file);
    doc = poppler_document_new_from_file(uri, NULL, &error);
    g_free(uri);

    if (!doc) {
        char *message = g_strdup_printf(_("Could not open PDF: %s"), error ? error->message : _("unknown error"));
        set_status(state, message);
        g_free(message);
        if (error) {
            g_error_free(error);
        }
        return;
    }

    clear_document(state);
    g_clear_object(&state->current_file);

    state->document = doc;
    state->current_file = g_object_ref(file);
    state->page_count = poppler_document_get_n_pages(state->document);
    state->page_index = 0;
    state->zoom = 1.0;

    if (state->page_count <= 0) {
        set_status(state, _("No PDF loaded"));
        clear_document(state);
        return;
    }

    update_title(state);
    refresh_view(state);
}

static void prev_page(KartonPdf *state) {
    if (!state || !state->document || state->page_index <= 0) {
        return;
    }

    state->page_index--;
    refresh_view(state);
}

static void next_page(KartonPdf *state) {
    if (!state || !state->document || state->page_index + 1 >= state->page_count) {
        return;
    }

    state->page_index++;
    refresh_view(state);
}

static void zoom_in(KartonPdf *state) {
    if (!state || !state->document) {
        return;
    }

    state->zoom = MIN(state->zoom * 1.15, 4.0);
    refresh_view(state);
}

static void zoom_out(KartonPdf *state) {
    if (!state || !state->document) {
        return;
    }

    state->zoom = MAX(state->zoom / 1.15, 0.25);
    refresh_view(state);
}

static void on_open_response(GtkNativeDialog *native, int response, gpointer user_data) {
    KartonPdf *state = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(native));
        if (file) {
            load_pdf_file(state, file);
            g_object_unref(file);
        }
    }

    g_object_unref(native);
}

static void open_dialog(KartonPdf *state) {
    GtkFileChooserNative *dialog;

    dialog = gtk_file_chooser_native_new(
        _("Open PDF file"),
        GTK_WINDOW(state->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Open"),
        _("Cancel")
    );

    g_signal_connect(dialog, "response", G_CALLBACK(on_open_response), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void activate(GtkApplication *app, gpointer user_data) {
    KartonPdf *state = user_data;
    GtkWidget *root;
    GtkWidget *toolbar;
    GtkWidget *open_btn;
    GtkWidget *prev_btn;
    GtkWidget *next_btn;
    GtkWidget *zoom_out_btn;
    GtkWidget *zoom_in_btn;
    GtkWidget *scroller;
    GtkWidget *hint_label;

    state->window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(state->window), 1000, 700);
    gtk_window_set_title(GTK_WINDOW(state->window), _("Karton PDF"));

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(state->window), root);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_widget_set_margin_start(toolbar, 8);
    gtk_widget_set_margin_end(toolbar, 8);
    gtk_widget_set_margin_top(toolbar, 8);
    gtk_widget_set_margin_bottom(toolbar, 8);
    gtk_box_append(GTK_BOX(root), toolbar);

    open_btn = gtk_button_new_with_label(_("Open PDF"));
    g_signal_connect_swapped(open_btn, "clicked", G_CALLBACK(open_dialog), state);
    gtk_box_append(GTK_BOX(toolbar), open_btn);

    prev_btn = gtk_button_new_with_label(_("Previous"));
    g_signal_connect_swapped(prev_btn, "clicked", G_CALLBACK(prev_page), state);
    gtk_box_append(GTK_BOX(toolbar), prev_btn);
    state->prev_btn = prev_btn;

    next_btn = gtk_button_new_with_label(_("Next"));
    g_signal_connect_swapped(next_btn, "clicked", G_CALLBACK(next_page), state);
    gtk_box_append(GTK_BOX(toolbar), next_btn);
    state->next_btn = next_btn;

    zoom_out_btn = gtk_button_new_with_label("-");
    g_signal_connect_swapped(zoom_out_btn, "clicked", G_CALLBACK(zoom_out), state);
    gtk_box_append(GTK_BOX(toolbar), zoom_out_btn);

    zoom_in_btn = gtk_button_new_with_label("+");
    g_signal_connect_swapped(zoom_in_btn, "clicked", G_CALLBACK(zoom_in), state);
    gtk_box_append(GTK_BOX(toolbar), zoom_in_btn);

    scroller = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_box_append(GTK_BOX(root), scroller);

    state->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state->drawing_area), render_page, state, NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), state->drawing_area);

    hint_label = gtk_label_new(_("Open PDF file to start reading."));
    gtk_widget_set_halign(hint_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(hint_label, 12);
    gtk_widget_set_margin_end(hint_label, 12);
    gtk_widget_set_margin_top(hint_label, 8);
    gtk_widget_set_margin_bottom(hint_label, 0);
    gtk_box_append(GTK_BOX(root), hint_label);

    state->status_label = gtk_label_new(_("Ready"));
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(state->status_label, 12);
    gtk_widget_set_margin_end(state->status_label, 12);
    gtk_widget_set_margin_top(state->status_label, 8);
    gtk_widget_set_margin_bottom(state->status_label, 8);
    gtk_box_append(GTK_BOX(root), state->status_label);

    state->zoom = 1.0;
    karton_theme_mode_apply();
    state->interface_settings = karton_theme_open_interface_settings();
    if (state->interface_settings) {
        g_signal_connect(state->interface_settings, "changed::color-scheme", G_CALLBACK(on_color_scheme_changed), state);
    }
    update_navigation(state);
    gtk_window_present(GTK_WINDOW(state->window));
}

static int command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data) {
    KartonPdf *state = user_data;
    int argc = 0;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);

    g_application_activate(app);

    if (argc > 1) {
        GFile *file = g_file_new_for_commandline_arg(argv[1]);
        load_pdf_file(state, file);
        g_object_unref(file);
    }

    g_strfreev(argv);
    return 0;
}

int main(int argc, char **argv) {
    KartonPdf state = {0};
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain("karton-session", LOCALEDIR);
    bind_textdomain_codeset("karton-session", "UTF-8");
    textdomain("karton-session");

    state.app = GTK_APPLICATION(gtk_application_new("io.karton.PDF", G_APPLICATION_HANDLES_COMMAND_LINE));
    g_signal_connect(state.app, "activate", G_CALLBACK(activate), &state);
    g_signal_connect(state.app, "command-line", G_CALLBACK(command_line), &state);

    status = g_application_run(G_APPLICATION(state.app), argc, argv);

    g_clear_object(&state.interface_settings);
    g_clear_object(&state.document);
    g_clear_object(&state.current_file);
    g_clear_object(&state.app);

    return status;
}
