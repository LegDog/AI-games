# AI Games: Heptalpha

A standalone single-player C11/GTK 4/libadwaita puzzle with Human, Random,
Heuristic, Search, and Ollama controllers. GPL-3.0-or-later. Algorithms discover
placements at runtime; there is no predefined solution sequence or solution file.

## Rules and topology

Based on [Ludosofia's Heptalpha article](https://ludosofia.com.br/arqueologia/heptalpha-parece-simples-mas-e-um-jogo-desafiador/)
and [printable rules](https://ludosofia.com.br/wp-content/uploads/2020/11/heptalpha_para_imprimir-1.pdf).
Start with 21 empty points and place 20 pieces. Each new piece enters an empty
point, crosses exactly the next point, and rests on the following empty point
**on the same continuous curved path**. The middle point may be occupied;
existing pieces remain unchanged. Do not change paths at an intersection,
skip points, or wrap past a path endpoint. Only the destination becomes occupied.
An attempt ends when 20 pieces are placed (solved), or when no legal placement
remains (stuck). Finished boards remain visible for review.

Outer vertices `v1`–`v7` run clockwise from the top. Intersections `i1`–`i14`
run clockwise from the upward direction. The seven ordered paths are:

```
p1: v1 — i2  — i3  — i5  — i6  — v4
p2: v2 — i4  — i5  — i7  — i8  — v5
p3: v3 — i6  — i7  — i9  — i10 — v6
p4: v4 — i8  — i9  — i11 — i12 — v7
p5: v5 — i10 — i11 — i13 — i14 — v1
p6: v6 — i12 — i13 — i1  — i2  — v2
p7: v7 — i14 — i1  — i3  — i4  — v3
```

Each path contains four consecutive triples, usable in either direction:
**56 directed placements** before occupancy filtering. Occupancy uses a 21-bit
`guint32`; move masks use `guint64`, including moves above bit 31. Placement
records retain entry, middle, destination, and path ID. Undo removes only the
last destination.

The board and icon are original project vector artwork, following the source
path arrangement without redistributing its illustrations. Seven rotationally
symmetric quadratic curves connect each outer vertex to the third following
vertex. A curve's control point is four times its endpoint midpoint relative
to the board center. Their 14 interior intersections supply the remaining
points. Checked-in geometry, SVG, and ordered path data share one generator:

```sh
python3 scripts/generate-heptalpha-board.py          # regenerate from repo root
python3 scripts/generate-heptalpha-board.py --check  # verify committed outputs
```

Normal builds do not run the generator or require Python. Rendering, hit targets,
labels, and movement use matching geometry. Arc-length lookup tables keep the
slide approximately constant-speed and ensure it passes through the middle point.

## Build and play

Use the same dependencies as Pentalpha: GLib, GTK 4, libadwaita, libsoup 3,
json-glib, and an SVG-capable GdkPixbuf loader. No extra SVG development library
is needed. From the repository root:

```sh
make GAME=heptalpha
make GAME=heptalpha run
make GAME=heptalpha test
make GAME=heptalpha test-ollama
make GAME=heptalpha test-ui
```

The independent project also supports `make -C heptalpha` and its usual targets.
Engine tests work with `-Dapp=false` without graphics/network libraries. Installation
provides executable `heptalpha`, desktop ID `io.github.aigames.Heptalpha`, and its
icon. The board is an embedded resource; launching outside the source directory
works. Language preferences live at `ai-games/heptalpha.ini` under the user
configuration directory. English, Portuguese, Spanish, and Italian support live
switching without resetting play.

Select an empty entry point and then a highlighted destination. Highlighted
trajectories follow the selected curve. Click the entry again or press Escape
to cancel selection. Human Undo applies to completed placements, including after
a solved/stuck board. Restart uses the same settings with a fresh seed; New
configuration abandons the attempt and unlocks controller settings.

## Controllers

- **Human** selects each placement; no hidden search assistance is applied.
- **Random** chooses uniformly among legal directed placements.
- **Heuristic** scores resulting piece count × 100 plus remaining legal directed
  placements, with random ties. This considers immediate mobility, not future
  search states.
- **Search** maximizes final piece count. Depth is 1–20, **default 20**. At a
  depth cutoff, remaining mobility breaks ties in piece-count scores. Full-depth
  Search discovers twenty-piece solutions from an empty board. Maximizing DFS
  memoizes by occupancy in a heap-allocated `guint16` table of approximately
  **4 MiB**. For a fixed evaluation root/depth, occupancy determines remaining
  depth, so a separate depth dimension is unnecessary. Repeated destinations
  share child evaluations; branches stop upon reaching the global score bound
  of 2,000. All root move values remain exact for the chosen depth, and equivalent
  best moves are sampled uniformly. Histories report depth, nodes, and duration.
- **Ollama** receives the rules, ordered curved paths, occupancy, and allowed
  placements, then returns entry/destination labels. The engine derives the
  middle point and path and validates the result. The model chooses the opening
  too. Pure mode supplies no solver values, rankings, or solution hints.

**Search assistance** defaults off. When enabled for Ollama, cancellable exact
search filters placements to those preserving the greatest achievable final
piece count. Histories distinguish assisted and pure attempts. Analysis and HTTP
work run on board copies in background workers; stale/canceled results cannot
reach the board. Failed model calls stop play with a visible error and never
substitute another controller. Restart begins a fresh attempt.

Ollama uses `/api/tags`, structured non-streaming `/api/generate`, and the existing
native/Cogito extended-thinking policies. Defaults allow **1,000 seconds** for
model discovery and **18,000 seconds (5 hours)** for a whole decision including
model loading, assistance, and thinking stages. Cancellation remains available.
`OLLAMA_URL` overrides `http://127.0.0.1:11434`.

## Animation and records

The delay is the animation duration after a decision is ready: 0–3 seconds in
0.1-second steps, default 0.5. A new piece blinks at entry for half that time,
then slides along its curve for the other half. Arrival commits occupancy and
history exactly once. Zero delay commits immediately. Delay changes affect
subsequent animations. Pause/Resume freezes automatic animation and holds ready
worker results. Restart/configuration changes cancel animation and pending work.

Completed attempts, including stuck attempts, append to `heptalpha-history.md`
in the working directory. Records include seed, controller/options, move triples
and path IDs, candidate evaluations, timings, and final count. Abandoned attempts
are not saved. Undo after a terminal board creates a continuation that can later
produce another completed record.

Full Ollama payloads, including returned thinking, append to `heptalpha-ollama.log`;
failures also append to `heptalpha-ollama-error.log`. `OLLAMA_LOG_FILE` and
`OLLAMA_ERROR_LOG_FILE` override these paths; `/dev/null` disables the respective
output. See [logging guidance](../docs/logging.md).

## Reproducible experiments and verification

```sh
./heptalpha/build/puzzle-runner --controller=search --depth=20 --runs=5 --seed=17
./heptalpha/build/puzzle-runner --controller=heuristic --runs=100 --seed=0
python3 scripts/test-heptalpha-runner.py
```

CSV columns match Pentalpha: `game,seed,controller,depth,result,pieces,sequence,nodes,elapsed_us`.
Heptalpha sequences additionally retain path IDs: `entry>middle>destination@pN`,
separated by spaces. Seeds increment per attempt. Choices and node counts are
repeatable; elapsed time varies. The runner supports Random, Heuristic, and
Search; pure/assisted Ollama attempts are recorded through the application.

Tests cover topology, masks above bit 31, independent reference-search values,
runtime solutions and all fourteen board symmetries, Undo, animation phases,
curved trajectories and speed, seeded CSV replay, mock Ollama, and UI lifecycle.
From the repository root, graphical tests and actual-widget screenshots can run
under a virtual display:

```sh
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- make GAME=heptalpha test-ui
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none xvfb-run -a dbus-run-session -- heptalpha/build-ui/screenshot screenshots
```

### Pause at the end

Configuration includes **Pause at the end (solved or blocked)**, off by default.
When enabled, the final placement finishes and the attempt is recorded before a
modal acknowledgement pauses play. Select **Continue** or close the dialog to
review the unchanged final board; use Restart, New configuration, or Human Undo
to proceed. With the option off, the existing final-board behavior is unchanged.
The setting applies to all controllers and is retained when restarting within
the current session.
