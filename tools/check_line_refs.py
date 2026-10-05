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

This checker does not fix a number.  It asks whether anything holds one.

Input set (docs/AUDIT.md 1.72 star 11): a line in docs/**/*.md that names a
target and then carries an inline number.  Three writings must all be in it:

    CMakeLists.txt:426-436          the strong form -- the name is glued to
                                    the number, so its owner is not in doubt
    [../CMakeLists.txt](../CMakeLists.txt) ... `:421`
                                    the markdown-link form -- a prefix-only
                                    rule never sees this one, and
                                    docs/AUDIT.md:1569 is exactly this shape
    `CMakeLists.txt` ... `:935`     the name, then a bare number later on the
                                    same line

The denominator is measured and printed on every run, because a sweep that
picks its own input by shape reports "nothing missed" while missing the shape
it was built to find.

The test: for a number `:N`, the quoted text on the line is looked for at line
N in EVERY file the line names.  That is what decides who owns the number --
not proximity, and not the name nearest the number.  Then:

    held              the target is among the files whose line N carries the
                      quoted text.  The number has something holding it.
    held elsewhere    another file named on the same line carries it.
                      docs/AUDIT.md:692 is this shape: it names
                      CMakeLists.txt, but the `:241` and `:1042` on it belong
                      to src/runtime/runtime_posix.c.  The number is fine.
    no anchor         no file the line names carries the quoted text at N.
                      REPORTED, NOT BLOCKED.  A line may describe the target
                      without quoting it, so this is a question for the
                      author -- is the number a pointer, or a record of a
                      value as it was? -- and not a verdict.

Honest boundaries:

  1. A missing anchor is not proof the number is wrong, which is why the
     default is a report.  `--strict` is for a caller who has already decided
     that every citation in docs/ must carry its own text.
  2. Proximity was tried and rejected.  "The quoted text nearest the number"
     and "the file named nearest the number" were both run over this repo;
     each turned hundreds of rows of prose into candidates, and neither could
     tell two files apart on a line that cites both.  Trying every named file
     is the only rule here that answered the question asked.
  3. The input set is a lower bound.  A reference whose file name sits on an
     earlier line, or a bare number whose line names no file at all, is out
     of scope even when it is a true reference.
  4. Document-to-document citations (docs/AUDIT.md:<N>, and tools/ citing
     docs/) are a third kind and are NOT in scope here.
  5. A match proves the quoted text sits at that number.  It does not prove
     the number is right for the sentence -- a number can be right for the
     wrong claim.
  6. The reporting unit is the PARAGRAPH, not the reference, and each line
     says how many of the paragraph's references DID resolve.  The same stale
     value is usually cited more than once a few lines apart, and three such
     pairs have been measured (docs/AUDIT.md:713 fixed :383 while :453 kept
     it; :3330 dropped an ordinal while :3326 kept it; :1659 fixed :935 while
     :1657, two lines above, kept :915).  A checker that reports only the line
     it was pointed at makes its reader's mistake.

Usage:
    python3 tools/check_line_refs.py             report only, always exit 0
    python3 tools/check_line_refs.py --strict    exit 1 if any reference has
                                                 no anchor in any file its line
                                                 names
"""
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Files whose line numbers docs/ cites.  Each is here for a stated reason, not
# because of its name.
TARGETS = {
    "CMakeLists.txt": "cited in docs/, moved eight times by eight offsets",
    "tools/gate.sh": "cited in docs/, grew by 35 lines in one merge",
}

PATHISH = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_./-]*\.[A-Za-z0-9]{1,6}")
NUMBER = re.compile(r":(\d+)(?:-(\d+))?")
SPAN = re.compile(r"`([^`\n]+)`")
PURE_NUMBER = re.compile(r"^:?\d+(?:-\d+)?$")

_CACHE = {}


def lines_of(rel):
    if rel not in _CACHE:
        path = REPO_ROOT / rel
        _CACHE[rel] = (
            path.read_text(encoding="utf-8", errors="replace").splitlines()
            if path.is_file()
            else None
        )
    return _CACHE[rel]


def docs_markdown():
    out = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "ls-files", "docs/"],
        capture_output=True, text=True, check=True,
    ).stdout
    return [p for p in out.splitlines() if p.endswith(".md")]


def file_mentions(line):
    """Every existing repo file named on this line, with the offset it ends at."""
    found = []
    for m in PATHISH.finditer(line):
        token = m.group(0).rstrip(".")
        if token in TARGETS or (REPO_ROOT / token).is_file():
            found.append((m.end(), token))
    return found


def anchors(line):
    """Quoted strings on the line that could hold a number."""
    out = []
    for span in SPAN.findall(line):
        span = span.strip()
        if not span or len(span) <= 2 or PURE_NUMBER.match(span):
            continue
        if any(t + ":" in span for t in TARGETS):
            continue
        out.append(span)
    return out


MAX_PARA = 20


def paragraphs(text):
    """(first line, last line) for each run of non-blank lines, 1-based.

    A run longer than MAX_PARA lines is not a paragraph.  docs/BOARD.md holds
    a 93-line markdown table with no blank line in it; read as one paragraph
    it carried 406 references and the report became a wall.  A table row is
    its own unit.
    """
    out, buf, start = [], False, None
    for n, line in enumerate(text.splitlines(), 1):
        if line.strip():
            if not buf:
                start = n
            buf = True
        elif buf:
            out.append((start, n - 1))
            buf = False
    if buf:
        out.append((start, len(text.splitlines())))
    bounded = []
    for first, last in out:
        if last - first + 1 <= MAX_PARA:
            bounded.append((first, last))
        else:
            bounded.extend((n, n) for n in range(first, last + 1))
    return bounded


def main():
    strict = "--strict" in sys.argv[1:]

    mention_lines = inline_lines = strong_refs = 0
    held = elsewhere = 0
    unanchored = []
    para_total = {}
    para_held = {}
    para_bounds = {}
    files_seen = set()

    for rel in docs_markdown():
        text = (REPO_ROOT / rel).read_text(encoding="utf-8", errors="replace")
        lines = text.splitlines()
        bounds = paragraphs(text)
        para_bounds[rel] = {n: b for b in bounds for n in range(b[0], b[1] + 1)}
        for first, last in bounds:
            for lineno in range(first, last + 1):
                line = lines[lineno - 1]
                mentions = file_mentions(line)
                if not mentions:
                    continue
                if any(tok in TARGETS for _e, tok in mentions):
                    mention_lines += 1
                spans = anchors(line)
                refs_here = 0
                for m in NUMBER.finditer(line):
                    owner = None
                    for end, token in mentions:
                        if end <= m.start() + 1:
                            owner = token
                    if owner not in TARGETS and not any(
                        t in TARGETS for _e, t in mentions
                    ):
                        continue
                    if owner in TARGETS:
                        strong_refs += 1
                    number = int(m.group(1))
                    if number < 1:
                        continue
                    refs_here += 1
                    files_seen.add(rel)
                    matched = []
                    for _e, token in mentions:
                        tl = lines_of(token)
                        if tl and 1 <= number <= len(tl):
                            if any(s in tl[number - 1] for s in spans):
                                matched.append(token)
                    key = (rel, para_bounds[rel][lineno][0])
                    para_total[key] = para_total.get(key, 0) + 1
                    if matched:
                        held += 1
                        para_held[key] = para_held.get(key, 0) + 1
                        if owner not in matched:
                            elsewhere += 1
                    else:
                        unanchored.append((rel, lineno, number))
                if refs_here:
                    inline_lines += 1

    print(
        "check_line_refs: %d line(s) in docs/ name a target; %d of them carry an "
        "inline number -- that is the input set (%d file(s)); %d reference(s) are "
        "written in the strong form, which is a subset."
        % (mention_lines, inline_lines, len(files_seen), strong_refs)
    )
    print(
        "check_line_refs: %d reference(s) held by the quoted text on their own "
        "line (%d of those hold it in another file the line names), %d with no "
        "anchor in any file their line names."
        % (held, elsewhere, len(unanchored))
    )

    by_value = {}
    for rel, lineno, number in unanchored:
        key = (rel, para_bounds[rel][lineno][0], number)
        by_value.setdefault(key, []).append(lineno)

    for (rel, first, number), linenos in sorted(by_value.items()):
        last = para_bounds[rel][first][1]
        total = para_total.get((rel, first), 0)
        ok = para_held.get((rel, first), 0)
        where = ", ".join(str(n) for n in sorted(set(linenos)))
        print(
            "  %s:%d-%d  :%d has no anchor -- cited at line(s) %s "
            "(%d of this unit's %d reference(s) do resolve)"
            % (rel, first, last, number, where, ok, total)
        )

    if strict and unanchored:
        print(
            "check_line_refs: --strict: %d reference(s) name a target and nothing "
            "on their line sits at the number they name.  A number nothing holds "
            "is a number that will move." % len(unanchored)
        )
        return 1
    if unanchored:
        print(
            "check_line_refs: reported, not blocked -- a missing anchor and a "
            "wrong number are not distinguishable without one (docs/AUDIT.md 1.72)."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
