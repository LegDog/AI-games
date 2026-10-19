# AI Games: Hex

A standalone C11, GTK 4, and libadwaita application for comparing human and
algorithmic players on small Hex boards. Choose **3×3**, **5×5** (default), or
**7×7**, independently configure both players, and press **Begin**.

## How to play

1. **Red starts** on an empty board. Red aims to connect the **top and bottom**
   red borders; Blue aims to connect the **left and right** blue borders.
2. Take turns placing one stone on an empty hexagon. Click a cell, or select it
   with the arrow keys and press Enter/Space. Each cell shows its coordinate:
   columns a–g and rows 1–7, restricted to the selected board size.
3. Stones of your color connect through shared edges. Each interior cell has
   six neighbors. For example, b2 touches b1, c1, a2, c2, a3, and b3; **a1 and
   c3 do not touch b2**. Corners belong to both adjacent board borders.
4. The first connected chain of your stones joining your two opposite borders
   wins immediately. There are **no captures, passes, or draws**. Stones remain
   where placed, except for the optional opening swap below. Stone counts do
   not determine the winner. A gold outline highlights a winning path; the
   final position stays visible until Restart or New configuration.

The optional **pie rule** is enabled by default. After Red's first placement,
Blue may place normally or select **Swap opening** instead. The implementation
keeps player colors and goals fixed: it removes the Red opening stone,
reflects its coordinate across the main diagonal, and recolors it Blue.
For example, **b3 becomes c2**; c3 remains c3 but changes color. Red plays next.
This is equivalent to exchanging colors/goals and reflecting the board.
Swap is available only as the second action, consumes a turn, and does not add
another stone. Algorithms and Ollama may also choose it. Disable **Allow opening
swap** before Begin to play without this rule.

Rules references: [University of Edinburgh Hex page](https://webhomes.maths.ed.ac.uk/~csangwin/hex/index.html)
and [Little Golem Hex rules](https://docs.littlegolem.net/games/hex/).
The small board sizes and reflected presentation of swap are application choices.

**Pause/Resume** freezes animation and prevents further moves; a running
background decision can finish and waits for Resume. **Undo** restores one
completed action, including an opening swap, and leaves the game paused. Undo
is available in unfinished games with a human player. **Restart** begins a new
attempt with the same configuration; **New configuration** returns to setup.
A search failure stops visibly: adjust limits and use **Retry** from the current
position. Restart/configuration/undo cancel old work and discard stale results.

Stones blink during the first half of the animation delay and become visible
in the second half; swap slides the reflected stone in the second half.
The board state changes only when animation finishes. Delay zero commits
immediately. Changing language preserves the game.

## Players and limits

- **Human** chooses a legal cell or the opening swap.
- **Random** samples uniformly among all legal actions, including swap when
  available. There is no special opening book.
- **Connection heuristic** scores each legal successor with Dijkstra shortest
  paths between both players' goals. Own stones cost 0, empty cells cost 1,
  and opposing stones are impassable. A blocked distance is `size² + 1`.
  Score is `100 × (opponent distance − own distance)`; terminal wins/losses
  override it with ±100000. Equal scores use seeded random selection. This
  deliberately simple evaluation does not prove virtual connections or account
  for all bridges, threats, or alternate routes.
- **Minimax** uses negamax with alpha-beta pruning and iterative deepening.
  Leaves use the connection heuristic. Only a fully completed root depth
  replaces the previous decision; an interrupted partial depth is discarded.
  Default depth 3; configurable from 1 to 10. Depth counts actions, including
  swap. There is no precomputed strategy or claim of perfect play.
- **Monte Carlo tree search** uses UCT selection, one-action expansion, uniform
  random legal rollouts, and backpropagation of root-player wins/losses.
  Selection reverses exploitation on the opponent's turn. Choose the root move
  with most visits, then highest mean reward, then a seeded tie-break. Default
  1000 simulations; configurable from 1 to 1,000,000. All root actions, including
  swap, enter the tree. Small budgets can leave actions unvisited.
- **Ollama** makes its own decision from the rules, board, and legal-action
  JSON schema, including the opening and swap. It receives no heuristic scores,
  search results, or suggested moves. Refresh models explicitly, choose a model
  for each Ollama player, and optionally enable Extended thinking. Native
  thinking and Cogito reasoning follow the other applications' protocol.
  Invalid, illegal, timed-out, or failed answers stop without a fallback move.

Minimax and MCTS share configurable per-decision time and working-memory budgets:
**5 seconds**, **64 MiB** by default; time 0 means unlimited. Memory controls the
search structures, not total process RSS or Ollama's model memory. MCTS bounds
its allocated tree; Minimax checks a conservative stack/work estimate. Reaching
a limit uses a completed decision if one exists and reports the limit; otherwise
Retry is required. Cancellation discards the decision. Both algorithms report
completed work, nodes, elapsed time, and estimated/allocated memory. Defaults
are teaching configurations, not tuned strong Hex engines.

## Build and run

From the repository root:

```sh
make GAME=hex run
make GAME=hex test
make GAME=hex test-ollama
make GAME=hex test-ui
```

See the [root README](../README.md) for development dependencies. Engine-only
builds use `meson setup hex/build-tests hex -Dapp=false` and require GLib and
libm without GTK or network libraries. Install with `make GAME=hex install`;
executable `hex`, desktop ID `io.github.aigames.Hex`.

`OLLAMA_URL` optionally overrides `http://localhost:11434`. Model discovery has
a 1,000,000 ms timeout; each model decision has an 18,000,000 ms timeout, retaining
the project's generous limits. The GUI stays cancellable during requests.
Language selection is saved in `~/.config/ai-games/hex.ini` and supports English,
Portuguese, Spanish, and Italian.

## Experiments, history, and verification

```sh
hex/build/tournament --games=20 --seed=0 --size=5 \
  --first=mcts --second=minimax --iterations-first=1000 \
  --depth-second=3 --time-limit=5 --memory-limit=64
python3 scripts/test-hex-tournament.py
```

Runner controllers: `random`, `heuristic`, `minimax`, `mcts`. Use `--no-swap` to
disable the pie rule. Per-player depth/iteration flags are independent. CSV records
size, rules, swap use, winner, stone/placement/action counts, complete coordinate
sequence (with `swap`), seed, search status, completed work, nodes, elapsed time,
and peak search memory. Each side has an independent seeded GLib PRNG.
With time limits disabled and sufficient memory, repeated seeds reproduce
choices; wall-clock limits may change completed work. A stopped undecided run
reports `ongoing`, never a fabricated winner.

Completed GUI games append to `hex-history.md` in the working directory,
including controller settings, decision seeds, candidates' connection distances,
search values/visits, and moves. Ollama communication uses `hex-ollama.log`
and `hex-ollama-error.log`; see [logging](../docs/logging.md) for overrides and
privacy. Nothing uploads automatically. The icon and Cairo board are original
vector artwork; no external board assets or solved games are bundled.

Tests independently replay connectivity and swap, exhaustively check all 512
filled 3×3 boards for exactly one winner, compare Minimax against unpruned
reference search, check deterministic MCTS and resource/cancellation behavior,
exercise mocked Ollama requests, and verify GUI animation/undo/restart/lifecycle.
