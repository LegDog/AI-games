#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently enumerate and replay English/Brazilian complete turns."""
import csv
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SQUARES = [(x, y) for y in range(8) for x in range(8) if (x + y) % 2 == 0]
LABELS = [f'{chr(97+x)}{y+1}' for x, y in SQUARES]


def legal(board, side, variant):
    brazil = variant == 'brazilian'
    chains = []

    def extend(cells, source, path, taken):
        x, y = source
        piece = cells[source]
        king = abs(piece) == 2
        found = False
        for dx, dy in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
            if not king and not brazil and dy != side:
                continue
            victim = None
            distance = 1
            while 0 <= x+dx*distance < 8 and 0 <= y+dy*distance < 8:
                target = x+dx*distance, y+dy*distance
                value = cells.get(target, 0)
                if value:
                    if victim is not None or (value > 0) == (side > 0) or target in taken:
                        break
                    victim = target
                elif victim is not None:
                    updated = dict(cells)
                    del updated[source]
                    updated[target] = piece
                    if not brazil:
                        del updated[victim]
                    found = True
                    next_path = (*path, target)
                    if not brazil and not king and target[1] == (7 if side == 1 else 0):
                        chains.append((next_path, taken | {victim}))
                    else:
                        extend(updated, target, next_path, taken | {victim})
                    if not (king and brazil):
                        break
                elif not (king and brazil):
                    break
                if not (king and brazil) and distance == 2:
                    break
                distance += 1
        if not found and taken:
            chains.append((path, taken))

    for source, piece in board.items():
        if (piece > 0) == (side > 0):
            extend(board, source, (source,), frozenset())
    if chains:
        most = max(len(taken) for _, taken in chains)
        return {notation(path, True): (path, taken)
                for path, taken in chains if not brazil or len(taken) == most}
    result = {}
    for (x, y), piece in board.items():
        if (piece > 0) != (side > 0):
            continue
        for dx, dy in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
            if abs(piece) == 1 and dy != side:
                continue
            d = 1
            while 0 <= x+dx*d < 8 and 0 <= y+dy*d < 8:
                target = x+dx*d, y+dy*d
                if target in board:
                    break
                path = ((x, y), target)
                result[notation(path, False)] = path, frozenset()
                if abs(piece) == 1 or not brazil:
                    break
                d += 1
    return result


def notation(path, capture):
    return ('x' if capture else '-').join(f'{chr(97+x)}{y+1}' for x, y in path)


def replay(row):
    variant = row['variant']
    assert variant in ('english', 'brazilian')
    board = {p: 1 if p[1] < 3 else -1 for p in SQUARES if p[1] < 3 or p[1] > 4}
    side = 1 if variant == 'brazilian' else -1
    quiet, result, reason = 0, 0, 0
    key = lambda: (tuple(sorted(board.items())), side)
    seen = {key(): 1}
    turns = row['moves'].split(';')
    for text in turns:
        assert not result, ('move after result', text)
        options = legal(board, side, variant)
        assert text in options, (variant, text, list(options))
        path, victims = options[text]
        piece = board.pop(path[0])
        for victim in victims:
            del board[victim]
        last = path[-1]
        board[last] = 2 * side if abs(piece) == 1 and last[1] == (7 if side == 1 else 0) else piece
        quiet = 0 if victims or abs(piece) == 1 else quiet + 1
        if victims or abs(piece) == 1:
            seen = {}
        side = -side
        seen[key()] = seen.get(key(), 0) + 1
        winner = 2 if side == 1 else 1
        if not any((p > 0) == (side > 0) for p in board.values()):
            result, reason = winner, 1
        elif not legal(board, side, variant):
            result, reason = winner, 2
        elif seen[key()] >= 3:
            result, reason = 3, 3
        elif quiet >= (40 if variant == 'brazilian' else 80):
            result, reason = 3, 4
    assert result and (result, reason, len(turns)) == (int(row['winner']), int(row['end']), int(row['actions'])), row


def main():
    binary = ROOT / 'checkers/build/tournament'
    assert binary.exists(), 'Build first: make GAME=checkers'
    count = 0
    for variant in ('english', 'brazilian'):
        for swap in (False, True):
            for plain in (False, True):
                args = [str(binary), f'--variant={variant}', '--games=6', '--seed=2026', '--depth=2', '--budget=5000']
                args += ['--swap'] if swap else []
                args += ['--plain'] if plain else []
                first = subprocess.check_output(args, text=True, timeout=60)
                assert first == subprocess.check_output(args, text=True, timeout=60)
                rows = list(csv.DictReader(io.StringIO(first)))
                assert len(rows) == 6
                for row in rows:
                    replay(row)
                    count += 1
    for invalid in ('--variant=unknown', '--depth=0', '--games=0', '--budget=0'):
        assert subprocess.run([str(binary), invalid], capture_output=True).returncode == 2
    print(f'Checkers: independently replayed {count} seeded games, both variants, seats and algorithms; reproducibility OK')


if __name__ == '__main__':
    main()
