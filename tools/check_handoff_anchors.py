#!/usr/bin/env python3
"""Verify the content anchors in docs/HANDOFF_INFIVERSE.md.

Why this exists: that document used to cite code as `path:line`.  A line
number is a claim about a tree, and it stops being true the first time anyone
edits above the cited line -- the handoff had citations whose target had
already moved hundreds of lines, and two that never pointed at the sentence
they were attached to at all.  The replacement is a content anchor: a
backticked path followed by a backticked quote that must appear verbatim in
that file today.  A quote does not drift; either the string is in the file or
the sentence it supports has changed, and both of those are worth red.

What counts as an anchor here: a backticked span that names an existing file
(relative to the repo root), followed on the SAME line by a connector particle
(里/中/内/的) and then a backticked quote.  The connector is the whole rule,
and it is deliberately narrow.  Without it, two neighbouring paths in one
sentence ("`CMakeLists.txt`、`docs/API.md`") or a section number written after
a path ("`docs/SYNTAX.md` §5 那句「`size`…」") would be read as path/quote
pairs and the checker would report red on its own prose.  A span that is a
path but is not followed that way is simply not an anchor -- this tool never
invents a pairing the text did not make.

A leftover `path:line` span is refused too (HANDOFF-STRAY-LINEREF): the whole
point of the rewrite is that the document no longer has any, and a stray one
would come back to life the first time the file above it grew.

Exit codes: 0 when every anchor's quote is present and no line reference is
left, 1 otherwise, with the first offender named on stderr.

  python3 tools/check_handoff_anchors.py
  python3 tools/check_handoff_anchors.py --self-check

--self-check builds a throwaway fixture (one small file, a few doc strings) and
proves the checker goes green on a present quote and red on a missing one, so a
green main run means something even if the document ever stops having anchors.
"""

import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "HANDOFF_INFIVERSE.md"

BACKTICK = re.compile(r"`([^`\n]+)`")
CONNECTOR = re.compile(r"^\s*(?:里|中|内|的)\s*$")
LINEREF = re.compile(r"^(.+):(\d+)(?:-\d+)?$")


def _text(path):
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def is_path(span, root):
    """Is this backticked span a file that exists, relative to the repo root?"""
    if not span or any(ch.isspace() for ch in span):
        return False
    if "/" not in span and "." not in span:
        return False
    return (root / span).is_file()


def check_doc(text, root):
    """Return (anchors, strays); an anchor is (path, quote, line number)."""
    anchors = []
    strays = []
    for lineno, line in enumerate(text.splitlines(), 1):
        spans = [(m.start(), m.end(), m.group(1)) for m in BACKTICK.finditer(line)]
        for i, (_start, end, span) in enumerate(spans):
            if is_path(span, root):
                if i + 1 < len(spans):
                    gap = line[end:spans[i + 1][0]]
                    if CONNECTOR.match(gap):
                        anchors.append((span, spans[i + 1][2], lineno))
                continue
            m = LINEREF.match(span)
            if m and is_path(m.group(1), root):
                strays.append((m.group(1), span, lineno))
    return anchors, strays


def run(doc_path=DOC, root=ROOT):
    """Check one document.  Returns (anchors, missing, strays) where each
    anchor entry is (path, quote, lineno, found)."""
    anchors, strays = check_doc(_text(doc_path), root)
    checked = []
    for path, quote, lineno in anchors:
        found = quote in _text(root / path)
        checked.append((path, quote, lineno, found))
    missing = [a for a in checked if not a[3]]
    return checked, missing, strays


def self_check():
    """Three controls on the verdict, plus one on the pairing rule itself."""
    checks = 0
    failures = []
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "lib").mkdir()
        (root / "lib" / "a.c").write_text(
            "static int add_one(int x) { return x + 1; }\n", encoding="utf-8"
        )
        (root / "lib" / "b.c").write_text("int other = 0;\n", encoding="utf-8")
        (root / "doc.md").write_text("", encoding="utf-8")

        def control(name, text, want_clean):
            nonlocal checks
            checks += 1
            anchors, strays = check_doc(text, root)
            missing = [
                (path, quote, lineno)
                for path, quote, lineno in anchors
                if quote not in _text(root / path)
            ]
            clean = not missing and not strays
            if clean != want_clean:
                failures.append(
                    "%s: expected %s, got missing=%d strays=%d"
                    % (name, "green" if want_clean else "red", len(missing), len(strays))
                )

        control("present quote is green", "`lib/a.c` 里 `add_one(int x)` 那句", True)
        control("missing quote is red", "`lib/a.c` 里 `not_in_this_file` 那句", False)
        control(
            "leftover line reference is red",
            "`lib/a.c` 里 `add_one(int x)` 那句，见 `lib/b.c:3`",
            False,
        )
        control(
            "a neighbouring path is not a quote",
            "改动在 `lib/a.c`、`lib/b.c` 这两处",
            True,
        )

        # A document with no anchors at all must not be red either: the gate is
        # about the anchors that exist, and the count is printed so a document
        # that quietly lost all of them is visible as anchors=0 rather than as
        # a failure this tool cannot name.
        checks += 1
        checked, missing, strays = run(doc_path=root / "doc.md", root=root)
        if missing or strays or checked:
            failures.append("an anchor-free document must read clean with anchors=0")

    if failures:
        for line in failures:
            print("HANDOFF-ANCHORS-SELFCHECK-FAIL|%s" % line, file=sys.stderr)
        print("HANDOFF-ANCHORS-SELFCHECK|checks=%d|verdict=FAIL" % checks)
        return 1
    print("HANDOFF-ANCHORS-SELFCHECK|checks=%d|verdict=PASS" % checks)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Verify the content anchors in docs/HANDOFF_INFIVERSE.md."
    )
    ap.add_argument(
        "--self-check",
        action="store_true",
        help="prove the checker goes red on a missing quote and green on a present one",
    )
    args = ap.parse_args(argv)

    if args.self_check:
        return self_check()

    if not DOC.is_file():
        print("check_handoff_anchors: no such document: %s" % DOC, file=sys.stderr)
        return 1

    checked, missing, strays = run()
    for path, quote, _lineno, found in checked:
        print(
            "HANDOFF-ANCHOR|path=%s|quote=%s|found=%s"
            % (path, quote, "yes" if found else "no")
        )
    for path, span, lineno in strays:
        print("HANDOFF-STRAY-LINEREF|path=%s|ref=%s|line=%d" % (path, span, lineno))

    found_n = len(checked) - len(missing)
    print(
        "HANDOFF-ANCHORS|anchors=%d|found=%d|missing=%d|verdict=%s"
        % (len(checked), found_n, len(missing), "PASS" if not missing and not strays else "FAIL")
    )

    if strays:
        path, span, lineno = strays[0]
        print(
            "check_handoff_anchors: line reference %r at document line %d still points "
            "with a number. Replace it with a path plus a verbatim quote from %s."
            % (span, lineno, path),
            file=sys.stderr,
        )
        return 1
    if missing:
        path, quote, lineno, _found = missing[0]
        print(
            "check_handoff_anchors: first missing anchor is document line %d: %r is not "
            "in %s. Either the file changed under the sentence, or the quote was typed "
            "from memory -- both mean the sentence is now unbacked."
            % (lineno, quote, path),
            file=sys.stderr,
        )
        return 1

    print(
        "check_handoff_anchors: every anchor's quote is in its file today. "
        "A green here says the quotes exist; it does not say they still describe "
        "what the sentence around them claims."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
