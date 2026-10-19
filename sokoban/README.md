# AI Games: Sokoban

A standalone GTK 4/libadwaita box-pushing puzzle. Play yourself or compare
Random, Greedy Best-first, A*, and pure Ollama. All plans are found at runtime.

## How to play

Move the blue player up, down, left or right through floor cells. Walk into a
box to push it one cell, provided the cell beyond is empty floor. You cannot
pull boxes, push two boxes together, walk through walls, or move diagonally.
A box on a goal may be pushed again. Cover every green goal with a box to win.

Choose a level and controller, then **Begin**. Eight original levels range from
one to six boxes. Arrow keys or WASD move the player when the board has focus;
click an adjacent destination to move there and focus the board. **Undo** reverses
one committed step or push, including after completion or a detected deadlock.
**Restart** restores the selected level. **Configure** cancels pending work and
returns to setup. Completed boards remain visible; there is no automatic advance.

A box stranded in a non-goal corner cannot be recovered except with Undo or
Restart. Other deadlocks also exist. Detection covers reverse-push dead squares,
impossible assignments to goals, frozen 2×2 wall/box blocks with an off-goal box,
and positions with no reachable push. It does not detect every possible deadlock.
The game never equates reaching a resource limit with proving a deadlock.

## Importing levels

**Import XSB levels** opens a local text file; **Built-in levels** restores the
original pack. Import replaces the level list only after the entire file passes
validation. A failed import preserves the previous list and board.

| Character | Meaning |
|---|---|
| `#` | Wall |
| space | Floor inside the enclosed warehouse |
| `.` | Goal |
| `$` | Box |
| `@` | Player |
| `*` | Box on goal |
| `+` | Player on goal |

Use blank lines between levels and semicolon-prefixed comments/titles. Both LF
and CRLF are accepted. Coordinates count from 1 at the top left, right/down.
Leading spaces are preserved; ragged padding and exterior spaces are inaccessible.
Maps must be enclosed, with exactly one player and equal positive box/goal counts.
Limits: 32×32 cells, 16 boxes per level, 256 levels, 1 MiB per UTF-8 file. Run-length
encoding, editors, online downloads and non-XSB collection metadata are not included.

```text
; First delivery
######
#@ $.#
######
```

## Algorithms and limits

Automated actions are **reachable pushes**: choose a box and direction, find a
shortest walk behind it, then push. Player walks are reconstructed individually.
Boxes are interchangeable; search keys combine their sorted locations with the
player's reachable region. Walking inside that region costs zero search edges.
Each push costs one. Displayed **Walking** excludes pushing steps; total movement
is Walking + Pushes.

- **A*** minimizes pushes. Its admissible heuristic is a minimum-cost perfect
  assignment of boxes to goals, using reverse-push distances that respect walls
  and supporting cells but ignore other boxes. Duplicates retain the best push
  cost and reopen when improved. Successful A* completion certifies minimum
  pushes from the current position; walking distance is not globally optimal.
- **Greedy Best-first** prioritizes the same heuristic without accumulated push
  cost. It can find solutions quickly but does not certify optimality.
- **Random** samples reachable pushes with seeded choices.
- **Deadlock pruning**, enabled by default, rejects conservatively proven dead
  branches for native algorithms. Disable it for comparisons. Impossible assignment
  estimates become zero instead of pruning when disabled, preserving admissibility.
  Current-board deadlocks still stop playback so the user can inspect the board.

Planning defaults to **60 seconds and 256 MiB**; zero seconds removes the time
limit. Accounted memory includes fixed-capacity node, heap and hash arenas,
preprocessing, path storage and conservative scratch/overhead allowances, rather
than process RSS. Arena exhaustion stops visibly; states are not silently evicted.
The allocation may be below the selected ceiling because capacities are powers of
two. Disabling pruning can exhaust small budgets even on bundled levels.

Results distinguish solved, detected deadlock, exhaustive unsolvability, resource
limit, cancellation and error. Adjust limits and **Retry** from the current board.
Random and Ollama have a configurable push limit, initially 10,000; reaching it is
incomplete. No algorithm silently falls back to another.

## Animation and Ollama

Each configured delay covers one walking/pushing step: blink the player for half
the delay, then slide during the other half. A push slides player and box together.
State changes atomically at arrival. Zero delay commits immediately. **Pause**
freezes animation and playback and holds completed planning results. Restart,
configuration, imports and level changes invalidate stale work.

Set the Ollama address, **Refresh models**, select a model and optionally enable
thinking. Ollama receives rules, a coordinate-labelled board and all reachable
pushes, without solver scores, pruning filters, certified choices or solutions.
It returns `{"x":4,"y":2,"direction":"right"}` for one box push. The application
computes the supporting walk and validates every returned action locally. Invalid
responses and request failures stop visibly for Retry; there is no fallback.

Native thinking and legacy Cogito's separate analysis stage follow the other apps.
Model discovery allows 1,000 seconds; a complete move allows 18,000 seconds,
including model loading and all thinking stages. Cancellation remains immediate.

## Build, tests and experiments

```sh
make GAME=sokoban
make GAME=sokoban run
make GAME=sokoban test
make GAME=sokoban test-ollama
xvfb-run -a dbus-run-session -- make GAME=sokoban test-ui
python3 scripts/test-sokoban-runner.py
sokoban/build/puzzle-runner --level=6 --controller=astar --seed=0
sokoban/build/puzzle-runner --file=pack.xsb --level=2 --controller=greedy --no-pruning
```

Dependencies: C11, Meson, GLib/GIO, GTK 4, libadwaita, libsoup 3 and JSON-GLib.
Engine tests build without graphical dependencies. Languages: English, Portuguese,
Spanish and Italian, switchable during play. Language is saved in
`~/.config/ai-games/sokoban.ini`. Install with `make GAME=sokoban install`.
Desktop ID: `io.github.aigames.Sokoban`.

The headless runner supports `astar`, `greedy`, `random`, level/file selection,
runs, seed, pruning, time/memory limits and Random's push limit. CSV includes a
replayable normalized initial map (`|` separates rows), configuration, results,
minimum-push certification, pushes, walking, nodes, elapsed microseconds, accounted
peak memory, and actions (`udlr` walking, `UDLR` pushing). Fixed seeds are
repeatable apart from timing and time-limited cutoffs.

The GUI writes outcomes and solver/model details to `sokoban-history.md` in its
working directory. Full Ollama exchanges go to `sokoban-ollama.log`, failures also
to `sokoban-ollama-error.log`. Standard logging environment overrides apply;
see [logging](../docs/logging.md). `make run` uses the game's directory.

## Rules, levels, artwork and license

Rules: [Sokoban official site](https://sokoban.jp/rule.html). All eight bundled
levels and Cairo/SVG artwork are original project work. No commercial levels,
source illustrations, solution sequences or databases are distributed. Code and
original assets are GPL-3.0-or-later; see [COPYING](COPYING).
