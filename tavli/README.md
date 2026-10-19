# AI Games: Tavli

A standalone C11, GTK 4, and libadwaita application with **Portes, Plakoto,
Fevga, Backgammon, and Running**. Compare Human, Random, a race/blocking heuristic,
Expectiminimax, stochastic MCTS, and pure Ollama. Default: a five-point Portes
match, Human Player 1 versus Heuristic Player 2.

## Board and common controls

The view always belongs to **Player 1 (White)**; Player 2 is Red. Point numbers
are physical screen coordinates: bottom right is 1, bottom left 12, upper left
13, upper right 24. Numbers and movement arrows never flip when turns change.
Each player has 15 checkers. Reserve, bar, and borne-off trays are separate.

Select a variant, Single game / Fixed-variant match / Traditional Tavli cycle,
match target (1–25; default 5), and both controllers, then **Begin**. Opening
contests are rolled automatically. Human players use **Roll dice** on subsequent
turns; automated players roll after completing any applicable cube decision.

For a human checker turn, click a source and destination, or choose a legal
checker-move button. Click Reserve/Bar for entry and Off to bear off. The move
buttons show which die is used; use them when two dice permit the same exit.
Arrow keys navigate points; Enter/Space selects, Escape clears selection.

Each die is an individual movement with a legal intermediate landing. The engine
considers both dice orders and requires the greatest possible number of uses.
If only one of either unequal die can be used, use the higher one. A blocked
roll permits **Confirm turn** as a forced pass. Winning moves end immediately.
Opponent pieces may be passed over when the destination and intermediate
*die landings* are legal.

Moves are drafted: **Confirm turn** commits the complete checker turn.
**Undo step** reverses only draft moves and preserves the dice. It cannot undo
a completed turn or cube decision. A draft winning position still needs Confirm.
Checker movement blinks during the first half of the configured delay and slides
along the route during the second half. Captured pieces slide to the bar; pinned
pieces remain underneath the captor, marked P. State changes after each animation,
and the authoritative turn changes on confirmation. Automated turns confirm
when their animation sequence finishes. Delay zero commits immediately.

**Pause/Resume** freezes animations and holds completed background results.
**Retry** after a search/model failure keeps the position and roll. Search limits
can be adjusted before Retry. **Restart current game** restores that game's
initial board, scores, and dice-generator snapshot; it is available only before
the game finishes. **New match / configuration** returns to setup.

Completed games remain onscreen. Use **Next game** to continue a match; no new
game starts automatically. Saving errors retain the pending report and disable
Next game until Retry succeeds. Language changes preserve play and are saved in
`~/.config/ai-games/tavli.ini` (English, Portuguese, Spanish, Italian).

## Rules: Running

Running is the custom variant supplied for this project. It uses the following
confirmed rules, independently of Fevga:

1. Start with an **empty board**, 15 pieces per player in reserve.
2. White moves counterclockwise, entering through the upper right and exiting
   through the lower right. Red moves clockwise, entering through the upper left
   and exiting through the lower left **from Red's own seat**. In the fixed
   White screen view, Red enters at the lower right and exits at the upper right.
   White's route is `24,23,…,1`; Red's is `1,2,…,24`.
3. Reserve is progress 0, the 24 board points are progress 1–24, and exit is
   progress 25. Die d enters on your d-th route point. Thus White enters on 24
   with 1 or 19 with 6; Red enters on 1 with 1 or 6 with 6.
4. Each die can enter a new piece or advance an existing one; both may move
   the same piece. Entry and movement may be mixed freely.
5. Own pieces stack without limit. **Any opposing piece blocks landing**.
   No captures, backward movement, or restrictions on building primes. Spreading
   pieces to block opposing landings is a legitimate strategy.
6. **Doubles give two moves**, not four. Normal maximum-use/higher-die rules apply.
7. Remove any piece as soon as an exact die reaches progress 25, even while
   others remain in reserve. The final board point needs **1**. No overshoots;
   no requirement to gather all pieces in the final quadrant.
8. First to remove all 15 wins **one point**, regardless of the opponent's
   progress. Every game starts with a one-die-per-player contest, ties rerolled;
   the winner then rolls a fresh pair.

## Rules: Portes

Use the normal backgammon arrangement. Relative to each player's route from
start to finish, place 2 on index 0, 5 on index 11, 3 on index 16, and 5 on
index 18. Players move in opposite directions: White `24→1`, Red `1→24`.

A point with two or more opposing checkers blocks landing. Landing on an
opposing singleton **hits** it onto the bar. All of your bar pieces must re-enter
before you may move other checkers; die d enters at your d-th route point.
Doubles give **four** uses. Once all active pieces are in your final six points,
you may bear off: an exact die removes that point's piece; an overshoot may remove
only a piece furthest from the exit. A hit during bearing off postpones further
bearing off until re-entry and return home.

The opening-contest winner rolls a fresh pair. In subsequent match games the
previous winner starts with a fresh roll. A win scores 1 if the loser has borne
anything off, otherwise 2. There are no triple wins or doubling cube.
[Portes rules](https://bkgm.com/variants/Portes.html).

## Rules: Plakoto

All 15 pieces start at each player's first route point: White 24, Red 1.
Players move in opposite directions, with four uses on doubles and the same
home-board and bearing-off requirements as Portes.

Landing on an opposing singleton **pins it underneath your pieces** instead of
hitting it. The pinned piece cannot move or receive its own reinforcements until
your last pinning checker leaves. A point with two opposing checkers, or one
opposing checker pinning yours, blocks landing. There are no bar captures.

The final checker still at your starting point is your **mother**. If it is
pinned after the opponent has emptied their own starting point, you immediately
lose 2 points. While the opponent still has starting checkers, play continues:
their mother is still threatened. **Both mothers pinned is a draw**, scoring zero.
The engine checks this exception after each movement, including releases.

Ordinary wins score 1, or 2 if the loser has borne nothing off. Opening and
subsequent-game starting rules match Portes. No cube.
[Plakoto rules](https://bkgm.com/variants/Plakoto.html).

## Rules: Fevga

All 15 pieces start at diagonally opposite heads: White 24, Red 12. Both follow
the same counterclockwise direction: White `24,23,…,1`, Red
`12,11,…,1,24,23,…,13`.
Own stacks are allowed; any opposing checker blocks landing. There is no hitting.
Doubles give four uses. All active pieces must reach the final six points before
bearing off; normal exact/overshoot rules apply.

Only your first departing checker may move until it has **passed the opponent's
starting point** (beyond route index 12); it may unlock other head checkers within
the same turn. Never occupy all six points of your starting table simultaneously.

If all the opponent's remaining checkers gather on the single point immediately
behind your six-point prime, you must open a gap when the roll permits it.
The application's explicit interpretation is: retain gap-opening turn sequences
if any exist, then maximize dice use among those sequences. If the roll cannot
open a gap, use the normal maximum-use rule and retain the obligation for the
next turn. Evaluate the obligation at turn completion; prohibit filling the
starting table at every intermediate landing. There are no additional Narde
or Giul rules.

Wins score 1, or 2 if the loser has borne nothing off. Opening and subsequent-game
starting rules match Portes. No cube.
[Fevga rules](https://bkgm.com/variants/Fevga.html).

## Rules: Backgammon and doubling

Checker setup, opposing movement, hits, bar priority, doubles, and bearing off
match Portes. Every game starts with a one-die-per-player contest, rerolling ties;
the winner **uses those two dice for the first turn**, without a fresh roll.

A normal win scores 1. If the loser has borne nothing off, a gammon scores 2;
if the loser additionally has a piece on the bar or in the winner's home board,
a backgammon scores 3. Multiply by the accepted cube value.

The cube is enabled in fixed-variant Backgammon matches. At stake 1 it is centered;
either player may offer a double **before rolling**, after the opening turn.
The responder may **Take**, doubling the stake and owning the cube, or **Drop**,
ending the game and awarding the offerer the **old** stake. Only the owner may
redouble. The checker turn stays with the offerer after a take.

The first game after a player reaches one point short of the target is
**Crawford**: the cube is disabled for that game only. Afterwards it returns,
but a player cannot offer a cube when the existing stake already suffices to
win the match (**dead cube**). Individual Backgammon games are cubeless; there
is no money-play mode, Jacoby, beavers, automatic doubling, clock, or resignation
protocol. The 25-point target limit and dead-cube rule naturally bound cube values.
[Checker rules](https://bkgm.com/rules.html),
[USBGF match and cube rules](https://usbgf.org/tournament-rules/rules-for-in-person-play/).

## Match formats

- **Single game:** stop after one result and retain the final board.
- **Fixed-variant match:** repeat the selected variant until a player reaches
  or exceeds the target score. Running always scores 1 per victory.
- **Traditional Tavli cycle:** Portes → Plakoto → Fevga → Portes, repeating to
  the target. No cube. The previous winner starts the next game with a fresh roll.

A Plakoto draw repeats the same variant, awards nothing, and contests a new
opening. The rotation advances only after a decisive result. The application
does not offer a five-variant rotation.

## Automated players and search limits

**Random** samples uniformly among currently legal next-step choices until a
complete turn is formed; it does not sample complete turns uniformly. Cube
choices are uniform among legal actions.

**Race and blocking heuristic** compares all distinct resulting positions of
legal complete turns. It computes:

```text
H = pip advantage
    + 24 × borne-off advantage
    +  8 × controlled-point advantage
    +  6 × blocked-landing advantage
    + 12 × (opponent bar/pinned − own bar/pinned)
    −  6 × (own vulnerable singletons − opponent vulnerable singletons)
```

Pip advantage is opponent remaining distance minus own. Reserve and bar pieces
cost 25 pips; board distances follow each color's route. Controlled points are
those that block an opponent: one checker in Running/Fevga, two or a pinning
checker in the other variants. Blocked landings count opposing occupied sources
and six possible die faces without weighting by stack size. A pinned piece
is penalized; Running/Fevga singletons are not vulnerable blots. These are fixed
teaching weights, not trained strong-play estimates. Equal values use seeded ties.

Normalize H with `tanh(H/200)`. Nonterminal match value is projected score margin
plus the normalized current stake, divided by the target and clamped below a
match victory. Match wins/losses are exactly ±1. Individual-game estimates are
divided by 3 and terminal values are the actual signed game points / 3. There
are no trained gammon probabilities or match-equity tables. Heuristic cube
choices compare estimated take versus drop values; an offered cube assumes
the opponent chooses the less favorable result for the offerer.

**Expectiminimax** performs iterative deepening over whole checker turns and
cube decisions. Future dice use 21 unordered outcomes: each double has weight
1/36, each unequal pair 2/36. Both players optimize their own decisions; the
responder can differ from the active checker player. Checker turns consume
one depth; cube actions and chance transitions do not. Default depth 2, range
1–6. Only a fully completed root depth replaces the previous result. It is not
deterministic Minimax applied to a dice game, and it does not claim perfect play.

**Stochastic MCTS** uses explicit decision/chance nodes, UCT selection with
opponent exploitation reversed, expansion of legal complete turns, independently
sampled future dice, and backpropagated root-player values. Root expansion is
shuffled by the decision seed; equivalent resulting positions are deduplicated.
Rollouts sample legal complete sequences and cube actions, stop after 128 checker
turns, and then use the heuristic; truncation never declares a draw. Choose the
root action with most completed visits, then highest mean, then a seeded tie.
Default 1000 simulations, configurable 1–1,000,000; small budgets can leave actions
unvisited.

Default decision limits are **5 seconds and 64 MiB** of accounted search working
memory. Zero seconds means unlimited time. Rules enumeration, root candidates,
and tree allocations participate in accounting; this is not a process RSS cap.
No legal set is silently truncated. Limits retain completed decisions if any;
otherwise play stops for Retry. Cancellation discards the decision. Results
report status, completed work, nodes, elapsed time, peak accounted memory, and
truncated-rollout counts. Random/Heuristic also use bounded rule enumeration.

**Ollama** is pure: it receives variant rules, physical board, dice, match/cube
state, and IDs for legal next actions. No evaluation scores, future rolls,
suggested moves, opening book, or search results are supplied. For a checker
turn it selects successive prefixes inside one worker and returns a fully
validated complete turn before animation starts. Invalid replies, HTTP errors,
timeouts, and cancellation stop without fallback or reroll. **Refresh models**
explicitly and choose a model for each Ollama controller. Extended thinking
supports advertised native controls and the existing Cogito analysis/final-answer
protocol. One shared deadline covers every stage/prefix of a turn:
1,000,000 ms for discovery and 18,000,000 ms per decision. Local rule enumeration
for model prompts has a 64 MiB cap. `OLLAMA_URL` overrides the default service.

Real dice, each player's decisions, and simulations use separate seeded streams.
Changing search settings cannot consume real dice. Reproducible comparisons
need identical seeds, player order, code/library versions, and nonbinding wall
clock limits; different strategies can finish games at different times.

The [TODO](../TODO.md) retains Gerald Tesauro's TD-Gammon research reference:
neural networks, temporal-difference learning, and self-play are relevant future
comparisons. This application does not bundle or train a TD-Gammon model.

## Build, experiments, and verification

From the repository root:

```sh
make GAME=tavli run
make GAME=tavli test
make GAME=tavli test-ollama
make GAME=tavli test-ui
```

Engine-only builds use `-Dapp=false`, requiring GLib and libm without GTK or
network dependencies. Installation uses executable `tavli`, desktop ID
`io.github.aigames.Tavli`, and original Cairo/SVG artwork.

```sh
tavli/build/tournament --variant=running --games=20 --seed=0 \
  --first=heuristic --second=mcts --iterations-second=1000

tavli/build/tournament --variant=backgammon --session=fixed --target=5 \
  --games=10 --first=expectiminimax --depth-first=2 --second=heuristic

python3 scripts/test-tavli-tournament.py
```

Runner controllers are `random`, `heuristic`, `expectiminimax`, and `mcts`.
`--session=single|fixed|traditional` defaults to single; `--games` counts sessions.
Each player has independent depth/iteration flags. Shared flags include
`--time-limit`, `--memory-limit`, and `--turn-limit` (default 10,000 per session).
Resource or turn exhaustion exports **incomplete**, never an invented winner.
CSV contains scores, seed, controllers, game/turn counts, search work/status,
memory, elapsed time, and a replayable event sequence:

- `B:Variant`: game start / next game.
- `O:a-b`: opening contest, including ties; `R:a-b`: ordinary roll.
- `D`, `T`, `X`: double offer, take, drop.
- `M:source>destination/die+…`: complete checker turn; `M:pass`: forced pass.
  Points are numbered 1–24; tray names are `reserve`, `bar`, and `off`.

Completed GUI games append independent reports to `tavli-history.md` in the
working directory. Reports include the seed, variant, score, rules context,
controllers/settings, actual rolls, moves, cube actions, and candidate diagnostics.
`tavli-ollama.log` and `tavli-ollama-error.log` contain full communication;
[logging guidance](../docs/logging.md) explains overrides and privacy.

Tests cover rules and conservation, randomized complete-turn/prefix agreement,
variant-specific exceptions, match/cube transitions, exhaustive 36-roll reference
search, resource/cancellation behavior, mocked Ollama, and GUI draft/animation/
undo/retry/lifecycle behavior. The independent Python replay engine validates
exported dice usage, moves, captures, pins, scores, cube decisions, and rotation.
