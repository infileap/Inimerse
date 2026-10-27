#!/usr/bin/env python3
"""gen_bounds_proof.py -- WSL-side generator for the T1 bounded proofs.

Rules (docs/streams/dec-formal-bounds.md):
  1. A proof's object is a MODEL, so every model input is a PIN: a file:line
     plus the verbatim line text.  The pin is checked against the tree on
     every run; a drifted line aborts the generator instead of letting a stale
     model keep proving things about code that has moved.
  2. The three states stay separate: unsat = proved (inside this model),
     sat = counterexample (a finding, not a proof), unknown = not proved.
  3. Every proved claim ships a MUTANT of the proved object that must turn sat;
     every finding ships a candidate PATCH that must turn unsat.  A reading
     without its red (or green) counterpart is not connected to the code.

The generated script is self-contained: it carries the pins, the axiom table
(with the verbatim source line beside every axiom) and plain z3py.  The
Windows interpreter only has to run it.
"""
import argparse
import hashlib
import subprocess
import json
import os
import re
import sys

# --------------------------------------------------------------------------
# pins: exactly what the model reads out of the tree
# --------------------------------------------------------------------------
PINNED = {
    "src/compiler/bytecode.c": {
        274: "    for (int i = 0; i < func_count && i < 1024; i++) {",
    },
    "src/compiler/bytecode.h": {
        76: "    int r1;            /* target or source1 */",
        79: "} RegInstruction;",
        104: "    struct Bytecode *funcs[1024];",
        106: "    int func_argc[1024];       /* argc of each function */",
    },
    "src/runtime/vm_exec_builtin.c": {
        61: "                if (m > 1) { x = vm_array_get(vm, ins.ival - 1, 1); r1 = (x.type == VAL_INT) ? x.ival : 0; }",
        83: "            bc->func_argc[bc->func_count] = (av.type == VAL_INT) ? av.ival : 0;",
    },
    "src/vm/vm.c": {
        479: "    if (g_cur_thread->sp >= 1023) return;",
        3026: "    Value *R = t->R;",
        3085: "                t->base = 0;",
        4034: "            if (!t->closure_env || ins.r2 < 0 || (size_t)ins.r2 >= im_closure_env_size(t->closure_env)) {",
        4068: "            if (fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL) {",
        4074: "            if (t->sp + 1 < argc) {",
        4080: "            if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap) {",
        4094: "            t->base += FRAME_REGS;",
        4096: "            int param_slots = root->func_argc[fidx] > argc ? root->func_argc[fidx] : argc;",
        4103: "            if (param_slots > VM_FRAME_REGS) param_slots = VM_FRAME_REGS;",
        4104: "            for (int i = 0; i < param_slots; i++)",
        4105: "                value_set(&FR[i + 1], VAL_NIL, 0, 0, NULL, NULL);",
        4107: "            for (int i = argc - 1; i >= 0; i--) {",
        4108: "                value_move(&FR[i + 1], &t->stack[t->sp]);",
        4109: "                t->sp--;",
        4112: "            t->frame_sp[t->frame_count - 1] = t->sp;   /* save sp AFTER popping args */",
        4133: "                t->base = t->frame_base[t->frame_count];",
        4519: "            nt->reg = malloc(nt->reg_cap * sizeof(Value));",
        4521: "            nt->R = nt->reg;",
        4523: "            nt->frame_code = calloc(nt->frame_cap, sizeof(Bytecode*));",
    },
    "src/vm/vm.h": {
        73: "#define VM_THREAD_REG_COUNT (2048 * 1024)",
        74: "#define VM_MAX_FRAMES 2048",
        76: "#define VM_TASK_REG_COUNT 8192",
        77: "#define VM_TASK_MAX_FRAMES 128",
        82: "#define VM_FRAME_REGS 1024",
        101: "    Value stack[1024]; int sp;",
    },
}

# A negative pin: inside the executor body these are the only lines that
# mention reg_cap.  If a register-index test is ever added, this set grows and
# the generator stops instead of repeating an out-of-date "no guard" premise.
EXECUTOR_SPAN = (3022, 4400)
REG_CAP_LINES_IN_EXECUTOR = [4080]

CASTS = {"size_t", "int", "long", "unsigned"}
SIZE_FUNCS = {"im_closure_env_size"}

TOK_RE = re.compile(
    r"\s*(?:(->|&&|\|\||==|!=|>=|<=|[<>+\-*/%!(),\[\].?:])|([A-Za-z_][A-Za-z0-9_]*)|([0-9]+))")


def tokenize(text):
    toks, pos = [], 0
    while pos < len(text):
        if text[pos:].strip() == "":
            break
        m = TOK_RE.match(text, pos)
        if not m:
            raise SystemExit("cannot tokenize near: " + text[pos:pos + 24])
        pos = m.end()
        if m.group(1):
            toks.append(("op", m.group(1)))
        elif m.group(2):
            toks.append(("id", m.group(2)))
        else:
            toks.append(("num", int(m.group(3))))
    return toks


class Expr(object):
    def __init__(self, text):
        self.text = text
        self.toks = tokenize(text)
        self.i = 0
        self.ast = self.cond()
        if self.i != len(self.toks):
            raise SystemExit("trailing tokens in: " + text)

    def peek(self, k=0):
        if self.i + k < len(self.toks):
            return self.toks[self.i + k]
        return (None, None)

    def at_op(self, v):
        t = self.peek()
        return t[0] == "op" and t[1] == v

    def at_id(self, v):
        t = self.peek()
        return t[0] == "id" and t[1] == v

    def eat(self, v):
        if self.at_op(v):
            self.i += 1
            return True
        return False

    def expect(self, v):
        if not self.eat(v):
            raise SystemExit("expected " + repr(v) + " in " + repr(self.text))

    def cond(self):
        a = self.lor()
        if self.eat("?"):
            b = self.cond()
            self.expect(":")
            c = self.cond()
            return ("cond", a, b, c)
        return a

    def lor(self):
        a = self.land()
        while self.eat("||"):
            a = ("or", a, self.land())
        return a

    def land(self):
        a = self.eq()
        while self.eat("&&"):
            a = ("and", a, self.eq())
        return a

    def eq(self):
        a = self.cmp()
        while True:
            if self.eat("=="):
                a = ("eq", a, self.cmp())
            elif self.eat("!="):
                a = ("ne", a, self.cmp())
            else:
                return a

    def cmp(self):
        a = self.add()
        for op in (">=", "<=", ">", "<"):
            if self.eat(op):
                return ("cmp", op, a, self.add())
        return a

    def add(self):
        a = self.mul()
        while True:
            if self.eat("+"):
                a = ("add", a, self.mul())
            elif self.eat("-"):
                a = ("sub", a, self.mul())
            else:
                return a

    def mul(self):
        a = self.unary()
        while True:
            if self.eat("*"):
                a = ("mul", a, self.unary())
            elif self.eat("/"):
                a = ("div", a, self.unary())
            elif self.eat("%"):
                a = ("mod", a, self.unary())
            else:
                return a

    def unary(self):
        if self.eat("!"):
            return ("not", self.unary())
        if self.eat("-"):
            return ("neg", self.unary())
        if (self.at_op("(") and self.peek(1)[0] == "id"
                and self.peek(1)[1] in CASTS and self.peek(2) == ("op", ")")):
            typ = self.peek(1)[1]
            self.i += 3
            return ("cast", typ, self.unary())
        return self.postfix(self.primary())

    def primary(self):
        t = self.peek()
        if t[0] == "num":
            self.i += 1
            return ("num", t[1])
        if t[0] == "id":
            self.i += 1
            return ("var", t[1])
        if self.eat("("):
            e = self.cond()
            self.expect(")")
            return e
        raise SystemExit("unexpected token in " + repr(self.text))

    def postfix(self, node):
        while True:
            if self.eat("->"):
                t = self.peek()
                self.i += 1
                node = ("path", node, "->" + t[1])
            elif self.eat("."):
                t = self.peek()
                self.i += 1
                node = ("path", node, "." + t[1])
            elif self.eat("["):
                idx = self.cond()
                self.expect("]")
                node = ("index", node, idx)
            elif self.eat("("):
                args = []
                if not self.at_op(")"):
                    args.append(self.cond())
                    while self.eat(","):
                        args.append(self.cond())
                self.expect(")")
                node = ("call", node, args)
            else:
                return node


def c_text(node):
    tag = node[0]
    if tag == "num":
        return str(node[1])
    if tag == "var":
        return node[1]
    if tag == "path":
        return c_text(node[1]) + node[2]
    if tag == "index":
        return c_text(node[1]) + "[" + c_text(node[2]) + "]"
    if tag == "call":
        return c_text(node[1]) + "(" + ",".join(c_text(a) for a in node[2]) + ")"
    if tag == "cast":
        return "(" + node[1] + ")" + c_text(node[2])
    if tag == "not":
        return "!" + c_text(node[1])
    if tag == "neg":
        return "-" + c_text(node[1])
    if tag == "cond":
        return "(" + c_text(node[1]) + "?" + c_text(node[2]) + ":" + c_text(node[3]) + ")"
    if tag in ("or", "and", "add", "sub", "mul", "div", "mod", "eq", "ne"):
        sym = {"or": "||", "and": "&&", "add": "+", "sub": "-", "mul": "*",
               "div": "/", "mod": "%", "eq": "==", "ne": "!="}[tag]
        return "(" + c_text(node[1]) + sym + c_text(node[2]) + ")"
    if tag == "cmp":
        return "(" + c_text(node[2]) + node[1] + c_text(node[3]) + ")"
    raise SystemExit("c_text: " + tag)


def py_name(canon):
    out = re.sub(r"[^0-9A-Za-z]+", "_", canon).strip("_")
    if not out or out[0].isdigit():
        out = "v_" + out
    return out


class Ctx(object):
    def __init__(self):
        self.ints = {}
        self.bools = {}
        self.sizes = {}
        self.decls = []
        self.extra = []

    def int_var(self, canon):
        if canon not in self.ints:
            name = py_name(canon)
            self.ints[canon] = name
            self.decls.append(name + " = z3.Int(" + repr(canon) + ")")
        return self.ints[canon]

    def bool_atom(self, canon):
        if canon not in self.bools:
            name = "b_" + py_name(canon)
            self.bools[canon] = name
            self.decls.append(name + " = z3.Bool(" + repr(canon) + ")")
        return self.bools[canon]

    def size_var(self, canon):
        if canon not in self.sizes:
            name = "sz_" + py_name(canon)
            self.sizes[canon] = name
            self.decls.append(name + " = z3.Int(" + repr(canon) + ")")
            self.extra.append(name + " >= 0")
            self.extra.append(name + " < 9223372036854775808")
        return self.sizes[canon]


def emit(node, ctx, as_bool=False):
    tag = node[0]
    if tag == "num":
        return str(node[1])
    if tag == "var":
        if node[1] == "NULL":
            return "NULL"
        canon = node[1]
        return ctx.bool_atom(canon) if as_bool else ctx.int_var(canon)
    if tag in ("path", "index", "call"):
        canon = c_text(node)
        if tag == "call":
            head = c_text(node[1])
            if head in SIZE_FUNCS:
                return ctx.size_var(canon)
        return ctx.bool_atom(canon) if as_bool else ctx.int_var(canon)
    if tag == "not":
        return "z3.Not(" + emit(node[1], ctx, True) + ")"
    if tag == "or":
        return "z3.Or(" + emit(node[1], ctx, True) + ", " + emit(node[2], ctx, True) + ")"
    if tag == "and":
        return "z3.And(" + emit(node[1], ctx, True) + ", " + emit(node[2], ctx, True) + ")"
    if tag in ("eq", "ne"):
        left, right = node[1], node[2]
        if (left[0] == "var" and left[1] == "NULL") or (right[0] == "var" and right[1] == "NULL"):
            other = right if left[0] == "var" and left[1] == "NULL" else left
            atom = ctx.bool_atom(c_text(other))
            return atom if tag == "eq" else "z3.Not(" + atom + ")"
        op = "==" if tag == "eq" else "!="
        return "(" + emit(left, ctx) + " " + op + " " + emit(right, ctx) + ")"
    if tag == "cmp":
        return "(" + emit(node[2], ctx) + " " + node[1] + " " + emit(node[3], ctx) + ")"
    if tag in ("add", "sub", "mul", "div", "mod"):
        op = {"add": "+", "sub": "-", "mul": "*", "div": "/", "mod": "%"}[tag]
        return "(" + emit(node[1], ctx) + " " + op + " " + emit(node[2], ctx) + ")"
    if tag == "neg":
        return "(-" + emit(node[1], ctx) + ")"
    if tag == "cond":
        return ("z3.If(" + emit(node[1], ctx, True) + ", " + emit(node[2], ctx)
                + ", " + emit(node[3], ctx) + ")")
    if tag == "cast":
        inner = emit(node[2], ctx)
        if node[1] == "size_t":
            return ("z3.If(" + inner + " < 0, " + inner
                    + " + 18446744073709551616, " + inner + ")")
        return inner
    raise SystemExit("emit: " + tag)


def as_bool_py(text, ctx):
    return emit(Expr(text).ast, ctx, True)


# --------------------------------------------------------------------------
# the axiom table
# --------------------------------------------------------------------------
# kind: measured = a reading this run's command can reproduce;
#       asserted = written down because the code makes it true, not re-read here.
AXIOMS = [
    ("A1", "reg_cap", "measured", [("src/vm/vm.h", 73), ("src/vm/vm.c", 4519)],
     "t->reg_cap == 2097152",
     "VM_THREAD_REG_COUNT is (2048 * 1024) and the register file is malloc'd with reg_cap elements"),
    ("A2", "frame_cap", "measured", [("src/vm/vm.h", 74), ("src/vm/vm.c", 4523)],
     "t->frame_cap == 2048",
     "VM_MAX_FRAMES is 2048 and frame_code is calloc(frame_cap, ...)"),
    ("A3", "frame width", "measured", [("src/vm/vm.h", 82), ("src/vm/vm.c", 4094)],
     "VM_FRAME_REGS == 1024",
     "one call advances base by FRAME_REGS, which is VM_FRAME_REGS (vm.c:3026)"),
    ("A4", "stack extent", "measured", [("src/vm/vm.h", 101), ("src/vm/vm.c", 479)],
     "t->sp >= -1 && t->sp <= 1023",
     "the stack is a fixed 1024-slot array and every push helper refuses at sp >= 1023"),
    ("A5", "func_count band", "measured", [("src/compiler/bytecode.h", 104), ("src/compiler/bytecode.c", 274)],
     "root->func_count >= 0 && root->func_count <= 1024",
     "the funcs[] array is 1024 wide and the reader refuses indices >= 1024"),
    ("A6", "param_slots definition", "measured", [("src/vm/vm.c", 4096)],
     "param_slots == (root->func_argc[fidx] > argc ? root->func_argc[fidx] : argc)",
     "the verbatim right-hand side of the declaration"),
    ("A7", "frame width bounds param_slots (the clamp)", "measured", [("src/vm/vm.h", 82), ("src/vm/vm.c", 4103)],
     "param_slots >= 0 && param_slots <= 1024",
     "line 4103 clamps param_slots to VM_FRAME_REGS before the zero-fill loop, so the bound is "
     "code and not a regime assumption: in T1/T2 this axiom was asserted, and the c6570169 fix "
     "made it measured."),
    ("A8", "base is frame-aligned", "measured",
     [("src/vm/vm.c", 4094), ("src/vm/vm.c", 4133), ("src/vm/vm.c", 3085)],
     "t->base >= 0 && t->base % 1024 == 0",
     "base starts at 0, advances by FRAME_REGS per call, and is restored from frame_base -- "
     "so every reachable base is a whole frame index"),
]

COMMON = ["A1", "A2", "A3", "A4", "A5"]


class Variant(object):
    def __init__(self, name, expect, kind, guard=None, claim=None,
                 axioms=(), raw_axioms=(), note=""):
        self.name = name
        self.expect = expect
        self.kind = kind
        self.guard = guard
        self.claim = claim
        self.axioms = list(axioms)
        self.raw_axioms = list(raw_axioms)
        self.note = note


class Prop(object):
    def __init__(self, pid, title, guard_anchor, guard_text, claim, axioms,
                 variants, story=""):
        self.pid = pid
        self.title = title
        self.guard_anchor = guard_anchor
        self.guard_text = guard_text
        self.claim = claim
        self.axioms = list(axioms)
        self.variants = variants
        self.story = story


PROPS = [
    Prop(
        "P1", "call-func index: the guard is the only thing between fidx and funcs[fidx]",
        ("src/vm/vm.c", 4068),
        "fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL",
        "fidx >= 0 && fidx < root->func_count",
        COMMON,
        [
            Variant("asis", "unsat", "proof",
                    note="the reject condition; its negation is the path that reaches funcs[fidx]"),
            Variant("extent", "unsat", "proof",
                    claim="fidx < 1024",
                    note="guard + A5 (func_count <= 1024) is what bounds fidx by the array; "
                         "the guard alone only bounds it by func_count"),
            Variant("drop_count_check", "sat", "mutant",
                    guard="fidx < 0 || root->funcs[fidx] == NULL",
                    note="the shape AUDIT.md 1.3 found: dropping fidx >= root->func_count leaves "
                         "sat, witness fidx = func_count = 0 (an empty function table)"),
        ],
        story="AUDIT.md 1.3's defect was this disjunct becoming a body instead of a guard.",
    ),
    Prop(
        "P2", "closure slot: the (size_t) cast on the bound side is safe only because r2 < 0 is tested first",
        ("src/vm/vm.c", 4034),
        "!t->closure_env || ins.r2 < 0 || (size_t)ins.r2 >= im_closure_env_size(t->closure_env)",
        "ins.r2 >= 0 && ins.r2 < im_closure_env_size(t->closure_env)",
        COMMON,
        [
            Variant("asis", "unsat", "proof",
                    note="size_t is modelled as the two's-complement wrap of the int"),
            Variant("strict_gt", "sat", "mutant",
                    guard="!t->closure_env || ins.r2 < 0 || (size_t)ins.r2 > im_closure_env_size(t->closure_env)",
                    note="off-by-one: r2 == size then reads env slot size"),
        ],
    ),
    Prop(
        "P3", "frame write: frame_code[frame_count] is inside calloc(frame_cap)",
        ("src/vm/vm.c", 4080),
        "t->frame_count >= t->frame_cap",
        "t->frame_count < t->frame_cap",
        COMMON,
        [
            Variant("asis", "unsat", "proof", note="first conjunct of the call guard"),
            Variant("strict_gt", "sat", "mutant",
                    guard="t->frame_count > t->frame_cap",
                    note="off-by-one: the write lands on frame_code[frame_cap]"),
        ],
    ),
    Prop(
        "P4", "the call guard reserves a whole child frame; the pre-fix +8 slack is now the red variant",
        ("src/vm/vm.c", 4080),
        "t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap",
        "t->base + VM_FRAME_REGS + param_slots < t->reg_cap",
        COMMON + ["A7", "A8"],
        [
            Variant("asis", "unsat", "proof",
                    note="the guard's second conjunct now reserves VM_FRAME_REGS twice, so the last "
                         "admitted base still leaves room for the callee's whole 1024-slot window"),
            Variant("prefix_guard_plus_8", "sat", "mutant",
                    guard="t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap",
                    note="the PRE-FIX guard, kept as the red reading: witness base = 2046*1024 = "
                         "2095104 with param_slots = 1024 writes reg[2097152] = reg[reg_cap], exactly "
                         "one Value past the file. Same model, only the guard differs, so the sat/unsat "
                         "pair attributes the difference to the guard and to nothing else"),
        ],
        story="The guard must keep base + FRAME_REGS + param_slots in range, and the callee addresses "
              "FR[1..param_slots] with param_slots <= VM_FRAME_REGS (A7): reserving 8 slots cannot "
              "cover a 1024-slot frame. On the pre-fix tree this property was sat with a witness; the "
              "fixed tree turns the same encoding unsat, which is the re-measurement T5 reports.",
    ),
    Prop(
        "P5", "param_slots is clamped to the frame width; without the clamp the guard fix alone is not enough",
        ("src/vm/vm.c", 4080),
        "t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap",
        "t->base + VM_FRAME_REGS + param_slots < t->reg_cap",
        COMMON + ["A6", "A8"],
        [
            Variant("asis", "unsat", "proof",
                    axioms=["A7"],
                    note="the clamp at 4103 is the code that bounds param_slots (A7, measured), so "
                         "under the fixed guard the init loop cannot leave the frame"),
            Variant("no_clamp", "sat", "mutant",
                    note="remove the clamp (A7) and the same guard still overflows: witness argc = "
                         "2048, base = 2094080, so the zero-fill walks past reg_cap. The clamp is "
                         "load-bearing"),
            Variant("clamp_only_prefix_guard", "sat", "mutant",
                    guard="t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap",
                    axioms=["A7"],
                    note="clamp without the guard fix: still sat at base = 2095104, so the two fixes "
                         "cover different halves -- P4 and P5 stay independent on the fixed tree too"),
            Variant("declared_2000_prefix_guard", "sat", "mutant",
                    guard="t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap",
                    raw_axioms=["root->func_argc[fidx] == 2000"],
                    note="the magnitude on the pre-fix guard: a handcrafted func_argc of 2000 makes "
                         "the init loop write 976 slots past the register file at the same depth"),
        ],
        story="bc_from_data copies func_argc out of the caller's dict with no band check and is "
              "reachable from .im through the vm_exec builtin (src/runtime/vm_exec_builtin.c:83). "
              "A6 plus A8 alone do not bound param_slots; only the clamp (A7) does, and on the fixed "
              "tree that clamp is measured code rather than an assumption.",
    ),
    Prop(
        "P6", "register operands: ins.r1 is an int with no guard, so R[ins.r1] is unchecked",
        ("src/vm/vm.c", 3026),
        "0",
        "ins.r1 >= 0 && ins.r1 < t->reg_cap",
        COMMON,
        [
            Variant("asis", "sat", "finding",
                    note="witness ins.r1 = -1 (one slot before the file); an .im-scale operand "
                         "such as -5000000 is 160 MB before it, and R = t->R = t->reg"),
            Variant("candidate_guard", "unsat", "candidate-fix",
                    guard="ins.r1 < 0 || ins.r1 >= t->reg_cap",
                    note="candidate: reject the operand, the way the call index already is"),
        ],
        story="Absence premise, pinned negatively: inside the executor body the only line that "
              "mentions reg_cap is the call guard at 4080, so R[ins.rN] has no bound at all.",
    ),
    Prop(
        "P7", "argument pops: the sp+1 < argc guard is the only thing above the stack floor",
        ("src/vm/vm.c", 4074),
        "t->sp + 1 < argc",
        "t->sp + 1 - argc >= 0",
        COMMON,
        [
            Variant("asis", "unsat", "proof",
                    note="the drain loop at 4107-4109 has no per-step floor test, so this guard "
                         "carries the whole invariant"),
            Variant("drop_guard", "sat", "mutant",
                    guard="0",
                    note="remove the check: argc = sp+2 reads stack[-1]"),
        ],
    ),
]


def sanitize_source_lines(path, cache):
    if path not in cache:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            cache[path] = fh.read().split("\n")
    return cache[path]


def pin_ok(want, got):
    """Exact line text; a trailing comment is allowed, anything else is drift."""
    if got == want:
        return True
    if got.startswith(want):
        rest = got[len(want):]
        return rest.strip() == "" or (rest.lstrip().startswith("/*") and rest.rstrip().endswith("*/"))
    return False


def locate_root(explicit=None):
    """Find the repo root from the tree, never from where this file happens to sit.

    A reader holding only a sha has to be able to re-run a reading, so the default has to
    describe the tree, not the directory the script was checked out into.  Returns
    (root, problem); a problem is a sentence for stderr, never a traceback.
    """
    def usable(p):
        return bool(p) and os.path.isdir(os.path.join(p, "src", "vm"))

    if explicit:
        p = os.path.abspath(explicit)
        if usable(p):
            return p, None
        return None, "--root %s is not a repo root (no src/vm under it)" % explicit

    try:
        here = os.path.dirname(os.path.abspath(__file__))
        r = subprocess.run(["git", "-C", here, "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True, timeout=30)
        if r.returncode == 0 and usable(r.stdout.strip()):
            return os.path.abspath(r.stdout.strip()), None
    except (OSError, subprocess.SubprocessError):
        pass
    p = os.path.dirname(os.path.abspath(__file__))
    while True:
        if usable(p):
            return p, None
        parent = os.path.dirname(p)
        if parent == p:
            return None, "cannot locate the repo root; --root is required"
        p = parent


def git_head(root):
    """The tree the pins were just checked against, as git reports it."""
    try:
        out = subprocess.run(["git", "-C", root, "rev-parse", "--short=7", "HEAD"],
                             capture_output=True, text=True)
        return out.stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def git_blob_sha(root, path):
    """git's content address for the pinned file: sha1("blob <len>\\0" + bytes).

    A line number plus its verbatim text still leaves one hole a reader cannot see
    from the output: whether the bytes the pins were read from are the bytes the
    proof model was built for.  The blob sha closes it by naming the content, and
    it is stable across rebases that do not touch the file.
    """
    with open(os.path.join(root, path), "rb") as fh:
        data = fh.read()
    h = hashlib.sha1()
    h.update(("blob %d" % len(data)).encode("ascii") + b"\0")
    h.update(data)
    return h.hexdigest()


# tree=38e1acf (T6).  Moved from 434b93d1d0508758 when the axiom ledger found that A4's
# second pin, src/vm/vm.c:479, had been dropped from PINNED by the T5 re-pin: 32 of the 33
# declared pins were being checked.  Restoring the pin adds one header line, so the model
# fingerprint moves -- the model grew a checked pin, it did not change any constraint.
SELF_CHECK_EXPECT_SHA256 = "dda5ff4a5fc75eb5"


def self_check_pins(root):
    """Negative control for the pin table: a drift must exit 2 and must not write a script.

    The control perturbs a COPY of the tree, never the tree itself, and the copy is
    committed into its own throwaway git repo so the GIT-BLOB path is exercised too.
    Three drifts are used, all on the one line the propositions hang from: a line
    inserted above it (every number now one too high), a character changed inside it,
    and the line deleted (the numbers below it now point at their neighbours).
    """
    import shutil
    import tempfile
    exp = SELF_CHECK_EXPECT_SHA256
    gen = os.path.abspath(__file__)
    target = None
    for lineno, text in sorted(PINNED["src/vm/vm.c"].items()):
        if "VM_FRAME_REGS + VM_FRAME_REGS" in text:
            target = (lineno, text)
    if target is None:
        print("SELFCHECK-PIN|refuse|why=the pinned guard line is not in PINNED", file=sys.stderr)
        return 2
    gl, _gtext = target
    results = []
    with tempfile.TemporaryDirectory(prefix="z3pin-") as td:
        for case in ("clean", "line-shift", "char", "truncate", "unpinned"):
            troot = os.path.join(td, case)
            for path in sorted(PINNED):
                dst = os.path.join(troot, path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(root, path), dst)
            vmc = os.path.join(troot, "src/vm/vm.c")
            with open(vmc, "rb") as fh:
                lines = fh.read().split(b"\n")
            if case == "line-shift":
                lines = [b"/* one line inserted above everything: every pinned number now points one line too high */"] + lines
            elif case == "char":
                lines[gl - 1] = lines[gl - 1].replace(b"frame_count", b"frame_count ", 1)
            elif case == "truncate":
                del lines[gl - 1]
            elif case == "unpinned":
                lines = lines + [b"/* an appended line that no pin points at */"]
            with open(vmc, "wb") as fh:
                fh.write(b"\n".join(lines))
            subprocess.run(["git", "init", "-q"], cwd=troot, capture_output=True)
            subprocess.run(["git", "-C", troot, "add", "-A"], capture_output=True)
            subprocess.run(["git", "-C", troot, "-c", "user.email=pin@check",
                            "-c", "user.name=pin", "commit", "-qm", case],
                           capture_output=True)
            out = os.path.join(troot, "out", "bounds_proof.py")
            fresh = case == "line-shift"
            sentinel = b"# SENTINEL: a drift must not touch this file\n"
            if not fresh:
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with open(out, "wb") as fh:
                    fh.write(sentinel)
            r = subprocess.run([sys.executable, gen, "--root", troot, "--out", out],
                               capture_output=True, text=True)
            named = ""
            for line in (r.stderr or "").splitlines():
                if line.startswith("PIN FAILED:"):
                    named = line.split("PIN FAILED: ", 1)[1].split()[0]
                    break
            if fresh:
                state = "not_created" if not os.path.exists(out) else "created"
                sha = "-"
                ok = r.returncode == 2 and named != "" and state == "not_created"
                expect = "exit2+named_line+out_not_created"
            elif case in ("clean", "unpinned"):
                sha = "-"
                if os.path.exists(out):
                    with open(out, "rb") as fh:
                        body = fh.read()
                    sha = hashlib.sha256(body).hexdigest()[:16]
                ok = r.returncode == 0 and sha == exp
                expect = "exit0+sha256=%s+script_written" % exp
            else:
                with open(out, "rb") as fh:
                    state = "sentinel_intact" if fh.read() == sentinel else "overwritten"
                sha = "-"
                ok = r.returncode == 2 and named != "" and state == "sentinel_intact"
                expect = "exit2+named_line+out_sentinel_intact"
            if case in ("clean", "unpinned"):
                got = "exit%d+sha256=%s+%s" % (r.returncode, sha,
                                               "written" if os.path.exists(out) else "absent")
            else:
                got = "exit%d+named=%s+out=%s" % (r.returncode, named or "none", state)
            results.append(ok)
            print("SELFCHECK-PIN|case=%s|expect=%s|got=%s|verdict=%s"
                  % (case, expect, got, "PASS" if ok else "FAIL"))
    print("SELFCHECK-PIN-DIRECTION|drift cases must exit 2, name the drifted pin, and leave the"
          " out path untouched; only the clean case may write, and its sha256 must equal %s"
          " -- a clean run whose sha moved means the model changed, not that the check got stricter" % exp)
    fails = results.count(False)
    print("SELFCHECK-PIN|summary|cases=%d|pass=%d|fail=%d"
          % (len(results), len(results) - fails, fails))
    if fails:
        print("SELFCHECK-PIN-FAILURE|红了先问：模型变了，还是 pin 漂了？两者都不是，才是工具坏了。")
    return 0 if fails == 0 else 1


def guard_pins(props, pinned):
    """Model text versus source text, for every proposition whose anchor is a guard.

    A blob sha says which bytes were read; this line says whether the condition the
    solver was given is one of them.  Without it a sat->unsat move could come from a
    pin that drifted onto some other line that happens to be true.

    `verbatim` = the pinned line is exactly "if (<model guard>) {";
    `conjunct` = the model guard is one conjunct of the pinned line (P3 models only
    the first conjunct of the call guard, on purpose);
    `not-a-guard-anchor` = the premise is an absence, not a condition (P6: the anchor
    is the R alias, and the absence claim is carried by the negative pin instead).
    """
    problems = []
    for prop in props:
        path, lineno = prop.guard_anchor
        want = pinned.get(path, {}).get(lineno)
        if want is None:
            problems.append("%s: no pinned line at %s:%d" % (prop.pid, path, lineno))
            continue
        if not want.strip().startswith("if ("):
            print("GUARD-PIN|%s|%s:%d|model_vs_source=not-a-guard-anchor|line=%s"
                  % (prop.pid, path, lineno, want.strip()[:88]))
            continue
        verbatim = want.strip() == "if (" + prop.guard_text + ") {"
        conjunct = prop.guard_text in want
        verdict = "verbatim" if verbatim else ("conjunct" if conjunct else "DIFFER")
        print("GUARD-PIN|%s|%s:%d|model_vs_source=%s|line=%s"
              % (prop.pid, path, lineno, verdict, want.strip()[:88]))
        if verdict == "DIFFER":
            problems.append("%s: model %r is not in source %r" % (prop.pid, prop.guard_text, want))
    return problems


# tree=9c13615: 5 pinned files, 33 pinned lines.  This number is written DOWN, not
# counted from PINNED, because a table cannot prove its own completeness: walking
# the table to check the table is exactly how the A4 hole survived -- the pin was
# declared by the axiom and absent from the table, so every check that iterates the
# table agreed with itself.  Moving this constant is a deliberate act, and the
# reason goes in the same commit as the move.
EXPECT_PIN_COUNT = 33


def declared_pins(axioms=None, props=None):
    """Every (path, line) some axiom or proposition claims to rest on."""
    axioms = AXIOMS if axioms is None else axioms
    props = PROPS if props is None else props
    out = []
    for ax in axioms:
        out.extend(ax[3] or [])
    for prop in props:
        out.append(prop.guard_anchor)
    return out


def pin_table_problems(pinned=None, axioms=None, props=None):
    """Two directions, both checked against something outside the table itself.

    (1) the table holds the expected number of entries -- compared with a written
        constant, not with len(PINNED);
    (2) every pin some axiom or proposition declares is actually in the table --
        this is the direction the A4 hole fell through, since a declaration that
        never reaches the table is invisible to a table walk.
    """
    pinned = PINNED if pinned is None else pinned
    axioms = AXIOMS if axioms is None else axioms
    props = PROPS if props is None else props
    problems = []
    held = sum(len(v) for v in pinned.values())
    if held != EXPECT_PIN_COUNT:
        problems.append("pin entry missing: expected %d, table holds %d"
                        % (EXPECT_PIN_COUNT, held))
    declared = sorted(set(declared_pins(axioms, props)))
    missing = [loc for loc in declared if loc[1] not in pinned.get(loc[0], {})]
    for path, lineno in missing:
        problems.append("declared pin not in the table: %s:%d" % (path, lineno))
    return problems, held, len(declared), missing


def check_pins(root, cache):
    problems = []
    pins = []
    digests = {}
    for path, lines in sorted(PINNED.items()):
        full = os.path.join(root, path)
        with open(full, "rb") as fh:
            data = fh.read()
        digests[path] = hashlib.sha256(data).hexdigest()
        src = sanitize_source_lines(full, cache)
        for lineno, want in sorted(lines.items()):
            got = src[lineno - 1] if lineno <= len(src) else "<no such line>"
            ok = pin_ok(want, got)
            if not ok:
                problems.append("%s:%d pin drift\n  pinned: %r\n  tree:   %r"
                                % (path, lineno, want, got))
            pins.append((path, lineno, got.strip(), ok))
    return pins, digests, problems


def check_negative_pins(root, cache, problems):
    src = sanitize_source_lines(os.path.join(root, "src/vm/vm.c"), cache)
    lo, hi = EXECUTOR_SPAN
    found = [i for i in range(lo, hi + 1) if i <= len(src) and "reg_cap" in src[i - 1]]
    if found != REG_CAP_LINES_IN_EXECUTOR:
        problems.append("negative pin drift: reg_cap is mentioned in the executor body at %r, "
                        "pinned %r -- re-read P6's premise before trusting it"
                        % (found, REG_CAP_LINES_IN_EXECUTOR))
    return found


def self_check_pin_table(root):
    """Does the table-check itself walk the table?  These four cells answer it.

    The first three hand the checker its inputs directly, so a version that walked
    PINNED instead of comparing against written expectations would pass all three and
    be caught nowhere; the fourth drives the real CLI with one entry removed and
    asserts the real refusal path (exit 2, the message, and no script written), so the
    control is a reading about the tool and not just about a helper function.
    """
    results = []
    problems, held, declared_n, _missing = pin_table_problems()
    results.append(("intact", "exit0+pins=%d+declared=%d" % (held, declared_n), problems, None))

    cut = dict((k, dict(v)) for k, v in PINNED.items())
    victim_path = sorted(cut)[0]
    victim_line = sorted(cut[victim_path])[0]
    del cut[victim_path][victim_line]
    p2, held2, _, _ = pin_table_problems(pinned=cut)
    results.append(("dropped-entry",
                    "pin entry missing: expected %d, table holds %d" % (EXPECT_PIN_COUNT, held2),
                    p2, None))

    # Built from a list rather than written as a registry-style tuple literal: the
    # ledger's site scan looks for that shape anywhere in this file, comments included,
    # and a negative control must not look like a ninth axiom to the scanner (it read
    # sites=19 twice -- once from the literal, once from naming it in this comment --
    # which is the control leaking into the reading it exists to check).
    ghost_row = tuple(["A9", "ghost pin (negative control)", "measured",
                       [("src/vm/vm.h", 999999)], "false", "control only"])
    ghost = list(AXIOMS) + [ghost_row]
    p3, _, _, _ = pin_table_problems(axioms=ghost)
    results.append(("declared-not-in-table",
                    "declared pin not in the table: src/vm/vm.h:999999", p3, None))

    fails = 0
    for case, expect, probs, _ in results:
        got = probs[0] if probs else "no findings"
        ok = (not probs) if case == "intact" else any(expect in p for p in probs)
        fails += 0 if ok else 1
        print("PINSCHECK|case=%s|expect=%s|got=%s|verdict=%s"
              % (case, expect, got, "PASS" if ok else "FAIL"))

    # The real CLI, with one entry removed at load time: the refusal must name the
    # count and must not leave a script behind.
    import tempfile
    gen = os.path.abspath(__file__)
    drop = "%s:%d" % (victim_path, victim_line)
    with tempfile.TemporaryDirectory(prefix="z3pintab-") as td:
        out = os.path.join(td, "out", "bounds_proof.py")
        r = subprocess.run([sys.executable, gen, "--root", root, "--out", out,
                            "--drop-pin-entry", drop], capture_output=True, text=True)
        text = (r.stderr or "") + (r.stdout or "")
        named = "pin entry missing: expected %d, table holds %d" % (EXPECT_PIN_COUNT, held2)
        wrote = os.path.exists(out)
        ok = r.returncode == 2 and named in text and not wrote
        expect = "exit2+%r+out_not_created" % named
        got = "exit%d+%s+out=%s" % (r.returncode,
                                    "named" if named in text else "unnamed",
                                    "created" if wrote else "not_created")
        fails += 0 if ok else 1
        print("PINSCHECK|case=cli-dropped-entry|expect=%s|got=%s|verdict=%s"
              % (expect, got, "PASS" if ok else "FAIL"))
    total = 4
    print("PINSCHECK|summary|cases=%d|pass=%d|fail=%d" % (total, total - fails, fails))
    return 0 if fails == 0 else 2


def self_check_flag_modes(root):
    """Negative control for the switches themselves.

    Same rule as the pin table: a switch that is not run and a switch that passed must not
    print the same thing.  Three modes are driven through the real CLI, so the cells are
    counted by the process that would be counted in a reader's own run: single A is 5,
    single B is 4, both is 9 -- and if any of those three numbers moves, this cell goes
    red rather than quietly reporting the mode it happened to execute.
    """
    import tempfile
    gen = os.path.abspath(__file__)
    cases = []
    with tempfile.TemporaryDirectory(prefix="z3flagmodes-") as td:
        def run(flags, name):
            out = os.path.join(td, name + "_out", "bounds_proof.py")
            r = subprocess.run([sys.executable, gen, "--root", root, "--out", out] + flags,
                               capture_output=True, text=True)
            return r, (r.stdout or "") + (r.stderr or "")

        r, text = run(["--self-check-pins"], "a")
        ok = ("SELFCHECK-PIN|summary|cases=5|pass=5|fail=0" in text)
        cases.append(("single-pins", "cells=5", "cells=5" if ok else "not 5", ok))

        r, text = run(["--self-check-pin-table"], "b")
        ok = ("PINSCHECK|summary|cases=4|pass=4|fail=0" in text)
        cases.append(("single-pin-table", "cells=4", "cells=4" if ok else "not 4", ok))

        r, text = run(["--self-check-pins", "--self-check-pin-table"], "ab")
        has_a = "SELFCHECK-PIN|summary|cases=5|pass=5|fail=0" in text
        has_b = "PINSCHECK|summary|cases=4|pass=4|fail=0" in text
        has_all = "SELFCHECK-ALL|cells=9" in text
        ok = has_a and has_b and has_all and r.returncode == 0
        got = "cells=%d%s" % ((5 if has_a else 0) + (4 if has_b else 0),
                               "+all" if has_all else "-nameless")
        cases.append(("both-flags", "cells=9+SELFCHECK-ALL", got, ok))

        # --quiet may silence commentary, not a measurement.
        out = os.path.join(td, "quiet_out", "bounds_proof.py")
        r = subprocess.run([sys.executable, gen, "--root", root, "--out", out, "--quiet"],
                           capture_output=True, text=True)
        text = (r.stdout or "") + (r.stderr or "")
        has_pins = "PINS|files=5|pins=33" in text
        cases.append(("quiet-keeps-pins", "PINS still printed",
                      "PINS printed" if has_pins else "PINS swallowed",
                      has_pins and r.returncode == 0))

    # A reader outside the repo, with no --root: a sentence, never a traceback.
    with tempfile.TemporaryDirectory(prefix="z3noroot-") as td2:
        copy = os.path.join(td2, "gen_bounds_proof.py")
        with open(gen, "rb") as src, open(copy, "wb") as dst:
            dst.write(src.read())
        out = os.path.join(td2, "out", "bounds_proof.py")
        r = subprocess.run([sys.executable, copy, "--out", out], cwd=td2,
                           capture_output=True, text=True)
        err = r.stderr or ""
        named = "cannot locate the repo root; --root is required" in err
        trace = "Traceback" in err
        ok = r.returncode == 2 and named and not trace
        cases.append(("no-root-outside-repo", "exit2+named+no traceback",
                      "exit%d+%s%s" % (r.returncode, "named" if named else "unnamed",
                                        "+traceback" if trace else ""), ok))

    fails = 0
    for case, expect, got, ok in cases:
        fails += 0 if ok else 1
        print("FLAGMODES|case=%s|expect=%s|got=%s|verdict=%s"
              % (case, expect, got, "PASS" if ok else "FAIL"))
    total = len(cases)
    print("FLAGMODES|summary|cases=%d|pass=%d|fail=%d" % (total, total - fails, fails))
    return 0 if fails == 0 else 2


def build_script(root, pins, digests, neg_lines, gen_sha, drop_axiom=None):
    out = []
    out.append('#!/usr/bin/env python3')
    out.append('"""Generated by tools/z3bounds/gen_bounds_proof.py -- do not edit.')
    out.append('')
    out.append('Tree pins (verbatim line text checked against the tree by the generator):')
    for path, lineno, text, _ok in pins:
        out.append("  %s:%d  %s" % (path, lineno, text))
    out.append('')
    out.append('Negative pin: inside the executor body the only reg_cap mention is %r.' % (neg_lines,))
    if drop_axiom:
        out.append('')
        out.append('MUTATION RUN: axiom %s is dropped from every variant (not the baseline).' % drop_axiom)
    out.append('"""')
    out.append('import json')
    out.append('import sys')
    out.append('import time')
    out.append('import z3')
    out.append('')
    out.append('RESULTS = {}')
    out.append('')
    expect = {}
    for prop in PROPS:
        ctx = Ctx()
        guard_ast = Expr(prop.guard_text).ast
        claim_ast = Expr(prop.claim).ast
        emit(guard_ast, ctx, True)
        emit(claim_ast, ctx, True)
        for ax in AXIOMS:
            if ax[0] in prop.axioms and ax[0] != drop_axiom:
                as_bool_py(ax[4], ctx)
        for var in prop.variants:
            if var.guard:
                as_bool_py(var.guard, ctx)
            if var.claim:
                as_bool_py(var.claim, ctx)
            for ax in AXIOMS:
                if ax[0] in var.axioms and ax[0] != drop_axiom:
                    as_bool_py(ax[4], ctx)
            for txt in var.raw_axioms:
                as_bool_py(txt, ctx)
        out.append("# ------------------------------------------------------------------")
        out.append("# " + prop.pid + ": " + prop.title)
        if prop.story:
            out.append("# " + prop.story)
        out.append("# ------------------------------------------------------------------")
        for line in ctx.decls:
            out.append(line)
        for idx, line in enumerate(ctx.extra):
            out.append("SIZE_AXIOM_%d = (%s)" % (idx + 1, line))
        out.append('')
        for var in prop.variants:
            gtext = var.guard or prop.guard_text
            ctext = var.claim or prop.claim
            ax_ids = prop.axioms + var.axioms
            fn = "check_" + prop.pid + "_" + var.name
            expect[prop.pid + "|" + var.name] = var.expect
            out.append("# " + prop.pid + " / " + var.name + " (" + var.kind + ", expect " + var.expect + ")")
            if var.note:
                out.append("# " + var.note)
            out.append("#   guard: " + gtext)
            out.append("#   claim: " + ctext)
            out.append("def " + fn + "():")
            out.append("    s = z3.Solver()")
            out.append("    s.set('timeout', 20000)")
            for ax in AXIOMS:
                if ax[0] in ax_ids and ax[0] != drop_axiom:
                    g = as_bool_py(ax[4], ctx)
                    out.append("    s.add(" + g + ")  # " + ax[0] + " " + ax[1])
            for idx in range(len(ctx.extra)):
                out.append("    s.add(SIZE_AXIOM_%d)" % (idx + 1))
            for txt in var.raw_axioms:
                out.append("    s.add(" + as_bool_py(txt, ctx) + ")  # variant axiom: " + txt)
            g = emit(Expr(gtext).ast, ctx, True)
            if g != "0":
                out.append("    guard = " + g)
                out.append("    s.add(z3.Not(guard))  # the guard rejecting is the path under test")
            c = emit(Expr(ctext).ast, ctx, True)
            out.append("    claim = " + c)
            out.append("    s.add(z3.Not(claim))")
            out.append("    t0 = time.time()")
            out.append("    r = s.check()")
            out.append("    dt = time.time() - t0")
            out.append("    verdict = 'sat' if r == z3.sat else ('unsat' if r == z3.unsat else 'unknown')")
            out.append("    model = {}")
            out.append("    if r == z3.sat:")
            out.append("        m = s.model()")
            out.append("        for name in " + repr(sorted(ctx.ints.values())) + ":")
            out.append("            model[name] = str(m.eval(globals()[name], model_completion=True))")
            out.append("        for name in " + repr(sorted(ctx.sizes.values())) + ":")
            out.append("            model[name] = str(m.eval(globals()[name], model_completion=True))")
            out.append("    RESULTS[%r] = (verdict, dt, model)" % (prop.pid + "|" + var.name,))
            out.append("    print('RESULT|" + prop.pid + "|" + var.name + "|%s|%.3f' % (verdict, dt))")
            out.append("    if model:")
            out.append("        print('MODEL|" + prop.pid + "|" + var.name + "|' + json.dumps(model, sort_keys=True))")
            out.append("    return verdict")
            out.append('')
    out.append("EXPECT = " + json.dumps(expect, indent=4, sort_keys=True))
    out.append('')
    out.append('def main():')
    out.append('    print("z3 " + z3.get_version_string() + " py " + sys.version.split()[0])')
    for prop in PROPS:
        for var in prop.variants:
            out.append("    check_" + prop.pid + "_" + var.name + "()")
    out.append('    bad = 0')
    out.append('    for key in sorted(EXPECT):')
    out.append('        got = RESULTS[key][0]')
    out.append('        ok = got == EXPECT[key]')
    out.append('        if not ok:')
    out.append('            bad += 1')
    out.append('        print("VERDICT|%s|expect %s|got %s|%s" % (key, EXPECT[key], got, "PASS" if ok else "FAIL"))')
    out.append('    print("SUMMARY|variants=%d|pass=%d|fail=%d" % (len(EXPECT), len(EXPECT) - bad, bad))')
    out.append('    return 0 if bad == 0 else 1')
    out.append('')
    out.append('if __name__ == "__main__":')
    out.append('    sys.exit(main())')
    out.append('')
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=None,
                    help="repo root; default is resolved from the tree (git toplevel, then "
                         "by walking up for src/vm), never from where the script was installed")
    ap.add_argument("--out")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--check-only", action="store_true")
    ap.add_argument("--drop-axiom", default=None, metavar="A7",
                    help="mutation run: emit the script WITHOUT this axiom in any variant "
                         "(the ledger uses this as the non-redundancy test; not a baseline)")
    ap.add_argument("--self-check-pins", action="store_true",
                    help="negative control for the pin table: a drift in a COPY of the tree "
                         "must exit 2 and must not write a script")
    ap.add_argument("--self-check-pin-table", action="store_true",
                    help="negative control for the table's COMPLETENESS: remove an entry and "
                         "the tool must refuse with the count it expected")
    ap.add_argument("--drop-pin-entry", default=None, metavar="path:line",
                    help="negative control only: delete one entry from PINNED at load time, so "
                         "the completeness check can be driven through the real exit path")
    ap.add_argument("--self-check-flag-modes", action="store_true",
                    help="negative control for the switches: single A, single B and both "
                         "must report 5 / 4 / 9 cells, and --quiet must not eat the PINS line")
    args = ap.parse_args()
    any_check = (args.self_check_pins or args.self_check_pin_table
                 or args.self_check_flag_modes)
    if not args.out and not any_check:
        ap.error("--out is required (except with a self-check)")
    root, problem = locate_root(args.root)
    if problem:
        print(problem, file=sys.stderr)
        return 2
    if any_check:
        # Both flags together used to return after the first one, so the second check
        # never ran and its silence looked like agreement.  Run every requested cell and
        # OR the exit codes; the single-flag output stays byte-identical, so a test that
        # pins those summary lines is unaffected.
        rc = 0
        if args.self_check_pin_table:
            rc |= self_check_pin_table(root)
        if args.self_check_pins:
            rc |= self_check_pins(root)
        if args.self_check_flag_modes:
            rc |= self_check_flag_modes(root)
        # The total is counted from the cells the run actually asked for: a combined line
        # that says 9 while fourteen cells ran would be the same silent-skip disease one
        # level up.
        total = ((5 if args.self_check_pins else 0)
                 + (4 if args.self_check_pin_table else 0)
                 + (5 if args.self_check_flag_modes else 0))
        if sum([bool(args.self_check_pins), bool(args.self_check_pin_table),
                bool(args.self_check_flag_modes)]) > 1 and not args.quiet:
            kinds = "+".join(k for k, on in (("pin-drift", args.self_check_pins),
                                             ("completeness", args.self_check_pin_table),
                                             ("flag-modes", args.self_check_flag_modes)) if on)
            print("SELFCHECK-ALL|cells=%d|kinds=%s|note=every requested check runs, and none "
                  "of them is a skip" % (total, kinds))
        return rc
    if args.drop_pin_entry:
        dpath, dline = args.drop_pin_entry.rsplit(":", 1)
        PINNED.get(dpath, {}).pop(int(dline), None)
    cache = {}
    pins, digests, problems = check_pins(root, cache)
    neg = check_negative_pins(root, cache, problems)
    table_problems, held, declared_n, missing = pin_table_problems()
    if problems or table_problems:
        for p in problems:
            print("PIN FAILED: " + p, file=sys.stderr)
        for p in table_problems:
            print("PIN TABLE FAILED: " + p, file=sys.stderr)
        return 2
    gen_path = os.path.abspath(__file__)
    with open(gen_path, "rb") as fh:
        gen_sha = hashlib.sha256(fh.read()).hexdigest()[:16]
    # --quiet silences the commentary, never a measurement: the PINS count, the per-file
    # digests and the guard-pin comparison all run in either mode.  A single reading that
    # a "be quiet" flag can delete is a reading that a reader cannot quote.
    head7 = git_head(root)
    mismatched = guard_pins(PROPS, PINNED)
    if mismatched:
        for line in mismatched:
            print("GUARD-PIN MISMATCH: " + line, file=sys.stderr)
        return 2
    if not args.quiet:
        for path, lineno, text, _ok in pins:
            print("pin ok  %s:%d  %s" % (path, lineno, text[:96]))
        print("pin ok  negative: reg_cap in executor body at %r" % (neg,))
    print("PINS|files=%d|pins=%d|expect=%d|declared=%d|missing_from_table=%d"
          % (len(PINNED), held, EXPECT_PIN_COUNT, declared_n, len(missing)))
    for path, dg in sorted(digests.items()):
        print("sha256  %s  %s" % (dg[:16], path))
    for path in sorted(digests):
        print("GIT-BLOB|path=%s|blob=%s|head=%s" % (path, git_blob_sha(root, path), head7))
    script = build_script(root, pins, digests, neg, gen_sha, drop_axiom=args.drop_axiom)
    out = args.out
    d = os.path.dirname(os.path.abspath(out))
    if d and not os.path.isdir(d):
        os.makedirs(d)
    if args.check_only:
        print("generator ok: %d pins, %d propositions" % (len(pins), len(PROPS)))
        return 0
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(script)
    print("wrote %s  bytes=%d  sha256=%s"
          % (out, len(script.encode("utf-8")),
             hashlib.sha256(script.encode("utf-8")).hexdigest()[:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
