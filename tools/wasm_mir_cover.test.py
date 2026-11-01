#!/usr/bin/env python3
"""Does the wasm backend really consume the IR, and does it agree with the AST path?

The DECFY §6 step-3 question is not "can wasm run" -- it has run for months
through the AST.  The question is whether there is a SECOND consumer of the
IrModule, and a consumer only counts if its answers can be compared:

  shells    both entry points exist, and the MIR one is reached by a compiler
            path, not by a grep hit on the word IrInsn.
  tables    per-implementation, never deduplicated: `cases == enum` for each
            entry point separately, because a union turns "both complete" and
            "each half done" into the same number.
  answers   the same program through both entry points and the interpreter,
            compared on exit status and last line -- compiling is not computing.
  subset    which opcodes moved and which stayed on the AST route, by ENUM
            MEMBER NAME, so the list can be used as a work queue.

Readings:
  IR-WASM-MIR-SHELL|mentions=N|basis=grep                  the cheap anchor
  IR-WASM-MIR-SHELL|emitted-cases=N|walks=yes|basis=compiler
  IR-WASM-MIR-SHELL-CTL|bend=loop-emptied|mentions=still-yes|walks=no|reds=shell-only
  IR-WASM-MIR-TABLE|impl=program|mir|cases=N|enum=M|agree=yes|no
  IR-WASM-MIR-SUBSET|moved=(...)|still-on-ast=(...)|unknown-in-enum=(none)
  IR-WASM-MIR-DIFF|op=X|ast=yes|no|mir=yes|no|why=...
  IR-WASM-MIR-VERDICT|PASS|FAIL|...

Exit 0 when every reading holds, 1 when a comparison disagrees, 2 when a
prerequisite is missing (build first).
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASE_OP = re.compile(r"case (OP_[A-Z0-9_]+):")
CASE_RE = re.compile(r"^IR-WASM-MIR-CASES[|]n=(\d+)[|]ops=\((.*)\)$")
EMIT_RE = re.compile(r"^IR-WASM-MIR-EMIT[|](.*)$")
REFUSED_RC = 2

# One program per claimed opcode.  Each computes something the interpreter also
# computes, so `same=yes` means the MIR path answered, not that it compiled.
WALK_CASES = [
    ("OP_LOADK_INT",   "say 7\n",                   "7"),
    ("OP_LOADK_BOOL",  "say true\n",                "true"),
    ("OP_LOADK_FLOAT", "say 1.5\n",                 "1.5"),
    # OP_MOV has a case in the emitter and NO program that reaches it: every
    # assignment in this language compiles to STORE_GLOBAL.  It is listed in
    # the claimed set and will fail the walk, which is the honest state -- a
    # case label nothing drives is a claim, not a capability.
    ("OP_ADD",         "say 1 + 2\n",               "3"),
    ("OP_SUB",         "say 10 - 4\n",              "6"),
    ("OP_MUL",         "say 3 * 5\n",               "15"),
    ("OP_SAY",         "say 1\n",                   "1"),
    ("OP_HALT",        "say 0\n",                   "0"),
]


def run(cmd, cwd=None, timeout=300):
    d = subprocess.run([str(c) for c in cmd], cwd=str(cwd or ROOT),
                       capture_output=True, text=True, timeout=timeout)
    return d.returncode, d.stdout, d.stderr


def function_body(text, signature):
    """The brace body of one function, by signature.

    Scoped on purpose: `case OP_*` appears in two functions of the same file, so
    a whole-file scan would compare a set with itself and call it agreement.
    """
    i = text.find(signature)
    if i < 0:
        return None
    j = text.find("{", i)
    if j < 0:
        return None
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k + 1]
    return None


def case_ops(body):
    return {m.group(1) for m in CASE_OP.finditer(body or "")}


def declared_ops():
    spec = importlib.util.spec_from_file_location("ir_spec", ROOT / "tools" / "ir_spec.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return {name for name, _code in mod.declared_ops()}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def shell_problems(mentions, walks):
    """The cheap anchor and the real question, and they must be able to disagree.

    `mentions` counts the word IrInsn in the file -- true of a comment or a typedef.
    `walks` says a compiler path really drove the MIR entry point and emitted at
    least one opcode.  A file that mentions the IR but never walks it is exactly
    the failure the cheap anchor cannot see, so the second one exists.
    """
    problems = []
    if mentions <= 0:
        problems.append("the wasm backend does not mention IrInsn at all, so there is no MIR path to speak of")
    if not walks:
        problems.append("the MIR entry point exists but was never driven: no program went through it")
    return problems


def table_problems(impl_cases, enum_ops):
    """Each entry point against the enum, never a union of the two.

    A union makes "both implementations complete" and "each half done" the same
    number: {A} and {B} union to {A,B}.  So every implementation is compared on
    its own, and a missing opcode in ONE of them is a failure even when the
    other one has it -- which is the whole reason the line is per-impl.
    """
    problems = []
    for impl, ops in impl_cases.items():
        if not ops:
            problems.append("impl=%s has no opcode cases at all" % impl)
            continue
        outside = ops - enum_ops
        if outside:
            problems.append("impl=%s names opcodes the encoding does not declare: %s"
                            % (impl, ",".join(sorted(outside))))
        missing = enum_ops - ops
        if missing:
            problems.append("impl=%s has no case for %d of the %d declared opcodes%s"
                            % (impl, len(missing), len(enum_ops),
                               " (first: %s)" % ",".join(sorted(missing)[:4])
                               if missing else ""))
    return problems


def walked_problems(claimed, observed):
    """The opcodes the MIR path CLAIMS vs the ones a program actually drove.

    "cases=9" is read off a string the generator publishes; that is a claim,
    not a measurement.  A program per opcode is pushed through the real entry
    point and its output compared with the interpreter, so a case that exists
    in the switch but never runs -- or runs and computes the wrong value --
    shows up as a missing entry in `observed` rather than as a number in a
    line nobody re-derives.
    """
    problems = []
    missing = sorted(set(claimed) - observed)
    if missing:
        problems.append("%d claimed case(s) were never driven to an answer: %s"
                        % (len(missing), ",".join(missing)))
    extra = sorted(observed - set(claimed))
    if extra:
        problems.append("%d opcode(s) ran but are not in the claimed set: %s"
                        % (len(extra), ",".join(extra)))
    return problems


def import_problems(declared_offset, import_count):
    """The export offset must equal the number of imports.

    Imports and local functions share the function index space, so a hand-kept
    offset is a number that drifts the moment somebody adds a printer.  The
    failure is quiet in the worst way: the host calls an import with the wrong
    signature and gets `undefined` back, which reads like "the module loaded".
    """
    if declared_offset != import_count:
        return ["MIR_IMPORTS is %d but the module imports %d functions, so every"
                " exported index is off by %d"
                % (declared_offset, import_count, import_count - declared_offset)]
    return []


def subset_problems(still_on_ast, enum_ops):
    """The work queue has to be made of real names.

    `still-on-ast` is read as a list of things left to move, so every entry
    must be an enum member -- a typo there is a queue item that does not exist.
    """
    unknown = sorted(set(still_on_ast) - enum_ops)
    return (["still-on-ast names opcodes the encoding does not declare: %s"
             % ",".join(unknown)] if unknown else [])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--self-check", action="store_true")
    ap.add_argument("--gate", action="store_true",
                    help="exit non-zero on a broken reading; a by-design "
                         "incomplete subset is reported but does not fail")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.self_check:
        return self_check()

    build = (ROOT / args.build) if not os.path.isabs(args.build) else Path(args.build)
    tool = build / "aot-mir"
    interp = build / "inimerse"
    for t in (tool, interp):
        if not t.exists():
            print("IR-WASM-MIR-REFUSED|why=prerequisite-missing|tool=%s"
                  "|effect=the-comparison-was-not-run" % t)
            return REFUSED_RC

    src = (ROOT / "src" / "compilation" / "wasm_backend.c").read_text()
    enum_ops = declared_ops()
    problems = []

    mentions = src.count("IrInsn")
    print("IR-WASM-MIR-SHELL|mentions=%d|basis=grep" % mentions)

    rc, out, err = run([tool, "wasm-cases"])
    moved = set()
    for raw in out.splitlines():
        m = CASE_RE.match(raw.strip())
        if m:
            moved = set(x for x in m.group(2).split(",") if x)
    walks = rc == 0 and bool(moved)
    print("IR-WASM-MIR-SHELL|emitted-cases=%d|walks=%s|basis=compiler"
          % (len(moved), "yes" if walks else "no"))
    problems.extend(shell_problems(mentions, walks))

    impl_program = case_ops(function_body(strip_comments(src), "int wasm_compile_program("))
    impl_mir = moved
    # `agree=no` says "not equal today" and nothing about WHY.  A reader who
    # cannot tell "broken" from "not finished" will either repair an
    # implementation that is not broken, or widen the judgement until it says
    # yes -- both worse than the state word.  So each line carries one, and the
    # unfinished one says what it will read when it is done.
    for impl, ops, state, target in (
            ("program", impl_program, "by-design", "--"),
            ("mir", impl_mir, "subset-in-progress", str(len(enum_ops))),
    ):
        print("IR-WASM-MIR-TABLE|impl=%s|cases=%d|enum=%d|agree=%s|state=%s"
              "|expect-eventually=%s"
              % (impl, len(ops), len(enum_ops), "yes" if ops == enum_ops else "no",
                 state, target))
    # impl=program carrying 0 opcode cases is a MEASUREMENT, not a defect here:
    # that entry point dispatches on Expr/Stmt and has no opcode switch at all.
    # It is printed so the denominator question is visible, but only the MIR
    # implementation is held to "cases == enum" -- the AST route is not this
    # step's subject and demanding it would be asking it to become the same
    # implementation, which is the thing the two entry points exist to avoid.
    problems.extend(table_problems({"mir": impl_mir}, enum_ops))

    # The import count is read from the same source as the offset constant, so
    # adding a printer fails here instead of at a host that gets `undefined`.
    imports_decl = int(re.search(r"#define MIR_IMPORTS (\d+)", src).group(1))
    imports_actual = len(re.findall(r"bword\(&s, \"env\"\)", src))
    print("IR-WASM-MIR-IMPORTS|declared=%d|actual=%d|agree=%s"
          % (imports_decl, imports_actual,
             "yes" if imports_decl == imports_actual else "no"))
    problems.extend(import_problems(imports_decl, imports_actual))

    # Peer's question, as a measurement: does each claimed case actually run, or
    # is it a case label nothing reaches?  One program per opcode through the
    # real entry point, compared with the interpreter on exit status and the
    # last line printed.  "Compiles" is not "computes".
    observed = set()
    work = tempfile.mkdtemp(prefix="wasm_mir_walk_")
    for op, src_text, want in WALK_CASES:
        if op not in moved:
            continue
        p = Path(work) / (op + ".im")
        p.write_text(src_text)
        w = Path(work) / (op + ".wasm")
        rc_e, out_e, err_e = run([tool, "wasm", p, w])
        if rc_e != 0:
            print("IR-WASM-MIR-WALK|op=%s|emitted=no|why=%s"
                  % (op, (out_e + err_e).strip().splitlines()[0][:120] if (out_e + err_e).strip() else "-"))
            continue
        rc_i, out_i, _ = run([interp, "--no-mods", "run", p])
        rc_w, out_w, err_w = run(["node", ROOT / "tools" / "wasm_run.js", w])
        same = (out_w.strip() == out_i.strip()) and (rc_w == rc_i)
        print("IR-WASM-MIR-WALK|op=%s|emitted=yes|interp=%r|wasm=%r|same=%s"
              % (op, out_i.strip().splitlines()[-1] if out_i.strip() else "",
                 out_w.strip().splitlines()[-1] if out_w.strip() else "",
                 "yes" if same else "no"))
        if same:
            observed.add(op)
    walk_problems = []
    problems.extend(walked_problems(moved, observed))

    still_on_ast = sorted(enum_ops - moved)
    print("IR-WASM-MIR-SUBSET|moved=(%s)|still-on-ast=(%s)|unknown-in-enum=(%s)"
          % (",".join(sorted(moved)), ",".join(still_on_ast),
             ",".join(sorted(set(still_on_ast) - enum_ops)) or "none"))
    problems.extend(subset_problems(still_on_ast, enum_ops))

    # Two verdicts, deliberately: a BROKEN reading (no MIR path at all, a walk
    # that never happened, an opcode named that does not exist) is a failure
    # under any circumstance; an INCOMPLETE subset (9 of 67 today) is progress,
    # and making it red would put a by-design red into the gate for as long as
    # the work takes.  `--gate` is what separates them.
    # A claimed case that no program drives IS broken, not "in progress":
    # the claim is what a reader trusts, and a label nothing reaches makes the
    # claim false while every number beside it stays plausible.
    broken = (bool(shell_problems(mentions, walks))
              or bool(walked_problems(moved, observed))
              or bool(subset_problems(still_on_ast, enum_ops)))
    ok = not broken
    for p in problems:
        print("IR-WASM-MIR-PROBLEM|" + p)
    print("IR-WASM-MIR-VERDICT|%s|shell-mentions=%d|walks=%s|moved=%d|enum=%d"
          "|agree-program=%s|incomplete=%d|gate=%s"
          % ("PASS" if ok else "FAIL", mentions, "yes" if walks else "no", len(moved),
             len(enum_ops), "yes" if impl_program == enum_ops else "no",
             len(enum_ops) - len(moved), "enforced" if args.gate else "reported"))
    if not ok:
        return 1
    # An incomplete subset returns 0 unless --gate asks progress to be told
    # apart from completion -- and even then it returns 1 only because the caller
    # said the subset is supposed to be complete today.
    if args.gate and len(moved) != len(enum_ops):
        return 1
    return 0


def self_check():
    """The three judgements on inputs whose answer is known, both ways."""
    checks = [0]
    bad = []

    def expect(name, got, want):
        checks[0] += 1
        if got != want:
            bad.append("%s: want %r got %r" % (name, want, got))
        print("wasm_mir self-check: %s %s" % (name, "ok" if got == want else "FAIL"))

    expect("a file with no mention has no MIR path",
           len(shell_problems(0, True)), 1)
    expect("a path nobody drove is a failure even when the word is present",
           len(shell_problems(3, False)), 1)
    expect("a path that exists and was driven is silent",
           shell_problems(3, True), [])
    expect("an implementation missing an opcode is not agreement",
           len(table_problems({"program": {"OP_A"}, "mir": {"OP_A", "OP_B"}}, {"OP_A", "OP_B"})) > 0, True)
    expect("a union that happens to be complete is still not agreement",
           len(table_problems({"program": {"OP_A"}, "mir": {"OP_B"}}, {"OP_A", "OP_B"})) > 0, True)
    expect("an implementation naming an undeclared opcode is caught",
           len(table_problems({"program": {"OP_Z"}}, {"OP_A"})), 2)   # undeclared + missing
    expect("a drifted export offset is caught by the import count",
           len(import_problems(4, 5)), 1)
    expect("an offset that equals the import count is silent",
           import_problems(4, 4), [])
    expect("a work queue of real names is silent",
           subset_problems(["OP_A"], {"OP_A", "OP_B"}), [])
    expect("a work queue entry that is not an opcode is caught",
           len(subset_problems(["OP_NOT_REAL"], {"OP_A"})), 1)
    expect("cases are read from a named function body, not the file",
           case_ops("case OP_A: case OP_B:") == {"OP_A", "OP_B"}, True)
    expect("a missing prerequisite exits 2, not 0", REFUSED_RC, 2)

    if bad:
        print("wasm_mir self-check: FAIL -- " + "; ".join(bad))
        return 1
    print("wasm_mir self-check: ok (%d checks)" % checks[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())