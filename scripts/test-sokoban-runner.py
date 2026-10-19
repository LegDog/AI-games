#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independently replay Sokoban CSV and compare tiny boards with 0/1 BFS."""
import csv
from collections import deque
import io
from pathlib import Path
import subprocess
import tempfile

RUNNER = Path(__file__).resolve().parent.parent / 'sokoban/build/puzzle-runner'
DIRECTIONS = {'u': (0, -1), 'd': (0, 1), 'l': (-1, 0), 'r': (1, 0)}


def board(text):
    rows = text.split('|')
    floor, goals, boxes = set(), set(), set()
    player = None
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            cell = x, y
            if ch != '#':
                floor.add(cell)
            if ch in '.*+':
                goals.add(cell)
            if ch in '$*':
                boxes.add(cell)
            if ch in '@+':
                assert player is None
                player = cell
    return floor, goals, frozenset(boxes), player


def step(floor, boxes, player, direction):
    dx, dy = DIRECTIONS[direction]
    target = player[0] + dx, player[1] + dy
    if target not in floor:
        return None
    pushed = target in boxes
    if pushed:
        dest = target[0] + dx, target[1] + dy
        if dest not in floor or dest in boxes:
            return None
        boxes = boxes - {target} | {dest}
    return frozenset(boxes), target, pushed


def optimum(text):
    floor, goals, boxes, player = board(text)
    best = {(boxes, player): 0}
    queue = deque([(boxes, player, 0)])
    while queue:
        boxes, player, cost = queue.popleft()
        if best[boxes, player] != cost:
            continue
        if boxes == goals:
            return cost
        for direction in DIRECTIONS:
            result = step(floor, boxes, player, direction)
            if result is None:
                continue
            next_boxes, next_player, pushed = result
            next_cost = cost + pushed
            key = next_boxes, next_player
            if next_cost >= best.get(key, float('inf')):
                continue
            best[key] = next_cost
            item = next_boxes, next_player, next_cost
            (queue.append if pushed else queue.appendleft)(item)
    return None


def run(*args):
    return list(csv.DictReader(io.StringIO(subprocess.check_output(
        [str(RUNNER), '--memory-limit=8', *args], text=True, timeout=30))))


def replay(row):
    floor, goals, boxes, player = board(row['initial'])
    pushes = walking = 0
    for action in row['sequence']:
        result = step(floor, boxes, player, action.lower())
        assert result is not None, row
        boxes, player, pushed = result
        assert action.isupper() == pushed
        pushes += pushed
        walking += not pushed
    assert pushes == int(row['pushes']) and walking == int(row['walking'])
    assert (boxes == goals) == (row['result'] == 'solved')
    assert int(row['peak_memory_bytes']) <= 8 * 1024 * 1024
    if row['optimal'] == 'yes':
        assert row['controller'] == 'A*' and row['result'] == 'solved'


def main():
    count = 0
    for level in range(1, 9):
        optimal_pushes = None
        for controller in ('astar', 'greedy', 'random'):
            for pruning in (True, False):
                args = [f'--level={level}', f'--controller={controller}', '--seed=17', '--push-limit=80']
                if not pruning:
                    args.append('--no-pruning')
                first, second = run(*args)[0], run(*args)[0]
                assert {k: v for k, v in first.items() if k != 'elapsed_us'} == {
                    k: v for k, v in second.items() if k != 'elapsed_us'}
                replay(first)
                if controller in ('astar', 'greedy'):
                    if pruning:
                        assert first['result'] == 'solved', first
                    else:
                        assert first['result'] == 'solved' or (first['result'] == 'incomplete' and first['search_status'] == 'limit-reached'), first
                if controller == 'astar' and first['result'] == 'solved':
                    if optimal_pushes is None:
                        optimal_pushes = int(first['pushes'])
                    assert int(first['pushes']) == optimal_pushes
                    if level in (1, 2, 3, 6):
                        assert optimal_pushes == optimum(first['initial'])
                count += 1
    with tempfile.TemporaryDirectory(prefix='sokoban-replay-') as directory:
        path = Path(directory) / 'pack.xsb'
        path.write_text('; Dead corner\n######\n#$@ .#\n#    #\n######\n\n'
                        '; Delivery\n######\n#@ $.#\n######\n')
        for extra in ([], ['--no-pruning']):
            row = run(f'--file={path}', '--level=1', *extra)[0]
            replay(row)
            assert row['result'] == 'unsolvable'
            assert optimum(row['initial']) is None
        row = run(f'--file={path}', '--level=2')[0]
        replay(row)
        assert row['result'] == 'solved'
        path.write_text('invalid')
        assert subprocess.run([str(RUNNER), f'--file={path}'], capture_output=True).returncode == 2
    row = run('--level=6', '--time-limit=0.000000000001')[0]
    replay(row)
    assert row['result'] == 'incomplete' and row['search_status'] == 'limit-reached'
    for arg in ('--level=0', '--level=9', '--runs=0', '--seed=-1', '--controller=ollama',
                '--memory-limit=0', '--time-limit=nan', '--time-limit=-1', '--push-limit=0', '--unknown'):
        assert subprocess.run([str(RUNNER), arg], capture_output=True).returncode == 2
    print(f'Sokoban: {count} deterministic sessions replayed; A* optimality, pruning, imports and incomplete runs OK')


if __name__ == '__main__':
    main()
