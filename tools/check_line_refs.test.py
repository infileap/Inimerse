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

# --- cell: `own` must be held by the OWNER -------------------------------
#
# A second document mentions `CMakeLists.txt` on ITS line 7.  Under the old
# grade that was enough to report the citation as `own`: the owner's own name
# was the holding span, and the report did not say which file the span came
# from.  Measured on fca60a25: 48 of the 57 `own` references were held by a
# file that is not the owner at all, and 42 of those had no holder in the
# owner file whatsoever -- so nothing about the cited line was checked, and
# the citation could be forged by writing the owner's name on line N of any
# document.
OWNER_MENTION = "docs/fix/mentions-owner.md"
OWNER_MENTION_TEXT = "\n".join(
    [
        "# fixture",
        "",
        "one",
        "two",
        "three",
        "four",
        "the file `CMakeLists.txt` is named here",
    ]
) + "\n"

# The citing line: the owner of `:7` is CMakeLists.txt, by the position rule
# (the last name ending at or before the number), and the other file's line 7
# mentions that owner by name.
OWN_DOC = "docs/fix/own-elsewhere.md"
OWN_DOC_TEXT = (
    "# fixture\n"
    "\n"
    "`docs/fix/mentions-owner.md` and `CMakeLists.txt` at `:7`.\n"
)

# --- cell: a code owner and a shared symbol ------------------------------
#
# The citing line names a TARGET and a C file, and the cited line of that C
# file says the very symbol the citing line says.  There are no backticks on
# the cited line, so the span mechanism has nothing to anchor to there, and
# the old code graded the citation as free text even though the cited line
# named the symbol the citing line was about.
#
# The TARGET has to be on the line too: a line that names no TARGET is not
# scanned at all, so a fixture without one would test nothing.
CODE_OWNER = "src/fix/vm.c"
CODE_DOC = "docs/fix/code-owner.md"
CODE_DOC_TEXT = (
    "# fixture\n"
    "\n"
    "`tools/gate.sh` and `src/fix/vm.c` both name `builtin_lookup` at `:4`.\n"
)
CODE_OWNER_TEXT = "\n".join(
    [
        "/* fixture */",
        "#include <stddef.h>",
        "",
        "int builtin_lookup(VM *vm, const char *name) {",
        "    return slot_of(vm, name);",
        "}",
    ]
) + "\n"

# --- cell: a coincidence is a hold by a file that is not the owner --------
#
# The citing line names TWO files, and the number belongs to the second one
# by the position rule.  The OWNER's line 7 carries no quoted text, so it
# cannot hold the reference -- but the FIRST file's line 7 carries the
# owner's own name, and that coincidence holds the reference.
#
# This is the shape 236 of the 629 held references on 88b898fd have (37%),
# and it is the shape the coincidence pin exists to refuse.  It is easy to
# write on purpose: put the owner's name on line N of any document.
COINC_DOC = "docs/fix/coincidence.md"
COINC_DOC_TEXT = (
    "# fixture\n"
    "\n"
    "`docs/fix/mentions-owner.md` is named, and `CMakeLists.txt` at `:7`.\n"
)

# --- cell: the cited FILE cannot be told apart ----------------------------
#
# The citing line names two files and the number belongs to the second one by
# the position rule -- but BOTH files carry the same quoted text on line 7,
# so the content does not choose between them.  The owner is still named the
# same way, and the reference is still held: what is missing is the ability
# to say WHICH file the citation is about.
#
# This is the shape 197 references on 9095b2ce have, and 162 of them are in
# docs/BOARD.md alone, whose table rows name thirty files between them.
#
# The count deliberately does not name an owner.  A count of "the holder is
# not the owner" would fall to zero the moment the owner rule were rewritten
# to be "the file that holds it" -- and a pin that cannot fail measures the
# rule rather than the tree.
AMBIG_DOC = "docs/fix/ambiguous.md"
AMBIG_DOC_TEXT = (
    "# fixture\n"
    "\n"
    "`tools/gate.sh` and `docs/fix/noise.md` are named, and `CMakeLists.txt` "
    "at `:7`.\n"
)
# Each of the two named files carries the OTHER one's name on line 7, so the
# quoted text of the citing line is found on line 7 of both of them.
AMBIG_OTHER_TEXT = "\n".join(
    [
        "#!/bin/bash",
        "# fixture",
        "",
        "echo one",
        "echo two",
        "echo three",
        'echo "docs/fix/noise.md"',
    ]
) + "\n"
AMBIG_NOISE_TEXT = "\n".join(
    [
        "# fixture",
        "",
        "one",
        "two",
        "three",
        "four",
        "the path `tools/gate.sh` sits here",
    ]
) + "\n"
# The same file with nothing quoted on line 7 at all, so it cannot hold.
AMBIG_NOISE_PLAIN_TEXT = "\n".join(
    [
        "# fixture",
        "",
        "one",
        "two",
        "three",
        "four",
        "nothing is quoted on this line",
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


def check_own_needs_the_owner() -> int:
    """`own` must be held by the file the number points into.

    The old grade called a reference `own` when the holding span was the
    owner's own NAME -- but it never said which file the span came from.
    Any document that mentions the owner on the same line NUMBER held it,
    and 42 of the 48 measured cases had no holder in the owner file at all,
    so nothing about the cited line was being checked.  A citation of a
    source file could be forged by writing the owner's name on line N of any
    document.

    The fixture is exactly that: the owner's line 7 carries no quoted text,
    so the owner cannot hold the citation, and another document's line 7
    says the owner's name.
    """
    failures = 0
    files = {
        OWN_DOC: OWN_DOC_TEXT,
        OWNER_MENTION: OWNER_MENTION_TEXT,
        SRC: SRC_TEXT,
    }
    r = scan_with(files)
    rows = r["held_rows"]
    if len(rows) != 1:
        print(f"check_line_refs.test.py: SETUP -- the own-elsewhere fixture "
              f"produced {len(rows)} held references, expected 1")
        return 2
    row = rows[0]
    if row[4] == "own":
        failures += 1
        print(f"check_line_refs.test.py: FAIL own/elsewhere -- the citation "
              f"of {row[3]} line {row[2]} was graded 'own' on the strength of "
              f"{row[5][0]}:{row[5][2]}, which is not the owner")
    if row[5][0] != row[3]:
        # The holder is still the other file -- that is where the evidence
        # is -- but the GRADE must not claim the owner held it.
        if row[4] == "own":
            failures += 1
            print("check_line_refs.test.py: FAIL own/elsewhere -- the "
                  "reported holder is not the owner but the grade is 'own'")
    return failures


def check_symbol_grade() -> int:
    """A code owner can be held by a shared symbol.

    The span mechanism reads backtick-quoted text, so it cannot reach a line
    of code: every citation of a C file was graded as free text even when
    the cited line named the very symbol the citing line was about.  The
    `symbol` grade says the two lines talk about the same symbol.
    """
    failures = 0
    files = {
        CODE_DOC: CODE_DOC_TEXT,
        OTHER: OTHER_TEXT,
        CODE_OWNER: CODE_OWNER_TEXT,
    }
    r = scan_with(files)
    rows = r["held_rows"]
    if len(rows) != 1:
        print(f"check_line_refs.test.py: SETUP -- the code-owner fixture "
              f"produced {len(rows)} held references, expected 1")
        return 2
    row = rows[0]
    if row[4] != "symbol":
        failures += 1
        print(f"check_line_refs.test.py: FAIL symbol -- expected the grade "
              f"'symbol' (the cited line names the symbol the citing line "
              f"names), got {row[4]!r}")
    if row[5][0] != row[3]:
        failures += 1
        print(f"check_line_refs.test.py: FAIL symbol -- the reported holder "
              f"{row[5][0]} is not the owner {row[3]}")
    return failures


def check_coincidence_is_counted() -> int:
    """A hold by a file that is not the owner is a coincidence, and counted.

    The reference is held -- there IS a quoted string on line 7 of a file the
    citing line names.  But the file that holds it is not the file the number
    points into, so the line the citation means was never checked.  The
    report said "held" and nothing else, and a number in a report is not a
    rule.

    So the coincidence is counted, and the count is what the pin reads.
    """
    failures = 0
    files = {
        COINC_DOC: COINC_DOC_TEXT,
        OWNER_MENTION: OWNER_MENTION_TEXT,
        SRC: SRC_TEXT,
    }
    r = scan_with(files)
    rows = r["held_rows"]
    if len(rows) != 1:
        print(f"check_line_refs.test.py: SETUP -- the coincidence fixture "
              f"produced {len(rows)} held references, expected 1")
        return 2
    coincident = r.get("coincident")
    if coincident is None:
        failures += 1
        print("check_line_refs.test.py: FAIL coincidence -- the report does "
              "not count coincidental holds at all, so a hold by a file that "
              "is not the owner cannot be told from a real anchor")
        return failures
    if len(coincident) != 1:
        failures += 1
        print(f"check_line_refs.test.py: FAIL coincidence -- expected 1 "
              f"coincidental hold, got {len(coincident)}: {coincident!r}")
    elif coincident[0][0] != COINC_DOC:
        failures += 1
        print(f"check_line_refs.test.py: FAIL coincidence -- the coincidence "
              f"was reported against {coincident[0][0]!r}, expected "
              f"{COINC_DOC!r}")
    # The evidence, stated independently of the counter: the holder is not
    # the owner, so the cited line was not the line that was checked.
    row = rows[0]
    if row[5][0] == row[3]:
        failures += 1
        print(f"check_line_refs.test.py: FAIL coincidence -- the reference is "
              f"held by its owner {row[3]!r}, so it is not a coincidence and "
              f"must not be counted as one")
    # And the same reference must NOT be counted when the owner holds it.
    files[OWNER_MENTION] = SRC_TEXT
    r2 = scan_with(files)
    if r2.get("coincident"):
        failures += 1
        print(f"check_line_refs.test.py: FAIL coincidence -- a reference the "
              f"owner holds was counted as a coincidence: "
              f"{r2['coincident']!r}")
    return failures


def check_ambiguity_is_counted() -> int:
    """A reference two files can hold is ambiguous, and counted.

    The line names two files and both carry the quoted text on line 7, so
    the content cannot say which one the number is about.  The reference is
    still held -- and the report still names one owner -- but the reader has
    no way to know that a second file would have answered the same way.

    The count is what the pin reads, and it must not depend on the owner
    rule: a count that compared the holder with the owner would go to zero
    the moment the owner rule were rewritten to be "the file that holds it".
    """
    failures = 0
    files = {
        AMBIG_DOC: AMBIG_DOC_TEXT,
        OTHER: AMBIG_OTHER_TEXT,
        NOISE: AMBIG_NOISE_TEXT,
    }
    r = scan_with(files)
    ambiguous = r.get("ambiguous")
    if ambiguous is None:
        failures += 1
        print("check_line_refs.test.py: FAIL ambiguity -- the report does "
              "not count references that two of the named files can hold, so "
              "an undecidable citation cannot be told from a decided one")
        return failures
    if len(ambiguous) != 1:
        failures += 1
        print(f"check_line_refs.test.py: FAIL ambiguity -- expected 1 "
              f"ambiguous reference, got {len(ambiguous)}: {ambiguous!r}")
        return failures
    rel, lineno, number, holders = ambiguous[0]
    if rel != AMBIG_DOC:
        failures += 1
        print(f"check_line_refs.test.py: FAIL ambiguity -- the ambiguity was "
              f"reported against {rel!r}, expected {AMBIG_DOC!r}")
    if sorted(holders) != sorted([OTHER, NOISE]):
        failures += 1
        print(f"check_line_refs.test.py: FAIL ambiguity -- the ambiguity names "
              f"the holders {holders!r}, expected both {OTHER!r} and {NOISE!r}: "
              f"the two files that carry the text on line {number} are the "
              f"ones the citation cannot be told apart from")
    # And it must NOT be counted when only one file carries the text.
    files[NOISE] = AMBIG_NOISE_PLAIN_TEXT
    r2 = scan_with(files)
    if r2.get("ambiguous"):
        failures += 1
        print(f"check_line_refs.test.py: FAIL ambiguity -- a reference only "
              f"one of the named files can hold was counted as ambiguous: "
              f"{r2['ambiguous']!r}")
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
    failures += check_own_needs_the_owner()
    failures += check_symbol_grade()
    failures += check_coincidence_is_counted()
    failures += check_ambiguity_is_counted()

    if failures:
        print(f"check_line_refs.test.py: {failures} cell(s) differed")
        return 1
    print("check_line_refs.test.py: OK -- every reported holder supports its "
          "reported grade, in both orders; the code spans are read the way "
          "Markdown reads them; `own` is held by the owner; a code owner "
          "can be held by a symbol; a hold by a file that is not the owner is "
          "counted as a coincidence; a reference two of the named files can "
          "hold is counted as ambiguous")
    return 0


if __name__ == "__main__":
    sys.exit(main())
