// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "karton-ext-idle-notify-client-protocol.h"

enum idle_kind {
	IDLE_KIND_BLANK,
	IDLE_KIND_LOCK,
};

struct karton_idle;
struct seat_state;

struct idle_notification {
	struct karton_idle *ctx;
	struct seat_state *seat;
	enum idle_kind kind;
	struct ext_idle_notification_v1 *notification;
};

struct seat_state {
	struct karton_idle *ctx;
	uint32_t global_name;
	struct wl_seat *seat;
	struct idle_notification blank;
	struct idle_notification lock;
	gboolean blank_idle;
	gboolean lock_idle;
};

struct karton_idle {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ext_idle_notifier_v1 *notifier;
	GMainLoop *main_loop;
	GPtrArray *seats;
	guint wayland_watch_id;
	guint blank_idle_count;
	guint lock_idle_count;
	guint blank_timeout;
	guint lock_delay;
	gboolean lock_enabled;
	const char *idle_off_command;
	const char *idle_on_command;
	const char *idle_lock_command;
};

static guint
parse_uint_text(const char *text, guint fallback)
{
	if (text == NULL || *text == '\0') {
		return fallback;
	}

	char *end = NULL;
	unsigned long value = g_ascii_strtoull(text, &end, 10);
	if (end == text || (end != NULL && *end != '\0')) {
		return fallback;
	}

	return (guint)value;
}

static gboolean
parse_bool_text(const char *text, gboolean fallback)
{
	if (text == NULL || *text == '\0') {
		return fallback;
	}

	if (g_ascii_strcasecmp(text, "true") == 0 || g_ascii_strcasecmp(text, "yes") == 0 || g_ascii_strcasecmp(text, "1") == 0) {
		return TRUE;
	}

	if (g_ascii_strcasecmp(text, "false") == 0 || g_ascii_strcasecmp(text, "no") == 0 || g_ascii_strcasecmp(text, "0") == 0) {
		return FALSE;
	}

	return fallback;
}

static char *
spawn_command_output(const char *command)
{
	gchar *stdout_data = NULL;
	gchar *stderr_data = NULL;
	gint wait_status = 0;
	GError *error = NULL;

	if (!g_spawn_command_line_sync(command, &stdout_data, &stderr_data, &wait_status, &error)) {
		g_clear_error(&error);
		g_free(stderr_data);
		return NULL;
	}

	if (!g_spawn_check_wait_status(wait_status, &error)) {
		g_clear_error(&error);
		g_free(stdout_data);
		g_free(stderr_data);
		return NULL;
	}

	g_free(stderr_data);
	return stdout_data;
}

static guint
load_timeout_setting(const char *env_name, const char *schema, const char *key, guint fallback)
{
	const char *env_value = g_getenv(env_name);
	if (env_value != NULL && *env_value != '\0') {
		return parse_uint_text(env_value, fallback);
	}

	g_autofree char *gsettings_path = g_find_program_in_path("gsettings");
	if (gsettings_path == NULL) {
		return fallback;
	}

	g_autofree char *command = g_strdup_printf("gsettings get %s %s", schema, key);
	g_autofree char *output = spawn_command_output(command);
	if (output == NULL) {
		return fallback;
	}

	g_strstrip(output);
	if (g_str_has_prefix(output, "uint32 ")) {
		return parse_uint_text(output + 7, fallback);
	}

	return parse_uint_text(output, fallback);
}

static gboolean
load_bool_setting(const char *env_name, const char *schema, const char *key, gboolean fallback)
{
	const char *env_value = g_getenv(env_name);
	if (env_value != NULL && *env_value != '\0') {
		return parse_bool_text(env_value, fallback);
	}

	g_autofree char *gsettings_path = g_find_program_in_path("gsettings");
	if (gsettings_path == NULL) {
		return fallback;
	}

	g_autofree char *command = g_strdup_printf("gsettings get %s %s", schema, key);
	g_autofree char *output = spawn_command_output(command);
	if (output == NULL) {
		return fallback;
	}

	g_strstrip(output);
	return parse_bool_text(output, fallback);
}

static const char *
load_command(const char *env_name, const char *fallback)
{
	const char *value = g_getenv(env_name);
	if (value != NULL && *value != '\0') {
		return value;
	}

	return fallback;
}

static guint
seconds_to_milliseconds(guint seconds)
{
	if (seconds > G_MAXUINT32 / 1000) {
		return G_MAXUINT32;
	}

	return seconds * 1000;
}

static void
run_command(const char *command)
{
	if (command == NULL || *command == '\0') {
		return;
	}

	gchar **argv = NULL;
	int argc = 0;
	GError *error = NULL;

	if (!g_shell_parse_argv(command, &argc, &argv, &error)) {
		g_printerr("karton-idle: invalid command '%s': %s\n", command, error->message);
		g_clear_error(&error);
		return;
	}

	if (argc == 0 || argv == NULL || argv[0] == NULL) {
		g_printerr("karton-idle: command '%s' resolved to empty argv\n", command);
		g_strfreev(argv);
		return;
	}

	if (!g_spawn_async(
		NULL,
		argv,
		NULL,
		G_SPAWN_SEARCH_PATH,
		NULL,
		NULL,
		NULL,
		&error)) {
		g_printerr("karton-idle: failed to run '%s': %s\n", command, error->message);
		g_clear_error(&error);
	}

	g_strfreev(argv);
}

static void
update_resume_state(struct karton_idle *ctx)
{
	if (ctx->blank_idle_count == 0 && ctx->lock_idle_count == 0) {
		run_command(ctx->idle_on_command);
	}
}

static void
handle_idle_notification_idled(void *data, struct ext_idle_notification_v1 *notification)
{
	(void)notification;
	struct idle_notification *idle_notification = data;
	struct karton_idle *ctx = idle_notification->ctx;
	struct seat_state *seat = idle_notification->seat;

	if (idle_notification->kind == IDLE_KIND_BLANK) {
		if (!seat->blank_idle) {
			seat->blank_idle = TRUE;
			ctx->blank_idle_count++;
			if (ctx->blank_idle_count == 1) {
				run_command(ctx->idle_off_command);
			}
		}
		return;
	}

	if (!seat->lock_idle) {
		seat->lock_idle = TRUE;
		ctx->lock_idle_count++;
		if (ctx->lock_idle_count == 1) {
			run_command(ctx->idle_lock_command);
		}
	}
}

static void
handle_idle_notification_resumed(void *data, struct ext_idle_notification_v1 *notification)
{
	(void)notification;
	struct idle_notification *idle_notification = data;
	struct karton_idle *ctx = idle_notification->ctx;
	struct seat_state *seat = idle_notification->seat;

	if (idle_notification->kind == IDLE_KIND_BLANK) {
		if (seat->blank_idle) {
			seat->blank_idle = FALSE;
			if (ctx->blank_idle_count > 0) {
				ctx->blank_idle_count--;
			}
		}
	} else {
		if (seat->lock_idle) {
			seat->lock_idle = FALSE;
			if (ctx->lock_idle_count > 0) {
				ctx->lock_idle_count--;
			}
		}
	}

	update_resume_state(ctx);
}

static const struct ext_idle_notification_v1_listener idle_notification_listener = {
	.idled = handle_idle_notification_idled,
	.resumed = handle_idle_notification_resumed,
};

static void
create_idle_notification(struct seat_state *seat, struct idle_notification *idle_notification, enum idle_kind kind, guint timeout)
{
	struct karton_idle *ctx = seat->ctx;

	if (timeout == 0 || seat->seat == NULL || ctx->notifier == NULL || idle_notification->notification != NULL) {
		return;
	}

	idle_notification->ctx = ctx;
	idle_notification->seat = seat;
	idle_notification->kind = kind;
	idle_notification->notification = ext_idle_notifier_v1_get_idle_notification(ctx->notifier, seat->seat, timeout);
	ext_idle_notification_v1_add_listener(idle_notification->notification, &idle_notification_listener, idle_notification);
}

static void
maybe_create_notifications_for_seat(struct seat_state *seat)
{
	struct karton_idle *ctx = seat->ctx;

	if (seat->seat == NULL || ctx->notifier == NULL) {
		return;
	}

	if (seat->blank.notification == NULL) {
		create_idle_notification(
			seat,
			&seat->blank,
			IDLE_KIND_BLANK,
			seconds_to_milliseconds(ctx->blank_timeout));
	}

	if (ctx->lock_enabled && seat->lock.notification == NULL) {
		guint lock_timeout_seconds = ctx->blank_timeout;
		if (ctx->lock_delay > 0) {
			if (ctx->blank_timeout > G_MAXUINT32 - ctx->lock_delay) {
				lock_timeout_seconds = G_MAXUINT32;
			} else {
				lock_timeout_seconds = ctx->blank_timeout + ctx->lock_delay;
			}
		}

		create_idle_notification(
			seat,
			&seat->lock,
			IDLE_KIND_LOCK,
			seconds_to_milliseconds(lock_timeout_seconds));
	}
}

static void
maybe_create_notifications(struct karton_idle *ctx)
{
	if (ctx->notifier == NULL || ctx->seats == NULL) {
		return;
	}

	for (guint i = 0; i < ctx->seats->len; i++) {
		struct seat_state *seat = g_ptr_array_index(ctx->seats, i);
		maybe_create_notifications_for_seat(seat);
	}
}

static void
destroy_idle_notification(struct idle_notification *idle_notification)
{
	if (idle_notification->notification != NULL) {
		ext_idle_notification_v1_destroy(idle_notification->notification);
		idle_notification->notification = NULL;
	}
}

static void
seat_state_destroy(struct seat_state *seat)
{
	if (seat == NULL) {
		return;
	}

	struct karton_idle *ctx = seat->ctx;

	if (seat->blank_idle && ctx->blank_idle_count > 0) {
		ctx->blank_idle_count--;
	}
	if (seat->lock_idle && ctx->lock_idle_count > 0) {
		ctx->lock_idle_count--;
	}

	destroy_idle_notification(&seat->blank);
	destroy_idle_notification(&seat->lock);

	if (seat->seat != NULL) {
		wl_seat_destroy(seat->seat);
		seat->seat = NULL;
	}

	g_free(seat);
}

static void
remove_seat_by_name(struct karton_idle *ctx, uint32_t global_name)
{
	if (ctx->seats == NULL) {
		return;
	}

	for (guint i = 0; i < ctx->seats->len; i++) {
		struct seat_state *seat = g_ptr_array_index(ctx->seats, i);
		if (seat->global_name != global_name) {
			continue;
		}

		g_ptr_array_remove_index(ctx->seats, i);
		seat_state_destroy(seat);
		update_resume_state(ctx);
		return;
	}
}

static void
add_seat(struct karton_idle *ctx, struct wl_registry *registry, uint32_t name, uint32_t version)
{
	struct seat_state *seat = g_new0(struct seat_state, 1);
	uint32_t bind_version = version > 1 ? 1 : version;

	seat->ctx = ctx;
	seat->global_name = name;
	seat->seat = wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
	g_ptr_array_add(ctx->seats, seat);

	maybe_create_notifications_for_seat(seat);
}

static void
handle_registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	struct karton_idle *ctx = data;
	uint32_t bind_version = version > 1 ? 1 : version;

	if (strcmp(interface, wl_seat_interface.name) == 0) {
		add_seat(ctx, registry, name, bind_version);
		return;
	}

	if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0 && ctx->notifier == NULL) {
		ctx->notifier = wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, bind_version);
		maybe_create_notifications(ctx);
	}
}

static void
handle_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)registry;
	struct karton_idle *ctx = data;
	remove_seat_by_name(ctx, name);
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_registry_global,
	.global_remove = handle_registry_global_remove,
};

static gboolean
on_wayland_io(GIOChannel *source, GIOCondition condition, gpointer user_data)
{
	(void)source;
	struct karton_idle *ctx = user_data;

	if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
		g_main_loop_quit(ctx->main_loop);
		return G_SOURCE_REMOVE;
	}

	if (condition & G_IO_IN) {
		while (wl_display_prepare_read(ctx->display) != 0) {
			if (wl_display_dispatch_pending(ctx->display) == -1) {
				g_main_loop_quit(ctx->main_loop);
				return G_SOURCE_REMOVE;
			}
		}

		if (wl_display_flush(ctx->display) == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
			wl_display_cancel_read(ctx->display);
			g_main_loop_quit(ctx->main_loop);
			return G_SOURCE_REMOVE;
		}

		if (wl_display_read_events(ctx->display) == -1) {
			g_main_loop_quit(ctx->main_loop);
			return G_SOURCE_REMOVE;
		}

		if (wl_display_dispatch_pending(ctx->display) == -1) {
			g_main_loop_quit(ctx->main_loop);
			return G_SOURCE_REMOVE;
		}
	}

	if (wl_display_flush(ctx->display) == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
		g_main_loop_quit(ctx->main_loop);
		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static void
load_configuration(struct karton_idle *ctx)
{
	ctx->blank_timeout = load_timeout_setting("KARTON_IDLE_DELAY", "org.gnome.desktop.session", "idle-delay", 300);
	ctx->lock_delay = load_timeout_setting("KARTON_LOCK_DELAY", "org.gnome.desktop.screensaver", "lock-delay", 0);
	ctx->lock_enabled = load_bool_setting("KARTON_LOCK_ENABLED", "org.gnome.desktop.screensaver", "lock-enabled", TRUE);
	ctx->idle_off_command = load_command("KARTON_IDLE_OFF_CMD", "wlopm --off *");
	ctx->idle_on_command = load_command("KARTON_IDLE_ON_CMD", "wlopm --on *");
	ctx->idle_lock_command = load_command("KARTON_IDLE_LOCK_CMD", "karton-settingsd --lock-now");
}

static void
cleanup(struct karton_idle *ctx)
{
	if (ctx->wayland_watch_id > 0) {
		g_source_remove(ctx->wayland_watch_id);
		ctx->wayland_watch_id = 0;
	}

	if (ctx->seats != NULL) {
		for (guint i = 0; i < ctx->seats->len; i++) {
			struct seat_state *seat = g_ptr_array_index(ctx->seats, i);
			seat_state_destroy(seat);
		}
		g_ptr_array_free(ctx->seats, TRUE);
		ctx->seats = NULL;
	}

	if (ctx->notifier != NULL) {
		ext_idle_notifier_v1_destroy(ctx->notifier);
		ctx->notifier = NULL;
	}

	if (ctx->registry != NULL) {
		wl_registry_destroy(ctx->registry);
		ctx->registry = NULL;
	}

	if (ctx->display != NULL) {
		wl_display_disconnect(ctx->display);
		ctx->display = NULL;
	}

	if (ctx->main_loop != NULL) {
		g_main_loop_unref(ctx->main_loop);
		ctx->main_loop = NULL;
	}
}

int
main(void)
{
	struct karton_idle ctx = { 0 };
	ctx.seats = g_ptr_array_new();

	load_configuration(&ctx);
	if (ctx.blank_timeout == 0) {
		cleanup(&ctx);
		return 0;
	}

	ctx.display = wl_display_connect(NULL);
	if (ctx.display == NULL) {
		g_printerr("karton-idle: unable to connect to Wayland display\n");
		cleanup(&ctx);
		return EXIT_FAILURE;
	}

	ctx.registry = wl_display_get_registry(ctx.display);
	wl_registry_add_listener(ctx.registry, &registry_listener, &ctx);
	wl_display_roundtrip(ctx.display);
	wl_display_roundtrip(ctx.display);

	if (ctx.seats->len == 0 || ctx.notifier == NULL) {
		g_printerr("karton-idle: compositor does not provide ext_idle_notifier_v1\n");
		cleanup(&ctx);
		return EXIT_FAILURE;
	}

	maybe_create_notifications(&ctx);

	int wayland_fd = wl_display_get_fd(ctx.display);
	if (wayland_fd < 0) {
		g_printerr("karton-idle: unable to obtain Wayland file descriptor\n");
		cleanup(&ctx);
		return EXIT_FAILURE;
	}

	ctx.main_loop = g_main_loop_new(NULL, FALSE);
	GIOChannel *wayland_channel = g_io_channel_unix_new(wayland_fd);
	g_io_channel_set_close_on_unref(wayland_channel, FALSE);
	ctx.wayland_watch_id = g_io_add_watch(
		wayland_channel,
		G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
		on_wayland_io,
		&ctx);
	g_io_channel_unref(wayland_channel);

	if (wl_display_flush(ctx.display) == -1 && errno == EPIPE) {
		cleanup(&ctx);
		return EXIT_FAILURE;
	}

	g_main_loop_run(ctx.main_loop);
	cleanup(&ctx);
	return EXIT_SUCCESS;
}
