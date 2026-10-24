# AI Games: Checkers

Independent C11 / GTK 4 / libadwaita application, GPL-3.0-or-later.

```sh
make GAME=checkers run
make GAME=checkers test         # GLib-only engine, search and translations
make GAME=checkers test-ollama  # mock HTTP; no installed model needed
make GAME=checkers test-ui      # requires a display
```

## Rules and conventions

Choose **English draughts** (American checkers, default) or **Brazilian draughts**.
Both use an 8×8 board with 32 playable dark squares and 12 men per side. Square
`a1` is dark, Light/player 1 starts on ranks 1–3 and moves toward rank 8;
Dark/player 2 starts on ranks 6–8 and moves toward rank 1. Dark moves first in
English; Light moves first in Brazilian. Board labels use algebraic coordinates
rather than federation numeric notation.

| Rule | English | Brazilian |
| --- | --- | --- |
| Quiet man movement | One diagonal square forward | One diagonal square forward |
| Man captures | Forward only | Forward or backward |
| King movement | One diagonal square | Any distance along an empty diagonal |
| King capture | Jump one adjacent enemy to the immediately following square | Cross empty squares, jump one enemy, and land on any empty square beyond it |
| Capture choice | Any complete chain | Globally most captured pieces; no king-value tie-break |
| Promotion | Reaching the far rank ends the turn | Only if the completed turn ends on the far rank; continue captures as a man |
| Captured victims | Removed at each hop | Remain blocking until turn end; cannot be crossed again |

Capturing is compulsory whenever any piece can capture. Continue with the same
piece until the chain is complete. Brazilian chains can revisit empty squares,
including the vacated source, but cannot jump the same victim twice. A man that
passes through the promotion rank and ends elsewhere stays a man. Flying-king
captures stop at the first occupied square, cannot cross friendly pieces, and
jump exactly one opponent per hop. No huffing or passing.

Movement references: [WCDF English rules](https://wcdf.net/rules/rules_of_checkers_english.pdf)
and [draughts-64 rules, especially Brazilian clauses 4.16](https://fmjd64.org/rules-of-the-game/).
The app omits clocks, touch-move enforcement, opening ballots and referee procedures.

Capture all opponents or leave the next player without a legal turn to win.
The app automatically draws on the third identical board, kings and side to move,
or after **80 individual turns in English / 40 in Brazilian** without a capture
or any man move (40/20 moves per side). Either event resets the quiet counter and
repetition window. A win takes priority over a draw. These are documented
simplified application adjudications: special material-dependent endgame draws,
draw claims and negotiated draws are not implemented.

## Controls and interface

Select a piece with a highlighted legal turn, then a highlighted landing.
During capture chains choose successive landings; **Cancel chain** discards the
draft. Invalid landings preserve a pending chain; clicking outside the playable
squares cancels it. Drafts never mutate the committed game. Brazilian choices
only include prefixes of globally maximum-length chains.

Each complete turn commits atomically and then animates each hop for 300 ms.
Brazilian captured pieces remain crossed out until the chain finishes. Kings
have a gold ring/cross, including during movement. Promotion appears after the
last step. Input waits for playback to finish.

Each side independently selects Human, Random, Heuristic, Minimax, Alpha-beta,
or pure Ollama. Defaults are Human Light versus Heuristic Dark, depth 4 (range
1–6), and a 400 ms automatic inter-turn delay. Thus the computer opens the default
English game. Pause cancels analysis; Resume continues. New game resets play;
changing the variant also starts a fresh game. Controller, model, depth and
reasoning changes cancel drafts and pending work without changing the committed
position. Completed results remain visible until New game.

English, Portuguese, Spanish and Italian switch immediately without resetting
play. Language preferences use `ai-games/checkers.ini` under the user configuration
directory. Fixed scrolling message/rules panels preserve board size. Search and
HTTP workers use copied game state, cancellation and generation checks.

## Engine and algorithms

Two 32-bit occupancy masks and a king mask represent the committed board. State
copies include variant, side, quiet counter and exact repetition records.
Brazilian preview/enumeration state also tracks captured victims still blocking
the board. Dynamic legal-turn lists contain source, ordered landings and victim
squares, with up to 12 captures per turn and no fixed action-count ceiling.
Notation uses `c3-d4` for a quiet move and `a1xc3xe5` for a complete capture chain.

The engine contains no RNG. Random samples complete legal turns uniformly.
Heuristic evaluates one ply. Minimax and alpha-beta share evaluation, move order,
full root windows and seeded reservoir tie-breaking. Iterative deepening retains
only fully completed depths under a 50,000-successor-node budget. The first legal
turn is retained if no iteration completes. Depth counts complete turns.
Cancellation is checked during search and capture enumeration. These are bounded
controllers, not claims of perfect play.

Evaluation is the player's score minus the opponent's:

| Component | Weight |
| --- | ---: |
| Man | 100 |
| King | 300 |
| Distinct legal first-step destinations, summed per source | 4 |
| Each man's ranks advanced from its own home rank | 2 |

Mobility respects capture obligations and Brazilian maximum capture filtering;
chains sharing a source and first landing count once. Wins/losses score ±100000;
draws score zero. The GUI seeds decisions with `actions * 2654435761 + side_to_move`
modulo 2³².

## Ollama and histories

Enter an installed model name for each Ollama side. The default URL is
`http://127.0.0.1:11434`; `OLLAMA_URL` overrides it. Prompts include variant rules,
coordinates, pieces/kings, quiet count, repetition context and all complete legal
choices. JSON schema and engine validation require an allowed complete turn.
Incomplete, illegal or malformed answers and transport errors pause play without
a local replacement. Extended thinking, the optional Propose/Review reasoning
harness and the shared conversation window are supported. See the
[harness guide](../docs/ollama-harness.md).

Wins and draws append timestamped turns to `checkers-history.md`, including
variant, controllers/models/depths, reasoning settings, completed search depths,
nodes and termination reason (1 material, 2 blocked, 3 repetition, 4 quiet limit).
Communication and errors use `checkers-ollama.log` and `checkers-ollama-error.log`.
Shared log-path overrides and [privacy guidance](../docs/logging.md) apply.
Paths are relative to the working directory; root `make ... run` launches inside
this application's directory.

## Repeatable experiments and validation

```sh
checkers/build/tournament --variant=english --games=20 --seed=2026 --depth=3 --budget=50000
checkers/build/tournament --variant=brazilian --games=20 --seed=2026 --depth=3 --swap
checkers/build/tournament --variant=brazilian --games=20 --seed=2026 --depth=3 --plain
python3 scripts/test-checkers-tournament.py
checkers/build/ollama-harness-eval --list
```

The runner compares Search against Random. CSV records variant, seed, requested
depth, budget, algorithm, seats, winner, termination, turn count and full moves.
Compare both seats and variants, and retain the revision. Search and Random share
a seeded stream; budget-limited algorithms may reach different depths and consume
different randomness. Small samples do not establish general playing strength.

Engine tests compare legal turns with an independent coordinate-array reference
across 1,000 generated positions. Regression fixtures cover capture obligations,
maximum chains, king rays, victim blocking, promotion and draw thresholds.
Shallow search is compared with exhaustive results. The Python validator
independently replays 48 seeded games across variants, seats and algorithms and
checks repeated seeds. Mock HTTP/harness and GUI tests cover complete-turn
validation, failures, drafts, sequential animation, promotion, language switching,
panel stability, transcript delivery and rejection of stale workers.
