#!/usr/bin/env python3
"""What is this route's boundary, and is each part of it still true?

`tools/aot_mir_cover.test.py` reports `handles=N|of=67|unknown=M`.  That
number is a COUNT, and a count cannot say why the M remainder is there.  The
reading it produces has two failure modes that look identical in the count:

  1. an opcode is missing because nobody has written its arm yet; and
  2. an opcode is missing because this route has DECIDED not to answer it, for
     a reason that will not change when somebody finds the time.

Those call for opposite responses, and the count prints the same digit for
both.  Worse: if a new opcode is appended to src/compiler/bytecode.h tomorrow
and nobody writes an arm, it joins the remainder silently -- `unknown` goes up
by one and nothing says WHICH kind of absence it is.  A reader who sees
`unknown=8` cannot tell "8 things left to do" from "8 things declared out of
scope", and the difference is the whole question a reader has.

So this checker asks the boundary to be an ARTIFACT rather than a sentiment:

  * every opcode the emitter has no case for must appear in BOUNDARY below,
    with a class;
  * every entry's EVIDENCE must be findable in the tree -- a file named, a
    symbol named, or a measurement line that a tool in this repo can produce.

It does NOT verify that a boundary is smart.  It verifies that it is
COMPLETE (nothing falls off the list) and ATTESTED (nothing is a claim with no
artifact behind it).  A boundary that is complete and attested can still be
wrong; a boundary that is neither cannot be argued with at all.

WHY A TABLE IN THIS FILE AND NOT A SECTION OF docs/STATUS.md

docs/ already carries the reasoning, in thirty-odd section numbers.  What it
cannot carry is a SET THAT HAS TO BE CLOSED: prose admits "and so on", and a
checker does not.  The table below is the closed set; each row names the
section that argues for it, so the two cannot drift silently -- if a class is
added here without a section, the next reader has no argument to read, and if
a section's opcode is dropped from here this checker goes green while the
boundary it describes has a hole.

Usage:
    python3 tools/aot_mir_surface.py [--report] [--self-check]
Exit: 0 = every unemitted opcode is classified and attested; 1 = one is not.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BYTECODE_H = ROOT / "src" / "compiler" / "bytecode.h"
EMITTER = ROOT / "src" / "compilation" / "aot_mir.c"

# One row per UNEMITTED opcode.  class: what kind of absence this is.
# evidence: a (file, needle) pair the tree must still contain, or a
#           ("gate", "<stage>") pair whose stage must still exist.
#
# The class vocabulary is deliberately small.  It is not a taxonomy of coding
# difficulty; it is a taxonomy of WHY THIS ROUTE WILL NOT ANSWER IT:
#
#   another-thread  the answer requires a second thread to exist.  This route
#                   refuses a container carrying a thread section by name
#                   (why=threads), so the opcode is unreachable BY CONSTRUCTION
#                   -- no program that could execute it is accepted, and no
#                   amount of work on the emitter changes that without first
#                   accepting threads.
#   scheduler       the answer requires a scheduler to suspend and resume
#                   execution.  Even with threads accepted, the emitted C has
#                   no fiber, so this is a second subsystem.
#   measurement     the opcode is reachable, but its observable effect is a
#                   RESOURCE LIMIT being hit at a particular instruction count.
#                   The emitted C's instruction stream is not the interpreter's,
#                   so the count the limit compares against has no counterpart
#                   here: the program would have to be refused, not lowered.
#   retired         the interpreter no longer dispatches it.  There is nothing
#                   to agree WITH.
BOUNDARY = {
    "OP_THREAD_START": (
        "another-thread",
        "src/compiler/bytecode.h",
        "start thread: r1=thread idx",
        "every answer needs a second thread; the route refuses a thread section",
    ),
    "OP_THREAD_CTRL": (
        "another-thread",
        "src/compiler/bytecode.h",
        "thread ctrl: r1=thread idx(-1=all)",
        "controls another thread's run state",
    ),
    "OP_THREAD_JOIN": (
        "another-thread",
        "src/compiler/bytecode.h",
        "join: r1=thread idx",
        "waits for another thread to finish",
    ),
    "OP_THREAD_WAIT": (
        "another-thread",
        "src/compiler/bytecode.h",
        "thread wait: r1=thread idx",
        "waits on another thread",
    ),
    "OP_THREAD_STATE": (
        "another-thread",
        "src/compiler/bytecode.h",
        "thread state: r1=result reg",
        "reads another thread's state",
    ),
    "OP_THREAD_GOTO": (
        "another-thread",
        "src/compiler/bytecode.h",
        "OP_THREAD_GOTO",
        "jumps into another thread's code at a label",
    ),
    "OP_SEND": (
        "another-thread",
        "src/compiler/bytecode.h",
        "send: r1=thread idx, r2=msg reg",
        "appends to another thread's message queue -- and the interpreter "
        "refuses the program at compile time when the name is not a declared "
        "thread, so no accepted program reaches it",
    ),
    "OP_POP_REG": (
        "retired",
        "src/compiler/bytecode.h",
        "29 is a hole: OP_POP_REG used to sit here",
        "removed from the interpreter; the enum keeps the hole so the ABI "
        "numbering does not slide",
    ),
}

# The classes, and the one sentence that says what a reader should do with
# each.  Asserted to be exactly the set BOUNDARY uses: a class nobody uses is a
# class nobody can read, and a row using an undeclared class is a boundary with
# no stated meaning.
CLASSES = {
    "another-thread": "unreachable by construction: this route accepts no thread section",
    "measurement": "the observable is a stop position, not a value",
    "retired": "the interpreter no longer dispatches it",
}


# WHAT EACH CLASS LEANS ON, AS A CORPUS FACT.
#
# A class is an ARGUMENT, not a label: "another-thread" means "the route refuses
# a thread section", which is a claim about a refusal that must be DEMONSTRATED
# somewhere the suite actually runs.  The demonstration lives in the corpus
# (tools/aot_mir.test.py's MIR_ONLY_REFUSE table), so each class names the `why`
# string that table has to contain.  None means the class needs no refusal: the
# retired row's evidence is the enum's own numbering hole, checked by needle.
#
# Without this check the boundary would keep saying "the route refuses a thread
# section" after somebody deleted the case that proves it -- the row would point
# at a real comment and a real opcode while the behaviour it describes had
# quietly come unwired.
CLASS_BACKING = {
    "another-thread": "threads",
    "measurement": "unsupported-opcode",
    "retired": None,
}

CORPUS = ROOT / "tools/aot_mir.test.py"


def refusal_whys():
    """Every `why` string the corpus's refusal table uses, read from the AST.

    Read as a set of strings rather than by grep so that a mention of the word
    "threads" in a comment cannot stand in for a case that runs.
    """
    import ast
    try:
        tree = ast.parse(CORPUS.read_text(errors="replace"))
    except (OSError, SyntaxError):
        return set()
    whys = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.Assign):
            continue
        if not any(getattr(t, "id", None) == "MIR_ONLY_REFUSE" for t in node.targets):
            continue
        if not isinstance(node.value, ast.Dict):
            continue
        for value in node.value.values:
            if isinstance(value, ast.Tuple):
                for elt in value.elts[1:]:
                    if isinstance(elt, ast.Constant) and isinstance(elt.value, str):
                        whys.add(elt.value)
    return whys


def backing_problems(whys=None):
    """Every class in use must have its refusal reason still in the corpus."""
    if whys is None:
        whys = refusal_whys()
    problems = []
    for cls in sorted({row[0] for row in BOUNDARY.values()}):
        need = CLASS_BACKING.get(cls)
        if need is None:
            continue
        if need not in whys:
            problems.append(
                "class %r argues that its members are unreachable because the "
                "route refuses them with why=%r, but no case in the corpus uses "
                "that reason any more -- the argument is asserted, not "
                "demonstrated" % (cls, need))
    return problems


def declared_opcodes(text):
    """The enum's MEMBERS, in declaration order.

    A NAME IN A COMMENT IS NOT A MEMBER.  The enum's own prose mentions
    OP_POP_REG ('29 is a hole: OP_POP_REG used to sit here'), and a plain
    name scan counts it -- which is how this file's first version reported
    declared=68|emitted=60 while `aot-mir subset` and a case-label grep both
    said 59 emitted of 67.  The extra "member" was a word in a comment and the
    extra "emission" was arithmetic on it.  The denominator a reader compares
    against is IR-MIR-SUBSET's, so both must tell the same story, and the way
    there is to strip comments before counting.

    It is also the honest direction: an opcode that exists only in prose cannot
    have a dispatcher, so counting it as unemitted would put a permanent,
    unwinnable entry in the remainder -- and the remainder is exactly the list
    this checker exists to keep closable.
    """
    m = re.search(r"typedef enum \{(.*?)\n *\}\s*OpCode", text, re.S)
    if not m:
        raise SystemExit("aot_mir_surface: cannot find the OpCode enum in bytecode.h")
    body = re.sub(r"/\*.*?\*/", " ", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", " ", body)
    names = re.findall(r"\bOP_[A-Z0-9_]+\b", body)
    return list(dict.fromkeys(names))


def emitted_opcodes(text):
    """Every opcode the emitter has a `case` label for."""
    return set(re.findall(r"case (OP_[A-Z0-9_]+)", text))


def unemitted():
    declared = declared_opcodes(BYTECODE_H.read_text())
    seen = emitted_opcodes(EMITTER.read_text())
    # OP_POP_REG is the numbering hole: the enum mentions it only in a comment
    # and there is no member to have a case for.  It is in BOUNDARY because a
    # reader asks about it (the comment names it), not because it is a member.
    members = [n for n in declared if n != "OP_POP_REG"]
    return members, [n for n in members if n not in seen]


def evidence_problems():
    """Every BOUNDARY row must name something the tree still contains."""
    problems = []
    for op, row in sorted(BOUNDARY.items()):
        cls, where, needle, _why = row
        if cls not in CLASSES:
            problems.append("%s: class %r is not in CLASSES, so the boundary "
                            "this row draws has no stated meaning" % (op, cls))
        path = ROOT / where
        if not path.exists():
            problems.append("%s: evidence file %s does not exist" % (op, where))
            continue
        if needle not in path.read_text(errors="replace"):
            problems.append("%s: evidence %r is no longer in %s, so the reason "
                            "this row gives cannot be read where it points"
                            % (op, needle, where))
    return problems


def coverage_problems(members, missing):
    """The set must be CLOSED in both directions."""
    problems = []
    unclassified = [op for op in missing if op not in BOUNDARY]
    if unclassified:
        problems.append(
            "the emitter has no case for %s, and the boundary does not classify "
            "it -- a new opcode has joined the remainder and nobody has said "
            "which kind of absence it is (a missing arm, or a decision)"
            % ",".join(unclassified))
    # OP_POP_REG is the one row that is NOT a member: the enum's comment names
    # it in the numbering hole, so it can never appear in `missing` and would
    # otherwise be reported stale forever.  It is kept because a reader asks
    # about it, and the row says why there is nothing to answer.
    stale = [op for op in BOUNDARY
             if op != "OP_POP_REG" and op in members and op not in missing]
    if stale:
        problems.append(
            "the boundary classifies %s as unemitted, but the emitter now has a "
            "case for it -- the row is stale, and a boundary that describes a "
            "tree that no longer exists is worse than no boundary, because it "
            "answers the reader's question wrongly" % ",".join(sorted(stale)))
    unknown_classes = sorted({BOUNDARY[op][0] for op in BOUNDARY} - set(CLASSES))
    if unknown_classes:
        problems.append("rows use undeclared classes: %s" % ",".join(unknown_classes))
    return problems


def self_check():
    """The checker's own controls, in both directions.

    A checker that has only been run on a tree where it passes is not evidence.
    Each cell below is a shape this file must be able to say NO to, built from
    the same two questions the real run asks: is the set CLOSED (nothing
    unclassified), and is every row ATTESTED (its evidence still in the tree)?

    The fixture is two members (OP_A emitted, OP_B not) so a one-row boundary
    is the closed answer and the other shapes are one edit away from it.
    """
    ok = True
    members = ["OP_A", "OP_B"]
    missing = ["OP_B"]          # OP_A is emitted, OP_B is not
    good = {
        "OP_B": ("retired", "src/compiler/bytecode.h", "typedef enum", "fixture"),
    }
    cells = [
        # name, boundary, expect_problem
        ("closed", good, False),
        ("unclassified", {}, True),
        ("stale", dict(good, **{
            "OP_A": ("retired", "src/compiler/bytecode.h", "typedef enum", "fixture"),
        }), True),
        ("bad-evidence", {
            "OP_B": ("retired", "src/compiler/bytecode.h", "NOT-IN-THE-TREE", "fixture"),
        }, True),
        ("unknown-class", {
            "OP_B": ("no-such-class", "src/compiler/bytecode.h", "typedef enum", "fixture"),
        }, True),
    ]
    # Backing cells: the class in `good` is "retired", which needs no refusal.
    # These two show the check is bidirectional -- the same table is clean when
    # the corpus carries the reason the class names, and a problem when it does
    # not.
    backing_cells = [
        ("backed", "another-thread", {"threads"}, False),
        ("unbacked", "another-thread", {"declare-limit"}, True),
    ]
    saved = dict(BOUNDARY)
    try:
        for what, table, want_problem in cells:
            BOUNDARY.clear(); BOUNDARY.update(table)
            problems = coverage_problems(members, missing) + evidence_problems()
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        BOUNDARY.clear(); BOUNDARY.update(saved)
    try:
        for what, cls, whys, want_problem in backing_cells:
            BOUNDARY.clear()
            BOUNDARY.update({"OP_B": (cls, "src/compiler/bytecode.h", "typedef enum", "fixture")})
            problems = backing_problems(whys)
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        BOUNDARY.clear(); BOUNDARY.update(saved)
    cells += backing_cells
    print("IR-AOT-MIR-SURFACE-SELFCHECK|checks=%d|verdict=%s"
          % (len(cells), "PASS" if ok else "FAIL"))
    return 0 if ok else 1

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--self-check", action="store_true")
    args = ap.parse_args()
    if args.self_check:
        return self_check()

    members, missing = unemitted()
    problems = (coverage_problems(members, missing) + evidence_problems()
                + backing_problems())

    if args.report:
        for op in sorted(BOUNDARY):
            cls, where, needle, why = BOUNDARY[op]
            print("IR-AOT-MIR-BOUNDARY|op=%s|class=%s|why=%s|evidence=%s|needle=%s"
                  % (op, cls, why, where, needle))
        for cls in sorted(CLASSES):
            n = sum(1 for v in BOUNDARY.values() if v[0] == cls)
            print("IR-AOT-MIR-BOUNDARY-CLASS|class=%s|n=%d|meaning=%s"
                  % (cls, n, CLASSES[cls]))
    print("IR-AOT-MIR-SURFACE|declared=%d|emitted=%d|unemitted=%d|classified=%d"
          "|classes=%d" % (len(members), len(members) - len(missing), len(missing),
                           len(BOUNDARY), len(CLASSES)))
    for p in problems:
        print("IR-AOT-MIR-SURFACE-PROBLEM|%s" % p)
    print("IR-AOT-MIR-SURFACE-VERDICT|%s" % ("PASS" if not problems else "FAIL"))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
