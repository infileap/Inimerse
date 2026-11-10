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
    # The array literal's ONE block is the value pool, not the array: the pool
    # is MV_SLOTS == 8 slots of 32 bytes = 256 bytes, allocated on FIRST use and
    # grown by doubling after that.  Every literal case below fits in those 8
    # slots -- a 3-element array needs 1 count + 3 items = 4; the nested case
    # needs 3 + 2 + 3 = 8 exactly -- so each of them measures allocs=1|bytes=256
    # and the empty literal measures the same, because the first use allocates
    # the pool whether or not any element follows.  A route that malloc'd the
    # array itself, or one that grew by one slot per element, lands on a
    # different bytes number here and the difference names which.
    "mir_container_new_array": (1, 256),
    "mir_container_new_array_mixed": (1, 256),
    "mir_container_index_get": (1, 256),
    "mir_container_index_edges": (1, 256),
    "mir_container_new_array_empty": (1, 256),
    # OP_IN itself allocates NOTHING -- mv_in reads the pool and calls the
    # equality pair.  These five numbers are the array literals the programs
    # happen to contain, MEASURED before they were written down: the pool holds
    # 8 slots of 32 bytes on first use and grows by DOUBLING, and the old block
    # is swept at exit instead of freed during the run, so a case whose distinct
    # literals need more than 16 slots reports three blocks --
    # 8+16+32 slots = 256+512+1024 bytes = 1792 -- and freed equals allocs.
    # mir_in_string's only container is one 3-slot ["a","b"] literal, so it
    # allocates the FIRST pool block alone (256); its strings are resident in
    # the string pool and cost no block.  mir_in_scalar has no container literal
    # at all, and 0|0 is the zero this arm proves: had mv_in allocated per test,
    # it would show up here and nowhere else in these five.
    "mir_in_array_hit": (3, 1792),
    "mir_in_array_miss": (3, 1792),
    "mir_in_string": (1, 256),
    "mir_in_scalar": (0, 0),
    # 1728 would be 8+16 slots; this case carries enough distinct literals
    # ([1,2,3], [x-1,x], [[1],[2]], [1], [2], [1]) to cross into the 32-slot
    # block, which is the growth point, not a second allocation rule.
    "mir_in_computed": (3, 1792),
    # The string arm of OP_INDEX_GET builds ONE 2-byte block per character it
    # actually returns (src/vm/vm.c:3706-3709 strdup's a one-character buffer;
    # this route's mv_alloc does the same): reads 0 and 1 answer 'h' and 'i',
    # read 2 is past the end and answers nil with no block, and the negative
    # read answers 'i' again -- three blocks, 6 bytes.
    "mir_container_string_index": (3, 6),
    # The type-mismatch case exits 1, so its reading is `reported=no|why=rc=1`
    # and no count is consulted; listed so the table says so on purpose.
    "mir_container_index_type_mismatch": (0, 0),
    # WRITE (OP_INDEX_SET).  The three numbers below are MEASURED, and each one
    # is a claim about what the write itself does: `mir_index_set_int` and
    # `mir_index_set_overwrite` both measure (1, 256) -- ONE pool block of 8
    # slots -- for programs that contain exactly one array literal and no
    # growth.  That is the pool, allocated on first use by mv_arr_new; the
    # writes add NOTHING to it, because every one of their subscripts is inside
    # the literal's count and mv_arr_set's in-place arm is a plain 32-byte slot
    # assignment with no mv_alloc call.  A route that copied an element, boxed
    # the value, or grew by one slot per write lands on a different number here.
    "mir_index_set_int": (1, 256),
    "mir_index_set_overwrite": (1, 256),
    # mir_index_set_grow measures THREE blocks, 1792 bytes, and the number is
    # the sum of the pool block's two doublings:
    #   a = [1,2,3] needs 1 count + 3 items = 4 slots  -> the first use
    #                allocates the 8-slot block                       256
    #   b = [7]     lands at used=7 and needs 7+1+1 = 9 slots, which no longer
    #                fits in 8, so the block doubles to 16               512
    #   b[s] = 8    with s=3 falls PAST the end of b (count 1) while c=[9,9]
    #                sits above b's region, so b must MOVE to the frontier:
    #                used=12, the moved region needs 4 slots and one more count
    #                slot, i.e. 17 total, past 16, so the block doubles to 32
    #                                                                   1024
    # The nil fill and the item moved do NOT allocate: the moved 4-slot region
    # is inside the block the doubling already bought.  A route that allocated
    # a fresh item block per array would report (5, 512) here (three literals
    # plus the move plus ... ) -- a different pair, on purpose.
    "mir_index_set_grow": (3, 1792),
    # The last two end on an exception, so like the type-mismatch case above
    # their reading is `reported=no|why=rc=1` and no count is consulted;
    # listed so the table says so on purpose.
    "mir_index_set_edges": (0, 0),
    "mir_index_set_negative": (0, 0),
    # DICT (OP_NEW_DICT + the read-only dict branch of OP_INDEX_GET).  All four
    # MEASURED before they were written down, and all four land on ONE pool
    # block: a dict shares mv_arrmem with an array (it IS an ArrayObj's items in
    # the interpreter), its region is 1 count slot + 2*pairs item slots, and the
    # first pool block is 8 slots = 256 bytes, so a 1-pair dict (3 slots) and a
    # 2-pair dict (5 slots) both fit the first block and only the block is ever
    # allocated.  The READS add nothing: mv_dict_get compares keys through
    # mv_eq and mv_dict_get_at copies a 32-byte KEY slot, neither calls mv_alloc.
    # A route that malloc'd a per-dict table (or that hash-boxed a key) lands on
    # a different pair here and the difference names which.
    "mir_dict_new_get": (1, 256),
    "mir_dict_missing_key": (1, 256),
    "mir_dict_key_register": (1, 256),
    "mir_dict_position_key": (1, 256),
    # The WRITE cases.  Measured first, then written, in that order:
    #   overwrite  the dict literal is the only allocation (5 slots -> block 1);
    #              overwriting an existing pair allocates nothing at all, which is
    #              the point of the case -- a route that re-appended the pair
    #              instead of overwriting it in place would print the right bytes
    #              and still move this number.
    #   append     two appends ({"a":1} -> +b -> +c) still fit the first 256-byte
    #              block: 3 + 2 + 2 = 7 slots <= 8.  So an append is not an
    #              allocation here, and "it printed the right thing" cannot be an
    #              accident of a fresh block either.
    #   grow       the dict cannot extend in place (an array was created after
    #              it), so the move allocates a second block: 2 blocks, 768 bytes
    #              -- dict block (256) + the frontier block for the moved copy and
    #              the array.  A route that reused the old region instead of
    #              copying would print the same bytes and report 1 block.
    "mir_dict_set_overwrite": (1, 256),
    "mir_dict_set_append": (1, 256),
    "mir_dict_set_grow_after_neighbour": (2, 768),
    # The set-key pair.  Two blocks each = the dict's block and one set's
    # block; `t` is a second SET region but the pool still fits in the second
    # block (the lookup case builds a 3-slot dict + two 3-slot sets = 9 slots
    # over 8, so the second 256-byte block is the frontier's; the append case
    # builds two sets first and the empty dict then extends the frontier in
    # place).  Key comparison allocates nothing on either arm -- a route that
    # BOXED a set key into a scalar to compare it would move these numbers.
    "mir_set_dict_key_lookup": (2, 768),
    "mir_set_dict_key_append": (2, 768),
    # len() of a moved container: the fix follows the head, so nothing is
    # allocated beyond the two containers themselves.  Both programs build
    # one dict/array plus a second container and double the pool block to
    # 16 slots at the copy; measured (allocs=2, bytes=768) before writing.
    "mir_len_after_a_dict_moves": (2, 768),
    "mir_len_after_an_array_moves": (2, 768),
    # len() itself allocates nothing; the one block is the ARRAY literal's pool
    # (8 slots * 32 bytes) and the one-pair dict fits in the same block.
    "mir_len_of_every_shape_this_route_holds": (1, 256),
    # Equality allocates nothing of its own: the dicts are the allocation.
    # identity_eq builds two 1-pair dicts and shares the handle once (1 block);
    # neq_and_cross_type builds two dicts AND an array, which is 2 blocks.
    # A row that compared CONTENT would have to walk pairs and, for a string
    # key or value, could box one; it would move these numbers.
    "mir_dict_identity_eq": (1, 256),
    "mir_dict_neq_and_cross_type": (2, 768),
    # The SET cases.  MEASURED FIRST, then written, in that order (the numbers
    # below are the generated programs' own IR-MIR-ALLOCS lines).  The unit is
    # the same MV_SLOTS*32-byte pool block every container on this route shares,
    # allocated on first use and DOUBLED when the frontier needs more than the
    # block holds -- so these three readings are really one reading in three
    # shapes:
    #   one literal   256 bytes = the first block (8 slots).  `s = 1, 2` stores
    #                 [count][1][2] = 3 slots, `s = 7, 7` and `s = a, b` store 2,
    #                 so all three fit and none doubles.  This is also the row
    #                 that says a set is NOT a per-element malloc: a route that
    #                 boxed each element would report one block per element.
    #   dedup         `s = 1, 1, 2` PUSHES three elements and stores TWO: the
    #                 region is [2][1][2], 3 slots, still the first block.  The
    #                 floor matters: a repeat that took a slot would land on 4
    #                 slots, which ALSO fits an 8-slot block, so the block count
    #                 cannot see that defect -- the stdout criterion can (len
    #                 would print 3), and this row is written to say so rather
    #                 than to pretend it is the guard.
    #   membership    FOUR literals, and the doubling is the reading: s = "a","b"
    #                 and t = 1.0,1.0 store 3 slots each (6 of 8), u = 1,1.0
    #                 needs a 9th, so the pool DOUBLES to 16 slots -- 256 + 512
    #                 = 768 bytes, 2 blocks -- and w = 1.0,1 then fits the
    #                 doubled block without allocating.  A route that re-used the
    #                 first region for u would report 1 block and could still
    #                 print the right bytes only by overwriting s, whose "b" in s
    #                 line would then read the wrong set; the two probes are of
    #                 the same copy-vs-overwrite decision.
    "mir_set_new_len_in": (1, 256),
    "mir_set_new_single": (1, 256),
    "mir_set_new_dedup": (1, 256),
    "mir_set_new_membership": (2, 768),
    "mir_set_new_register_elements": (1, 256),
    # The equality group builds its sets exactly the way the literals above do
    # -- one pool block on first use, both operands fitting in MV_SLOTS -- so
    # every one of the six measures allocs=1|freed=1|live=0|bytes=256.  These
    # numbers are MEASURED from each program's own IR-MIR-ALLOCS line and not
    # derived from the set cases above: the first spelling of
    # mir_set_eq_int_vs_float used single-element literals, which constant-fold
    # to plain values, and it measured allocs=0|bytes=0 -- which is why the case
    # is spelled with two elements and why its row here says 1 rather than 0.
    # The equality arm itself allocates nothing: it walks the two pools, so a
    # case that allocates here is allocating its literals, not its comparison.
    "mir_set_eq_same": (1, 256),
    "mir_set_eq_diff": (1, 256),
    "mir_set_eq_mixed_order": (1, 256),
    "mir_set_eq_int_vs_float": (1, 256),
    "mir_set_eq_vs_scalar": (1, 256),
    "mir_set_eq_dedup": (1, 256),
    # The throw pair ends on an exception too, so like the cases just above
    # their reading is `reported=no|why=rc=1` and no count is consulted; listed
    # so the table says so on purpose.  The numbers are MEASURED from the
    # generated programs' own IR-MIR-ALLOCS line (both print allocs=0|freed=0
    # |live=0|bytes=0 after the message): every say() operand here is a
    # resident literal, and a throw allocates nothing, so the exit sweep has
    # no block to sweep.  A route that boxed the thrown value, or that copied
    # the message into a buffer to print it, lands on a different pair and
    # this row is where that would have been said.
    "mir_throw_uncaught_top": (0, 0),
    "mir_throw_uncaught_cross": (0, 0),
    # The closure pair is the only place the MIR route allocates something the
    # interpreter allocates too, and the counts are per INSTANTIATION, not per
    # function: mir_closure_value_nocapture makes a function value with no
    # capture and measures allocs=0|bytes=0 (it is the default row), while
    # mir_closure_capture_read calls `make` TWICE and measures allocs=2|
    # bytes=64.  The two blocks are the two environments, one per call
    # (src/vm/vm.c:4040-4054 builds a FRESH ImClosureEnv per OP_MAKE_FUNC), and
    # 32 bytes each is sizeof(MirVal) = one slot of one captured value.  A
    # route that shared one environment across both instantiations would
    # measure 1|32, and it would also print 15,15 -- so the number and the
    # output disagree together, which is what makes this row worth writing
    # down.  MEASURED from the generated program's own IR-MIR-ALLOCS line
    # (allocs=2|freed=2|live=0|bytes=64) and not derived.
    "mir_closure_capture_read": (2, 64),
    # The other three closure programs measure their own numbers, and all
    # three are MEASURED off the generated program's own IR-MIR-ALLOCS line
    # rather than derived: higher_order = 1|32, return_value = 2|96,
    # zeroarg_string = 2|40.  What they buy is the per-program count; how the
    # blocks decompose is NOT claimed here, because a decomposition written
    # down without a reading to match it is exactly the kind of arithmetic
    # this repository has had to correct before.  A shared-environment route
    # would move higher_order to 0|0 and turn its output into 42/42/15-with-
    # the-wrong-env, so the number and the bytes move together.
    "mir_closure_higher_order": (1, 32),
    "mir_closure_return_value": (2, 96),
    "mir_closure_zeroarg_string": (2, 40),
    # Function equality allocates NOTHING on the capture-free side (there is no
    # environment to build) and exactly the two environments `make` builds in
    # the captured case -- one per instantiation, 32 bytes each.  Both MEASURED
    # off the generated program's own IR-MIR-ALLOCS line, and the second one
    # is what separates `m = make(1); n = make(2)` from a shared environment.
    "mir_function_equality_captured": (2, 64),
    # Printing a container allocates NOTHING of its own -- mv_vts formats into
    # stack scratch.  What these count is the POOL BLOCK the literal itself
    # needed, measured off each generated program's IR-MIR-ALLOCS line: one
    # 256-byte block for a flat array, a dict or a set; two for the two nested
    # shapes, because `[[1, 2], [3]]` and `[1.5, 2]`-inside-a-dict each build a
    # second region.  The empty case reads one block: `a = []` needs none and
    # `d = {}` still takes the one count slot.
    "mir_print_array_whole": (1, 256),
    "mir_print_dict_whole": (1, 256),
    "mir_print_nested_depth": (2, 768),
    "mir_print_set_whole": (1, 256),
    "mir_print_mixed_tags": (2, 768),
    "mir_print_empty": (1, 256),
    # OP_STOP allocates nothing itself; the three plain stops are therefore
    # (0, 0) and split out of ALLOCS_DEFAULT only to say so.  The fourth
    # allocates one array literal before it stops, which is what makes the
    # reading tell the exit apart from `exit(0)` skipping the sweep: freed
    # and live=0 still have to come back from atexit(mv_free_all).
    "mir_stop_at_toplevel": (0, 0),
    "mir_stop_mid_loop": (0, 0),
    "mir_stop_inside_a_function": (0, 0),
    "mir_stop_after_allocating": (1, 256),
    # OP_BIND allocates nothing itself: the table is a file-scope int array and
    # the stop is a printf to stderr.  What these count is the SET LITERAL the
    # declaration builds, measured off each generated program's own
    # IR-MIR-ALLOCS line: one 256-byte pool block for a three-element set.
    # `mir_bind_plain_constraint` reads 0|0 because `x: 5` declares a
    # constraint that is not a set -- no set literal, and the emitter records
    # no bound (L_BIND's sidx == -1) -- so a nonzero count here would mean the
    # route built a set the program never wrote.  The three throwing cases
    # report `reported=no|why=rc=1` and no count is consulted, exactly like the
    # throw pair above: their message is on stderr and their exit happens
    # inside the run.
    "mir_bind_constrained_store": (1, 256),
    "mir_bind_initial_value": (1, 256),
    "mir_bind_int_probe_finds_float": (1, 256),
    "mir_bind_plain_constraint": (0, 0),
    # OP_YIELD allocates nothing (it is an arm that emits a note and no
    # statement, see the emitter's case), so the three plain yields are (0, 0)
    # and split out of ALLOCS_DEFAULT only to say so.  The fourth builds one
    # array literal before yielding and reads 1|256 -- the yield itself adds
    # nothing to the count, which is the claim: a route that boxed a yielded
    # value, or that allocated a "scheduler ticket", would move this row.
    # Both numbers MEASURED off the generated programs' own IR-MIR-ALLOCS lines
    # rather than derived.
    "mir_yield_at_toplevel": (0, 0),
    "mir_yield_mid_loop": (0, 0),
    "mir_yield_inside_a_function": (0, 0),
    "mir_yield_around_allocating": (1, 256),
    # THE ONE ROW THIS CELL HAD TO CORRECT.  It was first written as (0, 0),
    # before the implementation existed, on the reasoning that an arm that so
    # far only printed and exited carries no block -- a guess, and the rule this
    # file states ("measured first, then written") is there because guesses get
    # this wrong.  The measured reading is 1|256, and the block is not the
    # throw: it is the `x: 1, 2, 3` DECLARATION, the same 1|256 that
    # mir_bind_constrained_store has read since section 10.135.  The thrown
    # value itself allocates nothing -- mv_str hands back a RESIDENT string
    # (ival = 1 says "static storage", see its note), so a catch that prints
    # `type_mismatch` costs zero blocks.  Kept as a row, with the correction
    # written down, because a silently fixed expectation teaches nobody.
    "mir_try_catches_an_arm_stop": (1, 256),
    # OP_WAIT allocates nothing: the sleep is a libc call, not a pool block, and
    # nothing below builds a container.  These rows exist so that a future arm
    # that boxed the duration or asked the pool for a "timer" would move them.
    # MEASURED like every other row, not derived from "it should be zero".
    "mir_wait_at_toplevel": (0, 0),
    "mir_wait_zero": (0, 0),
    "mir_wait_inside_a_function": (0, 0),
    "mir_wait_with_a_variable_delay": (0, 0),
    "mir_wait_in_a_loop": (0, 0),
    # MEASURED like every other row, on the tree that answers these cases.
    # OP_RECORD is metadata-only here: no store is consulted, no name is
    # interned at run time and the following STORE_GLOBAL is a plain copy.
    "mir_record_assigns_the_value": (0, 0),
    "mir_record_two_names": (0, 0),
    "mir_record_a_store_tag": (0, 0),
    "mir_record_default_store": (0, 0),
    "mir_record_then_overwrite": (0, 0),
    # The INTERVAL cases.  MEASURED on the tree that answers them, and TWO of the
    # eight are not what the general shape suggested -- which is exactly why the
    # rule is "measured first, then written":
    #   a_non_number_bound   0|0, not 1|256: the whole VALUE becomes nil, so no
    #                        interval region is built at all (the arm checks the
    #                        bounds before it calls mv_set_interval).
    #   equality_by_bounds   2|768, not 1|256: the program builds TWO intervals
    #                        (p and q) and then a third (r), so the pool grows
    #                        past the first eight-slot block.
    #   the other six       1|256: one region of four slots inside the first block.
    "mir_interval_membership": (1, 256),
    "mir_interval_inclusive_upper": (1, 256),
    "mir_interval_open_upper_bound": (1, 256),
    "mir_interval_bounds_from_registers": (1, 256),
    "mir_interval_len_and_print": (1, 256),
    "mir_interval_a_non_number_bound_makes_nil": (0, 0),
    "mir_interval_equality_by_bounds": (2, 768),
    "mir_interval_inside_a_function": (1, 256),
    # The LOCK cases allocate nothing: a mutex is not a pool region, and the
    # three programs below build no container.  `mir_lock_around_a_container`
    # builds one array literal, so it reads 1|256 -- MEASURED, not inferred
    # from "the lock adds nothing".
    "mir_lock_block": (0, 0),
    "mir_lock_inside_a_loop": (0, 0),
    "mir_lock_return_from_body": (0, 0),
    "mir_lock_around_a_container": (1, 256),

    # ---------------------------------------------------------- `list` builtin
    # Measured FIRST on the tree that implements it, then written down; the
    # `list(5) / list(nil) / list("ab")` row was first written as 0|0 and the
    # other four were left to fail as 0|0 defaults, which is what the first run
    # reported -- the numbers below are that run's numbers, not a prediction:
    #   IR-MIR-ALLOCS|case=mir_list_of_a_set|allocs=1|bytes=256
    #   IR-MIR-ALLOCS|case=mir_list_mixed_set|allocs=2|bytes=768
    #   IR-MIR-ALLOCS|case=mir_list_of_an_array|allocs=1|bytes=256
    #   IR-MIR-ALLOCS|case=mir_list_of_scalars|allocs=0|bytes=0
    #   IR-MIR-ALLOCS|case=mir_list_of_an_interval|allocs=1|bytes=256
    # The four non-zero rows are the pool's first block (8 MirVal slots = 256
    # bytes); mixed_set asks for TWO because its two sets plus their two
    # destination arrays no longer fit in one, so the pool doubles.
    #
    # mir_list_of_an_array reads 1|256 rather than 0|0 even though list() COPIES
    # nothing: the program's own `a = [1, 2]` builds the array it returns.
    # mir_list_of_an_interval reads 1|256 for the same reason -- `p = [0, 3)` is
    # a four-slot interval region -- and list() adds NOTHING to it, which is the
    # point of the row: nil costs no block.
    "mir_list_of_a_set": (1, 256),
    "mir_list_mixed_set": (2, 768),
    "mir_list_of_an_array": (1, 256),
    "mir_list_of_scalars": (0, 0),
    "mir_list_of_an_interval": (1, 256),
    # The narrowing row's pool: `s` is one region of three slots, and the array
    # list() builds is four more (one count header + three counted items, of
    # which only two are written when the truncation collides -- the region is
    # sized by the set's len, not by the emitted count).  Eight slots fit the
    # first block, so one 256-byte allocation.  Measured, not predicted.
    "mir_list_narrows_wide_ints": (1, 256),
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


# THE STDERR CHANNEL, and it is a CONTAINMENT predicate on purpose -- these
# NAMED SUBSTRINGS must appear, nothing about the tail or the order is claimed.
#
# WHY NOT BYTE EQUALITY.  stderr is NOT a channel the two routes ever promised
# to spell the same, and one ALREADY-PINNED GREEN case proves it:
# mir_container_index_type_mismatch (the entry below, in MIR_ONLY) has stopped
# with rc=1 on both routes since it landed, and the two texts differ today in
# three places -- the interpreter prints a LEADING BLANK LINE and the
# `  at ip=11 frames=0` ledger line, this route prints neither, and this route
# appends the `IR-MIR-ALLOCS|...` line that atexit(mv_free_all) writes.  A
# byte-equality instrument would have been red on the day it landed, on a case
# that is green, which is the cheapest way to make an instrument get deleted.
# So the question is asked the way the two routes actually agree: does the
# message that the thrown VALUE decides appear, and no other message does.
#
# WHY THE EXCLUSION IS A SEPARATE TABLE.  `"[exception] uncaught: boom" not in
# er` is a NEGATIVE claim, and folding it into the positive tuple would make
# one cell answer two questions.  The second throw case carries BOTH: its own
# message must be present and the FIRST case's message must be absent --
# a route whose OP_THROW arm printed a constant would satisfy the first and
# fail the second, and a route that printed nothing fails the first.  One
# table of positives cannot see "printed the wrong throw's word".
STDERR_EXPECT = {
    "mir_throw_uncaught_top": ("[exception] uncaught: boom",),
    "mir_throw_uncaught_cross": ("[exception] uncaught: from-func",),
    # OP_BIND can stop with two DIFFERENT messages, and the interpreter decides
    # which by WHERE the value came from, not by its value: an assignment that
    # misses the bound throws `type_mismatch` (L_STORE_GLOBAL), an initialiser
    # that misses it throws `initial value out of range` (L_BIND).  Each case
    # demands its own and forbids the other, so one constant message cannot
    # satisfy both.
    "mir_bind_out_of_range_store": ("[exception] uncaught: type_mismatch",),
    "mir_bind_out_of_range_init": ("[exception] uncaught: initial value out of range",),
    # A finally body runs WHILE the error is still uncaught: the try pair pops
    # nothing on the way out, so `cleanup` prints and the process still exits 1
    # with the original message.  The criterion demands the message AND the
    # exit code (mir_try_finally_cleanup's execution row carries rc=1), so a
    # route that swallowed the error would fail both halves.
    "mir_try_finally_cleanup": ("[exception] uncaught: boom",),
}
STDERR_EXCLUDE = {
    "mir_throw_uncaught_cross": ("[exception] uncaught: boom",),
    "mir_bind_out_of_range_store": ("[exception] uncaught: initial value out of range",),
    "mir_bind_out_of_range_init": ("[exception] uncaught: type_mismatch",),
}


def stderr_problems(text, case):
    """Which ways this run's stderr disagrees with the containment criterion."""
    need = STDERR_EXPECT.get(case)
    if need is None:
        return []
    forbid = STDERR_EXCLUDE.get(case, ())
    out = []
    for want in need:
        if want not in (text or ""):
            out.append("%s: stderr does not contain %r, so the thrown value never reached"
                       " the message the criterion demands (got %r)"
                       % (case, want, (text or "")[:160]))
    for banned in forbid:
        if banned in (text or ""):
            out.append("%s: stderr CONTAINS %r, a message belonging to a DIFFERENT throw -- "
                       "the arm answered a constant instead of the register's value (got %r)"
                       % (case, banned, (text or "")[:160]))
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
#
# The next four are the CONSTANT spelling of the same question -- the one the
# compiler answers with OP_EQK/OP_NEQK instead of OP_EQ/OP_NEQ, and the one
# docs/STATUS.md 10.105 recorded as "refused on this route while the variable
# spelling was answered".  None of them existed before, so the route's refusal
# was invisible in the readings: a refusal is counted, compared, and then
# dropped, and `refused=0` said nothing about a program nobody wrote.
#
#   mir_eqk_string            the EQUALITY answer from a constant: `s == "hi"`
#                             (true) and `s == "ho"` (false).  Two truth values
#                             on purpose -- a route that answered the literal
#                             comparison as `true` unconditionally reddens on
#                             the second line, and the interpreter baseline is
#                             recomputed in the same run.
#   mir_eqk_string_ne         the same for OP_NEQK (`s != "hi"` false,
#                             `s != "ho"` true).  Separate from the equality
#                             case because OP_NEQK is a SECOND opcode with its
#                             own dispatch body (the note in
#                             src/compilation/aot_mir.c names both): one opcode
#                             can be lowered correctly while its sibling is
#                             not, and a case carrying only `==` would not see
#                             it.
#   mir_eqk_number            THE NECESSARY CONDITION, not an eqk case: with no
#                             string operand in sight the compiler emits
#                             OP_EQ/OP_NEQ (measured: `1 == 1` / `1 != 1` /
#                             `true == true` lower with OP_EQ alone), so this
#                             case says what must NOT change -- numeric and
#                             boolean constant comparison.  It is here because
#                             the failure it guards is the plausible one: an
#                             OP_EQK arm written into the NUMERIC path would
#                             answer the string cases by comparison of a
#                             coerced 0, and the way to notice is a case whose
#                             expected bytes prove the numeric route still
#                             compares numbers as numbers.
#   mir_eqk_string_cross      the constant on the LEFT (`"hi" == s`, `"ho" == s`).
#                             This is the direction case: the compiler
#                             NORMALISES both spellings into one stream
#                             (measured on this tree: `s == "hi"` and
#                             `"hi" == s` both dump `3,1,0,0 / 23,0,1,0 /
#                             22,1,0,0 / 46,2,1,1 / 30,2,0,0 / 33,0,0,0`, byte
#                             for byte the same), so what the criterion can bite
#                             here is where the LITERAL's VALUE comes from --
#                             the container's string pool at r3 -- versus the
#                             value at register r2.  A route that read the pool
#                             index as a register reddens here, and the case
#                             exists so that "which operand is the constant" is
#                             measured rather than assumed.
#
# THE THROW PAIR -- the first two programs here whose ANSWER is an EXIT CODE and
# a stderr line rather than stdout bytes.  Before them, `throw` was refused whole
# by the pre-scan (`OP_THROW` had no lowering case), which is honest but says
# nothing about the observable contract; the interpreter's contract is measured
# in /home/sakiko/inimerse/.scratch/ruling-exceptions.md §4.1 and copied:
# uncaught throw -> the program's stdout SO FAR, `[exception] uncaught: <value>`
# on stderr, rc 1 (src/vm/vm.c:2639-2643, `vm->last_error = 1` at :2662-2663).
#
#   mir_throw_uncaught_top     `say "a" / throw "boom" / say "b"` -- the TOP
#                             level throw.  Three properties in one file, and
#                             each has a failure it names: stdout is exactly
#                             `a\n` (the `say "b"` after the throw must NOT run
#                             -- a lowering that printed it would be answering
#                             a control-flow question wrongly), rc is 1 (a
#                             route that exited 0 or aborted at 134 is a
#                             different kind of wrong), and stderr CONTAINS
#                             `[exception] uncaught: boom` (STDERR_EXPECT, a
#                             containment predicate, not equality -- see the
#                             table's own note for the pinned green case whose
#                             two stderrs already differ).
#   mir_throw_uncaught_cross   `func f() { throw "from-func" }` called from main
#                             with no handler.  The throw happens INSIDE a
#                             container, so the exit is taken from a function's
#                             body, not main's -- and the message proves WHICH
#                             value was thrown: it must say `from-func`, and
#                             STDERR_EXCLUDE demands that `boom` (the other
#                             case's word) does NOT appear.  That exclusion is
#                             the criterion against the plausible fake: an arm
#                             that emitted `mv_die_uncaught(mv_str("boom"))`
#                             satisfies every positive substring and both
#                             stdout checks while answering nothing about its
#                             operand.  A single throw case could not see it.
#                             What this case does NOT claim: the interpreter's
#                             `  at ip=2 frames=1 -> ip=3` ledger is NOT
#                             reproduced (this route has no bytecode ip or
#                             frame ledger), and the two routes' stderr tails
#                             differ by the ALLOCS line on purpose.
#
# Neither case can join the shared corpus: the AST route refuses both programs
# (a string expression is outside the numeric subset), so there is no second
# generated file to compare against -- the interpreter remains the only oracle,
# which is the same reason every case in this list is MIR-only.
MIR_ONLY = [
    # len() over the value domain this route can actually hold.  The builtin is
    # refused today by name (why=builtin-call, 10.122), so this case is red on
    # arrival; the interpreter answers 3, 1, 4, 7, 2, 0, 0, 0 for the eight
    # shapes below (arrays, dicts, strings, ints, floats, nil, bools, empty).
    ("mir_len_of_every_shape_this_route_holds",
     'a = [1, 2, 3]\nsay len(a)\nd = {"x": 1}\nsay len(d)\nsay len("abcd")\n'
     'say len(7)\nsay len(2.9)\nsay len(nil)\nsay len(true)\nsay len("")\n'),
    # The throw pair goes FIRST because it is the only entry whose criterion
    # reads the STDERR channel and the exit code, and a reader who scans this
    # list should meet the exception case before the many length questions.
    ("mir_throw_uncaught_top", 'say "a"\nthrow "boom"\nsay "b"\n'),
    ("mir_throw_uncaught_cross",
     'func f() {\n    throw "from-func"\n}\nsay "before"\nf()\nsay "after"\n'),
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
    # The constant spelling: OP_EQK / OP_NEQK, plus the numeric control that
    # must not change and the constant-on-the-left direction case.  See the
    # block above MIR_ONLY for what each one is the criterion OF.
    ("mir_eqk_string", 's = "hi"\nsay s == "hi"\nsay s == "ho"\n'),
    ("mir_eqk_string_ne", 's = "hi"\nsay s != "hi"\nsay s != "ho"\n'),
    ("mir_eqk_number", 'say 1 == 1\nsay 1 != 1\nsay true == true\n'),
    ("mir_eqk_string_cross", 's = "hi"\nsay "hi" == s\nsay "ho" == s\n'),
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
    # OP_OR, EXECUTED rather than merely emitted.  The corpus has this opcode in
    # three programmes' lowered streams and not one interpreter run of the
    # corpus ever executes it: `X or Y` short-circuits when X is true, so the
    # OP_OR itself is jumped over.  That is a CORPUS gap, not a correctness
    # gap -- `a or b` compiles differently depending on which side is false --
    # and the reading that names the gap is IR-AOT-MIR-UNEXECUTED.
    #
    # So every line here that is meant to run OP_OR has a FALSE left operand:
    #   `a or b`    false or true  -> the OP_OR runs and yields the right side.
    #   `a or c`    false or false -> the OP_OR runs and yields false: a print
    #               arm that answered a constant `true` would be caught here and
    #               not by the line above.
    #   `a or a`    the same register twice through the OP_OR.
    #   `b or a`    the CONTROL: a TRUE left operand, so the OP_OR is JUMPED
    #               OVER.  Its answer is still compared, which is what makes the
    #               case able to tell "short-circuits" apart from "always runs
    #               the OR" -- changing either shape changes the bytes.
    #   `a and b` / `b and a`  the mirror shape for OP_AND (and short-circuits on
    #               a false left, so only `b and a` reaches the AND).  These two
    #               lines are not what this case is for; they are here so the
    #               short-circuit rule is measured in both directions in one
    #               program instead of asserted in a comment.
    ("mir_short_circuit_or",
     'a = false\nb = true\nc = false\n'
     'say a or b\nsay a or c\nsay a or a\nsay b or a\n'
     'say a and b\nsay b and a\n'),
    # ---- the container step: OP_NEW_ARRAY and the read-only OP_INDEX_GET ----
    #
    # The first ask whose ANSWER is a heap OBJECT and not a scalar: `a =
    # [1, 2, 3]` is one array literal, and `a[1]` is a read off it.  Printed
    # ELEMENT BY ELEMENT so the criterion is byte-exact on stdout -- `say a`
    # (the object itself) is a FORMATTING answer, not this step's, and stays an
    # abort boundary named in docs/STATUS.md; every line below reads one element
    # through OP_INDEX_GET and prints that value, which is the closed loop.
    # What each one is the criterion OF:
    #   mir_container_new_array        three integers, every element read by a
    #                        CONSTANT subscript.  An emitter that built the
    #                        array but read the wrong register answers 1,3,2 or
    #                        misses; the operand layout (r1 = destination, r2 =
    #                        UNREAD, r3 = element count, elements from the last
    #                        r3 pushed values) is copied from
    #                        src/vm/vm.c:3620-3647 and
    #                        src/compiler/bytecode.h:14.
    #   mir_container_new_array_mixed  a string, an integer and a boolean in one
    #                        literal.  The array is a heap OBJECT holding
    #                        32-byte slots, so a route that copied only the
    #                        numeric tag, or that stored pointers into its own
    #                        stack, prints the wrong bytes here.
    #   mir_container_index_get an array whose two elements ARE arrays, read
    #                        through two subscript levels.  The element copy has
    #                        to keep a nested handle alive after the outer
    #                        literal is gone; a route that flattened the copy
    #                        prints nil.
    #   mir_container_index_edges NEGATIVE, past-the-end, float, and boolean
    #                        subscripts -- all of them COMPUTED (`z - 1`, not
    #                        `-1`), because a CONSTANT subscript this route can
    #                        prove out of range is refused before generation
    #                        (the reverse criterion below) and could not be
    #                        measured here.  The arithmetic is what makes them
    #                        unprovable to the pre-scan and run-time-only to this
    #                        route, which is exactly the arm this case owns.
    #                        These are the runtime arms of src/vm/vm.c:3675-3716:
    #                        an out-of-range read answers nil "compat", `1.9`
    #                        truncates to 1, and `true` converts to 1 through
    #                        the tag-checked val_as_double, not through the
    #                        union's double slot.  A route that silently
    #                        answered 0 on a miss differs in bytes here, and the
    #                        exit code would not notice.
    #   mir_container_new_array_empty  an EMPTY literal, read at run time.  n
    #                        == 0 means no PUSH_REG at all, so the generated code
    #                        must not reference an argument stack it never
    #                        declared, and the read must answer nil rather than
    #                        read item 0 of a zero-element block.
    #   mir_container_string_index     the OTHER container arm OP_INDEX_GET has
    #                        (src/vm/vm.c:3700-3711): a string subscripted with a
    #                        positive, a past-the-end and a NEGATIVE index.
    #                        OP_INDEX_GET is one opcode with two container arms,
    #                        and a route that answered a string container with
    #                        nil would print nil where the interpreter prints a
    #                        character -- the silent-wrong-answer shape this
    #                        list exists to redden, on a program the subset CAN
    #                        spell.
    #   mir_container_index_type_mismatch
    #                        nil as the subscript.  The interpreter throws
    #                        type_mismatch, prints NOTHING on stdout and exits 1
    #                        (src/vm/vm.c:323-334 reached through L_INDEX_GET),
    #                        so this case asserts rc AND bytes: a route that
    #                        coerced the nil to 0 would print `7` and exit 0, and
    #                        both halves of that mismatch are visible in the one
    #                        reading.
    ("mir_container_new_array",
     'a = [1, 2, 3]\nsay a[0]\nsay a[1]\nsay a[2]\n'),
    ("mir_container_new_array_mixed",
     'a = ["hi", 2, true]\nsay a[0]\nsay a[1]\nsay a[2]\n'),
    ("mir_container_index_get",
     'b = [[5, 6], [7]]\nsay b[0][1]\nsay b[1][0]\nsay b[0][0]\n'),
    ("mir_container_index_edges",
     'a = [7, 8, 9]\nz = 0\n'
     'neg = z - 1\nsay a[neg]\n'
     'past = z + 3\nsay a[past]\n'
     'fl = z + 1.9\nsay a[fl]\n'
     'bo = z == 0\nsay a[bo]\n'
     'say a[z]\n'
     'two = z + 2\nsay a[two]\n'),
    ("mir_container_new_array_empty",
     'a = []\ni = 0\nsay a[i]\n'),
    ("mir_container_string_index",
     's = "hi"\ni = 0\nsay s[i]\nj = 1\nsay s[j]\nk = 2\nsay s[k]\n'
     'm = 0 - 1\nsay s[m]\n'),
    ("mir_container_index_type_mismatch",
     'a = [7, 8, 9]\nsay a[nosuchglobal]\n'),
    # OP_INDEX_SET -- the WRITE half of the container step, and the first cell
    # in which a literal stops being read-only.  Read the handler before any of
    # these: `L_INDEX_SET` (src/vm/vm.c:3717) takes r1 = the CONTAINER, r2 = the
    # SUBSCRIPT, r3 = the VALUE (src/compiler/bytecode.h:16 spells it
    # `reg[r1][r2] = reg[r3]`), and r1 is not a destination -- the register's
    # own value survives the write.  Three things there are copied exactly and
    # none of them is invented here: the subscript is converted INSIDE the
    # array branch (so a bad subscript on a non-array is a SILENT no-op, not a
    # throw); a write past the end EXTENDS the array with nil fill
    # (src/vm/vm.c:983-989) while a negative subscript throws
    # index_out_of_range (src/vm/vm.c:3722); and the stored value is the value
    # (a string stays a string), with the clone-before-store order at
    # src/vm/vm.c:990-997 being what makes `a[0] = a[2]` safe.
    #
    #   mir_index_set_int   an int written at a constant subscript and at a
    #                       COMPUTED one (`i = z + 1`, never a constant: a
    #                       constant in range is fine for the pre-scan, but a
    #                       constant is the one subscript shape that could
    #                       answer without the runtime arm, so the arm is
    #                       driven by arithmetic).
    #   mir_index_set_overwrite
    #                       the same slot written twice, then read through the
    #                       ALIAS `b = a` -- an array is a handle, so the
    #                       interpreter's `b` sees what `a` stored, and a route
    #                       that copied the array on assignment would print the
    #                       OLD value here while passing the rc check.
    #   mir_index_set_grow   a write PAST the end (index 5 on a 3-element
    #                       array): the interpreter grows it and fills the hole
    #                       with nil, so `say a[4]` prints nil and `say a[3]`
    #                       prints nil while a[5] prints the stored value.  The
    #                       second half grows an array that is NOT the newest
    #                       one, which is the case a flat pool with no
    #                       forwarding would answer with the old length.
    #   mir_index_set_edges a non-array container (`x = 5`, then `x[...] = 7`)
    #                       is a silent no-op -- the interpreter's `say x` still
    #                       prints 5 -- and a string stored into an int slot
    #                       stays a string.  It ends on a nil subscript, which
    #                       throws type_mismatch: stdout keeps the lines
    #                       printed BEFORE the throw and rc is 1, so both
    #                       halves are asserted in one reading.
    #   mir_index_set_negative
    #                       a COMPUTED negative subscript (`n = z - 1`) throws
    #                       index_out_of_range, with the line printed before it
    #                       still on stdout and rc 1.  Asymmetric with the read
    #                       side on purpose: `say a[n]` answers nil for the same
    #                       n (src/vm/vm.c:1002), the write refuses it
    #                       (src/vm/vm.c:3722), and both readings are here so
    #                       that a route which quietly reused the read side's
    #                       answer goes red.
    ("mir_index_set_int",
     'a = [1, 2, 3]\na[0] = 99\nsay a[0]\nsay a[1]\n'
     'z = 0\ni = z + 1\na[i] = 42\nsay a[1]\nsay a[2]\n'
     'j = z + 2\na[j] = 7\nsay a[j]\n'),
    ("mir_index_set_overwrite",
     'a = [1, 2, 3]\nb = a\nz = 0\nq = z + 1\n'
     'a[q] = 77\na[q] = 88\nsay a[q]\nsay b[q]\n'
     'a[0] = a[2]\nsay a[0]\nsay a[1]\nsay a[2]\n'),
    ("mir_index_set_grow",
     'a = [1, 2, 3]\nz = 0\np = z + 5\na[p] = 9\n'
     'q = z + 4\nsay a[p]\nsay a[q]\n'
     'r = z + 3\nsay a[r]\nsay a[2]\nsay a[0]\n'
     'b = [7]\nc = [9, 9]\ns = z + 3\nb[s] = 8\nt = z + 1\n'
     'say b[s]\nsay b[t]\nsay b[0]\nsay c[0]\n'),
    ("mir_index_set_edges",
     'x = 5\nx[nosuchglobal] = 7\nsay x\n'
     's = "hi"\na = [1, 2, 3]\na[1] = s\nsay a[1]\n'
     'a[nosuchglobal] = 1\n'),
    ("mir_index_set_negative",
     'a = [1, 2, 3]\nz = 0\nsay a[1]\nn = z - 1\na[n] = 77\nsay a[0]\n'),
    # OP_MIN / OP_MAX, the SET REDUCTIONS -- not an arithmetic pair.  Read the
    # handlers before the names: `L_MIN`/`L_MAX` (src/vm/vm.c:4357) call
    # `set_minmax(vm, &R[ins.r2], &R[ins.r1], 0/1)` and `set_minmax`
    # (src/vm/vm.c:2476) begins by answering VAL_NIL; it only looks at the
    # operand when `src->type == VAL_SET`, and there it folds a set's int/float
    # elements (exact long long for int-like, double otherwise) or its strings
    # (strcmp), refusing a mix of string and number.  There is no two-operand
    # min: `min(1, 2)` is the interval-SET spelling, compiled into
    # OP_SET_INTERVAL (TOK_MIN in src/lexer/lexer.c:47, the UNARY rule at
    # src/parser/parser.c:640).
    #
    # MirVal has no set tag (MV_INT 0, MV_FLT 1, MV_BOO 3, MV_NIL 5, MV_STR), and
    # every producer of one is refused by prescan BEFORE OP_MIN is reached:
    # OP_NEW_SET / OP_SET_INTERVAL are not lowered, the 23 preset set globals are
    # refused by name (src/compilation/aot_mir.c:1306), and a literal `min(1, 2)`
    # leaves the argument stack unbalanced (asp=2).  Measured on this tree, not
    # recalled: `p = 1, 2, 3` + `say min(p)` refuses with "illegal-operand ...
    # leaves the argument stack unbalanced: asp=3" and `say min(Z)` with "global
    # 1 is the preset set global 'Z'".  So on every stream this route accepts
    # the operand is a non-set and the interpreter's own answer is VAL_NIL --
    # exactly, by the handler's first statement.  The value-domain argument
    # itself lives in the emitter note in src/compilation/aot_mir.c; these cases
    # are what it looks like from outside, and they are the ones that go red if
    # that agreement breaks (reversed direction, an operand read from the wrong
    # register, a popped register).
    #
    # WHAT THESE COVER: ints of both signs; the i32/i64 boundary values (no
    # arithmetic is done, so nothing here can overflow); floats including -0.0
    # (stdout prints `0` either way, so the SIGN is not observable through this
    # route -- and -0.0 as a set ELEMENT cannot even be written, `p = -0.0, 0.0`
    # is a parse error); a scalar string, which answers nil and NOT the string,
    # and never the strcmp rule (that rule only exists for set ELEMENTS); nil
    # itself; and `min(x)`/`max(q)` on stored variables, the spelling whose
    # operand comes from OP_LOAD_GLOBAL instead of a literal.
    # NaN is NOT covered and cannot be: the only way to make one is a builtin
    # call (`sqrt(-1)` prints `nan`), OP_CALL_BUILTIN is not lowered, and a set
    # literal holding a variable (`p = 0.0, q`) is refused with asp imbalance
    # before OP_MIN.  That is the honest boundary, recorded in STATUS 10.112.
    ("mir_minmax_int", 'say min(5)\nsay max(5)\nsay min(-7)\nsay max(-7)\n'),
    ("mir_minmax_boundary",
     'say min(2147483647)\nsay max(-2147483648)\n'
     'say min(9223372036854775807)\nsay max(-9223372036854775807)\n'),
    ("mir_minmax_float",
     'say min(-0.0)\nsay max(-0.0)\nsay min(0.5)\nsay max(-0.25)\nsay min(0.0)\n'),
    ("mir_minmax_string", 'say min("b")\nsay max("b")\nsay min("")\n'),
    ("mir_minmax_nil", 'say min(nil)\nsay max(nil)\n'),
    ("mir_minmax_var", 'x = 3\nq = -2\nsay min(x)\nsay max(q)\n'),

    # ---- OP_IN, and what the interpreter's own handler answers -------------
    #
    # The operand order is copied, not guessed: src/compiler/compiler.c:727 emits
    # `emit(comp->curBC, OP_IN, result, left, right)` and the VM reads it as
    # `set_contains_or_subset(vm, &R[ins.r2], &R[ins.r3])` writing R[ins.r1]
    # (src/vm/vm.c:4351-4356).  So r2 is `x`, r3 is the container, r1 gets a
    # VAL_BOOL.  src/compiler/bytecode.h:48 spells it "in: r1 = (r2 in r3)".
    #
    # The answers below are the handler's, in this order: `b` VAL_SET ->
    # subset-or-member against a set (src/vm/vm.c:2381, the first two arms);
    # `b` VAL_ARRAY -> `a` VAL_ARRAY is a SUBSET by val_eq
    # (src/vm/vm.c:2386-2390, an empty `a` answers 1 and a malformed handle
    # answers 1 too), `a` VAL_SET is a finite-set subset (:2392-2404), anything
    # else is membership by val_eq (:2406-2411); `b` anything else (a DICT, a
    # string, an int, nil) -> `return 0` at src/vm/vm.c:2417-2420.  That last
    # line is why mir_in_string is NOT a substring test and a dict is NOT a key
    # test: the interpreter has no such arm, and a lowering that invented one
    # would print `true` where the interpreter prints `false`.
    #
    # val_eq itself (src/vm/vm.c:413) is one rule: same-tag gate first, then
    # VAL_ARRAY/VAL_DICT compare `ival`, i.e. the HANDLE -- array equality is
    # IDENTITY, not element-wise -- and int/float/bool coerce through
    # `l == (long long)r` (:425-427).  mir_in_array_hit's `1.0 in [1, 2]`
    # (true) and mir_in_computed's `[x - 1] in c` (false even though the values
    # are equal) are the two halves of that one rule, and they are the lines a
    # value-wise array equality would flip.
    #
    # WHAT THESE COVER: the array membership arm with int, string, nil and
    # float-vs-int elements; an ARRAY operand (the subset arm, both true and
    # false, including NESTED arrays compared by identity); an empty operand
    # (subset true) against an empty container (member false); the string arm
    # -- the spelling `"b" in "abc"` a user may reach for, answered false by
    # the handler (:2417), with a real one-element array on the line above it so
    # a route that confused "a string is a container" with "a string is a
    # member" cannot pass both halves at once (`not` too); the scalar arm; and
    # computed operands (both sides through arithmetic, through a variable, and
    # inside `if`/`not`), so the registers are not literal constants.
    #
    # WHAT THEY CANNOT COVER: a dict or a set.  Both are STRUCTURAL: prescan
    # refuses the program before OP_IN is reached -- `d = {"k": 1}` refuses with
    # "container=0 leaves the argument stack unbalanced: asp=2" (OP_NEW_DICT's
    # pops are not counted), a set literal `p = 1, 2, 3` with asp=3, and the 23
    # preset set globals by name (the 'Z' check in src/compilation/aot_mir.c).
    # So on every stream this route accepts, the L_IN set arm is unreachable and
    # its array arm sees integers, strings, nil, floats, bools and handles --
    # named in docs/STATUS.md §10.115, not silently assumed.
    #
    # mir_in_computed's LAST line, `say [1] != c[0]`, is the OP_NEQ spelling of
    # the array-identity row the subset line above needs: without that arm the
    # two spellings would answer `==` (`[1] == [1]` false) and stop the program
    # on `!=` -- the asymmetry the equality note in src/compilation/aot_mir.c
    # exists to refuse.  It sits in this case because a membership test is what
    # made the row reachable at all.
    ("mir_in_array_hit",
     'a = [1, 2, 3]\n'
     'say 2 in [1, 2, 3]\n'
     'say 2 in a\n'
     'say "b" in ["a", "b"]\n'
     'say nil in [nil, 1]\n'
     'say 1.0 in [1, 2]\n'
     'say [1, 2] in [1, 2, 3]\n'
     'say [] in [1, 2, 3]\n'),
    ("mir_in_array_miss",
     'a = [1, 2, 3]\n'
     'say 5 in [1, 2, 3]\n'
     'say 5 in a\n'
     'say "bc" in ["a", "b"]\n'
     'say 2.5 in [1, 2]\n'
     'say [1, 4] in [1, 2, 3]\n'
     'say 7 in []\n'
     'say [1, 2] in []\n'),
    ("mir_in_string",
     'say "b" in "abc"\n'
     'say "abc" in "abc"\n'
     'say "" in "abc"\n'
     'say "" in ""\n'
     'say "b" in ["a", "b"]\n'
     'say not ("b" in "abc")\n'),
    ("mir_in_scalar",
     'say 1 in 5\n'
     'say 1.0 in 5\n'
     'say nil in 5\n'
     'say 1 in nil\n'
     'say "a" in 1\n'
     'say true in 1.5\n'),
    ("mir_in_computed",
     'a = [1, 2, 3]\n'
     'x = 1 + 2\n'
     'say x in a\n'
     'say (x - 1) in a\n'
     'say (x * 2) in a\n'
     'b = [x - 1, x]\n'
     'say x in b\n'
     'if x in a { say "T" } else { say "F" }\n'
     'say not (x in b)\n'
     'c = [[1], [2]]\n'
     'say [x - 1] in c\n'
     'say x in c\n'
     'say [1] != c[0]\n'),
    # THE DICT PAIR -- OP_NEW_DICT and the dict branch of the read-only
    # OP_INDEX_GET.  Same criterion shape as the container cases above: stdout
    # BYTES against the interpreter, no expected string anywhere.  What each
    # one is the criterion OF:
    #   mir_dict_new_get    two pairs, each value read by a STRING subscript
    #                       the compiler lowers to a constant register.  An
    #                       lowering that pushed key/value in the wrong order
    #                       (value,key) answers 1 where the interpreter answers
    #                       "a"'s value, or nil.
    #   mir_dict_missing_key
    #                       a key the literal does not contain.  The
    #                       interpreter's vm_dict_get answers nil (src/vm/vm.c:1149-1170);
    #                       a route that answered the INTEGER 0 there differs
    #                       in bytes and not merely in rc -- this case is the
    #                       one docs/STATUS.md's reverse criterion reads.
    #   mir_dict_key_register
    #                       a key carried in a REGISTER, not a literal,
    #                       and read twice: the pair scan has to compare
    #                       KEY CONTENT (val_eq -> mv_eq's string row,
    #                       src/vm/vm.c:417), not the two values' ival.  Both
    #                       resident keys have ival == 1, so an integer
    #                       comparison answers the FIRST pair's value for
    #                       every lookup and prints 1, 1 here.
    #   mir_dict_position_key
    #                       an INT subscript, which the interpreter's dict arm
    #                       does NOT treat as a key: it reads the pair at
    #                       position i and returns its KEY (src/vm/vm.c:3683-3696),
    #                       and a position past the last pair answers nil.
    #                       `z` and the arithmetic make the subscripts
    #                       non-constant so the pre-scan cannot fold them.
    ("mir_dict_new_get",
     'd = {"a": 1, "b": 2}\n'
     'say d["a"]\n'
     'say d["b"]\n'),
    ("mir_dict_missing_key",
     'd = {"a": 1}\n'
     'say d["nosuch"]\n'
     'say d["a"]\n'),
    ("mir_dict_key_register",
     'd = {"a": 1, "b": 2}\n'
     'k = "b"\n'
     'say d[k]\n'
     'say d[k]\n'
     'say d["a"]\n'),
    ("mir_dict_position_key",
     'd = {"a": 1, "b": 2}\n'
     'z = 0\n'
     'say d[z]\n'
     'i = z + 1\n'
     'say d[i]\n'
     'j = z + 2\n'
     'say d[j]\n'),
    # The WRITE half.  Before this step `d["a"] = 9` was emitted but a SILENT
    # no-op (mv_arr_set returns immediately for a non-array container), which is
    # the worst of the three possible answers: it prints the OLD value and looks
    # like an answer.  The overwrite case is what catches that -- the key exists,
    # so a no-op and a correct write differ in the second line and only there.
    ("mir_dict_set_overwrite",
     'd = {"a": 1, "b": 2}\n'
     'say d["a"]\n'
     'd["a"] = 9\n'
     'say d["a"]\n'
     'say d["b"]\n'),
    # A key that is NOT there: the pair is appended and reachable afterwards.
    # vm_dict_set clones key and value before touching the pool
    # (src/vm/vm.c:1186-1191), which is what makes `d[k] = d[k]` defined; this
    # case is the plain append, and the third write grows the same dict twice.
    ("mir_dict_set_append",
     'd = {"a": 1}\n'
     'd["b"] = 7\n'
     'say d["b"]\n'
     'say d["a"]\n'
     'd["c"] = 3\n'
     'say d["c"]\n'),
    # The append that CANNOT extend in place: an array is created after the
    # dict, so the dict's region no longer ends at the pool frontier and the
    # write must move it and leave a forwarding head.  The array is read back in
    # the same program, because a move that trampled a neighbour shows there
    # first.
    ("mir_dict_set_grow_after_neighbour",
     'd = {"a": 1}\n'
     'arr = [1, 2, 3]\n'
     'd["b"] = 7\n'
     'say d["b"]\n'
     'say d["a"]\n'
     'say arr[2]\n'
     'say arr[0]\n'),
    # THE SET KEY PAIR -- a dict key is accepted by `val_eq` in the interpreter
    # (src/vm/vm.c:1144 dict_hash_find, and the same test in vm_dict_set's
    # lookup), and `val_eq` has no VAL_SET row: its same-type chain falls out at
    # VAL_SET (src/vm/vm.c:413-428 lists INT, FLOAT, STRING, BOOL, NIL, ARRAY,
    # DICT, FUNCTION) and the cross-type tail wants two numbers, so a set answers
    # FALSE against a set, against itself and against anything else.  The
    # interpreter therefore ACCEPTED a pair it can never find again: `d[s] = 7;
    # say d[s]` prints `nil` there.  The MIR route compared keys with mv_eq, which
    # gained a set arm (by CONTENT, correctly, for OP_EQ/OP_NEQ in 10.127), so
    # the same program printed `7` -- the route's answer there agreed with nothing.
    # These two cases were RED on the merge base as IR-MIR-EXEC-DIFF (measured
    # there first, written here second):
    #   lookup  interp b'nil\nnil\n1\n' / mir b'7\n7\n1\n'
    #   append  interp b'2\nnil\n'      / mir b'1\n9\n'
    # The `t` operand is why: `s = 1, 2` and `t = 1, 2` are two regions with
    # content-identical sets, so a content rule hits where an identity rule
    # (or val_eq's no-row rule) does not -- and neither case can pass by
    # answering only one of them.
    ("mir_set_dict_key_lookup",
     'd = {"a": 1}\n'
     's = 1, 2\n'
     't = 1, 2\n'
     'd[s] = 7\n'
     'say d[s]\n'
     'say d[t]\n'
     'say d["a"]\n'),
    # The WRITE half's criterion, and it reads the PAIR COUNT rather than a
    # lookup: the interpreter appends `d[t] = 9` as a SECOND pair (its lookup
    # answered false), so len is 2 and the first pair's value stays 7 behind a
    # key that cannot be found; a route whose write-side comparison matched by
    # content overwrote instead and reported len 1 / d[s] 9.  Both lines
    # discriminate, and the dict never moves here (it is created after the two
    # sets), so len's own reading is pair count and nothing else.
    ("mir_set_dict_key_append",
     's = 1, 2\n'
     't = 1, 2\n'
     'd = {}\n'
     'd[s] = 7\n'
     'd[t] = 9\n'
     'say len(d)\n'
     'say d[s]\n'),
    # len() THROUGH A MOVED CONTAINER.  Both containers here are copied to the
    # pool frontier when a later region owns the slots above them, leaving a
    # forwarding head at the old handle (mv_arr_live's note in aot_mir.c).  The
    # header of a LIVE region is MV_INT (its item count); the forwarding head is
    # MV_ARR whose ival is the NEW handle.  mv_arr_get and mv_dict_get/dict_set
    # all call mv_arr_live first; mv_len read `(int)a.ival` straight and therefore
    # read the HEAD as a count.  Measured on the merge base, written here:
    #   dict moved   interp b'2\n1\n2\n'   / mir b'3\n1\n2\n'   (head 7 -> n/2 = 3)
    #   array moved  interp b'9\n3\n3\n'   / mir b'9\n7\n3\n'   (head 7 -> 7)
    # The dict case is a DICT (n/2) so the wrong answer is 3, not 7; the array
    # case is an ARRAY (n as-is) so the same head prints 7.  Neither case can pass
    # by fixing one arm only.  The array's write uses a VARIABLE subscript because
    # a constant one outside the literal is refused at translate time
    # (why=index-out-of-range-constant) -- that refusal is why this program exists
    # rather than `a[2] = 9` on a two-element literal.
    ("mir_len_after_a_dict_moves",
     'd = {"a": 1}\n'
     's = 1, 2\n'
     'd["b"] = 2\n'
     'say len(d)\n'
     'say d["a"]\n'
     'say d["b"]\n'),
    ("mir_len_after_an_array_moves",
     'a = [1, 2]\n'
     'b = [3, 4]\n'
     'i = 2\n'
     'a[i] = 9\n'
     'say a[i]\n'
     'say len(a)\n'
     'say b[0]\n'),
    # DICT EQUALITY.  The interpreter answers by IDENTITY, not by content
    # (src/vm/vm.c:421 `if (a->type == VAL_DICT) return a->ival == b->ival;`), so
    # two dicts built from the same literal are NOT equal while a copy is.  Before
    # this step mv_eq had no MV_DIC row and the operand reached mv_as_nv, which
    # ABORTS: the route had no answer at all (rc 134) where the interpreter had
    # three (false, true, true).  A criterion is needed rather than "it will
    # never come up": `f = d` gives two handles to one dict, which is how a
    # program says "this one".
    ("mir_dict_identity_eq",
     'd = {"a": 1}\n'
     'e = {"a": 1}\n'
     'say d == e\n'
     'say d == d\n'
     'f = d\n'
     'say f == d\n'),
    # The other side of the same row: `!=` must negate it, and a dict compared
    # with an ARRAY must be false rather than a stop -- val_eq falls through its
    # same-type chain to `return false` (src/vm/vm.c:428), so "different
    # containers" is an ANSWER, not a type error.
    ("mir_dict_neq_and_cross_type",
     'd = {"a": 1}\n'
     'e = {"a": 2}\n'
     'say d != e\n'
     'say d == e\n'
     'arr = [1, 2]\n'
     'say d == arr\n'
     'say d != arr\n'),
    # ---------------------------------------------------------- closures
    #
    # The closure group is three opcodes read out of src/vm/vm.c rather than
    # guessed: OP_MAKE_FUNC (:4040-4054) pops `r3` values off the argument
    # stack into a fresh environment and stores VAL_FUNCTION with `r2` as the
    # function index; OP_LOAD_CAPTURE (:4033-4039) reads slot `r2` of the
    # frame's environment (nil when there is none); OP_CALL_VALUE (:4056-4057)
    # requires register `r1` to hold VAL_FUNCTION and then REUSES the ordinary
    # call path with the function index taken from the value (:4058-4069).
    #
    # All five programs below are lowered: the capture-free form, one capture
    # read, higher order, a returned function with a transitive capture, and a
    # zero-argument call over a heap string.  The last three were written and
    # red on `stream/mirval-closure` before any capture existed; they came in
    # with the SAME mechanism as the capture read, because the compiler emits
    # the transitive capture as an ordinary re-load of the parent capture
    # before the inner OP_MAKE_FUNC.  That is the finding, not an accident --
    # so they are criteria here and not a promise, and each of them still
    # distinguishes a wrong route from a right one by its own reading (15,15
    # vs 15,10 for a shared environment; 3 vs 6 for a middle-frame-only copy;
    # a stack read before the first push for the zero-argument call).
    #
    # The program was run through the interpreter first (`build/inimerse
    # --no-mods run`); that reading is the oracle, and the route's answer
    # before this step was a pre-scan refusal with why=unsupported-opcode and
    # the opcodes on IR-MIR-NOT-YET.
    #
    # NO CAPTURE.  `() -> 42` is a function VALUE: it must print as
    # `<function>` (src/vm/vm.c:1379), be TRUE in the truth position
    # (vm_truthy's `default: return 1`, :400), survive a copy into another
    # variable, and be callable THROUGH that copy -- the copy is OP_MOV and
    # the call is OP_CALL_VALUE, so this is the smallest program that touches
    # all four.  A lowering that printed the integer index, or that made the
    # copy un-callable, reddens on line 1 or 3.
    ("mir_closure_value_nocapture",
     'f = () -> 42\n'
     'say f\n'
     'if f { say "truthy" }\n'
     'g = f\n'
     'say g()\n'),
    # CAPTURE READ.  `make` returns a lambda over its own local `n`, and the
    # frame that held `n` is GONE by the time the lambda runs (the interpreter
    # pops it at L_RETURN, src/vm/vm.c:4126-4142), so the value has to carry
    # its environment: OP_MAKE_FUNC (:4040-4054) pops `r3` values off the
    # argument stack into a FRESH ImClosureEnv for every instantiation, and
    # OP_LOAD_CAPTURE (:4033-4039) reads slot `r2` of the environment the
    # running frame was entered with (nil when it has none).
    #
    # TWO INSTANTIATIONS IN ONE PROGRAM are what make this a per-CLOSURE
    # criterion and not a per-function one: `make(10)` and `make(3)` answer
    # 15 then 10, and a route that put both environments on one slot -- or
    # that kept one env per function index -- would answer 15 then 15.  Same
    # three lines of output, same byte count, one number different: that is
    # the reading, and no shape comparison can see it.  `say f` pins the
    # other half: the value now carries a live environment and must still
    # print `<function>` (:1379), which reddens an arm that walks into the
    # environment instead of ignoring it.
    #
    # Oracle first: the program was run through the interpreter
    # (`build/inimerse --no-mods run`), which printed
    #     <function>
    #     15
    #     10
    # and exited 0.  The route's answer before this step was
    # why=illegal-operand (OP_LOAD_CAPTURE had no op_regs entry), with
    # prescan's OP_MAKE_FUNC arm refusing a nonzero capture count by name
    # behind it.
    ("mir_closure_capture_read",
     'func make(n) {\n'
     '  return x -> x + n\n'
     '}\n'
     'f = make(10)\n'
     'say f\n'
     'say f(5)\n'
     'g = make(3)\n'
     'say g(7)\n'),
    # HIGHER ORDER.  A lambda is passed as an ARGUMENT to a user function and
    # called there; the callee register inside `apply` is a PARAMETER, not a
    # name the compiler resolved, so the call is OP_CALL_VALUE on a value that
    # arrived through the argument stack.  The third line passes a closure
    # that already carries an environment through the same function, so the
    # dispatch has to pass `self` down; a route that only made direct-call
    # function names callable cannot print 42/42/15.
    ("mir_closure_higher_order",
     'func apply(g, x) { return g(x) }\n'
     'say apply(x -> x * 2, 21)\n'
     'say apply(x -> x + 1, 41)\n'
     'func make(n) { return x -> x + n }\n'
     'say apply(make(10), 5)\n'),
    # RETURNED FUNCTION and a TRANSITIVE capture.  `make` returns a function;
    # the caller calls it.  `outer(1)` returns a function whose body builds
    # ANOTHER function over both `a` and `b`: the innermost reads `a` through
    # the middle closure's environment, which is the compiler's
    # transitive-capture arm -- it RE-LOADS the parent capture as an ordinary
    # value before the inner OP_MAKE_FUNC (src/compiler/compiler.c:1297-1302),
    # so that read is the same OP_LOAD_CAPTURE this cell added.  A route that
    # copied only the middle frame's own captures prints 3 instead of 6 (or
    # nil), so the two lines are not redundant.
    ("mir_closure_return_value",
     'func make(n) { return x -> x + 1 }\n'
     'f = make(9)\n'
     'say f(4)\n'
     'func outer(a) { return b -> (c -> a + b + c) }\n'
     'g = outer(1)\n'
     'h = g(2)\n'
     'say h(3)\n'),
    # ZERO-ARGUMENT call of a closure over a HEAP-OWNED string.
    # `"hi" + "there"` allocates before it is captured; the closure is called
    # with no arguments, so OP_CALL_VALUE must not read the argument stack at
    # all (an implementation that always formatted `mvargs[asp-1]` would read
    # one slot before the first push).  The captured string has to survive
    # after `make` returns: "hithere" is the whole assertion.
    ("mir_closure_zeroarg_string",
     'func make(s) { return () -> s }\n'
     'f = make("hi" + "there")\n'
     'say f()\n'),
    # FUNCTION EQUALITY, capture-free.  `==` on two function values used to
    # STOP the program: mv_eq has no MV_FUN row, so the comparison fell to
    # nv_eq(mv_as_nv(..)) and mv_tag_to_nv's default abort() killed it after
    # cc had accepted it (rc 134, empty stdout).  The interpreter ANSWERS, and
    # the answers below are all false except the `!=`:
    #
    #     false false false true false false
    #
    # and that is the reading worth writing down.  Not because functions
    # "cannot be compared", but because val_eq's FUNCTION row (src/vm/vm.c:419,
    # `return a->ptr == b->ptr`) compares the ImClosureFunction POINTER, and
    # every read of a function value CLONES it (value_copy, src/vm/vm.c:530-537:
    # `im_closure_function_clone`).  So `f == f` loads the global twice into
    # two cloned closures and answers false, `h = f` clones on assignment, and
    # even `d[0] == d[0]` clones on each index read -- measured.  The row is
    # in effect dead: no shape that reads a value twice can see one pointer
    # twice.  This route copies MirVal structs without cloning, so comparing
    # ptr here would answer TRUE where the interpreter answers FALSE -- the
    # criterion pins the interpreter's answer, not the row's intent.
    ("mir_function_equality_nocapture",
     'f = () -> 42\n'
     'g = () -> 42\n'
     'say f == g\n'
     'h = f\n'
     'say f == h\n'
     'say f == f\n'
     'say g != f\n'
     'say f == 42\n'
     'say f == nil\n'),
    # FUNCTION EQUALITY where the values carry an environment.  Same stop before
    # this cell, same answer after: two instantiations answer false (the
    # interpreter cloned them), and a copy answers false too -- `p = m` is
    # value_copy, i.e. another clone with a shared env, so the closure objects
    # are distinct.  `m != n` is the one true line.  The allocations are the
    # two environments `make` built (32 bytes each, one captured value), measured
    # off the generated program's IR-MIR-ALLOCS line.
    ("mir_function_equality_captured",
     'func make(n) { return x -> x + n }\n'
     'm = make(1)\n'
     'n = make(2)\n'
     'p = m\n'
     'say m == n\n'
     'say m == p\n'
     'say m == m\n'
     'say m != n\n'),
    # SET LITERALS -- OP_NEW_SET.  The fifth container a program can spell and
    # the first one whose ELEMENTS ARE A SET and not a sequence: the same push
    # that an array keeps, a set MERGES (src/vm/vm.c:4286-4313 L_NEW_SET calls
    # vm_set_add per pushed value, whose int half is a sorted insert and whose
    # other half is a val_eq scan -- both no-ops on a repeat).  So the reading
    # that separates a set from an array with the same source text is len().
    #
    # The literal is the ONLY set source this route can reach: `s = 1, 2` is a
    # bare comma list (src/parser/parser.c:522 looks_like_set_start, :533
    # parse_set_literal -> EXPR_SETLIT, src/compiler/compiler.c:558-566), and
    # the other two set shapes sit behind refusals that fire FIRST -- the
    # comprehension `{v in S | c}` compiles OP_CALL_BUILTIN "list"
    # (src/compiler/compiler.c:575), which this route refuses by name (only
    # `len` resolves), and `min(1, 2)` is OP_SET_INTERVAL, which is not in the
    # table.  That is also why the EMPTY set has no case here: the only
    # OP_NEW_SET with count 0 is src/compiler/compiler.c:582, inside the
    # comprehension arm.
    #
    # Why each case is here, one by one:
    #   mir_set_new_len_in     the flat two-int set: len, one hit, one miss.
    #                          Nothing in it depends on dedup or on content
    #                          equality, so it stays green under both reverse
    #                          criteria in docs/STATUS.md 10.125.
    #   mir_set_new_single     `s = 7, 7`.  A ONE-element set literal is
    #                          unspellable without a repeated element (`s = 7,`
    #                          is a parse error: "expected 'expression', but got
    #                          'say'"; `s = ()` is one too), so this is the only
    #                          shape a single-element set ever takes -- which is
    #                          also why the dedup injection reddens this case
    #                          as well as mir_set_new_dedup.
    #   mir_set_new_dedup      `s = 1, 1, 2`: three pushes, two elements.  An
    #                          implementation that appended every push answers 3
    #                          here and passes every other case in this group.
    #   mir_set_new_membership the two membership rules a route gets wrong while
    #                          ints still work: a STRING element compared by
    #                          CONTENT rather than by handle, and a FLOAT element
    #                          found from an INT probe.  set_contains runs the
    #                          val_eq scan for an INT probe too (src/vm/vm.c:2020
    #                          finds 1 in {1.0}), while the reverse is a
    #                          different answer -- `1.0 in {1}` is FALSE, because
    #                          a non-int probe never looks at the int half --
    #                          and `u = 1, 1.0` is a TWO-element set.  The same
    #                          asymmetry is in vm_set_add's DEDUP (src/vm/vm.c:1872:
    #                          an INT looks only at the sorted i64 half, every
    #                          other tag only at items), so the spellings that
    #                          separate the halves are pinned in both orders:
    #                          `1, 1.0` and `1.0, 1` are BOTH two elements --
    #                          measured, and the second one is where a dedup that
    #                          ran one numeric equality over all pairs answers 1.
    #   mir_set_new_register_elements
    #                          the elements are VALUES in registers, not
    #                          constants: this pins that the lowering collects
    #                          the pushed values instead of the register numbers.
    ("mir_set_new_len_in",
     's = 1, 2\nsay len(s)\nsay 2 in s\nsay 3 in s\n'),
    ("mir_set_new_single",
     's = 7, 7\nsay len(s)\nsay 7 in s\nsay 8 in s\n'),
    ("mir_set_new_dedup",
     's = 1, 1, 2\nsay len(s)\nsay 1 in s\nsay 2 in s\nsay 3 in s\n'),
    ("mir_set_new_membership",
     's = "a", "b"\nsay len(s)\nsay "b" in s\nsay "z" in s\n'
     't = 1.0, 1.0\nsay 1 in t\nsay 1.0 in t\n'
     'u = 1, 1.0\nsay len(u)\nsay 1 in u\nsay 1.0 in u\n'
     'w = 1.0, 1\nsay len(w)\nsay 1 in w\nsay 1.0 in w\n'),
    ("mir_set_new_register_elements",
     'a = 1\nb = 2\ns = a, b\nsay len(s)\nsay 2 in s\nsay 4 in s\n'),
    # SET EQUALITY.  `L_EQ` (src/vm/vm.c:3539-3543) sends SET/SET to
    # set_equal() (src/vm/vm.c:2221-2260), which compares the two halves
    # SEPARATELY -- the sorted i64 half against itself, the item half against
    # itself -- and answers 0 when the COUNT of either half differs.  Everything
    # else (a set against a number, a set against an array) falls to val_eq()
    # (src/vm/vm.c:413-430), which has no VAL_SET row and answers FALSE.
    #
    # Why each line is here, one by one:
    #   mir_set_eq_same          two spellings of one content, `2, 1` against
    #                            `1, 2`.  A route comparing pool elements IN
    #                            ORDER answers FALSE here; the interpreter says
    #                            TRUE.
    #   mir_set_eq_diff          the not-equal direction, with `!=` beside `==`
    #                            so a route that hardcodes one answer shows.
    #   mir_set_eq_mixed_order   `1.0, 1` against `1, 1.0`: the int lands in one
    #                            half and the float in the other in BOTH, so this
    #                            is TRUE -- it is the case that kills "one numeric
    #                            equality for everything", which the next case
    #                            says must not happen.
    #   mir_set_eq_int_vs_float  `1, 2` against `1.0, 2.0` -- two elements on the
    #                            i64 half versus two on the item half, so
    #                            set_equal answers 0.  This is the half-for-half
    #                            rule and NOT a numeric coercion: comparing
    #                            values across the halves would answer TRUE here
    #                            (1 pairs 1.0 and 2 pairs 2.0 by val_eq), which
    #                            is the wrong shape the class check exists to
    #                            refuse.  Two spellings of one element are not the
    #                            discriminator: a single-element set literal is
    #                            constant-folded to a plain value, so `s = 1` and
    #                            `t = 1.0` compare as numbers on both sides.
    #   mir_set_eq_vs_scalar     a set against a number and against an array: three
    #                            FALSE answers that must not be an abort.  val_eq
    #                            has no set row, so a route that runs a set through
    #                            mv_eq as a dict (identity) or through mv_as_nv
    #                            prints something else or exits 134.
    #   mir_set_eq_dedup         `1, 1, 2` against `1, 2`: equality is over the
    #                            DEDUPED content, so the count each half holds is
    #                            what makes them equal.
    ("mir_set_eq_same",
     's = 1, 2\nt = 2, 1\nsay s == t\nsay s != t\n'),
    ("mir_set_eq_diff",
     's = 1, 2\nt = 1, 3\nsay s == t\nsay s != t\n'),
    ("mir_set_eq_mixed_order",
     's = 1.0, 1\nt = 1, 1.0\nsay s == t\n'),
    ("mir_set_eq_int_vs_float",
     's = 1, 2\nt = 1.0, 2.0\nsay s == t\nsay s != t\n'),
    ("mir_set_eq_vs_scalar",
     's = 1, 2\nsay s == 1\nsay s != 1\n'
     'a = [1, 2]\nsay s == a\nsay a == s\n'),
    ("mir_set_eq_dedup",
     's = 1, 1, 2\nt = 1, 2\nsay s == t\nsay len(s)\n'),
    # PRINTING A CONTAINER WHOLE -- `say a` where a is an array, a dict, a
    # set, or one of those nested inside another.  This is a separate exit
    # from `say` of a scalar and every case below was RED before this cell
    # as a runtime abort with empty stdout (rc 134), not a refusal:
    # mv_say hands every tag but nil, string and function to nv_say
    # through mv_as_nv, and mv_tag_to_nv aborts on MV_ARR/MV_DIC/MV_SET.
    # The interpreter prints a bracketed list, a braced pair list and
    # `set(N)`, and that text is what these compare BYTES against --
    # no expected string is written down here.
    #
    # Why each case is here, one by one:
    #   mir_print_array_whole  the flat list, plus len() behind it so a
    #                          formatter that prints `[...]` but loses
    #                          the count is caught.
    #   mir_print_dict_whole   pairs in INSERTION order, keys and values
    #                          formatted with the same scalar rules
    #                          (a string key prints unquoted).
    #   mir_print_nested_depth the recursion cap, twice: depth 1 expands
    #                          (`[[1, 2], [3]]`) and depth 2 does not
    #                          (`[[[1]]]` prints `[[[]]]`, measured on the
    #                          interpreter), because value_to_string
    #                          expands only `depth < 2`.
    #   mir_print_set_whole    a set prints `set(N)`, the DEDUPED element
    #                          count, and `1, 2, 2.0` is three elements
    #                          (the float lives in the items half and the
    #                          int 2 never dedups it), which a formatter
    #                          that counts pushes answers 2 for.
    #   mir_print_mixed_tags   every scalar tag inside one container --
    #                          bool, nil, string, float -- so no tag's
    #                          spelling can be borrowed from the say exit.
    #   mir_print_empty        `[]` and `{}`: the two shapes whose body
    #                          loop runs zero times.
    ("mir_print_array_whole",
     'a = [1, 2]\n'
     'say a\n'
     'say len(a)\n'),
    ("mir_print_dict_whole",
     'd = {"a": 1, "b": 2}\n'
     'say d\n'
     'say len(d)\n'),
    ("mir_print_nested_depth",
     'a = [[1, 2], [3]]\n'
     'b = [[[1]]]\n'
     'say a\n'
     'say b\n'),
    ("mir_print_set_whole",
     's = 1, 2, 2.0\n'
     'say s\n'
     'say len(s)\n'),
    ("mir_print_mixed_tags",
     'd = {"k": [true, nil, "s"]}\n'
     'say d\n'
     'c = [1.5, 2]\n'
     'say c\n'),
    ("mir_print_empty",
     'a = []\n'
     'd = {}\n'
     'say a\n'
     'say d\n'),

    # `stop all` is OP_STOP.  The interpreter's arm is two statements --
    # `t->running = false; continue;` (src/vm/vm.c, L_STOP) -- i.e. "the
    # RUNNING thread stops here", which for the one thread a program has is
    # "the program ends here, with rc 0 and no more output".  This route
    # emits one thread per program (a container with a thread section is
    # refused earlier, why=threads), so the same two statements are the same
    # exit: `return 0;` from main, `fflush(stdout); exit(0);` from a
    # container -- the exit OP_HALT already leaves through.
    #
    #   mir_stop_at_toplevel   the plain shape: `say 2` after `stop all` is
    #                          never printed, and the rc is still 0.
    #   mir_stop_mid_loop      reached from inside a `while` on its third
    #                          pass: a lowering that emits a `break` or a
    #                          flag instead of an exit prints `done` and/or
    #                          keeps counting, and one that emits it only at
    #                          top level does not stop at all.
    #   mir_stop_inside_a_function
    #                          the container path -- `return mv_nil()` would
    #                          be wrong here because the call site keeps
    #                          going and prints `3`; only the exit does.
    #   mir_stop_after_allocating
    #                          the exit sweep is atexit, not the end of main,
    #                          so a block allocated before the stop is still
    #                          freed and reported as freed.
    ("mir_stop_at_toplevel",
     'say 1\n'
     'stop all\n'
     'say 2\n'),
    ("mir_stop_mid_loop",
     'i = 0\n'
     'while i < 10 {\n'
     '  say i\n'
     '  i = i + 1\n'
     '  if i == 3 {\n'
     '    stop all\n'
     '  }\n'
     '}\n'
     'say "done"\n'),
    ("mir_stop_inside_a_function",
     'func f() {\n'
     '  say 1\n'
     '  stop all\n'
     '  say 2\n'
     '}\n'
     'f()\n'
     'say 3\n'),
    ("mir_stop_after_allocating",
     'a = [1, 2]\n'
     'say len(a)\n'
     'stop all\n'
     'say "unreached"\n'),

    # OP_BIND -- the constraint declaration `name: set [= init]` (parser.c:1409-
    # 1419, the only declaration shape).  The interpreter's arm (src/vm/vm.c,
    # L_BIND) does two things: it records `vm->global_bound[g] = set + 1`, and it
    # checks an initial value with the SAME test a later assignment uses
    # (set_contains).  The second half lives in L_STORE_GLOBAL, which is where
    # an out-of-range assignment throws `type_mismatch` -- so a lowering that
    # only records the binding answers every assignment wrongly and silently,
    # and one that checks only the initialiser leaves `x = 5` printing 5.
    #
    #   mir_bind_constrained_store   the record itself: before any store the
    #                          global is nil (L_BIND's no-init branch sets
    #                          VAL_NIL), and a member assignment lands.
    #   mir_bind_initial_value  the initialiser arm stores the value it was
    #                          given, and a later member assignment still lands.
    #   mir_bind_out_of_range_store
    #                          `x = 5` against `1, 2, 3`: the interpreter prints
    #                          NOTHING on stdout and exits 1 with `type_mismatch`
    #                          on stderr; a route that stores and prints `5` is
    #                          the wrong answer this case exists to catch.
    #   mir_bind_out_of_range_init
    #                          `= 5` in the declaration: the OTHER message
    #                          (`initial value out of range`), which is the half
    #                          a shared stop-with-type_mismatch arm would get
    #                          wrong while still passing the case above.
    #   mir_bind_int_probe_finds_float
    #                          set_contains' asymmetry, direction one: an INT
    #                          probe still scans the non-int items through
    #                          val_eq, so `1` IS a member of `{1.0, 2.0}`.
    #   mir_bind_float_probe_misses_int
    #                          the direction that makes it asymmetric: a FLOAT
    #                          probe only looks at the items half, so `1.0` is
    #                          NOT a member of `{1, 2, 3}` and the store throws.
    #                          One numeric membership rule for both answers
    #                          passes the first and fails this one.
    #   mir_bind_plain_constraint
    #                          `x: 5` -- the declared expression is not a set,
    #                          so the interpreter records NO bound (sidx == -1)
    #                          and every store lands.  A route that only knows
    #                          how to bind sets must not refuse this program.
    ("mir_bind_constrained_store",
     'x: 1, 2, 3\n'
     'say x\n'
     'x = 2\n'
     'say x\n'),
    ("mir_bind_initial_value",
     'x: 1, 2, 3 = 2\n'
     'say x\n'
     'x = 3\n'
     'say x\n'),
    ("mir_bind_out_of_range_store",
     'x: 1, 2, 3\n'
     'x = 5\n'
     'say x\n'),
    ("mir_bind_out_of_range_init",
     'x: 1, 2, 3 = 5\n'
     'say x\n'),
    ("mir_bind_int_probe_finds_float",
     'x: 1.0, 2.0 = 2.0\n'
     'say x\n'
     'x = 1\n'
     'say x\n'),
    ("mir_bind_float_probe_misses_int",
     'x: 1, 2, 3\n'
     'x = 1.0\n'
     'say x\n'),
    ("mir_bind_plain_constraint",
     'x: 5\n'
     'x = 9\n'
     'say x\n'),

    # OP_YIELD -- `yield` (src/parser/parser.c, the STMT_YIELD branch; the
    # compiler emits it with all three fields zero).  The interpreter's whole
    # arm is one guarded call: `if (t->is_task && t->fiber_sched)
    # SwitchToFiber(t->fiber_sched); continue;` -- a cooperative hand-off to a
    # fiber scheduler, and a NO-OP when the running thread is not a task.
    #
    # THAT IS THE POINT OF THESE CASES.  This route refuses a container with a
    # thread section by name (why=threads), so every program it accepts runs on
    # one plain thread with no fiber scheduler: both conditions of the
    # interpreter's guard are false, and the opcode cannot do anything a
    # program can see.  What has to be nailed down is therefore what the arm
    # must NOT be -- a route that answered `yield` with any of these would pass
    # a "is it still accepted" test and fail here:
    #
    #   not a stop     `say 2` after the yield still prints (mir_yield_at_toplevel)
    #   not a re-run   the loop body runs exactly three times, so `i` ends at 3
    #                  rather than restarting (mir_yield_mid_loop)
    #   not a frame    the caller continues after the callee yields, and the
    #                  callee finishes (mir_yield_inside_a_function)
    #   not free-of-charge a yield allocates nothing: the only block the fourth
    #                  case reports is the array literal it builds itself
    ("mir_yield_at_toplevel",
     'say 1\n'
     'yield\n'
     'say 2\n'),
    ("mir_yield_mid_loop",
     'i = 0\n'
     'while i < 3 {\n'
     '  yield\n'
     '  i = i + 1\n'
     '}\n'
     'say i\n'),
    ("mir_yield_inside_a_function",
     'func f() {\n'
     '  say "in"\n'
     '  yield\n'
     '  say "out"\n'
     '}\n'
     'f()\n'
     'say "end"\n'),
    ("mir_yield_around_allocating",
     'a = [1, 2]\n'
     'yield\n'
     'say len(a)\n'
     'yield\n'),

    # ---- renditions of try/catch/finally, the eleven shapes that matter -----
    #
    # The interpreter keeps one ExcFrame per try: `L_TRY_START` looks the range
    # up in the bytecode's try table by `start_off == t->ip - 1`, saves
    # {code, ip, sp, base, frame_count, catch_ip, var_idx} and `L_THROW` calls
    # vm_throw, which restores that frame and jumps to catch_ip (src/vm/vm.c
    # L_TRY_START / L_TRY_END / L_THROW near :4420, vm_throw near :2569).  The
    # eleven programs below are one per thing that restore can be wrong about;
    # each was RUN on the interpreter first and its bytes are what the case
    # demands.
    #
    #   mir_try_catches_a_throw        the catch runs and the `say "b"` behind the
    #                                  throw does NOT (a route that resumed after
    #                                  the throw would print a, boom, b, after)
    #   mir_try_body_finishes          a try with no throw leaves the catch
    #                                  untouched (this case spent its life as a
    #                                  REFUSAL -- see the retirement note below)
    #   mir_try_catches_from_a_callee  the frame that is unwound is the CALLEE's,
    #                                  and the caller's try catches
    #   mir_try_catches_an_arm_stop    an `x: 1, 2, 3` bound is not an opcode -- it
    #                                  is a stop inside OP_STORE_GLOBAL, and it is
    #                                  catchable (`say e` prints type_mismatch)
    #   mir_try_nested                 the inner handler re-throws; the outer one
    #                                  catches the second throw, not the first
    #   mir_try_bare_catch             `catch { }` has no variable, so there is no
    #                                  global to write the error into
    #   mir_try_catches_a_non_string   `throw 5` carries 5 through the catch
    #   mir_try_finally_cleanup        finally runs while the error is still
    #                                  uncaught and the process still exits 1
    #   mir_try_finally_keeps_return   a return inside try runs finally and keeps
    #                                  its value (the frames that function opened
    #                                  are popped on return, src/vm/vm.c:4142)
    #   mir_try_return_from_inside     return from inside try, catch never runs
    #   mir_try_keeps_a_register_...   a register written in the body is still the
    #                                  written value in the catch -- measured, and
    #                                  it is the one reading a setjmp/longjmp
    #                                  lowering gets wrong (both register allocators
    #                                  roll the local back to its setjmp value).
    ("mir_try_catches_a_throw",
     'try {\n'
     '  say "a"\n'
     '  throw "boom"\n'
     '  say "b"\n'
     '} catch (e) {\n'
     '  say e\n'
     '}\n'
     'say "after"\n'),
    ("mir_try_body_finishes",
     'try {\n'
     '  say "x"\n'
     '} catch (e) {\n'
     '  say "c"\n'
     '}\n'
     'say "done"\n'),
    ("mir_try_catches_from_a_callee",
     'func f() {\n'
     '  throw "inner"\n'
     '}\n'
     'try {\n'
     '  f()\n'
     '  say "not reached"\n'
     '} catch (e) {\n'
     '  say e\n'
     '}\n'
     'say "after"\n'),
    ("mir_try_catches_an_arm_stop",
     'x: 1, 2, 3\n'
     'try {\n'
     '  x = 5\n'
     '  say "not reached"\n'
     '} catch (e) {\n'
     '  say e\n'
     '}\n'
     'say "after"\n'),
    ("mir_try_nested",
     'try {\n'
     '  try {\n'
     '    throw "inner"\n'
     '  } catch (e) {\n'
     '    say e\n'
     '    throw "mid"\n'
     '  }\n'
     '} catch (e) {\n'
     '  say e\n'
     '}\n'
     'say "after"\n'),
    ("mir_try_bare_catch",
     'try {\n'
     '  throw "bare"\n'
     '} catch {\n'
     '  say "bare catch"\n'
     '}\n'
     'say "after"\n'),
    ("mir_try_catches_a_non_string",
     'try {\n'
     '  throw 5\n'
     '} catch (e) {\n'
     '  say e\n'
     '}\n'
     'say "after"\n'),
    ("mir_try_finally_cleanup",
     'try {\n'
     '  say "t"\n'
     '  throw "boom"\n'
     '} finally {\n'
     '  say "cleanup"\n'
     '}\n'),
    ("mir_try_finally_keeps_return",
     'func f() {\n'
     '  try {\n'
     '    say "t"\n'
     '    return 1\n'
     '  } finally {\n'
     '    say "cleanup"\n'
     '  }\n'
     '}\n'
     'say f()\n'),
    ("mir_try_return_from_inside",
     'func f() {\n'
     '  try {\n'
     '    return 5\n'
     '  } catch (e) {\n'
     '    say "c"\n'
     '  }\n'
     '}\n'
     'say f()\n'),
    ("mir_try_keeps_a_register_written_in_the_body",
     'func g() {\n'
     '  x = 1\n'
     '  try {\n'
     '    x = 2\n'
     '    throw "e"\n'
     '  } catch (e) {\n'
     '    say x\n'
     '  }\n'
     '}\n'
     'g()\n'),
    # OP_WAIT -- the second opcode in this route whose answer is not a value.
    # The interpreter's arm turns the operand into milliseconds with its own type
    # ladder (INT through ival, FLOAT through fval, ANYTHING ELSE as 0.0) and then,
    # in the CLI shape this route accepts (not a task, no gui pump, no per-frame
    # callbacks), hands it to a blocking sleep.  So the observable part of `wait`
    # is TIME, and every program below prints the same bytes whether the delay
    # happens or not.  That is why these are criteria about what `wait` must NOT
    # do -- change output, drop a statement, allocate -- and why the delay itself
    # is measured by hand and written into section 10.138 instead of asserted
    # here: a wall-clock assertion in this file would be a flaky test wearing a
    # measurement's clothes.
    ("mir_wait_at_toplevel",
     'say 1\n'
     'wait 0.05\n'
     'say 2\n'),
    ("mir_wait_zero",
     'say 1\n'
     'wait 0\n'
     'say 2\n'),
    # `pause` is a keyword (the first spelling of this case did not parse: "expected
    # 'function name', but got 'pause' (type 45)"), so the container is `hold`.
    ("mir_wait_inside_a_function",
     'func hold(a) {\n'
     '  say a\n'
     '  wait 0.02\n'
     '  say "done"\n'
     '}\n'
     'hold(7)\n'
     'say "after"\n'),
    ("mir_wait_with_a_variable_delay",
     'd = 0.01\n'
     'say 1\n'
     'wait d\n'
     'say 2\n'),
    ("mir_wait_in_a_loop",
     'i = 0\n'
     'while i < 3 {\n'
     '  wait 0.01\n'
     '  i = i + 1\n'
     '}\n'
     'say i\n'),
    # The type ladder, not the register's tag: the interpreter reads a STRING
    # operand as 0.0 and carries on, so this route must too -- and mv_as_nv, which
    # aborts on a string, is exactly what it must not use here.
    ("mir_wait_a_non_number_is_zero",
     'say 1\n'
     'wait "x"\n'
     'say 2\n'),

    # ---- record: a statement whose meaning lives on the host's disk ---------
    #
    # `record name = value` compiles to MOV + OP_RECORD + OP_STORE_GLOBAL: the
    # GLOBAL is written by the STORE_GLOBAL that follows, not by OP_RECORD.  What
    # OP_RECORD itself does is store metadata (name, store, scope, merge) and --
    # only when a save file was loaded at startup -- replace R[r3] with the value
    # found under that name in the loaded dict.
    #
    # That is the shape this route cannot carry: it keeps no record store, so it
    # writes no metadata AND CREATES NO save.dat.  With no store to consult, the
    # operand survives and the following STORE_GLOBAL writes it, which is exactly
    # what the interpreter prints in a FIRST run in a fresh directory.
    #
    # The divergences are real and are recorded in STATUS.md, not hidden here:
    # the interpreter WRITES save.dat at exit, and a later program in that same
    # directory reads the stored value instead of its own operand (measured: 5
    # stored by one run, `record a = 9 / say a` in the same directory prints 5
    # while this route prints 9).  So record is the first statement whose
    # interpreter output is a function of the host's disk, not of the program --
    # and the criteria below stay inside the only region where both routes are
    # deterministic: a fresh directory.
    #
    # Two rules make them stay deterministic even when the harness shares one
    # workdir across cases: every name below appears in THIS FILE exactly once,
    # so a store entry can never answer for a name it did not come from, and each
    # case writes back the value it would find stored (so a second run of the
    # same case reads the same bytes).
    ("mir_record_assigns_the_value",
     'record rnote = 5\n'
     'say rnote\n'
     'say "done"\n'),

    ("mir_record_two_names",
     'record rscore = 1\n'
     'record rflag = 2\n'
     'say rscore\n'
     'say rflag\n'),

    ("mir_record_a_store_tag",
     'record rtagged = 5, store: "both"\n'
     'rtotal = rtagged + 1\n'
     'say rtotal\n'),

    ("mir_record_default_store",
     'record default store = "both"\n'
     'say 1\n'),

    ("mir_record_then_overwrite",
     'record rover = 5\n'
     'say rover\n'
     'rover = 9\n'
     'say rover\n'),

    # ---- interval sets: a set whose answer is a predicate, not a list -------
    #
    # `[0, 3)` is not an array (that is `[0, 3]`) and not a finite set: it is
    # OP_SET_INTERVAL, and the interpreter answers it with SetObj.kind 2 -- a
    # DOMAIN NAME plus two bounds plus two inclusivity bits.  The two bounds are
    # not operands: they arrive on the ARGUMENT stack (`OP_PUSH_REG lo` then
    # `OP_PUSH_REG hi`), which is the first opcode in this route whose registers
    # are not all in the operand word.
    #
    # Everything below was read off the interpreter before it was written here:
    # `len()` of an interval is 0 (nothing to enumerate), it prints as
    # `set(R interval)` (the domain, not a count), membership is a predicate on
    # the DOMAIN first and the bounds second, and a non-numeric bound does not
    # refuse the program -- it makes the VALUE nil (`p` becomes nil, and
    # `1 in p` is then false).
    #
    # The domain is always R on this route: the compiler defaults the base name
    # to "R", and `[0, 3) Z` / operator algebra are not reachable from this
    # grammar (measured: `p | [5, 7)` does not parse at all), so a name other
    # than R is refused by name rather than guessed at.

    ("mir_interval_membership",
     'p = [0, 3)\n'
     'say 1 in p\n'
     'say 1.5 in p\n'
     'say 5 in p\n'
     'say "x" in p\n'
     'say true in p\n'),

    ("mir_interval_inclusive_upper",
     'p = (0, 3]\n'
     'say 0 in p\n'
     'say 2 in p\n'
     'say 3 in p\n'),

    ("mir_interval_open_upper_bound",
     'p = [0, )\n'
     'say -1 in p\n'
     'say 5 in p\n'
     'say len(p)\n'
     'say p\n'),

    ("mir_interval_bounds_from_registers",
     'lo = 1\n'
     't = 5\n'
     'p = [lo, t)\n'
     'say 1 in p\n'
     'say 2 in p\n'
     'say 6 in p\n'),

    ("mir_interval_len_and_print",
     'p = [0, 3)\n'
     'say p\n'
     'say len(p)\n'),

    ("mir_interval_a_non_number_bound_makes_nil",
     'p = [nil, 3)\n'
     'say p\n'
     'say 1 in p\n'),

    # MEASURED on the tree that answers them: every program builds exactly ONE
    # pool region (the interval is 4 slots, which fits the first 256-byte block),
    # so the reading is 1|256 -- the same shape as every other container row.
    # It was first written as 0|0 before the implementation existed; that guess is
    # corrected here rather than silently replaced.
    ("mir_interval_equality_by_bounds",
     'p = [0, 3)\n'
     'q = [0, 3)\n'
     'say p == q\n'
     'r = [0, 4)\n'
     'say p == r\n'
     'say p != r\n'),

    ("mir_interval_inside_a_function",
     'func band(lo, hi) {\n'
     '    return [lo, hi)\n'
     '}\n'
     'p = band(1, 4)\n'
     'say 2 in p\n'
     'say 4 in p\n'
     'say len(p)\n'),

    # ---- lock: a mutex this route never contends ----------------------------
    #
    # `lock m { ... }` compiles to OP_LOCK (acquire), the body, OP_LOCK (release)
    # -- the same opcode twice with r2 = 0 and r2 = 1 (src/compiler/compiler.c,
    # STMT_LOCK's isBlock == 1 path; isBlock 0 is the bare `lock m` form and
    # isBlock 2 is `unlock m`).  The interpreter's arm is a named ImMutex
    # (src/vm/vm.c, L_LOCK): look the index up, create the mutex on first use,
    # `im_mutex_lock` or `im_mutex_unlock` by r2, continue.
    #
    # On this route there is exactly ONE thread -- a container carrying a thread
    # section is refused earlier, why=threads -- so a mutex has nothing to
    # contend with and both calls are unobservable.  That makes this the honest
    # no-op of the group, like yield (10.136): what the criteria pin is what the
    # arm must NOT do (change output, drop the body, break a return out of the
    # body, allocate).
    #
    # The BARE form (`lock m` with no block) is REFUSED, not answered: it takes
    # the mutex and never releases it, and the interpreter measures as a HANG
    # (the probe printed `a\nin\nb` for the block form and never returned for
    # the bare one; killed after two minutes).  Reproducing a hang is not
    # agreement.
    ("mir_lock_block",
     'say "a"\n'
     'lock m {\n'
     '  say "in"\n'
     '}\n'
     'say "b"\n'),

    ("mir_lock_inside_a_loop",
     'i = 0\n'
     'while i < 3 {\n'
     '  lock m {\n'
     '    i = i + 1\n'
     '  }\n'
     '}\n'
     'say i\n'),

    ("mir_lock_return_from_body",
     'func f() {\n'
     '  lock m {\n'
     '    return 7\n'
     '  }\n'
     '}\n'
     'say f()\n'),

    ("mir_lock_around_a_container",
     'lock m {\n'
     '  a = [1, 2]\n'
     '  say len(a)\n'
     '}\n'
     'say "done"\n'),

    # OP_CALL_BUILTIN `list` -- the second builtin this route emits, and the
    # first whose ANSWER is a container.  The oracle is the interpreter
    # (src/runtime/runtime_posix.c:326-336 posix_core_list), measured before
    # each row below was written:
    #   an ARRAY is returned AS THE SAME VALUE (pop, then push it back -- not a
    #   copy, which mir_list_of_an_array pins with `list(a) == a`);
    #   a finite SET is enumerated by vm_set_to_array (src/vm/vm.c:2312-2340),
    #   which walks the sorted i64 half FIRST and the insertion-ordered items
    #   SECOND -- so the order is SEGMENTED, not sorted;
    #   any other value is nil, INCLUDING an interval set (measured: the
    #   interpreter prints nil for list([0, 3)) even though vm_set_to_array
    #   would enumerate kind 2, because this route's mv_set_interval has no
    #   members to give).
    # mir_list_mixed_set is the row that a whole-region sort would fail: both
    # of its programs print [1, 3, 2.5], never [1, 2.5, 3].
    ("mir_list_of_a_set",
     's = 3, 1, 2\n'
     'say list(s)\n'),

    ("mir_list_mixed_set",
     't = 1, 2.5, 3\n'
     'u = 2.5, 1, 3\n'
     'say list(t)\n'
     'say list(u)\n'),

    ("mir_list_of_an_array",
     'a = [1, 2]\n'
     'say list(a) == a\n'
     'say list(a)\n'),

    ("mir_list_of_scalars",
     'say list(5)\n'
     'say list(nil)\n'
     'say list("ab")\n'),

    ("mir_list_of_an_interval",
     'p = [0, 3)\n'
     'say list(p)\n'),

    # The narrowing row.  vm_set_to_array:2325 stores `(int)s->i64[i]`, so a
    # set element above 32 bits is TRUNCATED on the way out, and the truncated
    # value can collide with a literal that is already a member: 5000000000
    # becomes 705032704, which the set also contains, so a THREE-member set
    # enumerates TWO elements while len() still says three.  Found by
    # differential probing after the first four rows were green -- the four
    # above never leave the int32 range and could not have caught it.
    # MEASURED: interp `len 2` / `[705032704]`, and the route printed
    # `[1, 5000000000]` before the narrowing was copied.
    ("mir_list_narrows_wide_ints",
     's = 5000000000, 705032704\n'
     'say len(s)\n'
     'say list(s)\n'),

]


# The REVERSE half of MIR_ONLY: programs this route must REFUSE, by name, with
# the why it must refuse them with.  Asymmetric on purpose -- the observed fact
# that the interpreter answers such a program is the reason a refusal here has
# to be a NAMED decision and not an accident of what got implemented.
#
# mir_container_const_oob is `a = [1, 2, 3]` read at `a[3]`.  The array literal
# has THREE elements at a place the pre-scan can SEE, and the subscript is a
# constant register the pre-scan can SEE, so the index is out of range before
# any code runs.  The interpreter answers nil (src/vm/vm.c:1002 returns a nil
# Value when `i >= count`) and this route could answer nil at run time too --
# but a route that answers a provably-wrong subscript the same way it answers a
# good one has no way to say WHICH it did, and "silently the same answer" is the
# class of hole every MIR_ONLY comment above exists to close.  So the pre-scan
# refuses the program whole, with a why a reader can grep for; the interpreter
# keeps answering nil, and the reading prints BOTH facts.
#
# The two `threads` entries are about the OTHER thing a named refusal can be
# worth: keeping a boundary from drifting.  The decision they encode was taken
# after measuring, not because emitting threads is hard:
#
# 1. The boundary is CONTAINER level, not "a thread was actually started".
#    src/compilation/aot_mir.c:1632 returns -2 whenever bc->thread_count > 0, and
#    the compiler hangs that count on the container (src/compiler/compiler.c:250,
#    :423), so a file that DECLARES a thread and never starts it is refused too.
#    The interpreter answers that file (`defined-never-started`, rc 0), so the
#    reading prints an answer next to a refusal -- if a later change made the
#    refusal fire only at a start site, why=threads would move and this entry
#    would go red.  That drift is the measurement.
#
# 2. `thread_result("plain_worker")` would be a value-representation question
#    (unwrap a Result) if the container carried no thread section.  It is refused
#    by the SAME named why, so "outside because it is concurrent" cannot silently
#    become "outside because the argument stack was unbalanced" -- which, measured,
#    is exactly the why such a program gets the moment the thread section is gone
#    (IR-MIR-REFUSED|why=illegal-operand ... asp=3 for the no-thread variant).
#
# Emitting threads was rejected on purpose, and the reason is a measurement:
# /home/sakiko/inimerse/.scratch/ruling-concurrency.md records a real OS-thread
# probe (src/vm/vm.c:4459 starts a real thread, :4852 states it has no Fiber
# dependency) whose output differs run to run (2 distinct outputs over 20 runs),
# while every MIR-only criterion here compares bytes.  Emitting it would trade a
# named refusal for an unmeasurable one.
# mir_try_no_throw USED TO LIVE HERE, AND THIS IS THE RECORD OF ITS RETIREMENT.
# It was the NEGATIVE CRITERION for the throw step, and the reason OP_THROW could
# land as one arm instead of three: a `try`/`catch` with NO throw, where the
# interpreter answers rc=0, stdout `a\nd\n` and empty stderr (measured,
# ruling-exceptions.md §4.1 p14).  The cheap way to make `throw` "work" is to
# teach the arm to exit when a try frame is open; that would have turned the
# program from a NAMED refusal into `a\n[exception] uncaught: ...`, rc=1 --
# a wrong answer carrying an exit code, strictly worse than an honest refusal
# (ruling-exceptions.md §5 item 6).  So it sat on the refusal side with a side
# table saying what the refusal had to still be ABOUT: both TRY opcodes present,
# OP_THROW ABSENT.
#
# The try pair is lowered now, so the not-yet list can no longer carry the try
# pair and the entry cannot be a refusal any more.  Retiring it is not
# discarding the criterion: its program is now mir_try_body_finishes in MIR_ONLY,
# the same bytes demanded of an execution instead of of a refusal.  That is the
# stronger of the two claims -- a refusal only had to NAME the opcodes, an
# execution has to print a, d and nothing else.  The side table mechanism is
# kept (it is exercised by the self-check's own fixture) with no live rows.
#
# mir_try_escapes is what the try pair added to the refusal side, and the reason
# is a MEASUREMENT of the interpreter, not of this route: a `break` out of a try
# body leaks that try's ExcFrame (nothing pops it -- L_TRY_END is never reached,
# and only a RETURN pops frames, src/vm/vm.c:4142).  The measured program
# `while i < 3 { try { ... break } catch (e) { say "c" } }` followed by an
# `throw "late"` in ITS OWN try prints `2` then `late` with rc=0: the throw is
# caught by the STALE frame and its catch body runs after the loop has already
# finished.  This route refuses the leaking program by name instead of copying
# that, because a route cannot answer "which frame does this throw belong to"
# with a jump target and still be read.
MIR_ONLY_REFUSE = {
    "mir_container_const_oob": (
        'a = [1, 2, 3]\nsay a[3]\n',
        "index-out-of-range-constant"),
    "mir_thread_decl_never_started": (
        'thread plain_worker {\n    return 1\n}\nsay "defined-never-started"\n',
        "threads"),
    "mir_thread_result_unwrap": (
        'thread plain_worker {\n    return 11\n}\nr = thread_result("plain_worker")\nsay r\n',
        "threads"),
    # A builtin call.  Today neither of these reaches a named reason: the one
    # with an argument is refused for a DIFFERENT fault (the argument stack is
    # left unbalanced because nothing consumes OP_PUSH_REG), and the one without
    # an argument is refused as a generic unknown opcode.  Both readings are
    # measured below before the reason gets a name.
    "mir_builtin_call_with_arg": (
        'x = str(1)\nsay x\n',
        "builtin-call"),
    "mir_builtin_call_no_arg": (
        'say dbg_ip()\n',
        "builtin-call"),
    # A mutex taken and never released.  The INTERPRETER HANGS on this shape
    # (measured: the block form printed `a\nin\nb`, the bare probe never returned
    # and was killed after two minutes), so the refusal is the only honest
    # answer -- an "it is a no-op on one thread" reading would be true of the
    # BLOCK form and false here.  The mirror case refuses `unlock m` alone for
    # the same reason (it would release a mutex this route never took).
    "mir_lock_bare_never_released": (
        'lock m\nsay 1\n',
        "lock-unpaired"),
    "mir_lock_unlock_alone": (
        'unlock m\nsay 1\n',
        "lock-unpaired"),
    # The jump leaves the guarded body without passing TRY_END, so the frame
    # that a later throw would use is the wrong one -- see the note above.
    "mir_try_escapes": (
        'i = 0\n'
        'while i < 3 {\n'
        '  try {\n'
        '    i = i + 1\n'
        '    if i == 2 {\n'
        '      break\n'
        '    }\n'
        '  } catch (e) {\n'
        '    say "c"\n'
        '  }\n'
        '}\n'
        'say i\n'
        'try {\n'
        '  throw "late"\n'
        '} catch (e) {\n'
        '  say e\n'
        '}\n',
        "try-escapes"),
}

# THE NOT-YET SIDE TABLE, one row per refusal that is a REFUSAL-BY-OPCODE.
#
# A `why=unsupported-opcode` refusal alone says "something here is not lowered";
# the not-yet line that accompanies it names the opcodes.  Both are needed, and
# they answer different questions: the why says the refusal is this route's
# decision (not the interpreter's answer), the not-yet list says WHAT it is still
# about.  Entries are (must-contain, must-not-contain) over the opcode names
# parse_mir_out() found.
#
# It has NO live rows today: its only row was mir_try_no_throw, whose criterion
# was "the try pair is still un-emitted and OP_THROW is not" -- the try pair is
# lowered now, so the row would have to demand that a lowered opcode be
# un-lowered.  The mechanism is not dead code: notyet_problems() takes an
# optional spec, and the self-check below runs it on its own fixture in all three
# directions (naming what it must, leaking what it must not, losing a name).  A
# future refusal-by-opcode adds a row here and gets both halves back.
MIR_ONLY_REFUSE_NOTYET = {
}


def notyet_problems(notyet, case, spec=None):
    """Which ways this refusal's not-yet list disagrees with the side table."""
    if spec is None:
        spec = MIR_ONLY_REFUSE_NOTYET.get(case)
    if spec is None:
        return []
    need, forbid = spec
    seen = {op for op, _n in notyet}
    out = []
    for op in need:
        if op not in seen:
            out.append("%s: the refusal's not-yet list does not name %s (got %s), so the program"
                       " is no longer being refused for the try pair the criterion is about"
                       % (case, op, ",".join(sorted(seen)) or "none"))
    for op in forbid:
        if op in seen:
            out.append("%s: the refusal's not-yet list still names %s (got %s) -- that opcode is"
                       " lowered on this tree, so a refusal that names it is not the refusal this"
                       " case is the criterion OF"
                       % (case, op, ",".join(sorted(seen)) or "none"))
    return out


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
    # The STDERR instrument, asked both ways and with its EXCLUSION as a cell
    # of its own: the containment predicate must pass on the interpreter's real
    # shape (message + ledger + ALLOCS tail), fail when the message is absent,
    # and fail when the WRONG throw's word is the one printed.  A checker that
    # only ran one positive cell could not tell an arm that answered
    # `mv_str("boom")` from one that read its register, which is exactly the
    # hole STDERR_EXCLUDE exists to close.
    for what, text, want_clean in (
            ("stderr-contains",
             "[exception] uncaught: boom\n  at ip=4 frames=0\n"
             "IR-MIR-ALLOCS|allocs=0|freed=0|live=0|bytes=0\n", True),
            ("stderr-no-message", "a\n", False),
            ("stderr-wrong-word",
             "[exception] uncaught: boom\n", None)):
        checks += 1
        if what == "stderr-contains":
            got = stderr_problems(text, "mir_throw_uncaught_top")
        elif what == "stderr-no-message":
            got = stderr_problems(text, "mir_throw_uncaught_top")
        else:
            got = stderr_problems(text, "mir_throw_uncaught_cross")
        if want_clean is True:
            hit = not got
        elif want_clean is False:
            hit = len(got) == 1 and "does not contain" in got[0]
        else:
            # The wrong-word cell fails BOTH ways at once: `from-func` is absent
            # AND `boom` is present, and two named problems is the right reading
            # -- an instrument that saw only one of them would leave one
            # direction of the fake unmeasured.
            hit = len(got) == 2 and any("CONTAINS" in g for g in got)
        print("IR-MIR-SELFCHECK|what=%s|got=%s|want=%s|%s"
              % (what, "none" if not got else got[0].split(":")[0],
                 "clean" if want_clean is True else ("missing" if want_clean is False
                 else "leaked-word"), "ok" if hit else "WRONG"))
        ok &= hit
    # The NOT-YET side table, same shape: a list that carries exactly the two
    # names the spec demands is clean; naming a forbidden one is a failure;
    # losing one required name is a failure in the OTHER direction (the refusal
    # stopped being about what the spec says).  The table has no live rows any
    # more (its one row retired with mir_try_no_throw), so the spec below is a
    # fixture: the mechanism is what is under test here, not a refusal.
    SIDETABLE_FIXTURE = (("OP_TRY_START", "OP_TRY_END"), ("OP_THROW",))
    for what, notyet, want_ok in (
            ("notyet-try-only", [("OP_TRY_START", 1), ("OP_TRY_END", 1)], True),
            ("notyet-names-throw", [("OP_TRY_START", 1), ("OP_TRY_END", 1),
                                    ("OP_THROW", 1)], False),
            ("notyet-lost-try-end", [("OP_TRY_START", 1)], False),
            ("notyet-no-rows", [("OP_TRY_START", 1)], True)):
        checks += 1
        got = notyet_problems(notyet, "side-table-fixture",
                              None if what == "notyet-no-rows" else SIDETABLE_FIXTURE)
        hit = (not got) if want_ok else (len(got) == 1)
        print("IR-MIR-SELFCHECK|what=%s|got=%s|want=%s|%s"
              % (what, "none" if not got else got[0].split(":")[0],
                 "clean" if want_ok else "one-named-problem", "ok" if hit else "WRONG"))
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
        # The STDERR reading, taken from the same run.  It is a CONTAINMENT
        # predicate (STDERR_EXPECT carries the named substrings and the note
        # above it says why equality is not the criterion); the exit code alone
        # cannot see a program that stopped silently, and the stdout comparison
        # cannot see which VALUE the throw carried.
        sp = stderr_problems(er_text, name)
        if name in STDERR_EXPECT:
            problems.extend(sp)
            print("IR-MIR-STDERR|case=%s|need=(%s)|forbidden=(%s)|missing=(%s)|leaked=(%s)|ok=%s"
                  % (name, ",".join(STDERR_EXPECT[name]),
                     ",".join(STDERR_EXCLUDE.get(name, ())) or "none",
                     ",".join(w for w in STDERR_EXPECT[name] if w not in er_text) or "none",
                     ",".join(w for w in STDERR_EXCLUDE.get(name, ()) if w in er_text) or "none",
                     "yes" if not sp else "no"))
        print("IR-MIR-MIR-ONLY|case=%s|mir=accepted|cc=ok|exec=%s|rc=%s|bytes=%d|stdout-hex=%s"
              % (name, "same" if same else "diff", rc_r, len(out_r), out_r.hex()))

    # ------------------------------------------------------- the reverse criterion
    #
    # MIR_ONLY_REFUSE is judged ASYMMETRICALLY and in two directions, because a
    # refusal is a claim and a claim has two ways to be wrong: accepting the
    # program (which hides the question the entry exists to ask) and refusing it
    # for the WRONG reason (which would pass a "it errored out" check while the
    # named check it is supposed to be measured by never ran).  The reading
    # carries the interpreter's answer for the same file, captured here and not
    # assumed: the refusal is THIS ROUTE's decision standing next to an answer
    # the interpreter really gives, not a program nobody can run.
    ref_ran = ref_ok = 0
    for name, (src, want_why) in MIR_ONLY_REFUSE.items():
        s = Path(workdir) / (name + ".im")
        s.write_text(src)
        mir_c = Path(workdir) / (name + ".mir.c")
        rc_m, om, em = run([mir_tool, "translate", s, mir_c], workdir)
        got = parse_mir_out(om)
        ref_ran += 1
        why = got["refused"][0] if got.get("refused") else "no IR-MIR-REFUSED line"
        rc_i, out_i, _ei = run_bytes([interp, "--no-mods", "run", s], workdir)
        if rc_m == 0:
            problems.append("%s: the MIR route ACCEPTED a program this list exists to see refused"
                            " (the criterion demands why=%s); it translated %d bytes of C"
                            % (name, want_why, (mir_c.stat().st_size if mir_c.exists() else 0)))
        elif got.get("refused") is None or why != want_why:
            problems.append("%s: refused with why=%s, the criterion demands why=%s (a refusal for"
                            " the wrong reason is not the refusal this list measures)"
                            % (name, why, want_why))
        else:
            ref_ok += 1
        # The not-yet side table: a refusal is a claim about WHICH opcodes are
        # missing, and `why=unsupported-opcode` with no opcode names would
        # accept a program being refused for a reason nobody wrote down.  No
        # live row today (see MIR_ONLY_REFUSE_NOTYET's note); the mechanism is
        # exercised by the self-check's fixture and this loop keeps working the
        # moment a row comes back.
        np = notyet_problems(got.get("notyet") or [], name)
        if name in MIR_ONLY_REFUSE_NOTYET:
            problems.extend(np)
            need, forbid = MIR_ONLY_REFUSE_NOTYET[name]
            seen = {op for op, _n in (got.get("notyet") or [])}
            print("IR-MIR-NOTYET-CRITERION|case=%s|need=(%s)|forbid=(%s)|seen=(%s)"
                  "|missing=(%s)|leaked=(%s)|ok=%s"
                  % (name, ",".join(need), ",".join(forbid) or "none",
                     ",".join(sorted(seen)) or "none",
                     ",".join(op for op in need if op not in seen) or "none",
                     ",".join(op for op in forbid if op in seen) or "none",
                     "yes" if not np else "no"))
        print("IR-MIR-MIR-ONLY-REFUSED|case=%s|mir=%s|why=%s|expect=%s|ok=%s"
              "|interp-rc=%s|interp-out=%r|stdout-bytes=%d|stdout-hex=%s"
              % (name, "refused" if rc_m != 0 else "accepted", why, want_why,
                 "yes" if (rc_m != 0 and why == want_why) else "no",
                 rc_i, out_i, len(out_i), out_i.hex()))

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
    print("IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=%d|ok=%d|expect=%d"
          % (ref_ran, ref_ok, len(MIR_ONLY_REFUSE)))
    for line in unclassifiable:
        print("IR-MIR-C-UNCLASSIFIED|" + line)
    for line in problems:
        print("IR-MIR-PROBLEM|" + line)

    ok = (differ == 0 and not problems and not unclassifiable and conserved == accepted
          and perturb_fail == 0 and stdout_changes > 0 and perturbed > 0
          and mir_only_same == len(MIR_ONLY)
          and ref_ok == len(MIR_ONLY_REFUSE))
    if c_identical == 0 and c_diff > 0:
        ok = ok  # a corpus where every file differs is not itself a failure
    print("IR-MIR-VERDICT|%s|tree=%s|accepted=%d|refused=%d|differ=%d|c-identical=%d|c-diff=%d"
          "|perturb-stdout-changes=%d"
          % ("PASS" if ok else "FAIL", head_sha(), accepted, refused, differ, c_identical, c_diff,
             stdout_changes))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
