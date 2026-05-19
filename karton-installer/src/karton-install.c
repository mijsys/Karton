#include <ctype.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#ifndef DATADIR
#define DATADIR "/usr/local/share"
#endif

#ifndef _
#define _(String) gettext(String)
#endif

typedef struct {
    const char *code;
    const char *label;
} LanguageOption;

typedef struct {
    const char *id;
    const char *label;
    const char *variants[8];
} KeyboardLayout;

typedef struct {
    const char *region;
    const char *cities[16];
} TimezoneRegion;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *step_label;
    GtkWidget *title_label;
    GtkWidget *subtitle_label;
    GtkWidget *status_label;

    GtkWidget *back_button;
    GtkWidget *next_button;
    GtkWidget *install_button;

    GtkWidget *language_search;
    GtkWidget *language_list;
    gchar *selected_language;
    gchar *selected_locale;

    GtkWidget *keyboard_layout;
    GtkStringList *keyboard_layout_model;
    GPtrArray *keyboard_layout_codes;
    GtkStringList *keyboard_variant_model;
    GPtrArray *keyboard_variant_codes;
    GtkWidget *keyboard_variant;
    GtkWidget *keyboard_test;

    GtkWidget *timezone_region;
    GtkStringList *timezone_region_model;
    GPtrArray *timezone_region_codes;
    GtkStringList *timezone_city_model;
    GtkWidget *timezone_city;
    GPtrArray *timezone_list;

    GtkWidget *drivers_nvidia;
    GtkWidget *drivers_codecs;
    GtkWidget *profile_full;
    GtkWidget *profile_minimal;
    GtkWidget *drivers_hint;

    GtkWidget *disk_dropdown;
    GtkStringList *disk_model;
    GPtrArray *disk_ids;
    GtkWidget *partition_auto;
    GtkWidget *partition_manual;
    GtkWidget *partition_stack;
    GtkWidget *encrypt_switch;
    GtkWidget *encrypt_box;
    GtkWidget *encrypt_password;
    GtkWidget *manual_list;
    GtkStringList *manual_fs_model;
    GtkWidget *manual_size;
    GtkWidget *manual_fs;
    GtkWidget *manual_mount;

    GtkWidget *full_name;
    GtkWidget *user_name;
    GtkWidget *host_name;
    GtkWidget *password1;
    GtkWidget *password2;
    GtkWidget *password_strength;
    GtkWidget *show_password;
    GtkWidget *auto_login;
    GtkWidget *same_root_password;

    GtkWidget *summary_view;

    GtkWidget *progress_status;
    GtkWidget *progress_bar;
    GtkWidget *slideshow_stack;
    GtkWidget *show_logs_button;
    GtkWidget *logs_revealer;
    GtkWidget *logs_view;

    GtkWidget *finish_title;
    GtkWidget *finish_subtitle;

    int step;
    guint slideshow_timer_id;
    guint progress_timer_id;
    guint slideshow_index;
    gboolean install_running;

    GSubprocess *install_proc;
    GDataInputStream *install_stream;
} KartonInstall;

enum {
    STEP_LANGUAGE = 0,
    STEP_KEYBOARD = 1,
    STEP_TIMEZONE = 2,
    STEP_SOFTWARE = 3,
    STEP_PARTITION = 4,
    STEP_USER = 5,
    STEP_SUMMARY = 6,
    STEP_PROGRESS = 7,
    STEP_FINISHED = 8,
    STEP_COUNT = 9,
};

static const char *step_titles[STEP_COUNT] = {
    "Wybierz jezyk",
    "Klawiatura",
    "Strefa czasowa",
    "Oprogramowanie i sterowniki",
    "Przygotowanie dysku",
    "Tworzenie uzytkownika",
    "Podsumowanie",
    "Instalacja",
    "Zakonczono",
};

static const char *step_subtitles[STEP_COUNT] = {
    "Wybierz jezyk instalatora i systemu.",
    "Wybierz uklad i wariant klawiatury, potem przetestuj pisanie.",
    "Ustaw region i miasto. Instalator probuje wykryc strefe automatycznie.",
    "Wybierz sterowniki, kodeki i profil instalacji.",
    "Wybierz dysk i metode partycjonowania.",
    "Skonfiguruj konto, hostname i haslo.",
    "Sprawdz wszystko przed uruchomieniem instalacji.",
    "Trwa instalacja systemu.",
    "System jest gotowy do uzycia.",
};

static const KeyboardLayout keyboard_layouts[] = {
    { "pl", "Polski", { "Polski", "Polski (programisty)", "Polski (Dvorak)", NULL } },
    { "us", "Angielski (US)", { "US", "US International", "Dvorak", "Colemak", NULL } },
    { "gb", "Angielski (UK)", { "UK", "UK Extended", NULL } },
    { "de", "Niemiecki", { "German", "German Neo", NULL } },
    { "fr", "Francuski", { "French", "French AZERTY", NULL } },
    { NULL, NULL, { NULL } },
};


static gchar *read_first_line(const char *cmd) {
    char *out = NULL;
    char *err = NULL;
    gint status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync(cmd, &out, &err, &status, &error)) {
        g_clear_error(&error);
        g_free(out);
        g_free(err);
        return NULL;
    }

    if (status != 0 || !out || !out[0]) {
        g_free(out);
        g_free(err);
        return NULL;
    }

    gchar **parts = g_strsplit(out, "\n", 2);
    gchar *line = g_strdup(parts[0] ? g_strstrip(parts[0]) : "");
    g_strfreev(parts);
    g_free(out);
    g_free(err);
    return line;
}

static gboolean program_is_available(const char *name) {
    char *path = g_find_program_in_path(name);
    if (!path) {
        return FALSE;
    }
    g_free(path);
    return TRUE;
}

static char *read_command_output(const char *cmd);

static GPtrArray *list_system_locales(void) {
    char *output = NULL;
    if (program_is_available("localectl")) {
        output = read_command_output("localectl list-locales 2>/dev/null");
    }
    if (!output) {
        output = read_command_output("locale -a 2>/dev/null");
    }
    if (!output) {
        return NULL;
    }

    GPtrArray *locales = g_ptr_array_new_with_free_func(g_free);
    gchar **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (!line || !*line) {
            continue;
        }
        if (g_str_has_prefix(line, "C") || g_str_has_prefix(line, "POSIX")) {
            continue;
        }
        g_ptr_array_add(locales, g_strdup(line));
    }
    g_strfreev(lines);
    g_free(output);
    if (locales->len == 0) {
        g_ptr_array_add(locales, g_strdup("en_US.UTF-8"));
    }
    return locales;
}

static GPtrArray *list_system_timezones(void) {
    char *output = NULL;
    if (program_is_available("timedatectl")) {
        output = read_command_output("timedatectl list-timezones 2>/dev/null");
    }
    if (!output) {
        output = read_command_output("find /usr/share/zoneinfo -maxdepth 2 -type f | sed 's|/usr/share/zoneinfo/||' | grep -v '^right/' | grep -v '^posix/' | sort");
    }
    if (!output) {
        return NULL;
    }

    GPtrArray *zones = g_ptr_array_new_with_free_func(g_free);
    gchar **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (!line || !*line) {
            continue;
        }
        if (g_str_has_prefix(line, "posix/") || g_str_has_prefix(line, "right/")) {
            continue;
        }
        if (strchr(line, '/') == NULL) {
            continue;
        }
        g_ptr_array_add(zones, g_strdup(line));
    }
    g_strfreev(lines);
    g_free(output);
    if (zones->len == 0) {
        g_ptr_array_add(zones, g_strdup("Europe/Warsaw"));
    }
    return zones;
}

static GPtrArray *unique_timezone_regions(GPtrArray *zones) {
    if (!zones) {
        return NULL;
    }
    GHashTable *regions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < zones->len; i++) {
        const char *zone = g_ptr_array_index(zones, i);
        gchar **parts = g_strsplit(zone, "/", 2);
        if (parts[0] && *parts[0]) {
            g_hash_table_add(regions, g_strdup(parts[0]));
        }
        g_strfreev(parts);
    }
    GPtrArray *list = g_ptr_array_new_with_free_func(g_free);
    GList *keys = g_hash_table_get_keys(regions);
    keys = g_list_sort(keys, (GCompareFunc)g_ascii_strcasecmp);
    for (GList *l = keys; l; l = l->next) {
        g_ptr_array_add(list, g_strdup((const char *)l->data));
    }
    g_list_free(keys);
    g_hash_table_destroy(regions);
    return list;
}

static char *read_command_output(const char *cmd) {
    char *out = NULL;
    char *err = NULL;
    gint status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync(cmd, &out, &err, &status, &error)) {
        g_clear_error(&error);
        g_free(out);
        g_free(err);
        return NULL;
    }

    if (status != 0 || !out || !out[0]) {
        g_free(out);
        g_free(err);
        return NULL;
    }

    char *trimmed = g_strdup(g_strstrip(out));
    g_free(out);
    g_free(err);
    return trimmed;
}

static GPtrArray *list_system_keymap_layouts(void) {
    if (!program_is_available("localectl")) {
        return NULL;
    }

    char *output = read_command_output("localectl list-x11-keymap-layouts 2>/dev/null");
    if (!output) {
        return NULL;
    }

    GPtrArray *list = g_ptr_array_new_with_free_func(g_free);
    gchar **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (*line) {
            g_ptr_array_add(list, g_strdup(line));
        }
    }
    g_strfreev(lines);
    g_free(output);
    return list;
}

static GPtrArray *list_system_keymap_variants(const char *layout) {
    if (!program_is_available("localectl") || !layout || !*layout) {
        return NULL;
    }

    char *cmd = g_strdup_printf("localectl list-x11-keymap-variants %s 2>/dev/null", layout);
    char *output = read_command_output(cmd);
    g_free(cmd);
    if (!output) {
        return NULL;
    }

    GPtrArray *list = g_ptr_array_new_with_free_func(g_free);
    gchar **lines = g_strsplit(output, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (*line) {
            g_ptr_array_add(list, g_strdup(line));
        }
    }
    g_strfreev(lines);
    g_free(output);
    return list;
}

static gchar *find_best_locale(const char *lang_code) {
    if (!lang_code || !*lang_code) {
        return g_strdup("en_US.UTF-8");
    }

    gchar *canonical = NULL;
    if (program_is_available("locale")) {
        char *cmd = g_strdup_printf("/bin/bash -lc 'locale -a 2>/dev/null | grep -E \"^%s(_|-)\" | grep -i utf-8 | head -n 1'", lang_code);
        canonical = read_command_output(cmd);
        g_free(cmd);
    }

    if (!canonical || !*canonical) {
        if (program_is_available("localectl")) {
            char *cmd = g_strdup_printf("localectl list-locales 2>/dev/null | grep -E \"^%s(_|-)\" | grep -i utf-8 | head -n 1", lang_code);
            canonical = read_command_output(cmd);
            g_free(cmd);
        }
    }

    static const struct {
        const char *code;
        const char *locale;
    } fallback_locales[] = {
        { "pl", "pl_PL.UTF-8" },
        { "en", "en_US.UTF-8" },
        { "de", "de_DE.UTF-8" },
        { "fr", "fr_FR.UTF-8" },
        { "es", "es_ES.UTF-8" },
        { "it", "it_IT.UTF-8" },
        { "cs", "cs_CZ.UTF-8" },
        { "sk", "sk_SK.UTF-8" },
        { "uk", "uk_UA.UTF-8" },
        { "ru", "ru_RU.UTF-8" },
        { "pt", "pt_PT.UTF-8" },
        { "nl", "nl_NL.UTF-8" },
        { "sv", "sv_SE.UTF-8" },
        { NULL, NULL },
    };

    if (!canonical || !*canonical) {
        for (int i = 0; fallback_locales[i].code; i++) {
            if (g_strcmp0(lang_code, fallback_locales[i].code) == 0) {
                canonical = g_strdup(fallback_locales[i].locale);
                break;
            }
        }
    }

    if (!canonical || !*canonical) {
        canonical = g_strdup("en_US.UTF-8");
    }

    return canonical;
}

static void apply_selected_locale(KartonInstall *st) {
    g_clear_pointer(&st->selected_locale, g_free);
    st->selected_locale = find_best_locale(st->selected_language);
    if (!st->selected_locale || !*st->selected_locale) {
        st->selected_locale = g_strdup("en_US.UTF-8");
    }
    g_setenv("LANG", st->selected_locale, TRUE);
    g_setenv("LANGUAGE", st->selected_locale, TRUE);
    setlocale(LC_ALL, "");
}

static const char *language_code_to_keyboard_layout(const char *code) {
    if (!code) {
        return NULL;
    }
    struct {
        const char *lang;
        const char *layout;
    } map[] = {
        { "pl", "pl" },
        { "en", "us" },
        { "de", "de" },
        { "fr", "fr" },
        { "es", "es" },
        { "it", "it" },
        { "cs", "cz" },
        { "sk", "sk" },
        { "uk", "ua" },
        { "ru", "ru" },
        { "pt", "pt" },
        { "nl", "nl" },
        { "sv", "se" },
        { NULL, NULL },
    };
    for (int i = 0; map[i].lang; i++) {
        if (g_strcmp0(code, map[i].lang) == 0) {
            return map[i].layout;
        }
    }
    return NULL;
}

static guint find_layout_index_for_code(KartonInstall *st, const char *code) {
    if (!code || !st->keyboard_layout_codes) {
        return GTK_INVALID_LIST_POSITION;
    }
    for (guint i = 0; i < st->keyboard_layout_codes->len; i++) {
        const char *layout = g_ptr_array_index(st->keyboard_layout_codes, i);
        if (layout && g_strcmp0(layout, code) == 0) {
            return i;
        }
    }
    return GTK_INVALID_LIST_POSITION;
}

static const char *dropdown_selected_string(GtkDropDown *dropdown, GtkStringList *model) {
    guint idx = gtk_drop_down_get_selected(dropdown);
    if (idx == GTK_INVALID_LIST_POSITION) {
        return NULL;
    }
    return gtk_string_list_get_string(model, idx);
}

static void set_status(KartonInstall *st, const char *msg) {
    gtk_label_set_text(GTK_LABEL(st->status_label), msg ? msg : "");
}

static void update_password_strength(KartonInstall *st) {
    const char *pwd = gtk_editable_get_text(GTK_EDITABLE(st->password1));
    int score = 0;
    bool lower = false, upper = false, digit = false, special = false;

    for (const char *p = pwd; p && *p; p++) {
        if (g_ascii_islower(*p)) {
            lower = true;
        } else if (g_ascii_isupper(*p)) {
            upper = true;
        } else if (g_ascii_isdigit(*p)) {
            digit = true;
        } else {
            special = true;
        }
    }

    int len = (int)strlen(pwd ? pwd : "");
    if (len >= 8) {
        score++;
    }
    if (len >= 12) {
        score++;
    }
    if (lower) {
        score++;
    }
    if (upper) {
        score++;
    }
    if (digit) {
        score++;
    }
    if (special) {
        score++;
    }

    double frac = score / 6.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->password_strength), frac);
    if (frac < 0.34) {
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(st->password_strength), _("Slabe haslo"));
    } else if (frac < 0.67) {
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(st->password_strength), _("Srednie haslo"));
    } else {
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(st->password_strength), _("Mocne haslo"));
    }
}

static void update_user_autofill(KartonInstall *st) {
    const char *full = gtk_editable_get_text(GTK_EDITABLE(st->full_name));
    GString *user = g_string_new("");

    for (const char *p = full; p && *p; p++) {
        if (g_ascii_isalnum(*p)) {
            g_string_append_c(user, g_ascii_tolower(*p));
        } else if ((*p == ' ' || *p == '-' || *p == '_') && user->len > 0 && user->str[user->len - 1] != '-') {
            g_string_append_c(user, '-');
        }
    }

    while (user->len > 0 && user->str[user->len - 1] == '-') {
        g_string_truncate(user, user->len - 1);
    }
    if (user->len == 0) {
        g_string_assign(user, "karton");
    }

    gtk_editable_set_text(GTK_EDITABLE(st->user_name), user->str);
    char *host = g_strdup_printf("%s-pc", user->str);
    gtk_editable_set_text(GTK_EDITABLE(st->host_name), host);

    g_free(host);
    g_string_free(user, TRUE);
}

static void populate_keyboard_variants(KartonInstall *st) {
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->keyboard_layout));
    guint current_count = g_list_model_get_n_items(G_LIST_MODEL(st->keyboard_variant_model));
    if (current_count > 0) {
        gtk_string_list_splice(st->keyboard_variant_model, 0, current_count, NULL);
    }

    if (!st->keyboard_variant_codes) {
        st->keyboard_variant_codes = g_ptr_array_new_with_free_func(g_free);
    } else {
        for (guint i = 0; i < st->keyboard_variant_codes->len; i++) {
            g_free(g_ptr_array_index(st->keyboard_variant_codes, i));
        }
        g_ptr_array_set_size(st->keyboard_variant_codes, 0);
    }

    const char *layout_code = NULL;
    if (idx != GTK_INVALID_LIST_POSITION && st->keyboard_layout_codes && idx < st->keyboard_layout_codes->len) {
        layout_code = g_ptr_array_index(st->keyboard_layout_codes, idx);
    }

    GPtrArray *variants = list_system_keymap_variants(layout_code);
    if (variants && variants->len > 0) {
        for (guint i = 0; i < variants->len; i++) {
            const char *variant = g_ptr_array_index(variants, i);
            gtk_string_list_append(st->keyboard_variant_model, variant);
            g_ptr_array_add(st->keyboard_variant_codes, g_strdup(variant));
        }
        g_ptr_array_free(variants, TRUE);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(st->keyboard_variant), 0);
        return;
    }
    if (variants) {
        g_ptr_array_free(variants, TRUE);
        variants = NULL;
    }

    if (layout_code) {
        for (int i = 0; keyboard_layouts[i].id; i++) {
            if (g_strcmp0(keyboard_layouts[i].id, layout_code) == 0) {
                for (int j = 0; keyboard_layouts[i].variants[j]; j++) {
                    gtk_string_list_append(st->keyboard_variant_model, keyboard_layouts[i].variants[j]);
                }
                gtk_drop_down_set_selected(GTK_DROP_DOWN(st->keyboard_variant), 0);
                return;
            }
        }
    }

    gtk_string_list_append(st->keyboard_variant_model, _("Domyslny"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->keyboard_variant), 0);
}

static void populate_timezone_cities(KartonInstall *st, guint region_idx) {
    guint current_count = g_list_model_get_n_items(G_LIST_MODEL(st->timezone_city_model));
    if (current_count > 0) {
        gtk_string_list_splice(st->timezone_city_model, 0, current_count, NULL);
    }

    if (!st->timezone_list || region_idx == GTK_INVALID_LIST_POSITION) {
        gtk_string_list_append(st->timezone_city_model, "Warsaw");
        gtk_drop_down_set_selected(GTK_DROP_DOWN(st->timezone_city), 0);
        return;
    }

    const char *region = gtk_string_list_get_string(st->timezone_region_model, region_idx);
    GPtrArray *cities = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < st->timezone_list->len; i++) {
        const char *zone = g_ptr_array_index(st->timezone_list, i);
        gchar **parts = g_strsplit(zone, "/", 2);
        if (parts[0] && parts[1] && g_strcmp0(parts[0], region) == 0) {
            g_ptr_array_add(cities, g_strdup(parts[1]));
        }
        g_strfreev(parts);
    }

    if (cities->len == 0) {
        g_ptr_array_add(cities, g_strdup("Warsaw"));
    }

    g_ptr_array_sort(cities, (GCompareFunc)g_ascii_strcasecmp);
    for (guint i = 0; i < cities->len; i++) {
        gtk_string_list_append(st->timezone_city_model, g_ptr_array_index(cities, i));
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->timezone_city), 0);
    g_ptr_array_free(cities, TRUE);
}

static void populate_timezone_cities(KartonInstall *st, guint region_idx);
static void on_generic_change(GtkWidget *w, gpointer user_data);

static void try_autodetect_timezone(KartonInstall *st) {
    gchar *detected = read_first_line("/bin/bash -lc 'curl -fsS --max-time 3 https://ipapi.co/timezone 2>/dev/null'");
    if (!detected || !detected[0] || !strchr(detected, '/')) {
        g_free(detected);
        detected = read_first_line("timedatectl show -p Timezone --value 2>/dev/null");
    }

    if (!detected || !detected[0] || !strchr(detected, '/')) {
        g_free(detected);
        return;
    }

    gchar **parts = g_strsplit(detected, "/", 2);
    const char *region = parts[0] ? parts[0] : "";
    const char *city = parts[1] ? parts[1] : "";

    guint region_idx = GTK_INVALID_LIST_POSITION;
    guint region_count = g_list_model_get_n_items(G_LIST_MODEL(st->timezone_region_model));
    for (guint i = 0; i < region_count; i++) {
        const char *region_name = gtk_string_list_get_string(st->timezone_region_model, i);
        if (g_strcmp0(region_name, region) == 0) {
            region_idx = i;
            break;
        }
    }

    if (region_idx != GTK_INVALID_LIST_POSITION) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(st->timezone_region), region_idx);
        populate_timezone_cities(st, region_idx);
        guint city_count = g_list_model_get_n_items(G_LIST_MODEL(st->timezone_city_model));
        for (guint j = 0; j < city_count; j++) {
            const char *city_name = gtk_string_list_get_string(st->timezone_city_model, j);
            if (g_strcmp0(city_name, city) == 0) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(st->timezone_city), j);
                break;
            }
        }
    }

    g_strfreev(parts);
    g_free(detected);
}

static void populate_disks(KartonInstall *st) {
    guint count = g_list_model_get_n_items(G_LIST_MODEL(st->disk_model));
    if (count > 0) {
        gtk_string_list_splice(st->disk_model, 0, count, NULL);
    }
    g_ptr_array_set_size(st->disk_ids, 0);

    gchar *out = NULL;
    gchar *err = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    if (!g_spawn_command_line_sync("lsblk -dn -o PATH,SIZE,TYPE,RM", &out, &err, &exit_status, &error)
        || exit_status != 0 || !out) {
        gtk_string_list_append(st->disk_model, _("(Brak wykrytych dyskow)"));
        g_ptr_array_add(st->disk_ids, g_strdup(""));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(st->disk_dropdown), 0);
        g_clear_error(&error);
        g_free(out);
        g_free(err);
        return;
    }

    gchar **lines = g_strsplit(out, "\n", -1);
    bool have = false;
    for (int i = 0; lines[i]; i++) {
        if (!lines[i][0]) {
            continue;
        }

        gchar **parts = g_strsplit_set(lines[i], " \t", -1);
        if (!parts[0] || !parts[1] || !parts[2]) {
            g_strfreev(parts);
            continue;
        }

        const char *path = parts[0];
        const char *size = parts[1];
        const char *type = parts[2];
        const char *rm = parts[3] ? parts[3] : "0";
        if (g_strcmp0(type, "disk") != 0 || g_strcmp0(rm, "1") == 0) {
            g_strfreev(parts);
            continue;
        }

        char label[256] = {0};
        snprintf(label, sizeof(label), "%s (%s)", path, size);
        gtk_string_list_append(st->disk_model, label);
        g_ptr_array_add(st->disk_ids, g_strdup(path));
        have = true;
        g_strfreev(parts);
    }

    if (!have) {
        gtk_string_list_append(st->disk_model, _("(Brak wykrytych dyskow)"));
        g_ptr_array_add(st->disk_ids, g_strdup(""));
    }

    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->disk_dropdown), 0);

    g_strfreev(lines);
    g_free(out);
    g_free(err);
}

static void detect_nvidia_hint(KartonInstall *st) {
    gchar *out = NULL;
    gchar *err = NULL;
    gint status = 0;
    GError *error = NULL;

    bool has_nvidia = false;
    if (g_spawn_command_line_sync("/bin/bash -lc 'lspci 2>/dev/null | grep -i nvidia'", &out, &err, &status, &error)
        && status == 0 && out && out[0]) {
        has_nvidia = true;
    }

    if (has_nvidia) {
        gtk_label_set_text(GTK_LABEL(st->drivers_hint), _("Wykryto karte Nvidia. Zalecane wlaczenie sterownikow wlasnosciowych."));
        gtk_check_button_set_active(GTK_CHECK_BUTTON(st->drivers_nvidia), TRUE);
    } else {
        gtk_label_set_text(GTK_LABEL(st->drivers_hint), _("Nie wykryto Nvidia. Sterowniki wlasnosciowe pozostaja opcjonalne."));
    }

    g_clear_error(&error);
    g_free(out);
    g_free(err);
}

static gboolean language_filter_func(GtkListBoxRow *row, gpointer user_data) {
    KartonInstall *st = user_data;
    const char *query = gtk_editable_get_text(GTK_EDITABLE(st->language_search));
    const char *code = g_object_get_data(G_OBJECT(row), "lang-code");
    const char *label = g_object_get_data(G_OBJECT(row), "lang-label");

    if (!query || !query[0]) {
        return TRUE;
    }

    char *q = g_ascii_strdown(query, -1);
    char *c = g_ascii_strdown(code ? code : "", -1);
    char *l = g_ascii_strdown(label ? label : "", -1);
    bool match = strstr(c, q) != NULL || strstr(l, q) != NULL;
    g_free(q);
    g_free(c);
    g_free(l);
    return match;
}

static void language_search_changed(GtkEditable *editable, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)editable;
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(st->language_list));
}

static void update_keyboard_selection_from_language(KartonInstall *st) {
    if (!st->selected_language || !st->keyboard_layout) {
        return;
    }

    const char *layout_code = language_code_to_keyboard_layout(st->selected_language);
    if (!layout_code) {
        return;
    }

    guint idx = find_layout_index_for_code(st, layout_code);
    if (idx != GTK_INVALID_LIST_POSITION) {
        gtk_drop_down_set_selected(GTK_DROP_DOWN(st->keyboard_layout), idx);
        populate_keyboard_variants(st);
    }
}

static void language_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)box;
    if (!row) {
        return;
    }

    const char *locale = g_object_get_data(G_OBJECT(row), "lang-code");
    g_free(st->selected_locale);
    st->selected_locale = g_strdup(locale ? locale : "en_US.UTF-8");
    g_free(st->selected_language);
    st->selected_language = locale && locale[0] ? g_strndup(locale, 2) : g_strdup("en");
    apply_selected_locale(st);
    update_keyboard_selection_from_language(st);
}

static void populate_languages(KartonInstall *st) {
    GPtrArray *locales = list_system_locales();
    if (!locales || locales->len == 0) {
        locales = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(locales, g_strdup("en_US.UTF-8"));
    }

    const char *env_locale = g_getenv("LANG");
    gchar *current_locale = env_locale ? g_strdup(env_locale) : g_strdup("en_US.UTF-8");

    GtkListBoxRow *select_row = NULL;
    for (guint i = 0; i < locales->len; i++) {
        const char *locale = g_ptr_array_index(locales, i);
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(row_box, 8);
        gtk_widget_set_margin_end(row_box, 8);
        gtk_widget_set_margin_top(row_box, 8);
        gtk_widget_set_margin_bottom(row_box, 8);

        GtkWidget *label = gtk_label_new(locale);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_append(GTK_BOX(row_box), label);

        GtkWidget *row_widget = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row_widget), row_box);

        g_object_set_data_full(G_OBJECT(row_widget), "lang-code", g_strdup(locale), g_free);
        g_object_set_data_full(G_OBJECT(row_widget), "lang-label", g_strdup(locale), g_free);

        gtk_list_box_append(GTK_LIST_BOX(st->language_list), row_widget);
        if (g_strcmp0(locale, current_locale) == 0) {
            select_row = GTK_LIST_BOX_ROW(row_widget);
        }
    }

    if (!select_row) {
        select_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(st->language_list), 0);
    }

    if (select_row) {
        gtk_list_box_select_row(GTK_LIST_BOX(st->language_list), select_row);
        const char *locale = g_object_get_data(G_OBJECT(select_row), "lang-code");
        g_free(st->selected_locale);
        st->selected_locale = g_strdup(locale ? locale : "en_US.UTF-8");
        g_free(st->selected_language);
        st->selected_language = st->selected_locale && st->selected_locale[0] ? g_strndup(st->selected_locale, 2) : g_strdup("en");
        apply_selected_locale(st);
    }

    g_ptr_array_free(locales, TRUE);
    g_free(current_locale);
}


static void clear_manual_partitions(KartonInstall *st) {
    GtkWidget *child = gtk_widget_get_first_child(st->manual_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(st->manual_list), child);
        child = next;
    }
}

static int count_manual_partitions(KartonInstall *st) {
    int count = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(st->manual_list); child; child = gtk_widget_get_next_sibling(child)) {
        count++;
    }
    return count;
}

static void add_manual_partition(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    const char *size = gtk_editable_get_text(GTK_EDITABLE(st->manual_size));
    const char *mount = gtk_editable_get_text(GTK_EDITABLE(st->manual_mount));
    const char *fs = dropdown_selected_string(GTK_DROP_DOWN(st->manual_fs), st->manual_fs_model);

    if (!size || !size[0] || !mount || !mount[0] || !fs || !fs[0]) {
        set_status(st, _("Uzupelnij rozmiar, system plikow i punkt montowania."));
        return;
    }

    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(row_box, 8);
    gtk_widget_set_margin_end(row_box, 8);
    gtk_widget_set_margin_top(row_box, 4);
    gtk_widget_set_margin_bottom(row_box, 4);

    char line[256] = {0};
    snprintf(line, sizeof(line), "%s | %s | %s", size, fs, mount);
    GtkWidget *label = gtk_label_new(line);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(row_box), label);

    GtkWidget *row_widget = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row_widget), row_box);
    gtk_list_box_append(GTK_LIST_BOX(st->manual_list), row_widget);

    gtk_editable_set_text(GTK_EDITABLE(st->manual_size), "");
    gtk_editable_set_text(GTK_EDITABLE(st->manual_mount), "/");
    set_status(st, "");
}

static void remove_manual_partition(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    GtkWidget *last = gtk_widget_get_last_child(st->manual_list);
    if (!last) {
        return;
    }
    gtk_list_box_remove(GTK_LIST_BOX(st->manual_list), last);
}

static gboolean step_is_valid(KartonInstall *st, int step) {
    switch (step) {
    case STEP_LANGUAGE:
        return st->selected_language && st->selected_language[0];
    case STEP_KEYBOARD: {
        const char *layout = dropdown_selected_string(GTK_DROP_DOWN(st->keyboard_layout), st->keyboard_layout_model);
        const char *variant = dropdown_selected_string(GTK_DROP_DOWN(st->keyboard_variant), st->keyboard_variant_model);
        return layout && layout[0] && variant && variant[0];
    }
    case STEP_TIMEZONE: {
        const char *region = dropdown_selected_string(GTK_DROP_DOWN(st->timezone_region), st->timezone_region_model);
        const char *city = dropdown_selected_string(GTK_DROP_DOWN(st->timezone_city), st->timezone_city_model);
        return region && region[0] && city && city[0];
    }
    case STEP_PARTITION: {
        guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->disk_dropdown));
        if (idx == GTK_INVALID_LIST_POSITION || idx >= st->disk_ids->len) {
            return false;
        }
        const char *disk = g_ptr_array_index(st->disk_ids, idx);
        if (!disk || !g_str_has_prefix(disk, "/dev/")) {
            return false;
        }

        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(st->partition_auto))) {
            if (!gtk_switch_get_active(GTK_SWITCH(st->encrypt_switch))) {
                return true;
            }
            const char *pwd = gtk_editable_get_text(GTK_EDITABLE(st->encrypt_password));
            return pwd && strlen(pwd) >= 6;
        }

        return count_manual_partitions(st) > 0;
    }
    case STEP_USER: {
        const char *full = gtk_editable_get_text(GTK_EDITABLE(st->full_name));
        const char *user = gtk_editable_get_text(GTK_EDITABLE(st->user_name));
        const char *host = gtk_editable_get_text(GTK_EDITABLE(st->host_name));
        const char *p1 = gtk_editable_get_text(GTK_EDITABLE(st->password1));
        const char *p2 = gtk_editable_get_text(GTK_EDITABLE(st->password2));
        return full && full[0] && user && user[0] && host && host[0]
            && p1 && strlen(p1) >= 6 && g_strcmp0(p1, p2) == 0;
    }
    default:
        return true;
    }
}

static void update_summary(KartonInstall *st) {
    const char *layout = dropdown_selected_string(GTK_DROP_DOWN(st->keyboard_layout), st->keyboard_layout_model);
    const char *variant = dropdown_selected_string(GTK_DROP_DOWN(st->keyboard_variant), st->keyboard_variant_model);
    const char *region = dropdown_selected_string(GTK_DROP_DOWN(st->timezone_region), st->timezone_region_model);
    const char *city = dropdown_selected_string(GTK_DROP_DOWN(st->timezone_city), st->timezone_city_model);
    const char *disk = "(brak)";

    guint disk_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->disk_dropdown));
    if (disk_idx != GTK_INVALID_LIST_POSITION && disk_idx < st->disk_ids->len) {
        const char *candidate = g_ptr_array_index(st->disk_ids, disk_idx);
        if (candidate && candidate[0]) {
            disk = candidate;
        }
    }

    const char *profile = gtk_check_button_get_active(GTK_CHECK_BUTTON(st->profile_full)) ? _("Pelna instalacja") : _("Minimalna instalacja");
    const char *partition_mode = gtk_check_button_get_active(GTK_CHECK_BUTTON(st->partition_auto)) ? _("Automatycznie (wyczysc dysk)") : _("Recznie (zaawansowane)");
    const char *lang = st->selected_language ? st->selected_language : "en";

    gboolean enc = gtk_switch_get_active(GTK_SWITCH(st->encrypt_switch));
    int parts = count_manual_partitions(st);

    char *summary = g_strdup_printf(
        "- Jezyk: %s\n"
        "- Klawiatura: %s / %s\n"
        "- Strefa czasowa: %s/%s\n"
        "- Sterowniki wlasnosciowe: %s\n"
        "- Kodeki multimedialne: %s\n"
        "- Profil oprogramowania: %s\n"
        "- Dysk docelowy: %s\n"
        "- Partycjonowanie: %s\n"
        "- Szyfrowanie LUKS: %s\n"
        "- Reczne wpisy partycji: %d\n"
        "- Uzytkownik: %s\n"
        "- Hostname: %s\n"
        "- Auto logowanie: %s\n"
        "- To samo haslo root: %s\n\n"
        "UWAGA: Ten krok usunie dane z dysku, jesli wybrano automatyczne partycjonowanie.",
        lang,
        layout ? layout : "-",
        variant ? variant : "-",
        region ? region : "-",
        city ? city : "-",
        gtk_check_button_get_active(GTK_CHECK_BUTTON(st->drivers_nvidia)) ? _("Tak") : _("Nie"),
        gtk_check_button_get_active(GTK_CHECK_BUTTON(st->drivers_codecs)) ? _("Tak") : _("Nie"),
        profile,
        disk,
        partition_mode,
        enc ? _("Tak") : _("Nie"),
        parts,
        gtk_editable_get_text(GTK_EDITABLE(st->user_name)),
        gtk_editable_get_text(GTK_EDITABLE(st->host_name)),
        gtk_check_button_get_active(GTK_CHECK_BUTTON(st->auto_login)) ? _("Tak") : _("Nie"),
        gtk_check_button_get_active(GTK_CHECK_BUTTON(st->same_root_password)) ? _("Tak") : _("Nie"));

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st->summary_view));
    gtk_text_buffer_set_text(buffer, summary, -1);
    g_free(summary);
}

static char *resolve_install_script_path(void) {
    char *installed = g_build_filename(DATADIR, "karton-installer", "install-arch.sh", NULL);
    if (g_file_test(installed, G_FILE_TEST_EXISTS)) {
        return installed;
    }

    g_free(installed);
    return g_build_filename(g_get_current_dir(), "repo", "install-arch.sh", NULL);
}

static char *build_backend_command(KartonInstall *st) {
    char *script = resolve_install_script_path();
    guint disk_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->disk_dropdown));
    const char *disk = (disk_idx != GTK_INVALID_LIST_POSITION && disk_idx < st->disk_ids->len)
        ? g_ptr_array_index(st->disk_ids, disk_idx)
        : "";

    const char *region = dropdown_selected_string(GTK_DROP_DOWN(st->timezone_region), st->timezone_region_model);
    const char *city = dropdown_selected_string(GTK_DROP_DOWN(st->timezone_city), st->timezone_city_model);
    char *timezone = g_strdup_printf("%s/%s", region ? region : "Europe", city ? city : "Warsaw");

    const char *install_mode = gtk_check_button_get_active(GTK_CHECK_BUTTON(st->partition_auto)) ? "erase" : "manual";
    const char *profile = gtk_check_button_get_active(GTK_CHECK_BUTTON(st->profile_full)) ? "full" : "minimal";

    char *q_script = g_shell_quote(script);
    char *q_disk = g_shell_quote(disk ? disk : "");
    char *q_timezone = g_shell_quote(timezone);
    char *q_user = g_shell_quote(gtk_editable_get_text(GTK_EDITABLE(st->user_name)));
    char *q_full = g_shell_quote(gtk_editable_get_text(GTK_EDITABLE(st->full_name)));
    char *q_host = g_shell_quote(gtk_editable_get_text(GTK_EDITABLE(st->host_name)));
    char *q_lang = g_shell_quote(st->selected_locale ? st->selected_locale : "en_US.UTF-8");
    char *q_mode = g_shell_quote(install_mode);
    char *q_profile = g_shell_quote(profile);
    char *q_nvidia = g_shell_quote(gtk_check_button_get_active(GTK_CHECK_BUTTON(st->drivers_nvidia)) ? "1" : "0");
    char *q_codecs = g_shell_quote(gtk_check_button_get_active(GTK_CHECK_BUTTON(st->drivers_codecs)) ? "1" : "0");
    char *q_autologin = g_shell_quote(gtk_check_button_get_active(GTK_CHECK_BUTTON(st->auto_login)) ? "1" : "0");
    char *q_rootsame = g_shell_quote(gtk_check_button_get_active(GTK_CHECK_BUTTON(st->same_root_password)) ? "1" : "0");
    char *q_luks = g_shell_quote(gtk_switch_get_active(GTK_SWITCH(st->encrypt_switch)) ? "1" : "0");
    char *q_luks_pass = g_shell_quote(gtk_editable_get_text(GTK_EDITABLE(st->encrypt_password)));

    char *cmd = g_strdup_printf(
        "env KARTON_TARGET_DISK=%s "
        "KARTON_TIMEZONE=%s "
        "KARTON_USERNAME=%s "
        "KARTON_FULLNAME=%s "
        "KARTON_HOSTNAME=%s "
        "KARTON_LANGUAGE=%s "
        "KARTON_INSTALL_MODE=%s "
        "KARTON_SOFTWARE_PROFILE=%s "
        "KARTON_PROPRIETARY_DRIVERS=%s "
        "KARTON_MEDIA_CODECS=%s "
        "KARTON_AUTOLOGIN=%s "
        "KARTON_ROOT_SAME_PASSWORD=%s "
        "KARTON_ENABLE_LUKS=%s "
        "KARTON_LUKS_PASSWORD=%s "
        "/bin/bash %s",
        q_disk,
        q_timezone,
        q_user,
        q_full,
        q_host,
        q_lang,
        q_mode,
        q_profile,
        q_nvidia,
        q_codecs,
        q_autologin,
        q_rootsame,
        q_luks,
        q_luks_pass,
        q_script);

    g_free(script);
    g_free(timezone);
    g_free(q_script);
    g_free(q_disk);
    g_free(q_timezone);
    g_free(q_user);
    g_free(q_full);
    g_free(q_host);
    g_free(q_lang);
    g_free(q_mode);
    g_free(q_profile);
    g_free(q_nvidia);
    g_free(q_codecs);
    g_free(q_autologin);
    g_free(q_rootsame);
    g_free(q_luks);
    g_free(q_luks_pass);
    return cmd;
}

static gboolean progress_pulse(gpointer user_data) {
    KartonInstall *st = user_data;
    double frac = gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(st->progress_bar));
    if (!st->install_running) {
        return G_SOURCE_REMOVE;
    }

    if (frac < 0.92) {
        frac += 0.01;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progress_bar), frac);
    }
    return G_SOURCE_CONTINUE;
}

static gboolean slideshow_tick(gpointer user_data) {
    KartonInstall *st = user_data;
    if (st->step != STEP_PROGRESS) {
        return G_SOURCE_REMOVE;
    }

    st->slideshow_index = (st->slideshow_index + 1) % 4;
    char name[16] = {0};
    snprintf(name, sizeof(name), "slide-%u", st->slideshow_index);
    gtk_stack_set_visible_child_name(GTK_STACK(st->slideshow_stack), name);
    return G_SOURCE_CONTINUE;
}

static void append_log(KartonInstall *st, const char *line) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st->logs_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, line, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", -1);

    int pct = -1;
    for (const char *p = line; p && *p; p++) {
        if (g_ascii_isdigit(*p)) {
            pct = (int)strtol(p, NULL, 10);
            if (pct >= 0 && pct <= 100) {
                break;
            }
        }
    }

    if (pct >= 0 && pct <= 100) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progress_bar), pct / 100.0);
    }
}

static void read_install_output(KartonInstall *st);

static void read_install_output_ready(GObject *source, GAsyncResult *result, gpointer user_data) {
    KartonInstall *st = user_data;
    GError *error = NULL;
    gsize len = 0;
    gchar *line = g_data_input_stream_read_line_finish(st->install_stream, result, &len, &error);

    if (error) {
        g_clear_error(&error);
        return;
    }

    if (!line) {
        return;
    }

    append_log(st, line);
    g_free(line);
    read_install_output(st);
    (void)source;
}

static void read_install_output(KartonInstall *st) {
    if (!st->install_stream) {
        return;
    }

    g_data_input_stream_read_line_async(
        st->install_stream,
        G_PRIORITY_DEFAULT,
        NULL,
        read_install_output_ready,
        st);
}

static void update_navigation(KartonInstall *st) {
    char step_line[64] = {0};
    snprintf(step_line, sizeof(step_line), _("Krok %d z %d"), st->step + 1, STEP_COUNT);
    gtk_label_set_text(GTK_LABEL(st->step_label), step_line);
    gtk_label_set_text(GTK_LABEL(st->title_label), _(step_titles[st->step]));
    gtk_label_set_text(GTK_LABEL(st->subtitle_label), _(step_subtitles[st->step]));

    gtk_widget_set_visible(st->back_button, st->step >= STEP_KEYBOARD && st->step <= STEP_SUMMARY);
    gtk_widget_set_visible(st->next_button, st->step <= STEP_USER);
    gtk_widget_set_visible(st->install_button, st->step == STEP_SUMMARY);

    if (st->step <= STEP_USER) {
        gtk_widget_set_sensitive(st->next_button, step_is_valid(st, st->step));
    }

    if (st->step == STEP_SUMMARY) {
        gtk_widget_set_sensitive(st->install_button, step_is_valid(st, STEP_SUMMARY));
    }
}

static void set_step(KartonInstall *st, int step) {
    if (step < 0) {
        step = 0;
    }
    if (step >= STEP_COUNT) {
        step = STEP_COUNT - 1;
    }

    st->step = step;
    char page_name[16] = {0};
    snprintf(page_name, sizeof(page_name), "step-%d", step);
    gtk_stack_set_visible_child_name(GTK_STACK(st->stack), page_name);

    if (step == STEP_SUMMARY) {
        update_summary(st);
    }

    update_navigation(st);
}

static void on_generic_change(GtkWidget *w, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)w;

    if (st->step == STEP_SUMMARY) {
        update_summary(st);
    }
    update_navigation(st);
}

static void on_keyboard_layout_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)obj;
    (void)pspec;

    populate_keyboard_variants(st);
    on_generic_change(NULL, st);
}

static void on_timezone_region_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)obj;
    (void)pspec;

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(st->timezone_region));
    populate_timezone_cities(st, idx);
    on_generic_change(NULL, st);
}

static void on_partition_mode_toggled(GtkCheckButton *btn, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)btn;

    if (gtk_check_button_get_active(GTK_CHECK_BUTTON(st->partition_auto))) {
        gtk_stack_set_visible_child_name(GTK_STACK(st->partition_stack), "auto");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(st->partition_stack), "manual");
    }
    on_generic_change(NULL, st);
}

static void on_encrypt_switch_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)pspec;

    gtk_widget_set_visible(st->encrypt_box, gtk_switch_get_active(sw));
    on_generic_change(NULL, st);
}

static void on_full_name_changed(GtkEditable *editable, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)editable;

    update_user_autofill(st);
    on_generic_change(NULL, st);
}

static void on_password_changed(GtkEditable *editable, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)editable;

    update_password_strength(st);
    on_generic_change(NULL, st);
}

static void on_show_password_toggled(GtkCheckButton *btn, gpointer user_data) {
    KartonInstall *st = user_data;
    gboolean active = gtk_check_button_get_active(btn);

    gtk_entry_set_visibility(GTK_ENTRY(st->password1), active);
    gtk_entry_set_visibility(GTK_ENTRY(st->password2), active);
}

static void on_back_clicked(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;
    set_step(st, st->step - 1);
}

static void on_next_clicked(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    if (!step_is_valid(st, st->step)) {
        set_status(st, _("Uzupelnij wymagane pola zanim przejdziesz dalej."));
        return;
    }

    set_status(st, "");
    set_step(st, st->step + 1);
}

static void on_install_done(GObject *source, GAsyncResult *result, gpointer user_data) {
    KartonInstall *st = user_data;
    GError *error = NULL;

    gboolean ok = g_subprocess_wait_finish(st->install_proc, result, &error);
    st->install_running = FALSE;

    if (st->progress_timer_id) {
        g_source_remove(st->progress_timer_id);
        st->progress_timer_id = 0;
    }

    if (!ok || error || !g_subprocess_get_successful(st->install_proc)) {
        gtk_label_set_text(GTK_LABEL(st->progress_status), _("Instalacja zakonczona bledem. Sprawdz logi."));
        set_status(st, error && error->message ? error->message : _("Backend instalacji zwrocil blad."));
        g_clear_error(&error);
        return;
    }

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progress_bar), 1.0);
    gtk_label_set_text(GTK_LABEL(st->progress_status), _("Instalacja zakonczona sukcesem."));
    set_status(st, "");
    set_step(st, STEP_FINISHED);

    (void)source;
}

static void start_installation(KartonInstall *st) {
    if (st->install_running) {
        return;
    }

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(st->logs_view));
    gtk_text_buffer_set_text(buf, "", -1);

    set_step(st, STEP_PROGRESS);
    gtk_label_set_text(GTK_LABEL(st->progress_status), _("Przygotowanie instalacji..."));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progress_bar), 0.02);

    char *cmd = build_backend_command(st);
    GError *error = NULL;
    st->install_proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error,
        "/bin/bash",
        "-lc",
        cmd,
        NULL);
    g_free(cmd);

    if (!st->install_proc) {
        set_status(st, error && error->message ? error->message : _("Nie mozna uruchomic backendu instalacji."));
        gtk_label_set_text(GTK_LABEL(st->progress_status), _("Nie udalo sie uruchomic procesu instalacji."));
        g_clear_error(&error);
        return;
    }

    st->install_running = TRUE;
    GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(st->install_proc);
    st->install_stream = g_data_input_stream_new(stdout_stream);
    read_install_output(st);

    if (st->progress_timer_id) {
        g_source_remove(st->progress_timer_id);
    }
    st->progress_timer_id = g_timeout_add_seconds(1, progress_pulse, st);

    if (st->slideshow_timer_id) {
        g_source_remove(st->slideshow_timer_id);
    }
    st->slideshow_timer_id = g_timeout_add_seconds(8, slideshow_tick, st);

    g_subprocess_wait_async(st->install_proc, NULL, on_install_done, st);
}

static void on_confirm_install_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    KartonInstall *st = user_data;
    GError *error = NULL;
    int response = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (error) {
        g_clear_error(&error);
        return;
    }

    if (response == 1) {
        start_installation(st);
    }
}

static void on_install_clicked(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    if (!step_is_valid(st, STEP_SUMMARY)) {
        set_status(st, _("Podsumowanie zawiera niepelne dane."));
        return;
    }

    GtkAlertDialog *dialog = gtk_alert_dialog_new(_("Czy na pewno? Tej operacji nie da sie cofnac."));
    const char *buttons[] = { _("Anuluj"), _("Potwierdzam"), NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(st->window), NULL, on_confirm_install_response, st);
}

static void on_show_logs_clicked(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    gboolean reveal = !gtk_revealer_get_reveal_child(GTK_REVEALER(st->logs_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->logs_revealer), reveal);
    gtk_button_set_label(GTK_BUTTON(st->show_logs_button), reveal ? _("Ukryj logi") : _("Pokaz logi"));
}

static void on_finish_continue_clicked(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    gtk_window_close(GTK_WINDOW(st->window));
}

static void on_finish_reboot_clicked(GtkButton *button, gpointer user_data) {
    KartonInstall *st = user_data;
    (void)button;

    GError *error = NULL;
    if (!g_spawn_command_line_async("/bin/bash -lc 'systemctl reboot || reboot'", &error)) {
        set_status(st, error && error->message ? error->message : _("Nie mozna uruchomic restartu."));
        g_clear_error(&error);
        return;
    }

    gtk_window_close(GTK_WINDOW(st->window));
}

static GtkWidget *build_language_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    GtkWidget *logo = gtk_image_new_from_icon_name("io.karton.Install");
    gtk_widget_set_size_request(logo, 96, 96);
    gtk_box_append(GTK_BOX(box), logo);

    st->language_search = gtk_search_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(st->language_search), "");
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(st->language_search), _("Szukaj jezyka..."));
    gtk_box_append(GTK_BOX(box), st->language_search);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);

    st->language_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->language_list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(st->language_list), language_filter_func, st, NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), st->language_list);

    populate_languages(st);

    g_signal_connect(st->language_search, "changed", G_CALLBACK(language_search_changed), st);
    g_signal_connect(st->language_list, "row-selected", G_CALLBACK(language_row_selected), st);

    return box;
}

static GtkWidget *build_keyboard_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(box), grid);

    GtkWidget *label1 = gtk_label_new(_("Uklad klawiatury"));
    gtk_label_set_xalign(GTK_LABEL(label1), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), label1, 0, 0, 1, 1);

    GtkWidget *label2 = gtk_label_new(_("Wariant"));
    gtk_label_set_xalign(GTK_LABEL(label2), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), label2, 1, 0, 1, 1);

    st->keyboard_layout_model = gtk_string_list_new(NULL);
    if (st->keyboard_layout_codes) {
        g_ptr_array_unref(st->keyboard_layout_codes);
    }
    st->keyboard_layout_codes = g_ptr_array_new_with_free_func(g_free);

    GPtrArray *layouts = list_system_keymap_layouts();
    if (layouts && layouts->len > 0) {
        for (guint i = 0; i < layouts->len; i++) {
            const char *layout = g_ptr_array_index(layouts, i);
            gtk_string_list_append(st->keyboard_layout_model, layout);
            g_ptr_array_add(st->keyboard_layout_codes, g_strdup(layout));
        }
        g_ptr_array_free(layouts, TRUE);
    } else {
        if (layouts) {
            g_ptr_array_free(layouts, TRUE);
        }
        for (int i = 0; keyboard_layouts[i].id; i++) {
            gtk_string_list_append(st->keyboard_layout_model, keyboard_layouts[i].label);
            g_ptr_array_add(st->keyboard_layout_codes, g_strdup(keyboard_layouts[i].id));
        }
    }

    st->keyboard_layout = gtk_drop_down_new(G_LIST_MODEL(st->keyboard_layout_model), NULL);
    gtk_grid_attach(GTK_GRID(grid), st->keyboard_layout, 0, 1, 1, 1);

    st->keyboard_variant_model = gtk_string_list_new(NULL);
    if (st->keyboard_variant_codes) {
        g_ptr_array_unref(st->keyboard_variant_codes);
    }
    st->keyboard_variant_codes = g_ptr_array_new_with_free_func(g_free);
    st->keyboard_variant = gtk_drop_down_new(G_LIST_MODEL(st->keyboard_variant_model), NULL);
    gtk_grid_attach(GTK_GRID(grid), st->keyboard_variant, 1, 1, 1, 1);

    guint default_layout_idx = GTK_INVALID_LIST_POSITION;
    if (st->selected_language) {
        const char *layout_code = language_code_to_keyboard_layout(st->selected_language);
        if (layout_code) {
            default_layout_idx = find_layout_index_for_code(st, layout_code);
        }
    }
    if (default_layout_idx == GTK_INVALID_LIST_POSITION) {
        default_layout_idx = 0;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->keyboard_layout), default_layout_idx);
    populate_keyboard_variants(st);

    GtkWidget *test_label = gtk_label_new(_("Kliknij tutaj i wpisz cos, aby przetestowac klawiature"));
    gtk_label_set_xalign(GTK_LABEL(test_label), 0.0f);
    gtk_box_append(GTK_BOX(box), test_label);

    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_vexpand(frame, TRUE);
    gtk_box_append(GTK_BOX(box), frame);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scroll, -1, 180);
    gtk_frame_set_child(GTK_FRAME(frame), scroll);
    st->keyboard_test = gtk_text_view_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), st->keyboard_test);

    g_signal_connect(st->keyboard_layout, "notify::selected", G_CALLBACK(on_keyboard_layout_changed), st);
    g_signal_connect(st->keyboard_variant, "notify::selected", G_CALLBACK(on_generic_change), st);

    return box;
}

static GtkWidget *build_timezone_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_box_append(GTK_BOX(box), grid);

    GtkWidget *r_label = gtk_label_new(_("Region"));
    gtk_label_set_xalign(GTK_LABEL(r_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), r_label, 0, 0, 1, 1);

    GtkWidget *c_label = gtk_label_new(_("Miasto"));
    gtk_label_set_xalign(GTK_LABEL(c_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), c_label, 1, 0, 1, 1);

    st->timezone_region_model = gtk_string_list_new(NULL);
    if (st->timezone_region_codes) {
        g_ptr_array_unref(st->timezone_region_codes);
    }
    st->timezone_region_codes = g_ptr_array_new_with_free_func(g_free);

    if (st->timezone_list) {
        g_ptr_array_unref(st->timezone_list);
    }
    st->timezone_list = list_system_timezones();
    if (!st->timezone_list) {
        st->timezone_list = g_ptr_array_new_with_free_func(g_free);
        g_ptr_array_add(st->timezone_list, g_strdup("Europe/Warsaw"));
    }

    GPtrArray *regions = unique_timezone_regions(st->timezone_list);
    if (!regions || regions->len == 0) {
        if (!regions) {
            regions = g_ptr_array_new_with_free_func(g_free);
        }
        g_ptr_array_add(regions, g_strdup("Europe"));
    }
    for (guint i = 0; regions && i < regions->len; i++) {
        const char *region = g_ptr_array_index(regions, i);
        gtk_string_list_append(st->timezone_region_model, region);
        g_ptr_array_add(st->timezone_region_codes, g_strdup(region));
    }
    if (regions) {
        g_ptr_array_free(regions, TRUE);
    }

    st->timezone_region = gtk_drop_down_new(G_LIST_MODEL(st->timezone_region_model), NULL);
    gtk_grid_attach(GTK_GRID(grid), st->timezone_region, 0, 1, 1, 1);

    st->timezone_city_model = gtk_string_list_new(NULL);
    st->timezone_city = gtk_drop_down_new(G_LIST_MODEL(st->timezone_city_model), NULL);
    gtk_grid_attach(GTK_GRID(grid), st->timezone_city, 1, 1, 1, 1);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->timezone_region), 0);
    populate_timezone_cities(st, 0);
    try_autodetect_timezone(st);

    g_signal_connect(st->timezone_region, "notify::selected", G_CALLBACK(on_timezone_region_changed), st);
    g_signal_connect(st->timezone_city, "notify::selected", G_CALLBACK(on_generic_change), st);

    return box;
}

static GtkWidget *build_software_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    st->drivers_nvidia = gtk_check_button_new_with_label(_("Zainstaluj sterowniki wlasnosciowe (Nvidia/Wi-Fi)"));
    gtk_box_append(GTK_BOX(box), st->drivers_nvidia);

    st->drivers_codecs = gtk_check_button_new_with_label(_("Zainstaluj kodeki multimedialne (MP4/AAC itd.)"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(st->drivers_codecs), TRUE);
    gtk_box_append(GTK_BOX(box), st->drivers_codecs);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget *profile_title = gtk_label_new(_("Wybierz profil oprogramowania"));
    gtk_label_set_xalign(GTK_LABEL(profile_title), 0.0f);
    gtk_box_append(GTK_BOX(box), profile_title);

    st->profile_full = gtk_check_button_new_with_label(_("Instalacja pelna (srodowisko + aplikacje biurowe)"));
    st->profile_minimal = gtk_check_button_new_with_label(_("Instalacja minimalna (pulpit + przegladarka)"));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(st->profile_minimal), GTK_CHECK_BUTTON(st->profile_full));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(st->profile_full), TRUE);
    gtk_box_append(GTK_BOX(box), st->profile_full);
    gtk_box_append(GTK_BOX(box), st->profile_minimal);

    st->drivers_hint = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(st->drivers_hint), TRUE);
    gtk_label_set_xalign(GTK_LABEL(st->drivers_hint), 0.0f);
    gtk_box_append(GTK_BOX(box), st->drivers_hint);

    detect_nvidia_hint(st);

    g_signal_connect(st->drivers_nvidia, "toggled", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->drivers_codecs, "toggled", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->profile_full, "toggled", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->profile_minimal, "toggled", G_CALLBACK(on_generic_change), st);

    return box;
}

static GtkWidget *build_partition_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    GtkWidget *disk_label = gtk_label_new(_("Dysk docelowy"));
    gtk_label_set_xalign(GTK_LABEL(disk_label), 0.0f);
    gtk_box_append(GTK_BOX(box), disk_label);

    st->disk_model = gtk_string_list_new(NULL);
    st->disk_ids = g_ptr_array_new_with_free_func(g_free);
    st->disk_dropdown = gtk_drop_down_new(G_LIST_MODEL(st->disk_model), NULL);
    gtk_box_append(GTK_BOX(box), st->disk_dropdown);
    populate_disks(st);

    GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(box), modes);

    st->partition_auto = gtk_check_button_new_with_label(_("Automatycznie: wyczysc dysk i zainstaluj"));
    st->partition_manual = gtk_check_button_new_with_label(_("Recznie: zaawansowane partycjonowanie"));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(st->partition_manual), GTK_CHECK_BUTTON(st->partition_auto));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(st->partition_auto), TRUE);
    gtk_box_append(GTK_BOX(modes), st->partition_auto);
    gtk_box_append(GTK_BOX(modes), st->partition_manual);

    st->partition_stack = gtk_stack_new();
    gtk_box_append(GTK_BOX(box), st->partition_stack);

    GtkWidget *auto_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *enc_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *enc_label = gtk_label_new(_("Szyfruj system (LUKS)"));
    gtk_label_set_xalign(GTK_LABEL(enc_label), 0.0f);
    st->encrypt_switch = gtk_switch_new();
    gtk_box_append(GTK_BOX(enc_row), enc_label);
    gtk_box_append(GTK_BOX(enc_row), st->encrypt_switch);
    gtk_box_append(GTK_BOX(auto_box), enc_row);

    st->encrypt_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *enc_pwd_label = gtk_label_new(_("Haslo szyfrowania"));
    gtk_label_set_xalign(GTK_LABEL(enc_pwd_label), 0.0f);
    gtk_box_append(GTK_BOX(st->encrypt_box), enc_pwd_label);
    st->encrypt_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(st->encrypt_password), FALSE);
    gtk_box_append(GTK_BOX(st->encrypt_box), st->encrypt_password);
    gtk_widget_set_visible(st->encrypt_box, FALSE);
    gtk_box_append(GTK_BOX(auto_box), st->encrypt_box);

    GtkWidget *manual_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *manual_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(manual_grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(manual_grid), 8);
    gtk_box_append(GTK_BOX(manual_box), manual_grid);

    GtkWidget *size_label = gtk_label_new(_("Rozmiar"));
    gtk_label_set_xalign(GTK_LABEL(size_label), 0.0f);
    gtk_grid_attach(GTK_GRID(manual_grid), size_label, 0, 0, 1, 1);
    st->manual_size = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->manual_size), "80G");
    gtk_grid_attach(GTK_GRID(manual_grid), st->manual_size, 0, 1, 1, 1);

    GtkWidget *fs_label = gtk_label_new(_("System plikow"));
    gtk_label_set_xalign(GTK_LABEL(fs_label), 0.0f);
    gtk_grid_attach(GTK_GRID(manual_grid), fs_label, 1, 0, 1, 1);
    st->manual_fs_model = gtk_string_list_new(NULL);
    gtk_string_list_append(st->manual_fs_model, "EXT4");
    gtk_string_list_append(st->manual_fs_model, "Btrfs");
    gtk_string_list_append(st->manual_fs_model, "FAT32");
    gtk_string_list_append(st->manual_fs_model, "swap");
    st->manual_fs = gtk_drop_down_new(G_LIST_MODEL(st->manual_fs_model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->manual_fs), 0);
    gtk_grid_attach(GTK_GRID(manual_grid), st->manual_fs, 1, 1, 1, 1);

    GtkWidget *mount_label = gtk_label_new(_("Punkt montowania"));
    gtk_label_set_xalign(GTK_LABEL(mount_label), 0.0f);
    gtk_grid_attach(GTK_GRID(manual_grid), mount_label, 2, 0, 1, 1);
    st->manual_mount = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->manual_mount), "/");
    gtk_grid_attach(GTK_GRID(manual_grid), st->manual_mount, 2, 1, 1, 1);

    GtkWidget *manual_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(manual_box), manual_actions);
    GtkWidget *add_btn = gtk_button_new_with_label(_("Dodaj"));
    GtkWidget *remove_btn = gtk_button_new_with_label(_("Usun ostatni"));
    gtk_box_append(GTK_BOX(manual_actions), add_btn);
    gtk_box_append(GTK_BOX(manual_actions), remove_btn);

    GtkWidget *manual_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(manual_scroll, -1, 180);
    gtk_widget_set_vexpand(manual_scroll, TRUE);
    gtk_box_append(GTK_BOX(manual_box), manual_scroll);

    st->manual_list = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(manual_scroll), st->manual_list);
    clear_manual_partitions(st);

    gtk_stack_add_named(GTK_STACK(st->partition_stack), auto_box, "auto");
    gtk_stack_add_named(GTK_STACK(st->partition_stack), manual_box, "manual");
    gtk_stack_set_visible_child_name(GTK_STACK(st->partition_stack), "auto");

    g_signal_connect(st->disk_dropdown, "notify::selected", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->partition_auto, "toggled", G_CALLBACK(on_partition_mode_toggled), st);
    g_signal_connect(st->partition_manual, "toggled", G_CALLBACK(on_partition_mode_toggled), st);
    g_signal_connect(st->encrypt_switch, "notify::active", G_CALLBACK(on_encrypt_switch_changed), st);
    g_signal_connect(st->encrypt_password, "changed", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->manual_size, "changed", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->manual_mount, "changed", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->manual_fs, "notify::selected", G_CALLBACK(on_generic_change), st);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(add_manual_partition), st);
    g_signal_connect(remove_btn, "clicked", G_CALLBACK(remove_manual_partition), st);

    return box;
}

static GtkWidget *build_user_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_box_append(GTK_BOX(box), grid);

    GtkWidget *full_label = gtk_label_new(_("Twoje imie i nazwisko"));
    gtk_label_set_xalign(GTK_LABEL(full_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), full_label, 0, 0, 1, 1);
    st->full_name = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(st->full_name), "Karton User");
    gtk_grid_attach(GTK_GRID(grid), st->full_name, 1, 0, 1, 1);

    GtkWidget *user_label = gtk_label_new(_("Nazwa uzytkownika"));
    gtk_label_set_xalign(GTK_LABEL(user_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), user_label, 0, 1, 1, 1);
    st->user_name = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), st->user_name, 1, 1, 1, 1);

    GtkWidget *host_label = gtk_label_new(_("Nazwa komputera (hostname)"));
    gtk_label_set_xalign(GTK_LABEL(host_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), host_label, 0, 2, 1, 1);
    st->host_name = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), st->host_name, 1, 2, 1, 1);

    GtkWidget *p1_label = gtk_label_new(_("Wpisz haslo"));
    gtk_label_set_xalign(GTK_LABEL(p1_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), p1_label, 0, 3, 1, 1);
    st->password1 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(st->password1), FALSE);
    gtk_grid_attach(GTK_GRID(grid), st->password1, 1, 3, 1, 1);

    GtkWidget *p2_label = gtk_label_new(_("Powtorz haslo"));
    gtk_label_set_xalign(GTK_LABEL(p2_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), p2_label, 0, 4, 1, 1);
    st->password2 = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(st->password2), FALSE);
    gtk_grid_attach(GTK_GRID(grid), st->password2, 1, 4, 1, 1);

    st->show_password = gtk_check_button_new_with_label(_("Pokaz haslo"));
    gtk_grid_attach(GTK_GRID(grid), st->show_password, 1, 5, 1, 1);

    st->password_strength = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(st->password_strength), TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->password_strength), 0.0);
    gtk_grid_attach(GTK_GRID(grid), st->password_strength, 1, 6, 1, 1);

    st->auto_login = gtk_check_button_new_with_label(_("Loguj automatycznie bez pytania o haslo"));
    gtk_box_append(GTK_BOX(box), st->auto_login);

    st->same_root_password = gtk_check_button_new_with_label(_("Uzyj tego samego hasla dla konta administratora (root)"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(st->same_root_password), TRUE);
    gtk_box_append(GTK_BOX(box), st->same_root_password);

    update_user_autofill(st);
    update_password_strength(st);

    g_signal_connect(st->full_name, "changed", G_CALLBACK(on_full_name_changed), st);
    g_signal_connect(st->user_name, "changed", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->host_name, "changed", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->password1, "changed", G_CALLBACK(on_password_changed), st);
    g_signal_connect(st->password2, "changed", G_CALLBACK(on_password_changed), st);
    g_signal_connect(st->show_password, "toggled", G_CALLBACK(on_show_password_toggled), st);
    g_signal_connect(st->auto_login, "toggled", G_CALLBACK(on_generic_change), st);
    g_signal_connect(st->same_root_password, "toggled", G_CALLBACK(on_generic_change), st);

    return box;
}

static GtkWidget *build_summary_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    GtkWidget *warning = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warning), "<span foreground='#b00020' weight='bold'>UWAGA: To ostatni moment na zmiany przed modyfikacja dysku.</span>");
    gtk_label_set_wrap(GTK_LABEL(warning), TRUE);
    gtk_label_set_xalign(GTK_LABEL(warning), 0.0f);
    gtk_box_append(GTK_BOX(box), warning);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(box), scroll);

    st->summary_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st->summary_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(st->summary_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(st->summary_view), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), st->summary_view);

    return box;
}

static GtkWidget *build_progress_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    st->progress_status = gtk_label_new(_("Oczekiwanie na start instalacji..."));
    gtk_label_set_xalign(GTK_LABEL(st->progress_status), 0.0f);
    gtk_box_append(GTK_BOX(box), st->progress_status);

    st->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(st->progress_bar), TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progress_bar), 0.0);
    gtk_box_append(GTK_BOX(box), st->progress_bar);

    st->slideshow_stack = gtk_stack_new();
    gtk_widget_set_vexpand(st->slideshow_stack, TRUE);
    gtk_box_append(GTK_BOX(box), st->slideshow_stack);

    GtkWidget *slide0 = gtk_label_new(_("Witaj w KartON. Instalator przygotowuje nowy system."));
    gtk_label_set_wrap(GTK_LABEL(slide0), TRUE);
    gtk_stack_add_named(GTK_STACK(st->slideshow_stack), slide0, "slide-0");

    GtkWidget *slide1 = gtk_label_new(_("Skrot: Super + Enter uruchamia terminal po instalacji."));
    gtk_label_set_wrap(GTK_LABEL(slide1), TRUE);
    gtk_stack_add_named(GTK_STACK(st->slideshow_stack), slide1, "slide-1");

    GtkWidget *slide2 = gtk_label_new(_("Aplikacje i ustawienia znajdziesz w menu KartON."));
    gtk_label_set_wrap(GTK_LABEL(slide2), TRUE);
    gtk_stack_add_named(GTK_STACK(st->slideshow_stack), slide2, "slide-2");

    GtkWidget *slide3 = gtk_label_new(_("Po instalacji uruchom Karton Welcome, aby dokonczyc konfiguracje."));
    gtk_label_set_wrap(GTK_LABEL(slide3), TRUE);
    gtk_stack_add_named(GTK_STACK(st->slideshow_stack), slide3, "slide-3");

    st->show_logs_button = gtk_button_new_with_label(_("Pokaz logi"));
    gtk_box_append(GTK_BOX(box), st->show_logs_button);

    st->logs_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(st->logs_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_reveal_child(GTK_REVEALER(st->logs_revealer), FALSE);
    gtk_box_append(GTK_BOX(box), st->logs_revealer);

    GtkWidget *log_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(log_scroll, -1, 180);
    gtk_revealer_set_child(GTK_REVEALER(st->logs_revealer), log_scroll);

    st->logs_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st->logs_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(st->logs_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(st->logs_view), GTK_WRAP_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(log_scroll), st->logs_view);

    g_signal_connect(st->show_logs_button, "clicked", G_CALLBACK(on_show_logs_clicked), st);
    return box;
}

static GtkWidget *build_finished_step(KartonInstall *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);

    GtkWidget *icon = gtk_image_new_from_icon_name("emblem-ok-symbolic");
    gtk_widget_set_size_request(icon, 96, 96);
    gtk_box_append(GTK_BOX(box), icon);

    st->finish_title = gtk_label_new(_("Instalacja zakonczona sukcesem!"));
    gtk_widget_add_css_class(st->finish_title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(st->finish_title), 0.0f);
    gtk_box_append(GTK_BOX(box), st->finish_title);

    st->finish_subtitle = gtk_label_new(_("System jest gotowy do uzycia."));
    gtk_label_set_xalign(GTK_LABEL(st->finish_subtitle), 0.0f);
    gtk_box_append(GTK_BOX(box), st->finish_subtitle);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), actions);

    GtkWidget *continue_btn = gtk_button_new_with_label(_("Kontynuuj testowanie"));
    GtkWidget *reboot_btn = gtk_button_new_with_label(_("Uruchom ponownie teraz"));
    gtk_widget_add_css_class(reboot_btn, "suggested-action");
    gtk_box_append(GTK_BOX(actions), continue_btn);
    gtk_box_append(GTK_BOX(actions), reboot_btn);

    g_signal_connect(continue_btn, "clicked", G_CALLBACK(on_finish_continue_clicked), st);
    g_signal_connect(reboot_btn, "clicked", G_CALLBACK(on_finish_reboot_clicked), st);

    return box;
}

static void activate(GtkApplication *app, gpointer user_data) {
    KartonInstall *st = user_data;

    st->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(st->window), _("Karton Install"));
    gtk_window_set_default_size(GTK_WINDOW(st->window), 1000, 700);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(root, 20);
    gtk_widget_set_margin_end(root, 20);
    gtk_widget_set_margin_top(root, 20);
    gtk_widget_set_margin_bottom(root, 20);
    gtk_window_set_child(GTK_WINDOW(st->window), root);

    st->step_label = gtk_label_new(_("Krok 1 z 9"));
    gtk_label_set_xalign(GTK_LABEL(st->step_label), 0.0f);
    gtk_box_append(GTK_BOX(root), st->step_label);

    st->title_label = gtk_label_new(_("Wybierz jezyk"));
    gtk_widget_add_css_class(st->title_label, "title-2");
    gtk_label_set_xalign(GTK_LABEL(st->title_label), 0.0f);
    gtk_box_append(GTK_BOX(root), st->title_label);

    st->subtitle_label = gtk_label_new(_("Wybierz jezyk instalatora i systemu."));
    gtk_label_set_xalign(GTK_LABEL(st->subtitle_label), 0.0f);
    gtk_box_append(GTK_BOX(root), st->subtitle_label);

    st->stack = gtk_stack_new();
    gtk_widget_set_vexpand(st->stack, TRUE);
    gtk_box_append(GTK_BOX(root), st->stack);

    gtk_stack_add_named(GTK_STACK(st->stack), build_language_step(st), "step-0");
    gtk_stack_add_named(GTK_STACK(st->stack), build_keyboard_step(st), "step-1");
    gtk_stack_add_named(GTK_STACK(st->stack), build_timezone_step(st), "step-2");
    gtk_stack_add_named(GTK_STACK(st->stack), build_software_step(st), "step-3");
    gtk_stack_add_named(GTK_STACK(st->stack), build_partition_step(st), "step-4");
    gtk_stack_add_named(GTK_STACK(st->stack), build_user_step(st), "step-5");
    gtk_stack_add_named(GTK_STACK(st->stack), build_summary_step(st), "step-6");
    gtk_stack_add_named(GTK_STACK(st->stack), build_progress_step(st), "step-7");
    gtk_stack_add_named(GTK_STACK(st->stack), build_finished_step(st), "step-8");

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(root), bottom);

    st->back_button = gtk_button_new_with_label(_("Cofnij"));
    st->next_button = gtk_button_new_with_label(_("Dalej"));
    st->install_button = gtk_button_new_with_label(_("Zainstaluj system"));
    gtk_widget_add_css_class(st->install_button, "suggested-action");

    gtk_box_append(GTK_BOX(bottom), st->back_button);
    gtk_box_append(GTK_BOX(bottom), st->next_button);
    gtk_box_append(GTK_BOX(bottom), st->install_button);

    st->status_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st->status_label), 0.0f);
    gtk_box_append(GTK_BOX(root), st->status_label);

    g_signal_connect(st->back_button, "clicked", G_CALLBACK(on_back_clicked), st);
    g_signal_connect(st->next_button, "clicked", G_CALLBACK(on_next_clicked), st);
    g_signal_connect(st->install_button, "clicked", G_CALLBACK(on_install_clicked), st);

    set_step(st, STEP_LANGUAGE);
    gtk_window_present(GTK_WINDOW(st->window));
}

int main(int argc, char **argv) {
    KartonInstall st = {0};

    setlocale(LC_ALL, "");
    bindtextdomain("karton-installer", LOCALEDIR);
    bind_textdomain_codeset("karton-installer", "UTF-8");
    textdomain("karton-installer");

    st.app = GTK_APPLICATION(gtk_application_new("io.karton.Install", G_APPLICATION_NON_UNIQUE));
    g_signal_connect(st.app, "activate", G_CALLBACK(activate), &st);

    int status = g_application_run(G_APPLICATION(st.app), argc, argv);

    g_clear_object(&st.install_stream);
    g_clear_object(&st.install_proc);
    g_clear_pointer(&st.selected_language, g_free);
    if (st.disk_ids) {
        g_ptr_array_free(st.disk_ids, TRUE);
    }
    g_clear_object(&st.app);

    return status;
}
