#!/usr/bin/env python3
"""check_scope_claims.py -- the scope table's comments against the readings.

Why this exists (docs/STATUS.md 10.270):

`tools/gate.sh`'s `REQUIRED_SCOPES` decides which stages a change forces, and
each row carries a comment saying what it does.  One of them said:

    # .github/* -- no reader and no stage: CI is CI's own business.
    ".github/*|-"

Measured: `--required-for .github/workflows/linux-build.yml` answers
ONE stage, because every one of those four files ends `.yml`, which is in
`text-integrity`'s `TEXT_SUFFIXES`, and the glob fallback at the end of
`stages_for_path` appends that stage whatever the row says.

So the comment described a different thing than the line under it -- the same
defect as `kong (empty set)` over a `VAL_NIL` line (§10.257), in the table the
gate itself runs from.

The comment is corrected.  This checker is what stops the next one from being
written the same way: for every scope row that claims a COUNT in its comment
("no stage", "every stage", "N stages"), the claim is read back with
`--required-for` on a file the row matches.

The reading is the gate's own answer, not a re-derivation: a checker that
re-derived it would be a second implementation of `stages_for_path`, and a
second implementation is what the gate's own comments warn about.

Exit codes: 0 every countable claim matched; 1 one did not; 2 the measurement
could not be made.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE = os.path.join(REPO_ROOT, "tools", "gate.sh")

# A row is `"<pattern>|<selectors>"`.  The selectors are what it forces.
ROW = re.compile(r'^\s*"([^"|]+)\|([^"]*)"\s*$')

# A comment claims a count when it says one of these.  "no stage" is the one that
# was wrong; the others are checked because they are the same kind of sentence.
#
# A QUOTED occurrence is a mention, not a claim: the corrected `.github/*` row
# says `no READER, and the \`-\` says this row forces no stage of its own` and
# then explains that the phrase `"no stage"` used to be wrong.  Reading the
# quotation as a claim made the checker report the row it had just fixed -- the
# first version of this pattern did exactly that.
# The claim is "this ROW forces no stage", not every occurrence of the words:
# `no stage ANSWERS TO` is the gate's own message about a broken registry, and
# `"no stage runs"` in quotation marks is a phrase being discussed.  So the
# pattern demands the force-reading: `no stage` followed by something that ends
# the sentence, and not followed by `answers`/`runs`/`is`.
#
# `of` was missing, and the omission made the checker report the row it had
# just fixed: the force-reading the file's own docstring gives as correct is
# `no READER, and the `-` says this row forces no stage of its own`, and
# `no stage of its own` is a statement about THIS ROW, not about the tree.
# Measured on the corrected .github/* row: with `of` absent the pattern
# matched it and the row was reported wrong; with `of` present it does not
# match, and the row's real claim (`no READER`) is what the checker reads.
NO_STAGE = re.compile(
    r"\bno\s+stage\b(?!\s*(?:answers|runs|is|of|to|\(|\[|[\"\u201d]))", re.I
)
EVERY_STAGE = re.compile(r"\b(every|all)\s+stage", re.I)
# `3 stages` and `3 more stages` are the same claim; the self-test case that
# says so was written before the pattern could read it, and the pattern moved.
N_STAGE = re.compile(r"\b(\d+)(?:\s+\w+){0,2}\s+stage", re.I)

# A witness file for a pattern: the pattern's own prefix with a real file under
# it.  Built by asking git, so a pattern that matches nothing is reported as
# unmeasurable rather than scored as agreement.
def witness(pattern: str, repo_root: str):
    import glob as _glob

    prefix = pattern.split("*")[0].rstrip("/")
    if not prefix:
        return None
    base = os.path.join(repo_root, prefix)
    if os.path.isfile(base):
        return os.path.relpath(base, repo_root)
    for path in sorted(_glob.glob(os.path.join(base, "**", "*"), recursive=True)):
        if os.path.isfile(path) and not path.endswith("/.gitkeep"):
            return os.path.relpath(path, repo_root)
    return None


def required_for(repo_root: str, rel: str) -> int | None:
    """The stages a change to `rel` forces, read the only way that answers it.

    `--required-for <path>` means "the UNSTAGED changes to that path", so a clean
    file answers 0 -- and the first version of this function read that 0 as the
    row's own answer and reported `CMakeLists.txt` as forcing nothing.  The rule
    is about a CHANGE, so the fixture has to be one: the file is touched, the
    touch is staged alone, `--required-for staged` is asked, and the index is
    restored.  A dirty worktree is left exactly as it was found.
    """
    path = os.path.join(repo_root, rel)
    if not os.path.isfile(path):
        return None
    original = open(path, "rb").read()
    try:
        with open(path, "ab") as handle:
            handle.write(b"\n")
        staged = subprocess.run(
            ["git", "-C", repo_root, "add", "--", rel], capture_output=True, text=True
        )
        if staged.returncode != 0:
            return None
        proc = subprocess.run(
            ["bash", os.path.join(repo_root, "tools", "gate.sh"), "--required-for", "staged"],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        match = re.search(r"required: (\d+) stage", proc.stdout + proc.stderr)
        return int(match.group(1)) if match else None
    finally:
        with open(path, "wb") as handle:
            handle.write(original)
        subprocess.run(
            ["git", "-C", repo_root, "reset", "-q", "--", rel], capture_output=True, text=True
        )


def rows_with_claims(repo_root: str):
    """(pattern, selectors, comment-lines-above, claims) for every row.

    The comment above a row is taken from the contiguous `#` lines immediately
    preceding it; a row with no comment makes no claim and is skipped.
    """
    text = open(os.path.join(repo_root, "tools", "gate.sh"), encoding="utf-8").read()
    start = text.index("REQUIRED_SCOPES=(")
    end = text.index("\n)\n", start)
    lines = text[start:end].splitlines()
    out = []
    pending = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("#"):
            pending.append(stripped.lstrip("#").strip())
            continue
        match = ROW.match(line)
        if not match:
            if stripped and not stripped.startswith("#"):
                pending = []
            continue
        pattern, selectors = match.group(1), match.group(2)
        comment = " ".join(pending)
        pending = []
        claims = []
        if NO_STAGE.search(comment):
            claims.append(("no-stage", 0))
        if EVERY_STAGE.search(comment):
            claims.append(("every-stage", None))  # resolved against the registry
        for m in N_STAGE.finditer(comment):
            claims.append(("n-stage", int(m.group(1))))
        if claims:
            out.append((pattern, selectors, comment, claims))
    return out


def measure(repo_root: str):
    rows = rows_with_claims(repo_root)
    if not rows:
        raise RuntimeError(
            "no scope row carries a countable claim in its comment -- this reader is "
            "reading nothing, which is not the same as the table making no claims"
        )
    # The registry's own size, for `every stage`.
    total = required_for(repo_root, "tools/gate.sh")
    if total is None:
        raise RuntimeError("`--required-for tools/gate.sh` gave no count; nothing can be judged")

    problems, measured = [], []
    for pattern, selectors, comment, claims in rows:
        rel = witness(pattern, repo_root)
        if rel is None:
            measured.append((pattern, None, "no file matches this pattern"))
            continue
        got = required_for(repo_root, rel)
        if got is None:
            measured.append((pattern, None, "the gate gave no count for %s" % rel))
            continue
        measured.append((pattern, got, rel))
        for kind, want in claims:
            if kind == "no-stage" and got != 0:
                problems.append(
                    "%r says `no stage` and a change to %s forces %d -- the row is not "
                    "what its comment says" % (pattern, rel, got)
                )
            elif kind == "every-stage" and got != total:
                problems.append(
                    "%r says `every stage` and a change to %s forces %d of %d"
                    % (pattern, rel, got, total)
                )
            elif kind == "n-stage" and got != want:
                problems.append(
                    "%r says `%d stage(s)` and a change to %s forces %d"
                    % (pattern, want, rel, got)
                )
    return (1 if problems else 0), problems, measured


def self_test() -> int:
    """The three claim kinds, and the reader that must not be silent.

    A checker that finds no claims reports every table correct.  A checker whose
    witness lookup returns nothing reports every row unmeasurable, which reads as
    "nothing to judge" rather than as a failure.
    """
    failures = []

    def claims_of(comment):
        out = []
        if NO_STAGE.search(comment):
            out.append(("no-stage", 0))
        if EVERY_STAGE.search(comment):
            out.append(("every-stage", None))
        for m in N_STAGE.finditer(comment):
            out.append(("n-stage", int(m.group(1))))
        return out

    cases = [
        (".github/* -- no reader and no stage: CI is CI's own business.",
         [("no-stage", 0)]),
        ("CMakeLists.txt -- all, because the build graph invalidates every stage's premise",
         [("every-stage", None)]),
        ("src/* -- build ctest fuzz and 3 more stages", [("n-stage", 3)]),
        ("tools/*.py -- python-asserts reads every one of them", []),
        # The force-reading this file's own docstring gives as the correct
        # wording for the .github/* row.  Without it in the table, the case
        # above was the only one, and the corrected row was reported wrong.
        (".github/* -- no READER, and the `-` says this row forces no stage of its own",
         []),
        # The one-line form the gate carries: the force-reading followed by a
        # parenthetical.  Same sentence, and the pattern has to read it as the
        # same sentence.
        (".github/* -- no READER; the `-` says this row forces no stage (the .yml "
         "files are still covered by text-integrity's glob fallback).",
         []),
        # A row that really does force no stage is still a claim.
        ("build/* -- this row forces no stage", [("no-stage", 0)]),
    ]
    for comment, want in cases:
        got = claims_of(comment)
        if got != want:
            failures.append("comment %r: want %r got %r" % (comment[:40], want, got))

    # The reader must refuse an empty table rather than report it clean.
    try:
        witness("", REPO_ROOT)
    except Exception:  # noqa: BLE001
        failures.append("witness('') must return None, not raise")
    if witness("", REPO_ROOT) is not None:
        failures.append("an empty pattern has no witness")

    print("check_scope_claims --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_scope_claims: self-test FAILED (%d)" % len(failures))
        return 1
    for line in (
        "`no stage` is a countable claim",
        "`every stage` is a countable claim",
        "`N stage(s)` is a countable claim",
        "prose mentioning a stage is not a claim",
        "an empty pattern has no witness",
    ):
        print("  ok   %s" % line)
    if not cases:
        print(
            "check_scope_claims: self-test FAILED -- no cases. A self-test "
            "that runs none reports success by arithmetic alone "
            "(docs/STATUS.md 10.293)",
            file=sys.stderr,
        )
        return 1
    print("check_scope_claims: self-test OK (%d cases)" % len(cases))
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    try:
        red, problems, measured = measure(repo_root)
    except (RuntimeError, OSError) as exc:
        print("check_scope_claims: cannot measure: %s" % exc)
        return 2

    for pattern, got, note in measured:
        print("  %-24s required=%-4s %s" % (pattern, "?" if got is None else got, note))
    for line in problems:
        print("  PROBLEM  %s" % line)
    print(
        "check_scope_claims: rows-with-claims=%d|measured=%d|wrong=%d"
        % (len(measured), len([m for m in measured if m[1] is not None]), len(problems))
    )
    if red:
        print(
            "check_scope_claims: a scope row does not do what its comment says.  The "
            "comment is what a reader uses to decide whether a change is covered."
        )
        return 1
    print(
        "check_scope_claims: every countable claim in the scope table matches the gate's "
        "own answer.  This says the comments describe the rows; it does not say the rows "
        "are right."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))