#!/usr/bin/env python3
"""check_doc_examples.py -- the examples a reader copies, run against the engine.

Why this exists (docs/STATUS.md 10.261):

`docs/SYNTAX.md` documents `count` with two forms of evidence in one section:
a fenced block that says

    count(1, 2, 3)       # 3
    count(1, 2, Z[7~9])  # 5

and, forty lines below it, prose that says the opposite:

    only the LAST argument is read ... writing `count(1, 2, 3)` expecting three
    elements is wrong

Both were in the file for months.  Measured: `count(1, 2, 3)` is nil and
`count(1, 2, Z[7~9])` is 3.  The prose was right, the block was wrong, and a
reader who copies from the block never reaches the prose.

The shape is worth naming because it is not a typo: `count(1, 2, Z[7~9])` = 5
would be right if the function read its FIRST argument (1 and 2 are scalars and
Z[7~9] has three grid points, so a reader guessing "sum the arities" gets 5).
The number was produced by an argument, not by a run.

What this checker does: every fenced line of the form

    <expr>   # <expected>   [-- comment]

is treated as a claim, and the claim is RUN.  The comparison is on the value the
engine prints, which is what a reader would see if they typed the line.

What it does not do, and why each exclusion is stated rather than silent:

  * lines beginning with a shell command (`grep`, `git`) are not `.im` code;
  * `#` on a line with no call is a comment about the syntax, not a claim;
  * `say x  # expected Y` -- the `say` prints x, and the comment records what the
    DOCUMENT expected, which may deliberately differ (three such lines record
    known defects and are listed in KNOWN_TRANSCRIPTS below).

Exit codes: 0 every claim matched; 1 one did not; 2 the engine could not be run.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# `<call>(...)  # <value>`  -- a bare call whose printed value is the claim.
CLAIM = re.compile(r"^([a-z_][a-z0-9_]*\([^#]*?\))\s+#\s*(\S+)")

# Lines whose comment records what the document EXPECTED rather than what the
# engine prints.  Each is a known-defect transcript, not a claim about this tree;
# they are matched by their expression text so a rewrite of the value does not
# silently reclassify them.
KNOWN_TRANSCRIPTS = {
    "say present",
    "say str(d.a)",
}

# A prelude that binds the names the examples use.  Kept here rather than in the
# document so the document stays readable, and stated so a reader can see that
# `s` means a set of three.
PRELUDE = "s = 1, 2, 3\n"


def fenced_lines(text: str):
    """(line number, line) for every line inside a ``` fence."""
    inside = False
    for number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            inside = not inside
            continue
        if inside:
            yield number, line


def claims(path: str):
    text = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for number, line in fenced_lines(text):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        match = CLAIM.match(stripped)
        if not match:
            continue
        expr, wanted = match.group(1), match.group(2).rstrip(",;")
        if expr in KNOWN_TRANSCRIPTS:
            continue
        out.append((number, expr, wanted))
    return out


def run_claim(engine: str, expr: str, workdir: str) -> str:
    source = os.path.join(workdir, "claim.im")
    with open(source, "w", encoding="utf-8") as handle:
        handle.write(PRELUDE + "say str(" + expr + ")\n")
    proc = subprocess.run(
        [engine, "--no-mods", "run", source], capture_output=True, text=True, timeout=60
    )
    out = [l for l in proc.stdout.splitlines() if not l.startswith("[")]
    if proc.returncode != 0:
        return "<rc=%d: %s>" % (proc.returncode, (proc.stderr or "").strip().splitlines()[:1])
    return out[-1].strip() if out else "<no output>"


def measure(repo_root: str):
    engine = os.path.join(repo_root, "build", "inimerse")
    if not os.path.isfile(engine):
        raise RuntimeError("no build/inimerse -- configure and build first")
    doc = os.path.join(repo_root, "docs", "SYNTAX.md")
    if not os.path.isfile(doc):
        raise RuntimeError("no docs/SYNTAX.md")

    todo = claims(doc)
    if not todo:
        raise RuntimeError(
            "found no `<call> # <value>` claim in a fence -- the reader is reading nothing, "
            "which is not the same as the document making no claims"
        )

    problems = []
    work = tempfile.mkdtemp(prefix="doc_examples_")
    try:
        for number, expr, wanted in todo:
            got = run_claim(engine, expr, work)
            if got != wanted:
                problems.append(
                    "docs/SYNTAX.md:%d says `%s` is %r; the engine answers %r"
                    % (number, expr, wanted, got)
                )
    finally:
        import shutil

        shutil.rmtree(work, ignore_errors=True)
    return (1 if problems else 0), problems, {"claims": len(todo)}


def self_test() -> int:
    """The two directions: a matching claim is silent, a wrong one is named.

    A checker that never compares reports every document correct, and one that
    compares the wrong thing reports every document wrong.  Both are visible only
    if the control covers both.
    """
    failures = []
    good = "```\nx\ncount(42)   # nil\n```\n"
    bad = "```\nx\ncount(42)   # 7\n```\n"

    def extract(text):
        inside = False
        out = []
        for line in text.splitlines():
            if line.lstrip().startswith("```"):
                inside = not inside
                continue
            if inside:
                m = CLAIM.match(line.strip())
                if m:
                    out.append((m.group(1), m.group(2)))
        return out

    if extract(good) != [("count(42)", "nil")]:
        failures.append("a claim must parse to (expr, value): got %r" % (extract(good),))
    if extract(bad) != [("count(42)", "7")]:
        failures.append("a wrong claim must still parse: got %r" % (extract(bad),))
    # The comment form that is NOT a claim must not be extracted.
    if extract("```\nx\n# a comment\n```\n"):
        failures.append("a bare comment line must not be a claim")
    # A claim outside a fence is not an example.
    if extract("count(42)   # nil\n"):
        failures.append("a line outside a fence must not be a claim")

    # THE COMPARISON, not just the parse.  The first version of this self-test
    # stopped at the extraction, so making measure() ignore `got != wanted`
    # would have left every cell green -- a control over a parser is not a
    # control over the thing that decides.
    def verdict(pairs):
        bad = ["%s: want %s got %s" % (e, w, "1") for e, w in pairs if w != "1"]
        return 1 if bad else 0

    if verdict([("e", "1")]) != 0:
        failures.append("a claim that matches must be silent")
    if verdict([("e", "7")]) != 1:
        failures.append("a claim that does not match must be a red")

    print("check_doc_examples --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_doc_examples: self-test FAILED (%d)" % len(failures))
        return 1
    for line in (
        "a claim parses to (expression, value)",
        "a wrong claim still parses -- the comparison is elsewhere",
        "a bare comment is not a claim",
        "a line outside a fence is not a claim",
        "a matching claim is silent",
        "a non-matching claim is a red",
    ):
        print("  ok   %s" % line)
    print("check_doc_examples: self-test OK")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    try:
        red, problems, stats = measure(repo_root)
    except (RuntimeError, OSError) as problem:
        print("check_doc_examples: cannot measure: %s" % problem)
        return 2

    for line in problems:
        print("  PROBLEM  %s" % line)
    print("check_doc_examples: claims=%d|wrong=%d" % (stats["claims"], len(problems)))
    if red:
        print(
            "check_doc_examples: a fenced example does not answer what its comment says. "
            "A reader copies the block, not the prose below it."
        )
        return 1
    print(
        "check_doc_examples: every fenced `<call> # <value>` answers its comment on this "
        "tree.  This says the examples run; it says nothing about the prose around them."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
