#include "window.h"

#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <libintl.h>
#include <vte/vte.h>

#define _(s) gettext(s)

typedef struct {
    GtkWidget *window;
    VteTerminal *terminal;
    gboolean shell_running;
    char *initial_command;
    char *working_directory;
} TerminalWindow;

static void free_terminal_window_state(gpointer user_data) {
    TerminalWindow *state = user_data;
    if (!state) {
        return;
    }

    g_free(state->initial_command);
    g_free(state->working_directory);
    g_free(state);
}

static void focus_terminal(TerminalWindow *state) {
    GtkWidget *terminal_widget = GTK_WIDGET(state->terminal);
    gtk_window_set_focus(GTK_WINDOW(state->window), terminal_widget);
    gtk_widget_grab_focus(terminal_widget);
}

static char *resolve_shell(void) {
    const char *forced_shell = g_getenv("KARTON_TERMINAL_SHELL");
    if (forced_shell && *forced_shell) {
        if (g_path_is_absolute(forced_shell) && g_file_test(forced_shell, G_FILE_TEST_IS_EXECUTABLE)) {
            return g_strdup(forced_shell);
        }

        char *forced_resolved = g_find_program_in_path(forced_shell);
        if (forced_resolved) {
            return forced_resolved;
        }
    }

    const char *prefer_fish_env = g_getenv("KARTON_TERMINAL_PREFER_FISH");
    gboolean prefer_fish = !prefer_fish_env || g_strcmp0(prefer_fish_env, "0") != 0;
    if (prefer_fish) {
        char *fish = g_find_program_in_path("fish");
        if (fish) {
            return fish;
        }
    }

    const char *shell_env = g_getenv("SHELL");

    if (shell_env && *shell_env) {
        if (g_path_is_absolute(shell_env) && g_file_test(shell_env, G_FILE_TEST_IS_EXECUTABLE)) {
            return g_strdup(shell_env);
        }

        char *resolved = g_find_program_in_path(shell_env);
        if (resolved) {
            return resolved;
        }
    }

    if (g_file_test("/bin/bash", G_FILE_TEST_IS_EXECUTABLE)) {
        return g_strdup("/bin/bash");
    }

    return g_strdup("/bin/sh");
}

static void on_shell_spawned(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data) {
    (void)terminal;
    (void)pid;

    TerminalWindow *state = user_data;

    if (error) {
        char *msg = g_strdup_printf(_("Cannot start shell: %s\r\n"), error->message);
        vte_terminal_feed(state->terminal, msg, -1);
        g_free(msg);
        state->shell_running = FALSE;
        return;
    }

    state->shell_running = TRUE;
    focus_terminal(state);
}

static void spawn_shell(TerminalWindow *state) {
    if (state->shell_running) {
        return;
    }

    char *shell = resolve_shell();
    char *argv[] = {shell, NULL, NULL, NULL};

    if (state->initial_command && *state->initial_command) {
        argv[1] = "-lc";
        argv[2] = state->initial_command;
    }

    gboolean shell_is_fish = g_str_has_suffix(shell, "/fish") || g_strcmp0(shell, "fish") == 0;

    if (shell_is_fish) {
        vte_terminal_feed(state->terminal, _("Autosuggestions: fish active. Press Right arrow or End to accept hints.\r\n"), -1);
    } else {
        vte_terminal_feed(state->terminal, _("Tip: press Tab for completion. Install fish for live suggestions.\r\n"), -1);
    }

    vte_terminal_spawn_async(
        state->terminal,
        VTE_PTY_DEFAULT,
        state->working_directory && *state->working_directory
            ? state->working_directory
            : g_get_home_dir(),
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        -1,
        NULL,
        on_shell_spawned,
        state
    );

    g_free(shell);
}

static void on_child_exited(VteTerminal *terminal, gint status, gpointer user_data) {
    (void)status;

    TerminalWindow *state = user_data;
    state->shell_running = FALSE;

    vte_terminal_feed(terminal, _("\r\nShell exited. Press Ctrl+Shift+R to restart.\r\n"), -1);
}

static gboolean on_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;

    TerminalWindow *state = user_data;
    guint key = gdk_keyval_to_lower(event->keyval);
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean ctrl = (mods & GDK_CONTROL_MASK) != 0;
    gboolean shift = (mods & GDK_SHIFT_MASK) != 0;

    if (ctrl && shift && key == GDK_KEY_c) {
        vte_terminal_copy_clipboard_format(state->terminal, VTE_FORMAT_TEXT);
        return TRUE;
    }

    if (ctrl && shift && key == GDK_KEY_v) {
        vte_terminal_paste_clipboard(state->terminal);
        return TRUE;
    }

    if (ctrl && shift && key == GDK_KEY_r) {
        vte_terminal_reset(state->terminal, TRUE, TRUE);
        spawn_shell(state);
        return TRUE;
    }

    return FALSE;
}

static gboolean on_window_map(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;

    TerminalWindow *state = user_data;
    focus_terminal(state);
    return FALSE;
}

static void on_terminal_realize(GtkWidget *widget, gpointer user_data) {
    (void)widget;

    TerminalWindow *state = user_data;
    focus_terminal(state);
}

GtkWidget *karton_terminal_window_new(GtkApplication *app,
    const char *initial_command,
    const char *working_directory) {
    TerminalWindow *state = g_new0(TerminalWindow, 1);

    GtkWidget *window = gtk_application_window_new(app);
    state->window = window;
    state->initial_command = g_strdup(initial_command);
    state->working_directory = g_strdup(working_directory);

    gtk_window_set_title(GTK_WINDOW(window), _("Karton Terminal"));
    gtk_window_set_default_size(GTK_WINDOW(window), 960, 620);
    gtk_window_set_focus_on_map(GTK_WINDOW(window), TRUE);

    GtkWidget *terminal_widget = vte_terminal_new();
    state->terminal = VTE_TERMINAL(terminal_widget);

    gtk_widget_set_hexpand(terminal_widget, TRUE);
    gtk_widget_set_vexpand(terminal_widget, TRUE);
    gtk_widget_set_can_focus(terminal_widget, TRUE);

    vte_terminal_set_scrollback_lines(state->terminal, 10000);
    vte_terminal_set_cursor_blink_mode(state->terminal, VTE_CURSOR_BLINK_ON);
    vte_terminal_set_mouse_autohide(state->terminal, TRUE);
    vte_terminal_set_input_enabled(state->terminal, TRUE);

    GdkRGBA fg = {0};
    GdkRGBA bg = {0};
    GdkRGBA cursor = {0};
    gdk_rgba_parse(&fg, "#f1f5f9");
    gdk_rgba_parse(&bg, "#0f172a");
    gdk_rgba_parse(&cursor, "#22d3ee");
    vte_terminal_set_colors(state->terminal, &fg, &bg, NULL, 0);
    vte_terminal_set_color_cursor(state->terminal, &cursor);

    gtk_container_add(GTK_CONTAINER(window), terminal_widget);

    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_pressed), state);
    g_signal_connect(window, "map-event", G_CALLBACK(on_window_map), state);
    g_signal_connect(terminal_widget, "realize", G_CALLBACK(on_terminal_realize), state);
    g_signal_connect(state->terminal, "child-exited", G_CALLBACK(on_child_exited), state);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(free_terminal_window_state), state);

    vte_terminal_feed(state->terminal,
        state->initial_command && *state->initial_command
            ? _("Starting command...\r\n")
            : _("Starting shell...\r\n"),
        -1);
    spawn_shell(state);
    gtk_widget_show_all(window);
    return window;

    g_object_set_data_full(G_OBJECT(window), "karton-terminal-state", state, NULL);
}
