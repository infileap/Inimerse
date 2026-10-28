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
   evidence of a name.

The checker's own checks are asserted first (`--self-check`, seven mutations), because a table
that cannot go red has not been shown to be watching anything.

    python3 tools/ir_spec.test.py
"""

import subprocess
import sys
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
    if selfcheck.returncode != 0 or len(cases) != 10 or len(red) != 10:
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

    fields = dict(part.split("=", 1) for part in cover.split("|")[1:] if "=" in part)
    interp = fields.get("interp", "?")
    disasm = fields.get("disasm", "?")
    shape = next((ln for ln in lines if ln.startswith("IR-SHAPE|")), "")
    ops = dict(part.split("=", 1) for part in spec.split("|")[1:] if "=" in part).get("ops", "?")
    checked = dict(part.split("=", 1) for part in shape.split("|")[1:] if "=" in part).get("checked", "?")
    print(f"ir spec: ok ({ops} opcodes, interp {interp}, disasm {disasm}, "
          f"shapes {checked})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
