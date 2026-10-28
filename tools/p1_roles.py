#!/usr/bin/env python3
"""P1 entry conditions, executable (draft).

Three predicates over the pair (declared roles, printed tokens).  Roles are
declared by hand from the dispatch handler bodies in src/vm/vm.c; nothing here
infers a role from the position r1/r2/r3, because positions do not mean the
same thing for every opcode (OP_CALL_FUNC's destination is r2; OP_THREAD_STATE's
r2 is a thread index; OP_LOADK_I64's r2/r3 are two halves of one immediate).

  C1  printed registers are a subset of the declared roles
  C2  a declared destination appears among the printed registers
  C3  a declared immediate never appears as a register token

Printed tokens are read from the print calls only (the snprintf calls inside a
case), never from the whole case body: a case may compute (OP_CALL_BUILTIN
resolves its name from ins->r2 before printing) and a computation is not a line.

When a case branches its print (OP_RECV prints "timeout=none" or "timeout=r%d"),
the register set is the union over the branches: reading one branch would let a
register hide in the branch the reader skips, and that green would come from the
reader, not from the tree.

A slot declared `sentinel` may hold a negative sentinel instead of a register.
The tool names such a slot (P1-AMBIG) and reads whether the print call is behind a
test on that slot (`guarded=yes|no`).  That reading says a conversion exists; it
does not say the conversion is correct -- an inverted test still reads guarded=yes.
"""
import argparse
import re
import sys


# Declared from the L_* handler bodies at tree 9a4db3b.  dst/src/obj are
# registers; imm covers immediates, frame-variable indices and table indices --
# anything the handler reads as a number rather than as a register.
ROLES = {
    "OP_MOV":          {"dst": ["r1"], "src": ["r2"]},
    "OP_LOADK_INT":    {"dst": ["r1"], "imm": ["r2"]},
    "OP_LOADK_I64":    {"dst": ["r1"], "imm": ["r2", "r3"]},
    "OP_ADD":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_MOD":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_AND":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_MIN":          {"dst": ["r1"], "src": ["r2"]},
    # --- family 1: pure-register arithmetic / comparison (no immediate operand,
    #     no table index, dst never read as an input) ---
    "OP_SUB":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_MUL":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_DIV":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_NEG":          {"dst": ["r1"], "src": ["r2"]},
    "OP_MAX":          {"dst": ["r1"], "src": ["r2"]},
    "OP_EQ":           {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_NEQ":          {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_LT":           {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_GT":           {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_LE":           {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_GE":           {"dst": ["r1"], "src": ["r2", "r3"]},
    "OP_NOT":          {"dst": ["r1"], "src": ["r2"]},
    "OP_IS_NIL":       {"dst": ["r1"], "src": ["r2"]},
    # --- family 2a: thread family (the operand names a thread or a mailbox on
    #     one; the failure mode is "which thread, and how long to wait") ---
    "OP_THREAD_START": {"dst": ["r2"], "imm": ["r1", "r3"]},
    "OP_THREAD_CTRL":  {"imm": ["r1", "r2"]},
    "OP_THREAD_GOTO":  {"imm": ["r1", "r2"]},
    "OP_THREAD_JOIN":  {"imm": ["r1"], "src": ["r2"], "sentinel": ["r2"]},
    "OP_THREAD_WAIT":  {"imm": ["r1"], "src": ["r2"]},
    "OP_THREAD_STATE": {"dst": ["r1"], "imm": ["r2", "r3"]},
    "OP_SEND":         {"imm": ["r1"], "src": ["r2"]},
    "OP_RECV":         {"dst": ["r1"], "src": ["r2"], "sentinel": ["r2"]},
    "OP_WAIT":         {"src": ["r1"]},
    "OP_YIELD":        {},
    # --- family 5: constructor / interval family (the non-register operand is
    #     not a value operand at all: it is an element count or a packed
    #     string-pool index, and the element values travel on the value stack)
    "OP_NEW_ARRAY":    {"dst": ["r1"], "imm": ["r3"]},
    "OP_NEW_DICT":     {"dst": ["r1"], "imm": ["r3"]},
    "OP_NEW_SET":      {"dst": ["r1"], "imm": ["r3"]},
    "OP_SET_INTERVAL": {"dst": ["r1"], "imm": ["r2"]},
    "OP_SET_ADD":      {"obj": ["r1"], "mutate": True, "src": ["r2"]},
    # --- family 2b: resource family (the operand names a mutex or a resource
    #     limit; the failure mode is "which resource does this index point at") ---
    "OP_LOCK":         {"imm": ["r1", "r2"]},
    "OP_DECLARE":      {"imm": ["r1", "r2"]},
    "OP_INDEX_SET":    {"obj": ["r1"], "mutate": True, "src": ["r2", "r3"]},
    "OP_TRY_START":    {"imm": ["r1", "r2"]},
    # --- family 3: store family, one member (r1 is a store selector and the
    #     destination is r3: the premise "dst is r1" is already false here) ---
    "OP_RECORD":       {"dst": ["r3"], "imm": ["r1", "r2"]},
    # --- family 4: output family, one member (no destination at all) ---
    "OP_SAY":          {"src": ["r1"]},
    "OP_CALL_FUNC":    {"dst": ["r2"], "imm": ["r1", "r3"]},
    "OP_CALL_BUILTIN": {"dst": ["r1"], "imm": ["r2", "r3"]},
    "OP_MAKE_FUNC":    {"dst": ["r1"], "imm": ["r2", "r3"]},
    # --- family 6: k-forms (the non-register operand is a pool index or a
    #     literal, so the failure mode is "which constant", and dst is r1 in
    #     every member; EQK/NEQK also read a source register, and their print
    #     line does not show it -- see P1-PRINTSHORT) ---
    "OP_LOADK_FLOAT":  {"dst": ["r1"], "imm": ["r2"]},
    "OP_LOADK_STRING": {"dst": ["r1"], "imm": ["r2"]},
    "OP_LOADK_BOOL":   {"dst": ["r1"], "imm": ["r2"]},
    "OP_EQK":          {"dst": ["r1"], "src": ["r2"], "imm": ["r3"]},
    "OP_NEQK":         {"dst": ["r1"], "src": ["r2"], "imm": ["r3"]},
    # --- family 7: control-flow and exception family (the non-register operand
    #     is a bytecode offset, not a value; STOP/HALT/TRY_END take no operand at
    #     all; RETURN's r1 is a result register OR 0/-1 meaning nil, which makes
    #     it the second slot in the tree carrying a non-register encoding) ---
    "OP_JUMP":          {"imm": ["r2"]},
    "OP_JUMP_IF_FALSE": {"src": ["r1"], "imm": ["r2"]},
    "OP_JUMP_IF_TRUE":  {"src": ["r1"], "imm": ["r2"]},
    "OP_RETURN":        {"src": ["r1"], "sentinel": ["r1"]},
    "OP_THROW":         {"src": ["r1"]},
    "OP_STOP":          {},
    "OP_HALT":          {},
    "OP_TRY_END":       {},
    "OP_CALL_VALUE":    {"dst": ["r2"], "src": ["r1"], "imm": ["r3"]},
}

# One encoding convention usually has more than one landing site, and the count
# belongs to the convention, not to the defects: "negative means none" is one
# convention with two sites, while RETURN is a different one (0 or -1 means nil).
CONVENTIONS = [
    ("negative-timeout", [("OP_RECV", "r2"), ("OP_THREAD_JOIN", "r2")]),
    ("non-positive-nil-result", [("OP_RETURN", "r1")]),
]

# What the non-register encoding is, per slot, for the P1-AMBIG line.
SENT_NOTE = {("OP_RECV", "r2"): "-1", ("OP_THREAD_JOIN", "r2"): "-1",
             ("OP_RETURN", "r1"): "0,-1"}

CONV = set("diouxXeEfgGcsp")
CASE_RE = re.compile(r"case\s+(OP_[A-Z0-9_]+)\s*:")
PRINT_RE = re.compile(r"snprintf\(\s*out\s*,\s*outsz\s*,\s*\"((?:[^\"\\]|\\.)*)\"(.*?)\)\s*;", re.S)
SLOT_RE = re.compile(r"^ins->r([123])$")

def function_body(src, name):
    at = src.index(name + "(")
    open_brace = src.index("{", src.index(")", at))
    depth = 0
    for i in range(open_brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[open_brace:i + 1]
    raise ValueError("unbalanced braces after " + name)


def split_args(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def conversions(fmt):
    out, i = [], 0
    while i < len(fmt):
        if fmt[i] == "%" and i + 1 < len(fmt) and fmt[i + 1] != "%":
            j = i + 1
            while j < len(fmt) and fmt[j] not in CONV:
                j += 1
            if j >= len(fmt):
                break
            prefix = fmt[i - 1] if i else ""
            out.append((prefix if prefix.isalpha() else "", fmt[j]))
            i = j + 1
        else:
            i += 1
    return out


def printed_tokens(case_text):
    """Every snprintf the case can reach, unioned.

    A case that branches its print (vm.c 5383-5384 prints "timeout=none" or
    "timeout=r%d") has one line with two shapes; reading only the first branch
    would let a register hide in the branch the reader skips. The union is what
    the line can say, which is what the criteria are about."""
    ms = list(PRINT_RE.finditer(case_text))
    if not ms:
        return None
    toks = []
    for m in ms:
        fmt, rest = m.group(1), m.group(2)
        args = split_args(rest.lstrip(",")) if rest.strip() else []
        for n, (prefix, conv) in enumerate(conversions(fmt)):
            slot = None
            if n < len(args):
                sm = SLOT_RE.match(args[n])
                slot = "r" + sm.group(1) if sm else None
            toks.append((prefix, conv, slot, m.start()))
    return toks


def branch_guards(case_text, slot):
    """Heuristic reading: does every call that prints `slot` as a register
    sit behind a test on that same slot (600 chars of lookback)?

    This is a reading, not a criterion. It can say the code visibly decides
    before printing, which is enough to say a sentinel cannot reach the r%d
    branch; it cannot say the decision is correct."""
    out = []
    for m in PRINT_RE.finditer(case_text):
        fmt, rest = m.group(1), m.group(2)
        args = split_args(rest.lstrip(",")) if rest.strip() else []
        prints_slot = False
        for n, (prefix, conv) in enumerate(conversions(fmt)):
            if n < len(args) and prefix == "r" and conv in "diu":
                sm = SLOT_RE.match(args[n])
                if sm and "r" + sm.group(1) == slot:
                    prints_slot = True
        if not prints_slot:
            continue
        window = case_text[max(0, m.start() - 600):m.start()]
        g = None
        for gm in re.finditer(r"ins->r([123])", window):
            tail = window[gm.end():gm.end() + 20]
            if re.match(r"\s*(<=|>=|<|>|==|!=)", tail):
                g = gm
        if g is None or ("r" + g.group(1)) != slot:
            out.append(False)
        else:
            out.append(True)
    return out


def read_cases(src):
    body = function_body(src, "vm_disasm_ins")
    marks = [(m.start(), m.group(1)) for m in CASE_RE.finditer(body)]
    cases = {}
    for n, (start, op) in enumerate(marks):
        end = marks[n + 1][0] if n + 1 < len(marks) else len(body)
        cases[op] = body[start:end]
    return cases

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vm", default="src/vm/vm.c")
    a = ap.parse_args()
    src = open(a.vm, encoding="utf-8", errors="replace").read()
    cases = read_cases(src)

    bad = []
    ambig = []
    guard_stat = {}
    short = []
    declared = [op for op in cases if op in ROLES]
    unmeasured = sorted(op for op in cases if op not in ROLES)

    for op in sorted(declared):
        toks = printed_tokens(cases[op])
        nprints = len(list(PRINT_RE.finditer(cases[op])))
        if nprints > 1:
            print("P1-BRANCH|op=%s|calls=%d|effect=the-register-set-is-the-union-of-the-branches"
                  % (op, nprints))
        if toks is None:
            print("P1-NOPRINT|op=%s|effect=no-print-call-in-the-case" % op)
            bad.append("noprint:" + op)
            continue
        regs, typed, numbers = [], [], []
        for prefix, conv, slot, _at in toks:
            if not slot:
                continue
            if prefix == "r" and conv in "diu":
                regs.append(slot)
            elif prefix and conv in "diu":
                typed.append(prefix + slot[1:])
            else:
                numbers.append(slot)
        roles = ROLES[op]
        dst = roles.get("dst", [])
        ordinary = dst + roles.get("src", []) + roles.get("obj", [])
        imm = roles.get("imm", [])
        sentinel = roles.get("sentinel", [])
        ordinary = ordinary + sentinel

        print("P1-OPS|op=%s|printed-reg=%s|printed-typed=%s|printed-number=%s"
              "|roles=dst:%s,src:%s,obj:%s,imm:%s"
              % (op, ",".join(sorted(set(regs))) or "-", ",".join(typed) or "-",
                 ",".join(numbers) or "-", ",".join(dst) or "-",
                 ",".join(roles.get("src", [])) or "-", ",".join(roles.get("obj", [])) or "-",
                 ",".join(imm) or "-"))
        if sentinel:
            print("P1-OPS-SENTINEL|op=%s|slots=%s" % (op, ",".join(sentinel)))

        extra = sorted(set(r for r in regs if r not in ordinary))
        missing = [r for r in dst if r not in regs]
        leak = [r for r in imm if r in regs]

        if extra:
            print("P1-C1|op=%s|printed=%s|declared=%s|extra=%s"
                  "|effect=a-register-the-roles-do-not-declare"
                  % (op, ",".join(sorted(set(regs))), ",".join(ordinary) or "none", ",".join(extra)))
            bad.append("C1:" + op)
        if missing:
            print("P1-C2|op=%s|dst=%s|printed=%s|missing=%s"
                  "|effect=the-line-hides-the-register-it-writes"
                  % (op, ",".join(dst), ",".join(sorted(set(regs))) or "none", ",".join(missing)))
            bad.append("C2:" + op)
        # 1 only forbids a register the roles do not declare; it says nothing
        # about an operand the roles declare and the line never shows.  2 covers
        # that direction for dst only, so a hidden source is named here and not
        # judged: the three criteria cannot see it.
        for role_name in ("src", "obj"):
            for r in roles.get(role_name, []):
                if r in regs:
                    continue
                print("P1-PRINTSHORT|op=%s|slot=%s|role=%s|printed=%s"
                      "|effect=declared-and-read-but-not-printed-the-line-hides-an-operand"
                      % (op, r, role_name, ",".join(sorted(set(regs))) or "none"))
                short.append("SHORT:" + op + ":" + r)

        for r in sentinel:
            if r in regs:
                ambig.append("AMBIG:" + op)
                note = SENT_NOTE.get((op, r), "-1")
                guards = branch_guards(cases[op], r)
                guarded = bool(guards and all(guards))
                guard_stat[(op, r)] = guarded
                if guarded:
                    print("P1-AMBIG|op=%s|slot=%s|kinds=register|sentinel(%s)|guarded=yes"
                          "|effect=the-print-can-only-reach-the-register-glyph-when-the-slot-is-a-register"
                          % (op, r, note))
                else:
                    print("P1-AMBIG|op=%s|slot=%s|kinds=register|sentinel(%s)|guarded=no"
                          "|effect=the-sentinel-can-be-rendered-with-the-register-glyph"
                          % (op, r, note))
        if leak:
            print("P1-C3|op=%s|imm=%s|printed-reg=%s|leak=%s"
                  "|effect=an-immediate-printed-as-a-register"
                  % (op, ",".join(imm), ",".join(sorted(set(regs))), ",".join(leak)))
            bad.append("C3:" + op)

    print("P1-ROLES|declared=%d/%d|unmeasured=%d" % (len(declared), len(cases), len(unmeasured)))
    if unmeasured:
        print("P1-UNMEASURED|ops=%s|effect=no-role-declared-no-claim-made" % ",".join(unmeasured))
    if ambig:
        print("P1-AMBIG-COUNT|n=%d|effect=declared-outside-what-the-three-criteria-can-see-read-guarded-for-the-printer" % len(ambig))
    for conv, sites in CONVENTIONS:
        seen = [(o, s) for (o, s) in sites if (o, s) in guard_stat]
        if not seen:
            continue
        guarded_n = sum(1 for k in seen if guard_stat[k])
        print("P1-GUARD|convention=%s|sites=%d|guarded=%d|unguarded=%d"
              "|effect=this-counts-landing-sites-not-defects-and-it-goes-down-as-they-are-fixed"
              % (conv, len(seen), guarded_n, len(seen) - guarded_n))
    if short:
        print("P1-PRINTSHORT-COUNT|n=%d|effect=declared-and-read-but-unprinted-named-not-judged"
              % len(short))
    if bad:
        print("P1-VERDICT|red|failed=%s" % ",".join(bad))
        return 1
    print("P1-VERDICT|green|declared=%d|unmeasured=%d|note=an-unmeasured-op-is-not-a-passing-op"
          % (len(declared), len(unmeasured)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
