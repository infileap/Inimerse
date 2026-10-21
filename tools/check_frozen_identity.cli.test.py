#!/usr/bin/env python3
"""check_frozen_identity.cli.test.py — drive the harness through its CLI.

Why a second test file exists
-----------------------------
check_frozen_identity.test.py asserts the *pure classifier* in process, and it
asserts the checker's verdicts.  What it cannot do is fail when `main()` stops
calling the classifier at all: that cell would still pass, because it calls the
function directly.  The defect this file guards against lived precisely at the
call sites — one wrong assumption about the product was written three times in
three places, and each one died with a traceback and exit 1, which is the code
the checker uses to say "the identity does not hold".  A cell that calls the
function cannot see a missing call.

So this file goes one level out: it runs the harness as a subprocess and reads
its exit code and stdout, exactly as a person at a shell would.

Probe presence is asserted first
--------------------------------
An earlier attempt at this measurement created probes under /tmp, which was
cleared between shell invocations.  Every one of the four branches then reported
`INPUT_MISSING` -- because the probes were gone, not because the classifier was
right.  The degenerate answer and the correct answer were identical on screen.
So each case asserts its probe exists *before* running, and separately asserts
the marker is the one expected (not the absent-file one).  A probe set that
vanishes degrades from "N branches" to "one branch" silently: this is what
stops it.

Usage:
    python3 tools/check_frozen_identity.cli.test.py
Exit codes: 0 every branch as expected, 1 some branch differed.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.join(HERE, 'check_frozen_identity.test.py')


def run(args):
    r = subprocess.run([sys.executable, HARNESS] + args,
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    tmp = tempfile.mkdtemp(prefix='frozen-identity-cli-')
    results = []
    try:
        # probes: every one of them is checked for presence before use
        missing = os.path.join(tmp, 'not-there.json')
        isdir = os.path.join(tmp, 'a-directory')
        os.makedirs(isdir, exist_ok=True)
        bad = os.path.join(tmp, 'not-json.json')
        open(bad, 'w', encoding='utf-8').write('this is not json\n')
        nofr = os.path.join(tmp, 'no-frozen.json')
        open(nofr, 'w', encoding='utf-8').write('{"item": "x"}\n')

        # ★ `a-directory` is the only probe that separates isfile from exists.
        # Measured, not argued: changing the classifier's guard from
        # os.path.isfile to os.path.exists leaves `check_frozen_identity.test.py`
        # reporting pass=7/7 RC=0 -- all three of its probes (absent, not JSON,
        # no usable frozen) still classify correctly -- while this file fails on
        # a-directory alone, because a directory passes `exists`, reaches open(),
        # raises IsADirectoryError and is swallowed into "bad JSON".  So this
        # layer is not a second opinion on the in-process cells; for that
        # regression it is the only detector.  Do not simplify it away.
        cases = [
            # (label, path, present?, want rc, want marker in stdout)
            ('no-such-file', missing, False, 3, 'INPUT_MISSING'),
            ('a-directory', isdir, True, 3, 'INPUT_MISSING'),
            ('not-json', bad, True, 3, 'INPUT_BAD_JSON'),
            ('no-frozen-key', nofr, True, 3, 'INPUT_NO_FROZEN'),
        ]
        for label, path, present, want_rc, want_marker in cases:
            here = os.path.isfile(path) or os.path.isdir(path)
            if here != present:
                print('%-14s PROBE %s expected present=%s got=%s -> BAD'
                      % (label, path, present, here))
                results.append(False)
                continue
            got_rc, out = run([path])
            ok = got_rc == want_rc and want_marker in out
            # a probe that is present must never be reported as absent
            if present and 'INPUT_MISSING' in out and want_marker != 'INPUT_MISSING':
                ok = False
            results.append(ok)
            print('%-14s probe=%s rc want=%d got=%d marker=%s %s'
                  % (label, 'present' if present else 'absent',
                     want_rc, got_rc, want_marker, 'OK' if ok else 'BAD'))
            if not ok:
                for line in out.rstrip().splitlines():
                    print('    ' + line)

        # the healthy path must still be healthy through the same entry point
        got_rc, out = run([])
        ok = got_rc == 0 and 'pass=7/7' in out
        results.append(ok)
        print('%-14s rc want=0 got=%d pass=7/7 in out=%s %s'
              % ('healthy', got_rc, 'pass=7/7' in out, 'OK' if ok else 'BAD'))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    passed = sum(1 for r in results if r)
    print()
    print('CLI CELLS pass=%d/%d' % (passed, len(results)))
    print('VERDICT %s' % ('all_branches_expected' if passed == len(results)
                          else 'some_branches_failed'))
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    sys.exit(main())
