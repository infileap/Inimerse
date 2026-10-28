#!/usr/bin/env python3
"""`%` must mean the same thing on all three backends -- and the rule lives in ONE place.

The rule is not written here any more.  It is `tools/mod_spec.py` (`MOD_RULE`), which
holds it as executable Python; this file only *runs* the case matrix that spec
generates and compares each backend against it.  Before that, the rule was written
out four times -- interpreter, native `kPreamble`, wasm emission, and this test's own
hand-written `TABLE` -- and one of those copies was wrong: a NaN divisor printed 7 on
two backends and was refused on the third, and a NaN dividend printed -1, -1 and 0.

What a mismatch means, and why each shape is a distinct diagnosis:

  * a signal -- the guard is decorative again;
  * exit 0 where the rule refuses -- that backend accepts what the other two reject;
  * two different values -- two implementations, one rule: one of them is not the rule;
  * a value where the rule refuses, with a non-zero exit -- worse than agreement: the
    backend printed an answer and then failed.

`--self-check` proves the harness can fail: it tampers with one expectation and
requires a red result.  A harness that cannot be made to fail is a description of the
implementations, not a test of them.
"""
import case_counts
import os
import sys
import subprocess
import tempfile
from pathlib import Path

import mod_spec

REFUSED = mod_spec.REFUSED

# Generated, never typed: (expression, expected) comes from tools/mod_spec.py.
TABLE = mod_spec.matrix()

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


def judge(expr, want, got):
    """Return failure lines for one case; `got` maps backend -> (rc, out)."""
    bad = []
    for who, (rc, out) in got.items():
        if rc == EXIT_CRASH:
            bad.append('%s: %s died from a signal -- a guard is decorative again'
                       % (expr, who))
            continue
        if want is REFUSED:
            if rc == 0:
                bad.append('%s: %s accepted it (printed %r) where the rule refuses'
                           % (expr, who, out))
        else:
            if rc != 0:
                bad.append('%s: %s refused (rc=%s, %r) but the rule says %r'
                           % (expr, who, rc, out, want))
            elif out != want:
                bad.append('%s: %s printed %r, the rule says %r'
                           % (expr, who, out, want))
    vals = set((rc, out) for rc, out in got.values())
    if len(vals) > 1 and not bad:
        bad.append('%s: backends disagree -- %s' % (expr, {k: v for k, v in got.items()}))
    return bad


def self_check():
    """Tamper with one expectation and require the judge to notice."""
    expr, want = TABLE[1]
    tampered = REFUSED if want is not REFUSED else '12345'
    printed = '' if want is REFUSED else want
    bad = judge(expr, tampered, {'interpreter': (0, printed)})
    if not bad:
        print('MODCHECK|case=tampered-expectation|red=no|why=judge accepted a wrong expectation')
        raise SystemExit('self-check failed: the judge cannot fail')
    print('MODCHECK|case=tampered-expectation|red=yes|expectation=%r|outcome=%d line(s)'
          % (tampered, len(bad)))


def main():
    if '--self-check' in sys.argv:
        self_check()
        return
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
            bad.extend(judge(expr, want, got))

    info = mod_spec.summarise()
    print('MOD-PARITY|rule=%s|spec-sha=%s|cases=%d|refused=%d|backends=3|disagree=%d'
          % (info['spec'], info['sha'], info['cases'], info['refused'], len(bad)))
    if bad:
        for line in bad:
            print('FAIL ' + line)
        raise SystemExit('%d of %d cases not at the agreed semantics'
                         % (len(bad), len(TABLE)))
    # The matrix is generated from tools/mod_spec.py, so this harness has 12 more
    # rows than the hand-written table did at the case-count base (28 vs 16).
    # A case count is a decision: the delta moves in the same commit as the reason.
    case_counts.count(__file__, lambda m: len(m.TABLE), 12,
                      'mod semantics boundary cases')
    # The identities moved too, and that is the point: five hand-written rows are
    # gone (the two INT32_MIN cases, `5 / 0`, `7 % 3000000000`, `7.0 / 0`) and
    # seventeen are generated by the spec.  Declared here rather than discovered,
    # because a swap is exactly what a total cannot see.
    case_counts.expect_labels(__file__, 'mod semantics boundary cases',
                              expected_added=17, expected_removed=5)
    print('mod semantics: ok (%d boundary cases, interpreter/aot/wasm all agree)'
          % len(TABLE))


if __name__ == '__main__':
    main()