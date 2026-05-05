# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-settings',
  'c',
  version: '0.2',
  meson_version: '>=0.59.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

gtk4 = dependency('gtk4')

executable(
  'karton-settings',
  files('src/main.c'),
  dependencies: [gtk4],
  install: true,
)

install_data(
  files('data/karton-settings.desktop'),
  install_dir: join_paths(get_option('datadir'), 'applications'),
)
