#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently replay Heptalpha CSV, including path IDs and seeded choices."""
import csv
import io
from pathlib import Path
import subprocess

runner = Path(__file__).resolve().parent.parent / 'heptalpha/build/puzzle-runner'
# Explicit source topology, separate from the C engine's generated move table.
paths = [('v1','i2','i3','i5','i6','v4'), ('v2','i4','i5','i7','i8','v5'),
         ('v3','i6','i7','i9','i10','v6'), ('v4','i8','i9','i11','i12','v7'),
         ('v5','i10','i11','i13','i14','v1'), ('v6','i12','i13','i1','i2','v2'),
         ('v7','i14','i1','i3','i4','v3')]
triples = set()
for index, path in enumerate(paths, 1):
    for start in range(4):
        triple = path[start:start + 3]
        triples.update(((triple, index), (triple[::-1], index)))
assert len(triples) == 56
for controller in ('random', 'heuristic', 'search'):
    args = [str(runner), '--runs=5', '--seed=17', f'--controller={controller}']
    def records():
        return list(csv.DictReader(io.StringIO(subprocess.check_output(args, text=True))))
    first, second = records(), records()
    assert len(first) == 5
    for index, (a,b) in enumerate(zip(first, second)):
        assert int(a['seed']) == index + 17
        assert {k:v for k,v in a.items() if k != 'elapsed_us'} == {
            k:v for k,v in b.items() if k != 'elapsed_us'}
        occupied = set()
        for text in a['sequence'].split():
            sequence, path_id = text.split('@p')
            move = tuple(sequence.split('>'))
            assert (move, int(path_id)) in triples
            assert move[0] not in occupied and move[2] not in occupied
            assert len(occupied) < 20
            occupied.add(move[2])
        assert len(occupied) == int(a['pieces'])
        legal = [m for m,p in triples if m[0] not in occupied and m[2] not in occupied]
        assert len(occupied) == 20 or not legal
        assert a['result'] == ('solved' if len(occupied) == 20 else 'stuck')
        if controller == 'search': assert a['result'] == 'solved'
    print(f'Heptalpha {controller}: repeatable choices, path-aware legal replay, and results OK')
for arg in ('--runs=0', '--seed=-1', '--depth=0', '--depth=21', '--controller=human',
            '--controller=ollama', '--seed=2147483647', '--unknown'):
    assert subprocess.run([str(runner), arg], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0
print('Heptalpha: invalid arguments rejected')
