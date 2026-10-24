#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay complete Alquerque turns using independent coordinate-based rules."""
import csv
import io
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LABELS = [f'{x}{y}' for y in range(1, 6) for x in 'abcde']


def edge(a, b):
    dx, dy = abs(a % 5 - b % 5), abs(a // 5 - b // 5)
    return max(dx, dy) == 1 and (not dx or not dy or (a % 5 + a // 5) % 2 == 0)


def jumps(cells, side, source):
    for target, value in enumerate(cells):
        dx, dy = target % 5 - source % 5, target // 5 - source // 5
        if value or max(abs(dx), abs(dy)) != 2 or dx % 2 or dy % 2:
            continue
        middle = (source // 5 + dy // 2) * 5 + source % 5 + dx // 2
        if edge(source, middle) and edge(middle, target) and cells[middle] == 3 - side:
            yield target, middle


def quiet_moves(cells, side, source, variant):
    for target, value in enumerate(cells):
        if value or not edge(source, target):
            continue
        if variant == 'directional':
            if side == 1 and (source // 5 == 4 or target // 5 < source // 5):
                continue
            if side == 2 and (source // 5 == 0 or target // 5 > source // 5):
                continue
        yield target


def mobile(cells, side, variant):
    return any(list(jumps(cells, side, source)) or list(quiet_moves(cells, side, source, variant))
               for source, value in enumerate(cells) if value == side)


def replay(row):
    variant = row['variant']
    assert variant in ('free', 'directional')
    cells = [1] * 10 + [2, 2, 0, 1, 1] + [2] * 10
    side, quiet, result, reason = 1, 0, 0, 0
    seen = {(tuple(cells), side): 1}
    moves = row['moves'].split(';')
    for notation in moves:
        assert not result, ('move after termination', notation)
        capture = 'x' in notation
        assert re.fullmatch(r'[a-e][1-5](x[a-e][1-5])+|[a-e][1-5]-[a-e][1-5]', notation)
        path = [LABELS.index(label) for label in re.split('[x-]', notation)]
        source = path[0]
        assert cells[source] == side
        forced = any(list(jumps(cells, side, p)) for p, value in enumerate(cells) if value == side)
        assert forced == capture, ('capture obligation', notation)
        for target in path[1:]:
            if capture:
                options = dict(jumps(cells, side, source))
                assert target in options, ('invalid jump', notation)
                cells[options[target]] = 0
            else:
                assert target in quiet_moves(cells, side, source, variant), ('invalid quiet move', notation)
            cells[source], cells[target] = 0, side
            source = target
        assert not capture or not list(jumps(cells, side, source)), ('incomplete chain', notation)
        quiet = 0 if capture else quiet + 1
        if capture:
            seen = {}
        side = 3 - side
        key = tuple(cells), side
        seen[key] = seen.get(key, 0) + 1
        if side not in cells:
            result, reason = 3 - side, 1
        elif not mobile(cells, side, variant):
            result, reason = 3 - side, 2
        elif seen[key] >= 3:
            result, reason = 3, 3
        elif quiet >= 50:
            result, reason = 3, 4
    assert result
    assert (int(row['winner']), int(row['end']), int(row['actions'])) == (result, reason, len(moves))


def main():
    binary = ROOT / 'alquerque/build/tournament'
    if not binary.exists():
        subprocess.run(['make', '-C', str(binary.parent.parent)], check=True)
    total = 0
    for variant in ('free', 'directional'):
        for swap in (False, True):
            for plain in (False, True):
                args = [str(binary), '--variant', variant, '--games', '6', '--seed', '2026',
                        '--depth', '2', '--budget', '10000']
                if swap:
                    args += ['--swap']
                if plain:
                    args += ['--plain']
                first = subprocess.check_output(args, text=True, timeout=60)
                assert first == subprocess.check_output(args, text=True, timeout=60), 'nonrepeatable export'
                rows = list(csv.DictReader(io.StringIO(first)))
                assert len(rows) == 6
                for index, row in enumerate(rows):
                    assert row['variant'] == variant
                    assert int(row['seed']) == 2026 + index
                    assert int(row['alpha_beta']) == int(not plain)
                    assert int(row['swap']) == int(swap)
                    replay(row)
                    total += 1
    for args in (['--variant', 'unknown'], ['--depth', '0'], ['--depth', '7'],
                 ['--budget', '0'], ['--games', '-1'], ['extra']):
        assert subprocess.run([str(binary), *args], capture_output=True).returncode == 2
    print(f'Alquerque: {total} independent replays passed; both variants, seats, and search algorithms repeat.')


if __name__ == '__main__':
    main()
