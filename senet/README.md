# AI Games: Senet

Independent C11 / GTK 4 / libadwaita application, GPL-3.0-or-later.

```sh
make GAME=senet run                  # from the repository root
make -C senet test                   # GLib-only engine and translation tests
make -C senet test-ollama             # mock service, no models required
make -C senet test-ui                # needs a display
```

## Ruleset

Senet's complete ancient rules are unknown. This app implements a modern teaching
reconstruction based on the [Milwaukee Public Museum guide](https://www.mpm.edu/sites/default/files/images/content/education/programs/curiosity/may/senet_v3.pdf).
It is a named, fixed ruleset; other reconstructions differ.

Five pieces per player alternate on houses 1–10; player 1 starts on odd houses.
The 3×10 track winds right, left, then right. Four fair binary sticks give the
number of light faces, with all-dark counting as five. Move one piece forward
by the full throw, jumping freely. Throws 1,4,5 grant another throw after a move;
a blocked throw must pass and ends the turn. Own pieces cannot share a house.
Landing on an opponent exchanges their position with yours, unless that opponent
has a friendly neighbor immediately before or after it on the track.

Every piece must stop at 26 before advancing. Landing on 27 sends it back to 15.
From 28,29,30 the only move is to exit with exactly 3,2,1 respectively. First to
finish all five wins.

Implementation choices completing the guide: if 15 is occupied, use the nearest
empty house below it; adjacency follows track numbering across bends; exit from
30 requires one; no direct exit from 26. No additional safe houses, backward
moves, triple-piece barriers, or penalties for failing an exit throw are used.

## Interface and controllers

Select Human, Random, Heuristic, Expectiminimax, or pure Ollama independently for
each player. Human players click **Throw sticks**, then a source piece. **Pass**
is enabled only when no piece can move. The display shows the throw's total.
House numbers, blue Water, and symbols identify special squares. Exchanges move
both pieces, and water animations visit 27 before returning. The engine commits
the complete move atomically; human input and subsequent AI moves wait for the
300 ms animation to finish.

**Pause** cancels work; **Resume** keeps the already-thrown result. Changing a
controller also cancels work and retains the position. **New game** resets the
board and transcript. Background jobs receive copied state, and generation
checks discard canceled or superseded results.

Heuristic chooses the best immediate successor using own minus opponent score:
50 per finished piece, house number for each remaining piece, +5 for houses 26
and above, and +3 for a protected piece. Wins score ±10000. Expectiminimax explores
three decisions with exact throw probabilities 4:6:4:1:1 out of 16 for values
1,2,3,4,5. Bonus turns preserve the active player at decision nodes. Search is
depth bounded, has no learned knowledge, and is not perfect play. Tie-breaking
uses reservoir sampling. Actual sticks and controller randomness use separate
streams.

Enter an installed local model name for each Ollama player. The client uses
`http://127.0.0.1:11434`, supplies the rules, board, throw and legal sources,
and accepts only a legal JSON `move` string. No tactical assistance or fallback
move is applied. Errors pause play. Extended thinking and Reasoning harness are
optional and default off. The shared Ollama messages window receives actual
requests, thinking and responses; canceled generations cannot update the game.

English, Portuguese, Spanish and Italian switch live and use a separate
`~/.config/ai-games/senet.ini` preference. Board-adjacent status and rules panels
scroll without changing board allocation.

Completed games append timestamps, controller settings, throw/source sequences,
and winner to `senet-history.md` in the working directory. Abandoned games are
not saved. Logs use `senet-ollama.log` and `senet-ollama-error.log`; the usual
`OLLAMA_LOG_FILE` and `OLLAMA_ERROR_LOG_FILE` overrides apply. Logs contain full
model communication; see [logging guidance](../docs/logging.md).

## Engine and experiments

`Game` contains owner values 0,1,2 indexed by house 1–30, finished counts, active
player, pending throw (-1 before throwing), and action count. Source -1 is an
explicit forced pass. Legal choices are explicit lists of at most five sources.
Throwing and moving are separate validated actions. Water always resolves to a
vacant house; no valid completed position contains a piece on 27.

```sh
senet/build/tournament --seed 31415 --games 20 --depth 1 > /tmp/senet.csv
senet/build/tournament --seed 31415 --games 20 --depth 3 --swap
python3 scripts/test-senet-tournament.py
```

Player 1 uses Heuristic/depth 1 or Expectiminimax/depth 2–3 against Random.
`--swap` exchanges seats. CSV retains seed, depth, seat order, winner, actions,
and every `player:throw:source`. `--max-actions` bounds sessions; winner 0 means
unfinished, never a draw. Compare both seats and report exact rules, seeds and
depths before making strength claims.

Tests cover invalid moves, neighbor protection, exchanges, Happiness, Water,
exact exits, bonus turns, passes, and conservation across 100 randomized games.
An independent reference enumerates all sixteen stick outcomes and checks search
scores. A separate Python board implementation replays 40 seeded exported games
in both seat orders and checks input limits and deterministic output. Mock HTTP
tests cover direct, thinking and harness requests; GUI tests exercise animation,
reset, and stable scrolling panels. Build with `-Dapp=false` for engine-only use.
