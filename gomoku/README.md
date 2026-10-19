# AI Games: Go-Moku

A native GTK 4/libadwaita application for **freestyle Go-Moku (Gomoku)** on a
15×15 board. Licensed under GPL-3.0-or-later.

Black starts. Players alternate placing one stone on an empty intersection.
Five **or more** consecutive stones horizontally, vertically, or diagonally
wins immediately. A full board without a winning line draws. There are no
captures, passes, forbidden moves, or special opening rules. This is freestyle,
not exact-five or Renju. [Freestyle rules](https://gomokunow.com/rules/how-to-play-gomoku).

Coordinates run **a–o** left to right and **1–15** top to bottom. Click to place,
or move the focus ring with arrow keys and press Enter or Space. The last move
and winning stones are highlighted. A blinking placement animation commits
only when it finishes. Pause freezes animation and holds completed worker
results; Undo restores a whole turn and pauses. Restart retains the player
configuration; New configuration returns to player selection.

Status, diagnostics, and rules occupy fixed-height scrollable panels, keeping
the board stable. English, Portuguese, Spanish, and Italian are supported.

## Players and search

Each side independently selects Human, Random, Heuristic, Minimax (plain),
Minimax (alpha-beta), or Ollama. The heuristic sums unblocked length-five
windows with weights **1, 10, 100, 10000** for one through four stones, subtracting
the opponent's sum. Terminal wins/losses score ±1,000,000; draws score zero.
Nonterminal scores are clamped below terminal values.

Both search players use the same evaluation and consider **every empty point**,
with no neighborhood filtering. Alpha-beta prunes internal branches; every root
move uses a full window so values and ties agree with plain Minimax at equal
completed depth. Iterative deepening retains only complete iterations. Time,
memory, cancellation, and deterministic node limits are enforced. A limit before
any completed depth requires Retry rather than silently choosing another player.
Memory figures account for search working storage, not process RSS.

Weak requests depth 1; Strong requests depth 3; Custom allows depth 1–6. Default
budgets are five seconds and 64 MiB per decision. These presets describe search
depth, not expert strength: the opening has 225 moves and deeper iterations may
not finish within the budget. Histories record completed depth, nodes, leaves,
cutoffs, limits, evaluation tables, decision seeds, and chosen moves.

Ollama receives the rules, board, side to move, and **all legal coordinates**,
without heuristic/search assistance. Responses use a strict JSON schema such
as `{"move":"h8"}` and are validated for completion and legality. Native
thinking metadata and legacy Cogito's two-stage instruction follow the same
conventions as the other games. Requests use the existing full diagnostic logs;
the separate selectable, scrollable messages window displays Sent, Thinking,
Received, and errors. Responses appear after each non-streaming call completes.

## Build and test

From the repository root:

```sh
make GAME=gomoku
make GAME=gomoku run
make GAME=gomoku test
make GAME=gomoku test-ollama
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- make GAME=gomoku test-ui MESON_ARGS=-Dwerror=true
python3 scripts/test-gomoku-tournament.py
```

Engine-only tests need GLib and a C11 compiler. The interface also requires
GTK ≥4.10, libadwaita ≥1.4, libsoup 3, and JSON-GLib. See the [root README](../README.md)
for dependencies and installation. Executable: `gomoku`; desktop ID:
`io.github.aigames.Gomoku`. Language preference: `ai-games/gomoku.ini` under
`XDG_CONFIG_HOME`. Finished games append to `gomoku-history.md` in the working
directory; diagnostic files are `gomoku-ollama.log` and `gomoku-ollama-error.log`.
Ollama URL/log overrides use the same environment variables as the other apps.

## Reproducible tournaments

```sh
gomoku/build/tournament --games=4 --seed=10 --first=minimax --second=alpha-beta \
  --depth-first=1 --depth-second=1 --time-first=0 --time-second=0
```

The engine-only build also provides `gomoku/build-tests/tournament` after
`meson compile -C gomoku/build-tests tournament`. CSV contains configurations,
results, complete coordinate sequences, stone counts, search statistics, resource
limits and completed depths. `--node-limit` sets a deterministic per-decision
budget. If no iteration completes, the row is marked `incomplete`.
The Python regression independently replays each sequence, checks the result,
verifies seeds, and exercises limits and invalid arguments. Engine tests compare
plain/alpha-beta values against an independent exhaustive reference on dense
positions, plus wins in all directions, overlines, row boundaries, draws,
heuristic wins/blocks, and randomized legal games.
