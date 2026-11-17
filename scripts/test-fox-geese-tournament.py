#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently replay Fox and Geese CSV turns using coordinate sets."""
import csv
import io
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
POINTS = {(x, y) for x in range(7) for y in range(7)
          if 2 <= x <= 4 or 2 <= y <= 4}


def edge(a, b):
    dx, dy = abs(a[0]-b[0]), abs(a[1]-b[1])
    return max(dx, dy) == 1 and (not dx or not dy or sum(a) % 2 == 0)


def jumps(fox, geese):
    return {to: mid for mid in geese
            if (to := (2*mid[0]-fox[0], 2*mid[1]-fox[1])) in POINTS
            and to not in geese and edge(fox, mid) and edge(mid, to)}


def quiet(p, fox, geese, side, variant):
    return {q for q in POINTS - geese - {fox} if edge(p, q)
            and (side == 1 or variant == 'halatafl' or q[1] >= p[1])}


def replay(row):
    variant = row['variant']
    fox = (3, 3)
    geese = {p for p in POINTS if p[1] <= 2 or
             (variant == 'seventeen' and p[1] <= 4 and p[0] in (0, 6))}
    side, quiet_count, result, reason = 1, 0, 0, 0
    seen = {(fox, frozenset(geese), side): 1}
    moves = row['moves'].split(';')
    for notation in moves:
        assert not result
        assert re.fullmatch(r'[a-g][1-7](x[a-g][1-7])+|[a-g][1-7]-[a-g][1-7]', notation)
        path = [(ord(s[0])-97, int(s[1])-1) for s in re.split('[x-]', notation)]
        capture = 'x' in notation
        assert capture == (side == 1 and bool(jumps(fox, geese)))
        source = path[0]
        assert source == fox if side == 1 else source in geese
        for target in path[1:]:
            if capture:
                options = jumps(source, geese)
                assert target in options
                geese.remove(options[target])
                fox = target
            else:
                assert len(path) == 2 and target in quiet(source, fox, geese, side, variant)
                if side == 1:
                    fox = target
                else:
                    geese.remove(source)
                    geese.add(target)
            source = target
        if capture:
            assert not jumps(fox, geese)
        quiet_count = 0 if capture else quiet_count+1
        if capture:
            seen.clear()
        side = 3-side
        key = (fox, frozenset(geese), side)
        seen[key] = seen.get(key, 0)+1
        fox_mobile = bool(jumps(fox, geese) or quiet(fox, fox, geese, 1, variant))
        goose_mobile = any(quiet(p, fox, geese, 2, variant) for p in geese)
        if len(geese) < 4:
            result, reason = 1, 1
        elif not fox_mobile or (side == 2 and not goose_mobile):
            result, reason = 2, 2
        elif seen[key] >= 3:
            result, reason = 3, 3
        elif quiet_count >= 50:
            result, reason = 3, 4
    assert (result, reason, len(moves)) == (int(row['winner']), int(row['end']), int(row['actions']))


def main():
    binary = ROOT/'fox-and-geese/build/tournament'
    for variant in ('halatafl', 'seventeen'):
        for plain in (False, True):
            for swap in (False, True):
                cmd = [str(binary), '--variant', variant, '--games', '3', '--depth', '2',
                       '--budget', '3000', '--seed', '19']
                cmd += ['--plain'] if plain else []
                cmd += ['--swap'] if swap else []
                output = subprocess.check_output(cmd, text=True)
                assert output == subprocess.check_output(cmd, text=True), 'seed reproducibility'
                for row in csv.DictReader(io.StringIO(output)):
                    replay(row)
    for args in (['--variant', 'unknown'], ['--depth', '0'], ['--games', '0']):
        assert subprocess.run([str(binary), *args], capture_output=True).returncode == 2
    print('Fox and Geese: 24 seeded games independently replayed, both variants and seats.')


if __name__ == '__main__':
    main()
