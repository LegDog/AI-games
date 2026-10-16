project('jogo-da-velha', 'c', version: '1.0.0',
  license: 'GPL-3.0-or-later', license_files: 'COPYING',
  default_options: ['c_std=c11', 'warning_level=3'])

glib = dependency('glib-2.0', version: '>=2.66')
engine = static_library('game', ['src/game.c', 'src/minimax.c'], dependencies: glib)
engine_dep = declare_dependency(link_with: engine, dependencies: glib,
  include_directories: include_directories('src'))
test('minimax', executable('test-minimax', 'tests/test-minimax.c', dependencies: engine_dep), timeout: 120)
test('game', executable('test-game', 'tests/test-game.c', dependencies: engine_dep))

if get_option('app')
  gtk = dependency('gtk4', version: '>=4.10')
  adw = dependency('libadwaita-1', version: '>=1.4')
  soup = dependency('libsoup-3.0', version: '>=3.0')
  json = dependency('json-glib-1.0', version: '>=1.6')
  ollama = static_library('ollama', 'src/ollama.c', dependencies: [engine_dep, soup, json])
  ollama_dep = declare_dependency(link_with: ollama, dependencies: [engine_dep, soup, json])
  test('ollama-log', executable('test-ollama-log', 'tests/test-ollama-log.c', dependencies: [engine_dep, soup, json]))
  test('ollama', executable('test-ollama', 'tests/test-ollama.c', dependencies: ollama_dep), timeout: 30)
  math = meson.get_compiler('c').find_library('m', required: true)
  executable('jogo-da-velha', 'src/main.c',
    dependencies: [ollama_dep, gtk, adw, math], install: true)
  if get_option('ui_tests')
    test('ui', executable('test-ui', 'tests/test-ui.c',
      dependencies: [ollama_dep, gtk, adw, math]), timeout: 30)
  endif
  install_data('data/io.github.aigames.JogoDaVelha.desktop',
    install_dir: get_option('datadir') / 'applications')
  install_data('data/io.github.aigames.JogoDaVelha.svg',
    install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
endif
