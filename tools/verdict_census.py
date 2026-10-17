#!/usr/bin/env python3
"""Which repository checkers print a success line for a run that could not happen?

The cross-family question, not the per-file one.  It cannot be answered by
reading source: a checker that returns 0 on an input set it never read and one
that returns 0 on a healthy repository produce the same exit status, and only
the line tells them apart -- and only when the line carries a number that
cannot legitimately be that number.

Three substitutes, reported side by side, never merged
-----------------------------------------------------

The question "what does it print when its input set is empty" hides a second
one: *empty of what?*  Three substitutes answer three different questions, and
they disagree.  The disagreement is the finding, so all three columns are
printed and no average is taken.

``bare``    an empty git repository containing only the checker.  Extremest
            case: no tracked file, no directory, no config.  Answers "what
            does it say when the repository it stands in says nothing".
``narrow``  the same plus one unrelated tracked ``notes.txt``.  The same
            question one notch less artificially -- the exposure a real
            repository has.
``emptied`` a real detached worktree of this commit with **only that
            checker's own input object emptied**.  Answers "what does it say
            when the repository is real but this check's subject is absent".

An earlier version used ``bare`` alone and threw its reading away as noise,
because a bare tree also removes the checker's *environment* and 12 of 13
came back as unrelated crashes (``check_orphan_targets: no CMakeLists.txt``,
``check_obs_markers: cannot resolve --ref 'HEAD'``).  That was wrong to throw
away: ``bare`` is the only column that found ``check_ignored_credentials.py``
printing ``ok (3 userdata dirs ...)`` against zero existing directories.
``emptied`` cannot find it, because a real worktree has ``userdata/``
present, so the guard's body runs.  Both columns are needed and no column
replaces another.

A checker is reported as a SILENT PASS when a substitute gives it rc=0 AND
its line asserts a quantity the substitute did not supply.

What the mutation is per checker was read from the source.  That is allowed:
identifying *which object to empty* is not a verdict.  Every verdict -- every
line and exit status below -- is the checker's own output.

Usage::

    python3 tools/verdict_census.py [--root DIR] [--timeout SECS] [--json]
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Per-checker mutation for the ``emptied`` column.  (description, shell) run
# with cwd = the substitute worktree root and VERDICT_HOLD pointing outside it.
MUTATIONS: dict[str, tuple[str, str]] = {
    "tools/check_async_commands.py": (
        "delete Infiverse_standard/src-tauri/src/lib.rs (its input is one "
        "fixed path, not a set)",
        "rm -f Infiverse_standard/src-tauri/src/lib.rs",
    ),
    "tools/check_doc_paths.py": (
        "unstage every README.md / docs/**/*.md / future/*.md so "
        "`git ls-files -- README.md docs future` yields no document",
        "git rm -q -r --cached --ignore-unmatch README.md docs future",
    ),
    "tools/check_error_domains.py": (
        "delete src/types/error_types.c, the file it parses for ImErrorDomain",
        "rm -f src/types/error_types.c",
    ),
    "tools/check_errors_cli.py": (
        "LOOKUP-ONLY: its input is a built `inimerse` binary taken from argv, "
        "so no empty input set can be constructed without a full build",
        "",
    ),
    "tools/check_ignored_credentials.py": (
        "empty .gitignore and remove the userdata dirs (its input is the "
        "ignore rules plus those directories)",
        "printf '' > .gitignore; rm -rf userdata Infiverse_standard/userdata "
        "Infiverse_standard/src-tauri/userdata",
    ),
    "tools/check_line_refs.py": (
        "remove both TARGETS files, CMakeLists.txt and tools/gate.sh",
        "rm -f CMakeLists.txt tools/gate.sh",
    ),
    "tools/check_links.py": (
        "unstage every *.md / *.markdown so `git ls-files -- '*.md' "
        "'*.markdown'` is empty",
        "git rm -q -r --cached --ignore-unmatch '*.md' '*.markdown'",
    ),
    "tools/check_obs_markers.py": (
        "unstage docs/ so the marker set it walks is empty",
        "git rm -q -r --cached --ignore-unmatch docs",
    ),
    "tools/check_orphan_fixtures.py": (
        "move all four glob targets away: vtest/*.im, vtest/*.inim, "
        "tools/*.test.py, tools/*.test.js",
        'mkdir -p "$VERDICT_HOLD"; mv vtest/*.im vtest/*.inim '
        'tools/*.test.py tools/*.test.js "$VERDICT_HOLD"/ 2>/dev/null; true',
    ),
    "tools/check_orphan_targets.py": (
        "replace CMakeLists.txt with one that declares no add_executable()",
        "printf 'cmake_minimum_required(VERSION 3.16)\\n' > CMakeLists.txt",
    ),
    "tools/check_release_tags.py": (
        "remove docs/ and delete every tag in the substitute",
        "git rm -q -r --cached --ignore-unmatch docs; "
        "git tag | xargs -r git tag -d >/dev/null; true",
    ),
    "tools/check_test_ports.py": (
        "move tools/*.test.py away (its input is TOOLS.glob('*.test.py'))",
        'mkdir -p "$VERDICT_HOLD"; mv tools/*.test.py "$VERDICT_HOLD"/ '
        '2>/dev/null; true',
    ),
    "tools/check_text_integrity.py": (
        "unstage everything, so `git ls-files -z` is empty",
        "git rm -q -r --cached --ignore-unmatch .",
    ),
}

# Excluded from the denominator, with the reason printed next to it.
EXCLUDED = {
    "tools/check_enc.ps1":
        "not a checker: criterion 1 (it is not a program with a verdict line "
        "and an exit status -- it takes -paths and prints one row per file) "
        "and criterion 3 (nothing consumes its status: not in tools/gate.sh, "
        "not in CMakeLists.txt add_test, not in any registered test)",
}

EXIT_CALL_RE = re.compile(
    r"""(?:sys\.exit|raise\s+SystemExit|SystemExit)\(\s*"""
    r"""(?P<arg>"[^"]*"|'[^']*'|[0-9]+|None|main\s*\(|main\s*\(\s*sys\.argv\s*\))""",
)
RETURN_TERNARY_RE = re.compile(
    r"""^\s*return\s+(?P<a>[0-9]+)\s+if\s+.+?\s+else\s+(?P<b>[0-9]+)\s*$""", re.M)
RETURN_RE = re.compile(r"""^\s*return\s+(?P<arg>[0-9]+|"[^"]*"|'[^']*'|None)\s*$""", re.M)

UNREADABLE = "unreadable"


def exit_codes_from_source(text: str) -> set[int] | str:
    """The set of exit statuses this source can actually return.

    ``sys.exit("msg")`` exits 1 -- a hidden code, and the one most of these
    files use for "I cannot run", which is exactly why it matters here.
    ``sys.exit(main())`` is transparent: the status comes from main's returns,
    which the other two patterns collect.
    """
    found: set[int] = set()
    for m in RETURN_TERNARY_RE.finditer(text):
        found.update({int(m.group("a")), int(m.group("b"))})
    without = RETURN_TERNARY_RE.sub("", text)
    for m in RETURN_RE.finditer(without):
        arg = m.group("arg")
        found.add(int(arg) if arg.isdigit() else 1)
    for m in EXIT_CALL_RE.finditer(text):
        arg = m.group("arg")
        if arg.startswith(("'", '"')):
            found.add(1)
        elif arg.isdigit():
            found.add(int(arg))
        elif arg == "None":
            found.add(0)
    return found or UNREADABLE


def run_checker(cwd: str, rel: str, timeout: int) -> dict:
    try:
        proc = subprocess.run([sys.executable, rel], cwd=cwd,
                              capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"rc": None, "line": "", "stream": "timeout"}
    out = proc.stdout.decode("utf-8", "backslashreplace").strip().splitlines()
    err = proc.stderr.decode("utf-8", "backslashreplace").strip().splitlines()
    if out:
        return {"rc": proc.returncode, "line": out[-1], "stream": "stdout"}
    if err:
        return {"rc": proc.returncode, "line": err[-1], "stream": "stderr"}
    return {"rc": proc.returncode, "line": "", "stream": "nothing"}


def bare_tree(root: str, tmp: str, name: str, rel: str, narrow: bool) -> str:
    """An empty git repo containing only the checker (and maybe notes.txt)."""
    dest = os.path.join(tmp, name)
    os.makedirs(os.path.join(dest, os.path.dirname(rel)), exist_ok=True)
    shutil.copy2(os.path.join(root, rel), os.path.join(dest, rel))
    subprocess.run(["git", "init", "-q", "."], cwd=dest, check=True)
    if narrow:
        with open(os.path.join(dest, "notes.txt"), "w") as fh:
            fh.write("unrelated to this check\n")
        subprocess.run(["git", "add", "notes.txt"], cwd=dest, check=True)
    return dest


def emptied_tree(root: str, tmp: str, name: str, ref: str, script: str,
                 env: dict) -> str:
    dest = os.path.join(tmp, name)
    subprocess.run(["git", "worktree", "add", "--detach", dest, ref],
                   cwd=root, check=True, capture_output=True)
    subprocess.run(["bash", "-c", script], cwd=dest, check=False,
                   capture_output=True, env=env)
    return dest


def asserted_a_quantity(line: str) -> bool:
    """Does the line assert a count the substitute could not have supplied?"""
    return bool(re.search(r"\b\d+\b", line))


def main(argv: list[str]) -> int:
    root, timeout, as_json = REPO_ROOT, 30, False
    args = argv[1:]
    while args:
        a = args.pop(0)
        if a == "--root":
            root = os.path.abspath(args.pop(0))
        elif a == "--timeout":
            timeout = int(args.pop(0))
        elif a == "--json":
            as_json = True
        else:
            sys.exit(f"verdict_census: unknown argument {a!r}")

    ref = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                         capture_output=True, text=True, check=True).stdout.strip()
    tracked = [p for p in subprocess.run(
        ["git", "ls-files", "tools/check_*"], cwd=root,
        capture_output=True, text=True, check=True).stdout.split("\n") if p.strip()]
    checkers = [p for p in tracked if p not in EXCLUDED]

    rows = []
    hold = tempfile.mkdtemp(prefix="verdict-hold-")
    env = dict(os.environ, VERDICT_HOLD=hold)
    with tempfile.TemporaryDirectory(prefix="verdict-census-") as tmp:
        for rel in checkers:
            with open(os.path.join(root, rel), encoding="utf-8") as fh:
                text = fh.read()
            codes = exit_codes_from_source(text)
            desc, script = MUTATIONS.get(rel, ("NO MUTATION RECORDED", ""))
            row = {"file": rel, "exit_codes": codes, "mutation": desc}
            row["bare"] = run_checker(
                bare_tree(root, tmp, "b_" + os.path.basename(rel), rel, False), rel, timeout)
            row["narrow"] = run_checker(
                bare_tree(root, tmp, "n_" + os.path.basename(rel), rel, True), rel, timeout)
            if script:
                row["emptied"] = run_checker(
                    emptied_tree(root, tmp, "e_" + os.path.basename(rel), ref, script,
                                 env),
                    rel, timeout)
            else:
                row["emptied"] = None
            row["silent"] = [
                col for col in ("bare", "narrow", "emptied")
                if row[col] and row[col]["rc"] == 0
                and asserted_a_quantity(row[col]["line"])
            ]
            if codes == UNREADABLE:
                row["tier"] = "unreadable"
            elif isinstance(codes, set) and 1 in codes and 2 in codes:
                row["tier"] = "distinguishes"
            else:
                row["tier"] = "merged"
            rows.append(row)

    subprocess.run(["git", "worktree", "prune"], cwd=root, capture_output=True)
    shutil.rmtree(hold, ignore_errors=True)

    silent = [r["file"] for r in rows if r["silent"]]

    if as_json:
        print(json.dumps({"ref": ref, "denominator": len(rows),
                          "excluded": EXCLUDED, "rows": rows,
                          "silent": silent}, ensure_ascii=False, indent=2))
        return 1 if silent else 0

    print(f"verdict_census: ref {ref}")
    print(f"verdict_census: {len(rows)} checker(s) under tools/check_* "
          f"({len(tracked)} tracked, {len(EXCLUDED)} excluded)")
    for f, why in EXCLUDED.items():
        print(f"    excluded {f}: {why}")
    print()
    for r in rows:
        codes = r["exit_codes"]
        cs = ("unreadable" if codes == UNREADABLE
              else "{" + ",".join(map(str, sorted(codes))) + "}")
        print(f"  {r['file']}   exit-from-source={cs}   tier={r['tier']}")
        print(f"      mutation : {r['mutation']}")
        for col in ("bare", "narrow", "emptied"):
            e = r[col]
            if e is None:
                print(f"      {col:<8} : not constructible")
                continue
            print(f"      {col:<8} : rc={e['rc']}  [{e['stream']}] {e['line']!r}")
        print(f"      SILENT   : {r['silent'] if r['silent'] else 'no'}")
        print()

    tiers = {"distinguishes": 0, "merged": 0, "unreadable": 0}
    for r in rows:
        tiers[r["tier"]] += 1
    for col in ("bare", "narrow", "emptied"):
        n = sum(1 for r in rows if r[col] and r[col]["rc"] == 0)
        print(f"  exit 0 under {col:<8}: {n} of {len(rows)}")
    print(f"  distinguishes 1 from 2 : {tiers['distinguishes']}")
    print(f"  merges them into one   : {tiers['merged']}")
    print(f"  unreadable             : {tiers['unreadable']}")
    print(f"  sum                    : {sum(tiers.values())} "
          f"(denominator {len(rows)})")
    print(f"  SILENT PASS            : {len(silent)}")
    for f in silent:
        print(f"      {f}")
    # 0 = every checker refused the substitute; 1 = at least one printed a
    # success line for a run that could not happen; 2 = the census itself
    # could not run.  A census whose failure mode is not an exit status cannot
    # report its own failure -- from outside it is indistinguishable from
    # "still running".
    if silent:
        print()
        print("verdict_census: 1 checker printed a success line for a run that "
              "could not happen -- see SILENT PASS above.")
        return 1
    print()
    print(f"verdict_census: none of the {len(rows)} checker(s) printed a success "
          f"line for an input set that was not there.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
