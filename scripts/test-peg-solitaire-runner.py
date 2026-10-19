#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay exported jumps using independent coordinate rules."""
import csv
import io
from pathlib import Path
import subprocess

runner = Path(__file__).resolve().parent.parent / 'peg-solitaire/build/puzzle-runner'
for board, goal in [('english', 'any'), ('english', 'center'), ('triangle', 'any')]:
    holes = {(r, c) for r in range(7) for c in range(7)
             if 2 <= r <= 4 or 2 <= c <= 4} if board == 'english' else {
                 (r, c) for r in range(5) for c in range(r + 1)}
    labels = {f'{chr(97+c)}{r+1}': (r,c) for r,c in holes}
    axes = {(0,1), (0,-1), (1,0), (-1,0)}
    if board == 'triangle': axes |= {(1,1), (-1,-1)}
    opening = (3,3) if board == 'english' else (0,0)
    for controller in ['random', 'heuristic', 'backtracking', 'astar']:
        args = [str(runner), '--runs=2', '--seed=0', f'--board={board}',
                f'--goal={goal}', f'--controller={controller}']
        def records():
            return list(csv.DictReader(io.StringIO(subprocess.check_output(args, text=True))))
        first, second = records(), records()
        assert len(first) == 2
        for a,b in zip(first, second):
            assert {k:v for k,v in a.items() if k != 'elapsed_us'} == {
                k:v for k,v in b.items() if k != 'elapsed_us'}
            occupied = holes - {opening}
            steps = a['sequence'].split()
            for step in steps:
                source, middle, dest = [labels[x] for x in step.split('>')]
                delta = (middle[0]-source[0], middle[1]-source[1])
                assert delta in axes
                assert dest == (middle[0]+delta[0], middle[1]+delta[1])
                assert source in occupied and middle in occupied and dest not in occupied
                occupied -= {source, middle}
                occupied.add(dest)
            assert len(occupied) == int(a['pegs_remaining'])
            assert len(steps) == int(a['jumps'])
            solved = len(occupied) == 1 and (goal == 'any' or opening in occupied)
            assert (a['result'] == 'solved') == solved
            if controller in ('backtracking', 'astar'):
                assert solved and a['search_status'] == 'solved'
            else:
                assert not any((r+dr,c+dc) in occupied and
                               (r+2*dr,c+2*dc) in holes-occupied
                               for r,c in occupied for dr,dc in axes)
        print(f'{board}/{goal}/{controller}: deterministic legal replay OK')
for arg in ['--runs=0', '--seed=-1', '--board=bad', '--controller=ollama',
            '--memory-limit=0', '--time-limit=-1', '--time-limit=nan', '--unknown']:
    assert subprocess.run([str(runner),arg],stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0
row = list(csv.DictReader(io.StringIO(subprocess.check_output(
    [str(runner),'--time-limit=0.000000000001'],text=True))))[0]
assert row['search_status'] == 'limit-reached' and row['result'] == 'ongoing'
print('Invalid arguments and distinct resource-limit result OK')
