// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s) gettext(s)

struct prompt_state {
    GMainLoop *loop;
    GtkWidget *entry;
    gboolean accepted;
    char *value;
};

static void
prompt_finish(struct prompt_state *state, gboolean accepted)
{
    if (!state) {
        return;
    }

    state->accepted = accepted;
    if (state->loop && g_main_loop_is_running(state->loop)) {
        g_main_loop_quit(state->loop);
    }
}

static void
on_prompt_accept(GtkButton *button, gpointer user_data)
{
    (void)button;
    struct prompt_state *state = user_data;
    if (!state) {
        return;
    }

    const char *text = gtk_editable_get_text(GTK_EDITABLE(state->entry));
    g_free(state->value);
    state->value = g_strdup(text ? text : "");
    prompt_finish(state, TRUE);
}

static void
on_prompt_cancel(GtkButton *button, gpointer user_data)
{
    (void)button;
    prompt_finish((struct prompt_state *)user_data, FALSE);
}

static gboolean
on_prompt_close(GtkWindow *window, gpointer user_data)
{
    (void)window;
    prompt_finish((struct prompt_state *)user_data, FALSE);
    return FALSE;
}

static int
run_password_prompt(const char *title, const char *text)
{
    if (!gtk_init_check()) {
        return 1;
    }

    struct prompt_state state = { 0 };
    state.loop = g_main_loop_new(NULL, FALSE);

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title && *title ? title : _("Wi-Fi password"));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 440, -1);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);
    gtk_window_set_child(GTK_WINDOW(window), root);

    GtkWidget *label = gtk_label_new(text && *text ? text : _("Enter Wi-Fi password"));
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(root), label);

    state.entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(state.entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(state.entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_activates_default(GTK_ENTRY(state.entry), TRUE);
    gtk_box_append(GTK_BOX(root), state.entry);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
    GtkWidget *ok = gtk_button_new_with_label(_("Connect"));
    gtk_widget_add_css_class(ok, "suggested-action");

    gtk_box_append(GTK_BOX(actions), cancel);
    gtk_box_append(GTK_BOX(actions), ok);
    gtk_box_append(GTK_BOX(root), actions);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_prompt_cancel), &state);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_prompt_accept), &state);
    g_signal_connect(window, "close-request", G_CALLBACK(on_prompt_close), &state);

    gtk_window_set_default_widget(GTK_WINDOW(window), ok);
    gtk_window_present(GTK_WINDOW(window));

    g_main_loop_run(state.loop);

    gtk_window_destroy(GTK_WINDOW(window));

    int exit_code = 1;
    if (state.accepted && state.value && state.value[0]) {
        printf("%s\n", state.value);
        exit_code = 0;
    }

    g_free(state.value);
    g_main_loop_unref(state.loop);
    return exit_code;
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain("karton-session", LOCALEDIR);
    textdomain("karton-session");

    const char *title = _("Wi-Fi password");
    const char *text = _("Enter Wi-Fi password");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            title = argv[++i];
        } else if (strcmp(argv[i], "--text") == 0 && i + 1 < argc) {
            text = argv[++i];
        }
    }

    return run_password_prompt(title, text);
}