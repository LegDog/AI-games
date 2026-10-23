# AI Games: Nine Men’s Morris

Independent C11 / GTK 4 / libadwaita application, GPL-3.0-or-later.

```sh
make GAME=nine-mens-morris run
make GAME=nine-mens-morris test        # GLib-only engine, search, translations
make GAME=nine-mens-morris test-ollama # mock HTTP; no installed model needed
make GAME=nine-mens-morris test-ui     # requires a display
```

## Rules and conventions

The 24-point board has three nested squares joined at their side midpoints,
with 16 mills and 32 edges. Player 1 starts. Each player places nine reserve
pieces, alternating turns. After placement, move one piece along an edge to an
empty neighbor. With exactly three pieces and no reserves on either side, a
player may **fly** to any empty point.

Closing a mill (three pieces along a marked straight line) requires removing
one opposing piece outside mills. If every opposing piece belongs to a mill,
any one may be removed. Closing two mills in one turn still earns **one** capture.
Opening and later re-forming a mill earns another capture. Captured pieces never
return. Lose when reduced to fewer than three pieces including reserves, or
when unable to move. A mill with no opposing piece present requires no capture.

Reference: [Morris rules manual](https://www.nine-mens-morris.net/manual.html).
Flying and draw rules differ between implementations. This application enables
flying and fixes these conventions:

- Three occurrences of the same position, reserves and side to move draw.
- Fifty individual player movement turns without a capture draw. Placement and
  capture reset this counter and repetition history, since neither is reversible.
- Material or blocking wins take priority over a draw on the same turn.

These are application rules, not a claim that all traditional variants agree.
No diagonal, Lasker, no-flying, or multiple-capture variants are included.

## Controls and interface

Click an empty point to place. During movement, click your piece and then a
highlighted destination; click the selected piece again to deselect. A mill
previews the proposed placement/movement and highlights legal captures. Choose
one to commit the **whole turn**, or click elsewhere to cancel the draft.
Reserve and on-board counts, current phase, quiet-turn count and terminal result
remain visible. Placement/movement and shrinking captures animate for 300 ms;
input waits until the animation finishes.

Each side independently selects Human, Random, Heuristic, Minimax, Alpha-beta,
or pure Ollama, with depth 1–6 (default 4). Player 1 defaults to Human and
player 2 to Heuristic. Pause cancels analysis; Resume continues the position.
New game resets the board. Controller, depth, model and reasoning-option changes
cancel pending work without resetting the board. Automatic decisions begin
after a 400 ms inter-turn delay. Search and HTTP requests run on copied state;
canceled or superseded results cannot change the live board.

English, Portuguese, Spanish and Italian switch immediately without resetting
play. Preferences are stored in the app's own `ai-games/nine-mens-morris.ini`
under the user configuration directory. Status, errors and rules use fixed
scrolling panels. The board has its own contrasting background in either theme.

## Algorithms

A state holds two 24-bit occupancy masks, reserves, side to move, quiet counter
and exact repetition keys. Legal actions contain source (or placement), destination
and optional capture. All controllers and exports operate on complete turns.
The rules engine contains no RNG.

Random samples legal complete turns uniformly, so destinations with more capture
choices have greater aggregate probability. Heuristic performs one-ply search.
Plain Minimax and alpha-beta use the same evaluation, move order, full root
windows and seeded reservoir tie-breaking. Both deepen iteratively within a
50,000-successor-node budget; only completed root iterations replace the retained
choice. If none completes, the first legal action is retained. Cancellation is
checked at each visited successor. Depth counts complete player turns.

Evaluation is the player's score minus the opponent's:

| Component | Weight |
| --- | ---: |
| Pieces on board plus reserves | 100 |
| Closed mills | 24 |
| Unblocked two-piece mill threats | 18 |
| Legal placement/movement destinations (before capture choices) | 2 |
| Pieces with no empty adjacent neighbor | −8 |

Mobility accounts for placement and flying. Blocking pressure still measures
local adjacency during flying. Wins/losses score ±100000; draws score zero.
These bounded controllers do not claim perfect play. The GUI decision seed is
`actions * 2654435761 + side_to_move` modulo 2³².

## Ollama and histories

Enter an installed model name for each Ollama side. The default service is
`http://127.0.0.1:11434`; `OLLAMA_URL` overrides it. Prompts include the board,
reserves, mills, neighbors, repetition keys, quiet counter and all legal complete
turns. JSON schema restricts output to those turns, and the engine validates the
answer again. There is no tactical assistance or local replacement of an invalid
answer: errors pause play. Extended thinking and the optional Propose/Review
Reasoning harness use the shared clients and conversation window.

Notation uses conventional coordinates: `a7` places, `a7-a4` moves, and
`g4-g7xc5` moves and captures c5. Placement captures look like `g7xb6`.
Wins **and draws** append timestamped complete-turn histories to
`nine-mens-morris-history.md`, with controller/model settings, search depths,
visited nodes and termination reason. Reasons: 1 material, 2 blocked,
3 repetition, 4 quiet limit. Communication and errors go to
`nine-mens-morris-ollama.log` and `nine-mens-morris-ollama-error.log`; shared
log-path overrides and privacy guidance apply. Paths are relative to the working
directory; root `make ... run` launches inside this game's directory.

## Repeatable experiments

```sh
nine-mens-morris/build/tournament --games 20 --seed 2026 --depth 3 --budget 50000
nine-mens-morris/build/tournament --games 20 --seed 2026 --depth 3 --swap
nine-mens-morris/build/tournament --games 20 --seed 2026 --depth 3 --plain
python3 scripts/test-morris-tournament.py
```

The runner compares search with Random. `--swap` exchanges their seats and
`--plain` chooses plain Minimax. CSV records seed, requested depth, node budget,
algorithm, seats, winner (1/2 or 3 draw), termination reason, action count and the
complete move sequence. Search and random share a seeded controller stream;
node limits can cause the algorithms to finish different depths and consume
randomness differently. Compare both seats, record code revision, and avoid
interpreting a small sample as a general strength claim.

`ollama-harness-eval --list` lists three public fixtures: opening, a mill/capture
opportunity against three opponents, and a flying position. The shared evaluation
driver compares direct and Propose/Review model decisions; its immediate-success
metric means a win on that turn. See [the harness guide](../docs/ollama-harness.md).

Tests cover topology, placement, protected captures, flying, re-formed/double
mills, reserves, terminal priorities, draws, notation, search agreement and
budgets, mock protocol/harness failures, and GUI lifecycle behavior. The Python
replay independently derives edges/mills from board coordinates and checks every
exported action, capture and result.
