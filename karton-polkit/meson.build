project('karton-polkit', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2'
  ]
)

gtk_dep = dependency('gtk4')
polkit_agent_dep = dependency('polkit-agent-1')
polkit_gobject_dep = dependency('polkit-gobject-1')

executable('karton-polkit',
  'src/main.c',
  dependencies: [gtk_dep, polkit_agent_dep, polkit_gobject_dep],
  install: true,
  install_dir: get_option('bindir')
)

install_data('data/io.karton.Polkit.desktop',
  install_dir: get_option('sysconfdir') / 'xdg/autostart'
)