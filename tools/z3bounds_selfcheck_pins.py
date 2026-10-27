#!/usr/bin/env python3
"""The prover's own pin self-check, registered so a drift cannot pass unmeasured.

Why this wrapper exists

tools/z3bounds/gen_bounds_proof.py --self-check-pins builds five variants of
one tree in a scratch copy: the clean tree, a line shifted by one, a character
changed, a truncated file, and a file with an unpinned extra line.  Two of them
must be refused (exit 2, the drifted pin named, the out file untouched) and two
must still write (the clean tree and the unpinned line), with the clean one
writing a script whose sha256 is a constant.  That covers both directions of
drift, which is the only thing a pin can be wrong about.

It needs no z3 and no engine.  That is why it can be a registered test: the
gate has no z3, so a z3 test there would be a skip, and a skip is not a pass.
The generator refuses on its own when a pin no longer sits where the model
reads it, and the wrapper only has to (a) run it, (b) require the summary line
exactly, and (c) refuse -- exit 2, not skip -- when the pieces it needs are not
installed.

The trees in the out paths are names, not results: tree= says which tree the
expectation was pinned on, head= says which tree this run read.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GENERATOR = os.path.join(ROOT, "tools", "z3bounds", "gen_bounds_proof.py")

# Pinned on tree c5d44de, where the five cases pass 5/5 and the clean script's
# sha256 is 434b93d1d0508758.  These are expectations, so they are written down
# and compared for equality: a red here first asks whether the model changed or
# the pin drifted, and only if it is neither does it mean the tool broke.
PIN_TREE = "c5d44de"
EXPECT_SUMMARY = "SELFCHECK-PIN|summary|cases=5|pass=5|fail=0"

FAILED_ADVICE = (
    "红了先问：模型变了，还是 pin 漂了？两者都不是，才是工具坏了。\n"
    "(ask first whether the model changed or the pin drifted; only if it is "
    "neither does it mean the tool broke.)"
)


def refuse(why):
    """Exit 2 with the words the gate stage looks for -- never a silent skip."""
    print("z3bounds-pin-selfcheck: REFUSED|%s" % why, file=sys.stderr)
    print("not registered: nothing was checked", file=sys.stderr)
    return 2


def short_head():
    try:
        out = subprocess.run(
            ["git", "-C", ROOT, "rev-parse", "--short=7", "HEAD"],
            capture_output=True, text=True, encoding="utf-8",
        )
    except OSError:
        return "<git not runnable>"
    return out.stdout.strip() or "<no head>"


def main():
    if shutil.which("git") is None:
        return refuse("git is not on PATH; the pins are line numbers in a tree, and git is what names the tree")
    if not os.path.isfile(GENERATOR):
        return refuse("%s is not here, so no pin was read" % GENERATOR)

    print(
        "TEST|name=z3bounds-pin-selfcheck|scope=pin-drift|"
        "proves=the pinned lines still sit where the model reads them, and both "
        "directions of drift are refused"
    )
    print("EXPECT|tree=%s|summary=%s" % (PIN_TREE, EXPECT_SUMMARY))

    proc = subprocess.run(
        [sys.executable, GENERATOR, "--self-check-pins"],
        cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
    )
    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    last = lines[-1].strip() if lines else ""
    print("HEAD|tree=%s" % short_head())
    for ln in lines:
        if ln.startswith("SELFCHECK-PIN|case=") or ln.startswith("SELFCHECK-PIN|refuse"):
            print("  " + ln)
    print("GOT|rc=%d|summary=%s" % (proc.returncode, last or "<no summary line>"))

    if proc.returncode != 0 or last != EXPECT_SUMMARY:
        if proc.stderr.strip():
            for ln in proc.stderr.strip().splitlines()[-3:]:
                print("  stderr: " + ln)
        print("STATUS|FAIL|scope=pin-drift|why=the summary line is not the pinned "
              "one (rc=%d)" % proc.returncode)
        print(FAILED_ADVICE, file=sys.stderr)
        return 1

    print(
        "STATUS|PASS|scope=pin-drift|note=drift in both directions is refused "
        "by the generator itself; this wrapper adds only the exact summary line "
        "and a refusal when git or the generator is missing"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
