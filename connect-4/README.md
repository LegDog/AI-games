# Connect Four

An independent GNOME application written in C with GTK 4 and libadwaita,
licensed under GPL-3.0-or-later. Part of [AI-games](../README.md).

## Build and run

Requires a C11 compiler, Meson, Ninja, pkg-config, GLib >= 2.66, GTK >= 4.10,
libadwaita >= 1.4, libsoup 3, and JSON-GLib, with development files.

```sh
make
make run
make test
make install
```

Run these commands in this directory. From the repository root, use
`make GAME=connect-4 run`. `make install` installs the executable, icon, and
launcher under `~/.local`; `PREFIX=/path` and `DESTDIR=/staging` select another
prefix or stage an installation. The GNOME application ID is
`io.github.aigames.Connect4`, the executable is `connect-4`, and the English
display name is **Connect Four**.

## Playing

The board has seven columns and six rows. Click a column or use keys **1–7**,
including the numeric keypad with or without Num Lock. Each piece falls to
the lowest empty cell. Full columns reject moves without changing the turn.

Configure **Red** and **Yellow** independently as **Human**, **Positional**,
**Minimax**, or **Ollama**. All 16 combinations are allowed. The defaults are
human Red and positional Yellow; Red always starts. Red discs contain a cross
and yellow discs a ring so they can be distinguished beyond color. Four or
more matching pieces in a horizontal, vertical, or diagonal line win. All
winning alignments are highlighted immediately. A full board without a winner
is a draw.

The board preserves its 7:6 aspect ratio and square cells, centered in the
largest available area below the column numbers. The slider sets the
**minimum automated turn delay** from 0.0 to 3.0 seconds in 0.1-second steps,
initially 0.5 seconds. Analysis starts with the timer; the move is applied when
both finish. Changing the delay restarts only the timer, without recalculating.
Zero delay keeps the interface responsive between moves.

Results remain visible for three seconds before another game starts with the
same players and delay. **New configuration** cancels the game and its timers.
Player settings are not persisted after closing; the interface language is
saved separately.

## Strategic evaluation

The engine uses an `int[6][7]` matrix: 0 for empty, 1 for Red, and -1 for Yellow.
It evaluates only legal column moves, simulating gravity. On an empty board,
all strategic factors are zero and randomness alone chooses among the seven
columns.

After the opening, each move is simulated and **all 69 four-cell windows on
the resulting board** are evaluated. Windows containing both players' pieces
contribute nothing. For each side, windows with one, two, three, or four pieces
have weights **10, 100, 1000, and 10000**. Completely empty windows are counted
in the history but favor neither side.

Each window's contribution is divided by **1 + missing supports**. The count
includes empty cells below its empty cells, excluding cells belonging to the
window itself and counting each support only once. Thus a suspended horizontal
or diagonal line is worth less than an accessible one; cells in a vertical
window can support each other as the sequence develops.

The global position value, from the moving player's perspective, is:

```text
positional = 0.1 × (own potential − 1.25 × opponent potential)
```

Defense receives slightly greater weight. The scale keeps the global component
below tactical factors, even when the best move only improves an unfavorable
position. The history records both potentials and counts of windows with and
without missing external supports so the calculation can be checked.

These tactical components are added once per column:

| Situation | Points |
| --- | ---: |
| Immediate win | +1,000,000,000 |
| Block a win available before the move | +10,000,000 |
| Create an own fork or stacked threat | +100,000 |
| An opponent reply creates a relevant fork or stacked threat | −100,000 |
| Allow an immediate opponent win | −100,000,000 |
| Prove an own forced winning sequence | +1,000,000 |
| Prove an opponent forced winning sequence | −20,000,000 |

Already-winning moves receive no penalties. A fork requires at least two
**distinct columns where a piece can actually land and win**. A stacked threat
occurs when blocking one win supports another winning cell in the **same
column**. The engine simulates the block and checks whether the defender can
win immediately instead. The bonus applies once even when a move creates both
a fork and a stacked threat.

Opponent-reply analysis also includes stacked threats. It ignores dangerous
replies that leave our immediate win available. Unsupported cells are not
considered immediate wins; simulation detects when a move supplies that support.

The engine also searches for **continuous chains of threats and forced blocks**.
After the candidate move, it explores attacks that create an immediate win
for the next turn. The defender must block or win first; after the block,
analysis continues with the next attack. Two threats in distinct columns
complete the proof if the defender cannot win immediately. This detects fork
preparation across several forced blocks.

Analysis is limited to **12 plies** and **1,024 nodes per attempt**, with separate
budgets for opponent proofs and, when needed, own proofs. Already-proven wins,
forks, and stacked threats reuse short proofs. These bonuses add to earlier
components; immediate wins retain priority. Openings still use only randomness.

When continuous search finds no proof, an extension permits up to **two
preparatory attacker moves**, with **4,096 nodes per attempt** and the same
12-ply limit. Preparatory moves must create a cell that would complete four
pieces or provide support below such a cell. Without an immediate threat,
**every legal defender reply** must lead to an attacker win before the loss
is penalized. This anticipates prepared forks and stacked threats without
assuming the opponent will cooperate by supplying support.

The history records wins/losses requiring preparation, forced-win/loss flags,
and node-budget exhaustion. Preparations use the same bonuses and penalties
as other proofs, without duplicate scoring. Failure to find a chain does not
prove safety: other preparations and sequences beyond the limits remain outside
this analysis.

A random component in `[0, 0.1)` is added per column. The highest total wins;
exact ties favor the leftmost column. Weights are named constants in the
engine. Positional evaluation is supplemented by the tactical analysis and
forced chains above; it does not perform general Minimax search or guarantee
perfect play.

## Minimax

Each Minimax player has an independent depth slider: **1–12, default 6**, in
integer steps. Each level is one individual move, including the candidate;
effective depth is capped at remaining cells. Leaf evaluation uses the global
board value and accessibility discounts described above, from the root player's
perspective. It does not run the Positional player's forced-chain analysis.

The search uses alpha-beta pruning, iterative deepening, and a transposition
table of up to 16 MiB per search. At the root, every legal alternative receives
a complete search value so history values can be compared. Terminal results
take precedence over positional evaluation; faster wins and later losses are
preferred. Random selection breaks ties among the best alternatives within
1e-9 tolerance. On an empty board, no search is performed: all alternatives
have value zero and the opening move is uniformly random.

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

Before each move, including human moves, the history records the previous
board, column and landing row, positional components, tactical factors,
randomness, and total for every legal column. Positional uses these exact
values. Minimax adds a table with the search values used for selection,
requested/completed depth, visited nodes, duration, and chosen column. Move
titles and game configuration identify Human, Positional, Minimax, or Ollama.
Rows are numbered 1–6 from top to bottom. Text boards use V for Red
(Portuguese “vermelho”) and A for Yellow (“amarelo”).

Wins are appended to **`histórico-connect-4.md` in the working directory**,
with date, local time, time zone, configuration, winner, and final board.
Draws and abandoned games are discarded. In-memory history resets for each
new game; the accumulated file is preserved.

Write errors appear with a **Retry saving** button. Pending records remain in
the session while other games continue. New wins also retry saving; closing
the application discards unsaved records. Writes use interprocess locking and
roll back partial appends before allowing another attempt.

## Tests

`make test` tests the engine and requires only GLib and build tools. Tests cover
gravity, full columns, wins in four directions, multiple and extended winning
alignments, draws, opening behavior, priorities, forks, support traps, stacked
threats, accessibility and global evaluation, mirrored positions and color
swaps, 300 generated games, history, and partial-write recovery.

The game at 17:32:12 on 2026-10-16 is a regression: evaluation now prefers
column 4 on move 13 and detects the stacked threat at the end of the original
sequence. Another regression covers the 22-move loss from opening 7/4 with
seed 0: column 2 on move 11 is now identified as losing through a forced chain,
and an alternative without that proof of defeat is chosen. Both cover
horizontal mirroring and color swaps.

Minimax wins at 18:18:02 (25 moves) and 18:17:50 (29 moves) on 2026-10-16 are
also regressions. The preparation extension identifies column 5 on move 18
and column 2 on move 22, respectively, as losing and chooses alternatives
without that proof. Cases cover mirroring and color swaps; depth-8 Minimax
confirms the old choices lose and alternatives avoid those sequences. This
does not guarantee a draw or win in the complete game.

Resuming these two positions before the mistake, with seeds 0–7 and both
players recalculating subsequent moves (Red Minimax at depth 6), the 25-move
game instead ended after 40 or 41 moves: four wins for each side. The 29-move
game ended after 33 moves, with eight Minimax wins. None of these 16
continuations ended below 75% board occupancy (31 moves or fewer). This sample
preserves the first 17 or 21 historical moves; it is not a win rate for games
started from scratch.

In the earlier comparison introducing continuous chains, seeds 0–999 for each
opening 1/4 and 7/4 preserved only the first two moves. Those 2,000 automated
games changed from 1,239 Yellow wins after 22 moves and 761 draws to 1,239
Yellow wins after 40 moves and 761 draws. No game in the new sample had a
winner by move 31. These results depend on the openings and seeds tested;
they do not establish the absence of other quick losses.

`make test-ui` also builds and runs keyboard, mouse, geometry, timer, restart,
player-mode, and write-recovery tests. It requires a graphical session or
test display. Tests open a temporary window and use temporary histories. For
visual checks, try wide and tall windows, light and dark themes, and winning
alignment highlights.

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
response. The model chooses the opening move too. Positional evaluations
remain in the history but are not sent as suggestions to the model. The
engine validates the response again: occupied cells and full columns are
not accepted.

The interface remains responsive during requests. The delay remains a minimum
turn time and overlaps generation. Model listing has a total deadline of
100 seconds; each turn has 1,800 seconds (30 minutes), including model loading.
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

All model queries and move requests are appended to **`ollama.log` in the
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
jq -r 'select(.event == "response") | .body | fromjson | .thinking // empty' ollama.log
```

### Ollama playing strength

Prompts identify the player's pieces as **Y**, opposing pieces as **A**, and
empty cells as **.**. They number rows and columns and give coordinates for
every legal move. Connect Four includes each disc's landing row under gravity;
Tic-Tac-Toe retains numeric keypad numbering. Models are instructed to check
wins and opponent replies before considering forks and longer-term plans.

Each Ollama player has two independent options, preserved between games:

- **Tactical assistance**: off by default, preserving **pure** mode. When
  enabled, the engine restricts choices to immediate wins when available.
  Otherwise it restricts choices to moves avoiding an opponent win on the
  next reply, if any exist. If every move allows an immediate loss, all legal
  moves remain available. These are tactical facts verified by rule simulation;
  Positional and Minimax scores do not guide the choice. The model still
  chooses the move.
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
turn's 1,800-second (30-minute) deadline and cancellation. Failure at either
stage pauses the game, without automatic substitution.

The engine also validates the assistance filter: a legal response outside the
permitted set is rejected and can be retried. The history records pure or
assisted mode, allowed alternatives, reasoning request, strategy used, and
total duration. `ollama.log` includes metadata requests, analysis, the final
decision, and complete responses.

Tests reproduce the immediate threats in Cogito's losses at 18:43:39 and
18:43:54 on 2026-10-16: assistance requires blocks in columns 4 and 2,
respectively. At 18:43:47, two simultaneous threats already existed before the
last decision; the filter detects the absence of an immediate defense. This
does not guarantee avoiding forks prepared earlier or beating Positional.
These cases also cover mirroring and color swaps.

Protocol references: [Ollama thinking](https://docs.ollama.com/capabilities/thinking)
and [Cogito extended reasoning](https://ollama.com/library/cogito:3b).

### Ollama errors

**`ollama-error.log`** is created in the working directory when the first error
occurs. It accumulates only failures and cancellations across games: connection
and HTTP errors, invalid JSON or model lists, incomplete analysis, illegal
moves or moves outside the assistance filter, HTTP timeouts, and the total
turn deadline. Set `OLLAMA_ERROR_LOG_FILE` to choose another path.

The format is JSON Lines, with timestamp, game, process, operation, model when
applicable, assistance mode, reasoning request, duration, and error message.
`operation_id` connects all stages of a turn; `request_id` identifies the last
HTTP request, when present. These fields also appear in `ollama.log`. HTTP
and board/alternative context are included when available, allowing diagnosis
even after a successful HTTP response. Transport errors are not duplicated
by the operation summary. The full log continues to preserve sent and received
bodies.

Both files append with interprocess locking. Full-log write failures are
reported in the error log; if that also fails, the terminal reports the error.
Logging problems do not change moves or trigger automatic retries. Git ignores
both logs.

Connect Four retains its immediate-win/loss filter. Complete defense to the
end of the game is available only in Tic-Tac-Toe, whose state space is small.

## Interface language


The header offers **System default**, **English**, **Português**, **Español**
and **Italiano**. On first launch the app uses the system's preferred supported
language; if none matches, it uses **English**. Regional variants such as
`pt_BR`, `pt_PT`, `es_MX` and `it_IT` are recognized. Select an explicit language
to override detection, or **System default** to return to automatic selection.

Changes apply immediately to controls, tooltips, turn/result messages and
application-generated errors, preserving the current game and player settings.
The choice is saved separately for each application in
`$XDG_CONFIG_HOME/ai-games/connect-4.ini`, normally
`~/.config/ai-games/connect-4.ini`. Missing or unsupported preferences use
system detection. A malformed file also falls back to detection and reports
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
