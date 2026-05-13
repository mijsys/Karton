project('karton-terminal', 'c',
  version: '0.7.6',
  default_options: ['warning_level=2', 'c_std=c11']
)

localedir_abs = join_paths(get_option('prefix'), get_option('localedir'))
add_project_arguments('-DLOCALEDIR="@0@"'.format(localedir_abs), language: 'c')

gtk_dep = dependency('gtk+-3.0')
vte_dep = dependency('vte-2.91')

subdir('po')

executable('karton-terminal',
  ['src/main.c', 'src/window.c'],
  dependencies: [gtk_dep, vte_dep],
  install: true
)

install_data(
  ['data/io.karton.Terminal.desktop'],
  install_dir: join_paths(get_option('datadir'), 'applications')
)

install_data(
  ['data/icons/io.karton.Terminal.svg'],
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', 'scalable', 'apps')
)
