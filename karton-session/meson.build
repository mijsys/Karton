# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-session',
  'c',
  version: '0.7.5',
  meson_version: '>=0.59.0',
)

subdir('po')

executable(
  'karton-screenshot',
  files('src/karton-screenshot.c'),
  install: true,
)

install_data(
  [
    'bin/karton-launcher',
    'bin/karton-login-manager',
    'bin/karton-notifyd',
    'bin/karton-notify-log',
    'bin/karton-apply-theme',
    'bin/karton-settingsd',
    'bin/karton-sessiond',
  ],
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  ['theme.toml.example'],
  install_dir: join_paths(get_option('datadir'), 'karton'),
)
