#!/usr/bin/env python3
"""`%` and `/` must mean the same thing on all three backends (docs/AUDIT.md §1.6).

Before this test existed the three backends disagreed on 9 of the 16 cases in
TABLE below.  The interpreter narrowed `%` operands through `int`, so
`3000000000 % 7` read -2 (x86-64 `cvttsd2si` returns INT_MIN for an
out-of-range double) and `-2147483648 % -1` died with SIGFPE (rc 136).  The
native backend had no error path at all, so a zero divisor quietly produced 0
or inf.  The wasm backend truncated through the *saturating* i32 conversion, so
3000000000 became INT32_MAX and `3000000000 % 7` yielded 1.

The agreed rule, asserted here:

  * `%` truncates both operands toward zero to 64 bits and takes the remainder.
  * If the truncated DIVISOR is zero -- `5 % 0`, `7 % 0.5`, `7 % 0.0` -- the
    program is refused with `division_by_zero`.  The test is on the value the
    user wrote, not on the narrowed one, so `7 % 0.5` is a refusal and not a
    bogus "0".
  * `x % -1` is 0 (and never LLONG_MIN % -1, which is UB and traps).
  * A result that does not fit the interpreter's 32-bit int payload is
    promoted to float, exactly as L_NEG already does for INT_MIN.  `%` values
    agree; the *type* is O2's problem, not O1's.
  * `/` with both operands int and a zero divisor is also refused.

A refusal counts only when all three backends exit non-zero and none of them
died from a signal: a signal means a guard is decorative again, and exit 0
means the backend accepted something the other two rejected.
"""
import case_counts
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REFUSED = object()

# (expression, expected stdout line) -- REFUSED means every backend must refuse.
TABLE = [
    ("7 % 3",                     "1"),
    ("-7 % 3",                    "-1"),
    ("7 % -3",                    "1"),
    ("-7 % -3",                   "-1"),
    ("2.5 % 1",                   "0"),
    ("7 % 0.5",                   REFUSED),   # truncated divisor is 0
    ("5 % 0",                     REFUSED),
    ("7 % 0.0",                   REFUSED),
    ("3000000000 % 7",            "4"),
    ("2147483648 % 7",            "2"),
    ("7 % 3000000000",            "7"),
    ("10 % 4294967296",           "10"),
    ("7000000000 % 4000000000",   "3000000000"),  # > int32: promoted to float
    ("-2147483648 % -1",          "0"),           # used to be SIGFPE
    ("5 / 0",                     REFUSED),
    ("7.0 / 0",                   "inf"),
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
    return r.returncode, last_line(r.stdout)


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
    return r.returncode, last_line(r.stdout)


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
    return r.returncode, last_line(r.stdout)


def main():
    if len(sys.argv) < 3:
        raise SystemExit('usage: mod_semantics.test.py <inimerse> <aot-native> '
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

    bad = []
    with tempfile.TemporaryDirectory(prefix='mod-semantics-') as td:
        for i, (expr, want) in enumerate(TABLE):
            tag = 'c%02d' % i
            src = 'say %s\n' % expr
            got = {
                'interpreter': via_interpreter(engine, src, td, tag),
                'aot':         via_native(engine, aot, src, td, tag),
                'wasm':        via_wasm(engine, node, runner, src, td, tag),
            }
            for who, (rc, out) in got.items():
                if rc == EXIT_CRASH:
                    bad.append('%s: %s died from a signal -- a guard is '
                               'decorative again' % (expr, who))
                    continue
                if want is REFUSED:
                    if rc == 0:
                        bad.append('%s: %s accepted it (printed %r) where the '
                                   'other backends refuse' % (expr, who, out))
                else:
                    if rc != 0:
                        bad.append('%s: %s refused (rc=%s, %r) but the answer '
                                   'is %r' % (expr, who, rc, out, want))
                    elif out != want:
                        bad.append('%s: %s printed %r, expected %r'
                                   % (expr, who, out, want))
            if not any(b.startswith(expr + ':') for b in bad):
                vals = set((rc, out) for rc, out in got.values())
                if len(vals) > 1:
                    bad.append('%s: backends disagree -- %s'
                               % (expr, {k: v for k, v in got.items()}))

    if bad:
        for line in bad:
            print('FAIL ' + line)
        raise SystemExit('%d of %d cases not at the agreed semantics'
                         % (len(bad), len(TABLE)))
    case_counts.count(__file__, lambda m: len(m.TABLE), 0,
                      'mod semantics boundary cases')
    print('mod semantics: ok (%d boundary cases, interpreter/aot/wasm all agree)'
          % len(TABLE))


if __name__ == '__main__':
    main()
