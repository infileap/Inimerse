#!/usr/bin/env python3
"""Negative control for the two stamps the gate prints about its own subject.

tools/gate.sh stamps GATE|start|... and GATE|end|... with the HEAD it measured.
It also stamps the moment, because a sha cannot answer "was this reading taken
before or after that accident window": a reader holding such a window would have
to leave the log and run date -u and git reflog themselves.  The moment is UTC,
to the second, with a Z -- two stamps then compare as strings and nobody has to
convert a time zone in their head.

What this control is careful about:

  * the moment is a reading, not a criterion.  Nothing here asserts that a run
    whose stamps straddle some window is red: judging a window is a human's job,
    and gate.sh does not know when anyone else swapped a tree.
  * the fields are appended, not reordered or merged.  A regex written before
    at= existed has to keep matching, so one case pins that shape verbatim.
  * a positive control that cannot fail is decoration.  The fourth case strips
    at= from a copy of the script and requires the compatibility regex to stay
    green while the positive case goes red.

Each case runs tools/gate.sh --only links --fast in a throwaway worktree at HEAD,
with its own BUILD_DIR, so the control never competes for the build lock of the
run that is executing it, and never measures the lane's dirty tree.
"""
import calendar
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GATE = "tools/gate.sh"

START_AT = re.compile(
    r"^GATE\|start\|head=(?P<head>[0-9a-f]{40}|<not a git tree>)\|"
    r"worktree=(?P<worktree>\S+)\|"
    r"at=(?P<at>\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)@D@".replace("@D@", "$"))
END_AT = re.compile(
    r"^GATE\|end\|head=(?P<head>[0-9a-f]{40}|<not a git tree>)\|"
    r"at=(?P<at>\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)@D@".replace("@D@", "$"))

# The shapes as they were written before at= existed.  Fields are appended, so
# what has to keep matching is the prefix, not the whole line: the start stamp's
# pre-change regex was never end-anchored, and it must still match.  The end
# stamp's pre-change regex WAS end-anchored, and no appended field can keep it
# green -- that is the one shape appending breaks, and the case below says so out
# loud instead of quietly dropping the anchor.
START_BEFORE_AT = re.compile(r"^GATE\|start\|head=[0-9a-f]{40}\|worktree=")
END_BEFORE_AT = re.compile(r"^GATE\|end\|head=[0-9a-f]{40}")

STAMP = "%Y-%m-%dT%H:%M:%SZ"


def refuse(why, code=2):
    print(f"gate-attribution: REFUSED|{why}", file=sys.stderr)
    print("gate-attribution: not registered: nothing was checked", file=sys.stderr)
    raise SystemExit(code)


def find(out, pattern):
    for line in out.splitlines():
        found = pattern.match(line)
        if found:
            return line, found
    return None, None


def epoch(stamp):
    return calendar.timegm(time.strptime(stamp, STAMP))


def run(tree, tag, tz=None):
    env = dict(os.environ)
    env["BUILD_DIR"] = str(tree.parent / f"build-{tag}")
    if tz is not None:
        env["TZ"] = tz
    done = subprocess.run(["bash", str(tree / GATE), "--only", "links", "--fast"],
                          cwd=str(tree), env=env, capture_output=True, text=True)
    return done.returncode, done.stdout, done.stderr


def main():
    if shutil.which("git") is None:
        refuse("git is not on PATH")
    if subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"],
                      capture_output=True).returncode != 0:
        refuse(f"{ROOT} is not inside a git worktree, so there is no tree to stamp")
    if not (ROOT / GATE).exists():
        refuse(f"{GATE} is missing from the tree under test")

    holder = Path(tempfile.mkdtemp(prefix="gate-attribution-"))
    tree = holder / "tree"
    added = subprocess.run(["git", "-C", str(ROOT), "worktree", "add", "--detach",
                            str(tree), "HEAD"], capture_output=True, text=True)
    if added.returncode != 0:
        tail = added.stderr.strip().splitlines()
        refuse("could not create a throwaway worktree" + (": " + tail[-1] if tail else ""))
    script = tree / GATE
    shutil.copy2(ROOT / GATE, script)
    original = script.read_text()
    cases = []
    try:
        rc, out, err = run(tree, "plain")
        start_line, start = find(out, START_AT)
        end_line, end = find(out, END_AT)
        cases.append(("both-stamps-carry-a-moment",
                      rc == 0 and start is not None and end is not None,
                      f"rc={rc}, start at={'yes' if start else 'no'}, "
                      f"end at={'yes' if end else 'no'}; "
                      f"stderr={err.strip().splitlines()[-1] if err.strip() else ''}"))
        if start is not None and end is not None:
            cases.append(("the-moment-advances",
                          start.group("at") <= end.group("at"),
                          f"start {start.group('at')} -> end {end.group('at')}"))
        else:
            cases.append(("the-moment-advances", False, "a stamp had no at= to compare"))

        # A shifted local time zone must not reach the stamp: the point of UTC with
        # a Z is that the reader does not have to know where the run happened.
        shifted = []
        for tz in ("UTC+3", "America/New_York"):
            rc2, out2, _ = run(tree, "tz-" + tz.replace("/", "-"), tz=tz)
            _, found = find(out2, START_AT)
            shifted.append((tz, rc2, found))
        ok = all(rc2 == 0 and found is not None
                 and abs(epoch(found.group("at")) - time.time()) < 300
                 for _, rc2, found in shifted)
        cases.append(("stamp-is-utc-whatever-tz-says", ok,
                      "; ".join(f"TZ={tz} -> rc={rc2}, at="
                                f"{found.group('at') if found else 'missing'}"
                                for tz, rc2, found in shifted)))

        start_kept = START_BEFORE_AT.search(start_line or "") is not None
        end_prefix_kept = END_BEFORE_AT.search(end_line or "") is not None
        end_anchored = re.compile(
            r"^GATE\|end\|head=[0-9a-f]{40}$").search(end_line or "") is not None
        cases.append(("the-pre-change-shape-survives-as-a-prefix",
                      start_kept and end_prefix_kept and not end_anchored,
                      f"start pre-change regex kept={start_kept}, "
                      f"end prefix kept={end_prefix_kept}, "
                      f"end-anchored regex still matches={end_anchored} "
                      f"(the last one is what appending a field breaks)"))

        # Counter-proof: with at= stripped, the compatibility regex must stay green
        # and the positive case must go red -- otherwise the positive case is not
        # measuring the field, it is measuring whether the script runs at all.
        stripped = (original
                    .replace("|at=$GATE_START_AT", "")
                    .replace("|at=@D@GATE_END_AT", ""))
        script.write_text(stripped)
        rc3, out3, _ = run(tree, "stripped")
        compat3 = (find(out3, START_BEFORE_AT)[1] is not None
                   and find(out3, END_BEFORE_AT)[1] is not None)
        positive3 = (find(out3, START_AT)[1] is not None
                     and find(out3, END_AT)[1] is not None)
        cases.append(("stripping-at-reddens-the-stamp-case-not-the-compat-case",
                      compat3 and not positive3,
                      f"compat={compat3}, stamp-case={positive3} (want True, False)"))
    finally:
        script.write_text(original)
        subprocess.run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(tree)],
                       capture_output=True)
        shutil.rmtree(holder, ignore_errors=True)

    red = 0
    for name, ok, detail in cases:
        print(f"gate-attribution: {name} -- {'ok' if ok else 'RED'}: {detail}")
        if not ok:
            red += 1
    print(f"gate-attribution: cases={len(cases)} red={red} green={len(cases) - red} -- "
          + ("none" if red == 0 else "see RED cases above"))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main())

