#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check determinism and independently replay exported games through their first result."""
import csv
import io
from pathlib import Path
import subprocess


def outcome(board, rows, columns, length):
    for r in range(rows):
        for c in range(columns):
            value = board[r][c]
            if not value:
                continue
            for dr, dc in ((0, 1), (1, 0), (1, 1), (1, -1)):
                if all(0 <= r + i * dr < rows and 0 <= c + i * dc < columns
                       and board[r + i * dr][c + i * dc] == value for i in range(length)):
                    return 'first' if value == 1 else 'second'
    return 'draw' if all(all(row) for row in board) else None


root = Path(__file__).resolve().parent.parent
for game, rows, cols, length in (('tic-tac-toe', 3, 3, 3), ('four-in-a-row', 6, 7, 4)):
    executable = root / game / 'build/tournament'
    args = [str(executable), '--games=3', '--seed=17', '--first=positional',
            '--second=minimax', '--depth-second=2']
    first = subprocess.check_output(args, text=True)
    assert first == subprocess.check_output(args, text=True), game
    records = list(csv.DictReader(io.StringIO(first)))
    assert len(records) == 3
    for seed, record in enumerate(records, 17):
        assert int(record['seed']) == seed
        board = [[0] * cols for _ in range(rows)]
        sequence = [int(move) for move in record['sequence'].split()]
        assert len(sequence) == int(record['moves'])
        for turn, move in enumerate(sequence):
            assert outcome(board, rows, cols, length) is None
            if cols == 3:
                assert 1 <= move <= 9
                row, column = 2 - (move - 1) // 3, (move - 1) % 3
            else:
                assert 1 <= move <= 7
                column = move - 1
                empty = [r for r in range(rows) if board[r][column] == 0]
                assert empty
                row = max(empty)
            assert board[row][column] == 0
            board[row][column] = 1 if turn % 2 == 0 else -1
        assert outcome(board, rows, cols, length) == record['result']
    for invalid in ('--games=0', '--seed=-1', '--depth-first=0', '--first=human',
                    '--seed=2147483647', '--unknown'):
        assert subprocess.run([str(executable), invalid], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode != 0
    print(f'{game}: repeatable CSV, legal replay, results, and invalid arguments OK')
# Full-depth Tic-Tac-Toe must draw regardless of which opening is chosen.
rows = csv.DictReader(io.StringIO(subprocess.check_output([
    str(root / 'tic-tac-toe/build/tournament'), '--games=10', '--first=minimax',
    '--second=minimax', '--depth-first=9', '--depth-second=9'], text=True)))
assert all(row['result'] == 'draw' for row in rows)
print('Full-depth Tic-Tac-Toe: all 10 Minimax games drawn')
