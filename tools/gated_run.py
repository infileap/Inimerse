#!/usr/bin/env python3
"""Run a test program under three independent assertions, and say which it asked.

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

This wrapper puts both questions back in the same place, and adds a third that
is about the wrapper itself.  It asks exactly three, prints which three it
asked, and fails when any one fails:

    question 1: the program exited as required (`--exit 0` | `--exit N` | `--exit nonzero`)
    question 2: the program's output matches EVERY shape clause
    question 3: this report cannot satisfy the shape it just checked

They are three independent assertions, not one sentence: each is evaluated on
its own and each is reported on its own, so the next person can tell a missing
shape line from a non-zero exit without re-running anything.  Question 2 is
ALL, deliberately: it is the reading the property cannot give today.

Question 3 is the one the repr() echo was designed for and never checked.  A
self-reporting wrapper whose own output satisfies the pattern it checks will
pass on its own report when the program under test prints nothing -- so the pin
it guards becomes a decoration, and nothing in the output says so.  It is
measured, not assumed: a clause is a violation when this report satisfies it
**and the program's own output does not**, and the run then fails naming that
clause.  The verdict of question 3 is left out of the text being checked, so
that verdict cannot be what satisfies a clause.

The child's output is passed through unchanged, so a `PASS_REGULAR_EXPRESSION`
on the calling test still matches the real shape line and not this report.  The
clauses are echoed through repr(), which escapes regex metacharacters -- but a
clause that is a plain literal is echoed literally, so the echo alone could
satisfy such a property.  That is exactly the case question 3 fails on, and the
answer is to write the clause so this report cannot satisfy it.

Usage
-----
    gated_run.py --shape REGEX [--shape REGEX ...] [--exit 0|N|nonzero] -- CMD [ARG ...]

`--shape` may be repeated, and a single value containing `;` is split on `;`
(that is how CMake would read it).  Repeat the flag when wiring a `;`-joined
property into a CMake `COMMAND`, because CMake would split an unescaped `;`
into separate arguments before this program ever sees it.

`--exit` defaults to `0` -- the reading every test in this repository is written
under today.  Use `--exit nonzero` for a test whose subject *is* a refusal: the
program is expected to fail, and what is asserted is that it failed **and** said
why.  `--exit zero` is accepted as a spelling of `0`.

Exit codes
----------
    0   all three questions passed
    1   at least one question failed (the child is reported, not re-run)
    2   usage error: no --shape, an invalid regex, a bad --exit, or no command
        after `--`
"""

import argparse
import re
import subprocess
import sys

Q1 = "the program exited as required"
Q2 = "every shape clause matched"
Q3 = "this report cannot satisfy the shape it checked"


def clauses_of(values):
    """CMake list semantics: a `;` in the value separates items."""
    out = []
    for value in values:
        for part in value.split(";"):
            part = part.strip()
            if part:
                out.append(part)
    return out


def parse_exit(spec):
    """`0`/`N`/`zero` -> an exact code; `nonzero` -> any non-zero code."""
    if spec == "nonzero":
        return ("nonzero", None)
    if spec == "zero":
        return ("exact", 0)
    try:
        return ("exact", int(spec, 10))
    except ValueError:
        return None


def exit_line(mode, code):
    if mode == "nonzero":
        return "the program exited non-zero"
    return "the program exited %d" % code


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="gated_run.py",
        description="run a command under three independent assertions",
    )
    ap.add_argument("--shape", required=True, action="append",
                    help="regex clause the output must match (question 2); "
                         "repeat the flag, or join with ';', for several clauses")
    ap.add_argument("--exit", default="0", dest="exit_spec",
                    help="required exit code: 0 (default), N, or 'nonzero'")
    ap.add_argument("cmd", nargs=argparse.REMAINDER,
                    help="the program to run, after a literal `--`")
    args = ap.parse_args(argv)

    cmd = list(args.cmd)
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        sys.stderr.write("gated_run: usage: gated_run.py --shape REGEX [--shape REGEX ...] [--exit 0|N|nonzero] -- CMD [ARG ...]\n")
        sys.stderr.write("gated_run: nothing was run; there is no command after `--`.\n")
        return 2

    want = parse_exit(args.exit_spec)
    if want is None:
        sys.stderr.write("gated_run: --exit %r is not 0, an integer, or 'nonzero'.\n"
                         % args.exit_spec)
        sys.stderr.write("gated_run: nothing was run.\n")
        return 2

    texts = clauses_of(args.shape)
    if not texts:
        sys.stderr.write("gated_run: --shape was empty; nothing to match.\n")
        sys.stderr.write("gated_run: nothing was run.\n")
        return 2
    compiled = []
    for text in texts:
        try:
            # re.S, because CMake's regex engine lets `.` cross a newline and
            # Python's does not: the property `a.*b` matches an output that
            # prints `a` and `b` on separate lines, and this wrapper has to ask
            # the same question the property asked.  (Measured: with the default
            # flags, `result_runtime` failed question 2 while its output
            # plainly carried both halves on two lines.)
            compiled.append((text, re.compile(text, re.S)))
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

    mode, code = want
    if mode == "nonzero":
        exit_ok = proc.returncode != 0
    else:
        exit_ok = proc.returncode == code
    hits = [(text, rx.search(out) is not None) for text, rx in compiled]
    matched = sum(1 for _t, ok in hits if ok)
    shape_ok = matched == len(hits)

    report = []
    report.append("gated_run: question 1 of 3: %s .......... %s (exit code %d)"
                  % (exit_line(mode, code), "PASS" if exit_ok else "FAIL", proc.returncode))
    report.append("gated_run: question 2 of 3: %s (%d clause(s); CMake would read a ';' "
                  "list as ANY, this asks ALL) .......... %s (%d of %d matched)"
                  % (Q2, len(hits), "PASS" if shape_ok else "FAIL", matched, len(hits)))
    for i, (text, ok) in enumerate(hits, 1):
        report.append("gated_run:   clause %d of %d: %r .......... %s"
                      % (i, len(hits), text, "matched" if ok else "no match"))

    # Question 3: a clause is decorative when THIS report can satisfy it and the
    # program's own output cannot -- a property on the calling test would then
    # pass on the wrapper's words.  Measured, not assumed: the check runs on the
    # report with the question-3 verdict itself left out, so that verdict cannot
    # be what satisfies a clause.
    # The `clause N of M` lines quote the pattern back with %r, and a pattern
    # containing `.*` matches its own quotation -- so the probe leaves the
    # quotation out.  What is left is everything the wrapper says on its own
    # behalf: a clause that one of those lines satisfies is a clause the
    # wrapper could satisfy while the program did not.
    probe_lines = [ln for ln in report if not ln.startswith("gated_run:   clause ")]
    probe = ("\n".join(probe_lines)
             + "\ngated_run: question 3 of 3: %s .......... " % Q3)
    self_bad = [text for text, rx in compiled
                if rx.search(probe) and not rx.search(out)]
    self_ok = not self_bad
    report.append("gated_run: question 3 of 3: %s .......... %s"
                  % (Q3, "PASS" if self_ok else "FAIL"))
    for text in self_bad:
        report.append("gated_run:   this report satisfies this clause and the program's "
                      "output does not: %r" % text)

    failed = []
    if not exit_ok:
        failed.append("%s [FAIL]" % exit_line(mode, code))
    if not shape_ok:
        failed.append("%s [FAIL]" % Q2)
    if not self_ok:
        failed.append("%s [FAIL]" % Q3)

    if not failed:
        report.append("gated_run: 3 question(s) asked, 0 failed -- the exit code is as "
                      "required AND all %d shape clause(s) matched AND this report "
                      "matches none of them." % len(hits))
    else:
        report.append("gated_run: 3 question(s) asked, %d failed: %s"
                      % (len(failed), "; ".join(failed)))
        report.append("gated_run: the exit code is judged here because PASS_REGULAR_EXPRESSION replaces it")
        report.append("gated_run: (cmake --help-property PASS_REGULAR_EXPRESSION: \"The process exit code is ignored.\").")
        if not shape_ok:
            report.append("gated_run: and ALL clauses are required here because a ';' list is read as ANY")
            report.append("gated_run: there: a property written as \"ok;<pin>\" passes with <pin> gone.")
        if not self_ok:
            report.append("gated_run: and a clause this report can satisfy is a clause the report")
            report.append("gated_run: could satisfy on the test's behalf; write it so it cannot.")

    print("\n".join(report))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
