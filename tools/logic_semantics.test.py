#!/usr/bin/env python3
"""`and`/`or`/`not` must mean the same thing on all three backends (docs/AUDIT.md §1.0, §5 O0).

Before this test existed one operator had three answers:

  * the interpreter's compiler returned the deciding OPERAND (`1 and 5` -> 5,
    `0 or 5` -> 5, `2 or 3` -> 2),
  * the AOT backend returned a BOOLEAN (nv_boo(... && ...)),
  * the wasm backend refused to compile it outside a condition at all
    ("'and'/'or' outside a condition is not supported").

The agreed rule, asserted here:

  * `a and b` / `a or b` are TRUTH-VALUES in every position, not operands.
    Value position and condition position now give the same answer.
  * Truthiness: `false`, `0`, `0.0` and `nil` are false; every other value is
    true (so the empty string is true, matching the interpreter's rule).
  * `and` binds tighter than `or`, and both associate left.
  * `not` negates the truth-value of its operand.
  * SHORT-CIRCUIT EVALUATION IS PRESERVED.  The right operand must not be
    evaluated when the left already decides the answer.  SHORT_CIRCUIT below
    proves this with a division by zero: a right operand that runs is refused
    with `division_by_zero`, so "refused" and "ran" are the same observation.
    Each operator is probed in BOTH directions -- the direction that must not
    short-circuit has to refuse, otherwise a right operand that never ran at
    all would look like a passing short-circuit.

The proof is deliberately string-free: strings, closures and globals are all
outside the AOT/wasm numeric subset (or, for a global written inside a
function, an already-pinned divergence), so a marker built from any of them
would measure the wrong thing.

This test is deliberately narrow: it asserts agreement on the cases all three
backends accept.  Strings/collections are outside the wasm MVP subset, and
their divergence is a separate, documented limitation, not something this test
should paper over by silently skipping.

Two further families live here for the same reason -- one place where an
operator's meaning is asserted across all three backends:

  * a `+` chain of three or more terms (CHAIN).  `src/compiler/compiler.c`
    flattens such a chain into ONE `OP_CONCAT` while a two-term chain emits
    `OP_ADD`; the flattening claimed "identical per-step semantics to OP_ADD"
    and was not, for integer overflow: with `x = 2147483647`, `x + 1` was
    2147483648 and `x + 1 + 0` was -2147483648.  See docs/AUDIT.md §1.7.
  * printed floats (FLOAT).  Each backend had its own formatter -- `%g` in AOT,
    a JS re-implementation in `tools/wasm_run.js`, `vts_double` in the
    interpreter -- and they disagreed on the sign of a small negative, on a
    fraction that rounds up into the integer part, and on one that rounds away
    entirely.  All three now follow `vts_double`.  Its `|d| >= 1e15` arm calls
    `%.17g`, so the JS side has to port glibc there -- including the half-to-even
    tie rule, which ECMAScript's `toPrecision(17)` gets wrong: 1.0000000000000002e15
    is exactly 1000000000000000.25, and on that tie the two pick different digits.

It also caught a real, pre-existing bug the day it was written: the wasm
backend applied i32.eqz twice for `not`, so every `not` was inverted
(`not 0` printed false on wasm while interpreter and AOT printed true).
"""
import case_counts
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REFUSED = object()

# (expression, expected last stdout line) -- wrapped as `say <expr>`.
TABLE = [
    # value position: the O0 core.  These eight used to read 5, 3, 0, 0, 5, 1,
    # 2, 0 from the interpreter while AOT said true/true/false/false/true/...
    ("1 and 5",                  "true"),
    ("2 and 3",                  "true"),
    ("0 and 5",                  "false"),
    ("1 and 0",                  "false"),
    ("0 or 5",                   "true"),
    ("1 or 5",                   "true"),
    ("2 or 3",                   "true"),
    ("0 or 0",                   "false"),
    # truthiness of non-integer numbers
    ("0.0 and 1",                "false"),
    ("1.5 and 2",                "true"),
    ("0.0 or 0",                 "false"),
    ("0 or 0.0",                 "false"),
    # precedence: `and` binds tighter than `or`
    ("1 or 0 and 0",             "true"),
    ("0 and 0 or 1",             "true"),
    # chaining and nesting
    ("1 and 1 and 0",            "false"),
    ("(1 and 0) or 1",           "true"),
    ("(1 < 2) and (3 < 2)",      "false"),
    # `not` -- the inverted-on-wasm case
    ("not 0",                    "true"),
    ("not 1",                    "false"),
    ("not 0.0",                  "true"),
    ("not (0 or 0)",             "true"),
    ("not (0 and 0)",            "true"),
    ("not (1 or 0)",             "false"),
    ("not 1 and 1",              "false"),
]

# (condition expression, expected last stdout line) -- condition position must
# keep working after the change.
COND = [
    ("1 and 5", "11"),
    ("0 and 5", "22"),
    ("0 or 5",  "11"),
    ("0 or 0",  "22"),
    ("not 0",   "11"),
    ("not 1",   "22"),
]

# (expression, expectation) with `z = 0` prepended.  REFUSED means the right
# operand ran and divided by zero; a value means it was short-circuited away.
SHORT_CIRCUIT = [
    ("0 and (1 / z)", "false"),   # and short-circuits on false
    ("1 and (1 / z)", REFUSED),   # control: and must evaluate the right side
    ("1 or (1 / z)",  "true"),    # or short-circuits on true
    ("0 or (1 / z)",  REFUSED),   # control: or must evaluate the right side
]

# (label, source, expected last stdout line) -- a `+` chain must not answer
# differently depending on how many terms it has.  `src/compiler/compiler.c`
# flattens a chain of three or more operands into ONE `OP_CONCAT` while a
# two-term chain emits `OP_ADD`; the flattening claimed "identical per-step
# semantics to OP_ADD" and was not, for integer overflow.  Measured before the
# fix, with `x = 2147483647`: `x + 1` was 2147483648 and `x + 1 + 0` was
# -2147483648 -- the same expression, two answers, decided by term count.  AOT
# and wasm agreed with each other throughout, so the interpreter's OP_CONCAT was
# the outlier, not a two-way disagreement.  An operand that is itself a
# parenthesized expression is excluded by the flattening guard, so the last row
# is the control that proves both paths stay in step.
CHAIN = [
    # the two-term form is the reference every chain must match
    ('2-term overflow',      'x = 2147483647\nsay(x + 1)\n',       '2147483648'),
    ('3-term +0',            'x = 2147483647\nsay(x + 1 + 0)\n',   '2147483648'),
    ('3-term +1',            'x = 2147483647\nsay(x + 1 + 1)\n',   '2147483649'),
    ('4-term +0+0',          'x = 2147483647\nsay(x + 1 + 0 + 0)\n',
     '2147483648'),
    ('3-term identity',      'y = 1000000000\nsay(y + y + y)\n',   '3000000000'),
    ('3-term 2e9+2e9+1',     'x = 2000000000\nsay(x + 2000000000 + 1)\n',
     '4000000001'),
    ('3-term literal chain', 'say(2147483647 + 1 + 0)\n',          '2147483648'),
    # a chain that stays in range must be untouched by the promotion rule
    ('in-range 3-term',      'x = 10\nsay(x + 1 + 0)\n',           '11'),
    ('in-range to boundary', 'x = 2147483646\nsay(x + 1 + 0)\n',   '2147483647'),
    # control: parenthesized operands defeat the flattening, so this always took
    # the OP_ADD path; both paths must agree here too
    ('parenthesized control', 'x = 0\nsay(x + (0 - 2147483647) + (0 - 2))\n',
     '-2147483649'),
]

# (expression, expected last stdout line) -- printed floats must agree across
# all three backends.  `vts_double` (`src/vm/vm.c`) is the spec: nan -> "nan",
# zero -> "0", an integral value -> its integer text, |v| >= 1e15 -> "%.17g",
# otherwise the integer part plus up to six decimals with trailing zeros
# trimmed (and no bare "." once the fraction rounds away).  AOT's
# `nv_fmt_double` and wasm's `fmtFloat` (`tools/wasm_run.js`) are ports of it.
# Every row below is a case that a naive `%g`, a truncating six-decimal
# formatter, or one that loses the sign of a small negative gets wrong.
FLOAT = [
    ('1.0 / 3.0',              '0.333333'),
    ('2.0 / 3.0',              '0.666667'),
    ('123456789.125',          '123456789.125'),
    ('1.0',                    '1'),
    ('0.1 + 0.2',              '0.3'),
    ('2147483648.5',           '2147483648.5'),
    ('0.000001',               '0.000001'),
    ('1e-20',                  '0'),
    ('0.9999999',              '1'),
    ('0.0 - 0.5',              '-0.5'),
    ('0.0 - 1e-20',            '-0'),
    ('0.0 - 1.0 / 3.0',        '-0.333333'),
    ('1e20',                   '1e+20'),
    ('12345678901234567890.0', '1.2345678901234567e+19'),
    # |d| >= 1e15 goes through the %.17g path in C and an exact-expansion port
    # of it in the wasm runner.  A JS toPrecision(17) gets the first three
    # wrong: 1.0000000000000002e15 is exactly 1000000000000000.25, and on a tie
    # ECMAScript rounds "to the larger n" where glibc rounds half-to-even.
    ('1.0000000000000002e15',      '1000000000000000.2'),
    ('2000000000000000.25',        '2000000000000000.2'),
    ('0.0 - 1.0000000000000002e15', '-1000000000000000.2'),
    ('1500000000000000.5',         '1500000000000000.5'),
    ('1.5e15',                     '1500000000000000'),
    ('1e16',                       '10000000000000000'),
]

EXIT_CRASH = 'crashed'


def last_line(out):
    for ln in reversed(out.splitlines()):
        if ln.strip():
            return ln.strip()
    return ''


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
                          check=False, **kw)


def via_interpreter(engine, src, td, tag):
    f = Path(td) / (tag + '.im')
    f.write_text(src, encoding='utf-8')
    r = run([engine, '--no-mods', str(f)])
    if r.returncode < 0:
        return EXIT_CRASH, ''
    return r.returncode, r.stdout


def via_native(engine, aot, src, td, tag):
    f = Path(td) / (tag + '.im')
    c = Path(td) / (tag + '.c')
    b = Path(td) / (tag + '.native')
    f.write_text(src, encoding='utf-8')
    r = run([aot, 'translate', str(f), str(c)])
    if r.returncode != 0:
        return r.returncode, 'TRANSLATE_REFUSED'
    r = run([os.environ.get('CC', 'cc'), '-O2', '-w', str(c), '-o', str(b)])
    if r.returncode != 0:
        return r.returncode, 'COMPILE_FAIL'
    r = run([str(b)])
    if r.returncode < 0:
        return EXIT_CRASH, ''
    return r.returncode, r.stdout


def via_wasm(engine, node, runner, src, td, tag):
    f = Path(td) / (tag + '.im')
    w = Path(td) / (tag + '.wasm')
    f.write_text(src, encoding='utf-8')
    r = run([engine, 'compile', '--abi-target', 'wasm', str(f), str(w)])
    if r.returncode != 0:
        return r.returncode, 'COMPILE_REFUSED'
    r = run([node, runner, str(w)])
    if r.returncode < 0:
        return EXIT_CRASH, ''
    return r.returncode, r.stdout


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: logic_semantics.test.py <inimerse> <aot-native> '
                         '[--node NODE] [--wasm-runner PATH]')
    engine, aot = sys.argv[1], sys.argv[2]
    node = os.environ.get('NODE', 'node')
    runner = str(Path(__file__).resolve().parent / 'wasm_run.js')
    argv = sys.argv[3:]
    for i, a in enumerate(argv):
        if a == '--node':
            node = argv[i + 1]
        elif a == '--wasm-runner':
            runner = argv[i + 1]

    # (label, source, expected last line or REFUSED)
    cases = []
    for expr, want in TABLE:
        cases.append(('say %s' % expr, 'say %s\n' % expr, want))
    for expr, want in COND:
        src = 'if %s { say 11 } else { say 22 }\n' % expr
        cases.append(('if %s' % expr, src, want))
    for expr, want in SHORT_CIRCUIT:
        src = 'z = 0\nsay %s\n' % expr
        cases.append(('short-circuit: %s' % expr, src, want))
    for label, src, want in CHAIN:
        cases.append((label, src, want))
    for expr, want in FLOAT:
        cases.append(('say %s' % expr, 'say %s\n' % expr, want))

    bad = []
    with tempfile.TemporaryDirectory(prefix='logic-semantics-') as td:
        for i, (label, src, want) in enumerate(cases):
            tag = 'l%02d' % i
            got = {
                'interpreter': via_interpreter(engine, src, td, tag),
                'aot':         via_native(engine, aot, src, td, tag),
                'wasm':        via_wasm(engine, node, runner, src, td, tag),
            }
            for who, (rc, out) in got.items():
                if rc == EXIT_CRASH:
                    bad.append('%s: %s died from a signal -- a guard is '
                               'decorative again' % (label, who))
                    continue
                if want is REFUSED:
                    if rc == 0:
                        bad.append('%s: %s accepted it (printed %r) but the '
                                   'right operand must be evaluated and divide '
                                   'by zero' % (label, who, last_line(out)))
                elif rc != 0:
                    bad.append('%s: %s refused (rc=%s, %r) but the answer is %r'
                               % (label, who, rc, last_line(out), want))
                elif last_line(out) != want:
                    bad.append('%s: %s printed %r, expected %r'
                               % (label, who, last_line(out), want))
            if not any(b.startswith(label + ':') for b in bad):
                vals = set((rc, last_line(o)) for rc, o in got.values())
                if len(vals) > 1:
                    bad.append('%s: backends disagree -- %s'
                               % (label, {k: (v[0], last_line(v[1]))
                                          for k, v in got.items()}))

    if bad:
        for line in bad:
            print('FAIL ' + line)
        raise SystemExit('%d of %d cases not at the agreed semantics'
                         % (len(bad), len(cases)))
    rows = len(TABLE) + len(COND) + len(SHORT_CIRCUIT) + len(CHAIN) + len(FLOAT)
    case_counts.expect_equal(len(cases), rows,
                             'logic semantics: one case per table row')
    case_counts.count(__file__, lambda m: (len(m.TABLE) + len(m.COND)
                                           + len(m.SHORT_CIRCUIT) + len(m.CHAIN)
                                           + len(m.FLOAT)), 0,
                      'logic semantics cases')
    case_counts.expect_labels(__file__, 'logic semantics cases')
    print('logic semantics: ok (%d cases: value position, precedence, nesting, '
          'not, condition position, short-circuit both ways, `+` chains, '
          'printed floats; interpreter/aot/wasm all agree)' % len(cases))


if __name__ == '__main__':
    main()
