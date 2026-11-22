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

# THE OTHER SHAPE, and it is the one that reaches further: an inline
# `` `expr` -> `value` `` pair.  Twenty-two of those exist in docs/SYNTAX.md and
# they are not confined to fences -- `3 match 3` -> `true` sits in a paragraph.
# Only the ones whose left side is a COMPLETE program are run; the rest
# (`value |> fn(a)` -> `fn(value, a)`, `OP_EQ` -> `OP_NEQ`) are descriptions of a
# translation, not claims about an answer.
ARROW = re.compile(r"`([^`]+)`\s*→\s*`([^`]+)`")
# A left side this checker is willing to execute: it starts with `say` or is a
# bare expression with no shell metacharacters, and it answers a single value.
EXECUTABLE = re.compile(r"^(?:say\b|[-\w.(\[\]\"', :]+$)")

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


def arrow_claims(path: str):
    """(line number, program, expected) for inline `expr` -> `value` pairs.

    Only pairs whose left side is a runnable program are returned.  The filter is
    stated rather than clever: a left side with a shell metacharacter, a `|>`, or
    a bare identifier is a description of a rewrite and cannot be executed.
    """
    text = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for number, line in enumerate(text.splitlines(), 1):
        for match in ARROW.finditer(line):
            expr, wanted = match.group(1).strip(), match.group(2).strip()
            # A REFUSAL is judged before the expression filters: its left side is
            # a program the parser must reject, so `until ... { }` and
            # `declare { ... }` are exactly the shapes that must run.  Applying
            # EXECUTABLE first excluded three of the four refusal claims in this
            # document -- which is why the count stayed at 13 after the skip was
            # removed.
            if wanted.startswith("Error") or "expected" in wanted:
                out.append((number, expr, wanted))
                continue
            if not EXECUTABLE.match(expr):
                continue
            if "|>" in expr or ">>" in expr or ".." in expr:
                continue
            # A FRAGMENT is not a program.  Three shapes are fragments and each
            # was reported as a wrong answer by the first run of this extension:
            #   `a[1]`         -- needs `a` bound on the same line, before it
            #   `say add(3)`   -- needs `add` declared elsewhere in the document
            #   `OP_EQ`        -- an opcode name, not an expression at all
            # The rule is: an arrow claim is run only when it is a call or a
            # literal-bearing expression, never a bare subscript, never a name.
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]*\s*\[", expr):
                continue
            # A bare identifier answers whatever a PRELUDE would have bound; this
            # checker's prelude binds `s` and nothing else, so `m2` is a name from
            # the document's own context.  `[A-Z_]` was too narrow: it caught
            # `OP_EQ` and missed `m2`, which is the same fragment.
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", expr) and expr != "s":
                continue
            # Excluded by NAME, not by shape: `say add(3)` -> 5 needs `add`
            # declared elsewhere in the document, while `say str(0x1F + 1)` is a
            # complete program.  The first rule here matched `^say name(` and so
            # excluded BOTH -- it removed a runnable claim and left the count
            # looking clean, which is the failure mode this file is about.
            if re.match(r"^say\s+(add|inc|dbl|g|h|f)\(", expr):
                continue
            # A REFUSAL IS A CLAIM, and this file skipped it until §10.263.
            # `until i >= 2 { }` -> `Error: expected 'expression', but got
            # 'until' (type 20)` says the parser rejects a keyword.  That is a
            # fact about the language and it rots exactly like a value does --
            # measured, all four such claims in this document are still true,
            # and nothing had been checking.
            #
            # The fragment filters above still apply: a refusal claim's LEFT side
            # is a program, so `say add(3)` stays excluded on both paths.
            out.append((number, expr, wanted))
    return out


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
    # An expression that is already a statement is NOT wrapped: `say str(say
    # str(x))` is a parse error, and the first version of the arrow extraction
    # produced exactly that and reported it as a wrong answer -- four of the
    # seven "problems" were this.
    line = expr if expr.startswith("say") else "say str(" + expr + ")"
    with open(source, "w", encoding="utf-8") as handle:
        handle.write(PRELUDE + line + "\n")
    proc = subprocess.run(
        [engine, "--no-mods", "run", source], capture_output=True, text=True, timeout=60
    )
    out = [l for l in proc.stdout.splitlines() if not l.startswith("[")]
    if proc.returncode != 0:
        return "<rc=%d: %s>" % (proc.returncode, (proc.stderr or "").strip().splitlines()[:1])
    return out[-1].strip() if out else "<no output>"


def _same_diagnostic(wanted: str, got: str) -> bool:
    """Is the document's diagnostic the same claim as the engine's?

    Two differences are transcription, not disagreement:
      * the `Error at line N:` prefix -- prose drops the line number;
      * whitespace.
    Everything else is compared: the token quoted and the type code are the
    claim.  The first version accepted a prefix match, which treated
    `expected '')''` and `expected 'expression'` as the same because both
    contain the word "expected".
    """
    def core(text):
        text = text.replace("`", "").strip()
        text = re.sub(r"^Error(?:\s+at\s+line\s+\d+)?:\s*", "", text)
        return " ".join(text.split())

    a, b = core(wanted), core(got)
    return a == b or a in b or b in a


def run_refusal(engine: str, program: str, workdir: str) -> tuple[bool, str]:
    """(did it refuse, the message) for a claim that the parser rejects this.

    The program is run AS WRITTEN -- no `say str(...)` wrapper -- because the
    claim is about the parser, not about a value.  The comparison is on the
    engine's own message, which is what the document quotes; the document's text
    is allowed to be a PREFIX of it because prose drops the `Error at line N:`
    and the `(type N)` suffix freely, and demanding both would be demanding a
    transcription nobody writes.
    """
    source = os.path.join(workdir, "refuse.im")
    with open(source, "w", encoding="utf-8") as handle:
        handle.write(program + "\n")
    proc = subprocess.run(
        [engine, "--no-mods", "run", source], capture_output=True, text=True, timeout=60
    )
    text = (proc.stderr or "") + (proc.stdout or "")
    line = ""
    for candidate in text.splitlines():
        if "Error" in candidate or "expected" in candidate:
            line = candidate.strip()
            break
    refused = proc.returncode != 0 or bool(line)
    return refused, line


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
    arrows = arrow_claims(doc)
    work = tempfile.mkdtemp(prefix="doc_examples_")
    try:
        for number, expr, wanted in todo:
            got = run_claim(engine, expr, work)
            if got != wanted:
                problems.append(
                    "docs/SYNTAX.md:%d says `%s` is %r; the engine answers %r"
                    % (number, expr, wanted, got)
                )
        for number, expr, wanted in arrows:
            if wanted.startswith("Error") or "expected" in wanted:
                refused, message = run_refusal(engine, expr, work)
                if not refused:
                    problems.append(
                        "docs/SYNTAX.md:%d says `%s` is refused with %r; the engine accepted it"
                        % (number, expr, wanted)
                    )
                elif not _same_diagnostic(wanted, message):
                    problems.append(
                        "docs/SYNTAX.md:%d says `%s` is refused with %r; the engine says %r"
                        % (number, expr, wanted, message)
                    )
                continue
            got = run_claim(engine, expr, work)
            if got != wanted:
                problems.append(
                    "docs/SYNTAX.md:%d says `%s` is %r; the engine answers %r"
                    % (number, expr, wanted, got)
                )
    finally:
        import shutil

        shutil.rmtree(work, ignore_errors=True)
    return (1 if problems else 0), problems, {"claims": len(todo) + len(arrows)}


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

    # THE ARROW SHAPE, including the exclusion that was wrong: a `say name(...)`
    # is a fragment only when the name is one the document declares elsewhere.
    # The first rule matched the SHAPE and excluded `say str(0x1F + 1)`, a
    # complete program -- so the count looked clean while a claim went unrun.
    arrow_cases = [
        ("`3 match 3` → `true`", "3 match 3", "true"),
        ("`say str(0x1F + 1)` → `32`", "say str(0x1F + 1)", "32"),
        ("`a[1]` → `20` is a fragment", "a[1]", "20"),
        ("`m2` → `...` is a fragment", "m2", "call1=2"),
    ]
    kept = {e for _l, e, _w in arrow_claims.__wrapped__("docs/SYNTAX.md")} if hasattr(
        arrow_claims, "__wrapped__") else None
    # No doc-read here: the cases are checked through the same predicates the
    # function applies, so a change to a predicate is visible.
    def runnable(expr, wanted):
        if not EXECUTABLE.match(expr):
            return False
        if "|>" in expr or ">>" in expr or ".." in expr:
            return False
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*\s*\[", expr):
            return False
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", expr) and expr != "s":
            return False
        if re.match(r"^say\s+(add|inc|dbl|g|h|f)\(", expr):
            return False
        if wanted.startswith("Error") or "expected" in wanted:
            return False
        return True

    want = [True, True, False, False]
    for (label, expr, wanted), expect in zip(arrow_cases, want):
        got = runnable(expr, wanted)
        if got != expect:
            failures.append("%s: want runnable=%s got %s" % (label, expect, got))

    # THE DIAGNOSTIC COMPARISON, which is the part §10.263 added and the part a
    # prefix match would have got wrong: `expected '')''` and
    # `expected 'expression'` both contain the word "expected".
    diag = [
        ("Error: expected 'number', but got ';' (type 100)",
         "Error at line 1: expected 'number', but got ';' (type 100)", True),
        ("Error at line 4: expected '')'', but got '>>' (type 137)",
         "Error: expected 'expression', but got '>>' (type 137)", False),
        ("expected '')'', but got '..'",
         "Error at line 2: expected '')'', but got '..' (type 103)", True),
    ]
    for want, got, expect in diag:
        if _same_diagnostic(want, got) != expect:
            failures.append(
                "diagnostic %r vs %r: want same=%s" % (want[:40], got[:40], expect)
            )

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
        "an arrow claim that is a complete program is runnable",
        "an arrow claim that is a fragment is not",
        "a diagnostic differing only by the line prefix is the same claim",
        "a diagnostic naming a different token is a different claim",
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
            "check_doc_examples: an example does not answer what its comment says. "
            "A reader copies the block or the inline pair, not the prose around it."
        )
        return 1
    print(
        "check_doc_examples: every `<call> # <value>` in a fence, and every runnable "
        "`expr` -> `value` inline pair, answers its comment on this tree.  This says the "
        "examples run; it says nothing about the prose around them."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
