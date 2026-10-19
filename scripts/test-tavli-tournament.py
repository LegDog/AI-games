#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay exported Tavli sessions with an independent array-based rules engine."""
import copy
import csv
import io
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / 'tavli/build/tournament'
if not RUNNER.exists():
    RUNNER = ROOT / 'tavli/build-tests/tournament'


class Board:
    def __init__(self, variant):
        self.variant = variant
        self.points = [[0, 0] for _ in range(24)]
        self.pinned = [-1] * 24
        self.reserve, self.bar, self.off = [0, 0], [0, 0], [0, 0]
        self.escaped = [False, False]
        self.active = 0
        self.remaining = []
        self.winner = None
        self.mother = False
        for p in (0, 1):
            if variant == 'Running':
                self.reserve[p] = 15
            elif variant in ('Plakoto', 'Fevga'):
                self.points[self.route(p)[0]][p] = 15
            else:
                for i, count in ((0, 2), (11, 5), (16, 3), (18, 5)):
                    self.points[self.route(p)[i]][p] = count

    def clone(self):
        b = copy.copy(self)
        for name in ('pinned', 'reserve', 'bar', 'off', 'escaped', 'remaining'):
            setattr(b, name, getattr(self, name).copy())
        b.points = [row.copy() for row in self.points]
        return b

    def route(self, p):
        if not p:
            return list(range(23, -1, -1))
        if self.variant == 'Fevga':
            return list(range(11, -1, -1)) + list(range(23, 11, -1))
        return list(range(24))

    def roll(self, a, b):
        self.remaining = sorted([a, b] * (2 if a == b and self.variant != 'Running' else 1))

    def trapped(self, owner):
        opponent = 1 - owner
        occupied = [q for q in range(24) if self.points[q][opponent]]
        if len(occupied) != 1 or self.reserve[opponent] or self.bar[opponent]:
            return False
        index = self.route(opponent).index(occupied[0])
        return index + 6 < 24 and all(self.points[q][owner] for q in self.route(opponent)[index + 1:index + 7])

    def move(self, source, die):
        if die not in self.remaining or self.winner is not None:
            return None
        p, o = self.active, 1 - self.active
        route = self.route(p)
        if self.bar[p] and source != 25:
            return None
        if source == 24:
            if self.variant != 'Running' or not self.reserve[p]:
                return None
            index = -1
        elif source == 25:
            if not self.bar[p] or self.variant not in ('Portes', 'Backgammon'):
                return None
            index = -1
        else:
            if not self.points[source][p] or self.pinned[source] == p:
                return None
            index = route.index(source)
            if self.variant == 'Fevga' and not self.escaped[p] and index == 0 and self.points[source][p] != 15:
                return None
        destination = index + die
        if destination >= 24:
            if index < 0:
                return None
            if self.variant == 'Running':
                if destination != 24:
                    return None
            else:
                if self.bar[p] or self.reserve[p] or any(self.points[q][p] for q in route[:18]):
                    return None
                if destination > 24 and any(self.points[q][p] for q in route[:index]):
                    return None
            target = 26
        else:
            target = route[destination]
            if self.points[target][o] and (self.variant in ('Running', 'Fevga') or self.points[target][o] >= 2 or self.pinned[target] == p):
                return None
        b = self.clone()
        if source == 24:
            b.reserve[p] -= 1
        elif source == 25:
            b.bar[p] -= 1
        else:
            b.points[source][p] -= 1
            if b.points[source][p] == 0 and b.pinned[source] == o:
                b.pinned[source] = -1
        if target == 26:
            b.off[p] += 1
        else:
            if b.points[target][o] and not b.points[target][p]:
                if self.variant == 'Plakoto':
                    b.pinned[target] = o
                else:
                    b.points[target][o] = 0
                    b.bar[o] += 1
            b.points[target][p] += 1
        if self.variant == 'Fevga':
            b.escaped[p] |= destination > 12
            if all(b.points[q][p] for q in route[:6]):
                return None
        b.remaining.remove(die)
        if self.variant == 'Plakoto':
            mothers = [b.points[b.route(k)[0]][k] == 1 and b.pinned[b.route(k)[0]] == k for k in (0, 1)]
            if all(mothers):
                b.winner = -1
            else:
                for k in (0, 1):
                    if mothers[k] and b.points[b.route(1 - k)[0]][1 - k] == 0:
                        b.winner = 1 - k
                        b.mother = True
        for k in (0, 1):
            if b.off[k] == 15:
                b.winner = k
        return (source, target, die), b

    def legal_sequences(self):
        leaves = []
        def visit(b, prefix):
            children = [] if b.winner is not None else [move for d in sorted(set(b.remaining)) for source in range(26) if (move := b.move(source, d))]
            if not children:
                leaves.append((prefix, self.variant != 'Fevga' or not b.trapped(self.active), b.winner is not None))
            for step, child in children:
                visit(child, prefix + (step,))
        visit(self, ())
        releases = any(release for _, release, _ in leaves)
        eligible = [(seq, terminal) for seq, release, terminal in leaves if release or not releases]
        maximum = max(len(seq) for seq, _ in eligible)
        highest = max((seq[0][2] for seq, _ in eligible if len(seq) == 1), default=0)
        return {seq for seq, terminal in eligible if (len(seq) == maximum or terminal) and (maximum != 1 or seq[0][2] == highest)}

    def award(self, cube):
        if self.winner == -1:
            return 0
        if self.variant == 'Running':
            return cube
        p, o = self.winner, 1 - self.winner
        base = 2 if self.mother or self.off[o] == 0 else 1
        if self.variant == 'Backgammon' and self.off[o] == 0 and (self.bar[o] or any(self.points[q][o] for q in self.route(p)[18:])):
            base = 3
        return base * cube

    def valid(self):
        for p in (0, 1):
            assert self.reserve[p] + self.bar[p] + self.off[p] + sum(row[p] for row in self.points) == 15
        for q, row in enumerate(self.points):
            if all(row):
                assert self.variant == 'Plakoto' and self.pinned[q] in (0, 1) and row[self.pinned[q]] == 1
            else:
                assert self.pinned[q] == -1


def replay(row):
    scores, games, turns = [0, 0], 0, 0
    variant = 'Portes' if row['session'] == 'traditional' else row['variant']
    target, previous, crawford_used, winner = int(row['target']), None, False, None
    board, phase, cube, owner, actor, crawford = None, 'finished', 1, None, 0, False
    for event in row['sequence'].split(';'):
        if event.startswith('B:'):
            assert phase == 'finished', event
            if games and winner is not None and winner >= 0 and row['session'] == 'traditional':
                variant = {'Portes': 'Plakoto', 'Plakoto': 'Fevga', 'Fevga': 'Portes'}[variant]
            assert event[2:] == variant
            board = Board(variant)
            games += 1
            phase = 'pre' if games > 1 and previous is not None and previous >= 0 and variant not in ('Running', 'Backgammon') else 'opening'
            board.active = previous if phase == 'pre' else 0
            cube, owner, actor = 1, None, board.active
            crawford = variant == 'Backgammon' and row['session'] != 'single' and not crawford_used and target - 1 in scores
            crawford_used |= crawford
            winner = None
        elif event.startswith('O:'):
            assert phase == 'opening'
            a, b = map(int, event[2:].split('-'))
            assert 1 <= a <= 6 and 1 <= b <= 6
            if a != b:
                board.active = actor = 0 if a > b else 1
                phase = 'play' if variant == 'Backgammon' else 'pre'
                if phase == 'play':
                    board.roll(a, b)
        elif event.startswith('R:'):
            assert phase == 'pre'
            a, b = map(int, event[2:].split('-'))
            assert 1 <= a <= 6 and 1 <= b <= 6
            board.roll(a, b)
            phase = 'play'
        elif event == 'D':
            assert phase == 'pre' and variant == 'Backgammon' and row['session'] != 'single' and not crawford
            assert owner in (None, board.active) and scores[board.active] + cube < target
            phase, actor = 'response', 1 - board.active
        elif event == 'T':
            assert phase == 'response'
            owner, cube, phase, actor = actor, cube * 2, 'pre', board.active
        elif event == 'X':
            assert phase == 'response'
            winner = previous = board.active
            scores[winner] += cube
            phase = 'finished'
        elif event.startswith('M:'):
            assert phase == 'play'
            def point(text):
                return {'reserve': 24, 'bar': 25, 'off': 26}.get(text, int(text) - 1 if text.isdigit() else -1)
            selected = []
            if event[2:] != 'pass':
                for text in event[2:].split('+'):
                    source, tail = text.split('>')
                    dest, die = tail.split('/')
                    selected.append((point(source), point(dest), int(die)))
            assert tuple(selected) in board.legal_sequences(), (row['seed'], variant, event)
            for source, target_point, die in selected:
                step, board = board.move(source, die)
                assert step == (source, target_point, die)
                board.valid()
            turns += 1
            if board.winner is None:
                board.active = actor = 1 - board.active
                phase = 'pre'
            else:
                winner = previous = board.winner
                if winner >= 0:
                    scores[winner] += board.award(cube)
                phase = 'finished'
        else:
            raise AssertionError(event)
        board.valid()
    assert scores == [int(row['score_first']), int(row['score_second'])]
    assert games == int(row['games']) and turns == int(row['turns'])
    done = phase == 'finished' and (row['session'] == 'single' or max(scores) >= target)
    assert (row['result'] == 'completed') == done
    assert row['winner'] == ('first' if winner == 0 else 'second' if winner == 1 else 'draw') if done else row['winner'] == 'none'
    assert int(row['peak_memory_bytes']) <= 64 * 1024 * 1024


def run(*args):
    result = subprocess.run([str(RUNNER), *args], check=True, capture_output=True, text=True, timeout=90)
    return list(csv.DictReader(io.StringIO(result.stdout)))


def main():
    count = 0
    for variant in ('portes', 'plakoto', 'fevga', 'backgammon', 'running'):
        args = (f'--variant={variant}', '--games=3', '--seed=13', '--first=heuristic', '--second=random', '--time-limit=0')
        a, b = run(*args), run(*args)
        for first, second in zip(a, b):
            assert {k: v for k, v in first.items() if k != 'elapsed_us'} == {k: v for k, v in second.items() if k != 'elapsed_us'}
            replay(first)
            assert first['result'] == 'completed'
            count += 1
    for args in (('--variant=backgammon', '--session=fixed'), ('--session=traditional',), ('--variant=running', '--session=fixed')):
        for row in run(*args, '--games=2', '--target=3', '--first=random', '--second=heuristic', '--time-limit=0'):
            replay(row)
            assert row['result'] == 'completed'
            count += 1
    for row in run('--variant=running', '--first=expectiminimax', '--depth-first=1', '--second=mcts', '--iterations-second=8', '--games=1', '--turn-limit=12', '--time-limit=0'):
        replay(row)
        count += 1
    for row in run('--games=1', '--turn-limit=1', '--time-limit=0'):
        replay(row)
        assert row['result'] == 'incomplete'
        count += 1
    dice_args = ('--variant=running', '--seed=77', '--games=1', '--turn-limit=6', '--second=random', '--time-limit=0')
    first = run(*dice_args, '--first=heuristic')[0]
    second = run(*dice_args, '--first=mcts', '--iterations-first=3')[0]
    rolls = lambda row: [event for event in row['sequence'].split(';') if event.startswith(('O:', 'R:'))]
    assert rolls(first) == rolls(second), 'Search consumed the real dice stream'
    for arg in ('--variant=unknown', '--session=unknown', '--games=0', '--target=0', '--target=26', '--seed=-1', '--time-limit=nan', '--memory-limit=0', '--depth-first=7', '--iterations-second=0'):
        assert subprocess.run([str(RUNNER), arg], capture_output=True).returncode == 2
    for row in run('--variant=running', '--games=1', '--first=expectiminimax', '--second=expectiminimax', '--time-limit=0.000000000001'):
        replay(row)
        assert row['result'] == 'incomplete' and int(row['limited_decisions']) > 0
    print(f'Tavli: {count} sessions independently replayed; five variants, match rotation, cube, legal dice use, deterministic seeds and incomplete runs OK')


if __name__ == '__main__':
    main()
