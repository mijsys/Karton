// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys

#define _POSIX_C_SOURCE 200809L
#include <cairo.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libintl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <locale.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <gio/gio.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
#include "pool-buffer.h"
#include "karton-layer-shell-client-protocol.h"
#include "karton-foreign-toplevel-client-protocol.h"

#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#ifndef DATADIR
#define DATADIR "/usr/local/share"
#endif

#define _(s) gettext(s)
#define N_(s) s

#define MAX_TOPLEVELS 128
#define MAX_GROUPS 32
#define MAX_GROUP_ITEMS 32
#define MAX_LAUNCHER_ENTRIES 384
#define MAX_FAVORITES 384
#define MAX_CAT_OVERRIDES 384
#define MAX_CALENDAR_ITEMS 256
#define MAX_CLOCK_ZONES 8
#define MAX_ICON_CACHE 128
#define MAX_GLOBAL_MENU_TOP 16
#define MAX_GLOBAL_MENU_ITEMS 48
#define MAX_NOTIFICATIONS 8
#define MAX_STATUS_NETWORKS 8
#define MAX_STATUS_AUDIO_DEVICES 8
#define MAX_STATUS_REMOVABLE_DEVICES 8

enum panel_type {
PANEL_TOP,
PANEL_SIDE,
};

enum run_mode {
RUN_BOTH,
RUN_TOP_ONLY,
RUN_SIDE_ONLY,
};

enum theme_mode {
THEME_AUTO,
THEME_LIGHT,
THEME_DARK,
};

enum top_popup_mode {
TOP_POPUP_NONE,
TOP_POPUP_QUICK,
TOP_POPUP_NOTIFICATIONS,
TOP_POPUP_NETWORK,
TOP_POPUP_AUDIO,
TOP_POPUP_POWER,
TOP_POPUP_CALENDAR,
TOP_POPUP_CLOCK,
};

enum top_slider_drag_target {
TOP_SLIDER_DRAG_NONE,
TOP_SLIDER_DRAG_QUICK_BRIGHTNESS,
TOP_SLIDER_DRAG_QUICK_VOLUME,
TOP_SLIDER_DRAG_AUDIO_OUTPUT,
TOP_SLIDER_DRAG_AUDIO_INPUT,
};

enum launcher_category {
LCAT_ALL,
LCAT_FAVORITES,
LCAT_INTERNET,
LCAT_OFFICE,
LCAT_MEDIA,
LCAT_DEVELOPMENT,
LCAT_SYSTEM,
LCAT_UTILITY,
LCAT_OTHER,
LCAT_COUNT,
};

struct app;

struct panel {
enum panel_type type;
struct app *app;
struct wl_surface *surface;
struct zwlr_layer_surface_v1 *layer_surface;
struct pool_buffer buffers[2];
uint32_t width;
uint32_t height;
bool configured;
};

struct toplevel_entry {
bool used;
struct app *app;
struct zwlr_foreign_toplevel_handle_v1 *handle;
char app_id[96];
char title[256];
bool active;
bool minimized;
bool fullscreen;
};

struct app_group {
char app_id[96];
size_t indices[MAX_GROUP_ITEMS];
size_t count;
bool any_active;
};

struct launcher_entry {
char name[128];
char desktop_id[128];
char desktop_path[PATH_MAX];
char icon_name[128];
bool local_entry;
bool favorite;
int category;
int category_override;
};

struct launcher_category_override {
char desktop_id[128];
int category;
};

struct global_menu_item {
char label[96];
char action[128];
char target[192];
bool enabled;
};

struct global_menu_top {
char label[64];
struct global_menu_item items[MAX_GLOBAL_MENU_ITEMS];
size_t item_count;
};

struct calendar_item {
int year;
int month;
int day;
bool task;
char text[160];
};

enum notification_target_kind {
NOTIFICATION_TARGET_NONE,
NOTIFICATION_TARGET_OPEN_LOCATION,
NOTIFICATION_TARGET_LAUNCH_APP,
};

struct notification_entry {
char app_name[96];
char text[160];
char detail[192];
char action_label[96];
char action_target[PATH_MAX];
int target_kind;
};

struct icon_cache_entry {
char icon_name[PATH_MAX];
cairo_surface_t *surface;
};

struct shell_style {
uint32_t top_height;
uint32_t top_expanded_height;
uint32_t side_width;
uint32_t side_expanded_width;
int theme_mode;

uint32_t quick_button_radius;
uint32_t quick_panel_radius;
uint32_t quick_tile_radius;
uint32_t popup_radius;

uint32_t top_bg;
uint32_t top_text;
uint32_t quick_button_bg;
uint32_t quick_button_fg;
uint32_t quick_panel_bg;
uint32_t quick_tile_bg;
uint32_t quick_title_text;

uint32_t side_bg;
uint32_t side_popup_bg;
uint32_t side_separator;
uint32_t side_launcher_bg;
uint32_t side_slot_active;
uint32_t side_slot_inactive;
};

struct app {
struct wl_display *display;
struct wl_registry *registry;
struct wl_compositor *compositor;
struct wl_shm *shm;
struct zwlr_layer_shell_v1 *layer_shell;
struct wl_output *output;
struct wl_seat *seat;
struct wl_pointer *pointer;
struct wl_keyboard *keyboard;
struct zwlr_foreign_toplevel_manager_v1 *foreign_manager;

struct xkb_context *xkb_context;
struct xkb_keymap *xkb_keymap;
struct xkb_state *xkb_state;

struct shell_style style;
bool side_enabled;

int output_scale;
uint32_t output_height;
bool running;

struct wl_surface *pointer_surface;
double pointer_x;
double pointer_y;
uint32_t pointer_serial;

bool quick_open;
int top_popup_mode;
bool dnd_enabled;
bool notifications_read;
bool notifications_cleared;
struct notification_entry notifications[MAX_NOTIFICATIONS];
size_t notification_count;
int quick_hover_tile;
int quick_menu_tile;
int quick_menu_hover_item;
bool quick_wifi_enabled;
bool quick_bluetooth_enabled;
char quick_wifi_name[96];
char quick_connection_type[16];
char quick_connection_name[96];
char quick_iface[32];
unsigned long long quick_rx_bytes;
unsigned long long quick_tx_bytes;
char quick_networks[MAX_STATUS_NETWORKS][96];
size_t quick_network_count;
char quick_outputs[MAX_STATUS_AUDIO_DEVICES][96];
size_t quick_output_count;
char quick_inputs[MAX_STATUS_AUDIO_DEVICES][96];
size_t quick_input_count;
char quick_default_output[96];
char quick_default_input[96];
char quick_removable_paths[MAX_STATUS_REMOVABLE_DEVICES][96];
char quick_removable_names[MAX_STATUS_REMOVABLE_DEVICES][96];
size_t quick_removable_count;
bool quick_battery_present;
bool quick_battery_charging;
int quick_battery_percent;
int quick_battery_minutes_to_empty;
int quick_battery_minutes_to_full;
time_t quick_status_updated;
time_t notifications_updated;
int quick_brightness;
int quick_volume;
int quick_input_volume;
int top_slider_drag_target;
int calendar_year;
int calendar_month;
int calendar_selected_day;
struct calendar_item calendar_items[MAX_CALENDAR_ITEMS];
size_t calendar_item_count;
char clock_timezones[MAX_CLOCK_ZONES][64];
size_t clock_timezone_count;
bool clock_picker_open;
int hovered_group;
int popup_group;
int popup_hover_item;
int popup_selected_item;
bool launcher_open;
int launcher_hover_item;
int launcher_hover_favorite;
char launcher_query[96];
bool launcher_search_active;
int launcher_category;
size_t launcher_filtered[MAX_LAUNCHER_ENTRIES];
size_t launcher_filtered_count;
int launcher_selected;
int launcher_scroll_offset;
bool launcher_menu_open;
int launcher_menu_target;
int launcher_menu_hover;
double launcher_menu_x;
double launcher_menu_y;

struct panel top;
struct panel side;

struct toplevel_entry toplevels[MAX_TOPLEVELS];
struct app_group groups[MAX_GROUPS];
size_t group_count;

struct launcher_entry launcher_entries[MAX_LAUNCHER_ENTRIES];
size_t launcher_count;

struct icon_cache_entry icon_cache[MAX_ICON_CACHE];
size_t icon_cache_count;
char icon_theme_name[128];

char favorite_ids[MAX_FAVORITES][128];
size_t favorite_count;

struct launcher_category_override category_overrides[MAX_CAT_OVERRIDES];
size_t category_override_count;

char active_app_id[96];
char active_window_title[256];
char global_menu_bus[128];
char global_menu_path[256];
char global_menu_actions_path[256];
bool global_menu_available;
bool global_menu_open;
int global_menu_open_top;
struct global_menu_top global_menu_tops[MAX_GLOBAL_MENU_TOP];
size_t global_menu_top_count;
};

struct launcher_power_action {
const char *fallback_icon;
const char *icon_name;
const char *command;
};

static const struct launcher_power_action launcher_power_actions[] = {
    /* Fallback glyph | Preferred icon | Command */
	{ "󰈆", "logout", "sh -lc 'if command -v karton >/dev/null 2>&1; then karton --exit; elif command -v labwc >/dev/null 2>&1; then labwc --exit; else loginctl terminate-session self; fi'" },
    { "󰤄", "sleep", "systemctl suspend" },
    { "󰑓", "reboot", "systemctl reboot" },
    { "󰐥", "power", "systemctl poweroff" },
};

static const char *
launcher_power_action_label(int action_idx)
{
switch (action_idx) {
case 0:
return _("Logout");
case 1:
return _("Sleep");
case 2:
return _("Restart");
case 3:
return _("Power off");
default:
return "";
}
}

static const char *clock_timezone_candidates[] = {
"UTC",
"Europe/Warsaw",
"Europe/London",
"America/New_York",
"America/Los_Angeles",
"Asia/Tokyo",
"Asia/Dubai",
"Australia/Sydney",
};

static void panel_draw(struct panel *panel);
static bool point_in_rect(double px, double py, double x, double y, double w, double h);
static bool contains_nocase(const char *haystack, const char *needle);
static bool str_ends_with(const char *s, const char *suffix);
static void notification_prepare_entry(struct app *app, struct notification_entry *entry,
const char *app_name, const char *summary, const char *body);
static void notification_activate(struct app *app, const struct notification_entry *entry);
static void measure_pango_text(cairo_t *cairo, const char *family, PangoWeight weight,
double size, const char *text, int *width, int *height);
static bool top_quick_panel_rect(const struct panel *panel, double *x, double *y, double *w, double *h);
static bool top_calendar_panel_rect(const struct panel *panel, double *x, double *y, double *w, double *h);
static bool top_clock_panel_rect(const struct panel *panel, double *x, double *y, double *w, double *h);
static bool top_popup_rect_for_mode(const struct panel *panel, int mode,
double *x, double *y, double *w, double *h);
static void launcher_close(struct app *app);
static void launcher_activate_filtered(struct app *app, int filtered_idx);
static void shell_style_apply_theme(struct shell_style *style);
static void draw_karton_symbol(cairo_t *cairo, int type, double cx, double cy,
double size, uint32_t color, double alpha);
static uint32_t launcher_panel_width(const struct app *app);
static size_t launcher_collect_favorite_preview(const struct app *app, size_t *out, size_t max_out);
static int launcher_filtered_index_from_entry(const struct app *app, size_t entry_idx);
static bool launcher_category_rect(const struct panel *panel, int category,
double *x, double *y, double *w, double *h);
static bool launcher_favorite_tile_rect(const struct panel *panel, const struct app *app, int preview_idx,
double *x, double *y, double *w, double *h);
static int launcher_favorite_tile_hit(const struct panel *panel, const struct app *app, double px, double py);
static void popup_clamp_selection(struct app *app);
static void request_top_panel_size(struct app *app);
static void request_side_panel_size(struct app *app);
static void trigger_redraw(struct app *app);

static volatile sig_atomic_t theme_reload_requested = 0;

static void
handle_runtime_signal(int signo)
{
if (signo == SIGUSR1 || signo == SIGHUP) {
theme_reload_requested = 1;
}
}

static void
register_runtime_signals(void)
{
struct sigaction sa;
memset(&sa, 0, sizeof(sa));
sa.sa_handler = handle_runtime_signal;
sigemptyset(&sa.sa_mask);

sigaction(SIGUSR1, &sa, NULL);
sigaction(SIGHUP, &sa, NULL);
}

static void
init_i18n(void)
{
setlocale(LC_ALL, "");

const char *locale_dir = getenv("KARTON_LOCALEDIR");
if (!locale_dir || !*locale_dir) {
locale_dir = LOCALEDIR;
}

bindtextdomain("karton-shell", locale_dir);
bind_textdomain_codeset("karton-shell", "UTF-8");
textdomain("karton-shell");
}

static char *
trim_in_place(char *s)
{
if (!s) {
return s;
}

while (*s && isspace((unsigned char)*s)) {
s++;
}

char *end = s + strlen(s);
while (end > s && isspace((unsigned char)*(end - 1))) {
end--;
}
*end = '\0';
return s;
}

static bool
parse_hex_color(const char *value, uint32_t *out)
{
if (!value || !out || value[0] != '#') {
return false;
}

char *end = NULL;
errno = 0;
unsigned long parsed = strtoul(value + 1, &end, 16);
if (errno != 0 || !end || *end != '\0') {
return false;
}

if (strlen(value + 1) != 6 || parsed > 0xFFFFFFUL) {
return false;
}

*out = (uint32_t)parsed;
return true;
}

static bool
parse_u32_value(const char *value, uint32_t min, uint32_t max, uint32_t *out)
{
if (!value || !out) {
return false;
}

char *end = NULL;
errno = 0;
unsigned long parsed = strtoul(value, &end, 10);
if (errno != 0 || !end || *end != '\0' || parsed < min || parsed > max) {
return false;
}

*out = (uint32_t)parsed;
return true;
}

static void
karton_get_config_path(char *out, size_t out_size, const char *name)
{
const char *xdg = getenv("XDG_CONFIG_HOME");
const char *home = getenv("HOME");
if (xdg && *xdg) {
snprintf(out, out_size, "%s/karton/%s", xdg, name);
return;
}

if (!home || !home[0]) {
out[0] = '\0';
return;
}
snprintf(out, out_size, "%s/.config/karton/%s", home, name);
}

static void
karton_ensure_config_dir(void)
{
const char *xdg = getenv("XDG_CONFIG_HOME");
const char *home = getenv("HOME");
if ((!xdg || !*xdg) && (!home || !home[0])) {
return;
}

char path[PATH_MAX] = { 0 };
if (xdg && *xdg) {
snprintf(path, sizeof(path), "%s/karton", xdg);
} else {
snprintf(path, sizeof(path), "%s/.config/karton", home);
}
if (mkdir(path, 0700) != 0 && errno != EEXIST) {
fprintf(stderr, _("karton-shell: cannot create %s: %s\n"), path, strerror(errno));
}
}

static const char *
theme_mode_name(int mode)
{
switch (mode) {
case THEME_LIGHT:
return "light";
case THEME_DARK:
return "dark";
default:
return "auto";
}
}

static int
theme_mode_parse(const char *value)
{
if (!value || !*value) {
return THEME_AUTO;
}
if (!strcasecmp(value, "light")) {
return THEME_LIGHT;
}
if (!strcasecmp(value, "dark")) {
return THEME_DARK;
}
return THEME_AUTO;
}

static void
load_theme_mode_override(struct shell_style *style)
{
char path[PATH_MAX] = { 0 };
karton_get_config_path(path, sizeof(path), "theme-mode");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "r");
if (!f) {
return;
}

char line[32] = { 0 };
if (fgets(line, sizeof(line), f)) {
char *mode = trim_in_place(line);
style->theme_mode = theme_mode_parse(mode);
shell_style_apply_theme(style);
}
fclose(f);
}

static void
save_theme_mode_override(int mode)
{
karton_ensure_config_dir();

char path[PATH_MAX] = { 0 };
karton_get_config_path(path, sizeof(path), "theme-mode");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "w");
if (!f) {
return;
}
fprintf(f, "%s\n", theme_mode_name(mode));
fclose(f);
}

static bool
theme_mode_is_dark(int mode)
{
if (mode == THEME_DARK) {
return true;
}
if (mode == THEME_LIGHT) {
return false;
}

time_t now = time(NULL);
struct tm tm_now = { 0 };
if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
return true;
}
return tm_now.tm_hour < 7 || tm_now.tm_hour >= 19;
}

static void
shell_style_apply_theme(struct shell_style *style)
{
bool dark = theme_mode_is_dark(style->theme_mode);

if (dark) {
style->top_bg = 0x171f2d;
style->top_text = 0xeaf1ff;
style->quick_button_bg = 0x7b66f0;
style->quick_button_fg = 0xffffff;
style->quick_panel_bg = 0x1d283b;
style->quick_tile_bg = 0x283750;
style->quick_title_text = 0xebf2ff;

style->side_bg = 0x121a29;
style->side_popup_bg = 0x1a2436;
style->side_separator = 0x63779b;
style->side_launcher_bg = 0x8d72ff;
style->side_slot_active = 0x5dcdc3;
style->side_slot_inactive = 0x33445f;
} else {
style->top_bg = 0xf3f5f9;
style->top_text = 0x1f2a3d;
style->quick_button_bg = 0x7b66f0;
style->quick_button_fg = 0xffffff;
	style->quick_panel_bg = 0xffffff;
	style->quick_tile_bg = 0xeef2f8;
	style->quick_title_text = 0x283347;

style->side_bg = 0xf1f4f9;
style->side_popup_bg = 0xfcfdff;
style->side_separator = 0xc5d0e0;
style->side_launcher_bg = 0x7b66f0;
style->side_slot_active = 0x58c6be;
style->side_slot_inactive = 0xd7e0ec;
}
}

static void
shell_style_defaults(struct shell_style *style)
{
style->top_height = 42;
style->top_expanded_height = 392;
style->side_width = 74;
style->side_expanded_width = 362;
style->theme_mode = THEME_LIGHT;

style->quick_button_radius = 13;
style->quick_panel_radius = 22;
style->quick_tile_radius = 14;
style->popup_radius = 20;

shell_style_apply_theme(style);
}

static void
apply_style_var(struct shell_style *style, const char *name, const char *value)
{
uint32_t color = 0;
uint32_t metric = 0;

if (!strcmp(name, "--theme-mode")) {
if (!strcasecmp(value, "light")) {
style->theme_mode = THEME_LIGHT;
} else if (!strcasecmp(value, "dark")) {
style->theme_mode = THEME_DARK;
} else {
style->theme_mode = THEME_AUTO;
}
shell_style_apply_theme(style);
return;
}

if (!strcmp(name, "--top-height")
&& parse_u32_value(value, 24, 96, &metric)) {
style->top_height = metric;
return;
}
if (!strcmp(name, "--top-expanded-height")
&& parse_u32_value(value, 120, 900, &metric)) {
style->top_expanded_height = metric;
return;
}
if (!strcmp(name, "--side-width")
&& parse_u32_value(value, 48, 140, &metric)) {
style->side_width = metric;
return;
}
if (!strcmp(name, "--side-expanded-width")
&& parse_u32_value(value, 180, 700, &metric)) {
style->side_expanded_width = metric;
return;
}
if (!strcmp(name, "--quick-button-radius")
&& parse_u32_value(value, 0, 40, &metric)) {
style->quick_button_radius = metric;
return;
}
if (!strcmp(name, "--quick-panel-radius")
&& parse_u32_value(value, 0, 40, &metric)) {
style->quick_panel_radius = metric;
return;
}
if (!strcmp(name, "--quick-tile-radius")
&& parse_u32_value(value, 0, 40, &metric)) {
style->quick_tile_radius = metric;
return;
}
if (!strcmp(name, "--popup-radius")
&& parse_u32_value(value, 0, 40, &metric)) {
style->popup_radius = metric;
return;
}

if (!parse_hex_color(value, &color)) {
return;
}

if (!strcmp(name, "--top-bg")) {
style->top_bg = color;
} else if (!strcmp(name, "--top-text")) {
style->top_text = color;
} else if (!strcmp(name, "--quick-button-bg")) {
style->quick_button_bg = color;
} else if (!strcmp(name, "--quick-button-fg")) {
style->quick_button_fg = color;
} else if (!strcmp(name, "--quick-panel-bg")) {
style->quick_panel_bg = color;
} else if (!strcmp(name, "--quick-tile-bg")) {
style->quick_tile_bg = color;
} else if (!strcmp(name, "--quick-title-text")) {
style->quick_title_text = color;
} else if (!strcmp(name, "--side-bg")) {
style->side_bg = color;
} else if (!strcmp(name, "--side-popup-bg")) {
style->side_popup_bg = color;
} else if (!strcmp(name, "--side-separator")) {
style->side_separator = color;
} else if (!strcmp(name, "--side-launcher-bg")) {
style->side_launcher_bg = color;
} else if (!strcmp(name, "--side-slot-active")) {
style->side_slot_active = color;
} else if (!strcmp(name, "--side-slot-inactive")) {
style->side_slot_inactive = color;
}
}

static void
load_shell_style(struct shell_style *style)
{
shell_style_defaults(style);

const char *override_path = getenv("KARTON_SHELL_CSS");
char default_path[PATH_MAX] = { 0 };

if (!override_path || !*override_path) {
const char *xdg = getenv("XDG_CONFIG_HOME");
const char *home = getenv("HOME");
if (xdg && *xdg) {
snprintf(default_path, sizeof(default_path), "%s/karton/shell.css", xdg);
} else if (home && *home) {
snprintf(default_path, sizeof(default_path), "%s/.config/karton/shell.css", home);
}
override_path = default_path;
}

if (!override_path || !*override_path) {
load_theme_mode_override(style);
return;
}

FILE *f = fopen(override_path, "r");
if (!f) {
load_theme_mode_override(style);
return;
}

char *line = NULL;
size_t cap = 0;

while (getline(&line, &cap, f) != -1) {
char *var = strstr(line, "--");
if (!var) {
continue;
}

char *colon = strchr(var, ':');
if (!colon) {
continue;
}

*colon = '\0';
char *name = trim_in_place(var);
char *value = trim_in_place(colon + 1);
char *semi = strchr(value, ';');
if (semi) {
*semi = '\0';
}
value = trim_in_place(value);

if (name[0] && value[0]) {
apply_style_var(style, name, value);
}
}

free(line);
fclose(f);

load_theme_mode_override(style);
}

static void
format_local_datetime(char *date_out, size_t date_size,
char *time_out, size_t time_size)
{
time_t now = time(NULL);
struct tm tm_now = { 0 };

if (!date_out || date_size == 0 || !time_out || time_size == 0) {
return;
}

if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
date_out[0] = '\0';
time_out[0] = '\0';
return;
}

if (strftime(date_out, date_size, "%a, %d %b", &tm_now) == 0) {
date_out[0] = '\0';
}
if (strftime(time_out, time_size, "%H:%M", &tm_now) == 0) {
time_out[0] = '\0';
}
}

static void
spawn_command(const char *command)
{
if (!command || !*command) {
return;
}

pid_t pid = fork();
if (pid == 0) {
execl("/bin/sh", "sh", "-c", command, (char *)NULL);
_exit(127);
}
}

static bool
read_command_first_line(const char *command, char *out, size_t out_size)
{
if (!command || !out || out_size == 0) {
return false;
}
out[0] = '\0';

FILE *f = popen(command, "r");
if (!f) {
return false;
}

bool ok = fgets(out, out_size, f) != NULL;
pclose(f);
if (!ok) {
out[0] = '\0';
return false;
}

trim_in_place(out);
return out[0] != '\0';
}

static bool
command_output_is_yes(const char *command)
{
char line[64] = { 0 };
return read_command_first_line(command, line, sizeof(line))
&& (!strcasecmp(line, "yes") || !strcasecmp(line, "true") || !strcmp(line, "1"));
}

static void refresh_quick_status(struct app *app);

static int
clamp_percent_value(int value)
{
if (value < 0) {
return 0;
}
if (value > 100) {
return 100;
}
return value;
}

static void
status_helper_command(char *command, size_t command_size, const char *args)
{
if (!command || command_size == 0) {
return;
}

command[0] = '\0';

char exe_path[PATH_MAX] = { 0 };
ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
if (exe_len > 0) {
exe_path[exe_len] = '\0';
char *slash = strrchr(exe_path, '/');
if (slash) {
*slash = '\0';
char helper_path[PATH_MAX] = { 0 };
size_t exe_dir_len = strlen(exe_path);
static const char helper_suffix[] = "/karton-system-status";
if (exe_dir_len + sizeof(helper_suffix) <= sizeof(helper_path)) {
memcpy(helper_path, exe_path, exe_dir_len);
memcpy(helper_path + exe_dir_len, helper_suffix, sizeof(helper_suffix));
}
if (helper_path[0] && access(helper_path, X_OK) == 0) {
            snprintf(command, command_size, "'%s' %s", helper_path, args && *args ? args : "quick");
return;
}
}
}

snprintf(command, command_size,
"sh -lc 'if command -v karton-system-status >/dev/null 2>&1; then karton-system-status %s; elif [ -x \"$HOME/.local-karton/bin/karton-system-status\" ]; then \"$HOME/.local-karton/bin/karton-system-status\" %s; fi'",
args && *args ? args : "quick",
args && *args ? args : "quick");
}

static void
quick_status_command(char *command, size_t command_size)
{
status_helper_command(command, command_size, "quick");
}

static void
status_helper_spawn(const char *args)
{
char command[PATH_MAX + 96] = { 0 };
status_helper_command(command, sizeof(command), args);
if (command[0]) {
spawn_command(command);
}
}

static bool
run_command_line_sync(const char *command,
char *stdout_out, size_t stdout_size,
char *stderr_out, size_t stderr_size)
{
if (!command || !command[0]) {
return false;
}

if (stdout_out && stdout_size > 0) {
stdout_out[0] = '\0';
}
if (stderr_out && stderr_size > 0) {
stderr_out[0] = '\0';
}

gchar *raw_stdout = NULL;
gchar *raw_stderr = NULL;
gint wait_status = 0;
GError *error = NULL;
gboolean spawned = g_spawn_command_line_sync(command,
&raw_stdout, &raw_stderr, &wait_status, &error);
bool ok = false;
if (spawned) {
ok = g_spawn_check_wait_status(wait_status, NULL);
}

if (stdout_out && stdout_size > 0 && raw_stdout) {
snprintf(stdout_out, stdout_size, "%s", trim_in_place(raw_stdout));
}
if (stderr_out && stderr_size > 0) {
const char *text = "";
if (raw_stderr && raw_stderr[0]) {
text = trim_in_place(raw_stderr);
} else if (error && error->message) {
text = error->message;
}
snprintf(stderr_out, stderr_size, "%s", text);
}

g_clear_error(&error);
g_free(raw_stdout);
g_free(raw_stderr);
return ok;
}

static bool
status_helper_run_sync(const char *args,
char *stdout_out, size_t stdout_size,
char *stderr_out, size_t stderr_size)
{
char command[PATH_MAX + 96] = { 0 };
status_helper_command(command, sizeof(command), args);
return run_command_line_sync(command, stdout_out, stdout_size, stderr_out, stderr_size);
}

static void
audio_set_volume(bool input_device, int value)
{
char args[48] = { 0 };
snprintf(args, sizeof(args), "%s %d",
input_device ? "set-input-volume" : "set-output-volume", value);
status_helper_spawn(args);
}

static void
show_notification_message(const char *title, const char *message)
{
gchar *quoted_title = g_shell_quote(title && *title ? title : "Karton");
gchar *quoted_message = g_shell_quote(message && *message ? message : "");
char command[1024] = { 0 };
snprintf(command, sizeof(command),
"sh -lc 'command -v notify-send >/dev/null 2>&1 && notify-send %s %s || true'",
quoted_title, quoted_message);
g_free(quoted_title);
g_free(quoted_message);
spawn_command(command);
}

static void
set_quick_brightness(struct app *app, int value)
{
if (!app) {
return;
}

value = clamp_percent_value(value);
if (app->quick_brightness == value) {
return;
}

app->quick_brightness = value;
app->quick_status_updated = 0;

char args[48] = { 0 };
snprintf(args, sizeof(args), "set-brightness %d", value);
status_helper_spawn(args);
}

static void
set_audio_volume_value(struct app *app, bool input_device, int value)
{
if (!app) {
return;
}

value = clamp_percent_value(value);
int *target = input_device ? &app->quick_input_volume : &app->quick_volume;
if (*target == value) {
return;
}

*target = value;
app->quick_status_updated = 0;
audio_set_volume(input_device, value);
}

static bool
set_audio_default_device(struct app *app, bool input_device, size_t index)
{
if (!app) {
return false;
}

char args[64] = { 0 };
char stderr_text[256] = { 0 };
snprintf(args, sizeof(args), "%s %zu",
input_device ? "set-default-input" : "set-default-output", index);
if (!status_helper_run_sync(args, NULL, 0, stderr_text, sizeof(stderr_text))) {
show_notification_message(_("Sound"), stderr_text[0] ? stderr_text : _("Could not change the default device"));
return false;
}

app->quick_status_updated = 0;
refresh_quick_status(app);
return true;
}

static bool
load_quick_status_from_helper(struct app *app)
{
if (!app) {
return false;
}

app->quick_connection_type[0] = '\0';
app->quick_connection_name[0] = '\0';
app->quick_iface[0] = '\0';
app->quick_rx_bytes = 0;
app->quick_tx_bytes = 0;
app->quick_network_count = 0;
app->quick_output_count = 0;
app->quick_input_count = 0;
app->quick_default_output[0] = '\0';
app->quick_default_input[0] = '\0';
app->quick_removable_count = 0;
app->quick_battery_present = false;
app->quick_battery_charging = false;
app->quick_battery_percent = 0;
app->quick_battery_minutes_to_empty = -1;
app->quick_battery_minutes_to_full = -1;
app->quick_input_volume = 0;

char command[PATH_MAX + 96] = { 0 };
quick_status_command(command, sizeof(command));
if (!command[0]) {
return false;
}

FILE *f = popen(command, "r");
if (!f) {
return false;
}

bool saw_any = false;
char line[256] = { 0 };
while (fgets(line, sizeof(line), f)) {
char *text = trim_in_place(line);
if (!text[0]) {
continue;
}

char *sep = strchr(text, '=');
if (!sep) {
continue;
}
*sep = '\0';

char *key = trim_in_place(text);
char *value = trim_in_place(sep + 1);
if (!key[0]) {
continue;
}

saw_any = true;
if (!strcmp(key, "wifi_enabled")) {
app->quick_wifi_enabled = !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcmp(value, "1");
} else if (!strcmp(key, "bluetooth_enabled")) {
app->quick_bluetooth_enabled = !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcmp(value, "1");
} else if (!strcmp(key, "wifi_name")) {
snprintf(app->quick_wifi_name, sizeof(app->quick_wifi_name), "%s", value[0] ? value : _("Not connected"));
} else if (!strcmp(key, "connection_type")) {
snprintf(app->quick_connection_type, sizeof(app->quick_connection_type), "%s", value);
} else if (!strcmp(key, "connection_name")) {
snprintf(app->quick_connection_name, sizeof(app->quick_connection_name), "%s", value);
} else if (!strcmp(key, "iface")) {
snprintf(app->quick_iface, sizeof(app->quick_iface), "%s", value);
} else if (!strcmp(key, "rx_bytes")) {
app->quick_rx_bytes = strtoull(value, NULL, 10);
} else if (!strcmp(key, "tx_bytes")) {
app->quick_tx_bytes = strtoull(value, NULL, 10);
} else if (!strcmp(key, "battery_present")) {
app->quick_battery_present = !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcmp(value, "1");
} else if (!strcmp(key, "battery_charging")) {
app->quick_battery_charging = !strcasecmp(value, "yes") || !strcasecmp(value, "true") || !strcmp(value, "1");
} else if (!strcmp(key, "battery_percent")) {
app->quick_battery_percent = (int)strtol(value, NULL, 10);
} else if (!strcmp(key, "battery_minutes_to_empty")) {
app->quick_battery_minutes_to_empty = (int)strtol(value, NULL, 10);
} else if (!strcmp(key, "battery_minutes_to_full")) {
app->quick_battery_minutes_to_full = (int)strtol(value, NULL, 10);
} else if (!strcmp(key, "default_output")) {
snprintf(app->quick_default_output, sizeof(app->quick_default_output), "%s", value);
} else if (!strcmp(key, "default_input")) {
snprintf(app->quick_default_input, sizeof(app->quick_default_input), "%s", value);
} else if (!strcmp(key, "output_volume")) {
app->quick_volume = (int)strtol(value, NULL, 10);
} else if (!strcmp(key, "input_volume")) {
app->quick_input_volume = (int)strtol(value, NULL, 10);
} else if (!strcmp(key, "brightness")) {
int brightness = (int)strtol(value, NULL, 10);
if (brightness >= 0) {
app->quick_brightness = clamp_percent_value(brightness);
}
} else if (!strncmp(key, "network_", 8)) {
int idx = (int)strtol(key + 8, NULL, 10);
if (idx >= 0 && idx < MAX_STATUS_NETWORKS) {
snprintf(app->quick_networks[idx], sizeof(app->quick_networks[idx]), "%s", value);
if ((size_t)(idx + 1) > app->quick_network_count) {
app->quick_network_count = (size_t)(idx + 1);
}
}
} else if (!strncmp(key, "output_", 7)) {
int idx = (int)strtol(key + 7, NULL, 10);
if (idx >= 0 && idx < MAX_STATUS_AUDIO_DEVICES) {
snprintf(app->quick_outputs[idx], sizeof(app->quick_outputs[idx]), "%s", value);
if ((size_t)(idx + 1) > app->quick_output_count) {
app->quick_output_count = (size_t)(idx + 1);
}
}
} else if (!strncmp(key, "input_", 6)) {
int idx = (int)strtol(key + 6, NULL, 10);
if (idx >= 0 && idx < MAX_STATUS_AUDIO_DEVICES) {
snprintf(app->quick_inputs[idx], sizeof(app->quick_inputs[idx]), "%s", value);
if ((size_t)(idx + 1) > app->quick_input_count) {
app->quick_input_count = (size_t)(idx + 1);
}
}
} else if (!strncmp(key, "removable_path_", 15)) {
int idx = (int)strtol(key + 15, NULL, 10);
if (idx >= 0 && idx < MAX_STATUS_REMOVABLE_DEVICES) {
snprintf(app->quick_removable_paths[idx], sizeof(app->quick_removable_paths[idx]), "%s", value);
if ((size_t)(idx + 1) > app->quick_removable_count) {
app->quick_removable_count = (size_t)(idx + 1);
}
}
} else if (!strncmp(key, "removable_name_", 15)) {
int idx = (int)strtol(key + 15, NULL, 10);
if (idx >= 0 && idx < MAX_STATUS_REMOVABLE_DEVICES) {
snprintf(app->quick_removable_names[idx], sizeof(app->quick_removable_names[idx]), "%s", value);
if ((size_t)(idx + 1) > app->quick_removable_count) {
app->quick_removable_count = (size_t)(idx + 1);
}
}
}
}

pclose(f);
if (!saw_any) {
return false;
}

if (!app->quick_wifi_name[0]) {
snprintf(app->quick_wifi_name, sizeof(app->quick_wifi_name), "%s", _("Not connected"));
}
return true;
}

static bool
wifi_enabled(void)
{
return command_output_is_yes("sh -lc 'command -v timeout >/dev/null 2>&1 && command -v nmcli >/dev/null 2>&1 && timeout 1s nmcli -t -f WIFI general status 2>/dev/null | head -n1'");
}

static bool
bluetooth_enabled(void)
{
return command_output_is_yes("sh -lc 'command -v timeout >/dev/null 2>&1 && command -v bluetoothctl >/dev/null 2>&1 && timeout 1s bluetoothctl show 2>/dev/null | grep -q \"Powered: yes\" && printf yes || printf no'");
}

static void
wifi_network_name(char *out, size_t out_size)
{
if (!read_command_first_line("sh -lc 'command -v timeout >/dev/null 2>&1 && command -v nmcli >/dev/null 2>&1 && timeout 1s nmcli -t -f NAME connection show --active 2>/dev/null | head -n1'", out, out_size)) {
snprintf(out, out_size, "%s", _("Not connected"));
}
}

static void
clear_notifications_cache(void)
{
    const char *cache_home = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    char path[PATH_MAX] = { 0 };

    if (cache_home && *cache_home) {
        snprintf(path, sizeof(path), "%s/karton/notifications.log", cache_home);
    } else if (home && *home) {
        snprintf(path, sizeof(path), "%s/.cache/karton/notifications.log", home);
    }

    if (!path[0]) {
        return;
    }

    FILE *f = fopen(path, "w");
    if (f) {
        fclose(f);
    }
}

static void
load_system_notifications(struct app *app)
{
if (!app) {
return;
}

time_t now = time(NULL);
if (app->notifications_updated > 0
&& now != (time_t)-1
&& now - app->notifications_updated < 4) {
return;
}

app->notification_count = 0;
FILE *f = popen("sh -lc 'log=\"${XDG_CACHE_HOME:-$HOME/.cache}/karton/notifications.log\"; if [ -s \"$log\" ]; then tail -n 8 \"$log\"; elif command -v timeout >/dev/null 2>&1 && command -v makoctl >/dev/null 2>&1; then timeout 1s makoctl list 2>/dev/null | sed -n \"s/^[[:space:]]*summary: //p;s/^[[:space:]]*Summary: //p\"; fi'", "r");
if (!f) {
return;
}

char line[240];
while (app->notification_count < MAX_NOTIFICATIONS && fgets(line, sizeof(line), f)) {
char *text = trim_in_place(line);
if (!text[0]) {
continue;
}
char *app_name = "";
char *summary = text;
char *body = "";
char *sep = strchr(text, '\t');
if (sep) {
*sep = '\0';
app_name = trim_in_place(text);
summary = trim_in_place(sep + 1);
sep = strchr(summary, '\t');
if (sep) {
*sep = '\0';
body = trim_in_place(sep + 1);
}
}
notification_prepare_entry(app,
&app->notifications[app->notification_count++],
app_name, summary, body);
}
pclose(f);
if (app->notification_count > 0) {
    app->notifications_cleared = false;
}
app->notifications_updated = now;
}

static void
refresh_quick_status(struct app *app)
{
if (!app) {
return;
}

time_t now = time(NULL);
if (app->quick_status_updated > 0
&& now != (time_t)-1
&& now - app->quick_status_updated < 5) {
return;
}

if (!load_quick_status_from_helper(app)) {
app->quick_wifi_enabled = wifi_enabled();
app->quick_bluetooth_enabled = bluetooth_enabled();
wifi_network_name(app->quick_wifi_name, sizeof(app->quick_wifi_name));
}
app->quick_status_updated = now;
}

static bool
run_argv_sync(char **argv, char *output, size_t output_size)
{
if (output && output_size > 0) {
output[0] = '\0';
}

gchar *raw_stdout = NULL;
gchar *raw_stderr = NULL;
gint wait_status = 0;
GError *error = NULL;
gboolean spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
NULL, NULL, &raw_stdout, &raw_stderr, &wait_status, &error);
bool ok = false;
if (spawned) {
ok = g_spawn_check_wait_status(wait_status, NULL);
}

if (output && output_size > 0) {
const char *text = "";
if (raw_stderr && raw_stderr[0]) {
text = trim_in_place(raw_stderr);
} else if (raw_stdout && raw_stdout[0]) {
text = trim_in_place(raw_stdout);
} else if (error && error->message) {
text = error->message;
}
snprintf(output, output_size, "%s", text);
}

g_clear_error(&error);
g_free(raw_stdout);
g_free(raw_stderr);
return ok;
}

static bool
prompt_wifi_password(const char *ssid, char *password_out, size_t password_size)
{
if (!password_out || password_size == 0) {
return false;
}

password_out[0] = '\0';

char title[160] = { 0 };
char text[224] = { 0 };
snprintf(title, sizeof(title), "%s", _("Wi-Fi password"));
snprintf(text, sizeof(text), _("Enter the password for %s"), ssid && *ssid ? ssid : _("selected network"));

gchar *quoted_title = g_shell_quote(title);
gchar *quoted_text = g_shell_quote(text);
char command[1024] = { 0 };
snprintf(command, sizeof(command),
"sh -lc 'if command -v karton-dialog >/dev/null 2>&1; then karton-dialog --password --title %s --text %s; elif [ -x \"$HOME/.local-karton/bin/karton-dialog\" ]; then \"$HOME/.local-karton/bin/karton-dialog\" --password --title %s --text %s; else exit 127; fi'",
quoted_title, quoted_text, quoted_title, quoted_text);
g_free(quoted_title);
g_free(quoted_text);

return run_command_line_sync(command, password_out, password_size, NULL, 0)
&& trim_in_place(password_out)[0] != '\0';
}

static bool
connect_wifi_nmcli(const char *ssid, const char *password, char *error_out, size_t error_size)
{
char *argv[8] = { 0 };
int argc = 0;
argv[argc++] = "nmcli";
argv[argc++] = "device";
argv[argc++] = "wifi";
argv[argc++] = "connect";
argv[argc++] = (char *)ssid;
if (password && *password) {
argv[argc++] = "password";
argv[argc++] = (char *)password;
}
argv[argc] = NULL;
return run_argv_sync(argv, error_out, error_size);
}

static void
connect_wifi_network(struct app *app, const char *ssid)
{
if (!app || !ssid || !ssid[0]) {
return;
}

if (!app->quick_wifi_enabled) {
show_notification_message(_("Wi-Fi"), _("Enable Wi-Fi before connecting to a network"));
return;
}

if (!strcmp(app->quick_connection_type, "wifi")
&& app->quick_connection_name[0]
&& strcmp(app->quick_connection_name, _("Not connected"))
&& !strcmp(app->quick_connection_name, ssid)) {
return;
}

char error_text[256] = { 0 };
if (connect_wifi_nmcli(ssid, NULL, error_text, sizeof(error_text))) {
    app->quick_status_updated = 0;
    refresh_quick_status(app);
    return;
}

char password[256] = { 0 };
if (!prompt_wifi_password(ssid, password, sizeof(password))) {
    return;
}

if (connect_wifi_nmcli(ssid, password, error_text, sizeof(error_text))) {
    app->quick_status_updated = 0;
    refresh_quick_status(app);
    return;
}

show_notification_message(_("Wi-Fi"), error_text[0] ? error_text : _("Wrong password or connection failed"));
}

static void
format_status_bytes(unsigned long long value, char *out, size_t out_size)
{
if (!out || out_size == 0) {
return;
}

const char *units[] = { "B", "KB", "MB", "GB", "TB" };
double scaled = (double)value;
size_t unit = 0;
while (scaled >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
scaled /= 1024.0;
unit++;
}

if (unit == 0) {
snprintf(out, out_size, "%llu %s", value, units[unit]);
} else {
snprintf(out, out_size, "%.1f %s", scaled, units[unit]);
}
}

static void
format_status_minutes(int minutes, char *out, size_t out_size)
{
if (!out || out_size == 0) {
return;
}
if (minutes < 0) {
out[0] = '\0';
return;
}

int hours = minutes / 60;
int mins = minutes % 60;
if (hours > 0) {
snprintf(out, out_size, "%dh %02dm", hours, mins);
} else {
snprintf(out, out_size, "%dm", mins);
}
}

static void
sync_environment_theme(int mode)
{
char cmd[160] = { 0 };
snprintf(cmd, sizeof(cmd), "karton-apply-theme %s", theme_mode_name(mode));
spawn_command(cmd);

/* In split-process mode, restart side panel so it reloads the new theme. */
spawn_command("pkill -f 'karton-shell --side-only' >/dev/null 2>&1 || true");
}

static void
reload_shell_style_runtime(struct app *app)
{
if (!app) {
return;
}

load_shell_style(&app->style);
shell_style_apply_theme(&app->style);

if (app->top.layer_surface) {
zwlr_layer_surface_v1_set_margin(app->top.layer_surface,
0, 0, 0, app->side_enabled ? app->style.side_width : 0);
request_top_panel_size(app);
}
if (app->side.layer_surface) {
request_side_panel_size(app);
}

trigger_redraw(app);
}

static void
process_runtime_signals(struct app *app)
{
if (!theme_reload_requested) {
return;
}
theme_reload_requested = 0;
reload_shell_style_runtime(app);
}

static void
launch_desktop_id(const char *desktop_id)
{
if (!desktop_id || !*desktop_id) {
return;
}

pid_t pid = fork();
if (pid == 0) {
execlp("gtk-launch", "gtk-launch", desktop_id, (char *)NULL);
_exit(127);
}
}

static bool
notification_find_desktop_id(const struct app *app, const char *app_name,
char *out, size_t out_size)
{
if (!app || !app_name || !*app_name || !out || out_size == 0) {
return false;
}

out[0] = '\0';
for (size_t i = 0; i < app->launcher_count; i++) {
const struct launcher_entry *entry = &app->launcher_entries[i];
if (!entry->desktop_id[0]) {
continue;
}
if ((entry->name[0] && (contains_nocase(entry->name, app_name)
|| contains_nocase(app_name, entry->name)))
|| contains_nocase(entry->desktop_id, app_name)
|| contains_nocase(app_name, entry->desktop_id)) {
snprintf(out, out_size, "%s", entry->desktop_id);
return true;
}
}

return false;
}

static void
open_path_location(const char *path)
{
if (!path || !*path) {
return;
}

char target[PATH_MAX] = { 0 };
struct stat st = { 0 };
if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
snprintf(target, sizeof(target), "%s", path);
} else {
snprintf(target, sizeof(target), "%s", path);
char *slash = strrchr(target, '/');
if (slash && slash != target) {
*slash = '\0';
} else if (slash == target) {
target[1] = '\0';
} else {
snprintf(target, sizeof(target), ".");
}
}

pid_t pid = fork();
if (pid == 0) {
execlp("xdg-open", "xdg-open", target, (char *)NULL);
_exit(127);
}
}

static void
notification_prepare_entry(struct app *app, struct notification_entry *entry,
const char *app_name, const char *summary, const char *body)
{
if (!entry) {
return;
}

memset(entry, 0, sizeof(*entry));
if (app_name && *app_name) {
snprintf(entry->app_name, sizeof(entry->app_name), "%s", app_name);
}

const char *title = summary && *summary ? summary : body;
if (title && *title) {
snprintf(entry->text, sizeof(entry->text), "%s", title);
}

if (body && *body && (!title || strcmp(body, title))) {
const char *detail = body;
if (strchr(body, '/')) {
const char *base = strrchr(body, '/');
if (base && *(base + 1)) {
detail = base + 1;
}
}
snprintf(entry->detail, sizeof(entry->detail), "%s", detail);
} else if (entry->app_name[0]) {
snprintf(entry->detail, sizeof(entry->detail), "%s", entry->app_name);
}

if (body && *body && access(body, F_OK) == 0) {
entry->target_kind = NOTIFICATION_TARGET_OPEN_LOCATION;
snprintf(entry->action_target, sizeof(entry->action_target), "%s", body);
snprintf(entry->action_label, sizeof(entry->action_label), "%s",
((summary && contains_nocase(summary, "screenshot"))
|| contains_nocase(entry->app_name, "screenshot")
|| str_ends_with(body, ".png")
|| str_ends_with(body, ".jpg")
|| str_ends_with(body, ".jpeg")
|| str_ends_with(body, ".webp"))
? _("Open screenshot folder")
: _("Open file location"));
return;
}

if (notification_find_desktop_id(app, entry->app_name,
entry->action_target, sizeof(entry->action_target))) {
entry->target_kind = NOTIFICATION_TARGET_LAUNCH_APP;
snprintf(entry->action_label, sizeof(entry->action_label), "%s", _("Open app"));
}
}

static void
notification_activate(struct app *app, const struct notification_entry *entry)
{
if (!app || !entry) {
return;
}

if (entry->target_kind == NOTIFICATION_TARGET_OPEN_LOCATION) {
open_path_location(entry->action_target);
return;
}

if (entry->target_kind == NOTIFICATION_TARGET_LAUNCH_APP) {
launch_desktop_id(entry->action_target);
}
}

static bool
str_ends_with(const char *s, const char *suffix)
{
if (!s || !suffix) {
return false;
}
size_t ls = strlen(s);
size_t lf = strlen(suffix);
if (lf > ls) {
return false;
}
return strcmp(s + ls - lf, suffix) == 0;
}

static bool
launcher_geometry(const struct panel *panel, double *x, double *y, double *w, double *h)
{
if (!panel || !panel->app) {
return false;
}

if (!panel->app->launcher_open || panel->width <= panel->app->style.side_width + 40) {
return false;
}

*x = panel->app->style.side_width + 10.0;
*y = 8.0;
*w = panel->width - *x - 10.0;
*h = panel->height - 16.0;
if (*w < 180.0 || *h < 120.0) {
return false;
}
return true;
}

static uint32_t
launcher_panel_width(const struct app *app)
{
uint32_t width = app ? app->style.side_expanded_width : 0;
if (width < 720) {
width = 720;
}
return width;
}

static bool
contains_nocase(const char *haystack, const char *needle)
{
if (!needle || !needle[0]) {
return true;
}
if (!haystack || !haystack[0]) {
return false;
}

size_t needle_len = strlen(needle);
size_t hay_len = strlen(haystack);
if (needle_len > hay_len) {
return false;
}

for (size_t i = 0; i + needle_len <= hay_len; i++) {
size_t j = 0;
for (; j < needle_len; j++) {
unsigned char a = (unsigned char)haystack[i + j];
unsigned char b = (unsigned char)needle[j];
if (tolower(a) != tolower(b)) {
break;
}
}
if (j == needle_len) {
return true;
}
}

return false;
}

static void
load_icon_theme_name(struct app *app)
{
if (!app) {
return;
}

snprintf(app->icon_theme_name, sizeof(app->icon_theme_name), "%s", "hicolor");

char path[PATH_MAX] = { 0 };
const char *xdg = getenv("XDG_CONFIG_HOME");
const char *home = getenv("HOME");
if (xdg && *xdg) {
snprintf(path, sizeof(path), "%s/gtk-3.0/settings.ini", xdg);
} else if (home && *home) {
snprintf(path, sizeof(path), "%s/.config/gtk-3.0/settings.ini", home);
}
if (!path[0]) {
return;
}

FILE *f = fopen(path, "r");
if (!f) {
return;
}

char line[256];
while (fgets(line, sizeof(line), f)) {
char *s = trim_in_place(line);
if (strncmp(s, "gtk-icon-theme-name=", 20)) {
continue;
}
snprintf(app->icon_theme_name, sizeof(app->icon_theme_name), "%s", trim_in_place(s + 20));
break;
}
fclose(f);
}

static bool
desktop_id_matches_app_id(const char *desktop_id, const char *app_id)
{
if (!desktop_id || !desktop_id[0] || !app_id || !app_id[0]) {
return false;
}
if (!strcasecmp(desktop_id, app_id)) {
return true;
}

const char *tail = strrchr(app_id, '.');
tail = (tail && *(tail + 1)) ? tail + 1 : app_id;
if (!strcasecmp(desktop_id, tail)) {
return true;
}

char desktop_dash[128] = { 0 };
snprintf(desktop_dash, sizeof(desktop_dash), "%s", desktop_id);
for (size_t i = 0; desktop_dash[i]; i++) {
if (desktop_dash[i] == '-') {
desktop_dash[i] = '.';
}
}
if (!strcasecmp(desktop_dash, app_id)) {
return true;
}

return contains_nocase(app_id, desktop_id) || contains_nocase(desktop_id, tail);
}

static bool
launcher_icon_name_for_app_id(const struct app *app, const char *app_id,
char *out, size_t out_size)
{
if (!app || !app_id || !*app_id || !out || out_size == 0) {
return false;
}
out[0] = '\0';

for (size_t i = 0; i < app->launcher_count; i++) {
const struct launcher_entry *entry = &app->launcher_entries[i];
if (!entry->icon_name[0]) {
continue;
}
if (desktop_id_matches_app_id(entry->desktop_id, app_id)) {
snprintf(out, out_size, "%s", entry->icon_name);
return true;
}
}

return false;
}

static bool
resolve_repo_icon_path(const char *icon_name, char *out, size_t out_size)
{
if (!icon_name || !*icon_name || !out || out_size == 0) {
return false;
}

const char *env_dir = getenv("KARTON_ICON_DIR");
char project_icons_dir[PATH_MAX] = { 0 };
char repo_icons_dir[PATH_MAX] = { 0 };
char exe_path[PATH_MAX] = { 0 };
ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
if (exe_len > 0) {
exe_path[exe_len] = '\0';
char *slash = strrchr(exe_path, '/');
if (slash) {
*slash = '\0';
slash = strrchr(exe_path, '/');
if (slash) {
*slash = '\0';
size_t base_len = strlen(exe_path);
if (base_len + strlen("/icons") < sizeof(project_icons_dir)) {
memcpy(project_icons_dir, exe_path, base_len + 1);
memcpy(project_icons_dir + base_len, "/icons", strlen("/icons") + 1);
}

char *repo_slash = strrchr(exe_path, '/');
if (repo_slash) {
*repo_slash = '\0';
size_t repo_len = strlen(exe_path);
if (repo_len + strlen("/icons") < sizeof(repo_icons_dir)) {
memcpy(repo_icons_dir, exe_path, repo_len + 1);
memcpy(repo_icons_dir + repo_len, "/icons", strlen("/icons") + 1);
}
}
}
}
}

const char *dirs[] = {
env_dir,
DATADIR "/karton/icons",
repo_icons_dir,
project_icons_dir,
};
bool has_ext = str_ends_with(icon_name, ".svg") || str_ends_with(icon_name, ".png");

for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
if (!dirs[i] || !dirs[i][0]) {
continue;
}

if (has_ext) {
snprintf(out, out_size, "%s/%s", dirs[i], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
continue;
}

snprintf(out, out_size, "%s/%s.svg", dirs[i], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
snprintf(out, out_size, "%s/%s.png", dirs[i], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
}

out[0] = '\0';
return false;
}

static bool
resolve_icon_path(const struct app *app, const char *icon_name,
char *out, size_t out_size)
{
if (!icon_name || !*icon_name || !out || out_size == 0) {
return false;
}
out[0] = '\0';

bool has_ext = str_ends_with(icon_name, ".svg") || str_ends_with(icon_name, ".png");

if (icon_name[0] == '/') {
if (access(icon_name, R_OK) == 0
&& (str_ends_with(icon_name, ".png") || str_ends_with(icon_name, ".svg"))) {
snprintf(out, out_size, "%s", icon_name);
return true;
}
if (!strchr(icon_name, '.')) {
snprintf(out, out_size, "%s.svg", icon_name);
if (access(out, R_OK) == 0) {
return true;
}
snprintf(out, out_size, "%s.png", icon_name);
if (access(out, R_OK) == 0) {
return true;
}
}
return false;
}

if (resolve_repo_icon_path(icon_name, out, out_size)) {
return true;
}

const char *home = getenv("HOME");
char user_local[PATH_MAX] = { 0 };
char user_icons[PATH_MAX] = { 0 };
if (home && *home) {
snprintf(user_local, sizeof(user_local), "%s/.local/share/icons", home);
snprintf(user_icons, sizeof(user_icons), "%s/.icons", home);
}

const char *bases[] = {
user_local,
user_icons,
"/usr/share/icons",
"/usr/share/pixmaps",
};
const char *themes[] = {
app && app->icon_theme_name[0] ? app->icon_theme_name : "hicolor",
"hicolor",
"Adwaita",
"Papirus",
"Tela",
"Kora",
};
const int sizes[] = { 64, 48, 32, 24, 22, 16 };
const char *contexts[] = { "apps", "actions" };
const char *extensions[] = { ".png", ".svg" };

for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
if (!bases[b] || !bases[b][0]) {
continue;
}

if (!strcmp(bases[b], "/usr/share/pixmaps")) {
if (has_ext) {
snprintf(out, out_size, "%s/%s", bases[b], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
} else {
for (size_t ext = 0; ext < sizeof(extensions) / sizeof(extensions[0]); ext++) {
snprintf(out, out_size, "%s/%s%s", bases[b], icon_name, extensions[ext]);
if (access(out, R_OK) == 0) {
return true;
}
}
}
continue;
}

for (size_t t = 0; t < sizeof(themes) / sizeof(themes[0]); t++) {
if (!themes[t] || !themes[t][0]) {
continue;
}
for (size_t c = 0; c < sizeof(contexts) / sizeof(contexts[0]); c++) {
for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
if (has_ext) {
snprintf(out, out_size, "%s/%s/%dx%d/%s/%s",
bases[b], themes[t], sizes[s], sizes[s], contexts[c], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
continue;
}

for (size_t ext = 0; ext < sizeof(extensions) / sizeof(extensions[0]); ext++) {
snprintf(out, out_size, "%s/%s/%dx%d/%s/%s%s",
bases[b], themes[t], sizes[s], sizes[s], contexts[c], icon_name, extensions[ext]);
if (access(out, R_OK) == 0) {
return true;
}
}
}

if (has_ext) {
snprintf(out, out_size, "%s/%s/scalable/%s/%s",
bases[b], themes[t], contexts[c], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
snprintf(out, out_size, "%s/%s/%s/%s",
bases[b], themes[t], contexts[c], icon_name);
if (access(out, R_OK) == 0) {
return true;
}
continue;
}

for (size_t ext = 0; ext < sizeof(extensions) / sizeof(extensions[0]); ext++) {
snprintf(out, out_size, "%s/%s/scalable/%s/%s%s",
bases[b], themes[t], contexts[c], icon_name, extensions[ext]);
if (access(out, R_OK) == 0) {
return true;
}
snprintf(out, out_size, "%s/%s/%s/%s%s",
bases[b], themes[t], contexts[c], icon_name, extensions[ext]);
if (access(out, R_OK) == 0) {
return true;
}
}
}
}
}

out[0] = '\0';
return false;
}

static cairo_surface_t *
cairo_surface_from_pixbuf(GdkPixbuf *pixbuf)
{
if (!pixbuf) {
return NULL;
}

int width = gdk_pixbuf_get_width(pixbuf);
int height = gdk_pixbuf_get_height(pixbuf);
if (width <= 0 || height <= 0) {
return NULL;
}

cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
if (surface) {
cairo_surface_destroy(surface);
}
return NULL;
}

guchar *src = gdk_pixbuf_get_pixels(pixbuf);
int src_stride = gdk_pixbuf_get_rowstride(pixbuf);
gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
unsigned char *dst = cairo_image_surface_get_data(surface);
int dst_stride = cairo_image_surface_get_stride(surface);

for (int y = 0; y < height; y++) {
guchar *src_row = src + y * src_stride;
unsigned char *dst_row = dst + y * dst_stride;
for (int x = 0; x < width; x++) {
guchar *src_px = src_row + x * (has_alpha ? 4 : 3);
unsigned char *dst_px = dst_row + x * 4;
uint8_t r = src_px[0];
uint8_t g = src_px[1];
uint8_t b = src_px[2];
uint8_t a = has_alpha ? src_px[3] : 255;
uint8_t pr = (uint8_t)((r * a + 127) / 255);
uint8_t pg = (uint8_t)((g * a + 127) / 255);
uint8_t pb = (uint8_t)((b * a + 127) / 255);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
dst_px[0] = pb;
dst_px[1] = pg;
dst_px[2] = pr;
dst_px[3] = a;
#else
dst_px[0] = a;
dst_px[1] = pr;
dst_px[2] = pg;
dst_px[3] = pb;
#endif
}
}

cairo_surface_mark_dirty(surface);
return surface;
}

static cairo_surface_t *
icon_cache_get_surface(struct app *app, const char *icon_name)
{
if (!app || !icon_name || !*icon_name) {
return NULL;
}

for (size_t i = 0; i < app->icon_cache_count; i++) {
if (!strcmp(app->icon_cache[i].icon_name, icon_name)) {
return app->icon_cache[i].surface;
}
}

char path[PATH_MAX] = { 0 };
if (!resolve_icon_path(app, icon_name, path, sizeof(path))) {
return NULL;
}

GError *error = NULL;
GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
if (!pixbuf) {
if (error) {
g_error_free(error);
}
return NULL;
}

cairo_surface_t *surface = cairo_surface_from_pixbuf(pixbuf);
g_object_unref(pixbuf);
if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
if (surface) {
cairo_surface_destroy(surface);
}
return NULL;
}

if (app->icon_cache_count < MAX_ICON_CACHE) {
struct icon_cache_entry *entry = &app->icon_cache[app->icon_cache_count++];
snprintf(entry->icon_name, sizeof(entry->icon_name), "%s", icon_name);
entry->surface = surface;
return surface;
}

cairo_surface_destroy(surface);
return NULL;
}

static bool
draw_named_icon(cairo_t *cairo, struct app *app, const char *icon_name,
double cx, double cy, double size)
{
if (!cairo || !app || !icon_name || !*icon_name) {
return false;
}

cairo_surface_t *surface = icon_cache_get_surface(app, icon_name);
if (!surface) {
return false;
}

int sw = cairo_image_surface_get_width(surface);
int sh = cairo_image_surface_get_height(surface);
if (sw <= 0 || sh <= 0) {
return false;
}

double scale_x = size / (double)sw;
double scale_y = size / (double)sh;
double scale = scale_x < scale_y ? scale_x : scale_y;
double dw = sw * scale;
double dh = sh * scale;

cairo_save(cairo);
cairo_translate(cairo, cx - dw * 0.5, cy - dh * 0.5);
cairo_scale(cairo, scale, scale);
cairo_set_source_surface(cairo, surface, 0, 0);
cairo_paint(cairo);
cairo_restore(cairo);
return true;
}

static bool
draw_group_app_icon(cairo_t *cairo, struct app *app, const struct app_group *group,
double cx, double cy, double size)
{
if (!cairo || !app || !group) {
return false;
}

char icon_name[128] = { 0 };
if (!launcher_icon_name_for_app_id(app, group->app_id, icon_name, sizeof(icon_name))) {
return false;
}

return draw_named_icon(cairo, app, icon_name, cx, cy, size);
}

static void
launcher_layout(const struct panel *panel,
double *x, double *y, double *w, double *h,
double *search_y, double *chips_y, double *list_y,
double *row_h, int *max_visible)
{
launcher_geometry(panel, x, y, w, h);

*search_y = *y + 20.0;
*chips_y = *y + 74.0;

size_t preview_count = launcher_collect_favorite_preview(panel->app, NULL, 0);
if (preview_count > 8) {
preview_count = 8;
}
int preview_rows = preview_count == 0 ? 0 : (int)((preview_count + 3) / 4);
double favorites_h = preview_rows == 0 ? 0.0 : (30.0 + preview_rows * 82.0 + (preview_rows - 1) * 12.0);

*list_y = *search_y + 76.0 + favorites_h + 42.0;
*row_h = 44.0;
*max_visible = (int)((*y + *h - *list_y - 92.0) / *row_h);
if (*max_visible < 1) {
*max_visible = 1;
}
}

static void
launcher_content_rect(const struct panel *panel,
double *x, double *y, double *w, double *h)
{
double lx, ly, lw, lh;
if (!launcher_geometry(panel, &lx, &ly, &lw, &lh)) {
*x = *y = *w = *h = 0.0;
return;
}

*x = lx + 20.0;
*y = ly;
*w = lw - 40.0;
*h = lh;
}

static void
launcher_category_panel_rect(const struct panel *panel,
double *x, double *y, double *w, double *h)
{
double lx, ly, lw, lh;
double search_y, chips_y, list_y, row_h;
int max_visible = 0;

if (!launcher_geometry(panel, &lx, &ly, &lw, &lh)) {
*x = *y = *w = *h = 0.0;
return;
}

launcher_layout(panel, &lx, &ly, &lw, &lh,
&search_y, &chips_y, &list_y, &row_h, &max_visible);

*x = lx + 20.0;
*y = list_y;
*w = 146.0;
*h = ly + lh - list_y - 18.0;
}

static void
launcher_apps_panel_rect(const struct panel *panel,
double *x, double *y, double *w, double *h)
{
double content_x, content_y, content_w, content_h;
double nav_x, nav_y, nav_w, nav_h;

launcher_content_rect(panel, &content_x, &content_y, &content_w, &content_h);
launcher_category_panel_rect(panel, &nav_x, &nav_y, &nav_w, &nav_h);

*x = nav_x + nav_w + 18.0;
*y = nav_y;
*w = content_x + content_w - *x;
*h = nav_h;
}

static bool
launcher_power_action_rect(const struct panel *panel, int action_idx,
double *x, double *y, double *w, double *h)
{
double lx, ly, lw, lh, search_y, chips_y, list_y, row_h;
double content_x, content_y, content_w, content_h;
int max_visible = 0;
const double gap = 12.0;
const int action_count = (int)(sizeof(launcher_power_actions) / sizeof(launcher_power_actions[0]));

if (action_idx < 0 || action_idx >= action_count) {
return false;
}
if (!launcher_geometry(panel, &lx, &ly, &lw, &lh)) {
return false;
}

launcher_layout(panel, &lx, &ly, &lw, &lh,
&search_y, &chips_y, &list_y, &row_h, &max_visible);
launcher_content_rect(panel, &content_x, &content_y, &content_w, &content_h);

*w = 46.0;
*h = 46.0;
if (action_count != 4) {
return false;
}

double total_w = action_count * (*w) + (action_count - 1) * gap;
*x = content_x + content_w - total_w - 14.0 + action_idx * (*w + gap);
*y = ly + lh - *h - 18.0;
return true;
}

static int
launcher_power_action_hit(const struct panel *panel, double px, double py)
{
const int action_count = (int)(sizeof(launcher_power_actions) / sizeof(launcher_power_actions[0]));

for (int i = 0; i < action_count; i++) {
double x, y, w, h;
if (!launcher_power_action_rect(panel, i, &x, &y, &w, &h)) {
continue;
}
if (point_in_rect(px, py, x, y, w, h)) {
return i;
}
}

return -1;
}

static void
launcher_run_power_action(struct app *app, int action_idx)
{
const int action_count = (int)(sizeof(launcher_power_actions) / sizeof(launcher_power_actions[0]));

if (action_idx < 0 || action_idx >= action_count) {
return;
}

spawn_command(launcher_power_actions[action_idx].command);
launcher_close(app);
}

static int
launcher_visible_rows(const struct panel *panel)
{
int max_visible = 1;
double x, y, w, h, search_y, chips_y, list_y, row_h;

if (!panel) {
return max_visible;
}

if (launcher_geometry(panel, &x, &y, &w, &h)) {
launcher_layout(panel, &x, &y, &w, &h,
&search_y, &chips_y, &list_y, &row_h, &max_visible);
}
return max_visible;
}

static int
launcher_max_scroll_offset(const struct app *app, const struct panel *panel)
{
int max_visible = launcher_visible_rows(panel);
if (app->launcher_filtered_count <= (size_t)max_visible) {
return 0;
}
return (int)(app->launcher_filtered_count - (size_t)max_visible);
}

static void
launcher_clamp_scroll_offset(struct app *app, const struct panel *panel)
{
int max_scroll = launcher_max_scroll_offset(app, panel);
if (app->launcher_scroll_offset < 0) {
app->launcher_scroll_offset = 0;
}
if (app->launcher_scroll_offset > max_scroll) {
app->launcher_scroll_offset = max_scroll;
}
}

static void
launcher_ensure_selected_visible(struct app *app, const struct panel *panel)
{
int max_visible = launcher_visible_rows(panel);

launcher_clamp_scroll_offset(app, panel);
if (app->launcher_selected < 0) {
return;
}

if (app->launcher_selected < app->launcher_scroll_offset) {
app->launcher_scroll_offset = app->launcher_selected;
} else if (app->launcher_selected >= app->launcher_scroll_offset + max_visible) {
app->launcher_scroll_offset = app->launcher_selected - max_visible + 1;
}

launcher_clamp_scroll_offset(app, panel);
}

static void
launcher_scroll_by(struct app *app, int delta)
{
if (!app->launcher_open || delta == 0 || app->launcher_filtered_count == 0) {
return;
}

int old_offset = app->launcher_scroll_offset;
app->launcher_scroll_offset += delta;
launcher_clamp_scroll_offset(app, &app->side);
if (app->launcher_scroll_offset == old_offset) {
return;
}

app->launcher_menu_open = false;
app->launcher_menu_target = -1;
app->launcher_menu_hover = -1;
app->launcher_hover_item = -1;
panel_draw(&app->side);
}

static size_t
launcher_collect_favorite_preview(const struct app *app, size_t *out, size_t max_out)
{
if (!app) {
return 0;
}

size_t count = 0;
for (size_t i = 0; i < app->launcher_count; i++) {
const struct launcher_entry *entry = &app->launcher_entries[i];
if (!entry->favorite) {
continue;
}
if (app->launcher_query[0]
&& !contains_nocase(entry->name, app->launcher_query)
&& !contains_nocase(entry->desktop_id, app->launcher_query)) {
continue;
}
if (out && count < max_out) {
out[count] = i;
}
count++;
if (out && count >= max_out) {
break;
}
}

return count;
}

static int
launcher_filtered_index_from_entry(const struct app *app, size_t entry_idx)
{
if (!app) {
return -1;
}

for (size_t i = 0; i < app->launcher_filtered_count; i++) {
if (app->launcher_filtered[i] == entry_idx) {
return (int)i;
}
}

return -1;
}

static bool
launcher_category_rect(const struct panel *panel, int category,
double *x, double *y, double *w, double *h)
{
double nav_x, nav_y, nav_w, nav_h;
if (category < 0 || category >= LCAT_COUNT) {
return false;
}

launcher_category_panel_rect(panel, &nav_x, &nav_y, &nav_w, &nav_h);
if (nav_w <= 0.0 || nav_h <= 0.0) {
return false;
}

*x = nav_x;
*y = nav_y + category * 38.0;
*w = nav_w;
*h = 32.0;
return true;
}

static bool
launcher_favorite_tile_rect(const struct panel *panel, const struct app *app, int preview_idx,
double *x, double *y, double *w, double *h)
{
double lx, ly, lw, lh, search_y, chips_y, list_y, row_h;
int max_visible = 0;
size_t preview_count = launcher_collect_favorite_preview(app, NULL, 0);
if (preview_idx < 0 || preview_idx >= (int)preview_count || preview_idx >= 8
|| !launcher_geometry(panel, &lx, &ly, &lw, &lh)) {
return false;
}

launcher_layout(panel, &lx, &ly, &lw, &lh,
&search_y, &chips_y, &list_y, &row_h, &max_visible);

double content_x, content_y, content_w, content_h;
launcher_content_rect(panel, &content_x, &content_y, &content_w, &content_h);
double tile_w = 64.0;
double tile_h = 80.0;
double spacing = (content_w - 4.0 * tile_w) / 3.0;
if (spacing < 0) spacing = 0;
double base_y = search_y + 78.0;
int row = preview_idx / 4;
int col = preview_idx % 4;
*x = content_x + col * (tile_w + spacing);
*y = base_y + row * (tile_h + 12.0);
*w = tile_w;
*h = tile_h;
return true;
}

static int
launcher_favorite_tile_hit(const struct panel *panel, const struct app *app, double px, double py)
{
size_t preview[8] = { 0 };
size_t preview_count = launcher_collect_favorite_preview(app, preview, G_N_ELEMENTS(preview));
for (int i = 0; i < (int)preview_count; i++) {
double x, y, w, h;
if (!launcher_favorite_tile_rect(panel, app, i, &x, &y, &w, &h)) {
continue;
}
if (point_in_rect(px, py, x, y, w, h)) {
return i;
}
}

return -1;
}

static int
launcher_default_category(const char *categories)
{
if (!categories || !categories[0]) {
return LCAT_OTHER;
}
if (contains_nocase(categories, "Network") || contains_nocase(categories, "Internet")) {
return LCAT_INTERNET;
}
if (contains_nocase(categories, "Office")) {
return LCAT_OFFICE;
}
if (contains_nocase(categories, "Audio") || contains_nocase(categories, "Video")
|| contains_nocase(categories, "Graphics")) {
return LCAT_MEDIA;
}
if (contains_nocase(categories, "Development")) {
return LCAT_DEVELOPMENT;
}
if (contains_nocase(categories, "System") || contains_nocase(categories, "Settings")) {
return LCAT_SYSTEM;
}
if (contains_nocase(categories, "Utility")) {
return LCAT_UTILITY;
}
return LCAT_OTHER;
}

static const char *
launcher_category_label(int category)
{
switch (category) {
case LCAT_ALL:
return _("All");
case LCAT_FAVORITES:
return _("Favorites");
case LCAT_INTERNET:
return _("Internet");
case LCAT_OFFICE:
return _("Office");
case LCAT_MEDIA:
return _("Media");
case LCAT_DEVELOPMENT:
return _("Development");
case LCAT_SYSTEM:
return _("System");
case LCAT_UTILITY:
return _("Utility");
default:
return _("Other");
}
}

static const char *
launcher_category_icon(int category)
{
switch (category) {
case LCAT_ALL:
return "◎";
case LCAT_FAVORITES:
return "★";
case LCAT_INTERNET:
return "⌘";
case LCAT_OFFICE:
return "▦";
case LCAT_MEDIA:
return "▶";
case LCAT_DEVELOPMENT:
return "</>";
case LCAT_SYSTEM:
return "⚙";
case LCAT_UTILITY:
return "⌂";
default:
return "…";
}
}

static const char *
launcher_category_icon_name(int category)
{
switch (category) {
case LCAT_ALL:
return "category-all";
case LCAT_FAVORITES:
return "category-favorites";
case LCAT_INTERNET:
return "category-internet";
case LCAT_OFFICE:
return "category-office";
case LCAT_MEDIA:
return "category-media";
case LCAT_DEVELOPMENT:
return "category-development";
case LCAT_SYSTEM:
return "category-system";
case LCAT_UTILITY:
return "category-utility";
case LCAT_OTHER:
return "category-other";
default:
return "category-all";
}
}

static int
launcher_effective_category(const struct launcher_entry *entry)
{
return entry->category_override >= 0 ? entry->category_override : entry->category;
}

static void
launcher_get_path(char *out, size_t out_size, const char *name)
{
karton_get_config_path(out, out_size, name);
}

static void
launcher_ensure_config_dir(void)
{
karton_ensure_config_dir();
}

static int
launcher_find_favorite(const struct app *app, const char *desktop_id)
{
for (size_t i = 0; i < app->favorite_count; i++) {
if (!strcmp(app->favorite_ids[i], desktop_id)) {
return (int)i;
}
}
return -1;
}

static int
launcher_find_category_override(const struct app *app, const char *desktop_id)
{
for (size_t i = 0; i < app->category_override_count; i++) {
if (!strcmp(app->category_overrides[i].desktop_id, desktop_id)) {
return (int)i;
}
}
return -1;
}

static void
launcher_save_favorites(const struct app *app)
{
launcher_ensure_config_dir();

char path[PATH_MAX] = { 0 };
launcher_get_path(path, sizeof(path), "launcher-favorites.txt");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "w");
if (!f) {
fprintf(stderr, _("karton-shell: cannot write %s: %s\n"), path, strerror(errno));
return;
}
for (size_t i = 0; i < app->favorite_count; i++) {
fprintf(f, "%s\n", app->favorite_ids[i]);
}
fclose(f);
}

static void
launcher_save_categories(const struct app *app)
{
launcher_ensure_config_dir();

char path[PATH_MAX] = { 0 };
launcher_get_path(path, sizeof(path), "launcher-categories.txt");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "w");
if (!f) {
fprintf(stderr, _("karton-shell: cannot write %s: %s\n"), path, strerror(errno));
return;
}
for (size_t i = 0; i < app->category_override_count; i++) {
fprintf(f, "%s %d\n", app->category_overrides[i].desktop_id,
app->category_overrides[i].category);
}
fclose(f);
}

static void
launcher_load_preferences(struct app *app)
{
app->favorite_count = 0;
app->category_override_count = 0;

char path[PATH_MAX] = { 0 };
launcher_get_path(path, sizeof(path), "launcher-favorites.txt");
if (path[0]) {
FILE *f = fopen(path, "r");
if (f) {
char line[256];
while (fgets(line, sizeof(line), f)) {
char *id = trim_in_place(line);
if (!id[0] || id[0] == '#') {
continue;
}
if (app->favorite_count >= MAX_FAVORITES) {
break;
}
snprintf(app->favorite_ids[app->favorite_count++],
sizeof(app->favorite_ids[0]), "%s", id);
}
fclose(f);
}
}

launcher_get_path(path, sizeof(path), "launcher-categories.txt");
if (path[0]) {
FILE *f = fopen(path, "r");
if (f) {
char line[256];
while (fgets(line, sizeof(line), f)) {
char id[128] = { 0 };
int cat = LCAT_OTHER;
if (sscanf(line, "%127s %d", id, &cat) != 2) {
continue;
}
if (cat < LCAT_INTERNET || cat > LCAT_OTHER) {
continue;
}
if (app->category_override_count >= MAX_CAT_OVERRIDES) {
break;
}
snprintf(app->category_overrides[app->category_override_count].desktop_id,
sizeof(app->category_overrides[0].desktop_id), "%s", id);
app->category_overrides[app->category_override_count].category = cat;
app->category_override_count++;
}
fclose(f);
}
}
}

static void
launcher_rebuild_filtered(struct app *app)
{
app->launcher_filtered_count = 0;

for (size_t i = 0; i < app->launcher_count; i++) {
const struct launcher_entry *entry = &app->launcher_entries[i];

if (app->launcher_category == LCAT_FAVORITES && !entry->favorite) {
continue;
}

if (app->launcher_category >= LCAT_INTERNET && app->launcher_category <= LCAT_OTHER
&& launcher_effective_category(entry) != app->launcher_category) {
continue;
}

if (app->launcher_query[0]
&& !contains_nocase(entry->name, app->launcher_query)
&& !contains_nocase(entry->desktop_id, app->launcher_query)) {
continue;
}

if (app->launcher_filtered_count >= MAX_LAUNCHER_ENTRIES) {
break;
}
app->launcher_filtered[app->launcher_filtered_count++] = i;
}

if (app->launcher_filtered_count == 0) {
app->launcher_selected = -1;
app->launcher_scroll_offset = 0;
return;
}

if (app->launcher_selected < 0 || app->launcher_selected >= (int)app->launcher_filtered_count) {
app->launcher_selected = 0;
}

launcher_clamp_scroll_offset(app, &app->side);
launcher_ensure_selected_visible(app, &app->side);
}

static void
launcher_set_favorite(struct app *app, const char *desktop_id, bool enabled)
{
int idx = launcher_find_favorite(app, desktop_id);
if (enabled && idx < 0) {
if (app->favorite_count < MAX_FAVORITES) {
snprintf(app->favorite_ids[app->favorite_count++], sizeof(app->favorite_ids[0]), "%s",
desktop_id);
}
} else if (!enabled && idx >= 0) {
for (size_t i = (size_t)idx; i + 1 < app->favorite_count; i++) {
memmove(app->favorite_ids[i], app->favorite_ids[i + 1],
sizeof(app->favorite_ids[i]));
}
app->favorite_count--;
}

for (size_t i = 0; i < app->launcher_count; i++) {
if (!strcmp(app->launcher_entries[i].desktop_id, desktop_id)) {
app->launcher_entries[i].favorite = enabled;
}
}
launcher_save_favorites(app);
launcher_rebuild_filtered(app);
}

static void
launcher_set_category_override(struct app *app, const char *desktop_id, int category)
{
if (category < LCAT_INTERNET || category > LCAT_OTHER) {
return;
}

int idx = launcher_find_category_override(app, desktop_id);
if (idx < 0) {
if (app->category_override_count >= MAX_CAT_OVERRIDES) {
return;
}
idx = (int)app->category_override_count++;
snprintf(app->category_overrides[idx].desktop_id,
sizeof(app->category_overrides[idx].desktop_id), "%s", desktop_id);
}
app->category_overrides[idx].category = category;

for (size_t i = 0; i < app->launcher_count; i++) {
if (!strcmp(app->launcher_entries[i].desktop_id, desktop_id)) {
app->launcher_entries[i].category_override = category;
}
}

launcher_save_categories(app);
launcher_rebuild_filtered(app);
}

static int
cmp_launcher_entries(const void *a, const void *b)
{
const struct launcher_entry *ea = a;
const struct launcher_entry *eb = b;
return strcasecmp(ea->name, eb->name);
}

static bool
launcher_has_id(const struct app *app, const char *desktop_id)
{
for (size_t i = 0; i < app->launcher_count; i++) {
if (!strcmp(app->launcher_entries[i].desktop_id, desktop_id)) {
return true;
}
}
return false;
}

static bool
parse_desktop_file(const char *path,
char *name_out, size_t name_size,
char *categories_out, size_t categories_size,
char *icon_out, size_t icon_size)
{
FILE *f = fopen(path, "r");
if (!f) {
return false;
}

bool in_desktop_entry = false;
bool type_app = false;
bool no_display = false;
bool hidden = false;
name_out[0] = '\0';
if (categories_out && categories_size > 0) {
categories_out[0] = '\0';
}
if (icon_out && icon_size > 0) {
icon_out[0] = '\0';
}

char line[512];
while (fgets(line, sizeof(line), f)) {
char *s = trim_in_place(line);
if (!s[0]) {
continue;
}

if (s[0] == '[') {
if (!strcmp(s, "[Desktop Entry]")) {
in_desktop_entry = true;
continue;
}
if (in_desktop_entry) {
break;
}
continue;
}

if (!in_desktop_entry) {
continue;
}

if (!strncmp(s, "Type=", 5) && !strcmp(s + 5, "Application")) {
type_app = true;
continue;
}
if (!strncmp(s, "NoDisplay=", 10) && !strcasecmp(s + 10, "true")) {
no_display = true;
continue;
}
if (!strncmp(s, "Hidden=", 7) && !strcasecmp(s + 7, "true")) {
hidden = true;
continue;
}
if (!strncmp(s, "Name=", 5) && !name_out[0]) {
snprintf(name_out, name_size, "%s", s + 5);
continue;
}
if (categories_out && categories_size > 0
&& !strncmp(s, "Categories=", 11) && !categories_out[0]) {
snprintf(categories_out, categories_size, "%s", s + 11);
continue;
}
if (icon_out && icon_size > 0
&& !strncmp(s, "Icon=", 5) && !icon_out[0]) {
snprintf(icon_out, icon_size, "%s", s + 5);
}
}

fclose(f);
return in_desktop_entry && type_app && !no_display && !hidden && name_out[0];
}

static void
add_launcher_entries_from_dir(struct app *app, const char *dir_path)
{
DIR *dir = opendir(dir_path);
if (!dir) {
return;
}

struct dirent *ent = NULL;
while ((ent = readdir(dir)) != NULL) {
if (!str_ends_with(ent->d_name, ".desktop")) {
continue;
}
if (app->launcher_count >= MAX_LAUNCHER_ENTRIES) {
break;
}

char desktop_id[128] = { 0 };
size_t desktop_name_len = strlen(ent->d_name);
if (desktop_name_len >= sizeof(desktop_id)) {
continue;
}
memcpy(desktop_id, ent->d_name, desktop_name_len + 1);
char *dot = strrchr(desktop_id, '.');
if (dot) {
*dot = '\0';
}
if (!desktop_id[0] || launcher_has_id(app, desktop_id)) {
continue;
}

char path[PATH_MAX] = { 0 };
snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);

char name[128] = { 0 };
char categories[256] = { 0 };
char icon_name[128] = { 0 };
if (!parse_desktop_file(path, name, sizeof(name), categories, sizeof(categories),
icon_name, sizeof(icon_name))) {
continue;
}

struct launcher_entry *dst = &app->launcher_entries[app->launcher_count++];
snprintf(dst->name, sizeof(dst->name), "%s", name);
snprintf(dst->desktop_id, sizeof(dst->desktop_id), "%s", desktop_id);
snprintf(dst->desktop_path, sizeof(dst->desktop_path), "%s", path);
dst->local_entry = strstr(path, "/.local/share/applications/") != NULL;
snprintf(dst->icon_name, sizeof(dst->icon_name), "%s", icon_name);
dst->favorite = launcher_find_favorite(app, desktop_id) >= 0;
dst->category = launcher_default_category(categories);
int override_idx = launcher_find_category_override(app, desktop_id);
dst->category_override = override_idx >= 0
? app->category_overrides[override_idx].category : -1;
}

closedir(dir);
}

static void
load_launcher_entries(struct app *app)
{
launcher_load_preferences(app);
app->launcher_count = 0;

char dir_local[PATH_MAX] = { 0 };
char dir_xdg[PATH_MAX] = { 0 };
const char *home = getenv("HOME");
const char *xdg_data = getenv("XDG_DATA_HOME");

if (xdg_data && *xdg_data) {
snprintf(dir_xdg, sizeof(dir_xdg), "%s/applications", xdg_data);
add_launcher_entries_from_dir(app, dir_xdg);
}
if (home && *home) {
snprintf(dir_local, sizeof(dir_local), "%s/.local/share/applications", home);
add_launcher_entries_from_dir(app, dir_local);
}
add_launcher_entries_from_dir(app, "/usr/local/share/applications");
add_launcher_entries_from_dir(app, "/usr/share/applications");

if (app->launcher_count > 1) {
qsort(app->launcher_entries, app->launcher_count,
sizeof(app->launcher_entries[0]), cmp_launcher_entries);
}

launcher_rebuild_filtered(app);
}

static int
launcher_item_hit(const struct panel *panel, const struct app *app, double px, double py)
{
double x, y, w, h, search_y, chips_y, list_y, row_h;
int max_visible = 0;
if (!launcher_geometry(panel, &x, &y, &w, &h) || !point_in_rect(px, py, x, y, w, h)) {
return -1;
}

launcher_layout(panel, &x, &y, &w, &h, &search_y, &chips_y, &list_y, &row_h, &max_visible);
double apps_x, apps_y, apps_w, apps_h;
launcher_apps_panel_rect(panel, &apps_x, &apps_y, &apps_w, &apps_h);
if (py < list_y) {
return -1;
}
if (!point_in_rect(px, py, apps_x, list_y, apps_w, row_h * (double)max_visible)) {
return -1;
}

int idx = (int)((py - list_y) / row_h);
if (idx < 0 || idx >= max_visible) {
return -1;
}

idx += app->launcher_scroll_offset;
if (idx < 0 || idx >= (int)app->launcher_filtered_count) {
return -1;
}
return idx;
}

static int
launcher_category_hit(const struct panel *panel, double px, double py)
{
double x, y, w, h;
if (!launcher_geometry(panel, &x, &y, &w, &h)) {
return -1;
}

for (int cat = 0; cat < LCAT_COUNT; cat++) {
double cx, cy, cw, ch;
if (launcher_category_rect(panel, cat, &cx, &cy, &cw, &ch)
&& point_in_rect(px, py, cx, cy, cw, ch)) {
return cat;
}
}

return -1;
}

static bool
launcher_search_hit(const struct panel *panel, double px, double py)
{
double x, y, w, h, search_y, chips_y, list_y, row_h;
int max_visible = 0;
if (!launcher_geometry(panel, &x, &y, &w, &h)) {
return false;
}

launcher_layout(panel, &x, &y, &w, &h, &search_y, &chips_y, &list_y, &row_h, &max_visible);
double content_x, content_y, content_w, content_h;
launcher_content_rect(panel, &content_x, &content_y, &content_w, &content_h);
return point_in_rect(px, py, content_x, search_y, content_w, 38.0);
}

static int
launcher_menu_item_hit(const struct app *app, double px, double py)
{
if (!app->launcher_menu_open) {
return -1;
}

const double menu_w = 230.0;
const double row_h = 28.0;
const int item_count = 8;

if (!point_in_rect(px, py, app->launcher_menu_x, app->launcher_menu_y,
menu_w, item_count * row_h + 8.0)) {
return -1;
}

int idx = (int)((py - app->launcher_menu_y - 4.0) / row_h);
if (idx < 0 || idx >= item_count) {
return -1;
}
return idx;
}

static const char *
launcher_menu_favorite_label(const struct app *app)
{
if (!app || app->launcher_menu_target < 0
|| app->launcher_menu_target >= (int)app->launcher_filtered_count) {
return _("Add to favorites");
}

size_t entry_idx = app->launcher_filtered[app->launcher_menu_target];
if (entry_idx >= app->launcher_count) {
return _("Add to favorites");
}

return app->launcher_entries[entry_idx].favorite
? _("Remove from favorites")
: _("Add to favorites");
}

static void
print_usage(const char *argv0)
{
fprintf(stderr,
_("Usage: %s [--top-only|--side-only]\n"
"  --top-only   Run only the top panel\n"
"  --side-only  Run only the side dock\n"),
argv0);
}

static bool
parse_args(int argc, char **argv, enum run_mode *mode)
{
*mode = RUN_BOTH;
for (int i = 1; i < argc; i++) {
if (!strcmp(argv[i], "--top-only")) {
if (*mode == RUN_SIDE_ONLY) {
return false;
}
*mode = RUN_TOP_ONLY;
continue;
}
if (!strcmp(argv[i], "--side-only")) {
if (*mode == RUN_TOP_ONLY) {
return false;
}
*mode = RUN_SIDE_ONLY;
continue;
}
if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
print_usage(argv[0]);
exit(0);
}
return false;
}
return true;
}

static void
set_source_hex_a(cairo_t *cairo, uint32_t hex, double alpha)
{
double r = ((hex >> 16) & 0xff) / 255.0;
double g = ((hex >> 8) & 0xff) / 255.0;
double b = (hex & 0xff) / 255.0;
cairo_set_source_rgba(cairo, r, g, b, alpha);
}

static void
set_source_hex(cairo_t *cairo, uint32_t hex)
{
set_source_hex_a(cairo, hex, 1.0);
}

static void
rounded_rect(cairo_t *cairo, double x, double y, double w, double h, double r)
{
const double pi = 3.14159265358979323846;
double radius = r;
if (radius < 0.0) {
radius = 0.0;
}
if (radius > w * 0.5) {
radius = w * 0.5;
}
if (radius > h * 0.5) {
radius = h * 0.5;
}

cairo_new_sub_path(cairo);
cairo_arc(cairo, x + w - radius, y + radius, radius, -pi / 2.0, 0.0);
cairo_arc(cairo, x + w - radius, y + h - radius, radius, 0.0, pi / 2.0);
cairo_arc(cairo, x + radius, y + h - radius, radius, pi / 2.0, pi);
cairo_arc(cairo, x + radius, y + radius, radius, pi, 3.0 * pi / 2.0);
cairo_close_path(cairo);
}

static void
draw_pango_text(cairo_t *cairo, const char *family, PangoWeight weight,
double size, uint32_t color, double alpha,
double x, double y, int max_width,
PangoAlignment alignment, const char *text)
{
if (!text || !*text) {
return;
}

PangoLayout *layout = pango_cairo_create_layout(cairo);
PangoFontDescription *font = pango_font_description_new();

pango_font_description_set_family(font, family);
pango_font_description_set_weight(font, weight);
pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
pango_layout_set_font_description(layout, font);
pango_layout_set_text(layout, text, -1);

if (max_width > 0) {
pango_layout_set_width(layout, max_width * PANGO_SCALE);
pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
pango_layout_set_alignment(layout, alignment);
}

set_source_hex_a(cairo, color, alpha);
cairo_move_to(cairo, x, y);
pango_cairo_show_layout(cairo, layout);

g_object_unref(layout);
pango_font_description_free(font);
}

static void
measure_pango_text(cairo_t *cairo, const char *family, PangoWeight weight,
double size, const char *text, int *width, int *height)
{
if (width) {
*width = 0;
}
if (height) {
*height = 0;
}
if (!text || !*text) {
return;
}

PangoLayout *layout = pango_cairo_create_layout(cairo);
PangoFontDescription *font = pango_font_description_new();

pango_font_description_set_family(font, family);
pango_font_description_set_weight(font, weight);
pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
pango_layout_set_font_description(layout, font);
pango_layout_set_text(layout, text, -1);
pango_layout_get_pixel_size(layout, width, height);

g_object_unref(layout);
pango_font_description_free(font);
}

static void
draw_wifi_icon(cairo_t *cairo, double cx, double cy, double size,
uint32_t color, double alpha)
{
const double pi = 3.14159265358979323846;
cairo_save(cairo);
set_source_hex_a(cairo, color, alpha);
cairo_set_line_width(cairo, size * 0.11);
cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
cairo_new_path(cairo);

for (int i = 0; i < 3; i++) {
double r = size * (0.18 + i * 0.16);
cairo_arc(cairo, cx, cy + size * 0.12, r, pi * 1.18, pi * 1.82);
cairo_stroke(cairo);
}

cairo_arc(cairo, cx, cy + size * 0.36, size * 0.06, 0, 2.0 * pi);
cairo_fill(cairo);
cairo_restore(cairo);
}

static void
draw_speaker_icon(cairo_t *cairo, double cx, double cy, double size,
uint32_t color, double alpha)
{
const double pi = 3.14159265358979323846;
cairo_save(cairo);
set_source_hex_a(cairo, color, alpha);

double w = size * 0.46;
double h = size * 0.40;
double x = cx - w * 0.55;
double y = cy - h * 0.5;

cairo_move_to(cairo, x, y + h * 0.28);
cairo_line_to(cairo, x + w * 0.28, y + h * 0.28);
cairo_line_to(cairo, x + w * 0.52, y);
cairo_line_to(cairo, x + w * 0.52, y + h);
cairo_line_to(cairo, x + w * 0.28, y + h * 0.72);
cairo_line_to(cairo, x, y + h * 0.72);
cairo_close_path(cairo);
cairo_fill(cairo);

cairo_set_line_width(cairo, size * 0.10);
cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
cairo_new_path(cairo);
cairo_arc(cairo, cx + size * 0.06, cy, size * 0.20, -pi / 3.2, pi / 3.2);
cairo_stroke(cairo);
cairo_new_path(cairo);
cairo_arc(cairo, cx + size * 0.10, cy, size * 0.30, -pi / 3.2, pi / 3.2);
cairo_stroke(cairo);

cairo_restore(cairo);
}

static void
draw_battery_icon(cairo_t *cairo, double x, double y, double w, double h,
double level, uint32_t color, double alpha)
{
cairo_save(cairo);
set_source_hex_a(cairo, color, alpha * 0.26);

double cap_w = w * 0.10;
double body_w = w - cap_w - 1.0;
rounded_rect(cairo, x, y, body_w, h, h * 0.23);
cairo_fill(cairo);

cairo_new_path(cairo);
set_source_hex_a(cairo, color, alpha * 0.90);
cairo_rectangle(cairo, x + body_w + 1.0, y + h * 0.27, cap_w, h * 0.46);
cairo_fill(cairo);

if (level < 0.0) {
level = 0.0;
}
if (level > 1.0) {
level = 1.0;
}
double fill_w = (body_w - 4.0) * level;
if (fill_w > 0.0) {
set_source_hex_a(cairo, color, alpha);
rounded_rect(cairo, x + 2.0, y + 2.0, fill_w, h - 4.0, (h - 4.0) * 0.23);
cairo_fill(cairo);
}

cairo_restore(cairo);
}

static void
draw_top_status_icons(cairo_t *cairo, struct app *app, double x, double y, uint32_t color, bool dark)
{
(void)color;
bool has_unread_notification = app
&& app->notification_count > 0
&& !app->notifications_read
&& !app->notifications_cleared;
const double cy = y + 8.0;
const double centers[] = { x + 8.0, x + 30.0, x + 52.0, x + 72.0 };
const uint32_t colors[] = {
has_unread_notification ? 0xff5f87 : 0x9aa8bd,
app && app->quick_wifi_enabled ? 0x31d0aa : 0x8aa0bf,
0x7b66f0,
0x74d66f,
};

for (size_t i = 0; i < G_N_ELEMENTS(centers); i++) {
set_source_hex_a(cairo, colors[i], dark ? 0.14 : 0.18);
cairo_arc(cairo, centers[i], cy, 8.8, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
}

draw_karton_symbol(cairo, 3, centers[0], cy, 14.5, colors[0], dark ? 1.0 : 0.96);
if (has_unread_notification) {
    set_source_hex_a(cairo, dark ? 0x172031 : 0xffffff, dark ? 0.64 : 0.92);
    cairo_arc(cairo, centers[0] + 7.0, cy - 8.0, 4.8, 0, 2.0 * 3.14159265358979323846);
    cairo_fill(cairo);
set_source_hex_a(cairo, 0xf05d7b, 0.96);
cairo_arc(cairo, centers[0] + 7.0, cy - 8.0, 3.4, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
}
draw_wifi_icon(cairo, centers[1], cy, 15.0, colors[1], dark ? 1.0 : 0.96);
draw_speaker_icon(cairo, centers[2], cy, 15.0, colors[2], dark ? 1.0 : 0.96);
draw_battery_icon(cairo, centers[3] - 5.0, cy - 6.0, 18.0, 12.0, 0.72, colors[3], dark ? 1.0 : 0.96);
}

static uint32_t
karton_symbol_color(int type)
{
switch (type) {
case 0:
return 0x31d0aa;
case 1:
return 0x4aa3ff;
case 2:
return 0xffb84d;
case 3:
return 0xff5f87;
case 4:
return 0x62d0ff;
case 5:
return 0xff6b5f;
case 6:
return 0x7b66f0;
case 7:
return 0x9b7cff;
default:
return 0x7b66f0;
}
}

static void
draw_quick_header_icon(cairo_t *cairo, int type, double cx, double cy,
uint32_t color, double alpha)
{
const double pi = 3.14159265358979323846;
cairo_save(cairo);
set_source_hex_a(cairo, color, alpha);

if (type == 0) {
cairo_set_line_width(cairo, 1.8);
cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
cairo_new_path(cairo);
cairo_arc(cairo, cx, cy - 2.0, 4.0, pi * 1.05, pi * 1.95);
cairo_stroke(cairo);
cairo_new_path(cairo);
rounded_rect(cairo, cx - 4.5, cy - 0.5, 9.0, 7.0, 2.0);
cairo_fill(cairo);
} else if (type == 1) {
cairo_set_line_width(cairo, 1.6);
cairo_new_path(cairo);
static const double spokes[8][4] = {
{ 0.0, -4.2, 0.0, -6.4 },
{ 2.9, -2.9, 4.5, -4.5 },
{ 4.2, 0.0, 6.4, 0.0 },
{ 2.9, 2.9, 4.5, 4.5 },
{ 0.0, 4.2, 0.0, 6.4 },
{ -2.9, 2.9, -4.5, 4.5 },
{ -4.2, 0.0, -6.4, 0.0 },
{ -2.9, -2.9, -4.5, -4.5 },
};
for (size_t i = 0; i < 8; i++) {
cairo_move_to(cairo, cx + spokes[i][0], cy + spokes[i][1]);
cairo_line_to(cairo, cx + spokes[i][2], cy + spokes[i][3]);
}
cairo_stroke(cairo);
cairo_new_path(cairo);
cairo_arc(cairo, cx, cy, 3.0, 0, 2.0 * pi);
cairo_fill(cairo);
} else {
cairo_set_line_width(cairo, 1.9);
cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
cairo_new_path(cairo);
cairo_arc(cairo, cx, cy, 5.1, pi * 0.25, pi * 1.75);
cairo_stroke(cairo);
cairo_new_path(cairo);
cairo_move_to(cairo, cx, cy - 6.4);
cairo_line_to(cairo, cx, cy - 0.8);
cairo_stroke(cairo);
}

cairo_restore(cairo);
}

static void
draw_karton_symbol(cairo_t *cairo, int type, double cx, double cy,
double size, uint32_t color, double alpha)
{
const double pi = 3.14159265358979323846;
double s = size / 24.0;
cairo_save(cairo);
set_source_hex_a(cairo, color, alpha);
cairo_set_line_width(cairo, 2.0 * s);
cairo_set_line_cap(cairo, CAIRO_LINE_CAP_ROUND);
cairo_set_line_join(cairo, CAIRO_LINE_JOIN_ROUND);

if (type == 0) { /* network */
cairo_arc(cairo, cx, cy + 2.0 * s, 8.0 * s, pi * 1.18, pi * 1.82);
cairo_stroke(cairo);
cairo_arc(cairo, cx, cy + 4.0 * s, 4.5 * s, pi * 1.20, pi * 1.80);
cairo_stroke(cairo);
cairo_arc(cairo, cx, cy + 8.0 * s, 1.7 * s, 0, 2.0 * pi);
cairo_fill(cairo);
} else if (type == 1) { /* bluetooth */
cairo_move_to(cairo, cx - 1.0 * s, cy - 10.0 * s);
cairo_line_to(cairo, cx + 7.0 * s, cy - 3.0 * s);
cairo_line_to(cairo, cx - 1.0 * s, cy + 3.0 * s);
cairo_line_to(cairo, cx + 7.0 * s, cy + 10.0 * s);
cairo_line_to(cairo, cx - 1.0 * s, cy + 3.0 * s);
cairo_line_to(cairo, cx - 1.0 * s, cy - 10.0 * s);
cairo_move_to(cairo, cx - 8.0 * s, cy - 6.0 * s);
cairo_line_to(cairo, cx - 1.0 * s, cy + 1.0 * s);
cairo_move_to(cairo, cx - 8.0 * s, cy + 6.0 * s);
cairo_line_to(cairo, cx - 1.0 * s, cy - 1.0 * s);
cairo_stroke(cairo);
} else if (type == 2) { /* theme */
cairo_arc(cairo, cx, cy, 7.5 * s, 0, 2.0 * pi);
cairo_stroke(cairo);
static const double rays[8][4] = {
{ 0.0, -10.0, 0.0, -12.5 },
{ 7.1, -7.1, 8.8, -8.8 },
{ 10.0, 0.0, 12.5, 0.0 },
{ 7.1, 7.1, 8.8, 8.8 },
{ 0.0, 10.0, 0.0, 12.5 },
{ -7.1, 7.1, -8.8, 8.8 },
{ -10.0, 0.0, -12.5, 0.0 },
{ -7.1, -7.1, -8.8, -8.8 },
};
for (size_t i = 0; i < 8; i++) {
cairo_move_to(cairo, cx + rays[i][0] * s, cy + rays[i][1] * s);
cairo_line_to(cairo, cx + rays[i][2] * s, cy + rays[i][3] * s);
}
cairo_stroke(cairo);
cairo_arc(cairo, cx, cy, 3.2 * s, 0, 2.0 * pi);
cairo_fill(cairo);
} else if (type == 3) { /* bell */
cairo_move_to(cairo, cx - 8.0 * s, cy + 6.0 * s);
cairo_curve_to(cairo, cx - 5.0 * s, cy + 3.0 * s, cx - 5.0 * s, cy - 8.0 * s, cx, cy - 8.0 * s);
cairo_curve_to(cairo, cx + 5.0 * s, cy - 8.0 * s, cx + 5.0 * s, cy + 3.0 * s, cx + 8.0 * s, cy + 6.0 * s);
cairo_close_path(cairo);
cairo_fill(cairo);
cairo_arc(cairo, cx, cy + 8.0 * s, 2.0 * s, 0, pi);
cairo_fill(cairo);
} else if (type == 4) { /* lock */
rounded_rect(cairo, cx - 8.0 * s, cy - 1.0 * s, 16.0 * s, 12.0 * s, 4.0 * s);
cairo_fill(cairo);
cairo_new_path(cairo);
cairo_arc(cairo, cx, cy - 1.0 * s, 6.0 * s, pi, 2.0 * pi);
cairo_stroke(cairo);
} else if (type == 5) { /* power */
cairo_move_to(cairo, cx, cy - 9.0 * s);
cairo_line_to(cairo, cx, cy - 1.0 * s);
cairo_stroke(cairo);
cairo_arc(cairo, cx, cy + 1.0 * s, 8.0 * s, pi * 0.25, pi * 1.75);
cairo_stroke(cairo);
} else if (type == 6) { /* launcher */
for (int row = 0; row < 2; row++) {
for (int col = 0; col < 2; col++) {
rounded_rect(cairo, cx - 8.0 * s + col * 10.0 * s, cy - 8.0 * s + row * 10.0 * s,
6.0 * s, 6.0 * s, 1.8 * s);
cairo_fill(cairo);
}
}
} else if (type == 7) { /* generic app */
rounded_rect(cairo, cx - 9.0 * s, cy - 9.0 * s, 18.0 * s, 18.0 * s, 5.0 * s);
cairo_stroke(cairo);
rounded_rect(cairo, cx - 5.0 * s, cy - 5.0 * s, 10.0 * s, 10.0 * s, 3.0 * s);
cairo_fill(cairo);
}

cairo_restore(cairo);
}

static int
find_group(const struct app *app, const char *app_id)
{
for (size_t i = 0; i < app->group_count; i++) {
if (!strcmp(app->groups[i].app_id, app_id)) {
return (int)i;
}
}
return -1;
}

static void
rebuild_groups(struct app *app)
{
app->group_count = 0;
for (size_t i = 0; i < MAX_TOPLEVELS; i++) {
struct toplevel_entry *entry = &app->toplevels[i];
if (!entry->used || !entry->handle) {
continue;
}

const char *app_id = entry->app_id[0] ? entry->app_id : "app";
int group_idx = find_group(app, app_id);
if (group_idx < 0) {
if (app->group_count >= MAX_GROUPS) {
continue;
}
group_idx = (int)app->group_count++;
struct app_group *group = &app->groups[group_idx];
memset(group, 0, sizeof(*group));
snprintf(group->app_id, sizeof(group->app_id), "%s", app_id);
}

struct app_group *group = &app->groups[group_idx];
if (group->count < MAX_GROUP_ITEMS) {
group->indices[group->count++] = i;
}
if (entry->active && !entry->minimized) {
group->any_active = true;
}
}

if (app->popup_group >= (int)app->group_count) {
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
}
if (app->hovered_group >= (int)app->group_count) {
app->hovered_group = -1;
}
popup_clamp_selection(app);
}

static const char *
app_id_base(const char *app_id)
{
if (!app_id || !*app_id) {
return "app";
}
const char *dot = strrchr(app_id, '.');
if (dot && *(dot + 1)) {
return dot + 1;
}
return app_id;
}

static const struct toplevel_entry *
active_toplevel_entry(const struct app *app)
{
for (size_t i = 0; i < MAX_TOPLEVELS; i++) {
const struct toplevel_entry *entry = &app->toplevels[i];
if (!entry->used || !entry->handle) {
continue;
}
if (entry->active && !entry->minimized) {
return entry;
}
}
return NULL;
}

static bool
active_toplevel_is_fullscreen(const struct app *app)
{
const struct toplevel_entry *active = active_toplevel_entry(app);
return active && active->fullscreen && !active->minimized;
}

static void
global_menu_clear(struct app *app)
{
app->global_menu_available = false;
app->global_menu_open = false;
app->global_menu_open_top = -1;
app->global_menu_bus[0] = '\0';
app->global_menu_path[0] = '\0';
app->global_menu_actions_path[0] = '\0';
app->global_menu_top_count = 0;
for (size_t i = 0; i < MAX_GLOBAL_MENU_TOP; i++) {
app->global_menu_tops[i].label[0] = '\0';
app->global_menu_tops[i].item_count = 0;
}
}

static bool
global_menu_xml_has_interface(const char *xml, const char *iface)
{
if (!xml || !iface || !*iface) {
return false;
}

char needle[192] = { 0 };
snprintf(needle, sizeof(needle), "interface name=\"%s\"", iface);
return strstr(xml, needle) != NULL;
}

static size_t
global_menu_xml_collect_child_nodes(const char *xml, char children[][96], size_t max_children)
{
if (!xml || !children || max_children == 0) {
return 0;
}

size_t count = 0;
const char *p = xml;
const char *tag = "<node name=\"";
size_t tag_len = strlen(tag);

while ((p = strstr(p, tag)) != NULL && count < max_children) {
p += tag_len;
const char *end = strchr(p, '\"');
if (!end) {
break;
}

size_t len = (size_t)(end - p);
if (len > 0 && len < 96) {
snprintf(children[count], 96, "%.*s", (int)len, p);
count++;
}

p = end + 1;
}

return count;
}

static void
global_menu_discover_paths_recursive(GDBusConnection *conn, const char *bus,
const char *path, int depth, int *budget,
char *menu_path, size_t menu_path_size,
char *actions_path, size_t actions_path_size)
{
if (!conn || !bus || !path || !menu_path || !actions_path || !budget || *budget <= 0) {
return;
}

(*budget)--;

GError *error = NULL;
GVariant *reply = g_dbus_connection_call_sync(
conn,
bus,
path,
"org.freedesktop.DBus.Introspectable",
"Introspect",
NULL,
G_VARIANT_TYPE("(s)"),
G_DBUS_CALL_FLAGS_NONE,
120,
NULL,
&error);

if (!reply) {
g_clear_error(&error);
return;
}

const char *xml = NULL;
g_variant_get(reply, "(&s)", &xml);

if (!menu_path[0] && global_menu_xml_has_interface(xml, "org.gtk.Menus")) {
snprintf(menu_path, menu_path_size, "%s", path);
}
if (!actions_path[0] && global_menu_xml_has_interface(xml, "org.gtk.Actions")) {
snprintf(actions_path, actions_path_size, "%s", path);
}

if ((menu_path[0] && actions_path[0]) || depth <= 0 || *budget <= 0) {
g_variant_unref(reply);
return;
}

char children[24][96] = { { 0 } };
size_t child_count = global_menu_xml_collect_child_nodes(xml, children, G_N_ELEMENTS(children));
g_variant_unref(reply);

for (size_t i = 0; i < child_count; i++) {
char child_path[384] = { 0 };
if (!strcmp(path, "/")) {
snprintf(child_path, sizeof(child_path), "/%s", children[i]);
} else {
snprintf(child_path, sizeof(child_path), "%s/%s", path, children[i]);
}

global_menu_discover_paths_recursive(conn, bus, child_path, depth - 1, budget,
menu_path, menu_path_size, actions_path, actions_path_size);
if (menu_path[0] && actions_path[0]) {
break;
}
}
}

static bool
global_menu_add_action_item(struct global_menu_top *dst_top,
const char *label, const char *action,
const char *target, bool enabled)
{
if (!dst_top || !label || !*label) {
return false;
}
if (dst_top->item_count >= MAX_GLOBAL_MENU_ITEMS) {
return false;
}

struct global_menu_item *item = &dst_top->items[dst_top->item_count++];
memset(item, 0, sizeof(*item));
snprintf(item->label, sizeof(item->label), "%s", label);
if (action) {
snprintf(item->action, sizeof(item->action), "%s", action);
}
if (target) {
snprintf(item->target, sizeof(item->target), "%s", target);
}
item->enabled = enabled;
return true;
}

static void
global_menu_collect_items_from_model(GMenuModel *model,
struct global_menu_top *dst_top,
int depth)
{
if (!model || !dst_top || depth > 4 || dst_top->item_count >= MAX_GLOBAL_MENU_ITEMS) {
return;
}

int n_items = g_menu_model_get_n_items(model);
for (int i = 0; i < n_items; i++) {
if (dst_top->item_count >= MAX_GLOBAL_MENU_ITEMS) {
break;
}

GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
if (section) {
global_menu_collect_items_from_model(section, dst_top, depth + 1);
g_object_unref(section);
continue;
}

GMenuModel *submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
if (submenu) {
global_menu_collect_items_from_model(submenu, dst_top, depth + 1);
g_object_unref(submenu);
continue;
}

char *label = NULL;
if (!g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label)
|| !label || !label[0]) {
g_free(label);
continue;
}

char *action = NULL;
g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &action);

char *target_text = NULL;
GVariant *target = g_menu_model_get_item_attribute_value(model, i, G_MENU_ATTRIBUTE_TARGET, NULL);
if (target) {
target_text = g_variant_print(target, TRUE);
g_variant_unref(target);
}

bool enabled = true;
if (action && !*action) {
enabled = false;
}

global_menu_add_action_item(dst_top, label, action, target_text, enabled);
g_free(target_text);
g_free(action);
g_free(label);
}
}

static bool
global_menu_name_has_owner(GDBusConnection *conn, const char *name)
{
if (!conn || !name || !*name) {
return false;
}

GError *error = NULL;
GVariant *reply = g_dbus_connection_call_sync(
conn,
"org.freedesktop.DBus",
"/org/freedesktop/DBus",
"org.freedesktop.DBus",
"NameHasOwner",
g_variant_new("(s)", name),
G_VARIANT_TYPE("(b)"),
G_DBUS_CALL_FLAGS_NONE,
350,
NULL,
&error);

if (!reply) {
g_clear_error(&error);
return false;
}

gboolean has_owner = FALSE;
g_variant_get(reply, "(b)", &has_owner);
g_variant_unref(reply);
return has_owner;
}

static bool
global_menu_name_matches_app(const char *bus_name, const char *app_id)
{
if (!bus_name || !*bus_name || !app_id || !*app_id) {
return false;
}

char *bus_l = g_ascii_strdown(bus_name, -1);
char *app_l = g_ascii_strdown(app_id, -1);
char *base_l = g_ascii_strdown(app_id_base(app_id), -1);

bool match = false;
if (strstr(bus_l, app_l) || (base_l[0] && strstr(bus_l, base_l))) {
match = true;
}

g_free(base_l);
g_free(app_l);
g_free(bus_l);
return match;
}

static bool
global_menu_try_bus(struct app *app, GDBusConnection *conn, const char *bus_name)
{
if (!app || !conn || !bus_name || !*bus_name) {
return false;
}

char menu_path[256] = { 0 };
char actions_path[256] = { 0 };
int budget = 18;
global_menu_discover_paths_recursive(conn, bus_name, "/", 4, &budget,
menu_path, sizeof(menu_path), actions_path, sizeof(actions_path));

if (!menu_path[0] || !actions_path[0]) {
return false;
}

GMenuModel *root = G_MENU_MODEL(g_dbus_menu_model_get(conn, bus_name, menu_path));
if (!root) {
return false;
}

size_t top_count = 0;
int n_items = g_menu_model_get_n_items(root);
for (int i = 0; i < n_items && top_count < MAX_GLOBAL_MENU_TOP; i++) {
char *top_label = NULL;
g_menu_model_get_item_attribute(root, i, G_MENU_ATTRIBUTE_LABEL, "s", &top_label);

struct global_menu_top *top = &app->global_menu_tops[top_count];
memset(top, 0, sizeof(*top));
if (top_label && top_label[0]) {
snprintf(top->label, sizeof(top->label), "%s", top_label);
} else {
snprintf(top->label, sizeof(top->label), "%s", _("Menu"));
}

GMenuModel *submenu = g_menu_model_get_item_link(root, i, G_MENU_LINK_SUBMENU);
if (submenu) {
global_menu_collect_items_from_model(submenu, top, 0);
g_object_unref(submenu);
} else {
char *action = NULL;
g_menu_model_get_item_attribute(root, i, G_MENU_ATTRIBUTE_ACTION, "s", &action);

char *target_text = NULL;
GVariant *target = g_menu_model_get_item_attribute_value(root, i, G_MENU_ATTRIBUTE_TARGET, NULL);
if (target) {
target_text = g_variant_print(target, TRUE);
g_variant_unref(target);
}

global_menu_add_action_item(top,
top->label[0] ? top->label : _("Menu"),
action,
target_text,
action && action[0]);

g_free(target_text);
g_free(action);
}

g_free(top_label);
if (top->item_count > 0) {
top_count++;
}
}

g_object_unref(root);
if (top_count == 0) {
return false;
}

snprintf(app->global_menu_bus, sizeof(app->global_menu_bus), "%s", bus_name);
snprintf(app->global_menu_path, sizeof(app->global_menu_path), "%s", menu_path);
snprintf(app->global_menu_actions_path, sizeof(app->global_menu_actions_path), "%s", actions_path);
app->global_menu_top_count = top_count;
app->global_menu_available = true;
return true;
}

static void
global_menu_build_for_active_app(struct app *app)
{
global_menu_clear(app);

if (!app->active_app_id[0]) {
return;
}

GError *error = NULL;
GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
if (!conn) {
g_clear_error(&error);
return;
}

if (g_dbus_is_name(app->active_app_id)
&& global_menu_name_has_owner(conn, app->active_app_id)
&& global_menu_try_bus(app, conn, app->active_app_id)) {
g_object_unref(conn);
return;
}

GVariant *reply = g_dbus_connection_call_sync(
conn,
"org.freedesktop.DBus",
"/org/freedesktop/DBus",
"org.freedesktop.DBus",
"ListNames",
NULL,
G_VARIANT_TYPE("(as)"),
G_DBUS_CALL_FLAGS_NONE,
160,
NULL,
&error);

if (!reply) {
g_clear_error(&error);
g_object_unref(conn);
return;
}

gchar **names = NULL;
g_variant_get(reply, "(^as)", &names);
g_variant_unref(reply);

if (names) {
for (int i = 0; names[i]; i++) {
const char *name = names[i];
if (!name || !*name || name[0] == ':') {
continue;
}
if (!global_menu_name_matches_app(name, app->active_app_id)) {
continue;
}
if (global_menu_try_bus(app, conn, name)) {
break;
}
}
g_strfreev(names);
}

g_object_unref(conn);
}

static void
global_menu_sync_active_window(struct app *app)
{
char new_app_id[96] = { 0 };
char new_title[256] = { 0 };

const struct toplevel_entry *active = active_toplevel_entry(app);
if (active) {
if (active->app_id[0]) {
snprintf(new_app_id, sizeof(new_app_id), "%s", active->app_id);
}
if (active->title[0]) {
snprintf(new_title, sizeof(new_title), "%s", active->title);
}
}

bool app_changed = strcmp(new_app_id, app->active_app_id);
bool title_changed = strcmp(new_title, app->active_window_title);
if (!app_changed && !title_changed) {
return;
}

snprintf(app->active_app_id, sizeof(app->active_app_id), "%s", new_app_id);
snprintf(app->active_window_title, sizeof(app->active_window_title), "%s", new_title);

if (app_changed) {
global_menu_build_for_active_app(app);
}
if (!app->active_app_id[0]) {
global_menu_clear(app);
}
}

static bool
global_menu_activate_item(struct app *app, int top_idx, int item_idx)
{
if (!app || !app->global_menu_available
|| top_idx < 0 || item_idx < 0
|| top_idx >= (int)app->global_menu_top_count) {
return false;
}

struct global_menu_top *top = &app->global_menu_tops[top_idx];
if (item_idx >= (int)top->item_count) {
return false;
}

struct global_menu_item *item = &top->items[item_idx];
if (!item->enabled || !item->action[0]
|| !app->global_menu_bus[0] || !app->global_menu_actions_path[0]) {
return false;
}

GError *error = NULL;
GDBusConnection *conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
if (!conn) {
g_clear_error(&error);
return false;
}

GVariantBuilder params_builder;
g_variant_builder_init(&params_builder, G_VARIANT_TYPE("av"));
if (item->target[0]) {
GVariant *target = g_variant_parse(NULL, item->target, NULL, NULL, &error);
if (!target) {
g_clear_error(&error);
} else {
g_variant_builder_add_value(&params_builder, target);
}
}

GVariantBuilder platform_builder;
g_variant_builder_init(&platform_builder, G_VARIANT_TYPE("a{sv}"));

GVariant *call_args = g_variant_new("(s@av@a{sv})",
item->action,
g_variant_builder_end(&params_builder),
g_variant_builder_end(&platform_builder));

GVariant *reply = g_dbus_connection_call_sync(
conn,
app->global_menu_bus,
app->global_menu_actions_path,
"org.gtk.Actions",
"Activate",
call_args,
NULL,
G_DBUS_CALL_FLAGS_NONE,
450,
NULL,
&error);

if (!reply) {
g_clear_error(&error);
g_object_unref(conn);
return false;
}

g_variant_unref(reply);
g_object_unref(conn);
return true;
}

static void
group_badge_text(const struct app_group *group, char *out, size_t out_size)
{
if (!out || out_size == 0) {
return;
}
out[0] = '\0';
if (group->count > 1) {
snprintf(out, out_size, "%zu", group->count);
}
}

static void
request_top_panel_size(struct app *app)
{
if (!app->top.layer_surface || !app->top.surface) {
return;
}
uint32_t h = app->style.top_height;
if (app->quick_open) {
h = app->style.top_expanded_height;
if ((app->top_popup_mode == TOP_POPUP_QUICK
|| app->top_popup_mode == TOP_POPUP_NOTIFICATIONS
|| app->top_popup_mode == TOP_POPUP_NETWORK
|| app->top_popup_mode == TOP_POPUP_AUDIO
|| app->top_popup_mode == TOP_POPUP_POWER)
&& app->output_height > app->style.top_height) {
h = app->output_height;
}
}
zwlr_layer_surface_v1_set_size(app->top.layer_surface, 0, h);
zwlr_layer_surface_v1_set_exclusive_zone(app->top.layer_surface, app->style.top_height);
wl_surface_commit(app->top.surface);
}

static void
request_side_panel_size(struct app *app)
{
if (!app->side.layer_surface || !app->side.surface) {
return;
}
uint32_t w = app->popup_group >= 0 ? app->style.side_expanded_width : app->style.side_width;
if (app->launcher_open) {
w = launcher_panel_width(app);
}
zwlr_layer_surface_v1_set_size(app->side.layer_surface, w, 0);
zwlr_layer_surface_v1_set_keyboard_interactivity(app->side.layer_surface,
app->launcher_open
? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
: ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
zwlr_layer_surface_v1_set_layer(app->side.layer_surface,
ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
zwlr_layer_surface_v1_set_exclusive_zone(app->side.layer_surface, app->style.side_width);
wl_surface_commit(app->side.surface);
}

static void
trigger_redraw(struct app *app)
{
panel_draw(&app->top);
panel_draw(&app->side);
}

static bool
point_in_rect(double px, double py, double x, double y, double w, double h)
{
return px >= x && py >= y && px < x + w && py < y + h;
}

static int
days_in_month(int year, int month)
{
if (month < 1 || month > 12) {
return 30;
}

struct tm tm_date = {
 .tm_year = year - 1900,
 .tm_mon = month,
 .tm_mday = 0,
};
if (mktime(&tm_date) == (time_t)-1) {
return 30;
}
return tm_date.tm_mday;
}

static int
month_start_monday(int year, int month)
{
struct tm tm_date = {
 .tm_year = year - 1900,
 .tm_mon = month - 1,
 .tm_mday = 1,
};
if (mktime(&tm_date) == (time_t)-1) {
return 0;
}
return (tm_date.tm_wday + 6) % 7;
}

static void
calendar_set_today(struct app *app)
{
time_t now = time(NULL);
struct tm tm_now = { 0 };
if (now == (time_t)-1 || !localtime_r(&now, &tm_now)) {
app->calendar_year = 2026;
app->calendar_month = 1;
app->calendar_selected_day = 1;
return;
}

app->calendar_year = tm_now.tm_year + 1900;
app->calendar_month = tm_now.tm_mon + 1;
app->calendar_selected_day = tm_now.tm_mday;
}

static void
calendar_load_items(struct app *app)
{
app->calendar_item_count = 0;

char path[PATH_MAX] = { 0 };
launcher_get_path(path, sizeof(path), "calendar-items.txt");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "r");
if (!f) {
return;
}

char line[320];
while (fgets(line, sizeof(line), f)) {
if (app->calendar_item_count >= MAX_CALENDAR_ITEMS) {
break;
}
if (line[0] == '#' || line[0] == '\n') {
continue;
}

int y = 0;
int m = 0;
int d = 0;
char type[24] = { 0 };
char text[160] = { 0 };
if (sscanf(line, "%d-%d-%d|%23[^|]|%159[^\n]", &y, &m, &d, type, text) != 5) {
continue;
}
if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31 || !text[0]) {
continue;
}

struct calendar_item *it = &app->calendar_items[app->calendar_item_count++];
it->year = y;
it->month = m;
it->day = d;
it->task = !strcasecmp(type, "task") || !strcasecmp(type, "todo");
snprintf(it->text, sizeof(it->text), "%s", trim_in_place(text));
}

fclose(f);
}

static int
calendar_item_count_for_day(const struct app *app, int year, int month, int day)
{
int count = 0;
for (size_t i = 0; i < app->calendar_item_count; i++) {
const struct calendar_item *it = &app->calendar_items[i];
if (it->year == year && it->month == month && it->day == day) {
count++;
}
}
return count;
}

static void
clock_load_timezones(struct app *app)
{
app->clock_timezone_count = 0;

char path[PATH_MAX] = { 0 };
launcher_get_path(path, sizeof(path), "clock-timezones.txt");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "r");
if (!f) {
return;
}

char line[96];
while (fgets(line, sizeof(line), f)) {
if (app->clock_timezone_count >= MAX_CLOCK_ZONES) {
break;
}
char *tz = trim_in_place(line);
if (!tz[0] || tz[0] == '#') {
continue;
}
snprintf(app->clock_timezones[app->clock_timezone_count++],
sizeof(app->clock_timezones[0]), "%s", tz);
}

fclose(f);
}

static void
clock_save_timezones(struct app *app)
{
launcher_ensure_config_dir();

char path[PATH_MAX] = { 0 };
launcher_get_path(path, sizeof(path), "clock-timezones.txt");
if (!path[0]) {
return;
}

FILE *f = fopen(path, "w");
if (!f) {
fprintf(stderr, _("karton-shell: cannot write %s: %s\n"), path, strerror(errno));
return;
}

for (size_t i = 0; i < app->clock_timezone_count; i++) {
fprintf(f, "%s\n", app->clock_timezones[i]);
}
fclose(f);
}

static bool
clock_has_timezone(const struct app *app, const char *zone)
{
for (size_t i = 0; i < app->clock_timezone_count; i++) {
if (!strcmp(app->clock_timezones[i], zone)) {
return true;
}
}
return false;
}

static void
clock_add_timezone(struct app *app, const char *zone)
{
if (!zone || !*zone || app->clock_timezone_count >= MAX_CLOCK_ZONES) {
return;
}

if (clock_has_timezone(app, zone)) {
return;
}
snprintf(app->clock_timezones[app->clock_timezone_count++],
sizeof(app->clock_timezones[0]), "%s", zone);
clock_save_timezones(app);
}

static void
format_time_for_timezone(const char *zone, char *out, size_t out_size)
{
if (!out || out_size == 0) {
return;
}

const char *old_tz = getenv("TZ");
char old_buf[128] = { 0 };
bool had_old = old_tz && *old_tz;
if (had_old) {
snprintf(old_buf, sizeof(old_buf), "%s", old_tz);
}

if (zone && *zone) {
setenv("TZ", zone, 1);
} else {
unsetenv("TZ");
}
tzset();

time_t now = time(NULL);
struct tm tm_now = { 0 };
if (now == (time_t)-1 || !localtime_r(&now, &tm_now)
|| strftime(out, out_size, "%H:%M", &tm_now) == 0) {
snprintf(out, out_size, "--:--");
}

if (had_old) {
setenv("TZ", old_buf, 1);
} else {
unsetenv("TZ");
}
tzset();
}

static const char *
theme_mode_label(int mode)
{
if (mode == THEME_LIGHT) {
return _("Theme: Light");
}
if (mode == THEME_DARK) {
return _("Theme: Dark");
}
return _("Theme: Auto");
}

static bool
quick_tile_rect(const struct panel *panel, int tile, double *x, double *y, double *w, double *h)
{
double qx, qy, qw, qh;
if (tile < 0 || tile >= 4 || !top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return false;
}

const double tile_w = (qw - 34.0) / 2.0;
const double tile_h = 88.0;
const double base_y = qy + 314.0;
int row = tile / 2;
int col = tile % 2;

*x = qx + 16.0 + col * (tile_w + 12.0);
*y = base_y + row * (tile_h + 16.0);
*w = tile_w;
*h = tile_h;
return true;
}

static bool
quick_tile_arrow_rect(const struct panel *panel, int tile, double *x, double *y, double *w, double *h)
{
double tx, ty, tw, th;
if (!quick_tile_rect(panel, tile, &tx, &ty, &tw, &th)) {
return false;
}

*w = 24.0;
*h = 24.0;
*x = tx + tw - *w - 10.0;
*y = ty + th * 0.5 - *h * 0.5;
return true;
}

static int
quick_tile_menu_item_count(int tile)
{
switch (tile) {
case 0:
case 1:
return 2;
case 2:
return 3;
case 3:
return 2;
default:
return 0;
}
}

static const char *
quick_tile_menu_item_label(const struct app *app, int tile, int item)
{
switch (tile) {
case 0:
return item == 0 ? (app && app->quick_wifi_enabled ? _("Turn Wi-Fi off") : _("Turn Wi-Fi on")) : _("Open network settings");
case 1:
return item == 0 ? (app && app->quick_bluetooth_enabled ? _("Turn Bluetooth off") : _("Turn Bluetooth on")) : _("Open Bluetooth settings");
case 2:
if (item == 0) {
return _("Theme: Auto");
}
if (item == 1) {
return _("Theme: Light");
}
return _("Theme: Dark");
case 3:
return item == 0 ? _("Do not disturb: Off") : _("Do not disturb: On");
default:
return "";
}
}

static bool
quick_tile_menu_rect(const struct panel *panel, const struct app *app, int tile,
double *x, double *y, double *w, double *h)
{
double tx, ty, tw, th;
if (!app || tile < 0 || tile >= 4 || !quick_tile_rect(panel, tile, &tx, &ty, &tw, &th)) {
return false;
}

int item_count = quick_tile_menu_item_count(tile);
if (item_count <= 0) {
return false;
}

double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return false;
}

*w = tile < 2 ? 196.0 : 174.0;
*h = item_count * 28.0 + 10.0;
*x = tx + tw - *w;
*y = ty + th + 6.0;
if (*x + *w > qx + qw - 12.0) {
*x = qx + qw - *w - 12.0;
}
if (*x < qx + 12.0) {
*x = qx + 12.0;
}
if (*y + *h > qy + qh - 10.0) {
*y = ty - *h - 6.0;
}
if (*y < qy + 72.0) {
*y = qy + 72.0;
}
return true;
}

static int
quick_tile_arrow_hit(const struct panel *panel, double px, double py)
{
for (int i = 0; i < 4; i++) {
double x, y, w, h;
if (quick_tile_arrow_rect(panel, i, &x, &y, &w, &h)
&& point_in_rect(px, py, x, y, w, h)) {
return i;
}
}
return -1;
}

static int
quick_tile_menu_item_hit(const struct panel *panel, const struct app *app, double px, double py)
{
if (!app || app->quick_menu_tile < 0) {
return -1;
}

double x, y, w, h;
if (!quick_tile_menu_rect(panel, app, app->quick_menu_tile, &x, &y, &w, &h)
|| !point_in_rect(px, py, x, y, w, h)) {
return -1;
}

int row = (int)((py - y - 5.0) / 28.0);
if (row < 0 || row >= quick_tile_menu_item_count(app->quick_menu_tile)) {
return -1;
}
return row;
}

static void
top_quick_button_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
*w = 50.0;
*h = 28.0;
*x = panel->width - *w - 10.0;
*y = ((double)panel->app->style.top_height - *h) * 0.5;
if (*y < 4.0) {
*y = 4.0;
}
}

static void
top_date_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
double bx, by, bw, bh;
top_quick_button_rect(panel, &bx, &by, &bw, &bh);

*w = 144.0;
*h = 24.0;
double time_w = 76.0;
double gap = 8.0;
double total = *w + gap + time_w;
double left = 10.0;
double right = bx - 10.0;

*x = left + (right - left - total) * 0.5;
if (*x < left) {
*x = left;
}
*y = ((double)panel->app->style.top_height - *h) * 0.5;
if (*y < 3.0) {
*y = 3.0;
}
}

static void
top_time_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
double dx, dy, dw, dh;
top_date_rect(panel, &dx, &dy, &dw, &dh);
*w = 76.0;
*h = dh;
*x = dx + dw + 8.0;
*y = dy;
}

static bool
top_global_menu_bar_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
double dx, dy, dw, dh;
top_date_rect(panel, &dx, &dy, &dw, &dh);

*x = 10.0;
*y = 4.0;
*h = panel->app->style.top_height - 8.0;
*w = dx - *x - 10.0;
if (*h < 20.0 || *w < 120.0) {
return false;
}
return true;
}

static double
top_global_menu_item_width(const char *label)
{
size_t len = label ? strlen(label) : 0;
double width = 20.0 + (double)len * 7.0;
if (width < 52.0) {
width = 52.0;
}
if (width > 190.0) {
width = 190.0;
}
return width;
}

static bool
top_global_menu_item_rect(const struct panel *panel, int idx,
double *x, double *y, double *w, double *h)
{
struct app *app = panel->app;
if (idx < 0 || idx >= (int)app->global_menu_top_count || !app->global_menu_available) {
return false;
}

double bx, by, bw, bh;
if (!top_global_menu_bar_rect(panel, &bx, &by, &bw, &bh)) {
return false;
}

double cur_x = bx + 10.0;
double right = bx + bw;
for (int i = 0; i < (int)app->global_menu_top_count; i++) {
double item_w = top_global_menu_item_width(app->global_menu_tops[i].label);
if (cur_x + item_w > right - 8.0) {
break;
}
if (i == idx) {
*x = cur_x;
*y = by + 4.0;
*w = item_w;
*h = bh - 8.0;
return true;
}
cur_x += item_w + 4.0;
}

return false;
}

static int
top_global_menu_hit(const struct panel *panel, double px, double py)
{
for (int i = 0; i < (int)panel->app->global_menu_top_count; i++) {
double x, y, w, h;
if (!top_global_menu_item_rect(panel, i, &x, &y, &w, &h)) {
continue;
}
if (point_in_rect(px, py, x, y, w, h)) {
return i;
}
}
return -1;
}

static bool
top_global_menu_popup_rect(const struct panel *panel, int top_idx,
double *x, double *y, double *w, double *h)
{
if (top_idx < 0 || top_idx >= (int)panel->app->global_menu_top_count) {
return false;
}

double tx, ty, tw, th;
if (!top_global_menu_item_rect(panel, top_idx, &tx, &ty, &tw, &th)) {
return false;
}

const struct global_menu_top *top = &panel->app->global_menu_tops[top_idx];
double max_w = tw;
for (size_t i = 0; i < top->item_count; i++) {
double row_w = top_global_menu_item_width(top->items[i].label) + 110.0;
if (row_w > max_w) {
max_w = row_w;
}
}
if (max_w < 220.0) {
max_w = 220.0;
}
if (max_w > 460.0) {
max_w = 460.0;
}

double popup_h = top->item_count * 30.0 + 12.0;
if (popup_h < 40.0) {
popup_h = 40.0;
}

*x = tx;
*y = panel->app->style.top_height + 6.0;
*w = max_w;
*h = popup_h;

if (*x + *w > panel->width - 8.0) {
*x = panel->width - *w - 8.0;
}
if (*x < 8.0) {
*x = 8.0;
}
return true;
}

static int
top_global_menu_popup_item_hit(const struct panel *panel, int top_idx,
double px, double py)
{
double x, y, w, h;
if (!top_global_menu_popup_rect(panel, top_idx, &x, &y, &w, &h)) {
return -1;
}
if (!point_in_rect(px, py, x, y, w, h)) {
return -1;
}

int row = (int)((py - y - 6.0) / 30.0);
if (row < 0 || row >= (int)panel->app->global_menu_tops[top_idx].item_count) {
return -1;
}
return row;
}

static double
clamp_double(double value, double min_value, double max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static size_t
bounded_text_len(const char *text, size_t limit)
{
    size_t len = 0;

    if (!text) {
        return 0;
    }

    while (len < limit && text[len] != '\0') {
        len++;
    }
    return len;
}

static double
top_quick_panel_content_width(const struct panel *panel, double avail_w)
{
    struct app *app = panel->app;
    double min_w = 340.0;
    double max_w = 620.0;
    double base_w = panel->width * 0.30;
    size_t longest = 0;

    if (app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
        min_w = 340.0;
        max_w = 440.0;
        base_w = panel->width * 0.30;
    } else if (app->top_popup_mode == TOP_POPUP_NETWORK) {
        min_w = 360.0;
        max_w = 500.0;
        base_w = panel->width * 0.28;
        longest = bounded_text_len(app->quick_connection_name, sizeof(app->quick_connection_name));
        for (size_t i = 0; i < app->quick_network_count && i < 4; i++) {
            size_t len = bounded_text_len(app->quick_networks[i], sizeof(app->quick_networks[i]));
            if (len > longest) {
                longest = len;
            }
        }
        base_w += (double)longest * 3.4;
    } else if (app->top_popup_mode == TOP_POPUP_AUDIO) {
        min_w = 380.0;
        max_w = 540.0;
        base_w = panel->width * 0.30;
        for (size_t i = 0; i < app->quick_output_count && i < 3; i++) {
            size_t len = bounded_text_len(app->quick_outputs[i], sizeof(app->quick_outputs[i]));
            if (len > longest) {
                longest = len;
            }
        }
        for (size_t i = 0; i < app->quick_input_count && i < 3; i++) {
            size_t len = bounded_text_len(app->quick_inputs[i], sizeof(app->quick_inputs[i]));
            if (len > longest) {
                longest = len;
            }
        }
        base_w += (double)longest * 3.2;
    } else if (app->top_popup_mode == TOP_POPUP_POWER) {
        min_w = 360.0;
        max_w = 450.0;
        base_w = panel->width * 0.29;
    } else {
        min_w = 460.0;
        max_w = 620.0;
        base_w = panel->width * 0.38;
    }

    if (avail_w < min_w) {
        return avail_w;
    }

    return clamp_double(base_w, min_w, max_w < avail_w ? max_w : avail_w);
}

static double
top_quick_panel_content_height(const struct panel *panel, double avail_h)
{
    struct app *app = panel->app;
    double desired_h = 320.0;

    if (app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
        size_t shown = app->notification_count < 4 ? app->notification_count : 4;
        desired_h = shown == 0 ? 238.0 : 146.0 + (double)shown * 72.0;
        desired_h = clamp_double(desired_h, 250.0, avail_h);
        return desired_h;
    }

    if (app->top_popup_mode == TOP_POPUP_NETWORK) {
        size_t shown = app->quick_network_count < 4 ? app->quick_network_count : 4;
        desired_h = 310.0 + (double)shown * 26.0;
        desired_h = clamp_double(desired_h, 304.0, avail_h);
        return desired_h;
    }

    if (app->top_popup_mode == TOP_POPUP_AUDIO) {
        size_t shown_outputs = app->quick_output_count < 3 ? app->quick_output_count : 3;
        size_t shown_inputs = app->quick_input_count < 3 ? app->quick_input_count : 3;
                desired_h = 396.0 + (double)(shown_outputs + shown_inputs) * 24.0;
                desired_h = clamp_double(desired_h, 430.0, avail_h);
        return desired_h;
    }

    if (app->top_popup_mode == TOP_POPUP_POWER) {
		size_t shown = app->quick_removable_count < 3 ? app->quick_removable_count : 3;
		desired_h = (app->quick_battery_present ? 408.0 : 388.0) + (double)shown * 24.0;
		desired_h = clamp_double(desired_h, 360.0, avail_h);
        return desired_h;
    }

    if (app->top_popup_mode == TOP_POPUP_QUICK) {
        desired_h = 528.0;
        desired_h = clamp_double(desired_h, 528.0, avail_h);
        return desired_h;
    }

    desired_h = clamp_double(desired_h, 280.0, avail_h);
    return desired_h;
}

static bool
top_quick_panel_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
if (panel->height <= panel->app->style.top_height) {
return false;
}

double avail_w = panel->width - 20.0;
    *w = top_quick_panel_content_width(panel, avail_w);

double avail_h = panel->height - panel->app->style.top_height - 16.0;
    *h = top_quick_panel_content_height(panel, avail_h);
*x = panel->width - *w - 18.0;
if (*x < 12.0) {
*x = 12.0;
}
*y = panel->app->style.top_height + 12.0;
return true;
}

static bool
top_popup_rect_for_mode(const struct panel *panel, int mode,
double *x, double *y, double *w, double *h)
{
switch (mode) {
case TOP_POPUP_CALENDAR:
return top_calendar_panel_rect(panel, x, y, w, h);
case TOP_POPUP_CLOCK:
return top_clock_panel_rect(panel, x, y, w, h);
case TOP_POPUP_QUICK:
case TOP_POPUP_NOTIFICATIONS:
case TOP_POPUP_NETWORK:
case TOP_POPUP_AUDIO:
case TOP_POPUP_POWER:
return top_quick_panel_rect(panel, x, y, w, h);
default:
return false;
}
}

static bool
top_calendar_panel_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
if (panel->height <= panel->app->style.top_height) {
return false;
}
*w = 430.0;
*h = panel->height - panel->app->style.top_height - 16.0;
if (*h < 260.0) {
*h = 260.0;
}
*x = panel->width * 0.5 - *w * 0.5;
if (*x < 10.0) {
*x = 10.0;
}
if (*x + *w > panel->width - 10.0) {
*x = panel->width - *w - 10.0;
}
*y = panel->app->style.top_height + 8.0;
return true;
}

static bool
top_clock_panel_rect(const struct panel *panel, double *x, double *y, double *w, double *h)
{
if (panel->height <= panel->app->style.top_height) {
return false;
}

double avail_w = panel->width - 20.0;
double responsive_w = panel->width * 0.36;
if (responsive_w < 320.0) {
responsive_w = 320.0;
}
if (responsive_w > 460.0) {
responsive_w = 460.0;
}
if (responsive_w > avail_w) {
responsive_w = avail_w;
}
*w = responsive_w;

double avail_h = panel->height - panel->app->style.top_height - 16.0;
double desired_h = 240.0 + panel->app->clock_timezone_count * 36.0
+ (panel->app->clock_picker_open ? 44.0 : 0.0);
if (desired_h < 320.0) {
desired_h = 320.0;
}
if (desired_h > avail_h) {
desired_h = avail_h;
}
*h = desired_h;

double tx, ty, tw, th;
top_time_rect(panel, &tx, &ty, &tw, &th);
*x = tx + tw * 0.5 - *w * 0.5;

if (*x < 10.0) {
*x = 10.0;
}
if (*x + *w > panel->width - 10.0) {
*x = panel->width - *w - 10.0;
}
*y = panel->app->style.top_height + 8.0;
return true;
}

static bool
clock_picker_rect(const struct panel *panel, const struct app *app,
double *x, double *y, double *w, double *h)
{
double cx, cy, cw, ch;
if (!app->clock_picker_open
|| !top_clock_panel_rect(panel, &cx, &cy, &cw, &ch)) {
return false;
}

*w = 188.0;
*h = 8.0 * 24.0 + 34.0;
if (*h > ch - 80.0) {
*h = ch - 80.0;
}
if (*h < 120.0) {
*h = 120.0;
}
*x = cx + cw - *w - 12.0;
*y = cy + 48.0;
return true;
}

static int
clock_picker_item_hit(const struct panel *panel, const struct app *app, double px, double py)
{
double x, y, w, h;
if (!clock_picker_rect(panel, app, &x, &y, &w, &h)
|| !point_in_rect(px, py, x, y, w, h)) {
return -1;
}

double row_y = y + 30.0;
for (size_t i = 0; i < sizeof(clock_timezone_candidates) / sizeof(clock_timezone_candidates[0]); i++) {
if (point_in_rect(px, py, x + 8.0, row_y + i * 24.0, w - 16.0, 22.0)) {
return (int)i;
}
}

return -1;
}

static int
quick_tile_hit(const struct panel *panel, double px, double py)
{
for (int i = 0; i < 4; i++) {
double tx, ty, tw, th;
if (quick_tile_rect(panel, i, &tx, &ty, &tw, &th)
&& point_in_rect(px, py, tx, ty, tw, th)) {
return i;
}
}
return -1;
}

static int
quick_header_icon_hit(const struct panel *panel, double px, double py)
{
double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return -1;
}

for (int i = 0; i < 4; i++) {
double cx = qx + qw - 126.0 + i * 32.0;
double cy = qy + 44.0;
double dx = px - cx;
double dy = py - cy;
if (dx * dx + dy * dy <= 12.0 * 12.0) {
return i;
}
}
return -1;
}

static int
quick_slider_hit(const struct panel *panel, double px, double py, double *pct_out)
{
double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return -1;
}

for (int i = 0; i < 2; i++) {
double sy = qy + 156.0 + i * 104.0;
double tx = qx + 98.0;
double tw = qw - 196.0;
if (tw < 1.0) {
continue;
}

if (point_in_rect(px, py, tx, sy - 12.0, tw, 32.0)) {
double pct = (px - tx) / tw;
if (pct < 0.0) {
pct = 0.0;
}
if (pct > 1.0) {
pct = 1.0;
}
if (pct_out) {
*pct_out = pct;
}
return i;
}
}

return -1;
}

static int
audio_popup_slider_hit(const struct panel *panel, double px, double py, double *pct_out)
{
    double qx, qy, qw, qh;
    if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
        return -1;
    }

    for (int i = 0; i < 2; i++) {
        double sy = qy + 108.0 + i * 72.0;
        double tx = qx + 132.0;
        double tw = qw - 186.0;
        if (tw < 1.0) {
            continue;
        }
        if (point_in_rect(px, py, tx, sy + 24.0, tw, 20.0)) {
            double pct = (px - tx) / tw;
            if (pct < 0.0) {
                pct = 0.0;
            }
            if (pct > 1.0) {
                pct = 1.0;
            }
            if (pct_out) {
                *pct_out = pct;
            }
            return i;
        }
    }

    return -1;
}

static int
network_popup_item_hit(const struct panel *panel, const struct app *app, double px, double py)
{
if (!panel || !app) {
return -1;
}

double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return -1;
}

size_t shown = app->quick_network_count < 4 ? app->quick_network_count : 4;
for (size_t i = 0; i < shown; i++) {
double row_y = qy + 246.0 + i * 26.0;
if (point_in_rect(px, py, qx + 16.0, row_y, qw - 32.0, 22.0)) {
return (int)i;
}
}

return -1;
}

static int
audio_popup_device_hit(const struct panel *panel, const struct app *app,
bool input_device, double px, double py)
{
if (!panel || !app) {
return -1;
}

double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return -1;
}

if (!input_device) {
size_t shown = app->quick_output_count < 3 ? app->quick_output_count : 3;
for (size_t i = 0; i < shown; i++) {
double row_y = qy + 290.0 + i * 24.0;
if (point_in_rect(px, py, qx + 16.0, row_y, qw - 32.0, 20.0)) {
return (int)i;
}
}
return -1;
}

size_t shown_outputs = app->quick_output_count < 3 ? app->quick_output_count : 3;
double inputs_y = qy + 290.0 + shown_outputs * 24.0 + 18.0;
size_t shown_inputs = app->quick_input_count < 3 ? app->quick_input_count : 3;
for (size_t i = 0; i < shown_inputs; i++) {
double row_y = inputs_y + 24.0 + i * 24.0;
if (point_in_rect(px, py, qx + 16.0, row_y, qw - 32.0, 20.0)) {
return (int)i;
}
}

return -1;
}

static int
power_removable_item_hit(const struct panel *panel, const struct app *app, double px, double py)
{
    if (!app || app->quick_removable_count == 0) {
        return -1;
    }

    double qx, qy, qw, qh;
    if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
        return -1;
    }

    size_t shown = app->quick_removable_count < 3 ? app->quick_removable_count : 3;
    for (size_t i = 0; i < shown; i++) {
        double row_y = qy + 330.0 + i * 24.0;
        if (point_in_rect(px, py, qx + 16.0, row_y, qw - 32.0, 20.0)) {
            return (int)i;
        }
    }

    return -1;
}

static int
notification_action_hit(const struct panel *panel, double px, double py)
{
double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return -1;
}

for (int i = 0; i < 2; i++) {
double bx = qx + qw - 94.0 + i * 44.0;
double by = qy + 22.0;
if (point_in_rect(px, py, bx, by, 34.0, 34.0)) {
return i;
}
}
return -1;
}

static int
notification_item_hit(const struct panel *panel, double px, double py)
{
double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return -1;
}

for (int i = 0; i < 4; i++) {
double row_y = qy + 128.0 + i * 72.0;
if (point_in_rect(px, py, qx + 16.0, row_y, qw - 32.0, 62.0)) {
return i;
}
}
return -1;
}

static int
top_status_icon_hit(const struct panel *panel, double px, double py)
{
double bx, by, bw, bh;
top_quick_button_rect(panel, &bx, &by, &bw, &bh);

double sx = bx - 96.0;
double sy = by + 1.0;

if (point_in_rect(px, py, sx - 7.0, sy, 24.0, 24.0)) {
return 0;
}
if (point_in_rect(px, py, sx + 15.0, sy, 24.0, 24.0)) {
return 1;
}
if (point_in_rect(px, py, sx + 37.0, sy, 24.0, 24.0)) {
return 2;
}
if (point_in_rect(px, py, sx + 61.0, sy, 32.0, 24.0)) {
return 3;
}
return -1;
}

static void
quick_cycle_theme_mode(struct app *app)
{
if (app->style.theme_mode == THEME_AUTO) {
app->style.theme_mode = THEME_LIGHT;
} else if (app->style.theme_mode == THEME_LIGHT) {
app->style.theme_mode = THEME_DARK;
} else {
app->style.theme_mode = THEME_AUTO;
}

shell_style_apply_theme(&app->style);
save_theme_mode_override(app->style.theme_mode);
sync_environment_theme(app->style.theme_mode);
reload_shell_style_runtime(app);
}

static void
open_settings_page(const char *page)
{
const char *target = page && *page ? page : "appearance";
char cmd[512] = { 0 };
snprintf(cmd, sizeof(cmd),
"sh -lc 'if command -v karton-settings >/dev/null 2>&1; then karton-settings --page %s; elif [ -x \"$HOME/.local-karton/bin/karton-settings\" ]; then \"$HOME/.local-karton/bin/karton-settings\" --page %s; else command -v notify-send >/dev/null 2>&1 && notify-send \"Karton Settings\" \"karton-settings is not installed\" || true; fi'",
target, target);
spawn_command(cmd);
}

static void
quick_toggle_wifi(struct app *app)
{
if (!app) {
return;
}

app->quick_wifi_enabled = !app->quick_wifi_enabled;
if (!app->quick_wifi_enabled) {
snprintf(app->quick_wifi_name, sizeof(app->quick_wifi_name), "%s", _("Not connected"));
}
app->quick_status_updated = 0;

spawn_command(app->quick_wifi_enabled
? "sh -lc 'command -v nmcli >/dev/null 2>&1 && nmcli radio wifi on || true'"
: "sh -lc 'command -v nmcli >/dev/null 2>&1 && nmcli radio wifi off || true'");
}

static void
quick_toggle_bluetooth(struct app *app)
{
if (!app) {
return;
}

app->quick_bluetooth_enabled = !app->quick_bluetooth_enabled;
app->quick_status_updated = 0;

spawn_command(app->quick_bluetooth_enabled
? "sh -lc 'command -v bluetoothctl >/dev/null 2>&1 && bluetoothctl power on >/dev/null 2>&1 || true'"
: "sh -lc 'command -v bluetoothctl >/dev/null 2>&1 && bluetoothctl power off >/dev/null 2>&1 || true'");
}

static void
quick_apply_tile_menu_action(struct app *app, int tile, int item)
{
if (!app) {
return;
}

bool refresh_now = true;

if (tile == 0) {
if (item == 0) {
quick_toggle_wifi(app);
refresh_now = false;
} else {
open_settings_page("network");
}
} else if (tile == 1) {
if (item == 0) {
quick_toggle_bluetooth(app);
refresh_now = false;
} else {
open_settings_page("bluetooth");
}
} else if (tile == 2) {
app->style.theme_mode = item == 0 ? THEME_AUTO : (item == 1 ? THEME_LIGHT : THEME_DARK);
shell_style_apply_theme(&app->style);
save_theme_mode_override(app->style.theme_mode);
sync_environment_theme(app->style.theme_mode);
reload_shell_style_runtime(app);
} else if (tile == 3) {
app->dnd_enabled = item == 1;
}

app->quick_menu_tile = -1;
app->quick_menu_hover_item = -1;
if (refresh_now) {
refresh_quick_status(app);
}
panel_draw(&app->top);
if (app->side_enabled) {
panel_draw(&app->side);
}
}

static int
calendar_nav_hit(const struct panel *panel, double px, double py)
{
double x, y, w, h;
if (!top_calendar_panel_rect(panel, &x, &y, &w, &h)) {
return 0;
}

if (point_in_rect(px, py, x + 12.0, y + 12.0, 26.0, 24.0)) {
return -2;
}
if (point_in_rect(px, py, x + 42.0, y + 12.0, 26.0, 24.0)) {
return -1;
}
if (point_in_rect(px, py, x + w - 68.0, y + 12.0, 26.0, 24.0)) {
return 1;
}
if (point_in_rect(px, py, x + w - 38.0, y + 12.0, 26.0, 24.0)) {
return 2;
}
return 0;
}

static int
calendar_day_hit(const struct panel *panel, const struct app *app, double px, double py)
{
double x, y, w, h;
if (!top_calendar_panel_rect(panel, &x, &y, &w, &h)) {
return -1;
}

double grid_x = x + 16.0;
double grid_y = y + 74.0;
double cell_w = (w - 32.0) / 7.0;
double cell_h = 28.0;

if (!point_in_rect(px, py, grid_x, grid_y, cell_w * 7.0, cell_h * 6.0)) {
return -1;
}

int col = (int)((px - grid_x) / cell_w);
int row = (int)((py - grid_y) / cell_h);
int start = month_start_monday(app->calendar_year, app->calendar_month);
int day = row * 7 + col - start + 1;
if (day < 1 || day > days_in_month(app->calendar_year, app->calendar_month)) {
return -1;
}
return day;
}

static bool
clock_add_hit(const struct panel *panel, double px, double py)
{
double x, y, w, h;
if (!top_clock_panel_rect(panel, &x, &y, &w, &h)) {
return false;
}
return point_in_rect(px, py, x + w - 42.0, y + 8.0, 30.0, 28.0);
}

static int
clock_remove_hit(const struct panel *panel, const struct app *app, double px, double py)
{
double x, y, w, h;
if (!top_clock_panel_rect(panel, &x, &y, &w, &h)) {
return -1;
}

 double row_y = y + 154.0;
 const double row_step = 36.0;
 double content_right = app->clock_picker_open ? (x + w - 198.0) : (x + w - 12.0);
for (size_t i = 0; i < app->clock_timezone_count; i++) {
if (point_in_rect(px, py, content_right - 22.0, row_y + i * row_step, 20.0, 24.0)) {
return (int)i;
}
}
return -1;
}

static int
side_slot_hit(const struct panel *panel, double px, double py)
{
if (px < 0 || px > panel->app->style.side_width) {
return -1;
}

double cx = panel->app->style.side_width / 2.0;

double launcher_y = panel->height - 34.0;
double dx = px - cx;
double dy = py - launcher_y;
if (dx * dx + dy * dy <= 17.0 * 17.0) {
return 0;
}

double y = 40.0;
for (int slot = 1; slot < 64; slot++) {
dx = px - cx;
dy = py - y;
if (dx * dx + dy * dy <= 16.0 * 16.0) {
return slot;
}
y += 56.0;
if (y > launcher_y - 36.0) {
break;
}
}
return -1;
}

static double
group_center_y(int group_idx)
{
return 40.0 + group_idx * 56.0;
}

static void
popup_group_label(const char *app_id, char *out, size_t out_size)
{
if (!out || out_size == 0) {
return;
}

const char *base = app_id_base(app_id);
if (!base || !*base) {
snprintf(out, out_size, "%s", "App");
return;
}

bool upper = true;
size_t written = 0;
for (const char *p = base; *p && written + 1 < out_size; p++) {
char ch = *p;
if (ch == '.' || ch == '-' || ch == '_') {
if (written > 0 && out[written - 1] != ' ' && written + 1 < out_size) {
out[written++] = ' ';
}
upper = true;
continue;
}

out[written++] = upper ? (char)g_ascii_toupper(ch) : (char)g_ascii_tolower(ch);
upper = false;
}

out[written] = '\0';
}

static bool
popup_geometry(const struct panel *panel, const struct app *app,
int group_idx, double *x, double *y, double *w, double *h)
{
if (group_idx < 0 || group_idx >= (int)app->group_count) {
return false;
}
if (panel->width <= app->style.side_width + 40) {
return false;
}

double row_h = 36.0;
double header_h = 58.0;
double content_h = header_h + app->groups[group_idx].count * row_h + 10.0;
double center_y = group_center_y(group_idx);

*x = app->style.side_width + 10.0;
*w = panel->width - *x - 12.0;
if (*w > 252.0) {
*w = 252.0;
}
*h = content_h;
*y = center_y - content_h / 2.0;
if (*y < 8.0) {
*y = 8.0;
}
if (*y + *h > panel->height - 8.0) {
*y = panel->height - *h - 8.0;
}
return true;
}

static int
popup_item_hit(const struct panel *panel, const struct app *app,
int group_idx, double px, double py)
{
double x, y, w, h;
if (!popup_geometry(panel, app, group_idx, &x, &y, &w, &h)) {
return -1;
}
if (!point_in_rect(px, py, x, y, w, h)) {
return -1;
}

double row_h = 32.0;
double list_y = y + 34.0;
if (py < list_y) {
return -1;
}
int idx = (int)((py - list_y) / row_h);
if (idx < 0 || idx >= (int)app->groups[group_idx].count) {
return -1;
}
return idx;
}

static int
popup_preferred_item(const struct app *app, int group_idx)
{
if (!app || group_idx < 0 || group_idx >= (int)app->group_count) {
return -1;
}

const struct app_group *group = &app->groups[group_idx];
if (group->count == 0) {
return -1;
}

for (size_t i = 0; i < group->count; i++) {
const struct toplevel_entry *entry = &app->toplevels[group->indices[i]];
if (entry->active && !entry->minimized) {
return (int)i;
}
}

for (size_t i = 0; i < group->count; i++) {
const struct toplevel_entry *entry = &app->toplevels[group->indices[i]];
if (!entry->minimized) {
return (int)i;
}
}

return 0;
}

static void
popup_clamp_selection(struct app *app)
{
if (!app || app->popup_group < 0 || app->popup_group >= (int)app->group_count) {
if (app) {
app->popup_selected_item = -1;
}
return;
}

const struct app_group *group = &app->groups[app->popup_group];
if (group->count == 0) {
app->popup_selected_item = -1;
return;
}

if (app->popup_selected_item < 0 || app->popup_selected_item >= (int)group->count) {
app->popup_selected_item = popup_preferred_item(app, app->popup_group);
}
}

static void
popup_scroll_by(struct app *app, int delta)
{
if (!app || app->popup_group < 0 || app->popup_group >= (int)app->group_count || delta == 0) {
return;
}

const struct app_group *group = &app->groups[app->popup_group];
if (group->count < 2) {
return;
}

popup_clamp_selection(app);
if (app->popup_selected_item < 0) {
return;
}

int count = (int)group->count;
int next = app->popup_selected_item + delta;
while (next < 0) {
next += count;
}
while (next >= count) {
next -= count;
}

if (next == app->popup_selected_item) {
return;
}

app->popup_selected_item = next;
app->popup_hover_item = -1;
panel_draw(&app->side);
}

static void
activate_entry(struct app *app, struct toplevel_entry *entry)
{
if (!entry || !entry->handle) {
return;
}
if (app->seat) {
zwlr_foreign_toplevel_handle_v1_activate(entry->handle, app->seat);
}
zwlr_foreign_toplevel_handle_v1_unset_minimized(entry->handle);
}

static void
toggle_entry(struct app *app, struct toplevel_entry *entry)
{
if (!entry || !entry->handle) {
return;
}
if (entry->active && !entry->minimized) {
zwlr_foreign_toplevel_handle_v1_set_minimized(entry->handle);
return;
}
activate_entry(app, entry);
}

static void
render_top_panel(cairo_t *cairo, struct panel *panel)
{
char date_txt[64] = { 0 };
char time_txt[32] = { 0 };
format_local_datetime(date_txt, sizeof(date_txt), time_txt, sizeof(time_txt));
struct shell_style *style = &panel->app->style;
shell_style_apply_theme(style);
bool dark = theme_mode_is_dark(style->theme_mode);

cairo_save(cairo);
cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
set_source_hex_a(cairo, style->top_bg, 0.96);
rounded_rect(cairo, 4.0, 2.0, panel->width - 8.0, style->top_height - 4.0, 13.0);
cairo_fill(cairo);
cairo_restore(cairo);

set_source_hex_a(cairo, style->side_separator, dark ? 0.30 : 0.55);
rounded_rect(cairo, 4.5, 2.5, panel->width - 9.0, style->top_height - 5.0, 12.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);
set_source_hex_a(cairo, style->side_separator, dark ? 0.24 : 0.42);
cairo_rectangle(cairo, 14.0, style->top_height - 2.0, panel->width - 28.0, 1.0);
cairo_fill(cairo);

double bx, by, bw, bh;
top_quick_button_rect(panel, &bx, &by, &bw, &bh);

double dx, dy, dw, dh;
top_date_rect(panel, &dx, &dy, &dw, &dh);

double tx, ty, tw, th;
top_time_rect(panel, &tx, &ty, &tw, &th);

uint32_t chip_text = dark ? 0xf1f6ff : 0x22385d;

double mx, my, mw, mh;
if (panel->app->global_menu_available
&& top_global_menu_bar_rect(panel, &mx, &my, &mw, &mh)) {
double cur_x = mx + 10.0;
double right = mx + mw;
for (int i = 0; i < (int)panel->app->global_menu_top_count; i++) {
const char *label = panel->app->global_menu_tops[i].label;
double item_w = top_global_menu_item_width(label);
if (cur_x + item_w > right - 8.0) {
break;
}

bool active = panel->app->global_menu_open && panel->app->global_menu_open_top == i;
if (active) {
set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.62 : 0.40);
rounded_rect(cairo, cur_x, my + 4.0, item_w, mh - 8.0, 8.0);
cairo_fill(cairo);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
10.2, chip_text, active ? 0.98 : 0.90,
cur_x + 8.0, my + 7.0, (int)item_w - 12, PANGO_ALIGN_LEFT, label);

cur_x += item_w + 4.0;
}
}


draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.0, chip_text, 0.98, dx + 6.0, dy + 5.0,
(int)dw - 12, PANGO_ALIGN_CENTER, date_txt[0] ? date_txt : _("--:--"));

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
12.0, chip_text, 0.98, tx + 4.0, ty + 5.0,
(int)tw - 8, PANGO_ALIGN_CENTER, time_txt[0] ? time_txt : _("--:--"));

set_source_hex_a(cairo, dark ? 0x10192a : 0xffffff, dark ? 0.30 : 0.50);
rounded_rect(cairo, bx - 108.0, by, 102.0, bh, 14.0);
cairo_fill(cairo);
set_source_hex_a(cairo, dark ? 0xffffff : 0x1f3352, dark ? 0.08 : 0.12);
rounded_rect(cairo, bx - 107.5, by + 0.5, 101.0, bh - 1.0, 13.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

set_source_hex_a(cairo,
panel->app->quick_open && panel->app->top_popup_mode == TOP_POPUP_QUICK
? style->side_launcher_bg : (dark ? 0x10192a : 0xffffff),
panel->app->quick_open && panel->app->top_popup_mode == TOP_POPUP_QUICK
? 0.82 : (dark ? 0.38 : 0.52));
rounded_rect(cairo, bx, by, bw, bh, 14.0);
cairo_fill(cairo);
set_source_hex_a(cairo, dark ? 0xffffff : 0x1f3352, dark ? 0.08 : 0.12);
rounded_rect(cairo, bx + 0.5, by + 0.5, bw - 1.0, bh - 1.0, 13.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

draw_top_status_icons(cairo, panel->app, bx - 96.0, by + 5.0, style->top_text, dark);

draw_karton_symbol(cairo, 2, bx + bw * 0.5, by + bh * 0.52, 18.0,
panel->app->quick_open && panel->app->top_popup_mode == TOP_POPUP_QUICK
? 0xffffff : karton_symbol_color(2), dark ? 0.98 : 0.94);

if (panel->app->global_menu_open
&& panel->app->global_menu_available
&& panel->app->global_menu_open_top >= 0
&& panel->app->global_menu_open_top < (int)panel->app->global_menu_top_count) {
double px, py, pw, ph;
if (top_global_menu_popup_rect(panel, panel->app->global_menu_open_top, &px, &py, &pw, &ph)) {
set_source_hex_a(cairo, style->quick_panel_bg, 0.98);
rounded_rect(cairo, px, py, pw, ph, 11.0);
cairo_fill(cairo);

const struct global_menu_top *top = &panel->app->global_menu_tops[panel->app->global_menu_open_top];
for (size_t i = 0; i < top->item_count; i++) {
double ry = py + 6.0 + i * 30.0;
if (!top->items[i].enabled) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.58,
px + 12.0, ry + 8.0, (int)pw - 24, PANGO_ALIGN_LEFT, top->items[i].label);
continue;
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.4, style->quick_title_text, 0.95,
px + 12.0, ry + 8.0, (int)pw - 24, PANGO_ALIGN_LEFT, top->items[i].label);

set_source_hex_a(cairo, dark ? 0xffffff : 0x1f3352, dark ? 0.06 : 0.10);
cairo_rectangle(cairo, px + 8.0, ry + 28.0, pw - 16.0, 1.0);
cairo_fill(cairo);
}
}
}

if (!panel->app->quick_open) {
return;
}

if (panel->app->top_popup_mode == TOP_POPUP_CALENDAR) {
double qx, qy, qw, qh;
if (!top_calendar_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return;
}

set_source_hex_a(cairo, style->quick_panel_bg, 0.98);
rounded_rect(cairo, qx, qy, qw, qh, style->quick_panel_radius);
cairo_fill(cairo);

struct tm month_tm = {
 .tm_year = panel->app->calendar_year - 1900,
 .tm_mon = panel->app->calendar_month - 1,
 .tm_mday = 1,
};
char month_label[64] = { 0 };
if (mktime(&month_tm) != (time_t)-1) {
strftime(month_label, sizeof(month_label), "%B %Y", &month_tm);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
13.0, style->quick_title_text, 0.97, qx + 76.0, qy + 16.0,
(int)qw - 152, PANGO_ALIGN_CENTER, month_label[0] ? month_label : _("Calendar"));

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
13.0, style->quick_title_text, 0.95, qx + 12.0, qy + 16.0,
26, PANGO_ALIGN_CENTER, "«");
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
13.0, style->quick_title_text, 0.95, qx + 42.0, qy + 16.0,
26, PANGO_ALIGN_CENTER, "‹");
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
13.0, style->quick_title_text, 0.95, qx + qw - 68.0, qy + 16.0,
26, PANGO_ALIGN_CENTER, "›");
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
13.0, style->quick_title_text, 0.95, qx + qw - 38.0, qy + 16.0,
26, PANGO_ALIGN_CENTER, "»");

static const char *days[7] = { "Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd" };
double grid_x = qx + 16.0;
double grid_y = qy + 74.0;
double cell_w = (qw - 32.0) / 7.0;
double cell_h = 28.0;

for (int i = 0; i < 7; i++) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.84,
grid_x + i * cell_w, qy + 52.0, (int)cell_w, PANGO_ALIGN_CENTER, days[i]);
}

time_t now = time(NULL);
struct tm tm_now = { 0 };
int today_y = 0;
int today_m = 0;
int today_d = 0;
if (now != (time_t)-1 && localtime_r(&now, &tm_now)) {
today_y = tm_now.tm_year + 1900;
today_m = tm_now.tm_mon + 1;
today_d = tm_now.tm_mday;
}

int start = month_start_monday(panel->app->calendar_year, panel->app->calendar_month);
int total_days = days_in_month(panel->app->calendar_year, panel->app->calendar_month);
for (int row = 0; row < 6; row++) {
for (int col = 0; col < 7; col++) {
int day = row * 7 + col - start + 1;
double cx = grid_x + col * cell_w;
double cy = grid_y + row * cell_h;
if (day < 1 || day > total_days) {
continue;
}

bool selected = day == panel->app->calendar_selected_day;
bool today = day == today_d && panel->app->calendar_month == today_m
&& panel->app->calendar_year == today_y;
if (selected || today) {
set_source_hex_a(cairo, style->side_launcher_bg, selected ? 0.74 : 0.24);
rounded_rect(cairo, cx + 2.0, cy + 2.0, cell_w - 4.0, cell_h - 4.0, 7.0);
cairo_fill(cairo);
}

char num[16] = { 0 };
snprintf(num, sizeof(num), "%d", day);
draw_pango_text(cairo, "Noto Sans", selected ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
10.3, selected ? 0xffffff : style->quick_title_text,
selected ? 1.0 : 0.95, cx, cy + 6.0, (int)cell_w, PANGO_ALIGN_CENTER, num);

if (calendar_item_count_for_day(panel->app, panel->app->calendar_year,
panel->app->calendar_month, day) > 0) {
set_source_hex_a(cairo, 0x4f88ff, 0.95);
cairo_arc(cairo, cx + cell_w * 0.5, cy + cell_h - 5.0, 2.1, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
}
}
}

double list_y = qy + qh - 98.0;
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.8, style->quick_title_text, 0.95, qx + 14.0, list_y,
(int)qw - 28, PANGO_ALIGN_LEFT, _("Events and Tasks"));

int shown = 0;
for (size_t i = 0; i < panel->app->calendar_item_count && shown < 3; i++) {
const struct calendar_item *it = &panel->app->calendar_items[i];
if (it->year != panel->app->calendar_year || it->month != panel->app->calendar_month
|| it->day != panel->app->calendar_selected_day) {
continue;
}

char line[196] = { 0 };
snprintf(line, sizeof(line), "%s %s", it->task ? "☑" : "•", it->text);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.92, qx + 16.0, list_y + 20.0 + shown * 20.0,
(int)qw - 32, PANGO_ALIGN_LEFT, line);
shown++;
}
if (shown == 0) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.75, qx + 16.0, list_y + 22.0,
(int)qw - 32, PANGO_ALIGN_LEFT, _("No events or tasks"));
}
return;
}

if (panel->app->top_popup_mode == TOP_POPUP_CLOCK) {
double qx, qy, qw, qh;
if (!top_clock_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return;
}

set_source_hex_a(cairo, style->quick_panel_bg, 0.98);
rounded_rect(cairo, qx, qy, qw, qh, style->quick_panel_radius);
cairo_fill(cairo);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
13.5, style->quick_title_text, 0.96, qx + 14.0, qy + 14.0,
(int)qw - 48, PANGO_ALIGN_LEFT, _("World Clocks"));
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
15.0, style->quick_title_text, 0.95, qx + qw - 38.0, qy + 12.0,
26, PANGO_ALIGN_CENTER, "+");

if (panel->app->clock_picker_open) {
double px, py, pw, ph;
if (clock_picker_rect(panel, panel->app, &px, &py, &pw, &ph)) {
set_source_hex_a(cairo, dark ? 0x22304c : 0xe9f1ff, 0.96);
rounded_rect(cairo, px, py, pw, ph, 10.0);
cairo_fill(cairo);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.6, style->quick_title_text, 0.95, px + 10.0, py + 8.0,
(int)pw - 20, PANGO_ALIGN_LEFT, _("Choose timezone"));

for (size_t i = 0; i < sizeof(clock_timezone_candidates) / sizeof(clock_timezone_candidates[0]); i++) {
double ry = py + 30.0 + i * 24.0;
if (ry + 22.0 > py + ph - 6.0) {
break;
}

set_source_hex_a(cairo, dark ? 0x2f3f63 : 0xd8e6ff, 0.58);
rounded_rect(cairo, px + 8.0, ry, pw - 16.0, 22.0, 7.0);
cairo_fill(cairo);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.2, style->quick_title_text, 0.94, px + 14.0, ry + 5.0,
(int)pw - 28, PANGO_ALIGN_LEFT, clock_timezone_candidates[i]);
}
}
}

char local_clock[16] = { 0 };
format_time_for_timezone(NULL, local_clock, sizeof(local_clock));
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
35.0, style->quick_title_text, 0.98, qx + 18.0, qy + 52.0,
(int)qw - 32, PANGO_ALIGN_LEFT, local_clock);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
12.0, style->quick_title_text, 0.82, qx + 18.0, qy + 96.0,
(int)qw - 32, PANGO_ALIGN_LEFT, _("Local time"));

for (size_t i = 0; i < panel->app->clock_timezone_count; i++) {
double ry = qy + 154.0 + i * 36.0;
double content_right = panel->app->clock_picker_open ? (qx + qw - 198.0) : (qx + qw - 12.0);

set_source_hex_a(cairo, dark ? 0x253555 : 0xe9f1fc, 0.68);
rounded_rect(cairo, qx + 12.0, ry - 4.0, content_right - qx - 24.0, 28.0, 9.0);
cairo_fill(cairo);

char zone_clock[16] = { 0 };
format_time_for_timezone(panel->app->clock_timezones[i], zone_clock, sizeof(zone_clock));
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
12.4, style->quick_title_text, 0.96, qx + 18.0, ry + 4.0,
(int)(content_right - qx) - 72, PANGO_ALIGN_LEFT, panel->app->clock_timezones[i]);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
13.0, style->quick_title_text, 0.96, content_right - 94.0, ry + 4.0,
58, PANGO_ALIGN_RIGHT, zone_clock);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
12.0, style->quick_title_text, 0.78, content_right - 18.0, ry + 4.0,
18, PANGO_ALIGN_CENTER, "×");
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.5, style->quick_title_text, 0.74, qx + 18.0, qy + qh - 26.0,
(int)qw - 24, PANGO_ALIGN_LEFT, _("Click + to add timezone"));
return;
}

double qx, qy, qw, qh;
if (!top_quick_panel_rect(panel, &qx, &qy, &qw, &qh)) {
return;
}

set_source_hex_a(cairo, 0x000000, dark ? 0.30 : 0.16);
rounded_rect(cairo, qx + 6.0, qy + 10.0, qw, qh, style->quick_panel_radius + 4.0);
cairo_fill(cairo);

set_source_hex_a(cairo, style->quick_panel_bg, dark ? 0.96 : 0.98);
rounded_rect(cairo, qx, qy, qw, qh, style->quick_panel_radius);
cairo_fill(cairo);

set_source_hex_a(cairo, dark ? 0xffffff : 0x1f3352, dark ? 0.14 : 0.09);
rounded_rect(cairo, qx + 0.5, qy + 0.5, qw - 1.0, qh - 1.0, style->quick_panel_radius - 0.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

const char *panel_title = panel->app->top_popup_mode == TOP_POPUP_NOTIFICATIONS ? _("Notifications")
: panel->app->top_popup_mode == TOP_POPUP_NETWORK ? _("Network")
: panel->app->top_popup_mode == TOP_POPUP_AUDIO ? _("Sound")
: panel->app->top_popup_mode == TOP_POPUP_POWER ? _("Power")
: _("Quick Settings");

const char *panel_subtitle = panel->app->top_popup_mode == TOP_POPUP_NOTIFICATIONS ? _("Review recent notifications")
: panel->app->top_popup_mode == TOP_POPUP_NETWORK ? _("Connection and nearby devices")
: panel->app->top_popup_mode == TOP_POPUP_AUDIO ? _("Outputs, inputs and volume")
: panel->app->top_popup_mode == TOP_POPUP_POWER ? _("Battery and session actions")
: _("Network, sound and appearance");

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
19.0, style->quick_title_text, 0.98, qx + 30.0, qy + 34.0,
(int)qw - 60, PANGO_ALIGN_LEFT,
panel_title);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
12.8, style->quick_title_text, 0.70, qx + 30.0, qy + 72.0,
(int)qw - 60, PANGO_ALIGN_LEFT,
panel_subtitle);

if (panel->app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
for (int i = 0; i < 2; i++) {
double ax = qx + qw - 94.0 + i * 44.0;
double ay = qy + 22.0;
set_source_hex_a(cairo,
i == 0 ? style->side_launcher_bg : (dark ? 0x253555 : 0xf3f7fd),
i == 0 ? 0.82 : 0.94);
rounded_rect(cairo, ax, ay, 34.0, 34.0, 12.0);
cairo_fill(cairo);
bool have_icon = draw_named_icon(cairo, panel->app, i == 0 ? "check" : "clear",
ax + 17.0, ay + 17.0, 15.0);
if (!have_icon) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
13.0, i == 0 ? 0xffffff : style->quick_title_text, 0.96,
ax, ay + 9.0, 34, PANGO_ALIGN_CENTER, i == 0 ? "✓" : "×");
}
}

if (panel->app->notifications_cleared || panel->app->notification_count == 0) {
set_source_hex_a(cairo, dark ? 0x253555 : 0xf3f7fd, 0.88);
rounded_rect(cairo, qx + 16.0, qy + 128.0, qw - 32.0, 78.0, 18.0);
cairo_fill(cairo);
draw_named_icon(cairo, panel->app, "notifications", qx + qw * 0.5, qy + 152.0, 20.0);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.5, style->quick_title_text, 0.78,
qx + 20.0, qy + 172.0, (int)qw - 40, PANGO_ALIGN_CENTER, _("No notifications"));
return;
}

size_t visible_notifications = panel->app->notification_count < 4 ? panel->app->notification_count : 4;
for (size_t i = 0; i < visible_notifications; i++) {
double ny = qy + 128.0 + i * 72.0;
set_source_hex_a(cairo, dark ? 0x253555 : 0xf3f7fd, 0.90);
rounded_rect(cairo, qx + 16.0, ny, qw - 32.0, 62.0, 17.0);
cairo_fill(cairo);
set_source_hex_a(cairo, panel->app->notifications_read ? style->side_slot_inactive : style->side_launcher_bg,
panel->app->notifications_read ? 0.40 : 0.92);
cairo_arc(cairo, qx + 32.0, ny + 18.0, 4.0, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
draw_named_icon(cairo, panel->app, "notifications", qx + 44.0, ny + 31.0, 16.0);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
10.6, style->quick_title_text, 0.94,
qx + 60.0, ny + 10.0, (int)qw - 156, PANGO_ALIGN_LEFT, panel->app->notifications[i].text);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
8.9, style->quick_title_text, 0.58,
qx + 60.0, ny + 28.0, (int)qw - 156, PANGO_ALIGN_LEFT,
panel->app->notifications[i].detail[0] ? panel->app->notifications[i].detail
: (panel->app->notifications[i].app_name[0] ? panel->app->notifications[i].app_name : (panel->app->notifications_read ? _("Read") : _("Unread"))));
if (panel->app->notifications[i].app_name[0]) {
set_source_hex_a(cairo, dark ? 0x34435f : 0xe9f1fc, 0.84);
rounded_rect(cairo, qx + 60.0, ny + 42.0, 114.0, 14.0, 7.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
7.6, style->quick_title_text, 0.74,
qx + 68.0, ny + 45.0, 98, PANGO_ALIGN_LEFT, panel->app->notifications[i].app_name);
}
if (panel->app->notifications[i].action_label[0]) {
double pill_w = 124.0;
set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.22 : 0.14);
rounded_rect(cairo, qx + qw - pill_w - 24.0, ny + 39.0, pill_w, 17.0, 8.5);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
7.8, style->quick_title_text, 0.84,
qx + qw - pill_w - 18.0, ny + 42.0, (int)pill_w - 12, PANGO_ALIGN_CENTER,
panel->app->notifications[i].action_label);
}
}
return;
}

if (panel->app->top_popup_mode == TOP_POPUP_NETWORK) {
const char *connection_label = !strcmp(panel->app->quick_connection_type, "wifi") ? _("Connected via Wi-Fi")
: !strcmp(panel->app->quick_connection_type, "ethernet") ? _("Connected via Ethernet")
: _("No active connection");
const char *connection_name = panel->app->quick_connection_name[0] ? panel->app->quick_connection_name : _("Unknown network");
char down_text[96] = { 0 };
char up_text[96] = { 0 };
char down_value[48] = { 0 };
char up_value[48] = { 0 };
format_status_bytes(panel->app->quick_rx_bytes, down_value, sizeof(down_value));
format_status_bytes(panel->app->quick_tx_bytes, up_value, sizeof(up_value));
snprintf(down_text, sizeof(down_text), "%s %s", _("Down:"), down_value);
snprintf(up_text, sizeof(up_text), "%s %s", _("Up:"), up_value);

set_source_hex_a(cairo, dark ? 0x253555 : 0xf4f7fc, 0.90);
rounded_rect(cairo, qx + 16.0, qy + 108.0, qw - 32.0, 88.0, 20.0);
cairo_fill(cairo);
draw_wifi_icon(cairo, qx + 40.0, qy + 148.0, 22.0, karton_symbol_color(0), 0.96);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
12.8, style->quick_title_text, 0.96, qx + 66.0, qy + 124.0,
(int)qw - 116, PANGO_ALIGN_LEFT, connection_label);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
11.2, style->quick_title_text, 0.78, qx + 66.0, qy + 146.0,
(int)qw - 116, PANGO_ALIGN_LEFT, connection_name);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.66, qx + 66.0, qy + 167.0,
(int)(qw - 120.0) / 2, PANGO_ALIGN_LEFT, down_text);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.66, qx + qw * 0.5, qy + 167.0,
(int)(qw - 120.0) / 2, PANGO_ALIGN_LEFT, up_text);

set_source_hex_a(cairo, dark ? 0x34435f : 0xe9f1fc, 0.82);
rounded_rect(cairo, qx + 66.0, qy + 182.0, 108.0, 16.0, 8.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
8.0, style->quick_title_text, 0.78, qx + 74.0, qy + 185.0,
92, PANGO_ALIGN_LEFT, panel->app->quick_iface[0] ? panel->app->quick_iface : _("No interface"));

set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.20 : 0.14);
rounded_rect(cairo, qx + 182.0, qy + 182.0, 118.0, 16.0, 8.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
8.0, style->quick_title_text, 0.78, qx + 190.0, qy + 185.0,
102, PANGO_ALIGN_LEFT, panel->app->quick_wifi_enabled ? _("Wi-Fi enabled") : _("Wi-Fi disabled"));

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.2, style->quick_title_text, 0.86, qx + 18.0, qy + 220.0,
(int)qw - 36, PANGO_ALIGN_LEFT, _("Detected Wi-Fi networks"));

size_t shown = panel->app->quick_network_count < 4 ? panel->app->quick_network_count : 4;
for (size_t i = 0; i < shown; i++) {
double row_y = qy + 246.0 + i * 26.0;
set_source_hex_a(cairo, dark ? 0x253555 : 0xf7f9fd, 0.86);
rounded_rect(cairo, qx + 16.0, row_y, qw - 32.0, 22.0, 10.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.90, qx + 26.0, row_y + 5.0,
(int)qw - 52, PANGO_ALIGN_LEFT, panel->app->quick_networks[i]);
}

double button_y = qy + qh - 50.0;
double button_w = (qw - 42.0) / 2.0;
set_source_hex_a(cairo, style->side_launcher_bg, 0.18);
rounded_rect(cairo, qx + 16.0, button_y, button_w, 34.0, 12.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.94, qx + 16.0, button_y + 10.0,
(int)button_w, PANGO_ALIGN_CENTER, _("Add network"));

set_source_hex_a(cairo, dark ? 0x253555 : 0xf2f6fc, 0.94);
rounded_rect(cairo, qx + 26.0 + button_w, button_y, button_w, 34.0, 12.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.94, qx + 26.0 + button_w, button_y + 10.0,
(int)button_w, PANGO_ALIGN_CENTER, _("Open settings"));
return;
}

if (panel->app->top_popup_mode == TOP_POPUP_AUDIO) {
const char *audio_labels[2] = { _("Speakers"), _("Microphone") };
int audio_values[2] = { panel->app->quick_volume, panel->app->quick_input_volume };
const char *audio_defaults[2] = {
    panel->app->quick_default_output[0] ? panel->app->quick_default_output : _("Default output"),
    panel->app->quick_default_input[0] ? panel->app->quick_default_input : _("Default input")
};
for (int i = 0; i < 2; i++) {
    double sy = qy + 108.0 + i * 72.0;
    set_source_hex_a(cairo, dark ? 0x253555 : 0xf4f7fc, 0.90);
    rounded_rect(cairo, qx + 16.0, sy, qw - 32.0, 58.0, 18.0);
    cairo_fill(cairo);
    if (i == 0) {
        draw_speaker_icon(cairo, qx + 38.0, sy + 29.0, 16.0, karton_symbol_color(2), 0.96);
    } else {
        bool have_mic_icon = draw_named_icon(cairo, panel->app, "sound", qx + 38.0, sy + 29.0, 16.0);
        if (!have_mic_icon) {
            draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
            12.0, style->quick_title_text, 0.82, qx + 30.0, sy + 20.0,
            20, PANGO_ALIGN_CENTER, "M");
        }
    }
    draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
    12.0, style->quick_title_text, 0.96, qx + 62.0, sy + 12.0,
    (int)qw - 130, PANGO_ALIGN_LEFT, audio_labels[i]);
    char value_text[24] = { 0 };
    snprintf(value_text, sizeof(value_text), "%d%%", audio_values[i]);
    draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
    10.4, style->quick_title_text, 0.72, qx + qw - 70.0, sy + 12.0,
    54, PANGO_ALIGN_RIGHT, value_text);
    draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
    9.1, style->quick_title_text, 0.64, qx + 62.0, sy + 28.0,
    (int)qw - 136, PANGO_ALIGN_LEFT, audio_defaults[i]);
    double track_x = floor(qx + 132.0);
    double track_y = floor(sy + 33.0);
    double track_w = floor(qw - 186.0);
    if (track_w < 1.0) {
        track_w = 1.0;
    }
    set_source_hex_a(cairo, dark ? 0x34435f : 0xd6dfed, 0.70);
    rounded_rect(cairo, track_x, track_y, track_w, 8.0, 4.0);
    cairo_fill(cairo);
    double pct = audio_values[i] / 100.0;
    if (pct < 0.0) {
        pct = 0.0;
    }
    if (pct > 1.0) {
        pct = 1.0;
    }
    double fill_w = floor(track_w * pct);
    set_source_hex_a(cairo, i == 0 ? 0x5d9cff : 0x60c2a4, 0.96);
    if (fill_w > 0.0) {
        rounded_rect(cairo, track_x, track_y, fill_w, 8.0, 4.0);
        cairo_fill(cairo);
    }
    set_source_hex_a(cairo, 0xffffff, 0.96);
    cairo_arc(cairo, track_x + fill_w, track_y + 4.0, 7.0, 0, 2.0 * 3.14159265358979323846);
    cairo_fill(cairo);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.2, style->quick_title_text, 0.86, qx + 18.0, qy + 266.0,
(int)qw - 36, PANGO_ALIGN_LEFT, _("Output devices"));
size_t shown_outputs = panel->app->quick_output_count < 3 ? panel->app->quick_output_count : 3;
for (size_t i = 0; i < shown_outputs; i++) {
double row_y = qy + 290.0 + i * 24.0;
set_source_hex_a(cairo, dark ? 0x253555 : 0xf7f9fd, 0.86);
rounded_rect(cairo, qx + 16.0, row_y, qw - 32.0, 20.0, 9.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
9.8, style->quick_title_text, 0.90, qx + 24.0, row_y + 4.0,
(int)qw - 140, PANGO_ALIGN_LEFT, panel->app->quick_outputs[i]);
    if (panel->app->quick_default_output[0] && !strcmp(panel->app->quick_outputs[i], panel->app->quick_default_output)) {
        set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.22 : 0.14);
        rounded_rect(cairo, qx + qw - 108.0, row_y + 2.0, 76.0, 16.0, 8.0);
        cairo_fill(cairo);
        draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
        7.6, style->quick_title_text, 0.78, qx + qw - 102.0, row_y + 5.0,
        64, PANGO_ALIGN_CENTER, _("Default"));
    }
}

double inputs_y = qy + 290.0 + shown_outputs * 24.0 + 18.0;
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.2, style->quick_title_text, 0.86, qx + 18.0, inputs_y,
(int)qw - 36, PANGO_ALIGN_LEFT, _("Input devices"));
size_t shown_inputs = panel->app->quick_input_count < 3 ? panel->app->quick_input_count : 3;
for (size_t i = 0; i < shown_inputs; i++) {
double row_y = inputs_y + 24.0 + i * 24.0;
set_source_hex_a(cairo, dark ? 0x253555 : 0xf7f9fd, 0.86);
rounded_rect(cairo, qx + 16.0, row_y, qw - 32.0, 20.0, 9.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
9.8, style->quick_title_text, 0.90, qx + 24.0, row_y + 4.0,
(int)qw - 140, PANGO_ALIGN_LEFT, panel->app->quick_inputs[i]);
    if (panel->app->quick_default_input[0] && !strcmp(panel->app->quick_inputs[i], panel->app->quick_default_input)) {
        set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.22 : 0.14);
        rounded_rect(cairo, qx + qw - 108.0, row_y + 2.0, 76.0, 16.0, 8.0);
        cairo_fill(cairo);
        draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
        7.6, style->quick_title_text, 0.78, qx + qw - 102.0, row_y + 5.0,
        64, PANGO_ALIGN_CENTER, _("Default"));
    }
}

set_source_hex_a(cairo, dark ? 0x253555 : 0xf2f6fc, 0.94);
rounded_rect(cairo, qx + 16.0, qy + qh - 50.0, qw - 32.0, 34.0, 12.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.94, qx + 16.0, qy + qh - 40.0,
(int)qw - 32, PANGO_ALIGN_CENTER, _("Open sound settings"));
return;
}

if (panel->app->top_popup_mode == TOP_POPUP_POWER) {
set_source_hex_a(cairo, dark ? 0x253555 : 0xf4f7fc, 0.90);
rounded_rect(cairo, qx + 16.0, qy + 108.0, qw - 32.0, 68.0, 18.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.2, style->quick_title_text, 0.86, qx + 26.0, qy + 122.0,
110, PANGO_ALIGN_LEFT, _("Battery:"));
if (panel->app->quick_battery_present) {
draw_battery_icon(cairo, qx + qw - 90.0, qy + 120.0, 44.0, 18.0,
panel->app->quick_battery_percent / 100.0, karton_symbol_color(3), 0.96);
char battery_text[64] = { 0 };
snprintf(battery_text, sizeof(battery_text), "%d%%", panel->app->quick_battery_percent);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
18.0, style->quick_title_text, 0.98, qx + 26.0, qy + 140.0,
120, PANGO_ALIGN_LEFT, battery_text);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, style->quick_title_text, 0.72, qx + 26.0, qy + 160.0,
(int)qw - 140, PANGO_ALIGN_LEFT,
panel->app->quick_battery_charging ? _("Charging") : _("Discharging"));
char battery_time[48] = { 0 };
format_status_minutes(panel->app->quick_battery_charging ? panel->app->quick_battery_minutes_to_full : panel->app->quick_battery_minutes_to_empty,
battery_time, sizeof(battery_time));
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
9.8, style->quick_title_text, 0.66, qx + 140.0, qy + 122.0,
(int)qw - 170, PANGO_ALIGN_LEFT,
battery_time[0] ? (panel->app->quick_battery_charging ? _("Time to full:") : _("Time remaining:")) : _("Time estimate unavailable"));
if (battery_time[0]) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.82, qx + 140.0, qy + 142.0,
(int)qw - 170, PANGO_ALIGN_LEFT, battery_time);
}
} else {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.4, style->quick_title_text, 0.78, qx + 26.0, qy + 146.0,
(int)qw - 52, PANGO_ALIGN_LEFT, _("No battery detected"));
}

double action_w = (qw - 42.0) / 2.0;
double action_h = 42.0;
double action_y = qy + 196.0;
for (int i = 0; i < 4; i++) {
int row = i / 2;
int col = i % 2;
double rx = qx + 16.0 + col * (action_w + 10.0);
double ry = action_y + row * (action_h + 10.0);
set_source_hex_a(cairo, dark ? 0x253555 : 0xf7f9fd, 0.90);
rounded_rect(cairo, rx, ry, action_w, action_h, 14.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.94, rx, ry + 13.0,
(int)action_w, PANGO_ALIGN_CENTER, launcher_power_action_label(i));
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.8, style->quick_title_text, 0.84, qx + 18.0, qy + 314.0,
(int)qw - 36, PANGO_ALIGN_LEFT, _("Mounted devices"));
size_t shown_removable = panel->app->quick_removable_count < 3 ? panel->app->quick_removable_count : 3;
if (shown_removable == 0) {
    set_source_hex_a(cairo, dark ? 0x253555 : 0xf7f9fd, 0.82);
    rounded_rect(cairo, qx + 16.0, qy + 330.0, qw - 32.0, 20.0, 9.0);
    cairo_fill(cairo);
    draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
    9.4, style->quick_title_text, 0.72, qx + 24.0, qy + 334.0,
    (int)qw - 48, PANGO_ALIGN_LEFT, _("No removable devices"));
} else {
    for (size_t i = 0; i < shown_removable; i++) {
        double row_y = qy + 330.0 + i * 24.0;
        set_source_hex_a(cairo, dark ? 0x253555 : 0xf7f9fd, 0.86);
        rounded_rect(cairo, qx + 16.0, row_y, qw - 32.0, 20.0, 9.0);
        cairo_fill(cairo);
        draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
        9.6, style->quick_title_text, 0.90, qx + 24.0, row_y + 4.0,
        (int)qw - 150, PANGO_ALIGN_LEFT,
        panel->app->quick_removable_names[i][0] ? panel->app->quick_removable_names[i] : panel->app->quick_removable_paths[i]);
        draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
        8.0, style->quick_title_text, 0.62, qx + qw - 140.0, row_y + 6.0,
        108, PANGO_ALIGN_RIGHT, panel->app->quick_removable_paths[i]);
    }
}

set_source_hex_a(cairo, dark ? 0x253555 : 0xf2f6fc, 0.94);
rounded_rect(cairo, qx + 16.0, qy + qh - 50.0, qw - 32.0, 34.0, 12.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, style->quick_title_text, 0.94, qx + 16.0, qy + qh - 40.0,
(int)qw - 32, PANGO_ALIGN_CENTER, _("Open power settings"));
return;
}

const char *slider_labels[2] = {
_("Brightness"),
_("Volume"),
};

for (int i = 0; i < 2; i++) {
double sy = qy + 156.0 + i * 104.0;

set_source_hex_a(cairo, dark ? 0x253555 : 0xf4f7fc, 0.88);
rounded_rect(cairo, qx + 16.0, sy - 34.0, qw - 32.0, 74.0, 22.0);
cairo_fill(cairo);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
14.0, style->quick_title_text, 0.82, qx + 98.0, sy - 18.0,
(int)qw - 140, PANGO_ALIGN_LEFT, slider_labels[i]);

double track_x = floor(qx + 98.0);
double track_y = floor(sy + 12.0);
double track_w = floor(qw - 196.0);
if (track_w < 1.0) {
track_w = 1.0;
}

set_source_hex_a(cairo, dark ? 0x34435f : 0xd6dfed, 0.70);
rounded_rect(cairo, track_x, track_y, track_w, 9.0, 4.5);
cairo_fill(cairo);

double pct = i == 0 ? panel->app->quick_brightness / 100.0 : panel->app->quick_volume / 100.0;
if (pct < 0.0) {
pct = 0.0;
}
if (pct > 1.0) {
pct = 1.0;
}
double fill_w = floor(track_w * pct);
set_source_hex_a(cairo, i == 0 ? 0xffb84d : 0x5d9cff, 0.96);
if (fill_w > 0.0) {
rounded_rect(cairo, track_x, track_y, fill_w, 9.0, 4.5);
cairo_fill(cairo);
}

set_source_hex_a(cairo, dark ? 0xffffff : 0xffffff, 0.96);
cairo_arc(cairo, track_x + fill_w, track_y + 4.5, 8.0, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);

if (i == 0) {
draw_quick_header_icon(cairo, 1, qx + 54.0, sy + 3.0, style->quick_title_text, 0.84);
draw_quick_header_icon(cairo, 1, qx + qw - 56.0, sy + 3.0, style->quick_title_text, 0.48);
} else {
draw_speaker_icon(cairo, qx + 54.0, sy + 3.0, 12.0, style->quick_title_text, 0.86);
draw_speaker_icon(cairo, qx + qw - 56.0, sy + 3.0, 12.0, style->quick_title_text, 0.50);
}
}

bool tile_active[4] = {
panel->app->quick_wifi_enabled,
panel->app->quick_bluetooth_enabled,
style->theme_mode == THEME_DARK,
panel->app->dnd_enabled,
};
const char *icon_names[4] = {
"network",
"bluetooth",
"theme",
"dnd",
};
const char *labels[4] = {
_("Wi-Fi"),
_("Bluetooth"),
theme_mode_label(style->theme_mode),
panel->app->dnd_enabled ? _("Do not disturb: On") : _("Do not disturb: Off"),
};
for (int i = 0; i < 4; i++) {
double rx, ry, rw, rh;
quick_tile_rect(panel, i, &rx, &ry, &rw, &rh);
int row = i / 2;

set_source_hex_a(cairo, style->quick_tile_bg, dark ? 0.94 : 0.97);
rounded_rect(cairo, rx, ry, rw, rh, 22.0);
cairo_fill(cairo);

set_source_hex_a(cairo,
tile_active[i] ? style->side_launcher_bg : (dark ? 0xffffff : 0x1f3352),
tile_active[i] ? (dark ? 0.26 : 0.16) : (dark ? 0.08 : 0.08));
rounded_rect(cairo, rx + 0.5, ry + 0.5, rw - 1.0, rh - 1.0, 21.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

bool have_icon = draw_named_icon(cairo, panel->app, icon_names[i], rx + 26.0, ry + 29.0, 20.0);
if (!have_icon) {
draw_karton_symbol(cairo, i, rx + 26.0, ry + 29.0, 18.0,
karton_symbol_color(i), 0.96);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
14.5, dark ? 0xf2f6ff : 0x2a3b56, 0.98,
rx + 64.0, ry + 20.0, (int)rw - 112, PANGO_ALIGN_LEFT, labels[i]);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
11.4, dark ? 0xc9d5ec : 0x6a7f9f, 0.86,
rx + 64.0, ry + 50.0, (int)rw - 112, PANGO_ALIGN_LEFT,
(panel->app->quick_hover_tile == i && i == 0 ? panel->app->quick_wifi_name :
(i == 0 ? _("Open network settings") : (i == 1 ? _("Open Bluetooth settings") : (i == 2 ? _("Switch theme mode") : _("Toggle quiet mode"))))));

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
13.0, dark ? 0xf2f6ff : 0x2a3b56, 0.78,
rx + rw - 34.0, ry + rh * 0.5 - 10.0, 20, PANGO_ALIGN_CENTER,
row == 0 ? "›" : "⌄");
}

if (panel->app->top_popup_mode == TOP_POPUP_QUICK && panel->app->quick_menu_tile >= 0) {
double mx, my, mw, mh;
if (quick_tile_menu_rect(panel, panel->app, panel->app->quick_menu_tile, &mx, &my, &mw, &mh)) {
set_source_hex_a(cairo, dark ? 0x1b2434 : 0xffffff, 0.98);
rounded_rect(cairo, mx, my, mw, mh, 12.0);
cairo_fill(cairo);
set_source_hex_a(cairo, dark ? 0xffffff : 0x1f3352, dark ? 0.08 : 0.10);
rounded_rect(cairo, mx + 0.5, my + 0.5, mw - 1.0, mh - 1.0, 11.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

for (int i = 0; i < quick_tile_menu_item_count(panel->app->quick_menu_tile); i++) {
double item_y = my + 5.0 + i * 28.0;
if (i == panel->app->quick_menu_hover_item) {
set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.42 : 0.22);
rounded_rect(cairo, mx + 4.0, item_y, mw - 8.0, 24.0, 8.0);
cairo_fill(cairo);
}
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
9.6, style->quick_title_text, 0.95, mx + 10.0, item_y + 7.0,
(int)mw - 20, PANGO_ALIGN_LEFT,
quick_tile_menu_item_label(panel->app, panel->app->quick_menu_tile, i));
}
}
}

}

static void
render_side_panel(cairo_t *cairo, struct panel *panel)
{
struct app *app = panel->app;
rebuild_groups(app);
struct shell_style *style = &app->style;
bool dark = theme_mode_is_dark(style->theme_mode);
uint32_t launcher_text = dark ? 0xe8ecf4 : 0x2c3f61;
uint32_t launcher_subtle = dark ? 0xc9d5ec : 0x6a7f9f;
uint32_t popup_text = dark ? 0xe8ecf4 : 0x253a5d;
uint32_t popup_header = dark ? 0xe9edf3 : 0x2a3f63;
bool fullscreen = active_toplevel_is_fullscreen(app);

double dock_x = fullscreen ? 0.0 : 8.0;
double dock_y = fullscreen ? 0.0 : 10.0;
double dock_w = fullscreen ? style->side_width : style->side_width - 16.0;
double dock_h = fullscreen ? panel->height : panel->height - 20.0;
double dock_radius = fullscreen ? 0.0 : 16.0;
if (dock_w < 40.0) {
dock_w = style->side_width;
dock_x = 0.0;
}

set_source_hex_a(cairo, style->side_bg, dark ? 0.90 : 0.97);
rounded_rect(cairo, dock_x, dock_y, dock_w, dock_h, dock_radius);
cairo_fill(cairo);

set_source_hex_a(cairo, style->side_separator, dark ? 0.22 : 0.35);
rounded_rect(cairo, dock_x + 0.5, dock_y + 0.5, dock_w - 1.0, dock_h - 1.0,
dock_radius > 0.5 ? dock_radius - 0.5 : 0.0);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

double cx = style->side_width / 2.0;
double y = 40.0;
double launcher_y = panel->height - 34.0;

for (size_t i = 0; i < app->group_count; i++) {
if (y > launcher_y - 36.0) {
break;
}

const struct app_group *group = &app->groups[i];
bool all_minimized = true;
for (size_t group_item = 0; group_item < group->count; group_item++) {
const struct toplevel_entry *group_entry = &app->toplevels[group->indices[group_item]];
if (!group_entry->minimized) {
all_minimized = false;
break;
}
}

double slot_x = dock_x + 6.0;
double slot_y = y - 20.0;
double slot_w = dock_w - 12.0;
double slot_h = 40.0;

set_source_hex_a(cairo, style->side_slot_inactive, dark ? 0.45 : 0.60);
rounded_rect(cairo, slot_x, slot_y, slot_w, slot_h, 10.0);
cairo_fill(cairo);

if ((int)i == app->hovered_group || group->any_active) {
set_source_hex_a(cairo,
group->any_active ? style->side_slot_active : style->side_launcher_bg,
group->any_active ? 0.94 : 0.88);
rounded_rect(cairo, slot_x, slot_y, slot_w, slot_h, 10.0);
cairo_fill(cairo);

set_source_hex(cairo, group->any_active ? style->side_slot_active : style->side_launcher_bg);
cairo_arc(cairo, dock_x + 3.0, y, 2.2, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
}

if (!draw_group_app_icon(cairo, app, group, cx, y, 24.0)) {
draw_karton_symbol(cairo, 7, cx, y, 24.0,
group->any_active ? 0xffb84d : karton_symbol_color(7), all_minimized ? 0.58 : 0.98);
} else if (all_minimized) {
set_source_hex_a(cairo, dark ? 0x10192a : 0xffffff, dark ? 0.26 : 0.36);
rounded_rect(cairo, cx - 12.0, y - 12.0, 24.0, 24.0, 8.0);
cairo_fill(cairo);
}

if (all_minimized) {
set_source_hex_a(cairo, dark ? 0xffffff : 0x24395c, dark ? 0.28 : 0.34);
cairo_rectangle(cairo, slot_x + 10.0, slot_y + slot_h - 7.0, slot_w - 20.0, 2.0);
cairo_fill(cairo);
}

char badge[16] = { 0 };
group_badge_text(group, badge, sizeof(badge));
if (badge[0]) {
set_source_hex(cairo, 0xf4b400);
cairo_arc(cairo, slot_x + slot_w - 4.0, y - 10.0, 8.0, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
9.0, 0x1a1d20, 1.0, slot_x + slot_w - 11.0, y - 17.0,
14, PANGO_ALIGN_CENTER, badge);
}

y += 56.0;
}

double launcher_slot_x = dock_x + 6.0;
double launcher_slot_y = launcher_y - 20.0;
double launcher_slot_w = dock_w - 12.0;
double launcher_slot_h = 40.0;
set_source_hex_a(cairo, style->side_slot_inactive, dark ? 0.45 : 0.60);
rounded_rect(cairo, launcher_slot_x, launcher_slot_y, launcher_slot_w, launcher_slot_h, 10.0);
cairo_fill(cairo);

if (app->launcher_open) {
set_source_hex_a(cairo, style->side_launcher_bg, 0.90);
rounded_rect(cairo, launcher_slot_x, launcher_slot_y, launcher_slot_w, launcher_slot_h, 10.0);
cairo_fill(cairo);

set_source_hex(cairo, style->side_launcher_bg);
cairo_arc(cairo, dock_x + 3.0, launcher_y, 2.2, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);
}
draw_karton_symbol(cairo, 6, cx, launcher_y, 24.0, karton_symbol_color(6), 0.98);

if (app->launcher_open) {
double lx, ly, lw, lh;
if (launcher_geometry(panel, &lx, &ly, &lw, &lh)) {
double search_y, chips_y, list_y, row_h;
int max_visible = 0;
launcher_layout(panel, &lx, &ly, &lw, &lh,
&search_y, &chips_y, &list_y, &row_h, &max_visible);
launcher_clamp_scroll_offset(app, panel);

set_source_hex_a(cairo, style->side_popup_bg, 0.98);
rounded_rect(cairo, lx, ly, lw, lh, style->popup_radius);
cairo_fill(cairo);

set_source_hex_a(cairo, dark ? 0xffffff : 0x24395c, dark ? 0.08 : 0.10);
rounded_rect(cairo, lx + 0.5, ly + 0.5, lw - 1.0, lh - 1.0, style->popup_radius - 0.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

int power_hover = -1;
if (app->pointer_surface == panel->surface) {
power_hover = launcher_power_action_hit(panel, app->pointer_x, app->pointer_y);
}

double power_row_x = 0.0;
double power_row_y = 0.0;
double power_row_w = 0.0;
double power_row_h = 0.0;
double first_action_x, first_action_y, first_action_w, first_action_h;
double last_action_x, last_action_y, last_action_w, last_action_h;
if (launcher_power_action_rect(panel, 0,
&first_action_x, &first_action_y, &first_action_w, &first_action_h)
&& launcher_power_action_rect(panel,
(int)(sizeof(launcher_power_actions) / sizeof(launcher_power_actions[0])) - 1,
&last_action_x, &last_action_y, &last_action_w, &last_action_h)) {
power_row_x = first_action_x - 10.0;
power_row_y = first_action_y - 9.0;
power_row_w = (last_action_x + last_action_w) - first_action_x + 24.0;
power_row_h = first_action_h + 18.0;

set_source_hex_a(cairo, dark ? 0x0f1724 : 0xf7faff, dark ? 0.46 : 0.78);
rounded_rect(cairo, power_row_x, power_row_y, power_row_w, power_row_h, 18.0);
cairo_fill(cairo);

set_source_hex_a(cairo, dark ? 0xffffff : 0x22385d, dark ? 0.06 : 0.08);
rounded_rect(cairo, power_row_x + 0.5, power_row_y + 0.5, power_row_w - 1.0, power_row_h - 1.0, 17.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

set_source_hex_a(cairo, dark ? 0xffffff : 0xffffff, dark ? 0.05 : 0.20);
rounded_rect(cairo, power_row_x + 10.0, power_row_y + 7.0, power_row_w - 20.0, 9.0, 7.0);
cairo_fill(cairo);
}

for (int i = 0; i < (int)(sizeof(launcher_power_actions) / sizeof(launcher_power_actions[0])); i++) {
double px_action, py_action, pw_action, ph_action;
if (!launcher_power_action_rect(panel, i,
&px_action, &py_action, &pw_action, &ph_action)) {
continue;
}

set_source_hex_a(cairo, power_hover == i ? style->side_launcher_bg : (dark ? 0xffffff : 0x22385d),
power_hover == i ? 0.22 : (dark ? 0.09 : 0.06));
rounded_rect(cairo, px_action, py_action, pw_action, ph_action, 15.0);
cairo_fill(cairo);

set_source_hex_a(cairo, dark ? 0xffffff : 0x22385d, power_hover == i ? 0.14 : 0.07);
rounded_rect(cairo, px_action + 0.5, py_action + 0.5, pw_action - 1.0, ph_action - 1.0, 14.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

bool have_power_icon = draw_named_icon(cairo, app, launcher_power_actions[i].icon_name,
px_action + pw_action * 0.5, py_action + ph_action * 0.5, 20.0);
if (!have_power_icon) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
11.8, launcher_text, i == power_hover ? 0.98 : 0.78, px_action, py_action + 11.0,
(int)pw_action, PANGO_ALIGN_CENTER, launcher_power_actions[i].fallback_icon);
}
}

if (power_hover >= 0) {
double px_action, py_action, pw_action, ph_action;
if (launcher_power_action_rect(panel, power_hover,
&px_action, &py_action, &pw_action, &ph_action)) {
const char *tooltip = launcher_power_action_label(power_hover);
int tooltip_w = 0;
int tooltip_h = 0;
measure_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
10.2, tooltip, &tooltip_w, &tooltip_h);

double bubble_w = tooltip_w + 24.0;
double bubble_h = tooltip_h + 14.0;
double bubble_x = px_action + pw_action * 0.5 - bubble_w * 0.5;
double bubble_y = power_row_y - bubble_h - 10.0;
if (bubble_x < lx + 16.0) {
bubble_x = lx + 16.0;
}
if (bubble_x + bubble_w > lx + lw - 16.0) {
bubble_x = lx + lw - 16.0 - bubble_w;
}

set_source_hex_a(cairo, dark ? 0x111a29 : 0x22385d, dark ? 0.92 : 0.94);
rounded_rect(cairo, bubble_x, bubble_y, bubble_w, bubble_h, 12.0);
cairo_fill(cairo);

set_source_hex_a(cairo, 0xffffff, dark ? 0.10 : 0.14);
rounded_rect(cairo, bubble_x + 0.5, bubble_y + 0.5, bubble_w - 1.0, bubble_h - 1.0, 11.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
10.2, 0xffffff, 0.98, bubble_x + 12.0, bubble_y + 7.0,
(int)bubble_w - 24, PANGO_ALIGN_CENTER, tooltip);
}
}

double content_x, content_y, content_w, content_h;
launcher_content_rect(panel, &content_x, &content_y, &content_w, &content_h);

set_source_hex_a(cairo, dark ? 0x10192a : 0xffffff, dark ? 0.46 : 0.70);
rounded_rect(cairo, content_x, search_y, content_w, 38.0, 14.0);
cairo_fill(cairo);
set_source_hex_a(cairo, dark ? 0xffffff : 0x24395c, dark ? 0.07 : 0.10);
rounded_rect(cairo, content_x + 0.5, search_y + 0.5, content_w - 1.0, 37.0, 13.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

bool have_search_icon = draw_named_icon(cairo, app, "search",
content_x + 18.0, search_y + 19.0, 18.0);
if (!have_search_icon) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
11.6, launcher_subtle, 0.90, content_x + 16.0, search_y + 11.0,
16, PANGO_ALIGN_CENTER, "⌕");
}

const char *search_text = app->launcher_query;
if (!search_text[0] && !app->launcher_search_active) {
search_text = _("Search applications...");
}
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
11.1, launcher_text, search_text == app->launcher_query ? 0.96 : 0.58,
content_x + 34.0, search_y + 12.0,
(int)content_w - 48, PANGO_ALIGN_LEFT,
search_text);

size_t preview[8] = { 0 };
size_t preview_count = launcher_collect_favorite_preview(app, preview, G_N_ELEMENTS(preview));
if (preview_count > 0) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.0, launcher_text, 0.96, content_x, search_y + 54.0,
(int)content_w, PANGO_ALIGN_LEFT, _("Favorites"));

for (int i = 0; i < (int)preview_count; i++) {
double fx, fy, fw, fh;
if (!launcher_favorite_tile_rect(panel, app, i, &fx, &fy, &fw, &fh)) {
continue;
}

if (i == app->launcher_hover_favorite) {
    set_source_hex_a(cairo, style->side_launcher_bg, 0.20);
    rounded_rect(cairo, fx, fy, fw, fh, 12.0);
    cairo_fill(cairo);
}

const struct launcher_entry *fav = &app->launcher_entries[preview[i]];
bool have_icon = false;
double icon_size = 42.0;
double icon_y = fy + 6.0 + icon_size/2.0;
if (fav->icon_name[0]) {
have_icon = draw_named_icon(cairo, app, fav->icon_name, fx + fw/2.0, icon_y, icon_size);
}
if (!have_icon) {
draw_karton_symbol(cairo, 7, fx + fw/2.0, icon_y, 24.0, karton_symbol_color(7), 0.92);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
10.0, launcher_text, 0.96, fx - 4.0, fy + 58.0,
(int)fw + 8, PANGO_ALIGN_CENTER, fav->name);
}
}

double nav_x, nav_y, nav_w, nav_h;
double apps_x, apps_y, apps_w, apps_h;
launcher_category_panel_rect(panel, &nav_x, &nav_y, &nav_w, &nav_h);
launcher_apps_panel_rect(panel, &apps_x, &apps_y, &apps_w, &apps_h);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, launcher_subtle, 0.92, nav_x, list_y - 18.0,
(int)nav_w, PANGO_ALIGN_LEFT, _("Categories"));
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
10.0, launcher_subtle, 0.92, apps_x, list_y - 18.0,
(int)apps_w, PANGO_ALIGN_LEFT, _("All applications"));

for (int cat = 0; cat < LCAT_COUNT; cat++) {
double cx_chip, cy_chip, cw_chip, ch_chip;
if (!launcher_category_rect(panel, cat, &cx_chip, &cy_chip, &cw_chip, &ch_chip)) {
continue;
}

bool selected = cat == app->launcher_category;
set_source_hex_a(cairo,
selected ? style->side_launcher_bg : (dark ? 0xffffff : 0x24395c),
selected ? (dark ? 0.18 : 0.15) : (dark ? 0.05 : 0.04));
rounded_rect(cairo, cx_chip, cy_chip, cw_chip, ch_chip, 11.0);
cairo_fill(cairo);

bool have_cat_icon = draw_named_icon(cairo, app, launcher_category_icon_name(cat),
cx_chip + 15.0, cy_chip + ch_chip * 0.5, 16.0);
if (!have_cat_icon) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
11.5, selected ? style->side_launcher_bg : launcher_text,
selected ? 0.98 : 0.78, cx_chip + 4.0, cy_chip + 8.0,
22, PANGO_ALIGN_CENTER, launcher_category_icon(cat));
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
10.0, launcher_text, selected ? 0.98 : 0.88, cx_chip + 28.0, cy_chip + 8.0,
(int)cw_chip - 34, PANGO_ALIGN_LEFT, launcher_category_label(cat));
}

if (app->launcher_filtered_count == 0) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.5, launcher_text, 0.90, apps_x, list_y + 4.0,
(int)apps_w, PANGO_ALIGN_LEFT, _("No applications found"));
} else {
size_t start = (size_t)app->launcher_scroll_offset;
if (start > app->launcher_filtered_count) {
start = app->launcher_filtered_count;
}
size_t remaining = app->launcher_filtered_count - start;
size_t visible = remaining < (size_t)max_visible ? remaining : (size_t)max_visible;

for (size_t i = 0; i < visible; i++) {
int filtered_idx = (int)(start + i);
size_t entry_idx = app->launcher_filtered[start + i];
const struct launcher_entry *entry = &app->launcher_entries[entry_idx];
double row_y = list_y + i * row_h;

if (filtered_idx == app->launcher_hover_item || filtered_idx == app->launcher_selected) {
set_source_hex_a(cairo, style->side_launcher_bg,
filtered_idx == app->launcher_selected ? 0.16 : 0.10);
rounded_rect(cairo, apps_x, row_y + 2.0, apps_w, row_h - 4.0, 14.0);
cairo_fill(cairo);

set_source_hex_a(cairo, style->side_launcher_bg, 0.90);
rounded_rect(cairo, apps_x + 6.0, row_y + 8.0,
3.0, row_h - 16.0, 1.5);
cairo_fill(cairo);
}

bool have_icon = false;
if (entry->icon_name[0]) {
have_icon = draw_named_icon(cairo, app, entry->icon_name,
apps_x + 18.0, row_y + row_h * 0.5, 24.0);
}

if (!have_icon) {
draw_karton_symbol(cairo, 7, apps_x + 18.0, row_y + row_h * 0.5, 20.0,
karton_symbol_color(7), 0.92);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,
11.0, launcher_text, 0.96, apps_x + 36.0, row_y + 13.0,
(int)apps_w - 72, PANGO_ALIGN_LEFT, entry->name);

if (entry->favorite) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
12.0, 0xf4b400, 0.95, apps_x + apps_w - 26.0, row_y + 13.0,
18, PANGO_ALIGN_CENTER, "★");
}

set_source_hex_a(cairo, dark ? 0xffffff : 0x1f3352, dark ? 0.06 : 0.10);
cairo_rectangle(cairo, apps_x, row_y + row_h - 1.0, apps_w, 1.0);
cairo_fill(cairo);
}

size_t remaining_below = app->launcher_filtered_count - (start + visible);
if (remaining_below > 0) {
char more[64] = { 0 };
snprintf(more, sizeof(more), _("and %zu more..."), remaining_below);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, launcher_subtle, 0.92, apps_x, ly + lh - 82.0,
(int)apps_w, PANGO_ALIGN_LEFT, more);
}
}

if (app->launcher_menu_open) {
const double menu_w = 230.0;
const double row_h_menu = 28.0;
const int item_count = 8;
const char *menu_items[8] = {
launcher_menu_favorite_label(app),
_("Category: Internet"),
_("Category: Office"),
_("Category: Media"),
_("Category: Development"),
_("Category: System"),
_("Category: Utility"),
_("Uninstall local entry"),
};

set_source_hex_a(cairo, dark ? 0x1a202c : 0xf6f9ff, 0.98);
rounded_rect(cairo, app->launcher_menu_x, app->launcher_menu_y,
menu_w, item_count * row_h_menu + 8.0, 9.0);
cairo_fill(cairo);

for (int i = 0; i < item_count; i++) {
double item_y = app->launcher_menu_y + 4.0 + i * row_h_menu;
if (i == app->launcher_menu_hover) {
set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.55 : 0.28);
rounded_rect(cairo, app->launcher_menu_x + 4.0, item_y,
menu_w - 8.0, row_h_menu - 1.0, 6.0);
cairo_fill(cairo);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.0, launcher_text, 0.95, app->launcher_menu_x + 10.0, item_y + 8.0,
(int)menu_w - 20, PANGO_ALIGN_LEFT, menu_items[i]);
}
}
}
return;
}

if (app->popup_group >= 0) {
double px, py, pw, ph;
if (popup_geometry(panel, app, app->popup_group, &px, &py, &pw, &ph)) {
set_source_hex_a(cairo, style->side_popup_bg, dark ? 0.97 : 0.985);
rounded_rect(cairo, px, py, pw, ph, 18.0);
cairo_fill(cairo);
set_source_hex_a(cairo, dark ? 0xffffff : 0x24395c, dark ? 0.08 : 0.10);
rounded_rect(cairo, px + 0.5, py + 0.5, pw - 1.0, ph - 1.0, 17.5);
cairo_set_line_width(cairo, 1.0);
cairo_stroke(cairo);

const struct app_group *group = &app->groups[app->popup_group];
char group_label[96] = { 0 };
popup_group_label(group->app_id, group_label, sizeof(group_label));

set_source_hex_a(cairo, dark ? 0xffffff : 0xffffff, dark ? 0.06 : 0.24);
rounded_rect(cairo, px + 10.0, py + 10.0, 30.0, 30.0, 10.0);
cairo_fill(cairo);

if (!draw_group_app_icon(cairo, app, group, px + 25.0, py + 25.0, 18.0)) {
draw_karton_symbol(cairo, 7, px + 25.0, py + 25.0, 18.0, karton_symbol_color(7), 0.94);
}

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
11.4, popup_header, 0.97, px + 48.0, py + 13.0,
(int)pw - 78, PANGO_ALIGN_LEFT, group_label);

bool have_scroll_icon = draw_named_icon(cairo, app, "scroll",
px + 52.0, py + 34.0, 14.0);
if (!have_scroll_icon) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
8.6, popup_header, 0.68, px + 44.0, py + 28.0,
16, PANGO_ALIGN_CENTER, "↕");
}
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
8.8, popup_text, 0.62, px + 64.0, py + 28.0,
(int)pw - 124, PANGO_ALIGN_LEFT, _("Mouse wheel"));

char count_label[16] = { 0 };
snprintf(count_label, sizeof(count_label), "%zu", group->count);
set_source_hex_a(cairo, style->side_launcher_bg, dark ? 0.34 : 0.18);
rounded_rect(cairo, px + pw - 30.0, py + 14.0, 16.0, 14.0, 7.0);
cairo_fill(cairo);
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_BOLD,
8.4, popup_header, 0.88, px + pw - 30.0, py + 16.0,
16, PANGO_ALIGN_CENTER, count_label);

double row_h = 36.0;
double ry = py + 58.0;
for (size_t i = 0; i < group->count; i++) {
bool hovered = (int)i == app->popup_hover_item;
bool selected = (int)i == app->popup_selected_item;
const struct toplevel_entry *entry = &app->toplevels[group->indices[i]];
const char *title = entry->title[0] ? entry->title : app_id_base(entry->app_id);
if (hovered || selected) {
set_source_hex_a(cairo, 0x3a73d8,
hovered ? (dark ? 0.55 : 0.36) : (dark ? 0.36 : 0.24));
rounded_rect(cairo, px + 8.0, ry + i * row_h + 2.0,
pw - 16.0, row_h - 4.0, 10.0);
cairo_fill(cairo);
}
if (entry->minimized && !hovered && !selected) {
set_source_hex_a(cairo, dark ? 0xffffff : 0x24395c, dark ? 0.10 : 0.08);
rounded_rect(cairo, px + 8.0, ry + i * row_h + 2.0,
pw - 16.0, row_h - 4.0, 10.0);
cairo_fill(cairo);
}

set_source_hex_a(cairo,
entry->active && !entry->minimized ? style->side_launcher_bg : (dark ? 0xffffff : 0x24395c),
entry->active && !entry->minimized ? 0.90 : (entry->minimized ? 0.24 : 0.14));
cairo_arc(cairo, px + 20.0, ry + i * row_h + 18.0, 4.0, 0, 2.0 * 3.14159265358979323846);
cairo_fill(cairo);

draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_NORMAL,
10.4, popup_text, entry->minimized ? 0.68 : 0.96, px + 30.0, ry + i * row_h + 9.0,
(int)pw - 62, PANGO_ALIGN_LEFT, title);
if (entry->minimized) {
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_SEMIBOLD,
8.4, popup_text, 0.56, px + pw - 28.0, ry + i * row_h + 10.0,
16, PANGO_ALIGN_CENTER, "-");
}
}
}
}
}

static void
panel_draw(struct panel *panel)
{
struct app *app = panel->app;
if (!panel->configured || panel->width == 0 || panel->height == 0) {
return;
}

int scale = app->output_scale > 0 ? app->output_scale : 1;
uint32_t px_width = panel->width * scale;
uint32_t px_height = panel->height * scale;

struct pool_buffer *buffer = get_next_buffer(app->shm, panel->buffers,
px_width, px_height);
if (!buffer) {
return;
}

cairo_t *cairo = buffer->cairo;
cairo_save(cairo);
cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
cairo_set_source_rgba(cairo, 0, 0, 0, 0);
cairo_paint(cairo);
cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
cairo_scale(cairo, scale, scale);

if (panel->type == PANEL_TOP) {
render_top_panel(cairo, panel);
} else {
render_side_panel(cairo, panel);
}

cairo_restore(cairo);
cairo_surface_flush(buffer->surface);

wl_surface_set_buffer_scale(panel->surface, scale);
wl_surface_attach(panel->surface, buffer->buffer, 0, 0);
wl_surface_damage_buffer(panel->surface, 0, 0, INT_MAX, INT_MAX);
wl_surface_commit(panel->surface);
}

static void
update_side_hover(struct app *app)
{
int old_hovered = app->hovered_group;
int old_popup = app->popup_group;
int old_popup_item = app->popup_hover_item;
int old_popup_selected = app->popup_selected_item;
int old_launcher_hover = app->launcher_hover_item;
int old_launcher_favorite = app->launcher_hover_favorite;
int old_menu_hover = app->launcher_menu_hover;

app->hovered_group = -1;
app->launcher_hover_item = -1;
app->launcher_hover_favorite = -1;

if (app->pointer_surface != app->side.surface) {
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
app->launcher_menu_hover = -1;
if (old_popup != app->popup_group || old_launcher_hover != app->launcher_hover_item
|| old_launcher_favorite != app->launcher_hover_favorite) {
request_side_panel_size(app);
}
if (old_hovered != app->hovered_group
|| old_popup_item != app->popup_hover_item
|| old_popup_selected != app->popup_selected_item
|| old_launcher_hover != app->launcher_hover_item
|| old_launcher_favorite != app->launcher_hover_favorite
|| old_menu_hover != app->launcher_menu_hover) {
panel_draw(&app->side);
}
return;
}

if (app->launcher_open) {
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
app->launcher_hover_item = launcher_item_hit(&app->side, app,
app->pointer_x, app->pointer_y);
app->launcher_hover_favorite = launcher_favorite_tile_hit(&app->side, app,
app->pointer_x, app->pointer_y);
app->launcher_menu_hover = launcher_menu_item_hit(app,
app->pointer_x, app->pointer_y);

if (old_popup != app->popup_group) {
request_side_panel_size(app);
}
if (old_launcher_hover != app->launcher_hover_item
|| old_launcher_favorite != app->launcher_hover_favorite
|| old_popup != app->popup_group
|| old_hovered != app->hovered_group
|| old_popup_selected != app->popup_selected_item
|| old_menu_hover != app->launcher_menu_hover) {
panel_draw(&app->side);
}
return;
}

app->launcher_menu_hover = -1;
app->popup_hover_item = -1;

int slot = side_slot_hit(&app->side, app->pointer_x, app->pointer_y);
bool in_existing_popup = false;
int popup_item = -1;
if (old_popup >= 0) {
double px, py, pw, ph;
if (popup_geometry(&app->side, app, old_popup, &px, &py, &pw, &ph)
&& point_in_rect(app->pointer_x, app->pointer_y, px, py, pw, ph)) {
in_existing_popup = true;
popup_item = popup_item_hit(&app->side, app, old_popup,
app->pointer_x, app->pointer_y);
}
}

if (slot > 0) {
int group_idx = slot - 1;
if (group_idx >= 0 && group_idx < (int)app->group_count) {
app->hovered_group = group_idx;
if (app->groups[group_idx].count > 1) {
app->popup_group = group_idx;
popup_clamp_selection(app);
if (old_popup != group_idx || app->popup_selected_item < 0) {
app->popup_selected_item = popup_preferred_item(app, group_idx);
}
} else {
app->popup_group = -1;
app->popup_selected_item = -1;
}
} else {
app->popup_group = -1;
app->popup_selected_item = -1;
}
} else if (in_existing_popup && old_popup >= 0 && old_popup < (int)app->group_count
&& app->groups[old_popup].count > 1) {
app->hovered_group = old_popup;
app->popup_group = old_popup;
popup_clamp_selection(app);
app->popup_hover_item = popup_item;
} else {
app->popup_group = -1;
app->popup_selected_item = -1;
}

if (old_popup != app->popup_group) {
request_side_panel_size(app);
}

if (old_hovered != app->hovered_group
|| old_popup != app->popup_group
|| old_popup_item != app->popup_hover_item
|| old_popup_selected != app->popup_selected_item
|| old_launcher_hover != app->launcher_hover_item
|| old_launcher_favorite != app->launcher_hover_favorite
|| old_menu_hover != app->launcher_menu_hover) {
panel_draw(&app->side);
}
}

static void
top_popup_close(struct app *app)
{
app->quick_open = false;
app->top_popup_mode = TOP_POPUP_NONE;
app->clock_picker_open = false;
app->quick_menu_tile = -1;
app->quick_menu_hover_item = -1;
request_top_panel_size(app);
panel_draw(&app->top);
}

static void
top_popup_open(struct app *app, int mode)
{
if (mode == TOP_POPUP_CALENDAR) {
calendar_load_items(app);
}
if (mode == TOP_POPUP_CLOCK) {
clock_load_timezones(app);
app->clock_picker_open = false;
}
if (mode == TOP_POPUP_QUICK
|| mode == TOP_POPUP_NETWORK
|| mode == TOP_POPUP_AUDIO
|| mode == TOP_POPUP_POWER) {
refresh_quick_status(app);
}
if (mode == TOP_POPUP_NOTIFICATIONS) {
load_system_notifications(app);
}

app->quick_open = true;
app->top_popup_mode = mode;
app->quick_menu_tile = -1;
app->quick_menu_hover_item = -1;
request_top_panel_size(app);
panel_draw(&app->top);
}

static void
handle_top_click(struct app *app)
{
if (app->launcher_open) {
launcher_close(app);
}

int global_top = top_global_menu_hit(&app->top, app->pointer_x, app->pointer_y);
if (app->global_menu_available && global_top >= 0) {
if (app->quick_open) {
top_popup_close(app);
}

if (app->global_menu_open && app->global_menu_open_top == global_top) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
} else {
app->global_menu_open = true;
app->global_menu_open_top = global_top;
}

panel_draw(&app->top);
return;
}

if (app->global_menu_open && app->global_menu_available && app->global_menu_open_top >= 0) {
int global_item = top_global_menu_popup_item_hit(&app->top,
app->global_menu_open_top,
app->pointer_x,
app->pointer_y);
if (global_item >= 0) {
global_menu_activate_item(app, app->global_menu_open_top, global_item);
app->global_menu_open = false;
app->global_menu_open_top = -1;
panel_draw(&app->top);
return;
}

double gx, gy, gw, gh;
bool in_popup = top_global_menu_popup_rect(&app->top, app->global_menu_open_top,
&gx, &gy, &gw, &gh)
&& point_in_rect(app->pointer_x, app->pointer_y, gx, gy, gw, gh);
if (!in_popup) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
panel_draw(&app->top);
}
}

double bx, by, bw, bh;
top_quick_button_rect(&app->top, &bx, &by, &bw, &bh);

double dx, dy, dw, dh;
top_date_rect(&app->top, &dx, &dy, &dw, &dh);

double tx, ty, tw, th;
top_time_rect(&app->top, &tx, &ty, &tw, &th);

int status_icon = top_status_icon_hit(&app->top, app->pointer_x, app->pointer_y);
if (status_icon == 0) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_NOTIFICATIONS);
}
return;
}
if (status_icon == 1) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_NETWORK) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_NETWORK);
}
return;
}
if (status_icon == 2) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_AUDIO) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_AUDIO);
}
return;
}
if (status_icon == 3) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_POWER) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_POWER);
}
return;
}

if (point_in_rect(app->pointer_x, app->pointer_y, bx, by, bw, bh)) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_QUICK) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_QUICK);
}
return;
}

if (point_in_rect(app->pointer_x, app->pointer_y, dx, dy, dw, dh)) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_CALENDAR) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_CALENDAR);
}
return;
}

if (point_in_rect(app->pointer_x, app->pointer_y, tx, ty, tw, th)) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
if (app->quick_open && app->top_popup_mode == TOP_POPUP_CLOCK) {
top_popup_close(app);
} else {
top_popup_open(app, TOP_POPUP_CLOCK);
}
return;
}

if (!app->quick_open) {
return;
}

double qx, qy, qw, qh;
bool have_rect = top_popup_rect_for_mode(&app->top, app->top_popup_mode, &qx, &qy, &qw, &qh);

if (have_rect
&& app->pointer_y > app->style.top_height
&& !point_in_rect(app->pointer_x, app->pointer_y, qx, qy, qw, qh)) {
top_popup_close(app);
return;
}

if (app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
int action = notification_action_hit(&app->top, app->pointer_x, app->pointer_y);
if (action == 0) {
app->notifications_read = true;
panel_draw(&app->top);
return;
}
if (action == 1) {
app->notifications_cleared = true;
app->notifications_read = true;
app->notification_count = 0;
app->notifications_updated = 0;
clear_notifications_cache();
panel_draw(&app->top);
return;
}
int item = notification_item_hit(&app->top, app->pointer_x, app->pointer_y);
if (item >= 0 && item < (int)app->notification_count) {
app->notifications_read = true;
notification_activate(app, &app->notifications[item]);
if (app->notifications[item].target_kind != NOTIFICATION_TARGET_NONE) {
top_popup_close(app);
} else {
panel_draw(&app->top);
}
return;
}
}

if (app->top_popup_mode == TOP_POPUP_QUICK || app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
int header_icon = quick_header_icon_hit(&app->top, app->pointer_x, app->pointer_y);
if (app->top_popup_mode == TOP_POPUP_NOTIFICATIONS && header_icon == 3) {
if (app->top_popup_mode == TOP_POPUP_NOTIFICATIONS) {
top_popup_open(app, TOP_POPUP_QUICK);
} else {
top_popup_open(app, TOP_POPUP_NOTIFICATIONS);
}
return;
}
}

if (app->top_popup_mode == TOP_POPUP_NETWORK) {
double nx, ny, nw, nh;
if (top_quick_panel_rect(&app->top, &nx, &ny, &nw, &nh)) {
    int network = network_popup_item_hit(&app->top, app, app->pointer_x, app->pointer_y);
    if (network >= 0 && network < (int)app->quick_network_count) {
        connect_wifi_network(app, app->quick_networks[network]);
        panel_draw(&app->top);
        return;
    }
double button_y = ny + nh - 50.0;
double button_w = (nw - 42.0) / 2.0;
if (point_in_rect(app->pointer_x, app->pointer_y, nx + 16.0, button_y, button_w, 34.0)
|| point_in_rect(app->pointer_x, app->pointer_y, nx + 26.0 + button_w, button_y, button_w, 34.0)) {
open_settings_page("network");
return;
}
}
return;
}

if (app->top_popup_mode == TOP_POPUP_AUDIO) {
double ax, ay, aw, ah;
if (top_quick_panel_rect(&app->top, &ax, &ay, &aw, &ah)) {
double slider_pct = 0.0;
int slider = audio_popup_slider_hit(&app->top, app->pointer_x, app->pointer_y, &slider_pct);
if (slider >= 0) {
    int value = (int)(slider_pct * 100.0 + 0.5);
    if (slider == 0) {
        app->top_slider_drag_target = TOP_SLIDER_DRAG_AUDIO_OUTPUT;
        set_audio_volume_value(app, false, value);
    } else {
        app->top_slider_drag_target = TOP_SLIDER_DRAG_AUDIO_INPUT;
        set_audio_volume_value(app, true, value);
    }
    panel_draw(&app->top);
    return;
}
    int output_idx = audio_popup_device_hit(&app->top, app, false, app->pointer_x, app->pointer_y);
    if (output_idx >= 0 && output_idx < (int)app->quick_output_count
    && (!app->quick_default_output[0] || strcmp(app->quick_outputs[output_idx], app->quick_default_output))) {
        if (set_audio_default_device(app, false, (size_t)output_idx)) {
            panel_draw(&app->top);
        }
        return;
    }
    int input_idx = audio_popup_device_hit(&app->top, app, true, app->pointer_x, app->pointer_y);
    if (input_idx >= 0 && input_idx < (int)app->quick_input_count
    && (!app->quick_default_input[0] || strcmp(app->quick_inputs[input_idx], app->quick_default_input))) {
        if (set_audio_default_device(app, true, (size_t)input_idx)) {
            panel_draw(&app->top);
        }
        return;
    }
    if (point_in_rect(app->pointer_x, app->pointer_y, ax + 16.0, ay + ah - 50.0, aw - 32.0, 34.0)) {
        open_settings_page("audio");
        return;
    }
}
return;
}

if (app->top_popup_mode == TOP_POPUP_POWER) {
double px, py, pw, ph;
if (top_quick_panel_rect(&app->top, &px, &py, &pw, &ph)) {
double action_w = (pw - 42.0) / 2.0;
double action_h = 42.0;
double action_y = py + 136.0;
for (int i = 0; i < 4; i++) {
int row = i / 2;
int col = i % 2;
double rx = px + 16.0 + col * (action_w + 10.0);
double ry = action_y + row * (action_h + 10.0);
if (point_in_rect(app->pointer_x, app->pointer_y, rx, ry, action_w, action_h)) {
spawn_command(launcher_power_actions[i].command);
top_popup_close(app);
return;
}
}
if (point_in_rect(app->pointer_x, app->pointer_y, px + 16.0, py + ph - 50.0, pw - 32.0, 34.0)) {
open_settings_page("power");
return;
}
        int removable = power_removable_item_hit(&app->top, app, app->pointer_x, app->pointer_y);
        if (removable >= 0 && removable < (int)app->quick_removable_count) {
            open_path_location(app->quick_removable_paths[removable]);
            return;
        }
}
return;
}

if (app->top_popup_mode == TOP_POPUP_QUICK) {

int menu_item = quick_tile_menu_item_hit(&app->top, app, app->pointer_x, app->pointer_y);
if (app->quick_menu_tile >= 0 && menu_item >= 0) {
quick_apply_tile_menu_action(app, app->quick_menu_tile, menu_item);
return;
}

int arrow_tile = quick_tile_arrow_hit(&app->top, app->pointer_x, app->pointer_y);
if (arrow_tile >= 0) {
if (app->quick_menu_tile == arrow_tile) {
app->quick_menu_tile = -1;
app->quick_menu_hover_item = -1;
} else {
app->quick_menu_tile = arrow_tile;
app->quick_menu_hover_item = -1;
}
panel_draw(&app->top);
return;
}

double slider_pct = 0.0;
int slider = quick_slider_hit(&app->top, app->pointer_x, app->pointer_y, &slider_pct);
if (slider >= 0) {
int value = (int)(slider_pct * 100.0 + 0.5);
if (slider == 0) {
    app->top_slider_drag_target = TOP_SLIDER_DRAG_QUICK_BRIGHTNESS;
    set_quick_brightness(app, value);
} else {
    app->top_slider_drag_target = TOP_SLIDER_DRAG_QUICK_VOLUME;
    set_audio_volume_value(app, false, value);
}
panel_draw(&app->top);
return;
}

int tile = quick_tile_hit(&app->top, app->pointer_x, app->pointer_y);
if (tile >= 0 && tile < 4) {
app->quick_menu_tile = -1;
app->quick_menu_hover_item = -1;
if (tile == 0) {
open_settings_page("network");
} else if (tile == 1) {
open_settings_page("bluetooth");
} else if (tile == 2) {
quick_cycle_theme_mode(app);
} else if (tile == 3) {
app->dnd_enabled = !app->dnd_enabled;
}
refresh_quick_status(app);
panel_draw(&app->top);
if (app->side_enabled) {
panel_draw(&app->side);
}
return;
}
}

if (app->top_popup_mode == TOP_POPUP_CALENDAR) {
int nav = calendar_nav_hit(&app->top, app->pointer_x, app->pointer_y);
if (nav != 0) {
if (nav == -2) {
app->calendar_year--;
} else if (nav == 2) {
app->calendar_year++;
} else if (nav == -1) {
app->calendar_month--;
if (app->calendar_month < 1) {
app->calendar_month = 12;
app->calendar_year--;
}
} else if (nav == 1) {
app->calendar_month++;
if (app->calendar_month > 12) {
app->calendar_month = 1;
app->calendar_year++;
}
}

int max_day = days_in_month(app->calendar_year, app->calendar_month);
if (app->calendar_selected_day > max_day) {
app->calendar_selected_day = max_day;
}
panel_draw(&app->top);
return;
}

int day = calendar_day_hit(&app->top, app, app->pointer_x, app->pointer_y);
if (day > 0) {
app->calendar_selected_day = day;
panel_draw(&app->top);
return;
}
}

if (app->top_popup_mode == TOP_POPUP_CLOCK) {
if (clock_add_hit(&app->top, app->pointer_x, app->pointer_y)) {
app->clock_picker_open = !app->clock_picker_open;
panel_draw(&app->top);
return;
}

if (app->clock_picker_open) {
int pick = clock_picker_item_hit(&app->top, app, app->pointer_x, app->pointer_y);
if (pick >= 0
&& pick < (int)(sizeof(clock_timezone_candidates) / sizeof(clock_timezone_candidates[0]))) {
clock_add_timezone(app, clock_timezone_candidates[pick]);
app->clock_picker_open = false;
panel_draw(&app->top);
return;
}
}

int rm = clock_remove_hit(&app->top, app, app->pointer_x, app->pointer_y);
if (rm >= 0 && rm < (int)app->clock_timezone_count) {
for (size_t i = (size_t)rm; i + 1 < app->clock_timezone_count; i++) {
memmove(app->clock_timezones[i], app->clock_timezones[i + 1],
sizeof(app->clock_timezones[i]));
}
app->clock_timezone_count--;
clock_save_timezones(app);
panel_draw(&app->top);
return;
}
}

if (app->global_menu_open) {
double gx, gy, gw, gh;
bool in_popup = top_global_menu_popup_rect(&app->top, app->global_menu_open_top,
&gx, &gy, &gw, &gh)
&& point_in_rect(app->pointer_x, app->pointer_y, gx, gy, gw, gh);
if (!in_popup) {
app->global_menu_open = false;
app->global_menu_open_top = -1;
panel_draw(&app->top);
}
}

app->quick_menu_tile = -1;
app->quick_menu_hover_item = -1;
}

static void
launcher_close(struct app *app)
{
app->launcher_open = false;
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
app->launcher_hover_item = -1;
app->launcher_hover_favorite = -1;
app->launcher_search_active = false;
app->launcher_menu_open = false;
app->launcher_menu_target = -1;
app->launcher_menu_hover = -1;
request_side_panel_size(app);
panel_draw(&app->side);
}

static void
launcher_activate_filtered(struct app *app, int filtered_idx)
{
if (filtered_idx < 0 || filtered_idx >= (int)app->launcher_filtered_count) {
return;
}
size_t entry_idx = app->launcher_filtered[filtered_idx];
if (entry_idx >= app->launcher_count) {
return;
}

launch_desktop_id(app->launcher_entries[entry_idx].desktop_id);
launcher_close(app);
}

static void
launcher_uninstall_local_entry(struct app *app, int filtered_idx)
{
if (filtered_idx < 0 || filtered_idx >= (int)app->launcher_filtered_count) {
return;
}
size_t entry_idx = app->launcher_filtered[filtered_idx];
if (entry_idx >= app->launcher_count) {
return;
}

const struct launcher_entry *entry = &app->launcher_entries[entry_idx];
if (!entry->local_entry || !entry->desktop_path[0]) {
return;
}

if (unlink(entry->desktop_path) != 0) {
fprintf(stderr, _("karton-shell: cannot remove %s: %s\n"),
entry->desktop_path, strerror(errno));
return;
}

load_launcher_entries(app);
if (app->launcher_menu_target >= (int)app->launcher_filtered_count) {
app->launcher_menu_target = (int)app->launcher_filtered_count - 1;
}
panel_draw(&app->side);
}

static void
launcher_apply_menu_action(struct app *app, int action)
{
if (app->launcher_menu_target < 0
|| app->launcher_menu_target >= (int)app->launcher_filtered_count) {
return;
}

size_t entry_idx = app->launcher_filtered[app->launcher_menu_target];
if (entry_idx >= app->launcher_count) {
return;
}
const struct launcher_entry *entry = &app->launcher_entries[entry_idx];

switch (action) {
case 0:
launcher_set_favorite(app, entry->desktop_id, !entry->favorite);
break;
case 1:
launcher_set_category_override(app, entry->desktop_id, LCAT_INTERNET);
break;
case 2:
launcher_set_category_override(app, entry->desktop_id, LCAT_OFFICE);
break;
case 3:
launcher_set_category_override(app, entry->desktop_id, LCAT_MEDIA);
break;
case 4:
launcher_set_category_override(app, entry->desktop_id, LCAT_DEVELOPMENT);
break;
case 5:
launcher_set_category_override(app, entry->desktop_id, LCAT_SYSTEM);
break;
case 6:
launcher_set_category_override(app, entry->desktop_id, LCAT_UTILITY);
break;
case 7:
launcher_uninstall_local_entry(app, app->launcher_menu_target);
break;
default:
break;
}

app->launcher_menu_open = false;
app->launcher_menu_hover = -1;
panel_draw(&app->side);
}

static void
handle_side_click(struct app *app, uint32_t button)
{
if (app->pointer_surface != app->side.surface) {
return;
}

int slot = side_slot_hit(&app->side, app->pointer_x, app->pointer_y);

if (app->launcher_open) {
if (button == BTN_LEFT) {
app->launcher_search_active = launcher_search_hit(&app->side,
app->pointer_x, app->pointer_y);
}

if (button == BTN_LEFT) {
int menu_item = launcher_menu_item_hit(app, app->pointer_x, app->pointer_y);
if (menu_item >= 0) {
launcher_apply_menu_action(app, menu_item);
return;
}
}

if (slot == 0) {
launcher_close(app);
return;
}

if (button == BTN_LEFT) {
int power_action = launcher_power_action_hit(&app->side,
app->pointer_x, app->pointer_y);
if (power_action >= 0) {
launcher_run_power_action(app, power_action);
return;
}
}

if (button == BTN_LEFT) {
size_t preview[8] = { 0 };
size_t preview_count = launcher_collect_favorite_preview(app, preview, G_N_ELEMENTS(preview));
int favorite_tile = launcher_favorite_tile_hit(&app->side, app,
app->pointer_x, app->pointer_y);
if (favorite_tile >= 0 && favorite_tile < (int)preview_count) {
int filtered_idx = launcher_filtered_index_from_entry(app, preview[favorite_tile]);
if (filtered_idx >= 0) {
app->launcher_selected = filtered_idx;
launcher_activate_filtered(app, filtered_idx);
} else {
launch_desktop_id(app->launcher_entries[preview[favorite_tile]].desktop_id);
launcher_close(app);
}
return;
}
}

int cat = launcher_category_hit(&app->side, app->pointer_x, app->pointer_y);
if (cat >= 0 && button == BTN_LEFT) {
app->launcher_category = cat;
app->launcher_selected = 0;
app->launcher_scroll_offset = 0;
launcher_rebuild_filtered(app);
panel_draw(&app->side);
return;
}

int launcher_item = launcher_item_hit(&app->side, app,
app->pointer_x, app->pointer_y);
if (launcher_item >= 0 && launcher_item < (int)app->launcher_filtered_count) {
app->launcher_selected = launcher_item;
if (button == BTN_LEFT) {
launcher_activate_filtered(app, launcher_item);
return;
}
if (button == BTN_RIGHT) {
app->launcher_menu_open = true;
app->launcher_menu_target = launcher_item;
app->launcher_menu_x = app->pointer_x + 8.0;
app->launcher_menu_y = app->pointer_y + 6.0;
double max_x = app->side.width - 240.0;
double max_y = app->side.height - 240.0;
if (app->launcher_menu_x > max_x) {
app->launcher_menu_x = max_x;
}
if (app->launcher_menu_y > max_y) {
app->launcher_menu_y = max_y;
}
if (app->launcher_menu_x < app->style.side_width + 12.0) {
app->launcher_menu_x = app->style.side_width + 12.0;
}
if (app->launcher_menu_y < 12.0) {
app->launcher_menu_y = 12.0;
}
panel_draw(&app->side);
return;
}
}

if (button == BTN_LEFT && app->launcher_menu_open) {
app->launcher_menu_open = false;
app->launcher_menu_hover = -1;
panel_draw(&app->side);
return;
}

double lx, ly, lw, lh;
if (!launcher_geometry(&app->side, &lx, &ly, &lw, &lh)
|| !point_in_rect(app->pointer_x, app->pointer_y, lx, ly, lw, lh)) {
launcher_close(app);
}
return;
}

if (button != BTN_LEFT) {
return;
}

int popup_item = popup_item_hit(&app->side, app, app->popup_group,
app->pointer_x, app->pointer_y);
if (app->popup_group >= 0 && popup_item >= 0) {
struct app_group *group = &app->groups[app->popup_group];
struct toplevel_entry *entry = &app->toplevels[group->indices[popup_item]];
toggle_entry(app, entry);
app->quick_open = false;
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
request_side_panel_size(app);
panel_draw(&app->top);
panel_draw(&app->side);
return;
}

if (slot == 0) {
app->launcher_open = true;
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
app->launcher_query[0] = '\0';
app->launcher_search_active = false;
app->launcher_category = LCAT_ALL;
app->launcher_selected = 0;
app->launcher_scroll_offset = 0;
app->launcher_hover_favorite = -1;
app->launcher_menu_open = false;
app->launcher_menu_target = -1;
app->launcher_menu_hover = -1;
launcher_rebuild_filtered(app);
request_side_panel_size(app);
panel_draw(&app->side);
return;
}
if (slot < 1) {
return;
}

int group_idx = slot - 1;
if (group_idx < 0 || group_idx >= (int)app->group_count) {
return;
}

struct app_group *group = &app->groups[group_idx];
if (group->count == 0) {
return;
}

if (group->count == 1) {
struct toplevel_entry *entry = &app->toplevels[group->indices[0]];
toggle_entry(app, entry);
return;
}

if (app->popup_group == group_idx) {
int target_item = app->popup_hover_item >= 0
? app->popup_hover_item
: app->popup_selected_item;
if (target_item < 0 || target_item >= (int)group->count) {
target_item = popup_preferred_item(app, group_idx);
}
struct toplevel_entry *entry = &app->toplevels[group->indices[target_item]];
toggle_entry(app, entry);
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
request_side_panel_size(app);
panel_draw(&app->side);
} else {
app->popup_group = group_idx;
app->popup_hover_item = -1;
app->popup_selected_item = popup_preferred_item(app, group_idx);
request_side_panel_size(app);
panel_draw(&app->side);
}
}

static void
layer_surface_configure(void *data,
struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1,
uint32_t serial, uint32_t width, uint32_t height)
{
struct panel *panel = data;

zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);

if (panel->type == PANEL_TOP) {
if (height == 0) {
height = panel->app->quick_open
? panel->app->style.top_expanded_height
: panel->app->style.top_height;
}
} else {
if (width == 0) {
width = panel->app->popup_group >= 0
? panel->app->style.side_expanded_width
: panel->app->style.side_width;
if (panel->app->launcher_open) {
width = launcher_panel_width(panel->app);
}
}
}

panel->width = width;
panel->height = height;
panel->configured = true;
panel_draw(panel);
}

static void
layer_surface_closed(void *data,
struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1)
{
struct panel *panel = data;
(void)zwlr_layer_surface_v1;
panel->app->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
.configure = layer_surface_configure,
.closed = layer_surface_closed,
};

static void
output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
int32_t physical_width, int32_t physical_height, int32_t subpixel,
const char *make, const char *model, int32_t transform)
{
(void)data;
(void)output;
(void)x;
(void)y;
(void)physical_width;
(void)physical_height;
(void)subpixel;
(void)make;
(void)model;
(void)transform;
}

static void
output_mode(void *data, struct wl_output *output, uint32_t flags,
int32_t width, int32_t height, int32_t refresh)
{
(void)output;
(void)refresh;
struct app *app = data;
(void)output;
(void)width;

if (!app) {
return;
}

if ((flags & WL_OUTPUT_MODE_CURRENT) && height > 0) {
app->output_height = (uint32_t)height;
}
}

static void
output_done(void *data, struct wl_output *output)
{
(void)data;
(void)output;
}

static void
output_scale(void *data, struct wl_output *output, int32_t factor)
{
struct app *app = data;
(void)output;
if (factor < 1) {
factor = 1;
}
app->output_scale = factor;
trigger_redraw(app);
}

static void
output_name(void *data, struct wl_output *output, const char *name)
{
(void)data;
(void)output;
(void)name;
}

static void
output_description(void *data, struct wl_output *output,
const char *description)
{
(void)data;
(void)output;
(void)description;
}

static const struct wl_output_listener output_listener = {
.geometry = output_geometry,
.mode = output_mode,
.done = output_done,
.scale = output_scale,
.name = output_name,
.description = output_description,
};

static void
pointer_enter(void *data, struct wl_pointer *wl_pointer,
uint32_t serial, struct wl_surface *surface,
wl_fixed_t sx, wl_fixed_t sy)
{
struct app *app = data;
(void)wl_pointer;
app->pointer_serial = serial;
app->pointer_surface = surface;
app->pointer_x = wl_fixed_to_double(sx);
app->pointer_y = wl_fixed_to_double(sy);
if (surface == app->top.surface && app->quick_open && app->top_popup_mode == TOP_POPUP_QUICK) {
app->quick_hover_tile = quick_tile_hit(&app->top, app->pointer_x, app->pointer_y);
app->quick_menu_hover_item = quick_tile_menu_item_hit(&app->top, app, app->pointer_x, app->pointer_y);
panel_draw(&app->top);
}
update_side_hover(app);
}

static void
pointer_leave(void *data, struct wl_pointer *wl_pointer,
uint32_t serial, struct wl_surface *surface)
{
struct app *app = data;
(void)wl_pointer;
(void)serial;
(void)surface;
app->pointer_surface = NULL;
app->hovered_group = -1;
app->popup_group = -1;
app->popup_hover_item = -1;
app->popup_selected_item = -1;
app->launcher_hover_item = -1;
app->launcher_hover_favorite = -1;
app->launcher_menu_hover = -1;
app->quick_hover_tile = -1;
app->quick_menu_hover_item = -1;
app->top_slider_drag_target = TOP_SLIDER_DRAG_NONE;
if (app->launcher_open) {
launcher_close(app);
return;
}
request_side_panel_size(app);
panel_draw(&app->side);
}

static void
pointer_motion(void *data, struct wl_pointer *wl_pointer,
uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
struct app *app = data;
(void)wl_pointer;
(void)time;
app->pointer_x = wl_fixed_to_double(sx);
app->pointer_y = wl_fixed_to_double(sy);
if (app->pointer_surface == app->top.surface && app->top_slider_drag_target != TOP_SLIDER_DRAG_NONE) {
    double slider_pct = 0.0;
    if (app->top_slider_drag_target == TOP_SLIDER_DRAG_QUICK_BRIGHTNESS
    || app->top_slider_drag_target == TOP_SLIDER_DRAG_QUICK_VOLUME) {
        if (quick_slider_hit(&app->top, app->pointer_x, app->pointer_y, &slider_pct) >= 0) {
            int value = (int)(slider_pct * 100.0 + 0.5);
            if (app->top_slider_drag_target == TOP_SLIDER_DRAG_QUICK_BRIGHTNESS) {
                set_quick_brightness(app, value);
            } else {
                set_audio_volume_value(app, false, value);
            }
            panel_draw(&app->top);
        }
    } else if (audio_popup_slider_hit(&app->top, app->pointer_x, app->pointer_y, &slider_pct) >= 0) {
        int value = (int)(slider_pct * 100.0 + 0.5);
        if (app->top_slider_drag_target == TOP_SLIDER_DRAG_AUDIO_OUTPUT) {
            set_audio_volume_value(app, false, value);
        } else if (app->top_slider_drag_target == TOP_SLIDER_DRAG_AUDIO_INPUT) {
            set_audio_volume_value(app, true, value);
        }
        panel_draw(&app->top);
    }
}
if (app->pointer_surface == app->top.surface && app->quick_open && app->top_popup_mode == TOP_POPUP_QUICK) {
int old_hover = app->quick_hover_tile;
int old_menu_hover = app->quick_menu_hover_item;
app->quick_hover_tile = quick_tile_hit(&app->top, app->pointer_x, app->pointer_y);
app->quick_menu_hover_item = quick_tile_menu_item_hit(&app->top, app, app->pointer_x, app->pointer_y);
if (old_hover != app->quick_hover_tile || old_menu_hover != app->quick_menu_hover_item) {
panel_draw(&app->top);
}
}
update_side_hover(app);
}

static void
pointer_button(void *data, struct wl_pointer *wl_pointer,
uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
struct app *app = data;
(void)wl_pointer;
(void)time;
app->pointer_serial = serial;
if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
        app->top_slider_drag_target = TOP_SLIDER_DRAG_NONE;
    }
return;
}

if (app->pointer_surface == app->top.surface) {
if (button == BTN_LEFT) {
handle_top_click(app);
} else {
if (app->quick_open) {
top_popup_close(app);
}
if (app->launcher_open) {
launcher_close(app);
}
}
return;
}
if (app->pointer_surface == app->side.surface) {
if (button == BTN_LEFT || button == BTN_RIGHT) {
handle_side_click(app, button);
}
}
}

static void
pointer_axis(void *data, struct wl_pointer *wl_pointer,
uint32_t time, uint32_t axis, wl_fixed_t value)
{
struct app *app = data;
(void)wl_pointer;
(void)time;

if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
return;
}

if (app->pointer_surface == app->top.surface) {
    double amount = wl_fixed_to_double(value);
    int step = amount > 0.0 ? -5 : (amount < 0.0 ? 5 : 0);
    if (step == 0) {
        return;
    }

    if (app->quick_open && app->top_popup_mode == TOP_POPUP_AUDIO) {
        double slider_pct = 0.0;
        int slider = audio_popup_slider_hit(&app->top, app->pointer_x, app->pointer_y, &slider_pct);
        if (slider == 0) {
            set_audio_volume_value(app, false, app->quick_volume + step);
            panel_draw(&app->top);
            return;
        }
        if (slider == 1) {
            set_audio_volume_value(app, true, app->quick_input_volume + step);
            panel_draw(&app->top);
            return;
        }
    }

    if (app->quick_open && app->top_popup_mode == TOP_POPUP_QUICK) {
        double slider_pct = 0.0;
        int slider = quick_slider_hit(&app->top, app->pointer_x, app->pointer_y, &slider_pct);
        if (slider == 0) {
            set_quick_brightness(app, app->quick_brightness + step);
            panel_draw(&app->top);
            return;
        }
        if (slider == 1) {
            set_audio_volume_value(app, false, app->quick_volume + step);
            panel_draw(&app->top);
            return;
        }
    }

    return;
}

if (app->pointer_surface != app->side.surface
|| app->launcher_menu_open) {
    return;
}

if (!app->launcher_open) {
int popup_group = app->popup_group;
if (popup_group < 0) {
int slot = side_slot_hit(&app->side, app->pointer_x, app->pointer_y);
if (slot > 0) {
int group_idx = slot - 1;
if (group_idx >= 0 && group_idx < (int)app->group_count
&& app->groups[group_idx].count > 1) {
popup_group = group_idx;
}
}
}

if (popup_group >= 0 && popup_group < (int)app->group_count
&& app->groups[popup_group].count > 1) {
double px, py, pw, ph;
bool over_popup = popup_geometry(&app->side, app, popup_group,
&px, &py, &pw, &ph)
&& point_in_rect(app->pointer_x, app->pointer_y, px, py, pw, ph);
int slot = side_slot_hit(&app->side, app->pointer_x, app->pointer_y);
bool over_group_slot = slot > 0 && (slot - 1) == popup_group;
if (over_popup || over_group_slot) {
app->popup_group = popup_group;
popup_clamp_selection(app);
popup_scroll_by(app, wl_fixed_to_double(value) > 0.0 ? 1 : -1);
return;
}
}

return;
}

double x, y, w, h, search_y, chips_y, list_y, row_h;
int max_visible = 0;
if (!launcher_geometry(&app->side, &x, &y, &w, &h)) {
return;
}

launcher_layout(&app->side, &x, &y, &w, &h,
&search_y, &chips_y, &list_y, &row_h, &max_visible);
if (!point_in_rect(app->pointer_x, app->pointer_y,
x, list_y, w, row_h * (double)max_visible)) {
return;
}

double amount = wl_fixed_to_double(value);
if (amount > 0.0) {
launcher_scroll_by(app, 1);
} else if (amount < 0.0) {
launcher_scroll_by(app, -1);
}
}

static void
pointer_frame(void *data, struct wl_pointer *wl_pointer)
{
(void)data;
(void)wl_pointer;
}

static void
pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
uint32_t axis_source)
{
(void)data;
(void)wl_pointer;
(void)axis_source;
}

static void
pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
uint32_t time, uint32_t axis)
{
(void)data;
(void)wl_pointer;
(void)time;
(void)axis;
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
uint32_t axis, int32_t discrete)
{
(void)data;
(void)wl_pointer;
(void)axis;
(void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
.enter = pointer_enter,
.leave = pointer_leave,
.motion = pointer_motion,
.button = pointer_button,
.axis = pointer_axis,
.frame = pointer_frame,
.axis_source = pointer_axis_source,
.axis_stop = pointer_axis_stop,
.axis_discrete = pointer_axis_discrete,
};

static void
launcher_query_backspace(char *s)
{
size_t len = strlen(s);
if (len == 0) {
return;
}

size_t pos = len - 1;
while (pos > 0 && ((unsigned char)s[pos] & 0xc0) == 0x80) {
pos--;
}
s[pos] = '\0';
}

static void
keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
uint32_t format, int fd, uint32_t size)
{
struct app *app = data;
(void)wl_keyboard;

if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
close(fd);
return;
}

char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
close(fd);
if (map_shm == MAP_FAILED) {
return;
}

if (!app->xkb_context) {
app->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}
if (!app->xkb_context) {
munmap(map_shm, size);
return;
}

struct xkb_keymap *new_keymap = xkb_keymap_new_from_string(
app->xkb_context, map_shm,
XKB_KEYMAP_FORMAT_TEXT_V1,
XKB_KEYMAP_COMPILE_NO_FLAGS);
munmap(map_shm, size);
if (!new_keymap) {
return;
}

struct xkb_state *new_state = xkb_state_new(new_keymap);
if (!new_state) {
xkb_keymap_unref(new_keymap);
return;
}

if (app->xkb_state) {
xkb_state_unref(app->xkb_state);
}
if (app->xkb_keymap) {
xkb_keymap_unref(app->xkb_keymap);
}

app->xkb_keymap = new_keymap;
app->xkb_state = new_state;
}

static void
keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
(void)data;
(void)wl_keyboard;
(void)serial;
(void)surface;
(void)keys;
}

static void
keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
uint32_t serial, struct wl_surface *surface)
{
struct app *app = data;
(void)wl_keyboard;
(void)serial;
(void)surface;

if (app->launcher_open) {
launcher_close(app);
}
}

static void
keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
struct app *app = data;
(void)wl_keyboard;
(void)serial;
(void)time;

if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !app->launcher_open || !app->xkb_state) {
return;
}

xkb_keycode_t keycode = key + 8;
xkb_keysym_t sym = xkb_state_key_get_one_sym(app->xkb_state, keycode);

if (sym == XKB_KEY_Escape) {
launcher_close(app);
return;
}
if (sym == XKB_KEY_BackSpace) {
launcher_query_backspace(app->launcher_query);
app->launcher_search_active = true;
launcher_rebuild_filtered(app);
panel_draw(&app->side);
return;
}
if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
if (app->launcher_selected >= 0) {
launcher_activate_filtered(app, app->launcher_selected);
}
return;
}
if (sym == XKB_KEY_Up || sym == XKB_KEY_KP_Up) {
if (app->launcher_filtered_count > 0 && app->launcher_selected > 0) {
app->launcher_selected--;
launcher_ensure_selected_visible(app, &app->side);
panel_draw(&app->side);
}
return;
}
if (sym == XKB_KEY_Down || sym == XKB_KEY_KP_Down) {
if (app->launcher_filtered_count > 0
&& app->launcher_selected < (int)app->launcher_filtered_count - 1) {
app->launcher_selected++;
launcher_ensure_selected_visible(app, &app->side);
panel_draw(&app->side);
}
return;
}
if (sym == XKB_KEY_Tab) {
app->launcher_category = (app->launcher_category + 1) % LCAT_COUNT;
launcher_rebuild_filtered(app);
panel_draw(&app->side);
return;
}

char utf8[16] = { 0 };
int n = xkb_state_key_get_utf8(app->xkb_state, keycode, utf8, sizeof(utf8));
if (n <= 0) {
return;
}

if ((unsigned char)utf8[0] < 0x20) {
return;
}

size_t len = strlen(app->launcher_query);
if (len + (size_t)n >= sizeof(app->launcher_query)) {
return;
}

memcpy(app->launcher_query + len, utf8, (size_t)n);
app->launcher_query[len + (size_t)n] = '\0';
app->launcher_search_active = true;
launcher_rebuild_filtered(app);
panel_draw(&app->side);
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
uint32_t mods_locked, uint32_t group)
{
struct app *app = data;
(void)wl_keyboard;
(void)serial;
if (!app->xkb_state) {
return;
}

xkb_state_update_mask(app->xkb_state,
mods_depressed, mods_latched, mods_locked,
0, 0, group);
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
int32_t rate, int32_t delay)
{
(void)data;
(void)wl_keyboard;
(void)rate;
(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
.keymap = keyboard_keymap,
.enter = keyboard_enter,
.leave = keyboard_leave,
.key = keyboard_key,
.modifiers = keyboard_modifiers,
.repeat_info = keyboard_repeat_info,
};

static void
seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
struct app *app = data;
if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
app->pointer = wl_seat_get_pointer(seat);
wl_pointer_add_listener(app->pointer, &pointer_listener, app);
} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && app->pointer) {
wl_pointer_destroy(app->pointer);
app->pointer = NULL;
}

if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
app->keyboard = wl_seat_get_keyboard(seat);
wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
} else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && app->keyboard) {
wl_keyboard_destroy(app->keyboard);
app->keyboard = NULL;
}
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
(void)data;
(void)seat;
(void)name;
}

static const struct wl_seat_listener seat_listener = {
.capabilities = seat_capabilities,
.name = seat_name,
};

static void
foreign_handle_title(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle,
const char *title)
{
struct toplevel_entry *entry = data;
(void)handle;
snprintf(entry->title, sizeof(entry->title), "%s", title ? title : "");
}

static void
foreign_handle_app_id(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle,
const char *app_id)
{
struct toplevel_entry *entry = data;
(void)handle;
snprintf(entry->app_id, sizeof(entry->app_id), "%s", app_id ? app_id : "");
}

static void
foreign_handle_output_enter(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle,
struct wl_output *output)
{
(void)data;
(void)handle;
(void)output;
}

static void
foreign_handle_output_leave(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle,
struct wl_output *output)
{
(void)data;
(void)handle;
(void)output;
}

static void
foreign_handle_state(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle,
struct wl_array *state)
{
struct toplevel_entry *entry = data;
(void)handle;
entry->active = false;
entry->minimized = false;
entry->fullscreen = false;

uint32_t *s = state->data;
size_t n = state->size / sizeof(uint32_t);
for (size_t i = 0; i < n; i++) {
if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
entry->active = true;
}
if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
entry->minimized = true;
}
if (s[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) {
entry->fullscreen = true;
}
}
}

static void
foreign_handle_done(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle)
{
struct toplevel_entry *entry = data;
(void)handle;
rebuild_groups(entry->app);
global_menu_sync_active_window(entry->app);
panel_draw(&entry->app->top);
update_side_hover(entry->app);
panel_draw(&entry->app->side);
}

static void
foreign_handle_closed(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle)
{
struct toplevel_entry *entry = data;
if (entry->handle) {
zwlr_foreign_toplevel_handle_v1_destroy(entry->handle);
}
entry->handle = NULL;
entry->used = false;
entry->app_id[0] = '\0';
entry->title[0] = '\0';
entry->active = false;
entry->minimized = false;
(void)handle;

rebuild_groups(entry->app);
global_menu_sync_active_window(entry->app);
panel_draw(&entry->app->top);
update_side_hover(entry->app);
panel_draw(&entry->app->side);
}

static void
foreign_handle_parent(void *data,
struct zwlr_foreign_toplevel_handle_v1 *handle,
struct zwlr_foreign_toplevel_handle_v1 *parent)
{
(void)data;
(void)handle;
(void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener foreign_handle_listener = {
.title = foreign_handle_title,
.app_id = foreign_handle_app_id,
.output_enter = foreign_handle_output_enter,
.output_leave = foreign_handle_output_leave,
.state = foreign_handle_state,
.done = foreign_handle_done,
.closed = foreign_handle_closed,
.parent = foreign_handle_parent,
};

static struct toplevel_entry *
alloc_toplevel_entry(struct app *app)
{
for (size_t i = 0; i < MAX_TOPLEVELS; i++) {
if (!app->toplevels[i].used) {
struct toplevel_entry *entry = &app->toplevels[i];
memset(entry, 0, sizeof(*entry));
entry->used = true;
entry->app = app;
return entry;
}
}
return NULL;
}

static void
foreign_manager_toplevel(void *data,
struct zwlr_foreign_toplevel_manager_v1 *manager,
struct zwlr_foreign_toplevel_handle_v1 *toplevel)
{
struct app *app = data;
(void)manager;

struct toplevel_entry *entry = alloc_toplevel_entry(app);
if (!entry) {
zwlr_foreign_toplevel_handle_v1_destroy(toplevel);
return;
}

entry->handle = toplevel;
zwlr_foreign_toplevel_handle_v1_add_listener(toplevel,
&foreign_handle_listener, entry);
}

static void
foreign_manager_finished(void *data,
struct zwlr_foreign_toplevel_manager_v1 *manager)
{
struct app *app = data;
(void)manager;
app->foreign_manager = NULL;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener foreign_manager_listener = {
.toplevel = foreign_manager_toplevel,
.finished = foreign_manager_finished,
};

static void
registry_global(void *data, struct wl_registry *wl_registry,
uint32_t name, const char *interface, uint32_t version)
{
struct app *app = data;

if (!strcmp(interface, wl_compositor_interface.name)) {
app->compositor = wl_registry_bind(wl_registry, name,
&wl_compositor_interface, 4);
return;
}

if (!strcmp(interface, wl_shm_interface.name)) {
app->shm = wl_registry_bind(wl_registry, name,
&wl_shm_interface, 1);
return;
}

if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
app->layer_shell = wl_registry_bind(wl_registry, name,
&zwlr_layer_shell_v1_interface, 4);
return;
}

if (!strcmp(interface, wl_output_interface.name) && !app->output) {
uint32_t bind_version = version > 4 ? 4 : version;
if (bind_version < 1) {
return;
}
app->output = wl_registry_bind(wl_registry, name,
&wl_output_interface, bind_version);
wl_output_add_listener(app->output, &output_listener, app);
return;
}

if (!strcmp(interface, wl_seat_interface.name) && !app->seat) {
uint32_t bind_version = version > 5 ? 5 : version;
app->seat = wl_registry_bind(wl_registry, name, &wl_seat_interface,
bind_version);
wl_seat_add_listener(app->seat, &seat_listener, app);
return;
}

if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)
&& !app->foreign_manager) {
uint32_t bind_version = version > 3 ? 3 : version;
app->foreign_manager = wl_registry_bind(wl_registry, name,
&zwlr_foreign_toplevel_manager_v1_interface, bind_version);
zwlr_foreign_toplevel_manager_v1_add_listener(app->foreign_manager,
&foreign_manager_listener, app);
}
}

static void
registry_global_remove(void *data, struct wl_registry *wl_registry,
uint32_t name)
{
(void)data;
(void)wl_registry;
(void)name;
}

static const struct wl_registry_listener registry_listener = {
.global = registry_global,
.global_remove = registry_global_remove,
};

static bool
panel_create(struct app *app, struct panel *panel, enum panel_type type)
{
uint32_t anchors = 0;
panel->type = type;
panel->app = app;

panel->surface = wl_compositor_create_surface(app->compositor);
if (!panel->surface) {
return false;
}

panel->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
app->layer_shell,
panel->surface,
app->output,
ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
type == PANEL_TOP ? "karton-top-panel" : "karton-side-panel");
if (!panel->layer_surface) {
return false;
}

zwlr_layer_surface_v1_add_listener(panel->layer_surface,
&layer_surface_listener, panel);
zwlr_layer_surface_v1_set_keyboard_interactivity(panel->layer_surface,
ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

if (type == PANEL_TOP) {
anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
| ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
zwlr_layer_surface_v1_set_anchor(panel->layer_surface, anchors);
zwlr_layer_surface_v1_set_size(panel->layer_surface, 0,
app->quick_open ? app->style.top_expanded_height : app->style.top_height);
zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, app->style.top_height);
zwlr_layer_surface_v1_set_margin(panel->layer_surface,
0, 0, 0, app->side_enabled ? app->style.side_width : 0);
} else {
anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
| ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
zwlr_layer_surface_v1_set_anchor(panel->layer_surface, anchors);
zwlr_layer_surface_v1_set_margin(panel->layer_surface,
0, 0, 0, 0);
zwlr_layer_surface_v1_set_size(panel->layer_surface,
app->popup_group >= 0 ? app->style.side_expanded_width : app->style.side_width, 0);
if (app->launcher_open) {
zwlr_layer_surface_v1_set_size(panel->layer_surface,
launcher_panel_width(app), 0);
}
zwlr_layer_surface_v1_set_exclusive_zone(panel->layer_surface, app->style.side_width);
}

wl_surface_commit(panel->surface);
return true;
}

static void
panel_destroy(struct panel *panel)
{
if (panel->layer_surface) {
zwlr_layer_surface_v1_destroy(panel->layer_surface);
panel->layer_surface = NULL;
}
if (panel->surface) {
wl_surface_destroy(panel->surface);
panel->surface = NULL;
}
for (size_t i = 0; i < 2; i++) {
destroy_buffer(&panel->buffers[i]);
}
}

int
main(int argc, char **argv)
{
init_i18n();

enum run_mode mode = RUN_BOTH;
if (!parse_args(argc, argv, &mode)) {
print_usage(argv[0]);
return 2;
}

bool need_top = mode != RUN_SIDE_ONLY;
bool need_side = mode != RUN_TOP_ONLY;

struct app app = {
.output_scale = 1,
.running = true,
.top_popup_mode = TOP_POPUP_NONE,
.quick_brightness = 72,
.quick_volume = 58,
.hovered_group = -1,
.popup_group = -1,
.popup_hover_item = -1,
.popup_selected_item = -1,
.launcher_hover_item = -1,
.launcher_hover_favorite = -1,
.launcher_category = LCAT_ALL,
.launcher_selected = -1,
.launcher_menu_target = -1,
.launcher_menu_hover = -1,
.quick_hover_tile = -1,
.quick_menu_tile = -1,
.quick_menu_hover_item = -1,
.global_menu_open_top = -1,
};

register_runtime_signals();

load_shell_style(&app.style);
if (need_top) {
sync_environment_theme(app.style.theme_mode);
}
app.top.app = &app;
app.side.app = &app;
load_icon_theme_name(&app);
calendar_set_today(&app);
load_launcher_entries(&app);
clock_load_timezones(&app);
shell_style_apply_theme(&app.style);
app.side_enabled = need_side;

app.display = wl_display_connect(NULL);
if (!app.display) {
fprintf(stderr, "%s\n", _("karton-shell: cannot connect to wayland display"));
return 1;
}

app.registry = wl_display_get_registry(app.display);
wl_registry_add_listener(app.registry, &registry_listener, &app);
wl_display_roundtrip(app.display);
wl_display_roundtrip(app.display);

if (!app.compositor || !app.shm || !app.layer_shell) {
fprintf(stderr, "%s\n", _("karton-shell: required wayland globals are missing"));
wl_display_disconnect(app.display);
return 1;
}

if ((need_top && !panel_create(&app, &app.top, PANEL_TOP))
|| (need_side && !panel_create(&app, &app.side, PANEL_SIDE))) {
fprintf(stderr, "%s\n", _("karton-shell: failed to create layer surfaces"));
if (need_side) {
panel_destroy(&app.side);
}
if (need_top) {
panel_destroy(&app.top);
}
wl_registry_destroy(app.registry);
wl_display_disconnect(app.display);
return 1;
}

while (app.running) {
int dispatch_rc = wl_display_dispatch(app.display);
if (dispatch_rc == -1) {
if (errno == EINTR) {
process_runtime_signals(&app);
continue;
}
break;
}

process_runtime_signals(&app);
if (need_top) {
panel_draw(&app.top);
}
}

if (need_side) {
panel_destroy(&app.side);
}
if (need_top) {
panel_destroy(&app.top);
}

for (size_t i = 0; i < app.icon_cache_count; i++) {
if (app.icon_cache[i].surface) {
cairo_surface_destroy(app.icon_cache[i].surface);
}
}

for (size_t i = 0; i < MAX_TOPLEVELS; i++) {
if (app.toplevels[i].used && app.toplevels[i].handle) {
zwlr_foreign_toplevel_handle_v1_destroy(app.toplevels[i].handle);
}
}
if (app.foreign_manager) {
zwlr_foreign_toplevel_manager_v1_destroy(app.foreign_manager);
}
if (app.keyboard) {
wl_keyboard_destroy(app.keyboard);
}
if (app.pointer) {
wl_pointer_destroy(app.pointer);
}
if (app.seat) {
wl_seat_destroy(app.seat);
}
if (app.xkb_state) {
xkb_state_unref(app.xkb_state);
}
if (app.xkb_keymap) {
xkb_keymap_unref(app.xkb_keymap);
}
if (app.xkb_context) {
xkb_context_unref(app.xkb_context);
}
if (app.output) {
wl_output_destroy(app.output);
}
if (app.layer_shell) {
zwlr_layer_shell_v1_destroy(app.layer_shell);
}
if (app.shm) {
wl_shm_destroy(app.shm);
}
if (app.compositor) {
wl_compositor_destroy(app.compositor);
}
if (app.registry) {
wl_registry_destroy(app.registry);
}
wl_display_disconnect(app.display);
return 0;
}
