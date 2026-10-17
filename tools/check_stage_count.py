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
down in the places the DECLARED list below names -- and that list, not a number
in this sentence, is the count.  This sentence used to say "eighteen times": it
was true when it was typed and false by the time two declarations were removed.
A sentence that states a count as of today is one more place the count is
written down, and nothing read this one.
A count that has been copied into a constant stops having
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
  0  every declared location agrees with the derived count, and no candidate is
     git's `required: N stage(s)`
  1  a `must-equal` location disagrees, or a declared location is gone, or a
     candidate is git's `required: N stage(s)`
  2  the derived count could not be read -- this is the check failing to
     measure, NOT a verdict about any file

WHY rc=1 HAS A THIRD WAY TO BE RED
-----------------------------------
A candidate whose own carrier is `required: N stage(s)` is not a claim about the
gate at all, so it must not be a candidate.  Before this, those candidates fell
into the "not declared here" count and were never judged: the check printed
`0 red(s)`, and that zero was real, but it was not "there is no homograph" -- it
was "the homograph is in the bucket that is not judged".  A green produced by
where something sits is the same failure as a constant that has no reader.
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
    r"(?<![第每这那])(?P<n>%s)\s*(?:个)?\s*(?:阶段|stages?)" % NUM_ANY
)

# A number next to a stage word is a claim about the gate's stages -- except
# when the word is git's.  `required: N stage(s)` is the gate counting the paths
# a change touches (`git diff --cached --name-only`), and to a predicate that
# only looks at adjacency it reads as a stage count.  It is the same word in the
# same slot meaning a different thing, so no amount of widening the alternation
# finds it.
#
# The signature carries `N` and `$n` as well as digits on purpose.  Digits cover
# the ten places that PRINT the number; `N` and `$n` cover the five that print
# the CODE that prints it.  A signature written for the output misses the
# definition, and the definition lands in the residual -- which is where this
# shape was, at 12 of 15, until a control run before the work started caught it.
GIT_INDEX_STAGE = re.compile(r"required:\s*(?:[0-9]+|\$n|N)\s+stage\(s\)")

# The same word as a verb: git asking what it would stage.
GIT_VERB_STAGE = re.compile(r"would\s+(?:[a-z]+\s+)?stage\b")

# Every `stage`/`stages` token, for the census below.
STAGE_TOKEN = re.compile(r"\bstage(?:s)?\b")

# The phrases that can only be the gate's stages.  Keyed on the PHRASE the token
# sits in -- never on the line and never on the file.  A file-level attribute was
# tried and is false: of the 106 tokens in tools/gate.sh only 11 are the gate's,
# and of the 10 in tools/check_line_refs.py none are.
GATE_STAGE_PHRASES = (
    ("stages ran", re.compile(r"stages ran")),
    ("N/M stages", re.compile(r"[0-9]+/[0-9]+ stages")),
    ("STAGE_*", re.compile(r"\bSTAGE_(?:SPECS|NAMES|WANTED)\b")),
    ("--only", re.compile(r"--only")),
    ("stage(s)", re.compile(r"stage\(s\)")),
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
    # Removed, not moved.  docs/AUDIT.md:10824 and :10832 spell the phrase
    # "sixteen BRANCHES all green", and the character they use is this tree's
    # branch word (see "one branch green", "that branch", "three branches read
    # branch by branch").  It is not a stage word.  Both declarations were
    # manufactured by it sitting in the stage-word alternation, which also
    # manufactured 107 of 288 candidates.  Both keep their `16`: it was never
    # the gate's stage count.
    (
        "record",
        "docs/AUDIT.md",
        r"带环境变量的那一次，.+支全绿",
        r"(?P<n>16)/16 stages ran",
        "an append-only ledger entry: it carries the run's own verbatim output as its evidence",
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
SELF_REL = "tools/check_stage_count.py"
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
    undeclared, considered, homographs = [], 0, []
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
                if rel == SELF_REL:
                    # Excluded from the COUNT as well as from the LIST.  The
                    # count used to be taken before this skip, so the printed
                    # denominator and the printed list were computed under two
                    # different rules and differed by this file's own lines.
                    continue
                for i, ln in enumerate(rows):
                    if not N_NEAR_STAGE.search(ln):
                        continue
                    considered += 1
                    if (rel, i) not in located:
                        undeclared.append("%s:%d" % (rel, i + 1))
                    # A candidate is a claim about the gate's stages, so one
                    # whose own carrier is git's `required: N stage(s)` is a
                    # red.  Tested by SPAN CONTAINMENT, not by "the line holds
                    # required:".  A line can hold two of them -- the ledger
                    # line that reads `required: 15 stage(s)` and then
                    # `required: 1 stage(s)` -- and a line-level test sees only
                    # the first, which makes the second look like an unrelated
                    # match.  That is how "should redden 5" was predicted for a
                    # rule that reddens 8.
                    git_spans = [g.span() for g in GIT_INDEX_STAGE.finditer(ln)]
                    for m in N_NEAR_STAGE.finditer(ln):
                        if any(s <= m.start() and m.end() <= e for s, e in git_spans):
                            homographs.append(
                                "%s:%d  col=%d  %r"
                                % (rel, i + 1, m.start(), m.group(0))
                            )
    reds += len(homographs)

    ledger = {
        "derived": derived,
        "declared": len(declared),
        "must_equal": n_must,
        "records": n_rec,
        "ok": n_ok,
        "reds": reds,
        "near_stage": considered,
        "undeclared": len(undeclared),
        "homographs": len(homographs),
    }
    out.append("")
    if homographs:
        out.append(
            "check_stage_count: %d candidate(s) are git's `required: N stage(s)` -- "
            "the paths a change touches, not the gate's stages.  A candidate is a "
            "claim about the gate, so each one is a red." % len(homographs)
        )
        for h in homographs:
            out.append("  HOMOGRAPH %s" % h)
    else:
        out.append(
            "check_stage_count: no candidate is git's `required: N stage(s)`."
        )
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


def token_census(root):
    """Split every `stage`/`stages` token by the phrase it sits in.

    Context, not a verdict: these tokens are not "a number written down", so
    this section does not move the exit code.

    It exists because "the gate's stages" was the RESIDUAL of the earlier
    census -- everything that did not match a non-gate signature was counted as
    the gate's, which is the same as saying "I found no other meaning", which is
    the disease this file was written to treat.  A signature can be stated and
    falsified; a residual cannot.

    Known boundary, stated rather than hidden: the narrower a signature, the
    more it misses, and the wider, the more it mislabels.  Two real gate-stage
    tokens fall outside the signatures below -- the comment in tools/gate.sh
    reading "print the stages that change forces", and the line in
    tools/check_line_refs.py reading "The gate stage asserts DENOMINATORS".  A
    sentence saying what a gate stage asserts landed in the bucket for tokens
    with no denominator to assert anything about.
    """
    git_index, gate_stage, rest = [], [], []
    sig_lines = {n: 0 for n, _ in GATE_STAGE_PHRASES}
    sig_tokens = {n: 0 for n, _ in GATE_STAGE_PHRASES}
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
                if rel == SELF_REL:
                    continue
                try:
                    rows = _read(full).splitlines()
                except OSError:
                    continue
                for i, ln in enumerate(rows):
                    git_spans = [g.span() for g in GIT_INDEX_STAGE.finditer(ln)]
                    git_spans += [g.span() for g in GIT_VERB_STAGE.finditer(ln)]
                    gate_spans = [
                        m.span() for _, p in GATE_STAGE_PHRASES for m in p.finditer(ln)
                    ]
                    tokens = list(STAGE_TOKEN.finditer(ln))
                    # A signature can match a line and contain NONE of the
                    # tokens it claims.  `--only` is a flag and never holds the
                    # word; `STAGE_SPECS` is upper case and the token pattern is
                    # not.  Counting matched LINES would give 69 and 42 for
                    # those two, which is a number about lines wearing the label
                    # of a number about words.
                    for n, p in GATE_STAGE_PHRASES:
                        if p.search(ln):
                            sig_lines[n] += 1
                            sp = [q.span() for q in p.finditer(ln)]
                            sig_tokens[n] += sum(
                                1
                                for t in tokens
                                if any(s <= t.start() and t.end() <= e for s, e in sp)
                            )
                    for m in tokens:
                        where = "%s:%d col=%d" % (rel, i + 1, m.start())
                        if any(s <= m.start() and m.end() <= e for s, e in git_spans):
                            git_index.append(where)
                        elif any(s <= m.start() and m.end() <= e for s, e in gate_spans):
                            gate_stage.append(where)
                        else:
                            rest.append(where)
    per_sig = [(n, sig_lines[n], sig_tokens[n]) for n, _ in GATE_STAGE_PHRASES]
    return git_index, gate_stage, rest, per_sig


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

    # A homograph control.  This tree uses one character for "branch"; it was in
    # the stage-word alternation, and that single unvalidated alternate
    # manufactured 107 of 288 candidates and two declarations.  Undoing the
    # removal must not be quiet, so a word carrying it is asserted NOT to be a
    # candidate here.
    if N_NEAR_STAGE.search("\u6211\u4eec\u652f\u6301\u4e09\u79cd\u5e73\u53f0"):
        failures.append(
            "the branch character is not a stage word: that word must not be a candidate"
        )
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

    # Context, not a verdict: see token_census.  This section never moves rc.
    try:
        git_index, gate_stage, rest, per_sig = token_census(REPO_ROOT)
    except Exception as exc:  # noqa: BLE001
        print(
            "check_stage_count: the token census could not measure: %s: %s"
            % (type(exc).__name__, exc)
        )
    else:
        total = len(git_index) + len(gate_stage) + len(rest)
        print(
            "check_stage_count: the word itself, split by the phrase it sits in "
            "(context, not a verdict): %d token(s) -- git's index %d, the gate's "
            "stages %d, neither %d."
            % (total, len(git_index), len(gate_stage), len(rest))
        )
        print(
            "check_stage_count:   this counts the ASCII word only.  The candidate "
            "predicate above also matches the Chinese word for stage, so these "
            "tokens and those locations are two different word sets: a line can "
            "hold a candidate and no token here.  Printing the two side by side "
            "without saying so reads as two faces of one object."
        )
        for name, nlines, ntokens in per_sig:
            print(
                "check_stage_count:   %-12s matches %3d line(s) and holds %3d of the "
                "tokens it claims" % (name, nlines, ntokens)
            )
        print(
            "check_stage_count:   a signature that matches many lines and holds no "
            "token is a number about lines wearing the label of a number about words."
        )
        print(
            "check_stage_count:   the per-signature counts overlap -- one token can "
            "sit in two signatures' phrases -- while the split above is "
            "first-match-wins, so they do not sum to the gate's-stages count."
        )
        print(
            "check_stage_count:   boundary: two real gate-stage tokens sit outside "
            "every signature above -- the comment in tools/gate.sh about the stages "
            "a change forces, and the line in tools/check_line_refs.py about what a "
            "gate stage asserts.  Narrower misses more, wider mislabels more, and "
            "the corpus does not say how wide."
        )
    if rc:
        print("check_stage_count: FAILED")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
