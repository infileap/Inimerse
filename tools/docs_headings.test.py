#!/usr/bin/env python3
"""Controls for tools/check_docs_headings.py: a deletion must be the thing that reddens.

The interesting property is not "it counts headings" but "it can see a removal
that every ceiling in the repository is blind to".  So the cases are the three
shapes of a change:

  * a section deleted from a file that survives  -> removed > 0, red;
  * a whole document deleted                     -> removed > 0, red, and the
                                                    count is that file's pinned
                                                    number, not 1;
  * a new section added                          -> added > 0, still green,
                                                    because growth is what the
                                                    ceilings already bound.

Each case restores the tree before the next one, and every run happens in a
throwaway git worktree at HEAD: the tree under test is never modified.
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
TOOL = "tools/check_docs_headings.py"
PIN = "tools/docs_headings.pin.json"
HEADINGS = re.compile(r"^HEADINGS\|base=(\d+)\|now=(\d+)\|removed=(\d+)\|added=(\d+)$", re.M)


def refuse(why: str) -> "NoReturn":
    print(f"docs-headings: REFUSED|{why}", file=sys.stderr)
    print("docs-headings: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


def read(text: str) -> dict | None:
    m = HEADINGS.search(text)
    if not m:
        return None
    return {"base": int(m.group(1)), "now": int(m.group(2)), "removed": int(m.group(3)), "added": int(m.group(4))}


def run(tree: Path) -> tuple[int, str, str]:
    proc = subprocess.run([sys.executable, str(tree / TOOL)], cwd=str(tree), capture_output=True, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def main() -> int:
    argparse.ArgumentParser().parse_args()
    if shutil.which("git") is None:
        refuse("git is not on PATH")
    if subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"], capture_output=True).returncode != 0:
        refuse(f"{ROOT} is not inside a git worktree, so there is no tree to take a copy of")

    holder = tempfile.mkdtemp(prefix="docs-headings-")
    tree = Path(holder) / "tree"
    added = subprocess.run(["git", "-C", str(ROOT), "worktree", "add", "--detach", str(tree), "HEAD"],
                           capture_output=True, text=True)
    if added.returncode != 0:
        tail = added.stderr.strip().splitlines()
        refuse("could not create a throwaway worktree" + (": " + tail[-1] if tail else ""))
    try:
        # The tool under test is the working copy -- a run should say what the
        # version being committed does.  The PIN is not: it is a record of the
        # committed documents, so it is read from HEAD along with them.  A pin
        # that is newer than HEAD would measure a tree that does not exist yet.
        if not (ROOT / TOOL).exists():
            refuse(f"{TOOL} is missing from the tree under test")
        shutil.copy2(ROOT / TOOL, tree / TOOL)
        if not (tree / PIN).exists():
            refuse(f"{PIN} is not in HEAD; the control reads the committed pair, so commit the pin first")
        if not shutil.which("git"):
            refuse("git left PATH")
    except SystemExit:
        subprocess.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(tree)], capture_output=True)
        shutil.rmtree(holder, ignore_errors=True)
        raise

    cases: list[tuple[str, bool, str]] = []
    doc = "docs/STATUS.md"
    whole = "docs/streams/dec-inimbc-outlet.md"
    try:
        rc, out, err = run(tree)
        base = read(out)
        if base is None:
            refuse(f"the tool printed no HEADINGS line this control can read (rc={rc}): {err.strip()[:200]}")
        print(f"docs-headings: base readings -- base {base['base']}, now {base['now']}, removed {base['removed']}, added {base['added']}; rc={rc}")
        cases.append(("clean-tree", rc == 0 and base["removed"] == 0 and base["added"] == 0,
                      f"rc={rc}, removed={base['removed']}, added={base['added']} (want rc=0 with nothing removed or added)"))

        path = tree / doc
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines(keepends=True)
        idx = next(i for i, line in enumerate(lines) if line.startswith("## "))
        dropped = lines[idx].rstrip()
        path.write_text("".join(lines[:idx] + lines[idx + 1:]), encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        named = f"HEADINGS|removed|{doc}|" in out
        cases.append(("section-deleted", rc == 1 and got is not None and got["removed"] >= 1 and named,
                      f"rc={rc}, removed={got['removed'] if got else 'none'}, file named={named} (want rc=1, removed>=1, {doc} named; dropped {dropped!r})"))
        path.write_text(text, encoding="utf-8")
        rc_back, out_back, _ = run(tree)
        back = read(out_back)
        cases.append(("section-restored", rc_back == 0 and back is not None and back["removed"] == 0,
                      f"rc={rc_back}, removed={back['removed'] if back else 'none'} (want rc=0 and removed=0)"))

        whole_had = sum(1 for line in (tree / whole).read_text(encoding="utf-8").splitlines() if line.startswith("## "))
        (tree / whole).unlink()
        rc, out, err = run(tree)
        got = read(out)
        cases.append(("document-deleted", rc == 1 and got is not None and got["removed"] == whole_had,
                      f"rc={rc}, removed={got['removed'] if got else 'none'} (want rc=1 and removed={whole_had}, the whole file counted)"))
        subprocess.run(["git", "-C", str(tree), "checkout", "--", whole], check=True)

        grown = tree / doc
        grown.write_text(grown.read_text(encoding="utf-8") + "\n## scope control: a section that was not there before\n\nadded on purpose\n",
                         encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        cases.append(("section-added", rc == 0 and got is not None and got["added"] >= 1,
                      f"rc={rc}, added={got['added'] if got else 'none'} (want rc=0 and added>=1): growth is what the ceilings bound, not this"))
    finally:
        subprocess.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(tree)], capture_output=True)
        shutil.rmtree(holder, ignore_errors=True)

    red = 0
    for name, ok, detail in cases:
        if not ok:
            red += 1
        print(f"docs-headings: {name} -- {'ok' if ok else 'RED'}: {detail}")
    print(f"docs-headings: cases={len(cases)} red={red} green={len(cases) - red} -- " + (", ".join(n for n, ok, _ in cases if not ok) or "none"))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main())
