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
skips before claiming full validation. CI runs both apps and the GUI tests on
Ubuntu with a virtual display.

## Games and algorithms

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

Update `src/i18n-catalog.inc` in both games. Columns are English, Portuguese,
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
