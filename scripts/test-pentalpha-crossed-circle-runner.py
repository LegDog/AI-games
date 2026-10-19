#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay CSV using the agreed topology, independent of generated C tables."""
import csv
import io
from pathlib import Path
import subprocess
runner=Path(__file__).resolve().parent.parent/'pentalpha-crossed-circle/build/puzzle-runner'
paths=[
    ['v0','r1','i1','i2','i3','r7','v2'],
    ['v1','r5','i3','i4','i5','r11','v3'],
    ['v2','r9','i5','i6','i7','r15','v4'],
    ['v3','r13','i7','i8','i9','r19','v0'],
    ['v4','r17','i9','i0','i1','r3','v1'],
    ['r0','i0','c','i5','r10'],
    ['r2','i1','c','i6','r12'],
    ['r4','i2','c','i7','r14'],
    ['r6','i3','c','i8','r16'],
    ['r8','i4','c','i9','r18'],
    [f'r{k}' for k in range(20)],
]
triples=set()
for number,path in enumerate(paths,1):
    if number==11:path=path+path[:2]
    for start in range(len(path)-2):
        move=tuple(path[start:start+3]);triples|={(move,number),(move[::-1],number)}
assert len(triples)==120
for controller in ['random','heuristic','search','connectivity','spanning-forest','gf2-forest','search-symmetry']:
    args=[str(runner),'--runs=4','--seed=17',f'--controller={controller}','--depth=3','--memory-limit=16']
    def records():return list(csv.DictReader(io.StringIO(subprocess.check_output(args,text=True))))
    first,second=records(),records();assert len(first)==4
    for index,(a,b) in enumerate(zip(first,second)):
        assert int(a['seed'])==17+index
        assert {k:v for k,v in a.items() if k!='elapsed_us'}=={k:v for k,v in b.items() if k!='elapsed_us'}
        occupied=set();steps=a['sequence'].split()
        for step in steps:
            move,path=step.split('@p');move=tuple(move.split('>'))
            assert (move,int(path)) in triples
            assert move[0] not in occupied and move[2] not in occupied
            occupied.add(move[2])
        assert len(occupied)==int(a['pieces'])==len(steps)
        legal=[m for m,p in triples if m[0] not in occupied and m[2] not in occupied]
        assert len(occupied)==35 or not legal
        assert a['result']==('solved' if len(occupied)==35 else 'stuck')
        if controller in ('search','search-symmetry'):
            assert a['search_status']=='complete'
            assert int(a['limited_decisions'])==0
            assert int(a['completed_depth_min'])>=1
            assert int(a['peak_memory_bytes'])<=16*1024*1024
        if controller in ('connectivity','spanning-forest','gf2-forest','search-symmetry'):
            assert a['proof_status']=='proven' and int(a['proven_max_initial'])==34
            assert int(a['empty_components'])>=2
            assert a['optimum_reached']==('yes' if len(occupied)==34 else 'no')
        else:
            assert a['proof_status']=='not-computed' and a['proven_max_initial']=='-1'
        if controller in ('connectivity','spanning-forest','gf2-forest'):
            assert len(occupied)==34 and a['result']=='stuck' and a['optimum_reached']=='yes'
            assert int(a['empty_components'])==2
            assert int(a['peak_memory_bytes'])<=1024*1024
        if controller=='gf2-forest':
            assert a['matrix_rank_initial']=='34' and int(a['gf2_xor_operations'])>0
        if controller=='search-symmetry':assert int(a['sibling_prunes'])>0
    print(f'Crossed Circle {controller}: deterministic legal replay and results OK')
for arg in ['--runs=0','--seed=-1','--depth=0','--depth=36','--memory-limit=0','--memory-limit=4097','--time-limit=-1','--time-limit=nan','--time-limit=inf','--controller=ollama','--unknown']:
    assert subprocess.run([str(runner),arg],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode!=0
row=list(csv.DictReader(io.StringIO(subprocess.check_output([str(runner),'--runs=1','--time-limit=0.000000000001'],text=True))))[0]
assert row['result']=='ongoing' and row['search_status']=='limit-reached' and row['pieces']=='0'
print('Crossed Circle: invalid arguments and interrupted search OK')

# Independent connectivity calculation from the documented directed triples.
points={p for triple,path in triples for p in triple}
adj={p:set() for p in points}
for (a,middle,b),path in triples:adj[a].add(b)
unseen=set(points);components=[]
while unseen:
    todo=[min(unseen)];component=set(todo)
    while todo:
        for neighbor in adj[todo.pop()]-component:component.add(neighbor);todo.append(neighbor)
    unseen-=component;components.append(component)
assert sorted(map(len,components))==[11,25]
assert len(points)-len(components)==34
# With identical completed levels, symmetry pruning must retain the baseline choices.
def sequences(controller):
    args=[str(runner),'--runs=5','--seed=17','--depth=3','--memory-limit=16','--controller='+controller]
    return list(csv.DictReader(io.StringIO(subprocess.check_output(args,text=True))))
base,variant=sequences('search'),sequences('search-symmetry')
assert [r['sequence'] for r in base]==[r['sequence'] for r in variant]
assert sum(int(r['nodes'])for r in variant)<sum(int(r['nodes'])for r in base)
print('Independent two-component bound and baseline/symmetry seeded equivalence OK')

# Digests captured before adding variations: these do not contain playable plans.
import hashlib
import json
legacy_keys=['seed', 'controller', 'depth', 'result', 'pieces', 'sequence', 'nodes', 'completed_depth_min', 'completed_depth_last', 'search_status', 'limited_decisions']
legacy_expected={'random': 'ce677bbd1cb03be1cde5cd776d26e3e3bf78eccab67207b8d6780cf9838f6079', 'heuristic': '7fb47e59c6a3cac0aa424d42e88f30c026027a6844b7154b5918b3567bf58466', 'search': 'f3eb096e305379452adf5592f15120aa537a5c5d4207088115c22426884f0c79'}
for controller,digest in legacy_expected.items():
    records=sequences(controller)
    payload=json.dumps([{k:r[k]for k in legacy_keys}for r in records],sort_keys=True,separators=(',',':')).encode()
    assert hashlib.sha256(payload).hexdigest()==digest,controller+' legacy decisions/evaluations changed'
print('Pre-variation baseline digests for Random, Heuristic, and Search unchanged')
