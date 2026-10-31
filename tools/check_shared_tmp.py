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
# Four spellings of the same mistake, all measured on 2026-10-31 with a fixture
# holding one line of each; the first pattern caught only line 1 of 4:
#   1  os.path.join(tempfile.gettempdir(), "name")
#   2  "/tmp/name"
#   3  f"{tempfile.gettempdir()}/name"
#   4  tempfile.gettempdir() + "/name"
# A pattern that only knows the first is a pattern that reports "nothing shared"
# about a tree that shares three other ways -- so the rule is now: any mention of
# the temp directory that is not immediately a mkdtemp() call, and any literal
# /tmp path.  mkdtemp() is the fix, so it is the one thing excluded by name.
# Whitespace between the tokens is legal Python and invisible to a pattern that
# spells the dots and parens tightly.  orchid measured two such spellings on
# ac5a11c -- `tempfile.gettempdir ()` and `tempfile . gettempdir ( )` -- both
# compile, both build a shared path, and both were reported ok with the
# denominator printed.  So every separator here is \s* and the dot is escaped
# with optional space around it, not written as a bare dot.
TEMPDIR_CALL = re.compile(r"tempfile\s*\.\s*gettempdir\s*\(\s*\)")   # forms 1, 3, 4
TMP_LITERAL = re.compile(r"(?<![\w.])[\"']/tmp/")                        # form 2
# The sanctioned call: it CREATES a directory instead of naming a shared one.
MKDTEMP = re.compile(r"tempfile\s*\.\s*mkdtemp\s*\(")

REASON = (
    "builds an artifact at a path shared by every process on the machine. "
    "gettempdir() is /tmp on Linux, so a concurrent run -- or this suite's own "
    "subprocess re-entry -- overwrites the file while it is being executed. "
    "Measured with three worktrees at once: one run died with "
    "PermissionError [Errno 13] on the artifact, another's corpus control "
    "silently passed on the overwritten binary. Use "
    "tempfile.mkdtemp(prefix=...) and join the artifact name onto that."
)


def code_lines(text):
    """({lineno: line without comments}, {lineno: line without comments or strings}).

    The widened pattern matches a mention of the temp directory wherever it
    appears, and the first run of it reddened tools/mir_roundtrip.test.py:48 -- a
    COMMENT explaining the removed defect.  A check that flags its own
    explanation teaches people to delete the explanation, which is the opposite
    of what this file is for.

    Blanking (not dropping) is what makes this work: `tokenize` gives exact
    columns, so a line keeps its punctuation in code and loses it inside a
    comment or a string -- `note = "/tmp/x"` is one code line whose string is
    invisible, while `p = "/tmp/x" / "y"` keeps the operator.  Reconstructing the
    line by joining tokens instead turns `tempfile.gettempdir()` into
    `tempfile . gettempdir ( )`, which the pattern cannot see; measured, and it
    made this check report ok on a fixture holding all four bad spellings.
    """
    import io
    import tokenize

    masked = [list(ln) for ln in text.splitlines()]
    try:
        for tok in tokenize.generate_tokens(io.StringIO(text).readline):
            if tok.type != tokenize.COMMENT:
                continue
            (sr, sc), (er, ec) = tok.start, tok.end
            for ln in range(sr, er + 1):
                if not (0 <= ln - 1 < len(masked)):
                    continue
                row = masked[ln - 1]
                a = sc if ln == sr else 0
                b = ec if ln == er else len(row)
                for i in range(a, min(b, len(row))):
                    row[i] = " "
    except (tokenize.TokenError, IndentationError, SyntaxError):
        # A file this cannot tokenize is still a file with lines; blank whole
        # comment lines only, rather than reporting nothing about it.
        return {i: ln for i, ln in enumerate(text.splitlines(), 1)
                if not ln.lstrip().startswith("#")}
    # Two masks, because the two defects live in different places: a call to
    # tempfile.gettempdir() is CODE, while a literal "/tmp/x" is a STRING.  One
    # mask that blanks both (or neither) misses one of the two by construction.
    uncommented = {i: "".join(row) for i, row in enumerate(masked, 1)
                   if "".join(row).strip()}
    no_strings = {}
    for i, row in uncommented.items():
        no_strings[i] = row
    try:
        for tok in tokenize.generate_tokens(io.StringIO(text).readline):
            if tok.type != tokenize.STRING:
                continue
            (sr, sc), (er, ec) = tok.start, tok.end
            for ln in range(sr, er + 1):
                if ln not in no_strings:
                    continue
                row = list(no_strings[ln])
                a = sc if ln == sr else 0
                b = ec if ln == er else len(row)
                for k in range(a, min(b, len(row))):
                    row[k] = " "
                no_strings[ln] = "".join(row)
    except (tokenize.TokenError, IndentationError, SyntaxError):
        pass
    return uncommented, no_strings

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
        uncommented, no_strings = code_lines(text)
        seen = set()
        for lineno, line in uncommented.items():
            if MKDTEMP.search(line):
                continue
            # gettempdir() is code, so it is looked for where strings are blank;
            # a literal /tmp path is a string, so it is looked for where they are
            # not.  Both ignore comments.
            for pattern, hay in ((TEMPDIR_CALL, no_strings.get(lineno, "")),
                                 (TMP_LITERAL, line)):
                for m in pattern.finditer(hay):
                    key = (lineno, m.group(0))
                    if key in seen:
                        continue
                    seen.add(key)
                    failures.append(
                        f"{os.path.relpath(path, REPO_ROOT)}:{lineno}: "
                        f"{m.group(0).strip()!r} in {line.strip()[:90]!r} -- {REASON}"
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
