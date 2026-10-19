# AI Games: Go

A standalone GTK 4/libadwaita application for comparing Human, Random, Heuristic,
MCTS, and pure Ollama players on **9×9**. Default: Human Black versus Heuristic
White, with **7.5 komi**. Every decision is computed at runtime.

## How to play

1. Start with an empty board. Black plays first, then colors alternate. Click an
   intersection, or focus the board and use arrows with Enter/Space. Columns are
   **A–H, J** (skip I); rows **1–9** increase from bottom to top.
2. A stone's orthogonally adjacent empty intersections are its **liberties**.
   Orthogonally connected stones of one color form a group and share liberties.
   Diagonals do not connect stones or provide liberties.
3. Place on an empty intersection. Remove every adjacent opposing group with
   no liberties. Then check the new friendly group: a placement leaving it with
   no liberties is **suicide**, which is forbidden. A capture that creates a
   liberty is legal even if that point had no empty neighbors beforehand.
4. **Positional superko:** a placement must not recreate any earlier board
   arrangement, including the initial empty board. The side to move and capture
   totals do not change this rule. Full boards are compared when hashes match;
   hash collisions alone cannot reject a move.
5. **Pass** instead of placing. Passing is always legal during play and exempt
   from superko. A placement resets the consecutive-pass counter. Two consecutive
   passes finish the game immediately.
6. **Chinese-style area scoring:** each color gets one point for every remaining
   stone and each empty intersection in a region bordered exclusively by that
   color. Regions touching both colors, or neither color, are neutral. White adds
   komi, configurable before Begin from 0 to 20 in half-point steps. The larger
   score wins; equal scores draw. Captures are displayed but do not add points.

**Capture disputed or dead groups before passing.** This application has no
post-game dead-group marking, automatic life/death adjudication or score agreement
phase. Stones still present count for their color. Thus an early pair of passes
can produce a score different from an agreed tournament score. This is the
explicit played-out convention used for reproducible algorithm comparisons,
not a claim to implement every Chinese tournament regulation.

The final board remains visible with a territory overlay, both area scores,
White's komi, neutral count and winning margin. A red dot marks the previous
placement; red outlined squares mark its captured intersections. Numbered move
history includes passes. No handicap, resignation, clocks, SGF import/export,
larger boards or external Go engine is included.

Rules background: [British Go Association overview](https://www.britgo.org/gorules)
and [comparison of rules](https://www.britgo.org/rules/compare.html). The exact
choices above govern this application.

## Controls and animation

Choose each player's controller, komi and limits, then **Begin**. **Pause/Resume**
freezes animation and prevents further play. A background decision may finish
and waits for Resume. **Undo** restores one completed action and its capture,
turn, pass and repetition history, then leaves play paused. It is available in
unfinished games containing a Human player. **Restart** restores the opening
with the current configuration. **Configure** returns to setup. All cancel stale
workers and pending animations. Language changes preserve the game.

A placed stone blinks during the first half of the configured delay. During the
second half it becomes solid and captured groups fade. Placement and removal
commit atomically on arrival. Passing uses the same delay and appears in history;
zero delay commits immediately.

The default whole-game limit is **1,000 actions**, including passes. Reaching it
stops as **incomplete**, without a winner. Final scores are shown only after two
passes. Failed decisions stop visibly: adjust search settings and **Retry** from
the same position. History-write failures retain the board and permit Retry.

## Algorithms

- **Random** samples uniformly from all legal placements and Pass.
- **Heuristic** evaluates every legal successor from the moving color's view:
  `10 × area difference + 2 × group-liberty difference − 15 × atari-stone
  difference + 8 × newly captured opposing stones`. Group liberties are counted
  once per group; a shared liberty may contribute to several groups. Atari stones
  belong to groups with exactly one liberty. Completed wins/losses override the
  heuristic with ±100000, draws with 0. Ties use seeded random choice. Pass uses
  the same evaluation. Current-board area is a crude estimate during play: no
  life/death proof, eye preservation, opening book or strong-play claim is made.
- **MCTS** uses UCT selection, one-action expansion, uniform legal random rollouts,
  and backpropagation from the root player's view. Opponent turns reverse the
  exploitation sign. All legal root actions, including Pass, can enter the tree.
  Select by most visits, highest mean reward, then a seeded tie-break.
- **Ollama** receives the complete rules, komi, board, moves so far, captures,
  consecutive passes and legal choices. It independently returns
  `{"move":"D4"}` or `{"move":"pass"}`. No scores, suggested moves, native search
  assistance or precomputed strategy are supplied. Every answer is validated
  locally; malformed, illegal or failed responses stop without fallback.

### MCTS limits and estimates

Each player has a simulation count, default **1,000**. Per-decision defaults are
**five seconds and 64 MiB**. Zero seconds disables the deadline. Working-memory
accounting reserves tree storage, copied repetition histories, simulation scratch
space and overhead, rather than total process RSS. Superko is enforced against
both real-game history and the current simulated continuation. Branches never
share mutable histories.

Each simulation is capped at **324 additional actions**, including tree selection
and rollout. A completed game returns +1/−1 for a root-player win/loss, 0 for a draw.
An unfinished simulation returns its current area margin divided by `81 + komi`,
with the sign adjusted for the root player. This is a bounded estimate, not a
completed game or proven win. The UI and logs report cutoff counts.

A time/memory limit can return a partial decision using completed simulations;
its status remains `limit-reached`. With no completed simulation, play stops for
Retry. Interrupted simulations are not backpropagated. Cancellation and errors
never supply fallback moves. Random choices, rollouts and ties use seeded streams;
repeatability excludes elapsed time and time-limited cutoffs.

### Ollama configuration

Set the address, **Refresh models**, choose a model for each Ollama player, and
optionally enable thinking. Native thinking and legacy Cogito's separate reasoning
stage follow the other applications. Model discovery allows 1,000 seconds; a
complete move allows 18,000 seconds across loading, reasoning and generation.
Cancellation remains available throughout.

## Build, test and experiments

```sh
make GAME=go
make GAME=go run
make GAME=go test
make GAME=go test-ollama
xvfb-run -a dbus-run-session -- make GAME=go test-ui
python3 scripts/test-go-tournament.py
go/build/tournament --first=mcts --second=heuristic --iterations-first=1000 --seed=0
go/build/tournament --first=heuristic --second=random --komi=6.5 --games=10 --time-limit=0
```

The executable is **`go-game`**, avoiding a collision with the Go programming
language tool. Install with `make GAME=go install`; desktop ID:
`io.github.aigames.Go`. Dependencies: C11, Meson, GLib/GIO, GTK 4, libadwaita,
libsoup 3 and JSON-GLib. Engine tests build without graphical dependencies.
Languages are English, Portuguese, Spanish and Italian. Language is saved in
`~/.config/ai-games/go.ini`.

The headless runner supports `random`, `heuristic` and `mcts`, independent player
simulation counts, seed, komi, time/memory budgets, games and action limit. CSV
contains the rules, all configuration, replayable placements/passes, captures,
stones, territory, neutral points, scores, margin, MCTS work/cutoffs, limits,
elapsed time and accounted peak memory. Incomplete games have no winner; their
score fields describe the stopped position, not a completed result.

`go-history.md` records completed/incomplete games, decisions and settings in the
working directory. Full Ollama communication goes to `go-ollama.log`; errors also
to `go-ollama-error.log`. Standard environment overrides apply; see
[logging](../docs/logging.md). `make run` uses this game's directory.

## Artwork and license

The Cairo board and SVG icon are original project work. No trained networks,
external engines, opening books, solution databases or source artwork are bundled.
Code and original assets are GPL-3.0-or-later; see [COPYING](COPYING).
