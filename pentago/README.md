# AI Games: Pentago

A standalone C11, GTK 4/libadwaita application for comparing plain Minimax with
alpha-beta pruning. Configure each player independently and press **Begin**.
The default is Human Black versus alpha-beta White at requested depth 3.

## Rules and controls

The 6×6 board has four 3×3 quadrants: **A** top-left, **B** top-right,
**C** bottom-left, **D** bottom-right. Black starts on an empty board.
Each turn places one marble on an empty cell, then rotates any quadrant
90° clockwise or counterclockwise. The quadrant need not contain the new marble.

Five **or more** consecutive marbles horizontally, vertically, or diagonally win.
A placement that makes five wins **immediately, before rotation**. Otherwise,
check both colors after rotation: a single winning color wins, including the
opponent; simultaneous winning colors draw. A full board without a line draws.
There are no captures, passes, or optional nonwinning placement-only moves.
Rotating an empty or symmetric quadrant is legal even when its appearance does
not change. Rules source: [Mindtwister Pentago booklet](https://zacatrus.es/media/instrucciones/Pentago_Reglas_zacatrus.pdf).
No source artwork or precomputed solutions are included.

Click a cell to preview placement, then use a quadrant's **↷ / ↶** button.
You can change the preview by clicking another empty cell, or select **Cancel
placement**. Arrow keys select a cell; Enter/Space previews placement;
**A–D** rotate clockwise, **Shift+A–D** counterclockwise, and Escape cancels the
preview. Coordinates a–f/1–6 stay fixed; row 1 is at the top.

Placement blinks in the first half of the animation; the chosen quadrant and
its marbles rotate in the second half. Only the complete animated turn commits
to the engine and history. Zero delay commits immediately. **Pause/Resume**
freezes animation and holds completed background decisions. **Undo** restores
one completed turn in unfinished games with a human player and leaves play
paused. Restart and New configuration cancel outstanding work and previews;
stale responses cannot affect a new board. Finished boards stay visible.

Status/errors have a fixed 120-pixel scrolling panel above the board; rules
have a fixed 80-pixel scrolling panel below it. Text growth does not resize the
board. Text can be selected and copied. Language changes preserve play and
custom search settings. English, Portuguese, Spanish, and Italian preferences
are stored in `~/.config/ai-games/pentago.ini`.

## Controllers and fair comparisons

- **Human** chooses placement and rotation.
- **Random** samples uniformly among complete legal actions. Rotations producing
  identical successors remain distinct actions; there is no opening book.
- **Heuristic** evaluates every legal successor and selects a highest score,
  breaking ties with seeded randomness.
- **Minimax (plain)** searches every continuation to the selected depth.
- **Minimax (alpha-beta)** uses the same negamax evaluation, move order, root
  candidates, iterative deepening, and tie-breaking, adding only alpha-beta
  cutoffs. Each root action uses a full window: completed per-action values and
  best-move sets agree with plain Minimax at equal depth. There are no
  transposition tables, symmetry reductions, selective searches, or solved data.
- **Ollama** chooses its own complete move, including the opening. It receives
  rules, board, and legal actions without heuristic scores or search advice.
  Refresh models explicitly; optional Extended thinking supports native thinking
  metadata and the existing two-stage Cogito v1 protocol. Invalid, illegal,
  incomplete, failed, or timed-out responses stop visibly without a fallback.

The heuristic sums every uncontested length-five window: one through four own
marbles score **1, 10, 100, 1000**. Subtract the opponent's sum. A six-cell line
contains two windows. Terminal wins/losses override with **±1,000,000**; draws
score zero. This simple evaluation does not prove threats or perfect play.

Both searches offer **Weak (depth 1)** and **Strong (depth 3)** presets, plus
custom depths **1–6**. Depth counts complete turns, not placement and rotation
separately. Both presets set **5 seconds** and **64 MiB** per decision;
custom time and memory limits are independent for each side. Time zero means
unlimited. Strong is relative to Weak, not an expert-strength claim: Pentago
can have 288 opening actions, and the requested depth may exceed the budget.

Only completed root iterations replace the decision. A deeper interrupted
iteration retains the previous completed values and reports the limit. Without
a complete iteration, play stops; adjust limits and select **Retry**. Cancellation
discards the result. Nodes, leaves, cutoffs, completed depth, elapsed time, and
conservative accounted working-memory estimates are reported. Memory describes
search working structures, not process RSS or Ollama model memory.

## Build and test

```sh
make GAME=pentago run
make GAME=pentago test MESON_ARGS=-Dwerror=true
make GAME=pentago test-ollama MESON_ARGS=-Dwerror=true
make GAME=pentago test-ui MESON_ARGS=-Dwerror=true
python3 scripts/test-pentago-tournament.py
```

Engine-only builds (`-Dapp=false`) require GLib and libm. GUI builds also require
GTK 4, libadwaita, libsoup 3, and JSON-GLib, following the root README.
`make GAME=pentago install` installs executable `pentago` and desktop ID
`io.github.aigames.Pentago`. GUI tests and screenshots require a display; see
[the screenshot guide](../docs/screenshots.md).

Tests independently implement coordinate-based rotation and line detection,
check legal/illegal/terminal actions and draw cases, compare both searches with
exhaustive reference search, and exercise limits, cancellation, seeded choices,
mock HTTP protocol, atomic animation, provisional placement, undo, restart,
translation, stale responses, and stable board allocation during text overflow.

## Experiments and histories

```sh
pentago/build/tournament --games=10 --seed=0 \
  --first=minimax --second=alpha-beta --depth-first=2 --depth-second=2 \
  --time-first=0 --time-second=0
pentago/build/tournament --games=10 --seed=0 \
  --first=alpha-beta --second=minimax --depth-first=2 --depth-second=2 \
  --time-first=0 --time-second=0
```

The headless runner supports `random`, `heuristic`, `minimax`, and `alpha-beta`.
Independent controls are `--depth-first/second`, `--time-first/second` (seconds),
and `--memory-first/second` (MiB). `--node-limit=N` optionally sets a deterministic
per-decision work limit for both sides; zero disables it. Run `--help` for details.

CSV records controllers, depths, budgets, seed, result, marble/turn counts,
complete move sequence, aggregate nodes/leaves/cutoffs, elapsed microseconds,
last per-player search status/completed depth, limited-decision count, and peak
accounted search memory. Example move `c4:A:cw` places at c4 **before** rotating
A clockwise; `e1:win` is a terminal placement without rotation. Results are
`black`, `white`, `draw`, or `incomplete`; stopping search never fabricates a
winner. The two players have independent seeded PRNG streams. With time limits
disabled, repeated seeds reproduce decisions; omit elapsed time when comparing
CSV. Wall-clock limits can change completed depth and choices.

For pruning comparisons, use equal completed depths with nonbinding limits.
For strength comparisons, vary depth explicitly; play both colors and report
seeds, limits, code revision, and completed work. A favorable sample is not a
general win-rate estimate or evidence of perfect play.

Completed GUI wins **and draws** append to `pentago-history.md` in the working
directory. Reports include settings, decision seeds, complete actions, heuristic
components, and search statistics. Abandoned games are not saved. Communication
uses `pentago-ollama.log` and `pentago-ollama-error.log`; the standard
`OLLAMA_LOG_FILE` and `OLLAMA_ERROR_LOG_FILE` overrides apply. See the
[privacy/rotation guide](../docs/logging.md). Nothing uploads automatically.

`OLLAMA_URL` overrides `http://127.0.0.1:11434`. Model discovery uses the existing
1,000,000 ms timeout, and decisions have a total 18,000,000 ms budget. Requests
remain cancellable. The response is exactly one property, for example
`{"move":"c4:A:cw"}`, validated against the current complete legal actions.
