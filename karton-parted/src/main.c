#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <parted/parted.h>
#include <unistd.h>
#include <stdlib.h>

static GtkStringList *disk_model;
static GtkWidget *disk_dropdown;
static GtkWidget *partition_list_box; 
static GtkWidget *partition_map_box;
static GtkWidget *main_window;

static const char* FS_TYPES[] = { "ext4", "btrfs", "vfat", "ntfs", "linux-swap" };

static void load_partitions_for_device(PedDevice *dev);

static void refresh_current_disk() {
    const char *selected_str = gtk_string_list_get_string(disk_model, gtk_drop_down_get_selected(GTK_DROP_DOWN(disk_dropdown)));
    if (!selected_str) return;
    gchar **parts = g_strsplit(selected_str, " - ", 2);
    if (parts[0]) {
        PedDevice *dev = ped_device_get(parts[0]);
        if (dev) {
            load_partitions_for_device(dev);
        }
    }
    g_strfreev(parts);
}

// ---------------------------------
// COMMAND EXECUTION
// ---------------------------------
static void on_format_finished(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GSubprocess *subprocess = G_SUBPROCESS(source_object);
    GError *error = NULL;

    g_subprocess_wait_finish(subprocess, res, &error);

    if (error || !g_subprocess_get_successful(subprocess)) {
        GtkWidget *err = g_object_new(GTK_TYPE_MESSAGE_DIALOG,
                                            "transient-for", main_window,
                                            "modal", (GTK_DIALOG_MODAL) & GTK_DIALOG_MODAL ? TRUE : FALSE,
                                            "destroy-with-parent", (GTK_DIALOG_MODAL) & GTK_DIALOG_DESTROY_WITH_PARENT ? TRUE : FALSE,
                                            "message-type", GTK_MESSAGE_ERROR,
                                            "buttons", GTK_BUTTONS_OK,
                                            "text", _("Error"),
                                            "use-header-bar", FALSE,
                                            NULL);
    gtk_widget_add_css_class(err, "karton-window");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(err), _("Formatting failed. Make sure the partition is unmounted."));
        g_signal_connect(err, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_window_present(GTK_WINDOW(err));
        if (error) g_error_free(error);
    }

    gtk_widget_set_sensitive(main_window, TRUE);
    refresh_current_disk();
}

static void format_partition_async(const char *path, int fs_idx) {
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);

    if (fs_idx == 0) { g_ptr_array_add(args, g_strdup("mkfs.ext4")); g_ptr_array_add(args, g_strdup("-F")); }
    else if (fs_idx == 1) { g_ptr_array_add(args, g_strdup("mkfs.btrfs")); g_ptr_array_add(args, g_strdup("-f")); }
    else if (fs_idx == 2) { g_ptr_array_add(args, g_strdup("mkfs.fat")); g_ptr_array_add(args, g_strdup("-F32")); }
    else if (fs_idx == 3) { g_ptr_array_add(args, g_strdup("mkfs.ntfs")); g_ptr_array_add(args, g_strdup("-f")); }
    else if (fs_idx == 4) { g_ptr_array_add(args, g_strdup("mkswap")); g_ptr_array_add(args, g_strdup("-f")); }
    
    g_ptr_array_add(args, g_strdup(path));
    g_ptr_array_add(args, NULL);

    GError *error = NULL;
    GSubprocess *subprocess = g_subprocess_newv((const gchar * const *)args->pdata, G_SUBPROCESS_FLAGS_NONE, &error);
    g_ptr_array_free(args, TRUE);

    if (subprocess) {
        gtk_widget_set_sensitive(main_window, FALSE);
        g_subprocess_wait_async(subprocess, NULL, on_format_finished, NULL);
        g_object_unref(subprocess);
    } else {
        g_print("Failed to launch mkfs: %s\n", error->message);
        g_error_free(error);
    }
}


// ---------------------------------
// DELETE PARTITION LOGIC
// ---------------------------------
static void on_delete_confirm(GtkDialog *dialog, int response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *dev_path = g_object_get_data(G_OBJECT(dialog), "dev_path");
        int part_num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "part_num"));

        PedDevice *dev = ped_device_get(dev_path);
        if (dev) {
            PedDisk *disk = ped_disk_new(dev);
            if (disk) {
                PedPartition *part = ped_disk_get_partition(disk, part_num);
                if (part && ped_disk_delete_partition(disk, part)) {
                    ped_disk_commit(disk);
                }
                ped_disk_destroy(disk);
            }
        }
        refresh_current_disk();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_action_delete(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    GtkWidget *dialog = g_object_new(GTK_TYPE_MESSAGE_DIALOG,
                                            "transient-for", main_window,
                                            "modal", TRUE,
                                            "destroy-with-parent", TRUE,
                                            "message-type", GTK_MESSAGE_WARNING,
                                            "buttons", GTK_BUTTONS_NONE,
                                            "text", _("Delete Partition"),
                                            "use-header-bar", FALSE,
                                            NULL);
    gtk_widget_add_css_class(dialog, "karton-window");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), _("Are you sure you want to delete this partition? Data will be lost permanently."));
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Delete"), GTK_RESPONSE_ACCEPT);

    g_object_set_data_full(G_OBJECT(dialog), "dev_path", g_strdup(g_object_get_data(G_OBJECT(btn), "dev_path")), g_free);
    g_object_set_data(G_OBJECT(dialog), "part_num", g_object_get_data(G_OBJECT(btn), "part_num"));

    g_signal_connect(dialog, "response", G_CALLBACK(on_delete_confirm), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

// ---------------------------------
// FORMAT LOGIC
// ---------------------------------
static void on_format_confirm(GtkDialog *dialog, int response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *part_path = g_object_get_data(G_OBJECT(dialog), "part_path");
        GtkWidget *fs_dropdown = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "fs_dropdown"));
        guint fs_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(fs_dropdown));

        if (part_path) {
            format_partition_async(part_path, fs_idx);
        }
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_action_format(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    GtkWidget *dialog = g_object_new(GTK_TYPE_MESSAGE_DIALOG,
                                            "transient-for", main_window,
                                            "modal", TRUE,
                                            "destroy-with-parent", TRUE,
                                            "message-type", GTK_MESSAGE_WARNING,
                                            "buttons", GTK_BUTTONS_NONE,
                                            "text", _("Format Partition"),
                                            "use-header-bar", FALSE,
                                            NULL);
    gtk_widget_add_css_class(dialog, "karton-window");
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Format"), GTK_RESPONSE_ACCEPT);
    
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(area), vbox);

    gtk_box_append(GTK_BOX(vbox), gtk_label_new(_("Select filesystem type. ALL DATA WILL BE LOST!")));
    
    GtkStringList *fs_list = gtk_string_list_new(FS_TYPES);
    GtkWidget *fs_dropdown = gtk_drop_down_new(G_LIST_MODEL(fs_list), NULL);
    gtk_box_append(GTK_BOX(vbox), fs_dropdown);

    g_object_set_data_full(G_OBJECT(dialog), "part_path", g_strdup(g_object_get_data(G_OBJECT(btn), "part_path")), g_free);
    g_object_set_data(G_OBJECT(dialog), "fs_dropdown", fs_dropdown);

    g_signal_connect(dialog, "response", G_CALLBACK(on_format_confirm), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

// ---------------------------------
// RESIZE PARTITION LOGIC
// ---------------------------------
static void on_resize_confirm(GtkDialog *dialog, int response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *dev_path = g_object_get_data(G_OBJECT(dialog), "dev_path");
        int part_num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "part_num"));
        GtkWidget *size_spin = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "size_spin"));
        double new_mb = gtk_spin_button_get_value(GTK_SPIN_BUTTON(size_spin));
        long long current_length = *(long long*)g_object_get_data(G_OBJECT(dialog), "cur_length");
        
        PedDevice *dev = ped_device_get(dev_path);
        if (dev) {
            PedDisk *disk = ped_disk_new(dev);
            if (disk) {
                PedPartition *part = ped_disk_get_partition(disk, part_num);
                if (part) {
                    long long new_length = (long long)((new_mb * 1024.0 * 1024.0) / dev->sector_size);
                    long long start = part->geom.start;
                    long long end = start + new_length - 1;
                    
                    gboolean is_growing = (new_length > current_length);
                    const char *fs_type_name = part->fs_type ? part->fs_type->name : "";
                    
                    if (is_growing) {
                        PedGeometry *new_geom = ped_geometry_new(dev, start, new_length);
                        PedConstraint *constraint = ped_constraint_any(dev);
                        
                        if (ped_disk_set_partition_geom(disk, part, constraint, start, end)) {
                            ped_disk_commit(disk);
                            
                            if (g_strcmp0(fs_type_name, "ext4") == 0) {
                                gchar *part_path_str = ped_partition_get_path(part);
                                gchar *argv[] = { "resize2fs", part_path_str, NULL };
                                g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
                                free(part_path_str);
                            }
                        }
                        
                        ped_constraint_destroy(constraint);
                        ped_geometry_destroy(new_geom);
                    }
                }
                ped_disk_destroy(disk);
            }
        }
        refresh_current_disk();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_action_resize(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    GtkWidget *dialog = g_object_new(GTK_TYPE_MESSAGE_DIALOG,
                                            "transient-for", main_window,
                                            "modal", TRUE,
                                            "destroy-with-parent", TRUE,
                                            "message-type", GTK_MESSAGE_WARNING,
                                            "buttons", GTK_BUTTONS_NONE,
                                            "text", _("Resize Partition"),
                                            "use-header-bar", FALSE,
                                            NULL);
    gtk_widget_add_css_class(dialog, "karton-window");
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Resize"), GTK_RESPONSE_ACCEPT);

    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(area), vbox);

    gtk_box_append(GTK_BOX(vbox), gtk_label_new(_("Enter new partition size (MB):\nNotice: Shrinking is disabled for safety. Expanding ext4 is supported.")));
    
    PedDevice *dev = ped_device_get((const char*)g_object_get_data(G_OBJECT(btn), "dev_path"));
    long long cur_length = *(long long*)g_object_get_data(G_OBJECT(btn), "cur_length");
    double cur_mb = dev ? (double)cur_length * dev->sector_size / (1024.0 * 1024.0) : 0;
    
    // Zablokowanie zmniejszania partycji przez ustawienie dolnego limitu na cur_mb
    GtkWidget *size_spin = gtk_spin_button_new_with_range(cur_mb, 999999.0, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(size_spin), cur_mb);
    gtk_box_append(GTK_BOX(vbox), size_spin);

    g_object_set_data_full(G_OBJECT(dialog), "dev_path", g_strdup(g_object_get_data(G_OBJECT(btn), "dev_path")), g_free);
    g_object_set_data(G_OBJECT(dialog), "part_num", g_object_get_data(G_OBJECT(btn), "part_num"));
    long long *dialog_cur_p = g_new(long long, 1);
    *dialog_cur_p = cur_length;
    g_object_set_data_full(G_OBJECT(dialog), "cur_length", dialog_cur_p, g_free);
    g_object_set_data(G_OBJECT(dialog), "size_spin", size_spin);

    g_signal_connect(dialog, "response", G_CALLBACK(on_resize_confirm), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}


// ---------------------------------
// CREATE LOGIC
// ---------------------------------
static void on_create_confirm(GtkDialog *dialog, int response, gpointer user_data) {
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *dev_path = g_object_get_data(G_OBJECT(dialog), "dev_path");
        long long start = *(long long*)g_object_get_data(G_OBJECT(dialog), "start_sector");
        long long max_end = *(long long*)g_object_get_data(G_OBJECT(dialog), "end_sector");

        GtkWidget *fs_dropdown = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "fs_dropdown"));
        guint fs_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(fs_dropdown));

        GtkWidget *size_spin = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "size_spin"));
        double user_mb = gtk_spin_button_get_value(GTK_SPIN_BUTTON(size_spin));

        PedDevice *dev = ped_device_get(dev_path);
        if (dev) {
            long long wanted_length = (long long)((user_mb * 1024.0 * 1024.0) / dev->sector_size);
            long long end = start + wanted_length - 1;
            if (end > max_end) end = max_end;

            PedDisk *disk = ped_disk_new(dev);
            if (disk) {
                PedFileSystemType *fs_type = ped_file_system_type_get(FS_TYPES[fs_idx]);
                PedPartitionType part_type = PED_PARTITION_NORMAL;
                
                PedGeometry *geom = ped_geometry_new(dev, start, end - start + 1);
                PedConstraint *constraint = ped_constraint_exact(geom); 
                
                PedPartition *new_part = ped_partition_new(disk, part_type, fs_type, geom->start, geom->end);
                if (new_part) {
                    if (ped_disk_add_partition(disk, new_part, constraint)) {
                        ped_disk_commit(disk);
                        gchar *part_path = ped_partition_get_path(new_part);
                        if(part_path) {
                            format_partition_async(part_path, fs_idx);
                            free(part_path);
                        }
                    } else {
                         ped_partition_destroy(new_part);
                    }
                }
                
                ped_constraint_destroy(constraint);
                ped_geometry_destroy(geom);
                ped_disk_destroy(disk);
            }
        }
        refresh_current_disk();
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_action_create(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    GtkWidget *dialog = g_object_new(GTK_TYPE_MESSAGE_DIALOG,
                                            "transient-for", main_window,
                                            "modal", TRUE,
                                            "destroy-with-parent", TRUE,
                                            "message-type", GTK_MESSAGE_QUESTION,
                                            "buttons", GTK_BUTTONS_NONE,
                                            "text", _("Create Partition"),
                                            "use-header-bar", FALSE,
                                            NULL);
    gtk_widget_add_css_class(dialog, "karton-window");
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Create"), GTK_RESPONSE_ACCEPT);

    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(area), vbox);
    
    gtk_box_append(GTK_BOX(vbox), gtk_label_new(_("Select filesystem type:")));
    GtkStringList *fs_list = gtk_string_list_new(FS_TYPES);
    GtkWidget *fs_dropdown = gtk_drop_down_new(G_LIST_MODEL(fs_list), NULL);
    gtk_box_append(GTK_BOX(vbox), fs_dropdown);

    long long start = *(long long*)g_object_get_data(G_OBJECT(btn), "start_sector");
    long long end = *(long long*)g_object_get_data(G_OBJECT(btn), "end_sector");
    
    PedDevice *dev = ped_device_get((const char*)g_object_get_data(G_OBJECT(btn), "dev_path"));
    double max_mb = 0;
    if(dev) max_mb = (double)(end - start + 1) * dev->sector_size / (1024.0 * 1024.0);

    gtk_box_append(GTK_BOX(vbox), gtk_label_new(_("Partition size (MB):")));
    GtkWidget *size_spin = gtk_spin_button_new_with_range(1.0, max_mb, 1.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(size_spin), max_mb);
    gtk_box_append(GTK_BOX(vbox), size_spin);

    g_object_set_data_full(G_OBJECT(dialog), "dev_path", g_strdup(g_object_get_data(G_OBJECT(btn), "dev_path")), g_free);
    long long *start_ptr = g_new(long long, 1);
    long long *end_ptr = g_new(long long, 1);
    *start_ptr = start;
    *end_ptr = end;
    g_object_set_data_full(G_OBJECT(dialog), "start_sector", start_ptr, g_free);
    g_object_set_data_full(G_OBJECT(dialog), "end_sector", end_ptr, g_free);
    g_object_set_data(G_OBJECT(dialog), "fs_dropdown", fs_dropdown);
    g_object_set_data(G_OBJECT(dialog), "size_spin", size_spin);

    g_signal_connect(dialog, "response", G_CALLBACK(on_create_confirm), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

// ---------------------------------
// UI RENDERER
// ---------------------------------
static void load_partitions_for_device(PedDevice *dev) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(partition_list_box)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(partition_list_box), child);
    }
    while ((child = gtk_widget_get_first_child(partition_map_box)) != NULL) {
        gtk_box_remove(GTK_BOX(partition_map_box), child);
    }

    if (!dev) return;

    PedDisk *disk = ped_disk_new(dev);
    if (!disk) {
        GtkWidget *err_lbl = gtk_label_new(_("No partition table found."));
        gtk_list_box_append(GTK_LIST_BOX(partition_list_box), err_lbl);
        return;
    }

    PedPartition *part = NULL;
    PedSector total_sectors = dev->length;

    while ((part = ped_disk_next_partition(disk, part))) {
        if (part->type & PED_PARTITION_METADATA) continue;

        double size_gb = (double)part->geom.length * dev->sector_size / (1024.0 * 1024.0 * 1024.0);
        gboolean is_free = (part->type & PED_PARTITION_FREESPACE);
        const char *fs_name = is_free ? _("Free Space") : ((part->fs_type) ? part->fs_type->name : _("Unknown"));
        gchar *part_path = is_free ? g_strdup(_("Unallocated")) : ped_partition_get_path(part);

        // --- 1. Visual Map ---
        double portion = (double)part->geom.length / (double)total_sectors;
        if (portion > 0.005) { 
            GtkWidget *map_segment = gtk_label_new(NULL);
            gchar *map_lbl = g_strdup_printf("%s\n%.2f GB", fs_name, size_gb);
            gtk_label_set_text(GTK_LABEL(map_segment), map_lbl);
            g_free(map_lbl);
            
            gtk_widget_set_vexpand(map_segment, TRUE);
            gtk_widget_set_hexpand(map_segment, TRUE);
            gtk_widget_add_css_class(map_segment, "map-segment");
            if (is_free) gtk_widget_add_css_class(map_segment, "map-free");
            else if (g_strcmp0(fs_name, "ext4") == 0) gtk_widget_add_css_class(map_segment, "map-ext4");
            else if (g_strcmp0(fs_name, "btrfs") == 0) gtk_widget_add_css_class(map_segment, "map-btrfs");
            else if (g_strcmp0(fs_name, "linux-swap(v1)") == 0) gtk_widget_add_css_class(map_segment, "map-swap");
            else if (g_strcmp0(fs_name, "ntfs") == 0) gtk_widget_add_css_class(map_segment, "map-ntfs");
            else if (g_strcmp0(fs_name, "fat32") == 0) gtk_widget_add_css_class(map_segment, "map-fat");
            else gtk_widget_add_css_class(map_segment, "map-other");
            
            gtk_box_append(GTK_BOX(partition_map_box), map_segment);
            GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
            gtk_box_append(GTK_BOX(partition_map_box), sep);
        }

        // --- 2. List Item (GTK Box row instead of AdwActionRow) ---
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(row_box, 12);
        gtk_widget_set_margin_end(row_box, 12);
        gtk_widget_set_margin_top(row_box, 12);
        gtk_widget_set_margin_bottom(row_box, 12);
        
        GtkWidget *text_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_hexpand(text_vbox, TRUE);
        gtk_widget_set_valign(text_vbox, GTK_ALIGN_CENTER);

        GtkWidget *title_lbl = gtk_label_new(part_path ? part_path : "?");
        gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(title_lbl, "heading");
        
        gchar *subtitle = g_strdup_printf(_("Type: %s • Size: %.2f GB • Sectors: %lld to %lld"), fs_name, size_gb, (long long)part->geom.start, (long long)part->geom.end);
        GtkWidget *subtitle_lbl = gtk_label_new(subtitle);
        gtk_widget_set_halign(subtitle_lbl, GTK_ALIGN_START);
        gtk_widget_add_css_class(subtitle_lbl, "dim-label");
        g_free(subtitle);

        gtk_box_append(GTK_BOX(text_vbox), title_lbl);
        gtk_box_append(GTK_BOX(text_vbox), subtitle_lbl);
        gtk_box_append(GTK_BOX(row_box), text_vbox);

        if (!is_free) {
            GtkWidget *btn_format = gtk_button_new_from_icon_name("media-floppy-symbolic");
            gtk_widget_set_tooltip_text(btn_format, _("Format Partition"));
            gtk_widget_set_valign(btn_format, GTK_ALIGN_CENTER);
            g_object_set_data_full(G_OBJECT(btn_format), "part_path", g_strdup(part_path), g_free);
            g_signal_connect(btn_format, "clicked", G_CALLBACK(on_action_format), NULL);
            gtk_box_append(GTK_BOX(row_box), btn_format);

            GtkWidget *btn_resize = gtk_button_new_from_icon_name("view-restore-symbolic");
            gtk_widget_set_tooltip_text(btn_resize, _("Resize Partition"));
            gtk_widget_set_valign(btn_resize, GTK_ALIGN_CENTER);
            g_object_set_data_full(G_OBJECT(btn_resize), "dev_path", g_strdup(dev->path), g_free);
            g_object_set_data(G_OBJECT(btn_resize), "part_num", GINT_TO_POINTER(part->num));
            g_object_set_data_full(G_OBJECT(btn_resize), "part_path", g_strdup(part_path), g_free);
            long long *cur_size_p = g_new(long long, 1); *cur_size_p = part->geom.length;
            g_object_set_data_full(G_OBJECT(btn_resize), "cur_length", cur_size_p, g_free);
            g_signal_connect(btn_resize, "clicked", G_CALLBACK(on_action_resize), NULL);
            gtk_box_append(GTK_BOX(row_box), btn_resize);

            GtkWidget *btn_del = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_set_tooltip_text(btn_del, _("Delete Partition"));
            gtk_widget_set_valign(btn_del, GTK_ALIGN_CENTER);
            gtk_widget_add_css_class(btn_del, "destructive-action");
            g_object_set_data_full(G_OBJECT(btn_del), "dev_path", g_strdup(dev->path), g_free);
            g_object_set_data(G_OBJECT(btn_del), "part_num", GINT_TO_POINTER(part->num));
            g_signal_connect(btn_del, "clicked", G_CALLBACK(on_action_delete), NULL);
            gtk_box_append(GTK_BOX(row_box), btn_del);
        } else {
            if (size_gb >= 0.001) { 
                GtkWidget *btn_new = gtk_button_new_from_icon_name("list-add-symbolic");
                gtk_widget_set_tooltip_text(btn_new, _("Create Partition"));
                gtk_widget_set_valign(btn_new, GTK_ALIGN_CENTER);
                gtk_widget_add_css_class(btn_new, "suggested-action");
                
                long long *start_p = g_new(long long, 1); *start_p = part->geom.start;
                long long *end_p = g_new(long long, 1); *end_p = part->geom.end;
                
                g_object_set_data_full(G_OBJECT(btn_new), "dev_path", g_strdup(dev->path), g_free);
                g_object_set_data_full(G_OBJECT(btn_new), "start_sector", start_p, g_free);
                g_object_set_data_full(G_OBJECT(btn_new), "end_sector", end_p, g_free);
                
                g_signal_connect(btn_new, "clicked", G_CALLBACK(on_action_create), NULL);
                gtk_box_append(GTK_BOX(row_box), btn_new);
            }
        }
        
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(partition_list_box), row);

        if (part_path && !is_free) free(part_path);
        else if (part_path && is_free) g_free(part_path);
    }
    
    ped_disk_destroy(disk);
}

static void on_disk_selected(GObject *gobject, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    refresh_current_disk();
}

static void load_devices() {
    ped_device_probe_all();
    PedDevice *dev = NULL;
    while ((dev = ped_device_get_next(dev))) {
        double size_gb = (double)dev->length * dev->sector_size / (1024.0 * 1024.0 * 1024.0);
        gchar *desc = g_strdup_printf("%s - %.2f GB (%s)", dev->path, size_gb, dev->model);
        gtk_string_list_append(disk_model, desc);
        g_free(desc);
    }
}

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;
    main_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(main_window), _("Karton Parted"));
    gtk_window_set_default_size(GTK_WINDOW(main_window), 850, 600);
    gtk_window_set_icon_name(GTK_WINDOW(main_window), "gnome-disks");

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, 
        ".disk-map { border-radius: 8px; margin: 5px 20px 10px 20px; border: 1px solid alpha(currentColor, 0.1); overflow: hidden; }"
        ".map-segment { padding: 4px 10px; font-size: 0.9em; color: white; min-height: 25px; }"
        ".map-free { background-color: alpha(currentColor, 0.1); color: currentColor; }"
        ".map-ext4 { background-color: #3584e4; }"
        ".map-btrfs { background-color: #26a269; }"
        ".map-swap { background-color: #e01b24; }"
        ".map-ntfs { background-color: #33d17a; }"
        ".map-fat { background-color: #f6d32d; color: black; }"
        ".map-other { background-color: #9141ac; }"
        ".heading { font-weight: bold; font-size: 1.1em; }"
        ".dim-label { opacity: 0.6; }"
    );
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    
    // KartonDE Style: Zwykły blok w układzie, zamiast gtk_header_bar / CSD
    GtkWidget *header_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(header_bar, 20);
    gtk_widget_set_margin_end(header_bar, 20);
    gtk_widget_set_margin_top(header_bar, 10);
    gtk_widget_set_margin_bottom(header_bar, 5);
    
    GtkWidget *title_lbl = gtk_label_new(_("Partition Manager"));
    gtk_widget_add_css_class(title_lbl, "heading");
    gtk_widget_set_hexpand(title_lbl, TRUE);
    gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header_bar), title_lbl);

    disk_model = gtk_string_list_new(NULL);
    disk_dropdown = gtk_drop_down_new(G_LIST_MODEL(disk_model), NULL);
    gtk_widget_set_valign(disk_dropdown, GTK_ALIGN_CENTER);
    g_signal_connect(disk_dropdown, "notify::selected", G_CALLBACK(on_disk_selected), NULL);
    
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(header_box), gtk_label_new(_("Disk: ")));
    gtk_box_append(GTK_BOX(header_box), disk_dropdown);
    gtk_box_append(GTK_BOX(header_bar), header_box);
    
    gtk_box_append(GTK_BOX(vbox), header_bar);
    
    partition_map_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(partition_map_box, "disk-map");
    gtk_box_append(GTK_BOX(vbox), partition_map_box);

    partition_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(partition_list_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(partition_list_box, "boxed-list"); 
    
    // Add margin for nice look
    GtkWidget *list_margin_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(list_margin_box, 20);
    gtk_widget_set_margin_end(list_margin_box, 20);
    gtk_widget_set_margin_top(list_margin_box, 10);
    gtk_widget_set_margin_bottom(list_margin_box, 20);
    gtk_box_append(GTK_BOX(list_margin_box), partition_list_box);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list_margin_box);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);

    gtk_window_set_child(GTK_WINDOW(main_window), vbox);

    // Root check
    if (geteuid() != 0) {
        GtkWidget *banner = gtk_label_new("You are running without root privileges! Disks may not be detected.");
        gtk_widget_add_css_class(banner, "error");
        gtk_widget_set_margin_top(banner, 10);
        gtk_widget_set_margin_bottom(banner, 10);
        gtk_box_insert_child_after(GTK_BOX(vbox), banner, partition_map_box);
    }

    load_devices();
    if (g_list_model_get_n_items(G_LIST_MODEL(disk_model)) > 0) {
         refresh_current_disk();
    } else {
        GtkWidget *err_lbl = gtk_label_new(_("No disks found."));
        gtk_list_box_append(GTK_LIST_BOX(partition_list_box), err_lbl);
    }

    gtk_window_present(GTK_WINDOW(main_window));
}

int main(int argc, char **argv) {
    if (geteuid() != 0) {
        const char *wayland_display = g_getenv("WAYLAND_DISPLAY");
        const char *xdg_runtime_dir = g_getenv("XDG_RUNTIME_DIR");
        const char *display = g_getenv("DISPLAY");
        const char *xauthority = g_getenv("XAUTHORITY");
        char *argv_pkexec[] = {
            "pkexec",
            "env",
            g_strdup_printf("WAYLAND_DISPLAY=%s", wayland_display ? wayland_display : ""),
            g_strdup_printf("XDG_RUNTIME_DIR=%s", xdg_runtime_dir ? xdg_runtime_dir : ""),
            g_strdup_printf("DISPLAY=%s", display ? display : ""),
            g_strdup_printf("XAUTHORITY=%s", xauthority ? xauthority : ""),
            argv[0],
            NULL
        };
        int exit_status = 1;
        GError *spawn_error = NULL;

        if (!g_spawn_sync(NULL, argv_pkexec, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &exit_status, &spawn_error)) {
            g_printerr("Failed to launch pkexec: %s\n", spawn_error->message);
            g_clear_error(&spawn_error);
        }

        g_free(argv_pkexec[2]);
        g_free(argv_pkexec[3]);
        g_free(argv_pkexec[4]);
        g_free(argv_pkexec[5]);
        return exit_status;
    }

    setlocale(LC_ALL, ""); // i18n
    bindtextdomain("karton-parted", "/usr/share/locale");
    textdomain("karton-parted");

    GtkApplication *app = gtk_application_new("io.karton.Parted", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
