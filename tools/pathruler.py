#!/usr/bin/env python3
"""pathruler — answer "WHICH shard lock is held AT THIS LINE?" by WALKING THE PATH,
not by scanning lines.  No line-number whitelist anywhere.

  pathruler.py --ref <sha> --query <path>:<line> [--query ...]
  pathruler.py --ref <sha> --queries-file <file>      # one <path>:<line> per line

Why a second ruler.  A line scan answers "what lock event is textually nearest
above this line".  The real question is "what locks are held on ARRIVAL at this
line", which is a property of PATHS, not of text.  The two disagree in three
places that all three previous line-scanning rulers got wrong in the same way:

  - an early exit is the END OF ONE PATH, and a line scan reads it as the end of
    a STATE (src/vm/vm.c:3731 unlocks on the type_mismatch path only);
  - two same-level shard locks can guard one site and a line scan remembers only
    the last one (src/vm/vm.c:3713 and the re-take inside the grow branch);
  - a control header with a BRACELESS body is absorbed into its own body, so the
    body loses its loop context (that is why a lock inside
    `for (i = 0; i < VM_GLOBAL_SHARDS; i++)` used to degrade to the key '?').

It answers with lock IDENTITY, not just held/not-held: the key is the index
expression of the shard (`idx`, `g`, `f->var_idx`) or ALL when the lock sits
inside a loop bounded by VM_GLOBAL_SHARDS.  Knowing WHICH shard is held is what
lets a caller ask the second half of the question — is it the RIGHT one for the
slot being written — which no held/not-held ruler can ask at all.

The ruler contains no coordinates of its own: the acceptance set lives outside
this file (accept.txt), and the red controls live in redcontrol.py.
"""
import re,sys,subprocess,argparse,collections

TAKE = re.compile(r'im_mutex_lock\s*\(\s*\(ImMutex\s*\*\s*\)\s*(?:VM_GSHARD|vm->global_locks)')
DROP = re.compile(r'im_mutex_unlock\s*\(\s*\(ImMutex\s*\*\s*\)\s*(?:VM_GSHARD|vm->global_locks)')
GROW = re.compile(r'\bvm_global_grow\s*\(')

def file_lines(ref,path):
    return subprocess.run(["git","show",f"{ref}:{path}"],capture_output=True,
                          text=True,errors="replace").stdout.split("\n")

# ---------------- 1. normalise: one atom per (orig_line,text) ----------------
# atom kinds:  'stmt' (ends with ';'), '{', '}', 'hdr' (if/for/while/switch/do header),
#              'else', 'label' (L_x:), 'case' (case X: / default:)
CONT_END=re.compile(r'(&&|\|\||,|\(|\+|-|\*|/|%|<|>|=|!|\?|:)$')
def is_cont(t):
    t=t.strip()
    if LABEL_RE.match(t) or CASE_RE.match(t): return False
    return bool(CONT_END.search(t))
def normalize(lines):
    atoms=[]; in_block=False
    cur=""; depth=0
    for ln,raw in enumerate(lines,1):
        s=raw
        out=[]; i=0; instr=False; incomment=False
        while i < len(s):
            c=s[i]
            if incomment:
                if c=='*' and i+1<len(s) and s[i+1]=='/': incomment=False; i+=2; continue
                i+=1; continue
            if in_block:
                if c=='*' and i+1<len(s) and s[i+1]=='/': in_block=False; i+=2; continue
                i+=1; continue
            if instr:
                cur+=c
                if c=='\\' and i+1<len(s): cur+=s[i+1]; i+=2; continue
                if c=='"': instr=False
                i+=1; continue
            if c=='/' and i+1<len(s) and s[i+1]=='/': break
            if c=='/' and i+1<len(s) and s[i+1]=='*': in_block=True; i+=2; continue
            if c=='"': instr=True; cur+=c; i+=1; continue
            if c=="'": 
                cur+=c; i+=1
                while i<len(s) and s[i]!="'":
                    if s[i]=='\\': cur+=s[i]; i+=1
                    if i<len(s): cur+=s[i]; i+=1
                if i<len(s): cur+=s[i]; i+=1
                continue
            if c=='(': depth+=1; cur+=c; i+=1; continue
            if c==')': depth-=1; cur+=c; i+=1; continue
            if depth==0 and c=='{':
                t=cur.strip()
                if t:
                    cl=classify(t)
                    if cl: out.append(cl)
                    else:
                        sc=split_case(t)
                        if sc and not CASE_RE.match(t): out.extend(sc)
                        else: out.append(('hdr',t))
                out.append(('{','{')); cur=""; i+=1; continue
            if depth==0 and c=='}':
                t=cur.strip()
                if t:
                    cl=classify(t)
                    if cl: out.append(cl)
                    else:
                        sc=split_case(t)
                        if sc: out.extend(sc)
                        else: out.append(('stmt',t))
                out.append(('}','}')); cur=""; i+=1; continue
            if depth==0 and c==';':
                t=(cur+';').strip()
                sc=split_case(t) or split_label(t)
                if sc: out.extend(sc)
                else:
                    cc=split_ctrl(t)
                    if cc: out.extend(cc)
                    else:  out.append(('stmt',t))
                cur=""; i+=1; continue
            cur+=c; i+=1
        if cur.strip() and depth<=0 and not is_cont(cur):
            t=cur.strip()
            cl=classify(t)
            if cl: out.append(cl)
            else:
                sc=split_case(t)
                if sc: out.extend(sc)
                else: out.append(('hdr' if CTRL.match(t) else 'tail',t))
            cur=""
        elif cur.strip():
            cur+=" " 
        for kind,txt in out:
            atoms.append((ln,kind,txt))
    return atoms

LABEL_RE=re.compile(r'^[A-Za-z_]\w*\s*:$')
CASE_RE =re.compile(r'^(case\b[^:]*|default)\s*:$')
def classify(t):
    t=t.strip()
    if not t: return None
    if LABEL_RE.match(t): return ('label',t)
    if CASE_RE.match(t):  return ('case',t)
    if t=='else':         return ('else',t)
    return None
def split_label(t):
    m=re.match(r'^([A-Za-z_]\w*\s*:)\s*(.+)$',t.strip(),re.S)
    if not m: return None
    return [('label',m.group(1)),('stmt',m.group(2))]
CTRL_START=re.compile(r'^(if|for|while|switch)\b')
def split_ctrl(t):
    """'for (hdr) body;' -> [('hdr','for (hdr)'), ('stmt','body;')]

    A control header with a BRACELESS single-statement body never reaches the
    '{' branch, so without this it is absorbed into its own body statement and
    the Loop node disappears.  Consequence: the body loses its loop context,
    so `im_mutex_lock((ImMutex*)vm->global_locks[i])` inside
    `for (i = 0; i < VM_GLOBAL_SHARDS; i++)` is no longer recognisable as
    touching EVERY shard -- it degrades to the key '?'.  Returns None when the
    text is not that shape."""
    t=t.strip()
    m=CTRL_START.match(t)
    if not m: return None
    i=m.end()
    while i<len(t) and t[i] in ' \t': i+=1
    if i>=len(t) or t[i]!='(': return None
    d=0; j=i
    while j<len(t):
        if t[j]=='(': d+=1
        elif t[j]==')':
            d-=1
            if d==0: break
        j+=1
    if j>=len(t): return None
    rest=t[j+1:].strip()
    if not rest: return None
    out=[('hdr',t[:j+1])]
    sub=split_ctrl(rest)
    out.extend(sub if sub else [('stmt',rest)])
    return out

def split_case(t):
    """'case X: goto L;' -> [('case','case X:'), ('stmt','goto L;')]"""
    m=re.match(r'^((?:case\b[^:]*|default)\s*:)\s*(.*)$',t.strip(),re.S)
    if not m: return None
    out=[('case',m.group(1))]
    rest=m.group(2).strip()
    if rest: out.append(('stmt',rest))
    return out

CTRL=re.compile(r'^(if|for|while|switch)\b')
GOTO=re.compile(r'^goto\s+([A-Za-z_]\w*)\s*;$')

# ---------------- 2. structured parse over atoms ----------------
class Seq:
    def __init__(s,items): s.items=items
class Simple:
    def __init__(s,line,text): s.line,s.text=line,text
class If:
    def __init__(s,line,t,e): s.line,s.then,s.els=line,t,e
class Loop:
    def __init__(s,line,body,hdr=''): s.line,s.body,s.hdr=line,body,hdr
class Switch:
    def __init__(s,line,body): s.line,s.body=line,body
class Label:
    def __init__(s,line,name): s.line,s.name=line,name
class Jump:
    def __init__(s,line,kind,name=None): s.line,s.kind,s.name=line,kind,name

def parse_one(atoms,i,end):
    """parse exactly ONE item at i -> (node|None, next_index)"""
    ln,kind,txt=atoms[i]
    if kind=='{':
        return parse_seq(atoms,i+1,end)
    if kind=='else': return None,i+1
    if kind=='label': return Label(ln,txt[:-1].strip()),i+1
    if kind=='case': return Label(ln,txt),i+1
    if kind in ('hdr',) and CTRL.match(txt):
        kw=txt.split('(')[0].strip()
        if kw in ('if','for','while','switch'):
            then,i3=parse_body(atoms,i+1,end)
            els=None
            if kw=='if' and i3<end and atoms[i3][1]=='else':
                els,i3=parse_body(atoms,i3+1,end)
            node=If(ln,then,els) if kw=='if' else (Switch(ln,then) if kw=='switch' else Loop(ln,then,txt))
            return node,i3
    if kind=='hdr' and txt.strip()=='do':
        body,i2=parse_body(atoms,i+1,end)
        while i2<end and not (atoms[i2][1]=='hdr' and atoms[i2][2].startswith('while')): i2+=1
        return Loop(ln,body,txt),min(i2+1,end)
    if kind=='tail':
        m=GOTO.match(txt+';')
        if m: return Jump(ln,'goto',m.group(1)),i+1
        return Simple(ln,txt),i+1
    t=txt
    if re.match(r'^return\b',t): return Jump(ln,'return'),i+1
    if re.match(r'^break\s*;$',t): return Jump(ln,'break'),i+1
    if re.match(r'^continue\s*;$',t): return Jump(ln,'continue'),i+1
    m=GOTO.match(t)
    if m: return Jump(ln,'goto',m.group(1)),i+1
    return Simple(ln,t),i+1

def parse_seq(atoms,i,end):
    """parse until the matching '}' (consumed) or end"""
    items=[]
    while i<end:
        if atoms[i][1]=='}': return Seq(items),i+1
        node,i2=parse_one(atoms,i,end)
        if node is not None: items.append(node)
        i=i2 if i2>i else i+1
    return Seq(items),i

def parse_body(atoms,i,end):
    while i<end and atoms[i][1]=='stmt' and not atoms[i][2].strip(): i+=1
    if i>=end: return Seq([]),i
    if atoms[i][1]=='{': return parse_seq(atoms,i+1,end)
    node,i2=parse_one(atoms,i,end)          # a BRACELESS body is exactly ONE item,
    items=[node] if node is not None else []  # not "the rest of the enclosing block"
    return Seq(items),i2

# ---------------- 3. CFG ----------------
E={}; F={}; NID={}; TEXT={}; LABEL={}; GOTOS=[]; BREAKS=[]; CONTS=[]; CASES=[]; HDR={}; HDRTEXT={}
def new():
    new.n+=1; return new.n

GSHARD_ARG=re.compile(r'VM_GSHARD\s*\(\s*[^,]+,\s*([^)]+?)\s*\)')
GLOBALLOCKS_ARG=re.compile(r'global_locks\s*\[\s*([^\]]+?)\s*\]')
def shard_key(text):
    """the shard expression, from VM_GSHARD(vm, EXPR) or the raw vm->global_locks[EXPR];
       None if this line is not a shard op"""
    m=GSHARD_ARG.search(text)
    if m: return m.group(1).strip()
    m=GLOBALLOCKS_ARG.search(text)
    if m: return m.group(1).strip()
    return None

def shard_ops(text, all_shards):
    """[(pos,'+',key) | (pos,'-',key)] in textual order.
       all_shards=True when the op sits inside a loop bounded by VM_GLOBAL_SHARDS,
       in which case the op touches EVERY shard and the key is 'ALL'."""
    ops=[]
    for m in TAKE.finditer(text):
        ops.append((m.start(),'+','ALL' if all_shards else shard_key(text[m.start():]) or '?'))
    for m in DROP.finditer(text):
        ops.append((m.start(),'-','ALL' if all_shards else shard_key(text[m.start():]) or '?'))
    ops.sort()
    return ops

def effect_seq(text):
    hits=[]
    for m in TAKE.finditer(text): hits.append((m.start(),'H'))
    for m in DROP.finditer(text): hits.append((m.start(),'N'))
    for m in GROW.finditer(text): hits.append((m.start(),'N'))
    hits.sort(); return tuple(e for _,e in hits)

def flatten(node,entry,exit_,lines,loops=()):
    if isinstance(node,Seq):
        cur=entry
        for it in node.items:
            nxt=new(); flatten(it,cur,nxt,lines,loops); cur=nxt
        E.setdefault(cur,set()).add(exit_); return
    if isinstance(node,Simple):
        E.setdefault(entry,set()).add(exit_)
        allsh=any('VM_GLOBAL_SHARDS' in h for h in loops)
        so=shard_ops(node.text,allsh)
        if so: F.setdefault((entry,exit_),[]).extend([(sgn,key) for _,sgn,key in so])
        else:  F.setdefault((entry,exit_),[]).extend([('=','')])
        TEXT[entry]=node.text; NID.setdefault(node.line,entry); return
    if isinstance(node,Label):
        LABEL.setdefault(node.name,[]).append(entry)
        if node.name.startswith('case ') or node.name.startswith('default'):
            CASES.append(entry)
        E.setdefault(entry,set()).add(exit_); return
    if isinstance(node,If):
        a=new(); b=new(); HDR.setdefault(node.line,entry)
        E.setdefault(entry,set()).add(a); flatten(node.then,a,b,lines,loops); E.setdefault(b,set()).add(exit_)
        if node.els is not None:
            c=new(); d=new()
            E.setdefault(entry,set()).add(c); flatten(node.els,c,d,lines,loops); E.setdefault(d,set()).add(exit_)
        else:
            E.setdefault(entry,set()).add(exit_)
        return
    if isinstance(node,Loop):
        a=new(); b=new(); HDR.setdefault(node.line,entry); HDRTEXT.setdefault(node.line,node.hdr)
        hdr=HDRTEXT.get(node.line,'')
        sub=loops+((hdr,) if hdr else ())
        E.setdefault(entry,set()).add(a); E.setdefault(entry,set()).add(exit_)
        flatten(node.body,a,b,lines,sub)
        E.setdefault(b,set()).add(a); E.setdefault(b,set()).add(exit_)
        BREAKS.append((b,exit_)); CONTS.append((b,a))
        return
    if isinstance(node,Switch):
        a=new(); b=new(); HDR.setdefault(node.line,entry)
        E.setdefault(entry,set()).add(a); E.setdefault(entry,set()).add(exit_)
        before=len(CASES)
        flatten(node.body,a,b,lines,loops)
        for nid in CASES[before:]:                 # a switch may enter at ANY case
            E.setdefault(entry,set()).add(nid)
        E.setdefault(b,set()).add(exit_)
        BREAKS.append((b,exit_))
        return
    if isinstance(node,Jump):
        if node.kind=='goto': GOTOS.append((entry,node.name))
        elif node.kind=='return': E.setdefault(entry,set()).add(2)
        elif node.kind=='break': BREAKS.append((entry,None))
        elif node.kind=='continue': CONTS.append((entry,None))
        return
    raise SystemExit("unhandled "+type(node).__name__)

def find_function(lines,name):
    sig=re.compile(r'^[A-Za-z_][\w \t\*]*?\b'+re.escape(name)+r'\s*\([^;{]*\)\s*\{?\s*$')
    for i,l in enumerate(lines,1):
        if sig.match(l):
            j=i
            while j<=len(lines) and '{' not in lines[j-1]: j+=1
            d=0; started=False
            for k in range(j,len(lines)+1):
                d+=lines[k-1].count('{')-lines[k-1].count('}')
                if '{' in lines[k-1]: started=True
                if started and d<=0: return (i,k)
            return (i,len(lines))
    return None

def analyse(lines,name):
    global E,F,NID,TEXT,LABEL,GOTOS,BREAKS,CONTS,CASES,HDR,HDRTEXT
    E={};F={};NID={};TEXT={};LABEL={};GOTOS=[];BREAKS=[];CONTS=[];CASES=[];HDR={};HDRTEXT={}
    new.n=2
    sp=find_function(lines,name)
    if not sp: return None,None
    atoms=normalize(lines[sp[0]-1:sp[1]])
    # atoms carry ORIGINAL line numbers already (normalize used enumerate on the slice)
    off=sp[0]-1
    atoms=[(ln+off,k,t) for ln,k,t in atoms]
    root,_=parse_seq(atoms,0,len(atoms))
    flatten(root,1,2,lines,())
    # resolve gotos to label nodes
    for src,lname in GOTOS:
        for tgt in LABEL.get(lname,[]): E.setdefault(src,set()).add(tgt)
    # break/continue with explicit target ids
    for src,tgt in BREAKS:
        if tgt is None:
            for (s,t) in BREAKS:
                pass
    EMPTY=frozenset()
    IN=collections.defaultdict(set); IN[1]={EMPTY}
    changed=True
    while changed:
        changed=False
        for u in list(E):
            for v in E[u]:
                eff=F.get((u,v))
                for st in IN.get(u,set()):
                    ns=st
                    if eff is not None:
                        for sgn,key in eff:
                            if sgn=='+':
                                ns = ns | set([k for k in ([key] if key!='ALL' else [])]) | ({'ALL'} if key=='ALL' else set())
                            elif sgn=='-':
                                if key=='ALL': ns=frozenset()
                                else: ns=frozenset(k for k in ns if k!=key)
                    if ns not in IN[v]:
                        IN[v].add(ns); changed=True
    return IN,sp

SIGRE=re.compile(r'^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{?\s*$')
SKIP={'if','while','for','switch','return','sizeof','else','do'}
def all_functions(lines):
    """(name, start, end) for every definition, resolved by brace matching"""
    out=[]
    for i,l in enumerate(lines,1):
        m=SIGRE.match(l)
        if not m or m.group(1) in SKIP: continue
        sp=find_function(lines,m.group(1))
        if sp and sp[0]==i: out.append((m.group(1),sp[0],sp[1]))
    return out

def query(ref,path,line):
    lines=file_lines(ref,path)
    owner=None; best=None
    for nm,a,b in all_functions(lines):        # innermost enclosing definition wins
        if a<=line<=b and (best is None or (b-a)<best): best=b-a; owner=nm
    if not owner: return None,None,None
    r=analyse(lines,owner)
    if not r or r[0] is None: return owner,None,None
    IN,_=r
    nid=NID.get(line)
    if nid is None: nid=HDR.get(line)      # control header: report the state on entry to it
    if nid is None: return owner,None,None
    st=IN.get(nid,set())
    if not st: return owner,None,None,None
    held=[s for s in st if s]
    keys=sorted({k for s in st for k in s})
    return owner,('持分片' if held else '无分片'),sorted(st,key=lambda s:(len(s),sorted(s))),keys

def print_premises():
    """Every run must print the premises its verdicts stand on.  A premise that
    lives only in the README does not travel with the verdict, and an absent
    scope note is read as 'there is nothing there'."""
    print('#'*78)
    print('# pathruler -- boundary premises this run stands on (read them as part of every verdict)')
    print('#  P1  the owning function is chosen by brace matching; a function whose braces are')
    print('#      unbalanced (preprocessor-dependent) is spanned by the innermost definition')
    print('#      that contains the line.  task_scheduler_entry in src/vm/vm.c has two opening')
    print('#      braces and one closing brace under #ifdef, so it is exactly such a function.')
    print('#      A verdict inside a function it spans inherits that span.')
    print('#  P2  only VM_GSHARD(...) / vm->global_locks(...) count as THE SHARD.  VM_LOCK(vm)')
    print('#      (the global lock) is deliberately NOT a shard.  So "无分片" means "no shard is')
    print('#      held", NOT "no lock is held" -- read the absence accordingly.')
    print('#'*78)

if __name__=='__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('--ref',required=True); ap.add_argument('--query',action='append',default=[])
    ap.add_argument('--queries-file')
    a=ap.parse_args()
    if a.queries_file:
        with open(a.queries_file) as fh:
            a.query += [l.strip() for l in fh if l.strip() and not l.startswith('#')]
    print_premises()
    for q in a.query:
        p,l=q.rsplit(':',1); l=int(l)
        owner,verdict,states,keys=query(a.ref,p,l)
        k='ALL(全部分片)' if keys and keys==['ALL'] else (','.join(keys) if keys else '-')
        print(f"{q}  fn={owner}  -> {verdict}   持的是={k}   states={states}"
              f"   [P1:brace-span P2:no-global-lock]")
