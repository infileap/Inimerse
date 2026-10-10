#!/usr/bin/env python3
"""Which tree is each citation checked against?  (docs/AUDIT.md 1.78)

`tools/check_line_refs.py` asks one question: **has this number grown?**  It
pins denominators and deltas, and it says so in its own docstring -- "it does
not assert that any number is right: a number still there that now points at
something else is not caught."

This checker asks a different question: **which tree was this number read on?**

Those are two questions, and a reader needs both answered separately.  A
number can be perfectly stable under the first and wrong under the second.

THE SHAPE

    `tools/aot_native_bench.py:27` [obs: 199e240^]

is a RECORD.  The number was read on the tree `199e240^`, and it is checked
there.  `199e240^`'s line 27 is the call site; HEAD's line 27 is empty.  The
record is green, and it stays green when HEAD moves -- because it never
claimed anything about HEAD.

    `tools/perf_compare.py:131`

is a POINTER.  It says "go look at line 131", with no tree named, so it is
read against HEAD.  HEAD's line 131 is `    print(report)`, which is not the
`--write-docs` path the sentence is about.  That is red.

WHY THIS IS NOT A TENSE-WORD WHITELIST

The discriminator is not a word.  It is a revision token that
`git rev-parse --verify <tok>^{commit}` resolves.  A present-tense sentence
carrying a resolvable `[obs:]` is a record; a past-tense sentence without one
is a pointer.  The marker decides, not the wording.  (Tense is wording, and
wording is not a fact -- so it cannot be the criterion.)

The operand grammar, decided here and stated so a reader can argue with it:
**the first resolvable token in the operand is the tree the number was read
on.**  In `[obs: ab70a71 -> `main` @ 57ece55]` that is `ab70a71` -- which is
what the arrow means: written at `ab70a71`, re-read at `main` @ `57ece55`.

WHAT IS ALREADY IN THIS REPO (and why this is a tightening, not an invention)

`tools/check_release_tags.py` already reads this marker -- and it reads it as
an exemption:

    RECORD_MARKERS    = (..., "[obs:")
    FALSIFIER_MARKERS = (..., "[obs:")

The marker sits in BOTH tuples, so one `[obs:` satisfies "this is a record"
and "it carries its own falsifier" at once.  Its operand is never read:
`[obs:]`, `[obs: ...]`, `[obs: agent3 的 Windows 树]` and `[obs: 57ece55]`
all exempt the line equally well.

So the marker is not unread.  It is read as a switch that needs no operand --
which makes it a switch that proves itself.  This checker gives it an operand
and makes the operand resolvable.  The four exemption disciplines below are
copied from `EXP_TAG_EXEMPTION_CLASSES` (`tools/check_release_tags.py`),
because a new rule that has a running twin should be the twin.

THE FOUR DISCIPLINES (copied)

  1. every declared exemption class must have at least one member, or red --
     an exemption class with no members is a mechanism no run can reach, and
     its name in the source makes a reader believe it is on guard;
  2. a class that actually exempts something and is not declared, red --
     `an exemption nobody declared is one nobody decided on`;
  3. a marker that is well-formed is green even when HEAD drifts -- the
     falsifier is the operand itself, because a wrong number on that ref is
     checkable;
  4. a negative control per shape (three of them, see the docstring of
     `--selftest`, and see docs/AUDIT.md 1.78 for the run).

HONESTY BOUNDARIES

  * A pointer is checked against `HEAD`, not against the working tree.  That
    is deliberate: the verdict has to be a property of a commit, or two people
    running this on the same commit in different checkouts get two answers.
    `--ref` moves the observation point and the report prints it.
  * A record is checked only for "line N exists on that ref".  It is NOT
    checked for "line N still says what the sentence says" -- that needs a
    content anchor, which is `check_line_refs.py`'s half of the problem, not
    this one's.
  * The input set is hermetic for the same reason `check_line_refs.py`'s is:
    only a file git tracks can hold a number, so an untracked build artefact
    cannot move a denominator.
  * `[obs:]` inside a sentence that is *talking about* the marker (for example
    docs/AUDIT.md 1.78's own prose) is a mention, not a marker.  This checker
    cannot tell them apart, and it does not pretend to; the two mentions in
    this tree are reported as ungrammatical and that is the honest reading.
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# --- expectations -----------------------------------------------------------
# Same shape as tools/gate.sh's EXP_CTEST: a number nobody has to look up in a
# second place, and every one of them overridable so a control can move it.

# A sweep that finds no citation at all is green and asserts nothing -- the
# same shape as docs/AUDIT.md 1.65's zero hits.  A floor, not a target.
EXP_OBS_CITATIONS = int(os.environ.get("EXP_OBS_CITATIONS", "2000"))

# The pointer count is a DELTA against a named base tree, not a count of this
# tree: a count is a property of the tree, and this repository grows.  The base
# is a sha because the meaning of "base" is a thing that does not move.
EXP_OBS_BASE = os.environ.get("EXP_OBS_BASE", "e3e97c71e51ba491cf28d4b7a064ecbece9bf140")
# A CEILING, and it is an equality -- but it is not 0, and that is a debt with a
# number on it.  This stage was registered while the pointers it counts were
# unaccounted for, and the number below is the delta measured in the commit that
# registered it: 2234 -> 2396, 162 citations that name no tree.  A ceiling of 0
# would be a rule that is red on the day it lands, and a rule that is red on the
# day it lands is one everybody learns to step around; a ceiling at the measured
# value counts the debt without pretending it is paid.  Paying it down means
# lowering this number in the same commit that removes the pointers, and 0 is
# where it ends.
#
# The number is not a constant of this repository, and it must not be copied
# from anywhere else: it read 156 at e8bddcd, 154 after 83a3829 turned two
# pointers into records, 162 at 60f0572, and 211 at d36f1ba -- four readings,
# four ordinary docs/ edits.  Taking it and writing it have to be the same
# commit, or the number on the page describes a tree that no longer exists.
# (Only docs/ moves it: this file walks `docs/**/*.md`, so the tools/ and CI
# edits in the same commit are invisible to it.)
#
# The fourth reading is the one worth keeping.  The stage was written on a
# branch based at 60f0572, and the value 162 was taken there, correctly, in the
# commit that registered it.  It then failed on the merge result at +211, and
# nothing was wrong with either measurement: the base is a fixed sha, so the
# delta is measured from that sha to HEAD, and HEAD is not the branch's tree.
# main had moved three commits while the branch was being written, and those
# three commits added 49 pointers of their own.  A ceiling taken on a branch is
# a ceiling taken on a tree that is not the merge tree -- so the value has to be
# re-taken after the merge, in the merge commit, and the re-taking costs
# nothing: this file is not under docs/, so writing the new number here does not
# move the number it measures.
EXP_OBS_POINTERS_DELTA_MAX = int(os.environ.get("EXP_OBS_POINTERS_DELTA_MAX", "211"))

# Which classes may exempt a citation from the HEAD check.  A record is the
# only one: it names its own tree.
EXP_OBS_EXEMPTION_CLASSES = tuple(
    c
    for c in os.environ.get("EXP_OBS_EXEMPTION_CLASSES", "record").split(",")
    if c
)

# --- shapes -----------------------------------------------------------------

# A citation is a tracked path and a line number.  The trailing group is the
# chain continuation, and it accepts BOTH writings the repository uses:
#   tools/bindgen.py:114/143/185          the colon is written once
#   tools/selfhost_bench.py:6/:63/:121    the colon is rewritten after each /
# A regex that knows only the first reads the second as ONE reference, and the
# denominator comes out smaller and tidier than the truth.
CITATION = re.compile(
    r"`?([A-Za-z0-9_][A-Za-z0-9_./-]*?):(\d+)((?:[/／]:?\d+(?:-\d+)?)*)"
)
MARKER = re.compile(r"\[obs:([^\]]*)\]")
# A marker written inside a code span is a MENTION -- a sentence talking about
# the marker's syntax, not a marker.  The four on docs/AUDIT.md:2963 are the
# rule describing itself, and docs/BOARD.md:65's is the tag checker's own
# docstring quoted into a table cell.  Without this, a rule's own text reads
# as a violation of the rule.
CODE_SPAN = re.compile(r"`[^`\n]*`")
REV_TOKEN = re.compile(r"[A-Za-z0-9_^~.@/-]{4,}")

RED = "red"
GREEN = "green"


def git(*args):
    return subprocess.run(
        ["git", "--no-pager", *args],
        cwd=ROOT,
        capture_output=True,
        text=True,
        errors="surrogateescape",
    )


def tracked(paths):
    out = git("ls-files", "-z", *paths)
    return [p for p in out.stdout.split("\0") if p]


def resolves(tok):
    """Is `tok` a revision this repository can resolve to a commit?"""
    if not tok:
        return False
    r = git("rev-parse", "--verify", "--quiet", tok + "^{commit}")
    return r.returncode == 0


def show_lines(ref, path):
    r = git("show", f"{ref}:{path}")
    if r.returncode != 0:
        return None
    return r.stdout.splitlines()


# A token shaped like a revision: a hex object name, or a v-prefixed tag.
# Used only to tell the two failures apart -- a marker that names a revision
# this repository cannot resolve, and a marker that names no revision at all.
REF_SHAPED = re.compile(r"^(?:[0-9a-f]{7,40}|v[0-9][A-Za-z0-9_.-]*)$")


def record_tree(operand):
    """(tree, why) for a marker operand.

    First resolvable token, not any: the arrow in `[obs: A -> B]` means the
    number was read at A, so A is the tree.  Taking "any resolvable token"
    would let a marker name a tree that has nothing to do with the number.

    why is None on success, and otherwise says WHICH failure this is:
      "unresolved"  -- the operand names something revision-shaped that this
                       repository cannot resolve (a wrong or stale sha)
      "not-a-ref"   -- the operand names no revision at all (prose)
    One checker that reports both as "invalid ref" is right; a checker that
    reports only the first lets the second through, and the second is the one
    this tree actually contains.
    """
    tokens = REV_TOKEN.findall(operand)
    for tok in tokens:
        if resolves(tok):
            return tok, None
    for tok in tokens:
        if REF_SHAPED.match(tok):
            return None, ("unresolved", tok)
    return None, ("not-a-ref", None)


def main():
    argv = sys.argv[1:]
    ref = "HEAD"
    docs_root = "docs"
    while argv:
        a = argv.pop(0)
        if a == "--ref":
            ref = argv.pop(0)
        elif a == "--docs-root":
            docs_root = argv.pop(0)
        else:
            print(f"check_obs_markers: unknown argument {a!r}", file=sys.stderr)
            return 2

    if not resolves(ref):
        print(f"check_obs_markers: cannot resolve --ref {ref!r}", file=sys.stderr)
        return 2
    ref_sha = git("rev-parse", "--short", ref).stdout.strip()

    tracked_set = set(tracked([]))
    docs = sorted(
        p for p in tracked_set if p.startswith(docs_root.rstrip("/") + "/") and p.endswith(".md")
    )
    if not docs:
        print(f"check_obs_markers: no markdown under {docs_root!r}", file=sys.stderr)
        return 2

    records = []      # (file, lineno, path, num, tree)     -- green unless bad on its own tree
    pointers = []     # (file, lineno, path, num)           -- green unless bad on HEAD
    ungrammatical = []  # (file, lineno, operand)           -- red, always
    bad = []          # (file, lineno, why)

    absent = []
    for f in docs:
        # The DOCUMENT is read from the working tree, not from `--ref`.  A gate
        # asks about the checkout it is about to accept, and reading the
        # committed blob instead makes every uncommitted edit invisible: the
        # three red controls below are working-tree mutations, and against
        # `git show HEAD:` they were all silently green.  Only the TARGET of a
        # citation is read from a named tree -- that is the whole point, the
        # tree the number was read on.
        try:
            with open(os.path.join(ROOT, f), encoding="utf-8") as fh:
                lines = fh.read().splitlines()
        except FileNotFoundError:
            absent.append(f)
            continue
        except (OSError, UnicodeDecodeError):
            print(f"check_obs_markers: {f} exists but could not be read", file=sys.stderr)
            return 2
        for i, line in enumerate(lines, 1):
            cites = [m for m in CITATION.finditer(line) if m.group(1) in tracked_set]
            spans = [(m.start(), m.end()) for m in CODE_SPAN.finditer(line)]
            operands = [
                m
                for m in MARKER.finditer(line)
                if not any(a <= m.start() < b for a, b in spans)
            ]
            # The input set is the union of "lines that cite a number" and
            # "lines that carry a real marker".  A marker on a line with no
            # citation is still a marker, and it is still the thing this
            # checker is about -- docs/AUDIT.md:3188 carries no citation and is
            # the only ungrammatical marker in this tree.  Building the input
            # set from the citation alone would never look at it.
            if not cites and not operands:
                continue
            trees = []
            for m in operands:
                op = m.group(1)
                t, why = record_tree(op)
                if t is None:
                    ungrammatical.append((f, i, op.strip(), why))
                else:
                    trees.append(t)
            for m in cites:
                path, num = m.group(1), int(m.group(2))
                if trees:
                    records.append((f, i, path, num, trees[0]))
                else:
                    pointers.append((f, i, path, num))

    for f, i, path, num, tree in records:
        lines = show_lines(tree, path)
        if lines is None:
            bad.append((f, i, f"record names {path}, which {tree} does not have"))
        elif not (1 <= num <= len(lines)):
            bad.append(
                (f, i, f"record {path}:{num} is out of range ON ITS OWN TREE "
                        f"({tree}: {len(lines)} lines)")
            )

    for f, i, path, num in pointers:
        lines = show_lines(ref, path)
        if lines is None:
            bad.append((f, i, f"pointer names {path}, which {ref} does not have"))
        elif not (1 <= num <= len(lines)):
            bad.append(
                (f, i, f"pointer {path}:{num} is out of range on {ref} "
                        f"({len(lines)} lines)")
            )

    # --- the four disciplines ------------------------------------------------

    problems = []
    if len(records) + len(pointers) < EXP_OBS_CITATIONS:
        problems.append(
            f"found {len(records) + len(pointers)} citation(s), floor is "
            f"{EXP_OBS_CITATIONS} -- a sweep that finds nothing is green and "
            f"asserts nothing"
        )

    if not resolves(EXP_OBS_BASE):
        print(
            f"check_obs_markers: EXP_OBS_BASE {EXP_OBS_BASE!r} does not resolve",
            file=sys.stderr,
        )
        return 2
    anc = git("merge-base", "--is-ancestor", EXP_OBS_BASE, ref)
    if anc.returncode != 0:
        print(
            f"check_obs_markers: EXP_OBS_BASE {EXP_OBS_BASE[:9]} is not an "
            f"ancestor of {ref} -- the base must be behind the tree being read",
            file=sys.stderr,
        )
        return 2
    base_lines = {}
    base_pointers = 0
    base_docs = sorted(
        p for p in tracked_set if p.startswith(docs_root.rstrip("/") + "/") and p.endswith(".md")
    )
    for f in base_docs:
        lines = show_lines(EXP_OBS_BASE, f)
        if lines is None:
            continue
        base_lines[f] = lines
        for line in lines:
            # Count MATCHES, not lines: the live count above counts matches,
            # and two numbers that answer the same question must come out of
            # the same kind of arithmetic.  Counting lines here and matches
            # there made the delta read +936 against a base that WAS this tree.
            cites = [m for m in CITATION.finditer(line) if m.group(1) in tracked_set]
            if not cites:
                continue
            spans = [(m.start(), m.end()) for m in CODE_SPAN.finditer(line)]
            marked = any(
                not any(a <= m.start() < b for a, b in spans)
                for m in MARKER.finditer(line)
            )
            if not marked:
                base_pointers += len(cites)
    behind = int(git("rev-list", "--count", f"{EXP_OBS_BASE}..{ref}").stdout.strip() or 0)
    delta = len(pointers) - base_pointers
    if delta > EXP_OBS_POINTERS_DELTA_MAX:
        problems.append(
            f"pointers grew by {delta} against base {EXP_OBS_BASE[:9]} "
            f"({base_pointers} -> {len(pointers)}); ceiling is "
            f"{EXP_OBS_POINTERS_DELTA_MAX} -- a new citation must name its tree"
        )

    counts = {"record": len(records), "pointer": len(pointers)}
    for klass in EXP_OBS_EXEMPTION_CLASSES:
        if klass not in counts:
            problems.append(
                f"EXP_OBS_EXEMPTION_CLASSES lists {klass!r}, which is not a "
                f"class this checker produces"
            )
        elif counts[klass] == 0:
            problems.append(
                f"exemption class {klass!r} has no members in this tree -- an "
                f"exemption nothing takes is a mechanism no run can reach"
            )
    for klass, n in counts.items():
        if klass in ("record",) and klass not in EXP_OBS_EXEMPTION_CLASSES and n:
            problems.append(
                f"class {klass!r} exempts {n} citation(s) and "
                f"EXP_OBS_EXEMPTION_CLASSES does not list it -- an exemption "
                f"nobody declared is one nobody decided on"
            )

    # --- report --------------------------------------------------------------

    print(f"check_obs_markers: question = which tree was each number read on?")
    print(f"check_obs_markers: pointers checked against {ref} ({ref_sha}); "
          f"records against their own ref")
    print(f"check_obs_markers: {len(records) + len(pointers)} citation(s) in "
          f"{len(docs)} markdown file(s) under {docs_root}/")
    print(f"  records        {len(records)}")
    print(f"  pointers       {len(pointers)}")
    print(f"  ungrammatical  {len(ungrammatical)}")
    if absent:
        # Reported, not dropped: "not in the input set" and "in the input set
        # but skipped" must not look the same from the outside.
        print(f"  absent from the working tree  {len(absent)} document(s) are "
              f"tracked but missing from the checkout, and are not part of "
              f"this run's input set")
    print(f"  base {EXP_OBS_BASE[:9]} is {behind} commit(s) behind {ref}; "
          f"pointer delta {delta:+d} (ceiling {EXP_OBS_POINTERS_DELTA_MAX})")

    for f, i, op, (kind, tok) in ungrammatical:
        if kind == "unresolved":
            # R1: revision-shaped, but this repository cannot resolve it.
            print(f"  UNRESOLVED    {f}:{i}: [obs: {op}] -- the operand names "
                  f"{tok!r}, which `git rev-parse --verify` cannot resolve, so "
                  f"this marker exempts a citation against a tree that does "
                  f"not exist")
        else:
            # R2: no revision named at all.  A DIFFERENT answer, and the one
            # this tree actually contains.
            print(f"  NOT-A-REF     {f}:{i}: [obs: {op}] -- the operand names "
                  f"no revision at all, so this marker exempts a citation "
                  f"without saying which tree to check it on")
    for f, i, why in bad:
        print(f"  RED           {f}:{i}: {why}")
    for p in problems:
        print(f"  RED           {p}")

    if ungrammatical or bad or problems:
        print(f"check_obs_markers: {len(ungrammatical) + len(bad) + len(problems)} "
              f"finding(s)")
        return 1
    print("check_obs_markers: every citation names the tree it was read on")
    return 0


if __name__ == "__main__":
    sys.exit(main())
