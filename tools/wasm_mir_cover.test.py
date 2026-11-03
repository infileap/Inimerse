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
  IR-WASM-MIR-CENSUS|files=N|dumped=N|failed=N|reachable=N|enum=N|basis=...
  IR-WASM-MIR-CENSUS-FAIL|failed=N|classified=N|undeclared=N|declared=(...)|
            by-class=(...)|basis=...|agree=yes|no
            `failed` on the line above is a count with no meaning of its own: a
            fixture the front end is meant to refuse and a valid program that
            stopped parsing produce the same number.  So every rejection is
            matched against a declared reason class, and the ones matching
            nothing are printed as PROBLEM and carried by the verdict.  Without
            that, a program that fails is worth one point of `reachable` and no
            verdict moves.  The classes are identities rather than a pinned
            total, because a pinned total reddens when a fixture is added and
            stays green when a program stops parsing.
  IR-WASM-MIR-KIND-CONFLICT|rows=N|writes=W|conflicts=C|type-checks=T|
            type-conflicts=TC|clash-rows=R|kind-agree=...|type-agree=...|basis=...
            |agree=yes|no|n/a
            two counters from the emitter, printed on both its paths: writes is
            what the value-type check looked at, conflicts what it caught.  A
            check that stopped running prints writes=0, which is the same shape
            as a clean run unless the write count rides on the line.  R is
            checked against a pinned literal, not against the table it was
            counted from.  Each arm is scored on its own and the line keeps
            three words: yes means it ran and agreed, no means it ran (or its
            counts contradict) and disagreed, and n/a means the arm has never
            executed so there is nothing it could agree with.  One n/a arm keeps
            the whole line from stamping yes -- type-checks has been 0 for every
            program in P1, and a zero is not a pass.
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
# The value-type clash boundary is exercised by exactly this many WALK rows, and
# that number is a pinned literal on purpose.  Deriving it from the table would
# be the tautology this batch already hit once: delete the row and the
# expectation shrinks with it, so the emitter's clash counter goes unexercised
# and the reading stays green.  A literal turns "the boundary stopped being
# measured" into a red word, and adding a second clash row reddens until this
# number is updated with it.
EXPECT_CLASH_ROWS = 1
CONFLICT_RE = re.compile(r"^IR-WASM-MIR-KIND-CONFLICT[|]writes=(\d+)[|]conflicts=(\d+)[|]type-checks=(\d+)[|]type-conflicts=(\d+)[|]")

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
    # The register-clash form: one register written as integer and then as
    # float.  Before claim_slot the emitter had no dst-kind check on the
    # LOADK_FLOAT path at all, so this program produced a module that node
    # rejected with "local.set[0] expected type i64, found f64.const of type
    # f64" -- a message naming an instruction index and saying nothing about the
    # source program or which opcode created the integer.  The row declares the
    # refusal, and the substring it names is the one that identifies the writer.
    ("OP_ADD",         "say 1 + 2\nsay 1.5 + 2.5\n",
                       ("refused", "was made integer by OP_LOADK_INT")),
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

# Every dump that fails has to fail for a reason this line declares.  A class is
# a claim about WHY the front end may refuse a file: old `be` syntax, a cyclic
# import, a genuine syntax error, a finally it will not compile.  A file that
# fails for any OTHER reason -- a crash, an unreadable include, a program that
# is simply valid -- is a defect in the census, because its opcodes leave the
# evidence without leaving a trace: `reachable` loses a member and `failed`
# goes up by one, and the verdict does not move.
#
# The class list is identities rather than a count on purpose.  "failed=37"
# alone is the same number whether those 37 are fixtures or whether a real
# program broke, and a pinned count cannot tell the two apart either -- it
# reddens when a fixture is added and stays green when a program stops parsing.
# The keyword table is read from the lexer rather than restated here: a Python
# copy of it would be a second source of truth that drifts the moment a keyword
# is added, and the classification below turns on whether a token IS a keyword.
def lexer_keywords(root=ROOT):
    """Every word src/lexer/lexer.c turns into a token, read from the table."""
    text = (Path(root) / "src/lexer/lexer.c").read_text()
    return set(re.findall(r'\{"([a-z_]+)",\s*TOK_[A-Z_]+\}', text))


def but_got(message):
    """The (expected, got) pair a parser rejection prints, or (None, None)."""
    m = re.search(r"expected '([^']*)', but got '([^']*)'", message)
    return (m.group(1), m.group(2)) if m else (None, None)


# Each class is ONE sentence of language intent, not a syntax-school name.  That
# is the criterion a class has to meet: a rejection belongs here when somebody
# can say why the language refuses it, and a rejection that needs two different
# sentences is two classes.  The previous version satisfied neither -- it had a
# single `parse-error` bucket, so "you may not shadow a builtin" (measured: a
# fresh `func max(a, b)` file lands there, because `max` is a lexer keyword) sat
# in the same cell as `be` remnants and old `type` declarations.
#
# A class is (name, predicate).  The predicate takes the message and returns
# True when this rejection is explained by that intent.
CENSUS_FAIL_CLASSES = (
    ("removed-be",
     lambda m: "`be` declarations were removed" in m),
    # `type Name = ...` is gone, and it is the only construct that asks for a
    # "type name" -- so that expected-kind identifies the removed form everywhere
    # it appears, including when the file that fails is an importer of one.
    ("removed-type-decl",
     lambda m: but_got(m)[0] == "type name"),
    # `->TYPE` casts are gone; the type keyword is no longer an expression.
    ("removed-cast",
     lambda m: but_got(m)[0] == "expression"
     and but_got(m)[1] in ("float", "int", "str", "bool")),
    # `type(x)` was a builtin call; the keyword kept its token, so it now lands
    # where an expression is expected.
    ("removed-builtin-call",
     lambda m: but_got(m) == ("expression", "type")),
    # Anonymous `func(...) { }` as a value is gone.
    ("removed-anon-func",
     lambda m: but_got(m)[0] == "expression" and but_got(m)[1] in ("func", "fn")),
    # A statement keyword whose body the grammar stopped accepting, so the
    # parser reports the token that comes after it (`global count` -> `=`).
    ("removed-statement",
     lambda m: but_got(m) == ("expression", "=")),
    # The live rule: a builtin's name may not be declared by the program.  The
    # evidence is that the rejected token IS in the lexer table, not that the
    # message contains the word "max".
    ("reserved-name",
     lambda m: but_got(m)[0] == "function name"
     and but_got(m)[1] is not None and but_got(m)[1] in lexer_keywords()),
    ("circular-import", lambda m: "circular import" in m),
    ("finally-control-flow",
     lambda m: bool(re.search(r"(break|continue|goto) is not allowed in finally cleanup", m))),
    ("unknown-label", lambda m: "unknown label" in m),
    ("member-on-nonname", lambda m: "on something that is not a name" in m),
)


def census_fail_class(message, classes=CENSUS_FAIL_CLASSES):
    """The declared class this rejection belongs to, or None if none covers it."""
    for name, matches in classes:
        if matches(message):
            return name
    return None


def census_fail_problems(failures, classes=CENSUS_FAIL_CLASSES):
    """Rejections with no declared reason: the census measured, then lost it.

    Each entry of failures is (path, message).  Returns one problem per
    unclassified file, naming the file and the message verbatim, because the
    reader has to decide whether the program is invalid or the instrument is.
    """
    problems = []
    for path, message in failures:
        if census_fail_class(message, classes) is None:
            problems.append(
                "wasm mir: %s failed to dump for no declared reason (%r); a "
                "valid program that fails here leaves the census without a "
                "trace -- reachable shrinks by its opcodes and nothing goes red"
                % (path, message))
    return problems


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

    Relative imports resolve against the working directory, and this repo's .im
    files do not agree on one: `scripts/app.im` imports `utils.im` from beside
    itself, `projects/host/main.im` imports `sync.im` from the repo root.  A
    single cwd therefore rejects valid programs and books them as failures --
    measured at 51 before this was noticed, 37 after two layouts are tried.  The
    retry is not a rescue for broken files: a file that dumps under neither
    layout is still a failure, and still has to name a declared reason.

    The list of files walked comes from the INDEX, and that is a correction, not
    a convenience: the walk used to rglob the filesystem, so a git-ignored
    `.verify/` holding six broken programs was enough to print FAIL on a
    checkout whose tracked corpus was intact -- while the same commit printed
    PASS in a clean worktree, and `cwd-retried` read 14 there and 15 here.  A
    measurement that moves with what happens to be lying around is not a
    measurement of the tree.  Files the index does not carry are still COUNTED
    (as `off-index=`), because leaving a decision off the line makes it
    indistinguishable from a count nobody took.
    """
    seen = set()
    files = dumped = 0
    failures = []
    # Files that only dumped under the SECOND layout.  Printed, not absorbed: a
    # count of evidence obtained by choosing a working directory is the same
    # kind of hidden fact as a basis string that names a wider read than the one
    # performed, and the reviewer who checked it counted 14 where the message
    # said 13.
    retried = []
    # The corpus is what the TREE carries, not what happens to be lying on the
    # disk.  The walk used to rglob the filesystem; `.verify/` is a git-ignored
    # scratch directory, and six broken programs in it were enough to turn this
    # verdict red on a checkout whose tracked corpus was intact -- while the same
    # commit printed PASS in a clean worktree, and `cwd-retried` read 14 there
    # and 15 here.  A reading that changes with what happens to be lying around
    # is a reading of the machine, not of the tree, so the index supplies the
    # list of files walked.
    #
    # What the index does NOT carry is printed as `off-index=`: excluding files
    # is a decision, and a decision that is not on the line is indistinguishable
    # from a count that was never taken.  The basis string carries the same
    # obligation in the other direction -- an exported tree with no `.git` cannot
    # consult an index, falls back to the filesystem walk, and says so, because a
    # measurement that changed its object without saying so in the log is a
    # measurement nobody can compare with the last one.
    mode = GIT_INDEX if git_index_present(root) else FILESYSTEM
    tracked_files = tracked_im_files(root) if mode == GIT_INDEX else None
    disk = [q.relative_to(root).as_posix()
            for q in sorted(Path(root).rglob("*.im"))
            if not any(part in CENSUS_SKIP for part in q.relative_to(root).parts)]
    walk, excluded = corpus_to_walk(mode, tracked_files, disk)
    for rel in walk:
        p = Path(root) / rel
        if not p.exists():
            # In the index and gone from disk: not walked here, and the scope
            # line books the difference rather than this loop counting it twice.
            # That difference is a FAILURE, not a rounding: the index says the
            # tree carries this file and the disk says it does not, so neither
            # one can be trusted for the breadth of this walk.
            continue
        files += 1
        out = None
        # Two layouts, not one: see the docstring above.  Successful dump wins;
        # if neither succeeds the file is a failure either way.
        for attempt, (cwd, name) in enumerate(
                ((str(root), str(p)), (str(p.parent), p.name))):
            rc, text, _ = run([tool, "dump", name], cwd=cwd)
            if rc == 0 and text.strip():
                out = text
                if attempt:
                    retried.append(str(p.relative_to(root)))
                break
        if out is None:
            rcs, text, err = run([tool, "dump", str(p)], cwd=str(root))
            message = ("%s\n%s" % (text or "", err or "")).strip().splitlines()
            failures.append((str(p.relative_to(root)),
                             message[0] if message else "(no output)"))
            continue
        dumped += 1
        for m in re.finditer(r"op=(OP_[A-Z0-9_]+)", out):
            seen.add(m.group(1))
    return seen, files, dumped, failures, retried, excluded, mode


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


def tracked_im_files(root):
    """The .im files the index carries, relative to `root`; None when there is no index."""
    rc, out, _ = run(["git", "-C", str(root), "ls-files", "--", "*.im"])
    if rc != 0:
        return None
    return sorted(line.strip() for line in out.splitlines() if line.strip())


def off_index(disk, tracked):
    """Files on disk that the index does not carry: present, printed, not walked."""
    known = set(tracked)
    return sorted(rel for rel in disk if rel not in known)


# The two things a walk can be a walk OF.  Printed on the census line, because a
# reader comparing two readings has to know whether both measured the tree.
GIT_INDEX = "index"
FILESYSTEM = "filesystem"


def git_index_present(root):
    """Whether `root` is the top of a git work tree, i.e. owns an index of this corpus.

    `rev-parse --is-inside-work-tree` is not enough: any scratch directory inside
    a checkout answers "true" while `ls-files` reports the PARENT repository's
    tracked files, so an exported (or copied) tree sitting under a checkout would
    be walked as zero files and agree with itself -- measured: a `git archive`
    export inside `.scratch/` printed `files=0 ... index=0|walked=0|shortfall=0`
    and a PASS verdict while 399 `.im` files sat unread beside it.  Asking for the
    work-tree top and comparing it with `root` is the question that distinguishes
    "this directory has an index" from "some ancestor has one".
    """
    rc, out, _ = run(["git", "-C", str(root), "rev-parse", "--show-toplevel"])
    if rc != 0:
        return False
    try:
        return Path(out.strip()).resolve() == Path(root).resolve()
    except OSError:
        return False


def corpus_to_walk(mode, tracked_files, disk):
    """The files this census walks, and the ones it books instead of walking.

    Returns `(walk, off_index)`; `off_index` is None when no such count exists.

    In `GIT_INDEX` mode the walk is exactly the files the index carries.
    That is what makes the verdict a statement about the tree: a broken program
    in a git-ignored scratch directory no longer reddens a checkout whose tracked
    corpus is intact, while a tracked program that is broken is still walked and
    still has to name a declared reason or fail.  A tracked file that is missing
    from the disk is a SHORTFALL (see census_scope): the index says the tree
    carries it, the disk says otherwise, and neither can then certify the breadth
    of the walk.

    In `FILESYSTEM` mode there is no index to consult -- this repo's Windows
    readings were taken on `git archive` exports, which have no `.git` (docs/AUDIT.md
    §1.65, docs/BOARD.md ctest row).  There the walk stays on the filesystem,
    exactly as before this change, and the basis string says which of the two
    objects was measured.  Falling back is not the same as refusing, and saying so
    is not the same as being silent about it.
    """
    if mode == FILESYSTEM:
        return list(disk), None
    if tracked_files is None:
        return [], None
    return list(tracked_files), off_index(disk, tracked_files)


def census_scope(tracked, walked, skipped, mode=GIT_INDEX):
    """Whether the walk covered the corpus the census claims to have read.

    `files=` is produced by the very walk it describes, so a walk that stops
    early prints a smaller `files=` and the line keeps its shape -- this is the
    defect the hand-picked `roots` list had, one level up.  The index is an
    instrument that does not run this walk, so the two can be compared instead
    of trusting each other.

    A SHORTFALL is a failure: the skip list is hiding corpus, and every reach
    answer computed below it was computed over a narrowed language.  The same
    reading covers a file the index carries but the disk does not (`walked`
    falls one short): a walk whose breadth cannot be certified is not evidence.
    Extra files (an untracked probe, a new example) make the census wider, which
    is never wrong -- but they are not corpus, they are printed as `off-index=`,
    and they are not walked, because a walk that counted them turned a
    git-ignored `.verify/` into a red verdict on an intact tree.  An index that
    could not be read is a refusal too: "I could not look" must not print as
    "there is nothing there", because a census over zero files agrees with itself.

    `mode=FILESYSTEM` asserts nothing about breadth and says so by returning []:
    with no index there is no second instrument, and inventing a bound out of the
    walk's own count would be the defect this function exists to catch.  The
    census line prints that basis, so the reading is still comparable with the
    next one.
    """
    if mode == FILESYSTEM:
        return []
    if tracked < 0:
        return ["the census could not read the git index, so it cannot say which "
                ".im files this tree carries: walking the filesystem instead "
                "measures the machine, not the tree"]
    if walked >= tracked:
        return []
    return ["the census walked %d .im files but the index holds %d: skipping %s hides "
            "corpus, so every unreachable verdict below was computed over the part "
            "that was read" % (walked, tracked, ",".join(skipped) or "nothing")]


_MISSING = object()


def broken_reading(*, shell=_MISSING, scope=_MISSING, unreachable=_MISSING,
                   subset=_MISSING, rows=_MISSING, claims=_MISSING,
                   kinds=_MISSING, orphans=_MISSING):
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
                        ("rows", rows), ("claims", claims), ("kinds", kinds),
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
            or bool(rows) or bool(claims) or bool(kinds) or bool(orphans))


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


def kind_problems(runs, clashes, declared):
    """The value-type claims the emitter records, checked against what it printed.

    runs is [(label, returncode, counters)], counters being the tuple the tool
    prints on its own IR-WASM-MIR-KIND-CONFLICT line -- or None when that line is
    absent.  clashes is the number of rows the emitter refused because one
    register had already been claimed by another value type.

    The counters exist because a check that stops running and a check that
    passes look identical from the outside: both print no refusal.  Printing the
    number of writes it checked is what tells the two apart, so a run that emits
    a module without recording a single claim is a problem here, not a silence.
    """
    problems = []
    problems.extend(kind_side_problems(runs, clashes, declared))
    problems.extend(type_side_problems(runs))
    return problems


def kind_side_problems(runs, clashes, declared):
    """Disagreements about the WRITE half: did every claim get checked, and did
    the tally match the table that was supposed to exercise it."""
    problems = []
    missing = [label for label, _rc, ctr in runs if ctr is None]
    if missing:
        problems.append(
            "the emitter printed no value-type counter for %d row(s) (%s); a "
            "counter nobody prints is a check nobody can falsify"
            % (len(missing), ",".join(missing)))
    silent = [label for label, rc, ctr in runs
              if ctr is not None and rc == 0 and ctr[0] == 0]
    if silent:
        problems.append(
            "the emitter accepted %d program(s) while claiming no register at all "
            "(%s): the check was skipped, not passed"
            % (len(silent), ",".join(silent)))
    if clashes != declared:
        problems.append(
            "the driver table declared %d value-type clash row(s) and the emitter "
            "refused %d: the counter that makes this check falsifiable is exercised "
            "by that row, and a pinned number is what stops the expectation shrinking "
            "with the table" % (declared, clashes))
    found = [label for label, _rc, ctr in runs if ctr is not None and ctr[1] > 0]
    if len(found) < clashes:
        problems.append(
            "the emitter refused %d program(s) because one register was claimed by "
            "two value types, but its own counter recorded %d (%s)"
            % (clashes, len(found), ",".join(found) or "none"))
    return problems


def type_side_problems(runs):
    """Disagreements about the LABEL half: a builder that labelled an
    instruction with a value type its opcode contradicts."""
    problems = []
    lies = [label for label, _rc, ctr in runs if ctr is not None and ctr[3] > 0]
    if lies:
        problems.append(
            "a builder labelled an instruction with a value type the opcode "
            "contradicts (%s); the derived kind and the declared type cannot both "
            "be true, and the counter that catches it has to stay exercised"
            % ",".join(lies))
    return problems


def arm_agreement(problems, exercised):
    """One arm of the value-type line, with three answers instead of two.

    `agree=yes` is a statement about a comparison, so it needs two sides: the
    check ran `exercised` times and disagreed `problems` times.  An arm that has
    never executed cannot agree -- it has nothing to disagree with.  Printing
    that zero as `yes` is the shape this batch keeps hitting: a zero on a
    reading rendered as a pass.  So a zero denominator and no problems is
    `n/a`, which is the honest word, and the whole line's `agree` inherits it.

    Problems still win at a zero denominator: if the arm has not run AND the
    counts are inconsistent, the reading is broken, not merely unexercised.
    """
    if problems:
        return "no"
    return "yes" if exercised else "n/a"


def combine_agreement(*arms):
    """`no` if any arm disagreed, `n/a` if any arm was never exercised."""
    if any(a == "no" for a in arms):
        return "no"
    return "n/a" if any(a == "n/a" for a in arms) else "yes"


KIND_WIRING = re.compile(
    r"^\s*kind_agree, type_agree, line_agree = kind_agreement\(kind_runs, "
    r"clash_rows\)$", re.M)


def kind_line_wiring(text):
    """main() must print what kind_agreement returns, not words it typed.

    This is a peg, and the reason it has to exist is that the honest version
    failed: arm_agreement and combine_agreement had cells, the reading still
    printed agree=yes with type-checks=0, and every cell stayed green -- because
    the cells call the functions and only main() decides whether the line does.
    Replacing that one call with a literal tuple reproduced the whole defect
    with a PASS verdict and 74 green checks, which is the strongest possible
    argument that the call site needs a check of its own.
    """
    if KIND_WIRING.search(text):
        return []
    return ["the KIND-CONFLICT line's three words are not computed by "
            "kind_agreement(kind_runs, clash_rows); the cells can test that "
            "function but only main's call site decides whether the line prints "
            "it, and printing them inline is how type-checks=0 stamped agree=yes"]


def kind_agreement(runs, clash_rows):
    """The three words the KIND-CONFLICT line prints, in one place.

    They used to be computed inline in main(), which meant a self-check cell
    could test the arithmetic but not whether the line still used it -- the same
    shape as a guard nobody calls.  Here the caller and the cell take the same
    function, so the cell can assert the whole line's answer for a run whose
    label arm never executed.
    """
    kind_agree = arm_agreement(
        kind_side_problems(runs, len(clash_rows), EXPECT_CLASH_ROWS),
        sum((ctr[0] for _l, _rc, ctr in runs if ctr), 0))
    type_agree = arm_agreement(
        type_side_problems(runs),
        sum((ctr[2] for _l, _rc, ctr in runs if ctr), 0))
    return kind_agree, type_agree, combine_agreement(kind_agree, type_agree)


def label_gap(type_checks):
    """The ir->type cross-check counted its runs; zero runs is not agreement.

    Its sibling counter pair rides on the KIND-CONFLICT line, where a check that
    stops asking shows up as writes=0.  The label half fails the opposite way:
    it cannot ask yet, because every one of the places that build an IrInsn
    writes IR_T_UNKNOWN, so the arm has never executed for any program.  A zero
    left alone on that line reads as "checked, found nothing", which is the same
    shape as the OP_MOV case label that claimed the emitter handled a move no
    program could reach.  So it prints in the progress channel instead.
    """
    if type_checks == 0:
        return ("the ir->type cross-check ran 0 times: every instruction the "
                "builder produces is labelled IR_T_UNKNOWN, so the label arm is "
                "unexercised rather than passing")
    return None


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
    (reachable, nfiles, ndumped, nfailures, nretried,
     noffindex, cmode) = census_reachable(tool, ROOT)
    # The scope of that walk is itself a reading, and the walk cannot certify
    # its own breadth -- see census_scope.  With no index (an exported tree)
    # there is no second instrument to compare against, so the count is -1 and
    # the scope function declines to assert anything; the basis string below
    # says which object was measured.
    ntracked = tracked_im_count(ROOT) if cmode == GIT_INDEX else -1
    scope = census_scope(ntracked, nfiles, CENSUS_SKIP, mode=cmode)
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
    kind_runs = []
    clash_rows = []
    for op, n, src_text, want in rows:
        executed += 1
        rows_by_op[op] = n
        p = Path(work) / ("%s_%d.im" % (op, n))
        p.write_text(src_text)
        w = Path(work) / ("%s_%d.wasm" % (op, n))
        rc_e, out_e, err_e = run([tool, "wasm", p, w])
        reason_lines = (out_e + err_e).strip().splitlines()
        reason = reason_lines[0] if reason_lines else "-"
        ctr = None
        for ln in reason_lines:
            m = CONFLICT_RE.match(ln.strip())
            if m:
                ctr = tuple(int(x) for x in m.groups())
        kind_runs.append(("%s row %d" % (op, n), rc_e, ctr))
        if "one wasm local holds one value type" in (out_e + err_e):
            clash_rows.append("%s row %d" % (op, n))
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

    # The counters ride in one reading of their own.  Both halves are printed:
    # writes is what the check looked at, conflicts is what it caught.  A check
    # that stopped running reports writes=0 and no conflict, which is the same
    # shape as a clean run unless the write count is on the line.
    # The reading checks its own wiring.  This was only a --self-check cell at
    # first, and the reviewer ran the experiment that showed why that is not
    # enough: replacing the call below with a literal tuple left --self-check
    # red but the READING green -- agree=yes next to type-checks=0, PROBLEM-SCOPE
    # printed=0, VERDICT PASS.  A peg that only a flag runs is a peg the default
    # run does not have.
    kind_probs = (kind_problems(kind_runs, len(clash_rows), EXPECT_CLASH_ROWS)
                  + kind_line_wiring(Path(__file__).read_text()))
    kw = sum((ctr[0] for _l, _rc, ctr in kind_runs if ctr), 0)
    kc = sum((ctr[1] for _l, _rc, ctr in kind_runs if ctr), 0)
    tk = sum((ctr[2] for _l, _rc, ctr in kind_runs if ctr), 0)
    tkc = sum((ctr[3] for _l, _rc, ctr in kind_runs if ctr), 0)
    kind_agree, type_agree, line_agree = kind_agreement(kind_runs, clash_rows)
    print("IR-WASM-MIR-KIND-CONFLICT|rows=%d|writes=%d|conflicts=%d|type-checks=%d"
          "|type-conflicts=%d|clash-rows=%d|kind-agree=%s|type-agree=%s"
          "|basis=every-register-write-claims-a-value-type-and-the-count-is-"
          "printed-on-both-paths|agree=%s"
          % (len(kind_runs), kw, kc, tk, tkc, len(clash_rows), kind_agree,
             type_agree, line_agree))
    problems.extend(kind_probs)
    # The second pair on that line is a different kind of claim: the ir->type
    # cross-check cannot run until the builder starts labelling instructions, and
    # all four places that build an IrInsn write IR_T_UNKNOWN.  type-checks=0 is
    # therefore an arm that never executed, and it says so in the progress
    # channel rather than leaving a zero that reads as "checked, found nothing".
    gap = label_gap(tk)
    if gap:
        print("IR-WASM-MIR-NOT-YET|" + gap)

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
    # same shape.  `off-index` is `n/a` where no such count exists (a filesystem
    # walk in an exported tree), and `basis` names the object measured: two
    # readings can only be compared once a reader can see they measured the same
    # thing.
    print("IR-WASM-MIR-CENSUS|files=%d|dumped=%d|failed=%d|reachable=%d|enum=%d"
          "|cwd-retried=%d|off-index=%s|basis=%s"
          % (nfiles, ndumped, len(nfailures), len(reachable), len(enum_ops),
             len(nretried),
             "n/a" if noffindex is None else str(len(noffindex)),
             "dump-over-every-.im-the-index-carries" if cmode == GIT_INDEX
             else "dump-over-every-.im-on-disk (no git index: exported tree)"))
    # `failed` is a number with no meaning of its own: a fixture the front end is
    # meant to refuse and a valid program that stopped parsing are the same count.
    # So every rejection is matched against a declared reason, and the ones that
    # match nothing are the ingredient.  Without this, a program that fails is
    # worth exactly one point of `reachable`, and no verdict notices.
    fail_probs = census_fail_problems(nfailures)
    by_class = {}
    for _path, message in nfailures:
        name = census_fail_class(message) or "undeclared"
        by_class[name] = by_class.get(name, 0) + 1
    declared_classes = [name for name, _p in CENSUS_FAIL_CLASSES]
    print("IR-WASM-MIR-CENSUS-FAIL|failed=%d|classified=%d|undeclared=%d"
          "|declared=(%s)|by-class=(%s)|basis=every-rejection-matches-a-declared-reason"
          "|agree=%s"
          % (len(nfailures), len(nfailures) - len(fail_probs), len(fail_probs),
             ",".join(declared_classes),
             ",".join("%s:%d" % (n, by_class.get(n, 0)) for n in declared_classes),
             "yes" if not fail_probs else "no"))
    problems.extend(fail_probs)
    # ...and the walk's own breadth is a separate number with its own instrument:
    # an index that did not walk anywhere.  Without this, a skip list that grew
    # by one directory produces a smaller census with the same shape.
    print("IR-WASM-MIR-CENSUS-SCOPE|index=%s|walked=%d|shortfall=%s|skipped=(%s)"
          "|basis=%s|agree=%s"
          % ("unreadable" if ntracked < 0 else str(ntracked), nfiles,
             "unknown" if ntracked < 0 else max(0, ntracked - nfiles),
             ",".join(CENSUS_SKIP),
             "git-index-vs-this-walk" if cmode == GIT_INDEX
             else "disk-walk-no-index (nothing to compare it with)",
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
    # carried is the union of the ingredient lists, and it has to stay the union:
    # the value-type ingredient was added to the OR but not to this line at
    # first, so its one PROBLEM printed, the verdict failed through it, and
    # PROBLEM-SCOPE still said orphans=1 -- a defect line the tool reported as
    # invisible while it was the thing turning the reading red.
    # ...and the census's rejection ingredient went missing from exactly this
    # line when it was added: scope carries breadth, fail_probs carries reasons,
    # broken_reading took scope + fail_probs and this union took scope alone, so
    # dropping a declared class printed a PROBLEM, failed the verdict, and
    # PROBLEM-SCOPE said carried=0|orphans=1 about the line causing the failure.
    # Same defect a third time, in the same file, so it now reads as one term:
    carried = (shell_problems(mentions, walks) + list(scope) + fail_probs
               + subset_probs + row_problems + claim_probs + kind_probs)
    orphans = orphan_problems(problems, carried)
    print("IR-WASM-MIR-PROBLEM-SCOPE|printed=%d|carried=%d|orphans=%d"
          "|basis=every-printed-PROBLEM-must-ride-an-ingredient-of-the-verdict"
          "|rule=a-PROBLEM-line-exists-exactly-when-the-verdict-says-FAIL"
          % (len(problems), len(carried), len(orphans)))
    for p in orphans:
        print("IR-WASM-MIR-ORPHAN|" + p)
    broken = broken_reading(shell=shell_problems(mentions, walks),
                            # scope carries both halves of the census's own
                            # integrity: the breadth of the walk (index vs
                            # walked) and the reasons its rejections give.  A
                            # rejection no declared class covers is printed as a
                            # PROBLEM and carried here, so it cannot ride as a
                            # number on the census line while the verdict stays
                            # green.
                            scope=scope + fail_probs,
                            unreachable=unreachable, subset=subset_probs,
                            rows=row_problems, claims=claim_probs,
                            orphans=orphans, kinds=kind_probs)
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
    expect("a file on disk that the index does not carry is excluded, not walked",
           off_index(["a.im", "b.im"], ["a.im"]), ["b.im"])
    expect("an index that cannot be read refuses to call an empty census agreement",
           len(census_scope(-1, 399, CENSUS_SKIP)), 1)
    # The cells below pin the correction itself: the walk's list comes from the
    # index, so the verdict stops moving with what happens to be lying on the
    # disk -- and the one case that must still fail (a tracked file that is
    # broken) is the case the walk list keeps.
    expect("the walk takes its list from the index, not from the disk",
           corpus_to_walk(GIT_INDEX, ["kept.im"], ["kept.im", "stray.im"]),
           (["kept.im"], ["stray.im"]))
    expect("a tracked program is walked even when it is the one that is broken",
           corpus_to_walk(GIT_INDEX, ["broken.im"], ["broken.im"])[0], ["broken.im"])
    expect("a file the index carries and the disk does not is a shortfall",
           len(census_scope(400, 399, CENSUS_SKIP)), 1)
    expect("an exported tree walks the disk and books no off-index count",
           corpus_to_walk(FILESYSTEM, None, ["a.im", "b.im"]), (["a.im", "b.im"], None))
    expect("a filesystem walk does not borrow the index's bound",
           census_scope(-1, 399, CENSUS_SKIP, mode=FILESYSTEM), [])
    # A directory that merely lives under a checkout must not be read with the
    # checkout's index: that reading walks zero files and agrees with itself.
    expect("the checkout owns an index of its own", git_index_present(ROOT), True)
    with tempfile.TemporaryDirectory() as td:
        expect("a directory with no index of its own is walked from the disk",
               git_index_present(td), False)
    expect("a narrowed census breaks the reading through the verdict itself",
           broken_reading(shell=[], scope=census_scope(399, 35, CENSUS_SKIP),
                          unreachable=set(), subset=[], rows=[], claims=[], orphans=[], kinds=[]), True)
    expect("a wide census leaves the verdict to the other four",
           broken_reading(shell=[], scope=census_scope(399, 399, CENSUS_SKIP),
                          unreachable=set(), subset=[], rows=[], claims=[], orphans=[], kinds=[]), False)
    # One cell per ingredient.  The predicate is a symmetric OR, so an argument
    # passed in the wrong order is not a defect -- but an ingredient main() stops
    # passing is, and that stays invisible while only the scope branch is tested.
    expect("a shell that never drove the MIR path breaks the reading",
           broken_reading(shell=["no-mir-path"], scope=[], unreachable=set(),
                          subset=[], rows=[], claims=[], orphans=[], kinds=[]), True)
    expect("a claimed opcode nothing can reach breaks the reading",
           broken_reading(shell=[], scope=[], unreachable={"OP_Z"}, subset=[],
                          rows=[], claims=[], orphans=[], kinds=[]), True)
    expect("a subset naming an undeclared opcode breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(),
                          subset=["work-queue-names-OP_NOT_REAL"], rows=[],
                          claims=[], orphans=[], kinds=[]), True)
    # The ingredient the driver table added: a row states an answer, and the
    # reading is broken when the program does not reproduce it.  Until this
    # ingredient existed the third column could say "999" while every number
    # stayed put and the verdict stayed green.
    expect("a row whose stated answer is not reproduced breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=["the row for OP_ADD says the interpreter prints '3' "
                                "but it printed '999'"], claims=[], orphans=[], kinds=[]), True)
    # The ingredient whose whole job is to be the last one: a printed defect that
    # no ingredient carries.  import_problems was exactly that -- it printed
    # "MIR_IMPORTS is 5 but the module imports 4" while the verdict said PASS.
    expect("a defect line no ingredient carries breaks the reading",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=[], claims=[], orphans=["MIR_IMPORTS is 5 but the module imports 4"], kinds=[]),
           True)
    expect("an orphan is one that no carried list holds",
           orphan_problems(["d1", "d2"], ["d1"]), ["d2"])
    expect("a defect already carried is not an orphan",
           orphan_problems(["d1"], ["d1"]), [])
    expect("a line that nobody carries is an orphan even when it was printed as progress",
           orphan_problems(["note"], []), ["note"])
    expect("six silent ingredients break nothing",
           broken_reading(shell=[], scope=[], unreachable=set(), subset=[],
                          rows=[], claims=[], orphans=[], kinds=[]), False)
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
                                                       {"OP_ADD"}), orphans=[], kinds=[]),
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
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(), claims=[], kinds=[])), True)
    expect("a verdict handed None where a list belongs refuses too",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                         subset=None, claims=[], orphans=[], kinds=[])), True)
    expect("a verdict that names every ingredient but the driver table refuses",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                         subset=[], claims=[], kinds=[])), True)
    expect("claims is an ingredient the verdict cannot be silent about",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                        subset=[], rows=[], claims=None,
                                        orphans=[], kinds=[])), True)
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
    expect("a row that emitted a module without claiming one register is caught",
           len(kind_problems([("OP_A row 1", 0, (0, 0, 0, 0))], 0, 0)), 1)
    expect("a register clash the emitter refused with its counter agrees",
           kind_problems([("OP_A row 1", 1, (4, 1, 0, 0))], 1, 1), [])
    expect("a clash refused while the counter says none is caught",
           len(kind_problems([("OP_A row 1", 1, (4, 0, 0, 0))], 1, 1)), 1)
    expect("a builder whose label contradicts the opcode is caught",
           len(kind_problems([("OP_A row 1", 1, (0, 0, 1, 1))], 0, 0)), 1)
    expect("a row with no counter line at all is caught",
           len(kind_problems([("OP_A row 1", 0, None)], 0, 0)), 1)
    expect("a clash row that vanished from the table is caught by the pinned number",
           len(kind_problems([("OP_A row 1", 0, (4, 0, 0, 0))], 0, 1)), 1)
    expect("the value-type counter is an ingredient the verdict cannot be silent about",
           raises(lambda: broken_reading(shell=[], scope=[], unreachable=set(),
                                         subset=[], rows=[], claims=[],
                                         orphans=[])), True)
    expect("a cross-check that never ran prints as not-yet rather than agreement",
           bool(label_gap(0)), True)
    expect("a cross-check that ran and found nothing says nothing",
           label_gap(4), None)
    # A zero denominator is not agreement.  These three cells are the whole
    # point: the label arm has never run, and the line has to say n/a rather
    # than print a zero and call it green.
    expect("an arm that never ran is n/a, not yes", arm_agreement([], 0), "n/a")
    expect("an arm that ran and disagreed with nothing is yes",
           arm_agreement([], 4), "yes")
    expect("an arm that never ran but whose counts contradict is still broken",
           arm_agreement(["counts disagree"], 0), "no")
    expect("one n/a arm keeps the whole line from stamping yes",
           combine_agreement("yes", "n/a"), "n/a")
    expect("two agreeing arms stamp yes", combine_agreement("yes", "yes"), "yes")
    expect("a broken arm is broken even next to an unexercised one",
           combine_agreement("no", "n/a"), "no")
    # The line's own answer, through the function main() prints with: a clean
    # write arm plus a label arm that has never run is n/a, and no number of
    # green cells about the arithmetic changes that.
    # ...and the cell for the peg, because a cell that only tests the pure
    # function cannot see main's call site: replacing that one line with a
    # literal tuple reproduced agree=yes next to type-checks=0 with a PASS
    # verdict and every other check green.
    src = Path(__file__).read_text()
    expect("the reading's agreement is wired through the function the cells test",
           kind_line_wiring(src), [])
    expect("...and that peg is not vacuous: a line typed by hand is caught",
           len(kind_line_wiring(src.replace(
               "kind_agree, type_agree, line_agree = kind_agreement(kind_runs, clash_rows)",
               'kind_agree, type_agree, line_agree = ("yes", "yes", "yes")'))), 1)
    expect("a clean run with an unexercised label arm does not stamp the line yes",
           kind_agreement([("OP_A row 1", 1, (4, 1, 0, 0))],
                          ["OP_A row 2"])[2], "n/a")
    # ...and her second point: `failed` is a count with no meaning until every
    # rejection names a reason.  A fixture the front end is meant to refuse and
    # a valid program that stopped parsing are the same number on that line.
    expect("a rejection for a declared reason is not a problem",
           census_fail_problems([("a.im", "Error at line 2: `be` declarations "
                                          "were removed")]), [])
    expect("a rejection for no declared reason is a problem",
           len(census_fail_problems([("a.im", "Segmentation fault (core dumped)")])), 1)
    expect("a reason is matched by class, not by file",
           census_fail_class("[error] circular import: 'nscyc_a.im' as 'a'"),
           "circular-import")
    expect("an undeclared reason reaches the verdict, not just the print",
           broken_reading(shell=[], scope=census_fail_problems(
                              [("a.im", "Segmentation fault")]),
                          unreachable=set(), subset=[], rows=[], claims=[],
                          orphans=[], kinds=[]), True)
    expect("a missing prerequisite exits 2, not 0", REFUSED_RC, 2)

    if bad:
        print("wasm_mir self-check: FAIL -- " + "; ".join(bad))
        return 1
    print("wasm_mir self-check: ok (%d checks)" % checks[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())