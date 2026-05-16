# SPDX-FileCopyrightText: 2026 mijsys
project(
  'karton-lock',
  'c',
  version: '0.8.0',
  meson_version: '>=0.59.0',
)

localedir_abs = join_paths(get_option('prefix'), get_option('localedir'))
add_project_arguments('-DLOCALEDIR="@0@"'.format(localedir_abs), language: 'c')

glib = dependency('glib-2.0')
gtk4 = dependency('gtk4')
gtk4_layer_shell = dependency('gtk4-layer-shell-0')
pam_dep = dependency('pam', required: false)

conf = configuration_data()
conf.set('HAVE_PAM', pam_dep.found() ? 1 : 0)
configure_file(
  output: 'config.h',
  configuration: conf,
)

subdir('po')

deps = [glib, gtk4, gtk4_layer_shell]
if pam_dep.found()
  deps += pam_dep
  install_data(
    ['data/pam/karton-lock'],
    install_dir: join_paths(get_option('sysconfdir'), 'pam.d'),
  )
endif

executable(
  'karton-lock',
  files('src/karton-lock.c'),
  dependencies: deps,
  include_directories: include_directories('.'),
  install: true,
)

install_data(
  ['lock-style.example'],
  install_dir: join_paths(get_option('datadir'), 'karton'),
)
