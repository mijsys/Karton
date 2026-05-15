#include "window.h"
#include <gio/gio.h>
#include <glib.h>
#include <libintl.h>
#include <pango/pango.h>
#include <string.h>
#include <sys/statvfs.h>

#define _(s) gettext(s)
#define N_(s) s
#define MOUSE_BUTTON_BACK 8
#define MOUSE_BUTTON_FORWARD 9
#define MOUSE_BUTTON_FORWARD_ALT 10
#define UI_ICON_SIZE 16
#define NAV_ICON_SIZE 14
#define STATUS_ICON_SIZE 14
#define PLACE_ICON_SIZE 15
#define SEARCH_MAX_RESULTS 400

#define PLACE_TOKEN_RECENT "@recent"
#define PLACE_TOKEN_FAVORITES "@favorites"

typedef struct {
    char *token;
    char *display_name;
    gboolean is_dir;
} FileItem;

typedef enum {
    ACTION_NONE = 0,
    ACTION_CREATE_FILE,
    ACTION_CREATE_FOLDER,
    ACTION_COPY,
    ACTION_MOVE
} ActionType;

typedef struct {
    ActionType type;
    char *src_token;
    char *dst_token;
    gboolean is_dir;
} ActionRecord;

typedef struct {
    GtkWidget *window;
    GtkWidget *sidebar;
    GtkWidget *breadcrumb_box;
    GtkWidget *path_entry;
    GtkWidget *flowbox;
    GtkWidget *status_label;
    GtkWidget *empty_label;
    GtkWidget *loading_overlay;
    GtkWidget *loading_spinner;
    GtkWidget *loading_label;
    GtkWidget *settings_button;
    GtkWidget *back_button;
    GtkWidget *up_button;
    GtkWidget *grid_mode_button;
    GtkWidget *list_mode_button;
    GtkAdjustment *zoom_adjustment;
    GPtrArray *history;
    guint history_index;
    char *current_token;
    char *active_item_token;
    char *selection_anchor_token;
    gboolean active_item_is_dir;
    GHashTable *selected_tokens;
    char *clipboard_token;
    gboolean clipboard_cut;
    ActionRecord last_action;
    ActionRecord redo_action;
    gboolean has_last_action;
    gboolean has_redo_action;
    gboolean show_hidden_files;
    gboolean open_files_on_single_click;
    gboolean list_view;
    int icon_size;
    gboolean suppress_sidebar_signal;
    GVolumeMonitor *volume_monitor;
} FilesState;

typedef struct {
    FilesState *state;
    GtkWidget *popover;
    char *item_token;
    gboolean item_is_dir;
} TileMenuContext;

typedef struct {
    FilesState *state;
    GtkWidget *entry;
    char *item_token;
} RenameDialogContext;

typedef struct {
    FilesState *state;
    GtkWidget *entry;
} NewFolderDialogContext;

typedef struct {
    FilesState *state;
    GtkWidget *entry;
} NewFileDialogContext;

static void on_volume_mount_done(GObject *source_object, GAsyncResult *result, gpointer user_data);

static gboolean navigate_to_token(FilesState *state, const char *raw_token, gboolean add_history);
static void refresh_current_view(FilesState *state);
static void rebuild_sidebar(FilesState *state);
static void show_new_folder_dialog(FilesState *state);
static void show_new_file_dialog(FilesState *state);
static gboolean paste_clipboard_to_current(FilesState *state);
static gboolean undo_last_action(FilesState *state);
static gboolean redo_last_action(FilesState *state);
static void on_back_clicked(GtkButton *button, gpointer user_data);
static void on_up_clicked(GtkButton *button, gpointer user_data);
static void set_show_hidden_files(FilesState *state, gboolean show_hidden_files);
static void apply_window_theme_class(GtkWidget *window);
static gboolean token_points_to_directory(const char *token);
static gboolean search_in_current_location(FilesState *state, const char *query);
static void apply_view_mode(FilesState *state);

static gboolean copy_token_to_directory(const char *src_token, const char *dest_dir_token, char **out_dst_token, gboolean *out_is_dir, GError **error);
static gboolean copy_drop_value_to_destination(FilesState *state, const GValue *value, const char *dest_dir_token);
static GdkContentProvider *on_tile_drag_prepare(GtkDragSource *source, double x, double y, gpointer user_data);
static gboolean on_tile_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data);
static gboolean on_flowbox_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data);
static void refresh_tile_selection_visuals(FilesState *state);
static void clear_selected_items(FilesState *state);
static void select_all_visible_items(FilesState *state);

static gboolean is_recent_token(const char *token) {
    return g_strcmp0(token, PLACE_TOKEN_RECENT) == 0;
}

static gboolean is_favorites_token(const char *token) {
    return g_strcmp0(token, PLACE_TOKEN_FAVORITES) == 0;
}

static gboolean is_virtual_token(const char *token) {
    return is_recent_token(token) || is_favorites_token(token);
}

static gboolean is_uri_token(const char *token) {
    return token && g_uri_peek_scheme(token) != NULL;
}

static gboolean is_local_path_token(const char *token) {
    return token && g_path_is_absolute(token);
}

static gboolean is_browsable_token(const char *token) {
    return is_local_path_token(token) || is_uri_token(token);
}

static gboolean is_trash_token(const char *token) {
    if (!token || !is_local_path_token(token)) {
        return FALSE;
    }

    char *trash_root = g_build_filename(g_get_home_dir(), ".local", "share", "Trash", "files", NULL);
    size_t root_len = strlen(trash_root);
    gboolean is_match = g_str_has_prefix(token, trash_root)
        && (token[root_len] == '\0' || token[root_len] == '/');
    g_free(trash_root);

    return is_match;
}

static GFile *file_from_token(const char *token) {
    if (!token) {
        return NULL;
    }

    if (is_local_path_token(token)) {
        return g_file_new_for_path(token);
    }

    if (is_uri_token(token)) {
        return g_file_new_for_uri(token);
    }

    return NULL;
}

static char *token_from_file(GFile *file) {
    if (!file) {
        return NULL;
    }

    char *path = g_file_get_path(file);
    if (path) {
        return path;
    }

    return g_file_get_uri(file);
}

static GtkWidget *create_icon_button(const char *icon_name, const char *tooltip) {
    GtkWidget *button = gtk_button_new();
    GtkWidget *icon = NULL;

    if (icon_name && g_str_has_prefix(icon_name, "/io/karton/Files/icons/")) {
        icon = gtk_picture_new_for_resource(icon_name);
        gtk_picture_set_content_fit(GTK_PICTURE(icon), GTK_CONTENT_FIT_CONTAIN);
        gtk_picture_set_can_shrink(GTK_PICTURE(icon), TRUE);
        gtk_widget_set_size_request(icon, NAV_ICON_SIZE, NAV_ICON_SIZE);
    } else {
        icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), NAV_ICON_SIZE);
    }

    gtk_widget_add_css_class(icon, "toolbar-icon");

    gtk_button_set_child(GTK_BUTTON(button), icon);
    gtk_widget_add_css_class(button, "nav-button");

    if (tooltip && *tooltip) {
        gtk_widget_set_tooltip_text(button, tooltip);
    }

    return button;
}

static void open_directory_in_new_tab(FilesState *state, const char *token) {
    if (!state || !token || !token_points_to_directory(token)) {
        return;
    }

    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(state->window));
    if (!app) {
        return;
    }

    GtkWidget *new_window = karton_files_window_new(app);
    FilesState *new_state = g_object_get_data(G_OBJECT(new_window), "files-state");
    if (new_state) {
        navigate_to_token(new_state, token, TRUE);
    }
    gtk_window_present(GTK_WINDOW(new_window));
}

static void file_item_free(gpointer data) {
    FileItem *item = data;
    if (!item) {
        return;
    }

    g_free(item->token);
    g_free(item->display_name);
    g_free(item);
}

static gint file_item_compare(gconstpointer a, gconstpointer b) {
    const FileItem *left = *(const FileItem * const *)a;
    const FileItem *right = *(const FileItem * const *)b;

    if (!left && !right) {
        return 0;
    }
    if (!left) {
        return 1;
    }
    if (!right) {
        return -1;
    }

    if (left->is_dir != right->is_dir) {
        return left->is_dir ? -1 : 1;
    }

    const char *left_name = left->display_name ? left->display_name : "";
    const char *right_name = right->display_name ? right->display_name : "";
    return g_utf8_collate(left_name, right_name);
}

static void clear_flowbox(FilesState *state) {
    GtkWidget *child = gtk_widget_get_first_child(state->flowbox);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_flow_box_remove(GTK_FLOW_BOX(state->flowbox), child);
        child = next;
    }
}

static void clear_box(GtkWidget *box) {
    GtkWidget *child = gtk_widget_get_first_child(box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (GTK_IS_LIST_BOX(box)) {
            gtk_list_box_remove(GTK_LIST_BOX(box), child);
        } else {
            gtk_box_remove(GTK_BOX(box), child);
        }
        child = next;
    }
}

static void action_record_clear(ActionRecord *record) {
    if (!record) {
        return;
    }

    g_free(record->src_token);
    g_free(record->dst_token);
    record->src_token = NULL;
    record->dst_token = NULL;
    record->is_dir = FALSE;
    record->type = ACTION_NONE;
}

static void action_record_copy(ActionRecord *dest, const ActionRecord *src) {
    action_record_clear(dest);
    if (!src) {
        return;
    }

    dest->type = src->type;
    dest->src_token = g_strdup(src->src_token);
    dest->dst_token = g_strdup(src->dst_token);
    dest->is_dir = src->is_dir;
}

static void set_selection_anchor(FilesState *state, const char *token) {
    if (!state) {
        return;
    }

    g_free(state->selection_anchor_token);
    state->selection_anchor_token = (token && *token) ? g_strdup(token) : NULL;
}

static void remember_action(FilesState *state, ActionType type, const char *src_token, const char *dst_token, gboolean is_dir) {
    action_record_clear(&state->last_action);
    state->last_action.type = type;
    state->last_action.src_token = g_strdup(src_token);
    state->last_action.dst_token = g_strdup(dst_token);
    state->last_action.is_dir = is_dir;
    state->has_last_action = TRUE;

    action_record_clear(&state->redo_action);
    state->has_redo_action = FALSE;
}

static void set_active_item(FilesState *state, const char *token, gboolean is_dir) {
    g_free(state->active_item_token);
    state->active_item_token = g_strdup(token);
    state->active_item_is_dir = is_dir;
}

static GHashTable *ensure_selected_tokens(FilesState *state) {
    if (!state) {
        return NULL;
    }

    if (!state->selected_tokens) {
        state->selected_tokens = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }

    return state->selected_tokens;
}

static guint selected_items_count(FilesState *state) {
    if (!state || !state->selected_tokens) {
        return 0;
    }

    return g_hash_table_size(state->selected_tokens);
}

static gboolean is_item_selected(FilesState *state, const char *token) {
    if (!state || !state->selected_tokens || !token || !*token) {
        return FALSE;
    }

    return g_hash_table_contains(state->selected_tokens, token);
}

static void clear_selected_items(FilesState *state) {
    if (!state || !state->selected_tokens) {
        return;
    }

    g_hash_table_remove_all(state->selected_tokens);
}

static void refresh_tile_selection_visuals(FilesState *state) {
    if (!state || !state->flowbox) {
        return;
    }

    GtkWidget *child = gtk_widget_get_first_child(state->flowbox);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        GtkWidget *tile = GTK_IS_FLOW_BOX_CHILD(child) ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child)) : child;

        if (tile) {
            const char *token = g_object_get_data(G_OBJECT(tile), "item-token");
            if (is_item_selected(state, token)) {
                gtk_widget_add_css_class(tile, "file-tile-selected");
            } else {
                gtk_widget_remove_css_class(tile, "file-tile-selected");
            }
        }

        child = next;
    }
}

static void select_single_item(FilesState *state, const char *token, gboolean is_dir) {
    if (!state || !token || !*token) {
        return;
    }

    GHashTable *selected = ensure_selected_tokens(state);
    if (!selected) {
        return;
    }

    g_hash_table_remove_all(selected);
    g_hash_table_add(selected, g_strdup(token));
    set_active_item(state, token, is_dir);
    set_selection_anchor(state, token);
}

static gboolean select_range_between_tokens(FilesState *state,
                                            const char *anchor_token,
                                            const char *target_token,
                                            gboolean preserve_existing) {
    if (!state || !state->flowbox || !anchor_token || !*anchor_token || !target_token || !*target_token) {
        return FALSE;
    }

    GHashTable *selected = ensure_selected_tokens(state);
    if (!selected) {
        return FALSE;
    }

    gint anchor_index = -1;
    gint target_index = -1;
    gint index = 0;

    GtkWidget *child = gtk_widget_get_first_child(state->flowbox);
    while (child) {
        GtkWidget *tile = GTK_IS_FLOW_BOX_CHILD(child) ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child)) : child;
        const char *token = tile ? g_object_get_data(G_OBJECT(tile), "item-token") : NULL;

        if (token && anchor_index < 0 && g_strcmp0(token, anchor_token) == 0) {
            anchor_index = index;
        }
        if (token && target_index < 0 && g_strcmp0(token, target_token) == 0) {
            target_index = index;
        }

        child = gtk_widget_get_next_sibling(child);
        index++;
    }

    if (anchor_index < 0 || target_index < 0) {
        return FALSE;
    }

    gint start = MIN(anchor_index, target_index);
    gint end = MAX(anchor_index, target_index);

    if (!preserve_existing) {
        g_hash_table_remove_all(selected);
    }

    gboolean target_is_dir = FALSE;
    index = 0;
    child = gtk_widget_get_first_child(state->flowbox);
    while (child) {
        GtkWidget *tile = GTK_IS_FLOW_BOX_CHILD(child) ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child)) : child;
        const char *token = tile ? g_object_get_data(G_OBJECT(tile), "item-token") : NULL;
        gboolean is_dir = tile ? GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tile), "item-is-dir")) : FALSE;

        if (index >= start && index <= end && token && *token) {
            g_hash_table_add(selected, g_strdup(token));
        }

        if (token && g_strcmp0(token, target_token) == 0) {
            target_is_dir = is_dir;
        }

        child = gtk_widget_get_next_sibling(child);
        index++;
    }

    set_active_item(state, target_token, target_is_dir);
    return TRUE;
}

static gboolean toggle_item_selection(FilesState *state, const char *token, gboolean is_dir) {
    if (!state || !token || !*token) {
        return FALSE;
    }

    GHashTable *selected = ensure_selected_tokens(state);
    if (!selected) {
        return FALSE;
    }

    if (g_hash_table_contains(selected, token)) {
        g_hash_table_remove(selected, token);
        if (g_strcmp0(state->active_item_token, token) == 0) {
            set_active_item(state, NULL, FALSE);
        }
        return FALSE;
    }

    g_hash_table_add(selected, g_strdup(token));
    set_active_item(state, token, is_dir);
    return TRUE;
}

static void select_all_visible_items(FilesState *state) {
    if (!state || !state->flowbox) {
        return;
    }

    GHashTable *selected = ensure_selected_tokens(state);
    if (!selected) {
        return;
    }

    g_hash_table_remove_all(selected);

    const char *first_token = NULL;
    gboolean first_is_dir = FALSE;

    GtkWidget *child = gtk_widget_get_first_child(state->flowbox);
    while (child) {
        GtkWidget *tile = GTK_IS_FLOW_BOX_CHILD(child) ? gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child)) : child;
        if (tile) {
            const char *token = g_object_get_data(G_OBJECT(tile), "item-token");
            gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tile), "item-is-dir"));
            if (token && *token) {
                g_hash_table_add(selected, g_strdup(token));
                if (!first_token) {
                    first_token = token;
                    first_is_dir = is_dir;
                }
            }
        }
        child = gtk_widget_get_next_sibling(child);
    }

    if (first_token) {
        set_active_item(state, first_token, first_is_dir);
        set_selection_anchor(state, first_token);
    }

    refresh_tile_selection_visuals(state);

    guint count = selected_items_count(state);
    char *msg = g_strdup_printf(
        ngettext("%u item selected", "%u items selected", count),
        count
    );
    gtk_label_set_text(GTK_LABEL(state->status_label), msg);
    g_free(msg);
}

static void set_clipboard_item(FilesState *state, const char *token, gboolean cut) {
    g_free(state->clipboard_token);
    state->clipboard_token = g_strdup(token);
    state->clipboard_cut = cut;
}

static gboolean query_is_directory_token(const char *token) {
    GFile *file = file_from_token(token);
    if (!file) {
        return FALSE;
    }

    GError *error = NULL;
    GFileInfo *info = g_file_query_info(file, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, &error);
    g_clear_error(&error);

    gboolean is_dir = FALSE;
    if (info) {
        is_dir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        g_object_unref(info);
    }

    g_object_unref(file);
    return is_dir;
}

static gboolean delete_recursive_file(GFile *file, GError **error) {
    if (!file) {
        return FALSE;
    }

    if (!g_file_query_exists(file, NULL)) {
        return TRUE;
    }

    GFileInfo *info = g_file_query_info(file, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!info) {
        return FALSE;
    }

    GFileType type = g_file_info_get_file_type(info);
    g_object_unref(info);

    if (type == G_FILE_TYPE_DIRECTORY) {
        GFileEnumerator *enumerator = g_file_enumerate_children(
            file,
            "standard::name",
            G_FILE_QUERY_INFO_NONE,
            NULL,
            error
        );

        if (!enumerator) {
            return FALSE;
        }

        GFileInfo *child_info = NULL;
        while ((child_info = g_file_enumerator_next_file(enumerator, NULL, error)) != NULL) {
            const char *name = g_file_info_get_name(child_info);
            GFile *child = g_file_get_child(file, name);

            gboolean ok = delete_recursive_file(child, error);
            g_object_unref(child);
            g_object_unref(child_info);

            if (!ok) {
                g_object_unref(enumerator);
                return FALSE;
            }
        }

        if (*error) {
            g_object_unref(enumerator);
            return FALSE;
        }

        g_object_unref(enumerator);
    }

    return g_file_delete(file, NULL, error);
}

static gboolean copy_recursive_file(GFile *src, GFile *dst, GError **error) {
    GFileInfo *info = g_file_query_info(src, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!info) {
        return FALSE;
    }

    GFileType type = g_file_info_get_file_type(info);
    g_object_unref(info);

    if (type == G_FILE_TYPE_DIRECTORY) {
        if (!g_file_make_directory(dst, NULL, error)) {
            return FALSE;
        }

        GFileEnumerator *enumerator = g_file_enumerate_children(
            src,
            "standard::name",
            G_FILE_QUERY_INFO_NONE,
            NULL,
            error
        );
        if (!enumerator) {
            return FALSE;
        }

        GFileInfo *child_info = NULL;
        while ((child_info = g_file_enumerator_next_file(enumerator, NULL, error)) != NULL) {
            const char *name = g_file_info_get_name(child_info);
            GFile *src_child = g_file_get_child(src, name);
            GFile *dst_child = g_file_get_child(dst, name);

            gboolean ok = copy_recursive_file(src_child, dst_child, error);
            g_object_unref(src_child);
            g_object_unref(dst_child);
            g_object_unref(child_info);

            if (!ok) {
                g_object_unref(enumerator);
                return FALSE;
            }
        }

        if (*error) {
            g_object_unref(enumerator);
            return FALSE;
        }

        g_object_unref(enumerator);
        return TRUE;
    }

    return g_file_copy(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
}

static gboolean create_empty_file_at_token(const char *token, GError **error) {
    GFile *file = file_from_token(token);
    if (!file) {
        return FALSE;
    }

    GFileOutputStream *stream = g_file_create(file, G_FILE_CREATE_NONE, NULL, error);
    if (!stream) {
        g_object_unref(file);
        return FALSE;
    }

    g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, NULL);
    g_object_unref(stream);
    g_object_unref(file);
    return TRUE;
}

static void set_loading_state(FilesState *state, gboolean loading, const char *message) {
    if (!state->loading_overlay) {
        return;
    }

    if (message && state->loading_label) {
        gtk_label_set_text(GTK_LABEL(state->loading_label), message);
    }

    gtk_widget_set_visible(state->loading_overlay, loading);

    if (state->loading_spinner) {
        if (loading) {
            gtk_spinner_start(GTK_SPINNER(state->loading_spinner));
        } else {
            gtk_spinner_stop(GTK_SPINNER(state->loading_spinner));
        }
    }

    if (loading) {
        gtk_widget_set_visible(state->empty_label, FALSE);
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static void set_status_text(FilesState *state, guint item_count, const char *token_for_space) {
    char *count_text = g_strdup_printf(ngettext("%u item", "%u items", item_count), item_count);
    char *free_space = NULL;

    if (token_for_space && is_local_path_token(token_for_space)) {
        struct statvfs stats;
        if (statvfs(token_for_space, &stats) == 0) {
            guint64 bytes = (guint64)stats.f_bavail * (guint64)stats.f_frsize;
            free_space = g_format_size(bytes);
        }
    }

    char *full_text = NULL;
    if (free_space) {
        full_text = g_strdup_printf(_("%s, free space: %s"), count_text, free_space);
    } else {
        full_text = g_strdup(count_text);
    }

    gtk_label_set_text(GTK_LABEL(state->status_label), full_text);

    g_free(count_text);
    g_free(free_space);
    g_free(full_text);
}

static void set_empty_message(FilesState *state, const char *token) {
    if (is_recent_token(token)) {
        gtk_label_set_text(GTK_LABEL(state->empty_label), _("No recent files."));
        return;
    }

    if (is_favorites_token(token)) {
        gtk_label_set_text(GTK_LABEL(state->empty_label), _("No favorite locations."));
        return;
    }

    gtk_label_set_text(GTK_LABEL(state->empty_label), _("This folder is empty."));
}

static gboolean input_is_explicit_path(const char *input) {
    if (!input || !*input) {
        return FALSE;
    }

    return g_strcmp0(input, "~") == 0
        || g_str_has_prefix(input, "~/")
        || g_str_has_prefix(input, "/")
        || strchr(input, '/') != NULL
        || g_str_has_prefix(input, "./")
        || g_str_has_prefix(input, "../")
        || g_strcmp0(input, "recent://") == 0
        || g_strcmp0(input, "favorites://") == 0
        || is_uri_token(input)
        || is_recent_token(input)
        || is_favorites_token(input);
}

static void update_location_entry(FilesState *state, const char *token) {
    if (is_recent_token(token)) {
        gtk_editable_set_text(GTK_EDITABLE(state->path_entry), "recent://");
        return;
    }

    if (is_favorites_token(token)) {
        gtk_editable_set_text(GTK_EDITABLE(state->path_entry), "favorites://");
        return;
    }

    gtk_editable_set_text(GTK_EDITABLE(state->path_entry), token ? token : "");
}

static void update_navigation_buttons(FilesState *state) {
    gboolean has_back = state->history && state->history->len > 1 && state->history_index > 0;
    gtk_widget_set_sensitive(state->back_button, has_back);

    gboolean can_go_up = FALSE;
    if (state->current_token) {
        if (is_local_path_token(state->current_token) && g_strcmp0(state->current_token, "/") != 0) {
            can_go_up = TRUE;
        } else if (is_uri_token(state->current_token)) {
            GFile *file = g_file_new_for_uri(state->current_token);
            GFile *parent = g_file_get_parent(file);
            can_go_up = (parent != NULL);
            g_clear_object(&parent);
            g_object_unref(file);
        }
    }

    gtk_widget_set_sensitive(state->up_button, can_go_up);
}

static void on_breadcrumb_clicked(GtkButton *button, gpointer user_data) {
    FilesState *state = user_data;
    const char *token = g_object_get_data(G_OBJECT(button), "crumb-token");
    if (!token) {
        return;
    }

    navigate_to_token(state, token, TRUE);
}

static GtkWidget *create_breadcrumb_button(FilesState *state, const char *label, const char *token) {
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "breadcrumb-button");
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    g_object_set_data_full(G_OBJECT(button), "crumb-token", g_strdup(token), g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(on_breadcrumb_clicked), state);
    return button;
}

static void append_breadcrumb_separator(GtkWidget *box) {
    GtkWidget *sep = gtk_label_new(">");
    gtk_widget_add_css_class(sep, "breadcrumb-separator");
    gtk_box_append(GTK_BOX(box), sep);
}

static void rebuild_breadcrumb(FilesState *state, const char *token) {
    clear_box(state->breadcrumb_box);

    if (is_recent_token(token)) {
        GtkWidget *label = gtk_label_new(_("Recent files"));
        gtk_widget_add_css_class(label, "breadcrumb-current");
        gtk_box_append(GTK_BOX(state->breadcrumb_box), label);
        return;
    }

    if (is_favorites_token(token)) {
        GtkWidget *label = gtk_label_new(_("Favorite locations"));
        gtk_widget_add_css_class(label, "breadcrumb-current");
        gtk_box_append(GTK_BOX(state->breadcrumb_box), label);
        return;
    }

    if (is_uri_token(token)) {
        GtkWidget *label = gtk_label_new(token);
        gtk_widget_add_css_class(label, "breadcrumb-current");
        gtk_box_append(GTK_BOX(state->breadcrumb_box), label);
        return;
    }

    if (!is_local_path_token(token)) {
        GtkWidget *label = gtk_label_new(_("Files"));
        gtk_widget_add_css_class(label, "breadcrumb-current");
        gtk_box_append(GTK_BOX(state->breadcrumb_box), label);
        return;
    }

    GtkWidget *root_button = create_breadcrumb_button(state, "/", "/");
    gtk_box_append(GTK_BOX(state->breadcrumb_box), root_button);

    if (g_strcmp0(token, "/") == 0) {
        return;
    }

    char **parts = g_strsplit(token, "/", -1);
    char *accumulator = g_strdup("/");

    for (guint i = 0; parts[i] != NULL; i++) {
        if (!parts[i][0]) {
            continue;
        }

        char *next = NULL;
        if (g_strcmp0(accumulator, "/") == 0) {
            next = g_build_filename("/", parts[i], NULL);
        } else {
            next = g_build_filename(accumulator, parts[i], NULL);
        }

        append_breadcrumb_separator(state->breadcrumb_box);
        GtkWidget *crumb = create_breadcrumb_button(state, parts[i], next);
        gtk_box_append(GTK_BOX(state->breadcrumb_box), crumb);

        g_free(accumulator);
        accumulator = next;
    }

    g_free(accumulator);
    g_strfreev(parts);
}

static void sync_sidebar_selection(FilesState *state, const char *token) {
    state->suppress_sidebar_signal = TRUE;

    GtkListBoxRow *found = NULL;
    GtkWidget *row_widget = gtk_widget_get_first_child(state->sidebar);
    while (row_widget) {
        const char *row_token = g_object_get_data(G_OBJECT(row_widget), "place-token");
        if (row_token && g_strcmp0(row_token, token) == 0) {
            found = GTK_LIST_BOX_ROW(row_widget);
            break;
        }
        row_widget = gtk_widget_get_next_sibling(row_widget);
    }

    if (found) {
        gtk_list_box_select_row(GTK_LIST_BOX(state->sidebar), found);
    } else {
        gtk_list_box_unselect_all(GTK_LIST_BOX(state->sidebar));
    }

    state->suppress_sidebar_signal = FALSE;
}

static char *resolve_token_from_input(FilesState *state, const char *raw_input) {
    if (!raw_input || !*raw_input) {
        return NULL;
    }

    if (g_strcmp0(raw_input, "recent://") == 0 || is_recent_token(raw_input)) {
        return g_strdup(PLACE_TOKEN_RECENT);
    }

    if (g_strcmp0(raw_input, "favorites://") == 0 || is_favorites_token(raw_input)) {
        return g_strdup(PLACE_TOKEN_FAVORITES);
    }

    if (g_strcmp0(raw_input, "~") == 0) {
        return g_strdup(g_get_home_dir());
    }

    if (g_str_has_prefix(raw_input, "~/")) {
        char *expanded = g_build_filename(g_get_home_dir(), raw_input + 2, NULL);
        char *canonical = g_canonicalize_filename(expanded, NULL);
        g_free(expanded);
        return canonical;
    }

    if (is_uri_token(raw_input)) {
        return g_strdup(raw_input);
    }

    if (is_local_path_token(raw_input)) {
        return g_canonicalize_filename(raw_input, NULL);
    }

    if (state->current_token && is_browsable_token(state->current_token)) {
        GFile *base = file_from_token(state->current_token);
        GFile *child = g_file_resolve_relative_path(base, raw_input);
        char *token = token_from_file(child);
        g_object_unref(child);
        g_object_unref(base);

        if (token && is_local_path_token(token)) {
            char *canonical = g_canonicalize_filename(token, NULL);
            g_free(token);
            token = canonical;
        }
        return token;
    }

    char *joined = g_build_filename(g_get_home_dir(), raw_input, NULL);
    char *canonical = g_canonicalize_filename(joined, NULL);
    g_free(joined);
    return canonical;
}

static gboolean token_points_to_directory(const char *token) {
    if (!token || !is_browsable_token(token)) {
        return FALSE;
    }

    GFile *file = file_from_token(token);
    GError *error = NULL;
    GFileInfo *info = g_file_query_info(file, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, &error);
    g_clear_error(&error);

    gboolean is_dir = FALSE;
    if (info) {
        is_dir = (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY);
        g_object_unref(info);
    }

    g_object_unref(file);
    return is_dir;
}

static gboolean launch_executable_file(GFile *file, GError **error) {
    if (!file) {
        return FALSE;
    }

    char *path = g_file_get_path(file);
    if (!path || !*path) {
        g_free(path);
        return FALSE;
    }

    char *workdir = g_path_get_dirname(path);
    char *argv[] = {path, NULL};

    gboolean launched = g_spawn_async(
        workdir,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        error
    );

    g_free(workdir);
    g_free(path);
    return launched;
}

static gboolean open_file_with_association(FilesState *state, const char *token) {
    GFile *file = file_from_token(token);
    if (!file) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Cannot open this location."));
        return FALSE;
    }

    GError *error = NULL;
    GFileInfo *info = g_file_query_info(
        file,
        "standard::content-type,standard::type,access::can-execute",
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &error
    );
    g_clear_error(&error);

    const char *content_type = NULL;
    GFileType file_type = G_FILE_TYPE_UNKNOWN;
    gboolean can_execute = FALSE;
    if (info) {
        content_type = g_file_info_get_content_type(info);
        file_type = g_file_info_get_file_type(info);
        can_execute = g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
    }

    gboolean launched = FALSE;
    GError *exec_error = NULL;

    if (file_type == G_FILE_TYPE_REGULAR && can_execute && is_local_path_token(token)) {
        launched = launch_executable_file(file, &exec_error);
    }

    if (!launched && content_type) {
        GAppInfo *app = g_app_info_get_default_for_type(content_type, FALSE);
        if (app) {
            GList *files = NULL;
            files = g_list_append(files, file);
            launched = g_app_info_launch(app, files, NULL, &error);
            g_list_free(files);
            g_object_unref(app);
        }
    }

    if (!launched) {
        char *uri = g_file_get_uri(file);
        launched = g_app_info_launch_default_for_uri(uri, NULL, &error);
        g_free(uri);
    }

    if (!launched) {
        gtk_label_set_text(
            GTK_LABEL(state->status_label),
            exec_error ? exec_error->message : (error ? error->message : _("No application is associated with this file type."))
        );
    }

    g_clear_error(&exec_error);
    g_clear_error(&error);
    g_clear_object(&info);
    g_object_unref(file);
    return launched;
}

static void refresh_current_view(FilesState *state) {
    if (!state->current_token) {
        return;
    }
    navigate_to_token(state, state->current_token, FALSE);
}

static void tile_menu_context_free(gpointer data) {
    TileMenuContext *ctx = data;
    if (!ctx) {
        return;
    }

    g_free(ctx->item_token);
    g_free(ctx);
}

static TileMenuContext *popover_menu_context(GtkWidget *popover) {
    return g_object_get_data(G_OBJECT(popover), "tile-menu-context");
}

static void on_rename_dialog_response(GtkDialog *dialog, int response, gpointer user_data) {
    RenameDialogContext *ctx = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        char *name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(ctx->entry)));
        g_strstrip(name);

        if (!*name) {
            gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Name cannot be empty."));
            g_free(name);
            gtk_window_destroy(GTK_WINDOW(dialog));
            g_free(ctx->item_token);
            g_free(ctx);
            return;
        }

        GFile *src = file_from_token(ctx->item_token);
        GFile *parent = src ? g_file_get_parent(src) : NULL;

        if (!src || !parent) {
            gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Rename failed."));
            g_clear_object(&parent);
            g_clear_object(&src);
            g_free(name);
            gtk_window_destroy(GTK_WINDOW(dialog));
            g_free(ctx->item_token);
            g_free(ctx);
            return;
        }

        GFile *dst = g_file_get_child(parent, name);
        GError *error = NULL;
        char *src_token = token_from_file(src);
        char *dst_token = token_from_file(dst);

        if (!g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
            gtk_label_set_text(GTK_LABEL(ctx->state->status_label), error ? error->message : _("Rename failed."));
            g_clear_error(&error);
        } else {
            remember_action(ctx->state, ACTION_MOVE, src_token, dst_token, ctx->state->active_item_is_dir);
            set_active_item(ctx->state, dst_token, ctx->state->active_item_is_dir);
            gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Item renamed."));
            refresh_current_view(ctx->state);
        }

        g_free(src_token);
        g_free(dst_token);

        g_object_unref(dst);
        g_object_unref(parent);
        g_object_unref(src);
        g_free(name);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_free(ctx->item_token);
    g_free(ctx);
}

static void on_new_folder_dialog_response(GtkDialog *dialog, int response, gpointer user_data) {
    NewFolderDialogContext *ctx = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        if (!ctx->state->current_token || !is_browsable_token(ctx->state->current_token)) {
            gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Current location does not support creating folders."));
        } else {
            char *name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(ctx->entry)));
            g_strstrip(name);

            if (!*name) {
                gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Name cannot be empty."));
            } else {
                GFile *parent = file_from_token(ctx->state->current_token);
                GFile *folder = g_file_get_child(parent, name);
                GError *error = NULL;
                char *folder_token = token_from_file(folder);

                if (!g_file_make_directory(folder, NULL, &error)) {
                    gtk_label_set_text(GTK_LABEL(ctx->state->status_label), error ? error->message : _("Cannot create folder."));
                    g_clear_error(&error);
                } else {
                    remember_action(ctx->state, ACTION_CREATE_FOLDER, NULL, folder_token, TRUE);
                    set_active_item(ctx->state, folder_token, TRUE);
                    gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Folder created."));
                    refresh_current_view(ctx->state);
                }

                g_free(folder_token);
                g_object_unref(folder);
                g_object_unref(parent);
            }

            g_free(name);
        }
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_free(ctx);
}

static void show_new_folder_dialog(FilesState *state) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("Create folder"),
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Create"), GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(_("Folder name"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_box_append(GTK_BOX(content), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), _("New folder"));
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_box_append(GTK_BOX(content), entry);

    NewFolderDialogContext *dialog_ctx = g_new0(NewFolderDialogContext, 1);
    dialog_ctx->state = state;
    dialog_ctx->entry = entry;

    g_signal_connect(dialog, "response", G_CALLBACK(on_new_folder_dialog_response), dialog_ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_new_file_dialog_response(GtkDialog *dialog, int response, gpointer user_data) {
    NewFileDialogContext *ctx = user_data;

    if (response == GTK_RESPONSE_ACCEPT) {
        if (!ctx->state->current_token || !is_browsable_token(ctx->state->current_token)) {
            gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Current location does not support creating files."));
        } else {
            char *name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(ctx->entry)));
            g_strstrip(name);

            if (!*name) {
                gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Name cannot be empty."));
            } else {
                GFile *parent = file_from_token(ctx->state->current_token);
                GFile *file = g_file_get_child(parent, name);
                char *file_token = token_from_file(file);
                GError *error = NULL;

                if (!create_empty_file_at_token(file_token, &error)) {
                    gtk_label_set_text(GTK_LABEL(ctx->state->status_label), error ? error->message : _("Cannot create file."));
                    g_clear_error(&error);
                } else {
                    remember_action(ctx->state, ACTION_CREATE_FILE, NULL, file_token, FALSE);
                    set_active_item(ctx->state, file_token, FALSE);
                    gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("File created."));
                    refresh_current_view(ctx->state);
                }

                g_free(file_token);
                g_object_unref(file);
                g_object_unref(parent);
            }

            g_free(name);
        }
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    g_free(ctx);
}

static void show_new_file_dialog(FilesState *state) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("Create file"),
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Create"), GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(_("File name"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_box_append(GTK_BOX(content), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), _("New file"));
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_box_append(GTK_BOX(content), entry);

    NewFileDialogContext *dialog_ctx = g_new0(NewFileDialogContext, 1);
    dialog_ctx->state = state;
    dialog_ctx->entry = entry;

    g_signal_connect(dialog, "response", G_CALLBACK(on_new_file_dialog_response), dialog_ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void copy_active_item(FilesState *state) {
    if (!state->active_item_token) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Select an item first."));
        return;
    }

    set_clipboard_item(state, state->active_item_token, FALSE);
    gtk_label_set_text(GTK_LABEL(state->status_label), _("Copied to clipboard."));
}

static void cut_active_item(FilesState *state) {
    if (!state->active_item_token) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Select an item first."));
        return;
    }

    set_clipboard_item(state, state->active_item_token, TRUE);
    gtk_label_set_text(GTK_LABEL(state->status_label), _("Cut to clipboard."));
}

static gboolean move_active_to_trash(FilesState *state) {
    if (!state->active_item_token) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Select an item first."));
        return FALSE;
    }

    GFile *file = file_from_token(state->active_item_token);
    if (!file) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Delete failed."));
        return FALSE;
    }

    GError *error = NULL;
    gboolean ok = FALSE;

    if (is_trash_token(state->current_token)) {
        ok = delete_recursive_file(file, &error);
    } else {
        ok = g_file_trash(file, NULL, &error);
        if (!ok) {
            g_clear_error(&error);
            ok = g_file_delete(file, NULL, &error);
        }
    }

    if (!ok) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error ? error->message : _("Delete failed."));
        g_clear_error(&error);
        g_object_unref(file);
        return FALSE;
    }

    gtk_label_set_text(
        GTK_LABEL(state->status_label),
        is_trash_token(state->current_token) ? _("Deleted from trash.") : _("Moved to trash.")
    );
    set_active_item(state, NULL, FALSE);
    g_object_unref(file);
    refresh_current_view(state);
    return TRUE;
}

static gboolean paste_clipboard_to_current(FilesState *state) {
    if (!state->clipboard_token) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Nothing to paste."));
        return FALSE;
    }

    if (!state->current_token || !is_browsable_token(state->current_token)) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Current location does not support paste."));
        return FALSE;
    }

    GFile *src = file_from_token(state->clipboard_token);
    GFile *dest_dir = file_from_token(state->current_token);
    if (!src || !dest_dir) {
        g_clear_object(&src);
        g_clear_object(&dest_dir);
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Paste failed."));
        return FALSE;
    }

    char *basename = g_file_get_basename(src);
    GFile *dst = g_file_get_child(dest_dir, basename);
    char *src_token = token_from_file(src);
    char *dst_token = token_from_file(dst);
    gboolean is_dir = query_is_directory_token(src_token);
    gboolean success = FALSE;
    GError *error = NULL;

    if (g_file_equal(src, dst)) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Source and destination are the same."));
        goto cleanup;
    }

    if (state->clipboard_cut) {
        success = g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
        if (success) {
            remember_action(state, ACTION_MOVE, src_token, dst_token, is_dir);
            set_clipboard_item(state, NULL, FALSE);
        }
    } else {
        success = copy_recursive_file(src, dst, &error);
        if (success) {
            remember_action(state, ACTION_COPY, src_token, dst_token, is_dir);
        }
    }

    if (!success) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error ? error->message : _("Paste failed."));
        g_clear_error(&error);
        goto cleanup;
    }

    set_active_item(state, dst_token, is_dir);
    gtk_label_set_text(GTK_LABEL(state->status_label), _("Paste complete."));
    refresh_current_view(state);

cleanup:
    g_free(basename);
    g_free(src_token);
    g_free(dst_token);
    g_object_unref(dst);
    g_object_unref(dest_dir);
    g_object_unref(src);
    return success;
}

static gboolean copy_token_to_directory(const char *src_token,
                                        const char *dest_dir_token,
                                        char **out_dst_token,
                                        gboolean *out_is_dir,
                                        GError **error) {
    if (!src_token || !dest_dir_token || !is_browsable_token(src_token) || !is_browsable_token(dest_dir_token)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Paste failed."));
        return FALSE;
    }

    GFile *src = file_from_token(src_token);
    GFile *dest_dir = file_from_token(dest_dir_token);
    if (!src || !dest_dir) {
        g_clear_object(&src);
        g_clear_object(&dest_dir);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Paste failed."));
        return FALSE;
    }

    GError *query_error = NULL;
    GFileInfo *dest_info = g_file_query_info(dest_dir, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, &query_error);
    if (!dest_info) {
        g_propagate_error(error, query_error);
        g_object_unref(dest_dir);
        g_object_unref(src);
        return FALSE;
    }

    gboolean dest_is_dir = g_file_info_get_file_type(dest_info) == G_FILE_TYPE_DIRECTORY;
    g_object_unref(dest_info);
    if (!dest_is_dir) {
        g_object_unref(dest_dir);
        g_object_unref(src);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("Current location does not support paste."));
        return FALSE;
    }

    char *basename = g_file_get_basename(src);
    if (!basename || !*basename) {
        g_free(basename);
        g_object_unref(dest_dir);
        g_object_unref(src);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME, _("Paste failed."));
        return FALSE;
    }

    GFile *dst = g_file_get_child(dest_dir, basename);
    gboolean src_is_dir = query_is_directory_token(src_token);

    if (g_file_equal(src, dst)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, _("Source and destination are the same."));
        g_object_unref(dst);
        g_free(basename);
        g_object_unref(dest_dir);
        g_object_unref(src);
        return FALSE;
    }

    if (src_is_dir && (g_file_equal(dest_dir, src) || g_file_has_prefix(dest_dir, src))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Cannot copy a folder into itself."));
        g_object_unref(dst);
        g_free(basename);
        g_object_unref(dest_dir);
        g_object_unref(src);
        return FALSE;
    }

    gboolean success = FALSE;
    if (src_is_dir) {
        success = copy_recursive_file(src, dst, error);
    } else {
        success = g_file_copy(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
    }

    if (success && out_dst_token) {
        *out_dst_token = token_from_file(dst);
    }

    if (out_is_dir) {
        *out_is_dir = src_is_dir;
    }

    g_object_unref(dst);
    g_free(basename);
    g_object_unref(dest_dir);
    g_object_unref(src);
    return success;
}

static gboolean copy_drop_value_to_destination(FilesState *state, const GValue *value, const char *dest_dir_token) {
    if (!state || !value || !dest_dir_token || !is_browsable_token(dest_dir_token)) {
        return FALSE;
    }

    if (!G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Paste failed."));
        return FALSE;
    }

    GdkFileList *file_list = g_value_get_boxed(value);
    if (!file_list) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Paste failed."));
        return FALSE;
    }

    GSList *files = gdk_file_list_get_files(file_list);
    guint copied_count = 0;
    char *first_error = NULL;

    for (GSList *iter = files; iter != NULL; iter = iter->next) {
        GFile *src_file = G_FILE(iter->data);
        if (!src_file) {
            continue;
        }

        char *src_token = token_from_file(src_file);
        if (!src_token) {
            continue;
        }

        char *dst_token = NULL;
        gboolean is_dir = FALSE;
        GError *error = NULL;

        if (copy_token_to_directory(src_token, dest_dir_token, &dst_token, &is_dir, &error)) {
            copied_count++;
            remember_action(state, ACTION_COPY, src_token, dst_token, is_dir);
            set_active_item(state, dst_token, is_dir);
        } else if (!first_error) {
            first_error = g_strdup(error ? error->message : _("Paste failed."));
        }

        g_clear_error(&error);
        g_free(dst_token);
        g_free(src_token);
    }

    if (copied_count > 0) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Paste complete."));
        refresh_current_view(state);
    } else {
        gtk_label_set_text(GTK_LABEL(state->status_label), first_error ? first_error : _("Paste failed."));
    }

    g_free(first_error);
    return copied_count > 0;
}

static GdkContentProvider *on_tile_drag_prepare(GtkDragSource *source, double x, double y, gpointer user_data) {
    (void)source;
    (void)x;
    (void)y;

    FilesState *state = user_data;
    GtkWidget *tile_button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
    if (!state || !tile_button) {
        return NULL;
    }

    const char *token = g_object_get_data(G_OBJECT(tile_button), "item-token");

    if (!token || !is_browsable_token(token)) {
        return NULL;
    }

    GSList *files = NULL;

    if (is_item_selected(state, token) && selected_items_count(state) > 1) {
        GHashTableIter iter;
        gpointer key = NULL;
        g_hash_table_iter_init(&iter, state->selected_tokens);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            const char *selected_token = key;
            if (!is_browsable_token(selected_token)) {
                continue;
            }

            GFile *selected_file = file_from_token(selected_token);
            if (selected_file) {
                files = g_slist_prepend(files, selected_file);
            }
        }
    } else {
        GFile *file = file_from_token(token);
        if (file) {
            files = g_slist_prepend(files, file);
        }
    }

    if (!files) {
        return NULL;
    }

    files = g_slist_reverse(files);

    GdkFileList *list = gdk_file_list_new_from_list(files);
    g_slist_free_full(files, g_object_unref);
    if (!list) {
        return NULL;
    }

    GValue value = G_VALUE_INIT;
    g_value_init(&value, GDK_TYPE_FILE_LIST);
    g_value_take_boxed(&value, list);

    GdkContentProvider *provider = gdk_content_provider_new_for_value(&value);
    g_value_unset(&value);

    return provider;
}

static gboolean on_tile_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data) {
    (void)target;
    (void)x;
    (void)y;

    FilesState *state = user_data;
    GtkWidget *tile_button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
    if (!state || !tile_button) {
        return FALSE;
    }

    const char *dest_token = g_object_get_data(G_OBJECT(tile_button), "item-token");
    gboolean dest_is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tile_button), "item-is-dir"));
    if (!dest_token || !dest_is_dir) {
        return FALSE;
    }

    return copy_drop_value_to_destination(state, value, dest_token);
}

static gboolean on_flowbox_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data) {
    (void)target;
    (void)x;
    (void)y;

    FilesState *state = user_data;
    if (!state || !state->current_token || !is_browsable_token(state->current_token)) {
        return FALSE;
    }

    return copy_drop_value_to_destination(state, value, state->current_token);
}

static gboolean undo_last_action(FilesState *state) {
    if (!state->has_last_action) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Nothing to undo."));
        return FALSE;
    }

    ActionRecord action = {0};
    action_record_copy(&action, &state->last_action);

    gboolean success = FALSE;
    GError *error = NULL;

    if (action.type == ACTION_CREATE_FILE || action.type == ACTION_CREATE_FOLDER || action.type == ACTION_COPY) {
        GFile *dst = file_from_token(action.dst_token);
        success = dst && delete_recursive_file(dst, &error);
        g_clear_object(&dst);
    } else if (action.type == ACTION_MOVE) {
        GFile *src = file_from_token(action.src_token);
        GFile *dst = file_from_token(action.dst_token);
        success = src && dst && g_file_move(dst, src, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
        g_clear_object(&src);
        g_clear_object(&dst);
    }

    if (!success) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error ? error->message : _("Undo failed."));
        g_clear_error(&error);
        action_record_clear(&action);
        return FALSE;
    }

    action_record_copy(&state->redo_action, &action);
    state->has_redo_action = TRUE;
    action_record_clear(&state->last_action);
    state->has_last_action = FALSE;
    action_record_clear(&action);

    gtk_label_set_text(GTK_LABEL(state->status_label), _("Undo complete."));
    refresh_current_view(state);
    return TRUE;
}

static gboolean redo_last_action(FilesState *state) {
    if (!state->has_redo_action) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Nothing to redo."));
        return FALSE;
    }

    ActionRecord action = {0};
    action_record_copy(&action, &state->redo_action);

    gboolean success = FALSE;
    GError *error = NULL;

    if (action.type == ACTION_CREATE_FILE) {
        success = create_empty_file_at_token(action.dst_token, &error);
    } else if (action.type == ACTION_CREATE_FOLDER) {
        GFile *dst = file_from_token(action.dst_token);
        success = dst && g_file_make_directory(dst, NULL, &error);
        g_clear_object(&dst);
    } else if (action.type == ACTION_COPY) {
        GFile *src = file_from_token(action.src_token);
        GFile *dst = file_from_token(action.dst_token);
        success = src && dst && copy_recursive_file(src, dst, &error);
        g_clear_object(&src);
        g_clear_object(&dst);
    } else if (action.type == ACTION_MOVE) {
        GFile *src = file_from_token(action.src_token);
        GFile *dst = file_from_token(action.dst_token);
        success = src && dst && g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
        g_clear_object(&src);
        g_clear_object(&dst);
    }

    if (!success) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error ? error->message : _("Redo failed."));
        g_clear_error(&error);
        action_record_clear(&action);
        return FALSE;
    }

    action_record_copy(&state->last_action, &action);
    state->has_last_action = TRUE;
    action_record_clear(&state->redo_action);
    state->has_redo_action = FALSE;
    action_record_clear(&action);

    gtk_label_set_text(GTK_LABEL(state->status_label), _("Redo complete."));
    refresh_current_view(state);
    return TRUE;
}

static void on_menu_open_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    set_active_item(ctx->state, ctx->item_token, ctx->item_is_dir);
    if (ctx->item_is_dir) {
        navigate_to_token(ctx->state, ctx->item_token, TRUE);
    } else {
        open_file_with_association(ctx->state, ctx->item_token);
    }
}

static void on_menu_last_location_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    on_back_clicked(NULL, ctx->state);
}

static void on_menu_rename_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    set_active_item(ctx->state, ctx->item_token, ctx->item_is_dir);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("Rename item"),
        GTK_WINDOW(ctx->state->window),
        GTK_DIALOG_MODAL,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Rename"), GTK_RESPONSE_ACCEPT,
        NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(_("Choose new name"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_box_append(GTK_BOX(content), label);

    GtkWidget *entry = gtk_entry_new();
    GFile *item_file = file_from_token(ctx->item_token);
    char *base = item_file ? g_file_get_basename(item_file) : g_strdup("");
    gtk_editable_set_text(GTK_EDITABLE(entry), base ? base : "");
    g_free(base);
    g_clear_object(&item_file);

    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    gtk_widget_set_margin_bottom(entry, 12);
    gtk_box_append(GTK_BOX(content), entry);

    RenameDialogContext *dialog_ctx = g_new0(RenameDialogContext, 1);
    dialog_ctx->state = ctx->state;
    dialog_ctx->entry = entry;
    dialog_ctx->item_token = g_strdup(ctx->item_token);

    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_dialog_response), dialog_ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_menu_trash_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    set_active_item(ctx->state, ctx->item_token, ctx->item_is_dir);

    GFile *file = file_from_token(ctx->item_token);
    if (!file) {
        gtk_label_set_text(GTK_LABEL(ctx->state->status_label), _("Delete failed."));
        return;
    }

    GError *error = NULL;

    gboolean ok = FALSE;
    if (is_trash_token(ctx->state->current_token)) {
        ok = delete_recursive_file(file, &error);
    } else {
        ok = g_file_trash(file, NULL, &error);
        if (!ok) {
            g_clear_error(&error);
            ok = g_file_delete(file, NULL, &error);
        }
    }

    if (!ok) {
        gtk_label_set_text(GTK_LABEL(ctx->state->status_label), error ? error->message : _("Delete failed."));
        g_clear_error(&error);
        g_object_unref(file);
        return;
    }

    gtk_label_set_text(
        GTK_LABEL(ctx->state->status_label),
        is_trash_token(ctx->state->current_token) ? _("Deleted from trash.") : _("Moved to trash.")
    );
    g_object_unref(file);
    refresh_current_view(ctx->state);
}

static void on_menu_new_folder_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    show_new_folder_dialog(ctx->state);
}

static void on_menu_new_file_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    show_new_file_dialog(ctx->state);
}

static void on_menu_copy_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    set_active_item(ctx->state, ctx->item_token, ctx->item_is_dir);
    copy_active_item(ctx->state);
}

static void on_menu_cut_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    set_active_item(ctx->state, ctx->item_token, ctx->item_is_dir);
    cut_active_item(ctx->state);
}

static void on_menu_paste_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWidget *popover = GTK_WIDGET(user_data);
    TileMenuContext *ctx = popover_menu_context(popover);
    if (!ctx) {
        return;
    }

    gtk_popover_popdown(GTK_POPOVER(popover));
    paste_clipboard_to_current(ctx->state);
}

static void show_authors_dialog(FilesState *state) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        _("About authors"),
        GTK_WINDOW(state->window),
        GTK_DIALOG_MODAL,
        _("Close"), GTK_RESPONSE_CLOSE,
        NULL
    );
    gtk_widget_add_css_class(dialog, "files-settings-dialog");
    apply_window_theme_class(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new(
        "Karton Files\n\n"
        "Autor: MijagiKutasamoto\n"
        "Tektura compositor bazuje na labwc"
    );
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_box_append(GTK_BOX(content), label);

    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_settings_authors_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    FilesState *state = user_data;
    show_authors_dialog(state);
}

static void set_show_hidden_files(FilesState *state, gboolean show_hidden_files) {
    if (!state || state->show_hidden_files == show_hidden_files) {
        return;
    }

    state->show_hidden_files = show_hidden_files;
    refresh_current_view(state);
}

static void on_show_hidden_notify(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    FilesState *state = user_data;
    set_show_hidden_files(state, gtk_switch_get_active(GTK_SWITCH(obj)));
}

static void on_single_click_notify(GObject *obj, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    FilesState *state = user_data;
    state->open_files_on_single_click = gtk_switch_get_active(GTK_SWITCH(obj));
}

static void on_settings_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    FilesState *state = user_data;

    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("File manager settings"));
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(state->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_widget_add_css_class(dialog, "files-settings-dialog");
    apply_window_theme_class(dialog);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_widget_add_css_class(content, "files-settings-content");

    GtkWidget *hidden_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(hidden_row, "files-settings-row");
    gtk_widget_set_margin_start(hidden_row, 12);
    gtk_widget_set_margin_end(hidden_row, 12);
    gtk_widget_set_margin_top(hidden_row, 12);
    GtkWidget *hidden_label = gtk_label_new(_("Show hidden files"));
    gtk_widget_add_css_class(hidden_label, "files-settings-label");
    gtk_widget_set_hexpand(hidden_label, TRUE);
    gtk_widget_set_halign(hidden_label, GTK_ALIGN_START);
    GtkWidget *hidden_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(hidden_switch), state->show_hidden_files);
    gtk_box_append(GTK_BOX(hidden_row), hidden_label);
    gtk_box_append(GTK_BOX(hidden_row), hidden_switch);
    gtk_box_append(GTK_BOX(content), hidden_row);

    GtkWidget *single_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(single_row, "files-settings-row");
    gtk_widget_set_margin_start(single_row, 12);
    gtk_widget_set_margin_end(single_row, 12);
    gtk_widget_set_margin_top(single_row, 8);
    GtkWidget *single_label = gtk_label_new(_("Open files with single click"));
    gtk_widget_add_css_class(single_label, "files-settings-label");
    gtk_widget_set_hexpand(single_label, TRUE);
    gtk_widget_set_halign(single_label, GTK_ALIGN_START);
    GtkWidget *single_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(single_switch), state->open_files_on_single_click);
    gtk_box_append(GTK_BOX(single_row), single_label);
    gtk_box_append(GTK_BOX(single_row), single_switch);
    gtk_box_append(GTK_BOX(content), single_row);

    GtkWidget *shortcut_label = gtk_label_new(_("Shortcuts: Alt+Left, Alt+Up, Ctrl+H, Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+N, Ctrl+Shift+N, Ctrl+Z, Ctrl+Y, Ctrl+Click, Shift+Click"));
    gtk_widget_add_css_class(shortcut_label, "files-settings-hint");
    gtk_widget_set_margin_start(shortcut_label, 12);
    gtk_widget_set_margin_end(shortcut_label, 12);
    gtk_widget_set_margin_top(shortcut_label, 10);
    gtk_widget_set_halign(shortcut_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), shortcut_label);

    GtkWidget *authors_btn = gtk_button_new_with_label(_("About authors"));
    gtk_widget_add_css_class(authors_btn, "flat-button");
    gtk_widget_add_css_class(authors_btn, "files-settings-action");
    gtk_widget_set_margin_start(authors_btn, 12);
    gtk_widget_set_margin_end(authors_btn, 12);
    gtk_widget_set_margin_top(authors_btn, 10);
    gtk_widget_set_margin_bottom(authors_btn, 12);
    gtk_box_append(GTK_BOX(content), authors_btn);

    g_signal_connect(hidden_switch, "notify::active", G_CALLBACK(on_show_hidden_notify), state);
    g_signal_connect(single_switch, "notify::active", G_CALLBACK(on_single_click_notify), state);
    g_signal_connect(authors_btn, "clicked", G_CALLBACK(on_settings_authors_clicked), state);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_tile_secondary_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture;
    if (n_press != 1) {
        return;
    }

    TileMenuContext *ctx = user_data;
    if (!ctx || !ctx->popover) {
        return;
    }

    set_active_item(ctx->state, ctx->item_token, ctx->item_is_dir);

    GdkRectangle rect = {(int)x, (int)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(ctx->popover), &rect);
    gtk_popover_popup(GTK_POPOVER(ctx->popover));
}

static void setup_tile_context_menu(GtkWidget *tile_button, FilesState *state, const char *item_token, gboolean item_is_dir) {
    TileMenuContext *ctx = g_new0(TileMenuContext, 1);
    ctx->state = state;
    ctx->item_token = g_strdup(item_token);
    ctx->item_is_dir = item_is_dir;

    GtkWidget *popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "files-context-popover");
    apply_window_theme_class(popover);
    gtk_popover_set_autohide(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_widget_set_parent(popover, tile_button);
    ctx->popover = popover;

    g_object_set_data_full(G_OBJECT(popover), "tile-menu-context", ctx, tile_menu_context_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "files-context-menu");
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    GtkWidget *open_btn = gtk_button_new_with_label(item_is_dir ? _("Open folder") : _("Open"));
    GtkWidget *last_location_btn = gtk_button_new_with_label(_("Last location"));
    GtkWidget *copy_btn = gtk_button_new_with_label(_("Copy"));
    GtkWidget *cut_btn = gtk_button_new_with_label(_("Cut"));
    GtkWidget *paste_btn = gtk_button_new_with_label(_("Paste"));
    GtkWidget *rename_btn = gtk_button_new_with_label(_("Rename"));
    GtkWidget *trash_btn = gtk_button_new_with_label(_("Move to Trash"));
    GtkWidget *new_file_btn = gtk_button_new_with_label(_("New file"));
    GtkWidget *new_folder_btn = gtk_button_new_with_label(_("New Folder"));

    GtkWidget *sep_primary = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *sep_actions = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);

    if (is_trash_token(state->current_token)) {
        gtk_button_set_label(GTK_BUTTON(trash_btn), _("Delete from Trash"));
    }

    gtk_widget_add_css_class(open_btn, "flat-button");
    gtk_widget_add_css_class(last_location_btn, "flat-button");
    gtk_widget_add_css_class(copy_btn, "flat-button");
    gtk_widget_add_css_class(cut_btn, "flat-button");
    gtk_widget_add_css_class(paste_btn, "flat-button");
    gtk_widget_add_css_class(rename_btn, "flat-button");
    gtk_widget_add_css_class(trash_btn, "flat-button");
    gtk_widget_add_css_class(new_file_btn, "flat-button");
    gtk_widget_add_css_class(new_folder_btn, "flat-button");

    gtk_widget_add_css_class(open_btn, "context-menu-item");
    gtk_widget_add_css_class(last_location_btn, "context-menu-item");
    gtk_widget_add_css_class(copy_btn, "context-menu-item");
    gtk_widget_add_css_class(cut_btn, "context-menu-item");
    gtk_widget_add_css_class(paste_btn, "context-menu-item");
    gtk_widget_add_css_class(rename_btn, "context-menu-item");
    gtk_widget_add_css_class(trash_btn, "context-menu-item");
    gtk_widget_add_css_class(new_file_btn, "context-menu-item");
    gtk_widget_add_css_class(new_folder_btn, "context-menu-item");

    gtk_widget_add_css_class(sep_primary, "context-menu-separator");
    gtk_widget_add_css_class(sep_actions, "context-menu-separator");

    if (!is_browsable_token(state->current_token)) {
        gtk_widget_set_sensitive(paste_btn, FALSE);
        gtk_widget_set_sensitive(new_file_btn, FALSE);
        gtk_widget_set_sensitive(new_folder_btn, FALSE);
    }

    if (!state->clipboard_token) {
        gtk_widget_set_sensitive(paste_btn, FALSE);
    }

    gtk_widget_set_sensitive(last_location_btn, gtk_widget_is_sensitive(state->back_button));

    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_menu_open_clicked), popover);
    g_signal_connect(last_location_btn, "clicked", G_CALLBACK(on_menu_last_location_clicked), popover);
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_menu_copy_clicked), popover);
    g_signal_connect(cut_btn, "clicked", G_CALLBACK(on_menu_cut_clicked), popover);
    g_signal_connect(paste_btn, "clicked", G_CALLBACK(on_menu_paste_clicked), popover);
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_menu_rename_clicked), popover);
    g_signal_connect(trash_btn, "clicked", G_CALLBACK(on_menu_trash_clicked), popover);
    g_signal_connect(new_file_btn, "clicked", G_CALLBACK(on_menu_new_file_clicked), popover);
    g_signal_connect(new_folder_btn, "clicked", G_CALLBACK(on_menu_new_folder_clicked), popover);

    gtk_box_append(GTK_BOX(box), open_btn);
    gtk_box_append(GTK_BOX(box), last_location_btn);
    gtk_box_append(GTK_BOX(box), sep_primary);
    gtk_box_append(GTK_BOX(box), copy_btn);
    gtk_box_append(GTK_BOX(box), cut_btn);
    gtk_box_append(GTK_BOX(box), paste_btn);
    gtk_box_append(GTK_BOX(box), rename_btn);
    gtk_box_append(GTK_BOX(box), trash_btn);
    gtk_box_append(GTK_BOX(box), sep_actions);
    gtk_box_append(GTK_BOX(box), new_file_btn);
    gtk_box_append(GTK_BOX(box), new_folder_btn);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    GtkGesture *secondary = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(secondary), GDK_BUTTON_SECONDARY);
    g_signal_connect(secondary, "pressed", G_CALLBACK(on_tile_secondary_pressed), ctx);
    gtk_widget_add_controller(tile_button, GTK_EVENT_CONTROLLER(secondary));
}

static void on_tile_primary_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)x;
    (void)y;

    FilesState *state = user_data;
    GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    if (!button) {
        return;
    }

    const char *token = g_object_get_data(G_OBJECT(button), "item-token");
    gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "item-is-dir"));
    if (!token) {
        return;
    }

    GdkModifierType event_state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    gboolean ctrl_pressed = (event_state & GDK_CONTROL_MASK) != 0;
    gboolean shift_pressed = (event_state & GDK_SHIFT_MASK) != 0;

    if (n_press == 1 && shift_pressed) {
        const char *anchor = state->selection_anchor_token;
        if (!anchor || !*anchor) {
            anchor = (state->active_item_token && *state->active_item_token) ? state->active_item_token : token;
        }

        gboolean selected_range = select_range_between_tokens(state, anchor, token, ctrl_pressed);
        if (!selected_range) {
            if (ctrl_pressed) {
                toggle_item_selection(state, token, is_dir);
                set_active_item(state, token, is_dir);
            } else {
                select_single_item(state, token, is_dir);
            }
        }

        refresh_tile_selection_visuals(state);
        return;
    }

    if (n_press == 1 && ctrl_pressed) {
        toggle_item_selection(state, token, is_dir);
        set_active_item(state, token, is_dir);
        set_selection_anchor(state, token);
        refresh_tile_selection_visuals(state);
        return;
    }

    if (n_press == 1) {
        select_single_item(state, token, is_dir);
        refresh_tile_selection_visuals(state);

        if (!is_dir && state->open_files_on_single_click) {
            open_file_with_association(state, token);
        }
        return;
    }

    if (n_press == 2 && !ctrl_pressed && !shift_pressed) {
        select_single_item(state, token, is_dir);
        refresh_tile_selection_visuals(state);

        if (is_dir) {
            navigate_to_token(state, token, TRUE);
        } else if (!state->open_files_on_single_click) {
            open_file_with_association(state, token);
        }
    }
}

static void on_window_mouse_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)x;
    (void)y;
    if (n_press != 1) {
        return;
    }

    FilesState *state = user_data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    if (button == MOUSE_BUTTON_BACK) {
        on_back_clicked(NULL, state);
    } else if (button == MOUSE_BUTTON_FORWARD || button == MOUSE_BUTTON_FORWARD_ALT) {
        on_up_clicked(NULL, state);
    }
}

static void on_tile_middle_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)x;
    (void)y;
    if (n_press != 1) {
        return;
    }

    FilesState *state = user_data;
    GtkWidget *button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    if (!button) {
        return;
    }

    const char *token = g_object_get_data(G_OBJECT(button), "item-token");
    gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "item-is-dir"));
    if (!token || !is_dir) {
        return;
    }

    open_directory_in_new_tab(state, token);
}

static GtkWidget *create_file_tile(FilesState *state, const FileItem *item) {
    const char *icon_resource = item->is_dir ? "/io/karton/Files/icons/folder.svg" : "/io/karton/Files/icons/file.svg";
    gboolean item_is_executable = FALSE;

    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "file-tile");
    if (is_item_selected(state, item->token)) {
        gtk_widget_add_css_class(button, "file-tile-selected");
    }
    if (state->list_view) {
        gtk_widget_add_css_class(button, "file-tile-list");
        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_vexpand(button, FALSE);
    } else {
        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        gtk_widget_set_valign(button, GTK_ALIGN_START);
        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_vexpand(button, FALSE);
    }
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);

    GtkOrientation tile_orientation = state->list_view ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    GtkWidget *box = gtk_box_new(tile_orientation, state->list_view ? 10 : 8);
    gtk_widget_set_hexpand(box, state->list_view);
    gtk_widget_set_vexpand(box, FALSE);
    gtk_widget_set_margin_start(box, state->list_view ? 8 : 10);
    gtk_widget_set_margin_end(box, state->list_view ? 8 : 10);
    gtk_widget_set_margin_top(box, state->list_view ? 6 : 10);
    gtk_widget_set_margin_bottom(box, state->list_view ? 6 : 8);
    gtk_button_set_child(GTK_BUTTON(button), box);

    GtkWidget *icon = NULL;
    char *tooltip = NULL;

    if (!item->is_dir) {
        GFile *file = file_from_token(item->token);
        GError *error = NULL;
        GFileInfo *info = NULL;

        if (file) {
            info = g_file_query_info(
                file,
                "standard::content-type,standard::size,time::modified,standard::type,access::can-execute",
                G_FILE_QUERY_INFO_NONE,
                NULL,
                &error
            );
            g_clear_error(&error);
        }

        if (info) {
            const char *content_type = g_file_info_get_content_type(info);
            GFileType file_type = g_file_info_get_file_type(info);
            item_is_executable = (file_type == G_FILE_TYPE_REGULAR)
                && g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
            if (content_type && g_str_has_prefix(content_type, "image/")) {
                GdkTexture *texture = gdk_texture_new_from_file(file, &error);
                if (texture) {
                    icon = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
                    g_object_unref(texture);
                }
                g_clear_error(&error);
            }

            char *size_text = g_format_size((guint64)g_file_info_get_size(info));
            guint64 modified = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
            GDateTime *mtime = modified > 0 ? g_date_time_new_from_unix_local((gint64)modified) : NULL;
            char *mtime_text = mtime ? g_date_time_format(mtime, "%Y-%m-%d %H:%M") : g_strdup("-");
            tooltip = g_strdup_printf("%s\n%s\n%s", item->display_name ? item->display_name : "", size_text, mtime_text);
            g_free(size_text);
            g_free(mtime_text);
            g_clear_pointer(&mtime, g_date_time_unref);
            g_object_unref(info);
        }

        g_clear_object(&file);
    }

    if (!item->is_dir && item_is_executable) {
        icon_resource = "/io/karton/Files/icons/terminal.svg";
    }

    if (!icon) {
        icon = gtk_picture_new_for_resource(icon_resource);
    }

    int tile_icon_size = state->list_view ? 20 : state->icon_size;
    gtk_picture_set_content_fit(GTK_PICTURE(icon), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(icon), TRUE);
    gtk_widget_set_size_request(icon, tile_icon_size, tile_icon_size);
    gtk_widget_set_hexpand(icon, FALSE);
    gtk_widget_set_vexpand(icon, FALSE);
    gtk_widget_add_css_class(icon, "file-tile-icon");
    gtk_widget_set_halign(icon, state->list_view ? GTK_ALIGN_START : GTK_ALIGN_CENTER);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *label = gtk_label_new(item->display_name ? item->display_name : "");
    gtk_widget_set_halign(label, state->list_view ? GTK_ALIGN_START : GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(label, state->list_view);
    gtk_label_set_max_width_chars(GTK_LABEL(label), state->list_view ? 64 : 16);
    gtk_label_set_wrap(GTK_LABEL(label), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(label, "file-tile-label");
    if (state->list_view) {
        gtk_widget_add_css_class(label, "file-tile-label-list");
    }
    gtk_box_append(GTK_BOX(box), label);

    if (state->list_view) {
        gtk_widget_set_size_request(button, -1, tile_icon_size + 18);
    } else {
        gtk_widget_set_size_request(button, state->icon_size + 48, state->icon_size + 44);
    }
    g_object_set_data_full(G_OBJECT(button), "item-token", g_strdup(item->token), g_free);
    g_object_set_data(G_OBJECT(button), "item-is-dir", GINT_TO_POINTER(item->is_dir));
    GtkGesture *primary = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(primary), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(primary), GTK_PHASE_CAPTURE);
    g_signal_connect(primary, "pressed", G_CALLBACK(on_tile_primary_pressed), state);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(primary));

    if (is_browsable_token(item->token)) {
        GtkDragSource *drag = gtk_drag_source_new();
        gtk_drag_source_set_actions(drag, GDK_ACTION_COPY);
        g_signal_connect(drag, "prepare", G_CALLBACK(on_tile_drag_prepare), state);
        gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(drag));
    }

    if (item->is_dir && is_browsable_token(item->token)) {
        GtkDropTarget *drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
        g_signal_connect(drop, "drop", G_CALLBACK(on_tile_drop), state);
        gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(drop));
    }

    GtkGesture *middle = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(middle), GDK_BUTTON_MIDDLE);
    g_signal_connect(middle, "pressed", G_CALLBACK(on_tile_middle_pressed), state);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(middle));

    if (tooltip) {
        gtk_widget_set_tooltip_text(button, tooltip);
        g_free(tooltip);
    }

    setup_tile_context_menu(button, state, item->token, item->is_dir);

    return button;
}

static void push_history_token(FilesState *state, const char *token) {
    if (!state->history || !token) {
        return;
    }

    if (state->history->len > 0) {
        const char *current = g_ptr_array_index(state->history, state->history_index);
        if (g_strcmp0(current, token) == 0) {
            update_navigation_buttons(state);
            return;
        }
    }

    while (state->history->len > state->history_index + 1) {
        g_ptr_array_remove_index(state->history, state->history->len - 1);
    }

    g_ptr_array_add(state->history, g_strdup(token));
    state->history_index = state->history->len - 1;
    update_navigation_buttons(state);
}

static void add_unique_item(GPtrArray *items,
                            GHashTable *seen,
                            const char *token,
                            const char *display_name,
                            gboolean is_dir) {
    if (!token || !*token || !display_name || !*display_name) {
        return;
    }

    if (g_hash_table_contains(seen, token)) {
        return;
    }

    g_hash_table_add(seen, g_strdup(token));

    FileItem *item = g_new0(FileItem, 1);
    item->token = g_strdup(token);
    item->display_name = g_strdup(display_name);
    item->is_dir = is_dir;
    g_ptr_array_add(items, item);
}

static void collect_recent_items(GPtrArray *items, GHashTable *seen) {
    GtkRecentManager *manager = gtk_recent_manager_get_default();
    if (!manager) {
        return;
    }

    GList *list = gtk_recent_manager_get_items(manager);
    for (GList *iter = list; iter; iter = iter->next) {
        GtkRecentInfo *info = iter->data;
        const char *uri = gtk_recent_info_get_uri(info);
        if (!uri || !g_str_has_prefix(uri, "file://")) {
            continue;
        }

        GFile *file = g_file_new_for_uri(uri);
        char *token = token_from_file(file);
        if (!token) {
            g_object_unref(file);
            continue;
        }

        gboolean exists = g_file_query_exists(file, NULL);
        if (!exists) {
            g_free(token);
            g_object_unref(file);
            continue;
        }

        GError *error = NULL;
        GFileInfo *file_info = g_file_query_info(file, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, &error);
        g_clear_error(&error);
        gboolean is_dir = FALSE;
        if (file_info) {
            is_dir = (g_file_info_get_file_type(file_info) == G_FILE_TYPE_DIRECTORY);
            g_object_unref(file_info);
        }

        const char *display = gtk_recent_info_get_display_name(info);
        char *fallback = NULL;
        if (!display || !*display) {
            fallback = g_file_get_basename(file);
            display = fallback;
        }

        add_unique_item(items, seen, token, display, is_dir);
        g_free(fallback);
        g_free(token);
        g_object_unref(file);
    }

    g_list_free_full(list, (GDestroyNotify)gtk_recent_info_unref);
}

static void collect_bookmarks_from_file(const char *file_path, GPtrArray *items, GHashTable *seen) {
    if (!g_file_test(file_path, G_FILE_TEST_IS_REGULAR)) {
        return;
    }

    char *content = NULL;
    if (!g_file_get_contents(file_path, &content, NULL, NULL)) {
        return;
    }

    char **lines = g_strsplit(content, "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        char *line = g_strdup(lines[i]);
        g_strstrip(line);
        if (!*line || line[0] == '#') {
            g_free(line);
            continue;
        }

        char *space = strchr(line, ' ');
        if (space) {
            *space = '\0';
        }

        const char *uri_or_path = line;
        GFile *file = NULL;
        if (g_str_has_prefix(uri_or_path, "file://") || is_uri_token(uri_or_path)) {
            file = g_file_new_for_uri(uri_or_path);
        } else if (g_path_is_absolute(uri_or_path)) {
            file = g_file_new_for_path(uri_or_path);
        }

        if (!file) {
            g_free(line);
            continue;
        }

        char *token = token_from_file(file);
        if (!token || !g_file_query_exists(file, NULL)) {
            g_free(token);
            g_object_unref(file);
            g_free(line);
            continue;
        }

        GError *error = NULL;
        GFileInfo *file_info = g_file_query_info(file, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, &error);
        g_clear_error(&error);
        gboolean is_dir = FALSE;
        if (file_info) {
            is_dir = (g_file_info_get_file_type(file_info) == G_FILE_TYPE_DIRECTORY);
            g_object_unref(file_info);
        }

        char *display = NULL;
        if (space) {
            char *label = space + 1;
            g_strstrip(label);
            if (*label) {
                display = g_strdup(label);
            }
        }

        if (!display) {
            display = g_file_get_basename(file);
        }

        add_unique_item(items, seen, token, display ? display : token, is_dir);

        g_free(display);
        g_free(token);
        g_object_unref(file);
        g_free(line);
    }

    g_strfreev(lines);
    g_free(content);
}

static void collect_favorite_items(GPtrArray *items, GHashTable *seen) {
    char *gtk3 = g_build_filename(g_get_home_dir(), ".config", "gtk-3.0", "bookmarks", NULL);
    char *gtk4 = g_build_filename(g_get_home_dir(), ".config", "gtk-4.0", "bookmarks", NULL);

    collect_bookmarks_from_file(gtk3, items, seen);
    collect_bookmarks_from_file(gtk4, items, seen);

    g_free(gtk3);
    g_free(gtk4);
}

static void populate_items(FilesState *state, GPtrArray *items) {
    GHashTable *visible_tokens = g_hash_table_new(g_str_hash, g_str_equal);

    clear_flowbox(state);
    for (guint i = 0; i < items->len; i++) {
        FileItem *item = g_ptr_array_index(items, i);
        g_hash_table_add(visible_tokens, item->token);
        GtkWidget *tile = create_file_tile(state, item);
        gtk_flow_box_append(GTK_FLOW_BOX(state->flowbox), tile);
    }

    if (state->selected_tokens && g_hash_table_size(state->selected_tokens) > 0) {
        GPtrArray *to_remove = g_ptr_array_new();
        GHashTableIter iter;
        gpointer key = NULL;
        g_hash_table_iter_init(&iter, state->selected_tokens);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            const char *selected_token = key;
            if (!g_hash_table_contains(visible_tokens, selected_token)) {
                g_ptr_array_add(to_remove, key);
            }
        }

        for (guint i = 0; i < to_remove->len; i++) {
            g_hash_table_remove(state->selected_tokens, g_ptr_array_index(to_remove, i));
        }
        g_ptr_array_free(to_remove, TRUE);
    }

    refresh_tile_selection_visuals(state);
    g_hash_table_unref(visible_tokens);
}

static gboolean load_directory(FilesState *state, const char *token) {
    GFile *dir = file_from_token(token);
    if (!dir) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Cannot browse this location."));
        return FALSE;
    }

    GError *error = NULL;
    GFileInfo *dir_info = g_file_query_info(dir, "standard::type", G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (!dir_info) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error ? error->message : _("Cannot browse this location."));
        g_clear_error(&error);
        g_object_unref(dir);
        return FALSE;
    }

    gboolean is_dir = (g_file_info_get_file_type(dir_info) == G_FILE_TYPE_DIRECTORY);
    g_object_unref(dir_info);
    if (!is_dir) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Location is not a folder."));
        g_object_unref(dir);
        return FALSE;
    }

    GFileEnumerator *enumerator = g_file_enumerate_children(
        dir,
        "standard::name,standard::display-name,standard::type,standard::is-hidden",
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &error
    );

    if (!enumerator) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error ? error->message : _("Cannot open this location."));
        g_clear_error(&error);
        g_object_unref(dir);
        return FALSE;
    }

    GPtrArray *items = g_ptr_array_new_with_free_func(file_item_free);
    GFileInfo *info = NULL;

    while ((info = g_file_enumerator_next_file(enumerator, NULL, &error)) != NULL) {
        if (!state->show_hidden_files && g_file_info_get_is_hidden(info)) {
            g_object_unref(info);
            continue;
        }

        const char *name = g_file_info_get_name(info);
        if (!name || !*name) {
            g_object_unref(info);
            continue;
        }

        GFile *child = g_file_get_child(dir, name);
        char *child_token = token_from_file(child);
        if (!child_token) {
            g_object_unref(child);
            g_object_unref(info);
            continue;
        }

        const char *display_name = g_file_info_get_display_name(info);
        GFileType type = g_file_info_get_file_type(info);

        FileItem *item = g_new0(FileItem, 1);
        item->token = child_token;
        item->display_name = g_strdup(display_name && *display_name ? display_name : name);
        item->is_dir = (type == G_FILE_TYPE_DIRECTORY);
        g_ptr_array_add(items, item);

        g_object_unref(child);
        g_object_unref(info);
    }

    if (error) {
        gtk_label_set_text(GTK_LABEL(state->status_label), error->message);
        g_clear_error(&error);
    }

    g_ptr_array_sort(items, file_item_compare);
    populate_items(state, items);

    set_empty_message(state, token);
    gtk_widget_set_visible(state->empty_label, items->len == 0);
    set_status_text(state, items->len, token);

    g_ptr_array_free(items, TRUE);
    g_object_unref(enumerator);
    g_object_unref(dir);
    return TRUE;
}

static gboolean load_virtual_view(FilesState *state, const char *token) {
    GPtrArray *items = g_ptr_array_new_with_free_func(file_item_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (is_recent_token(token)) {
        collect_recent_items(items, seen);
    } else if (is_favorites_token(token)) {
        collect_favorite_items(items, seen);
    } else {
        g_hash_table_unref(seen);
        g_ptr_array_free(items, TRUE);
        return FALSE;
    }

    g_ptr_array_sort(items, file_item_compare);
    populate_items(state, items);

    set_empty_message(state, token);
    gtk_widget_set_visible(state->empty_label, items->len == 0);
    set_status_text(state, items->len, NULL);

    g_hash_table_unref(seen);
    g_ptr_array_free(items, TRUE);
    return TRUE;
}

static void collect_search_matches_recursive(FilesState *state,
                                             GFile *dir,
                                             const char *query_fold,
                                             GHashTable *seen,
                                             GPtrArray *items,
                                             guint *matches,
                                             guint max_matches) {
    if (*matches >= max_matches) {
        return;
    }

    GError *error = NULL;
    GFileEnumerator *enumerator = g_file_enumerate_children(
        dir,
        "standard::name,standard::display-name,standard::type,standard::is-hidden",
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &error
    );
    if (!enumerator) {
        g_clear_error(&error);
        return;
    }

    GFileInfo *info = NULL;
    while (*matches < max_matches && (info = g_file_enumerator_next_file(enumerator, NULL, &error)) != NULL) {
        if (!state->show_hidden_files && g_file_info_get_is_hidden(info)) {
            g_object_unref(info);
            continue;
        }

        const char *name = g_file_info_get_name(info);
        if (!name || !*name) {
            g_object_unref(info);
            continue;
        }

        const char *display_name = g_file_info_get_display_name(info);
        const char *search_name = (display_name && *display_name) ? display_name : name;
        char *name_fold = g_utf8_casefold(search_name, -1);
        gboolean matches_query = name_fold && strstr(name_fold, query_fold) != NULL;
        g_free(name_fold);

        GFile *child = g_file_get_child(dir, name);
        char *child_token = token_from_file(child);
        gboolean is_dir = (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY);

        if (matches_query && child_token) {
            add_unique_item(items, seen, child_token, search_name, is_dir);
            (*matches)++;
        }

        if (is_dir && *matches < max_matches) {
            collect_search_matches_recursive(state, child, query_fold, seen, items, matches, max_matches);
        }

        g_free(child_token);
        g_object_unref(child);
        g_object_unref(info);
    }

    if (error) {
        g_clear_error(&error);
    }

    g_object_unref(enumerator);
}

static gboolean search_in_current_location(FilesState *state, const char *query) {
    if (!query || !*query) {
        return FALSE;
    }

    set_loading_state(state, TRUE, _("Searching files..."));

    if (!state->current_token || !is_browsable_token(state->current_token)
            || !token_points_to_directory(state->current_token)) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Search is available only in folders."));
        set_loading_state(state, FALSE, NULL);
        return FALSE;
    }

    GFile *base = file_from_token(state->current_token);
    if (!base) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Search failed."));
        set_loading_state(state, FALSE, NULL);
        return FALSE;
    }

    GPtrArray *items = g_ptr_array_new_with_free_func(file_item_free);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    char *query_fold = g_utf8_casefold(query, -1);
    guint matches = 0;

    collect_search_matches_recursive(state, base, query_fold, seen, items, &matches, SEARCH_MAX_RESULTS);
    g_ptr_array_sort(items, file_item_compare);
    populate_items(state, items);

    gtk_label_set_text(GTK_LABEL(state->empty_label), _("No files match your search."));
    gtk_widget_set_visible(state->empty_label, items->len == 0);

    char *status = g_strdup_printf(
        ngettext("%u search result", "%u search results", items->len),
        items->len
    );
    gtk_label_set_text(GTK_LABEL(state->status_label), status);
    g_free(status);

    set_active_item(state, NULL, FALSE);
    update_navigation_buttons(state);

    set_loading_state(state, FALSE, NULL);

    g_free(query_fold);
    g_hash_table_unref(seen);
    g_ptr_array_free(items, TRUE);
    g_object_unref(base);
    return TRUE;
}

static gboolean load_from_token(FilesState *state, const char *token) {
    gboolean ok = FALSE;
    gboolean show_loading = is_virtual_token(token) || is_browsable_token(token);
    gboolean changed_location = (state->current_token && g_strcmp0(state->current_token, token) != 0);

    if (changed_location) {
        clear_selected_items(state);
        set_selection_anchor(state, NULL);
    }

    if (show_loading) {
        set_loading_state(state, TRUE, _("Loading files..."));
    }

    if (is_virtual_token(token)) {
        ok = load_virtual_view(state, token);
    } else if (is_browsable_token(token)) {
        ok = load_directory(state, token);
    }

    if (show_loading) {
        set_loading_state(state, FALSE, NULL);
    }

    if (!ok) {
        return FALSE;
    }

    g_free(state->current_token);
    state->current_token = g_strdup(token);
    set_active_item(state, NULL, FALSE);

    update_location_entry(state, token);
    rebuild_breadcrumb(state, token);
    sync_sidebar_selection(state, token);
    update_navigation_buttons(state);
    return TRUE;
}

static gboolean navigate_to_token(FilesState *state, const char *raw_token, gboolean add_history) {
    char *token = resolve_token_from_input(state, raw_token);
    if (!token) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Cannot resolve path."));
        return FALSE;
    }

    gboolean ok = load_from_token(state, token);
    if (ok && add_history) {
        push_history_token(state, token);
    }

    g_free(token);
    return ok;
}

static void on_flowbox_child_activated(GtkFlowBox *box, GtkFlowBoxChild *child, gpointer user_data) {
    (void)box;
    FilesState *state = user_data;

    GtkWidget *tile = gtk_flow_box_child_get_child(child);
    const char *token = g_object_get_data(G_OBJECT(tile), "item-token");
    gboolean is_dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tile), "item-is-dir"));

    if (!token) {
        return;
    }

    select_single_item(state, token, is_dir);
    refresh_tile_selection_visuals(state);

    if (!is_dir) {
        open_file_with_association(state, token);
        return;
    }

    navigate_to_token(state, token, TRUE);
}

static void on_back_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    FilesState *state = user_data;

    if (!state->history || state->history->len == 0 || state->history_index == 0) {
        return;
    }

    state->history_index--;
    const char *token = g_ptr_array_index(state->history, state->history_index);
    load_from_token(state, token);
    update_navigation_buttons(state);
}

static void on_up_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    FilesState *state = user_data;

    if (!state->current_token || !is_browsable_token(state->current_token)) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Cannot move up from this location."));
        return;
    }

    GFile *current = file_from_token(state->current_token);
    GFile *parent = g_file_get_parent(current);
    if (!parent) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Cannot move up from this location."));
        g_object_unref(current);
        return;
    }

    char *parent_token = token_from_file(parent);
    navigate_to_token(state, parent_token, TRUE);

    g_free(parent_token);
    g_object_unref(parent);
    g_object_unref(current);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    FilesState *state = user_data;
    refresh_current_view(state);
}

static void on_path_activate(GtkEntry *entry, gpointer user_data) {
    FilesState *state = user_data;
    const char *raw_text = gtk_editable_get_text(GTK_EDITABLE(entry));
    char *text = g_strdup(raw_text ? raw_text : "");
    g_strstrip(text);

    if (!*text) {
        g_free(text);
        return;
    }

    if (!input_is_explicit_path(text)) {
        search_in_current_location(state, text);
    } else {
        navigate_to_token(state, text, TRUE);
    }

    g_free(text);
}

static void on_zoom_changed(GtkAdjustment *adjustment, gpointer user_data) {
    FilesState *state = user_data;
    int icon_size = (int)(gtk_adjustment_get_value(adjustment) + 0.5);

    if (icon_size == state->icon_size) {
        return;
    }

    state->icon_size = icon_size;
    refresh_current_view(state);
}

static void apply_view_mode(FilesState *state) {
    if (!state || !state->flowbox) {
        return;
    }

    if (state->list_view) {
        gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(state->flowbox), FALSE);
        gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(state->flowbox), 1);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->flowbox), 1);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(state->flowbox), 6);
        gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(state->flowbox), 0);
        gtk_widget_add_css_class(state->flowbox, "files-list-mode");
    } else {
        gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(state->flowbox), TRUE);
        gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(state->flowbox), 4);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->flowbox), 10);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(state->flowbox), 12);
        gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(state->flowbox), 12);
        gtk_widget_remove_css_class(state->flowbox, "files-list-mode");
    }
}

static void on_view_mode_toggled(GtkToggleButton *button, gpointer user_data) {
    FilesState *state = user_data;
    if (!gtk_toggle_button_get_active(button)) {
        return;
    }

    gboolean list_mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "view-list-mode"));
    if (state->list_view == list_mode) {
        return;
    }

    state->list_view = list_mode;
    apply_view_mode(state);
    refresh_current_view(state);
}

static void on_sidebar_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    FilesState *state = user_data;
    if (state->suppress_sidebar_signal) {
        return;
    }

    if (!row) {
        return;
    }

    GVolume *volume = g_object_get_data(G_OBJECT(row), "place-volume");
    if (volume) {
        gtk_label_set_text(GTK_LABEL(state->status_label), _("Loading files..."));
        g_volume_mount(volume, G_MOUNT_MOUNT_NONE, NULL, NULL, on_volume_mount_done, state);
        return;
    }

    const char *token = g_object_get_data(G_OBJECT(row), "place-token");
    if (!token) {
        return;
    }

    navigate_to_token(state, token, TRUE);
}

static GtkWidget *append_section_header(GtkWidget *sidebar, const char *title) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);

    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 10);
    gtk_widget_set_margin_bottom(label, 4);
    gtk_widget_add_css_class(label, "place-section-label");

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(sidebar), row);
    return row;
}

static GtkWidget *append_place(GtkWidget *sidebar,
                               const char *title,
                               const char *icon_name,
                               const char *token) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 7);
    gtk_widget_set_margin_bottom(box, 7);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    GtkWidget *icon = NULL;
    if (icon_name && g_str_has_prefix(icon_name, "/io/karton/Files/icons/")) {
        icon = gtk_picture_new_for_resource(icon_name);
        gtk_picture_set_content_fit(GTK_PICTURE(icon), GTK_CONTENT_FIT_CONTAIN);
        gtk_picture_set_can_shrink(GTK_PICTURE(icon), TRUE);
        gtk_widget_set_size_request(icon, PLACE_ICON_SIZE, PLACE_ICON_SIZE);
    } else {
        icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), PLACE_ICON_SIZE);
    }
    gtk_widget_add_css_class(icon, "place-icon");
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "place-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);

    if (token) {
        g_object_set_data_full(G_OBJECT(row), "place-token", g_strdup(token), g_free);
    } else {
        gtk_widget_set_sensitive(row, FALSE);
    }

    gtk_widget_add_css_class(row, "place-row");
    gtk_list_box_append(GTK_LIST_BOX(sidebar), row);
    return row;
}

static GtkWidget *append_unmounted_volume_place(GtkWidget *sidebar,
                                                const char *title,
                                                const char *icon_name,
                                                GVolume *volume) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 7);
    gtk_widget_set_margin_bottom(box, 7);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    GtkWidget *icon = NULL;
    if (icon_name && g_str_has_prefix(icon_name, "/io/karton/Files/icons/")) {
        icon = gtk_picture_new_for_resource(icon_name);
        gtk_picture_set_content_fit(GTK_PICTURE(icon), GTK_CONTENT_FIT_CONTAIN);
        gtk_picture_set_can_shrink(GTK_PICTURE(icon), TRUE);
        gtk_widget_set_size_request(icon, PLACE_ICON_SIZE, PLACE_ICON_SIZE);
    } else {
        icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), PLACE_ICON_SIZE);
    }
    gtk_widget_add_css_class(icon, "place-icon");
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "place-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);

    if (volume) {
        g_object_set_data_full(G_OBJECT(row), "place-volume", g_object_ref(volume), g_object_unref);
    }

    gtk_widget_add_css_class(row, "place-row");
    gtk_list_box_append(GTK_LIST_BOX(sidebar), row);
    return row;
}

static char *query_display_name_for_token(const char *token, const char *fallback) {
    GFile *file = file_from_token(token);
    if (!file) {
        return g_strdup(fallback);
    }

    GError *error = NULL;
    GFileInfo *info = g_file_query_info(file, "standard::display-name", G_FILE_QUERY_INFO_NONE, NULL, &error);
    g_clear_error(&error);

    char *result = NULL;
    if (info) {
        const char *name = g_file_info_get_display_name(info);
        if (name && *name) {
            result = g_strdup(name);
        }
        g_object_unref(info);
    }

    if (!result) {
        result = g_strdup(fallback);
    }

    g_object_unref(file);
    return result;
}

static char *user_dir_or_home(GUserDirectory directory) {
    const char *value = g_get_user_special_dir(directory);
    if (value && *value) {
        return g_strdup(value);
    }
    return g_strdup(g_get_home_dir());
}

static void append_mount_section_rows(GtkWidget *sidebar,
                                      GPtrArray *mount_rows,
                                      const char *section_title,
                                      const char *icon_name,
                                      gboolean skip_network) {
    gboolean any = FALSE;

    for (guint i = 0; i < mount_rows->len; i++) {
        FileItem *item = g_ptr_array_index(mount_rows, i);
        if (!item) {
            continue;
        }

        gboolean is_network = is_uri_token(item->token) && !g_str_has_prefix(item->token, "file://");
        if (skip_network && is_network) {
            continue;
        }
        if (!skip_network && !is_network) {
            continue;
        }

        if (!any) {
            append_section_header(sidebar, section_title);
            any = TRUE;
        }

        append_place(sidebar, item->display_name, icon_name, item->token);
    }
}

static void rebuild_sidebar(FilesState *state) {
    state->suppress_sidebar_signal = TRUE;
    clear_box(state->sidebar);

    char *home = g_strdup(g_get_home_dir());
    char *desktop = user_dir_or_home(G_USER_DIRECTORY_DESKTOP);
    char *documents = user_dir_or_home(G_USER_DIRECTORY_DOCUMENTS);
    char *downloads = user_dir_or_home(G_USER_DIRECTORY_DOWNLOAD);
    char *music = user_dir_or_home(G_USER_DIRECTORY_MUSIC);
    char *pictures = user_dir_or_home(G_USER_DIRECTORY_PICTURES);
    char *videos = user_dir_or_home(G_USER_DIRECTORY_VIDEOS);
    char *public_share = user_dir_or_home(G_USER_DIRECTORY_PUBLIC_SHARE);
    char *trash = g_build_filename(g_get_home_dir(), ".local", "share", "Trash", "files", NULL);
    char *karton_drive = g_build_filename(g_get_home_dir(), "Karton Drive", NULL);

    append_section_header(state->sidebar, _("Places"));
    append_place(state->sidebar, _(N_("Recent")), "/io/karton/Files/icons/place-recent.svg", PLACE_TOKEN_RECENT);
    append_place(state->sidebar, _(N_("Favorites")), "/io/karton/Files/icons/place-favorites.svg", PLACE_TOKEN_FAVORITES);

    char *home_name = query_display_name_for_token(home, _(N_("Home")));
    char *desktop_name = query_display_name_for_token(desktop, _(N_("Desktop")));
    char *documents_name = query_display_name_for_token(documents, _(N_("Documents")));
    char *downloads_name = query_display_name_for_token(downloads, _(N_("Downloads")));
    char *music_name = query_display_name_for_token(music, _(N_("Music")));
    char *pictures_name = query_display_name_for_token(pictures, _(N_("Pictures")));
    char *videos_name = query_display_name_for_token(videos, _(N_("Videos")));
    char *public_name = query_display_name_for_token(public_share, _(N_("Public")));

    append_place(state->sidebar, home_name, "/io/karton/Files/icons/place-home.svg", home);
    append_place(state->sidebar, desktop_name, "/io/karton/Files/icons/place-desktop.svg", desktop);
    append_place(state->sidebar, documents_name, "/io/karton/Files/icons/place-documents.svg", documents);
    append_place(state->sidebar, downloads_name, "/io/karton/Files/icons/place-downloads.svg", downloads);
    append_place(state->sidebar, music_name, "/io/karton/Files/icons/place-music.svg", music);
    append_place(state->sidebar, pictures_name, "/io/karton/Files/icons/place-pictures.svg", pictures);
    append_place(state->sidebar, videos_name, "/io/karton/Files/icons/place-videos.svg", videos);
    append_place(state->sidebar, public_name, "/io/karton/Files/icons/place-public.svg", public_share);
    append_place(state->sidebar, _(N_("Trash")), "/io/karton/Files/icons/place-trash.svg", trash);

    if (g_file_test(karton_drive, G_FILE_TEST_IS_DIR)) {
        append_place(state->sidebar, _(N_("Karton Drive")), "/io/karton/Files/icons/place-drive.svg", karton_drive);
    }

    g_free(home_name);
    g_free(desktop_name);
    g_free(documents_name);
    g_free(downloads_name);
    g_free(music_name);
    g_free(pictures_name);
    g_free(videos_name);
    g_free(public_name);

    GPtrArray *mount_rows = g_ptr_array_new_with_free_func(file_item_free);
    if (state->volume_monitor) {
        GList *mounts = g_volume_monitor_get_mounts(state->volume_monitor);
        for (GList *iter = mounts; iter; iter = iter->next) {
            GMount *mount = iter->data;
            GFile *root = g_mount_get_root(mount);
            char *token = token_from_file(root);
            g_object_unref(root);

            if (!token || !*token) {
                g_free(token);
                continue;
            }

            if (g_strcmp0(token, home) == 0
                || g_strcmp0(token, desktop) == 0
                || g_strcmp0(token, documents) == 0
                || g_strcmp0(token, downloads) == 0
                || g_strcmp0(token, music) == 0
                || g_strcmp0(token, pictures) == 0
                || g_strcmp0(token, videos) == 0
                || g_strcmp0(token, public_share) == 0
                || g_strcmp0(token, trash) == 0
                || g_strcmp0(token, karton_drive) == 0) {
                g_free(token);
                continue;
            }

            FileItem *item = g_new0(FileItem, 1);
            item->token = token;
            item->display_name = g_strdup(g_mount_get_name(mount));
            item->is_dir = TRUE;
            g_ptr_array_add(mount_rows, item);
        }
        g_list_free_full(mounts, g_object_unref);
    }

    g_ptr_array_sort(mount_rows, file_item_compare);
    append_mount_section_rows(state->sidebar, mount_rows, _("Mounted devices"), "/io/karton/Files/icons/place-drive.svg", TRUE);
    append_mount_section_rows(state->sidebar, mount_rows, _("Network locations"), "/io/karton/Files/icons/place-network.svg", FALSE);
    g_ptr_array_free(mount_rows, TRUE);

    if (state->volume_monitor) {
        gboolean has_unmounted = FALSE;
        GList *volumes = g_volume_monitor_get_volumes(state->volume_monitor);
        for (GList *iter = volumes; iter; iter = iter->next) {
            GVolume *volume = iter->data;
            if (!g_volume_can_mount(volume)) {
                continue;
            }

            GMount *mounted = g_volume_get_mount(volume);
            if (mounted) {
                g_object_unref(mounted);
                continue;
            }

            if (!has_unmounted) {
                append_section_header(state->sidebar, _("Available devices"));
                has_unmounted = TRUE;
            }

            const char *name = g_volume_get_name(volume);
            append_unmounted_volume_place(
                state->sidebar,
                (name && *name) ? name : _("Mounted devices"),
                "/io/karton/Files/icons/place-drive.svg",
                volume
            );
        }
        g_list_free_full(volumes, g_object_unref);
    }

    g_free(home);
    g_free(desktop);
    g_free(documents);
    g_free(downloads);
    g_free(music);
    g_free(pictures);
    g_free(videos);
    g_free(public_share);
    g_free(trash);
    g_free(karton_drive);

    sync_sidebar_selection(state, state->current_token);
    state->suppress_sidebar_signal = FALSE;
}

static void on_mounts_changed(GVolumeMonitor *monitor, gpointer changed_object, gpointer user_data) {
    (void)monitor;
    (void)changed_object;
    FilesState *state = user_data;
    rebuild_sidebar(state);

    if (state->current_token && is_browsable_token(state->current_token) && !token_points_to_directory(state->current_token)) {
        navigate_to_token(state, g_get_home_dir(), TRUE);
    }
}

static void on_volume_mount_done(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    FilesState *state = user_data;
    if (!state) {
        return;
    }

    GVolume *volume = G_VOLUME(source_object);
    GError *error = NULL;
    if (!g_volume_mount_finish(volume, result, &error)) {
        gtk_label_set_text(
            GTK_LABEL(state->status_label),
            error ? error->message : _("Cannot open this location.")
        );
        g_clear_error(&error);
        rebuild_sidebar(state);
        return;
    }

    rebuild_sidebar(state);

    GMount *mount = g_volume_get_mount(volume);
    if (!mount) {
        refresh_current_view(state);
        return;
    }

    GFile *root = g_mount_get_root(mount);
    char *token = token_from_file(root);
    if (token) {
        navigate_to_token(state, token, TRUE);
    }

    g_free(token);
    g_object_unref(root);
    g_object_unref(mount);
}

static gboolean on_window_key_pressed(GtkEventControllerKey *controller,
                                      guint keyval,
                                      guint keycode,
                                      GdkModifierType state,
                                      gpointer user_data) {
    (void)controller;
    (void)keycode;
    FilesState *files_state = user_data;

    GdkModifierType mods = state & gtk_accelerator_get_default_mod_mask();

    if (keyval == GDK_KEY_Back) {
        on_back_clicked(NULL, files_state);
        return TRUE;
    }

    if (keyval == GDK_KEY_Forward) {
        on_up_clicked(NULL, files_state);
        return TRUE;
    }

    if ((mods & GDK_ALT_MASK) && keyval == GDK_KEY_Left) {
        on_back_clicked(NULL, files_state);
        return TRUE;
    }

    if ((mods & GDK_ALT_MASK) && keyval == GDK_KEY_Up) {
        on_up_clicked(NULL, files_state);
        return TRUE;
    }

    if (keyval == GDK_KEY_F5 || ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_r || keyval == GDK_KEY_R))) {
        on_refresh_clicked(NULL, files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        copy_active_item(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_x || keyval == GDK_KEY_X)) {
        cut_active_item(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        paste_clipboard_to_current(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_y || keyval == GDK_KEY_Y)) {
        redo_last_action(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (mods & GDK_SHIFT_MASK) && (keyval == GDK_KEY_z || keyval == GDK_KEY_Z)) {
        redo_last_action(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && !(mods & GDK_SHIFT_MASK) && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
        set_show_hidden_files(files_state, !files_state->show_hidden_files);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && !(mods & GDK_SHIFT_MASK) && (keyval == GDK_KEY_z || keyval == GDK_KEY_Z)) {
        undo_last_action(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        gtk_widget_grab_focus(files_state->path_entry);
        gtk_editable_select_region(GTK_EDITABLE(files_state->path_entry), 0, -1);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (mods & GDK_SHIFT_MASK) && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        show_new_folder_dialog(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_n || keyval == GDK_KEY_N)) {
        show_new_file_dialog(files_state);
        return TRUE;
    }

    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_a || keyval == GDK_KEY_A)) {
        select_all_visible_items(files_state);
        return TRUE;
    }

    if (keyval == GDK_KEY_Delete) {
        move_active_to_trash(files_state);
        return TRUE;
    }

    return FALSE;
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    FilesState *state = user_data;
    if (!state) {
        return;
    }

    g_free(state->current_token);
    g_free(state->active_item_token);
    g_free(state->selection_anchor_token);
    g_free(state->clipboard_token);
    if (state->selected_tokens) {
        g_hash_table_unref(state->selected_tokens);
    }
    action_record_clear(&state->last_action);
    action_record_clear(&state->redo_action);
    if (state->history) {
        g_ptr_array_free(state->history, TRUE);
    }

    if (state->volume_monitor) {
        g_object_unref(state->volume_monitor);
    }

    g_free(state);
}

static void apply_window_theme_class(GtkWidget *window) {
    GtkSettings *settings = gtk_widget_get_settings(window);
    gboolean prefer_dark = FALSE;

    if (settings) {
        g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark, NULL);
    }

    if (prefer_dark) {
        gtk_widget_remove_css_class(window, "theme-light");
        gtk_widget_add_css_class(window, "theme-dark");
    } else {
        gtk_widget_remove_css_class(window, "theme-dark");
        gtk_widget_add_css_class(window, "theme-light");
    }
}

static void on_theme_preference_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    GtkWidget *window = GTK_WIDGET(user_data);
    apply_window_theme_class(window);
}

static void enforce_window_opaque_region(GtkWidget *widget, gpointer user_data) {
    (void)user_data;

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
    if (!surface) {
        return;
    }

    /* Keep the region intentionally large; compositor clips it to real size. */
    cairo_rectangle_int_t rect = {
        .x = 0,
        .y = 0,
        .width = 16384,
        .height = 16384,
    };
    cairo_region_t *region = cairo_region_create_rectangle(&rect);
    gdk_surface_set_opaque_region(surface, region);
    cairo_region_destroy(region);
}

GtkWidget *karton_files_window_new(GtkApplication *app) {
    FilesState *state = g_new0(FilesState, 1);
    state->history = g_ptr_array_new_with_free_func(g_free);
    state->history_index = 0;
    state->icon_size = 56;
    state->show_hidden_files = FALSE;
    state->open_files_on_single_click = FALSE;

    GtkWidget *window = gtk_application_window_new(app);
    state->window = window;
    g_object_set_data(G_OBJECT(window), "files-state", state);

    gtk_window_set_title(GTK_WINDOW(window), _("Karton Files"));
    gtk_window_set_default_size(GTK_WINDOW(window), 847, 554);
    gtk_widget_add_css_class(window, "files-window");
    gtk_widget_set_opacity(window, 1.0);
    apply_window_theme_class(window);

    GtkSettings *settings = gtk_widget_get_settings(window);
    if (settings) {
        g_signal_connect_object(
            settings,
            "notify::gtk-application-prefer-dark-theme",
            G_CALLBACK(on_theme_preference_changed),
            window,
            0
        );
    }
    g_signal_connect(window, "realize", G_CALLBACK(enforce_window_opaque_region), NULL);

    GtkWidget *window_overlay = gtk_overlay_new();
    gtk_window_set_child(GTK_WINDOW(window), window_overlay);

    GtkWidget *window_background = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(window_background, TRUE);
    gtk_widget_set_vexpand(window_background, TRUE);
    gtk_widget_add_css_class(window_background, "files-solid-bg");
    gtk_overlay_set_child(GTK_OVERLAY(window_overlay), window_background);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    gtk_widget_add_css_class(root, "files-root");
    gtk_overlay_add_overlay(GTK_OVERLAY(window_overlay), root);

    GtkWidget *sidebar_scroller = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sidebar_scroller, 232, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(sidebar_scroller, "files-sidebar");
    gtk_box_append(GTK_BOX(root), sidebar_scroller);

    GtkWidget *sidebar = gtk_list_box_new();
    state->sidebar = sidebar;
    gtk_widget_add_css_class(sidebar, "places-sidebar");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scroller), sidebar);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_append(GTK_BOX(root), separator);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_widget_add_css_class(content, "files-content-root");
    gtk_box_append(GTK_BOX(root), content);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(header, "files-header");
    gtk_widget_set_margin_start(header, 16);
    gtk_widget_set_margin_end(header, 16);
    gtk_widget_set_margin_top(header, 14);
    gtk_widget_set_margin_bottom(header, 10);
    gtk_box_append(GTK_BOX(content), header);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(header), toolbar);

    state->back_button = create_icon_button("/io/karton/Files/icons/action-back.svg", _("Back (Alt+Left)"));
    gtk_box_append(GTK_BOX(toolbar), state->back_button);

    state->up_button = create_icon_button("/io/karton/Files/icons/action-up.svg", _("Up (Alt+Up)"));
    gtk_box_append(GTK_BOX(toolbar), state->up_button);

    GtkWidget *refresh_button = create_icon_button("/io/karton/Files/icons/action-refresh.svg", _("Refresh (F5)"));
    gtk_box_append(GTK_BOX(toolbar), refresh_button);

    state->settings_button = create_icon_button("/io/karton/Files/icons/action-settings.svg", _("File manager settings"));
    gtk_box_append(GTK_BOX(toolbar), state->settings_button);

    state->path_entry = gtk_entry_new();
    gtk_widget_set_hexpand(state->path_entry, TRUE);
    gtk_widget_add_css_class(state->path_entry, "location-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->path_entry), _("Enter path or search phrase and press Enter (Ctrl+L to focus)"));
    gtk_box_append(GTK_BOX(toolbar), state->path_entry);

    state->breadcrumb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(state->breadcrumb_box, "breadcrumb-box");
    gtk_widget_set_hexpand(state->breadcrumb_box, TRUE);
    gtk_widget_set_halign(state->breadcrumb_box, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header), state->breadcrumb_box);

    GtkWidget *content_scroller = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(content_scroller, TRUE);
    gtk_widget_set_vexpand(content_scroller, TRUE);
    gtk_widget_set_margin_start(content_scroller, 10);
    gtk_widget_set_margin_end(content_scroller, 10);
    gtk_widget_set_margin_bottom(content_scroller, 8);
    gtk_widget_add_css_class(content_scroller, "files-scroller");
    gtk_box_append(GTK_BOX(content), content_scroller);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(content_scroller), overlay);

    state->flowbox = gtk_flow_box_new();
    gtk_widget_add_css_class(state->flowbox, "files-grid");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(state->flowbox), GTK_SELECTION_NONE);
    gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(state->flowbox), FALSE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(state->flowbox), 4);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(state->flowbox), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(state->flowbox), 12);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(state->flowbox), 12);
    gtk_widget_set_halign(state->flowbox, GTK_ALIGN_FILL);
    gtk_widget_set_valign(state->flowbox, GTK_ALIGN_START);
    gtk_widget_set_margin_start(state->flowbox, 10);
    gtk_widget_set_margin_end(state->flowbox, 10);
    gtk_widget_set_margin_top(state->flowbox, 10);
    gtk_widget_set_margin_bottom(state->flowbox, 10);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), state->flowbox);

    GtkDropTarget *flowbox_drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(flowbox_drop, "drop", G_CALLBACK(on_flowbox_drop), state);
    gtk_widget_add_controller(state->flowbox, GTK_EVENT_CONTROLLER(flowbox_drop));

    state->empty_label = gtk_label_new(_("This folder is empty."));
    gtk_widget_add_css_class(state->empty_label, "empty-label");
    gtk_widget_set_halign(state->empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state->empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(state->empty_label, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), state->empty_label);

    state->loading_overlay = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(state->loading_overlay, "loading-overlay");
    gtk_widget_set_halign(state->loading_overlay, GTK_ALIGN_FILL);
    gtk_widget_set_valign(state->loading_overlay, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(state->loading_overlay, TRUE);
    gtk_widget_set_vexpand(state->loading_overlay, TRUE);
    gtk_widget_set_visible(state->loading_overlay, FALSE);

    GtkWidget *loading_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(loading_panel, "loading-panel");
    gtk_widget_set_halign(loading_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(loading_panel, GTK_ALIGN_CENTER);

    state->loading_spinner = gtk_spinner_new();
    gtk_widget_set_size_request(state->loading_spinner, 28, 28);
    gtk_box_append(GTK_BOX(loading_panel), state->loading_spinner);

    state->loading_label = gtk_label_new(_("Loading files..."));
    gtk_widget_add_css_class(state->loading_label, "loading-label");
    gtk_box_append(GTK_BOX(loading_panel), state->loading_label);

    gtk_box_append(GTK_BOX(state->loading_overlay), loading_panel);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), state->loading_overlay);

    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(status_bar, "files-statusbar");
    gtk_widget_add_css_class(status_bar, "files-statusbar-compact");
    gtk_widget_set_margin_start(status_bar, 12);
    gtk_widget_set_margin_end(status_bar, 12);
    gtk_widget_set_margin_bottom(status_bar, 8);
    gtk_box_append(GTK_BOX(content), status_bar);

    state->status_label = gtk_label_new("");
    gtk_widget_add_css_class(state->status_label, "status-label");
    gtk_widget_set_halign(state->status_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(state->status_label, TRUE);
    gtk_box_append(GTK_BOX(status_bar), state->status_label);

    GtkWidget *zoom_icon = gtk_picture_new_for_resource("/io/karton/Files/icons/action-zoom.svg");
    gtk_picture_set_content_fit(GTK_PICTURE(zoom_icon), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(zoom_icon), TRUE);
    gtk_widget_set_size_request(zoom_icon, STATUS_ICON_SIZE, STATUS_ICON_SIZE);
    gtk_widget_add_css_class(zoom_icon, "status-icon");
    gtk_box_append(GTK_BOX(status_bar), zoom_icon);

    state->zoom_adjustment = gtk_adjustment_new(56.0, 36.0, 112.0, 4.0, 8.0, 0.0);
    GtkWidget *zoom_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, state->zoom_adjustment);
    gtk_scale_set_draw_value(GTK_SCALE(zoom_scale), FALSE);
    gtk_widget_set_size_request(zoom_scale, 124, -1);
    gtk_widget_add_css_class(zoom_scale, "zoom-scale");
    gtk_box_append(GTK_BOX(status_bar), zoom_scale);

    state->grid_mode_button = gtk_toggle_button_new();
    g_object_set_data(G_OBJECT(state->grid_mode_button), "view-list-mode", GINT_TO_POINTER(FALSE));
    gtk_widget_add_css_class(state->grid_mode_button, "flat-button");
    gtk_widget_add_css_class(state->grid_mode_button, "view-mode-button");
    gtk_widget_set_tooltip_text(state->grid_mode_button, _("Grid view"));
    GtkWidget *grid_icon = gtk_picture_new_for_resource("/io/karton/Files/icons/action-grid.svg");
    gtk_picture_set_content_fit(GTK_PICTURE(grid_icon), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(grid_icon), TRUE);
    gtk_widget_set_size_request(grid_icon, STATUS_ICON_SIZE, STATUS_ICON_SIZE);
    gtk_widget_add_css_class(grid_icon, "status-icon");
    gtk_button_set_child(GTK_BUTTON(state->grid_mode_button), grid_icon);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state->grid_mode_button), TRUE);
    gtk_box_append(GTK_BOX(status_bar), state->grid_mode_button);

    state->list_mode_button = gtk_toggle_button_new();
    g_object_set_data(G_OBJECT(state->list_mode_button), "view-list-mode", GINT_TO_POINTER(TRUE));
    gtk_widget_add_css_class(state->list_mode_button, "flat-button");
    gtk_widget_add_css_class(state->list_mode_button, "view-mode-button");
    gtk_widget_set_tooltip_text(state->list_mode_button, _("List view"));
    GtkWidget *list_icon = gtk_picture_new_for_resource("/io/karton/Files/icons/action-list.svg");
    gtk_picture_set_content_fit(GTK_PICTURE(list_icon), GTK_CONTENT_FIT_CONTAIN);
    gtk_picture_set_can_shrink(GTK_PICTURE(list_icon), TRUE);
    gtk_widget_set_size_request(list_icon, STATUS_ICON_SIZE, STATUS_ICON_SIZE);
    gtk_widget_add_css_class(list_icon, "status-icon");
    gtk_button_set_child(GTK_BUTTON(state->list_mode_button), list_icon);
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(state->list_mode_button), GTK_TOGGLE_BUTTON(state->grid_mode_button));
    gtk_box_append(GTK_BOX(status_bar), state->list_mode_button);

    g_signal_connect(sidebar, "row-selected", G_CALLBACK(on_sidebar_row_selected), state);
    g_signal_connect(state->back_button, "clicked", G_CALLBACK(on_back_clicked), state);
    g_signal_connect(state->up_button, "clicked", G_CALLBACK(on_up_clicked), state);
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), state);
    g_signal_connect(state->settings_button, "clicked", G_CALLBACK(on_settings_clicked), state);
    g_signal_connect(state->path_entry, "activate", G_CALLBACK(on_path_activate), state);
    g_signal_connect(state->grid_mode_button, "toggled", G_CALLBACK(on_view_mode_toggled), state);
    g_signal_connect(state->list_mode_button, "toggled", G_CALLBACK(on_view_mode_toggled), state);
    g_signal_connect(state->flowbox, "child-activated", G_CALLBACK(on_flowbox_child_activated), state);
    g_signal_connect(state->zoom_adjustment, "value-changed", G_CALLBACK(on_zoom_changed), state);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), state);

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_window_key_pressed), state);
    gtk_widget_add_controller(window, keys);

    GtkGesture *mouse_buttons = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mouse_buttons), 0);
    g_signal_connect(mouse_buttons, "pressed", G_CALLBACK(on_window_mouse_pressed), state);
    gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(mouse_buttons));

    state->volume_monitor = g_volume_monitor_get();
    if (state->volume_monitor) {
        g_object_ref(state->volume_monitor);
        g_signal_connect(state->volume_monitor, "mount-added", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "mount-removed", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "mount-changed", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "volume-added", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "volume-removed", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "volume-changed", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "drive-connected", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "drive-disconnected", G_CALLBACK(on_mounts_changed), state);
        g_signal_connect(state->volume_monitor, "drive-changed", G_CALLBACK(on_mounts_changed), state);
    }

    rebuild_sidebar(state);
    apply_view_mode(state);
    navigate_to_token(state, g_get_home_dir(), TRUE);

    return window;
}
