#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent edge-list replay of paired Ludus Coriovalli sessions."""
import csv
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'ludus-coriovalli/build/tournament'
EDGES = ('AB', 'BC', 'AD', 'CE', 'EF', 'FG', 'DH', 'HI', 'IJ', 'JG',
         'DB', 'BE', 'BJ', 'HJ', 'JF')
adjacent = {chr(65+p): set() for p in range(10)}
for a, b in EDGES:
    adjacent[a].add(b)
    adjacent[b].add(a)


def replay(row):
    dogs, hares = set('ADHI'), set('EF')
    round_number, turn, survival = 1, 1, 0
    scores, capped = [None, None], [False, False]
    over = False
    actions = row['moves'].split(';')
    for action in actions:
        round_text, actor, move = action.split(':')
        if int(round_text) != round_number:
            assert over and round_number == 1 and int(round_text) == 2
            round_number, turn, survival = 2, 1, 0
            dogs, hares, over = set('ADHI'), set('EF'), False
        assert not over
        player = turn if round_number == 1 else 3-turn
        assert int(actor) == player
        occupied = dogs | hares
        role = dogs if turn == 1 else hares
        legal = {a+'-'+b for a in role for b in adjacent[a] if b not in occupied}
        if move == 'pass':
            assert turn == 1 and not legal
        else:
            assert move in legal, (row['match'], action, legal)
            a, b = move.split('-')
            role.remove(a)
            role.add(b)
            if turn == 2:
                survival += 1
        assert len(dogs) == 4 and len(hares) == 2 and not dogs & hares
        occupied = dogs | hares
        blocked = all(adjacent[h] <= occupied for h in hares)
        if blocked or survival >= int(row['limit']):
            over = True
            hare_player = 2 if round_number == 1 else 1
            scores[hare_player-1] = survival
            capped[hare_player-1] = not blocked
        turn = 3-turn
    assert over and round_number == 2
    assert scores == [int(row['survival1']), int(row['survival2'])]
    assert capped == [bool(int(row['capped1'])), bool(int(row['capped2']))]
    winner = 3 if scores[0] == scores[1] else 1 if scores[0] > scores[1] else 2
    assert winner == int(row['winner'])
    assert len(actions) == int(row['actions']) <= 4*int(row['limit'])


count = 0
for extra in ([], ['--swap'], ['--plain'], ['--plain', '--swap']):
    args = [str(BINARY), '--seed', '2718', '--matches', '10', '--depth', '3', '--limit', '20'] + extra
    output = subprocess.check_output(args, text=True)
    assert output == subprocess.check_output(args, text=True), 'decision seed reproducibility'
    for row in csv.DictReader(io.StringIO(output)):
        replay(row)
        count += 1
for invalid in (['--depth', '7'], ['--matches', '0'], ['--limit', '0']):
    assert subprocess.run([str(BINARY)] + invalid, capture_output=True).returncode == 2
print(f'Coriovalli: {count} paired matches independently replayed across both seats and search algorithms')
