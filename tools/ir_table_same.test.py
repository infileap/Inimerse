#!/usr/bin/env python3
"""Control cells for the IR-TABLE same-run (INIMERSE_IR_TABLE).

What is being pinned: on every fetch the table and the switch answer twice, and
the two answers are independent facts -- the switch says which body it sent the
instruction to, the table says which body should serve that opcode.  A row that
disagrees is a defect in one of them, so the readout names the opcode, the
program and the ip instead of only counting the disagreement.

The mutation cell is the control that keeps this from being a counter of
events: with one table slot pointed at a neighbouring body, the switch is still
right, so a red row can only come from the table -- and it must name OP_ADD.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("INIMERSE_BIN") or (ROOT / "build" / "inimerse"))
ENV = "INIMERSE_IR_TABLE"
MUT = "INIMERSE_IR_TABLE_MUTATE"

ROW = re.compile(r"^IR-TABLE-SAME\|op=([A-Z0-9_#]+)\|table=(\d+)\|switch=(\d+)\|first-diff=(none|yes)$")
SUM = re.compile(r"^IR-TABLE-SAME\|opcodes=(\d+)\|compared=(\d+)\|table=(\d+)\|switch=(\d+)\|mismatch=(\d+)$")
DIFF = re.compile(r"^IR-TABLE-DIFF\|op=([A-Z0-9_]+)\|prog=(.+?)\|ip=(\d+)\|table=([A-Z0-9_]+)\|switch=([A-Z0-9_]+)\|effect=(.+)$")


def refuse(why):
    print("ir_table_same: REFUSED|" + why, file=sys.stderr)
    print("ir_table_same: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


if not BIN.is_file():
    refuse("no interpreter at %s (build it, or set INIMERSE_BIN)" % BIN)


def run(script, ask=True, value="1", mutate=None):
    env = dict(os.environ)
    env.pop(ENV, None)
    env.pop(MUT, None)
    if ask:
        env[ENV] = value
    if mutate:
        env[MUT] = mutate
    done = subprocess.run([str(BIN), "--no-mods", "run", str(script)],
                          capture_output=True, text=True, env=env, cwd=str(ROOT))
    return done.stdout, done.stderr, done.returncode


def compile_to(script, out):
    env = dict(os.environ)
    env.pop(ENV, None)
    subprocess.run([str(BIN), "--no-mods", "compile", str(script), str(out)],
                   capture_output=True, text=True, env=env, cwd=str(ROOT))


def readout(text):
    """(rows, summary, diffs) -- summary is None when the run printed none."""
    rows, summary, diffs = {}, None, []
    for line in text.splitlines():
        m = ROW.match(line)
        if m:
            rows[m.group(1)] = (int(m.group(2)), int(m.group(3)), m.group(4))
            continue
        m = SUM.match(line)
        if m:
            summary = dict(opcodes=int(m.group(1)), compared=int(m.group(2)),
                           table=int(m.group(3)), switch=int(m.group(4)),
                           mismatch=int(m.group(5)))
            continue
        m = DIFF.match(line)
        if m:
            diffs.append(dict(op=m.group(1), prog=m.group(2), ip=int(m.group(3)),
                              table=m.group(4), switch=m.group(5), effect=m.group(6)))
    return rows, summary, diffs


TMP = Path(tempfile.mkdtemp(prefix="ir_table_same."))


def program(name, body):
    p = TMP / name
    p.write_text(body)
    return p


ADD1 = program("add1.im", 'a = 1\nb = a + 2\nsay b\n')
ADD3 = program("add3.im", 'a = 1\nb = a + 2\nc = b + 3\nd = c + 4\nsay d\n')
BAD = program("bad.im", 'a = (\nsay\n')


def cell_every_op_agrees():
    """Every row of every program in the corpus has table == switch, and the
    summary totals equal the sum of the rows."""
    scripts = sorted((ROOT / "vtest").glob("*.im")) + sorted((ROOT / "examples").rglob("*.im"))
    if len(scripts) < 50:
        return False, "corpus too small: %d scripts" % len(scripts)
    for s in scripts:
        out, _err, rc = run(s)
        rows, summary, diffs = readout(out)
        if summary is None:
            continue  # a script the interpreter refuses to run is another cell's business
        if summary["mismatch"] or diffs:
            return False, "%s: mismatch=%d" % (s.name, summary["mismatch"])
        if sum(r[0] for r in rows.values()) != summary["table"]:
            return False, "%s: rows do not sum to the summary" % s.name
        if sum(r[1] for r in rows.values()) != summary["switch"]:
            return False, "%s: switch rows do not sum to the summary" % s.name
        for op, (t, sw, flag) in rows.items():
            if (t == sw) != (flag == "none"):
                return False, "%s: op=%s row disagrees with its own first-diff" % (s.name, op)
    return True, "rows agree across %d scripts" % len(scripts)


def cell_silent_unless_asked():
    out, _err, _rc = run(ADD1, ask=False)
    return ("IR-TABLE" not in out), "no IR-TABLE lines without %s" % ENV


def cell_zero_means_off():
    out, _err, _rc = run(ADD1, value="0")
    return ("IR-TABLE" not in out), "%s=0 reads as off" % ENV


def cell_rows_sum_to_the_summary():
    out, _err, _rc = run(ADD1)
    rows, summary, _d = readout(out)
    if summary is None or not rows:
        return False, "no readout"
    ok = (sum(r[0] for r in rows.values()) == summary["table"]
          and sum(r[1] for r in rows.values()) == summary["switch"]
          and summary["compared"] == len(rows))
    return ok, "compared=%d rows=%d table=%d switch=%d" % (
        summary["compared"], len(rows), summary["table"], summary["switch"])


def cell_counts_follow_the_work():
    out1, _e1, _r1 = run(ADD1)
    out3, _e3, _r3 = run(ADD3)
    r1, _s1, _d1 = readout(out1)
    r3, _s3, _d3 = readout(out3)
    one = r1.get("OP_ADD", (None,))[1]
    three = r3.get("OP_ADD", (None,))[1]
    return (one == 1 and three == 3), "OP_ADD switch=%s then %s" % (one, three)


def cell_diff_names_the_opcode():
    out, _err, _rc = run(ADD1, mutate="OP_ADD=OP_SUB")
    _rows, summary, diffs = readout(out)
    if summary is None or not diffs:
        return False, "the mutation printed no disagreement"
    d = diffs[0]
    ok = (d["op"] == "OP_ADD" and d["switch"] == "OP_ADD" and d["table"] == "OP_SUB"
          and summary["mismatch"] >= 1)
    return ok, "op=%s table=%s switch=%s mismatch=%d" % (
        d["op"], d["table"], d["switch"], summary["mismatch"])


def cell_diff_carries_the_program_and_ip():
    bc = TMP / "add1.inim"
    compile_to(ADD1, bc)
    if not bc.is_file():
        return False, "could not compile a .inim to name"
    out, _err, _rc = run(bc, mutate="OP_ADD=OP_SUB")
    _rows, _summary, diffs = readout(out)
    if not diffs:
        return False, "no disagreement printed for a .inim"
    d = diffs[0]
    ok = d["prog"].endswith("add1.inim") and d["ip"] >= 0
    return ok, "prog=%s ip=%d" % (d["prog"], d["ip"])


def cell_no_diff_when_they_agree():
    bc = TMP / "agree.inim"
    compile_to(ADD1, bc)
    out, _err, _rc = run(bc)
    _rows, summary, diffs = readout(out)
    ok = summary is not None and summary["mismatch"] == 0 and not diffs
    return ok, "unmutated .inim: mismatch=%s diffs=%d" % (
        summary["mismatch"] if summary else "na", len(diffs))


def cell_only_what_ran_is_counted():
    out, _err, rc = run(BAD)
    return (rc != 0 and "IR-TABLE-SAME" not in out), "rc=%d no rows" % rc


CASES = [
    ("every-op-agrees", cell_every_op_agrees),
    ("silent-unless-asked", cell_silent_unless_asked),
    ("zero-means-off", cell_zero_means_off),
    ("rows-sum-to-the-summary", cell_rows_sum_to_the_summary),
    ("counts-follow-the-work", cell_counts_follow_the_work),
    ("diff-names-the-opcode", cell_diff_names_the_opcode),
    ("diff-carries-the-program-and-ip", cell_diff_carries_the_program_and_ip),
    ("no-diff-when-they-agree", cell_no_diff_when_they_agree),
    ("only-what-ran-is-counted", cell_only_what_ran_is_counted),
]


def main():
    red = []
    for name, fn in CASES:
        try:
            ok, detail = fn()
        except Exception as exc:  # a cell that raises is red, not skipped
            ok, detail = False, "%s: %s" % (type(exc).__name__, exc)
        print("IR-TABLE-CELL|case=%s|%s|%s" % (name, "green" if ok else "RED", detail))
        if not ok:
            red.append(name)
    print("ir_table_same: cases=%d red=%d green=%d" % (len(CASES), len(red), len(CASES) - len(red)))
    if red:
        print("ir_table_same: red cells: " + ", ".join(red))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
