#!/usr/bin/env python3
"""Refuse a tool that mutates this repository's refs through a substitute that
is not actually a separate repository.

Why this exists
---------------
`tools/verdict_census.py` builds a stand-in tree for each checker it runs by
calling `git worktree add --detach`.  A linked worktree does **not** get its own
`refs/`: it shares `.git` with the repository it was added to, so

    git tag | xargs -r git tag -d

run inside it deleted **the real repository's ten release tags**.  It ran five
times before the damage was noticed, and the visible symptom was a gate stage
going red with *no commit touching it*:

    check_release_tags: git tag -l reported no tags

"Isolation" is a verb, and it takes an object.  `git worktree` isolates the
**working tree**.  It does not isolate **refs** and it does not isolate
**objects**.  Treating "another working tree" as "another sandbox" is measuring
exclusive behaviour against shared state.

What this guard decides
-----------------------
For every tracked file under `tools/` and `.github/` that carries a
ref-mutating spelling, it looks for the construction that file uses to obtain a
substitute repository and asks two questions:

  * does that construction give the process its own `refs/`?
  * does it give the process its own object database?

`git tag -d` / `git update-ref` / `git branch -D` need the first.  `git gc` /
`git prune` need the second, because `git gc --prune` in a repository that only
*borrows* objects will delete objects the lender still needs.

A file with no substitute construction at all is the worst case, not the
safest one: it runs the mutation in the repository itself.

What this guard does **not** decide -- read this before trusting a green
------------------------------------------------------------------------
`替身不是 git worktree` is necessary and **not sufficient**, so the guard prints
its own reach rather than implying it.  It matches a fixed table of spellings
against file text.  It cannot see:

  * a substitute path held in a variable and handed to a helper defined in
    another file;
  * a `.sh` that `exec`s a second script which does the `git worktree add`;
  * a `subprocess` argv assembled at runtime from pieces;
  * a construction nobody has written down here yet.

So its green says exactly one thing: *among the spellings in its table, at least
one isolating construction appears in each file that mutates refs*.  It does not
say the file is safe.

Exit status
-----------
0  every ref-mutating file in the scanned set pairs with an isolating
   construction (or the set is empty -- both numbers are printed, and the empty
   case says so on the line rather than passing silently)
1  at least one file mutates refs through a substitute that shares them, or
   through no substitute at all; also 1 when nothing was scanned, because a
   search space of zero is not a passing search space
2  the guard could not measure (bad --root, unreadable file)

Usage
-----
    python3 tools/ref_substitute_guard.py                 # scan this checkout
    python3 tools/ref_substitute_guard.py --root DIR      # scan DIR/tools
    python3 tools/ref_substitute_guard.py --self-test     # build a synthetic
                                                          # offender, require red

Not wired into `tools/gate.sh`: the sixteen stages answer "is this tree
correct", and this answers "are those tools correct".  Run it on demand.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Extensions worth reading.  Adding a suffix changes the denominator, which the
# success line prints, so it is a visible edit rather than a silent widening.
SUFFIXES = (".py", ".js", ".sh", ".ps1", ".mjs", ".cjs")

# Spellings that mutate refs or collect objects.  `objects_only` marks the ones
# that are dangerous specifically because the object database is borrowed.
MUTATORS: list[tuple[str, str, bool]] = [
    ("tag_delete", r"git\s+tag\s+(?:-d|--delete)\b", False),
    ("update_ref", r"git\s+update-ref\b", False),
    ("branch_delete", r"git\s+branch\s+(?:-D|--delete)\b", False),
    ("prune_tags", r"prune-tags\b", False),
    ("gc", r"git\s+gc\b", True),
    ("prune", r"git\s+prune\b", True),
]

# argv separators.  `git worktree add` and `['git', 'worktree', 'add']` are the
# same command written twice, and a guard that only knows the first spelling
# says "safe" about the second.  Matching happens against a flattened view of
# the text where these characters are whitespace, so both spellings are one
# pattern.  This is part of the guard's stated reach, not an implementation
# detail: it means a command split across a helper's arguments is still missed.
ARGV_PUNCT = re.compile(r"['\"(),\[\]=]+")

# (name, pattern, private refs, private objects, why)
CONSTRUCTIONS: list[tuple[str, str, bool, bool, str]] = [
    (
        "worktree",
        r"git\s+(?:-C\s+\S+\s+)?worktree\s+add\b",
        False,
        False,
        "a linked worktree shares .git with the repository it was added to, "
        "so refs/ and objects/ both belong to the main repository",
    ),
    (
        "git-dir option",
        r"--git-dir\s*[= ]",
        False,
        False,
        "--git-dir points the process at the named repository's own refs/ and "
        "objects/, i.e. at the real one",
    ),
    (
        "GIT_DIR env",
        r"\bGIT_DIR\s*=",
        False,
        False,
        "GIT_DIR does what --git-dir does, through the environment",
    ),
    (
        "clone --shared",
        r"clone\b[^\n]*--shared\b",
        True,
        False,
        "an alternate object store: refs/ is private but objects/ is borrowed, "
        "so gc/prune can delete objects the lender still needs",
    ),
    (
        "clone --reference",
        r"clone\b[^\n]*--reference(?:-if-able)?\b",
        True,
        False,
        "same alternates mechanism as --shared",
    ),
    (
        "clone (plain)",
        r"git\s+clone\b",
        True,
        True,
        "a plain clone has its own refs/ and its own objects/",
    ),
    (
        "init (fresh)",
        r"git\s+init\b",
        True,
        True,
        "a fresh repository shares nothing with this one",
    ),
]

# Reported when a mutating call has no substitute construction within
# SITE_WINDOW lines: the worst case, not the safest one -- the mutation runs in
# whichever repository the process finds, which is this one when run here.
NONE_MARKER = ("none", "no substitute construction appears in this file; the "
                       "mutation runs against whatever repository the process "
                       "finds, which is this one when the tool is run here")

# How close a substitute construction has to be to a mutating call to count as
# that call's substitute.  This window is load-bearing and is printed with every
# run, because it decides the pairing: too wide and an unrelated isolating
# construction elsewhere in the file launders a call that shares state.
#
# Measured, not guessed: the first version had no window at all.  It asked a
# per-FILE question ("does this file contain any isolating construction?") about
# a per-CALL-SITE hazard, and so it returned green on the real offender, whose
# `git tag | xargs git tag -d` sits in one function while an unrelated `git init`
# sandbox sits in another.  A file-level quantifier over a call-level hazard
# answers a question of the wrong arity.
SITE_WINDOW = 30

# Ways to obtain a repository that this guard does NOT recognise.  Printed with
# every run, because "the substitute is not a git worktree" is a necessary and
# not a sufficient condition, and a guard that only knows one spelling has a
# green that says one thing: that spelling was not used.
REACH_MISSED: list[str] = [
    "a substitute path held in a variable and handed to a helper defined in "
    "another file (this file never names `worktree`)",
    "a .sh that execs a second script which performs the `git worktree add`",
    "an argv assembled at runtime from pieces (`git`, `work`, `tree`)",
    "a repository obtained by `git remote add` + `fetch` into a `git init` "
    "dir -- this one IS isolated, so it is a miss with no consequence",
    "any construction nobody has written down in CONSTRUCTIONS yet",
]


def reach_lines() -> list[str]:
    lines = [
        f"ref_substitute_guard: reach -- {len(CONSTRUCTIONS)} substitute "
        f"construction(s) are in the table, each with its own refs/objects "
        f"verdict:"
    ]
    for name, _pat, prefs, pobjs, why in CONSTRUCTIONS:
        lines.append(
            f"    caught  {name:<18} refs={'private' if prefs else 'SHARED':<7}"
            f" objects={'private' if pobjs else 'SHARED':<7} -- {why}"
        )
    lines.append(
        f"ref_substitute_guard: reach -- {len(REACH_MISSED)} construction(s) "
        f"are NOT in the table; for these the guard is silent, which is not the "
        f"same as satisfied:"
    )
    for miss in REACH_MISSED:
        lines.append(f"    missed  {miss}")
    lines.append(
        "ref_substitute_guard: reach -- so a green run asserts exactly this: "
        "among the spellings in the table, each file that mutates refs also "
        "contains one whose refs/ and objects/ are private.  It does not assert "
        "the file is safe."
    )
    return lines



def tracked_files(root: str) -> list[str]:
    """Files to scan, read from git so untracked scratch cannot join the set."""
    try:
        out = subprocess.run(
            ["git", "-C", root, "ls-files", "-z", "--", "tools", ".github"],
            capture_output=True, check=True,
        ).stdout.decode("utf-8", "replace")
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    return [n for n in out.split("\0") if n and n.endswith(SUFFIXES)]


def comment_span(line: str) -> bool:
    """True if the whole line is a comment.

    The guard's input set is text, and prose about `git tag -d` reads the same
    as a call to it.  Rather than pretend otherwise, both counts are printed.
    """
    s = line.strip()
    return s.startswith("#") or s.startswith("//")


def substitute_sites(text: str) -> list[tuple[int, str, str, bool, bool]]:
    """Every construction occurrence, as (line, name, why, prefs, pobjs)."""
    flat = ARGV_PUNCT.sub(" ", text)
    out = []
    for name, pattern, prefs, pobjs, why in CONSTRUCTIONS:
        for m in re.finditer(pattern, flat):
            out.append((flat.count("\n", 0, m.start()) + 1, name, why,
                        prefs, pobjs))
    return out


def mutation_sites(text: str) -> tuple[list[tuple[int, str, bool]], list[tuple[int, str]]]:
    """Executable and comment-only mutating calls, as (line, name[, objects-only])."""
    exe: list[tuple[int, str, bool]] = []
    com: list[tuple[int, str]] = []
    for i, line in enumerate(text.splitlines(), start=1):
        flat = ARGV_PUNCT.sub(" ", line)
        for name, pattern, needs_objects in MUTATORS:
            if re.search(pattern, flat):
                (com if comment_span(line) else exe).append(
                    (i, name) if comment_span(line) else (i, name, needs_objects)
                )
                break
    return exe, com


def pair(sites, line: int) -> tuple[str, tuple | None, int | None]:
    """Classify a call site against every construction in the file.

    Three answers, not two, because the real offender needs the third:

      near   an isolating-or-not construction within SITE_WINDOW lines -- the
             text supports a verdict about this call
      far    the file contains a construction, but none near this call.  The
             call is a *row in a table* and the substitute is built by a helper
             elsewhere; which repository the command actually runs in is a
             property of the call graph, and the text does not carry it
      none   the file offers no substitute at all, so the call runs against
             whichever repository the process finds -- this one, when run here

    Measured, not guessed: on the real offender
    (`b500162:tools/verdict_census.py`) `git tag -d` is at line 116, a row of a
    command table, and `git worktree add` is at line 206 in the helper that
    builds the substitute.  A per-file quantifier called that file OK; a
    30-line window calls it `none`.  Both are red for the wrong reason, and
    "the right reason" is not a distance -- it is "the text does not say".
    """
    best = None
    for site in sites:
        d = abs(site[0] - line)
        if best is None or d < best[0] or (d == best[0] and site[1] < best[1][1]):
            best = (d, site)
    if best is None:
        return "none", None, None
    if best[0] > SITE_WINDOW:
        return "far", best[1], best[0]
    return "near", best[1], best[0]


def scan(root: str) -> tuple[int, list[str], list[str], list[str]]:
    files = tracked_files(root)
    lines: list[str] = []
    offenders: list[str] = []
    comment_only: list[str] = []
    unresolved: list[str] = []
    sites_judged = 0
    hits = 0
    for rel in files:
        try:
            with open(os.path.join(root, rel), "r", encoding="utf-8",
                      errors="replace") as fh:
                text = fh.read()
        except OSError as exc:
            return 2, [f"cannot read {rel}: {exc}"], [], []
        cons = substitute_sites(text)
        exe, com = mutation_sites(text)
        if not exe and not com:
            continue
        hits += 1
        if not exe:
            comment_only.append(rel)
            lines.append(
                f"  {rel}: {', '.join(sorted({n for _l, n in com}))} in a "
                f"comment only -- no executable mutation, so this file is "
                f"counted but not judged"
            )
            continue
        bad_here = False
        for line_no, name, needs_objects in exe:
            sites_judged += 1
            kind, site, dist = pair(cons, line_no)
            if kind == "none":
                shown, why, ok = NONE_MARKER[0], NONE_MARKER[1], False
            elif kind == "far":
                _l, cname, _why, _a, _b = site
                shown = f"{cname} @line {site[0]} ({dist} line(s) away)"
                why = (
                    f"the file does contain {cname}, but {dist} line(s) away, "
                    f"outside the {SITE_WINDOW}-line window; whether this call "
                    f"runs inside it depends on the call graph, which the text "
                    f"does not carry.  The guard cannot answer this, and a "
                    f"guard that cannot answer must not answer `safe`"
                )
                ok = None
            else:
                _l, cname, cwhy, prefs, pobjs = site
                shown = f"{cname} @line {site[0]} ({dist} line(s) away)"
                why = cwhy
                ok = prefs and (pobjs or not needs_objects)
            verdict = ("OK" if ok else "SHARES STATE") if ok is not None else "UNRESOLVED"
            lines.append(
                f"  {rel}:{line_no}: mutates {name}"
                f"{' (objects only)' if needs_objects else ''}; nearest "
                f"substitute = {shown} -> {verdict}"
            )
            if ok is False:
                bad_here = True
                lines.append(f"      reason: {why}")
            elif ok is None:
                lines.append(f"      reason: {why}")
        if bad_here:
            offenders.append(rel)
        elif any(pair(cons, ln)[0] == "far" for ln, _n, _o in exe):
            unresolved.append(rel)
    lines.insert(0, (
        f"ref_substitute_guard: {len(files)} file(s) scanned under {root}/tools "
        f"and {root}/.github; {hits} carry a ref-mutating spelling "
        f"({len(comment_only)} in a comment only); {sites_judged} call site(s) "
        f"judged, each against the construction nearest to it within "
        f"{SITE_WINDOW} line(s), and {len(unresolved)} file(s) where no "
        f"construction is near enough to answer -- the first number is the "
        f"search space and it is printed because a search space of zero would "
        f"make every verdict below vacuous"
    ))
    if not files:
        lines.append(
            "ref_substitute_guard: the search space is empty, so no pairing "
            "below can be judged and a guard with nothing to look at is not a "
            "passing guard"
        )
        return 1, lines, ["<no tracked file under tools/ or .github/>"], []
    if not hits:
        lines.append(
            "ref_substitute_guard: no file in the search space mutates refs "
            "through any spelling in the table, so this run judges nothing -- "
            "the count above is how you know that, and only a synthetic "
            "offender (--self-test) can show the guard still fires"
        )
    for rel in unresolved:
        lines.append(
            f"ref_substitute_guard: UNRESOLVED {rel}: it mutates refs and it "
            f"does build a substitute, but not near enough for this guard to "
            f"say which one the call runs in.  This is a third answer, not a "
            f"softer `safe`: rc=2 means the guard could not measure, and an "
            f"unanswerable question is not a passing one"
        )
    if offenders:
        return 1, lines, offenders, comment_only
    if unresolved:
        # NOT `unresolved`: this slot is the offender list and main() prints it
        # under `FAILED`.  rc=2 means the guard could not measure, so naming a
        # file here would dress an "I cannot judge this" as a verdict -- the
        # same shape as the self-test that went red for the wrong reason.
        return 2, lines, [], comment_only
    return 0, lines, offenders, comment_only


def self_test() -> int:
    """Two synthetic entries in a temp repo; require red, then require green.

    The assertions are on the **reasons**, not only on the exit status.  Each
    control exists because a version of this guard got it wrong:

      offender_worktree.py   the argv-list spelling `['git', 'worktree', 'add']`
                             -- v1 did not match it at all and went red as
                             `none`: red for a different reason than the control
                             demonstrates.  Must be SHARES STATE via `worktree`.
      far_init_worktree.py   an unrelated `git init` 60 lines above a
                             `worktree add` + `git tag -d` pair -- v1 judged it
                             OK, because it asked a per-FILE question about a
                             per-CALL-SITE hazard.  The far construction must
                             not launder the near one.
      unresolved_table.py    a helper that builds a worktree, and a command
                             TABLE 62 lines below whose row is `git tag -d` --
                             the real offender's shape.  Neither a file
                             quantifier nor any window can decide which
                             repository that row runs in, so the answer must be
                             UNRESOLVED (rc=2), never `safe`.
      safe_plain_clone.py    a plain clone, which must stay green, or the guard
                             refuses every mutator rather than the unisolated
                             ones.
    """
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "tools"))

        def put(name: str, body: str) -> str:
            path = os.path.join(tmp, "tools", name)
            with open(path, "w") as fh:
                fh.write(body)
            return path

        owt = put(
            "offender_worktree.py",
            "import subprocess\n"
            "def build(wt):\n"
            "    subprocess.run(['git', 'worktree', 'add', '--detach', wt])\n"
            "def wipe(cwd):\n"
            "    subprocess.run('git tag | xargs -r git tag -d', shell=True, cwd=cwd)\n"
        )
        far = put(
            "far_init_worktree.py",
            "import subprocess\n"
            "def sandbox():\n"
            "    subprocess.run(['git', 'init', '-q'])\n"
            + "\n" * 60
            + "def build(wt):\n"
            "    subprocess.run(['git', 'worktree', 'add', '--detach', wt])\n"
            "def wipe(cwd):\n"
            "    subprocess.run(['git', 'tag', '-d', 'v0.1.0'], cwd=cwd)\n"
        )
        unres = put(
            "unresolved_table.py",
            "import subprocess\n"
            "def build(wt):\n"
            "    subprocess.run(['git', 'worktree', 'add', '--detach', wt])\n"
            + "\n" * 60
            + "MUTATIONS = {\n"
            "    'check_release_tags.py': 'git tag | xargs -r git tag -d',\n"
            "}\n"
        )
        put(
            "safe_plain_clone.py",
            "import subprocess\n"
            "def build(wt):\n"
            "    subprocess.run(['git', 'clone', '-q', '.', wt])\n"
            "def wipe(cwd):\n"
            "    subprocess.run(['git', 'tag', '-d', 'v0.1.0'], cwd=cwd)\n"
        )
        subprocess.run(["git", "-C", tmp, "init", "-q"], check=True)
        subprocess.run(["git", "-C", tmp, "add", "-A"], check=True)

        def run() -> tuple[int, str, list[str]]:
            rc, lines, offenders, _ = scan(tmp)
            for line in lines:
                print(line)
            print(f"ref_substitute_guard: self-test rc={rc} offenders={offenders}")
            return rc, "\n".join(lines), offenders

        def want(cond: bool, msg: str) -> None:
            nonlocal bad
            if not cond:
                bad = 1
                print(f"ref_substitute_guard: self-test FAILED -- {msg}",
                      file=sys.stderr)

        rc, text, offenders = run()
        want(rc == 1, "the two worktree offenders did not turn the guard red")
        want(offenders == ["tools/far_init_worktree.py",
                           "tools/offender_worktree.py"],
             f"the offending set is {offenders}, which is not the two worktree "
             f"entries")
        want("nearest substitute = worktree @line 3 (2 line(s) away) -> "
             "SHARES STATE" in text,
             "no entry is flagged for the worktree reason at the argv-list "
             "line; red for the wrong reason is not red")
        want("nearest substitute = worktree @line 65 (2 line(s) away) -> "
             "SHARES STATE" in text,
             "the far `git init` laundered the nearby `git worktree add` into "
             "OK, which is the per-file/per-call-site arity bug")
        want("unresolved_table.py:65: mutates tag_delete" in text
             and "-> UNRESOLVED" in text,
             "the command-table row is not reported as UNRESOLVED; a row whose "
             "substitute is built in a helper cannot be judged from the text")
        want("nearest substitute = clone (plain) @line 3 (2 line(s) away) -> OK"
             in text,
             "the plain-clone entry is not reported as isolated, so the guard "
             "refuses every mutator rather than the unisolated ones")

        os.remove(far)
        os.remove(owt)
        subprocess.run(["git", "-C", tmp, "add", "-A"], check=True)
        rc2, text2, off2 = run()
        want(rc2 == 2 and off2 == [],
             f"with both offenders removed the verdict is rc={rc2} offenders="
             f"{off2}; rc must be 2 (could not measure) and the slot that main() "
             f"prints under FAILED must be EMPTY, because naming a file there "
             f"dresses an unanswerable question as a verdict")
        want("UNRESOLVED" in text2,
             "the unresolvable row was not reported as UNRESOLVED, so rc=2 has "
             "nothing in the output explaining why it is not 0")
        want("SHARES STATE" not in text2,
             "a SHARES STATE verdict survived removing the offenders")

        os.remove(unres)
        subprocess.run(["git", "-C", tmp, "add", "-A"], check=True)
        rc3, text3, _ = run()
        want(rc3 == 0 and "SHARES STATE" not in text3 and "UNRESOLVED" not in text3,
             f"with every offender removed the guard is rc={rc3}, so the red "
             f"was not about the offenders")

        if bad == 0:
            print("ref_substitute_guard: self-test ok -- the argv-list worktree "
                  "offender is red BY THE WORKTREE REASON, the unrelated "
                  "far-away `git init` does not launder it, the command-table "
                  "row whose substitute lives in a helper is UNRESOLVED (rc=2) "
                  "rather than safe, the plain clone is green, and removing "
                  "every offender restores green")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=REPO_ROOT)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not os.path.isdir(os.path.join(args.root, "tools")):
        print(f"ref_substitute_guard: {args.root}/tools is not a directory; an "
              f"unmeasurable root is not an empty one", file=sys.stderr)
        return 2
    # Exit 1 means "some file mutates refs through a substitute that shares
    # them" -- a verdict.  An unhandled exception also exits 1, so without this
    # wrapper a crash in the guard is read as a finding about the repository.
    # The contract says 2 is "could not measure"; the guard has to honour its
    # own contract or its verdicts are not distinguishable from its failures.
    try:
        rc, lines, offenders, _ = scan(args.root)
    except Exception as exc:  # noqa: BLE001 -- see above
        print(f"ref_substitute_guard: could not measure {args.root}: "
              f"{type(exc).__name__}: {exc}", file=sys.stderr)
        print("ref_substitute_guard: rc=2 -- this is the guard failing, NOT a "
              "verdict about any file", file=sys.stderr)
        return 2
    for line in lines:
        print(line)
    if offenders:
        print("ref_substitute_guard: FAILED", file=sys.stderr)
        for rel in offenders:
            print(f"  - {rel}", file=sys.stderr)
    elif rc == 2:
        print("ref_substitute_guard: rc=2 -- no file was judged unsafe, and "
              "the guard still cannot call this a pass, because at least one "
              "call site could not be resolved from the text", file=sys.stderr)
    else:
        print("ref_substitute_guard: no file mutates refs through a substitute "
              "that shares them")
    for line in reach_lines():
        print(line)
    return rc


if __name__ == "__main__":
    sys.exit(main())
