#!/usr/bin/env python3
"""check_stage_count.py -- every location that writes the gate's stage count down
must equal the count derived from tools/gate.sh's STAGE_SPECS array.

WHY THIS EXISTS
---------------
`tools/gate.sh:1077` prints

    gate: OK — every stage passed (${#STAGE_NAMES[@]}/${#STAGE_WANTED[@]} stages ran).

and `tools/gate.sh:622-624` says in so many words that this count "is written
down nowhere" -- `all` is resolved from the STAGE_SPECS array and the summary
counts its entries, so adding a stage is that array and nothing else.

It is written down nowhere *in the gate*.  Elsewhere in this tree it is written
down eighteen times.  A count that has been copied into a constant stops having
a reader, because "has a reader" and "has no reader" look the same in source --
both are an assignment.  This check is that reader.

It is the second side of a comparison.  A rule of the form "the number you say
must equal the number that is derived" is only a comparison when the derived
side is produced by something other than the number itself; comparing a constant
with itself only ever says "the constant is still there".

TWO KINDS OF LOCATION
---------------------
  must-equal  spells the gate's stage count as it is today, as a statement about
              this tree.  The spelled number must equal the derived number.
              Adding a stage to STAGE_SPECS reddens these.

  record      an append-only ledger's line, saying what the count WAS when the
              line was written.  A ledger's numbers do not expire, so these are
              exempt -- and each exemption prints the reason it claims, so that
              an exemption which stops matching its location is a number and not
              a silence.

LOCATIONS ARE MATCHED BY PATTERN, NEVER BY LINE NUMBER
-----------------------------------------------------
A check that a written-down number must equal a derived number must not itself
be located by a written-down line number: it would go stale the same way its
subjects go stale, and look the same when it did.  Each declaration carries a
`locate` pattern that must match exactly one line of its file -- a declaration
whose location is gone is a red, not a silent pass -- and a `number` pattern
that reads the spelled number off that line.  The two are separate on purpose:
an edit that changes the number must redden as MISMATCH (the location is still
there, the number is not), not as GONE (the location is gone).

WHAT THIS IS NOT
----------------
It does not find new locations.  It judges the ones declared below, and prints
how many locations in the tree spell a number next to a stage word without being
declared, so the hole is a number rather than a silence.  Closing that hole is a
separate piece of work with its own denominator.

EXIT CODES
----------
  0  every declared location agrees with the derived count
  1  a `must-equal` location disagrees, or a declared location is gone
  2  the derived count could not be read -- this is the check failing to
     measure, NOT a verdict about any file
"""

import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE_REL = "tools/gate.sh"
DERIVED_ENV = "INIMERSE_STAGE_COUNT_DERIVED"

# A number written in Chinese, in Arabic, or in English.  The Chinese class must
# be general and not just `十六`: a location that spells the count as `十七` or
# `三十` would otherwise be invisible to a check whose whole subject is numbers
# written down, and the check would report "no disagreement" about a number it
# never read.  That is the same failure a constant has -- it answers only the
# question it was built for.
CN_NUM = r"[〇零一二三四五六七八九十百]+"
# `[Ss]ixteen(?:th)?` and not `[Ss]ixteenth?` -- the latter is "Sixteen" plus a
# required `t`, so it matched "Sixteenth" and missed "Sixteen".  Three real
# locations spell the count as the plain word and were reported UNSPELLED for it.
NUM_ANY = r"(?:%s|[0-9]+|[Ss]ixteen(?:th)?)" % CN_NUM

# The number and the word that can only mean "gate stage", adjacent.  The
# lookbehind drops the ordinal and the demonstrative -- `第一阶段`, `每一阶段`,
# `这一阶段` name a position in a list, not how many stages there are -- and it
# is why `每一阶段都通过（十六` read the `一` and reported the count as 1.
N_NEAR_STAGE = re.compile(
    r"(?<![第每这那])(?P<n>%s)\s*(?:个)?\s*(?:阶段|支|stages?)" % NUM_ANY
)

# kind, path, locate pattern, number pattern (None => N_NEAR_STAGE), why
DECLARED = [
    # --- must-equal: statements about this tree, today -------------------
    ("must-equal", "docs/README.md", r"#.+个阶段：build \+ ctest", None, None),
    ("must-equal", "tools/README.md", r"stages; exit 0 only if all pass\.", None, None),
    ("must-equal", "tools/README.md", r"# all .+ stages", None, None),
    ("must-equal", "docs/BOARD.md", r"个阶段，任一失败即整体失败", None, None),
    ("must-equal", "tools/ref_substitute_guard.py", r"the .+ stages answer", None, None),
    (
        "must-equal",
        "docs/AUDIT.md",
        r"不在任何 scope 里的文件强制全部",
        r"强制全部 (?P<n>%s) 个" % NUM_ANY,
        None,
    ),
    ("must-equal", "docs/AUDIT.md", r"排在第 .+ 阶段之后", None, None),
    # --- record: a ledger line naming what the count was when written -----
    (
        "record",
        "docs/AUDIT.md",
        r"增补四十七（补记：同一棵树的两次全量门禁，以及第一次.+支全绿）",
        None,
        "an append-only ledger entry: it names the batch it records, and that batch is over",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"带环境变量的那一次，.+支全绿",
        None,
        "an append-only ledger entry: it carries the run's own verbatim output as its evidence",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r".+支全绿是这一批里第一次",
        None,
        "an append-only ledger entry: it says which batch was the first, so it cannot be about a later one",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"全量门禁.+阶段绿 ⇒ \*\*内容接受",
        None,
        "an append-only ledger entry: it records a verdict passed on one commit",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"我派件的验收是「全量门禁.+阶段绿」",
        None,
        "an append-only ledger entry: it quotes the acceptance as it was stated to a lane",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"比那个.+阶段值钱",
        None,
        "an append-only ledger entry: it counts a price paid at the time it was written",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"每一阶段都通过（",
        r"每一阶段都通过（(?P<n>%s)分之" % CN_NUM,
        "an append-only ledger entry: it quotes a run's last line as it was then",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"11\. 验证：`BUILD_DIR=\$\(mktemp -d\)` 跑全量门禁",
        None,
        "an append-only ledger entry: it records what one verification run printed",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"甲组的 .+ 秒",
        None,
        "an append-only ledger entry: it distinguishes numbers it re-measured from numbers it did not",
    ),
    (
        "record",
        "docs/AUDIT.md",
        r"13\. 合并后的读数（主干上，合并且.+个阶段全部通过之后）",
        None,
        "an append-only ledger entry: it is dated by the merge it follows",
    ),
    (
        "record",
        "tools/static_counts_census.py",
        r"`obs-markers` was added to the gate as a .+ stage",
        None,
        "a comment recording the letter that added the stage, not a claim about today",
    ),
]

SCAN_SUFFIXES = (".md", ".py", ".sh", ".js", ".ps1", ".mjs", ".cjs", ".yml", ".yaml")
SCAN_ROOTS = ("docs", "tools", ".github")


def cn_to_int(text: str):
    """Read a Chinese numeral written with 〇一二三四五六七八九十百."""
    if not text:
        return None
    digits = {"〇": 0, "零": 0, "一": 1, "二": 2, "三": 3, "四": 4, "五": 5,
              "六": 6, "七": 7, "八": 8, "九": 9}
    total, section, number = 0, 0, 0
    units = {"十": 10, "百": 100}
    for ch in text:
        if ch in digits:
            number = digits[ch]
        elif ch in units:
            unit = units[ch]
            if number == 0:
                number = 1
            section += number * unit
            number = 0
        else:
            return None
    return total + section + number


def spell(pattern, line: str):
    """Return (number, raw) read off this line by this pattern."""
    m = re.search(pattern, line)
    if not m:
        return None, None
    raw = m.group("n")
    if re.fullmatch(CN_NUM, raw):
        return cn_to_int(raw), raw
    low = raw.lower()
    if low in ("sixteen", "sixteenth"):
        return 16, raw
    try:
        return int(raw), raw
    except ValueError:
        return None, raw


def derive(repo_root: str):
    """Return (count, stderr_text) using the gate's own expression.

    The derived number must come from the same expression the gate uses --
    `${#STAGE_SPECS[@]}` -- and not from a second parser written here.  A second
    parser would be one more place the number is written down, and if it agreed
    with the array because it was copied from it, the comparison would be a
    constant against itself.
    """
    gate = os.path.join(repo_root, GATE_REL)
    try:
        with open(gate, "r", encoding="utf-8") as fh:
            lines = fh.read().splitlines()
    except OSError as exc:
        raise RuntimeError("cannot read %s: %s" % (GATE_REL, exc))

    start = next((i for i, l in enumerate(lines) if l.startswith("STAGE_SPECS=(")), None)
    if start is None:
        raise RuntimeError("no `STAGE_SPECS=(` line in %s" % GATE_REL)
    end = next((i for i in range(start + 1, len(lines)) if lines[i].rstrip() == ")"), None)
    if end is None:
        raise RuntimeError("the STAGE_SPECS array in %s is not closed" % GATE_REL)

    script = "\n".join(lines[start : end + 1]) + '\necho "${#STAGE_SPECS[@]}"\n'
    proc = subprocess.run(
        ["bash", "-c", script], cwd=repo_root, capture_output=True, text=True
    )
    if proc.returncode != 0 or not proc.stdout.strip().isdigit():
        raise RuntimeError(
            "could not derive the stage count from %s: rc=%d stdout=%r stderr=%r"
            % (GATE_REL, proc.returncode, proc.stdout[:200], proc.stderr[:200])
        )
    return int(proc.stdout.strip()), proc.stderr.strip()


def _read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def scan(root, derived, declared=None):
    """Return (rc, lines, ledger)."""
    declared = DECLARED if declared is None else declared
    out, reds = [], 0
    n_must = sum(1 for d in declared if d[0] == "must-equal")
    n_rec = sum(1 for d in declared if d[0] == "record")
    n_ok = 0
    located = set()  # (rel, line index) that a declaration owns

    for kind, rel, locate, npat, why in declared:
        path = os.path.join(root, rel)
        try:
            text = _read(path)
        except OSError as exc:
            out.append("  MISSING   %s: %s" % (rel, exc))
            reds += 1
            continue
        rows = text.splitlines()
        hits = [i for i, ln in enumerate(rows) if re.search(locate, ln)]
        if len(hits) != 1:
            out.append(
                "  GONE      %s: locate pattern matches %d line(s), expected exactly 1: %s"
                % (rel, len(hits), locate)
            )
            reds += 1
            continue
        idx = hits[0]
        line = rows[idx]
        located.add((rel, idx))
        found, raw = spell(npat or N_NEAR_STAGE, line)
        if found is None:
            out.append(
                "  UNSPELLED %s:%d  matched but no number was read off it  locate=%s"
                % (rel, idx + 1, locate)
            )
            reds += 1
            continue
        if kind == "record":
            out.append(
                "  record    %s:%d  spells %-3d  exempt: %s" % (rel, idx + 1, found, why)
            )
            continue
        if found == derived:
            n_ok += 1
            out.append("  ok        %s:%d  spells %-3d == %d" % (rel, idx + 1, found, derived))
        else:
            out.append(
                "  MISMATCH  %s:%d  spells %-3d but %s derives %d"
                % (rel, idx + 1, found, GATE_REL, derived)
            )
            reds += 1

    # --- the size of what was not read -----------------------------------
    undeclared, considered = [], 0
    for sub in SCAN_ROOTS:
        base = os.path.join(root, sub)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [
                d for d in dirnames if d not in (".git", "__pycache__", "node_modules")
            ]
            for name in filenames:
                if not name.endswith(SCAN_SUFFIXES):
                    continue
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, root)
                try:
                    rows = _read(full).splitlines()
                except OSError:
                    continue
                for i, ln in enumerate(rows):
                    if not N_NEAR_STAGE.search(ln):
                        continue
                    considered += 1
                    if (rel, i) not in located:
                        if rel not in ("tools/check_stage_count.py",):
                            undeclared.append("%s:%d" % (rel, i + 1))

    ledger = {
        "derived": derived,
        "declared": len(declared),
        "must_equal": n_must,
        "records": n_rec,
        "ok": n_ok,
        "reds": reds,
        "near_stage": considered,
        "undeclared": len(undeclared),
    }
    out.append("")
    out.append(
        "check_stage_count: %s derives %d stage(s); %d location(s) declared "
        "(%d must-equal, %d record); %d agree, %d red(s)."
        % (GATE_REL, derived, len(declared), n_must, n_rec, n_ok, reds)
    )
    out.append(
        "check_stage_count: the tree spells a number next to a stage word at %d "
        "location(s); %d of them are not declared here.  This check does not judge "
        "those -- it counts them, so the hole is a number and not a silence."
        % (considered, len(undeclared))
    )
    if undeclared:
        out.append(
            "check_stage_count:   first undeclared: %s" % ", ".join(undeclared[:8])
        )
    return (1 if reds else 0), out, ledger


def self_test():
    """Move the derived side and watch which declared kind reddens.

    Both halves are read here, because either half alone cannot tell "it is
    comparing" from "it reports everything":

      * a `must-equal` location that still spells the old number must REDDEN;
      * a `record` location that still spells the old number must stay GREEN.
    """
    root = tempfile.mkdtemp(prefix="stage-count-selftest-")
    os.makedirs(os.path.join(root, "tools"), exist_ok=True)
    os.makedirs(os.path.join(root, "docs"), exist_ok=True)
    with open(os.path.join(root, GATE_REL), "w", encoding="utf-8") as fh:
        fh.write(
            'STAGE_SPECS=(\n  "a|first|stage_a"\n  "b|second|stage_b"\n'
            '  "c|third|stage_c"\n)\n'
        )
    with open(os.path.join(root, "docs", "say.md"), "w", encoding="utf-8") as fh:
        fh.write(
            "三个阶段，任一失败即整体失败\n"
            "记录：当时三个阶段全绿，出处是那一次调用的逐字输出\n"
        )

    synthetic = [
        ("must-equal", "docs/say.md", r"个阶段，任一失败即整体失败", None, None),
        (
            "record",
            "docs/say.md",
            r"记录：当时.+个阶段全绿",
            None,
            "a synthetic ledger line: it says what the count was, not what it is",
        ),
    ]

    failures, led0, led1, lines1 = [], None, None, []
    try:
        derived, _ = derive(root)
        rc0, lines0, led0 = scan(root, derived, synthetic)
        if rc0 != 0:
            failures.append(
                "with derived=%d nothing should be red, got rc=%d:\n%s"
                % (derived, rc0, "\n".join(lines0))
            )
        if led0["must_equal"] != 1 or led0["records"] != 1 or led0["ok"] != 1:
            failures.append("the synthetic declarations did not land: %r" % (led0,))

        rc1, lines1, led1 = scan(root, derived + 1, synthetic)
        if rc1 != 1:
            failures.append(
                "moving the derived side to %d must redden the must-equal location, "
                "got rc=%d:\n%s" % (derived + 1, rc1, "\n".join(lines1))
            )
        if not any("MISMATCH" in ln for ln in lines1):
            failures.append("expected a MISMATCH line:\n%s" % "\n".join(lines1))
        if any("MISMATCH" in ln for ln in lines1 if "record" in ln):
            failures.append(
                "a record must never be counted as a mismatch:\n%s" % "\n".join(lines1)
            )
        if led1["records"] != 1 or led1["reds"] != 1:
            failures.append(
                "exactly the must-equal location must redden, and the record must "
                "survive the move: %r" % (led1,)
            )
    except Exception as exc:  # noqa: BLE001
        failures.append("the self-test could not measure: %s: %s" % (type(exc).__name__, exc))

    print("check_stage_count --self-test")
    if failures:
        for ln in failures:
            print("  FAILED  %s" % ln)
        print("check_stage_count: self-test FAILED (%d)" % len(failures))
        return 1
    print(
        "  ok   derived=%d: %d must-equal location agrees, %d record exempt, 0 red"
        % (led0["derived"], led0["ok"], led0["records"])
    )
    print(
        "  ok   derived=%d: %d must-equal location reddens, %d record does not"
        % (led1["derived"], led1["reds"], led1["records"])
    )
    print("check_stage_count: self-test OK")
    return 0


def main(argv):
    if "--self-test" in argv:
        return self_test()

    forced = os.environ.get(DERIVED_ENV)
    if forced is not None:
        if not forced.isdigit():
            print("check_stage_count: %s=%r is not a number" % (DERIVED_ENV, forced))
            return 2
        derived = int(forced)
        stderr = ""
        print(
            "check_stage_count: ★ the derived side was FORCED to %d by %s -- this run "
            "did not read %s, so its verdict is about nothing in this tree."
            % (derived, DERIVED_ENV, GATE_REL)
        )
    else:
        try:
            derived, stderr = derive(REPO_ROOT)
        except RuntimeError as exc:
            print("check_stage_count: could not measure: %s" % exc)
            print("check_stage_count: rc=2 -- this is the check failing, NOT a verdict")
            return 2

    try:
        rc, out, _ = scan(REPO_ROOT, derived)
    except Exception as exc:  # noqa: BLE001 -- a crash must not look like a verdict
        print("check_stage_count: could not measure: %s: %s" % (type(exc).__name__, exc))
        print("check_stage_count: rc=2 -- this is the check failing, NOT a verdict")
        return 2

    print(
        "check_stage_count: question = does the number you wrote down still equal "
        "the number there is?"
    )
    if stderr:
        print(
            "check_stage_count: the array's own expansions wrote %d line(s) to stderr "
            "(expected: $FAST is unset outside the gate); the count was still read."
            % len(stderr.splitlines())
        )
    for ln in out:
        print(ln)
    if rc:
        print("check_stage_count: FAILED")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
