#!/usr/bin/env python3
"""CTest entry for tools/ir_spec.py -- P0 of docs/IR_DESIGN.md.

Two things are asserted, and nothing else:

1. the table is a table of *this* tree: a member of `OpCode` with no operand
   shape, or a shape for an opcode the enum does not have, is a failure;
2. no consumer lost an opcode it used to cover.  All 67 are dispatched and all
   67 are named by the disassembler; there is no gap budget, so a missing one
   fails here rather than being absorbed by a number in the table;
3. every declared shape is confirmed by the handler body that implements it:
   a shape may neither forget a register the handler reads nor claim one the
   handler never touches, and a case whose printed name cannot be read is not
   evidence of a name;
4. the handler column is a column of *this* tree: every opcode's case must jump
   to a label a body defines and to its own label, never to another opcode's.
   IR-TABLE-SHADOW says in the same run that no reader consumes the table yet,
   so this is a measurement of the dispatch, not of a table in use.
5. the AOT fraction is the *pinned* one: the number on IR-COVER, the gap named
   beside it, and what IR-AOT-COVER/IR-AOT-PIN say about where the number came
   from must all agree with tools/aot_cover.pin.json, and a pin that contradicts
   its own lists must not reach the reader as a fraction.  (The recomputation
   itself is not run here: it takes about 100 seconds, more than this test's own
   timeout, and it belongs to tools/aot_cover.test.py --check-pin.  What is
   asserted is that this reading does not print a number it did not verify,
   which is what `numbers-match=na` plus a named recomputation says.)

The checker's own checks are asserted first (`--self-check`, thirteen mutations), because a table
that cannot go red has not been shown to be watching anything.

    python3 tools/ir_spec.test.py
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "ir_spec.py"


def run(*args):
    return subprocess.run([sys.executable, str(TOOL), *args],
                          cwd=ROOT, capture_output=True, text=True)


def main():
    selfcheck = run("--self-check")
    cases = [ln for ln in selfcheck.stdout.splitlines() if ln.startswith("IRCHECK|")]
    red = [ln for ln in cases if "|red=yes" in ln]
    if selfcheck.returncode != 0 or len(cases) != 13 or len(red) != 13:
        print(f"ir spec: FAIL (self-check: {len(red)}/{len(cases)} mutations caught)")
        for ln in cases:
            print(ln)
        return 1

    got = run()
    lines = got.stdout.splitlines()
    spec = next((ln for ln in lines if ln.startswith("IR-SPEC|")), "")
    cover = next((ln for ln in lines if ln.startswith("IR-COVER|")), "")
    if got.returncode != 0 or not spec or not cover:
        print("ir spec: FAIL (table does not match the enum or a consumer regressed)")
        for ln in lines:
            if ln.startswith(("IR-SPEC|", "IR-STALE|", "IR-UNSHAPED|", "IR-COVER|", "IR-STALE|")):
                print(ln)
        print(got.stderr.strip())
        return 1

    table = next((ln for ln in lines if ln.startswith("IR-TABLE|")), "")
    if not table or "|mismatch=0" not in table:
        print("ir spec: FAIL (the handler column does not describe this dispatcher)")
        for ln in lines:
            if ln.startswith(("IR-TABLE", "IR-COVER|")):
                print(ln)
        return 1

    fields = dict(part.split("=", 1) for part in cover.split("|")[1:] if "=" in part)
    interp = fields.get("interp", "?")
    disasm = fields.get("disasm", "?")
    shape = next((ln for ln in lines if ln.startswith("IR-SHAPE|")), "")
    ops = dict(part.split("=", 1) for part in spec.split("|")[1:] if "=" in part).get("ops", "?")
    checked = dict(part.split("=", 1) for part in shape.split("|")[1:] if "=" in part).get("checked", "?")
    tfields = dict(part.split("=", 1) for part in table.split("|")[1:] if "=" in part)

    # The AOT fraction is quoted from tools/aot_cover.pin.json.  What is asserted
    # here is that the number a reader sees comes from that file, that the gap is
    # named on the same line, and that the file itself is checked -- the
    # recomputation belongs to the tool that owns it, and is asked for by name.
    pin = json.loads((ROOT / "tools" / "aot_cover.pin.json").read_text())
    aotcover = next((ln for ln in lines if ln.startswith("IR-AOT-COVER|")), "")
    aotpin = next((ln for ln in lines if ln.startswith("IR-AOT-PIN|")), "")
    if fields.get("aot") != "%s/%s" % (pin["covered"], pin["of"]):
        print(f"ir spec: FAIL (the aot fraction {fields.get('aot')} is not the pinned one)")
        return 1
    if "aot:" + ",".join(pin["missing_ops"]) not in fields.get("missing", ""):
        print("ir spec: FAIL (the aot gap is not named beside the fraction)")
        return 1
    if (not aotcover or not aotpin or "|numbers-match=na|" not in aotpin
            or "|pin-lists-agree=yes|" not in aotpin):
        print("ir spec: FAIL (the fraction is quoted without saying where it comes from)")
        for ln in lines:
            if ln.startswith(("IR-COVER|", "IR-AOT")):
                print(ln)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        # A pin that contradicts its own lists must not reach the reader as a
        # fraction: the reading has to refuse to quote it and name the field.
        bent = Path(tmp) / "bent.pin.json"
        bent.write_text(json.dumps(dict(pin, covered=pin["covered"] + 1)))
        r = subprocess.run([sys.executable, str(TOOL)], cwd=ROOT, capture_output=True,
                           text=True, env=dict(os.environ, INIMERSE_AOT_PIN=str(bent)))
        if (r.returncode == 0 or "IR-AOT-PIN-BROKEN|field=covered|" not in r.stdout
                or "|aot=na|" not in r.stdout):
            print("ir spec: FAIL (a pin that contradicts itself still produced a fraction)")
            return 1
        # ... and a recomputation's verdict is read: a stub stands in for the
        # 100-second measurement so the wiring is exercised both ways.
        stub = Path(tmp) / "stub.py"
        for verdict in ("no", "yes"):
            stub.write_text("print('IR-AOT-PIN|tree=0000000|head=0000000|covered=30"
                            "|recomputed=%s|of=67|numbers-match=%s|tree-moved=no"
                            "|basis=stub')\n" % (verdict, verdict))
            r = subprocess.run([sys.executable, str(TOOL)], cwd=ROOT, capture_output=True,
                               text=True,
                               env=dict(os.environ,
                                        INIMERSE_AOT_RECOMPUTE="%s %s" % (sys.executable, stub)))
            if (r.returncode == 0) != (verdict == "yes") or "|numbers-match=" + verdict + "|" not in r.stdout:
                print(f"ir spec: FAIL (a recomputation reporting numbers-match={verdict} was not read)")
                return 1

    print(f"ir spec: ok ({ops} opcodes, interp {interp}, disasm {disasm}, "
          f"shapes {checked}, handlers {tfields.get('handlers-named', '?')}, "
          f"aot {fields.get('aot')})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
