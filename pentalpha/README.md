# AI Games: Pentalpha

A standalone single-player GTK 4/libadwaita puzzle in C11, licensed under
GPL-3.0-or-later. Play yourself or watch algorithms discover placements.
The application contains no predefined solution sequence.

## Rules and board

The rules follow [Ludosofia's printable Pentalpha board](https://ludosofia.com.br/wp-content/uploads/2020/11/pentalpha_para_imprimir.pdf),
linked from [its Pentalpha article](https://ludosofia.com.br/arqueologia/pentalpha-um-jogo-de-3-mil-anos-que-serve-a-teoria-matematica-dos-grafos/).
Start with ten empty points and introduce nine pieces, one at a time.
A piece enters an empty point, passes over the immediately following point,
and stops at the next empty point, on the same straight line. No turns or
skipped points are allowed. The middle point can be empty or occupied;
pieces already on the board remain unchanged. Only the destination becomes
occupied. Place nine pieces to solve the puzzle. An attempt is stuck if fewer
than nine pieces are placed and no legal placement remains.

Outer vertices `v1`–`v5` run clockwise from the top. Inner intersections
`i1`–`i5` run clockwise from the upper right. The five straight lines are:

```
v1 — i1 — i2 — v3
v3 — i3 — i4 — v5
v5 — i5 — i1 — v2
v2 — i2 — i3 — v4
v4 — i4 — i5 — v1
```

Each line supplies two consecutive triples in either direction: 20 directed
placements before occupancy filtering. Rules use this topology, independently
of display coordinates. The engine stores a ten-bit occupancy mask and up to
nine move triples, making Undo exact.

`data/board.svg` is the board supplied by the project owner from
`~/Pictures/pentalpha.svg`, bundled without altering its artwork. Board overlay
coordinates apply the SVG's existing group transforms. The application icon
is original project artwork. The board is supplied for use with this project;
no illustration from the rules website is redistributed.

## Build and run

Use the same dependencies as the other applications: GLib, GTK 4, libadwaita,
libsoup 3, and json-glib, plus an SVG-capable GdkPixbuf loader (normally supplied
by the desktop's SVG/image loader package). There is no additional SVG development
library requirement.

```sh
make GAME=pentalpha
make GAME=pentalpha run
make GAME=pentalpha test
make GAME=pentalpha test-ollama
make GAME=pentalpha test-ui
```

From this directory the equivalent commands are `make`, `make run`, `make test`,
`make test-ollama`, and `make test-ui`. Engine tests support `-Dapp=false` and
require no graphics or network libraries. Installation provides the `pentalpha`
executable and `io.github.aigames.Pentalpha` desktop entry/icon. The SVG is an
embedded GResource, so installed apps do not depend on this source directory.

## Controllers

- **Human** selects an entry point and then a highlighted destination. Click
  the selected entry again or press Escape to cancel the selection. Undo removes
  the last completed placement, including after a completed/stuck attempt.
- **Random** samples uniformly among currently legal directed placements.
- **Heuristic** scores the resulting piece count × 100 plus the number of
  remaining legal directed placements. It prioritizes completion and otherwise
  favors immediate mobility; it does not search future states. Ties are random.
- **Search** maximizes the final piece count using depth-limited single-player
  search, with a memoization table indexed by occupancy and remaining depth.
  Choose depth 1–9 (default 9). At a depth cutoff, remaining mobility breaks
  ties in the piece-count score. Full depth searches through completion or a
  dead end and discovers nine-piece solutions from an empty board. Equivalent
  best moves are sampled randomly. Search values, depth, nodes, and time appear
  in histories.
- **Ollama** receives the rules, topology, occupied points, and allowed placements
  and selects an entry/destination pair. The engine derives the middle point and
  validates the response. The opening is also selected by the model. **Search
  assistance** defaults off: pure mode supplies no solver hints or rankings.
  When explicitly enabled, exact runtime search filters allowed moves to those
  retaining the greatest achievable final piece count; histories label this mode.

Model discovery allows 1,000 seconds; each decision allows 18,000 seconds
(5 hours), including model loading and any thinking stages. Cancellation remains
available throughout.

Ollama model discovery uses `/api/tags`; decisions use structured, non-streaming
`/api/generate` calls. Extended thinking uses advertised native controls when
available, or the existing two-stage policy for legacy Cogito. Failures are
visible and stop the attempt without substituting another controller. Restart
retries from a fresh board. `OLLAMA_URL` overrides `http://127.0.0.1:11434`.

## Animation and controls

The 0–3 second delay (0.1-second steps; default 0.5) is the animation duration
**after** a decision is available. The new piece blinks at its entry for the
first half, then slides in a straight line to its destination during the
second half. Only arrival commits the move. Zero delay places immediately.
Changing the delay affects the next animation, preserving the current one.
The board and overlays scale together when resized.

Pause/Resume freezes automatic animations and holds worker results. Restart
begins a fresh attempt with the same settings; New configuration abandons the
attempt and unlocks settings. Both cancel obsolete workers and animations.
Human moves are blocked during placement, and Undo applies to completed moves.
Solved and stuck boards remain visible until restarted or reconfigured.

The interface supports English, Portuguese, Spanish, and Italian, with live
switching that preserves the board and pending work. Language preferences are
stored under the user configuration directory at `ai-games/pentalpha.ini`.

## Histories, logging, and reproducible experiments

Completed attempts, including dead ends, append to `pentalpha-history.md` in
the working directory. Records include controller, seed, options, move triples,
evaluations, decision/animation timing, and final count. Abandoned attempts
are not saved; undoing a terminal attempt starts a continuation that can produce
another completed record. Randomness is seeded per attempt and per decision.

Complete Ollama communication, including returned thinking, appends to
`pentalpha-ollama.log`; failures also append to `pentalpha-ollama-error.log`.
The existing `OLLAMA_LOG_FILE` and `OLLAMA_ERROR_LOG_FILE` overrides apply;
`/dev/null` disables the corresponding output. See [logging guidance](../docs/logging.md).

The dedicated headless runner supports Random, Heuristic, and Search:

```sh
./build/puzzle-runner --controller=search --depth=9 --runs=20 --seed=17
./build/puzzle-runner --controller=heuristic --runs=100 --seed=0
python3 ../scripts/test-pentalpha-runner.py
```

CSV columns are `game,seed,controller,depth,result,pieces,sequence,nodes,elapsed_us`.
Sequences use `entry>middle>destination` triples separated by spaces. Seeds
increment per attempt. Timing varies; choices and node counts are reproducible.
The existing two-player tournament runner remains specific to the other games.

Tests compare Search with an independent endpoint-graph reference over reachable
states, verify runtime solutions and undo, replay CSV independently, exercise
mock Ollama calls and logs, and check animation phases and UI lifecycle. GUI
checks and the screenshot tool need a real or virtual display:

```sh
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- make test-ui
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- ./build-ui/screenshot ../screenshots
```

### Pause at the end

Configuration includes **Pause at the end (solved or blocked)**, off by default.
When enabled, the final placement finishes and the attempt is recorded before a
modal acknowledgement pauses play. Select **Continue** or close the dialog to
review the unchanged final board; use Restart, New configuration, or Human Undo
to proceed. With the option off, the existing final-board behavior is unchanged.
The setting applies to all controllers and is retained when restarting within
the current session.
