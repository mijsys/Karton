project('karton-settings', 'c',
  version: '0.8.0',
  default_options: ['warning_level=2', 'c_std=c11']
)

localedir_abs = join_paths(get_option('prefix'), get_option('localedir'))
add_project_arguments('-DLOCALEDIR="@0@"'.format(localedir_abs), language: 'c')

gtk4_dep = dependency('gtk4')
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')

src_files = [
  'src/page-appearance.c',
  'src/page-display.c',
  'src/page-audio.c',
  'src/page-input.c',
  'src/page-network.c',
  'src/page-bluetooth.c',
  'src/page-power.c',
  'src/page-users.c',
  'src/page-security.c',
  'src/page-notifications.c',
  'src/page-default-apps.c',
  'src/page-updates.c',
  'src/page-region.c',
  'src/page-accessibility.c',
  'src/page-files-disks.c',
  'src/page-session-startup.c',
  'src/page-system-info.c',
  'src/page-advanced.c',
  'src/main.c',
  'src/window.c'
]

subdir('po')

gnome = import('gnome')
resources = gnome.compile_resources(
  'karton-settings-resources',
  'data/karton-settings.gresource.xml',
  source_dir: 'data',
  c_name: 'karton_settings'
)

executable('karton-settings',
  src_files, resources,
  dependencies: [gtk4_dep, glib_dep, gio_dep, gio_unix_dep],
  install: true
)

install_data(
  ['data/io.karton.Settings.desktop'],
  install_dir: join_paths(get_option('datadir'), 'applications')
)

install_data(
  ['data/icons/io.karton.Settings.svg'],
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps')
)
