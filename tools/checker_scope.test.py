#!/usr/bin/env python3
"""Scope control for tools/check_line_refs.py: what its sweep does and does not see.

The readings line now says how much it swept ("swept=105 file(s) under
docs/**/*.md", "targets=2", "writings=...").  That claim is worth exactly as
much as its two directions, so this measures both:

  * a citation written OUTSIDE docs/ -- in a comment in CMakeLists.txt, or in
    the checker's own source -- must leave every number alone.  If the sweep
    were "the repository", one of these would move something.
  * a citation written INSIDE docs/ must move one.  Otherwise swept= is
    decoration, and a document could be edited without the readings noticing.

Every run happens in a throwaway git worktree at HEAD, so the tree under test
is never modified and a red here cannot be caused by someone else's edit
landing mid-run.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = "tools/check_line_refs.py"
READINGS = re.compile(
    r"readings -- explicit (\d+) \(base \d+\), unanchored (\d+) \(base \d+\), "
    r"mis-labelled (\d+) \(base \d+\)\.(.*)"
)
SWEPT = re.compile(r"swept=(\d+) file\(s\) under docs/\*\*/\*\.md")
TARGETS = re.compile(r"targets=(\d+)")
WRITINGS = re.compile(r"writings=([a-z+u,\-]+)")


def refuse(why: str) -> "NoReturn":
    print(f"scope-control: REFUSED|{why}", file=sys.stderr)
    print("scope-control: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


def readings(text: str) -> dict | None:
    for line in text.splitlines():
        m = READINGS.search(line)
        if not m:
            continue
        swept, targets, writings = SWEPT.search(m.group(4)), TARGETS.search(m.group(4)), WRITINGS.search(m.group(4))
        if not (swept and targets and writings):
            return None
        return {
            "explicit": int(m.group(1)),
            "unanchored": int(m.group(2)),
            "mis-labelled": int(m.group(3)),
            "swept": int(swept.group(1)),
            "targets": int(targets.group(1)),
            "writings": writings.group(1),
        }
    return None


def run(tree: Path) -> tuple[int, str]:
    proc = subprocess.run([sys.executable, str(tree / CHECKER)], cwd=str(tree), capture_output=True, text=True)
    return proc.returncode, proc.stdout


def append(tree: Path, rel: str, line: str) -> None:
    path = tree / rel
    path.write_text(path.read_text(encoding="utf-8") + line, encoding="utf-8")


def main() -> int:
    argparse.ArgumentParser().parse_args()
    if shutil.which("git") is None:
        refuse("git is not on PATH")
    top = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    if top.returncode != 0:
        refuse(f"{ROOT} is not inside a git worktree, so there is no tree to take a copy of")

    holder = tempfile.mkdtemp(prefix="checker-scope-")
    tree = Path(holder) / "tree"
    added = subprocess.run(
        ["git", "-C", str(ROOT), "worktree", "add", "--detach", str(tree), "HEAD"],
        capture_output=True, text=True,
    )
    if added.returncode != 0:
        tail = added.stderr.strip().splitlines()
        refuse("could not create a throwaway worktree" + (": " + tail[-1] if tail else ""))
    if not (tree / CHECKER).exists():
        subprocess.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(tree)], capture_output=True)
        refuse(f"the throwaway worktree at HEAD has no {CHECKER}")

    # The worktree is at HEAD; the tool under test is the one in the tree being
    # measured, which need not be committed yet.  Copy it over so a run says
    # what the working checker does, and the documents stay at HEAD.
    shutil.copy2(ROOT / CHECKER, tree / CHECKER)

    cases: list[tuple[str, bool, str]] = []
    try:
        rc, out = run(tree)
        base = readings(out)
        if base is None:
            refuse(f"the checker printed no readings line it could be read from (rc={rc})")
        print(
            "scope-control: base readings -- "
            + ", ".join(f"{k} {v}" for k, v in base.items())
            + f"; rc={rc}"
        )

        append(tree, "docs/BOARD.md", "\nScope control: CMakeLists.txt:1 is named from a document.\n")
        rc_in, out_in = run(tree)
        inside = readings(out_in)
        ok_in = inside is not None and inside["explicit"] == base["explicit"] + 1 and rc_in == 1
        cases.append((
            "in-scope-docs",
            ok_in,
            f"explicit {base['explicit']} -> {inside['explicit'] if inside else 'none'}, rc={rc_in} (want {base['explicit'] + 1} and rc=1)",
        ))
        subprocess.run(["git", "-C", str(tree), "checkout", "--", "docs/BOARD.md"], check=True)

        append(tree, "CMakeLists.txt", "\n# scope control: src/compiler/compiler.c:3116 is named from code, not from a document.\n")
        rc_c, out_c = run(tree)
        same_c = readings(out_c)
        cases.append((
            "out-of-scope-CMakeLists",
            same_c == base and rc_c == 0,
            f"readings {'unchanged' if same_c == base else 'MOVED'} (explicit {same_c['explicit'] if same_c else 'none'}), rc={rc_c} (want unchanged and rc=0)",
        ))
        subprocess.run(["git", "-C", str(tree), "checkout", "--", "CMakeLists.txt"], check=True)

        append(tree, CHECKER, "\n# scope control: tools/gate.sh:120 is named inside the checker itself.\n")
        rc_k, out_k = run(tree)
        same_k = readings(out_k)
        cases.append((
            "out-of-scope-checker",
            same_k == base and rc_k == 0,
            f"readings {'unchanged' if same_k == base else 'MOVED'} (explicit {same_k['explicit'] if same_k else 'none'}), rc={rc_k} (want unchanged and rc=0)",
        ))
    finally:
        subprocess.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(tree)], capture_output=True)
        shutil.rmtree(holder, ignore_errors=True)

    red = 0
    for name, ok, detail in cases:
        if not ok:
            red += 1
        print(f"scope-control: {name} -- {'ok' if ok else 'RED'}: {detail}")
    print(f"scope-control: cases={len(cases)} red={red} green={len(cases) - red} -- " + (", ".join(n for n, ok, _ in cases if not ok) or "none"))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main())
