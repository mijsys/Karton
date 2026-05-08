#include <gio/gio.h>
#include <pulse/pulseaudio.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define MAX_SCAN_NETWORKS 8
#define MAX_AUDIO_DEVICES 8
#define MAX_REMOVABLE_DEVICES 8

struct pulse_audio_state {
	pa_mainloop *mainloop;
	pa_context *context;
	char default_sink_name[256];
	char default_source_name[256];
	char output_names[MAX_AUDIO_DEVICES][256];
	char output_devices[MAX_AUDIO_DEVICES][96];
	char input_names[MAX_AUDIO_DEVICES][256];
	char input_devices[MAX_AUDIO_DEVICES][96];
	size_t output_count;
	size_t input_count;
	int output_volume;
	int input_volume;
};

struct pulse_volume_target {
	bool found;
	uint32_t index;
	uint8_t channels;
};

struct pulse_device_target {
	bool input_device;
	size_t desired_index;
	size_t current_index;
	bool found;
	char device_name[256];
};

static GVariant *
get_property_value(GDBusConnection *conn, const char *bus_name,
const char *object_path, const char *interface_name,
const char *property_name)
{
if (!conn || !bus_name || !object_path || !interface_name || !property_name) {
return NULL;
}

GError *error = NULL;
GVariant *reply = g_dbus_connection_call_sync(
conn,
bus_name,
object_path,
"org.freedesktop.DBus.Properties",
"Get",
g_variant_new("(ss)", interface_name, property_name),
G_VARIANT_TYPE("(v)"),
G_DBUS_CALL_FLAGS_NONE,
600,
NULL,
&error);
if (!reply) {
g_clear_error(&error);
return NULL;
}

GVariant *boxed = NULL;
g_variant_get(reply, "(@v)", &boxed);
g_variant_unref(reply);
if (!boxed) {
return NULL;
}

GVariant *value = g_variant_get_variant(boxed);
g_variant_unref(boxed);
return value;
}

static bool
read_ull_file(const char *path, unsigned long long *out)
{
if (!path || !out) {
return false;
}
FILE *f = fopen(path, "r");
if (!f) {
return false;
}
unsigned long long v = 0;
int rc = fscanf(f, "%llu", &v);
fclose(f);
if (rc != 1) {
return false;
}
*out = v;
return true;
}

static bool
read_text_file(const char *path, char *out, size_t out_size)
{
if (!path || !out || out_size == 0) {
return false;
}
out[0] = '\0';
FILE *f = fopen(path, "r");
if (!f) {
return false;
}
if (!fgets(out, out_size, f)) {
fclose(f);
out[0] = '\0';
return false;
}
fclose(f);
size_t n = strlen(out);
while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ' || out[n - 1] == '\t')) {
out[--n] = '\0';
}
return out[0] != '\0';
}

static size_t
read_command_lines(const char *command, char out[][96], size_t max_lines)
{
if (!command || !out || max_lines == 0) {
return 0;
}

for (size_t i = 0; i < max_lines; i++) {
out[i][0] = '\0';
}

FILE *f = popen(command, "r");
if (!f) {
return 0;
}

size_t count = 0;
char line[192] = { 0 };
while (count < max_lines && fgets(line, sizeof(line), f)) {
size_t n = strlen(line);
while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
line[--n] = '\0';
}
if (!line[0]) {
continue;
}
size_t copy_len = strlen(line);
if (copy_len > 95) {
copy_len = 95;
}
memcpy(out[count], line, copy_len);
out[count][copy_len] = '\0';
count++;
}

pclose(f);
return count;
}

static int
clamp_percent(int value)
{
	if (value < 0) {
		return 0;
	}
	if (value > 100) {
		return 100;
	}
	return value;
}

static bool
pulse_connect(struct pulse_audio_state *state)
{
	if (!state) {
		return false;
	}

	memset(state, 0, sizeof(*state));
	state->mainloop = pa_mainloop_new();
	if (!state->mainloop) {
		return false;
	}

	pa_mainloop_api *api = pa_mainloop_get_api(state->mainloop);
	state->context = pa_context_new(api, "karton-system-status");
	if (!state->context) {
		pa_mainloop_free(state->mainloop);
		state->mainloop = NULL;
		return false;
	}

	if (pa_context_connect(state->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		pa_context_unref(state->context);
		pa_mainloop_free(state->mainloop);
		state->context = NULL;
		state->mainloop = NULL;
		return false;
	}

	for (;;) {
		pa_context_state_t ctx_state = pa_context_get_state(state->context);
		if (ctx_state == PA_CONTEXT_READY) {
			return true;
		}
		if (ctx_state == PA_CONTEXT_FAILED || ctx_state == PA_CONTEXT_TERMINATED) {
			break;
		}
		if (pa_mainloop_iterate(state->mainloop, 1, NULL) < 0) {
			break;
		}
	}

	if (state->context) {
		pa_context_disconnect(state->context);
		pa_context_unref(state->context);
		state->context = NULL;
	}
	if (state->mainloop) {
		pa_mainloop_free(state->mainloop);
		state->mainloop = NULL;
	}
	return false;
}

static void
pulse_disconnect(struct pulse_audio_state *state)
{
	if (!state) {
		return;
	}

	if (state->context) {
		pa_context_disconnect(state->context);
		pa_context_unref(state->context);
		state->context = NULL;
	}
	if (state->mainloop) {
		pa_mainloop_free(state->mainloop);
		state->mainloop = NULL;
	}
}

static bool
pulse_wait_for_operation(struct pulse_audio_state *state, pa_operation *op)
{
	if (!state || !state->mainloop || !state->context || !op) {
		return false;
	}

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
		pa_context_state_t ctx_state = pa_context_get_state(state->context);
		if (ctx_state == PA_CONTEXT_FAILED || ctx_state == PA_CONTEXT_TERMINATED) {
			break;
		}
		if (pa_mainloop_iterate(state->mainloop, 1, NULL) < 0) {
			break;
		}
	}

	bool ok = pa_operation_get_state(op) == PA_OPERATION_DONE;
	pa_operation_unref(op);
	return ok;
}

static void
pulse_server_info_cb(pa_context *context, const pa_server_info *info, void *userdata)
{
	(void)context;
	struct pulse_audio_state *state = userdata;
	if (!state || !info) {
		return;
	}

	if (info->default_sink_name) {
		snprintf(state->default_sink_name, sizeof(state->default_sink_name), "%s", info->default_sink_name);
	}
	if (info->default_source_name) {
		snprintf(state->default_source_name, sizeof(state->default_source_name), "%s", info->default_source_name);
	}
}

static void
pulse_sink_info_cb(pa_context *context, const pa_sink_info *info, int eol, void *userdata)
{
	(void)context;
	if (eol != 0 || !info) {
		return;
	}

	struct pulse_audio_state *state = userdata;
	if (!state || state->output_count >= MAX_AUDIO_DEVICES) {
		return;
	}

	const char *label = info->description && info->description[0] ? info->description : info->name;
	if (label && *label) {
		snprintf(state->output_names[state->output_count], sizeof(state->output_names[0]), "%s", info->name ? info->name : "");
		snprintf(state->output_devices[state->output_count], sizeof(state->output_devices[0]), "%s", label);
		state->output_count++;
	}

	if (info->name && state->default_sink_name[0] && strcmp(info->name, state->default_sink_name) == 0) {
		int percent = (int)((double)pa_cvolume_avg(&info->volume) * 100.0 / (double)PA_VOLUME_NORM + 0.5);
		state->output_volume = clamp_percent(percent);
		if (label && *label) {
			snprintf(state->default_sink_name, sizeof(state->default_sink_name), "%s", label);
		}
	}
}

static void
pulse_source_info_cb(pa_context *context, const pa_source_info *info, int eol, void *userdata)
{
	(void)context;
	if (eol != 0 || !info || info->monitor_of_sink != PA_INVALID_INDEX) {
		return;
	}

	struct pulse_audio_state *state = userdata;
	if (!state || state->input_count >= MAX_AUDIO_DEVICES) {
		return;
	}

	const char *label = info->description && info->description[0] ? info->description : info->name;
	if (label && *label) {
		snprintf(state->input_names[state->input_count], sizeof(state->input_names[0]), "%s", info->name ? info->name : "");
		snprintf(state->input_devices[state->input_count], sizeof(state->input_devices[0]), "%s", label);
		state->input_count++;
	}

	if (info->name && state->default_source_name[0] && strcmp(info->name, state->default_source_name) == 0) {
		int percent = (int)((double)pa_cvolume_avg(&info->volume) * 100.0 / (double)PA_VOLUME_NORM + 0.5);
		state->input_volume = clamp_percent(percent);
		if (label && *label) {
			snprintf(state->default_source_name, sizeof(state->default_source_name), "%s", label);
		}
	}
}

static void
pulse_sink_lookup_cb(pa_context *context, const pa_sink_info *info, int eol, void *userdata)
{
	(void)context;
	if (eol != 0 || !info) {
		return;
	}

	struct pulse_volume_target *target = userdata;
	if (!target) {
		return;
	}

	target->found = true;
	target->index = info->index;
	target->channels = info->channel_map.channels > 0 ? info->channel_map.channels : 2;
}

static void
pulse_source_lookup_cb(pa_context *context, const pa_source_info *info, int eol, void *userdata)
{
	(void)context;
	if (eol != 0 || !info || info->monitor_of_sink != PA_INVALID_INDEX) {
		return;
	}

	struct pulse_volume_target *target = userdata;
	if (!target) {
		return;
	}

	target->found = true;
	target->index = info->index;
	target->channels = info->channel_map.channels > 0 ? info->channel_map.channels : 1;
}

static void
pulse_device_list_pick_cb(pa_context *context, const pa_sink_info *info, int eol, void *userdata)
{
	(void)context;
	if (eol != 0 || !info) {
		return;
	}

	struct pulse_device_target *target = userdata;
	if (!target || target->input_device) {
		return;
	}

	if (target->current_index == target->desired_index && info->name && info->name[0]) {
		target->found = true;
		snprintf(target->device_name, sizeof(target->device_name), "%s", info->name);
		return;
	}

	target->current_index++;
}

static void
pulse_source_list_pick_cb(pa_context *context, const pa_source_info *info, int eol, void *userdata)
{
	(void)context;
	if (eol != 0 || !info || info->monitor_of_sink != PA_INVALID_INDEX) {
		return;
	}

	struct pulse_device_target *target = userdata;
	if (!target || !target->input_device) {
		return;
	}

	if (target->current_index == target->desired_index && info->name && info->name[0]) {
		target->found = true;
		snprintf(target->device_name, sizeof(target->device_name), "%s", info->name);
		return;
	}

	target->current_index++;
}

static bool
pulse_load_audio_state(char output_devices[MAX_AUDIO_DEVICES][96], size_t *output_count,
	char *default_output, size_t default_output_size, int *output_volume,
	char input_devices[MAX_AUDIO_DEVICES][96], size_t *input_count,
	char *default_input, size_t default_input_size, int *input_volume)
{
	struct pulse_audio_state state;
	if (!pulse_connect(&state)) {
		return false;
	}

	bool ok = false;
	pa_operation *op = pa_context_get_server_info(state.context, pulse_server_info_cb, &state);
	if (!pulse_wait_for_operation(&state, op)) {
		goto cleanup;
	}

	op = pa_context_get_sink_info_list(state.context, pulse_sink_info_cb, &state);
	if (!pulse_wait_for_operation(&state, op)) {
		goto cleanup;
	}

	op = pa_context_get_source_info_list(state.context, pulse_source_info_cb, &state);
	if (!pulse_wait_for_operation(&state, op)) {
		goto cleanup;
	}

	if (output_devices) {
		for (size_t i = 0; i < state.output_count; i++) {
			snprintf(output_devices[i], 96, "%s", state.output_devices[i]);
		}
	}
	if (output_count) {
		*output_count = state.output_count;
	}
	if (default_output && default_output_size > 0) {
		snprintf(default_output, default_output_size, "%s", state.default_sink_name);
	}
	if (output_volume) {
		*output_volume = state.output_volume;
	}

	if (input_devices) {
		for (size_t i = 0; i < state.input_count; i++) {
			snprintf(input_devices[i], 96, "%s", state.input_devices[i]);
		}
	}
	if (input_count) {
		*input_count = state.input_count;
	}
	if (default_input && default_input_size > 0) {
		snprintf(default_input, default_input_size, "%s", state.default_source_name);
	}
	if (input_volume) {
		*input_volume = state.input_volume;
	}

	ok = true;

cleanup:
	pulse_disconnect(&state);
	return ok;
}

static bool
pulse_set_default_volume(bool input_device, int percent)
{
	struct pulse_audio_state state;
	if (!pulse_connect(&state)) {
		return false;
	}

	bool ok = false;
	pa_operation *op = pa_context_get_server_info(state.context, pulse_server_info_cb, &state);
	if (!pulse_wait_for_operation(&state, op)) {
		goto cleanup;
	}

	const char *target_name = input_device ? state.default_source_name : state.default_sink_name;
	if (!target_name[0]) {
		goto cleanup;
	}

	struct pulse_volume_target target = { 0 };
	if (input_device) {
		op = pa_context_get_source_info_by_name(state.context, target_name, pulse_source_lookup_cb, &target);
	} else {
		op = pa_context_get_sink_info_by_name(state.context, target_name, pulse_sink_lookup_cb, &target);
	}
	if (!pulse_wait_for_operation(&state, op) || !target.found) {
		goto cleanup;
	}

	pa_cvolume volume;
	pa_cvolume_set(&volume,
		target.channels > 0 ? target.channels : (input_device ? 1 : 2),
		pa_sw_volume_from_linear((double)clamp_percent(percent) / 100.0));

	if (input_device) {
		op = pa_context_set_source_volume_by_index(state.context, target.index, &volume, NULL, NULL);
	} else {
		op = pa_context_set_sink_volume_by_index(state.context, target.index, &volume, NULL, NULL);
	}
	if (!pulse_wait_for_operation(&state, op)) {
		goto cleanup;
	}

	ok = true;

cleanup:
	pulse_disconnect(&state);
	return ok;
}

static bool
pulse_set_default_device(bool input_device, size_t index)
{
	struct pulse_audio_state state;
	if (!pulse_connect(&state)) {
		return false;
	}

	bool ok = false;
	struct pulse_device_target target = {
		.input_device = input_device,
		.desired_index = index,
	};
	pa_operation *op = NULL;

	if (input_device) {
		op = pa_context_get_source_info_list(state.context, pulse_source_list_pick_cb, &target);
	} else {
		op = pa_context_get_sink_info_list(state.context, pulse_device_list_pick_cb, &target);
	}
	if (!pulse_wait_for_operation(&state, op) || !target.found || !target.device_name[0]) {
		goto cleanup;
	}

	if (input_device) {
		op = pa_context_set_default_source(state.context, target.device_name, NULL, NULL);
	} else {
		op = pa_context_set_default_sink(state.context, target.device_name, NULL, NULL);
	}
	if (!pulse_wait_for_operation(&state, op)) {
		goto cleanup;
	}

	ok = true;

cleanup:
	pulse_disconnect(&state);
	return ok;
}

static bool
backlight_find_device(char *device_path, size_t device_path_size)
{
	if (!device_path || device_path_size == 0) {
		return false;
	}

	device_path[0] = '\0';
	DIR *dir = opendir("/sys/class/backlight");
	if (!dir) {
		return false;
	}

	struct dirent *entry = NULL;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}
		snprintf(device_path, device_path_size, "/sys/class/backlight/%s", entry->d_name);
		closedir(dir);
		return true;
	}

	closedir(dir);
	return false;
}

static bool join_path(char *out, size_t out_size, const char *base, const char *suffix);

static bool
backlight_get_percent(int *percent_out)
{
	if (!percent_out) {
		return false;
	}

	char device_path[PATH_MAX] = { 0 };
	if (!backlight_find_device(device_path, sizeof(device_path))) {
		return false;
	}

	char brightness_path[PATH_MAX] = { 0 };
	char max_path[PATH_MAX] = { 0 };
	unsigned long long current = 0;
	unsigned long long max = 0;

	if (!join_path(brightness_path, sizeof(brightness_path), device_path, "brightness")
	|| !join_path(max_path, sizeof(max_path), device_path, "max_brightness")) {
		return false;
	}
	if (!read_ull_file(brightness_path, &current) || !read_ull_file(max_path, &max) || max == 0) {
		return false;
	}

	int percent = (int)((double)current * 100.0 / (double)max + 0.5);
	*percent_out = clamp_percent(percent);
	return true;
}

static bool
write_unsigned_value(const char *path, unsigned long long value)
{
	if (!path || !path[0]) {
		return false;
	}

	FILE *file = fopen(path, "w");
	if (!file) {
		return false;
	}

	bool ok = fprintf(file, "%llu\n", value) > 0;
	if (fclose(file) != 0) {
		ok = false;
	}
	return ok;
}

static bool
join_path(char *out, size_t out_size, const char *base, const char *suffix)
{
	if (!out || out_size == 0 || !base || !suffix) {
		return false;
	}

	int written = snprintf(out, out_size, "%s/%s", base, suffix);
	return written >= 0 && (size_t)written < out_size;
}

static bool
backlight_set_percent(int percent)
{
	char device_path[PATH_MAX] = { 0 };
	if (!backlight_find_device(device_path, sizeof(device_path))) {
		return false;
	}

	char brightness_path[PATH_MAX] = { 0 };
	char max_path[PATH_MAX] = { 0 };
	unsigned long long max = 0;
	int clamped = clamp_percent(percent);

	if (!join_path(brightness_path, sizeof(brightness_path), device_path, "brightness")
	|| !join_path(max_path, sizeof(max_path), device_path, "max_brightness")) {
		return false;
	}
	if (!read_ull_file(max_path, &max) || max == 0) {
		return false;
	}

	unsigned long long raw_value = (unsigned long long)((double)clamped * (double)max / 100.0 + 0.5);
	if (raw_value > max) {
		raw_value = max;
	}
	if (raw_value == 0 && clamped > 0) {
		raw_value = 1;
	}

	return write_unsigned_value(brightness_path, raw_value);
}

static bool
nm_wireless_enabled(GDBusConnection *conn)
{
GVariant *value = get_property_value(conn,
"org.freedesktop.NetworkManager",
"/org/freedesktop/NetworkManager",
"org.freedesktop.NetworkManager",
"WirelessEnabled");
if (!value) {
return false;
}
bool enabled = g_variant_get_boolean(value);
g_variant_unref(value);
return enabled;
}

static void
nm_active_connection_info(GDBusConnection *conn,
char *type_out, size_t type_size,
char *name_out, size_t name_size,
char *iface_out, size_t iface_size)
{
if (type_out && type_size > 0) {
snprintf(type_out, type_size, "%s", "none");
}
if (name_out && name_size > 0) {
name_out[0] = '\0';
}
if (iface_out && iface_size > 0) {
iface_out[0] = '\0';
}

GVariant *value = get_property_value(conn,
"org.freedesktop.NetworkManager",
"/org/freedesktop/NetworkManager",
"org.freedesktop.NetworkManager",
"ActiveConnections");
if (!value) {
return;
}

GVariantIter iter;
char *path = NULL;
g_variant_iter_init(&iter, value);
while (g_variant_iter_next(&iter, "o", &path)) {
GError *error = NULL;
GVariant *reply = g_dbus_connection_call_sync(
conn,
"org.freedesktop.NetworkManager",
path,
"org.freedesktop.DBus.Properties",
"GetAll",
g_variant_new("(s)", "org.freedesktop.NetworkManager.Connection.Active"),
G_VARIANT_TYPE("(a{sv})"),
G_DBUS_CALL_FLAGS_NONE,
600,
NULL,
&error);
if (!reply) {
g_clear_error(&error);
g_free(path);
continue;
}

GVariant *props = NULL;
g_variant_get(reply, "(@a{sv})", &props);
g_variant_unref(reply);
if (!props) {
g_free(path);
continue;
}

const char *type = NULL;
const char *id = NULL;
g_variant_lookup(props, "Type", "&s", &type);
g_variant_lookup(props, "Id", "&s", &id);

const char *mapped_type = NULL;
if (type && strcmp(type, "802-11-wireless") == 0) {
mapped_type = "wifi";
} else if (type && strcmp(type, "802-3-ethernet") == 0) {
mapped_type = "ethernet";
}

if (mapped_type) {
if (type_out && type_size > 0) {
snprintf(type_out, type_size, "%s", mapped_type);
}
if (name_out && name_size > 0 && id) {
snprintf(name_out, name_size, "%s", id);
}

GVariant *devices = g_variant_lookup_value(props, "Devices", G_VARIANT_TYPE("ao"));
if (devices) {
GVariantIter dit;
char *dev_path = NULL;
g_variant_iter_init(&dit, devices);
if (g_variant_iter_next(&dit, "o", &dev_path)) {
GVariant *iface = get_property_value(conn,
"org.freedesktop.NetworkManager",
dev_path,
"org.freedesktop.NetworkManager.Device",
"Interface");
if (iface) {
const char *ifname = g_variant_get_string(iface, NULL);
if (ifname && iface_out && iface_size > 0) {
snprintf(iface_out, iface_size, "%s", ifname);
}
g_variant_unref(iface);
}
g_free(dev_path);
}
g_variant_unref(devices);
}

g_variant_unref(props);
g_free(path);
break;
}

g_variant_unref(props);
g_free(path);
}

g_variant_unref(value);
}

static size_t
nm_scan_wifi_networks(GDBusConnection *conn, char out[MAX_SCAN_NETWORKS][96])
{
if (!conn) {
return 0;
}

for (size_t i = 0; i < MAX_SCAN_NETWORKS; i++) {
out[i][0] = '\0';
}

size_t count = 0;
GVariant *devices = get_property_value(conn,
"org.freedesktop.NetworkManager",
"/org/freedesktop/NetworkManager",
"org.freedesktop.NetworkManager",
"Devices");
if (!devices) {
return 0;
}

GVariantIter dit;
char *dev_path = NULL;
g_variant_iter_init(&dit, devices);
while (g_variant_iter_next(&dit, "o", &dev_path)) {
GVariant *dtype = get_property_value(conn,
"org.freedesktop.NetworkManager",
dev_path,
"org.freedesktop.NetworkManager.Device",
"DeviceType");
if (!dtype) {
g_free(dev_path);
continue;
}
unsigned int type = g_variant_get_uint32(dtype);
g_variant_unref(dtype);
if (type != 2) {
g_free(dev_path);
continue;
}

/* Trigger fresh hardware scan so the list reflects what the Wi-Fi card currently sees. */
GError *scan_error = NULL;
GVariantBuilder scan_builder;
g_variant_builder_init(&scan_builder, G_VARIANT_TYPE_VARDICT);
GVariant *scan_options = g_variant_builder_end(&scan_builder);
GVariant *scan_reply = g_dbus_connection_call_sync(
conn,
"org.freedesktop.NetworkManager",
dev_path,
"org.freedesktop.NetworkManager.Device.Wireless",
"RequestScan",
g_variant_new("(@a{sv})", scan_options),
NULL,
G_DBUS_CALL_FLAGS_NONE,
600,
NULL,
&scan_error);
if (scan_reply) {
g_variant_unref(scan_reply);
} else {
g_clear_error(&scan_error);
}

GVariant *aps = get_property_value(conn,
"org.freedesktop.NetworkManager",
dev_path,
"org.freedesktop.NetworkManager.Device.Wireless",
"AccessPoints");
g_free(dev_path);
if (!aps) {
continue;
}

GVariantIter apit;
char *ap_path = NULL;
g_variant_iter_init(&apit, aps);
while (g_variant_iter_next(&apit, "o", &ap_path)) {
if (count >= MAX_SCAN_NETWORKS) {
g_free(ap_path);
break;
}
GVariant *ssid = get_property_value(conn,
"org.freedesktop.NetworkManager",
ap_path,
"org.freedesktop.NetworkManager.AccessPoint",
"Ssid");
g_free(ap_path);
if (!ssid) {
continue;
}

gsize n = 0;
const guint8 *bytes = g_variant_get_fixed_array(ssid, &n, sizeof(guint8));
if (bytes && n > 0) {
char name[96] = { 0 };
size_t lim = n < sizeof(name) - 1 ? n : sizeof(name) - 1;
for (size_t i = 0; i < lim; i++) {
unsigned char ch = bytes[i];
name[i] = (ch >= 32 && ch <= 126) ? (char)ch : '?';
}
name[lim] = '\0';
bool exists = false;
for (size_t i = 0; i < count; i++) {
if (!strcmp(out[i], name)) {
exists = true;
break;
}
}
if (!exists && name[0]) {
snprintf(out[count++], 96, "%s", name);
}
}

g_variant_unref(ssid);
}

g_variant_unref(aps);
if (count >= MAX_SCAN_NETWORKS) {
break;
}
}

g_variant_unref(devices);
return count;
}

static bool
bluez_any_adapter_powered(GDBusConnection *conn)
{
if (!conn) {
return false;
}

GError *error = NULL;
GVariant *reply = g_dbus_connection_call_sync(
conn,
"org.bluez",
"/",
"org.freedesktop.DBus.ObjectManager",
"GetManagedObjects",
NULL,
G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
G_DBUS_CALL_FLAGS_NONE,
600,
NULL,
&error);
if (!reply) {
g_clear_error(&error);
return false;
}

GVariant *objects = NULL;
g_variant_get(reply, "(@a{oa{sa{sv}}})", &objects);
g_variant_unref(reply);
if (!objects) {
return false;
}

GVariantIter iter;
char *object_path = NULL;
GVariant *interfaces = NULL;
bool powered = false;

g_variant_iter_init(&iter, objects);
while (g_variant_iter_next(&iter, "{oa{sa{sv}}}", &object_path, &interfaces)) {
GVariant *adapter_props = g_variant_lookup_value(interfaces,
"org.bluez.Adapter1",
G_VARIANT_TYPE("a{sv}"));
if (adapter_props) {
GVariant *powered_value = g_variant_lookup_value(adapter_props,
"Powered",
G_VARIANT_TYPE_BOOLEAN);
if (powered_value) {
powered = g_variant_get_boolean(powered_value);
g_variant_unref(powered_value);
g_variant_unref(adapter_props);
g_free(object_path);
g_variant_unref(interfaces);
break;
}
g_variant_unref(adapter_props);
}

g_free(object_path);
g_variant_unref(interfaces);
}

g_variant_unref(objects);
return powered;
}

static void
battery_info(bool *present, int *percent, bool *charging,
int *minutes_to_empty, int *minutes_to_full)
{
if (present) {
*present = false;
}
if (percent) {
*percent = 0;
}
if (charging) {
*charging = false;
}
if (minutes_to_empty) {
*minutes_to_empty = -1;
}
if (minutes_to_full) {
*minutes_to_full = -1;
}

DIR *d = opendir("/sys/class/power_supply");
if (!d) {
return;
}

char bat_path[512] = { 0 };
struct dirent *ent = NULL;
while ((ent = readdir(d)) != NULL) {
if (strncmp(ent->d_name, "BAT", 3) == 0) {
snprintf(bat_path, sizeof(bat_path), "/sys/class/power_supply/%s", ent->d_name);
break;
}
}
closedir(d);
if (!bat_path[0]) {
return;
}

if (present) {
*present = true;
}

char path[640] = { 0 };
char status[64] = { 0 };

snprintf(path, sizeof(path), "%s/capacity", bat_path);
unsigned long long cap = 0;
if (read_ull_file(path, &cap) && percent) {
if (cap > 100) {
cap = 100;
}
*percent = (int)cap;
}

snprintf(path, sizeof(path), "%s/status", bat_path);
if (read_text_file(path, status, sizeof(status)) && charging) {
*charging = (strstr(status, "Charging") != NULL);
}

unsigned long long now = 0;
unsigned long long full = 0;
unsigned long long rate = 0;

snprintf(path, sizeof(path), "%s/energy_now", bat_path);
if (!read_ull_file(path, &now)) {
snprintf(path, sizeof(path), "%s/charge_now", bat_path);
read_ull_file(path, &now);
}

snprintf(path, sizeof(path), "%s/energy_full", bat_path);
if (!read_ull_file(path, &full)) {
snprintf(path, sizeof(path), "%s/charge_full", bat_path);
read_ull_file(path, &full);
}

snprintf(path, sizeof(path), "%s/power_now", bat_path);
if (!read_ull_file(path, &rate)) {
snprintf(path, sizeof(path), "%s/current_now", bat_path);
read_ull_file(path, &rate);
}

if (rate > 0) {
if (charging && *charging && full > now && minutes_to_full) {
*minutes_to_full = (int)(((double)(full - now) * 60.0) / (double)rate);
}
if (charging && !*charging && now > 0 && minutes_to_empty) {
*minutes_to_empty = (int)(((double)now * 60.0) / (double)rate);
}
}
}

static int
print_quick_status(void)
{
bool wifi_enabled = false;
bool bluetooth_enabled = false;
char conn_type[16] = "none";
char conn_name[96] = "Not connected";
char iface[32] = "";
unsigned long long rx_bytes = 0;
unsigned long long tx_bytes = 0;
char networks[MAX_SCAN_NETWORKS][96] = {{ 0 }};
size_t network_count = 0;
char output_devices[MAX_AUDIO_DEVICES][96] = {{ 0 }};
char input_devices[MAX_AUDIO_DEVICES][96] = {{ 0 }};
	char default_output[96] = { 0 };
	char default_input[96] = { 0 };
	int output_volume = 0;
	int input_volume = 0;
char removable_paths[MAX_REMOVABLE_DEVICES][96] = {{ 0 }};
char removable_names[MAX_REMOVABLE_DEVICES][96] = {{ 0 }};
size_t output_count = 0;
size_t input_count = 0;
size_t removable_count = 0;
	int brightness = -1;

GError *error = NULL;
GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
if (conn) {
wifi_enabled = nm_wireless_enabled(conn);
bluetooth_enabled = bluez_any_adapter_powered(conn);
nm_active_connection_info(conn, conn_type, sizeof(conn_type), conn_name, sizeof(conn_name), iface, sizeof(iface));
network_count = nm_scan_wifi_networks(conn, networks);
g_object_unref(conn);
} else {
g_clear_error(&error);
}

if (iface[0]) {
char rx_path[256] = { 0 };
char tx_path[256] = { 0 };
snprintf(rx_path, sizeof(rx_path), "/sys/class/net/%s/statistics/rx_bytes", iface);
snprintf(tx_path, sizeof(tx_path), "/sys/class/net/%s/statistics/tx_bytes", iface);
read_ull_file(rx_path, &rx_bytes);
read_ull_file(tx_path, &tx_bytes);
}

bool bat_present = false;
bool bat_charging = false;
int bat_percent = 0;
int bat_min_empty = -1;
int bat_min_full = -1;
battery_info(&bat_present, &bat_percent, &bat_charging, &bat_min_empty, &bat_min_full);

	(void)pulse_load_audio_state(output_devices, &output_count,
		default_output, sizeof(default_output), &output_volume,
		input_devices, &input_count,
		default_input, sizeof(default_input), &input_volume);
	(void)backlight_get_percent(&brightness);

char removable_rows[MAX_REMOVABLE_DEVICES][96] = {{ 0 }};
removable_count = read_command_lines(
"sh -lc 'command -v lsblk >/dev/null 2>&1 && lsblk -rno RM,MOUNTPOINT,LABEL,NAME 2>/dev/null | while read -r rm mp label name; do [ \"$rm\" = \"1\" ] && [ -n \"$mp\" ] || continue; [ -z \"$label\" ] && label=\"$name\"; printf \"%s|%s\\n\" \"$mp\" \"$label\"; done | sed -n 1,8p'",
removable_rows, MAX_REMOVABLE_DEVICES);
for (size_t i = 0; i < removable_count; i++) {
char *sep = strchr(removable_rows[i], '|');
if (!sep) {
snprintf(removable_paths[i], sizeof(removable_paths[i]), "%s", removable_rows[i]);
snprintf(removable_names[i], sizeof(removable_names[i]), "%s", removable_rows[i]);
continue;
}
*sep = '\0';
snprintf(removable_paths[i], sizeof(removable_paths[i]), "%s", removable_rows[i]);
snprintf(removable_names[i], sizeof(removable_names[i]), "%s", sep + 1);
}

printf("wifi_enabled=%s\n", wifi_enabled ? "yes" : "no");
printf("bluetooth_enabled=%s\n", bluetooth_enabled ? "yes" : "no");
printf("wifi_name=%s\n", conn_name[0] ? conn_name : "Not connected");
printf("connection_type=%s\n", conn_type);
printf("connection_name=%s\n", conn_name[0] ? conn_name : "Not connected");
printf("iface=%s\n", iface);
printf("rx_bytes=%llu\n", rx_bytes);
printf("tx_bytes=%llu\n", tx_bytes);
for (size_t i = 0; i < network_count; i++) {
printf("network_%zu=%s\n", i, networks[i]);
}
for (size_t i = 0; i < output_count; i++) {
printf("output_%zu=%s\n", i, output_devices[i]);
}
for (size_t i = 0; i < input_count; i++) {
printf("input_%zu=%s\n", i, input_devices[i]);
}
	printf("default_output=%s\n", default_output);
	printf("default_input=%s\n", default_input);
	printf("output_volume=%d\n", output_volume);
	printf("input_volume=%d\n", input_volume);
	printf("brightness=%d\n", brightness);
for (size_t i = 0; i < removable_count; i++) {
printf("removable_path_%zu=%s\n", i, removable_paths[i]);
printf("removable_name_%zu=%s\n", i, removable_names[i]);
}
printf("battery_present=%s\n", bat_present ? "yes" : "no");
printf("battery_charging=%s\n", bat_charging ? "yes" : "no");
printf("battery_percent=%d\n", bat_percent);
printf("battery_minutes_to_empty=%d\n", bat_min_empty);
printf("battery_minutes_to_full=%d\n", bat_min_full);

return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s quick|set-output-volume <percent>|set-input-volume <percent>|set-brightness <percent>|set-default-output <index>|set-default-input <index>\n", argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "quick") == 0) {
		return print_quick_status();
	}

	if ((strcmp(argv[1], "set-output-volume") == 0 || strcmp(argv[1], "set-input-volume") == 0) && argc >= 3) {
		char *end = NULL;
		long value = strtol(argv[2], &end, 10);
		if (!end || *end != '\0') {
			fprintf(stderr, "invalid volume: %s\n", argv[2]);
			return 1;
		}
		return pulse_set_default_volume(strcmp(argv[1], "set-input-volume") == 0, (int)value) ? 0 : 1;
	}

	if ((strcmp(argv[1], "set-default-output") == 0 || strcmp(argv[1], "set-default-input") == 0) && argc >= 3) {
		char *end = NULL;
		unsigned long value = strtoul(argv[2], &end, 10);
		if (!end || *end != '\0') {
			fprintf(stderr, "invalid device index: %s\n", argv[2]);
			return 1;
		}
		return pulse_set_default_device(strcmp(argv[1], "set-default-input") == 0, (size_t)value) ? 0 : 1;
	}

	if (strcmp(argv[1], "set-brightness") == 0 && argc >= 3) {
		char *end = NULL;
		long value = strtol(argv[2], &end, 10);
		if (!end || *end != '\0') {
			fprintf(stderr, "invalid brightness: %s\n", argv[2]);
			return 1;
		}
		return backlight_set_percent((int)value) ? 0 : 1;
	}

	fprintf(stderr, "unknown command: %s\n", argv[1]);
	return 1;
}