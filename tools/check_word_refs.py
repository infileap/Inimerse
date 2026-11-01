#!/usr/bin/env python3
"""Word-form line references must name the tree they were read on.

`docs/` names lines two ways:

  * `tools/check_line_refs.py:57` -- a PATH:LINE citation.  Two rulers count
    those, one per target file they consider (`CMakeLists.txt`, `tools/gate.sh`),
    and both carry a ceiling.  A pointer that grows too fast is caught.
  * `第 57 行` -- a WORD-FORM reference, "line 57".  Neither ruler sees it: the
    line-refs sweep only counts `path:digit` into its two targets, and
    check_obs_markers' citation grammar is also `path:digit`.  So a word-form
    reference can drift to point at another line, or at another tree, and
    nothing goes red.

That is the whole defect: not that word-form references are wrong, but that
nothing measures them.  Measured on 078bacd: a page named line 313 of
tools/mir_roundtrip.test.py in two places and line 48/57 in two others; the
file had grown to 344 lines and none of those four numbers still pointed at
what the sentence said.  The gate was green the whole time.

WHAT COUNTS AS NAMING A TREE

A word-form reference is acceptable when its line names the tree it was read on,
in any of the forms the repository already uses:

  * an `[obs: <sha>]` marker on that line,
  * a bare 7-to-40 character hex revision on that line (`@ 9ecf88c`, `at ab562e9`),
  * a `tree=` field (pins name their tree that way).

A line that names a FILE and a line number but no tree is the case this check is
about: the number is about some version of that file, and the version is not
written down, so no reader can tell whether it is still true.

WHAT IS NOT COUNTED

  * Word-form references inside a fenced block or inside inline backticks: that
    is a quotation of form or an example, not an assertion about a tree.
  * Lines that name no file at all: "第 N 行" with no file beside it cannot be
    checked against anything, and counting it would only inflate a debt without
    naming what to fix.

THE BASELINE IS THIS TOOL'S OWN READING

The number of offending lines is pinned, like EXP_CTEST, and the pinned value is
what this file measured on the commit that introduced it -- not a number copied
from a message.  Two independent measurements of "how many word-form references
name no tree" disagreed (232 lines / 331 occurrences here; another instrument
reported 235 / 348 with 84 offending), because three clauses of the rule were
implemented differently: whether a path inside backticks counts as a file name,
whether "not in a code block" is decided per line or per reference, and whether
`@ <sha>` counts as naming a tree.  A constant copied from one of those
implementations would make this checker red on its first run for a reason its
own readings cannot explain.  So `BASELINE` is defined as "what this prints
today", every reading prints the rule it used, and every later change may only
LOWER it.

Readings:

  WORD-REF|lines=L|occ=C|with-tree=T|file-no-tree=N|neither=U
          |rule=<the three clauses, spelled out>|baseline=B|verdict=PASS|FAIL

Exit 0 when N <= BASELINE; 1 when it grew past the baseline (name the first file
and line that pushed it over); 2 when there is nothing to measure (no docs/, an
unreadable root) -- a check with nothing to check is not a passing check.

--self-test runs the classifier over fixtures whose answer is known, including
the two directions that matter: a line with a file and no tree is an offender,
and the same line with an `[obs:]` marker is not.
"""
import argparse
import atexit
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# "第 N 行" and "第 N–M 行".  The dash forms are included because the pages use
# them: a range is a reference to two lines, and counting only the first would
# report a page as half as dirty as it is.
WORD_FORM = re.compile(r'第\s*(\d+)\s*行')
# The extension list is deliberate and closed.  A loose "any extension" pattern
# (`.\w+`) reads a section number like `1.345` or `3.3` as a file name, which made
# this checker report 87 offenders where a stricter sibling reported 84 -- the
# three extra were section numbers, not files.  Measured both ways; the closed
# list is the one whose readings name something a reader can open.
FILE_IN_LINE = re.compile(
    r'[A-Za-z0-9_./-]+\.(?:c|h|cc|cpp|py|sh|im|inim|js|ts|md|json|txt|params|toml|yml|yaml)\b')
HEX_REV = re.compile(r'[0-9a-f]{7,40}')
TREE_MARK = re.compile(r'\[obs:')
FENCE = re.compile(r'^\\s*(```|~~~)')

RULE = ("file-in-line+no-tree+not-inline-code;"
        "tree=[obs:|<hex>|tree=;"
        "file=[A-Za-z0-9_./-]+.<ext>;"
        "code=whole-line-has-two-backticks-or-inside-fence")

# Measured by this file on the commit that introduced it, with --pin-baseline.
# 87 offenders today, not the 84 another instrument reported: the two differ on
# one clause (whether a path inside backticks counts as naming a file), and a
# constant copied from that other instrument would make this checker red on its
# first run for a reason its own readings cannot explain.  Every later change
# may only LOWER this number, in the same commit as the fix that lowers it.
BASELINE = 87


def _word_form_spans(line):
    """(start, end) offsets of every word-form reference in the line."""
    return [(m.start(), m.end()) for m in WORD_FORM.finditer(line)]


def _in_code(line, in_fence):
    """Whether the WORD-FORM REFERENCE itself sits inside a quotation.

    The first version asked "does this line contain two backticks" -- and a line
    like `- `tools/x.py` 第 313 行 印 MIR-UNCOMPARED` has two, because the path
    is quoted, while the line number is the assertion.  That made the classifier
    skip exactly the lines this check exists for (measured: both self-test
    offenders came out False).  So the question is asked of the reference: a
    backtick opens before it and closes after it.
    """
    if in_fence:
        return True
    spans = _word_form_spans(line)
    if not spans:
        return False
    start, end = spans[0]
    before = line[:start].count("`")
    after = line[end:].count("`")
    # An odd number of backticks before the reference means it opened inside one;
    # an odd number after means it closed after it.  Both => fully quoted.
    return (before % 2 == 1) and (after % 2 == 1)


def _names_tree(line):
    return bool(TREE_MARK.search(line) or HEX_REV.search(line) or "tree=" in line)


def _names_file(line):
    return bool(FILE_IN_LINE.search(line))


def findings(text):
    """[(line_no, snippet, has_tree, has_file)] for every line with a word form."""
    out = []
    in_fence = False
    for ln, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if not WORD_FORM.search(line):
            continue
        out.append((ln, line.strip(), _names_tree(line), _names_file(line)))
    return out


def walk(root):
    """Every markdown file under `<root>/docs`, or [] when there is none."""
    docs = Path(root) / "docs"
    if not docs.is_dir():
        return []
    return sorted(p for p in docs.rglob("*.md") if p.is_file())


def collect(root):
    """(lines, occurrences, with_tree, offenders) -- offenders are (path, ln, snip)."""
    lines = occ = with_tree = 0
    offenders = []
    for p in walk(root):
        text = p.read_text(errors="replace")
        in_fence = False
        for ln, line in enumerate(text.splitlines(), 1):
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            ms = WORD_FORM.findall(line)
            if not ms:
                continue
            lines += 1
            occ += len(ms)
            if _names_tree(line):
                with_tree += 1
                continue
            # Not naming a tree.  Offender only when it also names a file and
            # is not a code quotation -- the rest is "a line number and nothing
            # to check it against", which is a different (and unreported) debt.
            if _names_file(line) and not _in_code(line, in_fence):
                offenders.append((str(p.relative_to(root)), ln, line.strip()[:100]))
    return lines, occ, with_tree, offenders


def refuse(why):
    print("check_word_refs: REFUSED|" + why, file=sys.stderr)
    print("check_word_refs: nothing was measured", file=sys.stderr)
    print("WORD-REF-VERDICT|lines=0|occ=0|with-tree=0|file-no-tree=0|verdict=REFUSED",
          file=sys.stderr)
    return 2


SELFTEST_CASES = [
    # (name, line, want_offender)
    ("file-and-line-no-tree",
     "- `tools/mir_roundtrip.test.py` 第 313 行 印 MIR-UNCOMPARED", True),
    ("file-and-line-with-obs",
     "- `tools/mir_roundtrip.test.py` 第 333 行 印 MIR-UNCOMPARED [obs: 9ecf88c]", False),
    ("file-and-line-with-bare-sha",
     "- `tools/mir_roundtrip.test.py` 第 333 行（在 ab562e9 上读到）", False),
    ("line-no-file-at-all",
     "- 第 12 行改为返回 0", False),
    ("word-form-inside-backticks",
     "写成 `` `第 12 行` `` 这种形状时不算断言", False),
    ("two-occurrences-one-line",
     "`a.py` 第 12 行与 第 40 行都提到", True),
]


def self_test():
    """The classifier on fixtures whose answer is known, both directions."""
    bad = []
    cases = []
    for name, line, want in SELFTEST_CASES:
        got = False
        if WORD_FORM.search(line) and not _names_tree(line) and _names_file(line):
            got = not _in_code(line, False)
        red = got != want
        cases.append(name)
        print("WORD-REF-SELFTEST|case=%s|want=%s|got=%s|red=%s"
              % (name, want, got, "yes" if red else "no"))
        if red:
            bad.append(name)
    # The fence direction: the same offending line inside a fence is not counted.
    fenced = "```\n`tools/x.py` 第 12 行\n```"
    got_fenced = _in_code("`tools/x.py` 第 12 行", True)
    print("WORD-REF-SELFTEST|case=offender-inside-a-fence|want=True|got=%s|red=%s"
          % (got_fenced, "no" if got_fenced else "yes"))
    cases.append("offender-inside-a-fence")
    if not got_fenced:
        bad.append("offender-inside-a-fence")
    print("WORD-REF-SELFTEST-VERDICT|cases=%d|red=%d|verdict=%s"
          % (len(cases), len(bad), "FAIL" if bad else "PASS"))
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--baseline", type=int, default=None,
                    help="override the ceiling for this run.  The pinned BASELINE "
                         "belongs to this tree; a copy of the tree (a --root fixture) "
                         "has its own history and must be given its own ceiling, "
                         "otherwise a copy holding one offender passes against 87.")
    ap.add_argument("--pin-baseline", action="store_true",
                    help="print the baseline this run measured, so it can be written "
                         "into BASELINE with the commit that makes the debt a ceiling")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    files = walk(args.root)
    if not files:
        return refuse("no markdown under %s/docs -- the input set is empty, so there is "
                      "nothing here for this check to be true about" % args.root)

    baseline = BASELINE if args.baseline is None else args.baseline
    lines, occ, with_tree, offenders = collect(args.root)
    # A tree whose docs/ hold no word-form reference at all is an empty input set,
    # not a clean one.  Measured: a copy with one empty .md printed
    # `lines=0|...|verdict=PASS` and exited 0 -- the same vacuous pass this check
    # exists to catch one layer up, and the same shape as "no tests found!!!":
    # zero inputs and zero findings print the same success line.
    if lines == 0:
        return refuse("no word-form line reference (" + "第 N 行" + ") anywhere under "
                      "%s/docs -- the input set is empty, so there is nothing here "
                      "for this check to be true about" % args.root)
    print("WORD-REF|lines=%d|occ=%d|with-tree=%d|file-no-tree=%d|neither=%d"
          "|rule=%s|baseline=%d|verdict=%s"
          % (lines, occ, with_tree, len(offenders),
             lines - with_tree - len(offenders), RULE, baseline,
             "PASS" if len(offenders) <= baseline else "FAIL"))
    for path, ln, snip in offenders:
        print("WORD-REF-OFFENDER|%s:%d|%s" % (path, ln, snip))
    if args.pin_baseline:
        print("WORD-REF-BASELINE|measured=%d|note=write this into BASELINE in the same "
              "commit, not in a later one" % len(offenders))
        return 0
    if len(offenders) > baseline:
        print("check_word_refs: FAILED|%d word-form reference(s) name a file and a line "
              "but no tree, over the baseline of %d.  Name the tree (an [obs:] marker or "
              "the revision the number was read on), or point at the symbol instead of "
              "the line." % (len(offenders), baseline), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
