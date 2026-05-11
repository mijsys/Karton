# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-shell',
  'c',
  version: '0.7.5',
  meson_version: '>=0.59.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

localedir_abs = join_paths(get_option('prefix'), get_option('localedir'))
add_project_arguments('-DLOCALEDIR="@0@"'.format(localedir_abs), language: 'c')

wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.39')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
glib = dependency('glib-2.0')
gio = dependency('gio-2.0')
gtk4 = dependency('gtk4')
xkbcommon = dependency('xkbcommon')
libpulse = dependency('libpulse')

wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

sources = files(
  'src/karton-shell.c',
  'src/pool-buffer.c',
)

xdg_shell_xml = wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml'
layer_shell_xml = 'protocols/wlr-layer-shell-unstable-v1.xml'
foreign_toplevel_xml = 'protocols/wlr-foreign-toplevel-management-unstable-v1.xml'

subdir('po')

sources += custom_target(
  'karton_xdg_shell_c',
  input: xdg_shell_xml,
  output: 'karton-xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
sources += custom_target(
  'karton_xdg_shell_client_h',
  input: xdg_shell_xml,
  output: 'karton-xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

sources += custom_target(
  'karton_layer_shell_c',
  input: layer_shell_xml,
  output: 'karton-layer-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
sources += custom_target(
  'karton_layer_shell_client_h',
  input: layer_shell_xml,
  output: 'karton-layer-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

sources += custom_target(
  'karton_foreign_toplevel_c',
  input: foreign_toplevel_xml,
  output: 'karton-foreign-toplevel-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
sources += custom_target(
  'karton_foreign_toplevel_client_h',
  input: foreign_toplevel_xml,
  output: 'karton-foreign-toplevel-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'karton-shell',
  sources,
  include_directories: [include_directories('src')],
  dependencies: [
    cairo,
    pangocairo,
    gdk_pixbuf,
    glib,
    gio,
    xkbcommon,
    wayland_client,
  ],
  install: true,
)

executable(
  'karton-system-status',
  files('src/karton-system-status.c'),
  dependencies: [glib, gio, libpulse],
  install: true,
)

executable(
  'karton-dialogd',
  files('src/karton-dialogd.c'),
  dependencies: [glib, gio, gtk4],
  install: true,
)

executable(
  'karton-dialog',
  files('src/karton-dialog.c'),
  dependencies: [glib, gio],
  install: true,
)

install_data(
  ['bin/karton-top-panel', 'bin/karton-side-dock'],
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x',
)
