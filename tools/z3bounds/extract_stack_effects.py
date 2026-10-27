#!/usr/bin/env python3
"""Executor-side stack-effect extractor for src/vm/vm.c (T2, main leg).

The executor dispatches with 'case OP_X: goto L_Y;' and keeps each op body at
its L_Y label.  This tool maps op -> label -> body, reports every t->sp write
with the guard line that precedes it, and prints anything it cannot classify
instead of skipping it:

    UNPARSED|path:line|<verbatim>
    UNPARSED-EXPR|path:line|<verbatim>

Baselines (independently grepped, word-bounded so nt->sp does not collide):
    t->sp--          20
    t->sp++           5
    t->sp = -<num>;   3
The self-check compares the tool's own counts with those numbers; a mismatch
means the tool is wrong first, the source second.

Usage: extract_stack_effects.py [--root DIR]
Exit: 0 self-check passed, 1 self-check failed, 2 tree does not match.
"""
import argparse
import os
import re
import sys

B = r'(?<![A-Za-z0-9_])'
DISPATCH = re.compile(r'^\s*case\s+(OP_[A-Z0-9_]+)\s*:\s*goto\s+(L_[A-Z0-9_]+)\s*;\s*$')
LABEL = re.compile(r'^\s*(L_[A-Z0-9_]+)\s*:\s*(\{)?\s*$')
CASE = re.compile(r'^\s*case\s+OP_')
SP_DEC = re.compile(B + r't->sp--')
SP_INC = re.compile(B + r't->sp\+\+')
SP_ASSIGN = re.compile(B + r't->sp\s*=\s*([^;]+);')
SP_PLUSEQ = re.compile(B + r't->sp\s*(\+=|-=)\s*([^;]+);')
SP_READ = re.compile(B + r't->sp\b')
GOTO = re.compile(r'goto\s+(L_[A-Z0-9_]+)\s*;')
GUARD = re.compile(r'^\s*(if|\}\s*while|\}\s*if)\s*\(.*t->sp')
REGUSE = re.compile(r'(\bFR\[|\bR\[|t->R\b)')
EXPECT = {'dec': 20, 'inc': 5, 'assign_literal': 3}

# Named reasons for ops with no t->sp write.  Each entry points at the body it
# was read from, so the claim is checkable rather than "the tool did not find
# anything".  reason-source=table below says these were reviewed by hand, not
# inferred by the extractor.
NAMED = {
    'OP_DECLARE': ('limits-only', 'src/vm/vm.c:4184'),
    'OP_HALT': ('terminates-run', 'src/vm/vm.c:4022'),
    'OP_STOP': ('terminates-run', 'src/vm/vm.c:4019'),
    'OP_JUMP': ('control-flow-only', 'src/vm/vm.c:3797'),
    'OP_LOCK': ('mutex-only', 'src/vm/vm.c:4700'),
    'OP_TRY_END': ('exception-depth-only', 'src/vm/vm.c:4436'),
    'OP_YIELD': ('task-yield-only', 'src/vm/vm.c:4027'),
}


def classify_mutation(raw):
    """Return ('dec'|'inc'|'assign_literal'|'assign_expr'|'plus_minus_eq', detail)."""
    if SP_DEC.search(raw):
        return 'dec', ''
    if SP_INC.search(raw):
        return 'inc', ''
    m = SP_PLUSEQ.search(raw)
    if m:
        return 'plus_minus_eq', (m.group(1), m.group(2).strip())
    m = SP_ASSIGN.search(raw)
    if m:
        rhs = m.group(1).strip()
        if re.fullmatch(r'-\s*\d+', rhs):
            return 'assign_literal', rhs
        return 'assign_expr', rhs
    return None, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.environ.get('Z3BOUNDS_ROOT', '.'))
    args = ap.parse_args()
    path = os.path.join(args.root, 'src', 'vm', 'vm.c')
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except OSError as exc:
        print('cannot read %s: %s' % (path, exc))
        return 2

    dispatch = []
    for i, raw in enumerate(lines, 1):
        m = DISPATCH.match(raw)
        if m:
            dispatch.append((m.group(1), m.group(2), i))
    if not dispatch:
        print('no dispatch lines found in %s' % path)
        return 2

    label_lines = {}
    for i, raw in enumerate(lines, 1):
        m = LABEL.match(raw)
        if m:
            label_lines[m.group(1)] = i
    ordered = sorted(label_lines.items(), key=lambda kv: kv[1])
    DEFAULT = re.compile(r'^s*defaults*:')
    blocks = {}
    for idx, (lab, start) in enumerate(ordered):
        stop = ordered[idx + 1][1] if idx + 1 < len(ordered) else len(lines) + 1
        base_indent = len(lines[start - 1]) - len(lines[start - 1].lstrip())
        for j in range(start + 1, min(stop, len(lines) + 1)):
            raw = lines[j - 1]
            stripped = raw.strip()
            indent = len(raw) - len(raw.lstrip())
            if CASE.match(raw) or DEFAULT.match(raw):
                stop = j
                break
            if stripped.startswith('}') and indent < base_indent:
                stop = j
                break
        blocks[lab] = (start, min(stop, len(lines) + 1))

    ops = []
    for op, lab, dline in dispatch:
        if lab not in blocks:
            print('UNPARSED|src/vm/vm.c:%d|case %s: goto %s;  (no label block)' % (dline, op, lab))
            continue
        start, stop = blocks[lab]
        muts = []
        reads = 0
        delegates = []
        body_has_regs = False
        for j in range(start, stop):
            raw = lines[j - 1]
            kind, detail = classify_mutation(raw)
            if kind:
                muts.append({'line': j, 'kind': kind, 'detail': detail,
                             'text': raw.strip(), 'guard': None})
                if kind == 'assign_expr':
                    print('UNPARSED-EXPR|src/vm/vm.c:%d|%s' % (j, raw.strip()))
                if kind == 'plus_minus_eq':
                    print('UNPARSED-EXPR|src/vm/vm.c:%d|%s' % (j, raw.strip()))
            else:
                n = len(SP_READ.findall(raw))
                reads += n
            m = GOTO.search(raw)
            if m and not kind:
                delegates.append((j, m.group(1)))
            if REGUSE.search(raw):
                body_has_regs = True
        for mt in muts:
            for k in range(mt['line'], start - 1, -1):
                if GUARD.match(lines[k - 1]):
                    mt['guard'] = (k, lines[k - 1].strip())
                    break
        ops.append({'op': op, 'label': lab, 'dline': dline, 'span': (start, stop),
                    'muts': muts, 'reads': reads, 'delegates': delegates,
                    'regs': body_has_regs})

    covered = set()
    for o in ops:
        for j in range(o['span'][0], o['span'][1]):
            covered.add(j)

    whole = {'dec': 0, 'inc': 0, 'assign_literal': 0}
    outside = []
    for i, raw in enumerate(lines, 1):
        kind, detail = classify_mutation(raw)
        if kind in whole:
            whole[kind] += 1
            if i not in covered:
                outside.append((i, kind, raw.strip()))

    in_body = {'dec': 0, 'inc': 0, 'assign_literal': 0}
    for o in ops:
        for mt in o['muts']:
            if mt['kind'] in in_body:
                in_body[mt['kind']] += 1

    with_effect = [o for o in ops if o['muts']]
    no_effect = [o for o in ops if not o['muts']]
    expr_count = sum(1 for o in ops for mt in o['muts'] if mt['kind'] in ('assign_expr', 'plus_minus_eq'))

    print('EXEC|ops_total=%d|ops_with_sp_effect=%d|ops_no_effect_but_declared=%d|reads=%d'
          % (len(ops), len(with_effect), len(no_effect), sum(o['reads'] for o in ops)))
    print('MUTATIONS|dec=%d|inc=%d|assign_literal=%d|assign_expr=%d'
          % (in_body['dec'], in_body['inc'], in_body['assign_literal'], expr_count))
    print('WHOLEFILE|dec=%d|inc=%d|assign_literal=%d|outside_bodies=%d'
          % (whole['dec'], whole['inc'], whole['assign_literal'], len(outside)))
    fails = []
    for key in ('dec', 'inc', 'assign_literal'):
        ok = whole[key] == EXPECT[key]
        if not ok:
            fails.append(key)
        print('SELFCHECK|%s=%d|baseline=%d|%s' % (key, whole[key], EXPECT[key], 'PASS' if ok else 'FAIL'))
    print('SELFCHECK|in_body:dec=%d|inc=%d|assign_literal=%d'
          % (in_body['dec'], in_body['inc'], in_body['assign_literal']))
    for i, kind, text in outside:
        print('OUTSIDE|src/vm/vm.c:%d|%s|%s' % (i, kind, text))
    for o in sorted(ops, key=lambda x: x['op']):
        eff = []
        for kind in ('dec', 'inc', 'assign_literal', 'assign_expr'):
            n = sum(1 for mt in o['muts'] if mt['kind'] == kind)
            if n:
                eff.append('%s x%d' % (kind, n))
        gl = '; '.join('%d' % mt['guard'][0] if mt['guard'] else 'none' for mt in o['muts']) or 'none'
        print('OP|%s|%s|span %d-%d|%s|guards=%s|reads=%d'
              % (o['op'], o['label'], o['span'][0], o['span'][1],
                 ','.join(eff) if eff else 'NONE', gl, o['reads']))
    unclassified = 0
    for o in sorted(no_effect, key=lambda x: x['op']):
        evidence = ''
        if o['op'] in NAMED:
            reason, evidence = NAMED[o['op']]
            src = 'table'
        elif o['delegates']:
            reason = 'delegates-to=%s' % ','.join(lab for _, lab in o['delegates'])
            evidence = 'src/vm/vm.c:%d' % o['delegates'][0][0]
            src = 'extracted'
        elif o['reads']:
            reason = 'read-only'
            src = 'extracted'
        elif o['regs']:
            reason = 'register-only'
            src = 'extracted'
        else:
            reason = 'unclassified'
            src = 'none'
            unclassified += 1
        print('NOEFFECT|%s|reason=%s|evidence=%s|source=%s|reads=%d'
              % (o['op'], reason, evidence or '-', src, o['reads']))
    print('REASONS|register-only=%d|delegates=%d|read-only=%d|named=%d|unclassified=%d'
          % (sum(1 for o in no_effect if not o['muts'] and o['regs'] and not o['delegates'] and not o['reads'] and o['op'] not in NAMED),
             sum(1 for o in no_effect if o['delegates'] and o['op'] not in NAMED),
             sum(1 for o in no_effect if o['reads'] and not o['delegates'] and o['op'] not in NAMED),
             sum(1 for o in no_effect if o['op'] in NAMED), unclassified))
    print('SUMMARY|ops_total=%d|with_effect=%d|no_effect=%d|unparsed_expr=%d|selfcheck=%s'
          % (len(ops), len(with_effect), len(no_effect), expr_count,
             'PASS' if not fails else 'FAIL'))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
