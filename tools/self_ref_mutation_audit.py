#!/usr/bin/env python3
"""Does each exemption in the self-referential checker have a control?

`tools/check_self_referential_counts.py` decides a line's verdict through one
function with a list of exemptions.  Every exemption is a hole by design: it
says "this shape is not a violation".  A hole nobody has a case for is a hole
nobody has seen work, and one that was widened by accident looks exactly like
one that was always there.

Measured, 2026-11-28 (§10.292): a hand-run audit of the six exemption sites
found two with no case, and deleting either changed nothing on the tree
either.  That audit was an experiment; this is the same audit as a tool, so a
future widening or a new exemption cannot be invisible.

HOW IT DECIDES

For each named mutation it rewrites the checker's source in a temporary copy,
runs that copy's `--self-test`, and asks whether the self-test FAILED.  A
mutation that the self-test cannot see is reported.  The checker under test is
never modified: the copy lives in a scratch directory beside it, because
`REPO_ROOT` is derived from the file's own location and a copy elsewhere would
read a different tree.

Usage:
    python3 tools/self_ref_mutation_audit.py [--report]

Exit codes: 0 every mutation was caught, 1 one or more were not, 2 setup fault.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CHECKER = REPO_ROOT / "tools" / "check_self_referential_counts.py"

# (label, anchor, replacement).  Each anchor is a line or block that a
# mutation can plausibly reach; the replacement is the widened, narrowed or
# deleted form.  A mutation whose anchor is gone is a SETUP fault, not a pass:
# an audit that silently skips is the defect it exists to find.
MUTATIONS = (
    (
        "record marker removed",
        "        if RECORD_MARKER.search(line) or INLINE_TREE.search(line):\n"
        "            return None\n",
        "",
    ),
    (
        "inline tree removed",
        "        if RECORD_MARKER.search(line) or INLINE_TREE.search(line):\n"
        "            return None\n",
        "        if RECORD_MARKER.search(line):\n            return None\n",
    ),
    (
        "placeholder exemption widened",
        "        if PLACEHOLDER.search(line) and not _digits_beside_placeholder(line):",
        "        if PLACEHOLDER.search(line):",
    ),
    (
        "span exemption removed",
        "        if in_span or _position_in_span(line, m.start()):\n"
        "            return None\n",
        "",
    ),
    (
        "carried span removed",
        "        if in_span or _position_in_span(line, m.start()):\n"
        "            return None\n",
        "        if _position_in_span(line, m.start()):\n            return None\n",
    ),
    (
        "intra-line span removed",
        "        if in_span or _position_in_span(line, m.start()):\n"
        "            return None\n",
        "        if in_span:\n            return None\n",
    ),
    (
        "inline-tree pattern narrowed to one ref",
        'INLINE_TREE = re.compile(r"`[0-9a-f]{7,40}`")',
        'INLINE_TREE = re.compile(r"`4b5065fc`")',
    ),
    (
        "record marker narrowed to one ref",
        'RECORD_MARKER = re.compile(r"\\[obs:\\s*([^\\]]*)\\]")',
        'RECORD_MARKER = re.compile(r"\\[obs:\\s*(4b5065fc)\\]")',
    ),
    # A narrowing of the UPPER BOUND only: short refs still work, long ones
    # stop.  Measured 2026-11-28: no mutation covered this, and with the audit
    # at 8/8 the only thing that could see it was a human running the
    # self-test against that edit -- so an edit that also dropped the case
    # would leave every automated check green.
    # Deleting the guard that makes an empty case list red.  Measured
    # 2026-11-28: with it, `--self-test` on zero cases printed
    # `ok (0 cases)` and exited 0 -- a green produced by a deletion, which is
    # the shape this repository keeps paying for.
    # Not in MUTATIONS: the empty-case guard cannot be covered by a case -- a
    # case list that is empty cannot contain the case that notices.  It is
    # checked by `_empty_case_guard_works` below, which asks a different
    # question (does an empty list exit non-zero?) rather than the same one.
    (
        "inline-tree upper bound narrowed",
        'INLINE_TREE = re.compile(r"`[0-9a-f]{7,40}`")',
        'INLINE_TREE = re.compile(r"`[0-9a-f]{7,20}`")',
    ),
)


# The exemption sites the mutations above are meant to cover.  An audit knows
# only the mutations it lists, so a NEW exemption -- a new hole -- is invisible
# to it; measured 2026-11-28, adding an uncovered `if line.startswith("|")`
# exemption left the audit reporting a clean 6/6.  The count is asserted so the
# blind spot is a red rather than a silence.
EXPECTED_EXEMPTION_SITES = 4


def _exemption_sites(src: str) -> int:
    """How many `return None` sites `_violation_key` actually has."""
    import re
    m = re.search(r"def _violation_key.*?\n(?=\ndef )", src, re.S)
    if m is None:
        return -1
    return m.group(0).count("return None")


def _empty_case_guard_works() -> "tuple[bool, str]":
    """Deleting every case must make --self-test exit non-zero.

    Measured 2026-11-28: with no guard, `--self-test` on an empty case list
    printed `ok (0 cases)` and exited 0.  No case can catch that -- the case
    that would is in the list being deleted -- so the property is asserted
    directly, on a copy, the same way the mutations are.
    """
    import re
    src = CHECKER.read_text(encoding="utf-8")
    m = re.search(r"    cases = \[\n(.*?)\n    \]\n", src, re.S)
    if m is None:
        return False, "could not find the case list"
    with tempfile.TemporaryDirectory(dir=CHECKER.parent) as tmp:
        copy = Path(tmp) / CHECKER.name
        copy.write_text(src.replace(m.group(1), ""), encoding="utf-8")
        r = subprocess.run(
            [sys.executable, str(copy), "--self-test"],
            capture_output=True, text=True, cwd=str(REPO_ROOT),
        )
    if r.returncode == 0:
        return False, "an empty case list exits 0"
    return True, "an empty case list exits non-zero"


def audit() -> dict:
    src = CHECKER.read_text(encoding="utf-8")
    results = []
    # The copy goes BESIDE the original so REPO_ROOT resolves to the same tree;
    # a temp dir elsewhere would make the checker read a different checkout and
    # every mutation would look caught for the wrong reason.
    with tempfile.TemporaryDirectory(dir=CHECKER.parent) as tmp:
        copy = Path(tmp) / CHECKER.name
        for label, anchor, replacement in MUTATIONS:
            if anchor not in src:
                results.append({"label": label, "outcome": "SETUP",
                                "detail": "anchor not found in the checker"})
                continue
            copy.write_text(src.replace(anchor, replacement, 1), encoding="utf-8")
            r = subprocess.run(
                [sys.executable, str(copy), "--self-test"],
                capture_output=True, text=True, cwd=str(REPO_ROOT),
            )
            results.append({
                "label": label,
                "outcome": "CAUGHT" if r.returncode != 0 else "MISSED",
                "detail": (r.stdout + r.stderr).strip().splitlines()[-1][:120]
                          if (r.stdout or r.stderr) else "",
            })
    return {"results": results}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()

    if not CHECKER.is_file():
        print(
            f"self_ref_mutation_audit: SETUP -- {CHECKER} does not exist; the "
            "audit does not test a checker that is not here",
            file=sys.stderr,
        )
        return 2

    src = CHECKER.read_text(encoding="utf-8")
    sites = _exemption_sites(src)
    if sites != EXPECTED_EXEMPTION_SITES:
        print(
            f"self_ref_mutation_audit: SETUP -- `_violation_key` has {sites} "
            f"exemption site(s) and this audit claims to cover "
            f"{EXPECTED_EXEMPTION_SITES}. A new hole is invisible to an audit "
            "that lists mutations, so the count is asserted: add a mutation "
            "and raise EXPECTED_EXEMPTION_SITES in the same edit.",
            file=sys.stderr,
        )
        return 2

    r = audit()
    ok, why = _empty_case_guard_works()
    r["results"].append({
        "label": "empty case list is a failure",
        "outcome": "CAUGHT" if ok else "MISSED",
        "detail": why,
    })
    missed = [x for x in r["results"] if x["outcome"] != "CAUGHT"]
    if args.report:
        for x in r["results"]:
            print(f"MUTATION|{x['outcome']}|{x['label']}|{x['detail']}")
    print(
        f"self_ref_mutation_audit: {len(r['results']) - len(missed)}/{len(r['results'])} "
        f"mutation(s) caught"
    )
    if missed:
        print(
            "self_ref_mutation_audit: "
            + ", ".join(f"{x['label']} ({x['outcome']})" for x in missed)
            + " -- an exemption or rule with no case is one nobody has seen work",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
