# Reproducible experiments

Build from the repository root with `make`. Each app also builds a headless
`tournament` executable linked to its own engine. It supports Positional and
Minimax players and requires no display, network, or Ollama service. Engine-only
builds (`-Dapp=false`) include the runner too.

```sh
mkdir -p results
git rev-parse HEAD > results/revision.txt
four-in-a-row/build/tournament --games=100 --seed=0 --first=positional --second=minimax --depth-second=6 > results/four-in-a-row.csv
tic-tac-toe/build/tournament --games=100 --seed=0 --first=minimax --second=positional --depth-first=9 > results/tic-tac-toe.csv
```

Run `--help` for options. Defaults are 10 games starting at seed 0, Positional
first and Minimax second; depth defaults to 9 in Tic-Tac-Toe and 6 in Connect
Four. Each game's seed increments by one. Both controllers have independent
random streams derived from that seed. Player order remains fixed; repeat with
the controllers swapped to compare both sides. Depth limits are 1–9 and 1–12.
Minimax has no turn deadline; high depths can take substantial time.

CSV includes game, seed, controllers, both depths, result (`first`, `second`,
or `draw`), move count, and the complete move sequence. Sequence entries are
numeric-keypad positions 1–9 for Tic-Tac-Toe and columns 1–7 for Four in a Row.
Games stop at the first terminal position. Timing is excluded so identical
invocations can produce identical exports. Match code revisions, compiler,
GLib version, and platform when reproducing comparisons; implementations and
floating-point behavior may change between environments.

Record build options and system versions alongside the CSV. A modified working
tree needs its diff as well as the commit hash. Raw exports under `results/`
are ignored by Git; publish selected, reviewed findings with their setup and
limitations in documentation. No conclusion should rely only on a favorable
seed or one player order.

`python3 scripts/test-tournament.py` runs both default-build runners twice,
compares outputs, independently replays moves and results, checks invalid
arguments, and verifies full-depth Tic-Tac-Toe Minimax draws.
