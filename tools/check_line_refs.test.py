#!/usr/bin/env python3
"""check_line_refs.test.py — the report must not contradict itself.

Why a test file (tools/ convention is `<name>.py` + `<name>.test.py`): the
thing this test holds is a SELF-CONSISTENCY property of the report, and a
self-consistency property that is only ever observed holding is a property
nobody has checked.  The defect it guards against is real and was measured:
the grade of a held reference was computed over the span of EVERY holder,
while the report named the FIRST holder, and those two can disagree.  Measured
on 9048cbb9: 27 of 640 held references reported a grade that their own named
holder did not support — a reference reported as `own` whose named holder
carried the span `FAIL_`, where the owner's own name is nowhere in `FAIL_`.

The report has three fields that a reader reads as one piece of evidence:

    grade    own / borrowed / picked up
    holder   the file the evidence came from
    span     the quoted text that did the holding

If the span does not support the grade, the three fields are three separate
things wearing one name.  A reader who checks one of them and believes
another has been misled, and the report is what they would have checked.

The fixture is the situation, in miniature:

  * the line names three files, and the LAST one owns the number — that is
    the checker's own position rule for who owns a number;
  * the owner's line N carries no quoted text at all, so the owner cannot
    hold it;
  * an unrelated file's line N carries a quoted word that holds nothing
    (`noise`), and the FIRST of those holders is therefore the one the old
    report named;
  * a second file's line N carries a quoted TARGET name, and THAT is what
    makes the grade `borrowed`.

So the grade and the reported holder disagree: the old code graded over
every holder's span and named the first one, which is `noise` and supports
`picked up`, not `borrowed`.  The fix names the holder that supports the
grade.  The second cell reverses the order of the two holders on the line
and asks the same question, because a report that only agrees with itself
when the files happen to be named in a given order is not a report.

Nothing here reads the checkout: the fixture is synthetic and the scan is
driven through a fake Tree, so the test cannot pass by inheriting a real
tree's accidents.

Usage:
    python3 tools/check_line_refs.test.py [-v]

Exit codes: 0 every cell as expected, 1 some cell differed, 2 a setup fault.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_line_refs as C  # noqa: E402


DOC_A = "docs/fix/two-holders.md"
NOISE = "docs/fix/noise.md"
OTHER = "tools/gate.sh"
SRC = "CMakeLists.txt"

# The citing line.  `noise.md` first, `tools/gate.sh` second, `CMakeLists.txt`
# last -- so the owner of `:7` is CMakeLists.txt, by the position rule.
DOC_TEXT = (
    "# fixture\n"
    "\n"
    "`docs/fix/noise.md` and `tools/gate.sh` are named, and `CMakeLists.txt` "
    "owns `:7`.\n"
)

# The same line with the two holders in the other order.
DOC_TEXT_REV = (
    "# fixture\n"
    "\n"
    "`tools/gate.sh` and `docs/fix/noise.md` are named, and `CMakeLists.txt` "
    "owns `:7`.\n"
)

# Line 7 of this file carries `noise` -- a quoted word that holds nothing.
NOISE_TEXT = "\n".join(
    [
        "# fixture",
        "",
        "one",
        "two",
        "three",
        "four",
        "the path `docs/fix/noise.md` sits here",
    ]
) + "\n"

# Line 7 of this file carries the quoted TARGET name `tools/gate.sh`, which
# is what makes the grade `borrowed`.
OTHER_TEXT = "\n".join(
    [
        "#!/bin/bash",
        "# fixture",
        "",
        "echo one",
        "echo two",
        "echo three",
        'echo "tools/gate.sh"',
    ]
) + "\n"

# The owner's line 7 carries no quoted text, so it cannot hold anything.
SRC_TEXT = "\n".join(
    [
        "# fixture",
        "",
        "set(X 1)",
        "set(Y 2)",
        "set(Z 3)",
        "set(W 4)",
        "set(V 5)",
        "set(U 6)",
    ]
) + "\n"


class FakeTree:
    """Just enough of Tree for scan(): four files, no git."""

    def __init__(self, files):
        self._files = files

    def tracked(self, rel):
        return rel in self._files

    def text(self, rel):
        return self._files.get(rel, "")

    def lines(self, rel):
        if rel not in self._files:
            return None
        return self._files[rel].splitlines()

    def docs(self):
        return sorted(
            p for p in self._files if p.startswith("docs/") and p.endswith(".md")
        )


def scan_with(files):
    old = C.TREE
    C.TREE = FakeTree(files)
    try:
        return C.scan()
    finally:
        C.TREE = old


def grade_supported_by(row, line, mentions):
    """The grade that the row's OWN reported holder supports."""
    _rel, _lineno, _number, owner, _grade, holder = row
    spans = [holder[1]] if holder[1] else []
    owner_text = None
    for start, end, token in mentions:
        if token == owner:
            owner_text = line[start:end].rstrip(".").strip()
    if owner_text and owner_text in spans:
        return "own"
    if any(
        token in C.TARGETS and line[start:end].rstrip(".").strip() in spans
        for start, end, token in mentions
    ):
        return "borrowed"
    return "picked up"


def check_cell(rows, doc_text, label):
    """Every reported holder must support its reported grade."""
    failures = 0
    lines = doc_text.splitlines()
    for row in rows:
        rel, lineno = row[0], row[1]
        line = lines[lineno - 1]
        mentions = C.file_mentions(line)
        want = grade_supported_by(row, line, mentions)
        if want != row[4]:
            failures += 1
            print(
                f"check_line_refs.test.py: FAIL {label} {rel}:{lineno} :{row[2]} "
                f"({row[3]}) reported grade {row[4]!r}, but its reported holder "
                f"{row[5][0]}:{row[5][2]} carrying {row[5][1]!r} supports "
                f"{want!r}"
            )
    return failures


def check_span_rule() -> int:
    """The code spans on a line, read the way Markdown reads them.

    Three cells, and each one is a shape the naive pairing gets wrong:

      1  a DOUBLE-backtick span is ONE span.  The naive pattern pairs the
         backticks left to right, so it reads `` `:421` `` as two empty
         spans -- and everything after it on the line is paired one
         position off.
      2  a run with no matching closer is literal text, and so is the
         rest of the line.
      3  a line that has such a run still has its OTHER spans: the naive
         pairing destroys them, and the measurement that matters is that
         the real ones come back.
    """
    failures = 0

    # cell 1: one double-backtick span, not two empty ones
    line = "the span is `` `:421` `` and the rule is `` `:NNN` ``"
    spans = C.SPAN.findall(line)
    want = ["`:421`", "`:NNN`"]
    if [s.strip() for s in spans] != want:
        failures += 1
        print(f"check_line_refs.test.py: FAIL span/double -- expected {want!r}, "
              f"got {[s.strip() for s in spans]!r}")

    # cell 2: an unclosed run makes the rest of the line literal
    line = "an unclosed ` run and then `docs/BOARD.md` is named"
    spans = [s.strip() for s in C.SPAN.findall(line)]
    if "docs/BOARD.md" in spans:
        failures += 1
        print("check_line_refs.test.py: FAIL span/unclosed -- the span after "
              "an unclosed backtick run was extracted: "
              f"{spans!r}")

    # cell 3: the real spans on a line with a double-backtick run survive
    line = (
        "The double span `` `:421` `` sits here, and `docs/BOARD.md:145` is "
        "named, and `git grep` is quoted."
    )
    got = set(C.anchors(line))
    for want_span in ("docs/BOARD.md:145", "git grep"):
        if want_span not in got:
            failures += 1
            print(f"check_line_refs.test.py: FAIL span/rescue -- the span "
                  f"{want_span!r} was lost; anchors are {sorted(got)!r}")
    # and the prose between the backticks is NOT a span
    for forged in ("span", "the double span", "is named, and"):
        if forged in got:
            failures += 1
            print(f"check_line_refs.test.py: FAIL span/rescue -- the prose "
                  f"{forged!r} was read as a span")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    failures = 0

    for label, doc_text in (("in-order", DOC_TEXT), ("reversed", DOC_TEXT_REV)):
        files = {
            DOC_A: doc_text,
            NOISE: NOISE_TEXT,
            OTHER: OTHER_TEXT,
            SRC: SRC_TEXT,
        }
        r = scan_with(files)
        rows = r["held_rows"]
        if not rows:
            print(f"check_line_refs.test.py: SETUP -- the {label} fixture "
                  "produced no held reference; the fixture, not the checker, "
                  "is broken")
            return 2
        if len(rows) != 1:
            print(f"check_line_refs.test.py: SETUP -- the {label} fixture "
                  f"produced {len(rows)} held references, expected 1")
            return 2
        failures += check_cell(rows, doc_text, label)
        row = rows[0]
        if args.verbose:
            print(f"check_line_refs.test.py: {label} -> grade={row[4]} "
                  f"holder={row[5][0]}:{row[5][2]} span={row[5][1]!r}")
        if row[4] != "borrowed":
            failures += 1
            print(f"check_line_refs.test.py: FAIL {label} -- expected the "
                  f"grade 'borrowed' (a TARGET name holds it), got {row[4]!r}")
        if row[5][0] == NOISE:
            failures += 1
            print(f"check_line_refs.test.py: FAIL {label} -- the report named "
                  f"the holder {NOISE!r}, whose span holds nothing")

    failures += check_span_rule()

    if failures:
        print(f"check_line_refs.test.py: {failures} cell(s) differed")
        return 1
    print("check_line_refs.test.py: OK -- every reported holder supports its "
          "reported grade, in both orders, and the code spans are read the "
          "way Markdown reads them")
    return 0


if __name__ == "__main__":
    sys.exit(main())
