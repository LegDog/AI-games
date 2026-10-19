# AI Games: Peg Solitaire

A standalone GTK 4/libadwaita puzzle with Human, Random, Heuristic, Backtracking,
A*, and Ollama controllers. All solutions are discovered at runtime.

## How to play

Choose a board and controller, then Begin. A legal jump moves one peg over an
adjacent peg into an empty hole in a straight line. The jumped peg is removed.
Each jump removes exactly one peg; chained jumps are separate moves.

- **English cross:** 33 holes, 32 pegs, center `d4` empty. Jump horizontally or
  vertically. Finish with one peg anywhere, or enable **Finish in center** to
  require the final peg at `d4` (31 jumps).
- **Triangle:** 15 holes in five rows, 14 pegs, apex `a1` empty. Jump along any
  of the three board axes. Finish with one peg anywhere (13 jumps).

Columns are letters and rows are numbers, shown on the board. Click a peg,
then its empty destination; Escape clears the selection. Undo reverses a human
jump. No available jump with several pegs is a stuck board. One peg outside the
required center is a wrong finish, rather than a win.

Restart restores the selected opening. Configure returns to setup and cancels
pending work. Search limits can be adjusted after a stopped search; Retry plans
from the current board. Board editing and alternate opening holes are not included.

## Animation and controls

The selected delay applies to a complete jump: the source blinks for half the
delay, then hops to the destination while the captured peg fades near arrival.
The board changes atomically at arrival. Zero delay commits immediately. Pause
freezes animation and automated play, including a completed pending plan.
Changing the delay affects subsequent jumps. Restart and configuration cancel
stale animation, search, and Ollama responses.

## Algorithms

Random samples legal jumps. Heuristic ranks mobility, isolated pegs, and distance
to the board centroid. Ties use a seeded generator.

Backtracking uses heuristic move ordering, symmetry canonicalization, and a
bounded cache of fully exhausted positions. A* uses `f = depth + pegs - 1`.
Every jump reduces the peg count by one, so this lower bound is constant on this
puzzle; deeper positions, heuristic score, and seeded ties order equal priorities.
All winning paths have the same number of jumps. The complete generated plan is
revalidated and played one jump at a time.

Planning defaults to **60 seconds and 256 MiB**. Zero seconds means no time limit;
the memory limit remains active. Memory accounting includes allocated search
structures and conservative stack/overhead allowances, rather than total process
RSS. Status distinguishes solved, proved unsolvable, resource limit, cancellation,
and error. A stopped search does not silently switch algorithms or claim the
position is unsolvable. Retry retains the current board.

Ollama receives the rules, board, goal, and legal jumps. Pure mode is the default.
Optional assistance uses Backtracking to certify jumps with a winning continuation.
Unknown jumps remain unknown. If limits interrupt certification after at least one
proof, the model may choose from the explicitly identified certified subset;
otherwise play stops visibly. Every returned jump is validated locally. No
precomputed solution sequences or databases are supplied to any controller.

Set the Ollama server and model in configuration. Thinking, complete communication
logs, and cancellation follow the other apps. Model discovery timeout is 1000
seconds; turn timeout is 18000 seconds. Planning limits separately bound assisted
search. Native thinking and Cogito model policies are supported.

## Build, run, and test

From the repository root:

```sh
make GAME=peg-solitaire
make GAME=peg-solitaire run
make GAME=peg-solitaire test
make GAME=peg-solitaire test-ollama
xvfb-run -a dbus-run-session -- make GAME=peg-solitaire test-ui
python3 scripts/test-peg-solitaire-runner.py
```

Dependencies match the other apps: C11, Meson, GLib, GTK 4, libadwaita, libsoup 3,
and JSON-GLib. Engine tests can build without graphical libraries. The UI supports
English, Portuguese, Spanish, and French. Install with `make GAME=peg-solitaire install`.
The desktop ID is `io.github.aigames.PegSolitaire`.

## Experiments and records

```sh
peg-solitaire/build/puzzle-runner --board=english --goal=center --controller=backtracking --runs=5 --seed=0
peg-solitaire/build/puzzle-runner --board=triangle --controller=astar --time-limit=60 --memory-limit=256
```

The runner supports random, heuristic, backtracking, and astar. CSV records game,
seed, controller, board, goal, result, search status, remaining pegs, jumps,
`source>middle>destination` sequence, nodes, elapsed microseconds, and peak accounted
search bytes. Timing may vary; seeded choices are repeatable.

`peg-solitaire-history.md` records decisions, candidate scores, solver statistics,
and outcomes. Full model exchanges go to `peg-solitaire-ollama.log`; failures also
go to `peg-solitaire-ollama-error.log`. Files are created in the working directory;
`make run` runs inside this game's directory. See [logging](../docs/logging.md).

## Rules, artwork, and license

Background: [Simon Tatham's Pegs rules](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/pegs.html),
[Peg Solitaire rules](https://pegsolitaire.app/rules/), and
[George Bell's research on triangular boards](https://arxiv.org/abs/0903.3696).
The board drawings and application icon are original code/vector artwork. Code is
GPL-3.0-or-later; see [COPYING](COPYING).
