# AI Games: Fox and Geese

A standalone GTK 4/libadwaita application for an asymmetric hunt game on a
33-point cross with orthogonal lines and diagonals at alternating intersections.
GPL-3.0-or-later. Build from the repository root with `make GAME=fox-and-geese`,
then launch with `make GAME=fox-and-geese run`.

## Rules implemented

Historical Fox and Geese / Halatafl rules differ. These are explicit playable
rulesets, not a claim to reconstruct a unique medieval game:

- **Halatafl (13 geese)**: geese occupy c1–e1, c2–e2 and a3–g3.
  All pieces may move one marked edge in any direction.
- **17 geese (forward)**: add a4, g4, a5 and g5. Geese move sideways or
  toward increasing row numbers, including marked forward diagonals.
- Player 1 controls the single fox, initially d4, and moves first.
  Player 2 controls all geese. Empty corner regions are outside the board.
- Only the fox captures: jump an adjacent goose to the empty point immediately
  beyond it along two straight marked edges. Remove the goose immediately.
  Captures are compulsory, and the fox must continue until no jump remains.
  Any complete chain is allowed; there is no maximum-capture requirement.
- Geese win by immobilizing the fox. The fox wins when fewer than four geese
  remain. A goose side with no legal move is also awarded the win as an explicit
  adjudication convention. Material victory has priority over blocking and draws.
- Threefold repetition, including side to move, or 50 individual turns without
  a capture ends in a draw. These automatic draw limits are application conventions.

References: [Masters Traditional Games](https://www.mastersofgames.com/rules/fox-geese-rules.htm),
[Hala-Tafl](https://www.vikingage.com/HTML/Hala-Tafl.html), and
[Historic Arkansas Museum teaching rules](https://www.arkansasheritage.com/docs/default-source/historic-arkansas-museum-documents/family-learning-resources/fox-and-geese18e7c9cc-62de-4e86-87ab-9d4004fffe88.pdf).
The four-geese threshold is a chosen practical ending, rather than a proof that
all positions with four geese can trap the fox. No huffing rule is implemented.

## Controls and algorithms

Click a piece and a highlighted destination. For multiple jumps, select every
landing; **Cancel chain** discards the preview. The committed game changes only
on a complete turn. Animations show each jump and disappearing victim. Pause,
restart, configuration changes, language selection and the Ollama transcript
use the same controls as the other applications.

Each player independently selects Human, Random, Heuristic, plain Minimax,
alpha-beta, or Ollama. Search runs in a background worker with cancellation;
stale results are discarded. Depth 1–6 counts complete turns. The UI allows
50,000 successor nodes per decision. Iterative deepening retains only complete
iterations; if none completes, the first legal turn is retained. Both search
algorithms use identical move ordering and full root windows for tie comparison.
The heuristic is fox freedom (12 per legal destination), minus goose material
(100 each) and goose mobility (2 per destination), reversed for the geese.
Terminal wins score ±100,000 and draws zero. Random ties use a seeded generator.

Ollama is pure model play with a validated list of complete legal turns and no
local tactical assistance. Optional extended thinking and the reasoning harness
are available. Network errors remain visible; they do not silently substitute a
local controller. Logs and saved completed games use the `fox-and-geese` prefix;
see the repository logging and reasoning-harness guides.

## Engine and experiments

Two 64-bit masks store the 33 cells, indexed by row and then column. Exact
repetition keys pack the goose mask, fox index, actor and variant; search copies
include repetition history. Legal turns are dynamic lists, including full jump
chains. Coordinates such as `d4-d5` and `c1xc3xe3` serialize turns.

```sh
make GAME=fox-and-geese test MESON_ARGS=-Dwerror=true
make GAME=fox-and-geese test-ollama MESON_ARGS=-Dwerror=true
make GAME=fox-and-geese test-ui MESON_ARGS=-Dwerror=true # needs a display
fox-and-geese/build/tournament --variant halatafl --games 10 --seed 19 --depth 3 --budget 50000
fox-and-geese/build/tournament --variant seventeen --swap --plain
python3 scripts/test-fox-geese-tournament.py
```

The runner compares search against Random, exports seeds, settings, result,
termination reason and every turn as CSV, and supports exchanging controller
seats. The Python validator independently replays both variants, both seats and
both search algorithms, including capture obligations and draw adjudication.
Results describe the chosen rules and budgets, not perfect play or historical
balance. Engine tests run with `-Dapp=false` without graphical/network libraries.
