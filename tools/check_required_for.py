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
        [(GATE_REL, "stage_links() {", "stage_links_renamed() {")],
        "no registry entry names it",
    ),
    (
        "text-integrity's scope reads as empty",
        [
            ("tools/check_text_integrity.py", "TEXT_SUFFIXES = frozenset(", "TEXT_SUFFIXES_X = frozenset("),
            ("tools/check_text_integrity.py", "TEXT_NAMES = frozenset(", "TEXT_NAMES_X = frozenset("),
        ],
        "could not read TEXT_SUFFIXES/TEXT_NAMES",
    ),
    (
        "the call-site table reads as empty",
        [(GATE_REL, "    /^stage_[a-z_]+\\(\\) *\\{/ {", "    /^stage_NEVER_MATCHES_[a-z_]+\\(\\) *\\{/ {")],
        "read no call site out of any stage_ function",
    ),
    (
        "a rule names a stage no stage answers to (dirty input)",
        [
            (
                GATE_REL,
                '  "docs/*|links doc-paths line-refs release-tags obs-markers fences"',
                '  "docs/*|links doc-paths line-refs release-tags obs-markers fences no-such-stage"',
            )
        ],
        "a rule names stage 'no-such-stage'",
    ),
    (
        # The same defect with NOTHING to answer for.  Before §10.246 this was
        # silent: the rule-table check looped over the picked stages, so an
        # empty `git diff` skipped it and the command answered zero with exit 0.
        "a rule names a stage no stage answers to (clean input)",
        [
            (
                GATE_REL,
                '  "docs/*|links doc-paths line-refs release-tags obs-markers fences"',
                '  "docs/*|links doc-paths line-refs release-tags obs-markers fences no-such-stage"',
            )
        ],
        "a rule names stage 'no-such-stage'",
        "clean",
    ),
    (
        "a registered stage only the 'all' rules reach",
        [(GATE_REL, "THIS-CASE-IS-NOT-INJECTED", "unused")],
        "",
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


def run_case(wt: str, name: str, subs, expected: str, clean: bool = False) -> tuple[bool, str]:
    """Inject one defect into a fresh worktree and require exit 2.

    `clean=True` runs the command with NOTHING to answer for -- the probe path
    is left untouched, so `git diff` names nothing and the command has no
    changed path to report on.  That is not a variation for its own sake: it is
    the input on which the rule-table check used to be silent, and a guard that
    is silent when the answer is "nothing to do" is silent on the run most
    likely to be believed.
    """
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
        if existed and not clean:
            marker = _read(dirty)
            _write(dirty, marker + "\n<!-- required-for case -->\n")
        args = ["bash", os.path.join(wt, GATE_REL), "--required-for", probe]
        proc = subprocess.run(args, cwd=wt, capture_output=True, text=True)
        if existed and not clean:
            _write(dirty, marker)
        # Exit 2 is not enough.  Bash itself exits 2 on a syntax error, so a
        # mutation that breaks the script's syntax and a guard that refuses the
        # tree both report rc=2 -- measured: reverting a change by hand produced
        # `syntax error near unexpected token 'done'` and this checker called it
        # FIRED.  The case must also see the message the guard is supposed to
        # print, which is the only thing that distinguishes "refused on purpose"
        # from "did not run".
        both = (proc.stderr or "") + (proc.stdout or "")
        spoke = expected in both
        fired = proc.returncode == 2 and spoke
        detail_lines = [ln for ln in both.strip().splitlines() if ln.strip()]
        first = detail_lines[0][:100] if detail_lines else "(no output)"
        if proc.returncode == 2 and not spoke:
            return False, "rc=2 but NOT the guard's message: %s" % first
        return fired, "rc=%d  %s" % (proc.returncode, first)
    finally:
        for rel, text in originals:
            _write(os.path.join(wt, rel), text)


def make_unborn_repo(base: str) -> str:
    """A git repository with no commit, carrying the gate.

    `--required-for worktree` asks `git diff HEAD`, and on a repository with no
    HEAD that command fails with rc=128 while printing `fatal: ambiguous
    argument 'HEAD'`.  This fixture is the only place that failure is real: in
    this repository HEAD always exists, so no mutation of the source produces
    the input that reaches the defect.
    """
    path = os.path.join(base, "unborn")
    os.makedirs(os.path.join(path, "tools"))
    subprocess.run(["git", "-C", path, "init", "-q"], check=True, capture_output=True)
    for rel in (GATE_REL, "tools/check_text_integrity.py"):
        src = os.path.join(REPO_ROOT, rel)
        if os.path.isfile(src):
            shutil.copyfile(src, os.path.join(path, rel))
    return path


def run_main_path(repo: str, args, expected: str) -> tuple[bool, str]:
    """Run the command in a fixture and require exit 2 AND the guard's message."""
    proc = subprocess.run(
        ["bash", os.path.join(repo, GATE_REL)] + args,
        cwd=repo, capture_output=True, text=True,
    )
    both = (proc.stderr or "") + (proc.stdout or "")
    spoke = expected in both
    lines = [l for l in both.strip().splitlines() if l.strip()]
    first = lines[0][:100] if lines else "(no output)"
    if proc.returncode == 2 and not spoke:
        return False, "rc=2 but NOT the guard's message: %s" % first
    return proc.returncode == 2, "rc=%d  %s" % (proc.returncode, first)


def measure(repo_root: str) -> tuple[int, list[tuple[str, bool, str]]]:
    base = tempfile.mkdtemp(prefix="check_required_for_")
    results = []
    try:
        wt = make_worktree(base)
        for case in CASES:
            name, subs, expected = case[0], case[1], case[2]
            mode = case[3] if len(case) > 3 else None
            if mode == "unmeasurable":
                results.append((name, None, "not injectable on this tree -- see CASES"))
                continue
            try:
                ok, detail = run_case(wt, name, subs, expected, clean=(mode == "clean"))
            except Exception as exc:  # noqa: BLE001
                ok, detail = False, "could not measure: %s: %s" % (type(exc).__name__, exc)
            results.append((name, ok, detail))

        # The MAIN PATH: the branch that reads `git diff` and turns it into an
        # answer.  §10.245 and §10.246 both declared it unmeasured; this is the
        # measurement.  On a repository with no HEAD, `git diff HEAD` fails
        # with rc=128 -- and the first version of that branch ran it and
        # `ls-files` inside ONE `$( ... )`, so the substitution reported the
        # last command's status, the `if !` saw 0, and the error text was
        # parsed as a list of changed files.  It answered `26 stage(s)` and
        # exited 0 on a repository it could not read at all.
        try:
            unborn = make_unborn_repo(base)
            ok, detail = run_main_path(
                unborn,
                ["--required-for", "worktree"],
                "could not diff the worktree against HEAD",
            )
            results.append(
                ("the worktree branch refuses when git cannot diff HEAD", ok, detail)
            )
        except Exception as exc:  # noqa: BLE001
            results.append(
                (
                    "the worktree branch refuses when git cannot diff HEAD",
                    False,
                    "could not measure: %s: %s" % (type(exc).__name__, exc),
                )
            )
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
        name, subs, expected = CASES[0][0], CASES[0][1], CASES[0][2]
        # A deliberately absent anchor: the case must report that it could not
        # inject, not that the guard fired.
        bogus = [(subs[0][0], "this text is not in the file", subs[0][2])]
        ok, detail = run_case(wt, name, bogus, expected)
        if ok:
            failures.append("a missing anchor must not count as the check firing")
        if "anchor appears 0 times" not in detail:
            failures.append(
                "a missing anchor must say so, got: %s" % detail[:120]
            )

        # And the other way a case can lie: bash exits 2 on a syntax error, so
        # a mutation that breaks the script looks exactly like a guard that
        # refused.  Measured before this control existed: reverting a change by
        # hand produced `syntax error near unexpected token 'done'` and the
        # checker reported FIRED.  The expected message is the only thing that
        # tells "refused on purpose" from "did not run".
        name, subs, expected = CASES[0][0], CASES[0][1], CASES[0][2]
        broken = [(subs[0][0], subs[0][1], subs[0][1][:-1] + " {{{")]
        ok, detail = run_case(wt, name, broken, expected)
        if ok:
            failures.append(
                "a syntax error (bash also exits 2) must not count as the guard firing"
            )
        if "NOT the guard's message" not in detail:
            failures.append(
                "a syntax error must be reported as the wrong message, got: %s"
                % detail[:120]
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
