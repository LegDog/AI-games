#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent 9x9 Go captures, positional superko and area-score replay."""
import csv
import io
from pathlib import Path
import subprocess

RUNNER = Path(__file__).resolve().parent.parent / 'go/build/tournament'
COLS = 'ABCDEFGHJ'


def adjacent(point):
    x, y = point % 9, point // 9
    return [yy * 9 + xx for xx, yy in ((x-1, y), (x+1, y), (x, y-1), (x, y+1))
            if 0 <= xx < 9 and 0 <= yy < 9]


def group(board, point):
    stones, liberties, pending = {point}, set(), [point]
    while pending:
        for other in adjacent(pending.pop()):
            if board[other] == -1:
                liberties.add(other)
            elif board[other] == board[point] and other not in stones:
                stones.add(other)
                pending.append(other)
    return stones, liberties


def score(board, komi):
    stones = [board.count(0), board.count(1)]
    territory, neutral, visited = [0, 0], 0, set()
    for point in range(81):
        if board[point] != -1 or point in visited:
            continue
        region, border, pending = {point}, set(), [point]
        visited.add(point)
        while pending:
            for other in adjacent(pending.pop()):
                if board[other] != -1:
                    border.add(board[other])
                elif other not in visited:
                    visited.add(other)
                    region.add(other)
                    pending.append(other)
        if len(border) == 1:
            territory[next(iter(border))] += len(region)
        else:
            neutral += len(region)
    area = [stones[0] + territory[0], stones[1] + territory[1] + komi]
    return stones, territory, neutral, area


def replay(row):
    board, history, captures = [-1] * 81, {(-1,) * 81}, [0, 0]
    passes = placements = consecutive = 0
    actions = row['sequence'].split()
    for index, action in enumerate(actions):
        assert consecutive < 2, row
        color = index % 2
        if action == 'pass':
            passes += 1
            consecutive += 1
            continue
        assert len(action) == 2 and action[0] in COLS and action[1] in '123456789'
        point = (int(action[1])-1)*9 + COLS.index(action[0])
        assert board[point] == -1, row
        board[point] = color
        for other in adjacent(point):
            if board[other] == 1-color:
                stones, liberties = group(board, other)
                if not liberties:
                    captures[color] += len(stones)
                    for cell in stones:
                        board[cell] = -1
        assert group(board, point)[1], ('suicide', row)
        pattern = tuple(board)
        assert pattern not in history, ('superko', row)
        history.add(pattern)
        placements += 1
        consecutive = 0
    stones, territory, neutral, area = score(board, float(row['komi']))
    for color, name in enumerate(('black', 'white')):
        assert int(row[f'{name}_captures']) == captures[color]
        assert int(row[f'{name}_stones']) == stones[color]
        assert int(row[f'{name}_territory']) == territory[color]
        assert float(row[f'{name}_score']) == area[color]
    assert int(row['neutral']) == neutral
    assert int(row['actions']) == len(actions)
    assert int(row['placements']) == placements and int(row['passes']) == passes
    assert float(row['margin']) == area[0] - area[1]
    finished = consecutive == 2
    assert row['result'] == ('completed' if finished else 'incomplete')
    winner = 'black' if area[0] > area[1] else 'white' if area[1] > area[0] else 'draw'
    assert row['winner'] == (winner if finished else 'none')
    assert int(row['peak_memory_bytes']) <= int(row['memory_limit_bytes'])
    assert row['rules'] == 'chinese-style-area-no-suicide-positional-superko'
    assert row['size'] == '9'


def run(*args):
    return list(csv.DictReader(io.StringIO(subprocess.check_output(
        [str(RUNNER), *args], text=True, timeout=40))))


def main():
    count = 0
    for first, second in (('random', 'random'), ('heuristic', 'random'), ('random', 'heuristic'), ('heuristic', 'heuristic')):
        args = (f'--first={first}', f'--second={second}', '--games=3', '--seed=13', '--time-limit=0')
        a, b = run(*args), run(*args)
        for row, repeat in zip(a, b):
            replay(row)
            assert {k: v for k, v in row.items() if k != 'elapsed_us'} == {
                k: v for k, v in repeat.items() if k != 'elapsed_us'}
            count += 1
    for first, second in (('mcts', 'heuristic'), ('random', 'mcts'), ('mcts', 'mcts')):
        args = (f'--first={first}', f'--second={second}', '--iterations-first=3', '--iterations-second=3',
                '--action-limit=12', '--time-limit=0', '--seed=2')
        a, b = run(*args)[0], run(*args)[0]
        replay(a)
        assert {k: v for k, v in a.items() if k != 'elapsed_us'} == {
            k: v for k, v in b.items() if k != 'elapsed_us'}
        count += 1
    for komi in ('0', '6.5', '20'):
        row = run(f'--komi={komi}', '--action-limit=1')[0]
        replay(row)
        assert row['result'] == 'incomplete' and row['winner'] == 'none'
        count += 1
    cutoff = run('--first=mcts', '--iterations-first=1', '--seed=21', '--action-limit=1', '--time-limit=0')[0]
    replay(cutoff)
    assert int(cutoff['cutoffs']) == 1 and int(cutoff['nodes']) == 324 and cutoff['winner'] == 'none'
    count += 1
    row = run('--first=mcts', '--time-limit=0.000000000001')[0]
    replay(row)
    assert row['result'] == 'incomplete' and int(row['limited_decisions']) == 1
    for arg in ('--size=13', '--games=0', '--seed=-1', '--first=unknown', '--komi=7.25',
                '--komi=nan', '--komi=-1', '--komi=21', '--iterations-first=0', '--iterations-second=1000001',
                '--memory-limit=0', '--time-limit=-1', '--action-limit=0', '--unknown'):
        assert subprocess.run([str(RUNNER), arg], capture_output=True).returncode == 2
    print(f'Go: {count} deterministic sessions replayed; captures, superko, passes, scores, limits and komi OK')


if __name__ == '__main__':
    main()
