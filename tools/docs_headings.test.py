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
                                                    ceilings already bound;
  * a section added under a pin nobody updated   -> stale > 0, still green:
                                                    an old baseline is a reading
                                                    (HEADINGS|stale|...), never a
                                                    verdict, so no lane pays a
                                                    re-pin for another lane's
                                                    commit.

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
# The reading line names the tree it was measured on, so a reader cannot pair
# these counts with another tree's sha; the pin line carries its own subject.
HEADINGS = re.compile(
    r"^HEADINGS\|tree=([0-9a-f]{7}|<no tree>)\|base=(\d+)\|now=(\d+)\|removed=(\d+)\|added=(\d+)$",
    re.M,
)
STALE = re.compile(
    r"^HEADINGS\|stale\|pin-tree=([0-9a-f]{7}|<unpinned>)\|head=([0-9a-f]{7}|<no tree>)\|net=(-?\d+)"
    r"\|pin-format=v1\|identity=([a-z-]+)\|identity-domain=([a-z,-]+)$",
    re.M,
)
# Two questions, two lines: DRIFT answers "how many sections appeared and
# disappeared" (added, removed) and STALE answers "how far did the total move"
# (net).  They are different numbers whenever a file loses a section and
# another gains one, and reading one as the other is how the tool once printed
# behind=1 on a tree whose total had not moved at all.
DRIFT = re.compile(r"^HEADINGS\|drift\|added=(\d+)\|removed=(\d+)\|net=(-?\d+)$", re.M)
# The warning is a second line of words, on purpose: it is not a threshold, so
# no reader has to guess whether a given net= is a situation or a verdict.
WARN = re.compile(
    r"^HEADINGS\|stale-warn\|net=(-?\d+)\|note=this reading is old; the criterion does not depend on it$",
    re.M,
)
# Absence has a name, not a number: warned is one of these two values and never
# a third.  A -1 sentinel already made one reviewer ask "no warning, or a
# miscount?" -- the same ambiguity as a threshold that is also a reading.
WARN_PRESENT = re.compile(r"^HEADINGS\|stale-warn\|", re.M)
WARN_DOMAIN = ("none", "stale")
IDENTITIES = ("per-file-count", "per-heading-identity")


def refuse(why: str) -> "NoReturn":
    print(f"docs-headings: REFUSED|{why}", file=sys.stderr)
    print("docs-headings: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


def read(text: str) -> dict | None:
    m = HEADINGS.search(text)
    if not m:
        return None
    return {"tree": m.group(1), "base": int(m.group(2)), "now": int(m.group(3)),
            "removed": int(m.group(4)), "added": int(m.group(5))}


def net_of(text: str) -> "int | None":
    """How far the section total moved since the pin, or None when not reported.
    A missing reading has no number: -1 would be summed and plotted as a real
    position, which is the same mistake as a sentinel warning count."""
    m = STALE.search(text)
    return int(m.group(3)) if m else None


def drift_of(text: str) -> "dict | None":
    """added / removed / net as the drift line reports them, or None when absent."""
    m = DRIFT.search(text)
    if not m:
        return None
    return {"added": int(m.group(1)), "removed": int(m.group(2)), "net": int(m.group(3))}


def warn_of(text: str) -> str:
    """One of WARN_DOMAIN: "stale" when the warning line is there and shaped as
    promised, "none" when it is absent, and "malformed" when it is there but not
    shaped as promised -- a third value the test must never accept."""
    if not WARN_PRESENT.search(text):
        return "none"
    return "stale" if WARN.search(text) else "malformed"


def warn_count(text: str) -> "int | None":
    """The count the warning repeats; None when there is no warning line."""
    m = WARN.search(text)
    return int(m.group(1)) if m else None


def identity_of(text: str) -> tuple[str, tuple[str, ...]]:
    """The applied comparison name and the alternative names the line lists."""
    m = STALE.search(text)
    if not m:
        return "", ()
    return m.group(4), tuple(m.group(5).split(","))


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
        # Read the copy against the committed pin once: this run names the tree
        # it measured and says how far the pin is behind.  Nothing here asserts
        # anything about the pin's age -- stale-is-a-reading owns that question.
        rc, out, err = run(tree)
        base = read(out)
        if base is None:
            refuse(f"the tool printed no HEADINGS line this control can read (rc={rc}): {err.strip()[:200]}")
        head = subprocess.run(["git", "-C", str(tree), "rev-parse", "--short=7", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        named_tree = base["tree"] == (head or "<no tree>")
        # Measured against the pin as committed, which may already be behind by
        # someone else's commit: every case below compares against this frame
        # instead of assuming a fresh baseline, or a control would go red for
        # the reason it exists to tolerate (a stale pin on someone else's tree).
        base_net = net_of(out)
        if base_net is None:
            refuse("the tool printed no stale line this control can read")
        print(f"docs-headings: base readings -- tree {base['tree']}, base {base['base']}, now {base['now']}, "
              f"removed {base['removed']}, added {base['added']}, stale net {net_of(out)}, "
              f"drift {drift_of(out)}, warning {warn_of(out)}, "
              f"names-head={named_tree}; rc={rc}")
        cases.append(("reading-names-its-tree", named_tree,
                      f"tree={base['tree']} head={head or '<no tree>'} (want the reading to name the tree it measured)"))

        # A control that measures deletion must not read an input that moves
        # with other people's commits.  This case is that input, alone: a
        # section is appended and the pin is NOT updated.  The tool must stay
        # green -- nothing was removed -- and report the distance in the stale
        # line.  Before this split the same shape reddened clean-tree, so every
        # lane paid a re-pin plus a gate whenever another lane added a section
        # (measured on c8bbe99 / 469a7b4 / 63d9344, all red on added=1).
        snapshot = (tree / doc).read_text(encoding="utf-8")
        (tree / doc).write_text(
            snapshot + "\n## scope control: a section the pin has never seen\n\nadded on purpose\n",
            encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        stale = net_of(out)
        warned = warn_of(out)
        applied, alternatives = identity_of(out)
        cases.append(("stale-is-a-reading",
                      rc == 0 and got is not None and got["removed"] == base["removed"]
                      and stale == base_net + 1,
                      f"rc={rc}, removed={got['removed'] if got else 'none'}, stale net={stale} "
                      f"(want rc=0, removed={base['removed']}, net={base_net + 1}: an old baseline is not a defect)"))
        # The warning says the same number in words and adds no verdict: it is
        # present exactly when the pin is behind, its name comes from a domain of
        # two, and it does not decide rc.  "none" is a name, not a -1: absence
        # must be said, not encoded.
        cases.append(("stale-warn-is-a-reading", rc == 0 and warned == "stale" and warn_count(out) == stale
                      and stale == base_net + 1,
                      f"rc={rc}, warning={warned} (domain {WARN_DOMAIN}), warning count={warn_count(out)}, "
                      f"stale net={stale} (want rc=0, warned=stale repeating the count, without a threshold)"))
        # The reading line says which comparison produced the count and names the
        # alternative that is not in use yet, so a reader never has to open the
        # source to learn what kind of comparison they are reading.
        cases.append(("identity-names-both-kinds", applied == "per-file-count" and alternatives == IDENTITIES,
                      f"identity={applied}, identity-domain={','.join(alternatives)} "
                      f"(want identity=per-file-count inside the domain {IDENTITIES})"))
        (tree / doc).write_text(snapshot, encoding="utf-8")

        # The deletion cases below run against the pin as committed -- the stale
        # one -- because deleting a section the pin already knows must be seen
        # without anyone re-pinning first.  If they were only green after a
        # re-pin, the pin would still be "the switch that turns the gate green".
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

        # One file loses a section while another gains one, so the total did not
        # move: net is 0 even though added and removed are both 1.  The stale
        # line must say net=0 -- it once printed behind=added here, claiming one
        # section the pin never saw on a tree whose total was unchanged, and the
        # drift line exists so both questions keep their own names.
        other = "docs/BOARD.md"
        st_text = (tree / doc).read_text(encoding="utf-8")
        bd_text = (tree / other).read_text(encoding="utf-8")
        st_lines = st_text.splitlines(keepends=True)
        st_idx = next(i for i, line in enumerate(st_lines) if line.startswith("## "))
        (tree / doc).write_text("".join(st_lines[:st_idx] + st_lines[st_idx + 1:]), encoding="utf-8")
        (tree / other).write_text(
            bd_text + "\n## scope control: a section the pin has never seen\n\nadded on purpose\n",
            encoding="utf-8")
        rc, out, err = run(tree)
        got = read(out)
        net = net_of(out)
        drift = drift_of(out)
        want_drift = {"added": base["added"] + 1, "removed": base["removed"] + 1, "net": base_net}
        cases.append(("one-deleted-one-added-is-not-behind",
                      rc == 1 and got is not None and got["removed"] == base["removed"] + 1
                      and got["added"] == base["added"] + 1 and net == base_net and drift == want_drift
                      and warn_of(out) == "stale" and warn_count(out) == base_net,
                      f"rc={rc}, added={got['added'] if got else 'none'}, removed={got['removed'] if got else 'none'}, "
                      f"stale net={net}, drift={drift}, warning={warn_of(out)}/{warn_count(out)} "
                      f"(want rc=1 from the deletion, net={base_net} with added and removed both one higher: "
                      f"two questions, two names)"))
        (tree / doc).write_text(st_text, encoding="utf-8")
        (tree / other).write_text(bd_text, encoding="utf-8")

        # Only now, after the deletion cases have run against the stale pin, pin
        # the copy's own tree and confirm the stale reading drops to zero: a
        # fresh pin reads behind=0, so the line cannot silently stop working.
        # This --write-pin runs in the throwaway copy
        # and never against the repository: the committed pin is written by the
        # lane that merges, because the pin's subject is the tree that exists
        # only after the fast-forward -- a lane author's pin is right on their
        # tree and wrong on the merged one, and its failure looks exactly like
        # "someone else's commit reddened me".
        pin_run = subprocess.run([sys.executable, str(tree / TOOL), "--write-pin"],
                                 cwd=str(tree), capture_output=True, text=True)
        if pin_run.returncode != 0:
            refuse(f"--write-pin failed in the throwaway copy: {pin_run.stderr.strip()[:200]}")
        rc, out, err = run(tree)
        fresh = read(out)
        fresh_stale = net_of(out)
        fresh_drift = drift_of(out)
        cases.append(("clean-tree", rc == 0 and fresh is not None and fresh["removed"] == 0 and fresh_stale == 0
                      and fresh_drift == {"added": 0, "removed": 0, "net": 0} and warn_of(out) == "none",
                      f"rc={rc}, removed={fresh['removed'] if fresh else 'none'}, stale net={fresh_stale}, "
                      f"drift={fresh_drift}, "
                      f"warning={warn_of(out)} (want rc=0, removed=0, net=0 and no warning right after "
                      f"re-pin; baseline freshness is not this case's question)"))
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
