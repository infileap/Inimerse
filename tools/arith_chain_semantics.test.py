#!/usr/bin/env python3
"""A `+` chain must not answer differently depending on how many terms it has.

`src/compiler/compiler.c:715-786` flattens a left-associative `+` chain of
three or more operands into ONE `OP_CONCAT`, while a two-term chain emits
`OP_ADD`.  The comment on that flattening claims "identical per-step semantics
to OP_ADD".  That claim was false for integer overflow:

  * `L_ADD` (`src/vm/vm.c`) adds in `int64_t` and promotes to float when the
    result leaves the interpreter's 32-bit `Value.ival` payload.
  * `L_CONCAT`'s integer fast path added the two 32-bit payloads directly, so
    it wrapped instead.  `x + 1` was 2147483648 and `x + 1 + 0` was
    -2147483648 -- the same expression, two answers, decided by term count.

Measured before the fix (`x = 2147483647`), interpreter vs AOT vs wasm:

    x + 1       2147483648      2147483648      2147483648     agree
    x + 1 + 0   -2147483648     2147483648      2147483648     interpreter wrong
    x + 1 + 1   -2147483647     2147483649      2147483649     interpreter wrong
    y + y + y   -1294967296     3000000000      3000000000     interpreter wrong

AOT and wasm agreed with each other throughout, so the interpreter's
`OP_CONCAT` was the outlier, not a two-way disagreement.

The agreed rule, asserted here:

  * A `+` chain of ANY length folds left to right, and each step obeys the
    int32-payload over/underflow rule: the result stays an int while it fits,
    and becomes a float the moment an intermediate leaves the range.  Once
    float, it stays float for the rest of the chain.
  * The rule is the one `contract_test.im:20` already pinned for the two-term
    form (`x + 1 == 2147483648.0`).  The defect was invisible for so long
    because that contract test contains no chain longer than two terms.

Why these cases are the ones that matter: an operand that is itself a
parenthesized or binary expression (`x + (0 - 1) + 0`, `x * 2 + 1 + 0`) is
excluded by the flattening guard at `src/compiler/compiler.c:732-733`, so it
never reached `OP_CONCAT` and was already correct.  Keeping one such control
row here pins that the two paths stay in step, and stops a future "fix" in
`OP_ADD` from silently re-opening the same divergence.

String chains and float formatting are out of scope: the AOT and wasm
backends do not implement `str()`, and AOT's float *printing* diverges
separately (see docs/AUDIT.md).  This test asserts numeric agreement only.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

# (label, source, expected last stdout line)
TABLE = [
    # the two-term form is the reference the chain must match
    ("2-term overflow",
     'x = 2147483647\nsay(x + 1)\n', '2147483648'),
    # three and more terms -> one OP_CONCAT
    ("3-term +0",
     'x = 2147483647\nsay(x + 1 + 0)\n', '2147483648'),
    ("3-term +1",
     'x = 2147483647\nsay(x + 1 + 1)\n', '2147483649'),
    ("4-term +0+0",
     'x = 2147483647\nsay(x + 1 + 0 + 0)\n', '2147483648'),
    ("3-term identity operand",
     'y = 1000000000\nsay(y + y + y)\n', '3000000000'),
    ("3-term 2e9+2e9+1",
     'x = 2000000000\nsay(x + 2000000000 + 1)\n', '4000000001'),
    ("3-term literal chain",
     'say(2147483647 + 1 + 0)\n', '2147483648'),
    # a chain that stays in range must be untouched by the promotion rule
    ("in-range 3-term",
     'x = 10\nsay(x + 1 + 0)\n', '11'),
    ("in-range 3-term to the boundary",
     'x = 2147483646\nsay(x + 1 + 0)\n', '2147483647'),
    # control: parenthesized operands defeat the flattening (compiler.c:732),
    # so this always took the OP_ADD path; the two paths must agree here too.
    ("parenthesized underflow (control)",
     'x = 0\nsay(x + (0 - 2147483647) + (0 - 2))\n', '-2147483649'),
]

EXIT_CRASH = 'crashed'


def last_line(out):
    for ln in reversed(out.splitlines()):
        if ln.strip():
            return ln.strip()
    return ''


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=180,
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
        raise SystemExit('usage: arith_chain_semantics.test.py <inimerse> '
                         '<aot-native> [--node NODE] [--wasm-runner PATH]')
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
    with tempfile.TemporaryDirectory(prefix='arith-chain-') as td:
        for i, (label, src, want) in enumerate(TABLE):
            tag = 'c%02d' % i
            got = {
                'interpreter': via_interpreter(engine, src, td, tag),
                'aot':         via_native(engine, aot, src, td, tag),
                'wasm':        via_wasm(engine, node, runner, src, td, tag),
            }
            for who, (rc, out) in got.items():
                if rc == EXIT_CRASH:
                    bad.append('%s: %s died from a signal -- a guard is '
                               'decorative again' % (label, who))
                elif rc != 0:
                    bad.append('%s: %s refused (rc=%s, %r) but the answer is %r'
                               % (label, who, rc, out, want))
                elif out != want:
                    bad.append('%s: %s printed %r, expected %r'
                               % (label, who, out, want))
            if not any(b.startswith(label + ':') for b in bad):
                vals = set((rc, out) for rc, out in got.values())
                if len(vals) > 1:
                    bad.append('%s: backends disagree -- %s'
                               % (label, {k: v for k, v in got.items()}))

    if bad:
        for line in bad:
            print('FAIL ' + line)
        raise SystemExit('%d of %d cases not at the agreed semantics'
                         % (len(bad), len(TABLE)))
    print('arith chain semantics: ok (%d cases, interpreter/aot/wasm all agree; '
          'a `+` chain answers the same for 2, 3 and 4 terms)' % len(TABLE))


if __name__ == '__main__':
    main()
