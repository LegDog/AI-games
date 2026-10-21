project('tic-tac-toe', 'c', version: '1.0.0',
  license: 'GPL-3.0-or-later', license_files: 'COPYING',
  default_options: ['c_std=c11', 'warning_level=3'])

glib = dependency('glib-2.0', version: '>=2.66', include_type: 'system')
engine = static_library('game', ['src/game.c', 'src/minimax.c', 'src/i18n.c'], dependencies: glib)
engine_dep = declare_dependency(link_with: engine, dependencies: glib,
  include_directories: include_directories('src'))
executable('tournament', '../tools/tournament.c', dependencies: engine_dep)
test('minimax', executable('test-minimax', 'tests/test-minimax.c', dependencies: engine_dep), timeout: 120)
test('i18n', executable('test-i18n', 'tests/test-i18n.c', dependencies: engine_dep))
test('game', executable('test-game', 'tests/test-game.c', dependencies: engine_dep))

if get_option('app')
  gtk = dependency('gtk4', version: '>=4.10', include_type: 'system')
  adw = dependency('libadwaita-1', version: '>=1.4', include_type: 'system')
  soup = dependency('libsoup-3.0', version: '>=3.0', include_type: 'system')
  json = dependency('json-glib-1.0', version: '>=1.6', include_type: 'system')
  ollama = static_library('ollama', '../tools/ollama-events.c', '../tools/ollama-harness.c', 'src/ollama.c', dependencies: [engine_dep, soup, json])
  ollama_dep = declare_dependency(link_with: ollama, dependencies: [engine_dep, soup, json])
  viewer=static_library('ollama-view','../tools/ollama-view.c',dependencies:[ollama_dep,gtk,adw])
  viewer_dep=declare_dependency(link_with:viewer,dependencies:[ollama_dep,gtk,adw])
  test('ollama-log', executable('test-ollama-log', 'tests/test-ollama-log.c', dependencies: ollama_dep))
  test('ollama', executable('test-ollama', 'tests/test-ollama.c', dependencies: ollama_dep), timeout: 30)
  math = meson.get_compiler('c').find_library('m', required: true)
  executable('tic-tac-toe', 'src/main.c',
    dependencies: [viewer_dep, ollama_dep, gtk, adw, math], install: true)
  if get_option('ui_tests')
    executable('screenshot', '../tools/screenshot.c',
      c_args: [],
      dependencies: [viewer_dep, ollama_dep, gtk, adw, math])
    test('ui', executable('test-ui', 'tests/test-ui.c',
      dependencies: [viewer_dep, ollama_dep, gtk, adw, math]), timeout: 60)
  endif
  install_data('data/io.github.aigames.TicTacToe.desktop',
    install_dir: get_option('datadir') / 'applications')
  install_data('data/io.github.aigames.TicTacToe.svg',
    install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')
endif

if get_option('app')
  test('ollama-harness-game',executable('test-ollama-harness-game','../tools/test-ollama-harness-game.c',include_directories:include_directories('tests'),c_args:['-DHARNESS_ASSISTED'],dependencies:ollama_dep),timeout:60)
endif

if get_option('app')
  executable('ollama-harness-eval','../tools/ollama-harness-eval.c',include_directories:include_directories('tests'),dependencies:ollama_dep)
endif
