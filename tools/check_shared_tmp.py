#!/usr/bin/env python3
"""Fail if a test suite builds an artifact at a path every process shares.

`tempfile.gettempdir()` is `/tmp` on Linux, so `os.path.join(tempfile.gettempdir(),
"name")` is one path for every worktree and every concurrent process on the
machine.  Two things then happen, and both were measured on 2026-10-30 with
three worktrees running `tools/mir_roundtrip.test.py` at once:

  * one process writes the file while another is executing it, and the loser
    dies with `PermissionError: [Errno 13] Permission denied:
    '/tmp/mir_roundtrip_harness'` -- no verdict line, no assertion, no
    diagnosis;
  * worse, a control group runs the overwritten binary and silently passes:
    `MIR-CORPUSCTL|case=simulate-missing|want=red|got=green|rc=1|named=0`.
    A wrong binary answering "nothing is wrong" is the failure shape this
    repository treats as a defect rather than bad luck.

It does not need parallelism to bite.  `tools/mir_roundtrip.test.py` re-runs
itself through `subprocess` for its corpus controls, and the child's `main()`
starts with `build_harness()`, so parent and child already race over the same
path in an ordinary single-threaded run.  Parallel gates only widen the window,
which is why "it passes when I run it alone" never refuted it.

The rule: an artifact a suite builds for itself goes in a directory it created
(`tempfile.mkdtemp(prefix=...)`).  `-o /tmp/fixed-name` is what to look for.

Two rules about this check's own output, both carried over from
`tools/check_test_ports.py` because both were measured there:

  * the success line carries its denominator -- a literal sentence with no
    number in it prints the same thing for an empty directory as for this
    repository;
  * an empty input set is not a passing input set: it refuses and names the
    object it could not find.
"""

import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TOOLS = REPO_ROOT / "tools"

# `tempfile.gettempdir()` joined with a literal name.  The name may be built by
# concatenation (`"name-" + tag`), so the literal may be empty -- what matters is
# that the *directory* is the shared one.
SHARED = re.compile(
    r"os\.path\.join\(\s*tempfile\.gettempdir\(\s*\)\s*,\s*(?:f?[\"']([^\"']*)[\"'])?"
)

REASON = (
    "builds an artifact at a path shared by every process on the machine. "
    "gettempdir() is /tmp on Linux, so a concurrent run -- or this suite's own "
    "subprocess re-entry -- overwrites the file while it is being executed. "
    "Measured with three worktrees at once: one run died with "
    "PermissionError [Errno 13] on the artifact, another's corpus control "
    "silently passed on the overwritten binary. Use "
    "tempfile.mkdtemp(prefix=...) and join the artifact name onto that."
)


def main():
    failures = []
    suites = sorted(TOOLS.glob("*.py"))

    if not suites:
        print(
            f"check_shared_tmp: no *.py under {TOOLS} -- the input set is empty, "
            f"so there is nothing here for this check to be true about, and a "
            f"check with nothing to check is not a passing check",
            file=sys.stderr,
        )
        return 1

    scanned = 0
    for path in suites:
        text = path.read_text(encoding="utf-8", errors="replace")
        if path.name.startswith("check_shared_tmp"):
            continue  # this file quotes the pattern in its own docstring
        scanned += 1
        for m in SHARED.finditer(text):
            line = text[: m.start()].count("\n") + 1
            failures.append(
                f"{os.path.relpath(path, REPO_ROOT)}:{line}: {m.group(0).strip()} "
                f"-> {m.group(1)!r} {REASON}"
            )

    if scanned == 0:
        # The guard above counts files before this check excludes itself, so a
        # directory holding nothing but this file reaches here with zero input
        # and would otherwise print the same sentence as this repository.
        # Measured: it did exactly that before this branch existed.
        print(
            f"check_shared_tmp: {len(suites)} file(s) under {TOOLS} and none of "
            f"them is an input -- the input set is empty, so there is nothing "
            f"here for this check to be true about",
            file=sys.stderr,
        )
        return 1

    if failures:
        print("check_shared_tmp: FAILED", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        return 1

    print(
        f"check_shared_tmp: ok ({scanned} *.py scanned; every artifact a suite "
        f"builds lands in a directory that suite created)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
