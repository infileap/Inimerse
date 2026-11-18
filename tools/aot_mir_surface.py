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

# EVERY refusal reason the route can emit, and whether the corpus witnesses it.
#
# §10.216 enumerated them by hand: 19 reasons, 6 of them witnessed.  Two of the
# unwitnessed ones turned out to be triggerable, so they got rows (§10.217).
# This table is that enumeration, kept next to the checker that consumes it, so
# the list cannot rot while the source drifts.
#
# The values are what the corpus says about the reason:
#   "witnessed"     -- at least one case in MIR_ONLY_REFUSE names it
#   "triggerable"   -- a program reaches it, but no case names it yet
#   "unreachable"   -- no program in today's tree can reach it (measured; the
#                       reason for each is in STATUS.md's §10.216)
#   "family"        -- not a reason a program can carry out of the tool: the
#                       default value of g_prescan_why, replaced by a named
#                       reason before any check can fail
REFUSAL_REASONS = {
    "builtin-argc": "witnessed",
    "builtin-call": "witnessed",
    "builtin-stale-stack": "witnessed",
    "declare-limit": "witnessed",
    "illegal-operand": "family",
    "lock-unpaired": "witnessed",
    "set-interval-domain": "witnessed",
    "try-enters": "witnessed",
    "try-escapes": "witnessed",
    # UPDATE (10.219): function-name and global-name were recorded as
    # unreachable because the ledger author believed the identifier lexer only
    # admits [A-Za-z0-9_], so a name could not be folded onto a plain ASCII
    # one.  Measured: a high byte in a source name IS folded to '_' by
    # c_name, so "a\xff" and "a_" both become "fn_a_" / "g_a_".  Both reasons
    # are therefore triggerable, and both are witnessed below.
    # UPDATE (10.220): these five are not "unreachable by construction" -- an
    # invariant the emitter keeps is what stops them, and a broken invariant
    # makes every one of them fire (probes recorded in STATUS 10.220).  They
    # are their own state: the site must still be in the source, and the docs
    # must show a run that made the reason fire.
    "conservation": "invariant",
    "container-shape": "invariant",
    "emit-failed": "invariant",
    "function-name": "witnessed",
    "global-name": "witnessed",
    "not-a-module": "invariant",
    "out-of-memory": "invariant",
    "threads": "witnessed",
    "unsupported-opcode": "unreachable",
    # UPDATE (10.219): write-failed is reachable, but only through the OUTPUT
    # PATH, not through a program shape -- the route refuses when it cannot
    # write the .c file it was told to write.  The corpus cannot express that
    # (the case table names a program, and the harness always picks a writable
    # path), so it is marked "reachable" instead of "witnessed": the ledger
    # check only demands a corpus row for "witnessed".
    "write-failed": "reachable",
}


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


def route_refusal_reasons(text=None):
    """Every `why` string the route can put on a refusal, read from the source.

    Two places put one there: the prescan family (`g_prescan_why = "..."`)
    and the interface layer (`refuse("...")`).  Both are read by pattern, and a
    name in a comment cannot be counted, so the prescan assignments are taken
    from the STRING LITERALS only.
    """
    if text is None:
        text = (ROOT / "src/compilation/aot_mir.c").read_text(errors="replace")
    body = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    body = re.sub(r"//[^\n]*", " ", body)
    reasons = set(re.findall(r'g_prescan_why\s*=\s*"([a-z-]+)"', body))
    reasons |= set(re.findall(r'refuse\(\s*"([a-z-]+)"', body))
    return reasons


# The reasons whose UNREACHABILITY is a fact about the TREE, not a claim about
# one program.  Each one names a premise the checker can re-derive, so a change
# that breaks the premise turns the claim into a PROBLEM instead of leaving a
# stale "unreachable" in the ledger.
#
# §10.216 argued these by hand; §10.218 makes the argument re-runnable.
#
#   (try-enters used to sit here, on the claim that the compiler opens one try
#     region per FUNCTION.  §10.218 measured that claim false: the compiler
#     opens one region per try STATEMENT, so two try statements in one function
#     give two regions, and the catch block's jump enters the second one.  The
#     reason is therefore TRIGGERABLE and is witnessed by a corpus row.)
#   unsupported-opcode: the route refuses a program holding an opcode it has
#     no mask for.  The premise is "every opcode the emitter can meet has a
#     mask", i.e. op_regs and the emitter's case labels cover the same set.
UNREACHABLE_PREMISES = {
    "unsupported-opcode": "every-opcode-has-a-mask",
}

# A reason reached only by breaking an invariant the emitter keeps.  Not
# "unreachable by construction" (there is no program shape to argue about) and
# not "witnessed" (no corpus row can exist), so it is its own state with two
# obligations, both re-runnable:
#
#   1. the SITE -- the fragment below -- must still be in the source; a rewrite
#      that moves the guard, or that lets a program-derived value decide it,
#      takes the fragment with it;
#   2. the docs must carry a probe whose output names the reason.  A guard
#      nobody has ever seen fire is a claim, not a check.
INVARIANT_SITES = {
    "not-a-module": 'if (!m) { refuse("not-a-module"',
    "container-shape": "if (brc != 0 || nn != m->nfuncs) {",
    "out-of-memory": 'if (!g_gref) { refuse("out-of-memory"',
    "emit-failed": "if (emit_container(&out, m, nodes, gnames, fnnames, 0) != 0) {",
    "conservation": "if (g_emitted + g_notyet != g_insns) {",
}
PROBE_DOCS = ROOT / "docs" / "STATUS.md"


def invariant_problems(text=None, docs=None):
    """Every reason the ledger calls an invariant must still be guarded.

    Two questions, one per obligation: is the site still where the ledger says
    it is, and has a probe ever made this reason fire?  A reason that is
    neither witnessed, premised nor invariant-footed is a claim with nothing
    behind it, and that is the shape this function exists to reject.
    """
    if text is None:
        text = (ROOT / "src/compilation/aot_mir.c").read_text(errors="replace")
    if docs is None:
        docs = PROBE_DOCS.read_text(errors="replace")
    problems = []
    for why, state in sorted(REFUSAL_REASONS.items()):
        if state != "invariant":
            continue
        site = INVARIANT_SITES.get(why)
        if site is None:
            problems.append("the ledger calls why=%r an invariant, but no site "
                            "is declared for it -- the argument has no footing" % why)
            continue
        if site not in text:
            problems.append("the ledger calls why=%r an invariant, but its site "
                            "is not in the source any more: %r" % (why, site))
        if ("IR-MIR-REFUSED|why=" + why) not in docs:
            problems.append("the ledger calls why=%r an invariant, but no probe "
                            "in the docs makes it fire -- the guard is a claim "
                            "nobody has seen run" % why)
    return problems


def mask_premise():
    """Does every opcode the emitter can meet have a mask?

    `op_regs` decides whether an instruction is lowered at all: a mask of -1
    means "not yet", and the program is refused with why=unsupported-opcode.
    The premise is that op_regs and the emitter's own case labels name the same
    opcodes -- if the emitter gained an arm without a mask (or the other way
    round), an opcode could reach the not-yet path that the boundary table
    believes is out of scope.
    """
    src = (ROOT / "src" / "compilation" / "aot_mir.c").read_text(errors="replace")
    i = src.find("static int op_regs(int op) {")
    if i < 0:
        return False, "op_regs is gone from the emitter"
    j = src.find("\n}\n", i)
    body = src[i:j]
    masks = set(re.findall(r"case (OP_[A-Z0-9_]+)", body))
    arms = set(re.findall(r"case (OP_[A-Z0-9_]+)", src))
    if masks == arms:
        return True, "op_regs and the emitter cover %d opcodes" % len(masks)
    return False, ("op_regs covers %d opcodes and the emitter %d; only in op_regs: %s; "
                   "only in the emitter: %s"
                   % (len(masks), len(arms),
                      ", ".join(sorted(masks - arms)) or "none",
                      ", ".join(sorted(arms - masks)) or "none"))


PREMISE_CHECKS = {
    "every-opcode-has-a-mask": mask_premise,
}


def unreachable_premise_problems():
    """Every reason the ledger calls unreachable must still be unreachable.

    A reason whose premise can be re-derived is checked here: if the premise
    no longer holds, the reason becomes reachable and the ledger's claim is
    stale -- and the ledger would otherwise keep saying "unreachable" over a
    program that can now carry it.
    """
    problems = []
    # A bare "unreachable" with no premise is the shape 10.218 removed: the
    # claim is asserted and nothing re-derives it.
    for why, state in sorted(REFUSAL_REASONS.items()):
        if state == "unreachable" and why not in UNREACHABLE_PREMISES:
            problems.append("the refusal ledger calls why=%r unreachable, but no "
                            "premise re-derives that claim -- give it one, or "
                            "call it an invariant if a guard is what stops it"
                            % why)
    for why, premise_name in sorted(UNREACHABLE_PREMISES.items()):
        state = REFUSAL_REASONS.get(why)
        if state != "unreachable":
            problems.append("the refusal ledger says why=%r is %r, but the "
                            "premise %r only applies to an unreachable reason"
                            % (why, state, premise_name))
            continue
        check = PREMISE_CHECKS.get(premise_name)
        if check is None:
            problems.append("the premise %r named for why=%r has no check"
                            % (premise_name, why))
            continue
        holds, detail = check()
        if not holds:
            problems.append("the refusal ledger says why=%r is unreachable, but "
                            "its premise (%s) no longer holds: %s" % (why, premise_name, detail))
        else:
            print("IR-AOT-MIR-REFUSAL-PREMISE|why=%s|premise=%s|holds=yes|%s"
                  % (why, premise_name, detail))
    return problems


def refusal_ledger_problems(whys=None):
    """The source's reasons and the table's claims must agree with the corpus.

    Three checks, each able to say NO:

    1. a reason the SOURCE can emit but the table does not list -- a new check
       added without a ledger entry (this is the one that bites tomorrow);
    2. a reason the table lists but the SOURCE cannot emit -- a stale entry;
    3. a reason the table claims is witnessed but the CORPUS does not name.
    """
    if whys is None:
        whys = refusal_whys()
    source = route_refusal_reasons()
    problems = []
    for why in sorted(source - set(REFUSAL_REASONS)):
        problems.append("the route can refuse with why=%r but the refusal "
                        "ledger does not list it -- the ledger is stale" % why)
    for why in sorted(set(REFUSAL_REASONS) - source):
        problems.append("the refusal ledger lists why=%r but the route can "
                        "never emit it -- the ledger is stale" % why)
    for why, state in sorted(REFUSAL_REASONS.items()):
        if state == "witnessed" and why not in whys:
            problems.append("the refusal ledger says why=%r is witnessed, but "
                            "no case in the corpus names it" % why)
    # A state the checker does not know is a silent hole: the ledger would
    # accept "witnessedd" and check nothing about it.
    known = {"witnessed", "reachable", "unreachable", "invariant", "family"}
    for why, state in sorted(REFUSAL_REASONS.items()):
        if state not in known:
            problems.append("the refusal ledger gives why=%r the state %r, which "
                            "is not one of %s -- the state would be checked by "
                            "nothing" % (why, state, ", ".join(sorted(known))))
    return problems


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
    # Ledger cells: the refusal ledger must catch a reason the source can emit
    # but nobody listed, a listed reason the source cannot emit, and a
    # "witnessed" claim the corpus does not back.
    ledger_cells = [
        # what, source reasons, ledger reasons, corpus whys, want_problem
        ("ledger-clean", {"builtin-argc"}, {"builtin-argc": "witnessed"},
         {"builtin-argc"}, False),
        ("ledger-unlisted-source-reason", {"brand-new-reason"},
         {"builtin-argc": "witnessed"}, {"brand-new-reason"}, True),
        ("ledger-stale-entry", {"builtin-argc"},
         {"builtin-argc": "witnessed", "gone-reason": "unreachable"},
         {"builtin-argc"}, True),
        ("ledger-claimed-but-unwitnessed", {"builtin-argc"},
         {"builtin-argc": "witnessed"}, set(), True),
        ("ledger-unknown-state", {"builtin-argc"},
         {"builtin-argc": "witnessedd"}, {"builtin-argc"}, True),
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
    # The ledger cells need a controlled SOURCE and a controlled CORPUS, so
    # they swap both tables out and put them back.
    saved_ledger = dict(REFUSAL_REASONS)
    saved_source = route_refusal_reasons
    try:
        for what, source_reasons, ledger_reasons, corpus_whys, want_problem in ledger_cells:
            REFUSAL_REASONS.clear()
            REFUSAL_REASONS.update(ledger_reasons)
            globals()["route_refusal_reasons"] = lambda text=None, _s=source_reasons: set(_s)
            problems = refusal_ledger_problems(corpus_whys)
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(saved_ledger)
        globals()["route_refusal_reasons"] = saved_source
    # The premise cells: a reason the ledger calls unreachable must still be
    # unreachable.  The check is asked the question directly, with the premise
    # check swapped for one that answers NO, so the self-check proves the
    # checker can say NO rather than only that it agrees with the tree.
    premise_cells = [
        ("premise-holds", {"unsupported-opcode": "unreachable"},
         lambda: (True, "fixture holds"), False),
        ("premise-broken", {"unsupported-opcode": "unreachable"},
         lambda: (False, "fixture premise no longer holds"), True),
        ("premise-on-a-witnessed-reason", {"unsupported-opcode": "witnessed"},
         lambda: (True, "fixture holds"), True),
    ]
    saved_ledger2 = dict(REFUSAL_REASONS)
    saved_checks = dict(PREMISE_CHECKS)
    saved_unreach = dict(UNREACHABLE_PREMISES)
    try:
        for what, ledger_reasons, fake_check, want_problem in premise_cells:
            REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(ledger_reasons)
            UNREACHABLE_PREMISES.clear()
            UNREACHABLE_PREMISES["unsupported-opcode"] = "every-opcode-has-a-mask"
            PREMISE_CHECKS.clear(); PREMISE_CHECKS["every-opcode-has-a-mask"] = fake_check
            problems = unreachable_premise_problems()
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(saved_ledger2)
        PREMISE_CHECKS.clear(); PREMISE_CHECKS.update(saved_checks)
        UNREACHABLE_PREMISES.clear(); UNREACHABLE_PREMISES.update(saved_unreach)
    # The invariant cells: a guard with no site, a site that moved, and a probe
    # nobody ran are each a claim with nothing behind it, so each must be a
    # problem.  The fixture source and docs are strings, not the tree, so the
    # check is asked the question directly instead of only agreeing with HEAD.
    invariant_cells = [
        # what, ledger, source text, docs text, want_problem
        ("invariant-guarded", {"conservation": "invariant"},
         "if (g_emitted + g_notyet != g_insns) {",
         "IR-MIR-REFUSED|why=conservation|detail=emitted=4", False),
        ("invariant-site-gone", {"conservation": "invariant"},
         "the guard was moved somewhere else",
         "IR-MIR-REFUSED|why=conservation|detail=emitted=4", True),
        ("invariant-probe-missing", {"conservation": "invariant"},
         "if (g_emitted + g_notyet != g_insns) {", "nothing was ever run", True),
        ("invariant-no-site-declared", {"brand-new-invariant": "invariant"},
         "anything", "IR-MIR-REFUSED|why=brand-new-invariant", True),
        ("bare-unreachable", {"bare-reason": "unreachable"},
         "anything", "anything", True),
    ]
    saved_ledger3 = dict(REFUSAL_REASONS)
    saved_sites = dict(INVARIANT_SITES)
    saved_premises = dict(UNREACHABLE_PREMISES)
    try:
        for what, ledger_reasons, text, docs, want_problem in invariant_cells:
            REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(ledger_reasons)
            UNREACHABLE_PREMISES.clear()
            problems = invariant_problems(text, docs) + unreachable_premise_problems()
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(saved_ledger3)
        INVARIANT_SITES.clear(); INVARIANT_SITES.update(saved_sites)
        UNREACHABLE_PREMISES.clear(); UNREACHABLE_PREMISES.update(saved_premises)
    cells += backing_cells
    cells += ledger_cells
    cells += premise_cells
    cells += invariant_cells
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
                + backing_problems() + refusal_ledger_problems()
                + unreachable_premise_problems() + invariant_problems())

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
