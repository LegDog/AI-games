#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent coordinate-array Go-Moku replay and search comparison."""
import csv
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / 'gomoku/build/tournament'
if not RUNNER.exists():
    RUNNER = ROOT / 'gomoku/build-tests/tournament'


def run(*args):
    result = subprocess.run([str(RUNNER), *args], text=True, capture_output=True, check=True)
    return list(csv.DictReader(io.StringIO(result.stdout)))


def wins(board, color):
    for r in range(15):
        for c in range(15):
            for dr, dc in ((0, 1), (1, 0), (1, 1), (1, -1)):
                cells = [(r + dr * i, c + dc * i) for i in range(5)]
                if all(0 <= y < 15 and 0 <= x < 15 and board[y][x] == color for y, x in cells):
                    return True
    return False


def outcome(board):
    colors = [color for color in ('black', 'white') if wins(board, color)]
    if len(colors) == 2:
        return 'draw'
    if colors:
        return colors[0]
    return 'draw' if all(cell for row in board for cell in row) else 'incomplete'


def replay(row):
    board = [[None] * 15 for _ in range(15)]
    tokens = row['sequence'].split()
    for turn, token in enumerate(tokens):
        assert outcome(board) == 'incomplete', row
        color = 'black' if turn % 2 == 0 else 'white'
        coordinate = token
        c, r = ord(coordinate[0]) - ord('a'), int(coordinate[1:]) - 1
        assert 0 <= c < 15 and 0 <= r < 15 and board[r][c] is None, row
        board[r][c] = color
    assert outcome(board) == row['result'], row
    assert len(tokens) == int(row['actions']), row
    for color in ('black', 'white'):
        assert int(row[color]) == sum(cell == color for line in board for cell in line), row
    assert int(row['black']) + int(row['white']) == len(tokens), row
    assert int(row['peak_memory_bytes']) <= max(int(row['memory_first']), int(row['memory_second'])) * 1024**2


def stable(rows):
    return [{k: v for k, v in row.items() if k != 'elapsed_us'} for row in rows]


def main():
    tested = 0
    search_sequences = []
    for first, second in (('random', 'random'), ('heuristic', 'random'), ('minimax', 'alpha-beta'), ('alpha-beta', 'minimax')):
        args = ('--games=2', '--seed=10', f'--first={first}', f'--second={second}',
                '--depth-first=1', '--depth-second=1', '--time-first=0', '--time-second=0')
        rows = run(*args)
        if first in ('minimax', 'alpha-beta'):
            search_sequences.append([(r['sequence'], r['result']) for r in rows])
        assert stable(rows) == stable(run(*args))
        for row in rows:
            replay(row)
            assert row['result'] != 'incomplete'
            tested += 1
    assert search_sequences[0] == search_sequences[1]
    for mode in ('minimax', 'alpha-beta'):
        row = run('--games=1', f'--first={mode}', '--node-limit=1', '--time-first=0')[0]
        replay(row)
        assert row['result'] == 'incomplete' and row['actions'] == '0' and row['status_first'] == 'limit-reached'
        tested += 1
        row = run('--games=1', f'--first={mode}', '--second=random', '--depth-first=3', '--node-limit=290', '--time-first=0')[0]
        replay(row)
        assert int(row['limited_decisions']) > 0 and int(row['completed_first']) >= 1
        tested += 1
    for arg in ('--games=0', '--depth-first=0', '--depth-second=7', '--first=unknown', '--first=ollama',
                '--time-first=nan', '--time-second=-1', '--memory-first=0', '--memory-second=4097', '--seed=-1', '--node-limit=-1', 'extra'):
        assert subprocess.run([str(RUNNER), arg], capture_output=True).returncode == 2, arg
    print(f'Go-Moku: {tested} independently replayed games; reproducible seeds, equal-depth searches, limits and invalid inputs OK')


if __name__ == '__main__':
    main()
