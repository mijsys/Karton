#include "page-files-disks.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libintl.h>

#define _(s) gettext(s)

static GtkWidget *g_mount_switch = NULL;
static GtkWidget *g_partitions_switch = NULL;
static GtkWidget *g_automount_switch = NULL;
static GtkWidget *g_network_folders_switch = NULL;
static GtkWidget *g_trash_switch = NULL;
static GtkWidget *g_thumbnails_switch = NULL;
static GtkWidget *g_permissions_switch = NULL;
static GtkWidget *g_encryption_switch = NULL;
static GtkWidget *g_device_entry = NULL;
static GtkWidget *g_mountpoint_entry = NULL;
static GtkWidget *g_devices_text = NULL;
static GtkWidget *g_status_label = NULL;

static gboolean command_is_available(const char *name)
{
    char *tool = g_find_program_in_path(name);
    if (!tool) {
        return FALSE;
    }

    g_free(tool);
    return TRUE;
}

static gboolean run_command_success(const char *command)
{
    int wait_status = 0;
    gboolean ok = g_spawn_command_line_sync(command, NULL, NULL, &wait_status, NULL);
    if (!ok) {
        return FALSE;
    }

    return g_spawn_check_wait_status(wait_status, NULL);
}

static gboolean run_command_capture(const char *command, char **stdout_out)
{
    gchar *stdout_data = NULL;
    int wait_status = 0;
    gboolean ok = g_spawn_command_line_sync(command,
                                            stdout_out ? &stdout_data : NULL,
                                            NULL,
                                            &wait_status,
                                            NULL);
    if (!ok) {
        g_free(stdout_data);
        return FALSE;
    }

    if (stdout_out) {
        *stdout_out = stdout_data;
    } else {
        g_free(stdout_data);
    }

    return g_spawn_check_wait_status(wait_status, NULL);
}

static GtkWidget *create_row(const char *title, GtkWidget *control)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), label);

    if (control) {
        gtk_widget_set_halign(control, GTK_ALIGN_END);
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
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

static char *files_disks_config_path(void)
{
    return g_build_filename(g_get_home_dir(), ".config", "karton", "files-disks.conf", NULL);
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

static void save_files_disks_config(void)
{
    GKeyFile *kf = g_key_file_new();

    g_key_file_set_boolean(kf, "files_disks", "mount_disks", gtk_switch_get_active(GTK_SWITCH(g_mount_switch)));
    g_key_file_set_boolean(kf, "files_disks", "partitions", gtk_switch_get_active(GTK_SWITCH(g_partitions_switch)));
    g_key_file_set_boolean(kf, "files_disks", "automount", gtk_switch_get_active(GTK_SWITCH(g_automount_switch)));
    g_key_file_set_boolean(kf, "files_disks", "network_folders", gtk_switch_get_active(GTK_SWITCH(g_network_folders_switch)));
    g_key_file_set_boolean(kf, "files_disks", "trash", gtk_switch_get_active(GTK_SWITCH(g_trash_switch)));
    g_key_file_set_boolean(kf, "files_disks", "thumbnails", gtk_switch_get_active(GTK_SWITCH(g_thumbnails_switch)));
    g_key_file_set_boolean(kf, "files_disks", "file_permissions", gtk_switch_get_active(GTK_SWITCH(g_permissions_switch)));
    g_key_file_set_boolean(kf, "files_disks", "disk_encryption", gtk_switch_get_active(GTK_SWITCH(g_encryption_switch)));

    char *data = g_key_file_to_data(kf, NULL, NULL);
    char *path = files_disks_config_path();
    char *dir = g_path_get_dirname(path);

    if (g_mkdir_with_parents(dir, 0700) == 0) {
        g_file_set_contents(path, data, -1, NULL);
    }

    g_free(dir);
    g_free(path);
    g_free(data);
    g_key_file_unref(kf);
}

static void load_files_disks_config(void)
{
    char *path = files_disks_config_path();
    GKeyFile *kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(kf);
        g_free(path);
        return;
    }

    GError *error = NULL;

    gboolean mount_disks = g_key_file_get_boolean(kf, "files_disks", "mount_disks", &error);
    if (error) {
        g_clear_error(&error);
        mount_disks = TRUE;
    }

    gboolean partitions = g_key_file_get_boolean(kf, "files_disks", "partitions", &error);
    if (error) {
        g_clear_error(&error);
        partitions = TRUE;
    }

    gboolean automount = g_key_file_get_boolean(kf, "files_disks", "automount", &error);
    if (error) {
        g_clear_error(&error);
        automount = TRUE;
    }

    gboolean network_folders = g_key_file_get_boolean(kf, "files_disks", "network_folders", &error);
    if (error) {
        g_clear_error(&error);
        network_folders = TRUE;
    }

    gboolean trash = g_key_file_get_boolean(kf, "files_disks", "trash", &error);
    if (error) {
        g_clear_error(&error);
        trash = TRUE;
    }

    gboolean thumbnails = g_key_file_get_boolean(kf, "files_disks", "thumbnails", &error);
    if (error) {
        g_clear_error(&error);
        thumbnails = TRUE;
    }

    gboolean file_permissions = g_key_file_get_boolean(kf, "files_disks", "file_permissions", &error);
    if (error) {
        g_clear_error(&error);
        file_permissions = TRUE;
    }

    gboolean disk_encryption = g_key_file_get_boolean(kf, "files_disks", "disk_encryption", &error);
    if (error) {
        g_clear_error(&error);
        disk_encryption = FALSE;
    }

    gtk_switch_set_active(GTK_SWITCH(g_mount_switch), mount_disks);
    gtk_switch_set_active(GTK_SWITCH(g_partitions_switch), partitions);
    gtk_switch_set_active(GTK_SWITCH(g_automount_switch), automount);
    gtk_switch_set_active(GTK_SWITCH(g_network_folders_switch), network_folders);
    gtk_switch_set_active(GTK_SWITCH(g_trash_switch), trash);
    gtk_switch_set_active(GTK_SWITCH(g_thumbnails_switch), thumbnails);
    gtk_switch_set_active(GTK_SWITCH(g_permissions_switch), file_permissions);
    gtk_switch_set_active(GTK_SWITCH(g_encryption_switch), disk_encryption);

    g_key_file_unref(kf);
    g_free(path);
}

static void refresh_devices_list(void)
{
    if (!g_devices_text) {
        return;
    }

    char *out = NULL;
    gboolean ok = run_command_capture("sh -lc 'lsblk -o NAME,SIZE,FSTYPE,TYPE,MOUNTPOINT -nr 2>/dev/null'", &out);
    if (!ok || !out || !*out) {
        gtk_label_set_text(GTK_LABEL(g_devices_text), _("Could not read block devices list."));
        g_free(out);
        return;
    }

    gtk_label_set_text(GTK_LABEL(g_devices_text), out);
    g_free(out);
}

static gboolean apply_device_mount_action(gboolean do_mount)
{
    const char *dev = gtk_editable_get_text(GTK_EDITABLE(g_device_entry));
    const char *mnt = gtk_editable_get_text(GTK_EDITABLE(g_mountpoint_entry));

    if (!dev || !*dev) {
        status_set(_("Provide a block device path (for example /dev/sdb1)."), TRUE);
        return FALSE;
    }

    char *q_dev = g_shell_quote(dev);
    gboolean ok = FALSE;

    if (command_is_available("udisksctl")) {
        char *cmd = g_strdup_printf("sh -lc 'udisksctl %s -b %s >/dev/null 2>&1'",
                                    do_mount ? "mount" : "unmount",
                                    q_dev);
        ok = run_command_success(cmd);
        g_free(cmd);
    } else if (do_mount && mnt && *mnt && command_is_available("sudo")) {
        char *q_mnt = g_shell_quote(mnt);
        char *cmd = g_strdup_printf("sh -lc 'sudo mount %s %s >/dev/null 2>&1'", q_dev, q_mnt);
        ok = run_command_success(cmd);
        g_free(cmd);
        g_free(q_mnt);
    }

    g_free(q_dev);

    if (!ok) {
        status_set(do_mount
                       ? _("Could not mount selected device. Install udisks2 or use sudo mount with mount point.")
                       : _("Could not unmount selected device. Install udisks2."),
                   TRUE);
        return FALSE;
    }

    refresh_devices_list();
    status_set(do_mount ? _("Device mounted successfully.") : _("Device unmounted successfully."), FALSE);
    return TRUE;
}

static void on_refresh_devices_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    refresh_devices_list();
    status_set(_("Device list refreshed."), FALSE);
}

static void on_mount_device_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    (void)apply_device_mount_action(TRUE);
}

static void on_unmount_device_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;
    (void)apply_device_mount_action(FALSE);
}

static void on_open_partitioner_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    gboolean ok = FALSE;
    if (command_is_available("gnome-disks")) {
        ok = run_command_success("sh -lc 'gnome-disks >/dev/null 2>&1 &' ");
    } else if (command_is_available("partitionmanager")) {
        ok = run_command_success("sh -lc 'partitionmanager >/dev/null 2>&1 &' ");
    } else if (command_is_available("gparted")) {
        ok = run_command_success("sh -lc 'gparted >/dev/null 2>&1 &' ");
    }

    if (!ok) {
        status_set(_("No partition management tool found (gnome-disks/partitionmanager/gparted)."), TRUE);
        return;
    }

    status_set(_("Partition management tool launched."), FALSE);
}

static void on_unlock_luks_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    const char *dev = gtk_editable_get_text(GTK_EDITABLE(g_device_entry));
    if (!dev || !*dev) {
        status_set(_("Provide encrypted device path to unlock (for example /dev/nvme0n1p3)."), TRUE);
        return;
    }

    if (!command_is_available("udisksctl")) {
        status_set(_("Could not unlock encrypted device: udisksctl is not available."), TRUE);
        return;
    }

    char *q_dev = g_shell_quote(dev);
    char *cmd = g_strdup_printf("sh -lc 'udisksctl unlock -b %s >/dev/null 2>&1'", q_dev);
    gboolean ok = run_command_success(cmd);
    g_free(cmd);
    g_free(q_dev);

    if (!ok) {
        status_set(_("Could not unlock encrypted device."), TRUE);
        return;
    }

    refresh_devices_list();
    status_set(_("Encrypted device unlocked."), FALSE);
}

static char *apply_runtime_files_disks(void)
{
    gboolean mount_disks = gtk_switch_get_active(GTK_SWITCH(g_mount_switch));
    gboolean partitions = gtk_switch_get_active(GTK_SWITCH(g_partitions_switch));
    gboolean automount = gtk_switch_get_active(GTK_SWITCH(g_automount_switch));
    gboolean network_folders = gtk_switch_get_active(GTK_SWITCH(g_network_folders_switch));
    gboolean trash = gtk_switch_get_active(GTK_SWITCH(g_trash_switch));
    gboolean thumbnails = gtk_switch_get_active(GTK_SWITCH(g_thumbnails_switch));
    gboolean file_permissions = gtk_switch_get_active(GTK_SWITCH(g_permissions_switch));
    gboolean disk_encryption = gtk_switch_get_active(GTK_SWITCH(g_encryption_switch));

    GString *issues = g_string_new(NULL);
    GString *env_block = g_string_new(NULL);

    g_string_append_printf(env_block,
                           "KARTON_FILES_MOUNT_DISKS=%s\n"
                           "KARTON_FILES_PARTITIONS=%s\n"
                           "KARTON_FILES_AUTOMOUNT=%s\n"
                           "KARTON_FILES_NETWORK_FOLDERS=%s\n"
                           "KARTON_FILES_TRASH=%s\n"
                           "KARTON_FILES_THUMBNAILS=%s\n"
                           "KARTON_FILES_PERMISSIONS=%s\n"
                           "KARTON_FILES_DISK_ENCRYPTION=%s",
                           mount_disks ? "1" : "0",
                           partitions ? "1" : "0",
                           automount ? "1" : "0",
                           network_folders ? "1" : "0",
                           trash ? "1" : "0",
                           thumbnails ? "1" : "0",
                           file_permissions ? "1" : "0",
                           disk_encryption ? "1" : "0");

    char *env_path = session_environment_path();
    gboolean env_ok = write_managed_env_block(env_path,
                                              "# BEGIN KartON managed files/disks env",
                                              "# END KartON managed files/disks env",
                                              env_block->str);
    if (!env_ok) {
        g_string_append(issues, _("Could not persist file manager and disks environment settings. "));
    }

    if (command_is_available("dbus-update-activation-environment")) {
        char *cmd = g_strdup_printf(
            "sh -lc 'dbus-update-activation-environment --systemd KARTON_FILES_MOUNT_DISKS=%s KARTON_FILES_PARTITIONS=%s KARTON_FILES_AUTOMOUNT=%s KARTON_FILES_NETWORK_FOLDERS=%s KARTON_FILES_TRASH=%s KARTON_FILES_THUMBNAILS=%s KARTON_FILES_PERMISSIONS=%s KARTON_FILES_DISK_ENCRYPTION=%s >/dev/null 2>&1 || true'",
            mount_disks ? "1" : "0",
            partitions ? "1" : "0",
            automount ? "1" : "0",
            network_folders ? "1" : "0",
            trash ? "1" : "0",
            thumbnails ? "1" : "0",
            file_permissions ? "1" : "0",
            disk_encryption ? "1" : "0");
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    if (command_is_available("gsettings")) {
        char *cmd = g_strdup_printf("sh -lc 'gsettings set org.gnome.desktop.media-handling automount %s >/dev/null 2>&1 || true'",
                                    automount ? "true" : "false");
        (void)run_command_success(cmd);
        g_free(cmd);
    }

    (void)run_command_success("sh -lc 'pkill -USR1 -x karton-settingsd >/dev/null 2>&1 || true'");

    g_free(env_path);
    g_string_free(env_block, TRUE);

    if (issues->len == 0) {
        g_string_free(issues, TRUE);
        return NULL;
    }

    return g_string_free(issues, FALSE);
}

static void on_reload_files_disks_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    load_files_disks_config();
    refresh_devices_list();
    status_set(_("File manager and disks settings reloaded"), FALSE);
}

static void on_apply_files_disks_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    (void)data;

    save_files_disks_config();
    char *issues = apply_runtime_files_disks();
    if (issues) {
        status_set(issues, TRUE);
        g_free(issues);
        return;
    }

    status_set(_("File manager and disks settings applied"), FALSE);
}

GtkWidget *page_files_disks_new(void)
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

    GtkWidget *title = gtk_label_new(_("File manager and disks"));
    gtk_widget_add_css_class(title, "appearance-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new(_("Storage settings for managing files, partitions and connected media."));
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "appearance-subtitle");

    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(box), hero);

    GtkWidget *features_frame = create_section(_("Storage and file operations"),
                                               _("Manage mass-storage behavior and file-manager integration."));
    GtkWidget *features_box = gtk_frame_get_child(GTK_FRAME(features_frame));

    g_mount_switch = gtk_switch_new();
    g_partitions_switch = gtk_switch_new();
    g_automount_switch = gtk_switch_new();
    g_network_folders_switch = gtk_switch_new();
    g_trash_switch = gtk_switch_new();
    g_thumbnails_switch = gtk_switch_new();
    g_permissions_switch = gtk_switch_new();
    g_encryption_switch = gtk_switch_new();

    gtk_box_append(GTK_BOX(features_box), create_row(_("Mounting disks"), g_mount_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Partitions"), g_partitions_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Automount"), g_automount_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Network folders"), g_network_folders_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Trash"), g_trash_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Thumbnails"), g_thumbnails_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("File permissions"), g_permissions_switch));
    gtk_box_append(GTK_BOX(features_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(features_box), create_row(_("Disk encryption"), g_encryption_switch));

    gtk_box_append(GTK_BOX(box), features_frame);

    GtkWidget *ops_frame = create_section(_("Direct disk operations"),
                                          _("Perform mount/unmount/unlock actions on selected block device."));
    GtkWidget *ops_box = gtk_frame_get_child(GTK_FRAME(ops_frame));

    g_device_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_device_entry), "/dev/sdb1");
    g_mountpoint_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_mountpoint_entry), "/mnt/data");

    gtk_box_append(GTK_BOX(ops_box), create_row(_("Device"), g_device_entry));
    gtk_box_append(GTK_BOX(ops_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(ops_box), create_row(_("Mount point (optional)"), g_mountpoint_entry));

    GtkWidget *ops_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *refresh_btn = gtk_button_new_with_label(_("Refresh devices"));
    GtkWidget *mount_btn = gtk_button_new_with_label(_("Mount"));
    GtkWidget *unmount_btn = gtk_button_new_with_label(_("Unmount"));
    GtkWidget *unlock_btn = gtk_button_new_with_label(_("Unlock encrypted"));
    GtkWidget *partition_btn = gtk_button_new_with_label(_("Open partition manager"));

    g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_devices_clicked), NULL);
    g_signal_connect(mount_btn, "clicked", G_CALLBACK(on_mount_device_clicked), NULL);
    g_signal_connect(unmount_btn, "clicked", G_CALLBACK(on_unmount_device_clicked), NULL);
    g_signal_connect(unlock_btn, "clicked", G_CALLBACK(on_unlock_luks_clicked), NULL);
    g_signal_connect(partition_btn, "clicked", G_CALLBACK(on_open_partitioner_clicked), NULL);

    gtk_box_append(GTK_BOX(ops_buttons), refresh_btn);
    gtk_box_append(GTK_BOX(ops_buttons), mount_btn);
    gtk_box_append(GTK_BOX(ops_buttons), unmount_btn);
    gtk_box_append(GTK_BOX(ops_buttons), unlock_btn);
    gtk_box_append(GTK_BOX(ops_buttons), partition_btn);
    gtk_box_append(GTK_BOX(ops_box), ops_buttons);

    g_devices_text = gtk_label_new("");
    gtk_widget_set_halign(g_devices_text, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_devices_text), TRUE);
    gtk_widget_add_css_class(g_devices_text, "row-subtitle");
    gtk_box_append(GTK_BOX(ops_box), g_devices_text);

    gtk_box_append(GTK_BOX(box), ops_frame);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);

    GtkWidget *reload_btn = gtk_button_new_with_label(_("Reload saved settings"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_files_disks_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), reload_btn);

    GtkWidget *apply_btn = gtk_button_new_with_label(_("Apply file manager and disks settings"));
    gtk_widget_add_css_class(apply_btn, "suggested-action");
    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_files_disks_clicked), NULL);
    gtk_box_append(GTK_BOX(actions), apply_btn);

    gtk_box_append(GTK_BOX(box), actions);

    g_status_label = gtk_label_new(_("Settings are saved and propagated to the current session environment."));
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(g_status_label), TRUE);
    gtk_widget_add_css_class(g_status_label, "row-subtitle");
    gtk_box_append(GTK_BOX(box), g_status_label);

    gtk_switch_set_active(GTK_SWITCH(g_mount_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_partitions_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_automount_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_network_folders_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_trash_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_thumbnails_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_permissions_switch), TRUE);
    gtk_switch_set_active(GTK_SWITCH(g_encryption_switch), FALSE);

    load_files_disks_config();
    refresh_devices_list();

    return outer_scroll;
}
