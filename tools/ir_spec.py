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
  IR-DISASM|cases=N/N|names-checked=N/N|names-unreadable=<names>|wrong-name=<names>
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

# There is no gap budget any more, and no number here to fill in.
#
# Until the commit that named the twenty "OP_%d" opcodes there was one: 20
# disassembler cases that printed a number.  A budget that the table itself
# declares is not evidence -- deleting a case and raising the budget passes.
# Now any missing consumer is a failure, so a regression must be fixed or
# reverted, never absorbed here.

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
    "OP_SET_ADD": "r1 = insert(r1, r2)  [r1 is the set and is mutated]",
    "OP_IN": "r1 = (r2 in r3)",
    "OP_MIN": "r1 = min(r2)",
    "OP_MAX": "r1 = max(r2)",
    "OP_INDEX_GET": "r1 = r2[r3]",
    "OP_INDEX_SET": "r1[r2] = r3",
    "OP_CONCAT": "r1 = left-assoc fold(+) of R[r2 .. r2+r3-1]",
    "OP_LOAD_GLOBAL": "r1 = G[r2]",
    "OP_STORE_GLOBAL": "G[r1] = r2",
    "OP_BIND": "G[r1] = bind(set r2, init r3)",
    "OP_DECLARE": "declare(kind=r1, float_pool[r2])  [no register written]",
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
    "OP_TRY_START": "open try, handler var = r2  [r1 is emitted as 0 and never read]",
    "OP_TRY_END": "- (close try)",
    "OP_THROW": "throw r1",
}


def declared_ops():
    """Parse `typedef enum { ... } OpCode;` -- names, codes, and the hole.

    Block comments are removed across line boundaries before anything is
    counted: one of them names an opcode in prose ("A separate opcode rather
    than a reuse of OP_LOADK_INT"), and a parser that strips comments line by
    line reads that prose as a second entry for a name the enum already has --
    68 entries, 67 distinct names, and the duplicate carries the code of the
    line the prose sat on.
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


def _handlers(vm):
    """opcode -> the text of its `L_X:` block.

    Two shapes occur in the dispatcher: most bodies are braced (`L_MOV: { ...
    }`), and a few are flat statements ending in `continue;` (`L_STOP:` /
    `L_HALT:`).  One body even begins after the previous body's closing brace
    on the same line (`} L_CALL_FUNC: {`).  Slicing from each label to the next
    covers all three, where brace matching covers only the first.
    """
    marks = [(m.start(), "OP_" + m.group(1)) for m in re.finditer(r"L_([A-Z0-9_]+):", vm)]
    # A label is sometimes named in a comment first ("Same reason as
    # L_STORE_GLOBAL / L_BIND: ..."), so a name is keyed to the first
    # occurrence that starts a line -- and to the last one when no occurrence
    # does, which is how `} L_CALL_FUNC: {` is written.
    out, fallback = {}, {}
    for i, (start, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(vm)
        line = vm[vm.rfind("\n", 0, start) + 1 : start]
        if line.strip() == "":
            out.setdefault(name, vm[start:end])
        else:
            fallback[name] = vm[start:end]
    for name, body in fallback.items():
        out.setdefault(name, body)
    return out


def measured():
    """Coverage measured from the sources: dispatch, disassembler, handlers."""
    vm = VM_C.read_text()
    start = vm.index("switch (ins.op)")
    dispatch = _cases(vm[start : vm.index("default:", start)])

    d = vm.index("static void vm_disasm_ins(")
    disasm_fn = vm[d : vm.index("default:", d)]
    return {"interp": dispatch, "disasm": _cases(disasm_fn),
            "disasm_names": _disasm_names(disasm_fn), "handlers": _handlers(vm)}


def report(ops, shape, meas, out=sys.stdout):
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
        if missing:
            bad.append(f"{consumer}:missing:{len(missing)}")

    # The name each case prints must be the opcode's own name, and a name the
    # reader cannot read is not evidence: a case that builds the name in a
    # variable instead of a literal used to be skipped, and skipping read as
    # passing.
    wrong, unreadable = [], []
    for n in names:
        if n not in meas["disasm"]:
            continue
        printed = meas.get("disasm_names", {}).get(n)
        if printed is None:
            unreadable.append(n)
        elif printed != n[len("OP_"):]:
            wrong.append((n, printed))
    for n in unreadable:
        print(f"IR-UNREADABLE|op={n}|effect=the-case-prints-a-name-the-checker-cannot-read",
              file=out)
    for n, printed in wrong:
        print(f"IR-WRONGNAME|op={n}|prints={printed}|effect=a-wrong-answer-in-the-shape-of-a-right-one",
              file=out)
    if unreadable:
        bad.append(f"disasm-names-unreadable:{len(unreadable)}")
    if wrong:
        bad.append(f"disasm-names:{len(wrong)}")
    cases = len([n for n in names if n in meas["disasm"]])
    checked = cases - len(unreadable)
    print(f"IR-DISASM|cases={cases}/{len(names)}|names-checked={checked}/{cases}"
          f"|names-unreadable={','.join(unreadable) or 'none'}"
          f"|wrong-name={','.join(n for n, _ in wrong) or 'none'}", file=out)

    # The shape is prose, so it can only be checked against something that is
    # not prose: the handler body.  A shape may forget a register the handler
    # reads (a claim that is too small) or claim one the handler never touches
    # (a claim the code does not confirm).  Both are red; neither is visible to
    # a check that only asks whether the name is an enum member.
    handlers = meas.get("handlers", {})
    forgets, unconfirmed, no_body = [], [], []
    for n in names:
        body = handlers.get(n)
        if body is None:
            no_body.append(n)
            continue
        claims = set("r" + x for x in re.findall(r"\br([123])\b", shape.get(n, "")))
        reads = set("r" + x for x in re.findall(r"ins\.r([123])", body))
        if not claims and reads:
            forgets.append((n, sorted(reads)))
        elif claims and not reads:
            unconfirmed.append((n, sorted(claims)))
    for n in no_body:
        print(f"IR-HANDLER|op={n}|effect=no-handler-body-found-in-the-dispatcher", file=out)
    for n, reads in forgets:
        print(f"IR-SHAPE|op={n}|claims=none|handler-reads={','.join(reads)}"
              f"|effect=declared-shape-forgets-registers", file=out)
    for n, claims in unconfirmed:
        print(f"IR-SHAPE-UNCONFIRMED|op={n}|claims={','.join(claims)}|handler-reads=none"
              f"|effect=the-handler-never-touches-a-register-the-shape-claims", file=out)
    if no_body:
        bad.append(f"no-handler:{len(no_body)}")
    if forgets:
        bad.append(f"shape-forgets:{len(forgets)}")
    if unconfirmed:
        bad.append(f"shape-unconfirmed:{len(unconfirmed)}")
    print(f"IR-SHAPE|checked={len(names) - len(no_body)}/{len(names)}"
          f"|forgets={','.join(n for n, _ in forgets) or 'none'}"
          f"|unconfirmed={','.join(n for n, _ in unconfirmed) or 'none'}"
          f"|no-handler={','.join(no_body) or 'none'}", file=out)

    missing_disasm = [n for n in names if n not in meas["disasm"]]
    print(f"IR-COVER|interp={len(names) - len([n for n in names if n not in meas['interp']])}/{len(names)}"
          f"|disasm={len(names) - len(missing_disasm)}/{len(names)}"
          f"|missing=disasm:{','.join(missing_disasm) or 'none'}"
          f"|budget=none|aot=na|wasm=na|selfhost=na", file=out)
    return 0 if not bad else 1


def main(argv):
    ops = declared_ops()
    shape = dict(SHAPE)
    meas = measured()

    if "--self-check" in argv:
        # Each mutation must be caught.  Five are input-side (the table lies);
        # the sixth is the one this table used to be blind to -- a shape that
        # forgets registers its own handler reads.
        ok = True
        drop = [n for n, _ in ops if n in shape][0]
        mutated = {k: v for k, v in shape.items() if k != drop}
        ok &= _check("unshaped", report(ops, mutated, meas, out=open("/dev/null", "w")))
        ok &= _check("stale", report(ops, dict(shape, **{"OP_THIS_IS_NOT_REAL": "r1 = 0"}),
                                    meas, out=open("/dev/null", "w")))
        worst = [n for n, _ in ops if n in meas["disasm"]][0]
        blind = dict(meas, disasm=set(meas["disasm"]) - {worst})
        ok &= _check("coverage", report(ops, shape, blind, out=open("/dev/null", "w")), worst)
        names_map = dict(meas.get("disasm_names", {}))
        first, second = [n for n, _ in ops if n in names_map][:2]
        names_map[first], names_map[second] = names_map[second], names_map[first]
        ok &= _check("wrongname", report(ops, shape, dict(meas, disasm_names=names_map),
                                        out=open("/dev/null", "w")), f"{first}<-{second}")
        unread = dict(meas.get("disasm_names", {}))
        unread[first] = None
        ok &= _check("unreadable", report(ops, shape, dict(meas, disasm_names=unread),
                                         out=open("/dev/null", "w")), first)
        forget = [n for n in shape
                  if re.search(r"ins\.r[123]", meas["handlers"].get(n, ""))
                  and set(re.findall(r"\br([123])\b", shape[n]))]
        mut = dict(shape, **{forget[0]: "- (claims nothing)"})
        ok &= _check("shape-forgets", report(ops, mut, meas, out=open("/dev/null", "w")), forget[0])
        return 0 if ok else 1

    return report(ops, shape, meas)


def _check(case, rc, op=None):
    where = f"|op={op}" if op else ""
    print(f"IRCHECK|case={case}|red={'yes' if rc else 'no'}{where}", file=sys.stdout)
    return bool(rc)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
