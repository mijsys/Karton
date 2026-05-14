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
		"window { background: #0f1620; }"
		".fallback-bg { background: radial-gradient(circle at 20% 20%, #27435f, #0f1620 60%); }"
		".overlay { background: rgba(6, 10, 16, 0.55); }"
		".panel { background: rgba(15, 22, 32, 0.78); border-radius: 18px; padding: 18px; min-width: 340px; }"
		".clock { font-size: 54px; font-weight: 700; color: #e8f0ff; }"
		".date { font-size: 14px; color: #b7cae6; }"
		"entry { min-height: 42px; font-size: 18px; border-radius: 10px; padding: 0 10px; }"
		"button { min-height: 42px; border-radius: 10px; font-weight: 600; }"
		"label.error { color: #ff8f9b; }"
		"label.ok { color: #90e6ae; }";

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
	if (focus_entry) { gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE); } else { gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND); }
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	if (monitor != NULL) {
		gtk_layer_set_monitor(GTK_WINDOW(window), monitor);
	}

	GtkWidget *overlay = gtk_overlay_new();
	gtk_window_set_child(GTK_WINDOW(window), overlay);
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

	GtkWidget *entry = gtk_password_entry_new();
	win->entry = entry;
	gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
	gtk_widget_set_hexpand(entry, TRUE);
	gtk_editable_set_text(GTK_EDITABLE(entry), "");
	g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), win);
        g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), win);
	GtkWidget *button = gtk_button_new_with_label(_("Unlock"));
	g_signal_connect(button, "clicked", G_CALLBACK(on_unlock_clicked), win);
	gtk_box_append(GTK_BOX(center), button);

	GtkWidget *message = gtk_label_new("");
	win->message = message;
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
	normalize_expected_password(&ui);

	GtkApplication *app = gtk_application_new("io.karton.Lock", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), &ui);
	int status = g_application_run(G_APPLICATION(app), argc, argv);

	if (ui.clock_timer_id != 0) {
		g_source_remove(ui.clock_timer_id);
	}
	g_ptr_array_free(ui.windows, TRUE);
	g_clear_pointer(&ui.expected_password, g_free);
	g_object_unref(app);
	return status;
}
