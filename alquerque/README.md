# AI Games: Alquerque

Independent C11 / GTK 4 / libadwaita application, GPL-3.0-or-later.

```sh
make GAME=alquerque run
make GAME=alquerque test        # GLib-only engine, search and translations
make GAME=alquerque test-ollama # local mock HTTP; no installed model needed
make GAME=alquerque test-ui     # requires a display
```

## Rules and conventions

The board has 25 intersections, labelled `a1` through `e5`, with 40 orthogonal
and 16 diagonal edges. Diagonals exist at points whose zero-based row plus
column is even. Light (player 1) starts with rows 1–2 and `d3/e3`; Dark has
rows 4–5 and `a3/b3`. Only `c3` is empty. Light starts.

Select one of two movement variants before playing:

- **Free movement** (default): move one marked edge in any direction.
- **Directional movement**: quiet moves go sideways or forward (Light toward
  row 5, Dark toward row 1). On the opponent’s home row, a piece can only capture;
  after leaving that row through a capture, it can move normally again.

If any piece can capture, quiet moves are forbidden. Jump over one adjacent
enemy along two straight marked edges to an empty point. Remove the victim
immediately. Captures may go backward in both variants. Continue with the same
piece until no further capture exists; any complete chain may be chosen, even
when another chain captures more pieces. There is no promotion, huffing,
per-piece move-reversal restriction, or pass.

Capture all opposing pieces or leave the next player without a legal turn to
win. Otherwise, three occurrences of the same board and side to move draw, as do
50 individual player turns without capture. Capture resets both quiet count
and repetition history. A material or blocking win takes priority over a draw.
These are fixed application conventions, not a claim of one universal historical
ruleset. The [reference rules](https://omerkel.github.io/Alquerque/doc/rules.html)
describe directional play and compulsory complete chains; this app additionally
offers free movement and explicit draw adjudication, and permits quiet reversals.

## Controls and interface

Click your piece and a highlighted destination. During a multi-jump turn, choose
each successive landing; **Cancel chain** discards the draft. Invalid landings
leave a pending chain intact; clicking outside the points also cancels it.
Drafts do not modify committed state. The last landing commits the entire turn,
then each jump and shrinking capture animates for 300 ms. Input waits until
playback completes.

Each side independently selects Human, Random, Heuristic, Minimax, Alpha-beta,
or pure Ollama. Defaults are Human versus Heuristic, search depth 4 (range 1–6),
and a 400 ms automatic inter-turn delay. Pause cancels analysis; Resume continues.
New game resets the board. Switching variant starts a fresh game and cancels
pending analysis or animation. Controller, model, depth, and reasoning changes
discard drafts and pending work while preserving the committed board. Completed
results remain visible until New game.

English, Portuguese, Spanish and Italian switch immediately without resetting
play. Language preferences use `ai-games/alquerque.ini` under the user configuration
directory. Fixed scrolling message/rules panels preserve board size; the board
has a contrasting background in either theme. Search and HTTP workers use copied
state, cancellation tokens, and generation checks to discard stale decisions.

## Engine and algorithms

Two 25-bit masks hold occupancy. State copies include the variant, side to move,
quiet count and exact repetition history. A move contains its source and up to
12 ordered landing points, plus capture/quiet identity. Dynamic move collections
enumerate all complete chains without a fixed action-count ceiling. Notation
uses `c2-c3` for a quiet move and `a1xc3xe5` for a chain of landing points.

The rules engine contains no RNG. Random samples complete legal turns uniformly,
so a first jump with more continuations receives more aggregate probability.
Heuristic searches one ply. Plain Minimax and alpha-beta use the same evaluation,
move order, full root windows and seeded reservoir tie-breaking. Iterative
deepening retains only fully completed depths within a 50,000-successor-node
budget, using the first legal turn if no iteration completes. Depth counts
complete turns; cancellation is checked at visited successors and during chain
enumeration. These are bounded controllers, not claims of perfect play.

Evaluation is the player’s score minus the opponent’s:

| Component | Weight |
| --- | ---: |
| Remaining pieces | 100 |
| Legal first-step destinations, summed per piece | 4 |
| Marked edges incident to occupied points | 2 |

Mobility counts capture landings when any capture exists for that side, otherwise
quiet destinations under its movement variant. Multiple chains sharing a first
jump count once. Wins/losses score ±100000; draws score zero. The GUI seed is
`actions * 2654435761 + side_to_move` modulo 2³².

## Ollama and histories

Enter an installed model name for each Ollama side. The default URL is
`http://127.0.0.1:11434`; `OLLAMA_URL` overrides it. Prompts include the variant,
board topology, turn, draw context and all complete legal choices. The JSON
schema restricts the `move` string to those choices, and the engine validates
it again. Invalid or incomplete answers and transport failures pause play,
without a local replacement. Extended thinking, optional Propose/Review harness,
and the shared conversation window are supported. See the
[harness guide](../docs/ollama-harness.md).

Wins and draws append timestamped turns to `alquerque-history.md`, including
variant, controller/model/depth settings, thinking/harness settings, completed
search depths, nodes, and termination reason. Reasons: 1 material, 2 blocked,
3 repetition, 4 quiet limit. Communication and errors use `alquerque-ollama.log`
and `alquerque-ollama-error.log`; shared log-path overrides and
[privacy guidance](../docs/logging.md) apply. Paths are relative to the working
directory; root `make ... run` launches inside this game’s directory.

## Repeatable experiments and validation

```sh
alquerque/build/tournament --variant=free --games 20 --seed 2026 --depth 3 --budget 50000
alquerque/build/tournament --variant=directional --games 20 --seed 2026 --depth 3 --swap
alquerque/build/tournament --variant=free --games 20 --seed 2026 --depth 3 --plain
python3 scripts/test-alquerque-tournament.py
alquerque/build/ollama-harness-eval --list
```

The runner compares search against Random. CSV records variant, seed, requested
depth, budget, algorithm, seats, winner, termination reason, turn count and full
move sequence. Compare both seats for each variant and retain the code revision.
Search and Random share a seeded stream; budget-limited algorithms can complete
different depths and consume different randomness. Small samples do not establish
general playing strength.

Engine tests independently derive coordinate rules across 500 positions, and
cover branching chains, forced continuation, immediate removal, directional
restrictions, terminal priorities, draws, notation, shallow exhaustive search,
budgets, completed-depth retention and cancellation. The Python validator
independently replays 48 games across both variants, seats and search algorithms,
and checks repeated seeds. Mock HTTP and GUI tests exercise complete-chain
validation, harness failures, draft cancellation, sequential animation, language
changes, panel stability, transcript delivery and stale-worker rejection.
