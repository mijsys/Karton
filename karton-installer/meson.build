project(
  'karton-installer',
  'c',
  version: '0.8.1',
  meson_version: '>=0.59.0',
)

gtk_dep = dependency('gtk4')
gio_dep = dependency('gio-2.0')
locale_dir = join_paths(get_option('prefix'), get_option('localedir'))
data_dir = join_paths(get_option('prefix'), get_option('datadir'))
c_args = [
  '-DLOCALEDIR="@0@"'.format(locale_dir),
  '-DDATADIR="@0@"'.format(data_dir),
]

subdir('po')

executable(
  'karton-welcome',
  files('src/karton-welcome.c'),
  dependencies: [gtk_dep, gio_dep],
  c_args: c_args,
  install: true,
)

executable(
  'karton-install',
  files('src/karton-install.c'),
  dependencies: [gtk_dep, gio_dep],
  c_args: c_args,
  install: true,
)

executable(
  'karton-shop',
  files('src/karton-shop.c'),
  dependencies: [gtk_dep, gio_dep],
  c_args: c_args,
  install: true,
)

install_data(
  [
    'data/io.karton.Welcome.desktop',
    'data/io.karton.Install.desktop',
    'data/io.karton.Shop.desktop',
  ],
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  [
    'data/icons/io.karton.Welcome.svg',
    'data/icons/io.karton.Install.svg',
    'data/icons/io.karton.Shop.svg',
  ],
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps'),
)

install_data(
  ['../repo/install-arch.sh'],
  install_dir: join_paths(get_option('datadir'), 'karton-installer'),
  install_mode: 'rwxr-xr-x',
)