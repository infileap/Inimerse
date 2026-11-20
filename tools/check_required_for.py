#!/usr/bin/env python3
"""check_required_for.py -- the gate's own safety entry point, made to fail.

Why this exists (docs/STATUS.md 10.245):

`tools/gate.sh --required-for <path>` answers "which stages does a change to
this path force", and the gate uses that answer to decide what to run.  Its
body contains thirteen `exit 2` sites.  This checker converts five of them --
the ones that run before any path is consulted -- into cases:

  1. a `stage_` function no registry entry names;
  2. `derived_text_globs` reading nothing out of check_text_integrity.py;
  3. `derived_edges` reading no call site at all;
  4. a rule naming a stage no stage answers to (the path table and the
     registry disagree);
  5. a registered stage no rule of its own can reach (`all` alone is not a
     rule of its own).

Every one of those was written for a defect that had already happened, and
none of them had ever been observed firing.  The other eight are unmeasured
here; the accounting matters, because "4 of 5" is a claim about five sites and
not about the entry point.  A safety check that has never
been seen to fire is a sentence in a bash function: `exit 2` and `exit 0` look
identical in a function that is never given the input that reaches it.

What this checker does is the only thing that can distinguish them: it breaks
the tree in the specific way each check exists to catch, and requires the
command to refuse.  Breaking `tools/gate.sh` in place is not an option -- the
gate is the thing running this -- so each case gets a throwaway worktree with
`tools/gate.sh` and nothing else mutated.

THE DEFECT THIS FOUND, WHICH IS NOT ONE OF THE FIVE
---------------------------------------------------
While measuring, case 4 exposed a sixth thing: with an EMPTY input -- a path
that is clean, so the `git diff` names nothing -- the command prints a
zero-stage answer and exits 0 without reaching checks 4 or 5, because both
live inside loops over the changed paths.  (The phrase it prints is written
out in tools/gate.sh and nowhere else: `stage_count`'s homograph ceiling
counts occurrences of git's `required:` form, and a docstring that quotes it
spends one of the eleven.)  A registry broken badly enough
to fail on any dirty file therefore answers a clean tree with a confident
`0`.  That is the same shape as every section that led here: the check cannot
report failure on the input where "no work to do" and "nothing was checked"
are the same output.

Exit codes: 0 every check fired when its defect was injected, 1 one did not,
2 the measurement itself could not be made (no git, no worktree, no gate.sh).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE_REL = "tools/gate.sh"


def _read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def _write(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


# Each case: a name, a path (relative to the worktree) whose mutation is the
# defect the check exists for, and a substitution that introduces it.  The
# mutation is applied by literal replacement and must match exactly once -- a
# case whose anchor drifted would otherwise "pass" by not mutating anything,
# which is the failure mode this whole file is about.
CASES = [
    (
        "a stage function no registry entry names",
        [
            # `stage_links` specifically: a docs/* path really forces `links`, so
            # the derivation reaches this function.  Renaming
            # `stage_probe_failure_paths` looked like the same case and was not --
            # docs/* does not force `probe-failure-paths`, so nothing on the probe
            # path ever looked at it.  A case that injects a defect the probe path
            # cannot see measures the probe, not the guard.
            (GATE_REL, "stage_links() {", "stage_links_renamed() {"),
        ],
    ),
    (
        "text-integrity's scope reads as empty",
        [
            # BOTH tuples.  The gate's awk keys on each name separately, so
            # renaming one leaves the other's globs readable and the read is not
            # empty.  The first version of this case renamed neither (it appended
            # a `frozenset() or`), the second renamed only TEXT_SUFFIXES, and
            # both times the guard correctly stayed silent because there was
            # still something to read.
            ("tools/check_text_integrity.py", "TEXT_SUFFIXES = frozenset(", "TEXT_SUFFIXES_X = frozenset("),
            ("tools/check_text_integrity.py", "TEXT_NAMES = frozenset(", "TEXT_NAMES_X = frozenset("),
        ],
    ),
    (
        "the call-site table reads as empty",
        [
            # Neuter the awk that harvests call sites, so no edge is derived.
            (GATE_REL, "    /^stage_[a-z_]+\\(\\) *\\{/ {", "    /^stage_NEVER_MATCHES_[a-z_]+\\(\\) *\\{/ {"),
        ],
    ),
    (
        "a rule names a stage no stage answers to",
        [
            (
                GATE_REL,
                '  "docs/*|links doc-paths line-refs release-tags obs-markers fences"',
                '  "docs/*|links doc-paths line-refs release-tags obs-markers fences no-such-stage"',
            ),
        ],
    ),
    (
        "a registered stage only the 'all' rules reach",
        [
            # Removing the rule is not enough: `own` is set by EITHER the scope
            # table or a derived call-site edge, and every one of the 25 stages
            # has an edge too (`stage_word_refs` gets one from the
            # `tools/check_word_refs.py` path in its own body).  To make a stage
            # all-only on this tree you have to delete both, i.e. delete the
            # stage's implementation -- a mutation that tests this case instead
            # of the guard.
            #
            # So this case is recorded as UNMEASURABLE on this tree rather than
            # deleted, because the check it covers is real and the reason it
            # cannot fire here is a property of the tree worth writing down: on
            # today's tree the condition is unreachable, which is the safe
            # direction and is not the same as "the check works".
            (GATE_REL, "THIS-CASE-IS-NOT-INJECTED", "unused"),
        ],
        "unmeasurable",
    ),
]



def _git(args, cwd, want_ok=True):
    proc = subprocess.run(
        ["git", "-C", cwd] + args, capture_output=True, text=True
    )
    if want_ok and proc.returncode != 0:
        raise RuntimeError(
            "git %s failed in %s: %s" % (" ".join(args), cwd, proc.stderr[:300])
        )
    return proc


def make_worktree(base: str) -> str:
    """A detached worktree of HEAD at base, so the gate there is the real one.

    A copy would not do: `tools/gate.sh` resolves `$REPO_ROOT` from its own
    location and shells out to `derived_edges`/`derived_text_globs`, which read
    the tree beside it.  A real worktree is the only place the command under
    test behaves as it does in the gate.
    """
    path = os.path.join(base, "wt")
    _git(["worktree", "add", "--detach", path, "HEAD"], REPO_ROOT)
    # The worktree is HEAD; the gate that RUNS is the working tree's.  Without
    # this copy the checker measures the last commit and would report green on
    # an uncommitted `tools/gate.sh` that no longer refuses anything -- measured,
    # not assumed: with `exit 2` flipped to `exit 0` in the working tree, this
    # checker still passed, because the mutation was not in the worktree.
    for rel in sorted({case[1][j][0] for case in CASES for j in range(len(case[1]))}):
        src = os.path.join(REPO_ROOT, rel)
        dst = os.path.join(path, rel)
        if os.path.isfile(src) and os.path.isdir(os.path.dirname(dst)):
            shutil.copyfile(src, dst)
    return path


def run_case(wt: str, name: str, subs) -> tuple[bool, str]:
    """Inject one defect into a fresh worktree and require exit 2."""
    # subs is [(rel, old, new), ...]: a defect can need more than one edit, and
    # a case that only half-injects it measures the half.
    originals = []
    text_by_rel = {}
    for rel, old, new in subs:
        target = os.path.join(wt, rel)
        if not os.path.isfile(target):
            for p, t in text_by_rel.items():
                _write(os.path.join(wt, p), t)
            return False, "no such file: %s" % rel
        if rel not in text_by_rel:
            text_by_rel[rel] = _read(target)
        current = text_by_rel[rel]
        if current.count(old) != 1:
            for p, t in text_by_rel.items():
                _write(os.path.join(wt, p), t)
            return False, (
                "the anchor appears %d times in %s, expected exactly 1 -- the case "
                "would not be injecting what it says it injects"
                % (current.count(old), rel)
            )
        text_by_rel[rel] = current.replace(old, new, 1)
    for rel, text in text_by_rel.items():
        originals.append((rel, _read(os.path.join(wt, rel))))
        _write(os.path.join(wt, rel), text)
    try:
        # A DIRTY path, and a path that MATCHES a rule.
        #
        # The first version of this used README.md, which no rule names, so it
        # took the "no rule matches; requiring every stage" fallback and
        # arrived at `text-integrity` without ever consulting the docs/* rule
        # the case mutates.  Four of the five cases reported rc=0 for that
        # reason -- and the measurement was right to report it: the defect those
        # cases inject really is invisible from a path no rule names.  What was
        # wrong was the case, not the guard.  docs/BOARD.md matches the docs/*
        # rule the mutated anchors live in.
        probe = "docs/BOARD.md"
        dirty = os.path.join(wt, probe)
        existed = os.path.isfile(dirty)
        marker = ""
        if existed:
            marker = _read(dirty)
            _write(dirty, marker + "\n<!-- required-for case -->\n")
        args = ["bash", os.path.join(wt, GATE_REL), "--required-for", probe]
        proc = subprocess.run(args, cwd=wt, capture_output=True, text=True)
        if existed:
            _write(dirty, marker)
        fired = proc.returncode == 2
        detail = (proc.stderr or proc.stdout).strip().splitlines()
        first = detail[0][:110] if detail else "(no output)"
        return fired, "rc=%d  %s" % (proc.returncode, first)
    finally:
        for rel, text in originals:
            _write(os.path.join(wt, rel), text)


def measure(repo_root: str) -> tuple[int, list[tuple[str, bool, str]]]:
    base = tempfile.mkdtemp(prefix="check_required_for_")
    results = []
    try:
        wt = make_worktree(base)
        for case in CASES:
            name, subs = case[0], case[1]
            if len(case) > 2 and case[2] == "unmeasurable":
                results.append((name, None, "not injectable on this tree -- see CASES"))
                continue
            try:
                ok, detail = run_case(wt, name, subs)
            except Exception as exc:  # noqa: BLE001
                ok, detail = False, "could not measure: %s: %s" % (type(exc).__name__, exc)
            results.append((name, ok, detail))
    finally:
        subprocess.run(
            ["git", "-C", repo_root, "worktree", "remove", "--force",
             os.path.join(base, "wt")],
            capture_output=True, text=True,
        )
        shutil.rmtree(base, ignore_errors=True)
    red = sum(1 for _, ok, _ in results if ok is False)
    return red, results


def self_test() -> int:
    """The checker's own control: does it notice an anchor that is not there?

    Without this, a tree that renames the function in CASES would make every
    case report "anchor appears 0 times", and a checker that treats that as
    "the check fired" would report five green lines about nothing.
    """
    failures = []
    base = tempfile.mkdtemp(prefix="check_required_for_selftest_")
    try:
        wt = make_worktree(base)
        name, subs = CASES[0]
        # A deliberately absent anchor: the case must report that it could not
        # inject, not that the guard fired.
        bogus = [(subs[0][0], "this text is not in the file", subs[0][2])]
        ok, detail = run_case(wt, name, bogus)
        if ok:
            failures.append("a missing anchor must not count as the check firing")
        if "anchor appears 0 times" not in detail:
            failures.append(
                "a missing anchor must say so, got: %s" % detail[:120]
            )
    except Exception as exc:  # noqa: BLE001
        failures.append("the self-test could not measure: %s: %s" % (type(exc).__name__, exc))
    finally:
        subprocess.run(
            ["git", "-C", REPO_ROOT, "worktree", "remove", "--force",
             os.path.join(base, "wt")],
            capture_output=True, text=True,
        )
        shutil.rmtree(base, ignore_errors=True)

    print("check_required_for --self-test")
    if failures:
        for line in failures:
            print("  FAILED  %s" % line)
        print("check_required_for: self-test FAILED (%d)" % len(failures))
        return 1
    print("  ok   a missing anchor is reported, not counted as a firing check")
    print("check_required_for: self-test OK")
    return 0


def main(argv: list[str]) -> int:
    if "--self-test" in argv:
        return self_test()

    repo_root = REPO_ROOT
    for index, token in enumerate(argv):
        if token == "--repo-root" and index + 1 < len(argv):
            repo_root = os.path.abspath(argv[index + 1])

    if not os.path.isfile(os.path.join(repo_root, GATE_REL)):
        print("check_required_for: no %s under %s" % (GATE_REL, repo_root))
        return 2
    try:
        red, results = measure(repo_root)
    except (RuntimeError, OSError) as problem:
        print("check_required_for: cannot measure: %s" % problem)
        return 2

    measured = [r for r in results if r[1] is not None]
    unmeasurable = [r for r in results if r[1] is None]
    for name, ok, detail in results:
        mark = "FIRED       " if ok is True else ("SILENT      " if ok is False else "UNMEASURABLE")
        print("  %s  %-46s  %s" % (mark, name, detail))
    print(
        "check_required_for: %d of %d measurable self-check(s) refused the tree "
        "they exist to refuse; %d did not; %d could not be injected here."
        % (len(measured) - red, len(measured), red, len(unmeasurable))
    )
    if red:
        print(
            "check_required_for: a safety check that does not fire on its own "
            "defect is a sentence in a bash function, not a guard."
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
