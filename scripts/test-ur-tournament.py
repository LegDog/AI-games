#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently replay every exported Ur move, including forced passes."""
import csv
import io
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
binary = root / 'royal-game-of-ur/build/tournament'
args = [str(binary), '--seed', '2718', '--games', '20', '--depth', '1']
output = subprocess.check_output(args, text=True)
assert output == subprocess.check_output(args, text=True), 'seed reproducibility'
rows = list(csv.DictReader(io.StringIO(output)))
rows += list(csv.DictReader(io.StringIO(subprocess.check_output(args + ['--swap'], text=True))))
for row in rows:
    pieces = [[0]*7 for _ in range(2)]
    turn = 0
    actions = row['moves'].split(';')
    for record in actions:
        actor, roll, source = record.split(':')
        roll = int(roll)
        assert int(actor) == turn+1 and 0 <= roll <= 4
        legal = set()
        for pos in pieces[turn]:
            dest = pos+roll
            if pos == 15 or roll == 0 or dest > 15:
                continue
            if dest != 15 and dest in pieces[turn]:
                continue
            if dest == 8 and dest in pieces[1-turn]:
                continue
            legal.add(pos)
        if source == 'pass':
            assert not legal
            turn = 1-turn
            continue
        source = int(source)
        assert source in legal, (row['game'], record, pieces)
        dest = source+roll
        pieces[turn][pieces[turn].index(source)] = dest
        if 5 <= dest <= 12 and dest in pieces[1-turn]:
            pieces[1-turn][pieces[1-turn].index(dest)] = 0
        if dest not in (4, 8, 14):
            turn = 1-turn
    winner = next((i+1 for i, side in enumerate(pieces) if side == [15]*7), 0)
    assert winner == int(row['winner']) and winner != 0
    assert len(actions) == int(row['actions'])
assert subprocess.run([str(binary), '--depth', '4'], capture_output=True).returncode == 2
print('Ur: 40 seeded games in both player orders independently replayed; deterministic output and input limits verified')
