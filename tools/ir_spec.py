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
measured  `handler=`-- the label the dispatcher's case jumps to, read out of
                       `case OP_X: goto L_Y;`.  The column is measured, never
                       typed: sixty-seven hand-written label names would be a
                       second copy of the dispatch, and the copy is what rots.
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
  IR-TABLE|rows=N|handlers-named=N|mismatch=M|source=table
  IR-TABLE-SHADOW|switched=N|readers=<table|none>|why=<who consumes the table today>
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
    "OP_EQK": "r1 = (R[r2] == string_pool[r3])",
    "OP_NEQK": "r1 = (R[r2] != string_pool[r3])",
    "OP_NEW_ARRAY": "r1 = array of the last r3 values on the stack  [unused: r2]",
    "OP_NEW_DICT": "r1 = {r3 pairs from the stack}",
    "OP_NEW_SET": "r1 = merge(stack[sp-1 .. sp-r3])",
    "OP_SET_INTERVAL": "r1 = interval(string_pool[r2 & 0xffffff], lo/hi from the stack)",
    "OP_SET_ADD": "r1 = insert(r1, r2)  [r1 is the set and is mutated]",
    "OP_IN": "R[r1] = (R[r2] in R[r3])",
    "OP_MIN": "r1 = min(r2)",
    "OP_MAX": "r1 = max(r2)",
    "OP_INDEX_GET": "R[r1] = R[r2][R[r3]]",
    "OP_INDEX_SET": "R[r1][R[r2]] = R[r3]",
    "OP_CONCAT": "r1 = left-assoc fold(+) of R[r2 .. r2+r3-1]",
    "OP_LOAD_GLOBAL": "r1 = G[r2]",
    "OP_STORE_GLOBAL": "G[r1] = r2",
    "OP_BIND": "G[r1] = bind(set r2, init r3)",
    "OP_DECLARE": "declare(kind=r1, float_pool[r2])  [no register written]",
    "OP_RECORD": "r3 = record into store gidx=r1, packed R[r2] (sidx | meta<<16)",
    "OP_JUMP": "ip = r2",
    "OP_JUMP_IF_FALSE": "if !truth(r1) ip = r2",
    "OP_JUMP_IF_TRUE": "if truth(r1) ip = r2",
    "OP_CALL_BUILTIN": "r1 = builtin(string_pool[r2])(argc=r3)",
    "OP_CALL_FUNC": "r2 = func[r1](argc=r3)",
    "OP_CALL_VALUE": "r2 = dotted-call of r1 (argc=r3)",
    "OP_MAKE_FUNC": "r1 = closure(fn idx r2, captures from the stack)",
    "OP_LOAD_CAPTURE": "r1 = captured[r2]",
    "OP_RETURN": "return the value in r1 when it names a live slot, else nil",
    "OP_IS_NIL": "r1 = (r2 == nil)",
    "OP_PUSH_REG": "push R[r1]",
    "OP_SAY": "say r1",
    "OP_WAIT": "wait r1",
    "OP_STOP": "- (stop the script)",
    "OP_HALT": "- (halt the VM)",
    "OP_YIELD": "- (cooperative yield)",
    "OP_THREAD_START": "thread[r1] = start(func r2, argc=r3)",
    "OP_THREAD_CTRL": "control thread[r1] with op r2",
    "OP_THREAD_JOIN": "join thread[r1], timeout R[r2] (-1 = infinite)",
    "OP_THREAD_WAIT": "wait thread[r1] for R[r2] seconds  [unused: r3]",
    "OP_THREAD_STATE": "r1 = state(thread r2, attr r3)",
    "OP_THREAD_GOTO": "goto label r2 inside thread[r1]",
    "OP_LOCK": "lock[r1] = r2",
    "OP_SEND": "send thread[r1] msg r2",
    "OP_RECV": "r1 = recv(timeout R[r2] | -1 = block forever)",
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


def _regs(pattern, text):
    """Registers named by `pattern`, in first-occurrence order, deduplicated."""
    out = []
    for m in re.finditer(pattern, text):
        r = "r" + m.group(1)
        if r not in out:
            out.append(r)
    return out


DST_RE = (r"(?:R\[ins\.r([123])\]\s*(?:\.|[+\-*/%&|^]?=)"
          r"|\w+\(&R\[ins\.r([123])\]|&R\[ins\.r([123])\])")


def _dst_list(body):
    """Registers written by the body: a store into R[ins.rN] or an &R[ins.rN] out-parameter."""
    out = []
    for m in re.finditer(DST_RE, body):
        r = "r" + (m.group(1) or m.group(2) or m.group(3))
        if r not in out:
            out.append(r)
    return out


def _handler_order(body):
    """The operand order the handler body itself implies: writes first, then reads.

    The dispatcher's bodies read their sources and then store into the
    destination, so first-occurrence order alone puts the destination last --
    which is not what a disassembly line shows.  Registers that appear in a
    write position (a store into `R[ins.rN]`, or a `&R[ins.rN]` out-parameter)
    are taken as the destination; the rest keep their order.
    """
    dsts = _dst_list(body)
    rest = []
    for r in _regs(r"ins\.r([123])", body):
        if r not in dsts:
            rest.append(r)
    return dsts + rest


def _disasm_regs(case_text):
    """Registers the case actually prints.

    Only the print statement counts.  Scanning the whole case body picks up
    scratch reads (CALL_BUILTIN resolves a name through ins->r2 before it
    prints r1/r3), which is the ruler inventing a disagreement.
    """
    lines = [l for l in case_text.splitlines() if "snprintf(" in l or "printf(" in l]
    return _regs(r"ins->r([123])", "\n".join(lines))


def _case_bodies(fn_text):
    hits = [(m.start(), m.group(1)) for m in re.finditer(r"case (OP_[A-Z0-9_]+):", fn_text)]
    out = {}
    for i, (start, name) in enumerate(hits):
        end = hits[i + 1][0] if i + 1 < len(hits) else len(fn_text)
        out[name] = fn_text[start:end]
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


def _switch_targets(vm):
    """opcode -> the label its case jumps to, or {} when no switch is left.

    Read separately from the handler table on purpose: this is the count of
    cases in this tree, and after the table takes over dispatch it has to fall
    to zero while the table's rows stay up.  One number cannot say both.
    """
    try:
        start = vm.index("switch (ins.op)")
    except ValueError:
        return {}
    end = vm.index("default:", start)
    return {m.group(1): m.group(2) for m in
            re.finditer(r"case (OP_[A-Z0-9_]+):\s*goto (L_[A-Z0-9_]+);", vm[start:end])}


def _table_slots(vm):
    """The dispatch table's own initializer: one label address per opcode.

    `[OP_MOV] = &&L_MOV,` -- the address of the body, taken inside the
    function, which is the only place a label address can be taken.  This is
    the table that becomes the dispatch, so the gauge has to read it rather
    than the cases: the cases are what is on its way out, and a gauge still
    reading them would go on printing a number about a thing that is gone.
    Parsed from the source for the same reason the cases were -- the reading
    has to describe this tree, not a copy of it.
    """
    m = re.search(r"static void \*g_ir_body\[68\] = \{(.*?)\};", vm, re.S)
    if m is None:
        return None
    body = m.group(1)
    return {mm.group(1): mm.group(2) for mm in
            re.finditer(r"\[(OP_[A-Z0-9_]+)\] = &&(L_[A-Z0-9_]+)", body)}


def _split_known(vm):
    """Opcodes whose body can be entered from two places, read off the source.

    A body that ends in `goto L_OTHER;` runs another opcode body while the
    instruction is still this opcode, so its opcode is served from two
    places: its own case, and that goto.  This is a source fact, so it cannot
    be a runtime count -- the runtime can only report what a run happened to
    see (split-ops-seen), and a run that never executes the opcode would
    otherwise read as "no opcode is split".
    """
    known, cur = set(), None
    for line in vm.splitlines():
        m = re.search(r"(?:^|[}]\s*)(L_[A-Z0-9_]+):", line)
        if m:
            cur = m.group(1)
        for g in re.finditer(r"goto (L_[A-Z0-9_]+);", line):
            if cur is None or g.group(1) == cur:
                continue
            known.add("OP_" + cur[2:])
    return known

def _dispatch_targets(vm):
    """opcode -> the label its case jumps to: `case OP_X: goto L_Y;`.

    This is the table's `handler=` column, and it is taken from the source for
    the same reason the shapes are measured against the handler bodies: the
    reading must describe *this* tree.  Taken from the dispatch, though, it
    cannot be a copy of the dispatch -- what it can be is stale or broken, and
    the two ways that happens are counted in report(): a case that jumps to a
    label no body defines, and a case that jumps to a label that is not its own
    (renamed or shared, which is how an opcode quietly starts executing another
    opcode's body).  Both are invisible to IR-COVER, which only asks whether a
    case exists at all.
    """
    start = vm.index("switch (ins.op)")
    end = vm.index("default:", start)
    return {m.group(1): m.group(2) for m in
            re.finditer(r"case (OP_[A-Z0-9_]+):\s*goto (L_[A-Z0-9_]+);", vm[start:end])}


def measured():
    """Coverage measured from the sources: dispatch, disassembler, handlers."""
    vm = VM_C.read_text()
    start = vm.index("switch (ins.op)")
    dispatch = _cases(vm[start : vm.index("default:", start)])

    d = vm.index("static void vm_disasm_ins(")
    disasm_fn = vm[d : vm.index("default:", d)]
    return {"interp": dispatch, "disasm": _cases(disasm_fn),
            "disasm_names": _disasm_names(disasm_fn),
            "disasm_cases": _case_bodies(disasm_fn), "handlers": _handlers(vm),
            "targets": _switch_targets(vm), "switch_targets": _switch_targets(vm),
            "table_slots": _table_slots(vm), "split_known": _split_known(vm)}


def report(ops, shape, meas, out=sys.stdout, handler_table=None):
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
    # A shape may also say a slot is *unused*.  That claim has a truth value:
    # if the handler reads the slot, the marker is stale and the shape is wrong
    # in the other direction -- it now hides a live operand behind "unused".
    stale_unused = []
    for n in names:
        body = handlers.get(n)
        if body is None:
            continue
        reads = set("r" + x for x in re.findall(r"ins\.r([123])", body))
        unused = set("r" + x for x in
                     re.findall(r"\[unused:\s*r([123])\]", shape.get(n, "")))
        for u in sorted(unused & reads):
            stale_unused.append((n, u))
    for n, u in stale_unused:
        print(f"IR-SHAPE-UNUSED|op={n}|slot={u}|handler-reads-it=yes"
              f"|effect=the-shape-calls-a-live-operand-unused", file=out)
    if stale_unused:
        bad.append(f"stale-unused:{len(stale_unused)}")

    # Slots written as a bare digit ("cond r2") are neither a register glyph
    # nor a plain immediate, so neither check above can see them: the shape
    # check reads `rN`, the print check reads `r%d`.  Counted, not judged, with
    # the reason on the line -- the same rule as IR-DISASM-REGS.
    bare = []
    for n in names:
        body = handlers.get(n)
        if body is None:
            continue
        reads = set("r" + x for x in re.findall(r"ins\.r([123])", body))
        tokens = set("r" + x for x in re.findall(
            r"\b(?:cond|timeout|op|attr|argc|count|res|base|kind|pair|sidx|meta)"
            r"\s+r?([123])\b", shape.get(n, "")))
        if tokens & reads:
            bare.append(n)
    print(f"IR-SHAPE-BARE|ops={','.join(bare) or 'none'}|count={len(bare)}"
          f"|enforced=no|why=a-slot-written-as-a-bare-digit-is-neither-a-register-glyph"
          f"-nor-an-immediate-so-neither-check-can-see-it", file=out)

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

    # The name of an instruction is not its operands: a case that prints ADD
    # for OP_ADD but reads the registers in the wrong order is a wrong answer
    # that every check above calls right.  The order the printer uses is
    # compared to the order the handler body implies (destination first, then
    # sources), which is a claim the printer cannot satisfy by agreeing with
    # itself.  A register the printer invents is red; a register it omits is
    # allowed only because an operand may print as an immediate -- the
    # relative order of the ones it does print is not optional.
    # What the printer claims about an instruction's operands, checked against
    # what the handler body touches.  Order is deliberately not compared: a
    # register machine has no natural operand order, only roles, and the roles
    # are not declared yet (see P1 of docs/IR_DESIGN.md).  The set is the part
    # that is already decidable, and it is the part that catches a printer
    # which hides the register the instruction writes.
    invent, hidden, unmeasured = [], [], []
    regs_checked = 0
    for n in names:
        body = handlers.get(n)
        case = meas.get("disasm_cases", {}).get(n)
        if body is None or case is None or n in meas.get("no_handler", ()):
            unmeasured.append(n)
            continue
        seen = _regs(r"ins\.r([123])", body)
        hop = re.search(r"goto (L_[A-Z0-9_]+)", body)
        if hop:
            seen += _regs(r"ins\.r([123])", handlers.get(hop.group(1), ""))
        seen = list(dict.fromkeys(seen))
        shown = _disasm_regs(case)
        if not shown or not seen:
            unmeasured.append(n)
            continue
        regs_checked += 1
        extra = [r for r in shown if r not in seen]
        miss = [r for r in seen if r not in shown]
        if extra:
            invent.append((n, shown, seen))
        if miss:
            hidden.append((n, shown, seen))
    for n, shown, seen in invent:
        print(f"IR-DISASM-OPERANDS|op={n}|prints={','.join(shown)}|handler={','.join(seen)}"
              f"|effect=the-line-names-a-register-the-instruction-does-not-touch", file=out)
    for n, shown, seen in hidden:
        print(f"IR-DISASM-HIDDEN|op={n}|prints={','.join(shown)}|handler={','.join(seen)}"
              f"|effect=the-line-hides-a-register-the-instruction-reads-or-writes", file=out)
    print(f"IR-DISASM-REGS|checked={regs_checked}/{len(names)}"
          f"|unmeasured={','.join(unmeasured) or 'none'}"
          f"|unexpected={','.join(n for n, _, _ in invent) or 'none'}"
          f"|hidden={','.join(n for n, _, _ in hidden) or 'none'}"
          f"|compared=register-set-not-order"
          f"|why=roles-are-not-declared-yet-immediates-and-goto-shared-bodies-look-like-mismatches-see-IR_DESIGN-P1", file=out)

    missing_disasm = [n for n in names if n not in meas["disasm"]]
    print(f"IR-COVER|interp={len(names) - len([n for n in names if n not in meas['interp']])}/{len(names)}"
          f"|disasm={len(names) - len(missing_disasm)}/{len(names)}"
          f"|missing=disasm:{','.join(missing_disasm) or 'none'}"
          f"|budget=none|aot=na|wasm=na|selfhost=na", file=out)

    # The handler column: one row per opcode, and the value is the label the
    # dispatcher's case jumps to.  Two ways that stops describing this tree --
    # a case that reaches no body, and a case that runs another opcode's body --
    # are both invisible to IR-COVER, which only asks whether a case exists.
    # This table is read by nobody yet (the shadow line below says so), and a
    # table nobody reads is exactly the one allowed to drift, so it is measured
    # rather than believed.
    # The table is the gauge's source now.  Reading the cases would describe a
    # dispatcher that is on its way out, and once it is gone it would describe
    # nothing while still printing a number.
    slots = handler_table if isinstance(handler_table, dict) else meas.get("table_slots")
    switched = len(meas.get("switch_targets", meas.get("targets", {})))
    if isinstance(slots, dict) and slots:
        targets, source = dict(slots), "table"
    else:
        targets, source = dict(meas.get("targets", {})), "switch"
    bodies = {"L_" + k[3:] for k in meas.get("handlers", {})}
    named, mismatch, shared, renamed = 0, [], {}, []
    for n in names:
        label, own = targets.get(n), "L_" + n[3:]
        if label is None:
            mismatch.append((n, "none", own, "the-row-has-no-handler-at-all"))
        elif label not in bodies:
            mismatch.append((n, label, own, "the-case-jumps-to-a-label-no-body-defines"))
        else:
            named += 1
            shared.setdefault(label, []).append(n)
            if label != own:
                if own in bodies:
                    mismatch.append((n, label, own,
                                     "the-case-runs-another-opcodes-body"))
                else:
                    renamed.append((n, label, own))
    print(f"IR-TABLE|rows={len(names)}|handlers-named={named}|mismatch={len(mismatch)}"
          f"|source={source}", file=out)
    for n, got, want, effect in mismatch:
        print(f"IR-TABLE-MISMATCH|op={n}|handler={got}|expected={want}|effect={effect}", file=out)
    for label, holders in sorted(shared.items()):
        if len(holders) > 1:
            print(f"IR-TABLE-SHARED|handler={label}|ops={','.join(holders)}"
                  f"|effect=one-body-serves-several-opcodes", file=out)
    for n, got, own in renamed:
        print(f"IR-TABLE-RENAMED|op={n}|handler={got}|expected={own}"
              f"|effect=the-opcodes-own-label-is-gone-so-this-is-a-rename-not-a-cross-jump",
              file=out)
    readers = "table" if (source == "table" and switched == 0) else "none"
    why = ("the-dispatcher-no-longer-switches-so-every-opcode-arrives-through-the-table"
           if readers == "table"
           else "the-dispatcher-still-switches-on-ins.op-so-every-opcode-arrives-through-a-case")
    print(f"IR-TABLE-SHADOW|switched={switched}|readers={readers}|why={why}", file=out)
    known_split = sorted(meas.get("split_known") or ())
    split_names = ",".join(known_split) or "none"
    print(f"IR-TABLE-SPLIT-KNOWN|ops={split_names}|count={len(known_split)}"
          f"|basis=a-body-that-jumps-into-another-body-serves-its-opcode-from-two-places", file=out)
    if mismatch:
        bad.append(f"handler-table:{len(mismatch)}")

    # Slots whose role the vocabulary cannot say yet.  OP_CONCAT takes a base
    # register plus a count and folds a range of registers; none of R[rN], a
    # bare digit or the word "imm" describes that, so its shape can only be a
    # sentence and the criteria can only check the endpoints it names.
    vocab_gap = [("OP_CONCAT", "range", "fold(+) of R[r2 .. r2+r3-1]")]
    for n, needs, today in vocab_gap:
        print(f"IR-SHAPE-VOCAB|op={n}|needs={needs}|today={today}|enforced=no"
              f"|why=the-role-vocabulary-has-no-word-for-a-ranged-operand-see-IR_DESIGN-P1",
              file=out)

    # A slot the shape calls a register must be printed as a register.  This is
    # the other direction from IR-DISASM-REGS: that one asks whether a print
    # line shows something the handler does not read, this one asks whether a
    # declared register is hidden from the reader.  It covers only the slots a
    # shape writes with the R[rN] glyph: a bare digit may be an immediate, and
    # those are counted separately by IR-SHAPE-BARE.
    printed_short = []
    for n in names:
        case = meas.get("disasm_cases", {}).get(n)
        if case is None:
            continue
        slots = set("r" + x for x in re.findall(r"R\[r([123])\]", shape.get(n, "")))
        printed = set("r" + x for x in re.findall(r"ins->r([123])", case))
        for miss in sorted(slots - printed):
            printed_short.append((n, miss))
    for n, miss in printed_short:
        print(f"IR-SHAPE-PRINTSHORT|op={n}|declared={miss}|printed-not"
              f"|effect=a-register-the-shape-declares-is-hidden-from-the-reader", file=out)
    if printed_short:
        bad.append(f"prints-short:{len(printed_short)}")
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
        # A shape that calls a live operand "unused" hides it the other way:
        # the marker says nothing is there while the handler reads it.
        live = [n for n in shape
                if re.search(r"ins\.r2\b", meas["handlers"].get(n, ""))
                and "[unused: r2]" not in shape[n]]
        if live:
            mut = dict(shape, **{live[-1]: shape[live[-1]] + "  [unused: r2]"})
            ok &= _check("shape-calls-a-live-slot-unused", report(ops, mut, meas,
                                                                out=open("/dev/null", "w")), live[-1])
        # The bare-slot reading is also not enforced: write one slot as a bare
        # digit and the reading must name that opcode.
        hs = meas["handlers"]
        cand = next((n for n in (o for o, _ in ops)
                     if re.search(r"ins\.r2\b", hs.get(n, ""))
                     and "R[r2]" in shape[n]), None)
        if cand:
            got = _report_text(ops, dict(shape, **{cand: shape[cand] + " timeout r2"}), meas)
            line = next(l for l in got.splitlines() if l.startswith("IR-SHAPE-BARE|"))
            named = cand in line.split("|ops=")[1].split("|")[0].split(",")
            print(f"IRCHECK|case=bare-slot|red={'yes' if named else 'no'}|op={cand}", file=sys.stdout)
            ok &= named
        # A register the shape declares must be printed; dropping it from the
        # print line has to red, otherwise "prints short" is unenforced.
        cases0 = dict(meas.get("disasm_cases", {}))
        tgt = next((n for n in cases0 if "ins->r2" in cases0[n]
                    and "R[r2]" in shape.get(n, "")), None)
        if tgt:
            got = _report_text(ops, shape, dict(meas, disasm_cases=dict(
                cases0, **{tgt: cases0[tgt].replace("ins->r2, ", "", 1)})))
            named = any(l.startswith("IR-SHAPE-PRINTSHORT|")
                        and ("op=" + tgt + "|") in l for l in got.splitlines())
            print(f"IRCHECK|case=prints-short|red={'yes' if named else 'no'}|op={tgt}", file=sys.stdout)
            ok &= named
        # The operand reading is not enforced, so it gets a detection test
        # rather than an enforcement test: drop one register from a print line
        # and the reading must name that opcode.  A reading nobody has ever
        # seen move is not a measurement.
        cases = dict(meas.get("disasm_cases", {}))
        target = next((n for n in cases if "ins->r2" in cases[n]), None)
        if target:
            def _drop(text):
                lines = text.splitlines(True)
                for i, l in enumerate(lines):
                    if ("snprintf(" in l or "printf(" in l) and "ins->r2" in l:
                        lines[i] = l.replace("ins->r2", "0", 1)
                        break
                return "".join(lines)
            got = _report_text(ops, shape, dict(meas, disasm_cases=dict(cases, **{target: _drop(cases[target])})))
            line = next(l for l in got.splitlines() if l.startswith("IR-DISASM-REGS|"))
            caught = target in line.split("|hidden=")[1].split("|")[0].split(",")
            print(f"IRCHECK|case=operand-set|red={'yes' if caught else 'no'}|op={target}", file=sys.stdout)
            ok &= caught
        # The handler column says which label each case jumps to.  The mutation
        # is not a typo in a hand-written table -- it is a dispatch that sends
        # one opcode into another opcode's body, which is the failure a table of
        # handlers exists to catch.  It must red *and name the opcode*: a count
        # that goes up without saying which row is wrong is not evidence.
        tmap = dict(meas.get("targets", {}))
        moved = next((n for n, _ in ops if tmap.get(n)), None)
        donor = next((n for n in tmap if n != moved), None)
        if moved and donor:
            got = _report_text(ops, shape, meas,
                               handler_table=dict(tmap, **{moved: tmap[donor]}))
            named = any(l.startswith("IR-TABLE-MISMATCH|") and ("op=" + moved + "|") in l
                        for l in got.splitlines())
            print(f"IRCHECK|case=handler-table|red={'yes' if named else 'no'}|op={moved}",
                  file=sys.stdout)
            ok &= named
        # The gauge now reads the table, so two facts have to be shown that the
        # old gauge could not show.  (a) The table OP_ADD slot points at OP_SUB
        # while every case still says goto L_ADD: a gauge reading the cases stays
        # green, one reading the table names OP_ADD.  (b) The switch is gone: the
        # table alone must still produce a whole IR-TABLE, with switched=0 and
        # readers=table -- otherwise the gauge dies on the same commit that
        # removes what it used to measure.
        slots = dict(meas.get("table_slots") or {})
        if "OP_ADD" in slots and "OP_SUB" in slots:
            bent = dict(slots, **{"OP_ADD": slots["OP_SUB"]})
            got = _report_text(ops, shape, dict(meas, table_slots=bent))
            lines = got.splitlines()
            named = any(l.startswith("IR-TABLE-MISMATCH|") and "op=OP_ADD|" in l for l in lines)
            from_table = any(l.startswith("IR-TABLE|") and "|source=table" in l for l in lines)
            gone = _report_text(ops, shape, dict(meas, switch_targets={}))
            survives = any(l.startswith("IR-TABLE-SHADOW|switched=0|readers=table")
                           for l in gone.splitlines())
            red = named and from_table and survives
            print(f"IRCHECK|case=gauge-reads-the-table|red={'yes' if red else 'no'}|op=OP_ADD",
                  file=sys.stdout)
            ok &= red
        return 0 if ok else 1

    return report(ops, shape, meas)


def _report_text(ops, shape, meas, handler_table=None):
    import io
    buf = io.StringIO()
    report(ops, shape, meas, out=buf, handler_table=handler_table)
    return buf.getvalue()


def _check(case, rc, op=None):
    where = f"|op={op}" if op else ""
    print(f"IRCHECK|case={case}|red={'yes' if rc else 'no'}{where}", file=sys.stdout)
    return bool(rc)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
