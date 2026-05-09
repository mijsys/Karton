# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-settings',
  'c',
  version: '0.1',
  meson_version: '>=0.59.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

localedir_abs = join_paths(get_option('prefix'), get_option('localedir'))
add_project_arguments('-DLOCALEDIR="@0@"'.format(localedir_abs), language: 'c')

gtk4 = dependency('gtk4')
glib = dependency('glib-2.0')

subdir('po')

executable(
  'karton-settings',
  files(
    'src/main.c',
    'src/backend/config.c',
    'src/backend/system.c',
    'src/ui/window.c',
  ),
  include_directories: [include_directories('src')],
  dependencies: [gtk4, glib],
  install: true,
)

install_data(
  files(
    'data/karton-settings.desktop',
    'data/io.karton.Settings.desktop',
  ),
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  files(
    'data/icons/hicolor/scalable/apps/karton-settings.svg',
    'data/icons/hicolor/scalable/apps/io.karton.Settings.svg',
  ),
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'),
)
