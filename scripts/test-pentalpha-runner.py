#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay Pentalpha CSV independently and check seeded decisions and arguments."""
import csv
import io
from pathlib import Path
import subprocess

runner = Path(__file__).resolve().parent.parent / 'pentalpha/build/puzzle-runner'
lines = [('v1', 'i1', 'i2', 'v3'), ('v3', 'i3', 'i4', 'v5'),
         ('v5', 'i5', 'i1', 'v2'), ('v2', 'i2', 'i3', 'v4'),
         ('v4', 'i4', 'i5', 'v1')]
triples = set()
for line in lines:
    for start in (0, 1):
        triple = line[start:start + 3]
        triples.update((triple, triple[::-1]))

for controller in ('random', 'heuristic', 'search'):
    args = [str(runner), '--runs=20', '--seed=17', f'--controller={controller}']
    def records():
        return list(csv.DictReader(io.StringIO(subprocess.check_output(args, text=True))))
    first, second = records(), records()
    assert len(first) == 20
    for index, (a, b) in enumerate(zip(first, second)):
        assert int(a['seed']) == index + 17
        # Runtime measurements vary; seeded choices and search nodes must match.
        assert {k: v for k, v in a.items() if k != 'elapsed_us'} == {
            k: v for k, v in b.items() if k != 'elapsed_us'}
        occupied = set()
        for text in a['sequence'].split():
            move = tuple(text.split('>'))
            assert move in triples
            assert move[0] not in occupied and move[2] not in occupied
            assert len(occupied) < 9
            occupied.add(move[2])
        assert len(occupied) == int(a['pieces'])
        legal = [m for m in triples if m[0] not in occupied and m[2] not in occupied]
        assert len(occupied) == 9 or not legal
        assert a['result'] == ('solved' if len(occupied) == 9 else 'stuck')
        if controller == 'search':
            assert a['result'] == 'solved'
    print(f'Pentalpha {controller}: seeded decisions, independent legal replay, and results OK')
for arg in ('--runs=0', '--seed=-1', '--depth=0', '--depth=10', '--controller=human',
            '--controller=ollama', '--seed=2147483647', '--unknown'):
    assert subprocess.run([str(runner), arg], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0
print('Pentalpha: invalid arguments rejected')
