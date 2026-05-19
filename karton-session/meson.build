# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-session',
  'c',
  version: '0.8.1',
  meson_version: '>=0.59.0',
)

gtk_dep = dependency('gtk4')
poppler_dep = dependency('poppler-glib')
locale_dir = join_paths(get_option('prefix'), get_option('localedir'))
locale_c_args = ['-DLOCALEDIR="@0@"'.format(locale_dir)]

subdir('po')

executable(
  'karton-screenshot',
  files('src/karton-screenshot.c'),
  install: true,
)

executable(
  'karton-password-dialog',
  files('src/karton-password-dialog.c'),
  dependencies: [gtk_dep],
  c_args: locale_c_args,
  install: true,
)

executable(
  'karton-images',
  files('src/karton-images.c', 'src/karton-theme.c'),
  dependencies: [gtk_dep],
  c_args: locale_c_args,
  install: true,
)

executable(
  'karton-media',
  files('src/karton-media.c', 'src/karton-theme.c'),
  dependencies: [gtk_dep],
  c_args: locale_c_args,
  install: true,
)

executable(
  'karton-text',
  files('src/karton-text.c', 'src/karton-theme.c'),
  dependencies: [gtk_dep],
  c_args: locale_c_args,
  install: true,
)

executable(
  'karton-pdf',
  files('src/karton-pdf.c', 'src/karton-theme.c'),
  dependencies: [gtk_dep, poppler_dep],
  c_args: locale_c_args,
  install: true,
)

install_data(
  [
    'bin/karton-bootstrap-user',
    'bin/karton-launcher',
    'bin/karton-login-manager',
    'bin/karton-notifyd',
    'bin/karton-notify-log',
    'bin/karton-apply-theme',
    'bin/karton-settingsd',
    'bin/karton-sessiond',
    'bin/karton-restart-session',
  ],
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  [
    'data/io.karton.Images.desktop',
    'data/io.karton.Media.desktop',
    'data/io.karton.Text.desktop',
    'data/io.karton.PDF.desktop',
  ],
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  [
    'data/icons/io.karton.Images.svg',
    'data/icons/io.karton.Media.svg',
    'data/icons/io.karton.Text.svg',
    'data/icons/io.karton.PDF.svg',
  ],
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'),
)

install_data(
  ['theme.toml.example'],
  install_dir: join_paths(get_option('datadir'), 'karton'),
)
