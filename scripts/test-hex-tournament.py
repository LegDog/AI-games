#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent coordinate-array Hex replay, including pie-rule reflection."""
import csv
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / 'hex/build/tournament'
if not RUNNER.exists():
    RUNNER = ROOT / 'hex/build-tests/tournament'


def run(*args):
    result = subprocess.run([str(RUNNER), *args], text=True, capture_output=True, check=True)
    return list(csv.DictReader(io.StringIO(result.stdout)))


def connects(board, color):
    n = len(board)
    todo = [(0, c) for c in range(n) if board[0][c] == color] if color == 'red' else [
        (r, 0) for r in range(n) if board[r][0] == color]
    seen = set(todo)
    while todo:
        r, c = todo.pop()
        if (r if color == 'red' else c) == n - 1:
            return True
        for dr, dc in ((-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0)):
            nr, nc = r + dr, c + dc
            if 0 <= nr < n and 0 <= nc < n and (nr, nc) not in seen and board[nr][nc] == color:
                seen.add((nr, nc))
                todo.append((nr, nc))
    return False


def replay(row):
    n = int(row['size'])
    board = [[None for _ in range(n)] for _ in range(n)]
    tokens = row['sequence'].split()
    placements = 0
    swapped = False
    for action, token in enumerate(tokens):
        assert not connects(board, 'red') and not connects(board, 'blue'), row
        color = 'red' if action % 2 == 0 else 'blue'
        if token == 'swap':
            assert action == 1 and row['swap_enabled'] == '1', row
            r, c = next((r, c) for r in range(n) for c in range(n) if board[r][c] == 'red')
            board[r][c] = None
            board[c][r] = 'blue'
            swapped = True
        else:
            c, r = ord(token[0]) - ord('a'), int(token[1:]) - 1
            assert 0 <= r < n and 0 <= c < n and board[r][c] is None, row
            board[r][c] = color
            placements += 1
    winners = [color for color in ('red', 'blue') if connects(board, color)]
    assert winners == ([] if row['result'] == 'ongoing' else [row['result']]), row
    assert int(row['moves']) == placements and int(row['actions']) == len(tokens), row
    assert int(row['swapped']) == swapped, row
    for color in ('red', 'blue'):
        assert int(row[color]) == sum(cell == color for line in board for cell in line), row
    assert int(row['peak_memory_bytes']) <= 64 * 1024 * 1024, row


def main():
    tested = swaps = 0
    for size in (3, 5, 7):
        for first, second in (('random', 'random'), ('heuristic', 'random'), ('minimax', 'mcts')):
            args = ('--games=8', '--seed=10', f'--size={size}', f'--first={first}',
                    f'--second={second}', '--depth-first=2', '--iterations-second=40', '--time-limit=0')
            rows = run(*args)
            again = run(*args)
            for a, b in zip(rows, again):
                assert {k: v for k, v in a.items() if k != 'elapsed_us'} == {
                    k: v for k, v in b.items() if k != 'elapsed_us'}
                replay(a)
                assert a['result'] in ('red', 'blue')
                tested += 1
                swaps += int(a['swapped'])
    assert swaps, 'No swap replayed'
    for row in run('--games=10', '--size=3', '--no-swap', '--first=random', '--second=random'):
        replay(row)
        assert row['swapped'] == '0' and 'swap' not in row['sequence']
    for row in run('--games=1', '--first=minimax', '--time-limit=0.000000000001'):
        replay(row)
        assert row['result'] == 'ongoing' and row['status_first'] == 'limit-reached'
    for arg in ('--size=4', '--games=0', '--depth-first=0', '--first=unknown', '--time-limit=nan',
                '--iterations-second=0', '--memory-limit=0', '--seed=-1'):
        assert subprocess.run([str(RUNNER), arg], capture_output=True).returncode == 2
    print(f'Hex: {tested + 11} independently replayed games; {swaps} swaps; deterministic seeds and invalid arguments OK')


if __name__ == '__main__':
    main()
