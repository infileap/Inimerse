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
    # match() itself only mallocs its translated pattern and frees it, so the
    # string-only program allocates nothing; this program's one block is the
    # `[1, 2]` literal, not the regex.
    "mir_match_non_strings_read_empty": (1, 256),
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
    # size()/count() add NO allocation: both read a header or the string length
    # on the stack.  These blocks are the literals the two programs write --
    # the array, the one-pair dict and the set -- MEASURED off each generated
    # program's own IR-MIR-ALLOCS line before writing the row.  A route that
    # boxed the answer, or enumerated the interval with a temporaries array,
    # would move these numbers.
    "mir_size_and_count_of_every_shape": (2, 768),
    "mir_len_size_count_side_by_side": (1, 256),
    # has()/keys() readings, MEASURED off each generated program's own
    # IR-MIR-ALLOCS line on the green run before they were written here.
    # mv_has allocates NOTHING; the two blocks are the literals the program
    # writes -- one two-pair dict and one one-pair dict, which do not fit the
    # first pool block together, so the pool doubles.  mv_keys allocates ONE
    # region per call and the program calls it five times (three over the
    # three-pair dict, one over {} , one over the two-pair dict), which is where
    # the four blocks come from.  A keys() that returned the dict's own region
    # would read 4 blocks fewer here AND move the first printed line.
    "mir_has_uses_the_coercing_rule": (2, 768),
    "mir_keys_returns_a_new_region": (4, 3840),
    # str() answers a HEAP string on every arm, so the allocation reading is
    # part of this cell rather than a side note: each call is one mv_alloc of
    # the answer's length plus one.  The two rows below are filled in from the
    # generated programs' own IR-MIR-ALLOCS lines once the arm exists -- they
    # are deliberately left at the default until then, because a number written
    # here before the run would be a guess dressed as a measurement.
    "mir_str_of_every_shape": (17, 864),
    "mir_str_float_split_side_by_side": (9, 350),
    # sum() itself allocates NOTHING -- it reads the container's items in place
    # and throws on a bad element.  What the first row counts is the literals the
    # program builds (four pool blocks / 1794 bytes: an array of four, the set,
    # and the ones behind `"1, 2"[0]`), MEASURED off the generated program's own
    # IR-MIR-ALLOCS line.  The throw case reports `reported=no|why=rc=1`: it
    # stops inside the run, so no sweep line is printed and no count is
    # consulted -- the same treatment the other throwing criteria get.
    "mir_sum_fold_and_nil_boundaries": (4, 1794),
    "mir_sum_non_numeric_throws": (1, 256),
    "mir_sum_bool_element_throws": (1, 256),
    # ABSOLUTE numbers.  THE PREDICTIONS WERE WRONG ON THREE OF THE FIVE, AND
    # THE ROWS BELOW ARE THE POST-MORTEM RATHER THAN A MATCHED-TO-THE-RUN EDIT.
    #
    # What was predicted, and why it was wrong.  The derivation assumed a
    # frontier copy costs "one block": the pool reserves 256 bytes (8 slots) on
    # first use and DOUBLES when the frontier needs more, so I read a copy as
    # +1 alloc / +256 bytes.  That is not how the pool works.  mv_arr_need
    # allocates the WHOLE NEW BLOCK at double size and copies the live prefix
    # into it (aot_mir.c's mv_arr_need), so a frontier copy that pushes the
    # frontier past 8 slots costs ONE allocation of 512 bytes, not one of 256.
    # The block count only rises when the pool has to grow at all -- a copy that
    # fits in the current block costs NOTHING.
    #
    # Measured, on the implementation, both sides of the discriminating pair
    # (same program, one line added):
    #     d={"a":1}; d["z"]=5; a=[7, 8]; say d["z"]; say a[0]
    #         -> allocs=1|bytes=256     (everything fits one 8-slot block)
    #     the same with `remove(d,"a")` inserted before the write
    #         -> allocs=2|bytes=768     (the copy doubles the pool: 256+512)
    # So the copy is real and observable -- it just costs a DOUBLING, not a
    # block.  The criterion's job (detect that the copy happened) is unchanged;
    # only my arithmetic about its price was wrong.
    #
    # The five measured rows, and what each block is:
    #   mir_remove_both_containers   2|768    dict + array literals (pool doubles once)
    #   mir_remove_aliases_two_rounds 1|256   ONE dict; the removals and the copy
    #                                         all fit the first block, so they cost 0
    #   mir_remove_then_grow         2|768    dict + the copy (256+512) -- the array
    #                                         literal is the SAME block as the copy
    #   mir_grow_without_remove      2|768    the control: dict + array, no copy
    #   mir_push_and_pop             1|256    two small arrays in one block
    # NOTE THE UNCOMFORTABLE PART, recorded rather than smoothed over: with these
    # numbers mir_remove_then_grow and its control read THE SAME 2|768, so the
    # ALLOCS row no longer distinguishes "copied" from "did not copy".  It still
    # pins that neither leaks (live=0) and that the totals are what they are, and
    # the copy is pinned where it IS distinguishable -- the pair measured above,
    # recorded in docs/STATUS.md §10.166.  A criterion that cannot see the thing
    # it was written for is worth saying out loud.
    "mir_remove_both_containers": (2, 768),
    "mir_remove_aliases_two_rounds": (1, 256),
    "mir_remove_then_grow": (2, 768),
    "mir_grow_without_remove": (2, 768),
    "mir_push_and_pop": (1, 256),
    # MEASURED, and this pair is the one that pins the frontier copy (see the
    # cases' comment for the block arithmetic).  The control is green on
    # arrival; the subject is 1|256 before the write lands in a fresh pool and
    # 2|768 once the shorten forces the copy.
    "mir_remove_pushes_frontier_control": (1, 256),
    "mir_remove_pushes_frontier": (2, 768),
    # substr() allocates ONE block per call: (clamped length + 1) bytes.  These
    # two rows are POST-RUN values with their derivations, not predictions --
    # the previous cell got three of five wrong by reading the pool as
    # "one block per copy", and the rule that came out of it is applied here:
    # write down what the number should be, then check.
    #   mir_substr_clamps_every_boundary: eleven calls, clamped lengths
    #     3,2,0,4,0,2,0,2,0,2,3 -> sum 18, plus one byte per answer = 11 calls
    #     -> 29 bytes.  The program's string literals are RESIDENT (mv_str,
    #     ival=1) and allocate nothing, so the eleven answers are the whole
    #     allocation.  PREDICTED 11|29, MEASURED 11|29 -- the prediction held.
    #   mir_substr_allocates_its_answer: three calls, clamped lengths 3,4,0
    #     -> 7 + 3 = 10.  PREDICTED 3|10, MEASURED 3|10.
    # Both are recorded here as the derivation PLUS the check, which is the
    # practice the previous cell's three-wrong-out-of-five earned: write what
    # the number should be, then look.
    "mir_substr_clamps_every_boundary": (11, 29),
    "mir_substr_allocates_its_answer": (3, 10),
    # split(), MEASURED, with the prediction that missed written above:
    #   the byte-arithmetic case reads 4|262 -- three element blocks of two
    #   bytes plus the pool's whole 256-byte first block.
    #   the thirteen-line case reads 20|1822: thirteen calls, each with its own
    #   elements, and the pool doubling twice as the regions stack up.  That
    #   total is reported as read, not decomposed, because every attempt to
    #   decompose it (including mine) has needed the block arithmetic that only
    #   the run knows.
    "mir_split_edges_and_the_trailing_empty": (20, 1822),
    "mir_split_allocates_new_bytes_per_element": (4, 262),
    # join(): MEASURED, and the prediction was wrong in a way worth keeping.
    #
    #   PREDICTED 2|262 for join([1,2,3],"-") on this reasoning: the answer is
    #   one block ("1-2-3" = 5 + NUL = 6 bytes) plus the array literal's pool
    #   block, and blocks(n_slots) = 256 for n <= 8, doubling thereafter.  The
    #   pool-block half of that arithmetic is right and is kept as a closed form.
    #
    #   MEASURED 15|3895 over the whole eleven-line program -- which is not a
    #   refutation so much as a scope error on my part: the prediction was for
    #   ONE join call and the row covers ELEVEN, each with its own array literal
    #   and its own answer block, and the pool doubles as those regions stack up.
    #   The per-call claim does hold; the row was never about one call.
    #
    #   THE REAL FINDING IN THIS CELL IS THE IMPLEMENTATION'S FIRST DRAFT.  It
    #   reserved 1024 bytes per element to size the answer, and the run read
    #   15|22294 for the same program -- a five-byte answer costing fifteen
    #   kilobytes.  The fix is two passes over the elements (format into a
    #   scratch buffer, sum the lengths, then allocate exactly) and the same
    #   program then reads 15|3895.  Both numbers are recorded because the first
    #   one is what "obviously fine" costs, and IR-MIR-ALLOCS is the only reading
    #   in this file that could see it -- the stdout was byte-identical either
    #   way.
    "mir_join_every_shape_and_the_default_separator": (15, 3895),
    "mir_join_float_format_and_percent": (8, 838),
    # chars()/ord()/chr(): PREDICTIONS, each with its DENOMINATOR written out --
    # the discipline three wrong predictions in a row earned.  "Denominator"
    # means: which calls does this total cover, and what does one call cost.
    #
    #   mir_bytes_ascii_roundtrip covers TWENTY-ONE calls, not one.  Per-call
    #   blocks, using blocks(n) = 256 bytes for n <= 8 pool slots:
    #       chars(x)  -> one array region (1 + len(x) slots) + one 2-byte block
    #                    PER BYTE of x
    #       chr(n)    -> one 1-or-2-byte block
    #       ord(x)    -> nothing
    #   The pool is shared across all of them and doubles as it fills, so the
    #   block count is NOT the sum of per-call blocks -- it is the answer blocks
    #   plus however many pool blocks the regions needed.
    #
    #   MEASURED 22|1830 and 12|281.  THE DENOMINATOR IS WHY THOSE ARE THE ROWS
    #   AND NOT A PER-CALL NUMBER: 22 blocks over 21 calls is not "one block per
    #   call" plus one -- it is the answers (one 2-byte string per byte of every
    #   chars() source, one 1-or-2-byte string per chr()) PLUS four pool
    #   doublings as the regions stack past 8, 16, 32 and 64 slots.  Reading
    #   either total as a per-call cost would be the same mistake as the three
    #   predictions before it, made a fourth time.
    #   The high-byte row is smaller because ord() allocates nothing and the
    #   join() at its end is one answer: 12 blocks, 281 bytes.
    "mir_bytes_ascii_roundtrip": (22, 1830),
    "mir_bytes_high_bytes_via_ord": (12, 281),
    # The four scans: PREDICTION with its denominator, then the run.
    #   mir_scan_empty_needle_and_overlap covers TWENTY calls; every one of them
    #   answers an int, a bool, or the SAME string back (replace with an empty
    #   needle returns the source unchanged), so the four scans themselves
    #   should allocate NOTHING and the pool blocks are only the literals.
    #   mir_replace_allocates_only_its_answer covers TWO replace calls: the first
    #   answers "aXaX" (4 bytes) and the second returns its 3-byte source
    #   unchanged, so its answer blocks are 5 and 4 bytes.
    #   Both numbers are filled after the run; see the note beside them.
    #   MEASURED 7|23 and 2|9.  READ BOTH WITH THEIR DENOMINATOR:
    #   the replace-only row is the clean one -- exactly 2 allocations for
    #   exactly 2 answers (aXaX is 5 bytes, the returned-unchanged aaa is 4,
    #   9 total), so replace allocates its ANSWER and nothing else.
    #   The scan row's 7 blocks are NOT the scans: each string CONSTANT that
    #   reaches an arm is one mv_str copy (the route copies literals), and this
    #   program reaches "abc" three times, "aaa"/"aa"/"b", "abcabc"/"bc"/"X",
    #   "héllo"/"l"/"hé"/"é" -- the four scanners themselves add zero,
    #   which is why its block count tracks the literals and not the calls.
    "mir_scan_empty_needle_and_overlap": (7, 23),
    #   upper/lower/trim: PREDICTION with its denominator first.
    #   The case program is NINE calls over seven literals: upper(123) and its
    #   len() see the empty string, which is allocated once by the literal load,
    #   and every upper/lower call allocates exactly its answer.  The trim
    #   program is NINE calls and its reading separates the two arms.
    #   Filled after the run.
    #   MEASURED 10|31, 10|24 and 1|4 -- all three read WITH their denominator:
    #   the case row is 9 calls and each upper/lower call allocates its answer,
    #   plus the literal loads; the trim row is 9 calls and 10 blocks because
    #   every trim that CUTS allocates its answer while the two no-op trims
    #   (trim("abc") and trim("")) share or pre-exist -- the row would be 11
    #   blocks if the no-op path copied.
    #   THE CLEAN ROW IS THE THIRD: exactly 1 allocation for exactly 1 cutting
    #   call (3 chars + NUL = 4 bytes); trim("abc") allocates NOTHING, which is
    #   the reading that exists to catch a copy added on the no-op path.
    "mir_case_ascii_only_and_nonstring": (10, 31),
    "mir_trim_charset_is_an_enumeration": (10, 24),
    "mir_trim_allocates_only_when_it_cuts": (1, 4),
    # THE ALLOCATION AXIS FOR THE BIT CELL IS A ZERO, and it is worth an explicit
    # row: all six builtins answer an INTEGER, so a program made of them should
    # allocate NOTHING.  A row of (0,0) is what catches an implementation that
    # builds an intermediate string or routes an answer through mv_str.
    "mir_bits_values_and_width": (0, 0),
    # bool/float allocate nothing themselves -- the ONE block in each program is
    # the container LITERAL (`[]` or `{}`), and the expected numbers say so
    # rather than asserting 0|0: a route that boxed a truthiness result or
    # formatted a double through a buffer would report one more.  MEASURED
    # first (both reported allocs=1|bytes=256), then written.
    "mir_bool_truthiness_and_zero_float": (1, 256),
    "mir_float_converts_scalars_and_zeroes_containers": (1, 256),
    # round allocates NOTHING itself (identity copies a value); the one block
    # is the first container literal [1, 2].  MEASURED 1|256, written after.
    "mir_round_is_identity": (1, 256),
    "mir_replace_allocates_only_its_answer": (2, 9),
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
    # The wrong-arity closure measures the same one-environment-per-
    # instantiation shape as the rows above, and its number was MEASURED the
    # same way -- `mk = (n) -> (a, b) -> n + a + b` then `f = mk(10)` is ONE
    # capture of ONE slot, so the generated program's own IR-MIR-ALLOCS line
    # reads allocs=1|freed=1|live=0|bytes=32.  It is here rather than left on
    # the (0, 0) default because the default made the harness report
    # `ok=no|expect-allocs=0|expect-bytes=0` on the green run, i.e. the row is
    # what the wrong-arity fix has to keep true, not a restatement of it: a fix
    # that padded by re-reading the callee's environment, or that built a fresh
    # argument vector per call, would move this number.  The other eight
    # wrong-arity criteria measure 0|0 and stay on the default.
    "mir_arity_value_capture_extra": (1, 32),
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
    # The COMPREHENSION cases.  MEASURED on the tree that answers them (a
    # comprehension builds the source array, the empty set, `list` of the source,
    # and a destination array for each `say list(s)`), then written down.  TWO
    # of the five were first written as 2|768 and the readings corrected them --
    # which is the rule this table states, applied to itself:
    #   empty_source   1|256, not 2|768: the source `[]` is an EMPTY array (no
    #                  region of its own beyond its header) and no add ever runs,
    #                  so the second block is never asked for.
    #   moves_region   3|1792, not 2|768: the set is copied to the frontier AND
    #                  the destination array for `list` lands beside it, so the
    #                  pool doubles twice.
    "mir_set_add_dedups_and_orders": (2, 768),
    "mir_set_add_filters": (2, 768),
    "mir_set_add_floats": (2, 768),
    "mir_set_add_empty_source": (1, 256),
    "mir_set_add_moves_the_region": (3, 1792),
    # OP_RECV allocates nothing: it reads a queue that cannot be non-empty on
    # this route.  MEASURED (all four read 0|0), and the row exists so that an
    # arm which boxed the "received" value, or asked the pool for a buffer,
    # would move the number.
    "mir_recv_is_nil": (0, 0),
    "mir_recv_with_a_timeout": (0, 0),
    "mir_recv_inside_a_function": (0, 0),
    "mir_recv_result_is_a_value": (0, 0),
    # The ORDERING-STOP family: every one of them ends on an exception, so the
    # reading is `reported=no|why=rc=1` and no count is consulted -- the same
    # shape as the throw pair.  The two that ANSWER get real numbers, and
    # `mir_order_is_catchable` builds the DICT it compares against, so it reads
    # 1|256: the block is the literal, not the throw.  MEASURED, and one of them
    # was written as 0|0 first and corrected here.
    "mir_order_a_string_against_a_number": (0, 0),
    "mir_order_a_number_against_a_string": (0, 0),
    "mir_order_a_set_against_a_number": (0, 0),
    "mir_order_an_array_against_a_number": (0, 0),
    "mir_order_a_dict_against_a_number": (0, 0),
    "mir_order_nil_against_a_number": (0, 0),
    "mir_order_a_bool_against_a_number": (0, 0),
    "mir_order_two_strings_still_answer": (0, 0),
    "mir_order_is_catchable": (1, 256),
    # The five rules of `+` (OP_ADD).  MEASURED FIRST on the tree that
    # implements them, then written down -- and the first pass DID guess 0|0 for
    # all eleven rows, because "an add is arithmetic, arithmetic does not
    # allocate" is the intuition.  The first run refuted it in eleven lines, and
    # the reason is the same in every one of them: rule 3 stringifies and then
    # concatenates, and mv_str_concat registers its buffer (1 alloc, bytes =
    # result length + 1).  The seven string rows therefore read 1|n and 2|n,
    # the two set rows read 2|768 and 3|1792 (each set is a pool block, and
    # mir_add_sets_overlap asks for a THIRD: two sets plus the union plus the
    # destination array of list() no longer fit in one block), and the interval
    # row reads 2|768 for the same 768-byte reason: two interval regions, then
    # the (empty) union region.
    #   IR-MIR-ALLOCS|case=mir_add_string_and_int|allocs=1|bytes=3
    #   IR-MIR-ALLOCS|case=mir_add_string_chain_with_a_number|allocs=2|bytes=9
    #   IR-MIR-ALLOCS|case=mir_add_sets_overlap|allocs=3|bytes=1792
    # The two fatal rows are not listed: the interpreter answers them by
    # exiting 1 from inside the arm, so the instrument reports `reported=no`,
    # the same shape as every other rc=1 case.
    "mir_add_string_and_int": (1, 3),
    "mir_add_int_and_string": (1, 3),
    "mir_add_string_and_float": (1, 5),
    "mir_add_string_and_bool": (1, 6),
    "mir_add_string_and_nil": (1, 5),
    "mir_add_string_and_a_variable": (1, 4),
    "mir_add_string_chain_with_a_number": (2, 9),
    "mir_add_sets_is_a_union": (2, 768),
    "mir_add_sets_overlap": (3, 1792),
    "mir_add_set_and_a_scalar_is_nil": (1, 256),
    "mir_add_intervals_is_an_empty_set": (2, 768),
    # MEASURED like every other row -- these nine lines were added only after the
    # first green run printed what they are; the figures and their reasons are in
    # the doc's readings section.
    "mir_sub_sets_is_a_difference": (2, 768),
    "mir_sub_lists_the_leftovers": (2, 768),
    "mir_sub_keeps_a_float_out": (2, 768),
    "mir_mul_sets_is_an_intersection": (3, 1792),
    "mir_mul_floats": (2, 768),
    "mir_sub_a_set_and_a_scalar_is_nil": (1, 256),
    "mir_sub_an_interval_is_nil": (2, 768),
    "mir_mul_with_an_interval_keeps_the_members": (2, 768),
    "mir_mod_a_set_still_stops": (1, 256),
    # measured, not assumed: the two set cases carry their own set literals
    # (one block for `a = 1, 2`, two for the pair), and no arithmetic on a
    # non-number allocates anything at all -- the string cases above report 0|0.
    "mir_try_arith_on_a_set_and_a_scalar": (1, 256),
    "mir_try_arith_on_two_sets": (2, 768),
    # measured on the first green run, like every other row
    "mir_index_set_negative_is_caught": (1, 256),
    "mir_index_set_negative_in_a_nested_array": (1, 256),   # measured: one block for both literals
    # measured on the first green run of the retired refusal: one block each, the
    # array literal itself; the read allocates nothing.
    "mir_read_past_a_literal_is_nil": (1, 256),
    "mir_read_a_value_written_past_the_literal": (1, 256),
    "mir_read_far_past_a_single_element_literal": (1, 256),
    # measured on the first run of these criteria: the literal's own block; the
    # comparison case builds two of them.
    "mir_grow_an_array_past_its_literal": (1, 256),
    "mir_grow_through_an_alias": (1, 256),
    "mir_grow_inside_a_function": (1, 256),
    "mir_grow_a_nested_element": (1, 256),
    "mir_grow_then_compare": (2, 768),
    "mir_grow_with_an_explicit_nil": (1, 256),
    # The reduction itself allocates nothing; the ONE block each of these
    # programs claims is the set literal itself (a set is [count][elem...]
    # in the pool).  Measured on the first run, not guessed: the first
    # pass of this table said (0, 0) for all seven and the run refuted it.
    "mir_set_min_max_of_a_function": (0, 0),   # no set is built at all
    "mir_set_min_max_ints": (1, 256),
    "mir_set_min_max_floats": (1, 256),
    "mir_set_min_max_strings": (1, 256),
    "mir_set_min_max_mixed_is_nil": (1, 256),
    "mir_set_min_max_of_an_interval": (1, 256),
    "mir_set_min_max_narrows_i64": (1, 256),
    "mir_set_min_max_mixed_numbers": (1, 256),
    "mir_index_read_negative_is_nil": (1, 256),
    "mir_dict_negative_key_is_nil": (1, 256),
    # The fourteen key-identity criteria, measured on the first run that had
    # the fix: every one of them builds ONE dict literal (the empty dict each
    # program starts from) and the fix itself allocates nothing, so the row is
    # (1, 256) and not the (0, 0) the table's default said.  The run is what
    # corrected it: the first green pass listed all fourteen as
    # `allocs=1|bytes=256, the criterion demands allocs=0|bytes=0`.  The
    # fifteenth case compares with `==` and never builds a dict, so (0, 0)
    # there is measured too, not inherited -- it reports allocs=0|bytes=0.
    "mir_boolkey_set_true_probe_skips_int_half": (1, 256),
    "mir_boolkey_set_int_probe_crosses_halves": (1, 256),
    "mir_boolkey_int_vs_bool": (1, 256),
    "mir_boolkey_bool_vs_int": (1, 256),
    "mir_boolkey_false_vs_zero": (1, 256),
    "mir_boolkey_bool_vs_float": (1, 256),
    "mir_boolkey_int_float_collapses": (1, 256),
    "mir_boolkey_int_float_collapses_reverse": (1, 256),
    "mir_boolkey_int_float_hash_truncates": (1, 256),
    "mir_boolkey_two_equal_floats": (1, 256),
    "mir_boolkey_zero_vs_negative_zero": (1, 256),
    "mir_boolkey_nil_vs_zero": (1, 256),
    "mir_boolkey_str_vs_int": (1, 256),
    "mir_boolkey_register_read": (1, 256),
    "mir_boolkey_eq_is_not_key_identity": (0, 0),
    # MEASURED on this control's first run (it is green on the unmodified
    # route).  The control's two array literals need 4 + 3 = 7 slots and the
    # first pool block is 8 slots = 256 bytes, so both fit ONE block; it is
    # the number that must not move when the multi-argument arm is emitted.
    "mir_builtin_argc_single_arg_control": (1, 256),
    # MEASURED on the first green run of the multi-argument arm; every one of
    # these numbers is the array literals the program happens to contain, not
    # the builtin call itself (a builtin call allocates nothing: it reads one
    # pool slot and pops the stack).  `len_last` carries [1, 2, 3] (4 slots),
    # [9] (2) and [1, 2] (3) = 9 slots, so the pool doubles: 8+16 slots =
    # 256 + 512 = 768 bytes in two blocks.  `len_many` is one 5-slot literal
    # [1, 2, 3, 4], inside the first block.  `list_last` is one 3-slot [5, 6],
    # `nested` one 4-slot [1, 2, 3], and `args_evaluated` has no container
    # literal at all -- and the default (0, 0) is its measured row: the call
    # and the side effect both allocate nothing.
    "mir_builtin_argc_len_last": (2, 768),
    "mir_builtin_argc_len_many": (1, 256),
    "mir_builtin_argc_list_last": (1, 256),
    "mir_builtin_argc_nested_expression_loop": (1, 256),
    # §10.160's container rows.  Every one of the five builds exactly one
    # container literal whose elements fit the first 8-slot pool block, so the
    # reading is the pool and nothing else: a three-element set (`1, 2, 3`
    # needs 1 count + 3 items = 4 slots), a two-element set (3 slots), an
    # array + set pair that fits EXACTLY 8 slots (4 + 4, which is why it still
    # measures one block and not two), a two-pair dict (5 slots) and one
    # `[0, 3)` interval (2 bound slots).  MEASURED from each generated
    # program's own IR-MIR-ALLOCS line, not derived from the rows above: all
    # five reported allocs=1|freed=1|live=0|bytes=256.  The `for` lowering and
    # the nil it reads allocate nothing; a route that boxed each iterated
    # value, or that copied the set, would move one of these numbers.
    "mir_set_three_elements_three_nils": (1, 256),
    "mir_set_two_elements_two_nils": (1, 256),
    "mir_array_then_set_iteration": (1, 256),
    "mir_dict_iteration_keys": (1, 256),
    "mir_interval_iteration_yields_nothing": (1, 256),
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
    # The bit cell throws TWO different messages and they name different things:
    # overflow of the SHIFT (numeric_overflow) and a non-INT operand
    # ("<name>: expected two integers" / "bit_not: expected an integer").
    # Pinning both means a coercing implementation cannot pass by producing
    # one message in the wrong place.
    "mir_shl_overflow_is_an_error": ("[exception] uncaught: numeric_overflow",),
    "mir_bitwise_noninteger_throws": (
        "[exception] uncaught: bit_and: expected two integers",),
    "mir_sum_non_numeric_throws": ("[exception] uncaught: sum: non-numeric element",),
    "mir_sum_bool_element_throws": ("[exception] uncaught: sum: non-numeric element",),
    # sqrt does not coerce a non-number to 0: it throws the interpreter's
    # type_mismatch, and the message is the message.
    "mir_sqrt_nil_throws": ("[exception] uncaught: type_mismatch",),
    "mir_sqrt_string_throws": ("[exception] uncaught: type_mismatch",),
    "mir_sqrt_array_throws": ("[exception] uncaught: type_mismatch",),
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
    "mir_mod_a_set_still_stops": ("[exception] uncaught: type_mismatch",),
    # the error falls OUTSIDE the try block, so it is uncaught: the reading must
    # carry the interpreter's own wording, not merely a non-zero exit.
    "mir_index_set_negative_uncaught": ("[exception] uncaught: index_out_of_range",),
    "mir_arith_type_error_outside_a_try": ("[exception] uncaught: type_mismatch",),
    # `+` on an array or a dict KILLS the process with this sentence and rc 1
    # (src/vm/vm.c:3280-3283) -- it is not a throw, so a try/catch cannot catch
    # it, and this route must print the same words rather than call it a
    # type_mismatch.
    "mir_add_arrays_is_a_fatal_error": ("Error: '+' is not defined for arrays/dicts",),
    "mir_add_dicts_is_a_fatal_error": ("Error: '+' is not defined for arrays/dicts",),
    # A finally body runs WHILE the error is still uncaught: the try pair pops
    # nothing on the way out, so `cleanup` prints and the process still exits 1
    # with the original message.  The criterion demands the message AND the
    # exit code (mir_try_finally_cleanup's execution row carries rc=1), so a
    # route that swallowed the error would fail both halves.
    "mir_try_finally_cleanup": ("[exception] uncaught: boom",),
    # A pair with no order is refused with the SAME message the interpreter
    # prints, so the criterion can demand it instead of accepting any stop.
    "mir_order_a_string_against_a_number": ("[exception] uncaught: type_mismatch",),
    "mir_order_a_number_against_a_string": ("[exception] uncaught: type_mismatch",),
    "mir_order_a_set_against_a_number": ("[exception] uncaught: type_mismatch",),
    "mir_order_an_array_against_a_number": ("[exception] uncaught: type_mismatch",),
    "mir_order_a_dict_against_a_number": ("[exception] uncaught: type_mismatch",),
    "mir_order_nil_against_a_number": ("[exception] uncaught: type_mismatch",),
    # §10.160's two throw readings.  Each demands the sentence that names the
    # error that SURFACED.  In the after-catch case the finally's throw is the
    # one that propagates, so `boom` is forbidden as well as unneeded: if the
    # original error resurfaced behind the cleanup, the output bytes could stay
    # the same (stdout `caught` is printed before either throw) and only
    # stderr would say which error won -- this row is what makes that visible.
    "mir_finally_throw_after_catch_propagates": ("[exception] uncaught: fin-throw",),
    "mir_finally_runs_before_try_error": ("[exception] uncaught: boom",),
}
STDERR_EXCLUDE = {
    "mir_sum_non_numeric_throws": ("[exception] uncaught: numeric_overflow",),
    "mir_throw_uncaught_cross": ("[exception] uncaught: boom",),
    "mir_bind_out_of_range_store": ("[exception] uncaught: initial value out of range",),
    "mir_bind_out_of_range_init": ("[exception] uncaught: type_mismatch",),
    "mir_finally_throw_after_catch_propagates": ("[exception] uncaught: boom",),
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
    # size()/count(): ONE collection body, TWO different tails.  Written
    # BEFORE the implementation, and measured red on arrival as
    # `why=builtin-call` (the pre-scan names only `len` and `list`).  The
    # interpreter's answer is 17 lines, in this order:
    #     2 0 2 1 0 2 nil 3 nil 2 nil nil 2 nil nil 2 2
    # Read them as the three questions the single case holds apart:
    #   - the shared collection body, where size and count AGREE:
    #     [1, 2]->2, []->0, "ab"->2, {a:1}->1, {}->0, 1,2->2;
    #   - the interval shape BOTH must answer nil for: [1~3] (an R interval
    #     has no lattice to enumerate), while len([1~3]) answers 0 -- that
    #     zero is §10.129's arm and must not leak into size/count;
    #   - size()'s COERCION tail, which count() drops: 3->3, -7->nil (the
    #     interpreter's `if (n < 0)` treats a negative result as no answer),
    #     2.5->2 (truncation toward zero), true->nil, nil->nil.
    # The cheap implementation `count = size` passes every collection line
    # above and prints 3 where count(3) is nil; the coercion tail is the
    # reason this case cannot be split into two independent ones.
    ("mir_size_and_count_of_every_shape",
     'a = [1, 2]\nsay size(a)\ne = []\nsay size(e)\nsay size("ab")\n'
     'd = {"a": 1}\nsay size(d)\ned = {}\nsay size(ed)\ns = 1, 2\nsay size(s)\n'
     'iv = [1~3]\nsay size(iv)\nsay size(3)\nsay size(-7)\nsay size(2.5)\n'
     'say size(true)\nsay size(nil)\nsay count(a)\nsay count(3)\nsay count(iv)\n'
     'say count("ab")\nsay count(s)\n'),
    # The three names side by side on ONE operand, because the first case
    # states the difference and this one makes it a single textual line:
    #   x = [1, 2]  -> len 2, size 2, count 2   (the shared body)
    #   iv = [1~3]  -> len 0, size nil, count nil (the interval split)
    #   size(3)=3, count(3)=nil                (the coercion split)
    # An implementation that aliased any two of the three names fails at
    # least one line of this program, and no single arm can satisfy it.
    ("mir_len_size_count_side_by_side",
     'x = [1, 2]\nsay len(x)\nsay size(x)\nsay count(x)\n'
     'iv = [1~3]\nsay len(iv)\nsay size(iv)\nsay count(iv)\n'
     'say size(3)\nsay count(3)\n'),
    # has()/keys(): the two KEY RULES in one cell, and they must not be the
    # same rule.  Written BEFORE the implementation; on arrival both are
    # `why=builtin-call`.  The interpreter's answers, measured on the same tree
    # that measured every line of this file, are:
    #   true false false false false false false false false   (has, first nine)
    #   true / nil                                             (n={1:"x"} pair)
    #   true / nil                                             (e={true:"b"} pair)
    # The last two PAIRS are the whole point: `has(n, true)` is TRUE while
    # `n[true]` is nil, and `has(e, 1)` is TRUE while `e[1]` is nil.  has()
    # scans the pairs with val_eq (which promotes a mixed numeric pair to
    # double), the dict read/write arms use the hash-gated mv_key_eq (8.10),
    # and the two rules disagree on exactly these pairs.  An implementation
    # that reused mv_key_eq for has() passes every string-key line above and
    # prints false twice here.
    ("mir_has_uses_the_coercing_rule",
     'd = {"a": 1, "b": 2}\n'
     'say has(d, "a")\nsay has(d, "z")\nsay has(d, 1)\nsay has({}, "a")\n'
     'say has([1, 2], 1)\nsay has("abc", "a")\nsay has(nil, "a")\n'
     'say has(d, nil)\nsay has(d, true)\n'
     'n = {1: "x"}\nsay has(n, true)\nsay n[true]\n'
     'e = {true: "b"}\nsay has(e, 1)\nsay e[1]\n'),
    # keys() hands back a NEW array, and the program MUTATES the dict between
    # two printings for that reason: a keys() that returned the dict's own
    # region (or an alias the pair-order overwrite rule then perturbs) prints a
    # different first line.  The third and fourth lines pin the two things a
    # fresh region has to get right -- element order (insertion, not sorted)
    # and length -- and the last two lines prove the returned array is not
    # reachable from the dict: writing into it must NOT change keys(d).
    ("mir_keys_returns_a_new_region",
     'm = {}\nm["z"] = 1\nm["a"] = 2\nm["m"] = 3\nsay keys(m)\n'
     'm["z"] = 9\nsay keys(m)\nsay keys(m)[0]\nsay len(keys(m))\n'
     'd = {"a": 1, "b": 2}\nsay keys(d)\nsay keys({})\nsay keys(nil)\n'
     'say keys([1, 2])\nsay keys("ab")\n'
     'k = keys(d)\nk[0] = "zzz"\nsay keys(d)\n'),
    # str(): the SECOND print exit, and the only builtin whose answer is a
    # freshly allocated string.  Written BEFORE the implementation; on arrival
    # `why=builtin-call`.  The interpreter's answers, measured per value shape:
    #   1 -7 2.5 0.10000000000000001 true nil ab "" [1, 2] [] {a: 1} {}
    #   <function> set(2) set(R interval)
    # Three facts in this one program are each a failure mode:
    #   - `str(0.1)` is `0.10000000000000001`, NOT `0.1`: str formats a float
    #     with %.17g while `say` uses the shorter value_to_string format.  A
    #     route that reused mv_say's formatter prints 0.1 and fails here.
    #   - `str([1, 2])` is `[1, 2]` and `str({"a": 1})` is `{a: 1}`: the
    #     container arm is value_to_string's, the same one `say` uses, so this
    #     is the case where the two formats COEXIST inside one call.
    #   - `str(f)` is `<function>` and `str(nil)` is `nil` -- the words, not
    #     empty strings and not a pointer.
    ("mir_str_of_every_shape",
     'say str(1)\nsay str(-7)\nsay str(2.5)\nsay str(0.1)\nsay str(true)\n'
     'say str(nil)\nsay str("ab")\nsay str("")\nsay str([1, 2])\nsay str([])\n'
     'say str({"a": 1})\nsay str({})\n'
     'f = () -> 42\nsay str(f)\n'
     's = 1, 2\nsay str(s)\n'
     'iv = [1~3]\nsay str(iv)\n'),
    # THE FLOAT SPLIT, side by side, because that is the only reading that
    # separates the two formatters: `say 0.1` is 0.1 and `say str(0.1)` is
    # 0.10000000000000001; `say 1/3` is 0.333333 and `say str(1/3)` is
    # 0.33333333333333331; `say 123456789.123456789` is 123456789.123457 and
    # its str is 123456789.12345679.  `str([0.1, 2])` is `[0.1, 2]` -- the
    # container arm uses the SHORT format even inside str, so the two formats
    # are chosen per value type and not per call.  `len(str(0.1))` is 19: the
    # answer is a real heap string, and 19 is its length in bytes.
    ("mir_str_float_split_side_by_side",
     'say 0.1\nsay str(0.1)\nsay 1 / 3\nsay str(1 / 3)\n'
     'say 123456789.123456789\nsay str(123456789.123456789)\n'
     'say str([0.1, 2])\nsay str(1.0)\nsay str(str(1))\nsay len(str(0.1))\n'),
    # sum(): the first builtin this route emits that can THROW, and the first
    # that consumes mv_list's set enumeration.  Written BEFORE the
    # implementation; on arrival `why=builtin-call`.  The interpreter's answers:
    #   6 0 3.5 3.5 nil nil nil nil 3.5 nil
    # Three separate answers live in this program and each has its own failure:
    #   - the fold: [1, 2, 3]->6 (int), []->0, [1, 2.5]->3.5 and [2.5, 1]->3.5
    #     (float as soon as ANY element is float, in either order), and a SET
    #     `1, 2.5`->3.5 through the same enumeration mv_list uses;
    #   - the nil answer for a NON-CONTAINER (3, "ab", nil) and for a set that
    #     cannot be enumerated (the R interval [1~3]);
    #   - nothing here throws, so this case is the fold and the nil boundaries.
    ("mir_sum_fold_and_nil_boundaries",
     'say sum([1, 2, 3])\nsay sum([])\nsay sum([1, 2.5])\n'
     's = 1, 2.5\nsay sum(s)\n'
     'iv = [1~3]\nsay sum(iv)\nsay sum(3)\nsay sum("ab")\nsay sum(nil)\n'
     'say sum([2.5, 1])\nsay sum("1, 2"[0])\n'),
    # THE THROW, and it is a separate case because it is the only one that reads
    # stderr and the exit code: `say "before"` prints, then sum([1, "a", 2])
    # raises `sum: non-numeric element`, so stdout is exactly `before\n` and rc
    # is 1.  A route that answered nil (the old POSIX behaviour) or 3 (summing a
    # as 0) would print a second line and exit 0 -- both are caught here and
    # neither is caught by the fold case above.  The message is pinned by
    # STDERR_EXPECT, and STDERR_EXCLUDE forbids `numeric_overflow`, which is the
    # OTHER way this builtin can stop.
    ("mir_sum_non_numeric_throws",
     'say "before"\nsay sum([1, "a", 2])\n'),
    # THE OTHER MALFORMED ELEMENT, and it needs its own program because the
    # first throw ends the run: a BOOL element.  `sum([1, true])` raises
    # `sum: non-numeric element` (the element rule is VAL_INT/VAL_FLOAT only),
    # so stdout is empty but for the marker and rc is 1.  This is the line that
    # separates "malformed element throws" from "anything that looks numeric is
    # summed": a route that let a BOOL fall into the int arm prints 2 and exits
    # 0, and every line of the case above would still pass.
    ("mir_sum_bool_element_throws",
     'say "bool"\nsay sum([1, true])\n'),
    # REMOVE, PUSH, POP -- the first WRITERS that change a region's LENGTH.
    # Written BEFORE the implementation; on arrival all four are
    # `why=builtin-call`.  The interpreter's answers are in the comments below.
    #
    # 1. remove() ON BOTH CONTAINERS, and the six shapes that answer false.
    #    MEASURED, in order: true 1 2 true [10, 30] 10 30 2 false false false
    #    false false.  The array line pins the SHIFT (a[1] is 30, not 20, and
    #    len is 2): a remove that nils the slot instead of shifting answers
    #    `[10, nil, 30]` -- different bytes, same length, and only `say a` sees
    #    it.  The six falses cover a missing key, an out-of-range index, a
    #    NEGATIVE index (a write with a negative subscript throws, a remove
    #    does NOT -- measured), an empty array, a non-container, and a missing
    #    key on a dict.
    ("mir_remove_both_containers",
     'd = {"a": 1, "b": 2}\na = [10, 20, 30]\n'
     'say remove(d, "a")\nsay len(d)\nsay d["b"]\n'
     'say remove(a, 1)\nsay a\nsay a[0]\nsay a[1]\nsay len(a)\n'
     'say remove(a, 9)\nsay remove(a, -1)\nsay remove([], 0)\n'
     'say remove(nil, "a")\nsay remove({"a": 1}, "z")\n'),
    # 2. THE ALIASES, TWO ROUNDS.  MEASURED: true 1 1 true 0 0 3 1 true 0
    #    false 0.
    #    Round one removes through `e` and reads through `d` (both see 1).
    #    Round one's second half empties the dict and BOTH aliases read 0 --
    #    that is the header moving with the shrink, and it is the reading a
    #    remove that forgot to rewrite the count fails.
    #    Then a WRITE on the emptied dict (which must move the region, see the
    #    ALLOCS row) and a THIRD removal through `e`, which reads true while
    #    `d` reports 0 -- so the two handles still agree after the move.
    #    THE SECOND ROUND IS THE POINT: after round one the old handle is a
    #    forwarding head whose target was itself later moved, so round two is
    #    the only shape that exercises mv_arr_live's hop MORE THAN ONCE.  A
    #    first-round-only criterion passes with a single-hop resolver.
    ("mir_remove_aliases_two_rounds",
     'd = {"a": 1, "b": 2}\ne = d\n'
     'say remove(e, "a")\nsay len(d)\nsay len(e)\n'
     'say remove(d, "b")\nsay len(d)\nsay len(e)\n'
     'd["c"] = 3\nsay d["c"]\nsay len(e)\n'
     'say remove(e, "c")\nsay len(d)\n'
     'say remove(d, "c")\nsay len(e)\n'),
    # 3. THE SHORTEN-THEN-GROW PATH, with its own control in the same file so
    #    the two allocation readings come from ONE run each and are absolute.
    #    MEASURED on the interpreter: true 5 1 7 1 (after `remove`, `d["z"]=5`
    #    lands and `len(d)` is 1).  What this case exists for is the ALLOCS
    #    row: a region that was SHORTENED is never at the frontier (`mv_arrused`
    #    is deliberately not rolled back -- see the note on mv_remove), so the
    #    next write MUST copy the dict to the frontier and leave a forwarding
    #    head.  The control program below does the same write without the
    #    remove, and the two readings are pinned separately:
    #      mir_remove_then_grow        allocs=3|bytes=1024   (dict + array + the copy)
    #      mir_grow_without_remove     allocs=2|bytes=768    (dict + array, no copy)
    #    The extra block IS the move.  Pinned as absolute numbers, not as
    #    "one more than the control": a relative assertion survives both numbers
    #    drifting together, which is the mistake the mv_fun_cap count made.
    ("mir_remove_then_grow",
     'd = {"a": 1}\nsay remove(d, "a")\nd["z"] = 5\nsay d["z"]\n'
     'say len(d)\na = [7, 8, 9]\nsay a[0]\nsay len(d)\n'),
    ("mir_grow_without_remove",
     'd = {"a": 1}\nd["z"] = 5\nsay d["z"]\n'
     'say len(d)\na = [7, 8, 9]\nsay a[0]\nsay len(d)\n'),
    # 4. push() AND pop().  MEASURED: 1 [1, 2, 3] 3 1 [1, 2] 1 1 3 [1, 2] nil
    #    nil nil.
    #    push ANSWERS 1 ALWAYS -- even for `push(nil, 1)` and `push(5, 1)`,
    #    where it writes nothing at all.  That "returns 1 and does nothing" is
    #    the semantic an implementation delegating to mv_arr_set/mv_dict_set
    #    cannot get from them, and the two lines after it prove the variables
    #    really did not change (`say b` is still [1, 2] from the alias write).
    #    pop answers the POPPED VALUE (`3`), leaves the array shorter, and
    #    answers nil for an empty array, nil, and a string.
    ("mir_push_and_pop",
     'a = [1, 2]\nsay push(a, 3)\nsay a\nsay len(a)\n'
     'b = [1]\nc = b\nsay push(c, 2)\nsay b\n'
     'say push(nil, 1)\nsay push(5, 1)\n'
     'say pop(a)\nsay a\nsay pop([])\nsay pop(nil)\nsay pop("ab")\n'),
    # THE FRONTIER COPY, made a NAMED PAIR instead of a paragraph.
    #
    # It existed only in a commit message and in docs/STATUS.md §10.166 before
    # this: two programs differing by one `remove(d, "a")`, whose readings
    # differ by exactly the doubling a frontier copy costs.  A comparison that
    # lives in prose is dropped the next time someone re-pins; a comparison that
    # is two named cases with two pinned ALLOCS rows survives.
    #
    # THE PRICE WAS THE THING I GOT WRONG, so the arithmetic is written down
    # here for the next person: the pool reserves 8 slots (256 bytes) on first
    # use and DOUBLES when the frontier needs more, allocating the whole new
    # block and copying the live prefix (mv_arr_need).  So
    #     a copy that FITS in the current block        costs 0 allocations
    #     a copy that pushes the frontier past 8 slots  costs ONE 512-byte block
    # -- not "one 256-byte block per copy", which is what I assumed and why
    # three of my five predictions were wrong in that cell.
    #
    # Both programs fit one block until the dict is written:
    #   control  d={"a":1}; d["z"]=5; a=[7, 8]; say d["z"]; say a[0]   -> 5 7
    #   subject  the same, with `say remove(d,"a")` first               -> true 5 7
    # The control's write lands in the frontier block; the subject's cannot,
    # because the shorten left `iv + n' < mv_arrused`, so it copies and doubles.
    #   IR-MIR-ALLOCS|case=mir_remove_pushes_frontier_control|allocs=1|bytes=256
    #   IR-MIR-ALLOCS|case=mir_remove_pushes_frontier|allocs=2|bytes=768
    # THIS is the row that can tell "copied" from "did not copy" -- the one
    # named mir_remove_then_grow cannot (both sides read 2|768 there, which is
    # recorded in ALLOCS_EXPECT and in the pin's allocs-scope).
    ("mir_remove_pushes_frontier_control",
     'd = {"a": 1}\nd["z"] = 5\na = [7, 8]\nsay d["z"]\nsay a[0]\n'),
    ("mir_remove_pushes_frontier",
     'd = {"a": 1}\nsay remove(d, "a")\nd["z"] = 5\na = [7, 8]\n'
     'say d["z"]\nsay a[0]\n'),
    # substr(): THE FIRST BUILTIN THAT GROWS BYTES IN THE POOL.  Every builtin
    # before it either read a container or produced a value from existing
    # slots; this one cuts a NEW string out of another and has to allocate for
    # it.  Written BEFORE the implementation; on arrival `why=builtin-call`.
    #
    # MEASURED, in order (the bytes, not a transcription):
    #   bcd / ef / (empty) / cdef / (empty) / ab / (empty) / ab / (empty)
    #   / the two bytes C3 A9 / 3
    # The clamping rules are all in that one program, and each line has its own
    # failure:
    #   ("abcdef",1,3) -> bcd      the plain case
    #   ("abcdef",-2,2) -> ef      a NEGATIVE start counts from the END
    #   ("abcdef",10,2) -> ""      start past the end gives an empty string, not
    #                              nil and not the whole string
    #   ("abcdef",2,99) -> cdef    a length past the end is CLAMPED, not refused
    #   ("",0,1) -> ""             the empty source
    #   ("ab",0,5) -> ab           clamp on an already-short source
    #   ("abcdef",0,0) -> ""       a zero length
    #   ("abcdef",-99,2) -> ab     a negative start past the beginning clamps to
    #                              0 (NOT to -99, and not empty)
    #   ("abcdef",3,-1) -> ""      a NEGATIVE length gives empty, and it does NOT
    #                              count from the end -- measured, because the
    #                              plausible-looking mirror of the start rule is
    #                              wrong here
    #   ("héllo",1,2) -> the bytes C3 A9   BYTE-wise, no UTF-8: two bytes were
    #                              asked for and two bytes come back, which happen
    #                              to be one character.  A rune-aware
    #                              implementation answers 2 bytes differently and
    #                              this line is the only one that can see it.
    #   len(substr("abcdef",1,3)) -> 3     the answer is a REAL string, not a
    #                              slice pointing into the source
    ("mir_substr_clamps_every_boundary",
     'say substr("abcdef", 1, 3)\nsay substr("abcdef", -2, 2)\n'
     'say substr("abcdef", 10, 2)\nsay substr("abcdef", 2, 99)\n'
     'say substr("", 0, 1)\nsay substr("ab", 0, 5)\nsay substr("abcdef", 0, 0)\n'
     'say substr("abcdef", -99, 2)\nsay substr("abcdef", 3, -1)\n'
     'say substr("h\u00e9llo", 1, 2)\nsay len(substr("abcdef", 1, 3))\n'),
    # THE ALLOCATION, and it is the point of this cell rather than a side note:
    # each call allocates one heap block of (clamped length + 1).  Pinned as a
    # total over a program whose every call has a DIFFERENT clamped length, so a
    # route that allocated the SOURCE length, or the requested length, moves the
    # number.  MEASURED after the arm landed (the row below is the post-run
    # value, and its derivation is written beside it in ALLOCS_EXPECT).
    ("mir_substr_allocates_its_answer",
     'say substr("abcdef", 1, 3)\nsay substr("abcdef", 2, 99)\n'
     'say substr("abcdef", 3, -1)\n'),
    # split(): the first builtin whose answer is a CONTAINER OF NEW STRINGS.
    # substr() produced one string; this produces an array whose elements are
    # each freshly allocated bytes.  Written BEFORE the implementation; on
    # arrival `why=builtin-call`.
    #
    # THE FOUR SHAPES THE PLAN'S TABLE DID NOT LIST, measured before a criterion
    # was written for any of them -- and ONE OF THEM DECIDES THE WHOLE ARM:
    #   ("a,b,", ",")   -> [a, b]   TWO elements: the TRAILING empty is DROPPED
    #   (",a", ",")     -> [, a]    TWO elements: the LEADING empty is KEPT
    #   ("a::b", "::")  -> [a, b]   the separator matches as a WHOLE STRING, not
    #                               character by character (that would be [a,,b])
    #   (123, ",")      -> []       a non-string source is read as the EMPTY
    #                               string, so it yields nothing -- not ["123"]
    # The leading/trailing asymmetry is not a bug in the interpreter: its loop
    # is `while (*p)` with `strstr`, so a match at position 0 still pushes an
    # empty piece while a trailing separator just advances p to the NUL and the
    # loop ends.  Copied, and pinned by the two lines that differ.
    #
    # THE PAIR THAT LOOKS IDENTICAL AND IS NOT:
    #   split(",", ",") -> prints [] but len is 1   (one empty-string element)
    #   split("", ",")  -> prints [] and len is 0   (a genuinely empty array)
    # Both print the same three bytes.  Only len() separates them, which is why
    # the program below prints len() beside both -- a route that answered "one
    # empty element" for the empty source would pass every `say` in this file.
    #
    # THE TWO OPPOSITE BOUNDARIES, in one program on purpose: an empty SOURCE
    # yields [] (nothing to split), while an empty SEPARATOR yields [the whole
    # string] (the interpreter substitutes " " and "abc" has no space in it).
    # Keeping them adjacent makes it visible which one is the special case.
    #
    # MEASURED, byte-exact, in order:
    #   [a, b, c] / [] / [abc] / [] / 2 / [, a] / [a, b] / [] / [] / 1
    #   / [, ] / [a, , b] / 0
    ("mir_split_edges_and_the_trailing_empty",
     'say split("a,b,c", ",")\nsay split("", ",")\nsay split("abc", "")\n'
     'say split(nil, ",")\nsay len(split("a,b,", ","))\nsay split(",a", ",")\n'
     'say split("a::b", "::")\nsay split(123, ",")\n'
     'say split(",", ",")\nsay len(split(",", ","))\nsay split(",,", ",")\n'
     'say split("a,,b", ",")\nsay len(split("", ""))\n'),
    # THE ALLOCATION, and it is what separates "a container of new bytes" from
    # "a container of views into the source".  PREDICTION, written before the
    # arm exists, in the form the previous two cells earned: state the number,
    # then check it, and if the run disagrees, find the mechanism -- do not
    # edit the number.
    #
    #   Every element is its OWN allocation of (piece length + 1).  The
    #   interpreter ALSO strdups the source and the separator; this route does
    #   not need to (it reads the values it was handed and owns neither), and
    #   the first thing the run settled is that it does not.
    #
    #   split("a,b,c", ",") = THREE pieces of one byte each, four slots needed
    #   (one count header + three elements) which the pool's first block of
    #   EIGHT slots (256 bytes) covers in one allocation, so:
    #        3 element blocks  1+1, 1+1, 1+1        = 6 bytes
    #        1 pool block      ................       = 256 bytes
    #        ------------------------------------------------
    #        4 blocks                                262 bytes
    #   PREDICTED 6|144 (which added the two strdups and priced the region at
    #   128), MEASURED 4|262.  BOTH differences are the prediction's fault, not
    #   the arm's, and they are recorded rather than smoothed:
    #     - the two strdups do not exist here (they are the interpreter owning
    #       its own inputs; this route does not), so 6 blocks -> 4;
    #     - the pool block is 256 bytes (8 slots), not 128 (4 slots), so 144 ->
    #       262.  I priced the region at exactly the slots it needed, which is
    #       the same "increments" mistake as the frontier-copy cell -- the pool
    #       allocates its whole block, never a fitted one.
    #   The criterion's job is unharmed: it still catches a route that shares
    #   bytes instead of copying (0 element blocks) or that allocates per
    #   character rather than per piece.
    ("mir_split_allocates_new_bytes_per_element",
     'a = split("a,b,c", ",")\nsay len(a)\nsay a[0]\nsay a[2]\n'),
    # join(): container -> ONE new string, the mirror of split (which answers a
    # container of new strings).  Written BEFORE the implementation.
    #
    # SIX SHAPES MEASURED BEFORE ANY CRITERION WAS WRITTEN, and ONE OF THEM
    # DECIDES THE ARM:
    #   join([1, 2, 3])            -> 123       the default separator is the
    #                                           EMPTY STRING
    #   join([1, 2.5, true, nil])  -> 1-2.5-true-nil
    #                                           bool and nil get their printed
    #                                           words, not "" and not a skip
    #   join([[1, 2], 3], "-")     -> [1, 2]-3  a nested container is
    #                                           SERIALISED, not refused
    #   join(nil, "-") / join("abc", "-") / join({"a": 1}, "-")
    #                              -> ""        non-arrays answer the EMPTY
    #                                           STRING (not nil, not a throw)
    #   join(["%s", "%d"], "-")    -> %s-%d     literal: no printf on the data
    #   join([1.0, 0.1+0.2, 1/3], "-") -> 1-0.3-0.333333
    #
    # THE LAST ONE IS THE REASON THIS CELL EXISTS.  0.1+0.2 prints 0.3 through
    # join and 0.30000000000000004 through str(): join formats each element with
    # value_to_string (mv_vts, the SHORT format) while str() reached for %.17g.
    # A criterion written only over integer elements cannot see the difference
    # (2.5 looks the same either way), which is why the float line below prints
    # join(...) and str(...) of the SAME expression side by side.
    #
    # MEASURED, byte-exact, in order:
    #   123 / 1-2-3 / ab / (empty) / 12 / 1-2.5-true-nil / [1, 2]-3
    #   / (empty) / (empty) / (empty) / 1|[2, [3]] / 1-0.3-0.333333
    #   / 0.30000000000000004 / 0.33333333333333331 / a::b::c / 0
    ("mir_join_every_shape_and_the_default_separator",
     'say join([1, 2, 3])\nsay join([1, 2, 3], "-")\nsay join(["a", "b"], "")\n'
     'say join([], "-")\nsay join([1, 2])\n'
     'say join([1, 2.5, true, nil], "-")\nsay join([[1, 2], 3], "-")\n'
     'say join(nil, "-")\nsay join("abc", "-")\nsay join({"a": 1}, "-")\n'
     'say join([1, [2, [3]]], "|")\n'),
    # THE FLOAT SPLIT, join beside str on the same expression, plus the literal
    # `%` case.  Both lines are needed: the first is the only pair that can tell
    # mv_vts from %.17g, and the second is what proves no printf is applied to
    # element bytes (`join(["%s","%d"])` must print them verbatim; a printf-based
    # implementation prints garbage or crashes).
    ("mir_join_float_format_and_percent",
     'say join([1.0, 0.1 + 0.2, 1 / 3], "-")\nsay str(0.1 + 0.2)\n'
     'say str(1 / 3)\nsay join(["%s", "%d"], "-")\nsay join(["a", "b", "c"], "::")\n'
     'say len(join([], "-"))\n'),
    # chars() / ord() / chr(): THE BYTE MODEL, and the cell whose real risk is
    # the MEASUREMENT CHANNEL rather than the implementation.
    #
    # THE CHANNEL PROBLEM, stated before the program: `say chr(200)` prints a
    # byte that is not text on every terminal, and a criterion that compares
    # stdout as TEXT could not tell "the right byte" from "the byte was eaten".
    # So the model is pinned in TWO programs on purpose:
    #   - the one below uses PRINTABLE ASCII only, so its stdout is text and the
    #     byte-for-byte comparison means what it looks like;
    #   - mir_bytes_high_bytes_via_ord below never prints a high byte: it reads
    #     each one back through ord(), which is an INTEGER channel.  MEASURED:
    #     ord(chr(128)) is 128, ord(chr(200)) is 200, ord(chr(255)) is 255 --
    #     the bytes ARE there and just cannot be printed.
    #
    # BYTE-WISE, NOT CODEPOINTS, and both directions have to agree for that
    # claim to mean anything -- measured together:
    #     len(chars("é"))  is 2      (two bytes, not one character)
    #     ord("é")         is 195    (0xC3, the FIRST byte, not 233)
    # One line alone could be satisfied by a rune-aware implementation; the pair
    # cannot.
    #
    # THE NON-STRING SOURCES, all measured: chars(nil) and chars(123) are [],
    # ord(nil) and ord(123) are 0, ord("") is 0, and chr("65") / chr(1.5) /
    # chr(true) all answer the EMPTY string (the argument is read as an ival,
    # and none of those carry a useful one).
    #
    # THE NUL FAMILY, every line with its len() because after a NUL every `say`
    # looks the same -- this is the rule the sibling cells earned and it applies
    # twice over here: chr(0) prints nothing AND len(chr(0)) is 0, while
    # chr(-1) prints a raw 0xFF (len 1) and chr(257) prints 0x01 (len 1), so
    # the "& 0xff then stop at NUL" rule is visible only through len.
    #
    # MEASURED, byte-exact, in order:
    #   [] / [] / 0 / 0 / (empty) / (empty) / (empty) / 0 / [] / 2 / 65
    #   / 0 / 0 / 0 / [] / 0/A / 5 / a-b-c / 6 / 97 / 0
    ("mir_bytes_ascii_roundtrip",
     'say chars(nil)\nsay chars(123)\nsay ord(nil)\nsay ord(123)\n'
     'say chr("65")\nsay chr(1.5)\nsay chr(true)\nsay len(chars(""))\n'
     'say chars("")\nsay len(chars("\u00e9"))\nsay ord("A")\n'
     'say len(chr(0))\nsay ord(chr(0))\nsay len(chars(chr(0)))\n'
     'say chars(chr(0))\nsay len(chr(256))\nsay chr(65)\n'
     'say len(join(chars("abc"), "-"))\nsay join(chars("abc"), "-")\n'
     'say ord("abc")\nsay ord("")\n'),
    # THE HIGH BYTES, READ BACK THROUGH ord() -- an integer channel, so the
    # comparison is text-safe even though the values are not text.  This is the
    # only program in the file whose subject cannot be printed, and it is also
    # the crossing point with §10.169: join(chars(s), "-") splits into bytes and
    # joins them back, so a byte that survived the round trip is proven twice.
    #
    # MEASURED: 128 / 255 / 200 / 65 / ab-c (via join of two one-byte pieces)
    #           / 1 / 1 / 1   -- the three lens are chr(-1), chr(257), chr(200).
    ("mir_bytes_high_bytes_via_ord",
     'say ord(chr(128))\nsay ord(chr(255))\nsay ord(chr(200))\nsay ord(chr(65))\n'
     'say join(chars(join(["a", "b"], "")), "-")\n'
     'say len(chr(-1))\nsay len(chr(257))\nsay len(chr(200))\n'),
    # replace() / index() / startswith() / endswith(): FOUR PURE SCANS, and the
    # correction that matters is about what a non-string argument MEANS.
    #
    # THE PLAN'S TABLE SAYS `startswith("abc", 3)` is true and calls it "the
    # non-string pattern is stringified".  MEASURED, that is not the mechanism:
    # every one of these four reads a non-string argument as the EMPTY STRING
    # (`v.type == VAL_STRING && v.sval ? v.sval : ""`), never through the `say`
    # formatting path.  Which is why
    #     startswith("abc", 3)  is true    (compare 0 bytes -> always true)
    #     startswith("abc", 4)  is ALSO true, and so is startswith("abc", 0)
    #     index("abc", 3)       is 0       (empty needle matches at 0)
    # A READER WHO TAKES `3` FOR A SUBSCRIPT would predict a length check and be
    # wrong on all three, so the criterion prints 3, 4 AND 0 -- the "4" line is
    # the one that kills the subscript reading, because as a subscript it would
    # be out of range.
    #
    # THE EMPTY-NEEDLE FAMILY, each measured:
    #     index("abc", "")      0        strstr returns the string itself
    #     replace("abc", "", "-") "abc"  returned unchanged, NOT an infinite loop
    #     startswith/endswith(...,"") true
    # replace's empty needle is the shape that hangs a naive implementation: the
    # interpreter returns the source unchanged before entering its loop.
    #
    # THE OVERLAP RULE: `replace("aaa", "aa", "b")` is `ba`, i.e. a left-to-right
    # NON-overlapping scan (after a hit it resumes past the needle).  A
    # restart-from-the-beginning scan answers `b` and a naive one loops forever.
    # `replace("abcabc", "bc", "X")` is `aXaX`, which pins the same rule twice.
    #
    # BYTE OFFSETS, not character offsets, consistent with §10.170:
    #     index("héllo", "l")  is 3   ("é" is two bytes, so the first `l` sits at
    #                                byte 3, not character 2)
    #     replace("hé", "é", "e") is `he`  -- the needle is those two bytes
    #
    # A NON-STRING SOURCE OR PATCH reads as the empty string too, so
    #     index(nil, "a")            -1
    #     startswith(nil, "")        true
    #     replace(nil, "a", "b")     ""      (empty source, so nothing to replace)
    #     replace("abc", "a", nil)   "bc"    the nil PATCH is the empty string,
    #                                        i.e. a deletion.  This line is the
    #                                        one most easily left unmeasured and
    #                                        it changes what replace means.
    #
    # MEASURED, byte-exact, in order:
    #   0 / abc / true / true / ba / aXaX / true / true / true / true / 0
    #   / -1 / true / (empty) / bc / a / 3 / he / 1 / true
    ("mir_scan_empty_needle_and_overlap",
     'say index("abc", "")\nsay replace("abc", "", "-")\n'
     'say startswith("abc", "")\nsay endswith("abc", "")\n'
     'say replace("aaa", "aa", "b")\nsay replace("abcabc", "bc", "X")\n'
     'say startswith("abc", 3)\nsay startswith("abc", 4)\n'
     'say startswith("abc", 0)\nsay endswith("abc", 3)\nsay index("abc", 3)\n'
     'say index(nil, "a")\nsay startswith(nil, "")\nsay replace(nil, "a", "b")\n'
     'say replace("abc", "a", nil)\nsay index("héllo", "l")\n'
     'say replace("hé", "é", "e")\nsay index("abcabc", "bc")\n'
     'say len(replace("abc", "", "xyz"))\n'),
    # THE ALLOCATION AXIS, kept SEPARATE on purpose: index/startswith/endswith
    # answer an int or a bool and allocate NOTHING, while replace allocates the
    # result bytes.  Mixing them into one total would hide the three zeros --
    # and "did this scan secretly build an intermediate string" is exactly what
    # the zero is for.  So this program is replace-ONLY, and the all-scan
    # program above carries the zero through its own row.
    ("mir_replace_allocates_only_its_answer",
     'say replace("abcabc", "bc", "X")\nsay len(replace("aaa", "", "-"))\n'),
    # upper() / lower() / trim(): the ASCII-only transforms, and the one that
    # asks a QUESTION about the trim set rather than about the string.
    #
    # ASCII ONLY, BYTE-WISE -- measured, not assumed: upper("é") is "é" (both
    # bytes unchanged, so this is not locale case folding), while
    # upper("àbc") is "àBC" (the 0xC3 0xA0 lead stays, the ASCII tail folds).
    # That pair is what separates "fold ASCII, leave every other byte" from
    # "fold per character according to the locale".
    #
    # NON-STRING SOURCE reads as the empty string, the same rule §10.171
    # measured for the scans (measured again here rather than carried over):
    #   upper(nil) / upper(123) / trim(nil) / trim(123) are all ""
    #
    # TRIM'S SET IS AN ENUMERATION, and the criteria are built to say so:
    #   trim(" x ")           "x"     (len 1)
    #   trim("\t\n x \r")     "x"     (len 1)   -- the four listed characters
    #   trim(chr(11) + " x")  0x0B x   (len 3)   -- vertical tab NOT in the set
    #   trim(chr(12) + " x")  0x0C x   (len 3)   -- form feed NOT in the set
    # NOTE ON HOW THE CONTROL BYTES ARE WRITTEN: the source language does NOT
    # recognise \v or \f escapes -- measured, `len("\v x")` is 4, i.e. a
    # backslash, a v, a space and an x -- so a criterion written with the escape
    # would be testing four PRINTABLE characters and would pass an isspace()
    # implementation for the wrong reason.  chr(11) and chr(12) put the real
    # bytes in, which is why the set question is actually asked here.
    # An isspace() implementation passes the first two and fails the last two;
    # leaving x inside those lines would only prove "did not eat x", which the
    # first two already prove.  This is §10.171's lesson applied forward: the
    # criterion is the question, not the shape.
    # trim() strips ONLY the ends: trim("a b  ") is "a b", the inner space stays.
    ("mir_case_ascii_only_and_nonstring",
     'say upper(nil)\nsay len(upper(123))\nsay upper(123)\nsay upper("\u00e9")\n'
     'say len(upper("\u00e9"))\nsay upper("\u00e0bc")\nsay lower("\u00c0BC")\n'
     'say upper("abc")\nsay lower("ABC")\nsay len(trim(" abc "))\n'),
    # THE TRIM ALLOCATION AXIS is its own program for the same reason the
    # replace axis was: one line has to answer "does a trim that cuts nothing
    # still copy" and the answer must not be averaged into a transform's total.
    #   trim("abc")   cuts nothing -- whether it copies is the reading
    #   trim(" abc ") DOES cut, so its answer block is 3 bytes
    ("mir_trim_charset_is_an_enumeration",
     'say len(trim(" x "))\nsay len(trim("\\t\\n x \\r"))\n'
     'say len(trim(chr(11) + " x"))\nsay len(trim(chr(12) + " x"))\n'
     'say trim("a b  ")\nsay len(trim(""))\n'
     'say len(trim("   "))\nsay trim("  a")\nsay len(trim("abc"))\n'),
    ("mir_trim_allocates_only_when_it_cuts",
     'say trim("abc")\nsay trim(" abc ")\n'),
    # THE BIT CELL: six builtins, and the ONE question that decides whether an
    # implementation is right is what a RIGHT SHIFT does to a negative number.
    #
    # MEASURED, all six value lines byte-exact:
    #   shr(-1,1) -1   shr(-8,2) -2   shr(-5,1) -3   -- ARITHMETIC, it keeps the
    #   sign, and a logical-shift implementation reddens exactly these three.
    #   shr(1,64) 0    shr(-8,65) -1  shr(1,65) 0   -- counts past 63 SATURATE
    #   rather than wrap or throw: positive to 0, negative to -1.
    #   shl(5,34) 85899345920  -- NOTE: the plan's example prints 8589934592,
    #   which is 1<<33; the measured answer for 5<<34 is ten times that, and the
    #   criterion pins the measured one.  A criterion copied from the plan would
    #   have been red for a reason that has nothing to do with the implementation.
    #   shl(1,62) 4611686018427387904  shl(-5,2) -20  -- big ints print in exact
    #   decimal, which is also what says the shifted width matches mv_int.
    #   bit_and(-5,3) 3, bit_or(4,2) 6, bit_xor(-1,5) -6, bit_xor(6,3) 5
    #   bit_not(3) -4, bit_not(0) -1, bit_not(-1) 0  -- two's complement.
    #
    # AND THE SHAPE THE PLAN GETS WRONG THE OTHER WAY: bit_and/bit_or/bit_xor/
    # bit_not DO NOT COERCE.  Measured, `bit_and(1.9,3)`, `bit_and(-1.9,3)`,
    # `bit_and("5",3)`, `bit_and(nil,3)`, `bit_and(true,3)` and `bit_not(nil)`
    # ALL throw `<name>: expected two integers` / `bit_not: expected an integer`.
    # The previous two cells established that scans and transforms read a
    # non-string as the empty string; THAT rule does NOT carry over here, which
    # is exactly why this line was measured instead of assumed.  A truncating
    # implementation passes every value line and reddens only these throw cases.
    #
    # OVERFLOW IS AN ERROR, NOT A WRAP: shl(1,63), shl(1,64), shl(3,62) all throw
    # numeric_overflow -- so shl is checked multiplication, while shr saturates.
    # A wrapping shl passes the value lines and reddens the overflow cases.
    #
    # THE COUNT ITSELF must be a non-negative INT: shr(1,-1) and shl(1,-1) throw
    # numeric_overflow (the same message as overflow, which is worth knowing).
    #
    # Throws are SEPARATE programs because an uncaught exception ends the
    # program: one program can only demonstrate one throw, and mixing them into
    # the value program would truncate the value lines and hide the rest.
    ("mir_bits_values_and_width",
     'say shr(-1, 1)\nsay shr(-8, 2)\nsay shr(-5, 1)\n'
     'say shr(1, 64)\nsay shr(-8, 65)\nsay shr(1, 65)\n'
     'say shl(5, 34)\nsay shl(1, 62)\nsay shl(-5, 2)\n'
     'say bit_and(5, 3)\nsay bit_and(-5, 3)\nsay bit_or(4, 2)\n'
     'say bit_xor(-1, 5)\nsay bit_xor(6, 3)\nsay bit_not(3)\n'
     'say bit_not(0)\nsay bit_not(-1)\n'),
    ("mir_shl_overflow_is_an_error",
     'say "before"\nsay shl(1, 63)\n'),
    ("mir_bitwise_noninteger_throws",
     'say "before"\nsay bit_and(1.9, 3)\n'),
    # THE CONVERSION FAMILY, first pair: bool and float.  Both are TOTAL --
    # every shape has an answer, so neither has a throw line -- and that is
    # exactly why they are measured together and why their matrixes are one
    # program each: a name-only alias to `say`'s truthiness would pass half the
    # bool lines and print a container's own bytes on the rest.
    #
    # bool() measured on the interpreter (each line below is one `say`):
    #   bool(1) true      bool(0)  false   bool(2) true     bool(-1) true
    #   bool(nil) false   bool(1.5) true   bool(0.0) false  bool(-0.0) false
    #   bool(true) true   bool("") true    bool("x") true   bool(f) true
    #   bool([]) true     bool({}) true    bool([1, 3)) true
    # The two lines that are NOT what a reader would guess: the EMPTY STRING is
    # TRUE (this language has no "empty is false" rule), and -0.0 is FALSE, so
    # the float arm is a numeric-zero test and not a sign test.
    #
    # float() measured on the interpreter:
    #   float(3) 3        float(true) 1     float(nil) 0
    #   float("2.5") 2.5  float("x") 0       float([]) 0    float({}) 0
    #   float([1, 3)) 0   float(f) 0        float(9223372036854775807)
    #     9.2233720368547758e+18            float(2.5) 2.5
    # The last-but-one line is the one that puts the display format under the
    # criterion: a route that formatted the double the way `say` formats a
    # literal would print 9223372036854775808 here.
    ("mir_bool_truthiness_and_zero_float",
     'f = () -> 42\n'
     'say bool(1)\nsay bool(0)\nsay bool(2)\nsay bool(-1)\n'
     'say bool(nil)\nsay bool(1.5)\nsay bool(0.0)\nsay bool(-0.0)\n'
     'say bool(true)\nsay bool("")\nsay bool("x")\nsay bool(f)\n'
     'say bool([])\nsay bool({})\nsay bool([1, 3))\n'),
    ("mir_float_converts_scalars_and_zeroes_containers",
     'f = () -> 42\n'
     'say float(3)\nsay float(true)\nsay float(nil)\n'
     'say float("2.5")\nsay float("x")\nsay float([])\nsay float({})\n'
     'say float([1, 3))\nsay float(f)\n'
     'say float(9223372036854775807)\nsay float(2.5)\n'),
    # round() is IDENTITY, and that is the finding, not a shortcut: measured on
    # the interpreter, round(2.5) is 2.5, round(2.6) is 2.6, round("x") is "x",
    # round(true) is true, round(nil) is nil, round([1,2]) is the same array
    # literal and round(f) is the function.  There is no half-away or banker
    # rule to copy because round does not round anything -- and the two lines
    # that would look like rounding are the ones a reader would add: 2.5 stays
    # 2.5 and 0.5 stays 0.5.  The display lines matter too: 2.6 prints 2.6,
    # not 3, because the value is unchanged all the way to the printer.
    #
    # A two-or-more-argument round answers a POSITION-DEPENDENT leftover
    # (measured: (1,2) -> 1, (1,2,3) -> 2, ("a",2) -> nil, (nil,2) -> nil) and
    # a zero-argument one answers the destination register's old value -- both
    # are the stale-stack class, so both are refused BY SHAPE and named below,
    # while the one-argument shape (the only one whose answer is a function of
    # the expression) is what this program measures.
    ("mir_round_is_identity",
     'f = () -> 42\n'
     'say round(2.5)\nsay round(-2.5)\nsay round(2.4)\nsay round(2.6)\n'
     'say round(0.5)\nsay round(-0.5)\nsay round(1)\n'
     'say round("2.5")\nsay round("x")\nsay round(true)\nsay round(nil)\n'
     'say round([1, 2])\nsay round({"a": 1})\nsay round(f)\n'
     'say round(round(2.5))\n'),
    # sqrt() reads ONE number and answers a double, and the number is not
    # always a float: sqrt(4) is 2, sqrt(9) is 3, sqrt(true) is 1 and
    # sqrt(9223372036854775807) is 3037000499.97605, while sqrt(-1) is `nan`
    # (the interpreter's printf prints it that way).  The short display is part
    # of the criterion: sqrt(2) prints 1.414214, not 1.4142135623730951.
    ("mir_sqrt_numbers_and_nan",
     'say sqrt(4)\nsay sqrt(2)\nsay sqrt(9)\nsay sqrt(2.25)\n'
     'say sqrt(0)\nsay sqrt(-1)\nsay sqrt(1e30)\nsay sqrt(true)\n'
     'say sqrt(9223372036854775807)\nsay sqrt(sqrt(9))\n'),
    # The three non-numbers are NOT coerced -- they THROW, and the throw is the
    # answer: the interpreter reports type_mismatch.  Three programs, not one:
    # an uncaught throw ends the program at the first line, and the try/catch
    # route refuses a program whose first statement is a try (try-enters,
    # measured while this cell was being lowered -- a structural refusal, not
    # a sqrt one).
    ("mir_sqrt_nil_throws", 'say "before"\nsay sqrt(nil)\n'),
    ("mir_sqrt_string_throws", 'say sqrt("4")\n'),
    ("mir_sqrt_array_throws", 'say sqrt([1, 2])\n'),
    # match() is not a substring test: it is a POSIX EXTENDED REGEX search
    # (measured: "^abc$" true, "a|z" true, "c{1,2}" true, "\d" true on "a1b"),
    # with \d \w \s translated to classes first.  A pattern that cannot
    # compile ("[") answers false rather than throwing, and the empty pattern
    # matches everything.  The last line (case-sensitive) is the one that tells
    # an implementation that forgot REG_EXTENDED's case default from one that
    # rewrote the match as strstr.
    ("mir_match_regex_and_empty_pattern",
     'say match("abc", "b")\nsay match("abc", "z")\nsay match("abc", "^b")\n'
     'say match("abc", "b$")\nsay match("abc", "^abc$")\nsay match("abc", "a|z")\n'
     'say match("abc", "a+")\nsay match("abc", "[b-d]")\nsay match("abc", "c{1,2}")\n'
     'say match("a1b", "\\d")\nsay match("a1b", "\\w")\nsay match("abc", "")\n'
     'say match("abc", "[")\nsay match("abc", "ABC")\n'),
    # A non-string on EITHER side reads as the EMPTY string, never through the
    # say formatting: match(123, 2) is TRUE because the subject is "" and the
    # pattern is "" (and "" matches ""), not because "123" contains "2".
    # match([1, 2], 1) is true for the opposite reason: the subject is "" too,
    # but the pattern 1 is also "", so it is true by the same empty rule -- which
    # is exactly why the "\d" line above has to be in a STRING-only program.
    ("mir_match_non_strings_read_empty",
     'say match(123, 2)\nsay match(nil, "a")\nsay match([1, 2], 1)\n'
     'say match("abc", 123)\n'),
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
    # when this criterion was written a constant one outside the literal was
    # refused at translate time (why=index-out-of-range-constant).  That refusal
    # was retired by §10.151, so `a[2] = 9` would work today -- the program is
    # left as it was measured rather than rewritten for no reason.
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

    # ---- OP_SET_ADD: the comprehension's write half -------------------------
    #
    # `{ x in S | cond }` compiles to: build an EMPTY set (OP_NEW_SET r, 0, 0),
    # loop over `list(S)`, and for each member whose condition holds emit
    # OP_SET_ADD result, x.  So this opcode is the first WRITE this route gives
    # a container other than an array -- and the first that makes a set's region
    # MOVE, because a set region is [count][elem...] with no capacity slot.
    #
    # The move is the array's move (mv_arr_live's note): at the pool frontier
    # the region extends in place; elsewhere it is COPIED to the frontier and a
    # forwarding head is left at the old handle.  That is why every set reader
    # gained `mv_arr_live` in the same commit -- before it, a set could not move,
    # so they read the handle directly.
    #
    # Every reading below was taken on the interpreter first:
    #   { x in [3, 1, 2, 2] | x > 0 }     set(3) / len 3 / [1, 2, 3]
    #   { x in [1, 2, 3] | x > 1 }        [2, 3]
    #   { x in [1.5, 2.5, 1.5] | x > 1 }  [1.5, 2.5] / len 2
    #   { x in [] | x > 0 }               set(0) / len 0 / []
    #   { x in [5, 1, 9, 3] | x > 2 }     [3, 5, 9], and a container built
    #                                     AFTER the set is untouched by the adds
    ("mir_set_add_dedups_and_orders",
     's = { x in [3, 1, 2, 2] | x > 0 }\n'
     'say list(s)\n'
     'say len(s)\n'),

    ("mir_set_add_filters",
     's = { x in [1, 2, 3] | x > 1 }\n'
     'say list(s)\n'),

    ("mir_set_add_floats",
     's = { x in [1.5, 2.5, 1.5] | x > 1 }\n'
     'say list(s)\n'
     'say len(s)\n'),

    # The EMPTY source: the comprehension still builds a set (OP_NEW_SET with
    # count 0) and adds nothing, so `say s` prints set(0), len is 0 and list is
    # the empty array.  A route that answered nil for the empty set would pass
    # the first two lines and fail the third.
    ("mir_set_add_empty_source",
     's = { x in [] | x > 0 }\n'
     'say s\n'
     'say len(s)\n'
     'say list(s)\n'),

    # THE MOVE: an array is built AFTER the set, so the set's region no longer
    # ends at the pool frontier and its first add must copy it forward and leave
    # a head.  The array is read back in the same program, because a move that
    # trampled a neighbour shows there first -- and `say list(s)` twice pins that
    # the old handle still names the set after the move.
    ("mir_set_add_moves_the_region",
     's = { x in [5, 1, 9, 3] | x > 2 }\n'
     't = [7, 8, 9]\n'
     'say list(s)\n'
     'say list(s)\n'
     'say t[0]\n'),

    # ---- recv: the message queue that only a thread can fill ----------------
    #
    # `recv x` is OP_RECV r1, r2, 0 -- result register, timeout register (-1
    # when there is none), nothing else (src/compiler/compiler.c, STMT_RECV).
    # The interpreter's arm polls `t->msg_q` under `t->msg_lock`, and BOTH of
    # those exist only on a thread started by OP_THREAD_START
    # (src/vm/vm.c:4532-4533: `ImMutex *ml = im_mutex_new(); nt->msg_lock = ml;`
    # inside the thread-spawn path).  So on the single thread this route
    # accepts, the whole poll is skipped and the answer is nil -- measured:
    # `recv x` / `say x` prints nil and returns immediately, with and without a
    # timeout (`recv x, 0.5` also prints nil and does not wait).
    #
    # A message can only ever arrive from another thread, and a container
    # carrying a thread section is refused before this arm (why=threads).  So
    # nil is not a simplification here: it is the only answer this route's
    # programs can reach, and the same one the interpreter gives them.
    ("mir_recv_is_nil",
     'recv msg\n'
     'say msg\n'
     'say "after"\n'),

    ("mir_recv_with_a_timeout",
     'recv msg, 0.5\n'
     'say msg\n'),

    ("mir_recv_inside_a_function",
     'func take() {\n'
     '  recv m\n'
     '  return m\n'
     '}\n'
     'say take()\n'
     'say "done"\n'),

    # The result is a VALUE like any other: storing it and comparing it must
    # behave as nil, not as "unset".  A route that left the register untouched
    # would print whatever happened to be in it, which reads as a coincidence
    # rather than an answer.
    ("mir_recv_result_is_a_value",
     'recv m\n'
     'say m == nil\n'
     'say m == 0\n'
     'x = m\n'
     'say x\n'),

    # ---- ORDERING WITH NO ANSWER: the stop this route answers by aborting ----
    #
    # Found by tools/aot_mir_fuzz.py after every hand-written case was green, in
    # the first twenty-five generated programs -- which is the argument for a
    # generator over a hand-written corpus, made twice in one week (the set's
    # 32-bit narrowing in 10.142 was the other).
    #
    # The interpreter REFUSES a pair with no order: val_orderable (src/vm/vm.c)
    # admits two strings or two numbers, and everything else reaches
    # vm_throw_kind("type_mismatch") -- measured, all six shapes below print the
    # exception on stderr, nothing on stdout, and exit 1.  This route answered
    # them with abort() (rc 134, no message), because mv_str_cmp's mixed-pair
    # guard is an abort and mv_lt/mv_le/mv_gt/mv_ge route every string operand
    # through it.
    #
    # `true > 1` is the necessary condition and it is in the third case: a BOOL
    # is NUMERIC here (val_is_num accepts it), so the interpreter answers false
    # and this route must keep answering false.  An implementation that refuses
    # "anything that is not int or float" reddens there.
    ("mir_order_a_string_against_a_number",
     'say ("a" < 1)\n'),

    ("mir_order_a_number_against_a_string",
     'x = "s"\n'
     'say (x > 2)\n'),

    ("mir_order_a_bool_against_a_number",
     'say (true > 1)\n'
     'say (false <= 0)\n'),

    ("mir_order_a_set_against_a_number",
     'c = 1, 2\n'
     'say (c > 1)\n'),

    ("mir_order_an_array_against_a_number",
     'a = [1]\n'
     'say (a >= 1)\n'),

    ("mir_order_a_dict_against_a_number",
     'd = {"a": 1}\n'
     'say (d < 1)\n'),

    ("mir_order_nil_against_a_number",
     'say (nil > 1)\n'),

    # THE STOP HAS TO BE CATCHABLE, and that is a separate claim from "it stops".
    # mv_throw only SETS a flag; the jump to the handler is the check emitted
    # after the instruction.  The four ordering arms had the guard without the
    # check, so inside a try the pair answered `false` and carried on -- measured
    # before the fix, from the fuzzer's seed 7: the interpreter printed `caught`
    # and this route printed `false`, with the same program and the same rc.
    ("mir_order_is_catchable",
     'c = {"a": 1}\n'
     'try {\n'
     '    say ("hi" < c)\n'
     '} catch (e) {\n'
     '    say "caught"\n'
     '}\n'
     'say "after"\n'),

    # The MIXED direction the other way: a string against a string still answers,
    # so a route that refused every ordering would pass the seven cases above and
    # fail this one.
    ("mir_order_two_strings_still_answer",
     'say ("a" < "b")\n'
     'say ("a" > "b")\n'
     'say ("a" <= "a")\n'
     'say ("b" >= "a")\n'),

    # -------------------------------------------------- OP_ADD and its five rules
    # `+` is not arithmetic on this language: the interpreter's L_ADD
    # (src/vm/vm.c:3276-3326) branches FOUR ways before it ever adds, and each
    # branch was measured on build/inimerse before the row below was written:
    #   array or dict on either side -> the program DIES with
    #     "Error: '+' is not defined for arrays/dicts (arr + [x] silently loses
    #     data; use push(arr, x) instead)" on stderr, rc 1.  Not a throw: a try
    #     cannot catch it.
    #   set on either side -> union when BOTH are sets (vm_set_union builds a new
    #     set from a's sorted i64 half, then a's items, then b's two halves, so
    #     `list(a + c)` is SEGMENTED like list() of any finite set) and nil
    #     otherwise -- `a + 5` and `a + nil` both print nil.
    #   string on either side -> BOTH sides go through value_to_string and are
    #     concatenated: `"a" + 1` is "a1", `1 + "a"` is "1a", `"a" + 1.5` is
    #     "a1.5", `"a" + true` is "atrue", `"a" + nil` is "anil".  This route
    #     answered the mixed pair by abort() until this step, which is why the
    #     six rows below carry rc=0 oracles rather than refusal oracles.
    #   otherwise -> int64 add (overflow throws numeric_overflow) or double add.
    ("mir_add_string_and_int",
     'say "a" + 1\n'),
    ("mir_add_int_and_string",
     'say 1 + "a"\n'),
    ("mir_add_string_and_float",
     'say "a" + 1.5\n'),
    ("mir_add_string_and_bool",
     'say "a" + true\n'),
    ("mir_add_string_and_nil",
     'say "a" + nil\n'),
    ("mir_add_string_and_a_variable",
     'x = 5\n'
     'say "v=" + x\n'),
    # The CHAIN shape: three operands are one OP_CONCAT (the compiler folds
    # `a + b + c`), and its per-step fold is the same L_ADD rule -- so a chain
    # that reaches a number is the one place the concat fast path and the add
    # rule can disagree.  Measured: "v=5!".
    ("mir_add_string_chain_with_a_number",
     'x = 5\n'
     'say "v=" + x + "!"\n'),
    ("mir_add_sets_is_a_union",
     'a = 1, 2\n'
     'b = 3, 4\n'
     'say a + b\n'
     'say len(a + b)\n'),
    ("mir_add_sets_overlap",
     'a = 1, 2\n'
     'c = 2, 3\n'
     'say list(a + c)\n'
     'say len(a + c)\n'),
    # A set against a non-set is nil, not a throw and not a union with one
    # element: the interpreter assigns n = -1 whenever the pair is not two sets.
    ("mir_add_set_and_a_scalar_is_nil",
     'a = 1, 2\n'
     'say a + 5\n'
     'say a + nil\n'),
    # An INTERVAL set has no members in the interpreter (iCount/count/comps are
    # all zero for kind 2), so a union of two intervals is the EMPTY set -- a
    # measured property of set_union, not an error path.  A route that refused
    # the pair, or that unioned the intervals as intervals, would answer
    # something else here.
    ("mir_add_intervals_is_an_empty_set",
     'p = (0, 3)\n'
     'q = (2, 5)\n'
     'say p + q\n'
     'say len(p + q)\n'),
    # The two FATAL rows.  Their stdout is empty and their rc is 1 on both
    # routes already; what separates "the same answer" from "the same exit code
    # for a different reason" is the sentence, so each demands it on stderr
    # (see STDERR_EXPECT).  This route answered `undefined: type_mismatch`
    # before this step.
    ("mir_add_arrays_is_a_fatal_error",
     'say [1] + [2]\n'),
    ("mir_add_dicts_is_a_fatal_error",
     'd = {"a": 1}\n'
     'say d + d\n'),

    # ---- SETS ON `-` AND `*`: the interpreter answers, this route threw ----
    #
    # Found by tools/aot_mir_fuzz.py in its 2500-program sweep, after the cell
    # that gave `+` its four branches.  `L_SUB` and `L_MUL` (src/vm/vm.c:3445-3500)
    # both test for SETS FIRST -- two sets go through set_diff / set_intersect,
    # one set beside anything else gives n = -1 and L_SUB/L_MUL turn that into
    # NIL -- and only then fall through to the int64/double arms.  This route
    # answered every one of them with vm_throw(count "type_mismatch"), because
    # mv_sub and mv_mul began with the numeric guard.  A wrong answer, not a
    # missing one: the interpreter has an answer for every shape below.
    #
    # DIV AND MOD ARE THE CONTROL, and they are NOT part of this cell: L_DIV
    # has no set arm at all (src/vm/vm.c:3501 falls straight into the numeric
    # path), so `a / b` and `a % b` over sets throw type_mismatch in the
    # interpreter too.  The criterion below keeps them there, so an
    # implementation that answered all four arithmetic operators with set
    # algebra would redden on the control.
    ("mir_sub_sets_is_a_difference",
     'a = 1, 2, 3\n'
     'b = 2, 3\n'
     'say a - b\n'
     'say len(a - b)\n'),

    ("mir_sub_lists_the_leftovers",
     'a = 1, 2, 3\n'
     'b = 1, 3\n'
     'say list(a - b)\n'),

    ("mir_sub_keeps_a_float_out",
     'a = 1, 2\n'
     'b = 1, 1.5\n'
     'say list(a - b)\n'),

    ("mir_mul_sets_is_an_intersection",
     'a = 1, 2, 3\n'
     'b = 2, 3, 4\n'
     'say a * b\n'
     'say len(a * b)\n'
     'say list(a * b)\n'),

    ("mir_mul_floats",
     'a = 1.5, 2.5\n'
     'b = 2.5, 9\n'
     'say a - b\n'
     'say a * b\n'),

    ("mir_sub_a_set_and_a_scalar_is_nil",
     'a = 1, 2\n'
     'say a - 5\n'
     'say a * 5\n'),

    ("mir_sub_an_interval_is_nil",
     'p = (0, 3)\n'
     'q = (2, 5)\n'
     'say p - q\n'
     'say p * q\n'
     'say len(p * q)\n'),

    ("mir_mul_with_an_interval_keeps_the_members",
     'a = 1, 2\n'
     'p = (0, 5)\n'
     'say a * p\n'
     'say len(a * p)\n'),

    # --- a negative subscript on a WRITE -----------------------------------------
    # The interpreter shelves a write to an array at a negative subscript into an
    # exception: `vm_throw_kind(vm, "index_out_of_range")` in L_INDEX_SET
    # (src/vm/vm.c:3721-3724, read before this cell was written).  A READ at a
    # negative subscript is a different shape: vm_array_get's guard returns nil and
    # no exception exists, so the two are pinned separately here.
    #
    # The answer is a caught exception, not a harbour: the value a[-1] names is not
    # an element of a, and `say a` afterwards must still be the two original elements.
    ("mir_index_set_negative_is_caught",
     'a = [1, 2]\n'
     'try { a[-1] = 9 } catch (e) { say e }\n'
     'say "after"\n'
     'say a\n',
     ),

    # Outside a try the same write is the fatal kind: nobody carries it, the route
    # stops with the interpreter's own message and its rc=1.  This criterion exists
    # so the fix above cannot be a swallow.
    ("mir_index_set_negative_uncaught",
     'a = [1, 2]\n'
     'a[-1] = 9\n'
     'say "after"\n',
     ),

    # Nested: the arms are per container, so a[0] has to be the same shape one level
    # down.  Before this cell the MIR route fsync'd the outer program out of existence
    # here (rc=1) while the interpreter printed the caught name and continued.
    # --- a constant subscript past a literal ---------------------------------
    # THE REFUSAL THESE THREE REPLACE (2026-11-11, §10.151): the route used to
    # refuse `say a[5]` on a two element literal at translate time
    # (why=index-out-of-range-constant).  The interpreter answers `nil`
    # (src/vm/vm.c:1002), and mv_arr_get answers nil the same way; the refusal
    # was a route policy, not a route limit.  Worse, the policy was unsound
    # where it mattered: the remembered length did not follow the growth that
    # `a[5] = 9` performs (src/vm/vm.c:3721-3724), so the route refused the
    # program below whose answer is 9 -- a non-nil answer, refused.  These are
    # the three shapes that refusal hid: a plain read past the literal, a read
    # of a value a write put past it, and a read far past it.
    ("mir_read_past_a_literal_is_nil",
     'a = [1, 2, 3]\n'
     'say a[3]\n'
     'say "after"\n'),
    ("mir_read_a_value_written_past_the_literal",
     'a = [1, 2]\n'
     'a[5] = 9\n'
     'say a[5]\n'
     'say len(a)\n'),
    # --- GROWTH: the answer a retired refusal used to hide ---------------------
    # A write past the end GROWS the array and fills the gap with nil
    # (src/vm/vm.c:3721-3724).  §10.151 retired the translate-time refusal that
    # called these shapes impossible; these six pin what the runtime answers,
    # because "we no longer refuse it" is not the same claim as "we answer what
    # the interpreter answers".  Each was measured by hand first
    # (`inimerse --no-mods` against the generated C) and written afterwards.
    ("mir_grow_an_array_past_its_literal",
     'a = [1, 2]\n'
     'a[5] = 9\n'
     'say a\n'
     'say len(a)\n'
     'say a[5]\n'
     'say a[3]\n'),
    ("mir_grow_through_an_alias",
     'a = [1]\n'
     'b = a\n'
     'a[3] = 7\n'
     'say b\n'
     'say b[3]\n'
     'say len(b)\n'),
    ("mir_grow_inside_a_function",
     'func grow(a) {\n'
     '    a[4] = 7\n'
     '    return a\n'
     '}\n'
     'x = [1]\n'
     'y = grow(x)\n'
     'say x\n'
     'say y\n'
     'say len(x)\n'),
    ("mir_grow_a_nested_element",
     'n = [[1]]\n'
     'n[3] = 5\n'
     'say n\n'
     'say n[3]\n'),
    ("mir_grow_then_compare",
     'a = [1]\n'
     'b = [2]\n'
     'a[3] = 1\n'
     'say a == b\n'
     'say len(a) == len(b)\n'),
    # --- min()/max() OVER A SET ------------------------------------------------
    # `min` is a SET REDUCTION, not a two-operand function: the interpreter's
    # set_minmax fills the destination with nil first and returns unless the
    # operand is a set, so `min(1, 2)` answers nil while `x = 1, 2` then
    # `min(x)` answers 1.  Until this cell the route ABORTED on every one of
    # these (rc=-6, empty stdout) because its arm only knew the scalar tags and
    # its note argued that no set could reach it -- true when it was written,
    # false since OP_NEW_SET (§10.125) and OP_SET_INTERVAL (§10.140) were
    # lowered.  Each expected line below was measured on the interpreter first.
    ("mir_set_min_max_ints",
     'x = 1, 2, 3\n'
     'say min(x)\n'
     'say max(x)\n'),
    ("mir_set_min_max_floats",
     'x = 2.5, 1.5\n'
     'say min(x)\n'
     'say max(x)\n'),
    ("mir_set_min_max_strings",
     'x = "b", "a"\n'
     'say min(x)\n'
     'say max(x)\n'),
    ("mir_set_min_max_mixed_is_nil",
     'x = 1, "a"\n'
     'say min(x)\n'
     'say max(x)\n'),
    ("mir_set_min_max_of_an_interval",
     'p = (0, 3)\n'
     'say min(p)\n'),
    ("mir_set_min_max_narrows_i64",
     'x = 5000000000, 1\n'
     'say min(x)\n'
     'say max(x)\n'),
    # A function operand is not a set either: set_minmax's first statement
    # answers nil, while this arm used to abort on the MV_FUN tag (the old note
    # listed it as "no answer on this route").  Measured: nil / nil.
    ("mir_set_min_max_of_a_function",
     'f = () -> 42\n'
     'say min(f)\n'
     'say max(f)\n'),
    ("mir_set_min_max_mixed_numbers",
     'x = 1, 2, 2.5\n'
     'say min(x)\n'
     'say max(x)\n'),
    ("mir_grow_with_an_explicit_nil",
     'a = [1, 2]\n'
     'a[4] = nil\n'
     'say a\n'
     'say len(a)\n'),
    ("mir_read_far_past_a_single_element_literal",
     'a = [1]\n'
     'say a[40]\n'
     'say "after"\n'),
    ("mir_index_set_negative_in_a_nested_array",
     'n = [[1, 2], [3]]\n'
     'try { n[0][-1] = 9 } catch (e) { say e }\n'
     'say n[0]\n',
     ),

    # Controls: a negative READ is nil on both routes (nothing to catch), and a dict
    # takes -1 as an ordinary key (the interpreter answers nil for it, which is a
    # separate defect, not this cell's).
    ("mir_index_read_negative_is_nil",
     'a = [1, 2]\n'
     'say a[-1]\n'
     'try { say a[-1] } catch (e) { say e }\n'
     'say "after"\n',
     ),
    ("mir_dict_negative_key_is_nil",
     'd = {}\n'
     'd[-1] = 9\n'
     'say d[-1]\n',
     ),

    ("mir_mod_a_set_still_stops",
     'a = 1, 2, 3\n'
     'b = 2, 3\n'
     'say a % b\n'),

    # ---- ARITHMETIC ON A NON-NUMBER, IN A PROGRAM THAT CARRIES A TRY
    #
    # These eight are about a bug that only exists INSIDE a program with a try
    # pair: with a try in the file the emitter routes OP_ADD/SUB/MUL/DIV/MOD/NEG
    # through mv_try_*, and mv_try_sub/mul/div/mod/neg called mv_as_nv on both
    # operands BEFORE asking whether they were numbers.  mv_as_nv on a string,
    # array, dict, nil or set is an abort(), so every one of these programs
    # died with rc=134 and an empty stdout -- measured on aef11cc -- while the
    # interpreter answered a catchable `type_mismatch`, or (for a set pair) a
    # value.  Outside a try the same program was fine, because mv_sub's own
    # guard runs first; that asymmetry is why a program that merely CONTAINS a
    # try is enough to expose it, and why case 7 below puts the error outside
    # the try block.
    #
    # Case 8 is the control: a genuine stop (integer division by zero) must
    # STILL be a throw with its own name.  A patch that made every wrapper
    # delegate to mv_div would pass cases 1-7 and break this one.
    ("mir_try_arith_sub_on_a_string",
     'try { say "a" - 1 } catch (e) { say e }\n'
     'say "after"\n'),

    ("mir_try_arith_mul_on_a_string",
     'try { say "a" * 2 } catch (e) { say e }\n'
     'say "after"\n'),

    ("mir_try_arith_div_on_a_string",
     'try { say "a" / 2 } catch (e) { say e }\n'
     'say "after"\n'),

    ("mir_try_arith_neg_on_a_string",
     'try { say -"a" } catch (e) { say e }\n'
     'say "after"\n'),

    ("mir_try_arith_on_a_set_and_a_scalar",
     'a = 1, 2\n'
     'try { say a - 5 } catch (e) { say e }\n'
     'say "after"\n'),

    ("mir_try_arith_on_two_sets",
     'a = 1, 2, 3\n'
     'b = 2, 3\n'
     'try { say a - b } catch (e) { say e }\n'
     'say "after"\n'),

    ("mir_arith_type_error_outside_a_try",
     'try { say "t" } catch (e) { say e }\n'
     'say "a" - 1\n'),

    ("mir_try_div_zero_is_still_caught",
     'try { say 1 / 0 } catch (e) { say e }\n'
     'say "after"\n'),


    # -------------------------------------------------- dict key IDENTITY
    #
    # A boolean and the integer it equals are ONE value for `==` and TWO keys
    # for a dict.  Both halves of that sentence are measured, and the second
    # one is not a simplification of the first: the interpreter's dict answers
    # by bucket hash THEN val_eq -- dict_hash_find accepts a pair only when the
    # stored hash equals the probe's hash AND val_eq says equal -- and
    # dict_hash_key gives BOOL a row of its own (0x85ebca6b for true,
    # 0xc2b2ae35 for false) that is NOT the INT row, while val_eq's numeric
    # tail (src/vm/vm.c:425-427) does put a bool in the same class as an int.
    # The hash SEPARATES a pair val_eq would join, so no reading of val_eq
    # alone can produce the interpreter's answer here.  That is why the repair
    # is a hash gate and not a tag gate.
    #
    # MEASURED IN THE INTERPRETER FIRST (build/inimerse --no-mods), the route's
    # answer measured second.  `d[0]` / `d[1]` are POSITIONAL reads
    # (src/vm/vm.c:3683-3696 returns the KEY at pair i for an INT subscript),
    # so they read out the pair ORDER as well as the pair COUNT:
    #
    #   d[1] = "a"; d[true] = "b"                   len 2   d[0]=1     d[1]=true
    #   d[true] = "b"; d[1] = "a"                   len 2   d[0]=true  d[1]=1
    #   d[false] = "b"; d[0] = "a"                  len 2   d[0]=false d[1]=0
    #   d[true] = "b"; d[1.0] = "f"                 len 2   d[0]=true  d[1]=1
    #   d[1] = "a"; d[1.0] = "f"                    len 1   (the VALUE is "f";
    #                                                        the key slot keeps 1)
    #   d[4294967297] = "a"; d[4294967297.0] = "f"  len 2
    #   d[2] = "a"; d[2.0] = "f"                    len 1
    #   d[-1] = "a"; d[-1.0] = "f"                  len 1
    #   d[1.5] = "a"; d[1.5] = "b"                  len 1
    #   d[0.0] = "a"; d[-0.0] = "b"                 len 1
    #   d[nil] = "n"; d[0] = "z"                    len 2
    #   d["1"] = "s"; d[1] = "i"                    len 2
    #   k = true; d[true]="b"; d[1]="a"; say d[k]   "b"     len 2
    #
    # THE 4294967297 ROW IS THE ONE THAT RULES OUT THE CHEAP REPAIR.  A model
    # that answered "same key iff val_eq and the tags share a class, with
    # {INT,FLOAT} one class and {BOOL} another" gets every other row right and
    # this one WRONG.  dict_hash_key truncates an int key to 32 bits, so
    # 4294967297 hashes exactly like 1 and shares the int bucket; the float
    # 4294967297.0 lies OUTSIDE the [-2^31, 2^31) window whose integral values
    # are re-hashed through the INT row, so it hashes by its IEEE bits -- a
    # different bucket, hence two pairs, though val_eq calls the two values
    # equal.  Only the interpreter's own hash reproduces that, so the route
    # copies it.
    #
    # `==` IS NOT THIS RULE, and a repair that made it so would be a second
    # defect.  The same interpreter prints true for `1 == true`, `0 == false`,
    # `1.0 == true` and `1 == 1.0`, and false for `nil == 0`.
    # mir_boolkey_eq_is_not_key_identity pins all five inside this group, so
    # widening the dict rule into mv_eq reddens it.
    #
    # THE SET SIDE SHARES THE MACHINERY BUT NOT THIS RULE.  set_contains
    # (src/vm/vm.c:2019-2040) lets an INT probe binary-search the sorted int
    # half AND THEN still scan the non-int items, while every other tag looks
    # at the items half only.  A bool is an item-half member, so the rule is
    # asymmetric and the interpreter says so:
    #
    #   s = { x in [1] | x > 0 }     true in s FALSE   1 in s TRUE   1.0 in s FALSE
    #   s = { x in [true] | x > 0 }  1 in s TRUE       true in s TRUE  1.0 in s TRUE
    #
    # mv_set_has already copies exactly that and is green on both, so these two
    # cases are the control that the key repair does NOT reach it: they are
    # written before the dict cases because a repair that widened mv_eq (or
    # pushed mv_key_eq into the set path) would have to redden them.
    ("mir_boolkey_set_true_probe_skips_int_half",
     's = { x in [1] | x > 0 }\n'
     'say true in s\n'
     'say 1 in s\n'
     'say 1.0 in s\n'),
    ("mir_boolkey_set_int_probe_crosses_halves",
     's = { x in [true] | x > 0 }\n'
     'say 1 in s\n'
     'say true in s\n'
     'say 1.0 in s\n'
     'say false in s\n'),
    # The brief's own program, both write orders.  The order matters: `d[0]`
    # and `d[1]` read the pair array by position, so a route that collapsed the
    # two pairs prints one line where the interpreter prints two, and gets the
    # surviving pair's position wrong in the other order.
    ("mir_boolkey_int_vs_bool",
     'd = {}\n'
     'd[1] = "a"\n'
     'd[true] = "b"\n'
     'say len(d)\n'
     'say d[0]\n'
     'say d[1]\n'),
    ("mir_boolkey_bool_vs_int",
     'd = {}\n'
     'd[true] = "b"\n'
     'd[1] = "a"\n'
     'say len(d)\n'
     'say d[0]\n'
     'say d[1]\n'),
    ("mir_boolkey_false_vs_zero",
     'd = {}\n'
     'd[false] = "b"\n'
     'd[0] = "a"\n'
     'say len(d)\n'
     'say d[0]\n'
     'say d[1]\n'),
    ("mir_boolkey_bool_vs_float",
     'd = {}\n'
     'd[true] = "b"\n'
     'd[1.0] = "f"\n'
     'say len(d)\n'
     'say d[0]\n'
     'say d[1]\n'),
    # THE POSITIVE CONTROL OF THE HASH GATE: an int and an integral float are
    # one key in the interpreter, because dict_hash_key re-hashes an integral
    # float inside the int window through the INT row.  A repair that separated
    # every cross-tag pair passes the four cases above and reddens these two.
    ("mir_boolkey_int_float_collapses",
     'd = {}\n'
     'd[1] = "a"\n'
     'd[1.0] = "f"\n'
     'say len(d)\n'
     'say d[0]\n'),
    ("mir_boolkey_int_float_collapses_reverse",
     'd = {}\n'
     'd[1.0] = "f"\n'
     'd[1] = "a"\n'
     'say len(d)\n'
     'say d[0]\n'),
    # The row that a tag-class model cannot answer, explained above: the int's
    # hash is truncated to 32 bits and collides with 1's, the float's is its
    # IEEE bits, so val_eq's TRUE does not make them one key.
    ("mir_boolkey_int_float_hash_truncates",
     'd = {}\n'
     'd[4294967297] = "a"\n'
     'd[4294967297.0] = "f"\n'
     'say len(d)\n'),
    # Two equal floats, and 0.0 against -0.0: both one key in the interpreter
    # (val_eq's float row is `a->fval == b->fval`, and 0.0 == -0.0; the hash
    # re-hashes -0.0 through the same int window as 0.0, giving both the int 0
    # bucket).  A hash gate that compared raw double bits answers two keys on
    # the second one.
    ("mir_boolkey_two_equal_floats",
     'd = {}\n'
     'd[1.5] = "a"\n'
     'd[1.5] = "b"\n'
     'say len(d)\n'),
    ("mir_boolkey_zero_vs_negative_zero",
     'd = {}\n'
     'd[0.0] = "a"\n'
     'd[-0.0] = "b"\n'
     'say len(d)\n'),
    # The two non-numeric rows the hash gate must not disturb: nil hashes to
    # 0x27d4eb2f and the string "1" to djb2's 177622, neither of which is the
    # int 1's bucket (0x9e3779b1), so both pairs are already two keys and stay
    # two.  They guard the new gate against JOINING pairs the old comparison
    # separated.
    ("mir_boolkey_nil_vs_zero",
     'd = {}\n'
     'd[nil] = "n"\n'
     'd[0] = "z"\n'
     'say len(d)\n'),
    ("mir_boolkey_str_vs_int",
     'd = {}\n'
     'd["1"] = "s"\n'
     'd[1] = "i"\n'
     'say len(d)\n'),
    # The KEY branch, not the position branch: the read that has to walk the
    # pair array and compare keys is the non-INT subscript, and the interpreter
    # finds the bool pair there and answers "b".  A route that collapsed the
    # two writes answers "a" -- which is what the merge base does.
    ("mir_boolkey_register_read",
     'd = {}\n'
     'd[true] = "b"\n'
     'd[1] = "a"\n'
     'k = true\n'
     'say d[k]\n'
     'say len(d)\n'),
    ("mir_boolkey_eq_is_not_key_identity",
     'say 1 == true\n'
     'say 0 == false\n'
     'say 1.0 == true\n'
     'say 1 == 1.0\n'
     'say nil == 0\n'),
    # ---- WRONG ARITY AT A USER-FUNCTION CALL
    #
    # The emitted callee signature is FIXED by the callee's own declaration
    # (`static MirVal fn_h(MirVal a0)`), so a call site that pushes a different
    # number of arguments than that callee declares used to hand C a call the
    # callee's own prototype rejects:
    #
    #     func h(n) { return n }
    #     say h(1, 2)
    #
    # `cc` answered `error: too many arguments to function 'fn_h'; expected 1,
    # have 2` -- after translate had already exited 0.  That is a failure at the
    # C COMPILER, which is exactly the class this route exists to eliminate: the
    # interpreter answers the program and the route cannot even build it.  The
    # other direction is the same wall: `say h()` gave `too few arguments ...
    # expected 1, have 0`.
    #
    # THE INTERPRETER'S RULE, MEASURED FIRST (`build/inimerse --no-mods run`),
    # because the interpreter is the only oracle this route has.  The whole rule
    # is three lines of src/vm/vm.c's L_CALL_FUNC arm, and BOTH call paths --
    # OP_CALL_FUNC by name and OP_CALL_VALUE through a value -- run that ONE arm
    # (`int fidx = (ins.op == OP_CALL_VALUE) ? call_fidx_override : ins.r1`):
    #
    #     int param_slots = root->func_argc[fidx] > argc ? root->func_argc[fidx] : argc;
    #     for (int i = 0; i < param_slots; i++) value_set(&FR[i + 1], VAL_NIL, ...);
    #     for (int i = argc - 1; i >= 0; i--) { value_move(&FR[i + 1], &t->stack[t->sp]); t->sp--; }
    #
    # The readings that follow from it, each observed rather than inferred:
    #
    #   1. EXTRA arguments are EVALUATED -- every argument expression's side
    #      effect happens, in argument order, before the callee's body runs --
    #      and then DISCARDED as arguments.  Measured with an extra argument
    #      that is itself a printing call: the printer runs first, then the
    #      body, and the callee's parameter is the FIRST argument.
    #   2. MISSING parameters read NIL.  The nil-fill above is why: every slot
    #      up to max(declared, argc) is set to nil BEFORE the pushed arguments
    #      are moved in, so a slot no argument reached keeps its nil.
    #   3. The CALLEE therefore sees argument k in register k+1 for k < argc,
    #      and nil for declared > k >= argc.  Nothing else.
    #   4. A FUNCTION VALUE obeys the SAME rule, because it is the same arm:
    #      `f = () -> 42` called as `f(1, 2)` prints 42, and `f = (a, b) -> a`
    #      called as `f(7)` / `f()` prints 7 then nil.  Measured, not assumed
    #      from the by-name path -- the two opcodes are pre-scanned differently,
    #      so the interpreter had to be asked separately.
    #
    # NOT REPRODUCED, and this is the one place the two differ: an extra
    # argument also lands in the callee's OWN frame slot for its position (the
    # move loop writes FR[k+1] for every k < argc, before anything is discarded
    # as an argument), so a callee that READS a local register before writing it
    # can see that extra argument -- and a later call at the same frame depth
    # keeps seeing it, because the interpreter nil-fills only up to
    # max(declared, argc).  Measured:
    #
    #     func g(a) { if false { y = 1 }  say y }
    #     g(1, 99, 88)     -> 88      (the third ARGUMENT, in local slot y)
    #     g(1)             -> 88      (still there: that depth was never re-filled)
    #
    # Both numbers are the interpreter's, and this route answers 0 for both --
    # its frame is initialised to 0 on every entry.  Reproducing it needs the
    # callee's registers writable from the call site, i.e. a signature that
    # carries the argument vector, and it is a property of the interpreter's
    # stale register file rather than of its calling convention.  It is named in
    # docs/STATUS.md as a decision, with this transcript, instead of being
    # half-reproduced here.
    #
    # READINGS.  Every program below was run through the interpreter first and
    # its stdout is quoted in that case's comment.  The route's reading before
    # this step was `cc failed` for the by-name cases (the C never compiled) and
    # a runtime abort, rc 134, for the value cases (mv_callv's emitted
    # `if (nargs != <argc>)` guard stopped the program after cc had accepted it).

    # EXTRA ARGUMENT, BY NAME.  The callee has one parameter and returns it; the
    # call pushes two.  Interpreter: `1`.  The first line of output is the
    # callee's own `say "n="`, so this also pins that the callee really RAN and
    # that the parameter it ran with is the FIRST argument, not the last.
    ("mir_arity_extra_args_by_name",
     'func h(n) {\n'
     '    say "n="\n'
     '    say n\n'
     '    return n\n'
     '}\n'
     'say h(1, 2)\n'),

    # MISSING ARGUMENT, BY NAME.  Two parameters, one argument.  Interpreter:
    # `7` then `nil`.  A route that padded with 0 would print `0` on the second
    # line and nothing else would change, which is why the nil is the reading.
    ("mir_arity_missing_args_by_name",
     'func g(a, b) {\n'
     '    say a\n'
     '    say b\n'
     '}\n'
     'g(7)\n'),

    # ZERO ARGUMENTS, BY NAME, to a two-parameter function.  Interpreter: `nil`
    # then `nil`.  This is the case with no argument-stack traffic at all, so it
    # also stops a fix that only pads when argc > 0.
    ("mir_arity_zero_args_by_name",
     'func g(a, b) {\n'
     '    say a\n'
     '    say b\n'
     '}\n'
     'g()\n'),

    # THE EXTRA ARGUMENT IS EVALUATED.  `mark()` is the extra argument and it
    # prints; the callee's body prints too.  Interpreter: `mark`, `body`, `1` --
    # so the argument expression ran BEFORE the body, and its value was thrown
    # away rather than becoming the parameter.  A fix that dropped the extra
    # argument at TRANSLATE time (or that only emitted the first `declared`
    # argument pushes) would lose the `mark` line, and this is the only criterion
    # whose red would be a missing side effect rather than a missing value.
    ("mir_arity_extra_arg_is_evaluated",
     'func mark() {\n'
     '    say "mark"\n'
     '    return 9\n'
     '}\n'
     'func h(n) {\n'
     '    say "body"\n'
     '    say n\n'
     '}\n'
     'h(1, mark())\n'),

    # NESTED AND MIXED.  Three inner calls -- one with an extra argument, one
    # exact, one with two extra -- feed one exact call.  Interpreter: `8`
    # (1 + 3 + 4).  The three inner shapes are the three arities in one program,
    # so a fix that only handled the outermost call site, or that padded in one
    # arm of the emitter, reddens here and passes the four cases above.
    ("mir_arity_nested_mixed",
     'func one(a) {\n'
     '    return a\n'
     '}\n'
     'func three(a, b, c) {\n'
     '    return a + b + c\n'
     '}\n'
     'say three(one(1, 2), one(3), one(4, 5, 6))\n'),

    # FUNCTION VALUE, EXTRA ARGUMENTS.  `f` is a closure of declared arity 0
    # called with two.  Interpreter: `42`.  This is OP_CALL_VALUE, a different
    # opcode and a different emitted dispatch (mv_callv) from the cases above,
    # and at the start of this step it aborted at RUN time with rc 134 instead
    # of failing to compile -- the same bug one layer later.
    ("mir_arity_value_extra",
     'f = () -> 42\n'
     'say f(1, 2)\n'),

    # FUNCTION VALUE, MISSING ARGUMENTS.  Declared arity 2, called once with one
    # and once with none.  Interpreter: `7` then `nil`.  The `f()` line is the
    # one that must not read the argument stack: OP_CALL_VALUE is emitted with a
    # literal `0, 0` when argc == 0, so a padding arm that formatted `args[0]`
    # unconditionally would dereference NULL here.
    ("mir_arity_value_missing",
     'f = (a, b) -> a\n'
     'say f(7)\n'
     'say f()\n'),

    # FUNCTION VALUE WITH A CAPTURE, EXTRA ARGUMENTS.  The dispatch has to pass
    # the value itself as the hidden leading `self` AND pad the user arguments,
    # so the two adaptations meet in one call.  Interpreter: `13` then `13`
    # (10 + 1 + 2, and the four-argument call discards 3 and 4).
    ("mir_arity_value_capture_extra",
     'mk = (n) -> (a, b) -> n + a + b\n'
     'f = mk(10)\n'
     'say f(1, 2)\n'
     'say f(1, 2, 3, 4)\n'),

    # THROUGH A PARAMETER.  The callee is a function value that arrived as an
    # argument, and the CALLER `apply` is itself over-applied (declared 2, called
    # with 3), so an extra argument is discarded twice on one line.  Interpreter:
    # `6`.  Measured value 6, not 9: the third argument dies at `apply`'s own
    # call site and never reaches `f`.
    ("mir_arity_value_through_param",
     'apply = (f, x) -> f(x)\n'
     'dbl = (n) -> n * 2\n'
     'say apply(dbl, 3, 9)\n'),

    # THE BUILTIN'S ARGUMENT COUNT.  `len` and `list` are the only two builtins
    # this route emits, and until this cell both were accepted ONLY with one
    # argument: the pre-scan refused every other count as `illegal-operand`, so
    # a program the interpreter answers never became C at all -- the failure
    # §10.157 records one layer down at cc, here one layer up at translate.
    #
    # WHAT THE INTERPRETER DOES with a count above one is NOT "count the
    # arguments".  It is `len` / `list` applied to the LAST argument, with the
    # others evaluated and left on the argument stack.  MEASURED before a
    # criterion was written (build/inimerse --no-mods):
    #
    #     say len(2, 1)               -> 1   (a count would answer 2)
    #     say len(12345, 0)           -> 0   (a count would answer 2)
    #     say len("abc", 1)           -> 1   (last is the int 1: len(1) is 1)
    #     say len(1, "abc")           -> 3   (last is the string: its length)
    #     say len([1, 2, 3], [9])     -> 1   (last is the array [9])
    #     say len(1, [1, 2])          -> 2
    #     say len(1, 2, 3)            -> 3
    #     say len(9, 8, [1, 2, 3, 4]) -> 4
    #     say len(mark(), 7)          -> mark, then 7   (every argument runs)
    #     say list(1, [5, 6])         -> [5, 6]
    #     say list([5, 6], 1)         -> nil
    #
    # The mechanism is one line of the runtime: the builtin reads
    # vm_cur_stack(vm)[vm_cur_sp(vm)] -- the stack TOP -- and pops exactly ONE
    # value and pushes exactly one, whatever the instruction's argument count
    # said.  The answer is a function of the last argument because the last
    # argument IS the top; the earlier ones are never read, they STAY on that
    # stack instead of being discarded.
    #
    # DECIDED: this route AGREES with that answer for a count of two or more,
    # and it does so without modelling the leftover pushes, because the leftover
    # has exactly ONE reader in the interpreter: a builtin called with ZERO
    # arguments, which reads the top (the leftover) or -- on an empty stack --
    # the destination register's stale value.  That shape is refused BY NAME on
    # the reverse side (why=builtin-stale-stack, in MIR_ONLY_REFUSE below), and
    # the pre-scan is STATIC over every container, so no program this route
    # accepts can contain such a call and no accepted program can observe what
    # the extra arguments left behind.  So answering 2 for `len(1, 2)` is NOT
    # copying a defect: it is the interpreter's own value, and the defect (a
    # builtin that ignores its argument count and leaks its pushes) is confined
    # to a shape this route refuses.
    #
    # RED READING of every case below on the unmodified route: refused at
    # translate, `why=illegal-operand` (the detail named the count and said this
    # route emits len and list only with one argument), so the program never
    # becomes C.  The LAST case is the CONTROL: it is green on arrival, and it
    # is here so that a fix which moved the one-argument arm reddens next to the
    # multi-argument ones it was meant to leave alone.

    # LAST, NOT COUNT, NOT FIRST.  Every line distinguishes the three candidate
    # rules: `len` of the last argument (1, 0, 1, 3, 1, 2), the number of
    # arguments (all 2), and the first argument (2, 12345, 3, 1, 3, 1).  No
    # two of the three agree on any line.  The last two lines are the
    # discriminator pair independently read on the main tree: with `len` of the
    # last argument they are 5 and 2, while a count would answer 2 for both.
    ("mir_builtin_argc_len_last",
     'say len(2, 1)\n'
     'say len(12345, 0)\n'
     'say len("abc", 1)\n'
     'say len(1, "abc")\n'
     'say len([1, 2, 3], [9])\n'
     'say len(1, [1, 2])\n'
     'say len("ab", 5)\n'
     'say len(5, "ab")\n'),

    # THREE AND MORE.  The count is not a special case of two: three and four
    # arguments answer the same way, and the third line pushes five.  Interpreter:
    # `3`, `4`, `5`.
    ("mir_builtin_argc_len_many",
     'say len(1, 2, 3)\n'
     'say len(9, 8, [1, 2, 3, 4])\n'
     'say len(1, 2, 3, 4, 5)\n'),

    # EVERY ARGUMENT IS EVALUATED, not just the one that answers.  `mark` prints
    # and returns 5.  Interpreter: `m`, `7`, `m`, `5` -- so the earlier argument
    # ran in both directions (it is the first argument on line 1 and the last on
    # line 2), and a fix that emitted only the last argument's push would lose
    # the first `m` while a fix that answered the first argument would print 5
    # for the second line.
    ("mir_builtin_argc_len_args_evaluated",
     'func mark() {\n'
     '    say "m"\n'
     '    return 5\n'
     '}\n'
     'say len(mark(), 7)\n'
     'say len(1, mark())\n'),

    # `list` OBEYS THE SAME RULE, and its answers are a container and nil rather
    # than a number, so the second and fourth lines pin that nil is a real
    # answer.  The third line is the one an "answer the first argument" fix gets
    # wrong: `list([5, 6], 1)` is nil, not the array.  Interpreter:
    # `nil`, `[5, 6]`, `nil`, `nil`.
    ("mir_builtin_argc_list_last",
     'say list(1, 2)\n'
     'say list(1, [5, 6])\n'
     'say list([5, 6], 1)\n'
     'say list(1, 2, 3)\n'),

    # NESTED, IN AN EXPRESSION, AND IN A LOOP.  The last argument of the outer
    # call is ITSELF a multi-argument call, `x` adds two of them, and the loop
    # calls one four times -- which is what the emitted pop has to keep balanced
    # (a route that read the last argument but popped only one value per call
    # would drift `asp` upward here).  Interpreter: `9`, `7`, `2`.
    ("mir_builtin_argc_nested_expression_loop",
     'say len(len(1, 2), len([1, 2, 3], 9))\n'
     'x = len(1, 2) + len(3, 4, 5)\n'
     'say x\n'
     'n = 0\n'
     'while n < 3 {\n'
     '  n = n + 1\n'
     '}\n'
     'say len(n, 2)\n'),

    # THE CONTROL.  One argument for both builtins, over four containers.  Green
    # on arrival, and it is the reading that must not move: `3`, `4`, `[1, 2]`,
    # `1`.  `list` of an array is the SAME value (`mv_list` returns it as-is),
    # and `len(1)` is 1 -- both are the one-argument arm, not this cell's shape.
    ("mir_builtin_argc_single_arg_control",
     'say len([1, 2, 3])\n'
     'say len("abcd")\n'
     'say list([1, 2])\n'
     'say len(1)\n'),
    # ==================================================================
    # §10.160 (2026-11-11, base 9dafc43).  THREE PLACES WHERE BOTH SIDES
    # ARE WRONG THE SAME WAY.
    #
    # THESE WERE GREEN ON FIRST WRITE, and that is the honest state: the
    # sweep that produced this cell found the interpreter and the route
    # already agreeing in these places, so there was no fix to drive a red
    # first.  They are not criteria of a repair; they are the record that the
    # agreement exists and the alarm for the day a later change breaks it.
    # The three are DEFECTS in the interpreter that the route faithfully
    # copies (try/catch/finally was lowered in §10.137); docs/STATUS.md §10.160
    # carries the evidence, and for each one the observation that would RETIRE
    # it.
    #
    # Why a red here is a real question and not noise: the agreement is in the
    # OUTPUT, not in the opcode set, so no cover instrument can see it.  The
    # only shape that catches a silent divergence is a program whose stdout
    # BYTES and exit code are pinned on BOTH sides.  A red therefore means the
    # two executables now disagree, and the first thing to establish is WHICH
    # side changed.  If the interpreter gained a fix, the route must learn the
    # same fix, and this criterion is retired with both readings quoted in the
    # message -- not quietly adjusted to whichever side was read last.
    #
    # 1. FINALLY RUNS ONLY UNDER A NON-EMPTY CATCH BODY.  `try { say "body" }
    #    finally { say "fin" }` prints `body` then `after`, NOT `fin`.  The
    #    compiler counts the catch body's STATEMENTS to decide whether a
    #    handler exists (`catch { }` and `catch (e) { }` both count zero), and
    #    with no handler the normal-path jump is patched PAST the finally body
    #    and its rethrow.  A non-empty catch body makes the jump land on the
    #    finally instead.
    #
    # 2. A THROW INSIDE A FINALLY.  When the finally RUNS, its throw PROPAGATES
    #    (measured: rc 1, `[exception] uncaught: fin-throw`); when the finally
    #    does NOT run -- shape 1 -- its throw is never reached and the process
    #    exits 0 with clean stderr.  The two halves are separate criteria on
    #    purpose: they can look like one swallowed error and are two different
    #    events, and a reader who pins the skipped shape as `the throw is
    #    swallowed` will be wrong about every finally that runs.
    #
    # 3. `for x in s` OVER A SET READS nil FOR EVERY ELEMENT.  The
    #    for-over-container lowering is a counted `while` reading `s[i]`, and
    #    the reader has no set arm: a set falls into the final catch-all that
    #    answers nil, while `len(s)` answers the count, so a three-element set
    #    prints three nils.  An array prints its elements; a dict prints its
    #    KEYS, because its integer subscript is the position-to-key read; an
    #    interval set is iterated ZERO times, because its len is 0.  The last
    #    one is only an agreement for the `[0, 3)` spelling: the `R[0~3)`
    #    spelling DISAGREES between the sides (interpreter `set(0)`/len 0/
    #    min 0/max 3, route `set(1)`/len 1/min and max answering the set
    #    itself), so it is reported as a different cell and is NOT folded in
    #    here -- an agreement that does not exist cannot be locked.

    # NORMAL PATH, NO HANDLER: the finally is SKIPPED.  Interpreter and route
    # both print `body` then `after`, rc 0.  A fix that ran finally blocks
    # unconditionally would print `body`, `fin`, `after` here and redden.
    ("mir_finally_only_skipped",
     'try {\n'
     '    say "body"\n'
     '} finally {\n'
     '    say "fin"\n'
     '}\n'
     'say "after"\n'),

    # AN EMPTY CATCH BODY IS NOT A HANDLER.  The same program with `catch { }`
    # added; its body contributes zero statements, so the finally is skipped
    # exactly as above.  Interpreter: `body\nafter`, rc 0.  Route: identical.
    ("mir_finally_empty_catch_skipped",
     'try {\n'
     '    say "body"\n'
     '} catch {\n'
     '} finally {\n'
     '    say "fin"\n'
     '}\n'
     'say "after"\n'),

    # A CATCH VARIABLE DOES NOT MAKE IT A HANDLER.  `catch (e) { }` binds a
    # name to an empty body; the statement count is still zero, so the finally
    # is still skipped.  Interpreter: `body\nafter`, rc 0.  Route: identical.
    # This is the question a reader asks after the previous case, and the
    # answer is that the variable is not the trigger -- the BODY is.
    ("mir_finally_empty_catch_var_skipped",
     'try {\n'
     '    say "body"\n'
     '} catch (e) {\n'
     '} finally {\n'
     '    say "fin"\n'
     '}\n'
     'say "after"\n'),

    # NON-EMPTY CATCH, NOTHING THROWN: the finally RUNS.  `catch { say
    # "caught" }` contributes a statement, the handler exists, and the normal
    # path falls onto the finally.  Interpreter: `body`, `fin`, `after`, rc 0.
    # The `caught` line is absent because nothing threw; pairing this with the
    # empty-catch cases is the point -- one statement flips the lowering.
    ("mir_finally_after_catch_runs",
     'try {\n'
     '    say "body"\n'
     '} catch {\n'
     '    say "caught"\n'
     '} finally {\n'
     '    say "fin"\n'
     '}\n'
     'say "after"\n'),

    # THE SKIPPED FINALLY'S THROW IS NEVER REACHED.  Quirk 1 and quirk 2 in
    # one program: no handler, so the finally -- and the throw inside it --
    # are off the path.  Interpreter and route both print `body` then
    # `after`, exit 0, and print NOTHING on stderr.  This is the one shape in
    # which the error `disappears`, and it disappears because the block never
    # ran; the two cases after it are the same throw in blocks that DO run.
    ("mir_finally_throw_in_skipped_block",
     'try {\n'
     '    say "body"\n'
     '} finally {\n'
     '    throw "fin-throw"\n'
     '}\n'
     'say "after"\n'),

    # THE FINALLY RUNS AFTER A CAUGHT ERROR, AND ITS THROW PROPAGATES.
    # Interpreter: stdout `caught`, rc 1, stderr `[exception] uncaught:
    # fin-throw`.  The route prints the same stdout, the same sentence (it has
    # no `at ip=` ledger line) and exits 1.  `boom` must NOT appear: the
    # original error did not resurface behind the new one.  This is the case
    # that retires `a throw in a finally is swallowed`.
    ("mir_finally_throw_after_catch_propagates",
     'try {\n'
     '    throw "boom"\n'
     '} catch {\n'
     '    say "caught"\n'
     '} finally {\n'
     '    throw "fin-throw"\n'
     '}\n'),

    # HANDLER-LESS ERROR PATH: THE FINALLY RUNS, THEN THE ORIGINAL RETHROWS.
    # `try { throw "boom" } finally { say "fin" }` prints `fin`, exits 1 and
    # reports `boom`, on both sides.  The finally is on the error path here --
    # the opposite of the first case -- because a throw jumps to its own frame
    # first.  Three claims in one reading: `fin` DID print, the exit code is 1,
    # and the sentence names `boom` rather than the cleanup.
    ("mir_finally_runs_before_try_error",
     'try {\n'
     '    throw "boom"\n'
     '} finally {\n'
     '    say "fin"\n'
     '}\n'),

    # A THREE-ELEMENT SET ITERATES AS THREE NILS.  `s = 1, 2, 3` is a set
    # literal, not an array.  Interpreter: `nil`, `nil`, `nil`, `end`, rc 0;
    # route: identical bytes and rc.
    ("mir_set_three_elements_three_nils",
     's = 1, 2, 3\n'
     'for x in s {\n'
     '    say x\n'
     '}\n'
     'say "end"\n'),

    # ... AND A TWO-ELEMENT SET IS TWO NILS.  Measured `nil`, `nil`, `end`.  The
    # reading pins that the nil count tracks len(s) -- the counted `while`'s
    # bound -- rather than a number baked into the literal's slot count.
    ("mir_set_two_elements_two_nils",
     's = 1, 2\n'
     'for x in s {\n'
     '    say x\n'
     '}\n'
     'say "end"\n'),

    # THE SAME LOOP OVER AN ARRAY ANSWERS THE ELEMENTS.  One program, both
    # containers: `1`, `2`, `3` then `nil`, `nil`, `nil`, then `end`, rc 0.
    # The contrast is the reading: it is not that `for` over any container
    # yields nil, it is that the reader has no set arm while it does have an
    # array arm.
    ("mir_array_then_set_iteration",
     'a = [1, 2, 3]\n'
     'for x in a {\n'
     '    say x\n'
     '}\n'
     's = 1, 2, 3\n'
     'for x in s {\n'
     '    say x\n'
     '}\n'
     'say "end"\n'),

    # A DICT ITERATES ITS KEYS.  `for k in d` reads `d[i]`, and an integer
    # subscript on a dict is the position-to-KEY read, so the loop answers the
    # keys `a`, `b` -- not the values 1, 2.  Interpreter and route: `a`, `b`,
    # `end`, rc 0.
    ("mir_dict_iteration_keys",
     'd = {"a": 1, "b": 2}\n'
     'for k in d {\n'
     '    say k\n'
     '}\n'
     'say "end"\n'),

    # AN INTERVAL SET IS ITERATED ZERO TIMES, because its len is 0.  `[0, 3)`
    # prints `set(R interval)`, len 0, and the loop body never runs: reading
    # `set(R interval)`, `0`, `end`, rc 0, on both sides.  The bound is the
    # `[0, 3)` spelling; the `R[0~3)` spelling DISAGREES (see the block above)
    # and is deliberately absent here.
    ("mir_interval_iteration_yields_nothing",
     'p = [0, 3)\n'
     'say p\n'
     'say len(p)\n'
     'for x in p {\n'
     '    say x\n'
     '}\n'
     'say "end"\n'),

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
    # `mir_container_const_oob` RETIRED (2026-11-11, §10.151): it demanded the
    # refuse-at-translate-time for `say a[3]` on a three element literal, and the
    # interpreter answers `nil` for exactly that program.  The refusal could not
    # survive the one measurement that mattered: `a = [1, 2]` / `a[5] = 9` /
    # `say a[5]` grows the array (src/vm/vm.c:3721-3724) and the stored value IS
    # 9 -- so the length this pass remembered was stale, and the route refused a
    # program with a non-nil answer.  It is now a positive criterion below.
    "mir_thread_decl_never_started": (
        'thread plain_worker {\n    return 1\n}\nsay "defined-never-started"\n',
        "threads"),
    "mir_thread_result_unwrap": (
        'thread plain_worker {\n    return 11\n}\nr = thread_result("plain_worker")\nsay r\n',
        "threads"),
    # A builtin call the route does not emit.  The one with an argument is
    # refused for a DIFFERENT fault than the one without: its argument stack is
    # left unbalanced because nothing consumes OP_PUSH_REG, and the no-argument
    # one is refused as a generic unknown opcode.  Both readings were measured
    # before the reason got a name, and the reason is the same for both.
    #
    # THE PROGRAM CHANGED, AND THIS IS WHY.  It used to be `x = str(1)`, which
    # was `builtin-call` when str() had no arm.  §10.164 emits str(), so that
    # program is now ACCEPTED and this criterion correctly went red: the arm it
    # names moved from the refusal side to the answer side.  It is repointed at
    # `trim`, which is still not emitted, rather than deleted -- the class this
    # criterion exists for (a builtin call with an argument is refused BY NAME)
    # is unchanged, and only the name it names moved.  When trim() lands, this
    # row moves to the next name still on the outside, for the same reason.
    # THE ANCHOR MOVES WITH THE FRONTIER (§10.164 wrote this obligation down when
    # str landed and trim became its target): trim is EMITTED as of §10.172, so
    # this now points at `sqrt`, the next name this route still refuses by name.
    # The class under test is unchanged -- a builtin call with an argument that
    # the pre-scan refuses as builtin-call -- only the name it names moves, and
    # it must keep moving forward, never be deleted or relaxed.
    # This row moves FORWARD as arms land; it exists to keep ONE still-refused
    # name under the refusal criterion.  sqrt landed in §10.176, so it points
    # at match, which is still refused by name (translate: builtin call 'match').
    "mir_builtin_call_with_arg": (
        'x = declared(1)\nsay x\n',
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
    # THE ARGUMENT COUNT THE ROUTE STILL WITHHOLDS, BY NAME.  `len()` and
    # `list()` with NO argument are accepted by the parser and answered by the
    # interpreter, but the answer is NOT a function of the program.  MEASURED
    # (build/inimerse --no-mods):
    #
    #     say len()                 -> 0        (a fresh program: the destination
    #                                            register's initial 0)
    #     x = 6                     then
    #     say len()                 -> 6        (the same register, still 6)
    #     x = "abcdef"              then
    #     say list()                -> abcdef   (the leftover, as the string it was)
    #     say 7                     then
    #     say len()                 -> 7        (the register's most recent int)
    #     say len("abc", len())     -> 3        (sp is NOT empty here: the inner
    #                                            call reads the OUTER call's pushed
    #                                            "abc" off the stack top)
    #
    # The runtime reads the argument stack top and, when the stack is empty, the
    # VM moves the -- unchanged -- destination register into the result.  Both
    # of those are leftovers: two programs that differ only in what was assigned
    # before the call print different values for the same expression.  There is
    # no answer to copy, so this is the one shape where withholding an answer is
    # the honest reading, and it gets a NAME (builtin-stale-stack) instead of
    # staying `illegal-operand`.  It is also what makes the DECISION above sound:
    # the unknown residue of a multi-argument call can only be READ by exactly
    # this zero-argument shape, and the pre-scan is static over every container,
    # so no program this route ACCEPTS contains one.
    "mir_builtin_argc_zero_len": (
        'x = 6\nsay len()\n',
        "builtin-stale-stack"),
    "mir_builtin_argc_zero_list": (
        'x = "abcdef"\nsay list()\n',
        "builtin-stale-stack"),
    # round: the two shapes whose answer is NOT a function of the expression
    # get named here (they are the same stale-stack class as the three rows
    # above), and the one-argument shape is what MIR_ONLY measures.
    "mir_round_zero_arg_is_not_lowered": (
        'x = 6\nsay round()\n',
        "builtin-stale-stack"),
    "mir_round_multi_arg_is_not_lowered": (
        'say round(1, 2)\n',
        "builtin-argc"),
    # sqrt: a zero-argument call answers the destination register's old value
    # (measured `x = 7\nsay sqrt()` prints 7), so it is the same stale-stack
    # class as len().  A two-argument call reads the TOP (measured sqrt(1, 2)
    # is 1.414214 = sqrt(2)), i.e. the ordinary asp-1 rule, and is left accepted.
    "mir_sqrt_zero_arg_is_not_lowered": (
        'x = 7\nsay sqrt()\n',
        "builtin-stale-stack"),
    # match() with one argument answers nothing at all (measured: the
    # interpreter pops and pushes NOTHING when the stack has fewer than two
    # values, so `say match("abc")` prints the argument it was handed).  The
    # refusal is the interpreter's behaviour, not a missing arm: a two-slot
    # builtin cannot be answered from one slot.
    "mir_match_single_argument_is_not_lowered": (
        'say match("abc")\n',
        "builtin-stale-stack"),
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
        rc3, _o3, err3 = run(["cc", "-O2", "-o", exe, out_c, "-lm"], workdir)
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
        rc_c0, _c, ec0 = run(["cc", "-O2", "-o", exe0, c0, "-lm"], workdir)
        rc_c1, _c1, ec1 = run(["cc", "-O2", "-o", exe1, c1, "-lm"], workdir)
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
        rc_cm, _o, ecm = run(["cc", "-O2", "-o", exe_m, mir_c, "-lm"], workdir)
        rc_ca, _o2, eca = run(["cc", "-O2", "-o", exe_a, ast_c, "-lm"], workdir)
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
        rc_c, _o, ec = run(["cc", "-O2", "-o", exe_m, mir_c, "-lm"], workdir)
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
