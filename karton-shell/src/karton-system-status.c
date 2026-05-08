#include <gio/gio.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_SCAN_NETWORKS 8
#define MAX_AUDIO_DEVICES 8
#define MAX_REMOVABLE_DEVICES 8

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
char removable_paths[MAX_REMOVABLE_DEVICES][96] = {{ 0 }};
char removable_names[MAX_REMOVABLE_DEVICES][96] = {{ 0 }};
size_t output_count = 0;
size_t input_count = 0;
size_t removable_count = 0;

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

output_count = read_command_lines(
"sh -lc 'command -v pactl >/dev/null 2>&1 && pactl list short sinks 2>/dev/null | cut -f2 | sed -n 1,8p'",
output_devices, MAX_AUDIO_DEVICES);
input_count = read_command_lines(
"sh -lc 'command -v pactl >/dev/null 2>&1 && pactl list short sources 2>/dev/null | cut -f2 | sed -n 1,8p'",
input_devices, MAX_AUDIO_DEVICES);

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
fprintf(stderr, "usage: %s quick\n", argv[0]);
return 1;
}

if (strcmp(argv[1], "quick") == 0) {
return print_quick_status();
}

fprintf(stderr, "unknown command: %s\n", argv[1]);
return 1;
}