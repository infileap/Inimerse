#!/usr/bin/env python3
"""Find every place this repository writes down a quantity it also computes.

Letter 9, item 乙.  `obs-markers` was added to the gate as a sixteenth stage and
two of the three places that state the stage count as a literal did not follow.
The question that matters is not "how many stale counts are there" but **how
many of them anything asks**, because a count nothing asks can be wrong
forever in silence.

So this tool does not look for "numbers that are wrong".  It looks for
**numbers that restate a quantity the tree can compute**, and then it asks of
each one whether the tree computes it (`counterpart`) and whether anything
compares the restated copy against that computation (`asked`).

Three answers, and the middle one is the point:

  is-pin          this site IS the tree's copy of the quantity (gate.sh's
                  EXP_CTEST, the STAGE_SPECS block).  A stage compares it, so a
                  drift makes the gate red.  This is the shape that works.
  restates        this site says the same quantity again, somewhere else.  The
                  tree computes it, so one addition moves the truth and leaves
                  this copy behind -- and nothing compares the copy to the
                  truth.  `agree` vs `stale` says whether it has happened yet.
  no-counterpart  the tree does not compute this quantity at all.  It cannot
                  go stale because nothing moves it, and it cannot be caught
                  being wrong because nothing knows the answer.  This is the
                  same silence by a different route, so it is counted.

A rewrite of a number that no one can contradict is not a smaller version of
this problem; it is the same problem with the alarm disconnected.

Exit codes follow the sibling instruments: 0 = measured, 1 = measured and the
tree has a stale count, 2 = the census could not measure (a denominator of zero
is a failure to read, never a clean tree).

--self-test builds a throwaway repository and asserts each of the three answers
by name, so that a green run says which classes of site it found rather than
merely that it found none.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The denominator is stated, not implied.  A census that does not print its
# search space cannot be told apart from one that found nothing.
SUFFIXES = (".sh", ".md", ".py", ".yml", ".yaml")

CN_DIGITS = {"一": 1, "二": 2, "三": 3, "四": 4, "五": 5,
             "六": 6, "七": 7, "八": 8, "九": 9}


def cn_to_int(text: str) -> int | None:
    """Read the numerals this repository actually writes: 十五, 二十三, 十六."""
    if not text:
        return None
    if text.isdigit():
        return int(text)
    if "十" not in text:
        return None if len(text) != 1 else CN_DIGITS.get(text)
    head, _, tail = text.partition("十")
    tens = 1 if head == "" else CN_DIGITS.get(head)
    ones = 0 if tail == "" else CN_DIGITS.get(tail)
    if tens is None or ones is None:
        return None
    return tens * 10 + ones


class Quantity:
    """One quantity the repository states, plus how (or whether) it computes it.

    `claims` are regexes with exactly one group: the number as written at that
    site.  `anchor` is required to match the SAME LINE: it names the authority
    the claim is about (the gate, EXP_CTEST, add_test, tools/check_*).

    The anchor is not decoration, it is the difference between a census and a
    word-frequency count.  Without it, "3.2 case try" is three stages, the `0`
    in "0 tests failed out of 157" is a test count, and `make -j2 check` is two
    checkers.  With it, the tool finds only claims that name their own subject.

    ★ And that is the finding, not a workaround: a count written down WITHOUT
    naming its authority is invisible to any text search over this repository,
    which is the same silence 乙 is trying to count, arriving by the other
    door.  The denominator below is anchored claims and says so.

    `counterpart` returns the tree's own value, or None when the tree does not
    compute it -- and None is a *finding*, not an excuse.
    """

    def __init__(self, name, noun, claims, anchor, counterpart, pin_span=None,
                 provenance="", askers=()):
        self.name = name
        self.noun = noun
        self.claims = [re.compile(c) for c in claims]
        self.anchor = re.compile(anchor)
        self.counterpart = counterpart
        self.pin_span = pin_span  # (relpath, first_line, last_line) or None
        # How the tree's own value was obtained.  One of exactly four
        # spellings, and they are never merged: `index` (git ls-files),
        # `worktree` (an open() of a file as it sits on disk),
        # `commit:<sha>` (a blob read out of a commit) or `source-literal`
        # (a number that is written in the source and computed from nothing).
        # The same N in the same sentence can be any of the four, which is the
        # finding, not a labelling detail.
        self.provenance = provenance
        # The things that would notice this quantity changing, named one by
        # one.  An empty list is the answer that matters most and is printed as
        # such: `none` is a count of instruments that do not exist.
        self.askers = list(askers)


def _read_stage_count(root):
    """The gate's stage table, counted rather than copied.

    `tools/gate.sh` says the stage list is never written down twice; this reads
    that one copy.
    """
    path = os.path.join(root, "tools", "gate.sh")
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return None
    m = re.search(r"^STAGE_SPECS=\(\n(.*?)^\)", text, re.M | re.S)
    if not m:
        return None
    body, start = m.group(1), text[:m.start()].count("\n") + 1
    rows = [ln for ln in body.splitlines() if ln.strip().startswith('"')]
    return len(rows), ("tools/gate.sh", start, start + len(rows))


def _read_exp(name):
    def reader(root):
        path = os.path.join(root, "tools", "gate.sh")
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                for n, line in enumerate(fh, 1):
                    if line.startswith(name + '="'):
                        m = re.search(r":-(\d+)\}", line)
                        if m:
                            return int(m.group(1)), ("tools/gate.sh", n, n)
        except OSError:
            return None
        return None
    return reader


def _read_checker_count(root):
    out = _ls_files(root)
    if out is None:
        return None
    rows = [p for p in out if p.startswith("tools/check_")
            and os.path.basename(p) != ""]
    return len(rows), None


def _ls_files(root):
    try:
        res = subprocess.run(["git", "ls-files", "-z"], cwd=root,
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return [p for p in res.stdout.split("\0") if p]


def quantities(root):
    """The table, with each counterpart read once, at this root."""
    stage = _read_stage_count(root) if root else None
    qs = [
        Quantity(
            "stages", "gate stages",
            [r"(\d{1,3}|[一二三四五六七八九十]{1,3})\s*个?\s*阶段",
             r"\b(\d{1,3})\s+(?:gate\s+)?stages?\b"],
            r"gate\.sh|gate:|STAGE_SPECS",
            (lambda r, s=stage: s) if stage else (lambda r: None),
            stage[1] if stage else None,
            provenance="worktree:tools/gate.sh (open() of the file on disk)",
            askers=[],
        ),
        Quantity(
            "ctest", "CTest cases",
            [r"EXP_CTEST[^0-9\n]{0,24}(\d{1,4})",
             r"\bexpect\s+(\d{2,4})\s*/\s*\d{2,4}\b"],
            r"EXP_CTEST|add_test|\bctest\b",
            _read_exp("EXP_CTEST"),
            None,
            provenance="worktree:tools/gate.sh:149",
            askers=[
                "tools/gate.sh:243 -- grep -q \"0 tests failed out of "
                "$EXP_CTEST\" on the ctest stage's own output",
                "tools/gate.sh:246 -- names `docs/BOARD.md 3` in the message it "
                "prints when that comparison fails",
            ],
        ),
        Quantity(
            "node", "node suites",
            [r"EXP_NODE[^0-9\n]{0,24}(\d{1,3})"],
            r"EXP_NODE",
            _read_exp("EXP_NODE"),
            None,
            provenance="worktree:tools/gate.sh:153",
            askers=[
                "tools/gate.sh:407 -- [ \"$ran\" -ne \"$EXP_NODE\" ] against the "
                "suite count the runner actually reported",
                "tools/gate.sh:410 -- names `docs/BOARD.md 3` on failure",
            ],
        ),
        Quantity(
            "checkers", "checker programs",
            [r"(\d{1,3})\s*个?\s*check(?:er)?s?\b"],
            r"tools/check_",
            _read_checker_count,
            None,
            provenance="index:git ls-files (not the working tree)",
            askers=[],
        ),
    ]
    return qs


def scan(root):
    """Return (rc, lines, findings) for one root."""
    lines: list[str] = []
    files = _ls_files(root)
    if files is None:
        lines.append(f"static_counts_census: {root} is not a git repository, so "
                     f"the denominator cannot be read; an unreadable tree is "
                     f"not an empty one")
        return 2, lines, []
    denom = [p for p in files if p.endswith(SUFFIXES)]
    if not denom:
        # An empty search space is not a clean tree.  Zero sites over zero files
        # reads exactly like a pass, and that is the one reading this census
        # must never produce: it would be reporting a number about nothing.
        lines.append(
            f"static_counts_census: {len(files)} tracked file(s) under {root}, "
            f"and none of them ends in {'/'.join(SUFFIXES)}.  The search space is "
            f"empty, so there is nothing here to measure and this is not a pass; "
            f"rc=2 -- an empty denominator is not a clean one"
        )
        return 2, lines, []
    qs = quantities(root)
    for q in qs:
        got = q.counterpart(root)
        if got is None:
            q.truth, q.pin = None, q.pin_span
        elif isinstance(got, tuple):
            q.truth, q.pin = got[0], got[1] or q.pin_span
        else:
            q.truth, q.pin = got, q.pin_span

    findings = []
    unanchored = 0
    for rel in denom:
        try:
            with open(os.path.join(root, rel), encoding="utf-8",
                      errors="replace") as fh:
                body = fh.read()
        except OSError as exc:
            lines.append(f"static_counts_census: cannot read {rel}: {exc}")
            return 2, lines, []
        for n, line in enumerate(body.splitlines(), 1):
            for q in qs:
                anchored = bool(q.anchor.search(line))
                for pat in q.claims:
                    # Every match, not the first one.  A line that states the
                    # same quantity twice ("fifteen stages before, sixteen
                    # after") is a line with two sites, and `break` would have
                    # silently made it one -- a denominator shrunk by an
                    # implementation detail is the failure this whole census is
                    # about.
                    for m in pat.finditer(line):
                        val = cn_to_int(m.group(1))
                        if val is None:
                            continue
                        if not anchored:
                            unanchored += 1
                            continue
                        pin = q.pin
                        if pin and rel == pin[0] and pin[1] <= n <= pin[2]:
                            klass = "is-pin"
                        elif q.truth is None:
                            klass = "no-counterpart"
                        elif val == q.truth:
                            klass = "restates-agree"
                        else:
                            klass = "restates-stale"
                        findings.append({
                            "file": rel, "line": n, "quantity": q.name,
                            "claimed": val, "truth": q.truth, "class": klass,
                            "text": line.strip()[:110],
                        })

    by_class: dict[str, int] = {}
    for f in findings:
        by_class[f["class"]] = by_class.get(f["class"], 0) + 1
    computed = sum(v for k, v in by_class.items() if k.startswith("restates")
                   or k == "is-pin")
    lines.append(
        f"static_counts_census: {len(findings)} site(s) that write down a "
        f"quantity, in {len({f['file'] for f in findings})} file(s), over a "
        f"denominator of {len(denom)} tracked .sh/.md/.py/.yml/.yaml file(s) "
        f"(of {len(files)} tracked file(s)).  The denominator is printed "
        f"because a site count over an unstated search space is not a "
        f"measurement"
    )
    lines.append(
        f"static_counts_census: {computed} of them restate a quantity this tree "
        f"actually computes, so one addition to that quantity moves the truth "
        f"under them: {by_class.get('is-pin', 0)} are the tree's own copy and a "
        f"stage compares them, {by_class.get('restates-stale', 0)} already "
        f"disagree with it, {by_class.get('restates-agree', 0)} agree with it "
        f"today and are compared by nothing.  {by_class.get('no-counterpart', 0)} "
        f"write down a quantity the tree never computes at all, so they can "
        f"neither go stale nor be caught being wrong"
    )
    lines.append(
        f"static_counts_census: asked-by-something -- the number of these sites "
        f"that anything compares against the tree's own value is "
        f"{by_class.get('is-pin', 0)} (those are the pins).  The other "
        f"{len(findings) - by_class.get('is-pin', 0)} are read by nothing, "
        f"which is a count of instruments that do not exist and not a count of "
        f"mistakes"
    )
    # `nothing` is an answer, and it has to be printed as a named one.  A column
    # reading `2 asked / 6 not asked` is a sentence with no object: the reader
    # cannot tell a quantity guarded by a stage from one nobody has ever
    # compared to anything.
    for q in qs:
        where = q.provenance or "unstated"
        if q.askers:
            lines.append(
                f"static_counts_census: askers for {q.name} -- {len(q.askers)} "
                f"thing(s) read it; the tree's value comes from {where}:"
            )
            for a in q.askers:
                lines.append(f"    asker  {a}")
        else:
            lines.append(
                f"static_counts_census: askers for {q.name} -- NONE.  Nothing in "
                f"this tree reads this quantity, so no addition can make it "
                f"fail; the tree's value comes from {where}.  `none` is a count "
                f"of instruments that do not exist"
            )
    lines.append(
        f"static_counts_census: NOT-counted -- {unanchored} claim(s) of the same "
        f"shape sit on a line that never names the authority they are about (no "
        f"gate.sh/STAGE_SPECS, no EXP_CTEST/add_test/ctest, no tools/check_).  A "
        f"count written down without naming its subject cannot be told apart from "
        f"prose about somebody else's numbers -- not by this tool and not by any "
        f"other text search over this repository.  They are reported as a number "
        f"here precisely because they are the population that no instrument is "
        f"able to even find, which is the same silence this census set out to "
        f"count, arriving by the other door"
    )
    for f in findings:
        if f["class"] in ("restates-stale", "no-counterpart"):
            lines.append(
                f"  {f['file']}:{f['line']}: {f['quantity']} = "
                f"{f['claimed']} ({f['class']}"
                + (f", tree says {f['truth']}" if f["truth"] is not None else "")
                + f") -- {f['text']}"
            )
    stale = by_class.get("restates-stale", 0)
    return (1 if stale else 0), lines, findings


def self_test() -> int:
    """Assert each class by name, so the labels are not decoration."""
    bad = 0

    def want(cond, msg):
        nonlocal bad
        if not cond:
            bad = 1
            print(f"static_counts_census: self-test FAILED -- {msg}",
                  file=sys.stderr)

    tmp = tempfile.mkdtemp(prefix="static-counts-")
    os.makedirs(os.path.join(tmp, "tools"))
    os.makedirs(os.path.join(tmp, "docs"))
    with open(os.path.join(tmp, "tools", "gate.sh"), "w", encoding="utf-8") as fh:
        fh.write('EXP_CTEST="${EXP_CTEST:-7}"\nSTAGE_SPECS=(\n'
                 '  "build|build|x"\n  "ctest|ctest|y"\n)\n')
    with open(os.path.join(tmp, "docs", "one.md"), "w", encoding="utf-8") as fh:
        fh.write("The tools/gate.sh table has 2 stages and EXP_CTEST is 7 tests.\n"
                 "The tools/gate.sh table used to have 3 stages.\n"
                 "Another project has 4 stages.\n")
    subprocess.run(["git", "init", "-q"], cwd=tmp, check=True)
    subprocess.run(["git", "-C", tmp, "add", "-A"], check=True)

    rc, lines, findings = scan(tmp)
    for line in lines:
        print(line)
    classes = {}
    for f in findings:
        classes.setdefault(f["class"], []).append(f)
    print(f"static_counts_census: self-test rc={rc} classes="
          f"{ {k: len(v) for k, v in classes.items()} }")

    want(len(classes.get("restates-agree", [])) == 2,
         "the two sites that agree with the tree's own counts were not both "
         "classified restates-agree, so `agree` is not measured")
    want(all(f["claimed"] in (2, 7) for f in classes.get("restates-agree", [])),
         "a restates-agree site does not carry the number it restated")
    want(len(classes.get("restates-stale", [])) == 1
         and classes["restates-stale"][0]["claimed"] == 3,
         "the site claiming three stages was not classified restates-stale, so "
         "the only class that can redden the tree is not measured")
    # ★ The anchor is not decoration, and this is the control that says so: a
    # claim of exactly the shape this census hunts, on a line that never names
    # the authority it is about.  It must NOT appear in `findings`.  Drop the
    # anchor and this line becomes a third `restates-stale`, which is exactly
    # how a word-frequency count reads it.  A control that should have been
    # caught and was not is worth more than one that was.
    want(not any(f["file"] == "docs/one.md" and f["line"] == 3
                 for f in findings),
         "the unanchored claim on docs/one.md:3 was counted as a site; an "
         "anchor that lets a line naming no authority through is decoration")
    want(any("NOT-counted -- 1 claim" in ln for ln in lines),
         "the unanchored claim is not reported in the NOT-counted line, so the "
         "population no text search can find is not counted anywhere")
    # And `none` has to be a printed answer, not an empty column.
    want(any("askers for stages -- NONE" in ln for ln in lines),
         "the stage count has no asker, and the run does not say so; `not "
         "asked` printed as a blank column is the silence this census counts")
    want(rc == 1, f"a tree with a stale count returned rc={rc}, not 1; a stale "
                  f"count that does not redden anything is a report nobody reads")

    # The whole point: pull the counterpart out and the classification must
    # change, or `counterpart` was never being read.
    with open(os.path.join(tmp, "tools", "gate.sh"), "w", encoding="utf-8") as fh:
        fh.write("# the stage table is gone\n")
    subprocess.run(["git", "-C", tmp, "add", "-A"], check=True)
    rc2, lines2, f2 = scan(tmp)
    for line in lines2:
        print(line)
    nc = [f for f in f2 if f["class"] == "no-counterpart"]
    print(f"static_counts_census: self-test rc={rc2} no-counterpart={len(nc)}")
    want(len(nc) == len(f2) and f2,
         f"with the counterpart removed, {len(nc)} of {len(f2)} sites were "
         f"classified no-counterpart; a class that does not move when its input "
         f"is deleted is a label, not a measurement")
    want(rc2 == 0,
         f"a tree whose quantities are computed nowhere returned rc={rc2}, not "
         f"0: with nothing to contradict them there is nothing to be stale "
         f"against, which is a different statement and must not print as red")

    # ★ An empty search space must show itself.  Measured with a substitute
    # rather than argued from the source: a repository whose tracked files carry
    # none of the suffixes.  `0 sites over 0 files` is the one reading this
    # census must never produce -- it is a pass about nothing, and a pass is
    # what a reader takes from it.
    tmp_e = tempfile.mkdtemp(prefix="static-counts-empty-")
    with open(os.path.join(tmp_e, "notes.txt"), "w", encoding="utf-8") as fh:
        fh.write("the gate has 16 stages\n")
    subprocess.run(["git", "init", "-q"], cwd=tmp_e, check=True)
    subprocess.run(["git", "-C", tmp_e, "add", "-A"], check=True)
    rcE, linesE, fE = scan(tmp_e)
    for line in linesE:
        print(line)
    print(f"static_counts_census: self-test empty-denominator rc={rcE} "
          f"sites={len(fE)}")
    want(rcE == 2 and not fE,
         f"a tree with no .sh/.md/.py/.yml file returned rc={rcE} with "
         f"{len(fE)} site(s), not rc=2 with 0; zero sites over a zero "
         f"denominator reads as a pass and it is a number about nothing")
    want(any("search space is empty" in ln for ln in linesE),
         "the empty denominator was not named, so rc=2 arrives without the "
         "reader being able to tell it from a crash")

    print("static_counts_census: self-test ok -- the two agreeing sites are "
          "counted, the stale one is counted AND reddens the run, and deleting "
          "the tree's own copy of the quantity moves every site to "
          "no-counterpart instead of leaving the labels where they were")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=REPO_ROOT)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"static_counts_census: {root} is not a directory; an "
              f"unmeasurable root is not an empty one", file=sys.stderr)
        return 2
    try:
        rc, lines, _ = scan(root)
    except Exception as exc:  # noqa: BLE001 -- the contract is 2, not a traceback
        print(f"static_counts_census: could not measure {root}: "
              f"{type(exc).__name__}: {exc}", file=sys.stderr)
        print("static_counts_census: rc=2 -- this is the census failing, NOT a "
              "verdict about any file", file=sys.stderr)
        return 2
    for line in lines:
        print(line)
    return rc


if __name__ == "__main__":
    sys.exit(main())
