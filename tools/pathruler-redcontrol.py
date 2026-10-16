#!/usr/bin/env python3
"""red controls for pathruler — mutate the source IN MEMORY and check that the
verdict moves for the right reason, and does not move for the wrong one.

Nothing here is used by the ruler itself.  The ruler contains no coordinates;
this file does, because an experiment must name what it mutates.  They are
FOUND by content inside a named label region, not written down as constants.

  RC1  kill one of the two same-level shard locks guarding the probe  -> 持分片
  RC2  kill the other one                                            -> 持分片
  RC3  kill BOTH                                                     -> 无分片
  RC4  kill the early-exit unlock on a DIFFERENT path                -> 持分片
  RC5  append a comment to an unrelated line (negative control)      -> 持分片

RC1+RC2 are the point: the probe has TWO same-level shard locks on its path,
and a line-scanning ruler remembers only the last one, so it cannot tell
"one of the two" from "one".
RC4 is the discriminating row: a line-scanning ruler reads the early-exit
unlock as "the nearest lock event" and answers 无分片, which is wrong --
that unlock is the END OF A DIFFERENT PATH.
"""
import sys,os,argparse
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import pathruler as P

def region(lines,label):
    """[first,last] line numbers of the `label: { ... }` region, found by content"""
    start=None
    for i,l in enumerate(lines,1):
        if l.strip().startswith(label+':'): start=i; break
    if start is None: raise SystemExit("label not found: "+label)
    d=0; seen=False
    for k in range(start,len(lines)+1):
        d+=lines[k-1].count('{')-lines[k-1].count('}')
        if '{' in lines[k-1]: seen=True
        if seen and d<=0: return start,k
    return start,len(lines)

def find(lines,lo,hi,pred):
    return [i for i,l in enumerate(lines[lo-1:hi],1) if pred(l) and (i+lo-1)]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--ref',default="6642fed0c8fe47d7107e6822db7da77247eccea7")
    ap.add_argument('--path',default="src/vm/vm.c")
    ap.add_argument('--fn',default="vm_execute_thread")
    ap.add_argument('--label',default="L_STORE_GLOBAL")
    ap.add_argument('--probe-slot',default="globals[idx].val")
    a=ap.parse_args()

    L=P.file_lines(a.ref,a.path)
    lo,hi=region(L,a.label)
    probe=[i for i in range(lo,hi+1)
           if a.probe_slot in L[i-1] and 'value_free' not in L[i-1] and 'im_mutex' not in L[i-1]]
    TARGET=probe[0]
    takes=[i for i in range(lo,hi+1) if 'im_mutex_lock' in L[i-1] and 'VM_GSHARD' in L[i-1]]
    drops=[i for i in range(lo,hi+1) if 'im_mutex_unlock' in L[i-1] and 'VM_GSHARD' in L[i-1]]
    early=[d for d in drops if d<TARGET
           and any('continue' in L[j-1] for j in range(d+1,min(d+5,hi)+1))]
    unrelated=[i for i in range(lo+1,TARGET)
               if 'im_mutex' not in L[i-1] and L[i-1].strip().endswith(';')][0]

    print(f"ref {a.ref[:8]}  {a.path}  fn={a.fn}  region {a.label} [{lo}-{hi}]")
    print(f"  probe line          : {TARGET}   {L[TARGET-1].strip()}")
    print(f"  same-level takes    : {takes}")
    print(f"  drops               : {drops}")
    print(f"  early-exit unlock   : {early}")
    print(f"  unrelated control   : {unrelated}\n")

    def kill(lines,lineno):
        """remove the lock EVENT but keep the statement's SHAPE.

        Replacing the whole line with a comment changes the mood of the braces
        as well, and then a red row can go red because the parse broke rather
        than because the lock went away -- a control that moves for the wrong
        reason.  Renaming the call keeps parens and ';' exactly where they were."""
        lines=list(lines)
        s=lines[lineno-1]
        assert 'im_mutex_' in s, (lineno,s)
        lines[lineno-1]=s.replace('im_mutex_unlock','__KILLED_unlock').replace('im_mutex_lock','__KILLED_lock')
        return lines

    def kill_raw(lines,lineno):
        lines=list(lines)
        lines[lineno-1]="/* killed lock event */"
        return lines

    def verdict(lines,line):
        IN,_=P.analyse(lines,a.fn)
        nid=P.NID.get(line) or P.HDR.get(line)
        st=IN.get(nid,set()) if nid else set()
        return ('持分片' if any(st) else '无分片') if st else '未解析'

    rows=[('baseline (unmutated)',verdict(L,TARGET))]
    for i,t in enumerate(takes,1):            # each row from the PRISTINE source.
        rows.append((f'RC{i}  take @{t} killed (alone)   -> must NOT move',
                     verdict(kill(L,t),TARGET)))  # stacking them would make every row
    both=L                                        # measure the row above it.
    for t in takes: both=kill(both,t)
    rows.append((f'RC  ALL {len(takes)} takes killed          -> must move',
                 verdict(both,TARGET)))
    if early:
        rows.append((f'RC  early-exit unlock @{early[0]} killed (OTHER path) -> must NOT move',
                     verdict(kill_raw(L,early[0]),TARGET)))
    m=list(L); m[unrelated-1]=m[unrelated-1].rstrip()+f'  /* unrelated @{unrelated} */'
    rows.append((f'RC  unrelated line @{unrelated} commented             -> must NOT move',
                 verdict(m,TARGET)))

    for k,v in rows: print(f"  {k:52s} -> {v}")
    print()
    print("  the unrelated control must NOT move : a control that moves for an unrelated edit")
    print("                                        measures the edit, not the object under test.")
    print("  the early-exit control must NOT move: a line-scanning ruler DOES move here, because")
    print("                                        it reads that unlock as the nearest lock event,")
    print("                                        but it is the END OF A DIFFERENT PATH.")

if __name__=='__main__': main()
