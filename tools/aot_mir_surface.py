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
# each.  A row using an undeclared class is a boundary with no stated meaning;
# a declared class with no row is a class nobody can read, and it has to be
# NAMED as dormant rather than merely sit in the table (see DORMANT_CLASSES).
CLASSES = {
    "another-thread": "unreachable by construction: this route accepts no thread section",
    "measurement": "the observable is a stop position, not a value",
    "retired": "the interpreter no longer dispatches it",
}


# CLASSES THAT HOLD NO ROW TODAY, EACH WITH THE REASON IT IS KEPT.
#
# The docstring above used to claim CLASSES "is exactly the set BOUNDARY uses"
# and only the `BOUNDARY - CLASSES` direction was ever coded.  The other
# direction was prose.  That is the failure mode this file warns about
# elsewhere in its own words -- "an exemption nobody declared is one nobody
# decided on", and a class nobody uses is a segment of mechanism that never
# runs -- so the set is now asserted in BOTH directions and the unused ones are
# listed here on purpose:
#
#   measurement  OP_DECLARE left this class when §10.208 emitted it, so the
#                class has no members.  The word is still the right one for a
#                measurement-shaped observable, and keeping the entry means the
#                next such opcode does not have to reinvent the vocabulary.
#                §10.210's boundary note records the same decision, together
#                with what deleting it would cost (the three-class narrative in
#                §10.192 becomes a two-class one).
#
# Deleting a class from CLASSES now reds the reverse assertion unless it is
# removed here in the same edit, and a class added here without losing its rows
# reds too: dormant is a claim about today's boundary, not a label to park a
# class in.  CLASS_BACKING for a dormant class is checked as well -- an entry
# that names a `why` the corpus has dropped is the same unfalsifiable
# assertion whether or not any row happens to lean on it.
DORMANT_CLASSES = {
    "measurement": "OP_DECLARE was emitted by §10.208, so the class has no row;"
                   " the word is kept for the next measurement-shaped observable",
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
#
# AN "unreachable" REASON CANNOT BE WITNESSED, so it is not a backing claim.
# `unsupported-opcode` is state `unreachable` in REFUSAL_REASONS by §10.223's own
# premise: no program the route accepts reaches it.  Demanding a corpus case for
# it asks the corpus for a program that cannot exist, which is an assertion no
# run can ever satisfy -- and `measurement` carried exactly that demand from the
# day it lost its only row (§10.208) until the dormancy assertion below gave it
# something to fail against.  A dormant class therefore names None: it has no
# members, so it demonstrates nothing, and pretending otherwise is the
# unfalsifiable shape this file exists to prevent.
CLASS_BACKING = {
    "another-thread": "threads",
    "measurement": None,
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
#   "invariant"     -- reached only by breaking an invariant the emitter keeps:
#                       the site must still be in the source and the docs must
#                       carry a run that made it fire (see INVARIANT_SITES)
#
# UPDATE (10.221): the "family" state is GONE.  It stood for "the default name
# of g_prescan_why, replaced by a named reason before any check can fail" --
# which is an escape hatch: a check nobody named yet still refuses with a name
# the ledger claimed no program could carry.  Measured, `say N` carries exactly
# that name (the preset set globals).  The default is still the default in the
# source; what changed is that the ledger no longer gets to call it
# un-carryable.
REFUSAL_REASONS = {
    "builtin-argc": "witnessed",
    "builtin-call": "witnessed",
    "builtin-stale-stack": "witnessed",
    "declare-limit": "witnessed",
    # UPDATE (10.221): this was recorded as a FAMILY -- "the default name,
    # replaced by a named check before anything can fail".  That was wrong: it
    # is a reason a PROGRAM carries.  The preset set globals (N, Z, Z+, Z-,
    # Float1..Float9, float1..float9) hold set objects and this subset cannot
    # build one, so `say N` refuses with why=illegal-operand.  The corpus row
    # mir_preset_set_global_is_refused witnesses it.
    # UPDATE (10.222): illegal-operand was the DEFAULT name of g_prescan_why,
    # so every unnamed check in the pre-scan reported under it.  Nine such
    # sites existed; each now names its own reason, and this entry is the
    # reason for the preset set globals (N, Z, Z+, Z-, Float1..Float9,
    # float1..float9), which hold set objects this subset cannot build.
    # The corpus row mir_preset_set_global_is_refused witnesses it.
    "illegal-operand": "invariant",
    # UPDATE (10.222): the four reasons the unnamed checks now carry.  Each is
    # a guard the emitter keeps -- the compiler never produces an out-of-range
    # register, a negative operand count or an underflowing argument stack --
    # so a program cannot carry them; breaking the guard makes them fire
    # (probes in STATUS 10.222).  They are therefore invariants, not
    # "unreachable by construction": the site is what stops them.
    "operand-count-out-of-range": "invariant",
    "register-out-of-range": "invariant",
    "argument-stack-underflow": "invariant",
    # UPDATE (10.222): the preset set globals got a name of their own, so the
    # reason a program can actually carry is no longer the catch-all.
    "preset-set-global": "witnessed",
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
#     no mask for.  §10.223 measured that the premise this entry used to name --
#     "op_regs and the emitter's case labels cover the same set" -- does not
#     argue THAT claim: two sets agreeing is what keeps a MASKED opcode out of
#     emit_one's default arm, i.e. it is what makes emit-failed an invariant
#     (§10.220).  The claim the ledger needs is about the UNMASKED opcodes: they
#     are the seven the compiler emits for a thread section, and `build_nodes`
#     refuses a thread section with why=threads before `prescan` ever reads an
#     instruction -- so the not-yet path needs a program that cannot be built.
#     The premise below re-derives that subset relation; the corpus backing for
#     the class it leans on is CLASS_BACKING, checked by backing_problems().
UNREACHABLE_PREMISES = {
    "unsupported-opcode": "every-unmasked-opcode-is-a-thread-opcode",
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
    # UPDATE (10.222): the four reasons the pre-scan's formerly unnamed
    # checks now carry.  Each site is a guard the compiler's own output can
    # never violate, so nothing but a broken emitter reaches it.
    "register-out-of-range": 'g_prescan_why = "register-out-of-range";',
    "operand-count-out-of-range": 'g_prescan_why = "operand-count-out-of-range";',
    "argument-stack-underflow": 'g_prescan_why = "argument-stack-underflow";',
    # illegal-operand is the DEFAULT name of g_prescan_why: the site below is
    # the declaration itself, so a check that stops naming its own reason
    # falls back to it again.
    "illegal-operand": 'static const char *g_prescan_why = "illegal-operand";',
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
        # A BLANK SNIPPET IS NOT A SITE.  `"" in text` and `" " in text` are both
        # True for every file, so a fragment that is empty or whitespace would
        # make this check pass without reading the source at all -- the same
        # vacuous shape as a dormant entry with no reason (§10.225), one table
        # over.  Measured on the §10.225 tree: "" and " " both passed, "\t" red
        # only because tab-indented source happens to lack that exact run.
        if not isinstance(site, str) or not site.strip():
            problems.append(
                "the ledger calls why=%r an invariant, but the site declared for "
                "it is blank -- a snippet that matches every file proves nothing "
                "about this one" % why)
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

    WHAT THIS ACTUALLY COMPARES, measured by an injection (§10.223): the set of
    case labels inside `op_regs` against the set of case labels ANYWHERE in the
    file -- and `op_regs` is part of "anywhere", so a mask added there with no
    other arm appears in both sets and this stays green.  The direction it does
    catch is an ARM WITHOUT A MASK (the emitter knows an opcode `op_regs` would
    never let through).  The converse -- a mask with no arm -- is caught by
    coverage_problems() instead, because that check counts any case label as
    emission and calls the boundary row stale.

    So this premise is narrower than its old docstring claimed, and §10.223 took
    it out of UNREACHABLE_PREMISES for that reason: it never argued that
    why=unsupported-opcode is unreachable.
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


def unmasked_thread_only_problems(members, masks, boundary):
    """Is every opcode without a mask one of the thread-section opcodes?

    Factored out of `unmasked_thread_premise` so the self-check can hand it a
    fixture instead of the tree: a control that cannot say NO is not evidence,
    and this has to be able to say NO independently of today's source.
    """
    unmasked = [op for op in members if op not in masks]
    problems = []
    for op in unmasked:
        row = boundary.get(op)
        cls = row[0] if row else None
        if cls != "another-thread":
            problems.append(
                "the emitter has no mask for %s, and the boundary classifies it "
                "as %r -- why=unsupported-opcode is only unreachable while EVERY "
                "unmasked opcode belongs to the class the route refuses outright"
                % (op, cls if row else "nothing at all"))
    return unmasked, problems


def unmasked_thread_premise():
    """Re-derive the argument behind why=unsupported-opcode being unreachable.

    (See the note above UNREACHABLE_PREMISES.)  Two facts compose: the unmasked
    opcodes are exactly the thread-section opcodes, and a program carrying one
    is refused with why=threads before any instruction is looked at.  This
    checks the first half; the second is a corpus row (CLASS_BACKING).
    """
    src = EMITTER.read_text(errors="replace")
    i = src.find("static int op_regs(int op) {")
    if i < 0:
        return False, "op_regs is gone from the emitter"
    j = src.find("\n}\n", i)
    masks = set(re.findall(r"case (OP_[A-Z0-9_]+)", src[i:j]))
    members = declared_opcodes(BYTECODE_H.read_text(errors="replace"))
    unmasked, problems = unmasked_thread_only_problems(members, masks, BOUNDARY)
    if problems:
        return False, problems[0]
    return True, ("%d of %d opcodes carry no mask, and every one of them is a "
                  "thread-section opcode (%s)"
                  % (len(unmasked), len(members), ", ".join(unmasked)))


PREMISE_CHECKS = {
    # The claim the ledger's `unreachable` row leans on (§10.223).
    "every-unmasked-opcode-is-a-thread-opcode": unmasked_thread_premise,
    # Kept, and run even though no ledger row names it any more: it is what
    # keeps a MASKED opcode out of the emitter's default arm, i.e. the reason
    # why=emit-failed stays an invariant (§10.220).  §10.223 took it out of
    # UNREACHABLE_PREMISES because it never argued the unsupported-opcode claim,
    # and left it here because it still argues another one.
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
    # A premise that no ledger row names any more is still RUN: PREMISE_CHECKS
    # is the set of structural facts this checker keeps, and one of them guards
    # an invariant (mask/arm agreement -> emit-failed) rather than an
    # unreachable row.  It is printed under its own name so a failure cannot be
    # read as a change in the unsupported-opcode argument.
    named = set(UNREACHABLE_PREMISES.values())
    for name, check in sorted(PREMISE_CHECKS.items()):
        if name in named:
            continue
        holds, detail = check()
        if holds:
            print("IR-AOT-MIR-REFUSAL-PREMISE|why=-|premise=%s|holds=yes|%s"
                  % (name, detail))
        else:
            problems.append("the structural premise %r no longer holds: %s"
                            % (name, detail))
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
    known = {"witnessed", "reachable", "unreachable", "invariant"}
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
    # Dormant classes are checked too: a CLASS_BACKING entry is a claim about the
    # corpus, and it goes stale on the corpus's schedule, not on the boundary's.
    # Skipping it would leave exactly one entry in this table unfalsifiable --
    # the shape §10.210 flagged when it said entries are kept "so the next
    # measurement-shaped opcode need not reinvent the vocabulary".
    classes = {row[0] for row in BOUNDARY.values()} | set(DORMANT_CLASSES)
    for cls in sorted(classes):
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
    # The OTHER direction: a declared class no row uses.  Undeclared dormancy is
    # how CLASSES silently becomes a museum -- the class still reads like a
    # live vocabulary, nothing exercises it, and no check ever notices.
    used_classes = {BOUNDARY[op][0] for op in BOUNDARY}
    dormant_unlisted = sorted(set(CLASSES) - used_classes - set(DORMANT_CLASSES))
    if dormant_unlisted:
        problems.append(
            "CLASSES declares %s but no boundary row uses it, and it is not in "
            "DORMANT_CLASSES -- a class nobody uses is a class nobody can read, "
            "so either give it a row or say why it is kept"
            % ",".join(dormant_unlisted))
    dormant_with_rows = sorted(set(DORMANT_CLASSES) & used_classes)
    if dormant_with_rows:
        problems.append(
            "DORMANT_CLASSES lists %s but the boundary still uses it -- dormant "
            "is a claim about today's rows, not a place to park a live class"
            % ",".join(dormant_with_rows))
    unknown_dormant = sorted(set(DORMANT_CLASSES) - set(CLASSES))
    if unknown_dormant:
        problems.append(
            "DORMANT_CLASSES names %s, which is not in CLASSES"
            % ",".join(unknown_dormant))
    # WHY IT IS KEPT IS THE WHOLE POINT.  A dormant entry exists so that somebody
    # has to say why a class with no rows is still here; an empty or blank reason
    # satisfies the key without satisfying the obligation, which is the same
    # silent museum in a new shape.  Measured on the §10.224 tree: blanking
    # `measurement`'s reason left every check green.
    silent_dormant = sorted(
        cls for cls, why in DORMANT_CLASSES.items()
        if not isinstance(why, str) or why.strip() == "")
    if silent_dormant:
        problems.append(
            "DORMANT_CLASSES gives %s no reason -- keeping a class with no rows "
            "is a decision that has to be written down, or the entry is just a "
            "way to switch the reverse assertion off"
            % ",".join(silent_dormant))
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
    # Dormancy cells: the reverse direction of the CLASSES assertion.  Each
    # fixture below is one edit away from an honest table, and each is a shape
    # the checker must be able to refuse -- otherwise "declared but unused"
    # stays prose, which is exactly how `measurement` sat unchecked.
    dormant_cells = [
        # name, boundary, dormant, want_problem
        ("dormant-declared", good, {"another-thread", "measurement"}, False),
        ("dormant-unlisted", good, {"measurement"}, True),
        ("dormant-but-live", dict(good, **{
            "OP_A": ("another-thread", "src/compiler/bytecode.h", "typedef enum", "fixture"),
        }), {"another-thread", "measurement"}, True),
        ("dormant-unknown", good, {"another-thread", "measurement", "no-such-class"}, True),
        # A reason is the obligation, not the key.  Blanking it must red: an
        # entry without a reason is a way to switch the reverse assertion off.
        ("dormant-blank-reason", good, {"another-thread", "measurement"}, True,
         {"measurement": "   "}),
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
    saved_dormant = dict(DORMANT_CLASSES)
    try:
        # The coverage cells ask about the closure and attestation of the
        # boundary, not about dormancy, so dormancy is neutralised here: every
        # class no cell uses is declared dormant, which keeps the reverse
        # assertion from reporting on the fixture.  Deriving the set from the
        # cell table rather than listing it by hand means a new cell cannot
        # silently reintroduce the interference -- a class it DOES use stays
        # live, so a correct fixture cannot trip dormant-but-live.
        used_in_cells = {row[0] for _, t, _ in cells for row in t.values()}
        DORMANT_CLASSES.clear()
        DORMANT_CLASSES.update({k: "self-check fixture" for k in set(CLASSES) - used_in_cells})
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
        DORMANT_CLASSES.clear(); DORMANT_CLASSES.update(saved_dormant)
    # Dormancy cells: each is one edit away from an honest table.
    try:
        for cell in dormant_cells:
            what, table, dormant, want_problem = cell[:4]
            overrides = cell[4] if len(cell) > 4 else {}
            BOUNDARY.clear(); BOUNDARY.update(table)
            DORMANT_CLASSES.clear()
            DORMANT_CLASSES.update(
                {k: overrides.get(k, "self-check fixture") for k in dormant})
            problems = coverage_problems(members, missing)
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        BOUNDARY.clear(); BOUNDARY.update(saved)
        DORMANT_CLASSES.clear(); DORMANT_CLASSES.update(saved_dormant)
    try:
        # The backing cells ask about ONE class, so dormancy is neutralised: with
        # a real dormant class still declared, every verdict here would also be
        # reporting on that class's corpus reason, not on the fixture's.
        DORMANT_CLASSES.clear()
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
        DORMANT_CLASSES.clear(); DORMANT_CLASSES.update(saved_dormant)
    # The unmasked-subset premise (§10.223): "no mask" and "another-thread
    # class" are kept as separate facts, so a cell can fail on either the set
    # that loses a mask or the class that claims the wrong reason.
    unmasked_cells = [
        ("unmasked-all-thread", ["OP_T1", "OP_T2"], set(),
         {"OP_T1": ("another-thread",), "OP_T2": ("another-thread",)}, False),
        ("unmasked-a-class-that-can-be-reached", ["OP_T1", "OP_X"], set(),
         {"OP_T1": ("another-thread",), "OP_X": ("retired",)}, True),
        ("unmasked-a-thread-row-reclassified", ["OP_T1"], set(),
         {"OP_T1": ("measurement",)}, True),
    ]
    for what, members_f, masks_f, table, want_problem in unmasked_cells:
        _unmasked, problems = unmasked_thread_only_problems(
            members_f, masks_f, table)
        hit = bool(problems) == want_problem
        print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
              % (what, len(problems), "one-or-more" if want_problem else "clean",
                 "ok" if hit else "WRONG"))
        ok &= hit

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
        # A blank snippet is True for every file, so it would pass without
        # reading the source.  Same vacuous shape as a dormant entry with no
        # reason (§10.225), one table over.
        ("invariant-blank-site", {"conservation": "invariant"},
         "if (g_emitted + g_notyet != g_insns) {",
         "IR-MIR-REFUSED|why=conservation|detail=emitted=4", True, ""),
        ("bare-unreachable", {"bare-reason": "unreachable"},
         "anything", "anything", True),
    ]
    saved_ledger3 = dict(REFUSAL_REASONS)
    saved_sites = dict(INVARIANT_SITES)
    saved_premises = dict(UNREACHABLE_PREMISES)
    try:
        for cell in invariant_cells:
            what, ledger_reasons, text, docs, want_problem = cell[:5]
            blank_site = cell[5] if len(cell) > 5 else None
            REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(ledger_reasons)
            UNREACHABLE_PREMISES.clear()
            if blank_site is not None:
                INVARIANT_SITES.clear()
                INVARIANT_SITES["conservation"] = blank_site
            else:
                INVARIANT_SITES.clear(); INVARIANT_SITES.update(saved_sites)
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
    # The family state is retired (10.221): a ledger that says "no program can
    # carry this name" must not be able to say it for a reason a program DOES
    # carry.  This cell says NO to the retired spelling.
    family_cells = [
        ("ledger-family-state-retired", {"builtin-argc": "family"},
         {"builtin-argc"}, True),
    ]
    saved_ledger4 = dict(REFUSAL_REASONS)
    saved_source2 = route_refusal_reasons
    try:
        for what, ledger_reasons, corpus_whys, want_problem in family_cells:
            REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(ledger_reasons)
            globals()["route_refusal_reasons"] = lambda text=None, _s={"builtin-argc"}: set(_s)
            problems = refusal_ledger_problems(corpus_whys)
            hit = bool(problems) == want_problem
            print("IR-AOT-MIR-SURFACE-SELFCHECK|what=%s|problems=%d|want=%s|%s"
                  % (what, len(problems), "one-or-more" if want_problem else "clean",
                     "ok" if hit else "WRONG"))
            ok &= hit
    finally:
        REFUSAL_REASONS.clear(); REFUSAL_REASONS.update(saved_ledger4)
        globals()["route_refusal_reasons"] = saved_source2
    cells += dormant_cells
    cells += backing_cells
    cells += ledger_cells
    cells += premise_cells
    cells += invariant_cells
    cells += family_cells
    cells += unmasked_cells
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
