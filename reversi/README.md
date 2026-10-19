# AI Games: Reversi

A standalone C11/GTK 4/libadwaita game with independently configurable Black and
White players: **Human, Random, Positional, Minimax, Monte Carlo tree search
(MCTS), and Ollama**. The default is Human Black against Positional White.

## How to play

This application uses the standard 8×8 opening and rules described by the
[World Othello Federation](https://www.worldothello.org/about/about-othello/othello-rules/official-rules/english).
Columns `a`–`h` run left to right; rows `1`–`8` run top to bottom. Initially,
White occupies `d4` and `e5`, and Black occupies `e4` and `d5`. **Black starts**.

Place a disc on an empty square that brackets at least one consecutive opposing
disc between the new disc and an existing disc of your color. Bracketing works
horizontally, vertically, and diagonally, in all eight straight directions.
All bracketed lines must flip. Do not skip gaps or friendly discs, wrap around
an edge, or create chain-reaction flips from the newly flipped discs.

Click a highlighted legal square. Alternatively, focus the board, use the arrow
keys to move the cursor, and press Enter or Space to place a disc. Black's opening
choices are `c4`, `d3`, `e6`, and `f5`.

If the current player has **no legal placement**, a forced pass is applied and
recorded automatically; that player cannot choose to pass when a placement is
available. No controller or language model is consulted for a forced pass.
The game ends when **neither player can move**, even if some squares remain
empty. Count only the discs on the board: more discs wins; equal counts draw.
Empty squares are not awarded to either player.

## Controls and animation

Choose each controller in configuration and press Begin. Minimax depth and MCTS
iteration count are configured independently for each side. Search time and
memory limits are shared defaults applied separately to each decision. Refresh
models to populate both Ollama selectors; each side can choose its own model
and extended-thinking option. Set `OLLAMA_URL` to change the default local server.

Each placement uses the configured animation delay. The newly placed disc blinks
for the first half; captured discs turn over during the second half. The game
state, disc counts, and player turn are committed atomically on arrival. Delay
zero commits immediately. A delay change affects subsequent placements.

Pause freezes automatic play and an active flip animation; completed worker
results wait until Resume. Restart resets the board with the same controller
configuration and a fresh seed. New configuration cancels pending work and
returns to setup. Old HTTP/search results cannot play on a reset board.

Undo is available in unfinished games with a Human player. It restores one action,
including a forced pass, and leaves play paused for review. Repeated Undo can
step back through the opponent's reply and your previous placement. Resume
continues play. Completed, recorded games remain visible until Restart or New
configuration; they are not automatically replaced.

If a search cannot complete any decision, or Ollama fails or returns an invalid
square, play stops visibly. Adjust search settings and Retry or restart. There
is no silent controller substitution. Saving failures also expose Retry and
retain the pending final report in the current session.

## Algorithms

- **Random:** uniformly selects among legal placements using a seeded generator.
- **Positional:** scores every legal child from the moving player's perspective.
  The evaluation combines a square table (valuable corners, risky corner-adjacent
  squares), legal-move mobility, frontier exposure, and disc difference. The score
  is `4 × square weights + 15 × mobility + 6 × frontier advantage + disc weight ×
  disc difference`; disc weight is 1 with more than 20 empties and 4 thereafter.
  Terminal wins/losses use a dominating score and the final disc margin. Equal
  candidate scores are randomized using the decision seed.
- **Minimax:** iterative-deepening negamax with alpha-beta pruning and corners-first
  ordering. Default depth **4**, range 1–10. Depth counts placements; forced
  passes do not consume a placement level. Each root candidate gets a full-window
  value. Only complete root iterations replace the saved evaluation, so an
  interrupted deeper level retains the last completed depth. This version uses
  bounded recursive storage without a transposition table.
- **MCTS:** UCT selection, one untried-action expansion per simulation, uniform
  random legal rollouts, and win/draw/loss backpropagation. Forced passes are
  explicit tree actions. Rewards are stored from the root player's perspective;
  opponent nodes reverse exploitation, while both players explore. Final move
  selection favors root visit count, then average reward, then a seeded tie-break.
  Default **1000 simulations**, range 1–1,000,000. MCTS values are mean outcomes
  scaled by 10,000; they are not positional or Minimax scores.
- **Ollama:** receives the rules, board, current color, disc counts, and legal
  coordinates, returning `{"square":"d3"}`. Local validation rejects malformed,
  occupied, or non-capturing choices. This is **pure model play**: no engine
  recommendations or tactical assistance are provided. Existing native/Cogito
  thinking policies and complete communication logging are supported.

Search defaults to **5 seconds and 64 MiB per decision**. Zero seconds disables
only the time limit. Minimax returns its last completed depth; MCTS returns only
statistics from completed simulations. If the MCTS tree reaches its memory cap,
it stops with an explicit resource-limit status rather than changing algorithms.
Cancellation discards the result. Accounted memory covers search allocation and
conservative stack/overhead allowances, not total process RSS. A limited result
with a completed decision can play; its limitation appears in the status and
history. Neither algorithm guarantees master-level play or a solved opening.

Ollama model discovery has a 1000-second timeout; turn requests have an
18000-second timeout, including thinking stages. HTTP/model failures remain
visible and cancellable. No running Ollama is needed for the automated tests.

## Build, test, and install

Dependencies match the existing apps: Meson, a C11 compiler, GLib, GTK 4,
libadwaita, libsoup 3, and JSON-GLib. From the repository root:

```sh
make GAME=reversi
make GAME=reversi run
make GAME=reversi test
make GAME=reversi test-ollama
xvfb-run -a dbus-run-session -- make GAME=reversi test-ui
python3 scripts/test-reversi-tournament.py
make GAME=reversi install
```

Engine tests build without graphical development libraries. Tests compare fast
bitboard moves against independent coordinate-based rules, check multidirectional
flips, forced passes, early endings, Minimax values, deterministic MCTS,
resource limits, mock HTTP protocols, and UI lifecycle. The interface supports
English, Portuguese, Spanish, and Italian. Desktop ID: `io.github.aigames.Reversi`.

## Reproducible tournaments

```sh
reversi/build/tournament --games=20 --seed=0 --first=positional --second=minimax --depth-second=4
reversi/build/tournament --games=20 --seed=0 --first=mcts --second=minimax --iterations-first=1000 --depth-second=4 --time-limit=5 --memory-limit=64
```

Runner choices are `random`, `positional`, `minimax`, and `mcts`. Use
`--depth-first`, `--depth-second`, `--iterations-first`, and `--iterations-second`
for independent settings. Each side has a separate seeded random stream.

CSV contains game, seed, controllers, depths, iteration settings, result,
placements, passes, final Black/White counts, full coordinate/pass sequence,
visited nodes, elapsed microseconds, each side's last search status and completed
level/iteration count, number of resource-limited decisions, and peak accounted
search bytes. Results are `black`, `white`, `draw`, or `ongoing` when no decision
could complete. Times vary; with budgets that complete, seeded choices repeat.
The independent Python replay validates every flip, forced pass, and outcome.

## Histories, artwork, and license

All finished games, including draws, append to `reversi-history.md` in the working
directory. Reports include controllers, seed, limits, each placement and flipped
squares, forced passes, candidate evaluations, Minimax/MCTS statistics, and final
counts. `make run` launches inside `reversi/`. Full Ollama exchanges go to
`reversi-ollama.log`; errors also go to `reversi-ollama-error.log`. See
[logging](../docs/logging.md).

The board is original Cairo artwork and the icon an original SVG. The application
is called Reversi and implements the cited standard Othello rules; it does not
redistribute federation illustrations or branding. Code and artwork are
GPL-3.0-or-later; see [COPYING](COPYING).
