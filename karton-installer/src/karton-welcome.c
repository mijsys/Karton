#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#ifndef _
#define _(String) gettext(String)
#endif

enum welcome_mode {
    WELCOME_MODE_INSTALL,
    WELCOME_MODE_FRESH,
    WELCOME_MODE_REGULAR,
};

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *title_label;
    GtkWidget *body_label;
    GtkWidget *status_label;
    GtkWidget *primary_button;
    GtkWidget *dont_show_check;
    bool auto_mode;
    enum welcome_mode mode;
} KartonWelcome;

static char *welcome_seen_path(void) {
    return g_build_filename(g_get_user_state_dir(), "karton", "welcome-seen", NULL);
}

static bool welcome_is_seen(void) {
    char *path = welcome_seen_path();
    bool seen = g_file_test(path, G_FILE_TEST_EXISTS);
    g_free(path);
    return seen;
}

static void welcome_mark_seen(void) {
    char *path = welcome_seen_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0755) == 0) {
        g_file_set_contents(path, "seen\n", -1, NULL);
    }

    g_free(dir);
    g_free(path);
}

static void welcome_clear_seen(void) {
    char *path = welcome_seen_path();
    g_remove(path);
    g_free(path);
}

static void apply_welcome_preference(KartonWelcome *state) {
    if (!state->dont_show_check) {
        return;
    }

    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(state->dont_show_check))) {
        welcome_mark_seen();
    } else {
        welcome_clear_seen();
    }
}

static bool file_contains_text(const char *path, const char *needle) {
    char *contents = NULL;
    bool contains = false;

    if (g_file_get_contents(path, &contents, NULL, NULL) && contents) {
        contains = strstr(contents, needle) != NULL;
    }

    g_free(contents);
    return contains;
}

static bool detect_live_environment(void) {
    const char *forced = g_getenv("KARTON_WELCOME_MODE");
    if (forced && *forced) {
        if (g_strcmp0(forced, "install") == 0 || g_strcmp0(forced, "live") == 0) {
            return true;
        }
        if (g_strcmp0(forced, "fresh") == 0 || g_strcmp0(forced, "regular") == 0) {
            return false;
        }
    }

    return g_file_test("/run/archiso/bootmnt", G_FILE_TEST_EXISTS)
        || g_file_test("/run/archiso/cowspace", G_FILE_TEST_EXISTS)
        || g_file_test("/.archiso", G_FILE_TEST_EXISTS)
        || file_contains_text("/etc/hostname", "archiso");
}

static enum welcome_mode detect_welcome_mode(void) {
    if (detect_live_environment()) {
        return WELCOME_MODE_INSTALL;
    }
    if (!welcome_is_seen()) {
        return WELCOME_MODE_FRESH;
    }
    return WELCOME_MODE_REGULAR;
}

static void set_status(KartonWelcome *state, const char *text) {
    gtk_label_set_text(GTK_LABEL(state->status_label), text ? text : "");
}

static void spawn_command(KartonWelcome *state, const char *command) {
    GError *error = NULL;

    if (!g_spawn_command_line_async(command, &error)) {
        set_status(state, error && error->message ? error->message : _("Could not start command"));
        g_clear_error(&error);
        return;
    }

    set_status(state, _("Uruchomiono."));
}

static void spawn_in_terminal(KartonWelcome *state, const char *shell_command) {
    char *quoted = g_shell_quote(shell_command);
    char *command = g_strdup_printf("karton-terminal -e %s", quoted);
    GError *error = NULL;

    if (!g_spawn_command_line_async(command, &error)) {
        set_status(state, error && error->message ? error->message : _("Could not start command"));
        g_clear_error(&error);
    } else {
        set_status(state, _("Uruchomiono."));
    }

    g_free(command);
    g_free(quoted);
}

static void on_open_shop(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    apply_welcome_preference(state);
    spawn_command(state, "karton-shop");
}

static void open_uri(KartonWelcome *state, const char *uri) {
    GError *error = NULL;
    if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
        set_status(state, error && error->message ? error->message : _("Could not open link"));
        g_clear_error(&error);
    }
}

static void on_primary_clicked(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;

    apply_welcome_preference(state);

    if (state->mode == WELCOME_MODE_INSTALL) {
        spawn_command(state, "karton-install");
        return;
    }

    spawn_command(state, "karton-settings");
}

static void on_secondary_settings(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    apply_welcome_preference(state);
    spawn_command(state, "karton-settings");
}

static void on_secondary_terminal(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    apply_welcome_preference(state);
    spawn_command(state, "karton-terminal");
}

static void on_open_install(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    apply_welcome_preference(state);
    spawn_command(state, "karton-install");
}

static char *update_system_script(void) {
    char *msg_sudo = g_shell_quote(_("Starting system update (sudo)."));
    char *msg_pkexec = g_shell_quote(_("Sudo unavailable, trying pkexec."));
    char *msg_no_elevate = g_shell_quote(_("No sudo or pkexec found. Cannot elevate privileges for system update."));
    char *msg_skip_yay = g_shell_quote(_("Skipping yay (yay does not work as root)."));
    char *msg_no_backend = g_shell_quote(_("No supported package manager found."));
    char *msg_done = g_shell_quote(_("Update completed."));
    char *msg_failed = g_shell_quote(_("Update failed or was canceled."));

    char *script = g_strdup_printf(
        "MSG_SUDO=%s "
        "MSG_PKEXEC=%s "
        "MSG_NO_ELEVATE=%s "
        "MSG_SKIP_YAY=%s "
        "MSG_NO_BACKEND=%s "
        "MSG_DONE=%s "
        "MSG_FAILED=%s "
        "bash -lc '"
        "set -u; "
        "ELEVATE=\"\"; "
        "status=0; "
        "if [ \"$(id -u)\" -ne 0 ]; then "
            "if command -v sudo >/dev/null 2>&1; then "
                "ELEVATE=\"sudo\"; "
                "printf \"%%s\\n\" \"$MSG_SUDO\"; "
            "elif command -v pkexec >/dev/null 2>&1; then "
                "ELEVATE=\"pkexec\"; "
                "printf \"%%s\\n\" \"$MSG_PKEXEC\"; "
            "else "
                "printf \"%%s\\n\" \"$MSG_NO_ELEVATE\"; "
                "status=1; "
            "fi; "
        "fi; "

        "if [ \"$status\" -eq 0 ]; then "
            "if command -v pacman >/dev/null 2>&1; then "
                "$ELEVATE pacman -Syu || status=1; "
                "if command -v yay >/dev/null 2>&1; then "
                    "if [ \"$(id -u)\" -eq 0 ]; then "
                        "printf \"%%s\\n\" \"$MSG_SKIP_YAY\"; "
                    "else "
                        "yay -Syu || status=1; "
                    "fi; "
                "fi; "
            "elif command -v apt-get >/dev/null 2>&1; then "
                "$ELEVATE env DEBIAN_FRONTEND=noninteractive apt-get update && "
                "$ELEVATE env DEBIAN_FRONTEND=noninteractive apt-get -y upgrade || status=1; "
            "elif command -v dnf >/dev/null 2>&1; then "
                "$ELEVATE dnf -y upgrade --refresh || status=1; "
            "elif command -v zypper >/dev/null 2>&1; then "
                "$ELEVATE zypper --non-interactive refresh && "
                "$ELEVATE zypper --non-interactive update || status=1; "
            "else "
                "printf \"%%s\\n\" \"$MSG_NO_BACKEND\"; "
                "status=1; "
            "fi; "

            "if command -v flatpak >/dev/null 2>&1; then "
                "flatpak update -y || status=1; "
            "fi; "
        "fi; "

        "if [ \"$status\" -eq 0 ]; then "
            "printf \"%%s\\n\" \"$MSG_DONE\"; "
        "else "
            "printf \"%%s\\n\" \"$MSG_FAILED\"; "
        "fi; "
        "exec \"${SHELL:-bash}\""
        "'",
        msg_sudo,
        msg_pkexec,
        msg_no_elevate,
        msg_skip_yay,
        msg_no_backend,
        msg_done,
        msg_failed);

    g_free(msg_sudo);
    g_free(msg_pkexec);
    g_free(msg_no_elevate);
    g_free(msg_skip_yay);
    g_free(msg_no_backend);
    g_free(msg_done);
    g_free(msg_failed);

    return script;
}

static void on_update_system(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    apply_welcome_preference(state);
    char *script = update_system_script();
    spawn_in_terminal(state, script);
    g_free(script);
}

static void on_readme_clicked(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    open_uri(state, "https://github.com/mijsys/Tektura-i-Karton");
}

static void on_dismiss_clicked(GtkButton *button, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)button;
    apply_welcome_preference(state);
    gtk_window_destroy(GTK_WINDOW(state->window));
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data) {
    KartonWelcome *state = user_data;
    (void)window;
    apply_welcome_preference(state);
    return FALSE;
}

static void configure_labels(KartonWelcome *state) {
    const char *user = g_get_real_name();
    if (!user || !user[0] || g_strcmp0(user, "Unknown") == 0) {
        user = g_get_user_name();
    }

    char *title = g_strdup_printf(_("Witaj %s w KartON"), user && user[0] ? user : _("uzytkowniku"));
    const char *body = _("Witamy w KartON.");
    const char *primary = _("Otworz ustawienia");

    if (state->mode == WELCOME_MODE_INSTALL) {
        body = _("Wykryto srodowisko live Arch. Mozesz uruchomic graficzny frontend instalacji KartON albo przejrzec repozytorium projektu.");
        primary = _("Uruchom karton-install");
    } else if (state->mode == WELCOME_MODE_FRESH) {
        body = _("To wyglada jak swiezo uruchomiona instalacja. Otworz ustawienia, sklep pakietow, zaktualizuj system lub zamknij ten ekran i nie pokazuj go ponownie.");
        primary = _("Zacznij konfiguracje");
    } else {
        body = _("Masz pod reka szybkie zakladki: ustawienia, sklep pakietow, aktualizacja systemu i terminal.");
    }

    gtk_label_set_text(GTK_LABEL(state->title_label), title);
    gtk_label_set_text(GTK_LABEL(state->body_label), body);
    gtk_button_set_label(GTK_BUTTON(state->primary_button), primary);
    g_free(title);
}

static void activate(GtkApplication *app, gpointer user_data) {
    KartonWelcome *state = user_data;
    GtkWidget *root;
    GtkWidget *hero;
    GtkWidget *actions;
    GtkWidget *switcher;
    GtkWidget *stack;
    GtkWidget *tab;
    GtkWidget *row1;
    GtkWidget *row2;
    GtkWidget *button;

    if (state->auto_mode && state->mode == WELCOME_MODE_REGULAR) {
        g_application_quit(G_APPLICATION(app));
        return;
    }

    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), _("Karton Welcome"));
    gtk_window_set_default_size(GTK_WINDOW(state->window), 760, 460);

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(root, 24);
    gtk_widget_set_margin_end(root, 24);
    gtk_widget_set_margin_top(root, 24);
    gtk_widget_set_margin_bottom(root, 24);
    gtk_window_set_child(GTK_WINDOW(state->window), root);

    hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(hero, "card");
    gtk_box_append(GTK_BOX(root), hero);

    state->title_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(state->title_label, "title-1");
    gtk_label_set_wrap(GTK_LABEL(state->title_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(state->title_label), 0.0f);
    gtk_box_append(GTK_BOX(hero), state->title_label);

    state->body_label = gtk_label_new(NULL);
    gtk_label_set_wrap(GTK_LABEL(state->body_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(state->body_label), 0.0f);
    gtk_box_append(GTK_BOX(hero), state->body_label);

    actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(actions, 24);
    gtk_box_append(GTK_BOX(root), actions);

    switcher = gtk_stack_switcher_new();
    gtk_box_append(GTK_BOX(actions), switcher);

    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    gtk_box_append(GTK_BOX(actions), stack);

    tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(tab), row1);

    state->primary_button = gtk_button_new_with_label(_("Primary"));
    gtk_widget_add_css_class(state->primary_button, "suggested-action");
    g_signal_connect(state->primary_button, "clicked", G_CALLBACK(on_primary_clicked), state);
    gtk_box_append(GTK_BOX(row1), state->primary_button);

    button = gtk_button_new_with_label(_("Otworz ustawienia"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_secondary_settings), state);
    gtk_box_append(GTK_BOX(row1), button);

    button = gtk_button_new_with_label(_("Otworz terminal"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_secondary_terminal), state);
    gtk_box_append(GTK_BOX(row1), button);
    gtk_stack_add_titled(GTK_STACK(stack), tab, "start", _("Start"));

    tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(tab), row2);

    button = gtk_button_new_with_label(_("Sklep pakietow"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_open_shop), state);
    gtk_box_append(GTK_BOX(row2), button);

    button = gtk_button_new_with_label(_("Zaktualizuj system"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_update_system), state);
    gtk_box_append(GTK_BOX(row2), button);

    button = gtk_button_new_with_label(_("Uruchom karton-install"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_open_install), state);
    gtk_box_append(GTK_BOX(row2), button);
    gtk_stack_add_titled(GTK_STACK(stack), tab, "system", _("System"));

    tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(tab), row2);

    button = gtk_button_new_with_label(_("Repozytorium projektu"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_readme_clicked), state);
    gtk_box_append(GTK_BOX(row2), button);

    button = gtk_button_new_with_label(_("Zamknij"));
    g_signal_connect(button, "clicked", G_CALLBACK(on_dismiss_clicked), state);
    gtk_box_append(GTK_BOX(row2), button);
    gtk_stack_add_titled(GTK_STACK(stack), tab, "info", _("Info"));

    state->dont_show_check = gtk_check_button_new_with_label(_("Nie pokazuj mnie wiecej"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(state->dont_show_check), welcome_is_seen());
    gtk_box_append(GTK_BOX(actions), state->dont_show_check);

    state->status_label = gtk_label_new("");
    gtk_widget_set_margin_top(state->status_label, 18);
    gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
    gtk_box_append(GTK_BOX(root), state->status_label);

    configure_labels(state);
    g_signal_connect(state->window, "close-request", G_CALLBACK(on_close_request), state);
    gtk_window_present(GTK_WINDOW(state->window));
}

int main(int argc, char **argv) {
    KartonWelcome state = {0};
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain("karton-installer", LOCALEDIR);
    bind_textdomain_codeset("karton-installer", "UTF-8");
    textdomain("karton-installer");

    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--auto") == 0) {
            state.auto_mode = true;
        }
    }

    state.mode = detect_welcome_mode();
    state.app = GTK_APPLICATION(gtk_application_new("io.karton.Welcome", G_APPLICATION_NON_UNIQUE));
    g_signal_connect(state.app, "activate", G_CALLBACK(activate), &state);

    status = g_application_run(G_APPLICATION(state.app), argc, argv);
    g_clear_object(&state.app);
    return status;
}