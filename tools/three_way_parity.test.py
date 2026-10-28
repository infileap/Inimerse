#!/usr/bin/env python3
"""One program, three backends: the interpreter, the AOT backend and the wasm backend must agree.

The repository has a three-way harness for ONE operator (`tools/mod_semantics.test.py`,
`%`) and one for `and`/`or`/`not` (`tools/logic_semantics.test.py`).  Nothing else runs
the same program on all three channels and compares the outcome, so a divergence in
any other operator would be found only by whoever happened to look.

What "agree" means here
-----------------------
    (exit status, last non-noise line of stdout)

Only the LAST line is compared: a module-loaded notice (`[mod] …`, `[infiverse mod] …`)
is not an answer, so the noise prefixes are dropped before the last line is taken, and
the interpreter is run with `--no-mods` for the same reason.  stderr is never a
criterion -- it is carried into the diff report so a reader can tell a refusal from a
wrong value.

A REFUSAL counts as an outcome, and this is deliberate: if all three refuse with the
same status and the same (empty) last line, they agree, exactly as `%`-by-zero agrees
in `tools/mod_spec.py`.  What must not happen is one backend printing an answer while
another refuses, or two backends printing different answers.

The numeric subset, and what is pinned instead of hidden
--------------------------------------------------------
The AOT backend compiles the "numeric subset" (`src/compilation/aot_native.c` says so
in as many words: `expression kind %d is outside the numeric subset`) and the wasm
backend implements the wasm MVP subset (no strings, no collections).  So a set, a
closure, a thread or a `try` block is refused by both of them while the interpreter
runs it.  That is a design boundary, not a defect, and it is recorded in
KNOWN_DIVERGENT with the backends that refuse and the reason.

KNOWN_DIVERGENT is a PIN, not an excuse:
  * the entry names the refusing backends (`aot+wasm refuse: …`); the run requires
    exactly those backends to refuse and the interpreter to run;
  * a case whose entry no longer diverges is a FAILURE that asks for the entry to be
    removed -- the set may only shrink;
  * a case that is not in the set may never diverge; a new divergence is red.

That asymmetry is the point: if the wasm backend grows set support tomorrow, this test
goes red and somebody has to say so out loud (and delete the entry), instead of the
new capability sliding in under a blanket "known difference".

`--self-check` proves the judge can fail: it feeds the judge a fabricated disagreement
and requires a red result, because a harness that cannot be made to fail describes the
implementations instead of testing them.

WHAT INVALIDATES THIS MATRIX (read before trusting a green run)
---------------------------------------------------------------
It is valid only on the region it was built on: the interpreter's language surface,
crossed with the AOT/wasm subsets as they are today.  The known entries pin the
REFUSAL side, so:

  * the moment the AOT or the wasm backend accepts sets, closures, threads, locks,
    `declare`, `record` or `try`, the pinned case stops diverging: the pin turns red
    (the alarm working), but every case that STAYS pinned keeps passing.  So a green
    run with a non-empty KNOWN_DIVERGENT says "still refused", not "consistent" --
    a shrinking refusal surface and a passing matrix are the same output until the
    entry is deleted.
  * `--strict` is for that handover: with it a non-empty KNOWN_DIVERGENT is itself a
    failure.  Turn it on once the backends are supposed to have closed the gap (the
    P3/P4 migration), so the matrix cannot keep going green on a stale boundary.
  * only the last answer line is compared, and only for the corpus below.  A new
    operator is invisible until a case is added for it.

Usage:
    three_way_parity.test.py <inimerse> <aot-native> [--node NODE] [--wasm-runner PATH]
                             [--verbose] [--jobs N] [--strict]
                             [--negative-control interp|aot|wasm|all [--cases N]]
    three_way_parity.test.py --self-check
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

EXIT_CRASH = 'crashed'

# Module-loaded notices are not answers; the last line of stdout must be the program's.
NOISE_PREFIXES = ('[mod]', '[infiverse', '[verse_dist')

BACKENDS = ('interp', 'aot', 'wasm')

# A pinned refusal must SAY it refused for the design reason: a backend that dies for
# an unrelated cause (missing binary, bad path) also exits non-zero, and accepting that
# would let a broken backend pass as a design boundary.
REFUSAL_MARKER = {
    'aot': 'outside the numeric subset',
    'wasm': 'wasm MVP subset',
}

# (name, family, source).  Each program ends with a `say` whose line is the answer,
# and each family has at least two cases.  The corpus is written here on purpose: a
# parity matrix that reads its programs from disk measures the disk too.
CASES = [
    # -- integer and float arithmetic -------------------------------------------
    ('arith-int', 'arith',
     'say (7 * 3) - (7 / 2)\n'),
    ('arith-float', 'arith',
     'say 1.5 * 4 + 0.25\n'),
    ('arith-int32-mul', 'arith',
     'x = 2147483647\nsay x * 2\n'),
    ('arith-int64-overflow', 'arith',
     'x = 9223372036854775807\nsay x + 1\n'),
    ('arith-mixed', 'arith',
     'say ((2 + 3) * (4 - 1)) / 2\n'),

    # -- comparisons, including a chain ------------------------------------------
    ('cmp-chain-true', 'compare',
     'a = 1\nb = 2\nc = 3\nsay a < b < c\n'),
    ('cmp-chain-false', 'compare',
     'a = 1\nb = 2\nc = 3\nsay b < a < c\n'),
    ('cmp-int-float', 'compare',
     'say 1 == 1.0\nsay 2.5 > 2\n'),
    ('cmp-float-sum', 'compare',
     'say 0.1 + 0.2 == 0.3\nsay 0.1 + 0.2 < 0.3\n'),

    # -- and / or / not, value position, with short-circuit ---------------------
    ('logic-and-value', 'logic',
     'say 1 and 5\n'),
    ('logic-or-value', 'logic',
     'say 0 or 3\n'),
    ('logic-not', 'logic',
     'say not 0\nsay not 1\n'),
    ('logic-short-circuit', 'logic',
     'z = 0\nsay 0 and (1 / z)\n'),

    # -- division and modulo by zero (agreement is "all three refuse") ---------
    ('div-by-zero', 'divmod',
     'say 1 / 0\n'),
    ('mod-by-zero', 'divmod',
     'say 1 % 0\n'),
    ('mod-negative', 'divmod',
     'say -7 % 3\nsay 7 % -3\n'),

    # -- functions and recursion -------------------------------------------------
    ('func-call', 'func',
     'func f(x) { return x * 2 + 1 }\nsay f(5)\n'),
    ('func-recursion', 'func',
     'func fib(n) { if n < 2 { return n }\n    return fib(n - 1) + fib(n - 2) }\nsay fib(10)\n'),

    # -- control flow on numbers -------------------------------------------------
    ('ctrl-while', 'control',
     'i = 0\ns = 0\nwhile i < 5 { s = s + i\ni = i + 1 }\nsay s\n'),
    ('ctrl-if', 'control',
     'x = 5\nr = 0\nif x > 3 { r = 1 } else { r = 0 }\nsay r\n'),
    ('ctrl-for-range', 'control',
     's = 0\nfor i in 1..4 { s = s + i }\nsay s\n'),

    # -- sets (design-refused by both compiled backends) -------------------------
    ('set-literal', 'set',
     'b = 1, 2, 7\nsay min(b)\nsay max(b)\n'),
    ('set-membership', 'set',
     'b = 1, 2, 7\nsay 2 in b\nsay 9 in b\n'),
    ('set-length', 'set',
     'b = 1, 2, 7\nsay len(b)\n'),

    # -- intervals (SET_INTERVAL / IN / MIN / MAX over a component) --------------
    ('interval-len', 'interval',
     'z = Z[1~3]\nsay len(z)\n'),
    ('interval-max', 'interval',
     'z = Z[1~3]\nsay max(z)\n'),

    # -- closures ---------------------------------------------------------------
    ('closure-plain', 'closure',
     'double = x -> x * 2\nsay double(21)\n'),
    ('closure-capture', 'closure',
     'func make_nested(base) { return y -> x -> x + y + base }\n'
     'outer = make_nested(2)\ninner = outer(3)\nsay inner(4)\n'),

    # -- threads and locks --------------------------------------------------------
    ('thread-await', 'thread',
     'thread w { wait 0.01\n    return 7 }\nstart w\n'
     'r = thread_await("w")\nsay result_value(r)\n'),
    ('thread-join', 'thread',
     'thread w2 { return 5 * 5 }\nstart w2\njoin w2\nsay 25\n'),
    ('lock-block', 'thread',
     'lock m { x = 1 }\nsay x\n'),

    # -- resource declarations and records --------------------------------------
    ('declare-limits', 'declare',
     'declare { threads 4 }\nsay 1\n'),
    ('record-value', 'record',
     'record x = 5\nsay x\n'),

    # -- exceptions --------------------------------------------------------------
    ('try-throw-catch', 'exception',
     'try { throw 1 } catch (e) { say 7 }\n'),
    ('try-div-zero', 'exception',
     'try { x = 1 / 0 } catch (e) { x = 8 }\nsay x\n'),
]

# name -> "refusing-backends reason".  The part before ':' names the backends that
# must refuse, joined by '+'.  Only these cases may disagree; the set may only shrink.
# The refusal surface, one row per family: family | owner | refusal | minimal repro.
# This is the table to copy into docs/IR_DESIGN.md as the surface P3/P4 should shrink;
# the `owner` column is the backends that refuse, and the repro is the shortest program
# that shows it (two lines, the second one is the `say` that never runs there).
#
#   family   | owner     | refusal                                              | minimal repro
#   ---------|-----------|------------------------------------------------------|------------------------------
#   control  | aot+wasm  | AOT: statement kind 10 outside the numeric subset;    | `s = 0`
#            |           | wasm: statement type not supported by the MVP subset  | `for i in 1..4 { s = s + i }`
#   set      | aot+wasm  | AOT: expression kind 13; wasm: expression type 13     | `b = 1, 2, 7`
#            |           | (strings/collections need the interpreter)            | `say min(b)`
#   interval | aot+wasm  | same as set (Z[a~b] is a set component)               | `z = Z[1~3]`
#            |           |                                                       | `say len(z)`
#   closure  | aot+wasm  | AOT: expression kind 17; wasm: expression type 17;    | `double = x -> x * 2`
#            |           | capture: function '<name>' not found (builtins are    | `say double(21)`
#            |           | not in the wasm MVP subset)                           |
#   thread   | aot+wasm  | AOT: statement kind 6; wasm: statement type not       | `thread w { return 7 }`
#            |           | supported; lock: statement kind 33                    | `start w`
#   declare  | aot+wasm  | AOT: statement kind 37                                | `declare { threads 4 }`
#   record   | aot+wasm  | AOT: statement kind 40                                | `record x = 5`
#   exception| aot+wasm  | AOT: statement kind 45                                | `try { throw 1 } catch (e) { say 7 }`
#
# Nothing here is a defect: the numeric subset and the wasm MVP subset are documented
# boundaries (src/compilation/aot_native.c says "outside the numeric subset" itself).
# The rows exist so that "how wide is the refusal" has a number and a repro.
# Who owns closing the AOT/wasm side of each family.  `unassigned` is a real answer, not
# a missing one: the column exists so the question "who shrinks this refusal" has a place
# in the reading, and so an empty owner can never be mistaken for nobody having asked.
# Fill one line in when a family gets a taker; the reading then names them.
FAMILY_OWNER = {
    'arith': 'unassigned', 'compare': 'unassigned', 'logic': 'unassigned',
    'divmod': 'unassigned', 'func': 'unassigned', 'control': 'unassigned',
    'set': 'unassigned', 'interval': 'unassigned', 'closure': 'unassigned',
    'thread': 'unassigned', 'declare': 'unassigned', 'record': 'unassigned',
    'exception': 'unassigned',
}

KNOWN_DIVERGENT = {
    'ctrl-for-range':      'aot+wasm refuse: for-in over a range is outside the numeric / wasm MVP subset',
    'set-literal':         'aot+wasm refuse: a set literal is outside the numeric / wasm MVP subset',
    'set-membership':      'aot+wasm refuse: a set is outside the numeric / wasm MVP subset',
    'set-length':          'aot+wasm refuse: a set is outside the numeric / wasm MVP subset',
    'interval-len':        'aot+wasm refuse: Z[a~b] is a set component, outside both subsets',
    'interval-max':        'aot+wasm refuse: Z[a~b] is a set component, outside both subsets',
    'closure-plain':       'aot+wasm refuse: a lambda is a function value, outside the numeric subset',
    'closure-capture':     'aot+wasm refuse: a closure capture is outside the numeric subset',
    'thread-await':        'aot+wasm refuse: a thread statement is outside the numeric subset',
    'thread-join':         'aot+wasm refuse: a thread statement is outside the numeric subset',
    'lock-block':          'aot+wasm refuse: lock is outside the numeric subset',
    'declare-limits':      'aot+wasm refuse: a resource declaration is outside the numeric subset',
    'record-value':        'aot+wasm refuse: record is outside the numeric subset',
    'try-throw-catch':     'aot+wasm refuse: try/catch is outside the numeric subset',
    'try-div-zero':        'aot+wasm refuse: try/catch is outside the numeric subset',
}


def last_line(out):
    """Last non-empty stdout line that is not a module-loaded notice."""
    for ln in reversed(out.splitlines()):
        s = ln.strip()
        if s and not s.startswith(NOISE_PREFIXES):
            return s
    return ''


def first_stderr(err):
    for ln in err.splitlines():
        s = ln.strip()
        if s and not s.startswith(NOISE_PREFIXES):
            return s
    return ''


def run(cmd, **kw):
    """Run a leg, and turn "the binary is not there" into an outcome rather than a crash.

    A missing or non-executable binary raises OSError out of subprocess.run; letting it
    escape would abort the matrix on the first case instead of reporting that backend
    as FAIL, which is exactly what a negative control has to be able to see.
    """
    try:
        return subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8',
                              errors='replace', timeout=180, check=False, **kw)
    except OSError as e:
        return subprocess.CompletedProcess(cmd, -1, '',
                                           'cannot run %s: %s' % (cmd[0], e))


def observe(r):
    """(exit status, last answer line, why, both streams) -- a signal death is its own status."""
    rc = EXIT_CRASH if r.returncode < 0 else r.returncode
    diag = (r.stderr or '') + '\n' + (r.stdout or '')
    return (rc, last_line(r.stdout), first_stderr(r.stderr), diag)


def via_interpreter(engine, src, td, tag):
    f = Path(td) / (tag + '.im')
    f.write_text(src, encoding='utf-8')
    return observe(run([engine, '--no-mods', str(f)]))


def via_native(engine, aot, src, td, tag):
    f = Path(td) / (tag + '.im')
    c = Path(td) / (tag + '.c')
    b = Path(td) / (tag + '.native')
    f.write_text(src, encoding='utf-8')
    r = run([aot, 'translate', str(f), str(c)])
    if r.returncode != 0:
        return observe(r)
    r = run([os.environ.get('CC', 'cc'), '-O2', '-w', str(c), '-o', str(b)])
    if r.returncode != 0:
        return observe(r)
    return observe(run([str(b)]))


def via_wasm(engine, node, runner, src, td, tag):
    f = Path(td) / (tag + '.im')
    w = Path(td) / (tag + '.wasm')
    f.write_text(src, encoding='utf-8')
    r = run([engine, 'compile', '--abi-target', 'wasm', str(f), str(w)])
    if r.returncode != 0:
        return observe(r)
    return observe(run([node, runner, str(w)]))


def pinned_backends(reason):
    """The backends the KNOWN_DIVERGENT reason says must refuse.

    The reason is written `aot+wasm refuse: …`, so the backend list is the FIRST
    whitespace-delimited token before the colon -- splitting the whole head on '+'
    would leave `wasm refuse` behind and silently pin the wrong set (the
    self-check caught exactly that).
    """
    head = reason.split(':', 1)[0].strip()
    token = head.split()[0] if head.split() else ''
    return tuple(part for part in token.split('+') if part)


def judge(name, runs):
    """Return (diff lines, known lines).  `runs` maps backend -> (rc, out, why)."""
    seen = [(runs[b][0], runs[b][1]) for b in BACKENDS]
    all_crashed = all(runs[b][0] == EXIT_CRASH for b in BACKENDS)
    if len(set(seen)) == 1 and not all_crashed:
        if name in KNOWN_DIVERGENT:
            return [], ['THREE-WAY-KNOWN|case=%s|state=no-longer-diverges'
                        % name], 'shrink'
        return [], [], 'agree'

    if name in KNOWN_DIVERGENT:
        reason = KNOWN_DIVERGENT[name]
        want = pinned_backends(reason)
        running = tuple(b for b in BACKENDS if runs[b][0] == 0)
        refusing = tuple(b for b in BACKENDS if runs[b][0] != 0)
        ok = (refusing == want) and (set(running) == set(BACKENDS) - set(want))
        marker_missing = tuple(b for b in want
                               if REFUSAL_MARKER[b] and REFUSAL_MARKER[b] not in runs[b][3])
        if not ok or marker_missing:
            # Name the backends that DEVIATED, not everyone involved: with a dead
            # interpreter every pinned case would otherwise blame the two refusing
            # backends, which are the ones behaving exactly as pinned.
            bad = sorted(set(want) ^ set(refusing)) if not ok else list(marker_missing)
            if not ok:
                detail = 'want-refusing=%s|got-refusing=%s' % (
                    '+'.join(want) or 'none', '+'.join(refusing) or 'none')
            else:
                detail = 'refused-without-the-pinned-reason=%s' % '+'.join(marker_missing)
            lines = ['THREE-WAY-PIN|case=%s|%s|backends=%s|reason=%s'
                     % (name, detail, '+'.join(bad) or '+'.join(sorted(set(want) | set(refusing))),
                        reason)]
            return lines, [], 'pin'
        return [], ['THREE-WAY-KNOWN|case=%s|refused-by=%s' % (name, '+'.join(want))], 'known'

    # Unexplained disagreement: one DIFF line per backend that is not on the
    # majority value (the interpreter breaks a three-way split, and the line says so).
    # A crashed backend never answered, so it is not evidence about the language.  If it
    # were allowed to vote, two crashes would outvote the one backend that did answer and
    # the report would nominate the healthy backend as the odd one out (a dead interpreter
    # blamed the AOT).  Crashes are counted as deviations below, not as values.
    survivors = [b for b in BACKENDS if runs[b][0] != EXIT_CRASH]
    if not survivors:
        # Nothing answered: that is three legs dying, not three backends agreeing.  It has
        # to be reported as lines, because an empty survivor set otherwise falls through the
        # majority code with zero deviations and the run prints disagree=0 and exits 0.
        return (['THREE-WAY-DIFF|case=%s|backend=%s|want=%r|got=%r|rc=%r|basis=all-crashed|why=%s'
                 % (name, b, '<no leg answered>', runs[b][1], runs[b][0], runs[b][3] or '-')
                 for b in BACKENDS], [], 'diverge')
    counts = {}
    for b in survivors or BACKENDS:
        counts[(runs[b][0], runs[b][1])] = counts.get((runs[b][0], runs[b][1]), 0) + 1
    top = max(counts.values())
    majority = [v for v, n in counts.items() if n == top]
    lines = []
    if len(majority) == 1:
        want, how = majority[0], 'majority-of-survivors' if survivors else 'majority'
    elif 'interp' in survivors:
        want, how = (runs['interp'][0], runs['interp'][1]), 'interpreter'
    else:
        want, how = (list(counts)[0]), 'first-survivor'
    for b in BACKENDS:
        rc, out, why = runs[b][:3]
        if (rc, out) != want:
            lines.append('THREE-WAY-DIFF|case=%s|backend=%s|want=%r|got=%r|rc=%r|basis=%s|why=%s'
                         % (name, b, want[1], out, rc, how, why or '-'))
    return lines, [], 'diverge'


def self_check():
    """Prove the judge can fail: a fabricated disagreement must be red."""
    def fake(rc, out, diag=''):
        return (rc, out, '', diag)

    agree = {b: fake(0, '1') for b in BACKENDS}
    if judge('self-check-agree', agree)[2] != 'agree':
        raise SystemExit('self-check failed: the judge rejected an agreement')
    split = {'interp': fake(0, '1'), 'aot': fake(0, '2'), 'wasm': fake(0, '1')}
    lines, _, state = judge('self-check-split', split)
    if state != 'diverge' or not lines:
        raise SystemExit('self-check failed: the judge accepted a disagreement')
    refused = {'interp': fake(0, '1'),
               'aot': fake(1, '', REFUSAL_MARKER['aot']),
               'wasm': fake(1, '', REFUSAL_MARKER['wasm'])}
    # The hazard the negative control exists for: three legs that all died have the SAME
    # (rc, out) pair, so a judge that compares tuples reports agreement.  A dead engine
    # must never be able to turn the matrix green.
    all_dead = {b: fake(EXIT_CRASH, '') for b in BACKENDS}
    if judge('self-check-all-crashed', all_dead)[2] == 'agree':
        raise SystemExit('self-check failed: three crashed legs were accepted as agreement')

    if judge('self-check-refused', refused)[2] != 'diverge':
        raise SystemExit('self-check failed: a refusal beside an answer was accepted')
    KNOWN_DIVERGENT['self-check-known'] = 'aot+wasm refuse: fabricated pin'
    try:
        pinned, _, state = judge('self-check-known', refused)
        if state != 'known' or pinned:
            raise SystemExit('self-check failed: a correctly pinned refusal was not known')
        drifted = {'interp': fake(0, '1'), 'aot': fake(0, '2'), 'wasm': fake(0, '1')}
        bad_lines, _, state = judge('self-check-known', drifted)
        if state != 'pin' or not bad_lines:
            raise SystemExit('self-check failed: a pin accepted a different refusal map')
        # A backend that dies for an unrelated reason must not pass as a design refusal.
        mute = {'interp': fake(0, '1'), 'aot': fake(1, '', 'aot_native: cannot open file'),
                'wasm': fake(1, '', REFUSAL_MARKER['wasm'])}
        mute_lines, _, state = judge('self-check-known', mute)
        if state != 'pin' or not mute_lines:
            raise SystemExit('self-check failed: a non-design refusal was accepted as pinned')
    finally:
        del KNOWN_DIVERGENT['self-check-known']
    print('THREE-WAY-SELFCHECK|red=yes|checked=agree,split,refused,pin,pin-without-reason,all-crashed')


def main():
    if '--self-check' in sys.argv:
        self_check()
        return
    if len(sys.argv) < 3:
        raise SystemExit('usage: three_way_parity.test.py <inimerse> <aot-native> '
                         '[--node NODE] [--wasm-runner PATH] [--self-check]')
    engine, aot = sys.argv[1], sys.argv[2]
    node = os.environ.get('NODE', 'node')
    runner = str(Path(__file__).resolve().parent / 'wasm_run.js')
    argv = sys.argv[3:]
    for i, a in enumerate(argv):
        if a == '--node':
            node = argv[i + 1]
        elif a == '--wasm-runner':
            runner = argv[i + 1]
    verbose = '--verbose' in argv
    strict = '--strict' in argv
    negctl = None
    for i, a in enumerate(argv):
        if a == '--negative-control':
            negctl = argv[i + 1]
    if negctl is not None and negctl != 'all' and negctl not in BACKENDS:
        raise SystemExit('--negative-control wants %s or all' % '|'.join(BACKENDS))
    jobs = 1
    for i, a in enumerate(argv):
        if a == '--jobs':
            jobs = max(1, int(argv[i + 1]))

    names = {name for name, _family, _src in CASES}
    stale = sorted(set(KNOWN_DIVERGENT) - names)
    if stale:
        raise SystemExit('KNOWN_DIVERGENT names cases that do not exist: %s' % ', '.join(stale))
    if strict and KNOWN_DIVERGENT:
        print('THREE-WAY-STRICT|known=%d|result=FAIL' % len(KNOWN_DIVERGENT))
        raise SystemExit('--strict: %d known-divergent entries remain (%s); the refusal '
                         'surface must be empty before this passes.  Delete an entry only '
                         'when the backend really stopped refusing.'
                         % (len(KNOWN_DIVERGENT), ', '.join(sorted(KNOWN_DIVERGENT))))

    # A negative control points one (or all) of the legs at a binary that cannot run.
    # The harness must then report a disagreement and exit non-zero -- never "all three
    # agreed", which is what three identical failures look like to a tuple comparison.
    leg = {}
    cases = CASES
    if negctl:
        nctl = 3
        for i, a in enumerate(argv):
            if a == '--cases':
                nctl = max(1, int(argv[i + 1]))
        cases = CASES[:nctl]
        for b in (BACKENDS if negctl == 'all' else (negctl,)):
            leg[b] = '/nonexistent/im-%s' % b

    def run_case(i, td):
        src = cases[i][2]
        tag = 'c%02d' % i
        return {
            'interp': via_interpreter(leg.get('interp', engine), src, td, tag),
            'aot': via_native(engine, leg.get('aot', aot), src, td, tag),
            'wasm': via_wasm(engine, leg.get('wasm', node), runner, src, td, tag),
        }

    diffs, known, pins, shrink = [], [], [], []
    table, by_family = [], {}
    with tempfile.TemporaryDirectory(prefix='three-way-') as td:
        if jobs > 1:
            from concurrent.futures import ThreadPoolExecutor
            with ThreadPoolExecutor(max_workers=jobs) as pool:
                results = list(pool.map(lambda i: run_case(i, td), range(len(cases))))
        else:
            results = [run_case(i, td) for i in range(len(cases))]
        for (name, family, _src), runs in zip(cases, results):
            table.append((name, family, runs))
            by_family.setdefault(family, []).append((name, runs))
            lines, known_lines, state = judge(name, runs)
            if state == 'diverge':
                diffs.extend(lines)
            elif state == 'pin':
                pins.extend(lines)
            elif state == 'shrink':
                shrink.extend(known_lines)
            elif state == 'known':
                known.extend(known_lines)

    def kind_of(name, runs):
        if name in KNOWN_DIVERGENT:
            return 'known'
        if all(runs[b][0] == 0 for b in BACKENDS):
            return 'answer'
        if len({(runs[b][0], runs[b][1]) for b in BACKENDS}) == 1:
            return 'refused-all'
        return 'red'

    families = len(by_family)
    broken = set()
    for line in diffs + pins:
        for b in BACKENDS:
            if '|backend=%s|' % b in line:
                broken.add(b)
            elif '|backends=' in line and b in line.split('|backends=', 1)[1].split('|')[0].split('+'):
                broken.add(b)
    flags = '|'.join('%s=%s' % (b, 'FAIL' if b in broken else 'ok') for b in BACKENDS)
    answered = sum(1 for _n, _f, r in table if kind_of(_n, r) == 'answer')
    refused_all = sum(1 for _n, _f, r in table if kind_of(_n, r) == 'refused-all')
    print('THREE-WAY|rule=parity-v1|cases=%d|families=%d|%s|disagree=%d'
          % (len(cases), families, flags, len(diffs) + len(pins)))
    # Agreement reached by all three REFUSING is a different fact from agreement on an
    # answer; say which is which instead of letting one number cover both.
    print('THREE-WAY-SHAPE|answered-by-all=%d|refused-by-all=%d|known-divergent=%d'
          % (answered, refused_all, len(known)))
    # A family that loses cases must be visible without recomputing the total.
    for family, rows in by_family.items():
        kinds = [kind_of(n, r) for n, r in rows]
        owner = [b for b in BACKENDS
                 if any(k in ('known', 'refused-all', 'red') and r[b][0] != 0
                        for (n, r), k in zip(rows, kinds))]
        # `owner` is who must close this family on the AOT/wasm side (unassigned is a
        # real answer); `refused-by` is which backends refuse it today.  Two columns,
        # because "nobody owns it" and "the backends refuse it" are different facts.
        print('THREE-WAY-FAMILY|family=%s|cases=%d|agreed=%d|refused=%d|owner=%s'
              '|refused-by=%s|diverged=%d'
              % (family, len(rows), kinds.count('answer'),
                 kinds.count('known') + kinds.count('refused-all'),
                 FAMILY_OWNER.get(family, 'unassigned'),
                 '+'.join(owner) or 'none', kinds.count('red')))
    if verbose:
        for name, _family, r in table:
            print('THREE-WAY-CASE|case=%s|%s' % (name, '|'.join(
                '%s=%s,%r' % (b, r[b][0], r[b][1]) for b in BACKENDS)))
    if known:
        print('THREE-WAY-KNOWN|n=%d' % len(known))
        for line in known:
            print(line)
    for line in diffs + pins + shrink:
        print(line)
    if negctl:
        red = len(diffs) + len(pins) + len(shrink)
        if red:
            print('THREE-WAY-NEGCTL|leg=%s|cases=%d|harness-red=yes|disagree=%d|result=PASS'
                  % (negctl, len(cases), red))
            raise SystemExit(0)
        print('THREE-WAY-NEGCTL|leg=%s|cases=%d|harness-red=NO|disagree=0|result=FAIL'
              '|why=a broken %s leg was reported as agreement'
              % (negctl, len(cases), negctl))
        raise SystemExit(1)
    if diffs or pins or shrink:
        raise SystemExit('three way parity: %d case(s) diverge (%d unexplained, %d pin drift, '
                         '%d known-divergent entry to remove)'
                         % (len(diffs) + len(pins) + len(shrink), len(diffs), len(pins), len(shrink)))
    print('three way parity: ok (%d cases, %d families, 3 backends agree)'
          % (len(cases), families))

if __name__ == '__main__':
    main()
