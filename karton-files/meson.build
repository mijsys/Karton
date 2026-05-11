project('karton-files', 'c',
  version: '0.7.5',
  default_options: ['warning_level=2', 'c_std=c11']
)

localedir_abs = join_paths(get_option('prefix'), get_option('localedir'))
add_project_arguments('-DLOCALEDIR="@0@"'.format(localedir_abs), language: 'c')
add_project_arguments('-Wno-deprecated-declarations', language: 'c')

gtk4_dep = dependency('gtk4')
glib_dep = dependency('glib-2.0')
gio_dep = dependency('gio-2.0')

src_files = [
  'src/main.c',
  'src/window.c'
]

subdir('po')

gnome = import('gnome')
resources = gnome.compile_resources(
  'karton-files-resources',
  'data/karton-files.gresource.xml',
  source_dir: 'data',
  c_name: 'karton_files'
)

executable('karton-files',
  src_files, resources,
  dependencies: [gtk4_dep, glib_dep, gio_dep],
  install: true
)

install_data(
  ['data/io.karton.Files.desktop'],
  install_dir: join_paths(get_option('datadir'), 'applications')
)

install_data(
  ['data/icons/io.karton.Files.svg'],
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps')
)
