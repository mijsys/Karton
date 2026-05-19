sed -i '4928,4958c\
for (int i = 0; i < (int)preview_count; i++) {\
double fx, fy, fw, fh;\
if (!launcher_favorite_tile_rect(panel, app, i, &fx, &fy, &fw, &fh)) {\
continue;\
}\
\
bool tilhover = (i == app->launcher_hover_favorite);\
if (tilhover) {\
set_source_hex_a(cairo, launcher_text, dark ? 0.08 : 0.06);\
rounded_rect(cairo, fx, fy, fw, fh, 12.0);\
cairo_fill(cairo);\
}\
\
double icon_bg_size = 46.0;\
double icon_bg_x = fx + (fw - icon_bg_size) / 2.0;\
double icon_bg_y = fy + 4.0;\
set_source_hex_a(cairo, launcher_accent, dark ? 0.16 : 0.12);\
rounded_rect(cairo, icon_bg_x, icon_bg_y, icon_bg_size, icon_bg_size, 14.0);\
cairo_fill(cairo);\
\
const struct launcher_entry *fav = &app->launcher_entries[preview[i]];\
draw_launcher_entry_icon(cairo, app, fav->icon_name,\
icon_bg_x + icon_bg_size * 0.5, icon_bg_y + icon_bg_size * 0.5, 26.0, 32.0, dark);\
\
draw_pango_text(cairo, "Noto Sans", PANGO_WEIGHT_MEDIUM,\
10.0, launcher_text, 0.90, fx - 4.0, fy + 52.0,\
(int)fw + 8, PANGO_ALIGN_CENTER, fav->name);\
}' src/karton-shell.c
