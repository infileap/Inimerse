#!/usr/bin/env python3
"""Does the second front end -- C generated from IrInsn -- agree with the first?

aot_native lowers an AST.  aot_mir lowers the MIR (src/compiler/mir.h) that the
compiler already builds and then throws away.  Both end in a C translation unit
that embeds the same value runtime, so the interesting question is not "does it
compile" but "on the programs the AST route accepts, does the MIR route reach
the same answers, and if the two files differ, can every difference be named?"

Four properties, checked in this order, because each one is the premise of the
next:

  ACCEPTED     aot-native accepted it, so the program is inside the numeric
               subset.  If aot-mir refuses such a program, that is reported by
               name (IR-AOT-MIR-REFUSED carries mir's own why= and n=), never
               swallowed: a silent refusal reads as coverage.
  EXECUTED     both native binaries print the same bytes and exit the same way
               as the interpreter.  The interpreter is the oracle; no expected
               output is written down here.
  CONSERVED    every file aot-mir wrote carries IR-MIR-CONSERVE with holds=yes:
               emitted + not-yet == insns, counted from the module's own
               instruction count and the emitter's own appends.  A file whose
               equation is red is a failure even if it compiled.
  NAMED DIFF   the two C files are compared byte for byte.  A difference must
               fall inside this closed set, and each reason is backed by a
               mechanical check on the files themselves:

                 identical                                no difference
                 mir-register-shape                       the byte-identical
                     prefix runs through the whole shared value runtime, and the
                     first difference lies in how containers and instructions
                     are spelled: the AST route has named locals and structured
                     statements, the MIR route has a flat MirVal v[] and labels.
                 mir-register-shape+mir-internal-globals  the same, and the two
                     global declaration sets differ, because the compiler keeps
                     globals of its own (a repeat counter, say) that no AST ever
                     names and the MIR faithfully carries.

               A difference that cannot be put in one of those buckets is a
               failure, not a finding: --self-check exercises exactly that path
               on synthetic pairs, including one whose shared prefix stops
               before the value runtime.

The negative control is the perturbed operand.  aot_mir dump lists the module's
instructions; the test picks LOADK_INT instructions in main, changes one operand
through mir_perturb(), and requires that the generated C text disappears from
the old bytes AND that at least one such change moves the program's stdout.  A
generator that ignores its operands passes every other check in this file.

Readings:
  IR-MIR-C|case=NAME|ast=accepted|mir=accepted|cc=ok|exec=same
  IR-MIR-C-SAME|case=NAME|bytes=N|reason=identical
  IR-MIR-C-DIFF|case=NAME|len=N|first-diff=line N|reason=...
  IR-MIR-REFUSED|case=NAME|why=...|not-yet=OP_X:count,...
  IR-MIR-EXEC-DIFF|case=NAME|side=mir|ast|interp|rc=..|stdout=...
  IR-MIR-PERTURB|case=NAME|func=F|idx=I|field=2|was=V|now=W|c-changed=yes|stdout-changed=yes
  IR-MIR-LAYOUT-ASSERT|files=N|need=6|files-complete=N|missing=(NAME,NAME)|
              basis=the-six-_Static_assert-lines-kMirValBlock-writes-into-every
              -generated-file-read-off-the-emitted-text
  IR-MIR-LAYOUT-ASSERT-MISS|case=NAME|missing=(NAME,NAME)
  IR-MIR-MIR-ONLY|case=NAME|mir=accepted|cc=ok|exec=same|diff|rc=..|bytes=N|stdout-hex=...
      Programs the AST route cannot lower (a string expression is refused there),
      so they cannot be shared cases; they are compared with the interpreter
      BYTE for byte and the hex rides on the line, because a string case is
      where a byte difference plausibly hides.
  IR-MIR-COVER|corpus=inline-cases:N|accepted=N|refused=N|differ=N|conserved=N
              |c-identical=N|c-diff=N|reasons=identical:N,mir-register-shape:N,...
              |perturbed=N|perturb-c-changed-failures=N|perturb-stdout-changes=N
              |mir-only-cases=N|mir-only-same-as-interpreter=N
  IR-MIR-VERDICT|PASS|FAIL|...

The layout-assertion reading is the measurement of a promise the previous
commit could only state: every generated file carries the six assertions that
hold MirVal to Value's layout, and the test now reads them off the file it
already compiled instead of trusting the template.  `need=6` rides on the line
so a reader can see the denominator without opening the source, and a missing
assertion is named (never a bare pattern-not-found), because "the check is
broken" and "the emitter dropped offsetof(sval)" must not read the same.

Exit 0 when every accepted program agrees, every generated file conserves, every
difference is in the closed set and the control fired; 1 when any of those is
false (each one named); 2 when a prerequisite is missing.

Usage:
    python3 tools/aot_mir.test.py [--build build] [--max N] [--all] [--self-check] [-v]
"""
import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ROOT / "tools" / "aot_native.test.py"

# The closed set.  Difference classification may return nothing else.
DIFF_REASONS = ("identical", "mir-register-shape", "mir-register-shape+mir-internal-globals")

# A byte-identical prefix only proves the two files share a runtime if it
# actually contains that runtime.  These markers are lines of kPreamble, handed
# to aot_mir through aot_native_preamble() so there is one copy of it.
PREAMBLE_MARKERS = ("nv_die_division_by_zero", "nv_fmt_double", "static void nv_say(NV a)")

# Each route declares its globals with its own register type: the AST route
# `static NV`, the MIR route `static MirVal` (kMirValBlock in aot_mir.c, which
# is byte-for-byte the interpreter's Value).  The comparison below is about
# which NAMES each route carries, so it has to read both spellings -- while it
# only read `NV`, the MIR route's globals were invisible to it and the
# compound bucket below could no longer be attributed to a real set
# difference.  Reading both is not a tolerance: MV_INT and NV_INT are both 0,
# so the two initializer spellings denote the same value.  The MIR spelling then
# gained a second initializer -- MV_NIL, because a global read before its first
# store is nil and not the integer 0 -- which is a difference in VALUE and not
# in NAME, so it is admitted here: this regex answers "which names" and must
# keep answering it when the initializer changes, or the globals it filters out
# become invisible again and the bucket silently loses a case.
GLOBAL_DECL = re.compile(r"^static (?:NV|MirVal) (g_\w+) = \{ (?:NV_INT|MV_INT|MV_NIL), 0, 0 \};$")
CODEGEN = re.compile(r"^IR-MIR-CODEGEN[|](.*)$")
CONSERVE = re.compile(r"^IR-MIR-CONSERVE[|](.*)$")
NOTYET = re.compile(r"^IR-MIR-NOT-YET[|]op=(OP_[A-Z0-9_]+)[|]n=(\d+)$")
REFUSED = re.compile(r"^IR-MIR-REFUSED[|]why=([^|]*)[|]detail=(.*)$")
INSN = re.compile(r"^IR-MIR-INSN[|]func=(\d+)[|]idx=(\d+)[|]op=(OP_[A-Z0-9_]+)"
                  r"[|]r1=(-?\d+)[|]r2=(-?\d+)[|]r3=(-?\d+)[|]argc=(\d+)$")

# The six layout assertions kMirValBlock (src/compilation/aot_mir.c) writes into
# every generated file.  Until this check existed they were a PROMISE: nothing
# read a generated file looking for them, so deleting one -- or changing 32 to
# 24 -- left all 86 cases green, because the C still compiles and the
# interpreter is still right.  The negative control that proved they matter
# (`==32` -> `==24`, every case red on `static assertion failed`) was run by
# hand, once, and left no trace in the tree; a measurement nobody repeats is an
# anecdote, and the next commit to edit the template would not know.
#
# What this check has teeth for: an assertion ABSENT from the emitted text.  It
# is read off the artifact the compiler consumed (mir_text), not off the C
# source that generates it, so a template that stops emitting the block -- an
# `#if`, an early return, a block spliced into a preamble that swallows it --
# is caught where the promise is actually made.  The needle is the whole
# statement, message included, so an edit of the message is a named miss as
# well rather than a silent pass.
#
# What it does NOT cover: whether the assertions MEAN anything.  Six lines
# nobody compiles prove nothing; the teeth are in the compile step this test
# already runs on every case (`cc -O2`), so an assertion that stops holding
# stops the build.  Nor does it compare the text against src/vm/vm.h:
# `sizeof(Value) == 32` is asserted there by the interpreter's own
# `_Static_assert`, and a copy of that number here would be a second owner of
# one fact.  It also cannot tell a deleted assertion from a reworded one: the
# needle is the whole line, so an intentional rename reddens too.  That is the
# intended cost of naming the missing assertion instead of matching a loose
# shape -- a reworded assertion is a decision, and it should cost a diff here.
MIRVAL_ASSERTS = (
    ("sizeof(MirVal)==32",
     '_Static_assert(sizeof(MirVal) == 32,\n'
     '               "MirVal must be 32 bytes: Value\'s size (src/vm/vm.h:47)");'),
    ("offsetof(type)==0",
     '_Static_assert(offsetof(MirVal, type) == 0, "MirVal.type at Value.type\'s offset");'),
    ("offsetof(ival)==8",
     '_Static_assert(offsetof(MirVal, ival) == 8, "MirVal.ival at Value.ival\'s offset");'),
    ("offsetof(fval)==8",
     '_Static_assert(offsetof(MirVal, fval) == 8, "MirVal.fval shares ival\'s union slot,'
     ' as in Value");'),
    ("offsetof(sval)==16",
     '_Static_assert(offsetof(MirVal, sval) == 16, "MirVal.sval at Value.sval\'s offset");'),
    ("offsetof(ptr)==24",
     '_Static_assert(offsetof(MirVal, ptr) == 24, "MirVal.ptr at Value.ptr\'s offset");'),
)


def mirval_assert_problems(text):
    """Which of the six layout assertions a generated file is missing, by name.

    Names, not a boolean: the difference between "this check is broken" and
    "the emitter stopped carrying offsetof(sval)" is the whole value of the
    measurement, and a bare pattern-not-found cannot tell the two apart."""
    return [name for name, needle in MIRVAL_ASSERTS if needle not in text]


# The ALLOCATION reading (IR-MIR-ALLOCS), the one thing the byte criterion
# structurally cannot see.  tools/aot_mir.test.py compares stdout bytes and an
# exit code against the interpreter; a run that leaked every buffer it ever
# malloc'd, or freed one twice, satisfies both.  mv_free_all() in kMirValBlock
# therefore prints, on stderr at program exit, how many blocks the program was
# handed, how many the exit sweep released, how many survived it (must be 0)
# and how many bytes it requested over its whole life.
#
# The assertion is an EXACT (allocs, bytes) PAIR per case, not just live == 0.
# That is the part with teeth: `live` is computed as allocs - freed inside the
# one sweep that also computes it, so a site that mallocs WITHOUT registering
# would leave all three counters at 0 and report a perfectly clean reading --
# the leak this instrument exists to name would be the one leak it cannot see.
# An exact expected count turns that into a named red (case, expected, actual),
# and a second exact number (bytes) keeps the check honest about SIZE: a count
# alone cannot tell seven 4-byte buffers from seven 40-byte ones.
ALLOCS_RE = re.compile(r"^IR-MIR-ALLOCS\|allocs=(\d+)\|freed=(\d+)\|live=(\d+)\|bytes=(\d+)$",
                       re.M)

# case name -> (allocs, bytes), the numbers the criterion DEMANDS.  A case not
# listed must allocate nothing, which is the reading for every case in this list
# that touches no string `+`.  mir_add_string_concat's 7 blocks are the seven
# concatenations its source performs, and its 33 bytes are their la + lb + 1
# sizes at the lengths that program actually reaches: (2+2+1) + (2+1+1) +
# (1+2+1) + (2+2+1) + (2+0+1) + (2+2+1) + (4+2+1) = 5+4+4+5+3+5+7.
#
# mir_concat_chain's 6 blocks are its SIX chains, one block each -- that is the
# whole point of the fold, and it is where a pairwise fold would differ without
# one byte of stdout changing: (3-1) + (11-1) + (4-1) + (3-1) + (3-1) + (3-1) =
# 21 blocks, not 6.  The 66 bytes are the la+...+1 sums at the lengths that
# program actually reaches: (2+2+2+1) + (2+1+2+1+2+1+2+1+2+1+2+1) + (2+2+0+2+1)
# + (2+2+2+1) + (1+6+0+1) + (6+6+6+1) = 7+18+7+7+8+19.
ALLOCS_EXPECT = {
    "mir_add_string_concat": (7, 33),
    "mir_concat_chain": (6, 66),
}
ALLOCS_DEFAULT = (0, 0)


def allocs_problems(text, case):
    """Which ways this run's allocation reading disagrees with the criterion.

    Named, not a boolean: the difference between "the instrument never
    reported", "a block survived the sweep" and "the program allocated a
    different number of blocks than the criterion demands" is the whole value
    of the measurement, and a bare pass/fail cannot tell the three apart."""
    m = ALLOCS_RE.search(text or "")
    want_allocs, want_bytes = ALLOCS_EXPECT.get(case, ALLOCS_DEFAULT)
    if not m:
        return ["%s: the generated program printed no IR-MIR-ALLOCS line, so this run measured "
                "no allocation and no lifetime (the criterion demands allocs=%d|bytes=%d)"
                % (case, want_allocs, want_bytes)]
    allocs, freed, live, nbytes = (int(g) for g in m.groups())
    out = []
    if live != 0:
        out.append("%s: LEAK -- live=%d block(s) survived the exit sweep (allocs=%d, freed=%d, "
                   "%d byte(s) requested in all); the criterion says the sweep must leave none"
                   % (case, live, allocs, freed, nbytes))
    if allocs != want_allocs or nbytes != want_bytes:
        out.append("%s: allocation count off -- got allocs=%d|bytes=%d, the criterion demands "
                   "allocs=%d|bytes=%d (a site that mallocs without registering reports 0|0)"
                   % (case, allocs, nbytes, want_allocs, want_bytes))
    return out


# Programs the AST route cannot lower AT ALL, so they cannot join the shared
# corpus: aot_native refuses a string expression ("expression kind 2 is outside
# the numeric subset"), and both this file's shared loop and the cover test
# require an AST file for every shared case.  They are still judged by the same
# oracle -- the interpreter -- and by the same EXECUTED property; only the
# AST-file comparison is inapplicable, because there is no second file.
#
# The list has ONE owner and the cover test imports it (tools/aot_mir_cover
# .test.py: load_mir_only()), so the parity case and the coverage case can never
# drift into being two different programs.
#
# The second case is the byte-level one: `\t` and `\n` are REAL bytes by the time
# the lexer finishes, so a lowering that put the two characters `\` `t` in the
# C string would print them and this case would notice.  The third exercises the
# escape writer's other arms in one line -- quote, backslash, a `?` that a C11
# translator may still read as a trigraph, and a byte >= 0x7f, which buf_c_string
# writes as octal so the generated literal stays pure ASCII and cannot be
# re-encoded on the way through the compiler.
#
# The other three are `nil`, and they are here for a reason that is the opposite
# of the string trio's.  `nil` is not a literal in this language -- there is no
# OP_LOADK_NIL and src/compiler/compiler.c has no case for the identifier -- so
# the ONLY way to reach it is an UNRESOLVED GLOBAL: `nil` and `nosuchglobal`
# both compile to OP_LOAD_GLOBAL with the index -1, and the interpreter answers
# VAL_NIL for that index (src/vm/vm.c L_LOAD_GLOBAL calls value_set(..., VAL_NIL, ...)).
# The AST route refuses those programs outright ("variable 'nosuchglobal' is
# used but never assigned"), so the value the interpreter has and the AST route
# has no name for is the one this route used to refuse at prescan -- a refusal
# that hid a DIFFERENCE instead of naming one.
#
# Why three and not one:
#   mir_nil_vs_zero          `x == nil` is TRUE and `x == 0` is FALSE for the
#                            same x.  A route that answered the unresolved
#                            global with the integer 0 -- which is exactly what
#                            the generated file used to initialise its globals
#                            with -- prints the two answers swapped.  The case is
#                            byte-compared against the interpreter AND prints its
#                            own stdout-hex, so it asserts the value rather than
#                            agreeing that something ran.
#   mir_nil_coalesce         `??` is the only form that REACHES OP_IS_NIL in a
#                            program this subset accepts.  The compiler emits it
#                            at four sites (src/compiler/compiler.c:648 `??`,
#                            :578 set comprehension, :1082 safe member `?.`,
#                            :1359 nested index assignment); the other three sit
#                            behind opcodes this subset cannot emit
#                            (OP_CALL_BUILTIN/OP_INDEX_GET/OP_NEW_ARRAY), so such
#                            a program is refused whole and never runs the opcode.
#                            "only `??` compiles to OP_IS_NIL" would be a claim
#                            a `grep -n OP_IS_NIL src/compiler/` of 4 contradicts.
#                            If the global were 0 the program prints 0 instead of 2.
#   mir_nil_print_truthiness reaches the three adapters a nil VALUE can enter
#                            besides comparison: mv_say (printing `nil`), mv_tru
#                            (an `if` on nil, which the interpreter's vm_truthy
#                            answers false) and mv_not (`not nil` is true).  Any
#                            of them handing nil to nv_* would abort instead of
#                            printing.
#
# The last three are strings on the COMPARISON side -- a different entry from
# the printing trio above, because printing a string was `mv_say`'s branch while
# comparing one is `mv_eq`/`mv_ne`'s.  Before they existed, `s = "hi"` /
# `t = "ho"` / `say s == t` passed prescan (OP_EQ is in the lowering table),
# compiled with cc, and then DIED AT RUN TIME with SIGABRT and empty stdout:
# `mv_eq` had no MV_STR arm, so `mv_as_nv(MV_STR)` reached `mv_tag_to_nv`'s
# `abort()`.  The interpreter answers `false`.  Accepted + compiled + crashed,
# while the answer was known -- that is the shape this list refuses to live with.
#
#   mir_loadk_string_eq       equal and unequal between two strings, both
#                             outcomes (`==` and `!=`).  Byte compared, so a
#                             route that compared pointers only, answered
#                             "not equal" always, or printed the operands would
#                             redden the same way a crash does.  The rule copied
#                             is src/vm/vm.c:417 (same-type string: pointer fast
#                             path, then strcmp with a NULL payload normalised
#                             to "").
#   mir_loadk_string_eq_number
#                             a string against a NUMBER.  The interpreter's
#                             val_eq falls through its type-dispatch
#                             (src/vm/vm.c:414) to the int/float/bool coercion
#                             block (:425-427) and then to `return false`
#                             (:428), so `s == 1` is false and `s != 1` is true.
#                             This is the case that stops the string arm from
#                             becoming a strcmp against a coerced 0, and the one
#                             that makes "dispatch on TYPE first, then compare
#                             CONTENT" a measured property rather than a rule in
#                             a comment.  Both an int and a float operand are in
#                             it: the coercion block is one rule, and a string
#                             arm that only knew ints would still be wrong.
#   mir_loadk_string_truthiness
#                             a string in the truth positions (`if`, `not`) --
#                             `mv_tru` had the SAME hole as `mv_eq`
#                             (`mv_as_nv(MV_STR)` -> abort), and vm_truthy's
#                             `default: return 1` (src/vm/vm.c:400) is the rule
#                             it must follow: every string is TRUE, including the
#                             empty one (measured: `not ""` is false and
#                             `if "" {}` takes the true branch).  It gets its own
#                             case rather than a line in the other two because
#                             an arm without a criterion is exactly the debt
#                             docs/STATUS.md 10.105 writes down.
#   mir_implicit_return
#                             a function that falls out of its end, a function
#                             with an EMPTY body, and a BARE `return` -- all
#                             three are one opcode shape: the compiler emits
#                             `OP_RETURN` with r1 == 0 (measured on
#                             `func n() { x = 1 }`: 6 instructions, the last
#                             `OP_RETURN r1=0`), and the interpreter answers nil
#                             for all three (src/vm/vm.c:4126-4127: the
#                             `ins.r1 > 0 && ins.r1 < FRAME_REGS` test is false,
#                             so the VAL_NIL branch runs).  The MIR route used
#                             to answer integer 0 here -- SILENTLY: `cc`
#                             succeeded, the binary exited 0, and the only
#                             visible difference was one stdout byte.  That is
#                             why this case is byte-compared like every other
#                             one: an exit code cannot see this hole.  Three
#                             shapes in one case rather than three cases, because
#                             they redden together and one decision fixes them;
#                             a case carrying only the empty body would not say
#                             that the non-empty body has the same hole.
#   mir_loadk_string_order
#                             the ORDERING pair on strings (`<`, `<=`, `>`,
#                             `>=`).  Same shape as the equality hole: prescan
#                             ACCEPTS it (OP_LT/OP_LE/OP_GT/OP_GE are all in the
#                             lowering table), cc compiles it, and the binary
#                             used to die with SIGABRT and EMPTY stdout while
#                             the interpreter answered true/false -- because
#                             mv_lt/mv_le/mv_gt/mv_ge handed
#                             mv_as_nv(MV_STR) to nv_*, and mv_tag_to_nv's
#                             `default: abort()` is a guard, not a capability.
#                             The rule copied is the interpreter's, and it is
#                             the SAME val_cmp the equality arm uses:
#                             src/vm/vm.c:443 says a same-type string pair is
#                             orderable, :449-451 compares pointer first and
#                             then strcmp with a NULL payload normalised to "".
#                             `strcmp(a,b) < 0` etc. is the whole answer -- no
#                             allocation, no new print exit.
#                             Variance, not one truth value: both directions
#                             (hi<ho true, ho<hi false), all four operators,
#                             and an EQUAL pair (s <= s / s >= s true, s < s /
#                             s > s false) so that the pointer fast path is
#                             exercised and not just content comparison.  A
#                             route that answered a constant, or only knew
#                             `<`, reddens here.
#                             NOT in the case, deliberately: a string against a
#                             non-string (`s < 1`).  The interpreter raises
#                             `uncaught: type_mismatch` (rc=1) and this route
#                             has no exception mechanism, so that stays an
#                             ABORTED boundary -- documented in the arm's debt
#                             note and in docs/STATUS.md 10.107.  Putting it
#                             here would demand a fake answer (silent false) in
#                             exchange for the crash, which is exactly what the
#                             ordering arm refuses to do.
MIR_ONLY = [
    ("mir_loadk_string", 's = "hi"\nsay s\n'),
    ("mir_loadk_string_escapes", 'say "a\\tb\\nc"\n'),
    ("mir_loadk_string_quotes", 'say "q\\"z \\\\ back ??= mid \u4e2d"\n'),
    ("mir_nil_vs_zero", 'say nosuchglobal == nil\nsay nosuchglobal == 0\n'),
    ("mir_nil_coalesce", 'say nosuchglobal ?? 2\n'),
    ("mir_nil_print_truthiness",
     'say nosuchglobal\nif nosuchglobal { say "T" } else { say "F" }\nsay not nosuchglobal\n'),
    ("mir_loadk_string_eq",
     's = "hi"\nt = "ho"\nsay s == t\nsay s == s\nsay s != t\nsay t != t\n'),
    ("mir_loadk_string_eq_number",
     's = "hi"\nsay s == 1\nsay s != 1\nsay s == 1.5\nsay s != 1.5\n'),
    ("mir_loadk_string_truthiness",
     's = "hi"\ne = ""\nif s { say "T" } else { say "F" }\n'
     'if e { say "T" } else { say "F" }\nsay not s\n'),
    ("mir_implicit_return",
     'func n() { x = 1 }\nsay n()\nfunc m() { }\nsay m()\n'
     'func b() { return }\nsay b()\n'),
    ("mir_loadk_string_order",
     's = "hi"\nt = "ho"\nsay s < t\nsay s <= t\nsay s > t\nsay s >= t\n'
     'say t < s\nsay t <= s\nsay t > s\nsay t >= s\n'
     'say s <= s\nsay s >= s\nsay s < s\nsay s > s\n'),
    # The first case in this list whose ANSWER DOES NOT EXIST WITHOUT THE HEAP.
    # `s + t` is a NEW buffer every time (the interpreter's L_ADD mallocs
    # la + lb + 1 and marks the result heap-owned with ival == 0), so this case
    # is also the first one the allocation reading has anything to say about:
    # ALLOCS_EXPECT demands 7 blocks and 33 bytes from it, and every other case
    # in this list must still report 0|0.
    #
    # Why each line is here, one by one:
    #   `s + t`       both operands resident, both variables -- the hole itself.
    #   `s + "x"`     a resident constant on the RIGHT; stops an arm that only
    #                 handles two globals.
    #   `"x" + s`     a resident constant on the LEFT; stops the same arm from
    #                 assuming the left operand is the variable.
    #   `s + s`       the same buffer twice; a concat that appended in place
    #                 would corrupt both operands instead of building a new one.
    #   `s + ""`      the empty resident string on the right; nothing may
    #                 short-circuit to `a` here, because a copy is what the
    #                 interpreter makes and ival would then mean two things.
    #   `u = s + t` / `say u`   the result survives a store into a global and a
    #                 later load -- i.e. it is a value, not a print-time trick.
    #   `say u + s`   a heap-owned string as an OPERAND of another concat.
    #   `say s` / `say t` after all of the above: concatenation does not mutate
    #                 its operands (the interpreter copies; an append-in-place
    #                 implementation would print something else here).
    # Mixed-type `+` (`s + 1`, `1 + "7"`) is deliberately NOT in this case: the
    # interpreter stringifies BOTH sides by one rule, which is a new class of
    # print exit and not part of this step; that boundary is written down in
    # docs/STATUS.md and stays an abort until it has its own criterion.
    ("mir_add_string_concat",
     's = "hi"\nt = "ho"\n'
     'say s + t\nsay s + "x"\nsay "x" + s\nsay s + s\nsay s + ""\n'
     'u = s + t\nsay u\nsay u + s\n'
     'say s\nsay t\n'),
    # The CONCAT CHAIN.  A chain is not N-1 concatenations: the interpreter's
    # L_CONCAT sums every segment's length, checks the sum for overflow with the
    # same predicate a pair would use, allocates ONE buffer and memcpys each
    # segment in order.  So the NUMBER of blocks is the criterion: every chain
    # here must produce ONE block, not sum(nops - 1).  That count is what
    # separates "fold" from "call the pair routine until the operands run out",
    # and no stdout comparison can see the difference.
    #
    # Why each line is here, one by one:
    #   `a + b + c`             three resident operands; the flat case.  Compiled
    #                           as ONE OP_CONCAT over 3 registers, not as
    #                           (a+b)+c, which would be an OP_ADD and then an
    #                           OP_ADD against a heap-owned left.
    #   `a + "|" + b + "|" + c + "|" + d + "|" + e + "|" + f`   eleven operands:
    #                           a chain long enough that a per-pair fold is ten
    #                           blocks where the criterion says one.  It also puts
    #                           five RESIDENT literals BETWEEN variables, which
    #                           stops an arm that assumes at most two operands are
    #                           variables.
    #   `a + b + "" + c`        the empty string in the MIDDLE of a chain.  A
    #                           0-length segment adds 0 bytes to the sum but is
    #                           still an operand, so a fold that short-circuits
    #                           `x + ""` into `x` here would print the right bytes
    #                           from a WRONG buffer; the block count would look
    #                           right, which is why bytes is read as well.
    #   `u = a + b + c`         a chain result stored in a global...
    #   `say u`                 ...and printed: a value, not a print-time trick.
    #   `u == v` / `u != v`     the chain result as an operand of a COMPARISON
    #                           against a resident string, so the buffer is a real
    #                           string and not a temporary only say() can read.
    #                           Variables, not an `== "aabbcc"`: a constant
    #                           comparison is OP_EQK/OP_NEQK, another arm.
    #   `"z" + u + ""`          a HEAP-OWNED chain result (u) in the middle of
    #                           another chain, empty string after it.  nops=3 and
    #                           one operand is already registry-owned: this line's
    #                           buffer must be new and must not absorb u.
    #   `u + u + u`             the same owned buffer three times: an arm that
    #                           appended in place would corrupt u and print a
    #                           different sum.
    #   `say a` / `say b`       the originals are untouched by all of the above.
    ("mir_concat_chain",
     'a = "aa"\nb = "bb"\nc = "cc"\nd = "dd"\ne = "ee"\nf = "ff"\n'
     'say a + b + c\n'
     'say a + "|" + b + "|" + c + "|" + d + "|" + e + "|" + f\n'
     'say a + b + "" + c\n'
     'u = a + b + c\nv = "aabbcc"\n'
     'say u\nsay u == v\nsay u != v\n'
     'say "z" + u + ""\nsay u + u + u\n'
     'say a\nsay b\n'),
]


def run_bytes(cmd, cwd):
    """run(), keeping stdout as BYTES.

    The shared corpus compares decoded text, which is fine for numbers.  A
    string case is where a byte difference most plausibly hides, so these cases
    are compared as bytes and the reading carries the hex."""
    try:
        d = subprocess.run([str(c) for c in cmd], cwd=str(cwd), capture_output=True,
                           timeout=300)
        return d.returncode, d.stdout, d.stderr
    except subprocess.TimeoutExpired:
        return "timeout", b"", b""


class Unclassifiable(Exception):
    """A byte difference the closed set does not cover.  Never caught silently."""


def load_cases():
    spec = importlib.util.spec_from_file_location("aot_cases", CASES)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(ROOT / "tools"))
    spec.loader.exec_module(mod)
    return mod


def head_sha():
    p = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                       capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else "<not a git tree>"


def run(cmd, cwd, timeout=300):
    try:
        d = subprocess.run([str(c) for c in cmd], cwd=str(cwd), capture_output=True,
                           text=True, timeout=timeout)
        return d.returncode, d.stdout, d.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def first_diff_line(a, b):
    """1-based line number of the first line that differs, or 0 when the files
    are equal; when one is a prefix of the other, the first missing line."""
    la, lb = a.splitlines(), b.splitlines()
    n = 0
    while n < len(la) and n < len(lb) and la[n] == lb[n]:
        n += 1
    return n + 1 if (n < len(la) or n < len(lb)) else 0


def classify(ast_text, mir_text):
    """Return (reason, first-diff-line).  Raises Unclassifiable when the
    difference is not one of the closed set's reasons."""
    if ast_text == mir_text:
        return "identical", 0
    line = first_diff_line(ast_text, mir_text)
    prefix = "\n".join(ast_text.splitlines()[:line - 1])
    for marker in PREAMBLE_MARKERS:
        if marker not in prefix:
            raise Unclassifiable("the byte-identical prefix ends at line %d, before the shared "
                                 "runtime marker %r, so a difference there is not lowering shape"
                                 % (line, marker))
    ga = {GLOBAL_DECL.match(l).group(1) for l in ast_text.splitlines() if GLOBAL_DECL.match(l)}
    gb = {GLOBAL_DECL.match(l).group(1) for l in mir_text.splitlines() if GLOBAL_DECL.match(l)}
    if ga != gb:
        return "mir-register-shape+mir-internal-globals", line
    return "mir-register-shape", line


def parse_mir_out(text):
    got = {"codegen": None, "conserve": None, "notyet": [], "refused": None}
    for raw in text.splitlines():
        line = raw.strip()
        m = CODEGEN.match(line)
        if m:
            got["codegen"] = dict(kv.split("=", 1) for kv in m.group(1).split("|") if "=" in kv)
            continue
        m = CONSERVE.match(line)
        if m:
            got["conserve"] = dict(kv.split("=", 1) for kv in m.group(1).split("|") if "=" in kv)
            continue
        m = NOTYET.match(line)
        if m:
            got["notyet"].append((m.group(1), int(m.group(2))))
            continue
        m = REFUSED.match(line)
        if m:
            got["refused"] = (m.group(1), m.group(2))
    return got


def self_check(verbose):
    """The classifier, on pairs whose answer is known -- including one it must
    refuse to classify.  No build, no corpus."""
    head = "static void nv_die_division_by_zero(void) { }\nstatic void nv_fmt_double(void) { }\n"
    tail = "static void nv_say(NV a) { }\n"
    run = sys.stdout.write
    checks = 0

    def expect(name, a, b, want):
        nonlocal checks
        checks += 1
        try:
            got, _line = classify(a, b)
        except Unclassifiable as exc:
            got = "unclassifiable:" + str(exc)
        ok = got == want
        print("IR-MIR-SELFCHECK|what=%s|got=%s|want=%s|%s" % (name, got, want, "ok" if ok else "WRONG"))
        return ok

    ok = True
    ok &= expect("equal-files", head + tail + "int main(void) { return 0; }\n",
                 head + tail + "int main(void) { return 0; }\n", "identical")
    ok &= expect("body-shape", head + tail + "int main(void) { NV v_x = nv_int(1); }\n",
                 head + tail + "int main(void) { NV v[2]; NV v; }\n", "mir-register-shape")
    ok &= expect("extra-global",
                 head + tail + "static NV g_a = { NV_INT, 0, 0 };\nint main(void) { return 0; }\n",
                 head + tail + "static NV g_a = { NV_INT, 0, 0 };\nstatic NV g_b = { NV_INT, 0, 0 };\n"
                 + "int main(void) { return 0; }\n",
                 "mir-register-shape+mir-internal-globals")
    checks += 1
    try:
        classify("int main(void) { return 0; }\n", "int main(void) { return 0; }\n\n",
                 )
        print("IR-MIR-SELFCHECK|what=no-shared-runtime|got=classified|want=unclassifiable|WRONG")
        ok = False
    except Unclassifiable:
        print("IR-MIR-SELFCHECK|what=no-shared-runtime|got=unclassifiable|want=unclassifiable|ok")
    checks += 1
    try:
        classify(head + "static void nv_say(NV a) { x }\n",
                 head + "static void nv_say(NV a) { y }\n")
        print("IR-MIR-SELFCHECK|what=runtime-varies|got=classified|want=unclassifiable|WRONG")
        ok = False
    except Unclassifiable:
        print("IR-MIR-SELFCHECK|what=runtime-varies|got=unclassifiable|want=unclassifiable|ok")

    # The layout-assertion check, asked both ways: a complete block passes, and
    # a block with one assertion removed is a NAMED miss.  The second cell is
    # the one that matters -- a checker that can only say "not found" is what
    # this measurement exists to replace.
    checks += 1
    complete = "".join(needle + "\n" for _name, needle in MIRVAL_ASSERTS)
    got = mirval_assert_problems(complete)
    print("IR-MIR-SELFCHECK|what=layout-assertions-all-present|got=%s|want=none|%s"
          % (",".join(got) or "none", "ok" if not got else "WRONG"))
    ok &= not got
    checks += 1
    victim_name, victim_text = MIRVAL_ASSERTS[-1]
    one_deleted = complete.replace(victim_text + "\n", "")
    got = mirval_assert_problems(one_deleted)
    print("IR-MIR-SELFCHECK|what=layout-assertion-deleted|deleted=%s|got=%s|want=%s|%s"
          % (victim_name, ",".join(got) or "none", victim_name,
             "ok" if got == [victim_name] else "WRONG"))
    ok &= got == [victim_name]
    # The allocation reading, asked four ways, because it has four ways to be
    # wrong and a boolean would merge them: a clean reading passes; a block
    # that survived the sweep is a NAMED leak carrying the case and the counts;
    # a count that does not match the criterion is a different NAMED problem
    # (this is the shape a site that mallocs without registering produces -- it
    # reports 0|0, i.e. it looks clean); and a run that printed no reading at
    # all is a third problem again.  The second and third cells are what make
    # this a checker rather than a formality: without them "not found" is the
    # only verdict it could ever give.
    checks += 1
    for what, text, want_kind in (
            ("allocs-clean", "IR-MIR-ALLOCS|allocs=7|freed=7|live=0|bytes=33\n", None),
            ("allocs-leak", "IR-MIR-ALLOCS|allocs=7|freed=6|live=1|bytes=33\n", "LEAK"),
            ("allocs-count-off", "IR-MIR-ALLOCS|allocs=6|freed=6|live=0|bytes=30\n",
             "allocation count off"),
            ("allocs-missing", "", "printed no IR-MIR-ALLOCS line")):
        checks += 1
        got = allocs_problems(text, "mir_add_string_concat")
        if want_kind is None:
            hit = not got
        else:
            hit = len(got) == 1 and want_kind in got[0] and "mir_add_string_concat" in got[0]
        print("IR-MIR-SELFCHECK|what=%s|got=%s|want=%s|%s"
              % (what, "none" if not got else got[0].split(":")[0], want_kind or "none",
                 "ok" if hit else "WRONG"))
        ok &= hit
    print("IR-MIR-SELFCHECK|checks=%d|verdict=%s" % (checks, "PASS" if ok else "FAIL"))
    return 0 if ok else 1


def perturb_case(mir_tool, workdir, name, src_path, base_c, verbose):
    """Change one LOADK_INT operand in main and require the generated C to move.

    Returns (tried, c_changed_failures, stdout_changes)."""
    rc, out, err = run([mir_tool, "dump", src_path], workdir)
    if rc != 0:
        return 0, 0, 0, ["dump failed for %s: %s" % (name, (err or out).strip()[:200])]
    picks = []
    for raw in out.splitlines():
        m = INSN.match(raw.strip())
        if not m:
            continue
        f, idx, op, r1, r2, r3, argc = (int(m.group(1)), int(m.group(2)), m.group(3),
                                        int(m.group(4)), int(m.group(5)), int(m.group(6)),
                                        int(m.group(7)))
        if f == 0 and op == "OP_LOADK_INT" and r2 != 2147483647:
            picks.append((idx, r2))
        if len(picks) >= 3:
            break
    tried = 0
    c_fail = 0
    stdout_changes = 0
    problems = []
    for idx, was in picks:
        tried += 1
        now = was + 1
        out_c = Path(workdir) / ("%s.p%d.c" % (name, idx))
        rc2, out2, err2 = run([mir_tool, "translate", "--perturb", "0", str(idx), "2", str(now),
                               src_path, out_c], workdir)
        if rc2 != 0:
            problems.append("perturb %s idx=%d was refused: %s" % (name, idx, (err2 or out2).strip()[:200]))
            continue
        try:
            new_c = out_c.read_text()
        except OSError as exc:
            problems.append("perturb %s idx=%d wrote no file: %s" % (name, idx, exc))
            continue
        c_changed = new_c != base_c
        if not c_changed:
            c_fail += 1
        exe = Path(workdir) / ("%s.p%d.exe" % (name, idx))
        rc3, _o3, err3 = run(["cc", "-O2", "-o", exe, out_c], workdir)
        stdout_changed = "no-file"
        if rc3 == 0:
            rc4, o4, _e4 = run([exe], workdir)
            stdout_changed = "yes" if o4 != BASE_STDOUT[0] else "no"
            if stdout_changed == "yes":
                stdout_changes += 1
        else:
            problems.append("perturb %s idx=%d did not compile: %s" % (name, idx, (err3 or "")[:200]))
        print("IR-MIR-PERTURB|case=%s|func=0|idx=%d|field=2|was=%d|now=%d|c-changed=%s|stdout-changed=%s"
              % (name, idx, was, now, "yes" if c_changed else "no", stdout_changed))
    return tried, c_fail, stdout_changes, problems


BASE_STDOUT = [None]

# The negative control has to reach BOTH wide operand slots.  mir_perturb
# writes `a`/`b` and clears the matching IR_F_WIDE_* flag, so an instruction
# whose real operand lives in `imm`/`wide` is the only place where the merged
# reader (ir_insn_r2/ir_insn_r3) can be told apart from a reader of `a`/`b`:
# perturb alone would pass either way, because both change `a`.
WIDE_CONTROLS = [
    # 40000 does not fit the int16 field, so the compiler parks it in `imm`
    # and sets IR_F_WIDE_A; mir_perturb field 2 writes `a` and clears it.
    ("wide-imm-slot", "say 40000\n", "OP_LOADK_INT", 2, "imm", "40000"),
    # 2**53+1 = 0x20000000000001, so LOADK_I64's high word 0x200000 = 2097152
    # does not fit either and lives in `wide`; field 3 writes `b` and clears it.
    # The number the C must carry is the reassembled one: a lowering that read
    # `b` (the truncated high word, 0) would print 1 instead.
    ("wide-wide-slot", "say 9007199254740993\n", "OP_LOADK_I64", 3, "wide",
     "9007199254740993"),
]


def wide_controls(mir_tool, interp, workdir, verbose):
    """Run the out-of-int16 operand controls and report them one per line.

    Each control asks three things: the lowering put the NUMBER THE PROGRAM MEANS
    in the C (so the wide slot really was read -- a reader of the truncated
    `a`/`b` field would print something else), perturbing that slot moves the C,
    and the executable's answer moves with it while the unperturbed one still
    agrees with the interpreter.  A control that only asked the middle question
    would pass a generator that read `a` -- the truncated field -- and dropped
    `imm`."""
    problems = []
    lines = []
    total = slot_in_c = c_changed = moved_out = same_as_interp = 0
    for name, src, op, field, slot, literal in WIDE_CONTROLS:
        total += 1
        s = Path(workdir) / (name + ".im")
        s.write_text(src)
        c0 = Path(workdir) / (name + ".c")
        c1 = Path(workdir) / (name + ".p.c")
        rc, out, err = run([mir_tool, "dump", s], workdir)
        pick = None
        if rc == 0:
            for raw in out.splitlines():
                m = INSN.match(raw.strip())
                if not m:
                    continue
                f, idx, o = int(m.group(1)), int(m.group(2)), m.group(3)
                r2, r3 = int(m.group(5)), int(m.group(6))
                if f == 0 and o == op:
                    v = r2 if field == 2 else r3
                    if v > 32767 or v < -32768:
                        pick = (idx, v)
                        break
        if pick is None:
            problems.append("%s: no %s in main carries an operand outside int16, so the"
                            " %s slot was never exercised" % (name, op, slot))
            lines.append("IR-MIR-WIDE|control=%s|op=%s|slot=%s|found=no"
                         % (name, op, slot))
            continue
        idx, was = pick
        now = was + 1
        rc0, o0, e0 = run([mir_tool, "translate", s, c0], workdir)
        if rc0 != 0:
            problems.append("%s: the unperturbed lowering was refused: %s"
                            % (name, (e0 or o0).strip()[:160]))
            continue
        text0 = c0.read_text()
        in_c = literal in text0
        slot_in_c += 1 if in_c else 0
        if not in_c:
            problems.append("%s: the C does not carry %s, so the %s slot (value %d) was"
                            " not read" % (name, literal, slot, was))
        rc1, o1, e1 = run([mir_tool, "translate", "--perturb", "0", str(idx),
                           str(field), str(now), s, c1], workdir)
        if rc1 != 0:
            problems.append("%s: the perturbed lowering was refused: %s"
                            % (name, (e1 or o1).strip()[:160]))
            continue
        did_move = c1.read_text() != text0
        c_changed += 1 if did_move else 0
        if not did_move:
            problems.append("%s: perturbing the %s slot (%d -> %d) changed no C"
                            % (name, slot, was, now))
        rc_i, out_i, _ei = run([interp, "--no-mods", "run", s], workdir)
        exe0 = Path(workdir) / (name + ".exe")
        exe1 = Path(workdir) / (name + ".p.exe")
        rc_c0, _c, ec0 = run(["cc", "-O2", "-o", exe0, c0], workdir)
        rc_c1, _c1, ec1 = run(["cc", "-O2", "-o", exe1, c1], workdir)
        same = moved = "no-file"
        if rc_c0 == 0 and rc_c1 == 0:
            rc_r0, out0, _r0 = run([exe0], workdir)
            rc_r1, out1, _r1 = run([exe1], workdir)
            same = "yes" if (out0 == out_i and rc_r0 == rc_i) else "no"
            moved = "yes" if (out1 != out0 or rc_r1 != rc_r0) else "no"
            if same == "yes":
                same_as_interp += 1
            else:
                problems.append("%s: the unperturbed lowering answers %r, the interpreter %r"
                                % (name, out0, out_i))
            if moved == "yes":
                moved_out += 1
            else:
                problems.append("%s: the %s slot moved in the C but not in the answer"
                                % (name, slot))
        else:
            problems.append("%s: cc failed (plain=%s perturbed=%s): %s %s"
                            % (name, rc_c0, rc_c1, (ec0 or "")[:160], (ec1 or "")[:160]))
        line = ("IR-MIR-WIDE|control=%s|op=%s|slot=%s|field=%d|func=0|idx=%d|was=%d|now=%d"
                "|beyond-int16=yes|means=%s|literal-in-c=%s|c-changed=%s"
                "|unperturbed-same-as-interpreter=%s"
                "|stdout-changed=%s"
                % (name, op, slot, field, idx, was, now, literal,
                   "yes" if in_c else "no",
                   "yes" if did_move else "no", same, moved))
        lines.append(line)
        if verbose:
            print(line)
    for line in lines:
        print(line)
    print("IR-MIR-WIDE-CONTROL|controls=%d|literal-in-c=%d|c-changed=%d|stdout-changed=%d"
          "|unperturbed-same-as-interpreter=%d|why=the-operand-does-not-fit-int16-so-it"
          "-lives-in-imm-or-wide-and-a-reader-of-a-or-b-alone-would-print-the-truncated-field"
          % (total, slot_in_c, c_changed, moved_out, same_as_interp))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--tool", default="",
                    help="the aot-mir executable; the CTest registration passes "
                         "$<TARGET_FILE:aot-mir>, so the registration names the target it runs")
    ap.add_argument("--max", type=int, default=0, help="stop after N cases (0 = all)")
    ap.add_argument("--all", action="store_true", help="run the whole corpus (the default)")
    ap.add_argument("--perturb-cases", type=int, default=3,
                    help="how many accepted cases get the perturbed-operand control")
    ap.add_argument("--self-check", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.self_check:
        return self_check(args.verbose)

    build = (ROOT / args.build) if not os.path.isabs(args.build) else Path(args.build)
    interp = build / "inimerse"
    ast_tool = build / "aot-native"
    # --tool is what the CTest registration passes ($<TARGET_FILE:aot-mir>, an
    # absolute path).  A relative one is resolved the same way --build is, because
    # the cases run in a temp cwd of their own.
    if args.tool:
        mir_tool = ROOT / args.tool if not os.path.isabs(args.tool) else Path(args.tool)
    else:
        mir_tool = build / "aot-mir"
    for tool in (interp, ast_tool, mir_tool):
        if not tool.exists():
            print("aot_mir: REFUSED|missing %s -- build it first" % tool, file=sys.stderr)
            return 2

    mod = load_cases()
    cases = [(n, src) for n, src, _want in mod.EQUIVALENCE]
    if args.max:
        cases = cases[:args.max]

    workdir = tempfile.mkdtemp(prefix="aot_mir_")
    accepted = refused = differ = conserved = 0
    c_identical = c_diff = 0
    reasons = {r: 0 for r in DIFF_REASONS}
    problems = []
    unclassifiable = []
    perturbed = perturb_fail = stdout_changes = 0
    layout_files = layout_complete = 0
    layout_missing = set()

    for name, src in cases:
        s = Path(workdir) / (name + ".im")
        s.write_text(src)
        ast_c = Path(workdir) / (name + ".ast.c")
        mir_c = Path(workdir) / (name + ".mir.c")

        rc_a, _oa, ea = run([ast_tool, "translate", s, ast_c], workdir)
        rc_m, om, em = run([mir_tool, "translate", s, mir_c], workdir)
        got = parse_mir_out(om)
        if rc_a != 0:
            problems.append("%s: the AST route refused a case from its own equivalence corpus: %s"
                            % (name, (ea or "").strip()[:200]))
            continue
        if rc_m != 0:
            refused += 1
            why = got["refused"][0] if got["refused"] else "no IR-MIR-REFUSED line"
            detail = got["refused"][1] if got["refused"] else (em or "").strip()[:120]
            notyet = ",".join("%s:%d" % (op, n) for op, n in got["notyet"])
            print("IR-MIR-REFUSED|case=%s|why=%s|detail=%s|not-yet=%s" % (name, why, detail, notyet or "-"))
            continue
        accepted += 1

        cons = got["conserve"] or {}
        if cons.get("holds") == "yes":
            conserved += 1
        else:
            problems.append("%s: IR-MIR-CONSERVE does not hold: %s" % (name, cons))

        rc_i, out_i, _ei = run([interp, "--no-mods", "run", s], workdir)
        BASE_STDOUT[0] = out_i
        exe_m = Path(workdir) / (name + ".mir.exe")
        exe_a = Path(workdir) / (name + ".ast.exe")
        rc_cm, _o, ecm = run(["cc", "-O2", "-o", exe_m, mir_c], workdir)
        rc_ca, _o2, eca = run(["cc", "-O2", "-o", exe_a, ast_c], workdir)
        if rc_cm != 0 or rc_ca != 0:
            problems.append("%s: cc failed (mir=%s ast=%s): %s %s"
                            % (name, rc_cm, rc_ca, (ecm or "")[:200], (eca or "")[:200]))
            continue
        rc_rm, out_m, _e = run([exe_m], workdir)
        rc_ra, out_a, _e2 = run([exe_a], workdir)
        same_exec = (out_m == out_i == out_a) and (rc_rm == rc_i == rc_ra)
        if not same_exec:
            differ += 1
            print("IR-MIR-EXEC-DIFF|case=%s|interp-rc=%s|mir-rc=%s|ast-rc=%s|interp-out=%r|mir-out=%r|ast-out=%r"
                  % (name, rc_i, rc_rm, rc_ra, out_i, out_m, out_a))
        print("IR-MIR-C|case=%s|ast=accepted|mir=accepted|cc=ok|exec=%s"
              % (name, "same" if same_exec else "diff"))

        ast_text = ast_c.read_text()
        mir_text = mir_c.read_text()
        # Read the layout assertions off the file that was just compiled, not off
        # the template that wrote it: the promise is about the artifact, and an
        # artifact is what a generator can silently stop producing.
        layout_files += 1
        miss = mirval_assert_problems(mir_text)
        if miss:
            layout_missing.update(miss)
            print("IR-MIR-LAYOUT-ASSERT-MISS|case=%s|missing=(%s)" % (name, ",".join(miss)))
            problems.append("%s: the generated C carries %d of the %d MirVal layout assertions"
                            " and is missing (%s), so this file would be compiled without the"
                            " layout pin"
                            % (name, len(MIRVAL_ASSERTS) - len(miss), len(MIRVAL_ASSERTS),
                               ",".join(miss)))
        else:
            layout_complete += 1
        try:
            reason, line = classify(ast_text, mir_text)
        except Unclassifiable as exc:
            unclassifiable.append("%s: %s" % (name, exc))
            continue
        reasons[reason] += 1
        if reason == "identical":
            c_identical += 1
            print("IR-MIR-C-SAME|case=%s|bytes=%d|reason=identical" % (name, len(mir_text)))
        else:
            c_diff += 1
            print("IR-MIR-C-DIFF|case=%s|len=%d|first-diff=line %d|reason=%s"
                  % (name, len(mir_text), line, reason))

        if perturbed < args.perturb_cases:
            t, cf, sc, probs = perturb_case(mir_tool, workdir, name, s, mir_text, args.verbose)
            perturbed += t
            perturb_fail += cf
            stdout_changes += sc
            problems.extend(probs)

    problems.extend(wide_controls(mir_tool, interp, workdir, args.verbose))

    # ------------------------------------------------------------ MIR-only cases
    #
    # Same oracle, no AST file: EXECUTED is the property that applies, and it is
    # byte equality.  `say` itself is where the second print exit lives
    # (mv_say in kMirValBlock carries exactly one string branch), so these
    # programs are also the only corpus that can catch that branch falling back
    # to nv_say -- which would print an integer or abort, never the string.
    mir_only_ran = mir_only_same = 0
    for name, src in MIR_ONLY:
        s = Path(workdir) / (name + ".im")
        s.write_text(src)
        mir_c = Path(workdir) / (name + ".mir.c")
        exe_m = Path(workdir) / (name + ".mir.exe")
        rc_m, om, em = run([mir_tool, "translate", s, mir_c], workdir)
        got = parse_mir_out(om)
        mir_only_ran += 1
        if rc_m != 0:
            why = got["refused"][0] if got["refused"] else "no IR-MIR-REFUSED line"
            notyet = ",".join("%s:%d" % (op, n) for op, n in got["notyet"])
            problems.append("%s: the MIR route refused a case this list exists to measure: %s (%s)"
                            % (name, why, notyet or (em or "").strip()[:120]))
            print("IR-MIR-MIR-ONLY|case=%s|mir=refused|why=%s" % (name, why))
            continue
        cons = got["conserve"] or {}
        if cons.get("holds") != "yes":
            problems.append("%s: IR-MIR-CONSERVE does not hold: %s" % (name, cons))
        miss = mirval_assert_problems(mir_c.read_text())
        if miss:
            problems.append("%s: missing layout assertions (%s)" % (name, ",".join(miss)))
        rc_c, _o, ec = run(["cc", "-O2", "-o", exe_m, mir_c], workdir)
        if rc_c != 0:
            problems.append("%s: cc failed: %s" % (name, (ec or "")[:200]))
            continue
        rc_i, out_i, _ei = run_bytes([interp, "--no-mods", "run", s], workdir)
        rc_r, out_r, er = run_bytes([exe_m], workdir)
        same = (out_r == out_i) and (rc_r == rc_i)
        mir_only_same += 1 if same else 0
        if not same:
            problems.append("%s: the MIR route printed %r rc=%s, the interpreter %r rc=%s"
                            % (name, out_r, rc_r, out_i, rc_i))
            print("IR-MIR-EXEC-DIFF|case=%s|interp-rc=%s|mir-rc=%s|interp-out=%r|mir-out=%r"
                  % (name, rc_i, rc_r, out_i, out_r))
        # The allocation reading, taken from the SAME run whose stdout was just
        # compared.  stderr is where mv_free_all() prints it, which is the one
        # channel this criterion does not read.  A run that stopped on a guard
        # never reached atexit, so it reports `reported=no`: the exit-code
        # problem above is that red, and this reading must not turn a crash into
        # a second, quieter one.
        er_text = er.decode("utf-8", "replace") if isinstance(er, bytes) else (er or "")
        want = ALLOCS_EXPECT.get(name, ALLOCS_DEFAULT)
        got = ALLOCS_RE.search(er_text)
        if rc_r == 0:
            problems.extend(allocs_problems(er_text, name))
            if got:
                print("IR-MIR-ALLOCS|case=%s|reported=yes|allocs=%s|freed=%s|live=%s|bytes=%s"
                      "|expect-allocs=%d|expect-bytes=%d|ok=%s"
                      % (name, got.group(1), got.group(2), got.group(3), got.group(4),
                         want[0], want[1], "yes" if not allocs_problems(er_text, name) else "no"))
            else:
                print("IR-MIR-ALLOCS|case=%s|reported=no|why=exited-0-but-printed-no-line"
                      "|expect-allocs=%d|expect-bytes=%d|ok=no" % (name, want[0], want[1]))
        else:
            print("IR-MIR-ALLOCS|case=%s|reported=no|why=rc=%s" % (name, rc_r))
        print("IR-MIR-MIR-ONLY|case=%s|mir=accepted|cc=ok|exec=%s|rc=%s|bytes=%d|stdout-hex=%s"
              % (name, "same" if same else "diff", rc_r, len(out_r), out_r.hex()))

    print("IR-MIR-LAYOUT-ASSERT|files=%d|need=%d|files-complete=%d|missing=(%s)"
          "|basis=the-six-_Static_assert-lines-kMirValBlock-writes-into-every-generated"
          "-file-read-off-the-emitted-text"
          % (layout_files, len(MIRVAL_ASSERTS), layout_complete,
             ",".join(sorted(layout_missing)) or "none"))
    print("IR-MIR-COVER|corpus=inline-cases:%d|accepted=%d|refused=%d|differ=%d|conserved=%d"
          "|c-identical=%d|c-diff=%d|reasons=%s|perturbed=%d|perturb-c-changed-failures=%d"
          "|perturb-stdout-changes=%d|mir-only-cases=%d|mir-only-same-as-interpreter=%d"
          % (len(cases), accepted, refused, differ, conserved, c_identical, c_diff,
             ",".join("%s:%d" % (r, reasons[r]) for r in DIFF_REASONS),
             perturbed, perturb_fail, stdout_changes,
             mir_only_ran, mir_only_same))
    for line in unclassifiable:
        print("IR-MIR-C-UNCLASSIFIED|" + line)
    for line in problems:
        print("IR-MIR-PROBLEM|" + line)

    ok = (differ == 0 and not problems and not unclassifiable and conserved == accepted
          and perturb_fail == 0 and stdout_changes > 0 and perturbed > 0
          and mir_only_same == len(MIR_ONLY))
    if c_identical == 0 and c_diff > 0:
        ok = ok  # a corpus where every file differs is not itself a failure
    print("IR-MIR-VERDICT|%s|tree=%s|accepted=%d|refused=%d|differ=%d|c-identical=%d|c-diff=%d"
          "|perturb-stdout-changes=%d"
          % ("PASS" if ok else "FAIL", head_sha(), accepted, refused, differ, c_identical, c_diff,
             stdout_changes))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
