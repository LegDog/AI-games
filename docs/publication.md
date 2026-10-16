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
application IDs are `io.github.aigames.FourInARow` and
`io.github.aigames.TicTacToe`. Confirm that this is the intended long-term
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
Pinned upstream releases: [checkout v4.3.1](https://github.com/actions/checkout/releases/tag/v4.3.1)
and [upload-artifact v4.6.2](https://github.com/actions/upload-artifact/releases/tag/v4.6.2).

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
