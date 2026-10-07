#!/usr/bin/env python3
"""REGISTERED, NOT RESOLVED -- one root, two faces: neither serializer detects a cycle.

`str()`/`say` render through `value_to_string`, which bounds the walk with
`if (depth < 2)` (src/vm/vm.c:1249 for arrays, :1271 for dicts) -- a depth
ceiling doing a cycle detector's job, so a cyclic value prints as `[[[]]]`
instead of being refused.  `json_serialize` renders through `json_write`
(src/mod/json_mod.c:100), which has **no such guard at all**: `depth` there
decides only who takes the lock, and the recursion is unbounded, so a cyclic
value dies on the stack (rc=-11, SIGSEGV).

    b = [[[1]]]
    say b                 ->  [[[]]]        the depth-2 array is gone
    say json_serialize(b) ->  [[[1]]]       nothing is gone
    a = []; push(a, a)
    say a                 ->  [[[]]]        a finite-looking answer
    say json_serialize(a) ->  (no answer, rc=-11)

The two faces are not two designs disagreeing: they are one root with two
symptoms.  Both serializers lack real cycle detection; `value_to_string`
*pretends* with a depth ceiling and `json_write` does not pretend at all.  That
is the whole difference between the outputs above, and it is why the fix is
one thing -- a "this same container is already on the stack" decision -- and not
two.

Both faces are live on the merged tree, and this reading is later than the
commits that made them live: `4783ad8` / `823cf7c` narrowed the VM lock to the
outermost frame (`if (depth == 0) VM_LOCK(vm)`), which removed the re-entrant
deadlock that used to sit in front of both defects.  Their commit messages say
nothing about any of this, because this reading is later than they are -- do not
amend them with it.  Measured against the engine built from 2239c16 (before
them, the only copy of the "before" side, at .worktrees/unpatched-dl): every
case below hangs with rc=124 and no output.  So the truncation and the
divergence are consequences of that change, not pre-existing differences.

This test asserts *today's behaviour* on purpose: a registration that cannot go
red is a comment.  When the cycle decision lands, this test MUST go red, and
whoever makes it red should read this paragraph: the truncation is not the spec,
it is the shape the defect has today.

REGISTERED, NOT RESOLVED.
"""

import os
import subprocess
import sys
import tempfile

TIMEOUT = 20

# Face 1: the depth ceiling in value_to_string.  (name, program, expected last
# line, expected rc)
TRUNCATION = [
    ("array 2 levels", "b = [[1, 2]]\nsay b\n", "[[1, 2]]", 0),
    ("array 3 levels", "b = [[[1]]]\nsay b\n", "[[[]]]", 0),
    ("dict 3 levels", 'd = {"a": {"b": {"c": 1}}}\nsay d\n', "{a: {b: {}}}", 0),
    ("cyclic array", "a = []\npush(a, a)\nsay a\n", "[[[]]]", 0),
]

# Face 2: the same values through json_serialize, which has no ceiling at all.
DIVERGENCE = [
    ("json_serialize 3 levels", "b = [[[1]]]\nsay json_serialize(b)\n", "[[[1]]]", 0),
]


def run(engine, program):
    """Return (rc, last_line, stdout).  rc is None on timeout."""
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "case.im")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(program)
        try:
            p = subprocess.run([engine, path], capture_output=True, text=True,
                               encoding="utf-8", errors="replace",
                               timeout=TIMEOUT, cwd=tmp)
        except subprocess.TimeoutExpired:
            return None, "", ""
    lines = [ln for ln in p.stdout.splitlines() if ln.strip()]
    return p.returncode, (lines[-1].strip() if lines else ""), p.stdout


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: serializer_cycle_regression.test.py <inimerse>")
    # Absolute: run() executes with cwd=<tempdir>, and CTest hands us an
    # absolute $<TARGET_FILE:inimerse> while a shell may hand us a relative one.
    engine = os.path.abspath(sys.argv[1])
    failed = 0
    total = 0

    for name, program, want, want_rc in TRUNCATION + DIVERGENCE:
        total += 1
        rc, got, out = run(engine, program)
        if rc is None:
            print(f"  {name}: TIMED OUT after {TIMEOUT}s (no output) -- this is "
                  f"the pre-lock-change behaviour; the engine under test does "
                  f"not have the narrowed lock")
            failed += 1
            continue
        ok = rc == want_rc and got == want
        print(f"  {name}: rc={rc} (want {want_rc}) got={got!r} want={want!r} "
              f"{'ok' if ok else 'MISMATCH'}")
        if not ok:
            failed += 1
            if out:
                print("    stdout:")
                for ln in out.splitlines():
                    print(f"      {ln}")

    # The cyclic half of face 2: json_serialize must not answer.  Registered as
    # "does not return a complete answer", not as a specific signal, because the
    # signal is a stack overflow and depends on the build.
    total += 1
    rc, got, out = run(engine, "a = []\npush(a, a)\nsay json_serialize(a)\n")
    if rc is None:
        print("  json_serialize cyclic: TIMED OUT -- pre-lock-change behaviour")
        failed += 1
    elif rc == 0:
        print(f"  json_serialize cyclic: rc=0 got={got!r} -- it answered a cyclic "
              f"value; if it now refuses the cycle instead, this registration has "
              f"gone red for the right reason")
        failed += 1
    else:
        print(f"  json_serialize cyclic: rc={rc} (no complete answer) ok")

    if failed:
        print(f"serializer cycle: {failed} of {total} reading(s) did not read as "
              f"registered")
        return 1
    print(f"serializer cycle: {total} reading(s) as registered -- one root, two "
          f"faces (str()/say truncates at depth 2, json_serialize recurses "
          f"unbounded) (REGISTERED, NOT RESOLVED)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
