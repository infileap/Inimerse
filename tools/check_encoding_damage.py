#!/usr/bin/env python3
"""check_encoding_damage.py -- damage that is valid, so nothing reads it.

Why this exists (docs/STATUS.md 10.258):

U+FFFD (the Unicode replacement character) is what a decoder writes when it
cannot read a byte sequence.  It is a LEGAL character: a file containing it
decodes cleanly, so every instrument this tree has passes it.

    $ python3 -c "open('src/compiler/bytecode.c','rb').read().decode('utf-8')"
    (no error)

    $ python3 tools/check_text_integrity.py
    check_text_integrity: 1158 text file(s), 0 with NUL bytes, ...

Measured on this tree: 508 of them across 12 files.  They are in comments --
`/* ---------- \ufffd\ufffd... ---------- */`, the box-drawing rules that
separate sections -- so a truncated Chinese section title now reads as
replacement characters and a reader sees `表达式解\ufffd?`.

The damage is IRRECOVERABLE.  `git log --reverse -- src/compiler/bytecode.c`
shows the file entering the repository with them already in it (8248e08e), so
the original bytes never existed in this history and no diff can restore them.

So this checker does not repair anything.  It does the one thing that is still
possible: it holds the count, per file, and a file that grows is a red.  The
existing damage is a ceiling in §10.242's shape; the ceiling may only fall, in
the commit that removes some.

Two things it deliberately does not do:

  * it does not judge the existing count.  A comment truncated by a bad
    transcoding in 2024 is not a defect a reader can act on today, and a
    checker that reddens on it is a checker that gets bypassed;
  * it does not look for mojibake.  `鐢ㄦ` (GBK bytes decoded as UTF-8) is a
    different failure -- legal characters, no replacement mark -- and detecting
    it needs a language model, not a byte scan.

Exit codes: 0 no file grew; 1 a file grew, or the total grew, or a file that
had none now has some; 2 the census could not be read.
"""

from __future__ import annotations

import glob
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MARK = b"\xef\xbf\xbd"

# The census this tree carries today, file -> count.  Measured, not guessed:
# `for f in src/**/*.{c,h} ...; do printf '%s %s\n' "$(grep -c ...)" "$f"; done`
#
# Each of these is irrecoverable (see the docstring), so the entry is a ceiling
# and not a to-do.  A file listed here may only LOWER.
BASELINE = {
    "src/compiler/bytecode.c": 409,
    "src/parser/parser.c": 26,
    "src/vm/vm.c": 25,
    "src/vm/vm.h": 21,
    "src/main.c": 15,
    "src/mod/verse_dist_mod.c": 3,
    "src/lexer/lexer.h": 3,
    "docs/AUDIT.md": 2,
    "src/runtime/runtime.c": 1,
    "src/parser/ast.h": 1,
    "docs/streams/dec-progress.md": 1,
    "docs/archive/ROADMAP_0.5-0.6.md": 1,
}

# The census's own denominator.  Printed so "0 problems" is not mistaken for
# "nothing was scanned": a glob that matches nothing reports a clean tree about
# an empty set, which is the shape §10.252 was about.
SCAN_PATTERNS = [
    "src/**/*.c",
    "src/**/*.h",
    "tools/**/*.py",
    "docs/**/*.md",
    "examples/**/*.im",
    "vtest/**/*.im",
]


def census(repo_root: str) -> tuple[dict, int]:
    counts: dict[str, int] = {}
    scanned = 0
    for pattern in SCAN_PATTERNS:
        for path in sorted(glob.glob(os.path.join(repo_root, pattern), recursive=True)):
            rel = os.path.relpath(path, repo_root)
            if not os.path.isfile(path):
                continue
            try:
                data = open(path, "rb").read()
            except OSError:
                continue
            scanned += 1
            n = data.count(MARK)
            if n:
                counts[rel] = n
    if scanned == 0:
        raise RuntimeError(
            "scanned 0 files -- the glob patterns match nothing, which is not the "
            "same answer as 'no file carries the mark'"
        )
    return counts, scanned


def compare(counts: dict, baseline: dict) -> list:
    """The rule, as a function, so the self-test measures it and not a copy.

    The first version of this self-test recomputed the comparison inline.  That
    made it a control over an expression nobody runs: replacing `elif n > base`
    with `elif False` in `measure` left all five cells green.  A control has to
    call the code it claims to fence.
    """
    problems = []
    for rel, n in sorted(counts.items()):
        base = baseline.get(rel)
        if base is None:
            problems.append("%s carries %d mark(s) and is not in the census" % (rel, n))
        elif n > base:
            problems.append(
                "%s grew from %d to %d -- %d new irrecoverable character(s)"
                % (rel, base, n, n - base)
            )
    total = sum(counts.values())
    base_total = sum(baseline.values())
    if total > base_total:
        problems.append(
            "the tree grew from %d to %d mark(s); the ceiling is the sum of the "
            "per-file census" % (base_total, total)
        )
    return problems


def measure(repo_root: str):
    counts, scanned = census(repo_root)
    problems = compare(counts, BASELINE)
    total = sum(counts.values())
    base_total = sum(BASELINE.values())
    stats = {
        "scanned": scanned,
        "files": len(counts),
        "total": total,
        "base_total": base_total,
    }
    return (1 if problems else 0), problems, stats


def self_test() -> int:
    """Three directions, on inputs whose answer is known.

    A checker that only ever says "not more than the baseline" passes when the
    baseline is wrong in the safe direction and when the scanner sees nothing.
    """
    import tempfile

    failures = []
    base = {"a.c": 2, "b.c": 0}
    cases = [
        ("at the ceiling is clean", {"a.c": 2}, 0),
        ("one new mark in a known file is a red", {"a.c": 3}, 1),
        ("a mark in an unlisted file is a red", {"a.c": 2, "c.c": 1}, 1),
    ]
    for name, counts, want in cases:
        got = 1 if compare(counts, base) else 0
        if got != want:
            failures.append("%s: want rc=%d got %d" % (name, want, got))

    # The scanner must refuse an empty census rather than report a clean tree.
    root = tempfile.mkdtemp(prefix="enc_damage_")
    try:
        try:
            census(root)
            failures.append("an empty tree must raise, not report 0 marks")
        except RuntimeError:
            pass
        # And it must find a mark when there is one.
        os.makedirs(os.path.join(root, "src"))
        with open(os.path.join(root, "src", "x.c"), "wb") as fh:
            fh.write(b"/* \xef\xbf\xbd */\n")
        counts, scanned = census(root)
        if counts.get("src/x.c") != 1:
            failures.append("a planted mark must be counted: got %r" % (counts,))
    finally:
        import shutil

        shutil.rmtree(root, ignore_errors=True)

    print("check_encoding_damage --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_encoding_damage: self-test FAILED (%d)" % len(failures))
        return 1
    for line in (
        "at the ceiling is clean",
        "one new mark in a known file is a red",
        "a mark in an unlisted file is a red",
        "an empty census raises instead of reporting clean",
        "a planted mark is counted",
    ):
        print("  ok   %s" % line)
    print("check_encoding_damage: self-test OK")
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
        print("check_encoding_damage: cannot measure: %s" % problem)
        return 2

    for line in problems:
        print("  PROBLEM  %s" % line)
    print(
        "check_encoding_damage: scanned=%d|files-with-marks=%d|marks=%d"
        "|ceiling=%d|slack=%d"
        % (
            stats["scanned"],
            stats["files"],
            stats["total"],
            stats["base_total"],
            stats["base_total"] - stats["total"],
        )
    )
    if red:
        print(
            "check_encoding_damage: new U+FFFD in a text file.  It decodes cleanly, so "
            "every other instrument in this tree passes it."
        )
        return 1
    print(
        "check_encoding_damage: no file carries more replacement characters than the "
        "census records.  The existing ones are irrecoverable and are a ceiling, not a "
        "to-do list."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
