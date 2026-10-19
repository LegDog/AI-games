# AI-games

This repository is a place to study, compare, and develop AI skills through
playing games. We may add more games and algorithms soon. Contributions are
welcome—feel free to contribute new games, algorithms, experiments, or improvements.
English is the primary language for project documentation and contributions.

The repository currently contains eleven independent GNOME applications written
in C with GTK 4 and libadwaita, licensed under GPL-3.0-or-later:

- [Tic-Tac-Toe](tic-tac-toe/README.md): a 3×3 board, mouse and numeric keypad controls.
- [AI Games: Four in a Row](four-in-a-row/README.md): a 7×6 board with gravity, mouse and 1–7 key controls.
- [AI Games: Pentalpha](pentalpha/README.md): a single-player star puzzle with animated placements and Human, Random, Heuristic, Search, and Ollama controllers.
- [AI Games: Heptalpha](heptalpha/README.md): a twenty-piece puzzle on 21 points, with curved animations and full-depth Search.
- [AI Games: Peg Solitaire](peg-solitaire/README.md): English and triangular boards, animated jumps, Backtracking, A*, and Ollama.
- [AI Games: Pentalfa Cruzado Circulado](pentalpha-crossed-circle/README.md): 36-point placement puzzle with a closed circle, curved animation, graph/forest/GF(2) controllers, and a separate symmetry-search variation. Its experimental target is 35; the proven optimum is 34.
- [AI Games: Tavli](tavli/README.md): Portes, Plakoto, Fevga, Backgammon, and custom Running, with match play, Backgammon cube, Expectiminimax, stochastic MCTS, and pure Ollama.
- [AI Games: Sokoban](sokoban/README.md): eight original box-pushing levels, XSB import, minimum-push A*, Greedy Best-first, deadlock pruning, Random, and pure Ollama.
- [AI Games: Go](go/README.md): 9×9, Chinese-style area scoring, positional superko, captures, configurable komi, Heuristic, MCTS, and pure Ollama.
- [AI Games: Hex](hex/README.md): connection game on 3×3, 5×5, or 7×7 boards, optional opening swap, connection heuristic, Minimax, MCTS, and pure Ollama.
- [AI Games: Reversi](reversi/README.md): standard 8×8 disc-flipping game with Human, Random, Positional, Minimax, MCTS, and pure Ollama players.

Each directory contains its own engine, interface, tests, Makefile, Meson
project, installation resources, and license copy. Within this checkout, each application can be
built and used independently. Shared development drivers live in `tools/`.

## Contributing and comparing algorithms

Start with [CONTRIBUTING.md](CONTRIBUTING.md). The
[headless tournament runner](docs/experiments.md) exports repeatable CSV results
with seeds, controller settings, and move sequences. CI builds all apps,
runs engine, translation, Ollama, and GUI tests, and captures the actual
interfaces. See [screenshot instructions](docs/screenshots.md) for generating
and reviewing previews.

Before sharing diagnostics, read the [log privacy and rotation guide](docs/logging.md).
The [publication review](docs/publication.md) describes history checks and
remaining release validation, including migration from the former application
names and IDs.

## Dependencies and build

On Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config glib2-devel gtk4-devel libadwaita-devel libsoup3-devel json-glib-devel
make
```

Requires C11, GLib >= 2.66, GTK >= 4.10, and libadwaita >= 1.4.
The build uses the libraries and development files installed on your system.

## Commands from the repository root

```sh
make                              # build all applications
make run                          # launch Tic-Tac-Toe
make GAME=four-in-a-row run            # launch Four in a Row
make GAME=pentalpha run                # launch Pentalpha
make GAME=heptalpha run                # launch Heptalpha
make GAME=peg-solitaire run            # launch Peg Solitaire
make GAME=pentalpha-crossed-circle run # launch Pentalfa Cruzado Circulado
make GAME=reversi run                 # launch Reversi
make GAME=hex run                     # launch Hex
make GAME=tavli run                   # launch Tavli
make GAME=sokoban run                 # launch Sokoban
make GAME=go run                      # launch Go (go-game)
make GAME=four-in-a-row                # build only Four in a Row
make test                         # test all engines without a display
make GAME=four-in-a-row test           # test one engine
make test-ollama                   # test all Ollama clients and logs
make test-ui                      # include GUI tests; requires a display
make install                      # install all applications under ~/.local
make install PREFIX=/other/path   # choose another installation prefix
make clean                        # remove build artifacts, preserving histories
```

The `build`, `configure`, `test`, `test-ui`, `install`, and `clean` targets
operate on all projects, or only the one selected by `GAME`. `run` defaults
to Tic-Tac-Toe. `PREFIX`, `MESON_ARGS`, and `DESTDIR` are forwarded to each
application's Makefile. Relative build paths are relative to each application's
directory.

You can also use `make -C four-in-a-row`, or enter either game directory and run
`make`, `make run`, `make test`, `make test-ui`, `make install`, or `make clean`.
For an optimized build, use `make MESON_ARGS='-Dbuildtype=release'`.

## Games and histories

In the two-player games, each side has a **Human / Positional / Minimax / Ollama** selector, allowing
all combinations. Each Minimax player has its own search depth: 1–9 (default 9)
in Tic-Tac-Toe and 1–12 (default 6) in Four in a Row. The default is a human
first player and a positional second player. Searches run in the background
and complete the selected depth; **New configuration** cancels analysis.

In the two-player games, the 0–3 second delay, adjustable in 0.1-second steps, is the minimum turn time:
analysis and the timer start together. Games restart three seconds after a
result, preserving controllers, depths, and delay. Positional and Minimax
players treat all opening moves as equivalent and choose randomly.

For Pentalpha and Heptalpha, the delay controls a half-blink, half-slide animation after each
decision. Pause/Resume, Human Undo, and persistent completed boards support
review. Completed and stuck attempts are recorded in each puzzle’s history file.
Their single-player Search discovers solutions at runtime, with full depth as
the default; optional Ollama assistance is off. See [Pentalpha](pentalpha/README.md)
and [Heptalpha](heptalpha/README.md).

In the two-player games, before every human or automated move, evaluation
components are recorded in memory. Games with a winner are appended to the history with a timestamp;
draws and abandoned games are not saved.

Tic-Tac-Toe uses `tic-tac-toe-history.md`; Four in a Row uses `four-in-a-row-history.md`.
Paths are relative to the working directory. `make run` and
`make GAME=four-in-a-row run` launch inside the respective application directory.
Direct launches and GNOME launchers use the working directory supplied by
the terminal or desktop.

Old build directories at the repository root are not reused after the project
split. Existing histories remain in their original locations. New builds live
inside each project; local builds and histories are ignored by Git.

## Ollama players

Both games support **Ollama** players, with an independent model selection for
each side and a refreshable model list from the local service
(`127.0.0.1:11434`). Application builds require libsoup 3 and JSON-GLib in
addition to GTK and libadwaita. `make test-ollama` tests the clients;
`make test` keeps engine tests independent of graphical and networking
libraries. See each game's README for configuration, error handling, and
history details.

All Ollama communication is appended to `<game>-ollama.log` in the working directory,
including prompts, complete responses, and `thinking` when returned by the
model. The format is JSON Lines; `OLLAMA_LOG_FILE` overrides the path.

The earlier games offer optional **Tactical assistance** (off by default)
and **Extended reasoning** (on by default). Histories distinguish pure and
assisted modes. Prompts identify pieces, coordinates, and landing cells;
Cogito v1 can analyze before producing its JSON decision. The game READMEs
explain these controls and their limits. Reversi, Hex, and Tavli use pure Ollama decisions
with optional Extended thinking and no tactical assistance.

Communication failures, validation errors, and timeouts are also appended to
`<game>-ollama-error.log`; `OLLAMA_ERROR_LOG_FILE` overrides its path. Both logs share
operation and request identifiers for diagnosis. In Tic-Tac-Toe, tactical
assistance searches to the end of the game and excludes forced losses when an
alternative exists, while the model chooses among the permitted moves. The
history explicitly identifies this mode; pure Ollama play remains available.

## Interface languages

All apps offer a header selector for **English**, **Português**, **Español**
and **Italiano**, plus **System default**. Automatic selection follows the system's
preferred supported language, with **English** as the fallback. Changes apply
immediately and are saved independently for each app under
`~/.config/ai-games/` (or `$XDG_CONFIG_HOME/ai-games/`). Translations are embedded
in the binaries; the normal build and `make test` include internationalization.

Default output files are separate even when multiple games run in the same directory:

| Game | History | Ollama communication | Ollama errors |
| --- | --- | --- | --- |
| Tic-Tac-Toe | `tic-tac-toe-history.md` | `tic-tac-toe-ollama.log` | `tic-tac-toe-ollama-error.log` |
| Four in a Row | `four-in-a-row-history.md` | `four-in-a-row-ollama.log` | `four-in-a-row-ollama-error.log` |
| Pentalpha | `pentalpha-history.md` | `pentalpha-ollama.log` | `pentalpha-ollama-error.log` |
| Heptalpha | `heptalpha-history.md` | `heptalpha-ollama.log` | `heptalpha-ollama-error.log` |
| Peg Solitaire | `peg-solitaire-history.md` | `peg-solitaire-ollama.log` | `peg-solitaire-ollama-error.log` |
| Pentalfa Cruzado Circulado | `pentalpha-crossed-circle-history.md` | `pentalpha-crossed-circle-ollama.log` | `pentalpha-crossed-circle-ollama-error.log` |
| Reversi | `reversi-history.md` | `reversi-ollama.log` | `reversi-ollama-error.log` |
| Hex | `hex-history.md` | `hex-ollama.log` | `hex-ollama-error.log` |
| Tavli | `tavli-history.md` | `tavli-ollama.log` | `tavli-ollama-error.log` |
| Sokoban | `sokoban-history.md` | `sokoban-ollama.log` | `sokoban-ollama-error.log` |
| Go | `go-history.md` | `go-ollama.log` | `go-ollama-error.log` |

Each JSON log entry also includes its game ID, and each saved game history
heading includes its game name. Existing files keep their original names and
contents; new writes use the defaults above. Explicit log-path environment
variables still override the default filenames.

Peg Solitaire includes independent rules and controls in its [README](peg-solitaire/README.md), a bounded runtime solver, and `peg-solitaire-history.md` decision histories. Its Ollama logs use the `peg-solitaire` filename prefix.
