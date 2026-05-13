#include "page-region.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define _(s) gettext(s)
#define N_(s) s

typedef struct {
    GtkStringList *labels;
    GPtrArray *values;
} ChoiceList;

struct option_value {
    const char *label;
    const char *value;
};

static const struct option_value g_date_format_options[] = {
    { N_("ISO (2026-05-10)"), "iso" },
    { N_("European (10.05.2026)"), "eu" },
    { N_("US (05/10/2026)"), "us" },
};

static const struct option_value g_time_format_options[] = {
    { N_("24-hour"), "24h" },
    { N_("12-hour"), "12h" },
};

static const struct option_value g_units_options[] = {
    { N_("Metric"), "metric" },
    { N_("Imperial"), "imperial" },
};

struct code_name_entry {
    const char *code;
    const char *name;
};

static const struct code_name_entry g_language_name_map[] = {
    { "ar", "Arabic" },
    { "bg", "Bulgarian" },
    { "cs", "Czech" },
    { "da", "Danish" },
    { "de", "German" },
    { "el", "Greek" },
    { "en", "English" },
    { "es", "Spanish" },
    { "et", "Estonian" },
    { "fi", "Finnish" },
    { "fr", "French" },
    { "hr", "Croatian" },
    { "hu", "Hungarian" },
    { "it", "Italian" },
    { "ja", "Japanese" },
    { "ko", "Korean" },
    { "lt", "Lithuanian" },
    { "lv", "Latvian" },
    { "nl", "Dutch" },
    { "no", "Norwegian" },
    { "pl", "Polish" },
    { "pt", "Portuguese" },
    { "ro", "Romanian" },
    { "ru", "Russian" },
    { "sk", "Slovak" },
    { "sl", "Slovenian" },
    { "sr", "Serbian" },
    { "sv", "Swedish" },
    { "tr", "Turkish" },
    { "uk", "Ukrainian" },
    { "zh", "Chinese" },
};

static const struct code_name_entry g_country_name_map[] = {
    { "AR", "Argentina" },
    { "AT", "Austria" },
    { "AU", "Australia" },
    { "BE", "Belgium" },
    { "BG", "Bulgaria" },
    { "BR", "Brazil" },
    { "CA", "Canada" },
    { "CH", "Switzerland" },
    { "CN", "China" },
    { "CZ", "Czech Republic" },
    { "DE", "Germany" },
    { "DK", "Denmark" },
    { "EE", "Estonia" },
    { "EG", "Egypt" },
    { "ES", "Spain" },
    { "FI", "Finland" },
    { "FR", "France" },
    { "GB", "United Kingdom" },
    { "GR", "Greece" },
    { "HR", "Croatia" },
    { "HU", "Hungary" },
    { "IE", "Ireland" },
    { "IT", "Italy" },
    { "JP", "Japan" },
    { "KR", "South Korea" },
    { "LT", "Lithuania" },
    { "LV", "Latvia" },
    { "MX", "Mexico" },
    { "NL", "Netherlands" },
    { "NO", "Norway" },
    { "NZ", "New Zealand" },
    { "PL", "Poland" },
    { "PT", "Portugal" },
    { "RO", "Romania" },
    { "RS", "Serbia" },
    { "RU", "Russia" },
    { "SE", "Sweden" },
    { "SI", "Slovenia" },
    { "SK", "Slovakia" },
    { "TR", "Turkey" },
    { "UA", "Ukraine" },
    { "US", "United States" },
};

static GtkWidget *g_timezone_dropdown = NULL;
static GtkWidget *g_date_format_dropdown = NULL;
static GtkWidget *g_time_format_dropdown = NULL;
static GtkWidget *g_language_dropdown = NULL;
static GtkWidget *g_units_dropdown = NULL;
static GtkWidget *g_keyboard_layout_dropdown = NULL;
static GtkWidget *g_regionalization_dropdown = NULL;

static GtkWidget *g_reload_btn = NULL;
static GtkWidget *g_apply_btn = NULL;

static GtkWidget *g_loading_box = NULL;
static GtkWidget *g_loading_spinner = NULL;
static GtkWidget *g_loading_label = NULL;
static GtkWidget *g_status_label = NULL;

static ChoiceList g_timezone_choices = { 0 };
static ChoiceList g_language_choices = { 0 };
static ChoiceList g_keyboard_choices = { 0 };
static ChoiceList g_regionalization_choices = { 0 };
static GHashTable *g_locale_display_name_map = NULL;

static gboolean run_command_capture(const char *command, char **stdout_out, char **stderr_out, int *wait_status_out)
{
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    int wait_status = 0;
    GError *error = NULL;

    gboolean ok = g_spawn_command_line_sync(command,
                                            stdout_out ? &stdout_data : NULL,
                                            stderr_out ? &stderr_data : NULL,
                                            &wait_status,
                                            &error);
    if (!ok) {
        if (stderr_out) {
            *stderr_out = g_strdup(error ? error->message : "spawn failed");
        }
        g_clear_error(&error);
        g_free(stdout_data);
        g_free(stderr_data);
        return FALSE;
    }

    if (stdout_out) {
        *stdout_out = stdout_data;
    } else {
        g_free(stdout_data);
    }

    if (stderr_out) {
        *stderr_out = stderr_data;
    } else {
        g_free(stderr_data);
    }

    if (wait_status_out) {
        *wait_status_out = wait_status;
    }

    return g_spawn_check_wait_status(wait_status, NULL);
}

static gboolean run_command_success(const char *command)
{
    char *stdout_data = NULL;
    char *stderr_data = NULL;
    int wait_status = 0;
    gboolean ok = run_command_capture(command, &stdout_data, &stderr_data, &wait_status);
    (void)wait_status;
    g_free(stdout_data);
    g_free(stderr_data);
    return ok;
}

static gboolean command_is_available(const char *name)
{
    char *tool = g_find_program_in_path(name);
    if (!tool) {
        return FALSE;
    }

    g_free(tool);
    return TRUE;
}

static GtkWidget *create_row(const char *title, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), label);

    if (control) {
        gtk_widget_set_halign(control, GTK_ALIGN_END);
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        gtk_widget_set_size_request(control, 320, -1);
        gtk_box_append(GTK_BOX(row), control);
    }

    return row;
}

static GtkWidget *create_section(const char *title, const char *description)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_add_css_class(frame, "appearance-card");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);

    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(title_lbl, "card-title");
    gtk_box_append(GTK_BOX(box), title_lbl);

    if (description && *description) {
        GtkWidget *desc_lbl = gtk_label_new(description);
        gtk_widget_set_halign(desc_lbl, GTK_ALIGN_START);
        gtk_label_set_wrap(GTK_LABEL(desc_lbl), TRUE);
        gtk_widget_add_css_class(desc_lbl, "card-subtitle");
        gtk_box_append(GTK_BOX(box), desc_lbl);
    }

    gtk_frame_set_child(GTK_FRAME(frame), box);
    return frame;
}

static void status_set(const char *text, gboolean is_error)
{
    if (!g_status_label) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(g_status_label), text ? text : "");
    gtk_widget_remove_css_class(g_status_label, "error");
    gtk_widget_remove_css_class(g_status_label, "success");
    gtk_widget_add_css_class(g_status_label, is_error ? "error" : "success");
}

static void region_set_controls_sensitive(gboolean sensitive)
{
    if (g_timezone_dropdown) {
        gtk_widget_set_sensitive(g_timezone_dropdown, sensitive);
    }
    if (g_date_format_dropdown) {
        gtk_widget_set_sensitive(g_date_format_dropdown, sensitive);
    }
    if (g_time_format_dropdown) {
        gtk_widget_set_sensitive(g_time_format_dropdown, sensitive);
    }
    if (g_language_dropdown) {
        gtk_widget_set_sensitive(g_language_dropdown, sensitive);
    }
    if (g_units_dropdown) {
        gtk_widget_set_sensitive(g_units_dropdown, sensitive);
    }
    if (g_keyboard_layout_dropdown) {
        gtk_widget_set_sensitive(g_keyboard_layout_dropdown, sensitive);
    }
    if (g_regionalization_dropdown) {
        gtk_widget_set_sensitive(g_regionalization_dropdown, sensitive);
    }
    if (g_reload_btn) {
        gtk_widget_set_sensitive(g_reload_btn, sensitive);
    }
    if (g_apply_btn) {
        gtk_widget_set_sensitive(g_apply_btn, sensitive);
    }
}

static void region_set_loading(gboolean loading, const char *message)
{
    region_set_controls_sensitive(!loading);

    if (g_loading_label && message) {
        gtk_label_set_text(GTK_LABEL(g_loading_label), message);
    }

    if (g_loading_box) {
        gtk_widget_set_visible(g_loading_box, loading);
    }

    if (g_loading_spinner) {
        if (loading) {
            gtk_spinner_start(GTK_SPINNER(g_loading_spinner));
        } else {
            gtk_spinner_stop(GTK_SPINNER(g_loading_spinner));
        }
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static char *region_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "locale.conf", NULL);
}

static char *session_environment_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "environment", NULL);
}

static gboolean write_managed_env_block(const char *file_path,
                                        const char *begin_marker,
                                        const char *end_marker,
                                        const char *block)
{
    char *existing = NULL;
    gsize len = 0;
    (void)g_file_get_contents(file_path, &existing, &len, NULL);
    if (!existing) {
        existing = g_strdup("");
    }

    gchar **lines = g_strsplit(existing, "\n", -1);
    GString *filtered = g_string_new(NULL);
    gboolean skip = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        if (g_strcmp0(lines[i], begin_marker) == 0) {
            skip = TRUE;
            continue;
        }
        if (g_strcmp0(lines[i], end_marker) == 0) {
            skip = FALSE;
            continue;
        }
        if (skip) {
            continue;
        }

        g_string_append(filtered, lines[i]);
        if (lines[i + 1] != NULL) {
            g_string_append_c(filtered, '\n');
        }
    }

    while (filtered->len > 0 && filtered->str[0] == '\n') {
        g_string_erase(filtered, 0, 1);
    }

    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "%s\n%s\n%s\n", begin_marker, block ? block : "", end_marker);

    if (filtered->len > 0) {
        g_string_append_c(out, '\n');
        g_string_append(out, filtered->str);
        if (out->str[out->len - 1] != '\n') {
            g_string_append_c(out, '\n');
        }
    }

    char *dir = g_path_get_dirname(file_path);
    gboolean ok = FALSE;

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        ok = g_file_set_contents(file_path, out->str, -1, NULL);
    }

    g_free(dir);
    g_string_free(out, TRUE);
    g_string_free(filtered, TRUE);
    g_strfreev(lines);
    g_free(existing);

    return ok;
}

static void choice_list_clear(ChoiceList *list)
{
    if (!list) {
        return;
    }

    if (list->labels) {
        g_object_unref(list->labels);
        list->labels = NULL;
    }

    if (list->values) {
        g_ptr_array_unref(list->values);
        list->values = NULL;
    }
}

static void choice_list_begin(ChoiceList *list)
{
    if (!list) {
        return;
    }

    choice_list_clear(list);
    list->labels = gtk_string_list_new(NULL);
    list->values = g_ptr_array_new_with_free_func(g_free);
}

static gboolean choice_list_contains_value(const ChoiceList *list, const char *value)
{
    if (!list || !list->values || !value) {
        return FALSE;
    }

    for (guint i = 0; i < list->values->len; i++) {
        const char *existing = g_ptr_array_index(list->values, i);
        if (g_strcmp0(existing, value) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

static void choice_list_add(ChoiceList *list, const char *label, const char *value)
{
    if (!list || !list->labels || !list->values || !label || !*label || !value || !*value) {
        return;
    }

    if (choice_list_contains_value(list, value)) {
        return;
    }

    gtk_string_list_append(list->labels, label);
    g_ptr_array_add(list->values, g_strdup(value));
}

static void choice_list_apply_model(GtkWidget *dropdown, const ChoiceList *list)
{
    if (!dropdown || !GTK_IS_DROP_DOWN(dropdown) || !list || !list->labels) {
        return;
    }

    gtk_drop_down_set_model(GTK_DROP_DOWN(dropdown), G_LIST_MODEL(list->labels));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), 0);
}

static const char *choice_list_selected_value(GtkWidget *dropdown, const ChoiceList *list)
{
    if (!dropdown || !GTK_IS_DROP_DOWN(dropdown) || !list || !list->values || list->values->len == 0) {
        return "";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (idx >= list->values->len) {
        idx = 0;
    }

    return g_ptr_array_index(list->values, idx);
}

static char *canonicalize_locale(const char *raw);
static gboolean locale_value_matches(const char *lhs, const char *rhs);

static char *locale_base_dup(const char *value)
{
    if (!value || !*value) {
        return g_strdup("");
    }

    const char *end = value;
    while (*end && *end != '.' && *end != '@') {
        end++;
    }

    return g_strndup(value, (gsize)(end - value));
}

static char *locale_language_code_dup(const char *value)
{
    char *base = locale_base_dup(value);
    if (!base || !*base) {
        g_free(base);
        return g_strdup("en");
    }

    char **parts = g_strsplit(base, "_", 2);
    char *lang = g_strdup(parts[0] ? parts[0] : "en");
    char *lang_lower = g_ascii_strdown(lang, -1);
    g_free(lang);
    lang = lang_lower;

    g_strfreev(parts);
    g_free(base);
    return lang;
}

static gboolean system_locale_available(const char *locale_value)
{
    if (!locale_value || !*locale_value || !command_is_available("locale")) {
        return FALSE;
    }

    char *target = canonicalize_locale(locale_value);
    gboolean found = FALSE;

    char *stdout_data = NULL;
    gboolean ok = run_command_capture("sh -lc 'LC_ALL=C locale -a 2>/dev/null'", &stdout_data, NULL, NULL);
    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        g_free(target);
        return FALSE;
    }

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        g_strstrip(lines[i]);
        if (!lines[i][0]) {
            continue;
        }

        char *candidate = canonicalize_locale(lines[i]);
        if (locale_value_matches(candidate, target)) {
            found = TRUE;
            g_free(candidate);
            break;
        }
        g_free(candidate);
    }

    g_strfreev(lines);
    g_free(stdout_data);
    g_free(target);
    return found;
}

static gboolean locale_value_matches(const char *lhs, const char *rhs)
{
    if (!lhs || !rhs) {
        return FALSE;
    }

    if (g_ascii_strcasecmp(lhs, rhs) == 0) {
        return TRUE;
    }

    char *left_base = locale_base_dup(lhs);
    char *right_base = locale_base_dup(rhs);
    gboolean equal = g_ascii_strcasecmp(left_base, right_base) == 0;
    g_free(right_base);
    g_free(left_base);
    return equal;
}

static guint choice_list_find_index(const ChoiceList *list, const char *value, gboolean locale_fuzzy)
{
    if (!list || !list->values || !value || !*value) {
        return 0;
    }

    for (guint i = 0; i < list->values->len; i++) {
        const char *candidate = g_ptr_array_index(list->values, i);
        if (!candidate) {
            continue;
        }

        if ((!locale_fuzzy && g_strcmp0(candidate, value) == 0)
            || (locale_fuzzy && locale_value_matches(candidate, value))) {
            return i;
        }
    }

    return 0;
}

static guint find_option_index(const struct option_value *options, guint count, const char *value)
{
    if (!options || count == 0 || !value || !*value) {
        return 0;
    }

    for (guint i = 0; i < count; i++) {
        if (g_strcmp0(options[i].value, value) == 0) {
            return i;
        }
    }

    return 0;
}

static const char *dropdown_selected_value(GtkWidget *dropdown, const struct option_value *options, guint count)
{
    if (!dropdown || !options || count == 0) {
        return "";
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (idx >= count) {
        idx = 0;
    }

    return options[idx].value;
}

static GtkWidget *dropdown_from_options(const struct option_value *options, guint count, gboolean translate_labels)
{
    GtkStringList *model = gtk_string_list_new(NULL);
    for (guint i = 0; i < count; i++) {
        gtk_string_list_append(model, translate_labels ? _(options[i].label) : options[i].label);
    }

    GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    g_object_unref(model);
    return dropdown;
}

static char *canonicalize_locale(const char *raw)
{
    if (!raw) {
        return g_strdup("");
    }

    char *copy = g_strdup(raw);
    g_strstrip(copy);

    if (g_str_has_suffix(copy, ".utf8")) {
        char *base = g_strndup(copy, strlen(copy) - strlen(".utf8"));
        char *out = g_strdup_printf("%s.UTF-8", base);
        g_free(base);
        g_free(copy);
        return out;
    }

    if (g_str_has_suffix(copy, ".UTF8")) {
        char *base = g_strndup(copy, strlen(copy) - strlen(".UTF8"));
        char *out = g_strdup_printf("%s.UTF-8", base);
        g_free(base);
        g_free(copy);
        return out;
    }

    return copy;
}

static const char *code_name_lookup(const struct code_name_entry *entries, guint entries_count, const char *code)
    {
        if (!entries || entries_count == 0 || !code || !*code) {
            return NULL;
        }

        for (guint i = 0; i < entries_count; i++) {
            if (g_ascii_strcasecmp(entries[i].code, code) == 0) {
                return entries[i].name;
            }
        }

        return NULL;
    }

    static char *extract_pipe_value(const char *line, const char *prefix)
    {
        if (!line || !prefix || !g_str_has_prefix(line, prefix)) {
            return NULL;
        }

        const char *pipe = strchr(line, '|');
        if (!pipe) {
            return NULL;
        }

        char *value = g_strdup(pipe + 1);
        g_strstrip(value);
        return value;
    }

    static void locale_name_map_commit_entry(GHashTable *map,
                                             const char *locale_key,
                                             const char *language_name,
                                             const char *territory_name)
    {
        if (!map || !locale_key || !*locale_key || !language_name || !*language_name
            || g_ascii_strcasecmp(language_name, "ISO") == 0) {
            return;
        }

        char *base = locale_base_dup(locale_key);
        if (!base || !*base) {
            g_free(base);
            return;
        }

        char *label = NULL;
        if (territory_name && *territory_name && g_ascii_strcasecmp(territory_name, "ISO") != 0) {
            label = g_strdup_printf("%s (%s)", language_name, territory_name);
        } else {
            label = g_strdup(language_name);
        }

        if (!g_hash_table_contains(map, base)) {
            g_hash_table_insert(map, base, label);
            return;
        }

        g_free(base);
        g_free(label);
    }

    static GHashTable *build_locale_display_name_map(void)
    {
        GHashTable *map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        if (!command_is_available("locale")) {
            return map;
        }

        char *stdout_data = NULL;
        gboolean ok = run_command_capture("sh -lc 'locale -av 2>/dev/null'", &stdout_data, NULL, NULL);
        if (!ok || !stdout_data || !*stdout_data) {
            g_free(stdout_data);
            return map;
        }

        gchar **lines = g_strsplit(stdout_data, "\n", -1);
        char *locale_key = NULL;
        char *language_name = NULL;
        char *territory_name = NULL;

        for (guint i = 0; lines[i] != NULL; i++) {
            g_strstrip(lines[i]);
            if (!lines[i][0]) {
                continue;
            }

            if (g_str_has_prefix(lines[i], "locale:")) {
                locale_name_map_commit_entry(map, locale_key, language_name, territory_name);
                g_clear_pointer(&locale_key, g_free);
                g_clear_pointer(&language_name, g_free);
                g_clear_pointer(&territory_name, g_free);

                const char *raw = lines[i] + strlen("locale:");
                while (*raw && g_ascii_isspace(*raw)) {
                    raw++;
                }

                const char *end = raw;
                while (*end && !g_ascii_isspace(*end)) {
                    end++;
                }

                char *token = g_strndup(raw, (gsize)(end - raw));
                locale_key = canonicalize_locale(token);
                g_free(token);
                continue;
            }

            char *value = extract_pipe_value(lines[i], "language |");
            if (value) {
                g_clear_pointer(&language_name, g_free);
                language_name = value;
                continue;
            }

            value = extract_pipe_value(lines[i], "territory |");
            if (value) {
                g_clear_pointer(&territory_name, g_free);
                territory_name = value;
            }
        }

        locale_name_map_commit_entry(map, locale_key, language_name, territory_name);
        g_free(locale_key);
        g_free(language_name);
        g_free(territory_name);
        g_strfreev(lines);
        g_free(stdout_data);
        return map;
    }

    static GHashTable *get_locale_display_name_map(void)
    {
        if (!g_locale_display_name_map) {
            g_locale_display_name_map = build_locale_display_name_map();
        }
        return g_locale_display_name_map;
    }

    static char *locale_label_from_codes(const char *locale_value)
    {
        if (!locale_value || !*locale_value) {
            return g_strdup("");
        }

        char *base = locale_base_dup(locale_value);
        char **parts = g_strsplit(base, "_", 3);
        const char *language_name = code_name_lookup(g_language_name_map,
                                                      G_N_ELEMENTS(g_language_name_map),
                                                      parts[0]);
        const char *country_name = code_name_lookup(g_country_name_map,
                                                    G_N_ELEMENTS(g_country_name_map),
                                                    parts[1]);

        char *label = NULL;
        if (language_name && country_name) {
            label = g_strdup_printf("%s (%s)", language_name, country_name);
        } else if (language_name) {
            label = g_strdup(language_name);
        } else {
            for (guint i = 0; base[i] != '\0'; i++) {
                if (base[i] == '_') {
                    base[i] = '-';
                }
            }
            label = g_strdup(base);
        }

        g_strfreev(parts);
        g_free(base);
        return label;
    }
static char *locale_label_from_value(const char *locale_value)
{
    if (!locale_value || !*locale_value) {
        return g_strdup("");
    }

    char *base = locale_base_dup(locale_value);
    GHashTable *display_name_map = get_locale_display_name_map();
    const char *display_name = display_name_map ? g_hash_table_lookup(display_name_map, base) : NULL;

    if (display_name && *display_name) {
        char *label = g_strdup(display_name);
        g_free(base);
        return label;
    }

    g_free(base);
    return locale_label_from_codes(locale_value);
}

static char *detect_runtime_timezone(void)
{
    if (command_is_available("timedatectl")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'timedatectl show -p Timezone --value 2>/dev/null | head -n 1'",
            &stdout_data,
            NULL,
            NULL);
        if (ok && stdout_data) {
            g_strstrip(stdout_data);
            if (*stdout_data) {
                return stdout_data;
            }
        }
        g_free(stdout_data);
    }

    const char *tz = g_getenv("TZ");
    if (tz && *tz) {
        return g_strdup(tz);
    }

    return g_strdup("UTC");
}

static char *detect_runtime_language(void)
{
    const char *lang = g_getenv("LANG");
    if (lang && *lang) {
        return canonicalize_locale(lang);
    }
    return g_strdup("en_US.UTF-8");
}

static char *detect_runtime_regionalization(void)
{
    const char *lc_time = g_getenv("LC_TIME");
    if (lc_time && *lc_time) {
        return canonicalize_locale(lc_time);
    }

    const char *lang = g_getenv("LANG");
    if (lang && *lang) {
        return canonicalize_locale(lang);
    }

    return g_strdup("en_US.UTF-8");
}

static char *detect_runtime_keyboard_layout(void)
{
    const char *layout = g_getenv("XKB_DEFAULT_LAYOUT");
    if (layout && *layout) {
        char **parts = g_strsplit(layout, ",", 2);
        char *first = g_strdup(parts[0] ? parts[0] : "us");
        g_strstrip(first);
        g_strfreev(parts);
        return first;
    }

    if (command_is_available("setxkbmap")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc \"setxkbmap -query 2>/dev/null | awk '/layout/ {print $2; exit}'\"",
            &stdout_data,
            NULL,
            NULL);
        if (ok && stdout_data) {
            g_strstrip(stdout_data);
            if (*stdout_data) {
                char **parts = g_strsplit(stdout_data, ",", 2);
                char *first = g_strdup(parts[0] ? parts[0] : "us");
                g_strstrip(first);
                g_strfreev(parts);
                g_free(stdout_data);
                return first;
            }
        }
        g_free(stdout_data);
    }

    return g_strdup("us");
}

static const char *detect_runtime_units(const char *locale_value)
{
    char *base = locale_base_dup(locale_value);
    gboolean imperial = g_ascii_strcasecmp(base, "en_US") == 0;
    g_free(base);
    return imperial ? "imperial" : "metric";
}

static const char *detect_runtime_date_format(const char *locale_value)
{
    char *base = locale_base_dup(locale_value);
    const char *result = "eu";
    if (g_ascii_strcasecmp(base, "en_US") == 0) {
        result = "us";
    }
    g_free(base);
    return result;
}

static const char *detect_runtime_time_format(void)
{
    if (command_is_available("gsettings")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'gsettings get org.gnome.desktop.interface clock-format 2>/dev/null'",
            &stdout_data,
            NULL,
            NULL);
        if (ok && stdout_data) {
            g_strstrip(stdout_data);
            if (strstr(stdout_data, "12") != NULL) {
                g_free(stdout_data);
                return "12h";
            }
        }
        g_free(stdout_data);
    }

    return "24h";
}

static void populate_timezone_choices(const char *runtime_timezone)
{
    choice_list_begin(&g_timezone_choices);

    if (command_is_available("timedatectl")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'timedatectl list-timezones 2>/dev/null'",
            &stdout_data,
            NULL,
            NULL);

        if (ok && stdout_data && *stdout_data) {
            gchar **lines = g_strsplit(stdout_data, "\n", -1);
            for (guint i = 0; lines[i] != NULL; i++) {
                g_strstrip(lines[i]);
                if (!lines[i][0]) {
                    continue;
                }
                choice_list_add(&g_timezone_choices, lines[i], lines[i]);
            }
            g_strfreev(lines);
        }

        g_free(stdout_data);
    }

    if (!g_timezone_choices.values || g_timezone_choices.values->len == 0) {
        choice_list_add(&g_timezone_choices, "Europe/Warsaw", "Europe/Warsaw");
        choice_list_add(&g_timezone_choices, "UTC", "UTC");
    }

    if (runtime_timezone && *runtime_timezone) {
        choice_list_add(&g_timezone_choices, runtime_timezone, runtime_timezone);
    }
}

static gboolean locale_base_is_special(const char *locale_value)
{
    if (!locale_value || !*locale_value) {
        return TRUE;
    }

    char *base = locale_base_dup(locale_value);
    gboolean special = (g_ascii_strcasecmp(base, "C") == 0)
                       || (g_ascii_strcasecmp(base, "POSIX") == 0);
    g_free(base);
    return special;
}

static void append_locale_choice(ChoiceList *list, const char *raw_locale)
{
    if (!list || !raw_locale || !*raw_locale) {
        return;
    }

    char *normalized = canonicalize_locale(raw_locale);
    if (!normalized || !*normalized || locale_base_is_special(normalized)) {
        g_free(normalized);
        return;
    }

    char *label = locale_label_from_value(normalized);
    choice_list_add(list, label, normalized);
    g_free(label);
    g_free(normalized);
}

static void collect_system_locales_from_locale_a(ChoiceList *list)
{
    if (!list || !command_is_available("locale")) {
        return;
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture("sh -lc 'LC_ALL=C locale -a 2>/dev/null'", &stdout_data, NULL, NULL);
    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return;
    }

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        g_strstrip(lines[i]);
        if (!lines[i][0]) {
            continue;
        }
        append_locale_choice(list, lines[i]);
    }

    g_strfreev(lines);
    g_free(stdout_data);
}

static void collect_system_locales_from_locale_av(ChoiceList *list)
{
    if (!list || !command_is_available("locale")) {
        return;
    }

    char *stdout_data = NULL;
    gboolean ok = run_command_capture("sh -lc 'LC_ALL=C locale -av 2>/dev/null'", &stdout_data, NULL, NULL);
    if (!ok || !stdout_data || !*stdout_data) {
        g_free(stdout_data);
        return;
    }

    gchar **lines = g_strsplit(stdout_data, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        g_strstrip(lines[i]);
        if (!g_str_has_prefix(lines[i], "locale:")) {
            continue;
        }

        const char *raw = lines[i] + strlen("locale:");
        while (*raw && g_ascii_isspace(*raw)) {
            raw++;
        }

        const char *end = raw;
        while (*end && !g_ascii_isspace(*end)) {
            end++;
        }

        char *token = g_strndup(raw, (gsize)(end - raw));
        append_locale_choice(list, token);
        g_free(token);
    }

    g_strfreev(lines);
    g_free(stdout_data);
}

static gboolean locale_token_is_utf8(const char *token)
{
    if (!token || !*token) {
        return FALSE;
    }

    return strstr(token, "UTF-8") != NULL
           || strstr(token, "UTF8") != NULL
           || strstr(token, "utf8") != NULL
           || strstr(token, "utf-8") != NULL;
}

static void collect_system_locales_from_supported_file(ChoiceList *list)
{
    if (!list) {
        return;
    }

    const char *path = "/usr/share/i18n/SUPPORTED";
    char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL) || !contents) {
        g_free(contents);
        return;
    }

    gchar **lines = g_strsplit(contents, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        g_strstrip(lines[i]);
        if (!lines[i][0] || lines[i][0] == '#') {
            continue;
        }

        gchar **parts = g_strsplit_set(lines[i], " \t", 0);
        const char *locale_token = NULL;
        const char *encoding_token = NULL;
        for (guint p = 0; parts[p] != NULL; p++) {
            if (!parts[p][0]) {
                continue;
            }
            if (!locale_token) {
                locale_token = parts[p];
            } else {
                encoding_token = parts[p];
                break;
            }
        }

        if (locale_token && *locale_token) {
            gboolean utf8_line = locale_token_is_utf8(locale_token)
                                || locale_token_is_utf8(encoding_token);
            if (utf8_line) {
                if (strchr(locale_token, '.') != NULL) {
                    append_locale_choice(list, locale_token);
                } else {
                    char *normalized = g_strdup_printf("%s.UTF-8", locale_token);
                    append_locale_choice(list, normalized);
                    g_free(normalized);
                }
            }
        }

        g_strfreev(parts);
    }

    g_strfreev(lines);
    g_free(contents);
}

static void collect_system_locales(ChoiceList *list, const char *runtime_locale)
{
    if (!list) {
        return;
    }

    collect_system_locales_from_locale_a(list);
    collect_system_locales_from_locale_av(list);
    collect_system_locales_from_supported_file(list);

    if (runtime_locale && *runtime_locale) {
        append_locale_choice(list, runtime_locale);
    }

    if (!list->values || list->values->len == 0) {
        choice_list_add(list, "English (United States)", "en_US.UTF-8");
        choice_list_add(list, "Polish (Poland)", "pl_PL.UTF-8");
    }
}

static void populate_language_choices(const char *runtime_language)
{
    choice_list_begin(&g_language_choices);
    collect_system_locales(&g_language_choices, runtime_language);
}

static void populate_regionalization_choices(const char *runtime_locale)
{
    choice_list_begin(&g_regionalization_choices);
    choice_list_add(&g_regionalization_choices, _("Automatic (match language)"), "auto");
    collect_system_locales(&g_regionalization_choices, runtime_locale);
}

static void collect_keyboard_layouts_from_base_lst(ChoiceList *list)
{
    if (!list) {
        return;
    }

    const char *path = "/usr/share/X11/xkb/rules/base.lst";
    char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL) || !contents) {
        g_free(contents);
        return;
    }

    gchar **lines = g_strsplit(contents, "\n", -1);
    gboolean in_layout_section = FALSE;

    for (guint i = 0; lines[i] != NULL; i++) {
        g_strstrip(lines[i]);
        if (!lines[i][0]) {
            continue;
        }

        if (g_str_has_prefix(lines[i], "!")) {
            in_layout_section = g_str_has_prefix(lines[i], "! layout");
            continue;
        }

        if (!in_layout_section) {
            continue;
        }

        char **parts = g_strsplit_set(lines[i], " \t", 2);
        if (parts[0] && *parts[0]) {
            char *layout = g_strdup(parts[0]);
            g_strstrip(layout);
            if (*layout) {
                const char *desc = parts[1] ? parts[1] : layout;
                while (*desc && g_ascii_isspace(*desc)) {
                    desc++;
                }
                choice_list_add(list, desc, layout);
            }
            g_free(layout);
        }
        g_strfreev(parts);
    }

    g_strfreev(lines);
    g_free(contents);
}

static void populate_keyboard_choices(const char *runtime_layout)
{
    choice_list_begin(&g_keyboard_choices);

    if (command_is_available("localectl")) {
        char *stdout_data = NULL;
        gboolean ok = run_command_capture(
            "sh -lc 'localectl list-x11-keymap-layouts 2>/dev/null'",
            &stdout_data,
            NULL,
            NULL);

        if (ok && stdout_data && *stdout_data) {
            gchar **lines = g_strsplit(stdout_data, "\n", -1);
            for (guint i = 0; lines[i] != NULL; i++) {
                g_strstrip(lines[i]);
                if (!lines[i][0]) {
                    continue;
                }
                choice_list_add(&g_keyboard_choices, lines[i], lines[i]);
            }
            g_strfreev(lines);
        }

        g_free(stdout_data);
    }

    if (!g_keyboard_choices.values || g_keyboard_choices.values->len == 0) {
        collect_keyboard_layouts_from_base_lst(&g_keyboard_choices);
    }

    if (!g_keyboard_choices.values || g_keyboard_choices.values->len == 0) {
        choice_list_add(&g_keyboard_choices, "us", "us");
        choice_list_add(&g_keyboard_choices, "pl", "pl");
        choice_list_add(&g_keyboard_choices, "de", "de");
        choice_list_add(&g_keyboard_choices, "fr", "fr");
    }

    if (runtime_layout && *runtime_layout) {
        choice_list_add(&g_keyboard_choices, runtime_layout, runtime_layout);
    }
}

static void refresh_shell_and_top_panel(void)
{
    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-shell >/dev/null 2>&1 || true; pkill -USR1 -x karton-top-panel >/dev/null 2>&1 || true; pkill -USR1 -x karton-side-dock >/dev/null 2>&1 || true'");
}

static void save_region_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_string(kf, "region", "timezone", choice_list_selected_value(g_timezone_dropdown, &g_timezone_choices));
    g_key_file_set_string(kf, "region", "date_format", dropdown_selected_value(g_date_format_dropdown, g_date_format_options, G_N_ELEMENTS(g_date_format_options)));
    g_key_file_set_string(kf, "region", "time_format", dropdown_selected_value(g_time_format_dropdown, g_time_format_options, G_N_ELEMENTS(g_time_format_options)));
    g_key_file_set_string(kf, "region", "language", choice_list_selected_value(g_language_dropdown, &g_language_choices));
    g_key_file_set_string(kf, "region", "units", dropdown_selected_value(g_units_dropdown, g_units_options, G_N_ELEMENTS(g_units_options)));
    g_key_file_set_string(kf, "region", "keyboard_layout", choice_list_selected_value(g_keyboard_layout_dropdown, &g_keyboard_choices));
    g_key_file_set_string(kf, "region", "regionalization", choice_list_selected_value(g_regionalization_dropdown, &g_regionalization_choices));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = region_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_region_config(void)
{
    region_set_loading(TRUE, _("Loading date, time and region settings..."));

    char *runtime_timezone = detect_runtime_timezone();
    char *runtime_language = detect_runtime_language();
    char *runtime_locale = detect_runtime_regionalization();
    char *runtime_keyboard = detect_runtime_keyboard_layout();
    const char *runtime_date_format = detect_runtime_date_format(runtime_locale);
    const char *runtime_time_format = detect_runtime_time_format();
    const char *runtime_units = detect_runtime_units(runtime_locale);

    populate_timezone_choices(runtime_timezone);
    populate_language_choices(runtime_language);
    populate_keyboard_choices(runtime_keyboard);
    populate_regionalization_choices(runtime_locale);

    choice_list_apply_model(g_timezone_dropdown, &g_timezone_choices);
    choice_list_apply_model(g_language_dropdown, &g_language_choices);
    choice_list_apply_model(g_keyboard_layout_dropdown, &g_keyboard_choices);
    choice_list_apply_model(g_regionalization_dropdown, &g_regionalization_choices);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_timezone_dropdown),
                               choice_list_find_index(&g_timezone_choices, runtime_timezone, FALSE));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_language_dropdown),
                               choice_list_find_index(&g_language_choices, runtime_language, TRUE));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_keyboard_layout_dropdown),
                               choice_list_find_index(&g_keyboard_choices, runtime_keyboard, FALSE));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_regionalization_dropdown),
                               choice_list_find_index(&g_regionalization_choices, runtime_locale, TRUE));

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_date_format_dropdown),
                               find_option_index(g_date_format_options, G_N_ELEMENTS(g_date_format_options), runtime_date_format));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_time_format_dropdown),
                               find_option_index(g_time_format_options, G_N_ELEMENTS(g_time_format_options), runtime_time_format));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_units_dropdown),
                               find_option_index(g_units_options, G_N_ELEMENTS(g_units_options), runtime_units));

    char *path = region_config_path();
    GKeyFile *kf = g_key_file_new();

    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;

        char *timezone = g_key_file_get_string(kf, "region", "timezone", &error);
        if (error) {
            g_clear_error(&error);
            timezone = NULL;
        }
        char *date_format = g_key_file_get_string(kf, "region", "date_format", &error);
        if (error) {
            g_clear_error(&error);
            date_format = NULL;
        }
        char *time_format = g_key_file_get_string(kf, "region", "time_format", &error);
        if (error) {
            g_clear_error(&error);
            time_format = NULL;
        }
        char *language = g_key_file_get_string(kf, "region", "language", &error);
        if (error) {
            g_clear_error(&error);
            language = NULL;
        }
        char *units = g_key_file_get_string(kf, "region", "units", &error);
        if (error) {
            g_clear_error(&error);
            units = NULL;
        }
        char *keyboard_layout = g_key_file_get_string(kf, "region", "keyboard_layout", &error);
        if (error) {
            g_clear_error(&error);
            keyboard_layout = NULL;
        }
        char *regionalization = g_key_file_get_string(kf, "region", "regionalization", &error);
        if (error) {
            g_clear_error(&error);
            regionalization = NULL;
        }

        if (timezone) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_timezone_dropdown),
                                       choice_list_find_index(&g_timezone_choices, timezone, FALSE));
        }
        if (date_format) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_date_format_dropdown),
                                       find_option_index(g_date_format_options, G_N_ELEMENTS(g_date_format_options), date_format));
        }
        if (time_format) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_time_format_dropdown),
                                       find_option_index(g_time_format_options, G_N_ELEMENTS(g_time_format_options), time_format));
        }
        if (language) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_language_dropdown),
                                       choice_list_find_index(&g_language_choices, language, TRUE));
        }
        if (units) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_units_dropdown),
                                       find_option_index(g_units_options, G_N_ELEMENTS(g_units_options), units));
        }
        if (keyboard_layout) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_keyboard_layout_dropdown),
                                       choice_list_find_index(&g_keyboard_choices, keyboard_layout, FALSE));
        }
        if (regionalization) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(g_regionalization_dropdown),
                                       choice_list_find_index(&g_regionalization_choices, regionalization, TRUE));
        }

        g_free(regionalization);
        g_free(keyboard_layout);
        g_free(units);
        g_free(language);
        g_free(time_format);
        g_free(date_format);
        g_free(timezone);
    }

    g_key_file_unref(kf);
    g_free(path);

    g_free(runtime_keyboard);
    g_free(runtime_locale);
    g_free(runtime_language);
    g_free(runtime_timezone);

    region_set_loading(FALSE, NULL);
}

static char *apply_runtime_region(void)
{
    const char *timezone = choice_list_selected_value(g_timezone_dropdown, &g_timezone_choices);
    const char *date_format = dropdown_selected_value(g_date_format_dropdown, g_date_format_options, G_N_ELEMENTS(g_date_format_options));
    const char *time_format = dropdown_selected_value(g_time_format_dropdown, g_time_format_options, G_N_ELEMENTS(g_time_format_options));
    const char *language = choice_list_selected_value(g_language_dropdown, &g_language_choices);
    const char *units = dropdown_selected_value(g_units_dropdown, g_units_options, G_N_ELEMENTS(g_units_options));
    const char *keyboard_layout = choice_list_selected_value(g_keyboard_layout_dropdown, &g_keyboard_choices);
    const char *regionalization = choice_list_selected_value(g_regionalization_dropdown, &g_regionalization_choices);

    const char *locale = (g_strcmp0(regionalization, "auto") == 0) ? language : regionalization;
    char *effective_locale = canonicalize_locale(locale);
    char *language_code = locale_language_code_dup(language);
    char *language_messages = g_strdup_printf("%s:en", language_code && *language_code ? language_code : "en");

    GString *issues = g_string_new(NULL);

    if (!system_locale_available(effective_locale)) {
        char *runtime_locale = detect_runtime_regionalization();
        char *fallback_locale = canonicalize_locale(runtime_locale);
        g_free(runtime_locale);

        if (system_locale_available(fallback_locale)) {
            g_free(effective_locale);
            effective_locale = fallback_locale;
        } else {
            g_free(fallback_locale);
            g_free(effective_locale);
            if (system_locale_available("en_US.UTF-8")) {
                effective_locale = g_strdup("en_US.UTF-8");
            } else if (system_locale_available("C.UTF-8")) {
                effective_locale = g_strdup("C.UTF-8");
            } else {
                effective_locale = g_strdup("C");
            }
        }

    }

    if (command_is_available("timedatectl")) {
        char *quoted_tz = g_shell_quote(timezone);
        char *cmd = g_strdup_printf("sh -lc 'timedatectl set-timezone %s >/dev/null 2>&1'", quoted_tz);
        if (!run_command_success(cmd)) {
            g_string_append(issues, _("Could not set system time zone (this may require elevated privileges). "));
        }
        g_free(cmd);
        g_free(quoted_tz);
    }

    if (command_is_available("setxkbmap")) {
        char *quoted_layout = g_shell_quote(keyboard_layout);
        char *cmd = g_strdup_printf("sh -lc 'setxkbmap %s >/dev/null 2>&1'", quoted_layout);
        if (!run_command_success(cmd)) {
            g_string_append(issues, _("Could not apply keyboard layout at runtime. "));
        }
        g_free(cmd);
        g_free(quoted_layout);
    }

    if (command_is_available("gsettings")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'gsettings set org.gnome.desktop.interface clock-format %s >/dev/null 2>&1'",
            time_format);
        if (!run_command_success(cmd)) {
            g_string_append(issues, _("Could not apply 12/24h clock format at runtime. "));
        }
        g_free(cmd);
    }

    GString *env_block = g_string_new(NULL);
    g_string_append_printf(env_block,
                           "LANG=%s\n"
                           "LC_MESSAGES=%s\n"
                           "LC_TIME=%s\n"
                           "LC_NUMERIC=%s\n"
                           "LC_MONETARY=%s\n"
                           "LC_MEASUREMENT=%s\n"
                           "LANGUAGE=%s\n"
                           "TZ=%s\n"
                           "XKB_DEFAULT_LAYOUT=%s\n"
                           "KARTON_DATE_FORMAT=%s\n"
                           "KARTON_TIME_FORMAT=%s\n"
                           "KARTON_UNITS=%s\n"
                           "KARTON_REGIONALIZATION=%s",
                           effective_locale,
                           effective_locale,
                           effective_locale,
                           effective_locale,
                           effective_locale,
                           effective_locale,
                           language_messages,
                           timezone,
                           keyboard_layout,
                           date_format,
                           time_format,
                           units,
                           regionalization);

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed region env",
                                              "# END KartON managed region env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist region environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd LANG=%s LC_MESSAGES=%s LC_TIME=%s LC_NUMERIC=%s LC_MONETARY=%s LC_MEASUREMENT=%s LANGUAGE=%s TZ=%s XKB_DEFAULT_LAYOUT=%s KARTON_DATE_FORMAT=%s KARTON_TIME_FORMAT=%s KARTON_UNITS=%s KARTON_REGIONALIZATION=%s >/dev/null 2>&1 || true'",
            effective_locale,
            effective_locale,
            effective_locale,
            effective_locale,
            effective_locale,
            effective_locale,
            language_messages,
            timezone,
            keyboard_layout,
            date_format,
            time_format,
            units,
            regionalization);
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    g_setenv("LANG", effective_locale, TRUE);
    g_setenv("LC_MESSAGES", effective_locale, TRUE);
    g_setenv("LC_TIME", effective_locale, TRUE);
    g_setenv("LC_NUMERIC", effective_locale, TRUE);
    g_setenv("LC_MONETARY", effective_locale, TRUE);
    g_setenv("LC_MEASUREMENT", effective_locale, TRUE);
    g_setenv("LANGUAGE", language_messages, TRUE);
    g_setenv("TZ", timezone, TRUE);
    g_setenv("XKB_DEFAULT_LAYOUT", keyboard_layout, TRUE);
    setlocale(LC_ALL, "");
    tzset();

    refresh_shell_and_top_panel();

    g_free(env_path);
    g_string_free(env_block, TRUE);
    g_free(effective_locale);
    g_free(language_code);
    g_free(language_messages);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_reload_region_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_region_config();
    status_set(_("Date, time and region settings reloaded"), FALSE);
}

static void on_apply_region_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    region_set_loading(TRUE, _("Applying date, time and region settings..."));

    save_region_config();

    char *issues = apply_runtime_region();
    region_set_loading(FALSE, NULL);

    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("Date, time and region settings applied"), FALSE);
}

GtkWidget *page_region_new(void)
{
    GtkWidget *outer_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outer_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(box, 28);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_end(box, 28);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_add_css_class(box, "appearance-page");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(outer_scroll), box);

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(hero, "appearance-hero");

    GtkWidget *title = gtk_label_new(_("Date, time and region"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Set time zone, date format, time format, language, measurement units, keyboard layout and regional profile."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *time_frame = create_section(_("Date and time"),
                                           _("Select your time zone, date format and 12/24h clock mode."));
    GtkWidget *time_box = gtk_frame_get_child(GTK_FRAME(time_frame));

    g_timezone_dropdown = gtk_drop_down_new(NULL, NULL);
    g_date_format_dropdown = dropdown_from_options(g_date_format_options, G_N_ELEMENTS(g_date_format_options), TRUE);
    g_time_format_dropdown = dropdown_from_options(g_time_format_options, G_N_ELEMENTS(g_time_format_options), TRUE);

    gtk_box_append(GTK_BOX(time_box), create_row(_("Time zone"), g_timezone_dropdown));
    gtk_box_append(GTK_BOX(time_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(time_box), create_row(_("Date format"), g_date_format_dropdown));
    gtk_box_append(GTK_BOX(time_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(time_box), create_row(_("Time format"), g_time_format_dropdown));

    gtk_box_append(GTK_BOX(box), time_frame);

    GtkWidget *region_frame = create_section(_("Language and regional settings"),
                                             _("Load language and region choices directly from the system locale list."));
    GtkWidget *region_box = gtk_frame_get_child(GTK_FRAME(region_frame));

    g_language_dropdown = gtk_drop_down_new(NULL, NULL);
    g_units_dropdown = dropdown_from_options(g_units_options, G_N_ELEMENTS(g_units_options), TRUE);
    g_regionalization_dropdown = gtk_drop_down_new(NULL, NULL);

    gtk_box_append(GTK_BOX(region_box), create_row(_("System language"), g_language_dropdown));
    gtk_box_append(GTK_BOX(region_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(region_box), create_row(_("Measurement units"), g_units_dropdown));
    gtk_box_append(GTK_BOX(region_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(region_box), create_row(_("Regionalization"), g_regionalization_dropdown));

    gtk_box_append(GTK_BOX(box), region_frame);

    GtkWidget *keyboard_frame = create_section(_("Keyboard"),
                                               _("Load keyboard layouts from the system and apply selected layout in current session."));
    GtkWidget *keyboard_box = gtk_frame_get_child(GTK_FRAME(keyboard_frame));

    g_keyboard_layout_dropdown = gtk_drop_down_new(NULL, NULL);

    gtk_box_append(GTK_BOX(keyboard_box), create_row(_("Keyboard layout"), g_keyboard_layout_dropdown));

    gtk_box_append(GTK_BOX(box), keyboard_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    g_reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(g_reload_btn, "clicked", G_CALLBACK(on_reload_region_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_reload_btn);

    g_apply_btn = gtk_button_new_with_label(_("Apply date/time/region settings"));
    gtk_widget_add_css_class(g_apply_btn, "suggested-action");
    g_signal_connect(g_apply_btn, "clicked", G_CALLBACK(on_apply_region_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), g_apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_loading_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(g_loading_box, GTK_ALIGN_START);
    gtk_widget_set_visible(g_loading_box, FALSE);

    g_loading_spinner = gtk_spinner_new();
    gtk_box_append(GTK_BOX(g_loading_box), g_loading_spinner);

    g_loading_label = gtk_label_new(_("Loading date, time and region settings..."));
    gtk_widget_add_css_class(g_loading_label, "row-subtitle");
    gtk_box_append(GTK_BOX(g_loading_box), g_loading_label);

    gtk_box_append(GTK_BOX(box), g_loading_box);

    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_date_format_dropdown),
                               find_option_index(g_date_format_options, G_N_ELEMENTS(g_date_format_options), "iso"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_time_format_dropdown),
                               find_option_index(g_time_format_options, G_N_ELEMENTS(g_time_format_options), "24h"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_units_dropdown),
                               find_option_index(g_units_options, G_N_ELEMENTS(g_units_options), "metric"));

    load_region_config();

    return outer_scroll;
}
