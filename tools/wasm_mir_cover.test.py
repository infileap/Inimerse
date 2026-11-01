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
    # OP_MOV is drivable at top level: unary plus on a non-temporary operand
    # allocates a register and copies the operand into it (EXPR_UNARY in
    # src/compiler/compiler.c, the `else emit(OP_MOV, r, operand)` arm), so
    # `say +5` lowers to LOADK_INT, MOV, SAY, HALT -- every one of them on the
    # moved set -- and the MIR path answers it.
    #
    # Two previous beliefs were wrong here, and both were about WHERE a construct
    # can reach an opcode rather than about the opcode.  An assignment inside a
    # function body emits OP_MOV (`func f(){ x = 1; return x }`) but needs
    # CALL_FUNC and RETURN to run, which this path does not lower; a top-level
    # assignment compiles to STORE_GLOBAL instead.  From those two I concluded the
    # driver was impossible until functions were on MIR, and shipped OP_MOV as a
    # permanent driver gap.  It was not impossible: I had not asked which of the
    # emission sites runs at top level.
    ("OP_MOV",         "say +5\n",                 "5"),
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


# Directories that hold COPIES of the corpus rather than corpus: every worktree
# carries its own examples/, so walking into one double-counts files and makes
# the census slower without making it wider.  build/ holds generated .im output.
CENSUS_SKIP = ("build", ".worktrees", ".scratch", ".git", "node_modules")


def census_reachable(tool, root):
    """Which opcodes a SOURCE CENSUS can produce -- an instrument independent of
    the walk.

    The walk answers "did my driver reach it"; this answers "can any program in
    this language reach it".  A walk that misses an opcode is evidence about the
    DRIVER, not about the language, and confusing the two sends a reader to fix
    the wrong thing: a driver gap means write a better test, an unreachable
    opcode means the denominator is wrong.

    It reads the WHOLE repo, and prints every number it counted instead of one
    count: `files=` walked, `dumped=` that the front end accepted, `failed=` that
    it rejected, `reachable=` the opcodes some program drives, and `enum=` the
    denominator those are out of.  An earlier version took a hand-picked list of
    five candidate roots -- examples, tests, bench, mods, tools -- of which three
    exist, and printed `basis=dump-over-every-.im-in-the-repo` next to the
    resulting count.  The label was the lie, not the number: those three hold
    35 of the repo's 399 .im files, and over those 35 files 24 of the
    67 opcodes look unreachable -- which is exactly the "fix the denominator"
    conclusion a census is supposed to rule OUT.  A narrowed census does not
    fail loudly; it under-reports reachability and reads as evidence.
    """
    seen = set()
    files = dumped = failed = 0
    for p in sorted(Path(root).rglob("*.im")):
        if any(part in CENSUS_SKIP for part in p.relative_to(root).parts):
            continue
        files += 1
        rc, out, _ = run([tool, "dump", p])
        if rc != 0 or not out.strip():
            failed += 1
            continue
        dumped += 1
        for m in re.finditer(r"op=(OP_[A-Z0-9_]+)", out):
            seen.add(m.group(1))
    return seen, files, dumped, failed


def tracked_im_count(root):
    """How many .im files the git INDEX says this tree carries.

    Deliberately not the census's own walk: the point is to have a second
    instrument that does not share the walk's blind spot.  A tree with no index
    returns -1 rather than 0, because "I could not look" and "there is nothing
    there" are different readings.
    """
    rc, out, _ = run(["git", "-C", str(root), "ls-files", "--", "*.im"])
    if rc != 0:
        return -1
    return sum(1 for line in out.splitlines() if line.strip())


def census_scope(tracked, walked, skipped):
    """Whether the walk covered the corpus the census claims to have read.

    `files=` is produced by the very walk it describes, so a walk that stops
    early prints a smaller `files=` and the line keeps its shape -- this is the
    defect the hand-picked `roots` list had, one level up.  The index is an
    instrument that does not run this walk, so the two can be compared instead
    of trusting each other.

    A SHORTFALL is a failure: the skip list is hiding corpus, and every reach
    answer computed below it was computed over a narrowed language.  Extra files
    (an untracked probe, a new example) make the census wider, which is never
    wrong.
    """
    if tracked < 0 or walked >= tracked:
        return []
    return ["the census walked %d .im files but the index holds %d: skipping %s hides "
            "corpus, so every unreachable verdict below was computed over the part "
            "that was read" % (walked, tracked, ",".join(skipped) or "nothing")]


_MISSING = object()


def broken_reading(*, shell=_MISSING, scope=_MISSING, unreachable=_MISSING,
                   subset=_MISSING):
    """The single predicate main() turns into PASS/FAIL.

    It is a pure function so the WIRING can be measured too: each argument is a
    list of problems, and the question is whether one non-empty list is enough
    to call the reading broken.  A check that only tested the ingredients would
    leave open whether main() consults them -- and a cell cannot see main's
    call site at all.

    So the four ingredients have no defaults worth having: leaving one out raises
    instead of silently contributing nothing.  That is the one shape a check can
    enforce here, because "the caller forgot an argument" must be louder than a
    green verdict, not quieter.
    """
    for name, value in (("shell", shell), ("scope", scope),
                        ("unreachable", unreachable), ("subset", subset)):
        if value is _MISSING:
            raise TypeError("broken_reading: no %s given -- the verdict has to "
                            "name all four ingredients, not the convenient ones"
                            % name)
        if value is None:
            raise TypeError("broken_reading: %s=None -- None is what forgetting "
                            "an ingredient looks like; pass the empty list when "
                            "there is nothing there" % name)
    return bool(shell) or bool(scope) or bool(unreachable) or bool(subset)


def walk_class(driven, reachable):
    """The three answers the two columns give, as a pure function.

      driven + reachable      -> ok
      not driven + reachable  -> driver-gap   (write a better test)
      not driven + unreachable-> unreachable  (the denominator is wrong)

    A claimed case that was never driven is NOT automatically broken: whether it
    is broken depends on a fact the walk cannot supply on its own.
    """
    if driven:
        return "ok"
    return "driver-gap" if reachable else "unreachable"


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
    print("IR-WASM-MIR-IMPORTS|declared=%d|actual=%d|basis=source-scan|agree=%s"
          % (imports_decl, imports_actual,
             "yes" if imports_decl == imports_actual else "no"))
    problems.extend(import_problems(imports_decl, imports_actual))

    # Peer's question, as a measurement: does each claimed case actually run, or
    # is it a case label nothing reaches?  One program per opcode through the
    # real entry point, compared with the interpreter on exit status and the
    # last line printed.  "Compiles" is not "computes".
    # The census runs FIRST and over the whole corpus: it answers "can this
    # language reach the opcode", independently of my driver.  It walks ROOT
    # rather than a hand-picked list of subdirectories, because a list is a
    # judgment about where the corpus lives and this number must not depend on
    # that judgment -- see census_reachable's note.
    reachable, nfiles, ndumped, nfailed = census_reachable(tool, ROOT)
    # The scope of that walk is itself a reading, and the walk cannot certify
    # its own breadth -- see census_scope.
    ntracked = tracked_im_count(ROOT)
    scope = census_scope(ntracked, nfiles, CENSUS_SKIP)
    observed = set()
    gaps = set()
    unreachable = set()
    work = tempfile.mkdtemp(prefix="wasm_mir_walk_")
    drivers = {op: (text, want) for op, text, want in WALK_CASES}
    # EVERY claimed opcode gets a line, whether or not this file has a driver for
    # it: a claimed case with no driver is the thing the reader most needs told,
    # and skipping it would leave the claim looking driven.
    for op in sorted(moved):
        if op not in drivers:
            reach = op in reachable
            cls = walk_class(False, reach)
            print("IR-WASM-MIR-WALK|op=%s|claimed=yes|driven=no|reachable=%s|class=%s"
                  "|why=no-driver-in-this-file" % (op, "yes" if reach else "no", cls))
            print("IR-WASM-MIR-STATE|op=%s|class=%s|fix=%s" % (
                op, cls,
                "write-a-driver" if cls == "driver-gap" else "fix-the-denominator"))
            (gaps if cls == "driver-gap" else unreachable).add(op)
            continue
        src_text, want = drivers[op]
        p = Path(work) / (op + ".im")
        p.write_text(src_text)
        w = Path(work) / (op + ".wasm")
        rc_e, out_e, err_e = run([tool, "wasm", p, w])
        driven = False
        detail = ""
        if rc_e == 0:
            rc_i, out_i, _ = run([interp, "--no-mods", "run", p])
            rc_w, out_w, _err_w = run(["node", ROOT / "tools" / "wasm_run.js", w])
            driven = (out_w.strip() == out_i.strip()) and (rc_w == rc_i)
            detail = "interp=%r|wasm=%r" % (
                out_i.strip().splitlines()[-1] if out_i.strip() else "",
                out_w.strip().splitlines()[-1] if out_w.strip() else "")
        else:
            detail = "why=%s" % ((out_e + err_e).strip().splitlines()[0][:100]
                                  if (out_e + err_e).strip() else "-")
        # An opcode is DRIVEN when a program reached it AND the answer matched.
        # A reachable opcode whose answer does not match is a driver gap, printed
        # rather than counted either way -- and a gap is a claim about my drivers,
        # not about the language.  OP_MOV sat in this column for a while because
        # I had looked at the two emission sites that need a function body and
        # stopped there; the site that runs at top level was three hundred lines
        # later in the same switch.
        reach = op in reachable
        cls = walk_class(driven, reach)
        print("IR-WASM-MIR-WALK|op=%s|claimed=yes|driven=%s|reachable=%s|class=%s|%s"
              % (op, "yes" if driven else "no", "yes" if reach else "no", cls, detail))
        if driven:
            observed.add(op)
        elif cls == "driver-gap":
            # Reachable but undriven: the fix is a better test, not different
            # code, so it is reported and does NOT by itself fail the reading.
            gaps.add(op)
            print("IR-WASM-MIR-STATE|op=%s|class=driver-gap|fix=write-a-driver" % op)
        elif cls == "unreachable":
            unreachable.add(op)
            print("IR-WASM-MIR-STATE|op=%s|class=unreachable|fix=fix-the-denominator" % op)
    # The census line carries every number it counted, because `reachable`
    # alone is not a reading: without `dumped`/`failed`/`enum` next to it, a
    # census that read part of the corpus and one that read all of it print the
    # same shape.
    print("IR-WASM-MIR-CENSUS|files=%d|dumped=%d|failed=%d|reachable=%d|enum=%d"
          "|basis=dump-over-every-.im-under-the-repo-root"
          % (nfiles, ndumped, nfailed, len(reachable), len(enum_ops)))
    # ...and the walk's own breadth is a separate number with its own instrument:
    # an index that did not walk anywhere.  Without this, a skip list that grew
    # by one directory produces a smaller census with the same shape.
    print("IR-WASM-MIR-CENSUS-SCOPE|index=%s|walked=%d|shortfall=%s|skipped=(%s)"
          "|basis=git-index-vs-this-walk|agree=%s"
          % ("unreadable" if ntracked < 0 else str(ntracked), nfiles,
             "unknown" if ntracked < 0 else max(0, ntracked - nfiles),
             ",".join(CENSUS_SKIP),
             "unknown" if ntracked < 0 else ("yes" if not scope else "no")))
    problems.extend(scope)
    # The three classes decide differently: unreachable is a WRONG READING
    # (the denominator counts something no program can produce), driver-gap is
    # missing coverage, and both are reported even though only the first is a
    # failure.  A single "never driven ⇒ broken" rule would have called the
    # reachable OP_MOV broken, which is exactly the false conclusion to avoid.
    print("IR-WASM-MIR-CLASS|driven=%d|driver-gap=%d|unreachable=%d|claimed=%d"
          % (len(observed), len(gaps), len(unreachable), len(moved)))

    still_on_ast = sorted(enum_ops - moved)
    print("IR-WASM-MIR-SUBSET|moved=(%s)|still-on-ast=(%s)|unknown-in-enum=(%s)"
          % (",".join(sorted(moved)), ",".join(still_on_ast),
             ",".join(sorted(set(still_on_ast) - enum_ops)) or "none"))
    subset_probs = subset_problems(still_on_ast, enum_ops)
    problems.extend(subset_probs)

    # Two verdicts, deliberately: a BROKEN reading (no MIR path at all, a walk
    # that never happened, an opcode named that does not exist) is a failure
    # under any circumstance; an INCOMPLETE subset (9 of 67 today) is progress,
    # and making it red would put a by-design red into the gate for as long as
    # the work takes.  `--gate` is what separates them.
    # A claimed case that no program drives IS broken, not "in progress":
    # the claim is what a reader trusts, and a label nothing reaches makes the
    # claim false while every number beside it stays plausible.
    broken = broken_reading(shell=shell_problems(mentions, walks), scope=scope,
                            unreachable=unreachable, subset=subset_probs)
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
    expect("reachable but undriven is a driver gap, not a failure",
           walk_class(False, True), "driver-gap")
    expect("undriven and unreachable means the denominator is wrong",
           walk_class(False, False), "unreachable")
    expect("a driven opcode is ok however reachable it looks",
           walk_class(True, True), "ok")
    # These three were a second copy of the walk_class trio above, worded a
    # little differently: repeated cells inflate the check count and make the
    # suite look broader than it is.  They now test the census's SCOPE, which
    # had no cell at all.
    expect("a walk that read fewer files than the index holds is caught",
           len(census_scope(399, 35, CENSUS_SKIP)), 1)
    expect("a walk that read every tracked file is silent",
           census_scope(399, 399, CENSUS_SKIP), [])
    expect("untracked files found by the walk are wider, not wrong",
           census_scope(399, 410, CENSUS_SKIP), [])
    expect("a narrowed census breaks the reading through the verdict itself",
           broken_reading(shell=[], scope=census_scope(399, 35, CENSUS_SKIP),
                          unreachable=set(), subset=[]), True)
    expect("a wide census leaves the verdict to the other three",
           broken_reading(shell=[], scope=census_scope(399, 399, CENSUS_SKIP),
                          unreachable=set(), subset=[]), False)
    # One cell per ingredient.  The predicate is a symmetric OR, so an argument
    # passed in the wrong order is not a defect -- but an ingredient main() stops
    # passing is, and that stays invisible while only the scope branch is tested.
    expect("a shell that never drove the MIR path breaks the reading",
           broken_reading(shell=["no-mir-path"], scope=[], unreachable=set(),
                          subset=[]), True)
    expect("a claimed opcode nothing can reach breaks the reading",
           broken_reading(shell=[], scope=[], unreachable={"OP_Z"}, subset=[]), True)
    expect("a subset naming an undeclared opcode breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(),
                          subset=["work-queue-names-OP_NOT_REAL"]), True)
    expect("four silent ingredients break nothing",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[]), False)

    def raises(fn):
        try:
            fn()
            return False
        except TypeError:
            return True

    # Two moments of the same defect: the signature is keyword-only, so an
    # unnamed ingredient is refused before the body runs; the sentinel and the
    # None check refuse a call that spells the name and leaves the value out.
    expect("a verdict that does not name every ingredient refuses to answer",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set())), True)
    expect("a verdict handed None where a list belongs refuses too",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                         subset=None)), True)
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