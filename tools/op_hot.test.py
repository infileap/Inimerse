#!/usr/bin/env python3
"""Control cells for the opcode census in the interpreter (INIMERSE_HOT_OPS).

What is being pinned: the census counts what this process executed, says so for
each opcode it saw, and says *na* -- with one reason per end -- for the two ends
it cannot see from here.  Every cell runs the built binary, because the subject
is a reading produced by that binary and by nothing else.

The negative controls are the silences: no INIMERSE_HOT_OPS prints nothing,
INIMERSE_HOT_OPS=0 reads as off, and a script that never runs is not a row of
zeros -- "did not run" and "ran zero times" are different sentences.

OP-HOT-TOTAL exists because a total above zero only proves somebody was
counting.  It compares the per-op rows against a fetch counter taken at the same
switch, so a dropped or doubled row disagrees; it deliberately does not claim to
prove the switch itself was reached, which is what the silence cells and the
monotonic cell cover instead.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("INIMERSE_BIN") or (ROOT / "build" / "inimerse"))
ENV = "INIMERSE_HOT_OPS"

OP_LINE = re.compile(
    r"^OP-HOT\|op=([A-Z0-9_#]+)\|interp=(\d+)\|aot=na\|wasm=na\|why=(.+)$")
SUM_LINE = re.compile(
    r"^OP-HOT\|executed=(\d+)\|opcodes=(\d+)\|source=interp\|unmeasured=aot,wasm$")
TOTAL_LINE = re.compile(
    r"^OP-HOT-TOTAL\|fetches=(\d+)\|rows=(\d+)\|rows-match-fetches=(yes|no)\|note=(.+)$")


def refuse(why):
    print("op-hot: REFUSED|" + why, file=sys.stderr)
    print("op-hot: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


def run(script, ask=True, value="1"):
    """Run one script through the built interpreter and return stdout, stderr, rc."""
    env = dict(os.environ)
    env.pop(ENV, None)
    if ask:
        env[ENV] = value
    done = subprocess.run([str(BIN), "--no-mods", "run", str(script)],
                          capture_output=True, text=True, env=env, cwd=str(ROOT))
    return done.stdout, done.stderr, done.returncode


def census(text):
    """(per-op counts, executed, opcodes) -- None when the summary is missing."""
    counts, executed, opcodes = {}, None, None
    for line in text.splitlines():
        m = OP_LINE.match(line)
        if m:
            counts[m.group(1)] = int(m.group(2))
            continue
        s = SUM_LINE.match(line)
        if s:
            executed, opcodes = int(s.group(1)), int(s.group(2))
    if executed is None:
        return None
    return counts, executed, opcodes


def total_of(text):
    """(fetches, rows, agree) from OP-HOT-TOTAL, or None."""
    for line in text.splitlines():
        m = TOTAL_LINE.match(line)
        if m:
            return int(m.group(1)), int(m.group(2)), m.group(3)
    return None


def script_with(tmp, says):
    path = Path(tmp) / ("says-%d.im" % says)
    path.write_text("".join(chr(115) + chr(97) + chr(121) + chr(32) + chr(34) + str(i) + chr(34) + chr(10) for i in range(says)), encoding="utf-8")
    return path


def main():
    if not BIN.is_file():
        refuse("no interpreter binary at %s: this test measures the built binary, so build it first (INIMERSE_BIN overrides)" % BIN)

    cases = []

    def case(name):
        def wrap(fn):
            cases.append((name, fn))
            return fn
        return wrap

    tmp = tempfile.mkdtemp(prefix="op-hot-")

    @case("census-names-each-op-and-totals")
    def _():
        out, err, rc = run(script_with(tmp, 2))
        got = census(out)
        if got is None:
            return False, "no OP-HOT summary line (rc=%d, stderr=%r)" % (rc, err[-200:])
        counts, executed, opcodes = got
        if counts.get("OP_SAY") != 2:
            return False, "two say statements should execute OP_SAY twice, read %r" % (counts.get("OP_SAY"),)
        if sum(counts.values()) != executed:
            return False, "the rows sum to %d but the summary claims %d" % (sum(counts.values()), executed)
        if opcodes != len(counts):
            return False, "%d opcodes are printed but the summary counts %d" % (len(counts), opcodes)
        return True, "OP_SAY=%d, %d opcode(s), executed=%d, rows sum to the total" % (
            counts["OP_SAY"], opcodes, executed)

    @case("each-end-carries-its-own-reason")
    def _():
        out, _err, _rc = run(script_with(tmp, 1))
        for line in out.splitlines():
            m = OP_LINE.match(line)
            if not m:
                continue
            why = m.group(3)
            if "build time" not in why or "counting a modified object" not in why:
                return False, "the AOT reason is missing its clause: %r" % why
            if "host engine" not in why or "not in it" not in why:
                return False, "the wasm reason is missing its clause: %r" % why
            return True, "both ends read na and each end states why in the same line"
        return False, "no per-op line to read a reason from"

    @case("silent-unless-asked")
    def _():
        out, _err, rc = run(script_with(tmp, 2), ask=False)
        if "OP-HOT|" in out:
            return False, "the census printed %d line(s) without being asked" % out.count("OP-HOT|")
        return True, "no census line without %s (rc=%d)" % (ENV, rc)

    @case("zero-means-off")
    def _():
        out, _err, _rc = run(script_with(tmp, 2), ask=True, value="0")
        if "OP-HOT|" in out:
            return False, "%s=0 must read as off, and it printed a census" % ENV
        return True, "%s=0 prints nothing, so the flag is read as a request and 0 is a refusal" % ENV

    @case("total-matches-the-fetch-count")
    def _():
        out, _err, rc = run(script_with(tmp, 2))
        got, total = census(out), total_of(out)
        if got is None or total is None:
            return False, "missing the census rows or the OP-HOT-TOTAL line (rc=%d)" % rc
        counts, executed, _opcodes = got
        fetches, rows, agree = total
        if agree != "yes":
            return False, "the total line reports rows-match-fetches=no (%d fetches, %d rows)" % (fetches, rows)
        if not (fetches == sum(counts.values()) == executed):
            return False, "three numbers that must be one: fetches=%d, rows sum=%d, executed=%d" % (
                fetches, sum(counts.values()), executed)
        return True, "fetches=%d == rows sum == executed, and the line says rows-match-fetches=yes" % fetches

    @case("a-move-is-counted")
    def _():
        path = Path(tmp) / "move.im"
        path.write_text(chr(97) + chr(32) + chr(61) + chr(32) + chr(49) + chr(10) +
                        chr(105) + chr(102) + chr(32) + chr(97) + chr(32) + chr(60) + chr(32) + chr(50) +
                        chr(32) + chr(123) + chr(32) + chr(115) + chr(97) + chr(121) + chr(32) + chr(34) +
                        chr(121) + chr(34) + chr(32) + chr(125) + chr(10), encoding="utf-8")
        out, _err, rc = run(path)
        got = census(out)
        if got is None:
            return False, "no census for the script that moves a register (rc=%d)" % rc
        counts = got[0]
        if counts.get("OP_MOV", 0) < 1:
            return False, "OP_MOV should be at least 1 in a script with a register move, read %r" % (counts.get("OP_MOV"),)
        return True, "OP_MOV=%d alongside %d other opcode(s)" % (counts["OP_MOV"], len(counts) - 1)

    @case("census-grows-with-the-work")
    def _():
        one, _e1, _r1 = run(script_with(tmp, 1))
        three, _e3, _r3 = run(script_with(tmp, 3))
        a, b = census(one), census(three)
        if a is None or b is None:
            return False, "a census line was missing on one of the two scripts"
        if (a[0].get("OP_SAY"), b[0].get("OP_SAY")) != (1, 3):
            return False, "expected OP_SAY 1 vs 3, read %r vs %r" % (a[0].get("OP_SAY"), b[0].get("OP_SAY"))
        if not a[1] < b[1]:
            return False, "the longer script should execute more opcodes: %d vs %d" % (a[1], b[1])
        return True, "OP_SAY 1 -> 3, executed %d -> %d" % (a[1], b[1])

    @case("only-what-ran-is-counted")
    def _():
        broken = Path(tmp) / "broken.im"
        broken.write_text(chr(115) + chr(97) + chr(121) + chr(32) + chr(34) + "never reached" + chr(10), encoding="utf-8")
        out, _err, rc = run(broken)
        if rc == 0:
            refuse("the broken script exited 0, so this cell cannot tell a refusal from a run")
        if "OP-HOT|" in out:
            return False, ("a script that never ran printed %d census line(s); a script that did not run is not a row of zeros"
                           % out.count("OP-HOT|"))
        return True, "rc=%d and no census line: the census reports executions, not intentions" % rc

    @case("same-script-twice-is-the-same-census")
    def _():
        script = script_with(tmp, 3)
        first, _e1, _r1 = run(script)
        second, _e2, _r2 = run(script)
        if first != second:
            return False, "two runs of the same script printed different censuses"
        return True, "identical lines on both runs (%d line(s))" % first.count("OP-HOT|")

    red = 0
    for name, fn in cases:
        try:
            ok, detail = fn()
        except Exception as exc:  # a cell that throws is a red cell, not a skipped one
            ok, detail = False, "%s: %s" % (type(exc).__name__, exc)
        print("%s%s%s" % (name, " ok: " if ok else " RED: ", detail))
        red += 0 if ok else 1

    print("op_hot: cases=%d red=%d green=%d -- %s"
          % (len(cases), red, len(cases) - red,
             "none" if not red else "see the RED case(s) above"))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main())