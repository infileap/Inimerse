#!/usr/bin/env python3
"""Control cells for IR-TABLE after the table became the dispatcher.

What is pinned now: the dispatch is a table of label addresses
('static void *g_ir_body[68] = { [OP_MOV] = &&L_MOV, ... }' followed by
'goto *ir_body_next'), and there is no switch left to compare it against.  So
the same-run summary says compared=na and names the tree where the question
could still be asked (since=...).  A reading that stops answering has to say so
in its own line: "no longer measured" and "broken" must not look alike.

The switch did not take its negative control with it.  The control moved to the
gauge, which reads the table's own initializer out of the source text, so the
last group bends one row of a copy of vm.c and requires the gauge to name that
opcode -- a check the deleted comparison can no longer perform.

The cells are three groups:
  * the dispatcher is the table (source=table, switched=0, readers=table);
  * what ran is still named (SPLIT carries both bodies and both legs);
  * the counts are sampled, the names are not -- a program that spawns threads
    prints threads=yes|counts=sampled, and every non-numeric field is identical
    to the same program without threads, so sampling covers the counters only.
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
GAUGE = ROOT / "tools" / "ir_spec.py"

SUM = re.compile(
    r"^IR-TABLE-SAME\|compared=([a-z0-9]+)\|why=([a-z-]+)\|since=([0-9a-f]+)"
    r"\|prog=(.+?)\|opcodes=(\d+)"
    r"\|split-ops-seen=(\d+)\|split-ops-known=(na|\d+)\|known-at=([^|]+)"
    r"\|split-basis=([a-z-]+)\|split-why=([a-z-]+)"
    r"\|goto-entries=(\d+)\|threads=(yes|no)\|counts=(sampled|exact)"
    r"\|counts-why=([a-z-]+)$")
SPLIT = re.compile(
    r"^IR-TABLE-SPLIT\|op=([A-Z0-9_]+)\|bodies=([A-Z0-9_,]+)"
    r"\|dispatched=([A-Za-z0-9_]+)\|goto=([A-Za-z0-9_]+)"
    r"\|dispatches=(\d+)\|goto-entries=(\d+)\|effect=(.+?)(?:\|why=([a-z-]+))?$")

FIELDS = ("compared", "why", "since", "prog", "opcodes", "split_ops_seen",
          "split_ops_known", "known_at", "split_basis", "split_why",
          "goto_entries", "threads", "counts", "counts_why")


def refuse(why):
    print("ir_table_same: REFUSED|" + why, file=sys.stderr)
    print("ir_table_same: not registered: nothing was checked", file=sys.stderr)
    sys.exit(2)


if not BIN.is_file():
    refuse("no interpreter at %s (build it, or set INIMERSE_BIN)" % BIN)
if not GAUGE.is_file():
    refuse("no gauge at %s" % GAUGE)


def run(script, ask=True, value="1"):
    env = dict(os.environ)
    env.pop(ENV, None)
    if ask:
        env[ENV] = value
    done = subprocess.run([str(BIN), "--no-mods", "run", str(script)],
                          capture_output=True, text=True, env=env, cwd=str(ROOT))
    return done.stdout


def gauge():
    done = subprocess.run([sys.executable, str(GAUGE)], capture_output=True,
                          text=True, cwd=str(ROOT))
    return done.stdout + done.stderr, done.returncode


def readout(text):
    """(summary dict or None, SPLIT rows) from one run's stdout."""
    summary, splits = None, []
    for line in text.splitlines():
        m = SUM.match(line)
        if m:
            summary = {k: m.group(i + 1) for i, k in enumerate(FIELDS)}
            summary["goto_entries"] = int(summary["goto_entries"])
            summary["split_ops_seen"] = int(summary["split_ops_seen"])
            continue
        m = SPLIT.match(line)
        if m:
            splits.append(dict(op=m.group(1), bodies=m.group(2).split(","),
                               dispatched=m.group(3), goto=m.group(4),
                               dispatches=int(m.group(5)), by_goto=int(m.group(6)),
                               effect=m.group(7), why=m.group(8)))
    return summary, splits


TMP = Path(tempfile.mkdtemp(prefix="ir_table_same."))


def program(name, body):
    p = TMP / name
    p.write_text(body)
    return p


ADD1 = program("add1.im", "a = 1\nb = a + 2\nsay b\n")
BAD = program("bad.im", "a = (\nsay\n")


def cell_dispatcher_is_the_table():
    """The source has no switch (ins.op) left and dispatches through the table,
    and the gauge describes that same tree: source=table, switched=0."""
    vm = (ROOT / "src" / "vm" / "vm.c").read_text()
    if "switch (ins.op)" in vm:
        return False, "vm.c still switches on ins.op"
    if "goto *ir_body_next;" not in vm:
        return False, "no computed goto into the table"
    out, _rc = gauge()
    tab = [l for l in out.splitlines() if l.startswith("IR-TABLE|")]
    shadow = [l for l in out.splitlines() if l.startswith("IR-TABLE-SHADOW|")]
    ok = (len(tab) == 1 and "|source=table" in tab[0] and "|mismatch=0" in tab[0]
          and len(shadow) == 1
          and shadow[0].startswith("IR-TABLE-SHADOW|switched=0|readers=table"))
    return ok, "table rows=%d shadow=%s" % (
        len(tab), shadow[0].split("|")[1] if shadow else "none")


def cell_silent_unless_asked():
    out = run(ADD1, ask=False)
    return ("IR-TABLE" not in out), "no IR-TABLE lines without %s" % ENV


def cell_zero_means_off():
    out = run(ADD1, value="0")
    return ("IR-TABLE" not in out), "%s=0 reads as off" % ENV


def cell_same_line_says_na():
    """compared=na is only honest if it names the tree where a comparison was
    still possible -- otherwise it reads like a comparison that found nothing."""
    summary, _s = readout(run(ADD1))
    if summary is None:
        return False, "no summary line"
    ok = (summary["compared"] == "na"
          and summary["why"] == "no-switch-left-to-compare-with"
          and re.fullmatch(r"[0-9a-f]{7,}", summary["since"]) is not None
          and summary["prog"].endswith("add1.im")
          and summary["opcodes"] == "67"
          and summary["split_ops_known"] == "na"
          and "IR-TABLE-SPLIT-KNOWN" in summary["known_at"])
    return ok, "compared=%s since=%s known-at=%s" % (
        summary["compared"], summary["since"], summary["known_at"])


def cell_split_is_named():
    """An opcode served by two bodies reports both bodies and both legs, and a
    plain program reports no such row at all."""
    comp = ROOT / "vtest" / "composition_v04.im"
    if not comp.is_file():
        return False, "no vtest/composition_v04.im to exercise the split"
    summary, splits = readout(run(comp))
    if summary is None:
        return False, "no summary"
    hit = [s for s in splits if s["op"] == "OP_CALL_VALUE"]
    if not hit:
        return False, "no SPLIT row for OP_CALL_VALUE (%d rows)" % len(splits)
    s = hit[0]
    ok = ("L_CALL_VALUE" in s["bodies"] and "L_CALL_FUNC" in s["bodies"]
          and s["dispatched"] == "L_CALL_VALUE" and s["goto"] == "L_CALL_FUNC"
          and s["by_goto"] >= 1 and s["dispatches"] >= 1
          and summary["split_ops_seen"] >= 1 and summary["goto_entries"] >= 1)
    if not ok:
        return False, "row=%s" % s
    _plain, plain_splits = readout(run(ADD1))
    return (not plain_splits), "OP_CALL_VALUE in %d bodies" % len(s["bodies"])


def cell_split_known_is_pointed_at():
    """The runtime cannot hold the static set of split opcodes (that would be a
    second table), so it points at the reading that does: known-at=."""
    summary, _s = readout(run(ADD1))
    if summary is None:
        return False, "no summary line"
    out, rc = gauge()
    known = [l for l in out.splitlines() if l.startswith("IR-TABLE-SPLIT-KNOWN|")]
    if rc != 0 or len(known) != 1:
        return False, "gauge printed %d SPLIT-KNOWN rows (rc=%d)" % (len(known), rc)
    ok = ("IR-TABLE-SPLIT-KNOWN" in summary["known_at"]
          and "tools/ir_spec.py" in summary["known_at"])
    return ok, "known-at=%s / %s" % (summary["known_at"], known[0].split("|")[1])


def cell_threads_are_sampled():
    """A program that spawns threads says counts=sampled and why; the same
    program without threads says exact.  Sampling must cover the counters and
    nothing else, so every non-numeric field has to match between the two."""
    cpu8 = ROOT / "examples" / "bench" / "cpu8.im"
    if not cpu8.is_file():
        return False, "no examples/bench/cpu8.im"
    threaded, _ts = readout(run(cpu8))
    plain, _ps = readout(run(ADD1))
    if threaded is None or plain is None:
        return False, "a run printed no summary"
    if not (threaded["threads"] == "yes" and threaded["counts"] == "sampled"
            and "without-atomicity" in threaded["counts_why"]):
        return False, "threaded run does not say sampled: %s|%s" % (
            threaded["threads"], threaded["counts"])
    if not (plain["threads"] == "no" and plain["counts"] == "exact"):
        return False, "plain run does not say exact: %s|%s" % (
            plain["threads"], plain["counts"])
    numeric = {"goto_entries", "threads", "counts", "counts_why", "prog"}
    left = {k: v for k, v in threaded.items() if k not in numeric}
    right = {k: v for k, v in plain.items() if k not in numeric}
    ok = left == right
    return ok, "threads=%s counts=%s, %d non-numeric fields match" % (
        threaded["threads"], threaded["counts"], len(left))


def cell_bent_table_is_named():
    """The negative control the deleted comparison used to provide.  One row of
    a copy of vm.c is pointed at a neighbouring body; the gauge reads the source,
    so it must name OP_ADD and fail."""
    src = (ROOT / "src" / "vm" / "vm.c").read_text()
    needle = "        [OP_ADD] = &&L_ADD,"
    if needle not in src:
        return False, "no OP_ADD row in the table to bend"
    bent = (TMP / "vm_bent.c")
    bent.write_text(src.replace(needle, "        [OP_ADD] = &&L_SUB,", 1))
    code = ("import sys; sys.path.insert(0, %r); import ir_spec; from pathlib import Path; "
            "ir_spec.VM_C = Path(%r); sys.exit(ir_spec.main([]))"
            % (str(ROOT / "tools"), str(bent)))
    done = subprocess.run([sys.executable, "-c", code], capture_output=True,
                          text=True, cwd=str(ROOT))
    out = done.stdout + done.stderr
    named = [l for l in out.splitlines()
             if l.startswith("IR-TABLE-MISMATCH|op=OP_ADD|") and "L_SUB" in l]
    ok = done.returncode != 0 and bool(named)
    return ok, "rc=%d names OP_ADD=%s" % (done.returncode, bool(named))


def cell_only_what_ran_is_counted():
    """A script that does not run does not print a census: the rows are taken
    where the instructions ran, so a failed parse cannot produce a zero row."""
    out = run(BAD)
    env = dict(os.environ)
    env[ENV] = "1"
    done = subprocess.run([str(BIN), "--no-mods", "run", str(BAD)],
                          capture_output=True, text=True, env=env, cwd=str(ROOT))
    return (done.returncode != 0 and "IR-TABLE-SAME" not in out,
            "rc=%d no rows" % done.returncode)


def cell_bodies_are_names():
    """SPLIT names bodies, it does not index them: every body it prints is a
    label name, and both legs name one of those bodies."""
    comp = ROOT / "vtest" / "composition_v04.im"
    if not comp.is_file():
        return False, "no vtest/composition_v04.im"
    _summary, splits = readout(run(comp))
    if not splits:
        return False, "no SPLIT rows"
    for s in splits:
        if len(s["bodies"]) < 2:
            return False, "%s: %d bodies" % (s["op"], len(s["bodies"]))
        for b in s["bodies"]:
            if not re.fullmatch(r"L_[A-Z0-9_]+", b):
                return False, "%s: body %r is not a label name" % (s["op"], b)
        for leg in ("dispatched", "goto"):
            if s[leg] != "na" and s[leg] not in s["bodies"]:
                return False, "%s: %s=%s not among its bodies" % (
                    s["op"], leg, s[leg])
    return True, "%d SPLIT row(s), all names" % len(splits)


CASES = [
    ("dispatcher-is-the-table", cell_dispatcher_is_the_table),
    ("silent-unless-asked", cell_silent_unless_asked),
    ("zero-means-off", cell_zero_means_off),
    ("same-line-says-na", cell_same_line_says_na),
    ("split-is-named", cell_split_is_named),
    ("split-known-is-pointed-at", cell_split_known_is_pointed_at),
    ("threads-are-sampled", cell_threads_are_sampled),
    ("bent-table-is-named", cell_bent_table_is_named),
    ("only-what-ran-is-counted", cell_only_what_ran_is_counted),
    ("bodies-are-names", cell_bodies_are_names),
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
