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
import re
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


def build_nameless_product(path):
    """Write a valid product whose "frozen" does not contain its own basename.

    Built from the harness's own fixture helper rather than hand-written, so the
    only difference from `healthy` is the missing self key: if the fixture were
    built by hand here, a cell failing for some unrelated reason would look like
    the behaviour this case is about.
    """
    import importlib.util
    spec = importlib.util.spec_from_file_location('_frozen_identity_harness',
                                                  HARNESS)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    key = 'nameless.json'
    mod.build_synthetic_product(path, key)
    with open(path, encoding='utf-8') as f:
        d = json.load(f)
    d['frozen'].pop(key, None)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(json.dumps(d, ensure_ascii=False, indent=1))
    return path


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

        # ★ `a-directory` is the probe that separates isfile from exists.
        # Measured, not argued: when the classifier's guard was changed from
        # os.path.isfile to os.path.exists, this file failed on a-directory
        # alone -- a directory passes `exists`, reaches open(), raises
        # IsADirectoryError and is swallowed into "bad JSON" -- while
        # check_frozen_identity.test.py stayed GREEN at pass=7/7, because none of
        # its probes was a directory.
        #
        # ★ And this layer is the only thing standing between "the classifier is
        # correct" and "the program still uses it".  Measured: with BOTH call
        # sites in check_frozen_identity.test.py replaced by a naive
        # json.load(open(...)), the in-process input-classify cell stays GREEN --
        # it calls the pure function directly, so it cannot see this -- while all
        # four bad-input cells here go BAD at rc want=3 got=1 and this file drops
        # to 1/5 with the tracebacks printed.  That is the whole reason this
        # file drives the entry point in a subprocess instead of importing it.
        #
        #   [then -- two guarded call sites; see the note below] removed one of the
        #   two call sites only -> still 5/5, because the other one classifies
        #   first; the guard was written twice, so a single-site bypass changed no
        #   observable behaviour.
        #
        # ★ That was measured on a tree with two guards.  The second call site's
        #   guard is gone now: tools/check_frozen_identity.test.py keeps only the
        #   `load_product` call there, because the cells below need the parsed
        #   object, and the early guard is the only classifier.  Measured before
        #   removing it: an instrumented counter read 0 for that branch across
        #   seven inputs, and deleting the EARLY guard instead made the same
        #   counter read 4 -- so the two were two complete classifiers, not a
        #   guard and its duplicate.  There is one guard today, and this file's
        #   reason for existing is unchanged: it drives the entry point.
        #
        # That made this layer the sole detector of the regression, which is a
        # bad place for a single point to live, so a-directory was added to the
        # in-process cases too.  Re-measured after the addition: the same
        # mutation turns the harness's own input-classify cell BAD as well.
        #
        # ★ But read the two red cells here for what they are.  After that
        # addition this file drops from 5/5 to 3/5 on the same mutation, and the
        # two failures are NOT independent:
        #
        #   a-directory  BAD  -- the marker came back INPUT_BAD_JSON instead of
        #                       INPUT_MISSING.  This is this layer's own finding.
        #   healthy      BAD  -- it asserts the harness exits 0 with a verdict of
        #                       all_cells_expected and the two numbers in
        #                       "CELLS pass=N/M" equal, so it turns red whenever
        #                       the harness is broken for ANY reason.  Upstream
        #                       breakage dragging it down is not evidence about
        #                       the classifier.
        #
        # So `healthy` is a liveness assertion on the harness, not a second
        # opinion: it is the downstream indicator that the coupling above was
        # noticed at all.  Only cells whose expectation is independent of the
        # harness's overall exit status -- here a-directory -- are evidence.
        #
        # ★ And that cell has to be exercised in BOTH directions.  It once
        # compared stdout against a hard-coded "pass=7/7", which made it a false
        # red on any all-green change that added a cell; replacing that with a
        # semantic assertion removes the false red, but a semantic assertion
        # that is never run against real breakage can flip to a false GREEN,
        # which is harder to notice.  Both directions measured on this cell:
        #
        #   one extra PASSING cell in the harness (all-green change)
        #     harness  CELLS pass=8/8  RC=0   old hard-coded -> BAD (false red)
        #                                     now            -> OK
        #   os.path.isfile -> os.path.exists (a real regression)
        #     harness  CELLS pass=6/7  RC=1   now            -> BAD
        #              cells=CELLS pass=6/7 verdict_ok=False  rc want=0 got=1
        #
        # The second row is the load-bearing one: it rules out the de-numbered
        # assertion having become vacuously true.  A negative control alone
        # would not have shown that.
        cases = [
            # (label, path, present?, want rc, want marker in stdout)
            ('no-such-file', missing, False, 3, 'INPUT_MISSING'),
            ('a-directory', isdir, True, 3, 'INPUT_MISSING'),
            ('not-json', bad, True, 3, 'INPUT_BAD_JSON'),
            ('no-frozen-key', nofr, True, 3, 'INPUT_NO_FROZEN'),
        ]
        # ★ `nameless` is a VALID product whose "frozen" simply does not name
        # itself, and it is deliberately NOT in the list above: it is not an input
        # fault.  It is the shape that `load_product` refuses to classify (see its
        # docstring) because calling it an input error would hide the checker's own
        # NO_FROZEN_SELF behind a test-side refusal -- the "masking the verdict"
        # defect that 699a88c fixed.  Asserted here, at the entry point, because
        # the in-process no-self-key cell proves only that the checker can still
        # say it, not that main() lets the run reach the checker.
        #
        # ★ Measured, and it is the reason this case exists: with a "valid JSON but
        # no self key" product, main() ran the cells and failed some of them
        # (rc 1, some_cells_failed).  A one-line mutation at the second call site
        # -- classifying a missing self key as an input fault -- turned that into
        # rc 3 INPUT_NO_SELF_KEY, and NOTHING registered here noticed: the harness
        # stayed at pass=7/7 and this file stayed at 5/5, because none of its five
        # branches carries a product that is valid but nameless.  This case is that
        # missing branch.
        nameless = os.path.join(tmp, 'nameless.json')
        build_nameless_product(nameless)
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

        # A valid-but-nameless product must reach the cells and fail some of them.
        # ★ Three semantic conditions, no hard-coded count (see the healthy case
        # below for why a literal number here is a false red waiting to happen):
        #   (a) a CELLS line exists -- every failure shape of "crash" prints none,
        #       so this separates "the cells said no" from "the program fell over";
        #   (b) the two numbers DIFFER -- some cell really did fail;
        #   (c) no input verdict was printed -- the door did not refuse it.
        here = os.path.isfile(nameless)
        if not here:
            print('%-14s PROBE %s expected present got=absent -> BAD'
                  % ('nameless', nameless))
            results.append(False)
        else:
            got_rc, out = run([nameless])
            m = re.search(r'CELLS pass=(\d+)/(\d+)\b', out)
            cells = m.group(0) if m else 'none'
            reached_cells = bool(m)
            some_failed = bool(m) and m.group(1) != m.group(2)
            not_refused = 'VERDICT input_' not in out
            ok = got_rc == 1 and reached_cells and some_failed and not_refused
            results.append(ok)
            print('%-14s probe=present rc want=1 got=%d cells=%s '
                  'reached_cells=%s refused=%s %s'
                  % ('nameless', got_rc, cells, reached_cells,
                     not not_refused, 'OK' if ok else 'BAD'))
            if not ok:
                for line in out.rstrip().splitlines():
                    print('    ' + line)

        # The healthy path must still be healthy through the same entry point.
        # ★ Do NOT assert the cell COUNT here.  This used to read
        # `'pass=7/7' in out`, a hard-coded number, and that made a false red:
        # adding one more PASSING cell to the harness -- a wholly legitimate,
        # all-green change -- stopped the literal from appearing, so this cell
        # reported BAD with rc 1 and pass=4/5, shaped exactly like a real
        # regression.  Nothing forces this number to be updated, unlike
        # EXP_CTEST, so it has to be phrased so that it needs no update.  The
        # three conditions below are semantic: exit 0, the two numbers agree,
        # and the verdict is the one that means every cell was as expected.
        got_rc, out = run([])
        m = re.search(r'CELLS pass=(\d+)/(\d+)\b', out)
        cells = m.group(0) if m else 'none'
        verdict_ok = 'VERDICT all_cells_expected' in out
        ok = got_rc == 0 and bool(m) and m.group(1) == m.group(2) and verdict_ok
        results.append(ok)
        print('%-14s rc want=0 got=%d cells=%s verdict_ok=%s %s'
              % ('healthy', got_rc, cells, verdict_ok, 'OK' if ok else 'BAD'))
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
