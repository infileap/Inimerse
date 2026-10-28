#!/usr/bin/env python3
"""Controls for tools/check_line_refs.py's citation count: a removal must redden,
and only a receipt that names the citation it excuses may turn it green.

The other bounds in that tool answer "is the number still in range" -- an explicit
delta, an unanchored ceiling, an off-target equality.  None of them can see a
citation that simply vanished when it was written as a bare number: removing one
moves neither the explicit delta (that pin holds the prefix form) nor a ceiling.
So the cases here are the shapes of a removal and of the excuse a removal needs:

  * one explicit citation deleted            -> removed >= 1, rc=1, the identity
                                                named on its own line;
  * one bare-number citation deleted         -> removed = 1, rc=1, the identity
                                                named -- the case no other pin sees;
  * a receipt naming exactly what went, with -> rc=0, CITATIONS|exempted|...;
    a reason
  * a receipt allowing two removals while    -> rc=1: allowed is a claim about a
    one happened                               number, and the claim must add up;
  * a receipt naming a citation that is      -> rc=1: a receipt may only exempt
    still there (nothing was removed)          a citation that is gone.

Every case runs in a throwaway git worktree at HEAD, so the tree under test is
never modified.  The pin is taken inside that copy: the committed pin's subject is
the tree that exists after the merge that lands this tool, not the author's tree.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = "tools/check_line_refs.py"
PIN = "tools/citations.pin.json"
EXEMPT = "tools/citations.exempt.json"
# The reading names the tree it was measured on and the scope/writing it counted,
# so a reader cannot pair this count with another tree's sha or another口径.
CITATIONS = re.compile(
    r"^CITATIONS\|base=(\d+)\|now=(\d+)\|removed=(\d+)\|added=(\d+)"
    r"\|tree=([0-9a-f]{7}|<no tree>)\|scope=([^|]+)\|writing=([a-z+,-]+)$",
    re.M,
)
REMOVED = re.compile(r"^CITATIONS\|removed\|(.+)$", re.M)
EXEMPTED = re.compile(r"^CITATIONS\|exempted\|removed=(\d+)\|allowed=(\d+)\|receipt=(.+)$", re.M)


def refuse(why: str) -> "NoReturn":
    print(f"citations: REFUSED|{why}", file=sys.stderr)
    print("citations: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


def read(text: str) -> dict | None:
    m = CITATIONS.search(text)
    if not m:
        return None
    return {"base": int(m.group(1)), "now": int(m.group(2)), "removed": int(m.group(3)),
            "added": int(m.group(4)), "tree": m.group(5), "scope": m.group(6),
            "writing": m.group(7).split(",")}


def gone_identity(text: str) -> "str | None":
    m = REMOVED.search(text)
    return m.group(1) if m else None


def exempted_of(text: str) -> "dict | None":
    m = EXEMPTED.search(text)
    if not m:
        return None
    return {"removed": int(m.group(1)), "allowed": int(m.group(2)), "receipt": m.group(3)}


def run(tree: Path, *argv: str) -> tuple[int, str, str]:
    proc = subprocess.run([sys.executable, str(tree / TOOL), *argv], cwd=str(tree),
                          capture_output=True, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def main() -> int:
    argparse.ArgumentParser().parse_args()
    if shutil.which("git") is None:
        refuse("git is not on PATH")
    if subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"],
                      capture_output=True).returncode != 0:
        refuse(f"{ROOT} is not inside a git worktree, so there is no tree to take a copy of")
    if not (ROOT / TOOL).exists():
        refuse(f"{TOOL} is missing from the tree under test")

    holder = tempfile.mkdtemp(prefix="citations-")
    tree = Path(holder) / "tree"
    added = subprocess.run(["git", "-C", str(ROOT), "worktree", "add", "--detach", str(tree), "HEAD"],
                           capture_output=True, text=True)
    if added.returncode != 0:
        tail = added.stderr.strip().splitlines()
        refuse("could not create a throwaway worktree" + (": " + tail[-1] if tail else ""))
    try:
        # Test the version being committed, not the one in HEAD: copy it in and
        # commit it in the copy, because --write-citations-pin pins a tree and
        # refuses to pin a dirty one.
        shutil.copy2(ROOT / TOOL, tree / TOOL)
        subprocess.run(["git", "-C", str(tree), "add", "-A"], check=True)
        committed = subprocess.run(
            ["git", "-C", str(tree), "-c", "user.name=citations-control",
             "-c", "user.email=citations-control@invalid", "commit", "-q", "-m",
             "the tool under test, committed so the pin has a tree to name"],
            capture_output=True, text=True)
        if committed.returncode != 0:
            refuse("could not commit the tool under test in the throwaway copy: "
                   + committed.stderr.strip()[:200])
        pin_run = run(tree, "--write-citations-pin")
        if pin_run[0] != 0:
            refuse("--write-citations-pin failed in the throwaway copy: " + pin_run[2].strip()[:200])

        cases: list[tuple[str, bool, str]] = []
        head = subprocess.run(["git", "-C", str(tree), "rev-parse", "--short=7", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        rc, out, err = run(tree)
        base = read(out)
        if base is None:
            refuse(f"the tool printed no CITATIONS line this control can read (rc={rc}): {err.strip()[:200]}")
        print(f"citations: base readings -- tree {base['tree']}, base {base['base']}, now {base['now']}, "
              f"removed {base['removed']}, added {base['added']}, scope {base['scope']}, "
              f"writing {','.join(base['writing'])}; rc={rc}")
        cases.append(("reading-names-its-tree-and-scope",
                      base["tree"] == (head or "<no tree>")
                      and set(base["writing"]) == {"prefix", "prefix-range", "link+bare", "bare"},
                      f"tree={base['tree']} head={head or '<no tree>'}, scope={base['scope']}, "
                      f"writing={','.join(base['writing'])} (want the reading to name the tree it measured "
                      f"and the口径 it counted)"))
        cases.append(("clean-tree", rc == 0 and base["removed"] == 0 and base["added"] == 0
                      and base["now"] == base["base"],
                      f"rc={rc}, removed={base['removed']}, added={base['added']}, now={base['now']} "
                      f"(want rc=0 with nothing moved right after re-pin)"))

        # The citations the control deletes are chosen from the pin, and chosen to
        # appear exactly once: an identity that survives elsewhere is still cited,
        # and a receipt may only exempt a citation that is gone, so removing one of
        # two duplicates would be red with no receipt able to excuse it.
        pinned = json.loads((tree / PIN).read_text(encoding="utf-8"))["rows"]
        counts: dict[str, int] = {}
        for ident in pinned:
            counts[ident] = counts.get(ident, 0) + 1

        explicit_target = None
        bare_target = None
        for ident, n in sorted(counts.items()):
            if n != 1:
                continue
            rel, number, owner = ident.split("|", 2)
            if owner in ("<none>", "") or not (tree / rel).is_file():
                continue
            text = (tree / rel).read_text(encoding="utf-8")
            if f"{owner}:{number}" in text:
                if explicit_target is None:
                    explicit_target = (rel, owner, number, ident)
            elif text.count(f":{number}") == 1 and bare_target is None:
                bare_target = (rel, owner, number, ident)
        if explicit_target is None or bare_target is None:
            refuse("the pin offers no citation of one of the two writings this control deletes; cannot run")

        # One explicit citation, turned into prose.  This reddens two cells at
        # once (the pinned explicit delta sees it too), which is correct and is why
        # it is not the case a receipt has to excuse.
        doc, owner, number, ident = explicit_target
        removed_text = f"{owner}:{number}"
        snapshot = (tree / doc).read_text(encoding="utf-8")
        (tree / doc).write_text(snapshot.replace(removed_text, "the paragraph now summarises it", 1),
                                encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        identity = gone_identity(out)
        cases.append(("citation-deleted",
                      rc == 1 and got is not None and got["removed"] >= 1 and identity == ident,
                      f"rc={rc}, removed={got['removed'] if got else 'none'}, first removed={identity} "
                      f"(want rc=1, removed=1 and the identity named; dropped {removed_text!r})"))
        (tree / doc).write_text(snapshot, encoding="utf-8")

        # A bare-number citation now: the only pin that can see this removal is the
        # citation count itself, so it is the case the count exists for.
        bdoc, bowner, bnumber, bident = bare_target
        bsnapshot = (tree / bdoc).read_text(encoding="utf-8")
        (tree / bdoc).write_text(bsnapshot.replace(f":{bnumber}", " (summarised)", 1), encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        identity = gone_identity(out)
        cases.append(("bare-citation-deleted",
                      rc == 1 and got is not None and got["removed"] == 1 and identity == bident,
                      f"rc={rc}, removed={got['removed'] if got else 'none'}, first removed={identity} "
                      f"(want rc=1, removed=1 and {bident} named; dropped the bare :{bnumber})"))

        # The receipt: it must name the citation that went and say why.  A receipt
        # is a claim about a number, so the number has to add up too.
        receipt = {"allowed": 1, "items": [{"identity": bident,
                                            "why": "the paragraph it supported was rewritten in prose"}]}
        (tree / EXEMPT).write_text(json.dumps(receipt, ensure_ascii=False, indent=1) + "\n",
                                   encoding="utf-8")
        rc, out, err = run(tree)
        excused = exempted_of(out)
        cases.append(("receipt-allows-the-one-that-went",
                      rc == 0 and excused is not None and excused["removed"] == 1
                      and excused["allowed"] == 1,
                      f"rc={rc}, exempted={excused} (want rc=0, removed=1 allowed=1 by the named receipt)"))

        # An allowance larger than the removal is a receipt for something that did
        # not happen: allowed is a claim, and the claim must equal the removals.
        receipt["allowed"] = 2
        (tree / EXEMPT).write_text(json.dumps(receipt, ensure_ascii=False, indent=1) + "\n",
                                   encoding="utf-8")
        rc, out, err = run(tree)
        cases.append(("receipt-cannot-allow-two-when-one-went",
                      rc == 1 and "allowed=2" in out,
                      f"rc={rc} (want rc=1: allowed=2 claims two removals, one happened); "
                      f"the report says allowed: {'allowed=2' in out}"))

        # Put the citation back, keep a receipt that names it: nothing was removed,
        # so the receipt excuses nothing and must be refused.  Otherwise a receipt
        # filed once becomes a standing licence for a removal nobody has made yet.
        (tree / bdoc).write_text(bsnapshot, encoding="utf-8")
        receipt = {"allowed": 0, "items": [{"identity": bident,
                                            "why": "filed in advance, before the removal it excuses"}]}
        (tree / EXEMPT).write_text(json.dumps(receipt, ensure_ascii=False, indent=1) + "\n",
                                   encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        cases.append(("receipt-cannot-name-a-live-citation",
                      rc == 1 and got is not None and got["removed"] == 0 and "still cited" in out,
                      f"rc={rc}, removed={got['removed'] if got else 'none'} "
                      f"(want rc=1 with removed=0: a receipt may only exempt a citation that is gone)"))

        # Restoring the citation and dropping the receipt must return the count
        # exactly to the pinned number -- green again, for the only reason that
        # should make it green.
        (tree / EXEMPT).unlink()
        rc, out, err = run(tree)
        back = read(out)
        cases.append(("citation-restored",
                      rc == 0 and back is not None and back["removed"] == 0
                      and back["now"] == base["now"],
                      f"rc={rc}, removed={back['removed'] if back else 'none'}, "
                      f"now={back['now'] if back else 'none'} (want rc=0 and now back to {base['now']})"))
    finally:
        subprocess.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(tree)],
                       capture_output=True)
        shutil.rmtree(holder, ignore_errors=True)

    red = 0
    for name, ok, detail in cases:
        if not ok:
            red += 1
        print(f"citations: {name} -- {'ok' if ok else 'RED'}: {detail}")
    print(f"citations: cases={len(cases)} red={red} green={len(cases) - red} -- "
          + (", ".join(n for n, ok, _ in cases if not ok) or "none"))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main())
