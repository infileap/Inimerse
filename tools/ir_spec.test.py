#!/usr/bin/env python3
"""CTest entry for tools/ir_spec.py -- P0 of docs/IR_DESIGN.md.

Two things are asserted, and nothing else:

1. the table is a table of *this* tree: a member of `OpCode` with no operand
   shape, or a shape for an opcode the enum does not have, is a failure;
2. no consumer lost an opcode it used to cover.  The disassembler names 48 of
   the 68 opcodes, and each names itself; a gap may shrink (lower the baseline
   in the same commit) but may not grow.

The checker's own checks are asserted first (`--self-check`), because a table
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
    if selfcheck.returncode != 0 or len(cases) != 5 or len(red) != 5:
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
    ops = dict(part.split("=", 1) for part in spec.split("|")[1:] if "=" in part).get("ops", "?")
    named = fields.get("disasm", "?")
    print(f"ir spec: ok ({ops} opcodes, interp {interp}, disasm {named}, "
          f"names agree)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
