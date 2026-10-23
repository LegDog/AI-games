#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently replay Morris CSV exports using coordinate-derived rules."""
import csv
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LABELS = 'a7 d7 g7 g4 g1 d1 a1 a4 b6 d6 f6 f4 f2 d2 b2 b4 c5 d5 e5 e4 e3 d3 c3 c4'.split()
POINTS = [(ord(s[0]) - ord('a'), int(s[1])) for s in LABELS]
# Derive lines by ring boundaries and cardinal spokes, independently of C tables.
LINES = []
for lo, hi in [(0, 6), (1, 5), (2, 4)]:
    for axis in (0, 1):
        for boundary in (lo, hi):
            pts = [i for i, xy in enumerate(POINTS)
                   if xy[axis] == (boundary if axis == 0 else boundary + 1)
                   and lo <= xy[1 - axis] - (1 if axis == 0 else 0) <= hi]
            # A ring boundary can include points from other rings on its long line;
            # the range selects exactly its three points.
            if len(pts) == 3:
                LINES.append(tuple(sorted(pts, key=lambda i: POINTS[i][1 - axis])))
for axis in (0, 1):
    for low in (True, False):
        pts = [i for i, xy in enumerate(POINTS)
               if xy[axis] == (3 if axis == 0 else 4)
               and ((xy[1-axis] < (4 if axis == 0 else 3)) == low)]
        LINES.append(tuple(sorted(pts, key=lambda i: POINTS[i][1-axis])))
assert len(LINES) == 16 and len(set(map(frozenset, LINES))) == 16
EDGES = {frozenset((line[i], line[i+1])) for line in LINES for i in (0, 1)}
assert len(EDGES) == 32


def replay(row):
    cells = [0] * 24
    reserve = [9, 9]
    turn, quiet, result, end = 1, 0, 0, 0
    seen = {(tuple(cells), tuple(reserve), turn): 1}
    labels = row['moves'].split(';')
    for step, notation in enumerate(labels, 1):
        assert result == 0, ('move after termination', step)
        parts = notation.split('x')
        assert len(parts) <= 2
        action = parts[0].split('-')
        assert len(action) in (1, 2)
        to = LABELS.index(action[-1])
        source = LABELS.index(action[0]) if len(action) == 2 else None
        assert cells[to] == 0
        if reserve[turn-1]:
            assert source is None
            reserve[turn-1] -= 1
        else:
            assert source is not None and cells[source] == turn
            assert (not any(reserve) and cells.count(turn) == 3) or frozenset((source, to)) in EDGES
            cells[source] = 0
        cells[to] = turn
        mill = any(to in line and all(cells[p] == turn for p in line) for line in LINES)
        enemy = 3 - turn
        if mill and enemy in cells:
            assert len(parts) == 2, ('missing capture', notation)
            target = LABELS.index(parts[1])
            protected = {p for line in LINES if all(cells[q] == enemy for q in line) for p in line}
            unprotected = {p for p, owner in enumerate(cells) if owner == enemy} - protected
            assert cells[target] == enemy and (not unprotected or target in unprotected)
            cells[target] = 0
        else:
            assert len(parts) == 1, ('unexpected capture', notation)
        irreversible = source is None or len(parts) == 2
        quiet = 0 if irreversible else quiet + 1
        if irreversible:
            seen = {}
        turn = enemy
        key = (tuple(cells), tuple(reserve), turn)
        seen[key] = seen.get(key, 0) + 1
        if cells.count(turn) + reserve[turn-1] < 3:
            result, end = 3-turn, 1
        else:
            empty = [p for p in range(24) if cells[p] == 0]
            mobile = (bool(empty) if reserve[turn-1] else
                      any((not any(reserve) and cells.count(turn) == 3) or frozenset((p, q)) in EDGES
                          for p in range(24) if cells[p] == turn for q in empty))
            if not mobile:
                result, end = 3-turn, 2
            elif seen[key] >= 3:
                result, end = 3, 3
            elif quiet >= 50:
                result, end = 3, 4
    assert result != 0
    assert (int(row['winner']), int(row['end']), int(row['actions'])) == (result, end, len(labels))


def main():
    binary = ROOT / 'nine-mens-morris/build/tournament'
    if not binary.exists():
        subprocess.run(['make', '-C', str(binary.parent.parent)], check=True)
    total = 0
    for swapped in (False, True):
        for plain in (False, True):
            args = [str(binary), '--games', '6', '--seed', '2026', '--depth', '2', '--budget', '10000']
            if swapped:
                args += ['--swap']
            if plain:
                args += ['--plain']
            first = subprocess.check_output(args, text=True, timeout=60)
            assert first == subprocess.check_output(args, text=True, timeout=60), 'nonrepeatable export'
            for row in csv.DictReader(io.StringIO(first)):
                replay(row)
                total += 1
    for args in (['--depth', '0'], ['--budget', '0'], ['--games', '-1'], ['extra']):
        assert subprocess.run([str(binary), *args], capture_output=True).returncode == 2
    print(f'Morris: {total} independent replays passed; seeds repeat in both seats and algorithms.')


if __name__ == '__main__':
    main()
