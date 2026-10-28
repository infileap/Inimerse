#!/usr/bin/env python3
"""The single table of register opcodes, and the coverage it can prove today.

WHY THIS FILE EXISTS
--------------------
The same opcode set is written down in five places: the interpreter's dispatch
(src/vm/vm.c), the AOT selector (src/compilation/aot_native.c), the wasm
selector (src/compilation/wasm_backend.c), the disassembler (src/vm/vm.c), and
the bootstrap compiler's writer (selfhost/compiler.im).  A rule changed in one
of them does not announce itself in the others; `%` drifted exactly that way
(see tools/mod_spec.py for how the arithmetic side was closed).

This is P0 of docs/IR_DESIGN.md: the table is written, and everything it claims
is *measured* against the sources, so a claim cannot rot silently.  It changes
no behaviour and no bytecode.

WHAT IS DECLARED AND WHAT IS MEASURED
-------------------------------------
declared  `SHAPE`: one operand shape per opcode, e.g. "r1 = r2 + r3" -- this is
          the authority for what an opcode *means* structurally (destination,
          sources, whether it moves control).  A member of `OpCode` with no
          shape here is a hole in the table, reported as IR-SPEC|unshaped=...
measured  `interp`  -- the opcode appears as a `case` in the interpreter's
                       dispatch switch (`switch (ins.op)`).
          `disasm`  -- the opcode appears as a `case` in vm_disasm_ins and the
                       format string it prints begins with the opcode's own
                       name (a case that prints "ADD" for OP_SUB is worse than
                       a case that prints a number: it is a wrong answer with
                       the shape of a right one).  Coverage and name
                       agreement are two separate readings.
not now   `aot` / `wasm` -- both selectors are written against the AST, not
                       against this bytecode (docs/IR_DESIGN.md sections 3 and
                       7), so there is nothing to measure yet.  They become
                       measurable in P3/P4, when those selectors take MIR.
          `selfhost`-- the bootstrap writer's number table is P6.

READINGS
--------
  IR-SPEC|ops=N|codes=LO..HI|hole=29|shaped=N|unshaped=<names>
  IR-ABI|version=3|abi=2|ops=N
  IR-COVER|interp=N/N|disasm=N/N|missing=disasm:OP_A,OP_B|aot=na|wasm=na|selfhost=na
  IR-DISASM|named=68/68|wrong-name=none
  IR-UNIMPLEMENTED|consumer=disasm|op=OP_X|effect=prints-a-number

Exit 0 when every declared shape matches a member of the enum and every
measured consumer covers every opcode it is supposed to.  Exit 1 otherwise, and
the failing lines name the consumer and the opcode.  `--self-check` mutates the
table and requires the mutation to be caught (a checker that cannot go red has
not been shown to work).
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BYTECODE_H = ROOT / "src" / "compiler" / "bytecode.h"
VM_C = ROOT / "src" / "vm" / "vm.c"

# Codes the enum no longer defines but the numbering remembers.  The gap is not
# free space: OP_SAY is pinned to 30 because committed .inim files carry raw
# opcode integers, so nothing may slide down into it.
RETIRED = {29: "OP_POP_REG"}

# The gaps that exist today, per consumer, measured when the table was written.
# A gap may shrink (record it here in the same commit, so the improvement is
# visible as a number instead of a claim) but it may not grow: a consumer that
# used to name an opcode and stopped is a regression, not a new gap.
#
# disasm went 48 -> 68 (0 left) in the commit that gave the twenty unnamed
# opcodes a name instead of "OP_%d": before that, a debugger looking at a
# thread/set/closure instruction saw a number.
BASELINE_MISSING = {"disasm": 0}

# declared: opcode -> operand shape.  "r1 = r2 + r3" reads as: writes r1, reads
# r2 and r3.  "ip = r2" is control flow.  "-" means no register operands at all.
SHAPE = {
    "OP_MOV": "r1 = r2",
    "OP_LOADK_INT": "r1 = int32(r2)",
    "OP_LOADK_FLOAT": "r1 = float_pool[r2]",
    "OP_LOADK_STRING": "r1 = string_pool[r2]",
    "OP_LOADK_BOOL": "r1 = bool(r2)",
    "OP_LOADK_I64": "r1 = r2 | (r3 << 32) as UNSIGNED halves",
    "OP_ADD": "r1 = r2 + r3",
    "OP_SUB": "r1 = r2 - r3",
    "OP_MUL": "r1 = r2 * r3",
    "OP_DIV": "r1 = r2 / r3",
    "OP_MOD": "r1 = r2 % r3  [authority: tools/mod_spec.py mod-rule-v1]",
    "OP_NEG": "r1 = -r2",
    "OP_EQ": "r1 = (r2 == r3)",
    "OP_NEQ": "r1 = (r2 != r3)",
    "OP_LT": "r1 = (r2 < r3)",
    "OP_GT": "r1 = (r2 > r3)",
    "OP_LE": "r1 = (r2 <= r3)",
    "OP_GE": "r1 = (r2 >= r3)",
    "OP_AND": "r1 = truth(r2) and truth(r3)  [boolean, short-circuit at the front end]",
    "OP_OR": "r1 = truth(r2) or truth(r3)",
    "OP_NOT": "r1 = !truth(r2)",
    "OP_EQK": "r1 = (r2 == string_pool[r3])",
    "OP_NEQK": "r1 = (r2 != string_pool[r3])",
    "OP_NEW_ARRAY": "r1 = [R[r2 .. r2+r3-1]]",
    "OP_NEW_DICT": "r1 = {r3 pairs from the stack}",
    "OP_NEW_SET": "r1 = merge(stack[sp-1 .. sp-r3])",
    "OP_SET_INTERVAL": "r1 = interval(string_pool[r2 & 0xffffff], lo/hi from the stack)",
    "OP_SET_ADD": "- (set insert)",
    "OP_IN": "r1 = (r2 in r3)",
    "OP_MIN": "r1 = min(r2)",
    "OP_MAX": "r1 = max(r2)",
    "OP_INDEX_GET": "r1 = r2[r3]",
    "OP_INDEX_SET": "r1[r2] = r3",
    "OP_CONCAT": "r1 = left-assoc fold(+) of R[r2 .. r2+r3-1]",
    "OP_LOAD_GLOBAL": "r1 = G[r2]",
    "OP_STORE_GLOBAL": "G[r1] = r2",
    "OP_BIND": "G[r1] = bind(set r2, init r3)",
    "OP_DECLARE": "- (declare)",
    "OP_RECORD": "record(kind=r1, float_pool[r2])",
    "OP_JUMP": "ip = r2",
    "OP_JUMP_IF_FALSE": "if !truth(r1) ip = r2",
    "OP_JUMP_IF_TRUE": "if truth(r1) ip = r2",
    "OP_CALL_BUILTIN": "r1 = builtin(string_pool[r2])(argc=r3)",
    "OP_CALL_FUNC": "r2 = func[r1](argc=r3)",
    "OP_CALL_VALUE": "r2 = dotted-call of r1 (argc=r3)",
    "OP_MAKE_FUNC": "r1 = closure(fn idx r2, captures from the stack)",
    "OP_LOAD_CAPTURE": "r1 = captured[r2]",
    "OP_RETURN": "return r1",
    "OP_IS_NIL": "r1 = (r2 == nil)",
    "OP_PUSH_REG": "push R[r1]",
    "OP_SAY": "say r1",
    "OP_WAIT": "wait r1",
    "OP_STOP": "- (stop the script)",
    "OP_HALT": "- (halt the VM)",
    "OP_YIELD": "- (cooperative yield)",
    "OP_THREAD_START": "thread[r1] = start(func r2, argc=r3)",
    "OP_THREAD_CTRL": "control thread[r1] with op r2",
    "OP_THREAD_JOIN": "join thread[r1] (timeout r2)",
    "OP_THREAD_WAIT": "wait thread[r1] cond r2 timeout r3",
    "OP_THREAD_STATE": "r1 = state(thread r2, attr r3)",
    "OP_THREAD_GOTO": "goto label r2 inside thread[r1]",
    "OP_LOCK": "lock[r1] = r2",
    "OP_SEND": "send thread[r1] msg r2",
    "OP_RECV": "r1 = recv(timeout r2)",
    "OP_TRY_START": "- (open try r1)",
    "OP_TRY_END": "- (close try)",
    "OP_THROW": "throw r1",
}


def declared_ops():
    """Parse `typedef enum { ... } OpCode;` -- names, codes, and the hole.

    Block comments are removed across line boundaries before anything is
    counted: one of them names an opcode in prose ("A separate opcode rather
    than a reuse of OP_LOADK_INT"), and a parser that strips comments line by
    line reads that prose as a member -- 68 members instead of 67, with the
    phantom carrying the code of the line it sat on.
    """
    text = BYTECODE_H.read_text()
    body = text[text.index("typedef enum {") : text.index("} OpCode;")]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
    body = re.sub(r"//[^\n]*", " ", body)
    code = -1
    out = []
    for line in body.splitlines()[1:]:
        for m in re.finditer(r"\b(OP_[A-Z0-9_]+)\b\s*(?:=\s*(\d+))?", line):
            code = int(m.group(2)) if m.group(2) else code + 1
            out.append((m.group(1), code))
    return out


def _cases(fn_text):
    return {m.group(1) for m in re.finditer(r"case (OP_[A-Z0-9_]+):", fn_text)}


def _disasm_names(fn_text):
    """opcode -> the name its format string prints first, e.g. OP_MOV -> MOV.

    A case may be followed by several lines before its snprintf (a multi-line
    argument list), so the search runs from the case to the next case.
    """
    out = {}
    hits = [(m.start(), m.group(1)) for m in re.finditer(r"case (OP_[A-Z0-9_]+):", fn_text)]
    for i, (start, name) in enumerate(hits):
        end = hits[i + 1][0] if i + 1 < len(hits) else len(fn_text)
        m = re.search(r'snprintf\(out, outsz, "([A-Za-z0-9_]+)', fn_text[start:end])
        if m:
            out[name] = m.group(1)
    return out


def measured():
    """Coverage measured from the sources: interpreter dispatch and disassembler."""
    vm = VM_C.read_text()
    start = vm.index("switch (ins.op)")
    dispatch = _cases(vm[start : vm.index("default:", start)])

    d = vm.index("static void vm_disasm_ins(")
    disasm_fn = vm[d : vm.index("default:", d)]
    return {"interp": dispatch, "disasm": _cases(disasm_fn),
            "disasm_names": _disasm_names(disasm_fn)}


def report(ops, shape, meas, out=sys.stdout, baseline=None):
    names = [n for n, _ in ops]
    codes = [c for _, c in ops]
    holes = sorted(set(range(min(codes), max(codes) + 1)) - set(codes))
    unshaped = [n for n in names if n not in shape]
    stale = sorted(set(shape) - set(names))
    bad = []

    version = re.search(r"INIM_BYTECODE_VERSION\s+(\d+)", BYTECODE_H.read_text()).group(1)
    abi = re.search(r"INIM_ABI_VERSION\s+(\d+)", BYTECODE_H.read_text()).group(1)

    print(f"IR-SPEC|ops={len(names)}|codes={min(codes)}..{max(codes)}"
          f"|hole={','.join(map(str, holes)) or 'none'}"
          f"|shaped={len(names) - len(unshaped)}|unshaped={','.join(unshaped) or 'none'}",
          file=out)
    print(f"IR-ABI|version={version}|abi={abi}|ops={len(names)}", file=out)
    if unshaped:
        for n in unshaped:
            print(f"IR-UNSHAPED|op={n}|effect=no operand shape declared", file=out)
        bad.append("unshaped")
    if stale:
        for n in stale:
            print(f"IR-STALE|op={n}|effect=declared shape for an opcode the enum does not have",
                  file=out)
        bad.append("stale")

    for code in holes:
        print(f"IR-RETIRED|code={code}|name={RETIRED.get(code, 'unknown')}|effect=never-issued",
              file=out)

    for consumer in ("interp", "disasm"):
        missing = [n for n in names if n not in meas[consumer]]
        for n in missing:
            effect = ("prints-a-number-instead-of-a-name" if consumer == "disasm"
                      else "refuses-or-mis-executes")
            print(f"IR-UNIMPLEMENTED|consumer={consumer}|op={n}|effect={effect}", file=out)
        base = (baseline or BASELINE_MISSING).get(consumer, 0)
        if len(missing) > base:
            bad.append(f"{consumer}:grew:{len(missing)}>{base}")

    # The name each case prints must be the opcode's own name.  This is the
    # check that keeps a copy-pasted case from answering confidently wrong.
    wrong = []
    for n in names:
        printed = meas.get("disasm_names", {}).get(n)
        if printed is None:
            continue
        if printed != n[len("OP_"):]:
            wrong.append((n, printed))
    for n, printed in wrong:
        print(f"IR-WRONGNAME|op={n}|prints={printed}|effect=a-wrong-answer-in-the-shape-of-a-right-one",
              file=out)
    if wrong:
        bad.append(f"disasm-names:{len(wrong)}")
    print(f"IR-DISASM|named={len([n for n in names if n in meas['disasm']])}/{len(names)}"
          f"|wrong-name={','.join(n for n, _ in wrong) or 'none'}", file=out)

    missing_disasm = [n for n in names if n not in meas["disasm"]]
    base_disasm = (baseline or BASELINE_MISSING).get("disasm", 0)
    print(f"IR-COVER|interp={len(names) - len([n for n in names if n not in meas['interp']])}/{len(names)}"
          f"|disasm={len(names) - len(missing_disasm)}/{len(names)}"
          f"|missing=disasm:{','.join(missing_disasm) or 'none'}"
          f"|baseline=disasm:{base_disasm}"
          f"|aot=na|wasm=na|selfhost=na", file=out)
    return 0 if not bad else 1


def main(argv):
    ops = declared_ops()
    shape = dict(SHAPE)
    meas = measured()

    if "--self-check" in argv:
        # Three mutations, each of which must be caught: an unshaped opcode, a
        # stale shape, and a consumer that stopped naming one opcode.
        cases = []
        drop = [n for n, _ in ops if n in shape][0]
        cases.append(("unshaped", {k: v for k, v in shape.items() if k != drop}))
        cases.append(("stale", dict(shape, **{"OP_THIS_IS_NOT_REAL": "r1 = 0"})))
        blind = dict(meas)
        worst = [n for n, _ in ops if n in meas["disasm"]][0]
        blind["disasm"] = set(meas["disasm"]) - {worst}
        ok = True
        for name, mutated in cases:
            rc = report(ops, mutated, meas, out=open("/dev/null", "w"))
            print(f"IRCHECK|case={name}|red={'yes' if rc else 'no'}", file=sys.stdout)
            ok = ok and bool(rc)
        grown = dict(BASELINE_MISSING)
        grown["disasm"] = BASELINE_MISSING["disasm"] - 1  # a baseline tightened without the fix
        rc = report(ops, shape, meas, out=open("/dev/null", "w"), baseline=grown)
        print(f"IRCHECK|case=baseline|red={'yes' if rc else 'no'}|op=baseline-tightened-without-a-fix", file=sys.stdout)
        ok = ok and bool(rc)
        rc = report(ops, shape, blind, out=open("/dev/null", "w"))
        print(f"IRCHECK|case=coverage|red={'yes' if rc else 'no'}|op={worst}", file=sys.stdout)
        ok = ok and bool(rc)
        swapped = dict(meas)
        names_map = dict(meas.get("disasm_names", {}))
        first, second = [n for n, _ in ops if n in names_map][:2]
        names_map[first], names_map[second] = names_map[second], names_map[first]
        swapped["disasm_names"] = names_map
        rc = report(ops, shape, swapped, out=open("/dev/null", "w"))
        print(f"IRCHECK|case=wrongname|red={'yes' if rc else 'no'}|op={first}<-{second}", file=sys.stdout)
        ok = ok and bool(rc)
        return 0 if ok else 1

    return report(ops, shape, meas)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
