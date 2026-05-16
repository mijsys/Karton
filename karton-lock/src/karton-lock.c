// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include "config.h"

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <glib.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s) gettext(s)

#if HAVE_PAM
#include <security/pam_appl.h>
#endif

struct lock_ui;

struct lock_window {
	struct lock_ui *ui;
	GtkWidget *window;
	GtkWidget *entry;
	GtkWidget *message;
	GtkWidget *clock_label;
	GtkWidget *date_label;
};

struct lock_ui {
	GtkApplication *app;
	GPtrArray *windows;
	gchar *expected_password;
	gboolean expected_password_is_sha256;
	guint failed_attempts;
	gint64 cooldown_until_us;
	guint clock_timer_id;
	gboolean syncing_entries;
	gboolean tty_switch_locked;
	gboolean require_tty_lock;
	int tty_fd;
};

static gboolean
env_truthy(const char *name)
{
	const char *value = g_getenv(name);
	if (value == NULL || *value == '\0') {
		return FALSE;
	}

	return g_ascii_strcasecmp(value, "1") == 0
		|| g_ascii_strcasecmp(value, "yes") == 0
		|| g_ascii_strcasecmp(value, "true") == 0
		|| g_ascii_strcasecmp(value, "on") == 0;
}

#ifdef __linux__
static void
lock_tty_switch(struct lock_ui *ui)
{
	ui->tty_fd = -1;
	ui->tty_switch_locked = FALSE;

	int fd = open("/dev/tty", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fd = open("/dev/tty0", O_RDONLY | O_CLOEXEC);
	}

	if (fd < 0) {
		return;
	}

	if (ioctl(fd, VT_LOCKSWITCH) == 0) {
		ui->tty_fd = fd;
		ui->tty_switch_locked = TRUE;
		return;
	}

	close(fd);
}

static void
unlock_tty_switch(struct lock_ui *ui)
{
	if (!ui->tty_switch_locked || ui->tty_fd < 0) {
		return;
	}

	(void)ioctl(ui->tty_fd, VT_UNLOCKSWITCH);
	close(ui->tty_fd);
	ui->tty_fd = -1;
	ui->tty_switch_locked = FALSE;
}
#else
static void
lock_tty_switch(struct lock_ui *ui)
{
	ui->tty_fd = -1;
	ui->tty_switch_locked = FALSE;
}

static void
unlock_tty_switch(struct lock_ui *ui)
{
	(void)ui;
}
#endif

static gboolean
constant_time_equals(const gchar *a, const gchar *b)
{
        if (a == NULL || b == NULL) {
                return FALSE;
        }


	gsize a_len = strlen(a);
	gsize b_len = strlen(b);
	gsize max_len = a_len > b_len ? a_len : b_len;
	guchar diff = (guchar)(a_len ^ b_len);

	for (gsize i = 0; i < max_len; i++) {
		guchar ac = i < a_len ? (guchar)a[i] : 0;
		guchar bc = i < b_len ? (guchar)b[i] : 0;
		diff |= (guchar)(ac ^ bc);
	}

	return diff == 0;
}

static gchar *
sha256_hex(const gchar *input)
{
	if (input == NULL) {
		return NULL;
	}

	return g_compute_checksum_for_string(G_CHECKSUM_SHA256, input, -1);
}

static gchar *
read_trimmed_first_line(const gchar *path)
{
	gchar *content = NULL;
	gsize len = 0;
	if (!g_file_get_contents(path, &content, &len, NULL)) {
		return NULL;
	}

	gchar **lines = g_strsplit(content, "\n", 2);
	gchar *line = g_strdup(g_strstrip(lines[0]));
	g_strfreev(lines);
	g_free(content);

	if (line[0] == '\0') {
		g_free(line);
		return NULL;
	}

	return line;
}

static gchar *
load_expected_password(void)
{
	const gchar *env = g_getenv("KARTON_LOCK_PASSWORD");
	if (env != NULL && *env != '\0') {
		return g_strdup(env);
	}

	g_autofree gchar *path = g_build_filename(g_get_user_config_dir(), "karton", "lock-password", NULL);
	if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
		return NULL;
	}

	return read_trimmed_first_line(path);
}

static void
normalize_expected_password(struct lock_ui *ui)
{
	if (ui->expected_password == NULL) {
		return;
	}

	if (g_str_has_prefix(ui->expected_password, "sha256:")) {
		gchar *hash = g_strdup(ui->expected_password + 7);
		g_free(ui->expected_password);
		ui->expected_password = hash;
		ui->expected_password_is_sha256 = TRUE;
	}
}

#if HAVE_PAM
struct pam_conv_data {
	const char *password;
};

static int
pam_conversation(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *appdata_ptr)
{
	struct pam_conv_data *data = appdata_ptr;
	struct pam_response *responses = calloc((size_t)num_msg, sizeof(*responses));
	if (responses == NULL) {
		return PAM_CONV_ERR;
	}

	for (int i = 0; i < num_msg; i++) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_OFF:
		case PAM_PROMPT_ECHO_ON:
			responses[i].resp = strdup(data->password ? data->password : "");
			if (responses[i].resp == NULL) {
				for (int j = 0; j < i; j++) {
					free(responses[j].resp);
				}
				free(responses);
				return PAM_CONV_ERR;
			}
			break;
		case PAM_ERROR_MSG:
		case PAM_TEXT_INFO:
			responses[i].resp = NULL;
			break;
		default:
			for (int j = 0; j <= i; j++) {
				free(responses[j].resp);
			}
			free(responses);
			return PAM_CONV_ERR;
		}
	}

	*resp = responses;
	return PAM_SUCCESS;
}

static gboolean
verify_with_pam_service(const char *typed, const char *pam_user, const char *pam_service)
{
	struct pam_conv_data conv_data = {
		.password = typed,
	};
	struct pam_conv conv = {
		.conv = pam_conversation,
		.appdata_ptr = &conv_data,
	};

	pam_handle_t *pamh = NULL;
	int rc = pam_start(pam_service, pam_user, &conv, &pamh);
	if (rc != PAM_SUCCESS) {
		if (pamh != NULL) {
			pam_end(pamh, rc);
		}
		return FALSE;
	}

	rc = pam_authenticate(pamh, 0);
	if (rc == PAM_SUCCESS) {
		rc = pam_acct_mgmt(pamh, 0);
	}

	pam_end(pamh, rc);
	return rc == PAM_SUCCESS;
}

static gboolean
verify_with_pam(const char *typed)
{
	const char *pam_service = g_getenv("KARTON_LOCK_PAM_SERVICE");
	gboolean service_from_env = TRUE;
	if (pam_service == NULL || *pam_service == '\0') {
		pam_service = "karton-lock";
		service_from_env = FALSE;
	}

	const char *pam_user = g_getenv("KARTON_LOCK_PAM_USER");
	if (pam_user == NULL || *pam_user == '\0') {
		pam_user = g_get_user_name();
	}

	if (verify_with_pam_service(typed, pam_user, pam_service)) {
		return TRUE;
	}

	if (!service_from_env && g_strcmp0(pam_service, "login") != 0) {
		return verify_with_pam_service(typed, pam_user, "login");
	}

	return FALSE;
}
#endif

static gboolean
verify_password(struct lock_ui *ui, const char *typed)
{
#if HAVE_PAM
	if (verify_with_pam(typed)) {
		return TRUE;
	}
#endif

	if (ui->expected_password == NULL) {
		return FALSE;
	}

	if (ui->expected_password_is_sha256) {
		g_autofree gchar *typed_hash = sha256_hex(typed);
		return constant_time_equals(typed_hash, ui->expected_password);
	}

	return constant_time_equals(typed, ui->expected_password);
}

static void
update_clock_labels(struct lock_window *win)
{
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree gchar *clock = g_date_time_format(now, "%H:%M");
	g_autofree gchar *date = g_date_time_format(now, "%A, %d %B %Y");

	gtk_label_set_text(GTK_LABEL(win->clock_label), clock);
	gtk_label_set_text(GTK_LABEL(win->date_label), date);
}

static void
show_message_all(struct lock_ui *ui, const char *text, const char *css_class)
{
	for (guint i = 0; i < ui->windows->len; i++) {
		struct lock_window *win = g_ptr_array_index(ui->windows, i);
		gtk_widget_remove_css_class(win->message, "ok");
		gtk_widget_remove_css_class(win->message, "error");
		gtk_widget_remove_css_class(win->message, "warn");
		gtk_widget_add_css_class(win->message, css_class);
		gtk_label_set_text(GTK_LABEL(win->message), text);
	}
}

static gboolean
on_clock_tick(gpointer data)
{
	struct lock_ui *ui = data;
	for (guint i = 0; i < ui->windows->len; i++) {
		update_clock_labels(g_ptr_array_index(ui->windows, i));
	}
	return G_SOURCE_CONTINUE;
}

static gboolean
try_unlock(struct lock_window *win)
{
	struct lock_ui *ui = win->ui;
	gint64 now_us = g_get_monotonic_time();
	if (ui->cooldown_until_us > now_us) {
		gint64 wait_ms = (ui->cooldown_until_us - now_us) / 1000;
		gchar *msg = g_strdup_printf(_("Too many attempts. Try again in %.1f s"), wait_ms / 1000.0);
		show_message_all(ui, msg, "error");
		g_free(msg);
		return FALSE;
	}

	const char *typed = gtk_editable_get_text(GTK_EDITABLE(win->entry));
	if (!verify_password(ui, typed)) {
		ui->failed_attempts++;
		if (ui->failed_attempts >= 5) {
			ui->cooldown_until_us = g_get_monotonic_time() + (3 * G_USEC_PER_SEC);
			ui->failed_attempts = 0;
		}

#if HAVE_PAM
		show_message_all(ui, _("Invalid password"), "error");
#else
		show_message_all(ui, _("Invalid password (or missing KARTON_LOCK_PASSWORD / ~/.config/karton/lock-password)"), "error");
#endif

		for (guint i = 0; i < ui->windows->len; i++) {
			struct lock_window *w = g_ptr_array_index(ui->windows, i);
			gtk_editable_set_text(GTK_EDITABLE(w->entry), "");
		}
		return FALSE;
	}

	ui->failed_attempts = 0;
	ui->cooldown_until_us = 0;
	show_message_all(ui, _("Unlocked"), "ok");
	g_application_quit(G_APPLICATION(ui->app));
	return TRUE;
}

static void
on_entry_changed(GtkEditable *editable, gpointer data)
{
	struct lock_window *win = data;
	struct lock_ui *ui = win->ui;

	if (ui->syncing_entries) {
		return;
	}

	ui->syncing_entries = TRUE;
	const char *text = gtk_editable_get_text(editable);

	for (guint i = 0; i < ui->windows->len; i++) {
		struct lock_window *w = g_ptr_array_index(ui->windows, i);
		if (w->entry != GTK_WIDGET(editable)) {
			gtk_editable_set_text(GTK_EDITABLE(w->entry), text);
		}
	}
	ui->syncing_entries = FALSE;
}

static gboolean
is_vt_function_key(guint keyval)
{
	return keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F12;
}

static gboolean
on_key_pressed(GtkEventControllerKey *controller,
	      guint keyval,
	      guint keycode,
	      GdkModifierType state,
	      gpointer data)
{
	(void)controller;
	(void)keycode;
	(void)data;

	gboolean alt = (state & GDK_ALT_MASK) != 0;
	gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
	gboolean super = (state & GDK_SUPER_MASK) != 0;

	if (alt && (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_Escape)) {
		return TRUE;
	}

	if (super && (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)) {
		return TRUE;
	}

	if (ctrl && alt && is_vt_function_key(keyval)) {
		return TRUE;
	}

	if (ctrl && alt && (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right)) {
		return TRUE;
	}

	return FALSE;
}

static void
on_entry_activate(GtkWidget *entry, gpointer data)
{
	(void)entry;
	struct lock_window *win = data;
	(void)try_unlock(win);
}

static void
on_unlock_clicked(GtkButton *button, gpointer data)
{
	(void)button;
	struct lock_window *win = data;
	(void)try_unlock(win);
}

static void
on_any_click_pressed(GtkGestureClick *gesture,
		     gint n_press,
		     gdouble x,
		     gdouble y,
		     gpointer data)
{
	(void)gesture;
	(void)n_press;
	(void)x;
	(void)y;
	struct lock_window *win = data;
	if (win->entry != NULL) {
		gtk_widget_grab_focus(win->entry);
	}
}

static gboolean
on_close_request(GtkWindow *window, gpointer data)
{
	(void)window;
	(void)data;
	return TRUE;
}

static GtkWidget *
make_background_widget(void)
{
	g_autofree gchar *path = g_build_filename(g_get_user_config_dir(), "karton", "lockscreen-path", NULL);
	g_autofree gchar *wallpaper = NULL;
	if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
		wallpaper = read_trimmed_first_line(path);
	}

	if (wallpaper != NULL && g_file_test(wallpaper, G_FILE_TEST_IS_REGULAR)) {
		GtkWidget *picture = gtk_picture_new_for_filename(wallpaper);
		gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
		gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
		return picture;
	}

	GtkWidget *fallback = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(fallback, "fallback-bg");
	return fallback;
}

static void
apply_css(void)
{
	const char *css =
		"window { background: #0b1017; }"
		".fallback-bg { background: radial-gradient(circle at 25% 20%, #2a4d73, #0b1017 62%); }"
		".overlay { background: rgba(4, 8, 14, 0.60); }"
		".panel {"
		"  background: linear-gradient(to bottom, rgba(26, 38, 54, 0.93), rgba(14, 22, 34, 0.93));"
		"  border: 1px solid rgba(175, 210, 255, 0.16);"
		"  border-radius: 22px;"
		"  padding: 24px;"
		"  min-width: 440px;"
		"}"
		".clock { font-size: 56px; font-weight: 800; color: #f3f8ff; letter-spacing: 1px; }"
		".date { font-size: 14px; color: #b8cde8; margin-bottom: 10px; }"
		".title { font-size: 20px; font-weight: 700; color: #eef5ff; }"
		".subtitle { font-size: 13px; color: #b9cde4; margin-bottom: 8px; }"
		".form-card {"
		"  background: rgba(10, 16, 25, 0.55);"
		"  border: 1px solid rgba(169, 201, 244, 0.18);"
		"  border-radius: 14px;"
		"  padding: 14px;"
		"}"
		".field-label { font-size: 12px; font-weight: 700; color: #c4d8f2; margin-bottom: 6px; }"
		"entry { min-height: 44px; font-size: 18px; border-radius: 10px; padding: 0 10px; }"
		"button { min-height: 44px; border-radius: 10px; font-weight: 700; }"
		".message { margin-top: 4px; }"
		"label.error { color: #ff8f9b; }"
		"label.ok { color: #90e6ae; }"
		"label.warn { color: #f5d57b; }";

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_string(provider, css);
	gtk_style_context_add_provider_for_display(
		gdk_display_get_default(),
		GTK_STYLE_PROVIDER(provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
}

static struct lock_window *
create_lock_window(struct lock_ui *ui, GtkApplication *app, GdkMonitor *monitor, gboolean focus_entry)
{
	(void)focus_entry;

	struct lock_window *win = g_new0(struct lock_window, 1);
	win->ui = ui;

	GtkWidget *window = gtk_application_window_new(app);
	win->window = window;
	gtk_window_set_title(GTK_WINDOW(window), _("Karton Lock"));
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), win);

	gtk_layer_init_for_window(GTK_WINDOW(window));
	gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
	gtk_layer_set_exclusive_zone(GTK_WINDOW(window), -1);
	gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	if (monitor != NULL) {
		gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
	}

	GtkWidget *overlay = gtk_overlay_new();
	gtk_window_set_child(GTK_WINDOW(window), overlay);
	GtkEventController *key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), win);
	gtk_widget_add_controller(window, key_controller);

	GtkGesture *click = gtk_gesture_click_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
	g_signal_connect(click, "pressed", G_CALLBACK(on_any_click_pressed), win);
	gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(click));

	GtkWidget *background = make_background_widget();
	gtk_overlay_set_child(GTK_OVERLAY(overlay), background);

	GtkWidget *shade = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(shade, "overlay");
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), shade);

	GtkWidget *center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_widget_set_halign(center, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(center, GTK_ALIGN_CENTER);
	gtk_widget_add_css_class(center, "panel");
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), center);

	GtkWidget *clock = gtk_label_new("");
	win->clock_label = clock;
	gtk_widget_add_css_class(clock, "clock");
	gtk_box_append(GTK_BOX(center), clock);

	GtkWidget *date = gtk_label_new("");
	win->date_label = date;
	gtk_widget_add_css_class(date, "date");
	gtk_box_append(GTK_BOX(center), date);

	GtkWidget *title = gtk_label_new(_("Screen is locked"));
	gtk_widget_add_css_class(title, "title");
	gtk_widget_set_halign(title, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(center), title);

	GtkWidget *subtitle = gtk_label_new(_("Enter your password to unlock this session"));
	gtk_widget_add_css_class(subtitle, "subtitle");
	gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(center), subtitle);

	GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_add_css_class(form, "form-card");
	gtk_box_append(GTK_BOX(center), form);

	GtkWidget *field_label = gtk_label_new(_("Password"));
	gtk_widget_add_css_class(field_label, "field-label");
	gtk_widget_set_halign(field_label, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(form), field_label);

	GtkWidget *entry = gtk_password_entry_new();
	win->entry = entry;
	gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_editable_set_text(GTK_EDITABLE(entry), "");
	g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), win);
	g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), win);
	gtk_box_append(GTK_BOX(form), entry);

	GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_append(GTK_BOX(form), actions);
	gtk_widget_set_halign(actions, GTK_ALIGN_FILL);
	GtkWidget *button = gtk_button_new_with_label(_("Unlock"));
	g_signal_connect(button, "clicked", G_CALLBACK(on_unlock_clicked), win);
	gtk_widget_set_hexpand(button, TRUE);
	gtk_box_append(GTK_BOX(actions), button);

	GtkWidget *message = gtk_label_new("");
	win->message = message;
	gtk_widget_add_css_class(message, "message");
	gtk_widget_set_halign(message, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(center), message);

	update_clock_labels(win);
	gtk_window_present(GTK_WINDOW(window));
	if (focus_entry) {
		gtk_widget_grab_focus(entry);
	}

	return win;
}

static void
activate(GtkApplication *app, gpointer user_data)
{
	struct lock_ui *ui = user_data;
	ui->app = app;
	lock_tty_switch(ui);

	apply_css();

	GdkDisplay *display = gdk_display_get_default();
	GListModel *monitors = gdk_display_get_monitors(display);
	guint n_monitors = g_list_model_get_n_items(monitors);

	if (n_monitors == 0) {
		struct lock_window *win = create_lock_window(ui, app, NULL, TRUE);
		g_ptr_array_add(ui->windows, win);
	} else {
		for (guint i = 0; i < n_monitors; i++) {
			GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, i));
			struct lock_window *win = create_lock_window(ui, app, monitor, i == 0);
			g_ptr_array_add(ui->windows, win);
			g_object_unref(monitor);
		}
	}

	ui->clock_timer_id = g_timeout_add_seconds(1, on_clock_tick, ui);

	if (!ui->tty_switch_locked) {
		if (ui->require_tty_lock) {
			show_message_all(ui, _("TTY switch lock failed. Enable CAP_SYS_TTY_CONFIG/root privileges for strict lock."), "error");
		} else {
			show_message_all(ui, _("Warning: TTY switch could not be kernel-locked in this session."), "warn");
		}
	}
}

int
main(int argc, char **argv)
{
	setlocale(LC_ALL, "");
	const char *locale_dir = g_getenv("KARTON_LOCALEDIR");
	if (locale_dir == NULL || *locale_dir == '\0') {
		locale_dir = LOCALEDIR;
	}
	bindtextdomain("karton-lock", locale_dir);
	bind_textdomain_codeset("karton-lock", "UTF-8");
	textdomain("karton-lock");

	struct lock_ui ui = { 0 };
	ui.windows = g_ptr_array_new_with_free_func(g_free);
	ui.expected_password = load_expected_password();
	ui.require_tty_lock = env_truthy("KARTON_LOCK_REQUIRE_TTY_LOCK");
	ui.tty_fd = -1;
	normalize_expected_password(&ui);

	GtkApplication *app = gtk_application_new("io.karton.Lock", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), &ui);
	int status = g_application_run(G_APPLICATION(app), argc, argv);

	if (ui.clock_timer_id != 0) {
		g_source_remove(ui.clock_timer_id);
	}
	unlock_tty_switch(&ui);
	g_ptr_array_free(ui.windows, TRUE);
	g_clear_pointer(&ui.expected_password, g_free);
	g_object_unref(app);
	return status;
}
