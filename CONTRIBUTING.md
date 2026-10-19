# Contributing to AI-games

Contributions are welcome: new games and algorithms, reproducible experiments,
regression cases, translations, documentation, and bug fixes. Use English for
code comments, documentation, issues, and pull requests. The interface supports
English, Portuguese, Spanish, and Italian.

## Development

See [the README](README.md) for dependencies and build commands. From the root:

```sh
make MESON_ARGS=-Dwerror=true
make test
make test-ollama MESON_ARGS=-Dwerror=true
python3 scripts/test-tournament.py
python3 scripts/test-pentalpha-runner.py
python3 scripts/test-heptalpha-runner.py
python3 scripts/test-peg-solitaire-runner.py
python3 scripts/test-pentalpha-crossed-circle-runner.py
python3 scripts/test-reversi-tournament.py
python3 scripts/test-hex-tournament.py
python3 scripts/test-tavli-tournament.py
python3 scripts/test-sokoban-runner.py
python3 scripts/test-go-tournament.py
python3 scripts/test-pentago-tournament.py
python3 scripts/generate-pentalpha-crossed-circle-board.py --check
python3 scripts/generate-heptalpha-board.py --check
python3 scripts/test-audit-history.py
python3 scripts/audit-history.py
make test-ui MESON_ARGS=-Dwerror=true
```

GUI tests need a display. On a Linux machine with Xvfb, xauth, and D-Bus:

```sh
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- make test-ui MESON_ARGS=-Dwerror=true
```

Ollama protocol tests use a mock service; no model download is required. Local
HTTP tests may skip when socket creation is blocked. Check the test log for
skips before claiming full validation. CI runs all twelve apps and the GUI tests on
Ubuntu with a virtual display.

## Games and algorithms

Pentalpha and Heptalpha use `src/search.*` for single-player maximizing search
and dedicated `puzzle-runner` executables. Heptalpha uses 64-bit move masks and
checked-in generated curve/topology data. Their READMEs document their controllers
and animation. Use `scripts/generate-heptalpha-board.py --check` to detect artwork
or topology drift; normal builds do not regenerate these assets.

Each game's directory contains its rules and scoring (`src/game.*`), Minimax
search (`src/minimax.*`), Ollama integration (`src/ollama.*`), GTK interface
(`src/main.c`), catalogs, and tests. Shared experiment and screenshot drivers
live in `tools/` and are compiled against each game's engine/interface.

For a new game, add its directory to the root Makefile and CI checks. Document
board representation, legal moves, termination, controls, evaluation weights,
and search limits. Keep engine tests usable with `-Dapp=false`; graphical
and network dependencies belong behind the application option. Add tests for
rules, illegal input, scoring priorities, and terminal positions.

For a new algorithm, preserve the distinction between its own decisions and
external tactical assistance. Extend controller configuration, cancellation,
history, and tests. Use copied game state for background work and discard
results from canceled or superseded turns. Document randomness and limitations.

Use the [experiment guide](docs/experiments.md) to compare changes. Include
seeds, exact board or opening, player order, depths, code revision, and results.
Check both colors and mirrored positions where appropriate. A selected sample
is not a general win rate or a proof of perfect play.

## Translations

Update `src/i18n-catalog.inc` in all applications. Columns are English, Portuguese,
Spanish, and Italian; preserve printf placeholders and their order. Keep the
shared catalog/helper copies synchronized. Add GUI labels through the existing
translation bindings so they change without resetting a game. `make test`
checks catalogs, locales, preferences, and concurrent translation reads.

## Reporting and reviewing changes

Report the game, OS/library versions, revision, controller/model configuration,
steps to reproduce, and expected/actual behavior. Include a minimal board or
move sequence when possible. Sanitize logs first: they include complete model
communication. See [logging guidance](docs/logging.md).

Keep pull requests focused. Explain the behavior change, relevant tradeoffs,
and checks performed, including skipped checks. Add a regression test for a
rule, search, protocol, or lifecycle bug. Do not commit local histories, logs,
credentials, generated builds, or raw experiment exports. Screenshot images
intended for documentation should come from the actual application.

All contributions are provided under the repository's GPL-3.0-or-later license.
Use `SPDX-License-Identifier: GPL-3.0-or-later` on new project source files.
Avoid introducing code or assets without compatible licensing and attribution.

Peg Solitaire uses `src/search.*` for bounded Backtracking and A* plans. Its tests independently enumerate triangular positions, replay CSV jumps, and verify search limits, certification, cancellation, and atomic animation. Keep all translation catalogs synchronized.

Pentalfa Cruzado Circulado uses explicit lists for its 120 movements and 64-bit occupancy for 36 points. Its Search uses bounded iterative deepening and a symmetry-aware sparse cache. Only complete root iterations may be used for decisions or Ollama assistance. Keep the topology, SVG, and animation geometry synchronized with the board generator.

Crossed Circle graph/algebra variations live separately from the legacy `search.c` and Ollama assistance. Its legal entry–destination graph has two components: target 35 is experimental and 34 is provably optimal. Keep board results separate from proof metadata. Tests compare the graph bound against exhaustive subsets, Gauss–Jordan rank against connectivity, symmetry-search values against baseline, and legacy CSV outputs against pre-variation digests.

Reversi uses `src/search.*` for bounded iterative alpha-beta Minimax and UCT Monte Carlo tree search. Its bitboard rules are checked against independent coordinate-based capture and pass logic. Keep forced passes distinct from placements, preserve atomic flip animations, and record both winners and draws.

Hex uses fixed-stride seven-bitboard rows, six-neighbor connectivity, and an optional pie-rule action (bit 63). Swap reflects and recolors the opening to keep controller colors fixed. Compare connection distances against independent replay; preserve no-draw termination, atomic placements/swap, bounded search, and stale-response cancellation.

Tavli uses dynamic complete-turn enumeration and legal prefixes, not fixed move masks. Keep variant-specific rules, dice quotas, cube actor versus active player, and match transitions in the engine. Real dice must remain isolated from search RNGs. Its independent Python replay engine validates exported sessions; Expectiminimax is checked against 36 ordered rolls. Preserve draft undo without rerolls, atomic animations, and complete-decision retention under limits.

Pentago uses explicit compound placement/rotation moves (up to 288 legal actions),
not a 64-bit action mask. Immediate placement wins skip rotation; after rotation
check both colors, including simultaneous-line draws. Keep plain Minimax and
alpha-beta evaluation, move order, root windows, and seeded tie-breaking equal.
Independent replay checks complete turns. Preserve atomic animation, provisional
human placement, and fixed scrolling text panels that keep board size stable.

Use `tools/ui-panels.h` for board-adjacent message and rules panels; keep the
outer panel visible when its contents are empty or hidden. GUI tests use
`tools/test-ui-panels.h` to verify board position and size through text overflow,
scrolling, and message visibility changes in each application.

Ollama clients expose an optional observer on each decision's `GCancellable`
through `tools/ollama-events.h`. Attach it before starting the worker. Capture
must not mutate payloads or logging, change model decisions, or add network
requests. `tools/ollama-view.c` copies events to a main-context idle queue, owns
no borrowed app-state pointers, and discards stale generations on reset/undo.
Hex builds the shared extraction and viewer/clipboard/contrast tests; every
app's mock HTTP lifecycle test verifies that its actual transcript receives text.
