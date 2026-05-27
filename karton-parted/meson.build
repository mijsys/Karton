project('karton-parted', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2'
  ]
)
gtk_dep = dependency('gtk4')

parted_dep = dependency('libparted')

executable('karton-parted',
  'src/main.c',
  dependencies: [gtk_dep, parted_dep],
  install: true,
  install_dir: get_option('bindir')
)
