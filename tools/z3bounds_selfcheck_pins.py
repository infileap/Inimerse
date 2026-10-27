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
reads it, and the wrapper has to (a) run it, (b) require the summary line
exactly, (c) read SELF_CHECK_EXPECT_SHA256 out of the generator and require the
clean case's printed sha256 to equal it, and (d) refuse -- exit 2, not skip --
when the pieces it needs are not installed.

(c) is the one that keeps this file honest.  The number below used to live only
in a comment, and the comment went stale while the test stayed green: a constant
asserted twice fails on the day the two disagree, a number asserted zero times
just waits to be believed.

The trees in the out paths are names, not results: tree= says which tree the
expectation was pinned on, head= says which tree this run read.
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GENERATOR = os.path.join(ROOT, "tools", "z3bounds", "gen_bounds_proof.py")

# Pinned on tree c5d44de, where the five cases pass 5/5.  The clean script's
# sha256 is dda5ff4a5fc75eb5, not the 434b93d1d0508758 this line used to carry:
# the T7 axiom ledger found that A4's second pin (src/vm/vm.c:479,
# `if (g_cur_thread->sp >= 1023) return;`) was dropped when T5 re-pinned, so
# 32/33 pinned statements were being checked.  With it back the model checks one
# more pin and the constraint is unchanged -- and the number moved.  These are
# expectations, so they are written down and compared for equality: a red here
# first asks whether the model changed or the pin drifted, and only if it is
# neither does it mean the tool broke.
PIN_TREE = "c5d44de"
EXPECT_SUMMARY = "SELFCHECK-PIN|summary|cases=5|pass=5|fail=0"
CONSTANT_NAME = "SELF_CHECK_EXPECT_SHA256"
CONSTANT_RE = re.compile(r'^%s = "([0-9a-f]{16})"\s*$' % CONSTANT_NAME, re.M)
CLEAN_RE = re.compile(r"^SELFCHECK-PIN\|case=clean\|")
EXPECT_SHA_RE = re.compile(r"expect=[^|]*?sha256=([0-9a-f]{16})")
GOT_SHA_RE = re.compile(r"got=[^|]*?sha256=([0-9a-f]{16})")

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


def read_constant():
    """(value, why) -- the number the generator itself will hold the clean run to.

    Read with a regex, not by importing the generator: importing it would run
    module-level work and make this check depend on a second program's side
    effects.  A missing or ambiguous assignment is a refusal, not a red: the
    constant is not wrong, it is unreadable, and a red would say the wrong thing.
    """
    try:
        with open(GENERATOR, encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        return None, "%s cannot be read (%s); the expected sha256 is unknown" % (GENERATOR, exc)
    found = CONSTANT_RE.findall(text)
    if len(found) != 1:
        return None, ("%s appears %d time(s) as a 16-hex assignment in %s; exactly "
                      "one was expected, so the constant cannot be trusted" % (
                          CONSTANT_NAME, len(found), GENERATOR))
    return found[0], None


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
    expected, why = read_constant()
    if expected is None:
        return refuse(why)

    print(
        "TEST|name=z3bounds-pin-selfcheck|scope=pin-drift|"
        "proves=the pinned lines still sit where the model reads them, both "
        "directions of drift are refused, and the clean case's sha256 is the "
        "constant the generator holds itself to"
    )
    print("EXPECT|tree=%s|summary=%s|clean_sha256=%s" % (PIN_TREE, EXPECT_SUMMARY, expected))
    print("CONST|file=tools/z3bounds/gen_bounds_proof.py|name=%s|value=%s" % (
        CONSTANT_NAME, expected))

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
    clean = [ln for ln in lines if CLEAN_RE.match(ln)]
    want = EXPECT_SHA_RE.findall(clean[0])[0] if clean and EXPECT_SHA_RE.findall(clean[0]) else ""
    real = GOT_SHA_RE.findall(clean[0])[0] if clean and GOT_SHA_RE.findall(clean[0]) else ""
    print("GOT|rc=%d|summary=%s" % (proc.returncode, last or "<no summary line>"))
    print("COMPARE|clean_expect=%s|clean_got=%s|constant=%s|same=%s" % (
        want or "<none>", real or "<none>", expected,
        "yes" if (want, real) == (expected, expected) else "no"))

    problems = []
    if proc.returncode != 0 or last != EXPECT_SUMMARY:
        problems.append("the summary line is not the pinned one (rc=%d)" % proc.returncode)
    if not clean:
        problems.append("the generator printed no clean case, so %s is unmeasured"
                        % CONSTANT_NAME)
    elif not real:
        problems.append("the clean case printed no got=...sha256=..., so %s is unmeasured"
                        % CONSTANT_NAME)
    else:
        if real != expected:
            problems.append("the clean case wrote sha256=%s but %s is %s; the artifact "
                            "and the constant disagree" % (real, CONSTANT_NAME, expected))
        if want != expected:
            problems.append("the clean case expected sha256=%s but %s is %s; the "
                            "generator's two copies of the number disagree"
                            % (want or "<none>", CONSTANT_NAME, expected))
    if problems:
        if proc.stderr.strip():
            for ln in proc.stderr.strip().splitlines()[-3:]:
                print("  stderr: " + ln)
        for why in problems:
            print("STATUS|FAIL|scope=pin-drift|why=%s" % why)
        print(FAILED_ADVICE, file=sys.stderr)
        return 1

    print(
        "STATUS|PASS|scope=pin-drift|note=drift in both directions is refused "
        "by the generator itself; the clean case's sha256 is the generator's own "
        "constant, so the number in the comment above cannot go stale alone"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
