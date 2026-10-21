# AI Games: The Royal Game of Ur

Independent C11 / GTK 4 / libadwaita application, GPL-3.0-or-later.

```sh
make GAME=royal-game-of-ur run    # from the repository root
make -C royal-game-of-ur test   # GLib-only engine and translation tests
make -C royal-game-of-ur test-ollama
make -C royal-game-of-ur test-ui # requires a display
```

This implements the common modern seven-piece, fourteen-square race described
in [RoyalUr.net's Finkel rules](https://royalur.net/rules), rather than claiming
to reproduce a uniquely known ancient ruleset. The
[British Museum board](https://www.britishmuseum.org/collection/object/W_1928-1009-378)
has twenty playing squares and five rosettes.

Each player starts with seven pieces in reserve. Four independent fair binary
dice produce sums 0–4 with weights 1,4,6,4,1 out of 16. Player 1 starts.
Move one piece by the full roll, entering from reserve as source 0. No stacking.
A zero or blocked roll must pass. Route squares 1–4 and 13–14 are private;
5–12 are shared. Landing on an enemy on the shared track returns it to reserve,
except square 8, which is safe and blocks enemy landing. Rosettes 4,8,14 grant
another roll. Exiting at 15 requires an exact roll; first to finish seven wins.

The board displays player 1's private track above the shared track and player
2's below. Both enter at column 4, travel left to square 4, then right along
shared squares 5–12, and turn left through private squares 13–14 before exit.
Numbers on the board are route positions, shared by both players.

Choose independent Human, Random, Heuristic, Expectiminimax, or Ollama
controllers. Click **Roll dice**, then a source piece or **Reserve** to enter.
**Pass** becomes available only for forced passes. Moves animate for 300 ms;
no input or next decision can overlap an animation. **Pause** cancels an active
worker; **Resume** retains the actual roll. **New game** resets the position.
Controller changes cancel workers and preserve the board and dice. Completed
games, including rolls and source positions, are appended to
`royal-game-of-ur-history.md` in the working directory. Abandoned games are not saved.

Heuristic chooses the best immediate successor. Evaluation is own minus enemy:
30 per finished piece, route position per active piece, +8 on the safe central
rosette and +2 on private rosettes. A win scores ±10000. Expectiminimax explores
three decisions, including bonus turns, with exact chance probabilities and
opponent minimization. It is bounded by depth, has no learned knowledge, and
is not perfect play. Equal scores use seeded reservoir tie-breaking. Search and
Random never consume the real dice generator. GTK workers receive copied state;
canceled generations cannot apply results.

Enter an installed local Ollama model name for each Ollama side. The client
uses `http://127.0.0.1:11434`, legal source strings (or forced `pass`), strict
JSON validation, and no tactical assistance or fallback moves. Errors pause play.
Optional Extended thinking and Reasoning harness default off. The shared
Ollama messages window shows actual requests and responses. Logs use
`royal-game-of-ur-ollama.log` and `royal-game-of-ur-ollama-error.log`, with the
repository's usual environment overrides and total request deadline.

The engine stores each side's occupancy at positions 1–14, reserve and finished
counts, active player, and pending roll (-1 before rolling). Dice and moves are
separate validated actions; source -1 means a forced pass. Legal moves are
explicit lists, not bit masks. Engine tests check capture, safe squares, exact
exit, bonus turns, zero, illegal actions, terminal states and conservation across
100 complete randomized games. Translation helpers support English, Portuguese,
Spanish and Italian with a separate saved language preference.

```sh
royal-game-of-ur/build/tournament --seed 2718 --games 20 --depth 1 > /tmp/ur.csv
python3 scripts/test-ur-tournament.py
```

The runner compares player 1 Heuristic/depth 1–3 Expectiminimax with player 2
Random. CSV retains seed, depth, winner, action count and `player:roll:source`
sequences. `--max-actions` bounds games; winner 0 means unfinished, never a draw.
The independent Python replay checks legality, captures, turns and winners.
Use `--swap` to exchange controller seats. Comparisons require both player orders
and larger samples before strength claims.
