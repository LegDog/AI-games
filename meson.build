project('jogo-da-velha', 'c', version: '1.0.0',
  license: 'GPL-3.0-or-later', license_files: 'COPYING',
  default_options: ['c_std=c11', 'warning_level=3'])

glib = dependency('glib-2.0', version: '>=2.66')
engine = static_library('game', 'src/game.c', dependencies: glib)
engine_dep = declare_dependency(link_with: engine, dependencies: glib,
  include_directories: include_directories('src'))
test('game', executable('test-game', 'tests/test-game.c', dependencies: engine_dep))

if get_option('app')
  gtk = dependency('gtk4', version: '>=4.10')
  adw = dependency('libadwaita-1', version: '>=1.4')
  math = meson.get_compiler('c').find_library('m', required: true)
  executable('jogo-da-velha', 'src/main.c',
    dependencies: [engine_dep, gtk, adw, math], install: true)
  if get_option('ui_tests')
    test('ui', executable('test-ui', 'tests/test-ui.c',
      dependencies: [engine_dep, gtk, adw, math]), timeout: 30)
  endif
  install_data('data/io.github.aigames.JogoDaVelha.desktop',
    install_dir: get_option('datadir') / 'applications')
  install_data('data/io.github.aigames.JogoDaVelha.svg',
    install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
endif
