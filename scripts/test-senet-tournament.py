#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent replay of Senet's documented teaching reconstruction."""
import csv
import io
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
binary = root / 'senet/build/tournament'
args = [str(binary), '--seed', '31415', '--games', '20', '--depth', '1']
output = subprocess.check_output(args, text=True)
assert output == subprocess.check_output(args, text=True), 'seed reproducibility'
rows = list(csv.DictReader(io.StringIO(output)))
rows += list(csv.DictReader(io.StringIO(subprocess.check_output(args + ['--swap'], text=True))))
for row in rows:
    pieces = [set(range(1, 10, 2)), set(range(2, 11, 2))]
    finished = [0, 0]
    turn = 0
    actions = row['moves'].split(';')
    for record in actions:
        actor, roll, source = record.split(':')
        roll = int(roll)
        assert int(actor) == turn+1 and 1 <= roll <= 5
        legal = set()
        for pos in pieces[turn]:
            dest = pos+roll
            if pos >= 28:
                if dest == 31:
                    legal.add(pos)
                continue
            if dest > 30 or (pos < 26 < dest) or dest in pieces[turn]:
                continue
            if dest in pieces[1-turn] and any(n in pieces[1-turn] for n in (dest-1, dest+1)):
                continue
            legal.add(pos)
        if source == 'pass':
            assert not legal
            turn = 1-turn
            continue
        source = int(source)
        assert source in legal, (row['game'], record, pieces)
        dest = source+roll
        pieces[turn].remove(source)
        if dest == 31:
            finished[turn] += 1
        elif dest == 27:
            occupied = pieces[0] | pieces[1]
            rebirth = max(house for house in range(1, 16) if house not in occupied)
            pieces[turn].add(rebirth)
        else:
            if dest in pieces[1-turn]:
                pieces[1-turn].remove(dest)
                pieces[1-turn].add(source)
            pieces[turn].add(dest)
        assert not pieces[0] & pieces[1]
        assert all(len(pieces[s])+finished[s] == 5 for s in (0, 1))
        if roll in (2, 3):
            turn = 1-turn
    winner = next((s+1 for s in (0, 1) if finished[s] == 5), 0)
    assert winner == int(row['winner']) and winner != 0
    assert len(actions) == int(row['actions'])
for invalid in (['--depth', '4'], ['--games', '0'], ['--max-actions', '0']):
    assert subprocess.run([str(binary)] + invalid, capture_output=True).returncode == 2
short = next(csv.DictReader(io.StringIO(subprocess.check_output(
    [str(binary), '--games', '1', '--max-actions', '1'], text=True))))
assert short['winner'] == '0' and short['actions'] == '1'
print('Senet: 40 seeded games in both player orders independently replayed; input limits verified')
