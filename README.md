# AI-games

This repository is a place to study, compare, and develop AI skills through
playing games. We may add more games and algorithms soon. Contributions are
welcome—feel free to contribute new games, algorithms, experiments, or improvements.
English is the primary language for project documentation and contributions.

The repository currently contains two independent GNOME applications written
in C with GTK 4 and libadwaita, licensed under GPL-3.0-or-later:

- [Tic-Tac-Toe](jogo-da-velha/README.md): a 3×3 board, mouse and numeric keypad controls.
- [Connect Four](connect-4/README.md): a 7×6 board with gravity, mouse and 1–7 key controls.

Each directory contains its own engine, interface, tests, Makefile, Meson
project, installation resources, and license copy. Either application can be
built and used independently.

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
make                              # build both applications
make run                          # launch Tic-Tac-Toe
make GAME=connect-4 run            # launch Connect Four
make GAME=connect-4                # build only Connect Four
make test                         # test both engines without a display
make GAME=connect-4 test           # test one engine
make test-ollama                   # test both Ollama clients and logs
make test-ui                      # include GUI tests; requires a display
make install                      # install both applications under ~/.local
make install PREFIX=/other/path   # choose another installation prefix
make clean                        # remove build artifacts, preserving histories
```

The `build`, `configure`, `test`, `test-ui`, `install`, and `clean` targets
operate on both projects, or only the one selected by `GAME`. `run` defaults
to Tic-Tac-Toe. `PREFIX`, `MESON_ARGS`, and `DESTDIR` are forwarded to each
application's Makefile. Relative build paths are relative to each application's
directory.

You can also use `make -C connect-4`, or enter either game directory and run
`make`, `make run`, `make test`, `make test-ui`, `make install`, or `make clean`.
For an optimized build, use `make MESON_ARGS='-Dbuildtype=release'`.

## Games and histories

Each side has a **Human / Positional / Minimax / Ollama** selector, allowing
all combinations. Each Minimax player has its own search depth: 1–9 (default 9)
in Tic-Tac-Toe and 1–12 (default 6) in Connect Four. The default is a human
first player and a positional second player. Searches run in the background
and complete the selected depth; **New configuration** cancels analysis.

The 0–3 second delay, adjustable in 0.1-second steps, is the minimum turn time:
analysis and the timer start together. Games restart three seconds after a
result, preserving controllers, depths, and delay. Positional and Minimax
players treat all opening moves as equivalent and choose randomly.

Before every human or automated move, evaluation components are recorded in
memory. Games with a winner are appended to the history with a timestamp;
draws and abandoned games are not saved.

Tic-Tac-Toe uses `histórico.md`; Connect Four uses `histórico-connect-4.md`.
Paths are relative to the working directory. `make run` and
`make GAME=connect-4 run` launch inside the respective application directory.
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

All Ollama communication is appended to `ollama.log` in the working directory,
including prompts, complete responses, and `thinking` when returned by the
model. The format is JSON Lines; `OLLAMA_LOG_FILE` overrides the path.

Each Ollama player offers optional **Tactical assistance** (off by default)
and **Extended reasoning** (on by default). Histories distinguish pure and
assisted modes. Prompts identify pieces, coordinates, and landing cells;
Cogito v1 can analyze before producing its JSON decision. The game READMEs
explain these controls and their limits.

Communication failures, validation errors, and timeouts are also appended to
`ollama-error.log`; `OLLAMA_ERROR_LOG_FILE` overrides its path. Both logs share
operation and request identifiers for diagnosis. In Tic-Tac-Toe, tactical
assistance searches to the end of the game and excludes forced losses when an
alternative exists, while the model chooses among the permitted moves. The
history explicitly identifies this mode; pure Ollama play remains available.

## Interface languages

Both apps offer a header selector for **English**, **Português**, **Español**
and **Italiano**, plus **System default**. Automatic selection follows the system's
preferred supported language, with **English** as the fallback. Changes apply
immediately and are saved independently for each app under
`~/.config/ai-games/` (or `$XDG_CONFIG_HOME/ai-games/`). Translations are embedded
in the binaries; the normal build and `make test` include internationalization.
