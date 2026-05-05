// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 mijsys
#include <ctype.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

enum theme_mode {
THEME_AUTO,
THEME_LIGHT,
THEME_DARK,
};

struct app_widgets {
GtkWindow *window;
GtkStack *stack;
GtkLabel *status;

GtkCheckButton *auto_btn;
GtkCheckButton *light_btn;
GtkCheckButton *dark_btn;
GtkDropDown *gtk_theme_drop;
GtkDropDown *icon_theme_drop;
GtkEntry *font_entry;

GtkSwitch *wifi_switch;
GtkSwitch *bt_switch;
GtkSwitch *airplane_switch;
GtkLabel *network_label;

GtkScale *volume_scale;
GtkSwitch *mute_switch;
GtkScale *mic_scale;
GtkSwitch *mic_mute_switch;
GtkLabel *audio_label;

GtkScale *brightness_scale;
GtkDropDown *profile_drop;
GtkLabel *power_label;

GtkLabel *display_label;
GtkDropDown *display_scale_drop;
char display_output[128];

GtkScale *mouse_speed_scale;
GtkSwitch *natural_scroll_switch;
GtkSwitch *tap_to_click_switch;
GtkLabel *input_label;

GtkSwitch *dnd_switch;
GtkLabel *notifications_label;

gboolean updating_network;
gboolean updating_audio;
gboolean updating_power;
gboolean updating_display;
gboolean updating_input;
gboolean updating_notifications;
};

static const char *startup_page = "appearance";

static const char *gtk_theme_options[] = {
"Adwaita",
"Adwaita-dark",
"Breeze",
"Breeze-Dark",
"Graphite-Light",
"Graphite-Dark",
NULL,
};

static const char *icon_theme_options[] = {
"Adwaita",
"Papirus",
"Papirus-Dark",
"Tela",
"Tela-dark",
"breeze",
"hicolor",
NULL,
};

static const char *app_css =
"window {"
"  background: linear-gradient(180deg, #f2f7ff 0%, #e7f0ff 100%);"
"}"
".root {"
"  padding: 16px;"
"}"
".title {"
"  font-size: 26px;"
"  font-weight: 700;"
"  color: #1f355a;"
"}"
".subtitle {"
"  font-size: 12px;"
"  color: #4f6690;"
"}"
".pane {"
"  margin-top: 8px;"
"}"
".card {"
"  background: rgba(255, 255, 255, 0.92);"
"  border-radius: 16px;"
"  border: 1px solid rgba(125, 152, 201, 0.35);"
"  padding: 14px;"
"}"
".card-title {"
"  font-size: 15px;"
"  font-weight: 700;"
"  color: #233a60;"
"}"
".subtle {"
"  color: #56709f;"
"  font-size: 11px;"
"}"
".status-ok {"
"  color: #2f6f56;"
"  font-weight: 600;"
"}"
".status-err {"
"  color: #9b3655;"
"  font-weight: 600;"
"}";

static gboolean
command_exists(const char *name)
{
gchar *path = g_find_program_in_path(name);
if (!path) {
return FALSE;
}
g_free(path);
return TRUE;
}

static gchar *
first_line_trimmed(const gchar *text)
{
if (!text) {
return g_strdup("");
}
gchar *copy = g_strdup(text);
g_strstrip(copy);
char *nl = strpbrk(copy, "\r\n");
if (nl) {
*nl = '\0';
}
return copy;
}

static int
first_integer_in_text(const gchar *text)
{
if (!text) {
return -1;
}
const unsigned char *p = (const unsigned char *)text;
while (*p && !isdigit(*p)) {
p++;
}
if (!*p) {
return -1;
}
int v = 0;
while (*p && isdigit(*p)) {
v = v * 10 + (*p - '0');
p++;
}
return v;
}

static gboolean
text_is_true(const gchar *text)
{
if (!text) {
return FALSE;
}
return strstr(text, "true") || strstr(text, "enabled") || strstr(text, "on") || strstr(text, "yes");
}

static gboolean
run_command_capture(const char *command, gchar **stdout_out, gchar **stderr_out, gint *status_out)
{
gchar *out = NULL;
gchar *err = NULL;
gint status = 0;
GError *error = NULL;

gboolean ok = g_spawn_command_line_sync(command, &out, &err, &status, &error);
if (!ok) {
if (stderr_out) {
*stderr_out = g_strdup(error ? error->message : "Command failed");
}
g_clear_error(&error);
g_free(out);
g_free(err);
return FALSE;
}

if (stdout_out) {
*stdout_out = out;
} else {
g_free(out);
}
if (stderr_out) {
*stderr_out = err;
} else {
g_free(err);
}
if (status_out) {
*status_out = status;
}
return TRUE;
}

static gboolean
run_command_ok(const char *command, gchar **stderr_msg)
{
gchar *out = NULL;
gchar *err = NULL;
gint status = 0;
if (!run_command_capture(command, &out, &err, &status)) {
if (stderr_msg && !*stderr_msg) {
*stderr_msg = g_strdup("Cannot execute command");
}
return FALSE;
}
g_free(out);

if (status != 0) {
if (stderr_msg) {
if (err && *err) {
*stderr_msg = g_strdup(err);
} else {
*stderr_msg = g_strdup("Command returned non-zero status");
}
}
g_free(err);
return FALSE;
}

if (stderr_msg) {
*stderr_msg = err;
} else {
g_free(err);
}
return TRUE;
}

static gboolean
run_command_async(const char *command, gchar **error_msg)
{
GError *error = NULL;
if (g_spawn_command_line_async(command, &error)) {
return TRUE;
}
if (error_msg) {
*error_msg = g_strdup(error ? error->message : "Cannot execute command");
}
g_clear_error(&error);
return FALSE;
}

static gchar *
get_karton_config_path(const gchar *leaf)
{
return g_build_filename(g_get_user_config_dir(), "karton", leaf, NULL);
}

static void
set_status(struct app_widgets *ui, const char *text, gboolean success)
{
gtk_label_set_text(ui->status, text ? text : "");
gtk_widget_remove_css_class(GTK_WIDGET(ui->status), "status-ok");
gtk_widget_remove_css_class(GTK_WIDGET(ui->status), "status-err");
gtk_widget_add_css_class(GTK_WIDGET(ui->status), success ? "status-ok" : "status-err");
}

static gboolean
apply_theme_mode(enum theme_mode mode, gchar **error_msg)
{
const char *mode_name = "auto";
if (mode == THEME_LIGHT) {
mode_name = "light";
} else if (mode == THEME_DARK) {
mode_name = "dark";
}

gchar *cfg_dir = g_build_filename(g_get_user_config_dir(), "karton", NULL);
if (g_mkdir_with_parents(cfg_dir, 0755) != 0) {
if (error_msg) {
*error_msg = g_strdup("Cannot create ~/.config/karton");
}
g_free(cfg_dir);
return FALSE;
}
g_free(cfg_dir);

gchar *mode_path = get_karton_config_path("theme-mode");
if (!g_file_set_contents(mode_path, mode_name, -1, NULL)) {
if (error_msg) {
*error_msg = g_strdup("Cannot write theme-mode file");
}
g_free(mode_path);
return FALSE;
}
g_free(mode_path);

const gchar *home = g_get_home_dir();
gchar *local_apply = g_build_filename(home, ".local-karton", "bin", "karton-apply-theme", NULL);
gchar *cmd = NULL;
if (g_file_test(local_apply, G_FILE_TEST_IS_EXECUTABLE)) {
cmd = g_strdup_printf("%s %s", local_apply, mode_name);
} else {
cmd = g_strdup_printf("karton-apply-theme %s", mode_name);
}
g_free(local_apply);

gboolean ok = run_command_ok(cmd, error_msg);
g_free(cmd);
return ok;
}

static enum theme_mode
current_mode_from_disk(void)
{
enum theme_mode mode = THEME_AUTO;
gchar *mode_path = get_karton_config_path("theme-mode");
gchar *content = NULL;

if (g_file_get_contents(mode_path, &content, NULL, NULL) && content) {
g_strstrip(content);
if (g_ascii_strcasecmp(content, "light") == 0) {
mode = THEME_LIGHT;
} else if (g_ascii_strcasecmp(content, "dark") == 0) {
mode = THEME_DARK;
}
}

g_free(content);
g_free(mode_path);
return mode;
}

static enum theme_mode
selected_mode(const struct app_widgets *ui)
{
if (gtk_check_button_get_active(ui->light_btn)) {
return THEME_LIGHT;
}
if (gtk_check_button_get_active(ui->dark_btn)) {
return THEME_DARK;
}
return THEME_AUTO;
}

static void
set_switch_if_changed(GtkSwitch *sw, gboolean value)
{
if (gtk_switch_get_active(sw) != value) {
gtk_switch_set_active(sw, value);
}
}

static void
set_range_if_changed(GtkRange *range, double value)
{
double current = gtk_range_get_value(range);
if (current < value - 0.1 || current > value + 0.1) {
gtk_range_set_value(range, value);
}
}

static const char *
get_dropdown_selected_text(GtkDropDown *dd, const char *fallback)
{
GObject *obj = gtk_drop_down_get_selected_item(dd);
if (!obj) {
return fallback;
}
if (GTK_IS_STRING_OBJECT(obj)) {
return gtk_string_object_get_string(GTK_STRING_OBJECT(obj));
}
return fallback;
}

static void
set_dropdown_to_text(GtkDropDown *dd, const char *value)
{
if (!value || !*value) {
return;
}
GListModel *model = gtk_drop_down_get_model(dd);
if (!model) {
return;
}
guint n = g_list_model_get_n_items(model);
for (guint i = 0; i < n; i++) {
GObject *obj = g_list_model_get_item(model, i);
if (!obj) {
continue;
}
if (GTK_IS_STRING_OBJECT(obj)) {
const char *txt = gtk_string_object_get_string(GTK_STRING_OBJECT(obj));
if (!g_ascii_strcasecmp(txt, value)) {
gtk_drop_down_set_selected(dd, i);
g_object_unref(obj);
return;
}
}
g_object_unref(obj);
}
}

static gchar *
get_gtk_settings_path(const char *version_dir)
{
return g_build_filename(g_get_user_config_dir(), version_dir, "settings.ini", NULL);
}

static gchar *
read_gtk_setting(const char *key)
{
const char *dirs[] = { "gtk-4.0", "gtk-3.0" };
for (size_t i = 0; i < G_N_ELEMENTS(dirs); i++) {
gchar *path = get_gtk_settings_path(dirs[i]);
GKeyFile *kf = g_key_file_new();
if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)
&& g_key_file_has_key(kf, "Settings", key, NULL)) {
gchar *val = g_key_file_get_string(kf, "Settings", key, NULL);
g_key_file_free(kf);
g_free(path);
return val;
}
g_key_file_free(kf);
g_free(path);
}
return NULL;
}

static gboolean
write_gtk_setting(const char *key, const char *value, gchar **error_msg)
{
const char *dirs[] = { "gtk-3.0", "gtk-4.0" };
for (size_t i = 0; i < G_N_ELEMENTS(dirs); i++) {
gchar *dir = g_build_filename(g_get_user_config_dir(), dirs[i], NULL);
if (g_mkdir_with_parents(dir, 0755) != 0) {
if (error_msg) {
*error_msg = g_strdup("Cannot create GTK config directory");
}
g_free(dir);
return FALSE;
}
g_free(dir);

gchar *path = get_gtk_settings_path(dirs[i]);
GKeyFile *kf = g_key_file_new();
g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
g_key_file_set_string(kf, "Settings", key, value);
gsize len = 0;
gchar *data = g_key_file_to_data(kf, &len, NULL);
gboolean ok = g_file_set_contents(path, data, (gssize)len, NULL);
g_free(data);
g_key_file_free(kf);
g_free(path);
if (!ok) {
if (error_msg) {
*error_msg = g_strdup("Cannot write GTK settings.ini");
}
return FALSE;
}
}
return TRUE;
}

static gboolean
gsettings_schema_exists(const char *schema)
{
if (!command_exists("gsettings")) {
return FALSE;
}
gchar *cmd = g_strdup_printf("gsettings list-schemas | grep -Fx '%s'", schema);
gint status = 0;
gboolean ok = run_command_capture(cmd, NULL, NULL, &status);
g_free(cmd);
return ok && status == 0;
}

static gboolean
gsettings_get_bool(const char *schema, const char *key, gboolean *out)
{
gchar *cmd = g_strdup_printf("gsettings get %s %s", schema, key);
gchar *out_txt = NULL;
gint status = 0;
gboolean ok = run_command_capture(cmd, &out_txt, NULL, &status);
g_free(cmd);
if (!ok || status != 0 || !out_txt) {
g_free(out_txt);
return FALSE;
}
gchar *line = first_line_trimmed(out_txt);
*out = text_is_true(line);
g_free(line);
g_free(out_txt);
return TRUE;
}

static gboolean
gsettings_get_double(const char *schema, const char *key, double *out)
{
gchar *cmd = g_strdup_printf("gsettings get %s %s", schema, key);
gchar *out_txt = NULL;
gint status = 0;
gboolean ok = run_command_capture(cmd, &out_txt, NULL, &status);
g_free(cmd);
if (!ok || status != 0 || !out_txt) {
g_free(out_txt);
return FALSE;
}
gchar *line = first_line_trimmed(out_txt);
*out = g_ascii_strtod(line, NULL);
g_free(line);
g_free(out_txt);
return TRUE;
}

static gboolean
gsettings_set_bool(const char *schema, const char *key, gboolean value, gchar **err)
{
gchar *cmd = g_strdup_printf("gsettings set %s %s %s", schema, key, value ? "true" : "false");
gboolean ok = run_command_ok(cmd, err);
g_free(cmd);
return ok;
}

static gboolean
gsettings_set_double(const char *schema, const char *key, double value, gchar **err)
{
gchar *cmd = g_strdup_printf("gsettings set %s %s %.2f", schema, key, value);
gboolean ok = run_command_ok(cmd, err);
g_free(cmd);
return ok;
}

static void update_network_page(struct app_widgets *ui);
static void update_audio_page(struct app_widgets *ui);
static void update_power_page(struct app_widgets *ui);
static void update_display_page(struct app_widgets *ui);
static void update_input_page(struct app_widgets *ui);
static void update_notifications_page(struct app_widgets *ui);

static void
refresh_all_dynamic_pages(struct app_widgets *ui)
{
update_network_page(ui);
update_audio_page(ui);
update_power_page(ui);
update_display_page(ui);
update_input_page(ui);
update_notifications_page(ui);
}

static gboolean
on_periodic_refresh(gpointer user_data)
{
struct app_widgets *ui = user_data;
refresh_all_dynamic_pages(ui);
return G_SOURCE_CONTINUE;
}

static gboolean
on_wifi_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_network) {
return FALSE;
}
if (!command_exists("nmcli")) {
set_status(ui, "nmcli not available", FALSE);
update_network_page(ui);
return FALSE;
}

gchar *err = NULL;
const char *cmd = state ? "nmcli radio wifi on" : "nmcli radio wifi off";
if (run_command_ok(cmd, &err)) {
set_status(ui, state ? "Wi-Fi enabled" : "Wi-Fi disabled", TRUE);
} else {
set_status(ui, err ? err : "Cannot toggle Wi-Fi", FALSE);
}
g_free(err);
update_network_page(ui);
return FALSE;
}

static gboolean
on_bt_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_network) {
return FALSE;
}

gchar *err = NULL;
gboolean ok = FALSE;
if (command_exists("nmcli")) {
ok = run_command_ok(state ? "nmcli radio bluetooth on" : "nmcli radio bluetooth off", &err);
} else if (command_exists("rfkill")) {
ok = run_command_ok(state ? "rfkill unblock bluetooth" : "rfkill block bluetooth", &err);
} else {
err = g_strdup("No bluetooth backend (nmcli/rfkill)");
}

if (ok) {
set_status(ui, state ? "Bluetooth enabled" : "Bluetooth disabled", TRUE);
} else {
set_status(ui, err ? err : "Cannot toggle Bluetooth", FALSE);
}
g_free(err);
update_network_page(ui);
return FALSE;
}

static gboolean
on_airplane_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_network) {
return FALSE;
}
if (!command_exists("nmcli")) {
set_status(ui, "nmcli not available", FALSE);
update_network_page(ui);
return FALSE;
}

gchar *err = NULL;
if (run_command_ok(state ? "nmcli radio all off" : "nmcli radio all on", &err)) {
set_status(ui, state ? "Airplane mode enabled" : "Airplane mode disabled", TRUE);
} else {
set_status(ui, err ? err : "Cannot change airplane mode", FALSE);
}
g_free(err);
update_network_page(ui);
return FALSE;
}

static void
on_open_network_advanced(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
gboolean ok = FALSE;
if (command_exists("nm-connection-editor")) {
ok = run_command_async("nm-connection-editor", &err);
} else if (command_exists("nmtui")) {
ok = run_command_async("sh -lc 'kitty -e nmtui || xterm -e nmtui || nmtui'", &err);
} else {
err = g_strdup("Install nm-connection-editor or nmtui");
}
set_status(ui, ok ? "Opened network editor" : (err ? err : "Cannot open network editor"), ok);
g_free(err);
}

static void
update_network_page(struct app_widgets *ui)
{
ui->updating_network = TRUE;

gboolean wifi_known = FALSE;
gboolean wifi_enabled = FALSE;
gboolean bt_known = FALSE;
gboolean bt_enabled = FALSE;
gboolean airplane = FALSE;
char summary[320] = { 0 };

if (command_exists("nmcli")) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("nmcli -t -f WIFI g", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
if (g_ascii_strcasecmp(line, "enabled") == 0 || g_ascii_strcasecmp(line, "on") == 0) {
wifi_known = TRUE;
wifi_enabled = TRUE;
} else if (g_ascii_strcasecmp(line, "disabled") == 0 || g_ascii_strcasecmp(line, "off") == 0) {
wifi_known = TRUE;
wifi_enabled = FALSE;
}
g_free(line);
}
g_free(out);

out = NULL;
if (run_command_capture("nmcli -t -f BLUETOOTH g", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
if (g_ascii_strcasecmp(line, "enabled") == 0 || g_ascii_strcasecmp(line, "on") == 0) {
bt_known = TRUE;
bt_enabled = TRUE;
} else if (g_ascii_strcasecmp(line, "disabled") == 0 || g_ascii_strcasecmp(line, "off") == 0) {
bt_known = TRUE;
bt_enabled = FALSE;
}
g_free(line);
}
g_free(out);

out = NULL;
if (run_command_capture("nmcli -t -f WIFI,BLUETOOTH g", &out, NULL, &status) && status == 0 && out) {
airplane = strstr(out, "disabled:disabled") != NULL;
}
g_free(out);

char *active = NULL;
if (run_command_capture("sh -lc \"nmcli -t -f NAME,DEVICE connection show --active | head -n1\"", &active, NULL, &status)
&& status == 0) {
gchar *line = first_line_trimmed(active);
if (line[0]) {
snprintf(summary, sizeof(summary), "Active connection: %s", line);
}
g_free(line);
}
g_free(active);
}

if (!bt_known && command_exists("rfkill")) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("sh -lc \"rfkill list bluetooth 2>/dev/null | sed -n 's/.*Soft blocked: //p' | head -n1\"", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
if (g_ascii_strcasecmp(line, "yes") == 0) {
bt_known = TRUE;
bt_enabled = FALSE;
} else if (g_ascii_strcasecmp(line, "no") == 0) {
bt_known = TRUE;
bt_enabled = TRUE;
}
g_free(line);
}
g_free(out);
}

gtk_widget_set_sensitive(GTK_WIDGET(ui->wifi_switch), wifi_known);
gtk_widget_set_sensitive(GTK_WIDGET(ui->bt_switch), bt_known);
gtk_widget_set_sensitive(GTK_WIDGET(ui->airplane_switch), command_exists("nmcli"));
set_switch_if_changed(ui->airplane_switch, airplane);
if (wifi_known) {
set_switch_if_changed(ui->wifi_switch, wifi_enabled);
} else {
set_switch_if_changed(ui->wifi_switch, FALSE);
}
if (bt_known) {
set_switch_if_changed(ui->bt_switch, bt_enabled);
} else {
set_switch_if_changed(ui->bt_switch, FALSE);
}

if (!summary[0]) {
snprintf(summary, sizeof(summary), "Use nmcli/rfkill backends for full network control");
}
gtk_label_set_text(ui->network_label, summary);
ui->updating_network = FALSE;
}

static gboolean
audio_using_pactl(void)
{
return command_exists("pactl");
}

static gboolean
audio_using_wpctl(void)
{
return !audio_using_pactl() && command_exists("wpctl");
}

static void
on_volume_value_changed(GtkRange *range, gpointer user_data)
{
struct app_widgets *ui = user_data;
if (ui->updating_audio) {
return;
}

int value = (int)(gtk_range_get_value(range) + 0.5);
if (value < 0) {
value = 0;
}
if (value > 150) {
value = 150;
}

char cmd[128] = { 0 };
gchar *err = NULL;
gboolean ok = FALSE;
if (audio_using_pactl()) {
snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", value);
ok = run_command_ok(cmd, &err);
} else if (audio_using_wpctl()) {
double frac = value / 100.0;
snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f", frac);
ok = run_command_ok(cmd, &err);
} else {
err = g_strdup("No audio backend (pactl/wpctl)");
}

if (!ok) {
set_status(ui, err ? err : "Cannot set volume", FALSE);
}
g_free(err);
update_audio_page(ui);
}

static gboolean
on_mute_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_audio) {
return FALSE;
}

gchar *err = NULL;
gboolean ok = FALSE;
if (audio_using_pactl()) {
ok = run_command_ok(state ? "pactl set-sink-mute @DEFAULT_SINK@ 1" : "pactl set-sink-mute @DEFAULT_SINK@ 0", &err);
} else if (audio_using_wpctl()) {
ok = run_command_ok(state ? "wpctl set-mute @DEFAULT_AUDIO_SINK@ 1" : "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0", &err);
} else {
err = g_strdup("No audio backend (pactl/wpctl)");
}

if (ok) {
set_status(ui, state ? "Output muted" : "Output unmuted", TRUE);
} else {
set_status(ui, err ? err : "Cannot toggle output mute", FALSE);
}
g_free(err);
update_audio_page(ui);
return FALSE;
}

static void
on_mic_value_changed(GtkRange *range, gpointer user_data)
{
struct app_widgets *ui = user_data;
if (ui->updating_audio) {
return;
}

int value = (int)(gtk_range_get_value(range) + 0.5);
if (value < 0) {
value = 0;
}
if (value > 150) {
value = 150;
}

char cmd[128] = { 0 };
gchar *err = NULL;
gboolean ok = FALSE;
if (audio_using_pactl()) {
snprintf(cmd, sizeof(cmd), "pactl set-source-volume @DEFAULT_SOURCE@ %d%%", value);
ok = run_command_ok(cmd, &err);
} else if (audio_using_wpctl()) {
double frac = value / 100.0;
snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SOURCE@ %.2f", frac);
ok = run_command_ok(cmd, &err);
} else {
err = g_strdup("No audio backend (pactl/wpctl)");
}

if (!ok) {
set_status(ui, err ? err : "Cannot set microphone volume", FALSE);
}
g_free(err);
update_audio_page(ui);
}

static gboolean
on_mic_mute_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_audio) {
return FALSE;
}

gchar *err = NULL;
gboolean ok = FALSE;
if (audio_using_pactl()) {
ok = run_command_ok(state ? "pactl set-source-mute @DEFAULT_SOURCE@ 1" : "pactl set-source-mute @DEFAULT_SOURCE@ 0", &err);
} else if (audio_using_wpctl()) {
ok = run_command_ok(state ? "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 1" : "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ 0", &err);
} else {
err = g_strdup("No audio backend (pactl/wpctl)");
}

if (ok) {
set_status(ui, state ? "Microphone muted" : "Microphone unmuted", TRUE);
} else {
set_status(ui, err ? err : "Cannot toggle microphone mute", FALSE);
}
g_free(err);
update_audio_page(ui);
return FALSE;
}

static void
on_open_audio_advanced(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
gboolean ok = FALSE;
if (command_exists("pavucontrol")) {
ok = run_command_async("pavucontrol", &err);
} else if (command_exists("helvum")) {
ok = run_command_async("helvum", &err);
} else {
err = g_strdup("Install pavucontrol or helvum for advanced audio");
}
set_status(ui, ok ? "Opened advanced audio" : (err ? err : "Cannot open advanced audio"), ok);
g_free(err);
}

static void
update_audio_page(struct app_widgets *ui)
{
ui->updating_audio = TRUE;

gboolean backend = audio_using_pactl() || audio_using_wpctl();
gtk_widget_set_sensitive(GTK_WIDGET(ui->volume_scale), backend);
gtk_widget_set_sensitive(GTK_WIDGET(ui->mute_switch), backend);
gtk_widget_set_sensitive(GTK_WIDGET(ui->mic_scale), backend);
gtk_widget_set_sensitive(GTK_WIDGET(ui->mic_mute_switch), backend);

int volume = -1;
int mic_volume = -1;
gboolean muted = FALSE;
gboolean mic_muted = FALSE;
char summary[320] = { 0 };

if (audio_using_pactl()) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("pactl get-sink-volume @DEFAULT_SINK@", &out, NULL, &status) && status == 0) {
volume = first_integer_in_text(out);
}
g_free(out);

out = NULL;
if (run_command_capture("pactl get-sink-mute @DEFAULT_SINK@", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
muted = strstr(line, "yes") != NULL;
g_free(line);
}
g_free(out);

out = NULL;
if (run_command_capture("pactl get-source-volume @DEFAULT_SOURCE@", &out, NULL, &status) && status == 0) {
mic_volume = first_integer_in_text(out);
}
g_free(out);

out = NULL;
if (run_command_capture("pactl get-source-mute @DEFAULT_SOURCE@", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
mic_muted = strstr(line, "yes") != NULL;
g_free(line);
}
g_free(out);
snprintf(summary, sizeof(summary), "Backend: pactl");
} else if (audio_using_wpctl()) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@", &out, NULL, &status) && status == 0) {
char *line = first_line_trimmed(out);
char *colon = strchr(line, ':');
if (colon) {
double v = g_ascii_strtod(colon + 1, NULL);
if (v >= 0.0) {
volume = (int)(v * 100.0 + 0.5);
}
}
muted = strstr(line, "MUTED") != NULL;
g_free(line);
}
g_free(out);

out = NULL;
if (run_command_capture("wpctl get-volume @DEFAULT_AUDIO_SOURCE@", &out, NULL, &status) && status == 0) {
char *line = first_line_trimmed(out);
char *colon = strchr(line, ':');
if (colon) {
double v = g_ascii_strtod(colon + 1, NULL);
if (v >= 0.0) {
mic_volume = (int)(v * 100.0 + 0.5);
}
}
mic_muted = strstr(line, "MUTED") != NULL;
g_free(line);
}
g_free(out);
snprintf(summary, sizeof(summary), "Backend: wpctl");
} else {
snprintf(summary, sizeof(summary), "Install pactl or wpctl to control audio");
}

if (volume >= 0) {
set_range_if_changed(GTK_RANGE(ui->volume_scale), volume);
}
if (mic_volume >= 0) {
set_range_if_changed(GTK_RANGE(ui->mic_scale), mic_volume);
}
set_switch_if_changed(ui->mute_switch, muted);
set_switch_if_changed(ui->mic_mute_switch, mic_muted);
gtk_label_set_text(ui->audio_label, summary);
ui->updating_audio = FALSE;
}

static gboolean
on_brightness_value_changed_idle(gpointer user_data)
{
struct app_widgets *ui = user_data;
int value = (int)(gtk_range_get_value(GTK_RANGE(ui->brightness_scale)) + 0.5);
if (value < 1) {
value = 1;
}
if (value > 100) {
value = 100;
}

char cmd[320] = { 0 };
gchar *err = NULL;
gboolean ok = FALSE;
if (command_exists("brightnessctl")) {
snprintf(cmd, sizeof(cmd), "brightnessctl set %d%%", value);
ok = run_command_ok(cmd, &err);
} else if (command_exists("light")) {
snprintf(cmd, sizeof(cmd), "light -S %d", value);
ok = run_command_ok(cmd, &err);
} else {
err = g_strdup("No brightness backend (brightnessctl/light)");
}

if (!ok) {
set_status(ui, err ? err : "Cannot set brightness", FALSE);
}
g_free(err);
return G_SOURCE_REMOVE;
}

static void
on_brightness_value_changed(GtkRange *range, gpointer user_data)
{
(void)range;
struct app_widgets *ui = user_data;
if (ui->updating_power) {
return;
}
g_idle_add(on_brightness_value_changed_idle, ui);
}

static void
on_power_profile_selected(GObject *object, GParamSpec *pspec, gpointer user_data)
{
(void)pspec;
GtkDropDown *dd = GTK_DROP_DOWN(object);
struct app_widgets *ui = user_data;
if (ui->updating_power || !command_exists("powerprofilesctl")) {
return;
}

const char *profiles[] = { "balanced", "power-saver", "performance" };
guint idx = gtk_drop_down_get_selected(dd);
if (idx >= G_N_ELEMENTS(profiles)) {
return;
}

char cmd[96] = { 0 };
snprintf(cmd, sizeof(cmd), "powerprofilesctl set %s", profiles[idx]);
gchar *err = NULL;
if (run_command_ok(cmd, &err)) {
set_status(ui, "Power profile updated", TRUE);
} else {
set_status(ui, err ? err : "Cannot set power profile", FALSE);
}
g_free(err);
}

static void
on_suspend_clicked(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
if (run_command_ok("systemctl suspend", &err)) {
set_status(ui, "Suspending system", TRUE);
} else {
set_status(ui, err ? err : "Cannot suspend", FALSE);
}
g_free(err);
}

static void
on_reboot_clicked(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
if (run_command_ok("systemctl reboot", &err)) {
set_status(ui, "Rebooting system", TRUE);
} else {
set_status(ui, err ? err : "Cannot reboot", FALSE);
}
g_free(err);
}

static void
on_poweroff_clicked(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
if (run_command_ok("systemctl poweroff", &err)) {
set_status(ui, "Powering off system", TRUE);
} else {
set_status(ui, err ? err : "Cannot power off", FALSE);
}
g_free(err);
}

static void
on_logout_clicked(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
if (run_command_ok("sh -lc 'karton --exit || loginctl terminate-session self'", &err)) {
set_status(ui, "Ending session", TRUE);
} else {
set_status(ui, err ? err : "Cannot end session", FALSE);
}
g_free(err);
}

static void
update_power_page(struct app_widgets *ui)
{
ui->updating_power = TRUE;

int brightness = -1;
char summary[320] = { 0 };
if (command_exists("brightnessctl")) {
gchar *out_g = NULL;
gchar *out_m = NULL;
gint status_g = 0;
gint status_m = 0;
if (run_command_capture("brightnessctl g", &out_g, NULL, &status_g)
&& run_command_capture("brightnessctl m", &out_m, NULL, &status_m)
&& status_g == 0 && status_m == 0) {
int g = first_integer_in_text(out_g);
int m = first_integer_in_text(out_m);
if (g >= 0 && m > 0) {
brightness = (int)((100.0 * g) / m + 0.5);
}
}
g_free(out_g);
g_free(out_m);
snprintf(summary, sizeof(summary), "Brightness backend: brightnessctl");
} else if (command_exists("light")) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("light -G", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
double v = g_ascii_strtod(line, NULL);
if (v >= 0.0) {
brightness = (int)(v + 0.5);
}
g_free(line);
}
g_free(out);
snprintf(summary, sizeof(summary), "Brightness backend: light");
} else {
snprintf(summary, sizeof(summary), "Install brightnessctl or light for brightness control");
}

if (command_exists("upower")) {
gchar *bat = NULL;
gint st = 0;
if (run_command_capture("sh -lc \"upower -e 2>/dev/null | grep BAT | head -n1\"", &bat, NULL, &st) && st == 0) {
gchar *bat_line = first_line_trimmed(bat);
if (bat_line[0]) {
gchar *quoted = g_shell_quote(bat_line);
gchar *cmd = g_strdup_printf("sh -lc \"upower -i %s | awk -F': ' '/state|percentage/ {gsub(/^ +/, \"\", $2); print $2}'\"", quoted);
gchar *bat_info = NULL;
gint bst = 0;
if (run_command_capture(cmd, &bat_info, NULL, &bst) && bst == 0 && bat_info && *bat_info) {
gchar *line = first_line_trimmed(bat_info);
if (line[0]) {
g_strlcat(summary, " | Battery: ", sizeof(summary));
g_strlcat(summary, line, sizeof(summary));
}
g_free(line);
}
g_free(bat_info);
g_free(cmd);
g_free(quoted);
}
g_free(bat_line);
}
g_free(bat);
}

gtk_widget_set_sensitive(GTK_WIDGET(ui->brightness_scale), brightness >= 0);
if (brightness >= 0) {
if (brightness < 1) {
brightness = 1;
}
if (brightness > 100) {
brightness = 100;
}
set_range_if_changed(GTK_RANGE(ui->brightness_scale), brightness);
}

gtk_widget_set_sensitive(GTK_WIDGET(ui->profile_drop), command_exists("powerprofilesctl"));
if (command_exists("powerprofilesctl")) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("powerprofilesctl get", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
if (g_strcmp0(line, "balanced") == 0) {
gtk_drop_down_set_selected(ui->profile_drop, 0);
} else if (g_strcmp0(line, "power-saver") == 0) {
gtk_drop_down_set_selected(ui->profile_drop, 1);
} else if (g_strcmp0(line, "performance") == 0) {
gtk_drop_down_set_selected(ui->profile_drop, 2);
}
g_free(line);
}
g_free(out);
}

gtk_label_set_text(ui->power_label, summary);
ui->updating_power = FALSE;
}

static void
on_display_apply_scale(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
if (!ui->display_output[0]) {
set_status(ui, "No output selected", FALSE);
return;
}
if (!command_exists("wlr-randr")) {
set_status(ui, "wlr-randr not available", FALSE);
return;
}

const char *scales[] = { "1", "1.25", "1.5", "2" };
guint idx = gtk_drop_down_get_selected(ui->display_scale_drop);
if (idx >= G_N_ELEMENTS(scales)) {
idx = 0;
}

gchar *quoted = g_shell_quote(ui->display_output);
gchar *cmd = g_strdup_printf("wlr-randr --output %s --scale %s", quoted, scales[idx]);
gchar *err = NULL;
if (run_command_ok(cmd, &err)) {
set_status(ui, "Display scale updated", TRUE);
} else {
set_status(ui, err ? err : "Cannot set display scale", FALSE);
}
g_free(err);
g_free(cmd);
g_free(quoted);
update_display_page(ui);
}

static void
on_open_display_advanced(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
gboolean ok = FALSE;
if (command_exists("wdisplays")) {
ok = run_command_async("wdisplays", &err);
} else if (command_exists("nwg-displays")) {
ok = run_command_async("nwg-displays", &err);
} else if (command_exists("gnome-control-center")) {
ok = run_command_async("gnome-control-center display", &err);
} else {
err = g_strdup("Install wdisplays, nwg-displays or gnome-control-center");
}
set_status(ui, ok ? "Opened display settings" : (err ? err : "Cannot open display settings"), ok);
g_free(err);
}

static void
update_display_page(struct app_widgets *ui)
{
ui->updating_display = TRUE;
ui->display_output[0] = '\0';

char summary[640] = { 0 };
if (command_exists("wlr-randr")) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("wlr-randr", &out, NULL, &status) && status == 0 && out) {
gchar **lines = g_strsplit(out, "\n", -1);
for (int i = 0; lines[i]; i++) {
char *line = lines[i];
if (!line[0]) {
continue;
}
if (!isspace((unsigned char)line[0]) && !ui->display_output[0]) {
char *dup = g_strdup(line);
char *tok = strtok(dup, " ");
if (tok) {
snprintf(ui->display_output, sizeof(ui->display_output), "%s", tok);
}
g_free(dup);
}
if (strlen(summary) < sizeof(summary) - 48 && i < 8) {
g_strlcat(summary, line, sizeof(summary));
g_strlcat(summary, "\n", sizeof(summary));
}
}
g_strfreev(lines);
}
g_free(out);
} else if (command_exists("xrandr")) {
gchar *out = NULL;
gint status = 0;
if (run_command_capture("xrandr --current", &out, NULL, &status) && status == 0 && out) {
gchar **lines = g_strsplit(out, "\n", -1);
for (int i = 0; lines[i]; i++) {
if (!strstr(lines[i], " connected")) {
continue;
}
char *dup = g_strdup(lines[i]);
char *tok = strtok(dup, " ");
if (tok && !ui->display_output[0]) {
snprintf(ui->display_output, sizeof(ui->display_output), "%s", tok);
}
if (strlen(summary) < sizeof(summary) - 64) {
g_strlcat(summary, lines[i], sizeof(summary));
g_strlcat(summary, "\n", sizeof(summary));
}
g_free(dup);
}
g_strfreev(lines);
}
g_free(out);
}

if (!summary[0]) {
snprintf(summary, sizeof(summary), "No display backend found (wlr-randr/xrandr)");
}

gtk_label_set_text(ui->display_label, summary);
gtk_widget_set_sensitive(GTK_WIDGET(ui->display_scale_drop), command_exists("wlr-randr") && ui->display_output[0]);
ui->updating_display = FALSE;
}

static void
on_mouse_speed_changed(GtkRange *range, gpointer user_data)
{
struct app_widgets *ui = user_data;
if (ui->updating_input) {
return;
}
gchar *err = NULL;
double val = gtk_range_get_value(range);
if (gsettings_set_double("org.gnome.desktop.peripherals.mouse", "speed", val, &err)) {
set_status(ui, "Mouse speed updated", TRUE);
} else {
set_status(ui, err ? err : "Cannot set mouse speed", FALSE);
}
g_free(err);
}

static gboolean
on_natural_scroll_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_input) {
return FALSE;
}
gchar *err = NULL;
if (gsettings_set_bool("org.gnome.desktop.peripherals.touchpad", "natural-scroll", state, &err)) {
set_status(ui, "Natural scroll updated", TRUE);
} else {
set_status(ui, err ? err : "Cannot set natural scroll", FALSE);
}
g_free(err);
update_input_page(ui);
return FALSE;
}

static gboolean
on_tap_to_click_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_input) {
return FALSE;
}
gchar *err = NULL;
if (gsettings_set_bool("org.gnome.desktop.peripherals.touchpad", "tap-to-click", state, &err)) {
set_status(ui, "Tap-to-click updated", TRUE);
} else {
set_status(ui, err ? err : "Cannot set tap-to-click", FALSE);
}
g_free(err);
update_input_page(ui);
return FALSE;
}

static void
on_open_input_advanced(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
gboolean ok = FALSE;
if (command_exists("gnome-control-center")) {
ok = run_command_async("gnome-control-center mouse", &err);
} else {
err = g_strdup("Advanced input tool not available");
}
set_status(ui, ok ? "Opened advanced input settings" : (err ? err : "Cannot open advanced input settings"), ok);
g_free(err);
}

static void
update_input_page(struct app_widgets *ui)
{
ui->updating_input = TRUE;
gboolean have_mouse = gsettings_schema_exists("org.gnome.desktop.peripherals.mouse");
gboolean have_touchpad = gsettings_schema_exists("org.gnome.desktop.peripherals.touchpad");

gtk_widget_set_sensitive(GTK_WIDGET(ui->mouse_speed_scale), have_mouse);
gtk_widget_set_sensitive(GTK_WIDGET(ui->natural_scroll_switch), have_touchpad);
gtk_widget_set_sensitive(GTK_WIDGET(ui->tap_to_click_switch), have_touchpad);

char summary[256] = { 0 };
if (have_mouse) {
double speed = 0.0;
if (gsettings_get_double("org.gnome.desktop.peripherals.mouse", "speed", &speed)) {
set_range_if_changed(GTK_RANGE(ui->mouse_speed_scale), speed);
}
}

if (have_touchpad) {
gboolean ns = FALSE;
gboolean tap = FALSE;
if (gsettings_get_bool("org.gnome.desktop.peripherals.touchpad", "natural-scroll", &ns)) {
set_switch_if_changed(ui->natural_scroll_switch, ns);
}
if (gsettings_get_bool("org.gnome.desktop.peripherals.touchpad", "tap-to-click", &tap)) {
set_switch_if_changed(ui->tap_to_click_switch, tap);
}
}

if (!have_mouse && !have_touchpad) {
snprintf(summary, sizeof(summary), "No supported gsettings schemas for input devices");
} else {
snprintf(summary, sizeof(summary), "Input backend: gsettings (%s%s)", have_mouse ? "mouse" : "", have_touchpad ? (have_mouse ? "+touchpad" : "touchpad") : "");
}
gtk_label_set_text(ui->input_label, summary);
ui->updating_input = FALSE;
}

static gboolean
on_dnd_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
(void)sw;
struct app_widgets *ui = user_data;
if (ui->updating_notifications) {
return FALSE;
}
if (!command_exists("swaync-client")) {
set_status(ui, "swaync-client not available", FALSE);
update_notifications_page(ui);
return FALSE;
}

gchar *err = NULL;
const char *cmd = state ? "swaync-client -dn -sw" : "swaync-client -df -sw";
if (run_command_ok(cmd, &err)) {
set_status(ui, state ? "Do not disturb enabled" : "Do not disturb disabled", TRUE);
} else {
set_status(ui, err ? err : "Cannot change do not disturb", FALSE);
}
g_free(err);
update_notifications_page(ui);
return FALSE;
}

static void
on_open_notifications_panel(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
if (run_command_ok("swaync-client -op -sw", &err)) {
set_status(ui, "Opened notification panel", TRUE);
} else {
set_status(ui, err ? err : "Cannot open notification panel", FALSE);
}
g_free(err);
}

static void
on_clear_notifications(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *err = NULL;
if (run_command_ok("swaync-client -C -sw", &err)) {
set_status(ui, "Notifications cleared", TRUE);
} else {
set_status(ui, err ? err : "Cannot clear notifications", FALSE);
}
g_free(err);
update_notifications_page(ui);
}

static void
update_notifications_page(struct app_widgets *ui)
{
ui->updating_notifications = TRUE;

if (!command_exists("swaync-client")) {
gtk_widget_set_sensitive(GTK_WIDGET(ui->dnd_switch), FALSE);
gtk_label_set_text(ui->notifications_label, "Install swaync to manage notifications");
ui->updating_notifications = FALSE;
return;
}

gtk_widget_set_sensitive(GTK_WIDGET(ui->dnd_switch), TRUE);
gchar *out = NULL;
gint status = 0;
gboolean dnd = FALSE;
if (run_command_capture("swaync-client -D -sw", &out, NULL, &status) && status == 0) {
dnd = text_is_true(out);
}
g_free(out);
set_switch_if_changed(ui->dnd_switch, dnd);

char summary[192] = { 0 };
out = NULL;
status = 0;
if (run_command_capture("swaync-client -c -sw", &out, NULL, &status) && status == 0) {
gchar *line = first_line_trimmed(out);
snprintf(summary, sizeof(summary), "Pending notifications: %s", line[0] ? line : "0");
g_free(line);
}
g_free(out);
if (!summary[0]) {
snprintf(summary, sizeof(summary), "Notification backend: swaync");
}
gtk_label_set_text(ui->notifications_label, summary);

ui->updating_notifications = FALSE;
}

static gboolean
apply_appearance_settings(struct app_widgets *ui, gchar **error_msg)
{
if (!apply_theme_mode(selected_mode(ui), error_msg)) {
return FALSE;
}

const char *gtk_theme = get_dropdown_selected_text(ui->gtk_theme_drop, "Adwaita");
const char *icon_theme = get_dropdown_selected_text(ui->icon_theme_drop, "Adwaita");
const char *font_name = gtk_editable_get_text(GTK_EDITABLE(ui->font_entry));
if (!font_name || !*font_name) {
font_name = "Inter 10";
}

if (!write_gtk_setting("gtk-theme-name", gtk_theme, error_msg)) {
return FALSE;
}
if (!write_gtk_setting("gtk-icon-theme-name", icon_theme, error_msg)) {
return FALSE;
}
if (!write_gtk_setting("gtk-font-name", font_name, error_msg)) {
return FALSE;
}
if (!write_gtk_setting("gtk-decoration-layout", "icon:minimize,maximize,close", error_msg)) {
return FALSE;
}

const char *dark_pref = selected_mode(ui) == THEME_DARK ? "true" : "false";
if (!write_gtk_setting("gtk-application-prefer-dark-theme", dark_pref, error_msg)) {
return FALSE;
}

return TRUE;
}

static void
on_apply_theme(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *error_msg = NULL;
if (apply_appearance_settings(ui, &error_msg)) {
set_status(ui, "Appearance updated", TRUE);
} else {
set_status(ui, error_msg ? error_msg : "Appearance update failed", FALSE);
}
g_free(error_msg);
}

static void
on_open_config(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *config_dir = g_build_filename(g_get_user_config_dir(), "karton", NULL);
gchar *quoted = g_shell_quote(config_dir);
gchar *cmd = g_strdup_printf("xdg-open %s", quoted);
gchar *error_msg = NULL;
if (run_command_async(cmd, &error_msg)) {
set_status(ui, "Opened config directory", TRUE);
} else {
set_status(ui, error_msg ? error_msg : "Cannot open config directory", FALSE);
}
g_free(error_msg);
g_free(cmd);
g_free(quoted);
g_free(config_dir);
}

static void
on_reload_shell(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
gchar *error_msg = NULL;
if (run_command_ok("karton --reconfigure", &error_msg)) {
set_status(ui, "Shell reconfigured", TRUE);
} else {
set_status(ui, error_msg ? error_msg : "Cannot reconfigure shell", FALSE);
}
g_free(error_msg);
}

static void
on_refresh_all(GtkButton *button, gpointer user_data)
{
(void)button;
struct app_widgets *ui = user_data;
refresh_all_dynamic_pages(ui);
set_status(ui, "Settings refreshed", TRUE);
}

static GtkWidget *
build_card_title(const char *title)
{
GtkWidget *label = gtk_label_new(title);
gtk_widget_add_css_class(label, "card-title");
gtk_widget_set_halign(label, GTK_ALIGN_START);
return label;
}

static GtkWidget *
build_hint_label(const char *text)
{
GtkWidget *label = gtk_label_new(text);
gtk_widget_add_css_class(label, "subtle");
gtk_label_set_wrap(GTK_LABEL(label), TRUE);
gtk_widget_set_halign(label, GTK_ALIGN_START);
return label;
}

static GtkWidget *
build_appearance_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Wyglad"));

GtkWidget *mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
ui->auto_btn = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Auto"));
ui->light_btn = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Jasny"));
ui->dark_btn = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Ciemny"));
gtk_check_button_set_group(ui->light_btn, ui->auto_btn);
gtk_check_button_set_group(ui->dark_btn, ui->auto_btn);
gtk_box_append(GTK_BOX(mode_row), GTK_WIDGET(ui->auto_btn));
gtk_box_append(GTK_BOX(mode_row), GTK_WIDGET(ui->light_btn));
gtk_box_append(GTK_BOX(mode_row), GTK_WIDGET(ui->dark_btn));
gtk_box_append(GTK_BOX(box), mode_row);

GtkWidget *theme_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(theme_row), gtk_label_new("GTK theme"));
ui->gtk_theme_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(gtk_theme_options));
gtk_widget_set_hexpand(GTK_WIDGET(ui->gtk_theme_drop), TRUE);
gtk_box_append(GTK_BOX(theme_row), GTK_WIDGET(ui->gtk_theme_drop));
gtk_box_append(GTK_BOX(box), theme_row);

GtkWidget *icon_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(icon_row), gtk_label_new("Icon theme"));
ui->icon_theme_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(icon_theme_options));
gtk_widget_set_hexpand(GTK_WIDGET(ui->icon_theme_drop), TRUE);
gtk_box_append(GTK_BOX(icon_row), GTK_WIDGET(ui->icon_theme_drop));
gtk_box_append(GTK_BOX(box), icon_row);

GtkWidget *font_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(font_row), gtk_label_new("Font"));
ui->font_entry = GTK_ENTRY(gtk_entry_new());
gtk_entry_set_placeholder_text(ui->font_entry, "Inter 10");
gtk_widget_set_hexpand(GTK_WIDGET(ui->font_entry), TRUE);
gtk_box_append(GTK_BOX(font_row), GTK_WIDGET(ui->font_entry));
gtk_box_append(GTK_BOX(box), font_row);

GtkWidget *apply_btn = gtk_button_new_with_label("Zastosuj wyglad");
g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply_theme), ui);
gtk_box_append(GTK_BOX(box), apply_btn);
gtk_box_append(GTK_BOX(box), build_hint_label("Zmiany motywu, ikon i czcionki sa zapisywane do gtk-3.0/gtk-4.0 settings.ini"));

switch (current_mode_from_disk()) {
case THEME_LIGHT:
gtk_check_button_set_active(ui->light_btn, TRUE);
break;
case THEME_DARK:
gtk_check_button_set_active(ui->dark_btn, TRUE);
break;
default:
gtk_check_button_set_active(ui->auto_btn, TRUE);
break;
}

gchar *v = read_gtk_setting("gtk-theme-name");
if (v) {
set_dropdown_to_text(ui->gtk_theme_drop, v);
g_free(v);
}
v = read_gtk_setting("gtk-icon-theme-name");
if (v) {
set_dropdown_to_text(ui->icon_theme_drop, v);
g_free(v);
}
v = read_gtk_setting("gtk-font-name");
if (v && *v) {
gtk_editable_set_text(GTK_EDITABLE(ui->font_entry), v);
g_free(v);
} else {
gtk_editable_set_text(GTK_EDITABLE(ui->font_entry), "Inter 10");
g_free(v);
}

return box;
}

static GtkWidget *
build_network_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Siec i Bluetooth"));

GtkWidget *air_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(air_row), gtk_label_new("Tryb samolotowy"));
ui->airplane_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->airplane_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(air_row), GTK_WIDGET(ui->airplane_switch));
gtk_box_append(GTK_BOX(box), air_row);
g_signal_connect(ui->airplane_switch, "state-set", G_CALLBACK(on_airplane_state_set), ui);

GtkWidget *wifi_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(wifi_row), gtk_label_new("Wi-Fi"));
ui->wifi_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->wifi_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(wifi_row), GTK_WIDGET(ui->wifi_switch));
gtk_box_append(GTK_BOX(box), wifi_row);
g_signal_connect(ui->wifi_switch, "state-set", G_CALLBACK(on_wifi_state_set), ui);

GtkWidget *bt_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(bt_row), gtk_label_new("Bluetooth"));
ui->bt_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->bt_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(bt_row), GTK_WIDGET(ui->bt_switch));
gtk_box_append(GTK_BOX(box), bt_row);
g_signal_connect(ui->bt_switch, "state-set", G_CALLBACK(on_bt_state_set), ui);

GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
GtkWidget *advanced_btn = gtk_button_new_with_label("Zaawansowana konfiguracja");
g_signal_connect(advanced_btn, "clicked", G_CALLBACK(on_open_network_advanced), ui);
gtk_box_append(GTK_BOX(buttons), advanced_btn);
GtkWidget *refresh_btn = gtk_button_new_with_label("Odswiez");
g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_all), ui);
gtk_box_append(GTK_BOX(buttons), refresh_btn);
gtk_box_append(GTK_BOX(box), buttons);

ui->network_label = GTK_LABEL(gtk_label_new(""));
gtk_widget_set_halign(GTK_WIDGET(ui->network_label), GTK_ALIGN_START);
gtk_label_set_wrap(ui->network_label, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->network_label));

return box;
}

static GtkWidget *
build_audio_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Dzwiek"));

GtkWidget *vol_label = gtk_label_new("Glosnosc wyjscia");
gtk_widget_set_halign(vol_label, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), vol_label);

ui->volume_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1));
gtk_scale_set_draw_value(ui->volume_scale, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->volume_scale));
g_signal_connect(ui->volume_scale, "value-changed", G_CALLBACK(on_volume_value_changed), ui);

GtkWidget *mute_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(mute_row), gtk_label_new("Mute wyjscia"));
ui->mute_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->mute_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(mute_row), GTK_WIDGET(ui->mute_switch));
gtk_box_append(GTK_BOX(box), mute_row);
g_signal_connect(ui->mute_switch, "state-set", G_CALLBACK(on_mute_state_set), ui);

GtkWidget *mic_label = gtk_label_new("Glosnosc mikrofonu");
gtk_widget_set_halign(mic_label, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), mic_label);

ui->mic_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1));
gtk_scale_set_draw_value(ui->mic_scale, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->mic_scale));
g_signal_connect(ui->mic_scale, "value-changed", G_CALLBACK(on_mic_value_changed), ui);

GtkWidget *mic_mute_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(mic_mute_row), gtk_label_new("Mute mikrofonu"));
ui->mic_mute_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->mic_mute_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(mic_mute_row), GTK_WIDGET(ui->mic_mute_switch));
gtk_box_append(GTK_BOX(box), mic_mute_row);
g_signal_connect(ui->mic_mute_switch, "state-set", G_CALLBACK(on_mic_mute_state_set), ui);

GtkWidget *advanced_btn = gtk_button_new_with_label("Zaawansowane audio");
g_signal_connect(advanced_btn, "clicked", G_CALLBACK(on_open_audio_advanced), ui);
gtk_box_append(GTK_BOX(box), advanced_btn);

ui->audio_label = GTK_LABEL(gtk_label_new(""));
gtk_widget_set_halign(GTK_WIDGET(ui->audio_label), GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->audio_label));

return box;
}

static GtkWidget *
build_power_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Zasilanie"));

GtkWidget *b_label = gtk_label_new("Jasnosc");
gtk_widget_set_halign(b_label, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), b_label);

ui->brightness_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 100, 1));
gtk_scale_set_draw_value(ui->brightness_scale, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->brightness_scale));
g_signal_connect(ui->brightness_scale, "value-changed", G_CALLBACK(on_brightness_value_changed), ui);

GtkWidget *p_label = gtk_label_new("Power profile");
gtk_widget_set_halign(p_label, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), p_label);

const char *profiles[] = { "balanced", "power-saver", "performance", NULL };
ui->profile_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(profiles));
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->profile_drop));
g_signal_connect(ui->profile_drop, "notify::selected", G_CALLBACK(on_power_profile_selected), ui);

GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
GtkWidget *suspend_btn = gtk_button_new_with_label("Uspij");
g_signal_connect(suspend_btn, "clicked", G_CALLBACK(on_suspend_clicked), ui);
gtk_box_append(GTK_BOX(actions), suspend_btn);
GtkWidget *reboot_btn = gtk_button_new_with_label("Restart");
g_signal_connect(reboot_btn, "clicked", G_CALLBACK(on_reboot_clicked), ui);
gtk_box_append(GTK_BOX(actions), reboot_btn);
GtkWidget *poweroff_btn = gtk_button_new_with_label("Wylacz");
g_signal_connect(poweroff_btn, "clicked", G_CALLBACK(on_poweroff_clicked), ui);
gtk_box_append(GTK_BOX(actions), poweroff_btn);
gtk_box_append(GTK_BOX(box), actions);

ui->power_label = GTK_LABEL(gtk_label_new(""));
gtk_widget_set_halign(GTK_WIDGET(ui->power_label), GTK_ALIGN_START);
gtk_label_set_wrap(ui->power_label, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->power_label));

return box;
}

static GtkWidget *
build_display_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Ekrany"));

GtkWidget *scale_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(scale_row), gtk_label_new("Skala"));
const char *scales[] = { "100%", "125%", "150%", "200%", NULL };
ui->display_scale_drop = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(scales));
gtk_widget_set_hexpand(GTK_WIDGET(ui->display_scale_drop), TRUE);
gtk_box_append(GTK_BOX(scale_row), GTK_WIDGET(ui->display_scale_drop));
gtk_box_append(GTK_BOX(box), scale_row);

GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
GtkWidget *apply_btn = gtk_button_new_with_label("Zastosuj skale");
g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_display_apply_scale), ui);
gtk_box_append(GTK_BOX(buttons), apply_btn);
GtkWidget *advanced_btn = gtk_button_new_with_label("Zaawansowane ekrany");
g_signal_connect(advanced_btn, "clicked", G_CALLBACK(on_open_display_advanced), ui);
gtk_box_append(GTK_BOX(buttons), advanced_btn);
gtk_box_append(GTK_BOX(box), buttons);

ui->display_label = GTK_LABEL(gtk_label_new(""));
gtk_widget_set_halign(GTK_WIDGET(ui->display_label), GTK_ALIGN_START);
gtk_label_set_wrap(ui->display_label, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->display_label));

return box;
}

static GtkWidget *
build_input_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Mysz i touchpad"));

GtkWidget *speed_label = gtk_label_new("Predkosc myszy");
gtk_widget_set_halign(speed_label, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), speed_label);

ui->mouse_speed_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -1.0, 1.0, 0.01));
gtk_scale_set_draw_value(ui->mouse_speed_scale, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->mouse_speed_scale));
g_signal_connect(ui->mouse_speed_scale, "value-changed", G_CALLBACK(on_mouse_speed_changed), ui);

GtkWidget *natural_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(natural_row), gtk_label_new("Natural scroll"));
ui->natural_scroll_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->natural_scroll_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(natural_row), GTK_WIDGET(ui->natural_scroll_switch));
gtk_box_append(GTK_BOX(box), natural_row);
g_signal_connect(ui->natural_scroll_switch, "state-set", G_CALLBACK(on_natural_scroll_state_set), ui);

GtkWidget *tap_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(tap_row), gtk_label_new("Tap to click"));
ui->tap_to_click_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->tap_to_click_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(tap_row), GTK_WIDGET(ui->tap_to_click_switch));
gtk_box_append(GTK_BOX(box), tap_row);
g_signal_connect(ui->tap_to_click_switch, "state-set", G_CALLBACK(on_tap_to_click_state_set), ui);

GtkWidget *advanced_btn = gtk_button_new_with_label("Zaawansowane urzadzenia wejscia");
g_signal_connect(advanced_btn, "clicked", G_CALLBACK(on_open_input_advanced), ui);
gtk_box_append(GTK_BOX(box), advanced_btn);

ui->input_label = GTK_LABEL(gtk_label_new(""));
gtk_widget_set_halign(GTK_WIDGET(ui->input_label), GTK_ALIGN_START);
gtk_label_set_wrap(ui->input_label, TRUE);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->input_label));

return box;
}

static GtkWidget *
build_notifications_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("Powiadomienia"));

GtkWidget *dnd_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
gtk_box_append(GTK_BOX(dnd_row), gtk_label_new("Do not disturb"));
ui->dnd_switch = GTK_SWITCH(gtk_switch_new());
gtk_widget_set_halign(GTK_WIDGET(ui->dnd_switch), GTK_ALIGN_END);
gtk_box_append(GTK_BOX(dnd_row), GTK_WIDGET(ui->dnd_switch));
gtk_box_append(GTK_BOX(box), dnd_row);
g_signal_connect(ui->dnd_switch, "state-set", G_CALLBACK(on_dnd_state_set), ui);

GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
GtkWidget *open_btn = gtk_button_new_with_label("Otworz panel");
g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_notifications_panel), ui);
gtk_box_append(GTK_BOX(buttons), open_btn);
GtkWidget *clear_btn = gtk_button_new_with_label("Wyczysc wszystkie");
g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_notifications), ui);
gtk_box_append(GTK_BOX(buttons), clear_btn);
gtk_box_append(GTK_BOX(box), buttons);

ui->notifications_label = GTK_LABEL(gtk_label_new(""));
gtk_widget_set_halign(GTK_WIDGET(ui->notifications_label), GTK_ALIGN_START);
gtk_box_append(GTK_BOX(box), GTK_WIDGET(ui->notifications_label));

return box;
}

static GtkWidget *
build_system_page(struct app_widgets *ui)
{
GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(box, "card");
gtk_box_append(GTK_BOX(box), build_card_title("System"));

GtkWidget *open_cfg_btn = gtk_button_new_with_label("Otworz folder konfiguracji");
g_signal_connect(open_cfg_btn, "clicked", G_CALLBACK(on_open_config), ui);
gtk_box_append(GTK_BOX(box), open_cfg_btn);

GtkWidget *reload_btn = gtk_button_new_with_label("Przeladuj powloke");
g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_shell), ui);
gtk_box_append(GTK_BOX(box), reload_btn);

GtkWidget *logout_btn = gtk_button_new_with_label("Wyloguj / zakoncz sesje");
g_signal_connect(logout_btn, "clicked", G_CALLBACK(on_logout_clicked), ui);
gtk_box_append(GTK_BOX(box), logout_btn);

GtkWidget *refresh_btn = gtk_button_new_with_label("Odswiez wszystkie dane");
g_signal_connect(refresh_btn, "clicked", G_CALLBACK(on_refresh_all), ui);
gtk_box_append(GTK_BOX(box), refresh_btn);

gtk_box_append(GTK_BOX(box), build_hint_label("Panel stale odswieza dynamiczne dane (siec, dzwiek, zasilanie, ekrany, wejscie, powiadomienia)."));

return box;
}

static GtkWidget *
build_ui(struct app_widgets *ui)
{
GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
gtk_widget_add_css_class(root, "root");

GtkWidget *title = gtk_label_new("Karton Control Center");
gtk_widget_add_css_class(title, "title");
gtk_widget_set_halign(title, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(root), title);

GtkWidget *subtitle = gtk_label_new("Pelne ustawienia srodowiska Karton: wyglad, siec, dzwiek, zasilanie, ekrany, wejscie i powiadomienia");
gtk_widget_add_css_class(subtitle, "subtitle");
gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
gtk_box_append(GTK_BOX(root), subtitle);

GtkWidget *pane = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
gtk_widget_add_css_class(pane, "pane");
gtk_widget_set_vexpand(pane, TRUE);
gtk_box_append(GTK_BOX(root), pane);

ui->stack = GTK_STACK(gtk_stack_new());
gtk_stack_set_transition_type(ui->stack, GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
gtk_widget_set_hexpand(GTK_WIDGET(ui->stack), TRUE);
gtk_widget_set_vexpand(GTK_WIDGET(ui->stack), TRUE);

GtkWidget *sidebar = gtk_stack_sidebar_new();
gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), ui->stack);
gtk_widget_set_size_request(sidebar, 200, -1);
gtk_box_append(GTK_BOX(pane), sidebar);
gtk_box_append(GTK_BOX(pane), GTK_WIDGET(ui->stack));

gtk_stack_add_titled(ui->stack, build_appearance_page(ui), "appearance", "Wyglad");
gtk_stack_add_titled(ui->stack, build_network_page(ui), "network", "Siec");
gtk_stack_add_titled(ui->stack, build_audio_page(ui), "audio", "Dzwiek");
gtk_stack_add_titled(ui->stack, build_power_page(ui), "power", "Zasilanie");
gtk_stack_add_titled(ui->stack, build_display_page(ui), "display", "Ekrany");
gtk_stack_add_titled(ui->stack, build_input_page(ui), "input", "Mysz i touchpad");
gtk_stack_add_titled(ui->stack, build_notifications_page(ui), "notifications", "Powiadomienia");
gtk_stack_add_titled(ui->stack, build_system_page(ui), "system", "System");

ui->status = GTK_LABEL(gtk_label_new("Ready"));
gtk_widget_set_halign(GTK_WIDGET(ui->status), GTK_ALIGN_START);
gtk_widget_add_css_class(GTK_WIDGET(ui->status), "status-ok");
gtk_box_append(GTK_BOX(root), GTK_WIDGET(ui->status));

gtk_stack_set_visible_child_name(ui->stack, startup_page);
refresh_all_dynamic_pages(ui);
g_timeout_add_seconds(5, on_periodic_refresh, ui);

return root;
}

static const char *
normalize_page(const char *candidate)
{
if (!candidate) {
return "appearance";
}
if (!strcmp(candidate, "appearance")
|| !strcmp(candidate, "network")
|| !strcmp(candidate, "audio")
|| !strcmp(candidate, "power")
|| !strcmp(candidate, "display")
|| !strcmp(candidate, "input")
|| !strcmp(candidate, "notifications")
|| !strcmp(candidate, "system")) {
return candidate;
}
return "appearance";
}

static void
parse_startup_page(int argc, char **argv)
{
for (int i = 1; i < argc; i++) {
if (!strcmp(argv[i], "--page") && i + 1 < argc) {
startup_page = normalize_page(argv[i + 1]);
i++;
continue;
}
if (g_str_has_prefix(argv[i], "--page=")) {
startup_page = normalize_page(argv[i] + 7);
}
}
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
(void)user_data;
GtkCssProvider *provider = gtk_css_provider_new();
gtk_css_provider_load_from_string(provider, app_css);
gtk_style_context_add_provider_for_display(
gdk_display_get_default(),
GTK_STYLE_PROVIDER(provider),
GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
g_object_unref(provider);

struct app_widgets *ui = g_new0(struct app_widgets, 1);
ui->window = GTK_WINDOW(gtk_application_window_new(app));
gtk_window_set_title(ui->window, "Karton Settings");
gtk_window_set_default_size(ui->window, 1120, 700);

GtkWidget *content = build_ui(ui);
gtk_window_set_child(ui->window, content);
gtk_window_present(ui->window);
}

int
main(int argc, char **argv)
{
parse_startup_page(argc, argv);

GtkApplication *app = gtk_application_new("org.karton.settings", G_APPLICATION_DEFAULT_FLAGS);
g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
int status = g_application_run(G_APPLICATION(app), argc, argv);
g_object_unref(app);
return status;
}
