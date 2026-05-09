#include "page-appearance.h"
#include <glib.h>
#include <gio/gio.h>
#include <libintl.h>
#include <pango/pangocairo.h>
#include <stdlib.h>

#define _(s) gettext(s)

static GtkWidget *g_btn_light = NULL;
static GtkWidget *g_btn_dark = NULL;
static GtkWidget *g_btn_auto = NULL;
static GtkWidget *g_transparency_scale = NULL;
static GtkWidget *g_text_scale_slider = NULL;
static GtkWidget *g_animation_switch = NULL;
static GtkWidget *g_font_dropdown = NULL;
static GtkWidget *g_wallpaper_entry = NULL;
static GtkWidget *g_lockscreen_entry = NULL;
static GtkWidget *g_wallpaper_status_label = NULL;
static GtkWidget *g_lockscreen_status_label = NULL;

static GFileMonitor *g_theme_monitor = NULL;
static GSettings *g_interface_settings = NULL;
static GSettings *g_background_settings = NULL;
static GSettings *g_screensaver_settings = NULL;
static GtkStringList *g_font_model = NULL;

static guint g_theme_apply_timeout_id = 0;
static gboolean g_block_runtime_handlers = FALSE;

static void schedule_apply_current_mode(void);
static void sync_controls_from_gsettings(void);

typedef struct {
    GtkWidget *entry;
} FilePickContext;

static void on_choose_image_from_disk_clicked(GtkButton *btn, gpointer data);
static void on_choose_image_from_defaults_clicked(GtkButton *btn, gpointer data);

static void update_active_button(const char *mode) {
    if (!g_btn_light || !g_btn_dark || !g_btn_auto) return;
    gtk_widget_remove_css_class(g_btn_light, "suggested-action");
    gtk_widget_remove_css_class(g_btn_dark, "suggested-action");
    gtk_widget_remove_css_class(g_btn_auto, "suggested-action");

    if (g_strcmp0(mode, "light") == 0) {
        gtk_widget_add_css_class(g_btn_light, "suggested-action");
    } else if (g_strcmp0(mode, "dark") == 0) {
        gtk_widget_add_css_class(g_btn_dark, "suggested-action");
    } else {
        gtk_widget_add_css_class(g_btn_auto, "suggested-action");
    }
}

static char *theme_mode_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "theme-mode", NULL);
}

static char *transparency_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "window-transparency", NULL);
}

static char *desktop_effects_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "desktop-effects", NULL);
}

static char *wallpaper_override_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "wallpaper-path", NULL);
}

static char *lockscreen_override_path(void) {
    return g_build_filename(g_get_home_dir(), ".config", "karton", "lockscreen-path", NULL);
}

static char *read_text_file_trimmed(const char *path) {
    char *content = NULL;
    if (!g_file_get_contents(path, &content, NULL, NULL)) {
        return NULL;
    }
    g_strstrip(content);
    return content;
}

static gboolean write_text_file(const char *path, const char *content) {
    char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_free(dir);
        return FALSE;
    }
    g_free(dir);
    return g_file_set_contents(path, content, -1, NULL);
}

static char *read_current_mode(void) {
    char *path = theme_mode_path();
    char *mode = read_text_file_trimmed(path);
    g_free(path);

    if (!mode || !*mode) {
        g_free(mode);
        return g_strdup("auto");
    }
    if (g_strcmp0(mode, "light") != 0 && g_strcmp0(mode, "dark") != 0 && g_strcmp0(mode, "auto") != 0) {
        g_free(mode);
        return g_strdup("auto");
    }
    return mode;
}

static int read_font_size_from_setting(const char *font_setting, int fallback) {
    if (!font_setting || !*font_setting) {
        return fallback;
    }

    const char *sep = strrchr(font_setting, ' ');
    if (!sep || !*(sep + 1)) {
        return fallback;
    }

    char *end = NULL;
    long size = strtol(sep + 1, &end, 10);
    if (end == (sep + 1) || *end != '\0') {
        return fallback;
    }
    if (size < 6 || size > 42) {
        return fallback;
    }
    return (int)size;
}

static char *extract_font_family(const char *font_setting) {
    if (!font_setting || !*font_setting) {
        return NULL;
    }

    const char *sep = strrchr(font_setting, ' ');
    if (!sep || sep == font_setting) {
        return g_strdup(font_setting);
    }

    gboolean numeric_size = TRUE;
    for (const char *p = sep + 1; *p; p++) {
        if (!g_ascii_isdigit(*p)) {
            numeric_size = FALSE;
            break;
        }
    }
    if (!numeric_size) {
        return g_strdup(font_setting);
    }

    return g_strndup(font_setting, (gsize)(sep - font_setting));
}

static gint compare_string_ptrs(gconstpointer a, gconstpointer b) {
    const char *sa = *((char * const *)a);
    const char *sb = *((char * const *)b);
    return g_ascii_strcasecmp(sa, sb);
}

static void populate_system_fonts(const char *preferred_family) {
    if (!g_font_model || !g_font_dropdown) {
        return;
    }

    while (g_list_model_get_n_items(G_LIST_MODEL(g_font_model)) > 0) {
        guint last = g_list_model_get_n_items(G_LIST_MODEL(g_font_model)) - 1;
        gtk_string_list_remove(g_font_model, last);
    }

    PangoFontMap *font_map = pango_cairo_font_map_get_default();
    PangoFontFamily **families = NULL;
    int n_families = 0;
    if (font_map) {
        pango_font_map_list_families(font_map, &families, &n_families);
    }

    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    for (int i = 0; i < n_families; i++) {
        const char *name = pango_font_family_get_name(families[i]);
        if (!name || !*name) {
            continue;
        }

        char *norm = g_utf8_strdown(name, -1);
        if (g_hash_table_contains(seen, norm)) {
            g_free(norm);
            continue;
        }

        g_hash_table_add(seen, norm);
        g_ptr_array_add(names, g_strdup(name));
    }

    g_free(families);
    g_hash_table_unref(seen);
    g_ptr_array_sort(names, compare_string_ptrs);

    guint selected = GTK_INVALID_LIST_POSITION;
    for (guint i = 0; i < names->len; i++) {
        const char *name = g_ptr_array_index(names, i);
        gtk_string_list_append(g_font_model, name);
        if (preferred_family && g_ascii_strcasecmp(preferred_family, name) == 0) {
            selected = i;
        }
    }

    if (g_list_model_get_n_items(G_LIST_MODEL(g_font_model)) == 0) {
        gtk_string_list_append(g_font_model, "Cantarell");
        selected = 0;
    } else if (selected == GTK_INVALID_LIST_POSITION) {
        selected = 0;
    }

    g_block_runtime_handlers = TRUE;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(g_font_dropdown), selected);
    g_block_runtime_handlers = FALSE;

    g_ptr_array_unref(names);
}

static char *resolve_wallpapers_dir(void) {
    char *user_local = g_build_filename(g_get_home_dir(), ".local-karton", "share", "wallpapers", NULL);
    if (g_file_test(user_local, G_FILE_TEST_IS_DIR)) {
        return user_local;
    }
    g_free(user_local);

    char *repo_dir = g_build_filename(g_get_home_dir(), "KartONDE", "wallpapers", NULL);
    if (g_file_test(repo_dir, G_FILE_TEST_IS_DIR)) {
        return repo_dir;
    }
    g_free(repo_dir);

    if (g_file_test("/usr/local/share/wallpapers", G_FILE_TEST_IS_DIR)) {
        return g_strdup("/usr/local/share/wallpapers");
    }
    if (g_file_test("/usr/share/wallpapers", G_FILE_TEST_IS_DIR)) {
        return g_strdup("/usr/share/wallpapers");
    }
    return NULL;
}

static void on_image_file_dialog_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    FilePickContext *ctx = user_data;
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);

    if (file) {
        char *path = g_file_get_path(file);
        if (path && GTK_IS_EDITABLE(ctx->entry)) {
            gtk_editable_set_text(GTK_EDITABLE(ctx->entry), path);
        }
        g_free(path);
        g_object_unref(file);
    }

    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_warning("Image dialog failed: %s", error->message);
    }
    g_clear_error(&error);

    g_object_unref(ctx->entry);
    g_free(ctx);
}

static void open_image_file_dialog_for_entry(GtkWidget *entry, gboolean defaults_only) {
    if (!entry) {
        return;
    }

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(
        dialog,
        defaults_only ? _("Choose default wallpaper") : _("Choose image from disk")
    );
    gtk_file_dialog_set_accept_label(dialog, _("Select"));

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, _("Image files"));
    gtk_file_filter_add_suffix(images, "png");
    gtk_file_filter_add_suffix(images, "jpg");
    gtk_file_filter_add_suffix(images, "jpeg");
    gtk_file_filter_add_suffix(images, "webp");
    gtk_file_filter_add_suffix(images, "bmp");
    g_list_store_append(filters, images);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, images);

    if (defaults_only) {
        char *wall_dir = resolve_wallpapers_dir();
        if (wall_dir) {
            GFile *folder = g_file_new_for_path(wall_dir);
            gtk_file_dialog_set_initial_folder(dialog, folder);
            g_object_unref(folder);
            g_free(wall_dir);
        }
    } else {
        GFile *home = g_file_new_for_path(g_get_home_dir());
        gtk_file_dialog_set_initial_folder(dialog, home);
        g_object_unref(home);
    }

    FilePickContext *ctx = g_new0(FilePickContext, 1);
    ctx->entry = g_object_ref(entry);

    GtkRoot *root = gtk_widget_get_root(entry);
    GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
    gtk_file_dialog_open(dialog, parent, NULL, on_image_file_dialog_response, ctx);

    g_object_unref(images);
    g_object_unref(filters);
    g_object_unref(dialog);
}

static gboolean has_font_extension(const char *basename) {
    if (!basename || !*basename) {
        return FALSE;
    }
    char *lower = g_ascii_strdown(basename, -1);
    gboolean ok =
        g_str_has_suffix(lower, ".ttf") ||
        g_str_has_suffix(lower, ".otf") ||
        g_str_has_suffix(lower, ".ttc");
    g_free(lower);
    return ok;
}

static void on_font_file_dialog_response(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)user_data;
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);

    if (!file) {
        if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_warning("Font dialog failed: %s", error->message);
        }
        g_clear_error(&error);
        return;
    }

    char *basename = g_file_get_basename(file);
    if (!has_font_extension(basename)) {
        g_warning("Unsupported font file extension: %s", basename ? basename : "(null)");
        g_free(basename);
        g_object_unref(file);
        return;
    }

    char *fonts_dir = g_build_filename(g_get_home_dir(), ".local", "share", "fonts", NULL);
    if (g_mkdir_with_parents(fonts_dir, 0755) != 0) {
        g_warning("Failed to create fonts directory: %s", fonts_dir);
        g_free(fonts_dir);
        g_free(basename);
        g_object_unref(file);
        return;
    }

    char *dst_path = g_build_filename(fonts_dir, basename, NULL);
    GFile *dst = g_file_new_for_path(dst_path);

    if (!g_file_copy(file, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
        g_warning("Failed to install font: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
    } else {
        gchar *argv[] = { "fc-cache", "-f", NULL };
        g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
        sync_controls_from_gsettings();
    }

    g_object_unref(dst);
    g_free(dst_path);
    g_free(fonts_dir);
    g_free(basename);
    g_object_unref(file);
}

static void on_install_font_clicked(GtkButton *btn, gpointer data) {
    (void)data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Install custom font"));
    gtk_file_dialog_set_accept_label(dialog, _("Install"));

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *fonts = gtk_file_filter_new();
    gtk_file_filter_set_name(fonts, _("Font files"));
    gtk_file_filter_add_suffix(fonts, "ttf");
    gtk_file_filter_add_suffix(fonts, "otf");
    gtk_file_filter_add_suffix(fonts, "ttc");
    g_list_store_append(filters, fonts);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, fonts);

    GFile *home = g_file_new_for_path(g_get_home_dir());
    gtk_file_dialog_set_initial_folder(dialog, home);

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
    GtkWindow *parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
    gtk_file_dialog_open(dialog, parent, NULL, on_font_file_dialog_response, NULL);

    g_object_unref(home);
    g_object_unref(fonts);
    g_object_unref(filters);
    g_object_unref(dialog);
}

static void on_choose_image_from_disk_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    open_image_file_dialog_for_entry(GTK_WIDGET(data), FALSE);
}

static void on_choose_image_from_defaults_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    open_image_file_dialog_for_entry(GTK_WIDGET(data), TRUE);
}

static gboolean schema_exists(const char *schema_name) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) {
        return FALSE;
    }
    GSettingsSchema *schema = g_settings_schema_source_lookup(source, schema_name, TRUE);
    if (!schema) {
        return FALSE;
    }
    g_settings_schema_unref(schema);
    return TRUE;
}

static GtkWidget *create_theme_choice_content(const char *label) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *preview = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(preview, "theme-choice-preview");
    gtk_widget_set_size_request(preview, 112, 62);

    GtkWidget *glyph = gtk_image_new_from_resource("/io/karton/Settings/icons/theme.svg");
    gtk_widget_add_css_class(glyph, "theme-choice-glyph");
    gtk_image_set_pixel_size(GTK_IMAGE(glyph), 20);
    gtk_widget_set_halign(glyph, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(glyph, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(preview), glyph);

    gtk_box_append(GTK_BOX(box), preview);

    GtkWidget *caption = gtk_label_new(label);
    gtk_widget_add_css_class(caption, "theme-choice-label");
    gtk_widget_set_halign(caption, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), caption);
    return box;
}

static GtkWidget *create_path_row(const char *title, GtkWidget **entry_out, GtkWidget **button_out, const char *button_label) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), title_lbl);

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Path to image file"));

    GtkWidget *browse_btn = gtk_button_new_with_label(_("Choose from disk"));
    GtkWidget *defaults_btn = gtk_button_new_with_label(_("Default wallpapers"));
    GtkWidget *button = gtk_button_new_with_label(button_label);
    gtk_box_append(GTK_BOX(inner), entry);
    gtk_box_append(GTK_BOX(inner), browse_btn);
    gtk_box_append(GTK_BOX(inner), defaults_btn);
    gtk_box_append(GTK_BOX(inner), button);
    gtk_box_append(GTK_BOX(row), inner);

    g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_choose_image_from_disk_clicked), entry);
    g_signal_connect(defaults_btn, "clicked", G_CALLBACK(on_choose_image_from_defaults_clicked), entry);

    *entry_out = entry;
    *button_out = button;
    return row;
}

static GtkWidget *create_status_label(void) {
    GtkWidget *label = gtk_label_new("");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_add_css_class(label, "row-subtitle");
    return label;
}

static void set_apply_status(GtkWidget *label, const char *text, gboolean is_error) {
    if (!label) {
        return;
    }

    gtk_label_set_text(GTK_LABEL(label), text ? text : "");
    gtk_widget_remove_css_class(label, "error");
    gtk_widget_remove_css_class(label, "success");
    gtk_widget_add_css_class(label, is_error ? "error" : "success");
}

static void apply_uri_setting(GSettings *settings, const char *key, const char *path_or_uri) {
    if (!settings || !path_or_uri || !*path_or_uri) {
        return;
    }

    char *uri = NULL;
    if (g_str_has_prefix(path_or_uri, "file://")) {
        uri = g_strdup(path_or_uri);
    } else {
        uri = g_filename_to_uri(path_or_uri, NULL, NULL);
    }
    if (!uri) {
        return;
    }

    g_settings_set_string(settings, key, uri);
    g_free(uri);
}

static void sync_controls_from_files(void) {
    if (g_transparency_scale) {
        char *cfg = transparency_path();
        char *raw = read_text_file_trimmed(cfg);
        g_free(cfg);

        if (raw && *raw) {
            char *end = NULL;
            long parsed = strtol(raw, &end, 10);
            if (end != raw && *end == '\0') {
                if (parsed < 0) {
                    parsed = 0;
                }
                if (parsed > 40) {
                    parsed = 40;
                }

                g_block_runtime_handlers = TRUE;
                gtk_range_set_value(GTK_RANGE(g_transparency_scale), (double)parsed);
                g_block_runtime_handlers = FALSE;
            }
        }
        g_free(raw);
    }

    if (g_wallpaper_entry) {
        char *cfg = wallpaper_override_path();
        char *path = read_text_file_trimmed(cfg);
        g_free(cfg);

        gtk_editable_set_text(GTK_EDITABLE(g_wallpaper_entry), (path && *path) ? path : "");
        g_free(path);
    }

    if (g_lockscreen_entry) {
        char *cfg = lockscreen_override_path();
        char *path = read_text_file_trimmed(cfg);
        g_free(cfg);

        gtk_editable_set_text(GTK_EDITABLE(g_lockscreen_entry), (path && *path) ? path : "");
        g_free(path);
    }

    if (g_animation_switch) {
        char *cfg = desktop_effects_path();
        char *raw = read_text_file_trimmed(cfg);
        g_free(cfg);

        if (raw && *raw) {
            gboolean enabled = FALSE;
            if (g_ascii_strcasecmp(raw, "true") == 0
                || g_ascii_strcasecmp(raw, "yes") == 0
                || g_ascii_strcasecmp(raw, "on") == 0
                || g_strcmp0(raw, "1") == 0) {
                enabled = TRUE;
            } else if (g_ascii_strcasecmp(raw, "false") == 0
                || g_ascii_strcasecmp(raw, "no") == 0
                || g_ascii_strcasecmp(raw, "off") == 0
                || g_strcmp0(raw, "0") == 0) {
                enabled = FALSE;
            } else {
                g_free(raw);
                return;
            }

            g_block_runtime_handlers = TRUE;
            gtk_switch_set_active(GTK_SWITCH(g_animation_switch), enabled);
            g_block_runtime_handlers = FALSE;
        }
        g_free(raw);
    }

}

static void read_initial_mode(void) {
    char *mode = read_current_mode();
    update_active_button(mode);
    g_free(mode);
    sync_controls_from_files();
}

static gboolean is_theme_mode_file(GFile *file) {
    if (!file) {
        return FALSE;
    }
    char *base = g_file_get_basename(file);
    gboolean match =
        g_strcmp0(base, "theme-mode") == 0 ||
        g_strcmp0(base, "window-transparency") == 0 ||
        g_strcmp0(base, "desktop-effects") == 0 ||
        g_strcmp0(base, "wallpaper-path") == 0 ||
        g_strcmp0(base, "lockscreen-path") == 0;
    g_free(base);
    return match;
}

static void on_theme_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor;
    (void)user_data;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT
        || event_type == G_FILE_MONITOR_EVENT_CREATED
        || event_type == G_FILE_MONITOR_EVENT_MOVED_IN
        || event_type == G_FILE_MONITOR_EVENT_MOVED
        || event_type == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED) {
        if (!is_theme_mode_file(file) && !is_theme_mode_file(other_file)) {
            return;
        }
        read_initial_mode();
    }
}

static void setup_file_monitor(void) {
    char *dir_path = g_build_filename(g_get_home_dir(), ".config", "karton", NULL);
    GFile *dir = g_file_new_for_path(dir_path);
    if (g_theme_monitor) {
        g_object_unref(g_theme_monitor);
        g_theme_monitor = NULL;
    }
    g_theme_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE, NULL, NULL);
    if (g_theme_monitor) {
        g_signal_connect(g_theme_monitor, "changed", G_CALLBACK(on_theme_file_changed), NULL);
    }
    g_object_unref(dir);
    g_free(dir_path);
}

static char *resolve_apply_theme_binary(void) {
    char *user_local = g_build_filename(g_get_home_dir(), ".local-karton", "bin", "karton-apply-theme", NULL);
    if (g_file_test(user_local, G_FILE_TEST_IS_EXECUTABLE)) {
        return user_local;
    }
    g_free(user_local);

    char *repo_local = g_build_filename(g_get_home_dir(), "KartONDE", "karton-session", "bin", "karton-apply-theme", NULL);
    if (g_file_test(repo_local, G_FILE_TEST_IS_EXECUTABLE)) {
        return repo_local;
    }
    g_free(repo_local);

    char *from_path = g_find_program_in_path("karton-apply-theme");
    if (from_path) {
        return from_path;
    }

    return NULL;
}

static void apply_theme_mode(const char *mode) {
    char *binary = resolve_apply_theme_binary();
    if (!binary) {
        g_warning("karton-apply-theme not found in PATH, ~/.local-karton/bin or ~/KartONDE");
        return;
    }

    gchar *argv[] = { binary, (gchar *)mode, NULL };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("Failed to spawn karton-apply-theme: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_free(binary);
        return;
    }
    g_free(binary);

    update_active_button(mode);
}

static gboolean apply_current_mode_timeout(gpointer data) {
    (void)data;
    char *mode = read_current_mode();
    apply_theme_mode(mode);
    g_free(mode);
    g_theme_apply_timeout_id = 0;
    return G_SOURCE_REMOVE;
}

static void schedule_apply_current_mode(void) {
    if (g_theme_apply_timeout_id) {
        g_source_remove(g_theme_apply_timeout_id);
        g_theme_apply_timeout_id = 0;
    }
    g_theme_apply_timeout_id = g_timeout_add(80, apply_current_mode_timeout, NULL);
}

static void on_light_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    apply_theme_mode("light");
}

static void on_dark_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    apply_theme_mode("dark");
}

static void on_auto_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    apply_theme_mode("auto");
}

static GtkWidget *create_section(const char *title, const char *description) {
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

static GtkWidget *create_slider_row(const char *title, const char *subtitle, GtkWidget *scale) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    GtkWidget *title_lbl = gtk_label_new(title);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), title_lbl);

    if (subtitle && *subtitle) {
        GtkWidget *subtitle_lbl = gtk_label_new(subtitle);
        gtk_widget_set_halign(subtitle_lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(subtitle_lbl, "row-subtitle");
        gtk_box_append(GTK_BOX(row), subtitle_lbl);
    }

    gtk_box_append(GTK_BOX(row), scale);
    return row;
}

static double normalize_text_scale_value(double value) {
    if (value < 0.85) {
        value = 0.85;
    }
    if (value > 1.35) {
        value = 1.35;
    }

    int hundredths = (int)(value * 100.0 + 0.5);
    return (double)hundredths / 100.0;
}

static void set_text_scale_value(double value, gboolean persist) {
    double normalized = normalize_text_scale_value(value);

    if (g_text_scale_slider) {
        double current = gtk_range_get_value(GTK_RANGE(g_text_scale_slider));
        double delta = current - normalized;
        if (delta < -0.0005 || delta > 0.0005) {
            g_block_runtime_handlers = TRUE;
            gtk_range_set_value(GTK_RANGE(g_text_scale_slider), normalized);
            g_block_runtime_handlers = FALSE;
        }
    }

    if (persist && g_interface_settings) {
        g_settings_set_double(g_interface_settings, "text-scaling-factor", normalized);
    }
}

static void on_transparency_changed(GtkRange *range, gpointer data) {
    (void)data;
    if (g_block_runtime_handlers) {
        return;
    }

    int val = (int)gtk_range_get_value(range);
    char *path = transparency_path();
    char *txt = g_strdup_printf("%d\n", val);

    if (write_text_file(path, txt)) {
        schedule_apply_current_mode();
    }

    g_free(txt);
    g_free(path);
}

static void on_font_changed(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    (void)data;
    if (!g_interface_settings || g_block_runtime_handlers || !g_font_dropdown) {
        return;
    }

    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(g_font_dropdown));
    guint count = g_list_model_get_n_items(G_LIST_MODEL(g_font_model));
    if (!g_font_model || count == 0 || idx >= count) {
        return;
    }

    const char *family = gtk_string_list_get_string(g_font_model, idx);
    if (!family || !*family) {
        return;
    }

    char *current = g_settings_get_string(g_interface_settings, "font-name");
    int size = read_font_size_from_setting(current, 10);
    g_free(current);

    char *font_name = g_strdup_printf("%s %d", family, size);
    g_settings_set_string(g_interface_settings, "font-name", font_name);
    g_free(font_name);
}

static gboolean signal_sessiond_wallpaper_reload(void) {
    gchar *argv[] = { "pkill", "-USR1", "-f", "karton-sessiond", NULL };
    gint status = 0;
    GError *error = NULL;

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, &error)) {
        g_clear_error(&error);
        return FALSE;
    }

    return status == 0;
}

static gboolean apply_wallpaper_with_swaybg(const char *path) {
    if (!path || !*path) {
        return FALSE;
    }

    char *swaybg = g_find_program_in_path("swaybg");
    if (!swaybg) {
        return FALSE;
    }
    g_free(swaybg);

    gchar *kill_argv[] = { "pkill", "-x", "swaybg", NULL };
    g_spawn_async(NULL, kill_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);

    gchar *argv[] = { "swaybg", "-i", (gchar *)path, "-m", "fill", NULL };
    GError *error = NULL;
    gboolean ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
    if (!ok) {
        g_clear_error(&error);
    }

    return ok;
}

static gboolean reload_wallpaper_runtime(const char *path) {
    if (signal_sessiond_wallpaper_reload()) {
        return TRUE;
    }
    return apply_wallpaper_with_swaybg(path);
}

static void on_apply_wallpaper(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    if (!g_wallpaper_entry) {
        return;
    }

    char *path = g_strdup(gtk_editable_get_text(GTK_EDITABLE(g_wallpaper_entry)));
    g_strstrip(path);

    if (!*path) {
        set_apply_status(g_wallpaper_status_label, _("Path is empty"), TRUE);
        g_free(path);
        return;
    }
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        set_apply_status(g_wallpaper_status_label, _("File not found"), TRUE);
        g_free(path);
        return;
    }

    char *cfg = wallpaper_override_path();
    if (!write_text_file(cfg, path)) {
        set_apply_status(g_wallpaper_status_label, _("Failed to save wallpaper path"), TRUE);
        g_free(cfg);
        g_free(path);
        return;
    }
    g_free(cfg);

    apply_uri_setting(g_background_settings, "picture-uri", path);
    apply_uri_setting(g_background_settings, "picture-uri-dark", path);

    if (!reload_wallpaper_runtime(path)) {
        set_apply_status(g_wallpaper_status_label, _("Wallpaper saved, but live apply failed"), TRUE);
        g_free(path);
        return;
    }

    set_apply_status(g_wallpaper_status_label, _("Wallpaper applied (OK)"), FALSE);
    g_free(path);
}

static void on_apply_lockscreen(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    if (!g_lockscreen_entry) {
        return;
    }

    char *path = g_strdup(gtk_editable_get_text(GTK_EDITABLE(g_lockscreen_entry)));
    g_strstrip(path);

    if (!*path) {
        set_apply_status(g_lockscreen_status_label, _("Path is empty"), TRUE);
        g_free(path);
        return;
    }
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        set_apply_status(g_lockscreen_status_label, _("File not found"), TRUE);
        g_free(path);
        return;
    }

    char *cfg = lockscreen_override_path();
    if (!write_text_file(cfg, path)) {
        set_apply_status(g_lockscreen_status_label, _("Failed to save lock screen path"), TRUE);
        g_free(cfg);
        g_free(path);
        return;
    }
    g_free(cfg);

    apply_uri_setting(g_screensaver_settings, "picture-uri", path);
    set_apply_status(g_lockscreen_status_label, _("Lock screen applied (OK)"), FALSE);
    g_free(path);
}

static void on_text_scale_changed(GtkRange *range, gpointer data) {
    (void)data;
    if (!g_interface_settings || g_block_runtime_handlers) {
        return;
    }

    set_text_scale_value(gtk_range_get_value(range), TRUE);
}

static void on_text_scale_default_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    (void)data;
    if (!g_interface_settings) {
        return;
    }

    set_text_scale_value(1.00, TRUE);
}

static void on_animation_toggled(GObject *obj, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    (void)data;
    if (!g_interface_settings || g_block_runtime_handlers) {
        return;
    }

    gboolean enabled = gtk_switch_get_active(GTK_SWITCH(obj));
    g_settings_set_boolean(g_interface_settings, "enable-animations", enabled);

    char *cfg = desktop_effects_path();
    if (write_text_file(cfg, enabled ? "true\n" : "false\n")) {
        schedule_apply_current_mode();
    }
    g_free(cfg);
}

static void sync_controls_from_gsettings(void) {
    if (!g_interface_settings) {
        return;
    }

    if (g_text_scale_slider) {
        set_text_scale_value(g_settings_get_double(g_interface_settings, "text-scaling-factor"), FALSE);
    }

    if (g_animation_switch) {
        g_block_runtime_handlers = TRUE;
        gtk_switch_set_active(
            GTK_SWITCH(g_animation_switch),
            g_settings_get_boolean(g_interface_settings, "enable-animations")
        );
        g_block_runtime_handlers = FALSE;
    }

    if (g_font_dropdown) {
        char *font = g_settings_get_string(g_interface_settings, "font-name");
        char *family = extract_font_family(font);
        populate_system_fonts(family);
        g_free(family);
        g_free(font);
    }
}

static void init_interface_settings(void) {
    if (schema_exists("org.gnome.desktop.interface")) {
        g_interface_settings = g_settings_new("org.gnome.desktop.interface");
    }
    if (schema_exists("org.gnome.desktop.background")) {
        g_background_settings = g_settings_new("org.gnome.desktop.background");
    }
    if (schema_exists("org.gnome.desktop.screensaver")) {
        g_screensaver_settings = g_settings_new("org.gnome.desktop.screensaver");
    }
}

static void on_page_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    if (g_theme_monitor) {
        g_object_unref(g_theme_monitor);
        g_theme_monitor = NULL;
    }
    if (g_interface_settings) {
        g_object_unref(g_interface_settings);
        g_interface_settings = NULL;
    }
    if (g_background_settings) {
        g_object_unref(g_background_settings);
        g_background_settings = NULL;
    }
    if (g_screensaver_settings) {
        g_object_unref(g_screensaver_settings);
        g_screensaver_settings = NULL;
    }
    g_wallpaper_status_label = NULL;
    g_lockscreen_status_label = NULL;
    if (g_theme_apply_timeout_id) {
        g_source_remove(g_theme_apply_timeout_id);
        g_theme_apply_timeout_id = 0;
    }
}

static GtkWidget* create_row(const char *title, GtkWidget *control) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), label);
    if (control) {
        gtk_widget_set_halign(control, GTK_ALIGN_END);
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), control);
    }
    return row;
}

GtkWidget *page_appearance_new(void) {
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

    GtkWidget *title = gtk_label_new(_("Appearance and personalization"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Configure theme and UI details so the environment works as one coherent whole."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *theme_frame = create_section(_("Light/Dark themes"), _("Changes instantly affect shell, window decorations and GTK applications."));
    GtkWidget *theme_box = gtk_frame_get_child(GTK_FRAME(theme_frame));

    GtkWidget *modes_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(modes_row, "theme-choice-row");

    g_btn_light = gtk_button_new();
    g_btn_dark = gtk_button_new();
    g_btn_auto = gtk_button_new();
    gtk_widget_add_css_class(g_btn_light, "theme-choice");
    gtk_widget_add_css_class(g_btn_dark, "theme-choice");
    gtk_widget_add_css_class(g_btn_auto, "theme-choice");
    gtk_widget_add_css_class(g_btn_light, "theme-choice-light");
    gtk_widget_add_css_class(g_btn_dark, "theme-choice-dark");
    gtk_widget_add_css_class(g_btn_auto, "theme-choice-auto");
    gtk_button_set_child(GTK_BUTTON(g_btn_light), create_theme_choice_content(_("Light")));
    gtk_button_set_child(GTK_BUTTON(g_btn_dark), create_theme_choice_content(_("Dark")));
    gtk_button_set_child(GTK_BUTTON(g_btn_auto), create_theme_choice_content(_("Auto")));

    g_signal_connect(g_btn_light, "clicked", G_CALLBACK(on_light_clicked), NULL);
    g_signal_connect(g_btn_dark, "clicked", G_CALLBACK(on_dark_clicked), NULL);
    g_signal_connect(g_btn_auto, "clicked", G_CALLBACK(on_auto_clicked), NULL);

    gtk_box_append(GTK_BOX(modes_row), g_btn_light);
    gtk_box_append(GTK_BOX(modes_row), g_btn_dark);
    gtk_box_append(GTK_BOX(modes_row), g_btn_auto);
    gtk_box_append(GTK_BOX(theme_box), modes_row);

    gtk_box_append(GTK_BOX(box), theme_frame);

    GtkWidget *window_frame = create_section(_("Window styles"), NULL);
    GtkWidget *window_box = gtk_frame_get_child(GTK_FRAME(window_frame));

    g_transparency_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 40, 1);
    gtk_scale_set_digits(GTK_SCALE(g_transparency_scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(g_transparency_scale), TRUE);
    g_signal_connect(g_transparency_scale, "value-changed", G_CALLBACK(on_transparency_changed), NULL);
    gtk_box_append(GTK_BOX(window_box), create_slider_row(_("Transparency"), _("Window and panel transparency level (0-40%)"), g_transparency_scale));

    gtk_box_append(GTK_BOX(box), window_frame);

    GtkWidget *ux_frame = create_section(_("Fonts, scaling and animations"), _("Behavior settings for GTK apps and interface."));
    GtkWidget *ux_box = gtk_frame_get_child(GTK_FRAME(ux_frame));

    g_font_model = gtk_string_list_new(NULL);
    g_font_dropdown = gtk_drop_down_new(G_LIST_MODEL(g_font_model), NULL);
    g_signal_connect(g_font_dropdown, "notify::selected", G_CALLBACK(on_font_changed), NULL);
    gtk_box_append(GTK_BOX(ux_box), create_row(_("System font"), g_font_dropdown));
    gtk_box_append(GTK_BOX(ux_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *install_font_btn = gtk_button_new_with_label(_("Install custom font"));
    g_signal_connect(install_font_btn, "clicked", G_CALLBACK(on_install_font_clicked), NULL);
    gtk_box_append(GTK_BOX(ux_box), create_row(_("Custom font"), install_font_btn));
    gtk_box_append(GTK_BOX(ux_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    g_text_scale_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.85, 1.35, 0.01);
    gtk_scale_set_digits(GTK_SCALE(g_text_scale_slider), 2);
    gtk_scale_set_draw_value(GTK_SCALE(g_text_scale_slider), TRUE);
    g_signal_connect(g_text_scale_slider, "value-changed", G_CALLBACK(on_text_scale_changed), NULL);
    gtk_box_append(GTK_BOX(ux_box), create_slider_row(_("Interface scaling"), _("Affects GTK application font size"), g_text_scale_slider));

    GtkWidget *scale_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(scale_actions, GTK_ALIGN_END);
    GtkWidget *scale_default_btn = gtk_button_new_with_label(_("Default"));
    g_signal_connect(scale_default_btn, "clicked", G_CALLBACK(on_text_scale_default_clicked), NULL);
    gtk_box_append(GTK_BOX(scale_actions), scale_default_btn);
    gtk_box_append(GTK_BOX(ux_box), scale_actions);

    gtk_box_append(GTK_BOX(ux_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    g_animation_switch = gtk_switch_new();
    g_signal_connect(g_animation_switch, "notify::active", G_CALLBACK(on_animation_toggled), NULL);
    gtk_box_append(GTK_BOX(ux_box), create_row(_("Animations"), g_animation_switch));

    gtk_box_append(GTK_BOX(box), ux_frame);

    GtkWidget *wall_frame = create_section(_("Wallpapers and lock screen"), _("Set image paths for desktop and lock screen."));
    GtkWidget *wall_box = gtk_frame_get_child(GTK_FRAME(wall_frame));
    GtkWidget *apply_wallpaper = NULL;
    GtkWidget *apply_lockscreen = NULL;

    gtk_box_append(GTK_BOX(wall_box), create_path_row(_("Wallpapers"), &g_wallpaper_entry, &apply_wallpaper, _("Apply wallpaper")));
    g_wallpaper_status_label = create_status_label();
    gtk_box_append(GTK_BOX(wall_box), g_wallpaper_status_label);
    gtk_box_append(GTK_BOX(wall_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(wall_box), create_path_row(_("Lock screen"), &g_lockscreen_entry, &apply_lockscreen, _("Apply lock screen")));
    g_lockscreen_status_label = create_status_label();
    gtk_box_append(GTK_BOX(wall_box), g_lockscreen_status_label);

    g_signal_connect(apply_wallpaper, "clicked", G_CALLBACK(on_apply_wallpaper), NULL);
    g_signal_connect(apply_lockscreen, "clicked", G_CALLBACK(on_apply_lockscreen), NULL);
    gtk_box_append(GTK_BOX(box), wall_frame);

    init_interface_settings();
    sync_controls_from_gsettings();

    read_initial_mode();
    setup_file_monitor();
    g_signal_connect(box, "destroy", G_CALLBACK(on_page_destroy), NULL);

    return outer_scroll;
}
