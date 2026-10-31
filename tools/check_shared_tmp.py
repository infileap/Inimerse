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

Four rules about this check's own output, each of which was learned by being
wrong first:

  * the success line carries its denominator -- a literal sentence with no
    number in it prints the same thing for an empty directory as for this
    repository;
  * an empty input set is not a passing input set: it refuses and names the
    object it could not find;
  * the verdict travels as a WORD on a line of its own
    (`SHARED-TMP-VERDICT|...|verdict=PASS`), because a consumer that reads only
    an exit status cannot tell "the check found something" from "the check
    never got as far as its verdict".  The same reason `stage_fences` reads
    `FENCES-VERDICT|`;
  * the self-test exists because a widened pattern is a pattern that may have
    widened into uselessness: on 2026-10-31 the first widening of this check
    reported "ok" on a fixture holding all four bad spellings, and the only
    way that was visible was that somebody measured it.
"""

import io
import os
import re
import sys
import tokenize
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
# A literal /tmp path.  The slash AFTER /tmp is not required: os.path.join('/tmp',
# 'name') is the same shared directory, and the old pattern demanded a trailing
# slash, so that line was reported ok.  What is required is that /tmp be the
# whole directory: a path separator, or the end of the string.
TMP_LITERAL = re.compile(r"(?<![\w.])[\"']/tmp(?:/|[\"'])")          # form 2
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


def _blank(text, token_types):
    """text with the given token kinds replaced by spaces, length preserved.

    Blanking (not dropping) is what makes this work: `tokenize` gives exact
    columns, so a line keeps its punctuation in code and loses it inside a
    comment or a string -- `note = "/tmp/x"` is one code line whose string is
    invisible, while `p = "/tmp/x" / "y"` keeps the operator.  Reconstructing
    the line by joining tokens instead turns `tempfile.gettempdir()` into
    `tempfile . gettempdir ( )`, which the pattern cannot see; measured, and it
    made this check report ok on a fixture holding all four bad spellings.

    The length is preserved on purpose: the statements below are cut out of the
    masked text by byte offset, so a mask that shifted the text would cut the
    wrong characters out.
    """
    rows = text.splitlines(keepends=True)
    masked = [list(r) for r in rows]
    try:
        for tok in tokenize.generate_tokens(io.StringIO(text).readline):
            if tok.type not in token_types:
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
        return text
    return "".join("".join(r) for r in masked)


def masks(text):
    """(without comments, without comments or strings).

    Two masks, because the two defects live in different places: a call to
    tempfile.gettempdir() is CODE, while a literal "/tmp/x" is a STRING.  One
    mask that blanks both (or neither) misses one of the two by construction.
    """
    uncommented = _blank(text, (tokenize.COMMENT,))
    no_strings = _blank(uncommented, (tokenize.STRING, tokenize.FSTRING_MIDDLE))
    return uncommented, no_strings


def statements(text):
    """[(start_offset, end_offset)] of the logical statements in text.

    A statement ends at a depth-0 `;` or at a depth-0 newline.  This is the
    unit the rules are applied to, and it is what closes two narrow openings
    measured on 2026-10-31:

      * a line holding BOTH a sanctioned mkdtemp() call and a shared path
        (`d = tempfile.mkdtemp(...); h = os.path.join(tempfile.gettempdir(),
        "x")`) is two statements, so the first one's exemption does not cover
        the second;
      * a call split across physical lines (`os.path.join(tempfile.` / newline /
        `gettempdir(), "x")`) is ONE statement, so its two halves are not two
        unrelated lines that each happen to be innocent.

    Depth is tracked so a `;` inside parentheses (or a lambda, or a slice) does
    not end a statement.
    """
    rows = text.splitlines(keepends=True)
    offsets = []
    off = 0
    for r in rows:
        offsets.append(off)
        off += len(r)

    def pos(row, col):
        if row - 1 >= len(offsets):
            return len(text)
        return offsets[row - 1] + col

    spans = []
    depth = 0
    begin = 0
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        toks = []
    for tok in toks:
        if tok.type == tokenize.OP:
            if tok.string in "([{":
                depth += 1
            elif tok.string in ")]}":
                depth = max(0, depth - 1)
            elif tok.string == ";" and depth == 0:
                end = pos(tok.end[0], tok.end[1])
                spans.append((begin, end))
                begin = end
                continue
        if tok.type in (tokenize.NEWLINE, tokenize.NL) and depth == 0:
            end = pos(tok.end[0], tok.end[1])
            if end > begin:
                spans.append((begin, end))
            begin = end
    if begin < len(text):
        spans.append((begin, len(text)))
    return spans


def _call_extent(text, call_start):
    """(start, end) of the parenthesised argument list starting at call_start.

    The exemption for the sanctioned call covers the call AND its arguments --
    a prefix argument like `prefix="/tmp/x"` is inside the call, not outside
    it -- and nothing else.  A shared path elsewhere in the same statement
    (`d = tempfile.mkdtemp(...) if c else os.path.join(tempfile.gettempdir(),
    "x")`) is outside the call and stays reported.
    """
    i = text.find("(", call_start)
    if i < 0:
        return (call_start, call_start)
    depth = 0
    for j in range(i, len(text)):
        ch = text[j]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return (call_start, j + 1)
    return (call_start, len(text))


def _sanctioned_spans(code):
    """[(start, end)] of the sanctioned calls, in the code mask's offsets."""
    spans = []
    for m in MKDTEMP.finditer(code):
        spans.append(_call_extent(code, m.start()))
    return spans


def findings(text):
    """[(line_number, matched_text, statement_text)] for one file's text."""
    uncommented, no_strings = masks(text)
    out = []
    for a, b in statements(text):
        code = no_strings[a:b]
        literals = uncommented[a:b]
        if not code.strip() and not literals.strip():
            continue
        # The exemption is judged where the sanctioned call is CODE (strings
        # blanked), so a docstring that merely SAYS "tempfile.mkdtemp(...)"
        # cannot become a shield for the shared path next to it, and it covers
        # the call's own arguments and nothing else.
        exempt = [(a + s, a + e) for s, e in _sanctioned_spans(code)]

        def is_exempt(pos):
            return any(s <= pos < e for s, e in exempt)

        for pattern, hay in ((TEMPDIR_CALL, code),
                             (TMP_LITERAL, literals)):
            for m in pattern.finditer(hay):
                if is_exempt(a + m.start()):
                    continue
                line_no = text.count("\n", 0, a + m.start()) + 1
                stmt = literals.strip() or code.strip()
                out.append((line_no, m.group(0).strip(), stmt[:90]))
    return out


# The self-test's own cases.  Each is (name, source, expected number of
# findings).  They exist because a widened pattern is a pattern that may have
# widened into uselessness: on 2026-10-31 the first widening of this check
# reported "ok" on a fixture holding all four bad spellings, and the only way
# that was visible was that somebody measured it.
SELFTEST_CASES = [
    ("sanctioned-call-alone",
     'd = tempfile.mkdtemp(prefix="x")\n', 0),
    ("sanctioned-call-with-spaces",
     'd = tempfile . mkdtemp ( prefix = "x" )\n', 0),
    ("sanctioned-call-with-a-tmp-prefix",
     'd = tempfile.mkdtemp(prefix="/tmp/x")\n', 0),
    ("shared-call-alone",
     'h = os.path.join(tempfile.gettempdir(), "fixed")\n', 1),
    ("shared-call-with-spaces",
     'h = os.path.join(tempfile . gettempdir ( ), "fixed")\n', 1),
    ("shared-literal-with-trailing-slash",
     'w = os.path.join("/tmp/", "name")\n', 1),
    ("shared-literal-without-trailing-slash",
     "w = os.path.join('/tmp', 'name')\n", 1),
    ("sanctioned-and-shared-on-one-line",
     'd = tempfile.mkdtemp(prefix="ok"); h = os.path.join(tempfile.gettempdir(), "fixed")\n', 1),
    ("sanctioned-inside-a-string-does-not-exempt",
     'HELP = "use tempfile.mkdtemp(prefix=...)"; h = os.path.join(tempfile.gettempdir(), "fixed")\n', 1),
    ("shared-call-across-two-lines",
     'b = os.path.join(tempfile.\n    gettempdir(), "fixed")\n', 1),
    ("shared-call-inside-an-f-string",
     'p = f"{tempfile.gettempdir()}/name"\n', 1),
    ("shared-path-outside-the-sanctioned-call",
     'd = tempfile.mkdtemp(prefix="x") if c else os.path.join(tempfile.gettempdir(), "fixed")\n', 1),
    ("comment-mentions-are-not-code",
     '# os.path.join(tempfile.gettempdir(), "fixed")\n', 0),
    ("a-name-that-merely-starts-with-tmp",
     'w = "/tmpfoo"\n', 0),
]


def run_selftest():
    """(red_cases, total) for the self-test's own cases."""
    red = 0
    for name, src, want in SELFTEST_CASES:
        got = len(findings(src))
        is_red = "yes" if got != want else "no"
        if is_red == "yes":
            red += 1
        print(f"SHARED-TMP-SELFTEST|case={name}|want={want}|got={got}|red={is_red}")
    print(f"SHARED-TMP-SELFTEST-VERDICT|cases={len(SELFTEST_CASES)}|red={red}|"
          f"verdict={'PASS' if red == 0 else 'FAIL'}")
    return red, len(SELFTEST_CASES)


def main():
    if "--self-test" in sys.argv[1:]:
        red, total = run_selftest()
        return 0 if red == 0 else 1

    # --root so the two directions can be shown without editing the tree the
    # stage is about: a copy carrying one shared path must be red, and this
    # tree must be green.  The root is the tree; the scanned set is its
    # tools/ directory.
    root = REPO_ROOT
    argv = sys.argv[1:]
    if "--root" in argv:
        i = argv.index("--root")
        if i + 1 >= len(argv):
            print("check_shared_tmp: --root needs a directory", file=sys.stderr)
            return 2
        root = Path(argv[i + 1]).resolve()
    tools = root / "tools"

    failures = []
    # This file quotes the very patterns it looks for, in its own docstring, so
    # it excludes itself -- and it excludes itself BEFORE counting, because
    # the denominator has to be the number of files actually read.  Counting
    # first and excluding later prints a denominator one larger than the set
    # the verdict was about.
    suites = sorted(p for p in tools.glob("*.py")
                    if not p.name.startswith("check_shared_tmp"))

    if not suites:
        print(
            f"check_shared_tmp: no *.py under {tools} -- the input set is empty, "
            f"so there is nothing here for this check to be true about, and a "
            f"check with nothing to check is not a passing check",
            file=sys.stderr,
        )
        print("SHARED-TMP-VERDICT|scanned=0|failures=0|verdict=FAIL", file=sys.stderr)
        return 1

    scanned = 0
    for path in suites:
        text = path.read_text(encoding="utf-8", errors="replace")
        scanned += 1
        for line_no, matched, stmt in findings(text):
            rel = os.path.relpath(path, root)
            failures.append(f"{rel}:{line_no}: {matched!r} in {stmt!r} -- {REASON}")

    if failures:
        print("check_shared_tmp: FAILED", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        print(
            f"SHARED-TMP-VERDICT|scanned={scanned}|failures={len(failures)}|verdict=FAIL",
            file=sys.stderr,
        )
        return 1

    print(
        f"check_shared_tmp: ok ({scanned} *.py scanned; every artifact a suite "
        f"builds lands in a directory that suite created)"
    )
    print(f"SHARED-TMP-VERDICT|scanned={scanned}|failures=0|verdict=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
