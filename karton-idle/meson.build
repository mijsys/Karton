# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-idle',
  'c',
  version: '0.8.1',
  meson_version: '>=0.59.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

glib = dependency('glib-2.0')
wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')

protocol_xml = files('protocols/ext-idle-notify-v1.xml')

sources = files('src/karton-idle.c')

sources += custom_target(
  'karton_ext_idle_notifier_protocol_c',
  input: protocol_xml,
  output: 'karton-ext-idle-notify-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)
sources += custom_target(
  'karton_ext_idle_notifier_client_h',
  input: protocol_xml,
  output: 'karton-ext-idle-notify-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

executable(
  'karton-idle',
  sources,
  dependencies: [glib, wayland_client],
  install: true,
)