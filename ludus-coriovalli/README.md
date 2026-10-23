# AI Games: Ludus Coriovalli

Independent C11 / GTK 4 / libadwaita application, GPL-3.0-or-later.

```sh
make GAME=ludus-coriovalli run       # from the repository root
make -C ludus-coriovalli test        # GLib-only rules, search and translation tests
make -C ludus-coriovalli test-ollama # mock service; no installed models
make -C ludus-coriovalli test-ui     # needs a display
```

## Candidate reconstruction

This is a playable candidate reconstruction, not a claim that one ancient
ruleset has been established. The research by Crist, Piette, Jeneson, Soemers,
Stephenson, van Goor and Browne identifies multiple plausible blocking-game
configurations: [Antiquity paper, 2026](https://doi.org/10.15184/aqy.2025.10264).

The selected candidate is Ludii ruleset **1845**, four dogs and two hares,
starting position 1, joined diagonals, no extensions:
[researchers' ruleset](https://ludii.games/variantDetails.php?keyword=Ludus+Coriovalli&variant=1845).
Board geometry follows `NoExtensionJoinedDiagonal` in the
[Ludii definition](https://ludii.games/lud/games/Ludus%20Coriovalli.lud), independently
implemented here with stable A–J vertex labels. The research paper and Ludii
source are references, not bundled assets or copied engine code.

```text
 I ------ J ------ G
 |      / | \      |
 H ----/  |  \---- F
 |        |        |
 D ----\  |  /---- E
 |      \ | /      |
 A ------ B ------ C
```

Edges: A–B, B–C, A–D, C–E, E–F, F–G, D–H, H–I, I–J, J–G,
D–B, B–E, B–J, H–J, J–F. Diagonal bends meet at B and J.
Dogs start on A,D,H,I; hares on E,F. Move one piece to an empty neighbor,
without capturing, jumping or directional restrictions. Dogs move first and
try to immobilize both hares. Hares try to survive. After the first round,
players exchange roles; the player who survives more hare moves wins.

Application stopping conventions, completing the source description:

- Survival counts completed hare moves; dogs always start each round.
- A dog with no legal move must pass. Hares never pass; immobilizing both ends
  the round immediately, including after a dog move.
- Each GUI round is capped at 100 hare moves. Headless `--limit` accepts 1–1000.
  Reaching the cap ends the round; the cap is not a historical rule.
- Equal survival scores produce an administrative match draw. A capped score
  is censored survival, not proof that the hares could evade forever.
- There is no repetition adjudication before the cap. A single trapped hare
  remains on the board; the other may continue moving.

Other Roman blocking games and alternative Coriovalli reconstructions are not
included in this first implementation.

## Controls and algorithms

Player 1 and Player 2 retain their own controller, depth and model when roles
swap. Piece colors denote roles: light dogs and dark hares. The status identifies
the current player and role. Select a source piece, then a highlighted neighbor;
click the source again to deselect. Illegal destinations leave the board unchanged.
Human **Pass** is available only when the dogs have no moves. After round 1,
click **Next round** to review and exchange roles, including AI-versus-AI sessions.

The six controllers are Human, Random, Heuristic, plain Minimax, alpha-beta, and
pure Ollama. Each player's depth control supports 1–6 plies (default 4). Heuristic
uses a single ply. Both search algorithms use identical evaluation, move order,
full root windows and seeded reservoir tie-breaking. They deepen iteratively
within 50,000 visited successor nodes; only a completed root iteration replaces
the previous choice. If no iteration finishes, the first legal move is retained.
The GUI and runner use the same search engine.

For hares, evaluation is 20 per legal hare action, +30 per mobile hare, −2 per
legal dog action, plus elapsed hare moves. Dogs use its negation. Blocking has
hare value −100000 plus elapsed survival; reaching the cap has hare value
+100000. These terminal scores are a bounded search objective for the current
round, not the result of the paired match. Controllers are not perfect-play
solvers. Depth, budget and cap affect apparent strength.

Moves slide for 300 ms. The rules commit a complete move atomically, while
input and subsequent turns wait for animation. **Pause** cancels current work;
**Resume** preserves the position. Controller changes cancel work and preserve
the match. **New game** starts a new paired match. Workers receive copied state;
generation checks discard canceled or superseded results. Search is node bounded.

Enter an installed local Ollama model name for each Ollama player. The client
uses `http://127.0.0.1:11434`, supplies the exact adjacency list, position, role,
limits and allowed actions, and accepts only a legal `{"move":"A-B"}` (or forced
`pass`). There is no tactical assistance or fallback move. Errors pause play.
Extended thinking and Reasoning harness default off. The shared messages window
shows actual requests and returned thinking and responses.

English, Portuguese, Spanish and Italian switch live; preferences are stored in
`~/.config/ai-games/ludus-coriovalli.ini` (respecting `XDG_CONFIG_HOME`). Status and
rules use fixed scrolling panels to keep board allocation stable.

Completed paired matches, including draws, append timestamps, controller/model
settings, role changes, moves, capped flags and results to
`ludus-coriovalli-history.md` in the working directory. Abandoned matches are
not saved. Logs use `ludus-coriovalli-ollama.log` and
`ludus-coriovalli-ollama-error.log`, with the usual environment overrides.
Logs contain full model communication; see [logging guidance](../docs/logging.md).

## Engine and experiments

`Game` stores ten role-owned cells, active role, round, hare-move count, total
actions, round cap, per-player survival/capped values and a round-ended flag.
`game_actor()` maps role to controller identity. Explicit legal lists contain
source/destination pairs; `{-1,-1}` is the forced dog pass. A match result remains
ongoing between rounds; `game_next_round()` explicitly starts the second round.

```sh
ludus-coriovalli/build/tournament --seed 2718 --matches 10 --depth 3 --limit 20
ludus-coriovalli/build/tournament --seed 2718 --matches 10 --depth 3 --limit 20 --plain --swap
python3 scripts/test-coriovalli-tournament.py
```

Player 1 searches against Random; `--swap` exchanges controllers. Each match
already includes both dog/hare assignments. `--plain` uses Minimax instead of
alpha-beta. CSV retains seed, requested depth, algorithm, controller seat order,
cap, match result, survival/capped values and every `round:player:move`. Winner
3 means an administrative draw. Compare both controller seats and retain capped
flags; a small sample is not evidence of general playing strength.

Engine tests independently construct all fifteen edges and exhaust all 3150
four-dog/two-hare placements for both active roles. They check illegal actions,
passes, blocking, role changes, scores, 100 randomized paired matches, search
agreement and node-budget retention. Python replay uses a separate set-based
board implementation to verify 40 paired matches across both seats and both
search algorithms. Mock HTTP covers direct/thinking/harness requests, malformed
and illegal moves, cancellation and service errors. GUI tests cover selection,
atomic animation, role exchange, reset, stale workers and stable text panels.
