#include <gio/gio.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <string.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#ifndef _
#define _(String) gettext(String)
#endif

typedef enum {
    SHOP_BACKEND_PACMAN = 0,
    SHOP_BACKEND_AUR = 1,
    SHOP_BACKEND_FLATPAK = 2,
} ShopBackend;

typedef struct {
    ShopBackend backend;
    gchar *id;
    gchar *repo;
    gchar *version;
    gchar *description;
    gboolean installed;
} ShopItem;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *search_entry;
    GtkDropDown *source_dropdown;
    GtkWidget *results_list;
    GtkWidget *status_label;
    GtkWidget *helper_label;
    gchar *aur_helper;
} ShopState;

typedef struct {
    ShopState *state;
    ShopItem *item;
    gboolean install;
} ShopActionContext;

static char *g_initial_search = NULL;

static void shop_item_free(ShopItem *item) {
    if (!item) {
        return;
    }

    g_free(item->id);
    g_free(item->repo);
    g_free(item->version);
    g_free(item->description);
    g_free(item);
}

static void shop_state_free(ShopState *state) {
    if (!state) {
        return;
    }

    g_free(state->aur_helper);
    g_free(state);
}

static void shop_action_context_free(ShopActionContext *context) {
    g_free(context);
}

static gboolean command_is_available(const char *name) {
    char *path = g_find_program_in_path(name);
    if (!path) {
        return FALSE;
    }

    g_free(path);
    return TRUE;
}

static gchar *find_aur_helper(void) {
    if (command_is_available("yay")) {
        return g_strdup("yay");
    }

    if (command_is_available("paru")) {
        return g_strdup("paru");
    }

    return NULL;
}

static ShopBackend selected_backend(ShopState *state) {
    guint idx = gtk_drop_down_get_selected(state->source_dropdown);
    if (idx > 2) {
        return SHOP_BACKEND_PACMAN;
    }

    return (ShopBackend)idx;
}

static const char *backend_label(ShopBackend backend) {
    switch (backend) {
        case SHOP_BACKEND_AUR:
            return _("AUR");
        case SHOP_BACKEND_FLATPAK:
            return _("Flatpak");
        case SHOP_BACKEND_PACMAN:
        default:
            return _("Pacman");
    }
}

static void status_set(ShopState *state, const char *text, gboolean is_error) {
    gtk_label_set_text(GTK_LABEL(state->status_label), text ? text : "");
    gtk_widget_remove_css_class(state->status_label, "error");
    gtk_widget_remove_css_class(state->status_label, "success");
    gtk_widget_add_css_class(state->status_label, is_error ? "error" : "success");
}

static gboolean run_command_capture(const char *command, char **stdout_out) {
    gchar *stdout_data = NULL;
    int wait_status = 0;
    gboolean ok = g_spawn_command_line_sync(command, stdout_out ? &stdout_data : NULL, NULL, &wait_status, NULL);

    if (!ok) {
        g_free(stdout_data);
        return FALSE;
    }

    if (stdout_out) {
        *stdout_out = stdout_data;
    } else {
        g_free(stdout_data);
    }

    (void)wait_status;
    return TRUE;
}

static char *theme_mode_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "theme-mode", NULL);
}

static char *read_theme_mode(void) {
    char *path = theme_mode_path();
    char *content = NULL;

    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        g_free(path);
        return g_strdup("auto");
    }

    g_free(path);
    g_strstrip(content);
    if (g_strcmp0(content, "light") != 0
        && g_strcmp0(content, "dark") != 0
        && g_strcmp0(content, "auto") != 0) {
        g_free(content);
        return g_strdup("auto");
    }

    return content;
}

static gboolean is_effective_dark_mode(const char *mode) {
    if (g_strcmp0(mode, "dark") == 0) {
        return TRUE;
    }

    if (g_strcmp0(mode, "light") == 0) {
        return FALSE;
    }

    GDateTime *now = g_date_time_new_now_local();
    gint hour = g_date_time_get_hour(now);
    g_date_time_unref(now);
    return (hour >= 19 || hour < 7);
}

static void apply_runtime_theme_mode(GtkWindow *window) {
    char *mode = read_theme_mode();
    gboolean prefer_dark = is_effective_dark_mode(mode);
    g_free(mode);

    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", prefer_dark, NULL);
    }

    if (window) {
        gtk_widget_remove_css_class(GTK_WIDGET(window), "theme-dark");
        gtk_widget_remove_css_class(GTK_WIDGET(window), "theme-light");
        gtk_widget_add_css_class(GTK_WIDGET(window), prefer_dark ? "theme-dark" : "theme-light");
    }
}

static gchar *next_token(const char **cursor) {
    const char *start;

    if (!cursor || !*cursor) {
        return NULL;
    }

    while (**cursor && g_ascii_isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }

    if (!**cursor) {
        return NULL;
    }

    start = *cursor;
    while (**cursor && !g_ascii_isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }

    return g_strndup(start, (gsize)(*cursor - start));
}

static gchar *trim_copy(const char *text) {
    if (!text) {
        return g_strdup("");
    }

    gchar *copy = g_strdup(text);
    g_strstrip(copy);
    return copy;
}

static GHashTable *load_installed_set(const char *command) {
    GHashTable *set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar *output = NULL;

    if (!run_command_capture(command, &output) || !output) {
        return set;
    }

    gchar **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (!line[0] || strchr(line, ' ') != NULL) {
            continue;
        }

        g_hash_table_add(set, g_strdup(line));
    }

    g_strfreev(lines);
    g_free(output);
    return set;
}

static gboolean set_contains(GHashTable *set, const char *value) {
    return set && value && g_hash_table_contains(set, value);
}

static void append_pacman_or_aur_item(GPtrArray *items, GHashTable *installed, ShopBackend backend, const char *line, ShopItem **current_item) {
    const char *cursor = line;
    gchar *package_token = next_token(&cursor);
    gchar *version_token = next_token(&cursor);

    if (!package_token || !version_token) {
        g_free(package_token);
        g_free(version_token);
        return;
    }

    gchar *repo = NULL;
    gchar *name = NULL;
    const char *slash = strchr(package_token, '/');
    if (slash) {
        repo = g_strndup(package_token, (gsize)(slash - package_token));
        name = g_strdup(slash + 1);
    } else {
        repo = g_strdup(backend == SHOP_BACKEND_AUR ? "AUR" : "Pacman");
        name = g_strdup(package_token);
    }

    ShopItem *item = g_new0(ShopItem, 1);
    item->backend = backend;
    item->id = name;
    item->repo = repo;
    item->version = g_strdup(version_token);
    item->installed = set_contains(installed, item->id);

    g_ptr_array_add(items, item);
    *current_item = item;

    g_free(package_token);
    g_free(version_token);
}

static void append_flatpak_item(GPtrArray *items, GHashTable *installed, const char *line) {
    const char *cursor = line;
    gchar *app_id = next_token(&cursor);
    gchar *branch = next_token(&cursor);
    gchar *desc = trim_copy(cursor);

    if (!app_id || !branch) {
        g_free(app_id);
        g_free(branch);
        g_free(desc);
        return;
    }

    ShopItem *item = g_new0(ShopItem, 1);
    item->backend = SHOP_BACKEND_FLATPAK;
    item->id = g_strdup(app_id);
    item->repo = g_strdup("Flatpak");
    item->version = g_strdup(branch);
    item->description = desc;
    item->installed = set_contains(installed, app_id);

    g_ptr_array_add(items, item);

    g_free(app_id);
    g_free(branch);
}

static GPtrArray *parse_search_output(ShopBackend backend, const char *output, GHashTable *installed) {
    GPtrArray *items = g_ptr_array_new();
    gchar **lines = g_strsplit(output ? output : "", "\n", -1);
    ShopItem *current_item = NULL;

    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = lines[i];
        if (!line) {
            continue;
        }

        if (backend == SHOP_BACKEND_FLATPAK) {
            gchar *trimmed = g_strstrip(line);
            if (!trimmed[0]) {
                continue;
            }
            append_flatpak_item(items, installed, trimmed);
            continue;
        }

        if (g_ascii_isspace((unsigned char)line[0])) {
            if (current_item && !current_item->description) {
                current_item->description = trim_copy(line);
            }
            continue;
        }

        if (!line[0]) {
            current_item = NULL;
            continue;
        }

        append_pacman_or_aur_item(items, installed, backend, line, &current_item);
    }

    g_strfreev(lines);
    return items;
}

static char *build_search_command(ShopState *state, ShopBackend backend, const char *query) {
    char *quoted_query = g_shell_quote(query ? query : "");
    char *command = NULL;

    switch (backend) {
        case SHOP_BACKEND_AUR:
            if (!state->aur_helper) {
                command = NULL;
            } else {
                command = g_strdup_printf("%s -Ss %s", state->aur_helper, quoted_query);
            }
            break;
        case SHOP_BACKEND_FLATPAK:
            command = g_strdup_printf("flatpak search --columns=application,branch,description %s", quoted_query);
            break;
        case SHOP_BACKEND_PACMAN:
        default:
            command = g_strdup_printf("pacman -Ss %s", quoted_query);
            break;
    }

    g_free(quoted_query);
    return command;
}

static char *build_action_command(ShopState *state, ShopItem *item, gboolean install) {
    char *quoted_id = g_shell_quote(item->id);
    char *script = NULL;

    switch (item->backend) {
        case SHOP_BACKEND_AUR:
            script = g_strdup_printf("%s %s %s; exec sh",
                install ? state->aur_helper : state->aur_helper,
                install ? "-S --needed" : "-Rns",
                quoted_id);
            break;
        case SHOP_BACKEND_FLATPAK:
            script = g_strdup_printf("flatpak %s -y %s; exec sh",
                install ? "install flathub" : "uninstall",
                quoted_id);
            break;
        case SHOP_BACKEND_PACMAN:
        default:
            script = g_strdup_printf(
                "if command -v sudo >/dev/null 2>&1; then sudo pacman %s %s; elif command -v pkexec >/dev/null 2>&1; then pkexec pacman %s %s; else pacman %s %s; fi; exec sh",
                install ? "-S --needed" : "-Rns",
                quoted_id,
                install ? "-S --needed" : "-Rns",
                quoted_id,
                install ? "-S --needed" : "-Rns",
                quoted_id);
            break;
    }

    g_free(quoted_id);
    return script;
}

static gboolean launch_terminal_command(const char *script, GtkWidget *parent, gchar **error_text) {
    char *wrapped = g_strdup_printf("sh -lc %s", g_shell_quote(script));
    gchar *argv[] = { "karton-terminal", "-e", wrapped, NULL };
    GError *error = NULL;

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        if (error_text) {
            *error_text = g_strdup(error && error->message ? error->message : "spawn failed");
        }
        g_clear_error(&error);
        g_free(wrapped);
        return FALSE;
    }

    (void)parent;
    g_free(wrapped);
    return TRUE;
}

static void on_item_action_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ShopActionContext *context = user_data;
    ShopState *state = context->state;
    char *script = build_action_command(state, context->item, context->install);
    gchar *error_text = NULL;

    if (!launch_terminal_command(script, state->window, &error_text)) {
        char *msg = g_strdup_printf("Blad: %s", error_text ? error_text : "spawn failed");
        status_set(state, msg, TRUE);
        g_free(msg);
        g_free(error_text);
    } else {
        char *msg = g_strdup_printf("%s: %s", context->install ? "Instalowanie" : "Usuwanie", context->item->id);
        status_set(state, msg, FALSE);
        g_free(msg);
    }

    g_free(script);
}

static void clear_results(ShopState *state) {
    GtkWidget *child = gtk_widget_get_first_child(state->results_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(state->results_list), child);
        child = next;
    }
}

static void update_helper_label(ShopState *state) {
    ShopBackend backend = selected_backend(state);

    if (backend == SHOP_BACKEND_AUR && !state->aur_helper) {
        gtk_label_set_text(GTK_LABEL(state->helper_label), _("Brak helpera AUR: zainstaluj yay lub paru"));
        gtk_widget_add_css_class(state->helper_label, "error");
        return;
    }

    gtk_widget_remove_css_class(state->helper_label, "error");
    if (backend == SHOP_BACKEND_AUR) {
        char *text = g_strdup_printf("AUR helper: %s", state->aur_helper);
        gtk_label_set_text(GTK_LABEL(state->helper_label), text);
        g_free(text);
        return;
    }

    gtk_label_set_text(GTK_LABEL(state->helper_label), backend == SHOP_BACKEND_FLATPAK ? "Flatpak" : "Pacman");
}

static GtkWidget *create_item_row(ShopState *state, ShopItem *item) {
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "card");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *title = gtk_label_new(item->id);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_add_css_class(title, "title-4");
    gtk_box_append(GTK_BOX(top), title);

    GtkWidget *badge = gtk_label_new(item->repo ? item->repo : backend_label(item->backend));
    gtk_widget_add_css_class(badge, "accent");
    gtk_box_append(GTK_BOX(top), badge);
    gtk_box_append(GTK_BOX(box), top);

    GtkWidget *meta = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(meta), 0.0f);
    char *meta_text = g_strdup_printf("%s • %s", item->repo ? item->repo : backend_label(item->backend), item->version ? item->version : "");
    gtk_label_set_text(GTK_LABEL(meta), meta_text);
    gtk_widget_add_css_class(meta, "dim-label");
    gtk_box_append(GTK_BOX(box), meta);
    g_free(meta_text);

    if (item->description && item->description[0]) {
        GtkWidget *desc = gtk_label_new(item->description);
        gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
        gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
        gtk_box_append(GTK_BOX(box), desc);
    }

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *install_button = gtk_button_new_with_label(_("Zainstaluj"));
    GtkWidget *remove_button = gtk_button_new_with_label(_("Usun"));

    gtk_widget_set_sensitive(install_button, !item->installed);
    gtk_widget_set_sensitive(remove_button, item->installed);

    ShopActionContext *install_context = g_new0(ShopActionContext, 1);
    install_context->state = state;
    install_context->item = item;
    install_context->install = TRUE;
    g_signal_connect_data(install_button, "clicked", G_CALLBACK(on_item_action_clicked), install_context, (GClosureNotify)shop_action_context_free, 0);

    ShopActionContext *remove_context = g_new0(ShopActionContext, 1);
    remove_context->state = state;
    remove_context->item = item;
    remove_context->install = FALSE;
    g_signal_connect_data(remove_button, "clicked", G_CALLBACK(on_item_action_clicked), remove_context, (GClosureNotify)shop_action_context_free, 0);

    gtk_box_append(GTK_BOX(actions), install_button);
    gtk_box_append(GTK_BOX(actions), remove_button);
    gtk_box_append(GTK_BOX(box), actions);
    gtk_frame_set_child(GTK_FRAME(frame), box);

    g_object_set_data_full(G_OBJECT(frame), "shop-item", item, (GDestroyNotify)shop_item_free);
    return frame;
}

static void populate_results(ShopState *state, GPtrArray *items) {
    clear_results(state);

    for (guint i = 0; i < items->len; i++) {
        ShopItem *item = g_ptr_array_index(items, i);
        GtkWidget *row = create_item_row(state, item);
        gtk_list_box_append(GTK_LIST_BOX(state->results_list), row);
    }
}

static void on_search_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ShopState *state = user_data;
    const char *query = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
    ShopBackend backend = selected_backend(state);
    char *search_command = NULL;
    char *output = NULL;
    GPtrArray *items = NULL;
    GHashTable *installed = NULL;

    if (!query || !*query) {
        status_set(state, "Wpisz nazwe pakietu.", TRUE);
        clear_results(state);
        return;
    }

    if (backend == SHOP_BACKEND_AUR && !state->aur_helper) {
        status_set(state, _("AUR: brak helpera (yay/paru)"), TRUE);
        clear_results(state);
        return;
    }

    search_command = build_search_command(state, backend, query);
    if (!search_command) {
        status_set(state, "Nie mozna zbudowac polecenia wyszukiwania.", TRUE);
        clear_results(state);
        return;
    }

    status_set(state, "Szukam...", FALSE);
    if (!run_command_capture(search_command, &output)) {
        status_set(state, "Nie udalo sie uruchomic wyszukiwania.", TRUE);
        g_free(search_command);
        g_free(output);
        return;
    }

    if (backend == SHOP_BACKEND_FLATPAK) {
        installed = load_installed_set("flatpak list --app --columns=application");
    } else {
        installed = load_installed_set("pacman -Qq");
    }

    items = parse_search_output(backend, output, installed);
    populate_results(state, items);

    if (items->len == 0) {
        status_set(state, "Brak wynikow.", TRUE);
    } else {
        char *msg = g_strdup_printf("Znaleziono %u wynikow.", items->len);
        status_set(state, msg, FALSE);
        g_free(msg);
    }

    g_ptr_array_free(items, TRUE);
    g_hash_table_destroy(installed);
    g_free(output);
    g_free(search_command);
}

static void run_search_from_entry(ShopState *state) {
    on_search_clicked(NULL, state);
}

static void on_source_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    update_helper_label(user_data);
}

static void on_clear_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    ShopState *state = user_data;
    gtk_editable_set_text(GTK_EDITABLE(state->search_entry), "");
    clear_results(state);
    status_set(state, _("Gotowy"), FALSE);
}

static void build_ui(ShopState *state) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(root, 20);
    gtk_widget_set_margin_end(root, 20);
    gtk_widget_set_margin_top(root, 20);
    gtk_widget_set_margin_bottom(root, 20);

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(hero, "card");
    gtk_widget_set_margin_bottom(hero, 4);

    GtkWidget *title = gtk_label_new(_("Karton Shop - Menedzer pakietow"));
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(hero), title);

    GtkWidget *subtitle = gtk_label_new(_("Wyszukuj pakiety i uruchamiaj instalacje w terminalu KartON."));
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(root), hero);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(controls, "card");

    GtkStringList *source_model = gtk_string_list_new(NULL);
    gtk_string_list_append(source_model, _("Pacman"));
    gtk_string_list_append(source_model, _("AUR"));
    gtk_string_list_append(source_model, _("Flatpak"));
    state->source_dropdown = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(source_model), NULL));
    g_object_unref(source_model);
    gtk_drop_down_set_selected(state->source_dropdown, 0);
    gtk_box_append(GTK_BOX(controls), GTK_WIDGET(state->source_dropdown));

    state->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->search_entry), "np. firefox");
    gtk_widget_set_hexpand(state->search_entry, TRUE);
    gtk_box_append(GTK_BOX(controls), state->search_entry);

    GtkWidget *search_button = gtk_button_new_with_label(_("Szukaj"));
    gtk_widget_add_css_class(search_button, "suggested-action");
    gtk_box_append(GTK_BOX(controls), search_button);

    GtkWidget *clear_button = gtk_button_new_with_label(_("Wyczysc"));
    gtk_box_append(GTK_BOX(controls), clear_button);

    gtk_box_append(GTK_BOX(root), controls);

    state->helper_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(state->helper_label), 0.0f);
    gtk_box_append(GTK_BOX(root), state->helper_label);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);

    state->results_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state->results_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(state->results_list, "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), state->results_list);
    gtk_box_append(GTK_BOX(root), scrolled);

    state->status_label = gtk_label_new(_("Gotowy"));
    gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
    gtk_widget_add_css_class(state->status_label, "dim-label");
    gtk_box_append(GTK_BOX(root), state->status_label);

    gtk_window_set_child(GTK_WINDOW(state->window), root);

    g_signal_connect(search_button, "clicked", G_CALLBACK(on_search_clicked), state);
    g_signal_connect(state->search_entry, "activate", G_CALLBACK(on_search_clicked), state);
    g_signal_connect(clear_button, "clicked", G_CALLBACK(on_clear_clicked), state);
    g_signal_connect(state->source_dropdown, "notify::selected", G_CALLBACK(on_source_changed), state);

    update_helper_label(state);

    if (g_initial_search && *g_initial_search) {
        gtk_editable_set_text(GTK_EDITABLE(state->search_entry), g_initial_search);
        run_search_from_entry(state);
    }
}

static void init_locale(void) {
    setlocale(LC_ALL, "");

    const char *locale_dir = g_getenv("KARTON_LOCALEDIR");
    if (!locale_dir || !*locale_dir) {
        locale_dir = LOCALEDIR;
    }

    bindtextdomain("karton-installer", locale_dir);
    bind_textdomain_codeset("karton-installer", "UTF-8");
    textdomain("karton-installer");
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    ShopState *state = g_new0(ShopState, 1);
    state->app = app;
    state->aur_helper = find_aur_helper();

    state->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state->window), _("Karton Shop"));
    gtk_window_set_default_size(GTK_WINDOW(state->window), 980, 680);

    apply_runtime_theme_mode(GTK_WINDOW(state->window));
    build_ui(state);
    g_object_set_data_full(G_OBJECT(state->window), "shop-state", state, (GDestroyNotify)shop_state_free);
    gtk_window_present(GTK_WINDOW(state->window));
}

int main(int argc, char **argv) {
    init_locale();

    for (int i = 1; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "--search=")) {
            g_free(g_initial_search);
            g_initial_search = g_strdup(argv[i] + strlen("--search="));
            continue;
        }

        if (g_strcmp0(argv[i], "--search") == 0 && i + 1 < argc) {
            g_free(g_initial_search);
            g_initial_search = g_strdup(argv[i + 1]);
            i++;
        }
    }

    GtkApplication *app = gtk_application_new("io.karton.Shop", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    g_free(g_initial_search);
    return status;
}