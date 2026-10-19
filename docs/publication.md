# Publication review

Run `python3 scripts/audit-history.py` before publication. It examines all
commits reachable through local refs and reflogs, every historical blob/path,
and current publishable working files. It flags common credential formats,
private keys, credential URLs, and private/generated filenames without printing
matched secrets. It is a targeted check, not a guarantee against all secrets;
inspect unusual findings and use additional scanning when appropriate.

The initial review found no suspicious files or credential-pattern matches in
the five branch commits or the two superseded initial commits retained locally.
Current branch author and committer metadata use a GitHub noreply address.
The two superseded initial commits contain personal email metadata only in the
local reflog. A normal branch push does not publish reflog-only commits. Do not
share the `.git` directory as a publication artifact. No history rewrite or
reflog deletion was needed or performed.

The apps declare GPL-3.0-or-later in Meson and their READMEs, include license
copies, and mark project C sources/headers with SPDX identifiers. The current
application IDs are `io.github.aigames.FourInARow`,
`io.github.aigames.TicTacToe`, `io.github.aigames.Pentalpha`, and
`io.github.aigames.Heptalpha`. Confirm that this is the intended long-term
namespace before distributing desktop packages; the repository cannot establish
namespace ownership. Changing IDs later affects desktop integration and app
identity, so no automatic rename is made here.

Before announcing a release, check the first CI run, including HTTP fixtures,
GUI tests, and screenshots. Local builds alone do not validate a newly added
hosted workflow. See [contributor guidance](../CONTRIBUTING.md),
[experiments](experiments.md), [logs](logging.md), and
[screenshot capture](screenshots.md).

CI uses read-only repository permissions and version-pinned actions, following
[GitHub workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).
Pinned upstream releases: [checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)
and [upload-artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2).

## Application rename

The four-in-a-row app is now **AI Games: Four in a Row** (`four-in-a-row/`,
executable `four-in-a-row`, ID `io.github.aigames.FourInARow`). Tic-Tac-Toe uses
`tic-tac-toe/`, executable `tic-tac-toe`, and ID
`io.github.aigames.TicTacToe`. The old game-branded titles are no longer used
in the current four-in-a-row interface or translations. This naming change
is not a trademark-clearance opinion.

Saved language preferences fall back to the former `connect-4.ini` or
`jogo-da-velha.ini` only when the new file is absent. Subsequent language
changes save to the new filename. Existing game histories remain local;
the four-in-a-row history filename is now `four-in-a-row-history.md`.
Existing entries are preserved as recorded, including any older titles.

A renamed checkout needs fresh builds because Meson stores absolute paths.
Previously installed executables and launchers are not automatically removed
by a new installation. Remove only the old installations you no longer want;
then run `make install` to install both renamed applications. Their previous
IDs were `io.github.aigames.Connect4` and `io.github.aigames.JogoDaVelha`.

Output files now use game-specific prefixes. Old histories and logs are left
untouched; new game records go to `<game>-history.md`, communication to
`<game>-ollama.log`, and failures to `<game>-ollama-error.log`.

Peg Solitaire is published as `peg-solitaire`, desktop ID `io.github.aigames.PegSolitaire`. Its board and icon are original vector/Cairo artwork; no solution database is distributed.

Pentalfa Cruzado Circulado uses executable `pentalpha-crossed-circle` and desktop ID `io.github.aigames.PentalphaCrossedCircle`. Its board and icon are original generated vectors. The user-supplied photograph and branding are not distributed; no solution database is included.

Reversi uses executable `reversi` and desktop ID `io.github.aigames.Reversi`. The 8×8 board is drawn with Cairo and the icon is original SVG artwork. The game follows the cited World Othello Federation rules without redistributing its illustrations or branding.

Hex uses executable `hex` and desktop ID `io.github.aigames.Hex`. Its rhombus board, hexagonal cells, colored borders, and icon are original Cairo/SVG artwork. Rules sources and the reflected opening-swap convention are documented in its README; no source illustrations or precomputed solutions are redistributed.

Tavli uses executable `tavli` and desktop ID `io.github.aigames.Tavli`. Its board and icon are original Cairo/SVG artwork. The README cites the published Greek/backgammon rules and identifies Running as the user's custom rules. No trained models or precomputed strategies are redistributed.

Sokoban uses executable `sokoban` and desktop ID `io.github.aigames.Sokoban`.
Its eight bundled levels, Cairo graphics and SVG icon are original project work.
The README links the official rules; no commercial level collections, source
illustrations, branding, or precomputed solutions are redistributed.

Go uses executable `go-game` (avoiding the Go language tool's `go` name),
desktop ID `io.github.aigames.Go`, and repository selector `GAME=go`. Its board
and SVG icon are original drawings. No external engine, trained network, opening
book, or source artwork is redistributed. The README identifies the exact
Chinese-style rules and played-out capture convention.

Pentago uses executable `pentago` and desktop ID `io.github.aigames.Pentago`.
Its quadrant board and SVG icon are original vector artwork. The README links
the rules booklet; no source illustrations, opening books, or solved positions
are redistributed. GUI screenshots are captured from the application.

Go-Moku uses executable `gomoku` and desktop ID `io.github.aigames.Gomoku`.
It has an original SVG icon and a freestyle 15×15 engine.
