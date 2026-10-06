#!/usr/bin/env python3
"""Why this exists (docs/AUDIT.md 1.72, star 11).

`docs/` cites line numbers in two files whose numbers nothing checks:

    CMakeLists.txt   -- moved eight times in one merge series, by eight
                        different offsets (+43 +34 +15 +34 +23 +14 +11 +11)
                        and again by +67.  There is no uniform offset, so
                        "add N" is not a repair; only re-taking by content is.
    tools/gate.sh    -- same shape, and the file itself grew by 35 lines in
                        one merge, invalidating 13 references to it at once.

`tools/gate.sh`'s own numbers are held by the push-stage table and by
`--required-for`.  `CMakeLists.txt`'s numbers were held by nothing.

This checker does not *fix* a number.  It asks whether a number has anything
holding it: a **content anchor** -- some quoted string on the same line that
actually sits at the cited line.  A number with no anchor is reported, not
blocked, because a missing anchor and a wrong number are not distinguishable
without one.

Input set (docs/AUDIT.md 1.72 star 11): a line in docs/**/*.md that names a
target and then carries an inline number.  The writings are counted
separately on every run, and the gate stage asserts that each of them is
still present:

    CMakeLists.txt:426-436          the prefix form, and the range form
    [../CMakeLists.txt](../CMakeLists.txt) ... `:421`
                                    the markdown-link form -- a prefix-only
                                    rule never sees this one, and
                                    docs/AUDIT.md:1569 is exactly this shape
    `CMakeLists.txt` ... `:935`     the name, then a bare number later on the
                                    same line

The denominators are measured and printed on every run, because a sweep that
picks its own input by shape reports "nothing missed" while missing the shape
it was built to find.  Asserting that each writing is still present is what
stops this from quietly degenerating into a prefix scan.

The test: for a number `:N`, the quoted text on the line is looked for at line
N in EVERY file the line names.  That is what decides who owns the number --
not proximity, and not the name nearest the number.  Then:

    held              the target is among the files whose line N carries the
                      quoted text.  The number has something holding it.
    held elsewhere    another file named on the same line carries it, so the
                      number is fine and the line is talking about two files.
    no anchor         nothing named on the line carries it.

The gate stage asserts DENOMINATORS, not anchor rates:

  - the input set is not empty and every writing above is present at least
    once;
  - the explicit-form reference count has a pin (EXP_LINE_REFS), so it cannot
    shrink while nobody is looking;
  - the unanchored count has a ceiling (EXP_LINE_REFS_UNANCHORED_MAX), so it
    cannot grow while nobody is looking.  This is the tooth: 508 is a known
    quantity today, and 527 tomorrow is an unannounced regression.

What those pins do NOT say -- written here rather than in a letter, so that
the claim and the artifact travel together:

  - a pin proves no shrinkage and no growth.  It proves no anchor is right.
  - the pin measures a set that contains the paragraph describing the pin.
    docs/BOARD.md's line-refs row names CMakeLists.txt and tools/gate.sh and
    carries about a dozen inline numbers, so it is itself in the input set:
    writing that row moved the counts it records.  EXP_LINE_REFS was measured
    at 686 before the row existed, and the row's own tree already read 690 --
    the pin shipped stale by its own subject matter.  This is a fixed point,
    not a slip: the number and its description move together or not at all,
    so a change to that row and a re-measurement are one action.
  - "has an anchor" is not "the number is right".  The rule is "some quoted
    text on the citing line sits at line N of a file that line names", so a
    match proves the number is HELD by content, not that it points at the
    right object.  A number that is still there and now points at something
    else -- the :673 shape agent4 found this round -- is not caught here.
  - splitting a block longer than MAX_PARA lines into single lines is a
    heuristic.  docs/BOARD.md's 93-line table with no blank line in it is why
    the heuristic exists, and is also its boundary.
  - the anchor is *found*, not *declared*: a non-match is not evidence that a
    number is wrong, only that nothing on that line can test it.
  - the input set is a lower bound.  A reference whose file name sits on an
    earlier line, or a bare number whose line names no file, is out of scope
    even when it is a true reference.
  - document-to-document citations (docs/AUDIT.md:<N>, and tools/ citing
    docs/) are a third kind and are NOT in scope here.
  - the reporting unit is the PARAGRAPH, not the reference, because the same
    stale value is usually cited more than once a few lines apart -- three
    such pairs are already measured (docs/AUDIT.md:713 fixed :383 while :453
    kept it; :3330 dropped an ordinal while :3326 kept it; :1659 fixed :935
    while :1657, two lines above, kept :915).
  - two rules that look reasonable were tried on this repository and rejected:
    "the file named nearest before the number owns it" (240 references
    unresolved) and "the quoted text nearest the number anchors it" (held fell
    to 50).  Both die the same way -- a documentation line interleaves several
    file names, several numbers and several quoted spans, and neither
    proximity rule can tell which belongs to which.

Negative control -- redo it, and note that the first version was worthless:

    sed -i '426s|.*|# NEGATIVE CONTROL line|' CMakeLists.txt
    python3 tools/check_line_refs.py
        held 497 -> 491, unanchored 508 -> 514 -- six references move, and the
        stage goes red on the ceiling, not on a rate
    git checkout -- CMakeLists.txt

The first version of this control replaced the line with

    # NEGATIVE CONTROL: this line used to be if(WIN32)

and moved NOTHING, even though `git diff` showed the edit had landed: the
replacement text still contained the string being tested, so the mutation
landed in the file and not in the thing under test.  Confirming that an edit
landed is necessary, not sufficient -- confirm it landed in the measured
object.

Usage:
    python3 tools/check_line_refs.py             assert the pins; exit 1 if a
                                                 pin moved
    python3 tools/check_line_refs.py --report    also list every unit with
                                                 unanchored references
    python3 tools/check_line_refs.py --strict    exit 1 if ANY reference has
                                                 no anchor (a diagnostic, not
                                                 the gate's mode: today that
                                                 is every one of them)
"""
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Files whose line numbers docs/ cites.  Each is here for a stated reason, not
# because of its name.
TARGETS = {
    "CMakeLists.txt": "moved eight times by eight offsets; cited throughout docs/",
    "tools/gate.sh": "grew by 35 lines in one merge, invalidating 13 citations",
}

# Pins in the sense of tools/gate.sh's EXP_CTEST: neither may move without
# someone saying so in a commit message.
EXP_LINE_REFS = int(os.environ.get("EXP_LINE_REFS", "694"))
EXP_LINE_REFS_UNANCHORED_MAX = int(
    os.environ.get("EXP_LINE_REFS_UNANCHORED_MAX", "508")
)

# Every writing that must still be in the input set.  A prefix scan satisfies
# the first two and none of the others, which is exactly the failure this list
# exists to catch.
WRITINGS = ("prefix", "prefix-range", "link+bare", "bare")

PATHISH = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]{1,6}")
NUMBER = re.compile(r":(\d+)(?:-(\d+))?")
SPAN = re.compile(r"`([^`\n]+)`")
PURE_NUMBER = re.compile(r"^:?\d+(?:-\d+)?$")
LINK = re.compile(r"\[[^\]]*\]\([^)]*\)")
MAX_PARA = 20

_CACHE: dict[str, list[str] | None] = {}


def lines_of(rel: str) -> list[str] | None:
    if rel not in _CACHE:
        path = REPO_ROOT / rel
        _CACHE[rel] = (
            path.read_text(encoding="utf-8", errors="replace").splitlines()
            if path.is_file()
            else None
        )
    return _CACHE[rel]


def docs_markdown() -> list[str]:
    out = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "ls-files", "docs/"],
        capture_output=True, text=True, check=True,
    ).stdout
    return [p for p in out.splitlines() if p.endswith(".md")]


def file_mentions(line: str) -> list[tuple[int, int, str]]:
    """(start, end, path) for every existing repo file named on this line."""
    found = []
    for m in PATHISH.finditer(line):
        token = m.group(0).rstrip(".")
        if token in TARGETS or (REPO_ROOT / token).is_file():
            found.append((m.start(), m.end(), token))
    return found


def anchors(line: str) -> list[str]:
    """Quoted strings on the line that could anchor a number."""
    out = []
    for span in SPAN.findall(line):
        span = span.strip()
        if not span or len(span) <= 2 or PURE_NUMBER.match(span):
            continue
        if any(t + ":" in span for t in TARGETS):
            continue
        out.append(span)
    return out


def classify(line: str, mentions: list[tuple[int, int, str]], m: re.Match) -> str:
    """Which writing this reference uses, judged by where a TARGET name sits.

    A target glued to the number is the prefix form; a target inside a
    markdown link with the number later on the line is the link form; a target
    named anywhere else on the line with the number later is the bare form.
    Which of them OWNS the number is not decided here -- the content anchor
    decides that, because the nearest name is the rule that was tried and
    rejected.
    """
    for _start, end, token in mentions:
        if token in TARGETS and line[end:m.start()] == "":
            return "prefix-range" if m.group(2) else "prefix"
    links = [(x.start(), x.end()) for x in LINK.finditer(line)]
    for start, _end, token in mentions:
        if token in TARGETS and any(a <= start < b for a, b in links):
            return "link+bare"
    return "bare"


def paragraphs(text: str) -> list[tuple[int, int, list[str]]]:
    """(first line, last line, lines) per run of non-blank lines, 1-based.

    A run longer than MAX_PARA lines is yielded line by line: docs/BOARD.md
    holds a 93-line table with no blank line in it, and reading that as one
    paragraph carries 406 references in a single unit.
    """
    out, buf, start = [], [], None
    for n, line in enumerate(text.splitlines(), 1):
        if line.strip():
            if start is None:
                start = n
            buf.append(line)
        elif buf:
            out.append((start, n - 1, buf))
            buf, start = [], None
    if buf:
        out.append((start, start + len(buf) - 1, buf))
    flat = []
    for first, last, lines in out:
        if len(lines) <= MAX_PARA:
            flat.append((first, last, lines))
        else:
            for offset, line in enumerate(lines):
                flat.append((first + offset, first + offset, [line]))
    return flat


def scan() -> dict:
    """Collect every reference and its verdict."""
    writings = {w: 0 for w in WRITINGS}
    explicit = 0
    held = 0
    held_elsewhere = 0
    no_anchor: list[tuple] = []
    mention_lines = 0
    inline_lines = 0
    files: set[str] = set()

    for rel in docs_markdown():
        text = (REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
        for first, last, lines in paragraphs(text):
            for offset, line in enumerate(lines):
                lineno = first + offset
                mentions = file_mentions(line)
                if not mentions:
                    continue
                if any(tok in TARGETS for _s, _e, tok in mentions):
                    mention_lines += 1
                spans = anchors(line)
                refs_here = 0
                names_a_target = any(tok in TARGETS for _s, _e, tok in mentions)
                for m in NUMBER.finditer(line):
                    number = int(m.group(1))
                    if number < 1 or not names_a_target:
                        continue
                    owner = None
                    for _start, end, token in mentions:
                        if end <= m.start() + 1:
                            owner = token
                    refs_here += 1
                    files.add(rel)
                    writing = classify(line, mentions, m)
                    writings[writing] += 1
                    if writing.startswith("prefix"):
                        explicit += 1
                    hits = []
                    for _s, _e, token in mentions:
                        target_lines = lines_of(token)
                        if target_lines and 1 <= number <= len(target_lines):
                            if any(s in target_lines[number - 1] for s in spans):
                                hits.append(token)
                    if hits:
                        held += 1
                        if owner not in hits:
                            held_elsewhere += 1
                    else:
                        target = owner if owner in TARGETS else next(
                            t for _s, _e, t in mentions if t in TARGETS
                        )
                        target_lines = lines_of(target)
                        cited = (
                            target_lines[number - 1].strip()
                            if target_lines and 1 <= number <= len(target_lines)
                            else "<out of range>"
                        )
                        no_anchor.append(
                            (rel, lineno, first, last, number, target, cited[:64])
                        )
                if refs_here:
                    inline_lines += 1

    return {
        "writings": writings, "explicit": explicit, "held": held,
        "held_elsewhere": held_elsewhere, "no_anchor": no_anchor,
        "mention_lines": mention_lines, "inline_lines": inline_lines,
        "files": files,
    }


def main() -> int:
    args = sys.argv[1:]
    report = "--report" in args
    strict = "--strict" in args
    r = scan()

    print(
        f"check_line_refs: {r['mention_lines']} line(s) in docs/ name a target; "
        f"{r['inline_lines']} of them carry an inline number -- that is the "
        f"input set ({len(r['files'])} file(s))."
    )
    print(
        "check_line_refs: writings -- "
        + ", ".join(f"{w} {r['writings'][w]}" for w in WRITINGS)
        + f"; explicit form {r['explicit']}."
    )
    print(
        f"check_line_refs: {r['held']} reference(s) held by the quoted text on "
        f"their own line ({r['held_elsewhere']} of those hold it in another "
        f"file the line names), {len(r['no_anchor'])} with no anchor in any "
        f"file their line names."
    )

    failures = []
    if not r["files"]:
        failures.append(
            "the input set is empty -- the sweep found nothing to check, which "
            "is a failure and not a pass"
        )
    for w in WRITINGS:
        if r["writings"][w] == 0:
            failures.append(
                f"no reference is written in the {w} form any more; a checker "
                f"that has quietly become a prefix scan reports that nothing "
                f"was missed while missing the shape it exists to find"
            )
    if r["explicit"] != EXP_LINE_REFS:
        moved = "shrank" if r["explicit"] < EXP_LINE_REFS else "grew"
        failures.append(
            f"explicit-form references: {r['explicit']}, but EXP_LINE_REFS is "
            f"{EXP_LINE_REFS} -- the cited form {moved} without anyone saying so"
        )
    if len(r["no_anchor"]) > EXP_LINE_REFS_UNANCHORED_MAX:
        failures.append(
            f"unanchored references: {len(r['no_anchor'])}, over the ceiling "
            f"EXP_LINE_REFS_UNANCHORED_MAX={EXP_LINE_REFS_UNANCHORED_MAX} -- "
            f"{len(r['no_anchor']) - EXP_LINE_REFS_UNANCHORED_MAX} more "
            f"number(s) now have nothing holding them"
        )

    print(
        f"check_line_refs: pins -- explicit {r['explicit']} (expect "
        f"{EXP_LINE_REFS}), unanchored {len(r['no_anchor'])} (ceiling "
        f"{EXP_LINE_REFS_UNANCHORED_MAX})."
    )

    if report:
        by_unit: dict[tuple, list] = {}
        for rel, lineno, first, last, number, owner, cited in r["no_anchor"]:
            by_unit.setdefault((rel, first, last), []).append(
                (lineno, number, owner, cited)
            )
        for (rel, first, last), refs in sorted(by_unit.items()):
            print(
                f"  {rel}:{first}-{last}  {len(refs)} reference(s) with no "
                f"anchor in this unit:"
            )
            for lineno, number, owner, cited in sorted(refs):
                print(f"      :{number} ({owner}) cited at line {lineno}: {cited!r}")

    if strict and r["no_anchor"]:
        print(
            f"check_line_refs: --strict: {len(r['no_anchor'])} reference(s) "
            f"have no content anchor.  A number nothing holds will move."
        )
        return 1

    if failures:
        for f in failures:
            print(f"check_line_refs: {f}")
        print(f"check_line_refs: FAILED ({len(failures)} assertion(s)).")
        return 1

    print(
        "check_line_refs: every pin held.  A pin proves no shrinkage and no "
        "growth; it does not prove any anchor is right, and a number that is "
        "still there and points at something else is not caught here."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
