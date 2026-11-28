#!/usr/bin/env python3
"""A count that moves when you write it must not be written down.

`tools/check_line_refs.py` prints two counts whose value depends on the
prose of the document quoting them: `impossible_numbers` (numbers larger
than the longest file in the tree) and `walked_numbers` (every number the
scan walks past).  Both count numbers in `docs/`, so a section that quotes
an epoch or a hash **adds rows to the group the section describes**.

Measured, not assumed (append one line quoting `:1790919002` to
docs/STATUS.md and re-scan): exactly those two counts move, and the other
nine do not.  That measurement is what makes this checkable -- the set is
not a matter of taste.

THE RULE

    A reading that moves when you write it  -> write the command.
    A reading that is stable under its own description -> write the number.

docs/STATUS.md 10.289 adopted that rule after writing `13`, then `22`,
then `24` for the same quantity in one round, each correct for its tree and
each wrong the moment it landed.  A rule that only lives in prose is a rule
nobody is held to, which is what 10.289 §7 said and did not fix.

WHAT THIS CHECKS

A line in `docs/` that (a) phrases one of the self-referential counts as a
present value and (b) carries a concrete digit rather than a placeholder.
The exemptions are explicit and each is a class:

  * `[obs: <ref>]` on the line -- a RECORD: the number was read on that
    tree, and a fixed tree's count does not move.  This is the same
    discipline `tools/check_obs_markers.py` applies to line references.
  * the line names the tree inline (``on `4b5065fc` ``) -- same reason,
    stated in prose instead of in a marker.
  * a fenced block -- a transcript of a run's output is evidence, not an
    assertion about this tree.
  * a placeholder (`N`, `<N>`, `B`, `…`, `?`) instead of a digit.

Usage:
    python3 tools/check_self_referential_counts.py [--report] [--self-test]

Exit codes: 0 clean, 1 a violation, 2 a setup fault, 4 environment refusal.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# The two counts, and the words a document uses when it states them as a
# reading.  Kept as phrases rather than bare numbers: `26` alone appears
# everywhere and means nothing here.
SELF_REFERENTIAL_PHRASES = (
    (r"number\(s\)\s+exceed", "impossible_numbers"),
    (r"exceed\s+the\s+longest\s+file", "impossible_numbers"),
    (r"\bwalked\s+(past\s+)?(<N>|N\b|\d)", "walked_numbers"),
    (r"\bimpossible\s+(<N>|N\b|\d)", "impossible_numbers"),
)

# A digit that is being asserted, not shown.  `<N>` / `N` / `…` / `?` are the
# placeholder forms this file accepts, and they are the point of the rule.
CONCRETE = re.compile(r"(?<![<\w])\d[\d,]{0,11}(?![\w>])")

RECORD_MARKER = re.compile(r"\[obs:\s*([^\]]*)\]")
# A tree named in prose: a hex ref of 7+ characters inside backticks.
INLINE_TREE = re.compile(r"`[0-9a-f]{7,40}`")
FENCE = re.compile(r"^\s*(```|~~~)")
# A line quoting what a run PRINTED is a transcript of that run, not a claim
# about this tree: it is the inline form of the fenced-block exemption.  The
# tell is the reporting verb the checker's own output uses.
QUOTED_OUTPUT = re.compile(r"^\s*$")   # replaced by the code-span rule below


def _span_state(carry_open: bool, line: str) -> tuple[bool, int]:
    """(still-open after this line, the position where the FIRST span opened).

    A code span may open on one line and close on the next (docs/STATUS.md
    20970 -> 20971 does exactly that, quoting a checker's output).  Judging a
    line by its own backticks alone reads such a transcript as an assertion.
    """
    open_at = 0 if carry_open else -1
    state = carry_open
    for i, ch in enumerate(line):
        if ch == "`":
            if not state and open_at < 0:
                open_at = i
            state = not state
    return state, open_at
# A placeholder is the CORRECT way to write a moving reading.
PLACEHOLDER = re.compile(r"<N>|<B>|\bN\b|…|\?")


def markdown_files() -> list[str]:
    out = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "ls-files", "-z", "--", "docs/"],
        capture_output=True, encoding="utf-8", check=False,
    )
    if out.returncode != 0:
        print(
            "check_self_referential_counts: ENVIRONMENT -- git ls-files failed; "
            "the file set is the input, so nothing was measured",
            file=sys.stderr,
        )
        raise SystemExit(4)
    return sorted(p for p in out.stdout.split("\0") if p.endswith(".md"))


def scan(files: list[str] | None = None) -> dict:
    """Every line that states a self-referential count with a digit."""
    violations = []
    fenced = 0
    for rel in (files if files is not None else markdown_files()):
        try:
            text = (REPO_ROOT / rel).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        in_fence = False
        span_open = False          # a code span may open on the previous line
        for lineno, line in enumerate(text.splitlines(), 1):
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            if in_fence:
                fenced += 1
                continue
            _opened_inside, _open_at = _span_state(span_open, line)
            hit = _violation_key(line, span_open)
            if hit is None:
                span_open = _span_state(span_open, line)[0]
                continue
            # Inside a code span the text is a transcript of a run, not a claim
            # about this tree -- and the span may have opened on the line
            # above, which is why the state is carried.
            if span_open:
                span_open = _span_state(span_open, line)[0]
                continue
            violations.append(
                {
                    "file": rel, "line": lineno, "count": hit,
                    "text": line.strip()[:150],
                }
            )
            span_open = _span_state(span_open, line)[0]
            span_open = _opened_inside
    return {"violations": violations, "fenced_lines": fenced,
            "files": len(files if files is not None else markdown_files())}


def report(r: dict) -> None:
    for v in r["violations"]:
        print(
            f"SELF-REF|{v['file']}:{v['line']}|count={v['count']}|"
            f"{v['text']}"
        )
    print(
        f"SELF-REF|summary|files={r['files']}|violations={len(r['violations'])}|"
        f"fenced-skipped={r['fenced_lines']}"
    )


def selftest() -> int:
    """Two controls, each of which must fire, and two that must not.

    A checker that has only ever been observed green is a checker nobody has
    seen work -- the defect docs/STATUS.md 10.287 named, where a cell passed
    its own reverse control for a reason unrelated to its purpose.  These
    cases are synthetic: they do not read the checkout, so the result does
    not depend on what docs/ happens to contain today.
    """
    import tempfile
    failures = 0
    cases = [
        # (label, line, want_violation)
        ("a count written as a present number",
         "the scan walked past 6238 number(s).", True),
        ("the same count as a placeholder",
         "the scan walked past N number(s).", False),
        ("the same count as a record",
         "the scan walked past 6238 number(s) [obs: 4b5065fc].", False),
        ("the same count with its tree inline",
         "the scan walked past 6238 number(s) on `4b5065fc`.", False),
        ("the same count quoted from a run",
         "gate printed `the scan walked past 6238 number(s)`", False),
        ("impossible stated as a number",
         "26 number(s) exceed the longest file in the tree (26140 line(s))",
         True),
        ("impossible stated with a placeholder",
         "<N> number(s) exceed the longest file in the tree (<B> line(s))",
         False),
        # 10.290 §7 named this evasion and did not fix it; 10.291 measured it
        # and did.  A checkbox that a slot excused the whole line meant a
        # writer could keep the value and add the slot.
        ("a placeholder used to smuggle the value in",
         "the scan walked past N number(s), actually 6238.", True),
        ("a placeholder with the value in parentheses",
         "the scan walked past N number(s) (6238 today).", True),
        # ... and the legitimate shape, so the fix is not "always red".
        ("a placeholder whose bound is a file length",
         "<N> number(s) exceed the longest file in the tree (26140 line(s))",
         False),
        # A table cell quoting the value inside a span that opens on the SAME
        # line.  The carried state is False while the match is still inside the
        # span, so a unification that only checked the carried state flagged
        # this -- measured when scan() and the self-test were merged.
        ("a value quoted inline in a table cell",
         "| the plain value (`walked past 6238`) | 1 | 1 |", False),
    ]
    for label, line, want in cases:
        # The transcript case opens its span on the same line, so compute the
        # state the scan would carry into it: False, then the line's own.
        _state, open_at = _span_state(False, line)
        _open_before = open_at >= 0 and line.count("`", 0, open_at + 1) % 2 == 1
        got = _line_violates(line, _open_before)
        if got != want:
            failures += 1
            print(
                f"check_self_referential_counts --self-test: FAIL {label!r}: "
                f"expected violation={want}, got {got} for {line!r}"
            )
    if failures:
        print(f"check_self_referential_counts --self-test: {failures} case(s) "
              "differed")
        return 1
    print(f"check_self_referential_counts --self-test: ok ({len(cases)} cases)")
    return 0


def _digits_beside_placeholder(line: str) -> bool:
    """True when a concrete digit shares the line with a placeholder.

    `N number(s), actually 6238` states a value and dresses it as a slot.
    The bound is the one number that legitimately sits beside a count
    placeholder -- `<N> ... (26140 line(s))` names a file length, not the
    count -- so it is exempted by being the value of the phrase that names it.
    """
    for m in CONCRETE.finditer(line):
        after = line[m.end():m.end() + 14]
        if re.match(r"\s*line\(s\)", after):
            continue
        return True
    return False


def _position_in_span(line: str, pos: int) -> bool:
    """True when `pos` sits after a code span opened on THIS line.

    A transcript quoted inline opens and closes on one line, so the carried
    state is False while the match is still inside the span.
    """
    opened = line.rfind("`", 0, pos)
    return opened >= 0 and line.count("`", 0, pos) % 2 == 1


def _violation_key(line: str, in_span: bool = False) -> "str | None":
    """Which count this line states as a number, or None.

    The ONE decision.  `scan()` and the self-test both call it, because two
    copies of a decision drift: measured 2026-11-28, the copy inside `scan()`
    kept the placeholder exemption while the copy the self-test drove had been
    fixed, so seven self-test cases passed and the real run reported zero
    violations on a document that contained one.
    """
    for pattern, key in SELF_REFERENTIAL_PHRASES:
        m = re.search(pattern, line, re.IGNORECASE)
        if not m:
            continue
        if not CONCRETE.search(line):
            continue
        if RECORD_MARKER.search(line) or INLINE_TREE.search(line):
            return None
        if PLACEHOLDER.search(line) and not _digits_beside_placeholder(line):
            return None
        if in_span or _position_in_span(line, m.start()):
            return None
        return key
    return None


def _line_violates(line: str, _in_span: bool = False) -> bool:
    """The single-line decision, extracted so the self-test can drive it."""
    return _violation_key(line, _in_span) is not None



def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return selftest()

    r = scan()
    if args.report:
        report(r)
    if r["violations"]:
        v = r["violations"][0]
        print(
            f"check_self_referential_counts: {len(r['violations'])} line(s) "
            f"state a self-referential count as a number. {v['file']}:"
            f"{v['line']} -- `{v['count']}` moves when the text describing it "
            "is edited, so it has no value to write down; write the command "
            "instead, or name the tree the number was read on ([obs: <ref>]).",
            file=sys.stderr,
        )
        return 1
    print(
        "check_self_referential_counts: every count that moves under its own "
        "description is given as a command, a placeholder, or a tree."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
