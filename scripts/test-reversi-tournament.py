#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay Reversi CSV with an independent coordinate-based rules engine."""
import csv
import io
from pathlib import Path
import subprocess
runner=Path(__file__).resolve().parent.parent/'reversi/build/tournament'
DIRECTIONS=[(dr,dc)for dr in (-1,0,1)for dc in (-1,0,1)if dr or dc]
def flips(board,turn,square):
    if square in board:return set()
    result=set();r,c=square
    for dr,dc in DIRECTIONS:
        row,col=r+dr,c+dc;line=set()
        while 0<=row<8 and 0<=col<8 and board.get((row,col))==-turn:
            line.add((row,col));row+=dr;col+=dc
        if 0<=row<8 and 0<=col<8 and board.get((row,col))==turn:result|=line
    return result
def legal(board,turn):return {(r,c)for r in range(8)for c in range(8)if flips(board,turn,(r,c))}
seen_pass=False;seen_high=False
for first,second in [('random','random'),('positional','random'),('minimax','positional'),('mcts','minimax')]:
    args=[str(runner),'--games=4','--seed=0','--first='+first,'--second='+second,'--depth-first=2','--depth-second=2','--iterations-first=60','--iterations-second=60','--time-limit=0','--memory-limit=4']
    def records():return list(csv.DictReader(io.StringIO(subprocess.check_output(args,text=True))))
    a,b=records(),records();assert len(a)==4
    for index,(left,right) in enumerate(zip(a,b)):
        assert {k:v for k,v in left.items()if k!='elapsed_us'}=={k:v for k,v in right.items()if k!='elapsed_us'}
        assert int(left['seed'])==index
        board={(3,3):-1,(4,4):-1,(3,4):1,(4,3):1};turn=1;moves=passes=0
        for token in left['sequence'].split():
            if token=='pass':
                assert not legal(board,turn) and legal(board,-turn)
                passes+=1;seen_pass=True
            else:
                square=(int(token[1])-1,ord(token[0])-97);captured=flips(board,turn,square)
                assert captured and square not in board
                for point in captured|{square}:board[point]=turn
                moves+=1;seen_high|=square==(7,7)
            turn=-turn
        assert not legal(board,1) and not legal(board,-1)
        black=sum(p==1 for p in board.values());white=sum(p==-1 for p in board.values())
        assert moves==int(left['moves']) and passes==int(left['passes'])
        assert black==int(left['black']) and white==int(left['white']) and len(board)==moves+4
        assert left['result']==('black' if black>white else 'white' if white>black else 'draw')
        assert int(left['limited_decisions'])==0
        assert int(left['peak_memory_bytes'])<=4*1024*1024
    print(f'Reversi {first}/{second}: deterministic choices, independent flips/passes/results OK')
assert seen_pass and seen_high
for arg in ['--games=0','--seed=-1','--depth-first=0','--depth-second=11','--iterations-first=0','--iterations-second=1000001','--time-limit=nan','--time-limit=-1','--memory-limit=0','--memory-limit=4097','--first=ollama','--second=human','--unknown']:
    assert subprocess.run([str(runner),arg],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0
row=list(csv.DictReader(io.StringIO(subprocess.check_output([str(runner),'--games=1','--first=minimax','--time-limit=0.000000000001'],text=True))))[0]
assert row['result']=='ongoing' and row['moves']=='0' and row['status_first']=='limit-reached'
print('Reversi: invalid arguments and distinct interrupted-search result OK')
