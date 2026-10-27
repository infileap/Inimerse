#!/usr/bin/env python3
"""Count the level-2 headings under docs/, against a pinned baseline.

Why this exists: tools/check_line_refs.py measures whether references grew
past a ceiling, and a ceiling only ever stops growth.  A section that is
deleted is below every ceiling there is, so deleting one -- and with it every
reference it carried -- is invisible there.  The two directions need two
shapes: growth is bounded by a ceiling, removal has to be measured against a
record of what was there.  This is the record.

The baseline is pinned in tools/docs_headings.pin.json, together with the tree
it was taken on.  A number without a tree is a claim about nothing in
particular, so the pin is only meaningful next to that sha, and updating it is
a deliberate act (--write-pin) rather than a quiet side effect.

  removed > 0  ->  red.  A section vanished; a later reader will not know it
                   was ever there, and no ceiling will ever say so.
  added   > 0  ->  printed, not red.  Growth is what the ceilings are for, and
                   growth is also what the stale line below counts.

The stale line answers a different question: how many sections exist now that
the pin has never seen (HEADINGS|stale|...).  It is a reading, not a gate.  The
verdict comes from removed alone, because a baseline that is merely old is not
a defect -- and letting it redden means every lane pays a re-pin plus a full
gate whenever another lane adds a section, which teaches everyone that a red
here means "re-pin" rather than "look".  A measurement whose input moves with
other people's commits is not a measurement of this tree.  Re-pinning stays a
deliberate act; the stale line is how a reader sees it is due.

One limit, stated rather than hidden: the comparison is by count per file, so
while the pin is behind, a deletion and an addition of equal size in the same
file cancel out and removed reads 0.  That window exists only between a commit
that adds sections and the re-pin that follows it -- the pin is written by the
lane that merges, on the tree the merge produced -- and the stale line is what
says the window is open.  Measuring a deletion by identity instead of by count
would close it; that is a change to the pin format, not to this verdict.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN = ROOT / "tools" / "docs_headings.pin.json"


def refuse(why: str, code: int) -> "NoReturn":
    print(f"check_docs_headings: {why}", file=sys.stderr)
    sys.exit(code)


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, encoding="utf-8")


def docs_files() -> list[str]:
    proc = git("ls-files", "-z", "docs")
    if proc.returncode != 0:
        refuse("not a git repository at " + str(ROOT) + "; there is no file list to sweep.", 2)
    out = [p for p in proc.stdout.split("\0") if p.endswith(".md")]
    return sorted(out)


def heading_counts(files: list[str]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for rel in files:
        try:
            text = (ROOT / rel).read_text(encoding="utf-8")
        except FileNotFoundError:
            # A tracked document that is gone from the working tree is a
            # deletion, not an unreadable file: it contributes zero headings,
            # and the comparison against the pin is what reddens.  Every other
            # read failure is the environment, and says so with rc 4.
            counts[rel] = 0
            continue
        except (OSError, UnicodeError) as exc:
            refuse(f"cannot read {rel!r}: {exc.__class__.__name__}: {exc}", 4)
        counts[rel] = sum(1 for line in text.splitlines() if line.startswith("## "))
    return counts


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--write-pin", action="store_true", help="record the current counts as the baseline")
    args = ap.parse_args()

    files = docs_files()
    now = heading_counts(files)
    now_total = sum(now.values())

    if args.write_pin:
        tree = git("rev-parse", "--short=7", "HEAD").stdout.strip() or "<no tree>"
        PIN.write_text(
            json.dumps({"tree": tree, "docs": len(files), "headings": now_total, "per_file": now},
                       ensure_ascii=False, indent=1, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(f"HEADINGS|pin|tree={tree}|docs={len(files)}|headings={now_total}")
        return 0

    if not PIN.exists():
        refuse("no baseline pin at tools/docs_headings.pin.json; run with --write-pin once, deliberately.", 2)
    try:
        pin = json.loads(PIN.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        refuse(f"cannot read the baseline pin: {exc.__class__.__name__}: {exc}", 4)
    base_counts: dict[str, int] = pin["per_file"]
    base_total = int(pin["headings"])

    rows = []
    removed = 0
    added = 0
    for rel in sorted(set(base_counts) | set(now)):
        b = int(base_counts.get(rel, 0))
        n = int(now.get(rel, 0))
        if n < b:
            removed += b - n
            rows.append((rel, b, n, "removed"))
        elif n > b:
            added += n - b
            rows.append((rel, b, n, "added"))

    # One line, one subject.  The reading line names the tree it was measured on;
    # the pin line reports only the pin's own counts.  The earlier shape paired
    # tree= with files= taken from the working copy, so a reader comparing
    # docs=<pinned> with swept=<now> was comparing two different trees without
    # being told (measured in review: HEADINGS|pin|tree=6e41869|docs=106|files=107).
    tree_now = git("rev-parse", "--short=7", "HEAD").stdout.strip() or "<no tree>"
    print(f"HEADINGS|tree={tree_now}|base={base_total}|now={now_total}|removed={removed}|added={added}")
    print(f"HEADINGS|pin|tree={pin.get('tree', '<unpinned>')}|docs={pin.get('docs', '?')}|files={len(base_counts)}")
    # How far the pin is behind HEAD, counted in sections.  Printed always, so
    # that a fresh pin reads behind=0 and a reader can tell "nothing new" from
    # "nothing measured"; it never touches the exit code.  See the docstring.
    print(f"HEADINGS|stale|pin-tree={pin.get('tree', '<unpinned>')}|head={tree_now}|behind={added}")
    for rel, b, n, kind in rows:
        print(f"HEADINGS|{kind}|{rel}|base={b}|now={n}")
    if len(rows) > 40:
        print(f"HEADINGS|note|{len(rows) - 40} more file(s) with a change not shown")

    if removed:
        print(
            f"check_docs_headings: {removed} level-2 heading(s) that the pin records are gone. "
            "A ceiling cannot see a deletion -- it only bounds growth -- so this is measured "
            "against the pinned counts instead. If the removal was meant, re-pin deliberately "
            "with --write-pin and say so in the commit; if it was not, restore the section.",
            file=sys.stderr,
        )
        return 1
    print("check_docs_headings: no pinned section is missing. A green here says nothing about whether the text under a surviving heading still says the same thing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
