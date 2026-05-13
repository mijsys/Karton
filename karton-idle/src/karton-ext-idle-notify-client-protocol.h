/* SPDX-License-Identifier: MIT */
/*
 * Fallback client header for ext-idle-notify-v1.
 * This mirrors wayland-scanner client-header output for the protocol used by karton-idle.
 */

#ifndef KARTON_EXT_IDLE_NOTIFY_CLIENT_PROTOCOL_H
#define KARTON_EXT_IDLE_NOTIFY_CLIENT_PROTOCOL_H

#include <stdint.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_seat;
struct wl_proxy;
struct wl_interface;

struct ext_idle_notifier_v1;
struct ext_idle_notification_v1;

extern const struct wl_interface ext_idle_notifier_v1_interface;
extern const struct wl_interface ext_idle_notification_v1_interface;

#ifndef WL_HIDE_DEPRECATED

static inline int
ext_idle_notification_v1_add_listener(struct ext_idle_notification_v1 *ext_idle_notification_v1,
					      const struct ext_idle_notification_v1_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) ext_idle_notification_v1, (void (**)(void)) listener, data);
}

#define EXT_IDLE_NOTIFICATION_V1_IDLED 0
#define EXT_IDLE_NOTIFICATION_V1_RESUMED 1

struct ext_idle_notification_v1_listener {
	void (*idled)(void *data,
		      struct ext_idle_notification_v1 *ext_idle_notification_v1);
	void (*resumed)(void *data,
			struct ext_idle_notification_v1 *ext_idle_notification_v1);
};

#define EXT_IDLE_NOTIFICATION_V1_DESTROY 0

static inline void
ext_idle_notification_v1_destroy(struct ext_idle_notification_v1 *ext_idle_notification_v1)
{
	wl_proxy_marshal_flags((struct wl_proxy *) ext_idle_notification_v1,
			      EXT_IDLE_NOTIFICATION_V1_DESTROY, NULL,
			      wl_proxy_get_version((struct wl_proxy *) ext_idle_notification_v1),
			      WL_MARSHAL_FLAG_DESTROY);
}

#define EXT_IDLE_NOTIFIER_V1_GET_IDLE_NOTIFICATION 0
#define EXT_IDLE_NOTIFIER_V1_DESTROY 1

static inline struct ext_idle_notification_v1 *
ext_idle_notifier_v1_get_idle_notification(struct ext_idle_notifier_v1 *ext_idle_notifier_v1,
					   struct wl_seat *seat, uint32_t timeout)
{
	struct wl_proxy *id;

	id = wl_proxy_marshal_flags((struct wl_proxy *) ext_idle_notifier_v1,
				    EXT_IDLE_NOTIFIER_V1_GET_IDLE_NOTIFICATION,
				    &ext_idle_notification_v1_interface,
				    wl_proxy_get_version((struct wl_proxy *) ext_idle_notifier_v1),
				    0,
				    NULL, seat, timeout);

	return (struct ext_idle_notification_v1 *) id;
}

static inline void
ext_idle_notifier_v1_destroy(struct ext_idle_notifier_v1 *ext_idle_notifier_v1)
{
	wl_proxy_marshal_flags((struct wl_proxy *) ext_idle_notifier_v1,
			      EXT_IDLE_NOTIFIER_V1_DESTROY, NULL,
			      wl_proxy_get_version((struct wl_proxy *) ext_idle_notifier_v1),
			      WL_MARSHAL_FLAG_DESTROY);
}

#endif /* WL_HIDE_DEPRECATED */

#ifdef __cplusplus
}
#endif

#endif /* KARTON_EXT_IDLE_NOTIFY_CLIENT_PROTOCOL_H */
