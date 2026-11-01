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
  IR-WASM-MIR-CLAIMS|claimed=N|driven=M|undriven=(...)|basis=...|agree=yes|no
            the case string is a claim; the walk is the measurement, and the
            two numbers above are the reconciliation -- the ingredient that
            made a deleted driver turn the verdict red instead of moving a
            label nobody checked.
  IR-WASM-MIR-PROBLEM-SCOPE|printed=N|carried=M|orphans=K|basis=...|rule=...
  IR-WASM-MIR-VERDICT|PASS|FAIL|...|moved=(claim)|driven=(measured)|...

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
    # Both beliefs I had written down here were about WHERE a construct can reach
    # an opcode rather than about the opcode: that OP_MOV comes from an assignment
    # inside a function body (`func f(){ x = 1; return x }`), which needs
    # CALL_FUNC and RETURN to run -- neither lowered here; and that a top-level
    # assignment compiles to STORE_GLOBAL.  From those I concluded the driver was
    # impossible until functions were on MIR, and shipped OP_MOV as a permanent
    # driver gap.  It was not impossible: I had not read the emission site that
    # runs at top level.
    ("OP_MOV",         "say +5\n",                 "5"),
    ("OP_ADD",         "say 1 + 2\n",               "3"),
    ("OP_SUB",         "say 10 - 4\n",              "6"),
    # The float form of the same opcode.  Without this case OP_ADD was "moved"
    # on the strength of its integer form alone: `say 1.5 + 2.5` emitted
    # i64.add over f64 slots and the engine rejected the module at instantiation
    # (i64.add[0] expected type i64, found local.get of type f64), so the moved
    # claim had a scope nobody had written down.  This case is that scope: one
    # driver per operand-kind form that can reach the opcode.
    ("OP_ADD",         "say 1.5 + 2.5\n",           "4"),
    # The third form of the same opcode: a mixed pair, which no single wasm
    # instruction can compute.  The row declares the refusal, so a lowering that
    # correctly refuses certifies the boundary; a lowering that silently emits
    # something turns stated=wrong and the reading says so.  Before this row the
    # two refusal messages existed and no program in the corpus ever reached
    # them, which is a declared boundary nobody had measured.
    ("OP_ADD",         "say 1.5 + true\n",        ("refused", "mixes a float operand")),
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

    Scoped on purpose: `case OP_*` labels also appear in this file's other
    functions -- the emitter carries them too -- so a whole-file scan would
    compare a set with itself and call it agreement.  Which body to read is a
    property of the caller's question, not of the file, so the scope belongs in
    this signature.  Naming the other functions would be a count wearing a
    name's clothes: a third one carrying a case label makes the list wrong, and
    the argument for scoping stays exactly the same.
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
    return problems


def table_gaps(impl_cases, enum_ops):
    """What an implementation does not implement yet -- progress, not a defect.

    This used to be the second half of table_problems, printed under the PROBLEM
    prefix and then exempted from the verdict by a name list in main().  A
    PROBLEM line that cannot fail the verdict is a permanent green channel:
    whoever edits main() adds one more name and no reading moves.  The fix is
    the prefix: an incomplete implementation prints as IR-WASM-MIR-NOT-YET and
    never joins the problem channel, so "PROBLEM printed" and "verdict FAIL"
    stay one fact instead of two conventions.

    Missing opcodes are still counted per implementation, never as a union:
    {A} and {B} union to {A,B}, which would make "each half done" and "both
    complete" the same number.
    """
    gaps = []
    for impl, ops in impl_cases.items():
        if not ops:
            continue
        missing = enum_ops - ops
        if missing:
            gaps.append("impl=%s has no case for %d of the %d declared opcodes%s"
                        % (impl, len(missing), len(enum_ops),
                           " (first: %s)" % ",".join(sorted(missing)[:4])))
    return gaps


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
                   subset=_MISSING, rows=_MISSING, claims=_MISSING,
                   orphans=_MISSING):
    """The single predicate main() turns into PASS/FAIL.

    It is a pure function so the WIRING can be measured too: each argument is a
    list of problems, and the question is whether one non-empty list is enough
    to call the reading broken.  A check that only tested the ingredients would
    leave open whether main() consults them -- and a cell cannot see main's
    call site at all.

    So the ingredients have no defaults worth having: leaving one out raises
    instead of silently contributing nothing.  That is the one shape a check can
    enforce here, because "the caller forgot an argument" must be louder than a
    green verdict, not quieter.

    The seventh is claims, and it is the one the WALK labels could plead: a
    claimed opcode with no driver printed stated=wrong or class=driver-gap on a
    line and still passed, because neither label entered problems.
    """
    for name, value in (("shell", shell), ("scope", scope),
                        ("unreachable", unreachable), ("subset", subset),
                        ("rows", rows), ("claims", claims),
                        ("orphans", orphans)):
        if value is _MISSING:
            raise TypeError("broken_reading: no %s given -- the verdict has to "
                            "name every ingredient it consults, not the "
                            "convenient ones" % name)
        if value is None:
            raise TypeError("broken_reading: %s=None -- None is what forgetting "
                            "an ingredient looks like; pass the empty list when "
                            "there is nothing there" % name)
    return (bool(shell) or bool(scope) or bool(unreachable) or bool(subset)
            or bool(rows) or bool(claims) or bool(orphans))


def orphan_problems(problems, carried):
    """Printed defect lines that no ingredient of the verdict carries.

    broken_reading is a symmetric OR over the lists main() hands it, so a
    problem that is printed but not one of those lists is a line the verdict
    cannot see: the reading says FAIL nowhere while the evidence sits in its
    own output.  Carried is the union of the ingredient lists.

    There is deliberately no allow-list here.  An allow-list is a channel that
    stays green forever: a line added to it prints and cannot fail, and no cell
    can see that the list grew.  A line that is not a defect is printed under a
    prefix that is not PROBLEM, which is a structural answer rather than a
    convention maintained by whoever edits main().
    """
    have = set(carried)
    return [p for p in problems if p not in have]


def row_stated(rc_e, reason, got, want):
    """What one driver row says, and whether the run reproduced it.

    Returns (ok, kind, problem).  kind is "refused" for a row that declares the
    lowering MUST reject the program and "value" for a row that declares an
    answer.  The distinction exists because a legal refusal was being recorded as
    a false statement: with only ok/wrong, rc_e != 0 meant wrong no matter why,
    so a row that exists to pin a declared boundary turned stated=wrong and the
    opcode it certifies fell back to driver-gap with the advice write-a-driver,
    i.e. go change a driver that is correct.

    A refusal row carries its reason as ("refused", substring), so the row states
    WHICH refusal it expects rather than just "some error".
    """
    refused_want = isinstance(want, tuple) and want and want[0] == "refused"
    if rc_e == 0:
        if refused_want:
            return (False, "refused",
                    "the row declares that the lowering must refuse this"
                    " program (%r) but it emitted a module" % (want[1],))
        if got != want.strip():
            return (False, "value",
                    "the row says the interpreter prints %r but it printed %r"
                    % (want, got))
        return (True, "value", None)
    if refused_want:
        if want[1] in reason:
            return (True, "refused", None)
        return (False, "refused",
                "the row declares the refusal %r and the lowering refused"
                " with %r instead" % (want[1], reason))
    return (False, "value",
            "the row states %r but the lowering refused the program: %r"
            % (want, reason))


def walk_class(driven, reachable):
    """The three answers the two columns give, as a pure function.

      driven + reachable      -> ok
      not driven + reachable  -> driver-gap   (write a better test)
      not driven + unreachable-> unreachable  (the denominator is wrong)

    The label is a suggested repair, not a verdict: brokenness is decided by
    main's claims ingredient, which compares a claim against a measurement, and
    this function cannot see either.
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


def walk_rows(rows):
    """Every row of the driver table, with its per-opcode index.

    A table keyed by the opcode cannot hold two forms of one opcode, and a dict
    keyed on it drops the second silently -- that is how adding the float form of
    OP_ADD stopped the integer driver from running without moving a single
    reading.  Rows are a list and this is the only place that numbers them, so a
    reading cannot disagree with the table about how many rows exist.
    """
    seen = {}
    numbered = []
    for op, src, want in rows:
        n = seen.get(op, 0) + 1
        seen[op] = n
        numbered.append((op, n, src, want))
    return numbered


def unclaimed_rows(rows, moved_ops):
    """Rows naming an opcode no claim covers: executed, but certifying nothing."""
    return [op for op, _src, _want in rows if op not in moved_ops]


def duplicate_rows(rows):
    """Rows that run the same program for the same opcode -- one is redundant."""
    seen = {}
    dup = []
    for op, src, _want in rows:
        key = (op, src)
        if seen.get(key):
            dup.append(op)
        seen[key] = True
    return dup


def walk_scope(rows, moved_ops):
    """Problems with the driver table itself, rather than with what it measured."""
    problems = []
    bad = unclaimed_rows(rows, moved_ops)
    if bad:
        problems.append(
            "wasm mir: %d row(s) of WALK_CASES name an opcode that is not in the moved "
            "subset (%s), so they were executed but certify nothing"
            % (len(bad), ",".join(sorted(bad))))
    dup = duplicate_rows(rows)
    if dup:
        problems.append(
            "wasm mir: %d row(s) of WALK_CASES repeat a program already run for the "
            "same opcode (%s), so the table claims more rows than it measures"
            % (len(dup), ",".join(sorted(dup))))
    return problems

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
    # An incomplete implementation is progress, so it never enters the problem
    # channel: it prints as NOT-YET, and the invariant PROBLEM-printed <=> verdict-FAIL
    # stays structural instead of depending on a name list.
    for gap in table_gaps({"mir": impl_mir}, enum_ops):
        print("IR-WASM-MIR-NOT-YET|" + gap)

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
    # Rows, not a dict keyed on the opcode.  A dict silently drops a second form
    # of the same opcode -- which is what happened when the float form of OP_ADD
    # was added: the integer driver stopped running and no reading moved, so the
    # blindness only switched operand kind.  Every row is executed here, and the
    # rows-for-unclaimed-opcodes count is separate, because a row nobody walks is
    # a claim nobody checks.
    rows = walk_rows(WALK_CASES)
    executed = 0
    ok_by_op = {}
    rows_by_op = {}
    stated_by_op = {}
    # The driver table's own demands, kept separate from the progress notes: a
    # row whose answer the interpreter does not reproduce is a BROKEN reading,
    # while "58 opcodes are not moved yet" is only incompleteness.
    row_problems = []
    for op, n, src_text, want in rows:
        executed += 1
        rows_by_op[op] = n
        p = Path(work) / ("%s_%d.im" % (op, n))
        p.write_text(src_text)
        w = Path(work) / ("%s_%d.wasm" % (op, n))
        rc_e, out_e, err_e = run([tool, "wasm", p, w])
        reason_lines = (out_e + err_e).strip().splitlines()
        reason = reason_lines[0] if reason_lines else "-"
        driven = False
        if rc_e == 0:
            rc_i, out_i, _ = run([interp, "--no-mods", "run", p])
            rc_w, out_w, _err_w = run(["node", ROOT / "tools" / "wasm_run.js", w])
            got = out_i.strip().splitlines()[-1] if out_i.strip() else ""
            got_w = out_w.strip().splitlines()[-1] if out_w.strip() else ""
            route_ok = (got_w == got) and (rc_w == rc_i)
            detail = "interp=%r|wasm=%r|want=%r" % (got, got_w, want)
        else:
            got = ""
            route_ok = False
            detail = "why=%s" % reason[:100]
        ok, kind, why = row_stated(rc_e, reason, got, want)
        # A declared refusal is agreement when it actually refuses: the lowering
        # delivered what the row says it must deliver.
        driven = route_ok if kind == "value" else ok
        if why:
            # The opcode and the row travel with the message, because two rows
            # of one opcode are exactly the case this table now supports.
            problems.append("row %d for %s: %s" % (n, op, why))
            row_problems.append(problems[-1])
        ok_by_op.setdefault(op, []).append(driven)
        stated_by_op.setdefault(op, []).append((ok, kind))
        print("IR-WASM-MIR-WALK-ROW|op=%s|row=%d|src=%r|%s"
              % (op, n, src_text, detail))

    # EVERY claimed opcode gets a line, whether or not this file has a driver for
    # it: a claimed case with no driver is the thing the reader most needs told,
    # and skipping it would leave the claim looking driven.
    for op in sorted(moved):
        if op not in rows_by_op:
            reach = op in reachable
            cls = walk_class(False, reach)
            print("IR-WASM-MIR-WALK|op=%s|claimed=yes|driven=no|reachable=%s|class=%s"
                  "|why=no-driver-in-this-file" % (op, "yes" if reach else "no", cls))
            print("IR-WASM-MIR-STATE|op=%s|class=%s|fix=%s" % (
                op, cls,
                "write-a-driver" if cls == "driver-gap" else "fix-the-denominator"))
            (gaps if cls == "driver-gap" else unreachable).add(op)
            continue
        # An opcode is DRIVEN only when EVERY row for it agrees with the
        # interpreter: the claim is about the opcode, and the opcode has as many
        # forms as the table gave it.
        driven = all(ok_by_op[op])
        rows_ok = sum(1 for ok in ok_by_op[op] if ok)
        reach = op in reachable
        cls = walk_class(driven, reach)
        # driven asks whether the two routes agree; stated asks whether the
        # table third column survives the run. They are different questions,
        # and a row that states the wrong answer must not hide behind driven=yes.
        kinds = stated_by_op.get(op, [])
        if not all(ok for ok, _kind in kinds):
            stated = "wrong"
        elif all(kind == "refused" for _ok, kind in kinds):
            stated = "refused"
        else:
            stated = "ok"
        print("IR-WASM-MIR-WALK|op=%s|claimed=yes|driven=%s|reachable=%s|class=%s"
              "|rows=%d|rows-agree=%d|stated=%s"
              % (op, "yes" if driven else "no", "yes" if reach else "no", cls,
                 rows_by_op[op], rows_ok, stated))
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
    # The table is a claim about what WILL be run, so the count that ran is a
    # reading of its own: rows == executed would be true by construction, and
    # what a reader needs instead is how many rows name an opcode nobody claims.
    walk_probs = walk_scope(WALK_CASES, moved)
    print("IR-WASM-MIR-WALK-SCOPE|rows=%d|executed=%d|ops=%d|unclaimed=%d"
          "|basis=every-row-of-WALK_CASES-ran|a-row-for-an-unclaimed-opcode-checks-nothing"
          % (len(rows), executed, len(rows_by_op),
             len(unclaimed_rows(WALK_CASES, moved))))
    problems.extend(walk_probs)
    row_problems.extend(walk_probs)
    # walked_problems was defined in this file and called by nobody: its own
    # docstring said the case string is a CLAIM and the walk is the
    # MEASUREMENT, and with no call site that sentence left no trace in the
    # verdict.  Measured, deleting one WALK_CASES row moved one opcode to
    # class=driver-gap and the run still said PASS|moved=9 -- the tool vouching
    # for a claim it had never tested.
    claim_probs = walked_problems(moved, observed)
    print("IR-WASM-MIR-CLAIMS|claimed=%d|driven=%d|undriven=(%s)|basis="
          "the-case-string-is-a-claim-the-walk-is-the-measurement|agree=%s"
          % (len(moved), len(observed),
             ",".join(sorted(moved - observed)) or "none",
             "yes" if not claim_probs else "no"))
    problems.extend(claim_probs)
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
    # The classes are labels on one measurement, not votes on the verdict.
    # unreachable says the denominator counts something no program can produce;
    # driver-gap says the opcode is reachable and no row for it drove anything.
    # Neither label decides PASS: the claims ingredient does, because it is the
    # one place that knows a claimed case from a measured one.  This comment
    # used to argue the opposite -- that a never-driven case is not
    # automatically broken, and that ruling so would falsely call the reachable
    # OP_MOV broken.  OP_MOV ended up proving the reverse twice: once as a
    # driver-gap that was only a missing driver (fixed by writing one), and once
    # as the case where deleting a driver left PASS|moved=9 standing.
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
    # claim false while every number beside it stays plausible.  That is why
    # the claims ingredient exists, and why the comment beside CLASS above had
    # to go: it said the opposite, and a reader who believed it would have
    # relaxed the one check that catches the state it described.
    # Every printed PROBLEM has to ride an ingredient of the verdict, or the
    # reading fails on a line it cannot see.  This is the failure the docstring
    # above predicted and no cell could catch: import_problems was extended into
    # problems and left out of the OR, so a module whose every exported index was
    # off by one printed a PROBLEM and still said PASS with rc=0.
    carried = (shell_problems(mentions, walks) + list(scope) + subset_probs
               + row_problems + claim_probs)
    orphans = orphan_problems(problems, carried)
    print("IR-WASM-MIR-PROBLEM-SCOPE|printed=%d|carried=%d|orphans=%d"
          "|basis=every-printed-PROBLEM-must-ride-an-ingredient-of-the-verdict"
          "|rule=a-PROBLEM-line-exists-exactly-when-the-verdict-says-FAIL"
          % (len(problems), len(carried), len(orphans)))
    for p in orphans:
        print("IR-WASM-MIR-ORPHAN|" + p)
    broken = broken_reading(shell=shell_problems(mentions, walks), scope=scope,
                            unreachable=unreachable, subset=subset_probs,
                            rows=row_problems, claims=claim_probs,
                            orphans=orphans)
    ok = not broken
    for p in problems:
        print("IR-WASM-MIR-PROBLEM|" + p)
    # moved is the CLAIM (the generator's published case string) and driven is
    # what ran; printing only the first let a FAILing run still headline 9 while
    # 8 had answered, which is the same shape as the defect this line closes.
    print("IR-WASM-MIR-VERDICT|%s|shell-mentions=%d|walks=%s|moved=%d|driven=%d"
          "|enum=%d|agree-program=%s|incomplete=%d|gate=%s"
          % ("PASS" if ok else "FAIL", mentions, "yes" if walks else "no", len(moved),
             len(observed), len(enum_ops),
             "yes" if impl_program == enum_ops else "no",
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
    expect("an implementation missing an opcode is counted against it",
           len(table_gaps({"program": {"OP_A"}, "mir": {"OP_A", "OP_B"}}, {"OP_A", "OP_B"})) > 0, True)
    expect("a union that happens to be complete is still not agreement",
           len(table_gaps({"program": {"OP_A"}, "mir": {"OP_B"}}, {"OP_A", "OP_B"})) > 0, True)
    # ...and incompleteness cannot reach the problem channel: it is printed under
    # its own prefix, so the invariant PROBLEM <=> FAIL is structural, not a
    # convention kept by whoever edits main().
    expect("an incomplete implementation is not a table defect",
           table_problems({"mir": {"OP_A"}}, {"OP_A", "OP_B"}), [])
    expect("incomplete implementations are still counted, per impl",
           len(table_gaps({"mir": {"OP_A"}}, {"OP_A", "OP_B"})), 1)
    expect("an implementation naming an undeclared opcode is caught",
           len(table_problems({"program": {"OP_Z"}}, {"OP_A"})), 1)   # the missing one is a gap
    expect("...and its missing opcode is counted as progress, not as a defect",
           len(table_gaps({"program": {"OP_Z"}}, {"OP_A"})), 1)
    expect("reachable but undriven is a driver gap, not a failure",
           walk_class(False, True), "driver-gap")
    expect("undriven and unreachable means the denominator is wrong",
           walk_class(False, False), "unreachable")
    expect("a driven opcode is ok however reachable it looks",
           walk_class(True, True), "ok")
    # The driver table's own shape.  The first cell is the control against the
    # defect this file just fixed: a dict keyed on the opcode keeps one row of a
    # two-row opcode, and the dictionary comprehension that used to build it did
    # exactly that -- silently, with no reading moving.
    two_rows = [("OP_ADD", "say 1 + 2\n", "3"), ("OP_ADD", "say 1.5 + 2.5\n", "4")]
    expect("rows keep both forms of one opcode",
           len(walk_rows(two_rows)), 2)
    expect("a dict keyed on the opcode keeps one of them -- the trap being avoided",
           len({op: (s, w) for op, s, w in two_rows}), 1)
    expect("a row for an opcode nobody claims is caught by name",
           len(unclaimed_rows([("OP_NOT_CLAIMED", "say 1\n", "1")], {"OP_ADD"})), 1)
    expect("rows for claimed opcodes are not called unclaimed",
           unclaimed_rows(two_rows, {"OP_ADD"}), [])
    expect("a repeated program for one opcode is caught",
           len(duplicate_rows(two_rows + [two_rows[0]])), 1)
    expect("distinct programs for one opcode are not called duplicates",
           duplicate_rows(two_rows), [])
    expect("a table of claimed, distinct rows is silent",
           walk_scope(two_rows, {"OP_ADD"}), [])
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
                          unreachable=set(), subset=[], rows=[], claims=[], orphans=[]), True)
    expect("a wide census leaves the verdict to the other four",
           broken_reading(shell=[], scope=census_scope(399, 399, CENSUS_SKIP),
                          unreachable=set(), subset=[], rows=[], claims=[], orphans=[]), False)
    # One cell per ingredient.  The predicate is a symmetric OR, so an argument
    # passed in the wrong order is not a defect -- but an ingredient main() stops
    # passing is, and that stays invisible while only the scope branch is tested.
    expect("a shell that never drove the MIR path breaks the reading",
           broken_reading(shell=["no-mir-path"], scope=[], unreachable=set(),
                          subset=[], rows=[], claims=[], orphans=[]), True)
    expect("a claimed opcode nothing can reach breaks the reading",
           broken_reading(shell=[], scope=[], unreachable={"OP_Z"}, subset=[],
                          rows=[], claims=[], orphans=[]), True)
    expect("a subset naming an undeclared opcode breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(),
                          subset=["work-queue-names-OP_NOT_REAL"], rows=[],
                          claims=[], orphans=[]), True)
    # The ingredient the driver table added: a row states an answer, and the
    # reading is broken when the program does not reproduce it.  Until this
    # ingredient existed the third column could say "999" while every number
    # stayed put and the verdict stayed green.
    expect("a row whose stated answer is not reproduced breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=["the row for OP_ADD says the interpreter prints '3' "
                                "but it printed '999'"], claims=[], orphans=[]), True)
    # The ingredient whose whole job is to be the last one: a printed defect that
    # no ingredient carries.  import_problems was exactly that -- it printed
    # "MIR_IMPORTS is 5 but the module imports 4" while the verdict said PASS.
    expect("a defect line no ingredient carries breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=[], claims=[], orphans=["MIR_IMPORTS is 5 but the module imports 4"]),
           True)
    expect("an orphan is one that no carried list holds",
           orphan_problems(["d1", "d2"], ["d1"]), ["d2"])
    expect("a defect already carried is not an orphan",
           orphan_problems(["d1"], ["d1"]), [])
    expect("a line that nobody carries is an orphan even when it was printed as progress",
           orphan_problems(["note"], []), ["note"])
    expect("six silent ingredients break nothing",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=[], claims=[], orphans=[]), False)
    # A row declares one of two things: an answer, or a refusal.  Both are
    # statements, and before this helper existed only the first could be true --
    # a legal refusal was recorded as a false statement and the opcode it certifies
    # fell back to driver-gap with the advice to write a driver that already worked.
    expect("a value row the interpreter reproduces is ok",
           row_stated(0, "-", "4", "4")[:2], (True, "value"))
    expect("a value row that prints something else is wrong",
           row_stated(0, "-", "5", "4")[0], False)
    expect("a declared refusal that refuses as declared is ok",
           row_stated(1, "mixes a float operand", "", ("refused", "mixes a float operand"))[:2],
           (True, "refused"))
    expect("a refusal row the lowering emitted instead is wrong",
           row_stated(0, "-", "4", ("refused", "mixes a float operand"))[0], False)
    expect("a refusal for a different reason is wrong",
           row_stated(1, "some other reason", "", ("refused", "mixes a float operand"))[0],
           False)
    expect("a value row the lowering refused is a statement not delivered",
           row_stated(1, "no such opcode", "", "4")[0], False)
    # The closure that orphans cannot provide on its own: every not-ok row has to
    # carry a problem, because the WALK line label (stated=wrong / driver-gap) is
    # printed, not scanned.  A not-ok row without a message would be a defect
    # label that no reading can fail on -- the same hole one level down.
    expect("every not-ok row carries a problem, so orphans can see it",
           all(len(case) == 3 and case[2] is not None for case in (
               row_stated(0, "-", "5", "4"),
               row_stated(0, "-", "4", ("refused", "mixes a float operand")),
               row_stated(1, "some other reason", "", ("refused", "mixes a float operand")),
               row_stated(1, "no such opcode", "", "4"))), True)
    # walked_problems was dead code in this file until the claims ingredient fed
    # it: the reconciliations below are its three shapes, so a future silent
    # claim cannot hide behind a green verdict.
    expect("a claimed case that never ran is a problem",
           walked_problems({"OP_ADD", "OP_MUL"}, {"OP_ADD"}),
           ["1 claimed case(s) were never driven to an answer: OP_MUL"])
    expect("a driven opcode missing from the claim is a problem too",
           walked_problems({"OP_ADD"}, {"OP_ADD", "OP_NEQ"}),
           ["1 opcode(s) ran but are not in the claimed set: OP_NEQ"])
    expect("a claim and its measurement agree in silence",
           walked_problems({"OP_ADD"}, {"OP_ADD"}), [])
    expect("an undriven claimed opcode breaks the verdict",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=[], claims=walked_problems({"OP_ADD", "OP_MUL"},
                                                       {"OP_ADD"}), orphans=[]),
           True)

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
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(), claims=[])), True)
    expect("a verdict handed None where a list belongs refuses too",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                         subset=None, claims=[], orphans=[])), True)
    expect("a verdict that names every ingredient but the driver table refuses",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                         subset=[], claims=[])), True)
    expect("claims is an ingredient the verdict cannot be silent about",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                        subset=[], rows=[], claims=None,
                                        orphans=[])), True)
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