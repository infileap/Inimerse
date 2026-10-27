#!/usr/bin/env python3
"""Emit-side reconciliation for T2, secondary leg.

'Ops_total=67' already showed that one 'case OP_' line can live in two worlds
(the dispatcher and the disassembler).  This leg asks the neighbouring question:
is every op the compiler emits actually dispatched, and does the argument the
compiler passes survive to the point the executor reads it?

Readings, in the order a reader needs them:

    EMIT|ops=..|dispatched=..|sites=..
    EMIT-DIFF|emitted_not_dispatched=..|dispatched_not_emitted=..
    EMIT-ONLY|OP_X|prov=path:line|text=...        (only when non-empty)
    EXCLUDED|OP_X|prov=..|why=only_in_comment     (the naive grep's extra hits)
    MIRROR|ops=..|not_in_dispatch=..              (selfhost/ mirror enum, separate world)
    ARGC|OP_X|r3_reads=..|emitted=..|verdict=..   (only the suspicious subset)
    ARGC-SUMMARY|checked=..|executor_reads_never_fed=..|emitted_never_read=..

Comment and string stripping is not decoration: OP_POP_REG sits in a bytecode.h
comment ('29 is a hole'), so the unstripped set is 68 and the real one is 67.

Usage: reconcile_emit_dispatch.py [--root DIR]
Exit: 0 clean (no emitted-only op, no dispatched-only op), 1 a difference was
      found (the reading is printed, not swallowed), 2 tree does not match.
"""
import argparse
import os
import re
import sys

DISPATCH = re.compile(r'^\s*case\s+(OP_[A-Z0-9_]+)\s*:\s*goto\s+(L_[A-Z0-9_]+)\s*;\s*$')
OPTOK = re.compile(r'\bOP_[A-Z0-9_]+\b')
EMIT_CALL = re.compile(r'\bemit\s*\(\s*[^,()]+,\s*(OP_[A-Z0-9_]+)\s*,')
STRUCT_DEF = re.compile(r'\btypedef\s+struct\b|\bstruct\s+\w+\s*\{')
MIRROR_ENUM = re.compile(r'^\s*(OP_[A-Z0-9_]+)\s*=\s*-?\d+')


def strip_code(text):
    """Blank out comments and string/char literals, preserving length exactly.

    The length guarantee is load-bearing: emit-site arguments are sliced out of
    the raw text using indexes found in the stripped text.  An earlier version
    turned '//' into one space, slid every index by one per comment, and
    invented r3 values that do not exist in the source.
    """
    out = list(text)
    i, n = 0, len(text)
    state = None  # None, 'line', 'block', 'str', 'chr'
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ''
        if state is None:
            if c == '/' and nxt == '/':
                state = 'line'
                out[i] = out[i + 1] = ' '
                i += 2
                continue
            if c == '/' and nxt == '*':
                state = 'block'
                out[i] = out[i + 1] = ' '
                i += 2
                continue
            if c == '"':
                state = 'str'
                out[i] = ' '
                i += 1
                continue
            if c == "'":
                state = 'chr'
                out[i] = ' '
                i += 1
                continue
            i += 1
            continue
        if state == 'block':
            if c == '*' and nxt == '/':
                state = None
                out[i] = out[i + 1] = ' '
                i += 2
                continue
        if c != '\n':
            out[i] = ' '
        elif state in ('str', 'chr'):
            state = None
        if state == 'line' and c == '\n':
            state = None
        i += 1
    res = ''.join(out)
    assert len(res) == len(text), 'strip must preserve length'
    return res


def read(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()


def dispatch_set(lines):
    ops, labels = set(), set()
    for i, raw in enumerate(lines, 1):
        m = DISPATCH.match(raw)
        if m:
            ops.add(m.group(1))
            labels.add((m.group(1), m.group(2), i))
    return ops, labels


def compiler_files(root):
    d = os.path.join(root, 'src', 'compiler')
    out = []
    for name in sorted(os.listdir(d)):
        if name.endswith(('.c', '.h')):
            out.append(os.path.join(d, name))
    return out


def emitted_sets(root, paths):
    """Return (real, comment_only, sites) where sites maps op -> [(path, line, args_text)]."""
    real, comment_only, sites = {}, {}, []
    for path in paths:
        raw = read(path)
        stripped = strip_code(raw)
        raw_lines = raw.splitlines()
        stripped_lines = stripped.splitlines()
        rel = os.path.relpath(path, root)
        for idx, sline in enumerate(stripped_lines, 1):
            for m in OPTOK.finditer(sline):
                real.setdefault(m.group(0), (rel, idx, raw_lines[idx - 1].strip()))
        for idx, rline in enumerate(raw_lines, 1):
            for m in OPTOK.finditer(rline):
                if not OPTOK.search(stripped_lines[idx - 1]):
                    comment_only.setdefault(m.group(0), (rel, idx, rline.strip()))
                elif m.group(0) not in real:
                    comment_only.setdefault(m.group(0), (rel, idx, rline.strip()))
        # emit call sites, with their three arguments (paren-balanced across lines)
        pos = 0
        while True:
            m = EMIT_CALL.search(stripped, pos)
            if not m:
                break
            op = m.group(1)
            open_paren = stripped.rfind('(', 0, m.end())
            depth, j = 0, open_paren
            while j < len(stripped):
                if stripped[j] == '(':
                    depth += 1
                elif stripped[j] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            inner = raw[open_paren + 1:j]
            flat = ' '.join(inner.split())
            args = split_args(flat)
            line = raw[:open_paren].count('\n') + 1
            sites.append((op, rel, line, args))
            pos = j
    return real, comment_only, sites


def split_args(flat):
    args, depth, cur = [], 0, []
    for ch in flat:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == ',' and depth == 0:
            args.append(''.join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if cur:
        args.append(''.join(cur).strip())
    return args


def r3_reads(lines):
    """Per dispatched op: how often the body it ends up executing reads ins.r3.

    Delegation is followed: OP_CALL_VALUE's own block is just 'goto L_CALL_FUNC;',
    and the argc it forwards is read there, so counting only the first block
    would report 'the executor ignores r3' for the one op that forwards it.
    """
    LABEL = re.compile(r'^\s*(L_[A-Z0-9_]+)\s*:\s*(\{)?\s*$')
    CASE = re.compile(r'^\s*case\s+OP_')
    DEFAULT = re.compile(r'^\s*default\s*:')
    GOTO = re.compile(r'goto\s+(L_[A-Z0-9_]+)\s*;')
    label_line = {}
    for i, raw in enumerate(lines, 1):
        m = LABEL.match(raw)
        if m:
            label_line.setdefault(m.group(1), i)
    order = sorted(label_line.items(), key=lambda kv: kv[1])
    blocks = {}
    for idx, (lab, start) in enumerate(order):
        stop = order[idx + 1][1] if idx + 1 < len(order) else len(lines) + 1
        base = len(lines[start - 1]) - len(lines[start - 1].lstrip())
        for j in range(start + 1, min(stop, len(lines) + 1)):
            raw = lines[j - 1]
            s = raw.strip()
            ind = len(raw) - len(raw.lstrip())
            if CASE.match(raw) or DEFAULT.match(raw):
                stop = j
                break
            if s.startswith('}') and ind < base:
                stop = j
                break
        blocks[lab] = (start, min(stop, len(lines) + 1))
    direct, jumped = {}, {}
    for lab, (start, stop) in blocks.items():
        count = 0
        targets = []
        for j in range(start, stop):
            count += len(re.findall(r'\bins\.r3\b', lines[j - 1]))
            targets.extend(GOTO.findall(lines[j - 1]))
        direct[lab] = count
        jumped[lab] = targets
    def total(lab, seen):
        if lab in seen:
            return 0
        seen.add(lab)
        n = direct.get(lab, 0)
        for tgt in jumped.get(lab, []):
            n += total(tgt, seen)
        return n
    out = {}
    for op, lab, dline in dispatch_triples(lines):
        if lab in blocks:
            out[op] = total(lab, set())
    return out


def dispatch_triples(lines):
    for i, raw in enumerate(lines, 1):
        m = DISPATCH.match(raw)
        if m:
            yield m.group(1), m.group(2), i


def mirror(root):
    path = os.path.join(root, 'selfhost', 'compiler.im')
    if not os.path.exists(path):
        return set(), 0
    ops = set()
    for raw in read(path).splitlines():
        m = MIRROR_ENUM.match(raw)
        if m:
            ops.add(m.group(1))
    return ops, len(ops)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.environ.get('Z3BOUNDS_ROOT', '.'))
    args = ap.parse_args()
    vm = os.path.join(args.root, 'src', 'vm', 'vm.c')
    try:
        lines = read(vm).splitlines()
    except OSError as exc:
        print('cannot read %s: %s' % (vm, exc))
        return 2
    dispatched, _ = dispatch_set(lines)
    if not dispatched:
        print('no dispatch lines found in %s' % vm)
        return 2

    real, comment_only, sites = emitted_sets(args.root, compiler_files(args.root))
    emitted = set(real)
    only_emitted = sorted(emitted - dispatched)
    only_dispatched = sorted(dispatched - emitted)
    print('EMIT|ops=%d|dispatched=%d|emit_sites=%d|files=src/compiler/*.{c,h}'
          % (len(emitted), len(dispatched), len(sites)))
    print('EMIT-DIFF|emitted_not_dispatched=%d|dispatched_not_emitted=%d'
          % (len(only_emitted), len(only_dispatched)))
    for op in only_emitted:
        rel, line, text = real[op]
        print('EMIT-ONLY|%s|prov=%s:%d|text=%s' % (op, rel, line, text))
    for op in only_dispatched:
        print('DISPATCH-ONLY|%s' % op)
    for op in sorted(comment_only):
        if op not in emitted:
            rel, line, text = comment_only[op]
            print('EXCLUDED|%s|prov=%s:%d|why=only_in_comment|text=%s' % (op, rel, line, text))
    mops, mcount = mirror(args.root)
    print('MIRROR|enum_ops=%d|not_in_dispatch=%s|absent_from_mirror=%d'
          % (mcount, ','.join(sorted(mops - dispatched)) or '-', len(dispatched - mops)))

    reads = r3_reads(lines)
    per_op = {}
    for op, rel, line, a in sites:
        if len(a) < 5 or ';' in a[-1]:
            continue
        third = a[4]
        rec = per_op.setdefault(op, {'sites': 0, 'kinds': set(), 'samples': []})
        rec['sites'] += 1
        if re.fullmatch(r'0+', third):
            kind = 'literal_zero'
        elif re.fullmatch(r'-?\d+', third):
            kind = 'literal_nonzero'
        else:
            kind = 'expr'
        rec['kinds'].add(kind)
        if len(rec['samples']) < 2:
            rec['samples'].append('%s:%d r3=%s' % (rel, line, third))
    suspicious = 0
    for op in sorted(set(reads) | set(per_op)):
        r = reads.get(op, 0)
        rec = per_op.get(op)
        kinds = ','.join(sorted(rec['kinds'])) if rec else 'none'
        verdict = None
        if r > 0 and rec and rec['kinds'] == {'literal_zero'}:
            verdict = 'executor-reads-but-never-fed'
        elif r == 0 and rec and rec['kinds'] != {'literal_zero'}:
            verdict = 'emitted-but-executor-ignores-r3'
        if verdict:
            suspicious += 1
            print('ARGC|%s|r3_reads=%d|emitted=%s|sites=%d|verdict=%s|sample=%s'
                  % (op, r, kinds, rec['sites'], verdict, '; '.join(rec['samples'])))
    print('ARGC-SUMMARY|checked=%d|suspicious=%d|note=r3=third_emit_arg_vs_ins.r3_reads'
          % (len(set(reads) | set(per_op)), suspicious))
    print('SUMMARY|emitted=%d|dispatched=%d|only_emitted=%d|only_dispatched=%d|clean=%s'
          % (len(emitted), len(dispatched), len(only_emitted), len(only_dispatched),
             'YES' if not only_emitted and not only_dispatched else 'NO'))
    return 0 if not only_emitted and not only_dispatched else 1


if __name__ == '__main__':
    sys.exit(main())
