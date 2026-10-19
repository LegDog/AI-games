# Tic-Tac-Toe

A GNOME application written in C with GTK 4 and libadwaita, supporting human
and automated players. Part of [AI-games](../README.md), licensed under
GPL-3.0-or-later.

## Build and run

Dependencies: a C11 compiler, Meson, Ninja, pkg-config, GLib >= 2.66,
GTK >= 4.10, libadwaita >= 1.4, libsoup 3, and JSON-GLib, including development
files.

On Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config glib2-devel gtk4-devel libadwaita-devel libsoup3-devel json-glib-devel
make
make run
```

To install the executable, icon, and GNOME launcher for your user:

```sh
make install
```

The Makefile uses Meson and Ninja. `make help` lists targets; `make test` tests
the engine, `make test-ui` includes GUI tests, and `make clean` removes build
artifacts without deleting history. Choose a prefix with
`make install PREFIX=/path`, or pass Meson options with
`make MESON_ARGS='-Dbuildtype=release'`.

The installed executable is `~/.local/bin/tic-tac-toe`. Each instance has its
own game. Player settings are not persisted after closing; the interface
language is saved separately.

## Rules

Two players, **X** and **O**, play on an initially empty 3×3 board. X starts,
and players take turns placing one mark in an empty cell. Placed marks cannot
be moved or removed, and occupied cells cannot be played again.

The first player to complete a horizontal, vertical, or diagonal line of
three matching marks wins. If all nine cells are filled without a winning
line, the game ends in a draw. Play stops as soon as either result is reached.

## Playing

Configure **X** and **O** independently as **Human**, **Positional**, **Minimax**,
or **Ollama**. All 16 combinations are allowed, including different algorithms.
The defaults are human X and positional O; X always starts. Use the mouse or
keys 1–9, including the numeric keypad with or without Num Lock:

```text
7 8 9
4 5 6
1 2 3
```

Only empty cells accept input during a human turn. The board fills the largest
available square, centered without distortion. The slider sets the **minimum
automated turn delay** from 0.0 to 3.0 seconds in 0.1-second steps, initially
0.5 seconds. Computation and delay run concurrently; the move is applied when
both finish. Changing the delay restarts only the timer, preserving the
calculation. The interface remains responsive even at zero delay.

A green line marks the three winning pieces immediately. The result remains
visible for three seconds, then the next game starts with X and the same
players and delay. **New configuration** abandons the current game and lets
you change players.

## Positional strategy

The 3×3 matrix contains 0 for empty, 1 for X, and -1 for O. Before each move,
the evaluator considers rows, columns, and diagonals through each empty cell.
A line containing both players' pieces contributes nothing. Each eligible
line contributes:

| Situation before the move | Points |
| --- | ---: |
| Two own pieces | 1000 |
| Two opponent pieces | 100 |
| One own piece, no opponent pieces | 10 |
| One opponent piece, no own pieces | 1 |
| Empty line | 0 |

From the second move onward, each line through the evaluated cell with no
opponent pieces adds **0.2 points**, whether empty or containing own pieces.
This measures development opportunities. Against a center opening, corners
retain two open lines, while edge cells retain only one. The history records
this component under “Linhas abertas ×0,2” (open lines ×0.2). Its maximum of
0.8 points breaks ties between earlier criteria before randomness. On an empty
board, this component is zero: all nine cells have equal strategic value,
and randomness alone chooses the opening. All automated players first choose
center (5), sides (2, 4, 6, 8), or corners (1, 3, 7, 9) with probability
1/3 each, then choose uniformly within that set. Thus the center has probability
1/3 and each individual side or corner has probability 1/12.

A random component in `[0, 0.1)` is added once per cell. After the opening move,
an empty center also receives a **5-point bonus**. This favors the center on
the first reply without overriding wins, blocks, or a line with an own piece.
The bonus is zero on the opening move. The history records it under
“Bônus centro” (center bonus).

Each candidate cell is also simulated to detect forks: two or more distinct
cells where the player could win next turn. Creating a fork adds **50 points**.
If an opponent reply creates a fork without leaving an immediate win available
to us, the candidate receives **−50 points**, applied once. Fork prevention
therefore accounts for threats that force the opponent to defend. Allowing an
immediate opponent win costs **500 points**; an already-winning move receives
no penalties. The history records these components and the number of replies
that would create forks. Analysis is limited to the candidate move and opponent
replies.

The automated player chooses the highest total. Exact ties favor the first
cell in reading order, from top to bottom. Weights ensure immediate wins take
precedence over blocks, and blocks over lower criteria. This player uses the
formula; it does not run Minimax or guarantee perfect play.

## Minimax

Each Minimax player has an independent depth slider: **1–9, default 9**, in
integer steps. Depth includes the candidate move and is capped at the remaining
cells. At depth 9, it searches to the end and plays optimally; randomized
opening selection gives each position set equal probability. At smaller depths,
cutoff evaluation adds 1 for one piece and 10 for two pieces in an unblocked
line, subtracting the opponent's corresponding values. Shallower searches can
make mistakes.

The search uses alpha-beta pruning, iterative deepening, and a transposition
table of up to 16 MiB per search. At the root, every legal alternative receives
a complete search value so history values can be compared. Terminal results
take precedence over positional evaluation; faster wins and later losses are
preferred. Random selection breaks ties among the best alternatives within
1e-9 tolerance. On an empty board, no search is performed: all alternatives
have value zero and the opening uses the position-set probabilities above.

The **Positional** player retains its own formula and tactical factors.
Automated calculations use board copies and independent random generators
in background tasks. Each application runs at most one analysis at a time.
During Minimax search, the progress indicator shows completed/target depth.

Minimax has no time limit: it completes the selected depth. High depths may
take time; **New configuration** cancels the calculation so you can choose
another depth. Changing a player's type hides or shows its depth slider while
preserving the selected value. Automatic restarts preserve types and depths.
Closing the window cancels tasks, and stale results cannot make moves in a
new game.

## History

Before every human or automated move, the in-memory history records the
previous board, components and totals for every empty cell, and the chosen
cell. The Positional player uses these exact values. Minimax adds a section
with each legal cell's search value, requested/completed depth, visited nodes,
duration, and selected cell. Move titles identify the controller; game
configuration includes each Minimax player's depth.

When a player wins, the complete game is appended to **`tic-tac-toe-history.md` in the
working directory**, with date, local time, time zone, configuration, winner,
and final board. The file is created if needed. Draws and abandoned games are
not saved. In-memory history resets for each new game; accumulated file
contents are preserved.

When launched from GNOME, the desktop environment determines the working
directory. To choose where histories are saved, launch from a terminal:

```sh
cd /path/to/histories
~/.local/bin/tic-tac-toe
```

Write failures appear with a **Retry saving** button. Pending games remain in
memory during the session, even after another game starts; new wins also retry
the queue. Closing the application discards records that could not be saved.

## Tests

```sh
meson test -C build --print-errorlogs
```

To test only the engine, without graphical dependencies:

```sh
meson setup build-tests -Dapp=false
meson test -C build-tests --print-errorlogs
```

Tests cover rules, priorities across all reachable positions, scoring
components, and appended history. To include control, timer, restart, and
write-error tests in a graphical session or on a test display:

```sh
meson configure build -Dui_tests=true
meson test -C build --print-errorlogs
```

GUI tests open a temporary window and use temporary files. For visual checks,
resize the window along both axes, check light and dark themes, and try each
player type.

## Repository location

This application lives in `tic-tac-toe/`. Run the commands above there, or
use `make run` from the repository root. The root Makefile also accepts
`make GAME=tic-tac-toe test` and `make GAME=tic-tac-toe install`.
Builds at the root from before the reorganization are not reused. Old histories
remain in their original locations; Make commands now launch the application
inside this directory.

## Ollama player

Each player can select **Ollama**, alongside Human, Positional, or Minimax.
The **Model** selector appears only for Ollama and is independent for each
player. The list loads in the background, is sorted by name, and can be
reloaded with **Refresh models**. It uses `GET /api/tags`, the API for the
models available on the service queried by `ollama list`. Initially the first
model is selected; refreshes preserve each selection while it remains
available. If the service or models are unavailable, starting with Ollama is
disabled; other player types remain available.

The service must be running at **http://127.0.0.1:11434**. Install a model
through Ollama before playing; the application does not download models or
start the server. If needed, run `ollama serve` and, for example,
`ollama pull llama3.2:3b`. Selected models carry over between games during the
session. Models that cannot generate text or produce the requested structured
response cause a turn error, without substitution by another algorithm.

Each turn sends the rules, board symbols and coordinates, player identity,
and legal moves through `POST /api/generate`, requesting a structured JSON
response. On an empty board, the application chooses the random opening
using the position-set probabilities above without calling the model. Positional evaluations
remain in the history but are not sent as suggestions to the model. The
engine validates the response again: occupied cells and full columns are
not accepted.

The interface remains responsive during requests. The delay remains a minimum
turn time and overlaps generation. Model listing has a total deadline of
1,000 seconds; each turn has 18,000 seconds (5 hours), including model loading.
Connection failures, timeouts, and invalid responses **pause the game** while
preserving the board. Use **Retry** to request a move for the same position,
or **New configuration** to abandon the game. There is no fallback automated
move. Closing or reconfiguring cancels the request and discards late results.

The history identifies the model, move, duration, and failed attempts. Only
games with a winner are saved, followed by a restart after three seconds.
Draws and abandoned games are not saved.

Application builds also require **libsoup 3** and **JSON-GLib**:

```sh
# Fedora
sudo dnf install libsoup3-devel json-glib-devel
```

`make test` tests the engines using only GLib. `make test-ollama` builds the
application and tests the client without a display or a real Ollama instance:
it includes protocol validation without networking and HTTP tests using a
mock server. HTTP tests are skipped when the environment blocks local sockets.
`make test-ui` covers all 16 player combinations, independent model selections,
list refresh, failures, retries, and cancellation. GUI tests require a
graphical session.

### Full communication log

All model queries and move requests are appended to **`tic-tac-toe-ollama.log` in the
working directory**, regardless of wins, draws, errors, or abandoned games.
The file is preserved across games and application runs. Set `OLLAMA_LOG_FILE`
to choose another path.

Each line is a JSON object containing a timestamp and time zone, game, process,
request identifier, event, method, URL, available headers, elapsed time, and
complete outgoing or incoming body. Requests, responses, errors, cancellations,
and timeouts share the request identifier. Bodies that are not valid UTF-8
are preserved as Base64. Incoming bodies are logged before validation,
including HTTP error responses and invalid JSON.

The `thinking` field and any `<think>` text are preserved when returned by
Ollama. The log records what the server actually returns. **Extended reasoning**
controls the reasoning request described below. Since the integration uses
`stream: false`, the complete response is logged when the request finishes;
interruptions may leave no partial body. Log write failures are reported in
the terminal and do not interrupt the game. Git ignores the log file.

To read `thinking` from JSON responses using `jq`:

```sh
jq -r 'select(.event == "response") | .body | fromjson | .thinking // empty' tic-tac-toe-ollama.log
```

### Ollama playing strength

Prompts identify the player's pieces as **Y**, opposing pieces as **A**, and
empty cells as **.**. They number rows and columns and give coordinates for
every legal move. Four in a Row includes each disc's landing row under gravity;
Tic-Tac-Toe retains numeric keypad numbering. Models are instructed to check
wins and opponent replies before considering forks and longer-term plans.

Each Ollama player has two independent options, preserved between games:

- **Tactical assistance**: off by default, preserving **pure** mode. When
  enabled, uses **complete Tic-Tac-Toe defense**: an exhaustive Boolean search,
  equivalent to Minimax's loss-avoidance criterion, searches to the end of the
  game. The engine prioritizes immediate wins and excludes forced losses when
  a drawing or winning alternative exists. The model still chooses among the
  remaining moves; neither Positional scores nor the Minimax player's configured
  depth are used. If the position is already a forced loss, the immediate-loss
  filter is retained where possible. The history identifies this as
  “assistido (defesa completa)” (assisted, complete defense).
- **Extended reasoning**: on by default. For models advertising controls in
  `/api/show`, requests `true`, the `high` level when available, or an advertised
  supported default. Without this metadata, preserves the model's default
  behavior to avoid incompatible parameters. Turning it off also preserves
  default behavior, which may include reasoning.

For models named `cogito` or `cogito:tag` (including namespaced models), extended
reasoning uses the Cogito v1 protocol. It first requests analysis with
`Enable deep thinking subroutine.` in the system prompt, without restricting
generation to a JSON schema. It then sends the analysis to the same model in
a second request to select the move using the JSON schema. Name recognition
does not cover arbitrarily renamed models or Cogito 2.1. Requests share the
turn's 18,000-second (5-hour) deadline and cancellation. Failure at either
stage pauses the game, without automatic substitution.

The engine also validates the assistance filter: a legal response outside the
permitted set is rejected and can be retried. The history records pure or
assisted mode, allowed alternatives, reasoning request, strategy used, and
total duration. `tic-tac-toe-ollama.log` includes metadata requests, analysis, the final
decision, and complete responses.

Four in a Row tests reproduce the immediate threats in Cogito's losses at
18:43:39 and 18:43:54 on 2026-10-16: assistance requires blocks in columns 4
and 2, respectively. At 18:43:47, two simultaneous threats already existed
before the last decision, and the filter detects the absence of an immediate
defense. Four in a Row assistance does not guarantee avoiding forks prepared
earlier or beating Positional; Tic-Tac-Toe uses the complete defense described
above. These cases also cover mirroring and color swaps.

Protocol references: [Ollama thinking](https://docs.ollama.com/capabilities/thinking)
and [Cogito extended reasoning](https://ollama.com/library/cogito:3b).

### Ollama errors

**`tic-tac-toe-ollama-error.log`** is created in the working directory when the first error
occurs. It accumulates only failures and cancellations across games: connection
and HTTP errors, invalid JSON or model lists, incomplete analysis, illegal
moves or moves outside the assistance filter, HTTP timeouts, and the total
turn deadline. Set `OLLAMA_ERROR_LOG_FILE` to choose another path.

The format is JSON Lines, with timestamp, game, process, operation, model when
applicable, assistance mode, reasoning request, duration, and error message.
`operation_id` connects all stages of a turn; `request_id` identifies the last
HTTP request, when present. These fields also appear in `tic-tac-toe-ollama.log`. HTTP
and board/alternative context are included when available, allowing diagnosis
even after a successful HTTP response. Transport errors are not duplicated
by the operation summary. The full log continues to preserve sent and received
bodies.

Both files append with interprocess locking. Full-log write failures are
reported in the error log; if that also fails, the terminal reports the error.
Logging problems do not change moves or trigger automatic retries. Git ignores
both logs.

### Assisted-loss regressions

The games at 19:07:11 and 19:07:25 on 2026-10-16 were lost by
`ahmadwaqar/smolvlm2-256m-video:fp16` after it answered a corner opening with
an edge cell. The previous filter only avoided losing on the next move;
these choices already allowed a forced sequence and a fork. Complete defense
now permits only the center for that first reply. It also requires an edge
against the opposite-corners-and-center configuration, preventing the classic
fork setup.

A test explores all legal opponent replies and all model-permitted choices
from an empty board on both sides: 1,938 states as O and 3,722 as X, with no
losses. This guarantee belongs to the **assistance's permitted move set**,
rather than the pure model's strategic ability. Responses outside that set are
rejected and logged; timeouts still pause the game. The filter cannot recover
an already-lost position.

## Interface language


The header offers **System default**, **English**, **Português**, **Español**
and **Italiano**. On first launch the app uses the system's preferred supported
language; if none matches, it uses **English**. Regional variants such as
`pt_BR`, `pt_PT`, `es_MX` and `it_IT` are recognized. Select an explicit language
to override detection, or **System default** to return to automatic selection.

Changes apply immediately to controls, tooltips, turn/result messages and
application-generated errors, preserving the current game and player settings.
The choice is saved separately for each application in
`$XDG_CONFIG_HOME/ai-games/tic-tac-toe.ini`, normally
`~/.config/ai-games/tic-tac-toe.ini`. Missing or unsupported preferences use
system detection. If the new file is absent, the app also reads the former
`jogo-da-velha.ini` in the same directory; later language changes save to the new
filename. A malformed file also falls back to detection and reports
its read error in the terminal.

Catalogs are embedded in the binaries, so direct builds and installed apps use
the same translations without additional locale packages. To maintain them,
edit `src/i18n-catalog.inc` (English, Portuguese, Spanish, Italian). Keep all
printf placeholders in the same order. `make test` covers the full catalog,
format compatibility, locale selection, preference persistence and concurrent
translation reads. `make test-ui` additionally covers live switching during a
game and computation; it requires a graphical session.

Ollama prompts and the established Markdown history syntax retain their existing
language and format. Recorded log entries and server/OS error details are
preserved as received; application-generated errors use the active language.

## Contributing and experiments

See the repository [contribution guide](../CONTRIBUTING.md),
[reproducible tournament runner](../docs/experiments.md),
[log privacy and rotation](../docs/logging.md), and
[screenshot capture instructions](../docs/screenshots.md).
