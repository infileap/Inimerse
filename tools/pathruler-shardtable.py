#!/usr/bin/env python3
"""pathruler-shardtable — the SECOND half of the question.

pathruler.py answers "which shard is held at this line".  This asks the next
question, which a held/not-held ruler cannot ask at all:

    the shard held here -- is it the shard of the slot this line WRITES?

A shard protects vm->globals[i] by i mod VM_GLOBAL_SHARDS, so the shard of a slot
is a function of its INDEX.  A line that writes vm->globals[i] while holding the
shard of some OTHER expression is writing outside its own lock -- and every
held/not-held ruler in this repository reports that line as "guarded".

  pathruler-shardtable.py --ref <sha> --sites <file>

Output columns: site, owning function, shard held, slot expression written on
that line, and the match verdict.  The verdict is mechanical; whether a mismatch
is EXPLOITABLE is a separate question and is not answered here.

Boundary premises this run stands on (P1, P2) are printed as part of the output,
because a run whose premises are only in the README is a run whose premises
travel nowhere.
"""
import re,sys,argparse
sys.path.insert(0,".")
import pathruler as P

SLOT=re.compile(r'vm->globals\s*\[\s*([^\]]+?)\s*\]')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--ref',required=True); ap.add_argument('--sites',required=True)
    a=ap.parse_args()
    P.print_premises()
    cache={}; rows=[]
    for raw in open(a.sites):
        raw=raw.strip()
        if not raw or raw.startswith('#'): continue
        path,l=raw.rsplit(':',1); l=int(l)
        if path not in cache:
            L=P.file_lines(a.ref,path); cache[path]=L
        L=cache[path]
        owner=None;best=None
        for nm,x,y in P.all_functions(L):
            if x<=l<=y and (best is None or (y-x)<best): best=y-x;owner=nm
        IN,_=P.analyse(L,owner)
        nid=P.NID.get(l) or P.HDR.get(l)
        st=IN.get(nid,set()) if nid else set()
        keys=sorted({k for s in st for k in s})
        txt=(L[l-1] if l-1<len(L) else '')
        rows.append((raw,owner,keys,SLOT.search(txt),txt.strip()))
    print(f"{'site':26s} {'owning function':22s} {'holds':16s} {'slot written':14s} verdict")
    for site,fn,keys,m,txt in rows:
        k='ALL' if keys==['ALL'] else (','.join(keys) if keys else '-')
        slot=m.group(1) if m else '-'
        if not keys:            v='(no shard held)'
        elif keys==['ALL']:     v='ALL -> yes'
        elif m and slot in keys:v='yes'
        elif not m:             v='n/a (writes no slot)'
        else:                   v='*** NO ***'
        print(f"{site:26s} {str(fn):22s} {k:16s} {slot:14s} {v}")
    mis=[r for r in rows if r[2] and r[2]!=['ALL'] and r[3] and r[3].group(1) not in r[2]]
    allsh=[r for r in rows if r[2]==['ALL']]
    print()
    print(f"sites read                : {len(rows)}")
    print(f"holds ALL shards          : {len(allsh)} sites in "
          f"{len({r[1] for r in allsh})} function(s) {sorted({r[1] for r in allsh})}")
    print(f"shard held != slot's shard: {len(mis)} sites {[r[0] for r in mis]}")

if __name__=='__main__': main()
