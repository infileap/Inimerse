#!/usr/bin/env python3
"""Run a test program under two independent assertions, and say which it asked.

Why this exists
---------------
`PASS_REGULAR_EXPRESSION` is not an addition to the exit-code judgement, it is a
replacement for it.  `cmake --help-property PASS_REGULAR_EXPRESSION` says,
verbatim:

    "The process output ... The process exit code is ignored."

So a test that asserts its shape line through that property stops noticing a
non-zero exit -- and a future harness that prints its shape line and *then*
fails would go green.  That hole is not a property of any one test; it belongs
to the property itself.

The property has a second hole, and it is the sharper one.  CMake reads the
property's value as a **list**: `PASS_REGULAR_EXPRESSION "A;B"` is two items, and
the test passes if *either* matches.  So a property written as `"ok;<pin>"`
asserts `ok` and mentions `<pin>` -- and the pin can vanish entirely while the
test stays green.  Measured on a minimal project: `"A;B"` with only `A` printed
=> Passed, only `B` printed => Passed, `C` printed => Failed.  Five sites in
this repository are written that way today, and at `CMakeLists.txt:690` the
second item is the per-platform parity pin whose own comment says the known
divergence *is* asserted there.

This wrapper puts both questions back in the same place.  It asks exactly two,
prints which two it asked, and fails when either one fails:

    question 1: the program exited 0
    question 2: the program's output matches EVERY shape clause

They are two independent assertions, not one sentence: each is evaluated on its
own and each is reported on its own, so the next person can tell a missing
shape line from a non-zero exit without re-running anything.  Question 2 is
ALL, deliberately: it is the reading the property cannot give today.

The child's output is passed through unchanged, so a `PASS_REGULAR_EXPRESSION`
on the calling test still matches the real shape line and not this report.  The
clauses are echoed through repr() for that reason as well: the echoed form can
never satisfy the pattern it describes.

Usage
-----
    gated_run.py --shape REGEX [--shape REGEX ...] -- CMD [ARG ...]

`--shape` may be repeated, and a single value containing `;` is split on `;`
(that is how CMake would read it).  Repeat the flag when wiring a `;`-joined
property into a CMake `COMMAND`, because CMake would split an unescaped `;`
into separate arguments before this program ever sees it.

Exit codes
----------
    0   both questions passed
    1   either question failed (the child is reported, not re-run)
    2   usage error: no --shape, an invalid regex, or no command after `--`
"""

import argparse
import re
import subprocess
import sys

Q1 = "the program exited 0"
Q2 = "every shape clause matched"


def clauses_of(values):
    """CMake list semantics: a `;` in the value separates items."""
    out = []
    for value in values:
        for part in value.split(";"):
            part = part.strip()
            if part:
                out.append(part)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="gated_run.py",
        description="run a command under two independent assertions",
    )
    ap.add_argument("--shape", required=True, action="append",
                    help="regex clause the output must match (question 2); "
                         "repeat the flag, or join with ';', for several clauses")
    ap.add_argument("cmd", nargs=argparse.REMAINDER,
                    help="the program to run, after a literal `--`")
    args = ap.parse_args(argv)

    cmd = list(args.cmd)
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        sys.stderr.write("gated_run: usage: gated_run.py --shape REGEX [--shape REGEX ...] -- CMD [ARG ...]\n")
        sys.stderr.write("gated_run: nothing was run; there is no command after `--`.\n")
        return 2

    texts = clauses_of(args.shape)
    if not texts:
        sys.stderr.write("gated_run: --shape was empty; nothing to match.\n")
        sys.stderr.write("gated_run: nothing was run.\n")
        return 2
    compiled = []
    for text in texts:
        try:
            compiled.append((text, re.compile(text)))
        except re.error as exc:
            sys.stderr.write("gated_run: --shape %r is not a regex: %s\n" % (text, exc))
            sys.stderr.write("gated_run: nothing was run.\n")
            return 2

    proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True,
                          errors="backslashreplace")
    out = proc.stdout or ""
    sys.stdout.write(out)
    sys.stdout.flush()

    exit_ok = proc.returncode == 0
    hits = [(text, rx.search(out) is not None) for text, rx in compiled]
    matched = sum(1 for _t, ok in hits if ok)
    shape_ok = matched == len(hits)

    print("gated_run: question 1 of 2: %s .......... %s (exit code %d)"
          % (Q1, "PASS" if exit_ok else "FAIL", proc.returncode))
    print("gated_run: question 2 of 2: %s (%d clause(s); CMake would read a ';' "
          "list as ANY, this asks ALL) .......... %s (%d of %d matched)"
          % (Q2, len(hits), "PASS" if shape_ok else "FAIL", matched, len(hits)))
    for i, (text, ok) in enumerate(hits, 1):
        print("gated_run:   clause %d of %d: %r .......... %s"
              % (i, len(hits), text, "matched" if ok else "no match"))

    failed = []
    if not exit_ok:
        failed.append("%s [FAIL]" % Q1)
    if not shape_ok:
        failed.append("%s [FAIL]" % Q2)

    if not failed:
        print("gated_run: 2 question(s) asked, 0 failed -- exit code is 0 AND all "
              "%d shape clause(s) matched." % len(hits))
        return 0

    print("gated_run: 2 question(s) asked, %d failed: %s"
          % (len(failed), "; ".join(failed)))
    print("gated_run: the exit code is judged here because PASS_REGULAR_EXPRESSION replaces it")
    print("gated_run: (cmake --help-property PASS_REGULAR_EXPRESSION: \"The process exit code is ignored.\").")
    if not shape_ok:
        print("gated_run: and ALL clauses are required here because a ';' list is read as ANY")
        print("gated_run: there: a property written as \"ok;<pin>\" passes with <pin> gone.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
