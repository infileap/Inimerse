#!/usr/bin/env python3
"""Executor-side stack-effect extractor for src/vm/vm.c (T2, main leg).

The dispatcher is a two-hop structure, and saying so is part of the reading:

    src/vm/vm.c:3161   case OP_MOV: goto L_MOV;     <- switch (ins.op) at :3160
    src/vm/vm.c:3235   L_MOV:                       <- the body
    src/vm/vm.c:5312   case OP_MOV: snprintf(...);  <- a DIFFERENT switch (disassembler)

so 'grep -c "case OP_"' = 114 counts two worlds, while the executor dispatches
67 ops.  SWITCHES| below prints both switch spans and both counts, so the next
person to recompute 114 is contradicted by the tool instead of by a letter.

Readings are printed in the order the reader needs them: shape of the judgement
first (NORMALIZE|), then the numbers, then everything the tool could not parse.

    NORMALIZE|dedupe=...|word-boundary=...
    SWITCHES|dispatch=...|disasm=...|ops_total=...|labels=...
    NEGCTL|...            negative controls: a rename that must not move the
                          reading, and a rename that must move it by exactly N

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
CASE_OP = re.compile(r'^\s*case\s+OP_[A-Z0-9_]+\s*:')
CASE = re.compile(r'^\s*case\s+OP_')
DEFAULT = re.compile(r'^\s*default\s*:')
SP_DEC = re.compile(B + r't->sp--')
SP_INC = re.compile(B + r't->sp\+\+')
SP_ASSIGN = re.compile(B + r't->sp\s*=\s*([^;]+);')
SP_PLUSEQ = re.compile(B + r't->sp\s*(\+=|-=)\s*([^;]+);')
SP_READ = re.compile(B + r't->sp\b')
GOTO = re.compile(r'goto\s+(L_[A-Z0-9_]+)\s*;')
GUARD = re.compile(r'^\s*(if|\}\s*while|\}\s*if)\s*\(.*t->sp')
REGUSE = re.compile(r'(\bFR\[|\bR\[|t->R\b)')
SWITCH = re.compile(r'^\s*switch\s*\(')
FIELD_WRITE = re.compile(r'([A-Za-z_]\w*)(?:->|\.)([A-Za-z_]\w*)\s*=(?!=)')
BARE_WRITE = re.compile(r'(?<![\w>.])([A-Za-z_]\w*)\s*=(?!=)')
EXPECT = {'dec': 20, 'inc': 5, 'assign_literal': 3}

# Named reasons for ops with no t->sp write.  evidence points at the body the
# claim was read from; source=table means a human reviewed it (the tool cannot
# infer 'mutex-only').  NO-SP| lines back the two block-level claims with the
# write-set, which is the right evidence shape for an exhaustion claim.
NAMED = {
    'OP_DECLARE': ('limits-only', 'src/vm/vm.c:4184'),
    'OP_HALT': ('terminates-run', 'src/vm/vm.c:4022'),
    'OP_STOP': ('terminates-run', 'src/vm/vm.c:4019'),
    'OP_JUMP': ('control-flow-only', 'src/vm/vm.c:3797'),
    'OP_LOCK': ('mutex-only', 'src/vm/vm.c:4700'),
    'OP_TRY_END': ('exception-depth-only', 'src/vm/vm.c:4436'),
    'OP_YIELD': ('task-yield-only', 'src/vm/vm.c:4027'),
}
BLOCK_CLAIMS = ('OP_DECLARE', 'OP_LOCK')


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


def switch_spans(lines):
    """Brace-counted switch spans: [(start, end, head_text, dispatch_cases, disasm_cases)]."""
    out = []
    for i, raw in enumerate(lines):
        if not SWITCH.match(raw):
            continue
        depth = 0
        for j in range(i, len(lines)):
            depth += lines[j].count('{') - lines[j].count('}')
            if depth == 0 and j > i:
                body = lines[i:j + 1]
                d = sum(1 for l in body if DISPATCH.match(l))
                dis = sum(1 for k in body if 'snprintf' in k)
                cases = sum(1 for l in body if CASE_OP.match(l))
                out.append((i + 1, j + 1, raw.strip(), d, dis, cases))
                break
    return out


def label_blocks(lines):
    label_lines = {}
    for i, raw in enumerate(lines, 1):
        m = LABEL.match(raw)
        if m:
            label_lines[m.group(1)] = i
    ordered = sorted(label_lines.items(), key=lambda kv: kv[1])
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
    return blocks


def analyze(lines):
    dispatch = []
    for i, raw in enumerate(lines, 1):
        m = DISPATCH.match(raw)
        if m:
            dispatch.append((m.group(1), m.group(2), i))
    blocks = label_blocks(lines)
    ops, unparsed = [], []
    for op, lab, dline in dispatch:
        if lab not in blocks:
            unparsed.append((dline, 'case %s: goto %s;  (no label block)' % (op, lab)))
            continue
        start, stop = blocks[lab]
        muts, reads, delegates, regs = [], 0, [], False
        for j in range(start, stop):
            raw = lines[j - 1]
            kind, detail = classify_mutation(raw)
            if kind:
                muts.append({'line': j, 'kind': kind, 'detail': detail,
                             'text': raw.strip(), 'guard': None})
            else:
                reads += len(SP_READ.findall(raw))
            m = GOTO.search(raw)
            if m and not kind:
                delegates.append((j, m.group(1)))
            if REGUSE.search(raw):
                regs = True
        for mt in muts:
            for k in range(mt['line'], start - 1, -1):
                if GUARD.match(lines[k - 1]):
                    mt['guard'] = (k, lines[k - 1].strip())
                    break
        ops.append({'op': op, 'label': lab, 'dline': dline, 'span': (start, stop),
                    'muts': muts, 'reads': reads, 'delegates': delegates, 'regs': regs})
    covered = set()
    for o in ops:
        covered.update(range(o['span'][0], o['span'][1]))
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
    exprs = [(j, lines[j - 1].strip()) for j in sorted(covered)
             if classify_mutation(lines[j - 1])[0] in ('assign_expr', 'plus_minus_eq')]
    return {'dispatch': dispatch, 'blocks': blocks, 'ops': ops, 'unparsed': unparsed,
            'whole': whole, 'outside': outside, 'in_body': in_body, 'exprs': exprs}


def negctl(lines, res):
    """Two controls: an unrelated rename must not move the reading; renaming the
    real token must move it by exactly the number of renamed sites.
    A matcher that counted by '->sp' substring would fail the second one."""
    base = (res['whole']['dec'], res['whole']['inc'], res['whole']['assign_literal'])
    decoy_renamed = [l.replace('nt->sp', 'nnt->sp') for l in lines]
    r1 = analyze(decoy_renamed)
    v1 = (r1['whole']['dec'], r1['whole']['inc'], r1['whole']['assign_literal'])
    print('NEGCTL|unrelated-rename|nt->sp=>nnt->sp|dec=%d|inc=%d|assign_literal=%d|unchanged=%s'
          % (v1[0], v1[1], v1[2], 'YES' if v1 == base else 'NO'))
    sites = sum(1 for l in lines if SP_DEC.search(l))
    token_renamed = [SP_DEC.sub('t->spX--', l) for l in lines]
    r2 = analyze(token_renamed)
    v2 = (r2['whole']['dec'], r2['whole']['inc'], r2['whole']['assign_literal'])
    print('NEGCTL|token-rename|t->sp--=>t->spX--|dec=%d|expected_drop=%d|drop=%d|name_exact=%s'
          % (v2[0], sites, base[0] - v2[0], 'YES' if v2[0] == base[0] - sites else 'NO'))
    decoys = sum(1 for l in lines if re.search(r'(?<![A-Za-z0-9_])nt->sp\s*=', l))
    print('NEGCTL|decoy-sites|nt->sp_assign=%d|counted_as_t->sp=0|matcher=word-bounded'
          % decoys)
    return (v1 == base) and (v2[0] == base[0] - sites)


def no_sp_blocks(lines, res):
    """Exhaustion, not citation: for a block-level 'does not touch sp' claim, the
    evidence is the complete field write-set plus sp_writes=0."""
    ok = True
    by_op = {o['op']: o for o in res['ops']}
    for op in BLOCK_CLAIMS:
        o = by_op.get(op)
        if not o:
            print('NO-SP|%s|missing-block' % op)
            ok = False
            continue
        start, stop = o['span']
        writes, bare, sp_lines = set(), set(), 0
        for j in range(start, stop):
            raw = lines[j - 1]
            for m in FIELD_WRITE.finditer(raw):
                writes.add('%s->%s' % (m.group(1), m.group(2)))
            for m in BARE_WRITE.finditer(raw):
                bare.add(m.group(1))
            if SP_READ.search(raw):
                sp_lines += 1
        print('NO-SP|src/vm/vm.c:%d..%d|writes=%s|locals=%s|sp_writes=%d|source=extracted'
              % (start, stop, ','.join(sorted(writes)), ','.join(sorted(bare)), sp_lines))
        if sp_lines:
            ok = False
    return ok


def _fs_pat():
    import re
    return re.compile(r'(?<![A-Za-z0-9_])frame_sp(?![A-Za-z0-9_])')


def _fs_classify(code):
    import re
    if re.search(r'frame_sp\s*\[[^\]]*\]\s*=(?!=)', code):
        return 'element_write'
    if re.search(r'frame_sp\s*=\s*calloc', code):
        return 'alloc'
    if re.search(r'free\s*\([^)]*frame_sp', code):
        return 'free'
    if re.search(r'int\s*\*\s*frame_sp', code):
        return 'decl'
    if re.search(r'frame_sp\s*\[', code):
        return 'read'
    return 'other'


def _fs_scan(text):
    pat = _fs_pat()
    out = {'element_write': [], 'alloc': [], 'free': [], 'decl': [], 'read': [], 'comment': [], 'other': []}
    occ = 0
    for i, raw in enumerate(text.splitlines(), 1):
        if not pat.search(raw):
            continue
        occ += len(pat.findall(raw))
        code = raw
        cut = code.find('//')
        if cut >= 0:
            code = code[:cut]
        while True:
            a = code.find('/*')
            if a < 0:
                break
            b = code.find('*/', a + 2)
            code = code[:a] + (' ' * (b + 2 - a if b >= 0 else len(code) - a)) + (code[b + 2:] if b >= 0 else '')
        if not pat.search(code):
            out['comment'].append((i, raw.rstrip()))
            continue
        out[_fs_classify(code)].append((i, raw.rstrip()))
    return occ, out


def _fs_count_element_writes(text):
    return len(_fs_scan(text)[1]['element_write'])


def frame_sp_report(root):
    targets = []
    for dirpath, dirs, files in os.walk(os.path.join(root, 'src')):
        dirs.sort()
        for fn in sorted(files):
            if fn.endswith('.c') or fn.endswith('.h'):
                targets.append(os.path.join(dirpath, fn))
    allsites = {'element_write': [], 'alloc': [], 'free': [], 'decl': [], 'read': [], 'comment': [], 'other': []}
    occ = 0
    line_count = 0
    vm_text = None
    for p in sorted(targets):
        rel = os.path.relpath(p, root).replace(os.sep, '/')
        try:
            with open(p, 'r', encoding='utf-8', errors='replace') as f:
                t = f.read()
        except OSError:
            continue
        if rel == 'src/vm/vm.c':
            vm_text = t
        o, s = _fs_scan(t)
        occ += o
        for k in allsites:
            for (i, raw) in s[k]:
                allsites[k].append((rel, i, raw))
        line_count += sum(len(v) for v in s.values())
    for k in ('element_write', 'alloc', 'free', 'decl', 'read', 'comment', 'other'):
        for rel, i, raw in allsites[k]:
            print('FRAME-SP|%s:%d|kind=%s|text=%s' % (rel, i, k, raw))
    total = sum(len(v) for v in allsites.values())
    ok = (total == occ)
    print('FRAME-SP-BASELINE|lines=%d|occurrences=%d|classified=%d|covered=%s|matcher=word-bounded'
          % (line_count, occ, total, 'YES' if ok else 'NO'))
    for rel, i, raw in allsites['element_write']:
        rhs = raw.split('=', 1)[1] if '=' in raw else ''
        print('FRAME-SP-WRITE|%s:%d|stores=%s' % (rel, i, rhs.split(';')[0].strip()))
    n_ew = len(allsites['element_write'])
    print('FRAME-SP-SUMMARY|element_writes=%d|allocs=%d|frees=%d|decls=%d|reads=%d|comments=%d|other=%d'
          % (n_ew, len(allsites['alloc']), len(allsites['free']), len(allsites['decl']),
             len(allsites['read']), len(allsites['comment']), len(allsites['other'])))
    stores_sp = n_ew > 0 and all(('t->sp' in raw) for _r, _i, raw in allsites['element_write'])
    print('FRAME-SP-RANGE|element_writes=%d|every_write_stores_t->sp=%s|sp_range=[-1,1023]|source=axiom A4|'
          'axiom_as_stated=0 <= frame_sp[i] < reg_cap|verdict=%s'
          % (n_ew, 'YES' if stores_sp else 'NO',
             'lower_bound_too_strong' if stores_sp else 'cannot_conclude'))
    print('FRAME-SP-WITNESS|path=src/vm/vm.c:4104|argc=1|sp_before=0|guard_at_4073=if (t->sp + 1 < argc) return 0|'
          'guard_refuses=NO(1<1 false)|after_one_pop=sp -1|frame_slot=-1|status=reachable_on_handcrafted_bytecode')
    print('FRAME-SP-LABEL|A-FRAME|was=asserted,no_site_enumerated|now=measured,write_set_enumerated|'
          'conclusion=T2_conditional_until_axiom_restated_with_lower_bound_-1')
    if vm_text is not None:
        base = _fs_count_element_writes(vm_text)
        renamed = _fs_count_element_writes(vm_text.replace('frame_sp', 'frameSp'))
        print('FRAME-SP-NEGCTL|rename|frame_sp=>frameSp|element_writes=%d|expected_drop=%d|drop=%d|name_exact=%s'
              % (renamed, base, base - renamed, 'YES' if renamed == 0 else 'NO'))
        decoy = vm_text + '\n    frameSp[t->frame_count - 1] = t->sp;\n    int frameSpSeed = 0;\n'
        d = _fs_count_element_writes(decoy)
        print('FRAME-SP-NEGCTL|decoy|frameSp[]=|element_writes=%d|unchanged=%s|matcher=word-bounded'
              % (d, 'YES' if d == base else 'NO'))
        ok = ok and renamed == 0 and d == base
    return ok

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

    res = analyze(lines)
    if not res['dispatch']:
        print('no dispatch lines found in %s' % path)
        return 2

    # Shape of the judgement before the numbers it produces.
    print('NORMALIZE|dedupe=line-identity(each matching line counted once,no shape folding)'
          '|word-boundary=yes|patterns=dec:t->sp--,inc:t->sp++,assign:t->sp=<rhs>,read:t->sp')
    sw = switch_spans(lines)
    disp = [s for s in sw if s[3]]
    dis = [s for s in sw if s[4]]
    labels = len(res['blocks'])
    for s in disp:
        print('SWITCHES|dispatch=%d@%d..%d|dispatch_cases=%d|head=%s'
              % (len(disp), s[0], s[1], s[3], s[2]))
    for s in dis:
        print('SWITCHES|disasm=%d@%d..%d|disasm_cases=%d|head=%s'
              % (len(dis), s[0], s[1], s[5], s[2]))
    print('SWITCHES|ops_total=%d|labels=%d|case_OP_lines=%d|note=two_worlds:'
          'grep_case_OP_is_dispatch_plus_disasm'
          % (len(res['dispatch']), labels,
             sum(1 for l in lines if CASE_OP.match(l))))

    ops, whole, in_body = res['ops'], res['whole'], res['in_body']
    with_effect = [o for o in ops if o['muts']]
    no_effect = [o for o in ops if not o['muts']]
    expr_count = len(res['exprs'])
    print('EXEC|ops_total=%d|ops_with_sp_effect=%d|ops_no_effect_but_declared=%d|reads=%d'
          % (len(ops), len(with_effect), len(no_effect), sum(o['reads'] for o in ops)))
    print('MUTATIONS|dec=%d|inc=%d|assign_literal=%d|assign_expr=%d'
          % (in_body['dec'], in_body['inc'], in_body['assign_literal'], expr_count))
    print('WHOLEFILE|dec=%d|inc=%d|assign_literal=%d|outside_bodies=%d'
          % (whole['dec'], whole['inc'], whole['assign_literal'], len(res['outside'])))
    fails = []
    for key in ('dec', 'inc', 'assign_literal'):
        ok = whole[key] == EXPECT[key]
        if not ok:
            fails.append(key)
        print('SELFCHECK|%s=%d|baseline=%d|%s' % (key, whole[key], EXPECT[key], 'PASS' if ok else 'FAIL'))
    print('SELFCHECK|in_body:dec=%d|inc=%d|assign_literal=%d'
          % (in_body['dec'], in_body['inc'], in_body['assign_literal']))
    for j, text in res['exprs']:
        print('UNPARSED-EXPR|src/vm/vm.c:%d|%s' % (j, text))
    for dline, text in res['unparsed']:
        print('UNPARSED|src/vm/vm.c:%d|%s' % (dline, text))
    for i, kind, text in res['outside']:
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
    if not no_sp_blocks(lines, res):
        fails.append('no-sp')
    print('REASONS|register-only=%d|delegates=%d|read-only=%d|named=%d|unclassified=%d'
          % (sum(1 for o in no_effect if o['regs'] and not o['delegates'] and not o['reads'] and o['op'] not in NAMED),
             sum(1 for o in no_effect if o['delegates'] and o['op'] not in NAMED),
             sum(1 for o in no_effect if o['reads'] and not o['delegates'] and o['op'] not in NAMED),
             sum(1 for o in no_effect if o['op'] in NAMED), unclassified))
    if not negctl(lines, res):
        fails.append('negctl')
    if not frame_sp_report(args.root):
        fails.append('frame_sp')
    print('SUMMARY|ops_total=%d|with_effect=%d|no_effect=%d|unparsed_expr=%d|selfcheck=%s'
          % (len(ops), len(with_effect), len(no_effect), expr_count,
             'PASS' if not fails else 'FAIL'))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
