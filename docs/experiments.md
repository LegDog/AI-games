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

## Pentalpha single-player experiments

Pentalpha has a dedicated `pentalpha/build/puzzle-runner` rather than two-player
tournaments. Use `--controller=random|heuristic|search`, `--depth=1..9`,
`--runs=N`, and `--seed=N`. CSV includes final piece count, solved/stuck outcome,
move triples, search nodes, and elapsed microseconds. Compare controller success
counts and seeds, keeping assisted Ollama attempts separate from pure attempts.
`python3 scripts/test-pentalpha-runner.py` checks repeatability and independently
replays all exported placements. See [Pentalpha](../pentalpha/README.md).

## Heptalpha single-player experiments

`heptalpha/build/puzzle-runner` supports the same controller/seed/run options as
Pentalpha, with depths 1–20 (default 20). Its sequence field additionally includes
the curve identifier: `entry>middle>destination@pN`. Run
`python3 scripts/test-heptalpha-runner.py` for independent path-aware replay and
repeatability checks. Compare pure and assisted Ollama histories separately.
Full-depth Search discovers twenty-piece solutions at runtime; no predefined
solution sequence is shipped. See [Heptalpha](../heptalpha/README.md).

## Peg Solitaire

`peg-solitaire/build/puzzle-runner --board=english --goal=center --controller=backtracking --runs=10 --seed=0` discovers plans at runtime. Select `triangle`, `random`, `heuristic`, or `astar` for comparisons. Set `--time-limit` (seconds, zero unlimited) and `--memory-limit` (MiB). CSV includes search status separately from board result, legal jump sequences, remaining pegs, nodes, elapsed time, and peak accounted search memory. `python3 scripts/test-peg-solitaire-runner.py` independently replays both boards and goals. See the [rules and algorithm notes](../peg-solitaire/README.md).

## Pentalfa Cruzado Circulado

`pentalpha-crossed-circle/build/puzzle-runner --controller=search --depth=3 --runs=10 --seed=0 --time-limit=60 --memory-limit=256` uses bounded iterative deepening per placement. CSV records requested depth, minimum/last completed depths, last status, limited-decision count, nodes, and peak accounted memory. Interrupted deeper iterations retain only a completed shallower evaluation. A blocked result is possible. Run `python3 scripts/test-pentalpha-crossed-circle-runner.py` for independent path-aware replay; see the [game README](../pentalpha-crossed-circle/README.md) for all fields.

Pentalfa Cruzado Circulado also supports `connectivity`, `spanning-forest`, `gf2-forest`, and `search-symmetry`. The first three construct or preserve an optimal 34-piece finish. Target 35 and the `stuck` board result remain unchanged; appended CSV proof fields identify the achieved optimum separately. The new symmetry variation preserves baseline completed-depth values and reports sibling prunes separately from cache hits. Compare with identical seeds and nonbinding limits; regression digests protect pre-existing decisions.

## Reversi

`reversi/build/tournament --games=20 --seed=0 --first=mcts --second=minimax --iterations-first=1000 --depth-second=4 --time-limit=5 --memory-limit=64` compares independent controllers under shared per-decision limits. Options include `random`, `positional`, `minimax`, and `mcts`. CSV includes disc counts, forced passes, coordinates, search statuses, completed work, nodes, and memory. Run `python3 scripts/test-reversi-tournament.py` for independent capture/pass/result replay. See the [rules and algorithm notes](../reversi/README.md).

## Hex

`hex/build/tournament --games=20 --seed=0 --size=5 --first=mcts --second=minimax --iterations-first=1000 --depth-second=3 --time-limit=5 --memory-limit=64` compares connection search on small boards. Controllers are `random`, `heuristic`, `minimax`, and `mcts`; sizes are 3, 5, and 7. Add `--no-swap` to disable the pie rule. CSV separates stone placements from actions (swap consumes a turn), and includes the rules, move sequence, winner, search status, completed work, and memory. `python3 scripts/test-hex-tournament.py` independently replays connectivity and opening swaps. See [rules and algorithm notes](../hex/README.md).

## Tavli

`tavli/build/tournament --variant=backgammon --session=fixed --target=5 --games=10 --seed=0 --first=expectiminimax --depth-first=2 --second=mcts --iterations-second=1000 --time-limit=5 --memory-limit=64` compares dice-aware players including cube decisions. Select `portes`, `plakoto`, `fevga`, `backgammon`, or `running`; session modes are `single`, `fixed`, and `traditional`. CSV includes replayable opening/ordinary dice, complete moves, cube events, score, search status/work, and memory. Time/turn exhaustion is `incomplete`. Real dice have a separate seed stream from simulations. `python3 scripts/test-tavli-tournament.py` independently replays all five variants and match rules. See [Tavli rules and algorithm notes](../tavli/README.md).

## Sokoban

`sokoban/build/puzzle-runner --level=6 --controller=astar --memory-limit=256 --seed=0`
compares push-based puzzle solving. Controllers are `astar`, `greedy`, and `random`;
add `--no-pruning` for a deadlock-pruning comparison. `--file=pack.xsb --level=2`
selects an imported level. CSV contains the normalized initial map (`|` separates
rows), configuration, action sequence, pushes, walking steps, search status,
optimality and resource statistics. Lowercase `udlr` means walking; uppercase
`UDLR` means pushing. Resource/push limits are incomplete outcomes.
`python3 scripts/test-sokoban-runner.py` independently replays moves and compares
small A* solutions against exhaustive 0/1 BFS. See [Sokoban](../sokoban/README.md).

## Go

`go/build/tournament --first=mcts --second=heuristic --iterations-first=1000
--komi=7.5 --games=5 --seed=0` compares players on 9×9 with Chinese-style area
scoring, no suicide, and positional superko. Controllers are `random`, `heuristic`,
and `mcts`. Time and memory are per decision; `--action-limit=1000` bounds whole
games. CSV records all settings, placements/passes, captures, area scores,
neutral points, MCTS cutoffs and limits. Incomplete games have no winner; their
area fields are estimates of the stopped board. `python3 scripts/test-go-tournament.py`
independently verifies captures, repetition legality and scoring.
See [Go rules and algorithms](../go/README.md).
