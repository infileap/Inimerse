/* aot_mir -- lower an IrModule (MIR) to a standalone C translation unit.
 *
 * See aot_mir.h for why this exists and what it refuses.  Implementation notes:
 *
 *  - The generator reads IrInsn and the container's pools, and nothing else of
 *    the program's shape.  No AST is passed in: the switch in emit_one() is
 *    driven by the opcode, so a wrong operand role is a bug here, not a
 *    coincidence of the source text.
 *
 *  - Registers become one flat MirVal v[N] array per container, because the MIR
 *    has no named locals.  MirVal is the interpreter's Value layout by name
 *    (see kMirValBlock below), not a second value runtime.  N is derived from
 *    the operands the instructions actually use plus one slot per parameter,
 *    never from a claim in the module: mir.h says nregs is 0 in P1 and that
 *    deriving it there would be inference.
 *
 *  - Parameters arrive in registers 1..argc.  That is read from the runtime:
 *    src/vm/vm.c's L_CALL_FUNC copies the popped arguments into FR[i + 1], so
 *    the k-th argument is register k+1 and register 0 is unused.
 *
 *  - Every instruction that cannot be emitted is reported and the file is
 *    refused.  The conservation line is computed from two independent counts
 *    (what the module claims, what the emitter appended), so a silently skipped
 *    instruction turns it red instead of staying invisible.
 */
#include "aot_mir.h"
#include "../vm/vm.h"
#include "aot_native.h"     /* aot_native_preamble(): one value runtime, not two */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One backslash escape for the whole file: emitted lines end with this rather
   than carrying a literal \n inside every format string. */
#define NL "\n"

/* The MIR route's register type, appended AFTER the shared preamble
 * (aot_native_preamble()), never inside it: kPreamble keeps one owner and one
 * byte-identical copy in both routes.  Appending rather than splicing is also
 * what keeps tools/aot_mir.test.py's classify() honest -- it takes the two
 * routes' first differing line and requires the shared prefix to still carry
 * all three preamble markers (nv_die_division_by_zero, nv_fmt_double,
 * nv_say); a block inserted between them would break that reading.
 *
 * docs/streams/aot-mir-value.md §4 rules out 甲 (a private second Value
 * runtime) as two runtimes for one language.  This is 丙: a distinct NAME for
 * Value's layout whose every operation delegates to the nv_* helper the
 * preamble already defines, so the runtime stays single and the name carries
 * a dated delete condition instead of an aspiration. */
static const char *const kMirValBlock =
"/* --------------------------------------------------------------- MirVal\n"
" * The value a MIR register holds.  Byte-for-byte the layout of the\n"
" * interpreter's Value (src/vm/vm.h:22-49): `int type`, an anonymous union\n"
" * of `long long ival` / `double fval`, `char *sval`, `void *ptr`; 32 bytes,\n"
" * not NV's 24.  This is a NAME for that layout, not a second value\n"
" * runtime: every operation below hands its operands to the nv_* helper the\n"
" * shared preamble already defined, so division-by-zero, overflow,\n"
" * comparison and printing still have exactly one implementation.\n"
" *\n"
" * Why not keep NV: NV has three tags (aot_native.c:175-177) and Value has\n"
" * ten (src/vm/vm.h:22).  Eight MIR opcodes stay unlowered for want of the\n"
" * payload fields NV does not have (string, nil, array, dict, set), so\n"
" * narrowing registers to NV is what made them unknowable, not what made\n"
" * them hard.  The tag numbers below are Value's, so a register this route\n"
" * writes means to the interpreter what the interpreter means by it.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish): delete this whole block --\n"
" * typedef, tags, asserts and every mv_* adapter -- in the commit where\n"
" * aot_native stops lowering ASTs and starts consuming the IR too, i.e. the\n"
" * commit that completes P3 (docs/streams/aot-mir-value.md §4 丙).  That\n"
" * commit leaves kPreamble with one consumer, so the struct moves into the\n"
" * preamble and this copy has no reason to exist; deleting it earlier, while\n"
" * the second consumer still lowers ASTs, is the fork this block avoids.\n"
" */\n"
"#include <stddef.h>   /* offsetof */\n"
"\n"
"typedef struct {\n"
"    int type;\n"
"    union { long long ival; double fval; };   /* anonymous union, C11 6.7.2.1 */\n"
"    char *sval;\n"
"    void *ptr;\n"
"} MirVal;\n"
"\n"
"#define MV_INT 0   /* = VAL_INT */\n"
"#define MV_FLT 1   /* = VAL_FLOAT */\n"
"#define MV_STR 2   /* = VAL_STRING -- resident (ival 1) or heap-owned (ival 0) */\n"
"#define MV_BOO 3   /* = VAL_BOOL -- 3, not NV's 2; see the tag pair below */\n"
"#define MV_NIL 5   /* = VAL_NIL -- the value NV cannot spell; see MV_NIL below */\n"
"\n"
"/* The placeholder's own test: these are compiled into every generated file,\n"
"   and tools/aot_mir.test.py compiles every generated file with the system\n"
"   cc, so a layout that drifts from Value stops the generated program from\n"
"   building instead of letting it print a confident wrong answer. */\n"
"_Static_assert(sizeof(MirVal) == 32,\n"
"               \"MirVal must be 32 bytes: Value's size (src/vm/vm.h:47)\");\n"
"_Static_assert(offsetof(MirVal, type) == 0, \"MirVal.type at Value.type's offset\");\n"
"_Static_assert(offsetof(MirVal, ival) == 8, \"MirVal.ival at Value.ival's offset\");\n"
"_Static_assert(offsetof(MirVal, fval) == 8, \"MirVal.fval shares ival's union slot, as in Value\");\n"
"_Static_assert(offsetof(MirVal, sval) == 16, \"MirVal.sval at Value.sval's offset\");\n"
"_Static_assert(offsetof(MirVal, ptr) == 24, \"MirVal.ptr at Value.ptr's offset\");\n"
"\n"
"/* --------------------------------------------------- the allocation registry\n"
" *\n"
" * WHY IT EXISTS.  The criterion this route is graded by (tools/aot_mir.test.py's\n"
" * MIR_ONLY cases) compares stdout BYTES and the exit code against the\n"
" * interpreter.  That is the right criterion for an ANSWER, and it is blind to\n"
" * an ALLOCATION: a program that leaks every buffer it ever malloc'd -- or that\n"
" * frees one buffer twice -- prints exactly the right lines and exits 0.  String\n"
" * concatenation is the first opcode in this subset whose answer does not exist\n"
" * without the heap (the answer of `s + t` is la + lb + 1 bytes that no static\n"
" * array holds), so the route needs, BEFORE that answer lands, a reading that\n"
" * says how many blocks it handed out, how many it released, and how many are\n"
" * still outstanding when the program ends.  The generated program prints it\n"
" * itself, so the number is measured by the run and not by the host:\n"
" *\n"
" *     IR-MIR-ALLOCS|allocs=N|freed=N|live=N|bytes=N\n"
" *\n"
" * allocs = blocks handed out; freed = blocks the exit sweep released;\n"
" * live = blocks still outstanding after the sweep, which MUST be 0;\n"
" * bytes = bytes requested over the program's whole life.  `bytes` is the\n"
" * lifetime total on purpose: after the sweep the live BYTE count is always 0\n"
" * by construction, so a live-byte field could never disagree with itself.\n"
" * tools/aot_mir.test.py asserts live == 0 AND an exact (allocs, bytes) pair per\n"
" * case.  The exact pair is the part with teeth: a site that mallocs WITHOUT\n"
" * registering leaves allocs at 0 and would otherwise report a clean 0/0/0 --\n"
" * which is precisely the leak this instrument is supposed to name.\n"
" *\n"
" * WHERE IT PRINTS.  On stderr, and through atexit().  stdout is the criterion:\n"
" * one extra byte there is a false difference against the interpreter on every\n"
" * case in the corpus, so the reading goes to the one channel the criterion does\n"
" * not read.  atexit, not the end of main: OP_HALT in a function that is not\n"
" * main leaves through exit(0) (emit_one, the OP_HALT arm), so a program that\n"
" * always takes that path would otherwise report nothing at all.  abort() skips\n"
" * atexit handlers by design, so a program that stops on a guard prints no\n"
" * reading at all -- that is a run that never reached a return, and the exit\n"
" * code says so.\n"
" *\n"
" * WHY A REGISTRY AND NOT PER-REGISTER OWNERSHIP.  The interpreter marks a string\n"
" * heap-owned with ival == 0 (src/vm/vm.c, the L_ADD string branch:\n"
" * `value_set(&R[ins.r1], VAL_STRING, 0, 0, nb, NULL)`), and it has exactly ONE\n"
" * throat where a register is written, so \"who owns this\" has an answer there.\n"
" * This route has no such throat: every register write in the generated file is a\n"
" * bare assignment `v[i] = expr;` (emit_one has dozens of emit points) and OP_MOV\n"
" * is `v[r1] = v[r2];`, a SHALLOW copy that under per-register ownership would\n"
" * hand one heap pointer to two owners and therefore double-free it.  A registry\n"
" * of allocated blocks, swept once at exit, keeps ownership from flowing between\n"
" * registers AT ALL: no register write can create a second owner, so a double\n"
" * free is structurally impossible rather than guarded against.  It is\n"
" * ACCOUNTING, not ownership: nothing is returned to the registry before the\n"
" * program ends, so a loop that concatenates grows the registry LINEARLY.  That\n"
" * is the price of the narrow step and it is written down in the concat arm's\n"
" * debt note, not hidden here.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  A block malloc'd outside mv_alloc() is invisible to\n"
" * it: no registration, no counter, no reading.  The only defence against that\n"
" * is the harness's exact-count assertion, not this instrument.  Nor can it see\n"
" * a double free -- by the paragraph above there is none to see.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish).  Delete this registry and\n"
" * mv_free_all() in the commit that lands per-register ownership: the mechanical\n"
" * form is `grep -c 'mv_free_all'` over a generated translation unit == 0 and\n"
" * `grep -c 'mv_set('` == the number of register write sites, i.e. a single setter\n"
" * has replaced the bare assignments.  Deleting it earlier removes the only\n"
" * reading that can see an allocation at all. */\n"
"typedef struct MvBlock { void *p; size_t n; struct MvBlock *next; } MvBlock;\n"
"static MvBlock *mv_blocks = 0;\n"
"static long long mv_allocs = 0, mv_freed = 0, mv_live = 0, mv_bytes = 0;\n"
"\n"
"static void *mv_alloc(size_t n) {\n"
"    void *p = malloc(n ? n : 1);\n"
"    if (!p) abort();                  /* OOM is a stop; see the concat note */\n"
"    MvBlock *e = malloc(sizeof *e);\n"
"    if (!e) abort();\n"
"    e->p = p; e->n = n; e->next = mv_blocks;\n"
"    mv_blocks = e;\n"
"    mv_allocs++;\n"
"    mv_bytes += (long long)n;\n"
"    return p;\n"
"}\n"
"\n"
"static void mv_free_all(void) {\n"
"    long long freed = 0;\n"
"    while (mv_blocks) {\n"
"        MvBlock *e = mv_blocks;\n"
"        mv_blocks = e->next;\n"
"        free(e->p);\n"
"        free(e);\n"
"        freed++;\n"
"    }\n"
"    mv_freed = freed;\n"
"    mv_live  = mv_allocs - mv_freed;\n"
"    fprintf(stderr, \"IR-MIR-ALLOCS|allocs=%lld|freed=%lld|live=%lld|bytes=%lld\\n\",\n"
"            mv_allocs, mv_freed, mv_live, mv_bytes);\n"
"}\n"
"\n"
"/* The tag pair is the only place where the two numberings meet: NV_INT 0 /\n"
"   NV_FLT 1 / NV_BOO 2 (aot_native.c:175-177) against MV_INT 0 / MV_FLT 1 /\n"
"   MV_BOO 3.  int and float agree; bool does not, so it is translated here\n"
"   and nowhere else.  A tag with no NV counterpart stops the program rather\n"
"   than answering with NV_BOO's number, which would be a silent wrong\n"
"   answer.  MV_STR is the first of those: NV has no string payload, so\n"
"   mv_as_nv() must abort on a string rather than hand back an integer -- which\n"
"   is why the one place a string is printed is mv_say() below, not the shared\n"
"   exit.  MV_NIL is the second: NV has three tags and none of them is nil,\n"
"   which used to make a nil indistinguishable from the NUMBER 0 the generated\n"
"   globals were initialised with (see MV_NIL below).  Each of the two gets its\n"
"   own arms in exactly those adapters a value can enter, and every adapter\n"
"   WITHOUT such an arm still stops the program instead of answering with\n"
"   NV_BOO's number -- an abort, never a silent wrong answer. */\n"
"static inline int mv_tag_from_nv(int t) {\n"
"    switch (t) { case NV_INT: return MV_INT; case NV_FLT: return MV_FLT;\n"
"                 case NV_BOO: return MV_BOO; default: abort(); }\n"
"}\n"
"static inline int mv_tag_to_nv(int t) {\n"
"    switch (t) { case MV_INT: return NV_INT; case MV_FLT: return NV_FLT;\n"
"                 case MV_BOO: return NV_BOO; default: abort(); }\n"
"}\n"
"\n"
"static inline MirVal mv_from_nv(NV a) {\n"
"    MirVal r;\n"
"    r.type = mv_tag_from_nv(a.t);\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;   /* no emitted opcode has a payload to put here yet */\n"
"    if (a.t == NV_FLT) r.fval = a.f; else r.ival = a.i;\n"
"    return r;\n"
"}\n"
"static inline NV mv_as_nv(MirVal a) {\n"
"    NV r;\n"
"    r.t = mv_tag_to_nv(a.type);\n"
"    if (r.t == NV_FLT) { r.i = 0;     r.f = a.fval; }\n"
"    else               { r.i = a.ival; r.f = 0.0;    }\n"
"    return r;\n"
"}\n"
"\n"
"/* MV_NIL -- the value the interpreter has and NV has no name for.\n"
" *\n"
" * WHY IT EXISTS.  Every global the interpreter creates starts as VAL_NIL,\n"
" * and a name that was never assigned compiles to OP_LOAD_GLOBAL with index\n"
" * -1, for which the dispatch answers value_set(..., VAL_NIL, ...).  This\n"
" * route answered that programme with a REFUSAL: prescan rejected the\n"
" * instruction ('global index -1 out of range'), and the file it did emit\n"
" * initialised every global with { MV_INT, 0, 0 } -- the integer zero.  So\n"
" * `say nosuchglobal == nil` printed true in the interpreter and the same\n"
" * program could not be translated here at all, and the false equivalence\n"
" * between 0 and nil sat where a difference should have been visible.  Zero is\n"
" * a FALSE EQUIVALENCE: it is falsy like nil, but it prints '0' where nil\n"
" * prints 'nil', and `x == nil` and `x == 0` are two answers, not one.\n"
" *\n"
" * WHAT IT ADDS.  One tag (MV_NIL = 5, Value's VAL_NIL) whose payload is all\n"
" * zeroes, a constructor, and the arms nil can actually enter: equality\n"
" * (mv_eq/mv_ne, where nil equals only nil), truthiness (mv_tru, where\n"
" * vm_truthy says nil is false), `not` (mv_not, defined through mv_tru so the\n"
" * one truth-value rule keeps one owner), the opcode OP_IS_NIL (mv_is_nil),\n"
" * and one branch of the print exit (mv_say).  The producer is likewise a\n"
" * single one: OP_LOAD_GLOBAL with index -1, plus the globals initialiser.\n"
" * Index -1 is not a magic number here -- it is the compiler's own spelling of\n"
" * 'unresolved', the same -1 the emitter reads, and the only value it takes.\n"
" *\n"
" * WHAT IT DOES NOT COVER.\n"
" *  - Arithmetic and ordering on a nil (mv_add/mv_lt and the rest).  The\n"
" *    interpreter raises type_mismatch there; this route has no exception\n"
" *    mechanism yet, so mv_as_nv() stops the program.  Loud, never a number --\n"
" *    but it is NOT the interpreter's answer, and closing it needs the\n"
" *    exception opcodes (the C class in docs/streams/aot-mir-value.md).\n"
" *  - nil as a container: INDEX_GET on nil, nil in a dict/list, nil returned\n"
" *    from a call.  Those opcodes are outside this subset, so a nil this block\n"
" *    cannot receive is a nil it need not define.\n"
" *  - A STORE to an unresolved global.  Only OP_LOAD_GLOBAL accepts -1;\n"
" *    OP_STORE_GLOBAL still refuses a negative index, because the interpreter\n"
" *    creates the global on assignment and this route has no table to create\n"
" *    it in.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish).  Delete this tag, the constructor\n"
" * and every nil arm in the commit that gives the shared NV a nil tag and a\n"
" * nv_say branch for it: then mv_nil() becomes mv_from_nv(nv_nil()), the other\n"
" * arms collapse into the same delegation every operation above already uses,\n"
" * and the test is that the string 'MV_NIL' no longer occurs in this block at\n"
" * all (grep -c over kMirValBlock == 0).  Deleting it earlier, while NV still\n"
" * has three tags, leaves those adapters with no answer for a value the\n"
" * emitter produces, which is the shape this block exists to name.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_nil_vs_zero,\n"
" * mir_nil_coalesce and mir_nil_print_truthiness compare bytes against the\n"
" * interpreter; the first exists precisely so that answering nil with 0 is a\n"
" * red criterion rather than a disagreement nobody looked at. */\n"
"static inline MirVal mv_nil(void) {\n"
"    MirVal r;\n"
"    r.type = MV_NIL;\n"
"    r.ival = 0;\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    return r;\n"
"}\n"
"\n"
"/* Every operation below is the nv_* one, reached through the field moves\n"
"   above: the delegating body IS the claim that the runtime is single.  The\n"
"   exceptions are the arms that name MV_NIL, each one spelled the way the\n"
"   interpreter's own val_* spells it for VAL_NIL, because nv_* has no nil to\n"
"   spell: mv_is_nil (the OP_IS_NIL test itself), the equality pair (nil equals\n"
"   only nil), and truthiness (vm_truthy says nil is false).  A further nil\n"
"   rule would be a second rule, so nil does NOT get its own `not`: mv_not\n"
"   goes through mv_tru, and vm_truthy stays the only truth rule.  The print\n"
"   exit is the one arm outside this list -- mv_say, whose nil branch prints\n"
"   the word the interpreter prints.\n"
"   The second such tag is MV_STR, and for the same reason: nv_* carries no\n"
"   string payload either, so the equality pair, truthiness and the ordering\n"
"   pair each hold ONE string arm, spelled the way the interpreter's\n"
"   val_eq/val_orderable/val_cmp/vm_truthy spell it for VAL_STRING.  Those\n"
"   arms are a debt, not a licence: the notes above mv_eq and above\n"
"   mv_str_cmp say exactly what they add, what they do not cover, and the\n"
"   decidable condition under which they must be deleted. */\n"
"static inline MirVal mv_int(long long x) { return mv_from_nv(nv_int(x)); }\n"
"static inline MirVal mv_flt(double x)    { return mv_from_nv(nv_flt(x)); }\n"
"static inline MirVal mv_boo(int x)       { return mv_from_nv(nv_boo(x)); }\n"
"static inline MirVal mv_is_nil(MirVal a) { return mv_boo(a.type == MV_NIL); }\n"
"static inline int mv_tru(MirVal a) {\n"
"    if (a.type == MV_NIL) return 0;   /* vm_truthy: VAL_NIL is false, and only it */\n"
"    if (a.type == MV_STR) return 1;   /* vm_truthy's `default: return 1`\n"
"                                         (src/vm/vm.c:400): every string is\n"
"                                         true, the empty one included.\n"
"                                         Truthiness reads no payload, so this\n"
"                                         arm needs no guard on the payload --\n"
"                                         and none on ival either: a resident\n"
"                                         and a heap-owned string are both\n"
"                                         true, and `ival` here is only the\n"
"                                         free marker the value carries (see\n"
"                                         mv_str_concat). */\n"
"    return nv_tru(mv_as_nv(a));\n"
"}\n"
"static inline MirVal mv_neg(MirVal a) { return mv_from_nv(nv_neg(mv_as_nv(a))); }\n"
"static inline MirVal mv_not(MirVal a) { return mv_boo(!mv_tru(a)); }\n"
"/* THE STRING ARM OF + -- the first value in this route whose answer is\n"
" * HEAP-OWNED, and therefore the first place heap ownership enters the MIR\n"
" * route at all.  Same structure as the notes below: WHY IT EXISTS / WHAT IT\n"
" * ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  `s = \"hi\"` / `t = \"ho\"` / `say s + t` prints `hiho` on the\n"
" * interpreter and died here: mv_add handed both operands to nv_add through\n"
" * mv_as_nv, and mv_as_nv aborts on MV_STR because NV has no string payload, so\n"
" * the run was rc=-6 with EMPTY stdout.  The missing answer is not a formatting\n"
" * detail -- it is la + lb + 1 NEW bytes, which no resident literal holds.  So\n"
" * answering it means allocating, and allocating means ownership, which is the\n"
" * thing the resident-string convention (ival == 1, zero allocation, see\n"
" * mv_str below) had made unnecessary so far.\n"
" *\n"
" * WHAT IT ADDS.  One helper and one branch, and they COPY the interpreter\n"
" * instead of inventing a rule.  src/vm/vm.c's L_ADD string branch (:3291-3308)\n"
" * does exactly: la = strlen(sa), lb = strlen(sb), nb = malloc(la + lb + 1) at\n"
" * :3305, copy both at :3307, NUL at the end, then\n"
" * value_set(&R[ins.r1], VAL_STRING, 0, 0, nb, NULL) at :3308 -- whose third\n"
" * argument IS ival == 0.  That is the same ival convention this block already\n"
" * uses for resident strings (ival == 1 means interned -- the note on Value\n"
" * in src/vm/vm.h:227 says exactly this, and src/vm/vm.c:3255-3262 is where a\n"
" * loaded literal gets that marker), so the result here carries\n"
" * ival = 0 and ival keeps ONE meaning across both routes: the residency and\n"
" * free marker.  Neither operand is written through and neither is freed, which\n"
" * is why the criterion prints `s` and `t` again after every concatenation.\n"
" *\n"
" * OVERFLOW: the check is copied from the CHAINED path, not from L_ADD.\n"
" * L_CONCAT refuses to wrap -- src/vm/vm.c:3351\n"
" * `if (total > (size_t)-1 - l - 1) {...}` -- while L_ADD (:3305) calls\n"
" * malloc(la + lb + 1) with no check at all, so a sum that wrapped would\n"
" * allocate a short buffer and then write past it.  This route copies the CHECK\n"
" * and not the omission; `(size_t)-1 - lb - 1` is that expression spelled for\n"
" * two operands.  The interpreter's own inconsistency is a defect in the\n"
" * interpreter, not a rule to follow.\n"
" *\n"
" * OOM: this route ABORTS where the interpreter substitutes the empty string\n"
" * (`if (!nb) nb = strdup(\"\");` at src/vm/vm.c:3306).  That substitution is no\n"
" * recovery -- strdup is itself a malloc, so when the heap is gone the\n"
" * interpreter's fallback leaves nb NULL and stores a NULL sval -- and on the\n"
" * path where it does succeed it answers `s + t` with \"\" while `s + t` is not\n"
" * the empty string: a silent wrong answer with exit code 0, the exact class\n"
" * this route never prints.  (The chain path's OOM at :3356 does the same.)  So\n"
" * the DIVERGENCE is deliberate and named here: an OOM on this route is a\n"
" * stop with no stdout, never a silently empty string.  mv_alloc() aborts for\n"
" * the same reason.\n"
" *\n"
" * WHAT IT DOES NOT COVER -- each a decision, not an omission:\n"
" *   - a MIXED pair (`s + 1`, `1 + \"7\"`, `s + nil`).  The interpreter stringifies\n"
" *     BOTH sides by one rule (:3292-3303, including its own note that `1 + \"7\"`\n"
" *     once answered \"7\" and lost the 1 silently), and that rule is a new class\n"
" *     of PRINT exit this route does not have.  So a mixed pair still ABORTS:\n"
" *     no answer is better than a made-up one, and it is kept out of the\n"
" *     criterion for exactly this reason.\n"
" *   - the empty right operand (`s + \"\"`) is NOT short-circuited to `a`.  The\n"
" *     interpreter allocates a copy (:3305) and the criterion DEMANDS the copy\n"
" *     (ALLOCS_EXPECT counts its 3 bytes); returning `a` itself would put one\n"
" *     pointer where the exit sweep cannot tell a literal from a block, and\n"
" *     then free a string literal.\n"
" *   - anything returned to the registry before the program ends.  Nothing is:\n"
" *     the registry is accounting, not ownership (see its note), so a loop that\n"
" *     concatenates grows it LINEARLY, one block per iteration, and every\n"
" *     intermediate stays live until exit.  `s = s + \"x\"` N times is O(N^2)\n"
" *     bytes retained at the end rather than O(N).  That is the price of not\n"
" *     doing per-register ownership in this step.  OP_CONCAT -- the chain fold,\n"
" *     which allocates once for a chain -- is a different cell and is NOT\n"
" *     covered or claimed here.\n"
" *   - a NULL payload on either side.  Nothing this route builds produces one\n"
" *     (mv_str points at a literal, mv_str_concat at a live block), so that\n"
" *     abort is an IMPOSSIBLE-STATE GUARD, not a missing branch.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish).  This arm is NOT deleted by the\n"
" * P3 step the equality arms name: a concat still has to build a buffer after\n"
" * the shared NV learns a string tag.  It is REPLACED by whatever commit lands\n"
" * per-register ownership, and the mechanical form is the registry's own\n"
" * (`grep -c 'mv_free_all'` over a generated TU == 0, `grep -c 'mv_set('` ==\n"
" * the number of register write sites).  Until then this arm is the only thing\n"
" * answering `+` on two strings, so it is not debt with a deadline; the\n"
" * registry is.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY case mir_add_string_concat\n"
" * compares BYTES against the interpreter over seven concatenations, and its\n"
" * ALLOCS_EXPECT entry demands allocs=7|bytes=33 from the SAME run -- so an arm\n"
" * that prints the right bytes while allocating the wrong number of them is\n"
" * red too.  Before this arm the case was rc=-6 with empty stdout.\n"
" */\n"
"static inline MirVal mv_str_concat(MirVal a, MirVal b) {\n"
"    if (!a.sval || !b.sval) abort();            /* no state this route builds */\n"
"    size_t la = strlen(a.sval), lb = strlen(b.sval);\n"
"    if (la > (size_t)-1 - lb - 1) abort();      /* copy chain's check (vm.c:3351),\n"
"                                                   not L_ADD's omission (:3305) */\n"
"    char *nb = mv_alloc(la + lb + 1);           /* registered, swept at exit */\n"
"    memcpy(nb, a.sval, la);                     /* vm.c:3307 */\n"
"    memcpy(nb + la, b.sval, lb);\n"
"    nb[la + lb] = 0;\n"
"    MirVal r;\n"
"    r.type = MV_STR;\n"
"    r.ival = 0;      /* heap-owned: value_set(..., VAL_STRING, 0, 0, nb, NULL), vm.c:3308 */\n"
"    r.sval = nb;\n"
"    r.ptr  = 0;\n"
"    return r;\n"
"}\n"
"static inline MirVal mv_add(MirVal a, MirVal b) {\n"
"    if (a.type == MV_STR || b.type == MV_STR) {\n"
"        if (a.type != MV_STR || b.type != MV_STR) abort();   /* MIXED PAIR: no\n"
"                                                                answer on this route */\n"
"        return mv_str_concat(a, b);\n"
"    }\n"
"    return mv_from_nv(nv_add(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"/* THE CHAIN FOLD -- one buffer for a whole `a + b + c + ...`, OP_CONCAT.\n"
" *\n"
" * WHY IT EXISTS.  `a + b + c` is compiled to ONE OP_CONCAT (the compiler\n"
" * flattens a 3+-operand string chain; see its emit site), not to\n"
" * (a + b) + c, and the interpreter answers it with ONE malloc: its L_CONCAT\n"
" * branch sums every segment's length, allocates total + 1 once and memcpys the\n"
" * segments in order.  Folding here by calling mv_str_concat N-1 times would\n"
" * print the same BYTES while asking the heap for N-1 blocks -- the shape this\n"
" * cell exists to remove -- so the all-string path below is the ONLY place a\n"
" * chain allocates, and the exact (allocs, bytes) reading is what makes the\n"
" * difference visible.\n"
" *\n"
" * WHAT IT ADDS.  The two shapes the interpreter's own branch has, in its own\n"
" * order, so a 2-term `+` and a 3-term `+` cannot answer differently:\n"
" *   - count <= 0 -> nil, count == 1 -> the operand itself (value_assign);\n"
" *   - all operands strings -> sum, ONE mv_alloc, per-segment memcpy, result\n"
" *     marked heap-owned exactly as every concat result is.\n"
" * The overflow check is the CHAIN's, not the pair's: the predicate is copied\n"
" * from src/vm/vm.c:3351 and evaluated against the RUNNING total per segment,\n"
" * so a chain is checked against the whole sum and not only one neighbour.\n"
" *\n"
" * WHAT IT DOES NOT COVER -- each a decision:\n"
" *   - a chain that is not all strings.  The interpreter's general path folds\n"
" *     pairwise through the L_ADD rule, which STRINGIFIES a non-string operand\n"
" *     (and unions two sets).  This route has no stringify exit, so the fold is\n"
" *     delegated to mv_add and stops where mv_add stops: a mixed pair aborts\n"
" *     (the boundary the pair arm already owns), never a made-up answer.  This\n"
" *     helper is therefore a FAST PATH over the same fold semantics, not a\n"
" *     second semantic.\n"
" *   - an overflow, an allocation failure, or a NULL string payload.  The\n"
" *     interpreter answers each with an empty string (:3351 falls back to the\n"
" *     pairwise path, :3356 is strdup(\"\"), the lengths read `sval ? sval : \"\"`);\n"
" *     this route ABORTS.  For OOM and for a NULL payload that is the divergence\n"
" *     already stated above mv_str_concat (an OOM fallback answers `s + t` with\n"
" *     \"\" while the answer is not \"\", and strdup is itself a malloc).  For the\n"
" *     overflow the fallback cannot help either: the sum does not fit, so the\n"
" *     pairwise path it falls back to cannot build the buffer; abort is the\n"
" *     honest stop.\n"
" *   - an intermediate buffer returned to the registry: nothing is, so a mixed\n"
" *     chain retains one block per step until exit (the registry's accounting\n"
" *     debt, not a new one).\n"
" *   - the interpreter's VAL_ARRAY/VAL_DICT trap in the same branch: it has no\n"
" *     operand here, because no opcode this route emits builds an array or a\n"
" *     dict and MirVal has no tag for one.\n"
" *   - the interpreter's VAL_SET union arm in the general path: same reason\n"
" *     (MirVal has no set tag, and the preset set global is already refused).\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY case mir_concat_chain\n"
" * compares BYTES against the interpreter over SIX chains (three, eleven, four,\n"
" * three, resident and heap-owned segments mixed, the empty string in the\n"
" * middle, one owned buffer three times), and its ALLOCS_EXPECT entry demands an\n"
" * exact (allocs, bytes) pair -- one block per all-string chain, so a pairwise\n"
" * fold is red on the NUMBER alone even when every printed byte is right. */\n"
"static inline MirVal mv_concat_n(MirVal *ops, size_t n) {\n"
"    if (n == 0) return mv_nil();             /* count <= 0 -> VAL_NIL */\n"
"    if (n == 1) return ops[0];               /* value_assign, L_CONCAT */\n"
"    int all_str = 1;\n"
"    size_t total = 0;\n"
"    size_t i;\n"
"    for (i = 0; i < n; i++) {\n"
"        if (ops[i].type != MV_STR) { all_str = 0; break; }   /* -> mv_add fold */\n"
"        if (!ops[i].sval) abort();          /* impossible state; see the note */\n"
"        size_t l = strlen(ops[i].sval);\n"
"        if (total > (size_t)-1 - l - 1) abort();  /* vm.c:3351, summed over the\n"
"                                                     CHAIN, not over one pair */\n"
"        total += l;\n"
"    }\n"
"    if (all_str) {\n"
"        char *nb = mv_alloc(total + 1);     /* ONE block for the whole chain */\n"
"        size_t off = 0;\n"
"        for (i = 0; i < n; i++) {\n"
"            size_t l = strlen(ops[i].sval);\n"
"            memcpy(nb + off, ops[i].sval, l);\n"
"            off += l;\n"
"        }\n"
"        nb[total] = 0;\n"
"        MirVal r;\n"
"        r.type = MV_STR;\n"
"        r.ival = 0;      /* heap-owned: the marker mv_str_concat also sets */\n"
"        r.sval = nb;\n"
"        r.ptr  = 0;\n"
"        return r;\n"
"    }\n"
"    {   /* not all strings: the interpreter's left-assoc fold, delegated */\n"
"        MirVal acc = ops[0];\n"
"        for (i = 1; i < n; i++) acc = mv_add(acc, ops[i]);\n"
"        return acc;\n"
"    }\n"
"}\n"
"static inline MirVal mv_sub(MirVal a, MirVal b) { return mv_from_nv(nv_sub(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_mul(MirVal a, MirVal b) { return mv_from_nv(nv_mul(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_div(MirVal a, MirVal b) { return mv_from_nv(nv_div(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_mod(MirVal a, MirVal b) { return mv_from_nv(nv_mod(mv_as_nv(a), mv_as_nv(b))); }\n"
"/* THE STRING ARM IN THE EQUALITY PAIR -- the debt this block used to pay for\n"
" * by dying.  Same structure as the nil note above: WHY IT EXISTS / WHAT IT\n"
" * ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  A string could be printed but not compared.  `s = \"hi\"` /\n"
" * `t = \"ho\"` / `say s == t` passed prescan (OP_EQ is in the lowering table),\n"
" * compiled with cc, and then DIED AT RUN TIME with SIGABRT and empty stdout,\n"
" * because mv_eq named MV_NIL and nothing else and mv_as_nv(MV_STR) reached\n"
" * mv_tag_to_nv's `default: abort()`.  The interpreter answered `false` all\n"
" * along.  That is worse than a refusal: a REFUSAL is honest, while this was a\n"
" * program the route ACCEPTED, compiled, and killed while the answer existed.\n"
" * The hole was in mv_ne and in mv_tru too (`if s {}` aborted the same way),\n"
" * so the three arms are one change.  NOTE the asymmetry this note USED to\n"
" * carry: the CONSTANT form `s == \"hi\"` (OP_EQK/OP_NEQK, the literal in the\n"
" * instruction rather than in a register) was refused while the variable form\n"
" * was answered -- one question, two spellings, one answer and one refusal.\n"
" * That hole is CLOSED by the OP_EQK/OP_NEQK arm in emit_one below, which keeps\n"
" * this one rule (it calls mv_eq/mv_ne rather than repeating the content\n"
" * comparison) and docs/STATUS.md 10.109 records the closure.  The sentence\n"
" * stays in past tense on purpose: it is a record of what this note did NOT\n"
" * cover at the time.\n"
" *\n"
" * WHAT IT ADDS.  One arm per adapter, dispatched on TYPE first and on\n"
" * CONTENT second -- never a string inside the numeric path.  The rules are\n"
" * COPIED from the interpreter, not invented here:\n"
" *\n"
" *   - same-type strings, equality: src/vm/vm.c:417,\n"
" *       return (a->sval == b->sval) ? true : strcmp(a->sval ? a->sval : \"\",\n"
" *                                                   b->sval ? b->sval : \"\") == 0;\n"
" *     i.e. a pointer fast path, then content, with a NULL payload read as the\n"
" *     empty string.  The fast path is why `s == s` costs no strcmp at all.\n"
" *   - a string against a NON-string: src/vm/vm.c:414 gates the same-type\n"
" *     block, the coercion block (:425-427) applies only to INT/FLOAT/BOOL\n"
" *     pairs, and every other combination reaches `return false` (:428) --\n"
" *     which is why `s == 1` is false and `s != 1` is true.  `!=` is the same\n"
" *     val_eq negated (:3552-3553), so mv_ne below negates mv_eq rather than\n"
" *     keeping a second copy of the content rule: one owner, one rule.\n"
" *   - truthiness: src/vm/vm.c:400, vm_truthy's `default: return 1`.  Every\n"
" *     string is TRUE, the empty one included.\n"
" *\n"
" * Zero allocation: the arm reads `sval`; it never copies it, resident or not.\n"
" * No new print exit, and no change to what an operation means beyond the tags\n"
" * that genuinely had no answer.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  Deliberately, and each is a decision:\n"
" *   - ORDERING on strings (`s < t`, `<=`, `>`, `>=`).  This WAS a live hole\n"
" *     of exactly this shape and this note carried it as the next entry.  It\n"
" *     is CLOSED now: mv_str_cmp below reads src/vm/vm.c:443 and :449-451 the\n"
" *     way val_cmp does (pointer fast path, then strcmp), and\n"
" *     docs/STATUS.md 10.107 writes the mixed-type boundary down.  The bullet\n"
" *     stays in past tense on purpose -- it is a record of what this note did\n"
" *     NOT cover at the time, not a claim about today.\n"
" *   - A string that is not resident (`ival != 1`).  This WAS the second bullet\n"
" *     here and it is CLOSED too: mv_str_concat (above) builds one by copying\n"
" *     the interpreter's L_ADD, so ival == 0 is now a value this route really\n"
" *     produces and the arms must answer for it.  What the arms now test is the\n"
" *     PAYLOAD (`sval != NULL`), not the residency marker: `ival` keeps the one\n"
" *     job the interpreter gives it -- 1 means interned-and-not-freed, 0 means\n"
" *     heap-owned -- and reading it for a semantic decision here would have\n"
" *     made this arm's answer depend on which allocator produced the string.\n"
" *     The NULL test stays, as an IMPOSSIBLE-STATE GUARD (both producers here\n"
" *     set `sval`), spelled as an abort rather than a silent answer so that a\n"
" *     future producer cannot make this arm lie.\n"
" *   - A string against an array/dict/set, a string as a set or dict key,\n"
" *     case folding, Unicode normalisation, locale collation.  Those opcodes\n"
" *     are outside this subset.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish): delete the three arms in the\n"
" * commit that gives the shared NV a string tag and moves Value's comparison\n"
" * semantics into the IR -- the P3 step.  Then mv_eq/mv_ne/mv_tru go back to\n"
" * delegating to nv_* like every operation around them, and the check is\n"
" * mechanical: MV_STR no longer appears in this block outside the tag #define,\n"
" * mv_str(), mv_str_concat() and mv_say's print branch.  (The old form of this\n"
" * check -- `grep -c 'a.ival != 1'` == 0 -- is now ALREADY satisfied, because\n"
" * mv_str_concat relaxed this arm's residency test to a payload test; that is\n"
" * why the check is spelled as MV_STR's reach here and not as the absence of a\n"
" * marker that a heap-owned string made meaningless.)  Deleting the arms\n"
" * earlier, while NV still carries no string payload, would leave OP_EQ with no\n"
" * answer for a value the emitter does produce -- which is precisely the crash\n"
" * this note exists to name.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_loadk_string_eq,\n"
" * mir_loadk_string_eq_number and mir_loadk_string_truthiness compare BYTES\n"
" * against the interpreter.  Before these arms the first was red as a SIGABRT\n"
" * with empty stdout; the second (a string against an int AND a float) is what\n"
" * stops the arm from degenerating into a strcmp against a coerced 0, and the\n"
" * third is what stops mv_tru from keeping the old hole. */\n"
"static inline MirVal mv_eq(MirVal a, MirVal b) {\n"
"    if (a.type == MV_NIL || b.type == MV_NIL) return mv_boo(a.type == b.type);\n"
"    if (a.type == MV_STR || b.type == MV_STR) {\n"
"        if (a.type != b.type) return mv_boo(0);   /* val_eq:414/425-427/428 */\n"
"        if (!a.sval || !b.sval) abort();  /* payload guard only: a resident and a\n"
"                                             heap-owned string compare alike, and\n"
"                                             ival is the free marker, not a\n"
"                                             residency claim (see mv_str_concat) */\n"
"        if (a.sval == b.sval) return mv_boo(1);   /* val_eq:417 pointer fast path */\n"
"        return mv_boo(strcmp(a.sval, b.sval) == 0);\n"
"    }\n"
"    return mv_from_nv(nv_eq(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_ne(MirVal a, MirVal b) {\n"
"    if (a.type == MV_NIL || b.type == MV_NIL) return mv_boo(a.type != b.type);\n"
"    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(!mv_tru(mv_eq(a, b)));\n"
"    return mv_from_nv(nv_ne(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"/* THE ORDERING PAIR ON STRINGS -- the equality arm's sibling, and the same\n"
" * shape of hole: accepted by prescan, compiled by cc, then killed at run\n"
" * time.  Same structure as the note above mv_eq: WHY IT EXISTS / WHAT IT\n"
" * ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  `s = \"hi\"` / `t = \"ho\"` / `say s < t` passed prescan\n"
" * (OP_LT/OP_LE/OP_GT/OP_GE are all in the lowering table), compiled, and\n"
" * then ran to SIGABRT with EMPTY stdout, while the interpreter answered\n"
" * true/false.  Root cause is the one the equality entry named: mv_lt and its\n"
" * three siblings handed mv_as_nv(MV_STR) to nv_*, and mv_tag_to_nv's\n"
" * `default: abort()` is a guard for tags NO opcode can produce -- not a\n"
" * capability for a tag OP_LOADK_STRING does produce.  Note the precedence:\n"
" * the equality note used to carry this hole as its NEXT entry; this entry is\n"
" * that entry, and the equality note now points here.\n"
" *\n"
" * WHAT IT ADDS.  ONE content rule (mv_str_cmp) and four comparisons,\n"
" * because the rule IS one rule and the four operators differ only in the\n"
" * sign they ask for (lt: < 0, le: <= 0, gt: > 0, ge: >= 0).  The rule is\n"
" * COPIED from the interpreter, not invented here:\n"
" *   - src/vm/vm.c:443 (val_orderable): a same-type string pair IS orderable.\n"
" *     The interpreter therefore HAS this answer; refusing to compile the\n"
" *     question would hide an existing answer behind unsupported-opcode.\n"
" *   - src/vm/vm.c:449-451 (val_cmp): same-type strings compare the sval\n"
" *     POINTER first (a->sval == b->sval returns 0), then strcmp, with a NULL\n"
" *     payload normalised to the empty string.  mv_str_cmp keeps BOTH halves:\n"
" *     the pointer fast path (so `s <= s` costs no strcmp) and the content\n"
" *     comparison.  The NULL normalisation itself is unreachable here, because\n"
" *     the residency guard below stops first -- deliberately: a NULL payload\n"
" *     read as the empty string would be a silent answer, and this block\n"
" *     spells that class of value as a stop (see the equality note)\n"
" *   - the four comparisons are then the signs of ONE int, so no operator can\n"
" *     disagree with another about the content rule.\n"
" *\n"
" * Zero allocation: strcmp reads `sval`, no string is created -- resident or\n"
" * heap-owned, the comparison is the same bytes (see mv_str_concat: a\n"
" * heap-owned string carries ival == 0 purely as the free marker).\n"
" * No new print exit, no new opcode (OP_LT/OP_LE/OP_GT/OP_GE were already in\n"
" * the lowering table).\n"
" *\n"
" * WHAT IT DOES NOT COVER -- a BOUNDARY, deliberately, not an oversight.\n"
" * A string against a NON-string (`s < 1`, `1 < s`, `s < nil`) is REFUSED by\n"
" * the interpreter with `uncaught: type_mismatch` (rc=1): ::443-445 says a\n"
" * pair is orderable only when BOTH are strings or BOTH are numeric.  This\n"
" * route has no exception mechanism (the C class is still unbuilt), so a\n"
" * refusal cannot be reproduced -- and both available fake answers are worse\n"
" * than a stop: a silent false/true would be a lie with exit code 0, which is\n"
" * exactly the SILENT class of wrong answer the implicit-return entry\n"
" * (docs/STATUS.md 10.106) just closed.  So a mixed pair ABORTS, and that\n"
" * abort is an UNDEFINED-BOUNDARY GUARD -- it says THIS PAIR HAS NO ANSWER ON\n"
" * THIS ROUTE, not that a branch is missing.  Written down so that nobody\n"
" * later 'fixes' the crash by making the comparison a constant.  (nil against\n"
" * nil never reaches this helper: it falls through to nv_*, where mv_as_nv\n"
" * stops, which is the same boundary one layer down.)\n"
" *\n"
" * DELETE CONDITION (decidable, and an EXPERIMENT rather than a wish): the\n"
" * commit that gives the shared NV a string payload and an nv ordering rule\n"
" * (the same P3 step that deletes the equality arms).  Test: delete this\n"
" * helper and the four arms, restore the plain one-line nv_* delegation,\n"
" * rebuild, and run mir_loadk_string_order -- green (same as interpreter)\n"
" * means NV owns the rule now and the arms are gone for good; red (SIGABRT\n"
" * with empty stdout again) means NV still has no string payload and deleting\n"
" * the arms early is precisely the crash this note exists to name.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY case\n"
" * mir_loadk_string_order compares BYTES against the interpreter: all four\n"
" * operators, both directions (hi < ho true, ho < hi false), and an equal\n"
" * pair (s <= s / s >= s true, s < s / s > s false) so the pointer fast path\n"
" * is exercised and not just the content comparison.  Before this arm it\n"
" * reddened as rc=-6 with empty stdout, interp-out\n"
" * b'true\\ntrue\\nfalse\\nfalse\\n...'.\n"
" */\n"
"static inline int mv_str_cmp(MirVal a, MirVal b) {\n"
"    if (a.type != MV_STR || b.type != MV_STR) abort();  /* MIXED PAIR: no\n"
"                                                         answer on this route (see above) */\n"
"    if (!a.sval || !b.sval) abort();  /* payload guard only: ival is the free\n"
"                                         marker, not a residency claim (see\n"
"                                         mv_str_concat, which relaxed it) */\n"
"    if (a.sval == b.sval) return 0;   /* val_cmp:450 pointer fast path */\n"
"    return strcmp(a.sval, b.sval);    /* val_cmp:451 */\n"
"}\n"
"static inline MirVal mv_lt(MirVal a, MirVal b) {\n"
"    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(mv_str_cmp(a, b) < 0);\n"
"    return mv_from_nv(nv_lt(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_le(MirVal a, MirVal b) {\n"
"    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(mv_str_cmp(a, b) <= 0);\n"
"    return mv_from_nv(nv_le(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_gt(MirVal a, MirVal b) {\n"
"    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(mv_str_cmp(a, b) > 0);\n"
"    return mv_from_nv(nv_gt(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_ge(MirVal a, MirVal b) {\n"
"    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(mv_str_cmp(a, b) >= 0);\n"
"    return mv_from_nv(nv_ge(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"\n"
"/* A string constant.  RESIDENT, not owned: the argument is always a C string\n"
"   literal -- an array with static storage duration that nobody frees -- and\n"
"   `ival = 1` is the marker the interpreter itself uses for exactly that\n"
"   (src/vm/vm.h:229, src/vm/vm.c:227: VAL_STRING with ival == 1 is an\n"
"   interned string, and every free site in the runtime skips ival == 1).\n"
"   Zero allocation, zero copying, and no owner to leak.\n"
"   `ival` carrying both a payload and a residency flag is debt the VM already\n"
"   carries; this route follows that convention instead of inventing a cleaner\n"
"   field that nothing else in the runtime has. */\n"
"static inline MirVal mv_str(const char *s) {\n"
"    MirVal r;\n"
"    r.type = MV_STR;\n"
"    r.ival = 1;                 /* 1 = resident (static storage), 0 = not */\n"
"    r.sval = (char *)s;         /* never written through; see above */\n"
"    r.ptr  = 0;\n"
"    return r;\n"
"}\n"
"\n"
"/* THE FIRST CONTAINER THIS ROUTE BUILDS -- a heap ARRAY plus the read-only\n"
" * index that takes a value back out of it (OP_NEW_ARRAY / OP_INDEX_GET).\n"
" * Same note structure as the two blocks above: WHY / WHAT IT ADDS / WHAT IT\n"
" * DOES NOT COVER / DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  Until this pair, every value this route produced lived in a\n"
" * register, and a program that built an array was refused whole: the\n"
" * interpreter answered `say [1, 2, 3][1]` and this route had a name for\n"
" * neither opcode (both were IR-MIR-NOT-YET, and the array's elements left the\n"
" * argument stack unbalanced at prescan).  An array literal is the first value\n"
" * whose answer NEEDS memory, so this is also the first container the\n"
" * allocation instrument measures.\n"
" *\n"
" * WHAT IT ADDS.  One tag, MV_ARR = VAL_ARRAY, and Value's OWN array row\n"
" * copied rather than re-invented: ival is the array's POOL INDEX and sval and\n"
" * ptr are NULL, which is exactly what src/vm/vm.c:3644 writes --\n"
" * `value_set(&R[ins.r1], VAL_ARRAY, aidx + 1, 0, NULL, NULL)` -- so an MV_ARR\n"
" * value is byte-for-byte the VAL_ARRAY value a translator may move into a\n"
" * Value.  The 32 bytes cannot carry the element COUNT (the interpreter keeps\n"
" * that in its ArrayObj), so the count lives in the POOL SLOT IMMEDIATELY\n"
" * BEFORE the array's first item and pool[ival] is item 0: the same pointer\n"
" * relation ArrayObj's aidx has, with the header in the pool instead of in the\n"
" * value.  mv_arr_new and mv_arr_get are the only two readers of that slot.\n"
" * Growth doubles into a new mv_alloc block and leaves the old block to the\n"
" * exit sweep; there is no free list yet and this route reclaims nothing\n"
" * mid-run, which is mv_str_concat's debt word for word (a loop that builds N\n"
" * arrays allocates as the pool grows and frees once, at exit).\n"
" *\n"
" * WHAT IT DOES NOT COVER, each named by the thing that would need it:\n"
" *   - OP_INDEX_SET is not emitted, so no program on this route can store INTO\n"
" *     an array; a literal is read-only for its whole life, which is also why\n"
" *     the pool is never re-indexed.\n"
" *   - OP_NEW_DICT and OP_NEW_SET are not emitted, and prescan refuses the\n"
" *     preset `set` global by name, so no DICT can reach this pool.  mv_arr_get\n"
" *     answers a container that is neither array nor string with nil, which is\n"
" *     the interpreter's own else branch (src/vm/vm.c:3712-3714) and the right\n"
" *     answer for nil, an int, a float and a bool; for a dict it would be the\n"
" *     WRONG answer, so a dict container is unreachable here rather than\n"
" *     handled here, and docs/STATUS.md says so.\n"
" *   - `say a` -- printing a container -- is a STOP, not an answer: mv_say\n"
" *     hands every tag but nil and string to nv_say through mv_as_nv, and\n"
" *     mv_tag_to_nv aborts on MV_ARR where the interpreter prints a bracketed\n"
" *     list.  That is a print-exit class of its own, recorded in docs/STATUS.md\n"
" *     with this step, not silently answered.\n"
" *   - vm_array_get's intern step (src/vm/vm.c:1006-1009: a non-resident string\n"
" *     read back out of an array becomes the interned one) is NOT reproduced.\n"
" *     There is no intern table on this route; the bytes printed are identical,\n"
" *     and the only thing interning changed was string IDENTITY, which no MIR\n"
" *     path can observe (mv_eq compares string payloads through mv_str_cmp).\n"
" *\n"
" * DELETE CONDITION (decidable).  Delete this block in the commit where the\n"
" * route's registers ARE the VM's registers, so arrays are read through\n"
" * vm_array_get and `grep -c mv_arrmem` over this block falls to 0 -- or in the\n"
" * commit that gives Value's array row a home in this route\n"
" * (docs/streams/aot-mir-value.md section 4).  A container runtime that\n"
" * outlives its reason is the debt this comment exists to date.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's container cases, byte-compared against\n"
" * the interpreter: mir_container_new_array (element 0, every element, both\n"
" * ends, a negative subscript and one past the end), mir_container_index_get\n"
" * (a literal reached through a register, and a nested literal read through a\n"
" * register), mir_container_index_edges (a float subscript truncating, a bool\n"
" * subscript, a zero-element literal, nil out of range), mir_container_string_index\n"
" * (the string container's one-character read, both directions), and\n"
" * mir_container_index_type_mismatch (nil as a subscript: the interpreter's\n"
" * type_mismatch leaves stdout empty and exits 1, and this route stops with the\n"
" * same rc and the same words).  The refusal criterion mir_container_const_oob\n"
" * demands that a CONSTANT out-of-range subscript is refused before generation,\n"
" * with its own why.  ALLOCS_EXPECT carries the pool's own (allocs, bytes) pair,\n"
" * measured first and written down after.\n"
" */\n"
"\n"
"#define MV_ARR 6   /* = VAL_ARRAY -- ival is a POOL INDEX, as Value's row has */\n"
"\n"
"static MirVal *mv_arrmem = 0;   /* the item pool: one flat block, doubled */\n"
"static int mv_arrcap = 0, mv_arrused = 0;\n"
"\n"
"static inline MirVal mv_arr_new(int n, const MirVal *items) {\n"
"    int want = mv_arrused + n + 1;             /* 1 count slot, then n items */\n"
"    if (want > mv_arrcap) {\n"
"        int cap = mv_arrcap ? mv_arrcap : 8;\n"
"        while (cap < want) cap *= 2;\n"
"        MirVal *fresh = (MirVal *)mv_alloc(sizeof(MirVal) * (size_t)cap);\n"
"        for (int i = 0; i < mv_arrused; i++) fresh[i] = mv_arrmem[i];\n"
"        mv_arrmem = fresh;                     /* old block: swept at exit */\n"
"        mv_arrcap = cap;\n"
"    }\n"
"    MirVal r;\n"
"    r.type = MV_ARR;\n"
"    r.ival = mv_arrused + 1;                   /* item 0 sits at pool[ival] */\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    mv_arrmem[r.ival - 1] = mv_int(n);         /* the count, one slot before */\n"
"    for (int i = 0; i < n; i++) mv_arrmem[r.ival + i] = items[i];\n"
"    mv_arrused = want;\n"
"    return r;\n"
"}\n"
"\n"
"/* val_as_double's classification (src/vm/vm.c:323-334), without a VM to throw\n"
"   into.  int, float and bool are numbers there; nil, string and array call\n"
"   vm_throw_kind(\"type_mismatch\") and read nothing, and an uncaught throw\n"
"   prints NOTHING on stdout and exits 1 (measured: `say a[nosuchglobal]` prints\n"
"   the exception on stderr, stdout empty, rc 1).  This route has no exception\n"
"   mechanism, so the classification is reproduced as a STOP carrying the\n"
"   interpreter's rc and the interpreter's words -- NOT as a silent 0, which\n"
"   would print a number the interpreter never prints.  Prescan cannot decide\n"
"   this for a variable subscript: a register's tag is only knowable when a\n"
"   constant was loaded into it, and the refusal that would hide the\n"
"   interpreter's answer is not the refusal this route makes.\n"
"   The interpreter checks `idxv->type == VAL_INT` first and calls\n"
"   val_as_double for everything else (src/vm/vm.c:3679, :3702); the two agree\n"
"   on every tag, so one tag table serves both opcodes. */\n"
"static inline int mv_index_i32(MirVal a) {\n"
"    switch (a.type) {\n"
"    case MV_INT:   return (int)a.ival;\n"
"    case MV_FLT:   return (int)a.fval;\n"
"    case MV_BOO:   return a.ival ? 1 : 0;\n"
"    default:       break;            /* MV_NIL / MV_STR / MV_ARR: vm.c:328 */\n"
"    }\n"
"    fprintf(stderr, \"[exception] uncaught: type_mismatch\\n\");\n"
"    exit(1);\n"
"    return 0;   /* not reached: exit is noreturn.  Spelled so a compiler that\n"
"                   does not say so still cannot hand back a silent 0. */\n"
"}\n"
"\n"
"static inline MirVal mv_arr_get(MirVal obj, MirVal idxv) {\n"
"    int i = mv_index_i32(idxv);   /* converted BEFORE the container is looked at:\n"
"                                     a bad subscript is type_mismatch even when\n"
"                                     the array is empty (src/vm/vm.c:3678-3680) */\n"
"    if (obj.type == MV_ARR) {\n"
"        int iv = (int)obj.ival;\n"
"        if (iv < 1 || iv > mv_arrused) return mv_nil();   /* not a live slot */\n"
"        int n = (int)mv_arrmem[iv - 1].ival;              /* the count header */\n"
"        if (i < 0 || i >= n) return mv_nil();             /* src/vm/vm.c:1002 */\n"
"        return mv_arrmem[iv + i];\n"
"    }\n"
"    if (obj.type == MV_STR) {\n"
"        /* The other container this subset can hold, answered the way\n"
"           src/vm/vm.c:3700-3711 answers it: a negative subscript counts from\n"
"           the end, out of range on either side is nil, and a hit is a NEW\n"
"           one-character string (strdup(buf) there, mv_alloc(2) here -- ival 0,\n"
"           heap-owned, swept at exit).  A loop of these allocates per read\n"
"           exactly as the interpreter does. */\n"
"        int la = (int)strlen(obj.sval ? obj.sval : \"\");\n"
"        if (i < 0) i += la;\n"
"        if (obj.sval && i >= 0 && i < la) {\n"
"            char *nb = (char *)mv_alloc(2);\n"
"            MirVal r;\n"
"            nb[0] = obj.sval[i];\n"
"            nb[1] = 0;\n"
"            r.type = MV_STR;\n"
"            r.ival = 0;\n"
"            r.sval = nb;\n"
"            r.ptr  = 0;\n"
"            return r;\n"
"        }\n"
"        return mv_nil();\n"
"    }\n"
"    return mv_nil();   /* every other container: src/vm/vm.c:3712-3714 */\n"
"}\n"
"\n"
"/* THE ONE MIR-SIDE PRINT EXIT -- the second honest debt in this block.\n"
" *\n"
" * WHY IT EXISTS.  The shared preamble's nv_say(NV a) has exactly three\n"
" * branches: NV_BOO prints true/false, NV_FLT prints through nv_fmt_double,\n"
" * and everything else prints printf(\"%lld\", a.i).  NV is\n"
" * `{int t; long long i; double f;}`, so it has nowhere to put a string and\n"
" * no tag for nil, and neither `say s` nor `say nil` can be lowered through the\n"
" * shared exit at all -- mv_as_nv() aborts on MV_STR and MV_NIL by design (see\n"
" * the tag pair above).\n"
" *\n"
" * WHAT IT ADDS.  Exactly two branches, one per tag NV cannot carry: a resident\n"
" * string (the string itself, not a formatted integer), and the three letters\n"
" * the interpreter prints for VAL_NIL (its say-op prints the literal word `nil`\n"
" * where it prints a number, so the word is the answer, not a formatting rule).\n"
" * Every other tag is handed to nv_say(mv_as_nv(a)), so integer, float and bool\n"
" * formatting keep\n"
" * one implementation.  Formatting those again here would be a second runtime\n"
" * in shadow, which is the thing the whole MirVal block exists to avoid.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  A string with a NULL payload.  That WAS spelled\n"
" * `ival != 1` here -- no emittable opcode built a non-resident string -- and\n"
" * mv_str_concat closed that premise when it started building one by copying\n"
" * the interpreter: ival == 0 is now a value this route really prints.  So the\n"
" * branch tests the PAYLOAD (`sval != NULL`), which is what it always meant, and\n"
" * ival keeps only its free-marker job (1 = interned, 0 = heap-owned).  A NULL\n"
" * payload is still a stop, never a printf of the NULL.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish).  Delete this function in the\n"
" * commit where nv_say() learns BOTH tags -- strings and nil -- then both\n"
" * branches belong in the shared exit and both routes print through it (the\n"
" * decidable form: `grep -c MV_NIL` and `grep -c MV_STR` over this block fall\n"
" * to 0 apart from the two #define lines) -- or in the commit that completes P3\n"
" * and leaves one printer for the route\n"
" * (docs/streams/aot-mir-value.md section 4).  A print exit that outlives its\n"
" * reason is the debt this comment exists to date. */\n"
"static inline void mv_say(MirVal a) {\n"
"    if (a.type == MV_NIL) { printf(\"nil\\n\"); return; }\n"
"    if (a.type == MV_STR) {\n"
"        if (a.sval) { printf(\"%s\\n\", a.sval); return; }   /* resident or\n"
"                                                       heap-owned: same bytes */\n"
"        abort();   /* NULL payload: not a state either producer creates */\n"
"    }\n"
"    nv_say(mv_as_nv(a));\n"
"}\n"
"\n";

/* A register index beyond this is refused rather than turned into a C array
   that would be hundreds of megabytes.  The interpreter's frames are far below
   it; the check exists so that a corrupt container is a refusal, not UB in
   generated C. */
#define REG_LIMIT 4096

/* ------------------------------------------------------------------ buffer */

typedef struct {
    char  *d;
    size_t len, cap;
} Buf;

static void buf_init(Buf *b) { b->cap = 8192; b->len = 0; b->d = malloc(b->cap); if (b->d) b->d[0] = 0; }
static void buf_free(Buf *b) { free(b->d); b->d = NULL; b->len = b->cap = 0; }

static void buf_need(Buf *b, size_t extra) {
    if (!b->d) return;
    if (b->len + extra + 1 <= b->cap) return;
    while (b->len + extra + 1 > b->cap) b->cap *= 2;
    char *n = realloc(b->d, b->cap);
    if (!n) { free(b->d); b->d = NULL; return; }
    b->d = n;
}

static void buf_str(Buf *b, const char *s) {
    size_t n = strlen(s);
    buf_need(b, n);
    if (!b->d) return;
    memcpy(b->d + b->len, s, n);
    b->len += n;
    b->d[b->len] = 0;
}

/* One pool string as the BODY of a C string literal, byte for byte.

   The bytes have to survive the compiler: a quote or a backslash would end or
   corrupt the literal, a control byte (a tab, a newline -- both of which a
   source program writes as \t and \n, and the lexer turns into the real bytes)
   must come back as an escape, and every byte >= 0x7f becomes a three-digit
   octal escape so the literal is pure ASCII and cannot be re-encoded on the way
   through the compiler.  Octal rather than \xNN because \x consumes every
   following hex digit ("\x09" followed by 'b' would be the byte 0x9b), and
   '?' is escaped because a C11 translator may still implement trigraphs, where
   "??!" is a legal sequence inside a string literal. */
static void buf_c_string(Buf *b, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        char tmp[8];
        unsigned char ch = *p;
        if (ch == '"')  { buf_str(b, "\\\""); continue; }
        if (ch == '\\') { buf_str(b, "\\\\"); continue; }
        if (ch == '\n') { buf_str(b, "\\n");  continue; }
        if (ch == '\t') { buf_str(b, "\\t");  continue; }
        if (ch == '\r') { buf_str(b, "\\r");  continue; }
        if (ch == '?')  { buf_str(b, "\\?");  continue; }
        if (ch < 0x20 || ch >= 0x7f) {
            snprintf(tmp, sizeof tmp, "\\%03o", (unsigned)ch);
            buf_str(b, tmp);
            continue;
        }
        tmp[0] = (char)ch; tmp[1] = 0;
        buf_str(b, tmp);
    }
}

static void buf_fmt(Buf *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { buf_str(b, tmp); return; }
    char *big = malloc((size_t)n + 1);
    if (!big) return;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    buf_str(b, big);
    free(big);
}

/* -------------------------------------------------------------- reporting */

static char  g_err[512];
static char  g_detail[512];
static char  g_namedetail[256];
static unsigned char *g_gref;        /* per-global: some instruction touches it */
static int   g_gref_n;
static int   g_globals_renamed;

/* The `why` a prescan refusal carries.  One family ("illegal-operand") covers
   every operand this route cannot emit, but a refusal whose reason a reader
   can NAME is worth more than one family name, and the criterion needs to
   tell them apart: a program refused because a constant subscript is outside
   a length this route can prove is refused for a different reason than one
   whose pool index is out of range.  Every check resets it to the family name
   except the ones that set it themselves, so a new check cannot inherit
   another check's why by accident. */
static const char *g_prescan_why = "illegal-operand";

/* The compiler registers these names into the global table before it has seen
   any source (src/compiler/compiler.c:2894-2901, "must match vm.c vm_init
   order").  They hold set objects.  MirVal has the payload fields a set needs
   (sval/ptr) but no emitted opcode builds one yet, so a global seeded here
   would read as a zeroed payload -- a program that touches one is outside this
   subset and is refused by name rather than lowered to an integer that would
   be quietly wrong. */
static const char *const kPresetGlobals[23] = {
    "N","Z","Z+","Z-",
    "Float1","float1","Float2","float2","Float3","float3","Float4","float4",
    "Float5","float5","Float6","float6","Float7","float7","Float8","float8","Float9","float9",
    "\xBF\xD5"
};

static int is_preset_global(const char *name) {
    int n = (int)(sizeof kPresetGlobals / sizeof kPresetGlobals[0]);
    for (int i = 0; i < n; i++) if (name && strcmp(name, kPresetGlobals[i]) == 0) return 1;
    return 0;
}
static int   g_insns, g_emitted, g_notyet, g_funcs, g_regs_max;
static size_t g_body_bytes;
static int   g_notyet_by_op[256];
static int   g_used_by_op[256];

static void set_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

static int bad(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_detail, sizeof g_detail, fmt, ap);
    va_end(ap);
    return -1;
}

static void refuse(const char *why, const char *detail) {
    set_err("%s: %s", why, detail ? detail : "-");
    printf("IR-MIR-REFUSED|why=%s|detail=%s" NL, why, detail ? detail : "-");
}

static const char *op_name(int op);

static void print_not_yet(void) {
    for (int op = 0; op < 256; op++)
        if (g_notyet_by_op[op])
            printf("IR-MIR-NOT-YET|op=%s|n=%d" NL, op >= 0 ? op_name(op) : "OP_UNKNOWN", g_notyet_by_op[op]);
}

static void print_conserve(void) {
    printf("IR-MIR-CONSERVE|insns=%d|emitted=%d|not-yet=%d"
           "|equation=emitted+not-yet=insns|holds=%s|bytes=%lu" NL,
           g_insns, g_emitted, g_notyet,
           (g_emitted + g_notyet == g_insns) ? "yes" : "no",
           (unsigned long)g_body_bytes);
}

/* --------------------------------------------------------------- opcodes */

/* This file used to carry its own 67-entry copy of the opcode strings, identical
   to the VM's table string for string and asserted by nothing.  The names now
   come from the VM, which is the only place a compile-time assert ties them to
   the enum; the out-of-enum answer stays "OP_UNKNOWN" here because every call
   site in this file is describing an instruction it already read. */
static const char *op_name(int op) {
    if (op >= 0 && op <= 255) {
        const char *n = vm_opcode_name((unsigned)op);
        if (n && n[0] == 'O' && n[1] == 'P') return n;
    }
    return "OP_UNKNOWN";
}

/* Which of r1/r2/r3 name registers, read one dispatch body at a time from
   src/vm/vm.c.  R1 is the destination for every opcode here; the exceptions
   are named explicitly (OP_STORE_GLOBAL's r1 is a global index, OP_CALL_FUNC's
   r1 is a function index, OP_LOAD_GLOBAL's r2 and OP_JUMP's r2 are indices and
   targets, OP_LOADK_STRING's r2 is a string-pool index).  Returns -1 for an
   opcode this generator does not emit. */
enum { R1 = 1, R2 = 2, R3 = 4 };

static int op_regs(int op) {
    switch (op) {
    case OP_MOV: return R1 | R2;
    case OP_LOADK_INT: case OP_LOADK_FLOAT: case OP_LOADK_BOOL: case OP_LOADK_I64: return R1;
    case OP_LOADK_STRING: return R1;
    case OP_EQK: case OP_NEQK:
        /* r3 is a STRING POOL INDEX, not a register -- the same split
           OP_LOADK_STRING has, where r2 is a pool index and not a register.
           Reading it as a register bound would cap the pool at REG_LIMIT (and
           would silently accept a pool index that happens to be small); not
           validating it at all would hand generated C an out-of-range
           subscript.  So only r1/r2 are registers here and the pool bound is
           checked in prescan, next to OP_LOADK_STRING's own bound. */
        return R1 | R2;
    /* OP_NEW_ARRAY's operand layout, READ OUT of the handler rather than
       guessed: r1 is the register the array value lands in, r3 is the element
       COUNT (a NUMBER, not a register -- treating it as a register bound would
       accept a pool index as an array length), and r2 is not read at all
       (src/vm/vm.c:3620-3647 reads ins.r1 and ins.r3 only; the compiler
       hard-codes the 0 operand: src/compiler/compiler.c:1047 emits
       `OP_NEW_ARRAY, r, 0, expr->list.count`).  The ELEMENTS are not registers
       of this instruction either: the compiler pushes each one
       (src/compiler/compiler.c:1042-1045) and only the count survives here,
       which is why the interpreter reads them off its own argument stack. */
    case OP_NEW_ARRAY: return R1;
    /* OP_INDEX_GET reads r2 (the container) and r3 (the subscript) and writes
       r1: `R[ins.r1] = ...`, src/vm/vm.c:3675-3716, where the handler's own
       first two lines are `Value *obj = &R[ins.r2]; Value *idxv = &R[ins.r3];`.
       The compiler emits `emit(OP_INDEX_GET, r, arr, idx)`
       (src/compiler/compiler.c:1067-1068) with idx the register the subscript
       was compiled into, and it is r3 that the handler reads; bytecode.h:15
       spells the same three operands in one line of comment.  So r3 is a real
       register bound here even though that comment's bracket writes r1. */
    case OP_INDEX_GET: return R1 | R2 | R3;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
    case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
    case OP_AND: case OP_OR: return R1 | R2 | R3;
    case OP_CONCAT: return R1 | R2;   /* r3 is an OPERAND COUNT, not a register:
                                         vm.c L_CONCAT reads `int first = ins.r2,
                                         count = ins.r3`, so the operands are the
                                         contiguous registers r2..r2+r3-1.  The
                                         count is deliberately OUT of this mask,
                                         or the generic bound loop below would
                                         check the COUNT against REG_LIMIT while
                                         the LAST operand went unchecked; that
                                         bound is prescan's case and
                                         container_nregs' widening, one place
                                         each. */
    case OP_NEG: case OP_NOT: return R1 | R2;
    case OP_IS_NIL: return R1 | R2;   /* reads r2, writes the BOOL in r1 */
    case OP_LOAD_GLOBAL: return R1;
    case OP_STORE_GLOBAL: return R2;
    case OP_JUMP: return 0;
    case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: return R1;
    case OP_PUSH_REG: return R1;
    case OP_CALL_FUNC: return R2;
    case OP_RETURN: return R1;
    case OP_SAY: return R1;
    case OP_HALT: return 0;
    default: return -1;
    }
}

const char *aot_mir_op_name(int opcode) { return op_name(opcode); }

int aot_mir_emits(int opcode) { return op_regs(opcode) >= 0; }

int aot_mir_emitted_count(void) {
    int n = 0;
    for (int op = 0; op < 256; op++) if (op_regs(op) >= 0) n++;
    return n;
}

int aot_mir_emitted_opcode(int i) {
    int k = 0;
    for (int op = 0; op < 256; op++) {
        if (op_regs(op) < 0) continue;
        if (k == i) return op;
        k++;
    }
    return -1;
}

const char *aot_mir_subset_description(void) {
    static char desc[512];
    if (!desc[0]) {
        int o = 0;
        for (int op = 0; op < 256; op++) {
            if (op_regs(op) < 0) continue;
            int n = snprintf(desc + o, sizeof desc - (size_t)o, "%s%s", o ? "," : "", op_name(op));
            if (n < 0) break;
            o += n;
        }
    }
    return desc;
}

/* ------------------------------------------------------ containers and ids */

/* One IrFunc, paired with the Bytecode container it came from and the facts
   that live only in the parent (name, argument count).  mir.c walks
   self -> funcs -> threads and numbers the module in that order; this walk
   reproduces it, and the caller checks the two counts agree. */
typedef struct {
    const Bytecode *node;
    const char     *name;
    int             argc;
    int             is_main;
    int            *child_ids;
    int             nchild;
} Node;

static int build_nodes(const Bytecode *bc, const char *name, int argc,
                       int is_main, Node *out, int *n, int cap) {
    if (!bc || *n >= cap) return -1;
    int id = (*n)++;
    out[id].node = bc;
    out[id].name = name;
    out[id].argc = argc;
    out[id].is_main = is_main;
    out[id].child_ids = NULL;
    out[id].nchild = 0;
    if (bc->thread_count > 0) return -2;   /* threads are outside this route */
    if (bc->func_count < 0) return -1;
    if (bc->func_count > 0) {
        out[id].child_ids = malloc((size_t)bc->func_count * sizeof(int));
        if (!out[id].child_ids) return -1;
    }
    for (int i = 0; i < bc->func_count; i++) {
        out[id].child_ids[i] = *n;
        int rc = build_nodes(bc->funcs[i], bc->func_names ? bc->func_names[i] : NULL,
                             bc->func_argc ? bc->func_argc[i] : 0, 0, out, n, cap);
        if (rc != 0) return rc;
    }
    out[id].nchild = bc->func_count;
    return 0;
}

/* A C identifier for a source-level name, or NULL when nothing legal can be
   spelled.  Every byte outside [A-Za-z0-9_] becomes an underscore; the prefix
   makes the first character legal.  Two names that collide after this mapping
   cannot both exist in one translation unit, and the caller refuses. */
static char *c_name(const char *prefix, const char *src) {
    if (!src || !src[0]) return NULL;
    char tmp[512];
    size_t o = 0;
    for (size_t i = 0; src[i]; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (o + 1 >= sizeof tmp) return NULL;
        tmp[o++] = ((ch == '_') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                       ? (char)ch : '_';
    }
    tmp[o] = 0;
    size_t plen = strlen(prefix);
    char *res = malloc(plen + o + 1);
    if (!res) return NULL;
    memcpy(res, prefix, plen);
    memcpy(res + plen, tmp, o + 1);
    return res;
}

static void free_names(char **v, int n) {
    if (!v) return;
    for (int i = 0; i < n; i++) free(v[i]);
    free(v);
}

static char **build_global_names(const Bytecode *root) {
    int n = root->global_name_count;
    if (n < 0) { snprintf(g_namedetail, sizeof g_namedetail, "global_name_count=%d", n); return NULL; }
    char **out = calloc((size_t)n + 1, sizeof(char *));
    if (!out) return NULL;
    /* Only the globals some instruction actually names get a C identifier.
       The rest -- the 23 preset slots above all -- are not declared and not
       named, so a table nobody reads cannot make a name collide. */
    for (int i = 0; i < n; i++) {
        if (!g_gref || !g_gref[i]) continue;
        const char *src = root->global_names ? root->global_names[i] : NULL;
        out[i] = c_name("g_", src);
        if (!out[i]) {
            snprintf(g_namedetail, sizeof g_namedetail, "index %d has name '%s', which no C identifier can be made from", i, src ? src : "(null)");
            free_names(out, n);
            return NULL;
        }
    }
    /* Two source names that sanitize to one identifier: every member of the
       group takes its own index, so the mapping stays a function of
       (name, index) and stays deterministic. */
    for (int i = 0; i < n; i++) {
        if (!out[i]) continue;
        int collide = 0;
        for (int j = 0; j < n; j++)
            if (j != i && out[j] && strcmp(out[i], out[j]) == 0) { collide = 1; break; }
        if (collide) {
            char tmp[600];
            snprintf(tmp, sizeof tmp, "%s_%d", out[i], i);
            free(out[i]);
            out[i] = strdup(tmp);
            g_globals_renamed++;
        }
    }
    for (int i = 0; i < n; i++) {           /* the rename must have ended it */
        if (!out[i]) continue;
        for (int j = i + 1; j < n; j++) {
            if (!out[j] || strcmp(out[i], out[j]) != 0) continue;
            snprintf(g_namedetail, sizeof g_namedetail, "index %d and index %d still both become '%s'", i, j, out[i]);
            free_names(out, n);
            return NULL;
        }
    }
    return out;
}

static char **build_func_names(const Node *nodes, int n) {
    if (n <= 0) return NULL;
    char **out = calloc((size_t)n + 1, sizeof(char *));
    if (!out) return NULL;
    for (int i = 0; i < n; i++) {
        if (nodes[i].is_main) {
            out[i] = malloc(5);
            if (out[i]) memcpy(out[i], "main", 5);
        } else {
            out[i] = c_name("fn_", nodes[i].name);
        }
        if (!out[i]) {
            snprintf(g_namedetail, sizeof g_namedetail, "container %d has name '%s', which no C identifier can be made from",
                     i, nodes[i].name ? nodes[i].name : "(null)");
            free_names(out, n);
            return NULL;
        }
    }
    for (int i = 1; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (strcmp(out[i], out[j]) != 0) continue;
            snprintf(g_namedetail, sizeof g_namedetail, "container %d and container %d both become '%s'",
                     i, j, out[i]);
            free_names(out, n);
            return NULL;
        }
    }
    return out;
}

/* --------------------------------------------------- provable subscripts */

/* What this route can PROVE about one register in a straight line of
   instructions, and the only two users of it: the length of an array a
   register holds, and the integer value a register holds.  Kept per container,
   cleared at every JOIN POINT -- a block label and every jump target -- because
   a label can be reached with two different values in the same register, and
   knowledge that survived a join would be a lie the refusal below acts on.
   File scope and static: REG_LIMIT is a compile-time bound here, so a table
   that is never allocated is a table that cannot leak on the refusal paths
   this file takes; the global half is bounded by KS_GLOBALS just below, and
   its soundness rule (a call clears it) lives there too. */
static unsigned char ks_klen_ok[REG_LIMIT];
static int           ks_klen_val[REG_LIMIT];
static unsigned char ks_kint_ok[REG_LIMIT];
static long long     ks_kint_val[REG_LIMIT];
/* The same two facts, keyed by GLOBAL id instead of by register, because a
   constant that travels through `a = [1, 2, 3]` and is read back as `a[3]` is
   just as provable as the literal-in-place spelling and is the shape a reader
   actually writes.  Soundness is the whole difficulty: a global's contents can
   be rewritten by any call, so every OP_CALL_FUNC CLEARS this table (a call
   this pass cannot see into must invalidate exactly what it could touch), and
   an OP_STORE_GLOBAL overwrites the entry with the knowledge of its source
   register -- unknown source, unknown entry.  What survives is only what one
   straight line between two such events proves, which is the same claim the
   register tables make.  Bounded by KS_GLOBALS: an id beyond it is simply not
   tracked, which can only make this pass refuse LESS, never wrongly refuse. */
#define KS_GLOBALS 4096
static unsigned char ks_gklen_ok[KS_GLOBALS];
static int           ks_gklen_val[KS_GLOBALS];

static void ks_reset(void) {
    memset(ks_klen_ok, 0, sizeof ks_klen_ok);
    memset(ks_klen_val, 0, sizeof ks_klen_val);
    memset(ks_kint_ok, 0, sizeof ks_kint_ok);
    memset(ks_kint_val, 0, sizeof ks_kint_val);
    memset(ks_gklen_ok, 0, sizeof ks_gklen_ok);
    memset(ks_gklen_val, 0, sizeof ks_gklen_val);
}

/* A call may store any global, so it forgets every array length known through
   one.  Register knowledge is untouched: a callee cannot write the caller's
   registers, only globals. */
static void ks_globals_clobbered(void) {
    memset(ks_gklen_ok, 0, sizeof ks_gklen_ok);
    memset(ks_gklen_val, 0, sizeof ks_gklen_val);
}

/* The ONE refusal this route makes before it generates anything, about a
   subscript: a CONSTANT index outside a length the same straight line proves.
   `say [7, 8, 9][3]` -- or the same program spelled `a = [1, 2, 3]` then
   `say a[3]`, which the global table above makes just as provable -- cannot
   read anything the interpreter's answer (nil) would
   not also be, and the interpreter DOES answer it -- so why refuse instead of
   generating a read that mv_arr_get answers nil?  Because the criterion for
   this step is "a bound a reader can see is impossible is refused before
   generation" (the same shape OP_LOADK_STRING's pool bound has), and because
   a constant subscript is exactly the case a reader can see it in.  What is
   NOT refused is anything this pass cannot prove: a variable subscript is
   answered at run time with the interpreter's rc (mv_arr_get), because a
   refusal there would hide an answer that exists.
   The negative half is the same rule: an ARRAY does not wrap a negative
   subscript (src/vm/vm.c:1002 answers nil for i < 0), so a constant -1 is
   just as provably outside as a constant 3 is.  A STRING container does wrap
   (src/vm/vm.c:3705 `if (i < 0) i = len + i;`), and this check cannot see a
   string length at all -- only array lengths are tracked -- so a constant
   negative subscript on a string is left to the runtime, which answers it
   correctly.
   Returns 0 or -1 (g_detail and g_prescan_why say which). */
static int prescan_constant_subscripts(const IrFunc *fn, int f) {
    int n = fn->ninsns;
    if (n <= 0) return 0;
    unsigned char *join = calloc((size_t)n + 1, 1);
    if (!join) return bad("container=%d: out of memory for the join table", f);
    join[0] = 1;
    for (int b = 0; b < fn->nblocks; b++) {
        int s = fn->blocks[b].start;
        if (s >= 0 && s <= n) join[s] = 1;
    }
    for (int i = 0; i < n; i++) {
        int op = fn->insns[i].op;
        if (op != OP_JUMP && op != OP_JUMP_IF_FALSE && op != OP_JUMP_IF_TRUE) continue;
        int32_t t = ir_insn_r2(&fn->insns[i]);
        if (t >= 0 && t <= n) join[t] = 1;
    }
    ks_reset();
    for (int i = 0; i < n; i++) {
        const IrInsn *in = &fn->insns[i];
        int op = in->op;
        int mask = op_regs(op);
        if (join[i]) ks_reset();
        if (mask < 0) continue;
        if ((mask & R1) && in->dst >= 0 && in->dst < REG_LIMIT) {
            /* Any store to a register ends what we knew about its contents.
               Invalidation comes BEFORE the arms below, so the arms' own
               `set` calls are the last word about the register they write. */
            ks_klen_ok[in->dst] = 0;
            ks_kint_ok[in->dst] = 0;
        }
        switch (op) {
        case OP_LOADK_INT:
            ks_kint_ok[in->dst] = 1;
            ks_kint_val[in->dst] = ir_insn_r2(in);
            break;
        case OP_LOADK_I64: {
            long long v = (long long)((unsigned long long)(unsigned int)ir_insn_r2(in) |
                                      ((unsigned long long)(unsigned int)ir_insn_r3(in) << 32));
            ks_kint_ok[in->dst] = 1;
            ks_kint_val[in->dst] = v;
            break;
        }
        case OP_NEW_ARRAY: {
            /* The count is r3 (op_regs' note).  A non-negative count is already
               guaranteed by the operand pass; the length is what this pass
               needs next to the subscript. */
            ks_klen_ok[in->dst] = 1;
            ks_klen_val[in->dst] = (int)ir_insn_r3(in);
            break;
        }
        case OP_STORE_GLOBAL: {
            /* r1 is the GLOBAL id, r2 the source register (op_regs returns R2
               for this op for exactly that reason, and the emitter writes
               `gnames[r1] = v[r2]`).  The entry takes the source's knowledge
               verbatim; unknown source, unknown entry. */
            int gid = in->dst, src = in->a;
            if (gid >= 0 && gid < KS_GLOBALS) {
                int known = (src >= 0 && src < REG_LIMIT) ? ks_klen_ok[src] : 0;
                ks_gklen_ok[gid] = (unsigned char)known;
                ks_gklen_val[gid] = known ? ks_klen_val[src] : 0;
            }
            break;
        }
        case OP_LOAD_GLOBAL: {
            /* The register half of the same fact: -1 is the unresolved global
               the emitter spells out as nil, and it knows nothing. */
            int gid = in->a, dst = in->dst;
            if (dst >= 0 && dst < REG_LIMIT && gid >= 0 && gid < KS_GLOBALS && ks_gklen_ok[gid]) {
                ks_klen_ok[dst] = 1;
                ks_klen_val[dst] = ks_gklen_val[gid];
            }
            break;
        }
        case OP_CALL_FUNC:
            ks_globals_clobbered();
            break;
        case OP_INDEX_GET: {
            int rcont = in->a, ridx = in->b;   /* r2 = container, r3 = subscript */
            if (rcont < 0 || rcont >= REG_LIMIT || ridx < 0 || ridx >= REG_LIMIT) break;
            if (!ks_klen_ok[rcont] || !ks_kint_ok[ridx]) break;
            long long k = ks_kint_val[ridx];
            int len = ks_klen_val[rcont];
            if (k < 0 || k >= len) {
                free(join);
                g_prescan_why = "index-out-of-range-constant";
                return bad("container=%d insn=%d constant subscript %lld is outside the %d element(s) "
                           "of the array built into register %d",
                           f, i, k, len, rcont);
            }
            break;
        }
        default:
            break;
        }
    }
    free(join);
    return 0;
}

/* --------------------------------------------------------------- pre-scan */

/* Two jobs, deliberately in one pass so they cannot disagree:
 *   - count every instruction the module claims (g_insns) and every one that
 *     has no emitters (g_notyet), which is one half of the conservation line;
 *   - check every operand the emitter will index with, because the emitter
 *     must not have to guess.  A bad operand is a refusal with a detail, not a
 *     silent the-array-is-big-enough.
 * Returns the number of distinct un-emittable opcodes, or -1 for a bad
 * operand (g_detail says which). */
static int prescan(const IrModule *m, const Node *nodes, const Bytecode *root,
                   char **gnames, char **fnnames) {
    int kinds = 0;
    (void)gnames; (void)fnnames;
    for (int f = 0; f < m->nfuncs; f++) {
        const IrFunc *fn = &m->funcs[f];
        const Bytecode *node = nodes[f].node;
        int asp = 0;
        g_insns += fn->ninsns;
        for (int i = 0; i < fn->ninsns; i++) {
            const IrInsn *in = &fn->insns[i];
            int op = in->op;
            int mask = op_regs(op);
            if (mask < 0) {
                g_notyet++;
                if (op >= 0 && op < 256) g_notyet_by_op[op]++;
                else g_notyet_by_op[0]++;
                continue;
            }
            if ((mask & R1) && (in->dst < 0 || in->dst >= REG_LIMIT))
                return bad("container=%d insn=%d op=%s register r1=%d out of range", f, i, op_name(op), in->dst);
            if ((mask & R2) && (in->a < 0 || in->a >= REG_LIMIT))
                return bad("container=%d insn=%d op=%s register r2=%d out of range", f, i, op_name(op), in->a);
            if ((mask & R3) && (in->b < 0 || in->b >= REG_LIMIT))
                return bad("container=%d insn=%d op=%s register r3=%d out of range", f, i, op_name(op), in->b);
            /* OP_CONCAT is checked here, OUTSIDE the switch, on purpose: its
               third operand is an OPERAND COUNT and not a register, so it is not
               one of the opcodes this switch exists to bound (a pool / global /
               function / label index).  The operands instrument reads this
               switch as "the opcodes that read an index", and spelling a count
               check as a case label there would make it report a dead case for
               an opcode that never reads an index. */
            if (op == OP_CONCAT) {
                int nops = in->b;
                if (nops < 0)
                    return bad("container=%d insn=%d op=%s operand count %d is negative",
                               f, i, op_name(op), nops);
                if (nops > 0 && in->a + nops - 1 >= REG_LIMIT)
                    return bad("container=%d insn=%d op=%s last operand register %d out of range "
                               "(first=%d count=%d)", f, i, op_name(op), in->a + nops - 1, in->a, nops);
            }
            /* The container pair is checked HERE and not in the switch below, and
               that placement is itself a claim worth stating: every case label
               in this switch exists because its emitter reads a POOL, GLOBAL,
               FUNCTION or LABEL index, and the coverage test's operand detector
               (tools/aot_mir_cover.test.py:288-327) reads the switch's labels
               against the emitter's indexed arms in both directions.  Neither
               of these two opcodes reads such an index -- OP_NEW_ARRAY's r3 is
               an ELEMENT COUNT (op_regs' note; the emitter turns it into a
               stack offset) and OP_INDEX_GET's three operands are plain
               registers -- so a case label here would be a label the detector
               calls dead, which is a true reading of the wrong question.  The
               bounds they do need are the arm's own: the count versus the
               pool's register bound, and the count versus the argument stack.
               OP_NEW_ARRAY is the argument stack's consumer, and the one that
               makes OP_PUSH_REG a CONSUMED operand rather than a leak: the
               elements were pushed one at a time by the compiler
               (src/compiler/compiler.c:1042-1045, `emit(OP_PUSH_REG, ...)` per
               element) and the count in r3 says how many of them this
               instruction takes (src/vm/vm.c:3637-3642 reads them back as
               `base = t->sp - (n - 1)`, so the FIRST push is element 0).
               Bound first, then consume -- an n larger than asp is a refusal,
               not a negative stack. */
            if (op == OP_NEW_ARRAY) {
                int n = ir_insn_r3(in);
                if (n < 0 || n >= REG_LIMIT)
                    return bad("container=%d insn=%d element count %d out of range", f, i, n);
                if (asp < n)
                    return bad("container=%d insn=%d argument stack underflows: asp=%d elements=%d",
                               f, i, asp, n);
                asp -= n;
                continue;
            }
            /* OP_INDEX_GET has nothing to check in this pass: its operands are
               registers (bounded above), and the one thing a reader can prove
               about them -- a CONSTANT subscript outside a length the same
               straight line proves -- is checked by prescan_constant_subscripts
               below, which needs this container's whole instruction list.  The
               RUNTIME half is mv_arr_get's, deliberately: the interpreter
               answers an out-of-range read (nil) and a non-numeric subscript
               (type_mismatch, rc 1), so refusing those here would hide answers
               that exist. */
            if (op == OP_INDEX_GET) continue;
            switch (op) {
            case OP_LOADK_FLOAT: {
                int32_t ix = ir_insn_r2(in);
                if (ix < 0 || ix >= node->float_count)
                    return bad("container=%d insn=%d float pool index %d out of range (count=%d)",
                               f, i, ix, node->float_count);
                break;
            }
            case OP_LOADK_STRING: {
                /* r2 indexes the CONTAINER's string pool, exactly as
                   OP_LOADK_FLOAT's r2 indexes its float pool: the emitter reads
                   `node->string_pool`, so the bound has to be `node`, not the
                   module's root container.  A NULL slot is an operand the
                   emitter cannot turn into a literal, so it is a refusal with a
                   detail rather than a jump into the emitter. */
                int32_t ix = ir_insn_r2(in);
                if (ix < 0 || ix >= node->string_count)
                    return bad("container=%d insn=%d string pool index %d out of range (count=%d)",
                               f, i, ix, node->string_count);
                if (!node->string_pool || !node->string_pool[ix])
                    return bad("container=%d insn=%d string pool entry %d is NULL",
                               f, i, ix);
                break;
            }
            case OP_EQK:
            case OP_NEQK: {
                /* r3 indexes the CONTAINER's STRING POOL -- the operand that
                   carries the constant -- exactly as OP_LOADK_STRING's r2 does,
                   so the bound is `node->string_count` and not REG_LIMIT, and
                   it is `node` (the container this instruction lives in) and
                   not the module's root: a function body has its own pool.  A
                   NULL slot has no literal to emit, so it is refused with a
                   detail here rather than reached inside the emitter.  This is
                   the check the cover test's operand detector insists on: an
                   emitter case that reads a pool without a prescan case lets an
                   out-of-range index through into generated C. */
                int32_t ix = ir_insn_r3(in);
                if (ix < 0 || ix >= node->string_count)
                    return bad("container=%d insn=%d string pool index %d out of range (count=%d)",
                               f, i, ix, node->string_count);
                if (!node->string_pool || !node->string_pool[ix])
                    return bad("container=%d insn=%d string pool entry %d is NULL",
                               f, i, ix);
                break;
            }
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL: {
                int ix = (op == OP_LOAD_GLOBAL) ? ir_insn_r2(in) : in->dst;
                /* Index -1 is not an out-of-range operand: it is the compiler's
                   spelling of an UNRESOLVED global, and the interpreter answers
                   it with VAL_NIL (src/vm/vm.c's L_LOAD_GLOBAL: `if (idx < 0)
                   value_set(..., VAL_NIL, ...)`).  It used to be refused here,
                   which turned the difference between nil and the integer 0 the
                   generated globals were initialised with into an invisible
                   refusal -- the program never even reached the emitter.
                   Accepting it is one instruction: the emitter writes mv_nil()
                   and NO name, so there is no gnames[] lookup and no g_gref
                   entry to miss.  Only OP_LOAD_GLOBAL takes it: a STORE needs a
                   global to store into, and this route has no table to create
                   one in, so a negative index there stays a refusal. */
                if (op == OP_LOAD_GLOBAL && ix == -1) break;
                if (ix < 0 || ix >= root->global_name_count || ix >= g_gref_n)
                    return bad("container=%d insn=%d global index %d out of range (count=%d)",
                               f, i, ix, root->global_name_count);
                const char *gname = root->global_names ? root->global_names[ix] : NULL;
                if (is_preset_global(gname))
                    return bad("container=%d insn=%d global %d is the preset set global '%s', whose value is a set, "
                               "not an NV int/float/bool -- outside this subset",
                               f, i, ix, gname);
                g_gref[ix] = 1;
                break;
            }
            case OP_CALL_FUNC: {
                int fidx = in->dst;
                int argc = ir_insn_r3(in);
                if (fidx < 0 || fidx >= root->func_count)
                    return bad("container=%d insn=%d function index %d out of range (count=%d)",
                               f, i, fidx, root->func_count);
                if (argc < 0 || argc >= REG_LIMIT)
                    return bad("container=%d insn=%d argc %d out of range", f, i, argc);
                if (asp < argc)
                    return bad("container=%d insn=%d argument stack underflows: asp=%d argc=%d",
                               f, i, asp, argc);
                asp -= argc;
                break;
            }
            case OP_PUSH_REG:
                asp++;
                break;
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: {
                int32_t t = ir_insn_r2(in);
                if (t < 0 || t > fn->ninsns)
                    return bad("container=%d insn=%d jump target %d out of range (ninsns=%d)",
                               f, i, t, fn->ninsns);
                break;
            }
            default:
                break;
            }
        }
        if (asp != 0)
            return bad("container=%d leaves the argument stack unbalanced: asp=%d", f, asp);
        /* Runs after the operand pass so that a pool bound or a register bound
           is still reported first: the readings the existing corpus pinned
           must not move because a new check took the earlier turn. */
        if (prescan_constant_subscripts(fn, f) != 0)
            return -1;
    }
    for (int op = 0; op < 256; op++) if (g_notyet_by_op[op]) kinds++;
    return kinds;
}

/* --------------------------------------------------------------- emitter */

typedef struct {
    const IrModule *m;
    const Bytecode *root;
    const Bytecode *node;
    const Node     *nodes;
    char          **gnames;
    char          **fnnames;
    int             id;
} Ctx;

static int container_nregs(const IrFunc *fn, int argc) {
    int maxr = 0;
    for (int i = 0; i < fn->ninsns; i++) {
        const IrInsn *in = &fn->insns[i];
        int mask = op_regs(in->op);
        if (mask < 0) continue;
        if ((mask & R1) && in->dst > maxr) maxr = in->dst;
        if ((mask & R2) && in->a > maxr) maxr = in->a;
        /* OP_CONCAT's r3 is a count, so its operand span reaches past r2: the
           generated line reads values from `&v[r2]` upward.  Without this, a
           container whose highest register is a chain's LAST operand would size
           its array from the first one, and the copy would walk off the end. */
        if (in->op == OP_CONCAT && (mask & R2)) {
            int last = in->a + (in->b > 0 ? in->b - 1 : 0);
            if (last > maxr) maxr = last;
        }
        if ((mask & R3) && in->b > maxr) maxr = in->b;
    }
    int n = maxr + 1;
    if (n < argc + 1) n = argc + 1;   /* parameters live in registers 1..argc */
    if (n < 1) n = 1;
    return n;
}

static int emit_one(Ctx *c, Buf *b, int i) {
    const IrInsn *in = &c->m->funcs[c->id].insns[i];
    int op = in->op;
    int r1 = in->dst, r2 = in->a, r3 = in->b;
    int32_t w2 = ir_insn_r2(in), w3 = ir_insn_r3(in);
    switch (op) {
    case OP_MOV:          buf_fmt(b, "    v[%d] = v[%d];", r1, r2); return 0;
    case OP_LOADK_INT:    buf_fmt(b, "    v[%d] = mv_int(%dLL);", r1, w2); return 0;
    case OP_LOADK_I64: {
        long long v = (long long)((unsigned long long)(unsigned int)w2 |
                                  ((unsigned long long)(unsigned int)w3 << 32));
        buf_fmt(b, "    v[%d] = mv_int(%lldLL);", r1, v);
        return 0;
    }
    case OP_LOADK_BOOL:   buf_fmt(b, "    v[%d] = mv_boo(%d);", r1, w2 ? 1 : 0); return 0;
    case OP_LOADK_FLOAT:  buf_fmt(b, "    v[%d] = mv_flt(%.17g);", r1, c->node->float_pool[w2]); return 0;
    case OP_LOADK_STRING: {
        /* The pool string is emitted INLINE as a C string literal: the literal's
           array has static storage duration, so mv_str's resident marker is
           honest and there is no allocation and no copy.  buf_c_string writes
           the BODY only (escapes and no quotes), so the byte string the
           interpreter interned and this file carries are the same bytes -- a
           tab is a tab, not the two characters `\` `t`. */
        buf_fmt(b, "    v[%d] = mv_str(\"", r1);
        buf_c_string(b, c->node->string_pool[w2]);
        buf_str(b, "\");");
        return 0;
    }
    case OP_ADD:          buf_fmt(b, "    v[%d] = mv_add(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_CONCAT: {
        /* A chain: r2 is the first operand and r3 the count, NOT a third
           register (see op_regs).  `&v[r2]` plus the count go to mv_concat_n,
           which owns the interpreter's L_CONCAT semantics -- one allocation for
           an all-string chain.  Emitting `v[r1] = mv_add(v[r2], v[r2+1])` in a
           loop here would be exactly the N-1 concatenations this cell exists to
           delete, and the ALLOCS reading would be the only thing that noticed.
           r1 is written ONCE, after the fold, as value_set(&R[rres], ...) writes
           it there, so a chain whose destination aliases one of its own operands
           (`s + s + s` into v[first]) is safe: every operand is read before the
           assignment. */
        if (r3 < 0) return bad("container=%d insn=%d OP_CONCAT with negative operand count %d",
                               c->id, i, r3);
        buf_fmt(b, "    v[%d] = mv_concat_n(&v[%d], %d);", r1, r2, r3);
        return 0;
    }
    case OP_SUB:          buf_fmt(b, "    v[%d] = mv_sub(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_MUL:          buf_fmt(b, "    v[%d] = mv_mul(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_DIV:          buf_fmt(b, "    v[%d] = mv_div(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_MOD:          buf_fmt(b, "    v[%d] = mv_mod(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_EQ:           buf_fmt(b, "    v[%d] = mv_eq(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_NEQ:          buf_fmt(b, "    v[%d] = mv_ne(v[%d], v[%d]);", r1, r2, r3); return 0;
    /* THE CONSTANT SPELLING OF STRING EQUALITY -- the asymmetry docs/STATUS.md
     * 10.105 RECORDED ("先记不顺手修") and this arm closes.  Same five-part
     * structure as the mv_eq note in kMirValBlock: WHY IT EXISTS / WHAT IT ADDS
     * / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.
     *
     * WHY IT EXISTS.  The compiler does not keep `s == "hi"` as "compare s with
     * a string": it lowers the CONSTANT into the instruction.  In
     * src/compiler/compiler.c:700-719 it picks the string literal as `lit` and
     * the other side as `other`, compiles `other` into a register, adds the
     * literal to the container's string pool and emits OP_EQK/OP_NEQK with
     * r1=result, r2=the other side's register, r3=the pool index.  Both
     * spellings therefore become ONE stream (measured: `s == "hi"` and
     * `"hi" == s` dump `3,1,0,0 / 23,0,1,0 / 22,1,0,0 / 46,2,1,1 / 30,2,0,0 /
     * 33,0,0,0` byte for byte -- r2 and r3 are the two OPERAND ROLES, and the
     * question "which spelling" has already been erased by then).  Before this
     * arm op_regs answered -1 for OP_EQK and OP_NEQK, so the whole program was
     * REFUSED with `unsupported-opcode` (`IR-MIR-NOT-YET|op=OP_EQK|n=1`,
     * `IR-AOT-MIR-REFUSED|why=unsupported-opcode: see IR-MIR-NOT-YET|n=6`)
     * while the variable spelling `s == t` -- OP_EQ, one opcode over -- was
     * accepted and answered `true`.  The refusal was honest and that is not
     * the problem: the problem is that one QUESTION was refused or answered
     * depending on which opcode the compiler happened to choose for it, and
     * the interpreter had the answer all along.  A refusal that hides an
     * existing answer behind a compiler detail is the same defect as a wrong
     * answer, one exit code quieter.
     *
     * WHAT IT ADDS.  Two cases and NO second copy of the content rule: the
     * value comes from register r2, the constant from the container's string
     * pool at r3 emitted inline as a resident literal (exactly as
     * OP_LOADK_STRING emits its pool entry, buf_c_string writes the BODY only,
     * so a tab is a tab and not `\` `t`), and the comparison is DELEGATED to
     * mv_eq / mv_ne.  mv_eq's string arm already IS the interpreter's rule --
     * pointer fast path then strcmp, and "a string against any non-string is
     * false" -- so one owner answers the register form and the constant form,
     * and a rule that is ever wrong is wrong in one place.  The rules copied
     * from the interpreter, with the lines they were copied from:
     *   - operand ROLES: src/vm/vm.c:4159-4173 (L_EQK) builds `sv.type =
     *     VAL_STRING` and interns the container pool entry at `code->string_pool`
     *     index sidx (`vm_intern(vm, code->string_pool + sidx)` at :4164-4166), takes
     *     `Value opv = R[ins.r2]`, and stores
     *     `value_set(&R[ins.r1], VAL_BOOL, val_eq(&opv, &sv) ? 1 : 0, ...)`.
     *     src/vm/vm.c:4175-4189 (L_NEQK) is the same shape with `? 0 : 1`.  So
     *     r2 is the VALUE and r3 is the CONSTANT; they are not interchangeable
     *     names and this arm does not treat them as such.
     *   - the comparison itself: src/vm/vm.c:413 `bool val_eq(const Value *a,
     *     const Value *b)`, :414 the same-type gate, :415/:416 int/float,
     *     :417 `if (a->type == VAL_STRING) return (a->sval == b->sval) ? true :
     *     strcmp(a->sval ? a->sval : "", b->sval ? b->sval : "") == 0;`,
     *     :418 bool, :419 nil, :425-427 coercion for INT/FLOAT/BOOL pairs only,
     *     :428 `return false`.
     *   - `!=` is val_eq NEGATED (the interpreter spells that negation itself
     *     at :4175-4189), which is why this arm calls mv_ne(v, c) -- alias
     *     `!mv_tru(mv_eq(v, c))` -- rather than a strcmp with the opposite sign.
     * Zero allocation: the constant is a C string literal with static storage
     * duration, mv_str marks it resident (ival=1), so mv_eq takes the pointer
     * fast path when the value IS the same resident literal and strcmp
     * otherwise.  No copy, no intern, no new print exit, no new opcode.
     *
     * WHAT IT DOES NOT COVER -- each MEASURED on this tree, and each stays
     * refused or aborted rather than answered:
     *   - a NON-RESIDENT constant.  The pool entry is not a Value: it is bytes
     *     the interpreter interns at run time (src/vm/vm.c:4164-4166 vm_intern over
     *     the pool entry) and this route emits as a literal mv_str marks
     *     resident.  So `ival != 1` cannot originate here; if a future
     *     opcode puts a non-resident string into r2, mv_eq's impossible-state
     *     guard abort()s instead of answering -- deliberately not downgraded
     *     to false, because a silent answer for a value the route cannot have
     *     produced is how the next entry of this kind gets written.
     *   - a CONSTANT AGAINST A CONTAINER (`arr == "hi"`).  The compiler DOES
     *     emit OP_EQK for it -- the literal side is chosen regardless of the
     *     other side's type -- but the opcode that would build the array is
     *     outside this subset, so prescan refuses the program before this arm
     *     is reached.  MEASURED on `a = [1, 2]` / `say a == "hi"`:
     *     `IR-MIR-REFUSED|why=illegal-operand|detail=container=0 leaves the
     *     argument stack unbalanced: asp=2` and
     *     `IR-AOT-MIR-REFUSED|...|why=illegal-operand: ...|n=10`, exit 1, no
     *     partial answer.  The interpreter answers false (:428); this route
     *     refuses, which is honest about a capability it does not have.  It is
     *     not answered from this arm because NO emittable opcode builds a
     *     container tag: OP_NEW_ARRAY, OP_NEW_DICT and OP_NEW_SET are all
     *     absent from the lowering table (the same measurement that makes
     *     handles 34 and not 37).
     *   - FLOAT -0.0 / NaN as the compared value.  MEASURED: `say -0.0 == "hi"`
     *     / `say -0.0 != "hi"` answer false/true on BOTH routes, and so does
     *     `say (0.0/0.0) == "hi"` -- NaN IS reachable here (division by zero
     *     does not abort this route; the shared runtime's
     *     nv_die_division_by_zero is NOT on this path), and a float never
     *     coerces against a string (:414 gates the block and :425-427 lists
     *     only INT/FLOAT/BOOL pairs), so NaN against a string constant is
     *     false rather than the "false for everything" a NaN comparison would
     *     give on the numeric path.  This is a property of mv_eq's existing
     *     arm, not of this one, so the probe lives in .scratch and not in the
     *     corpus.
     *   - a NON-RESIDENT string VALUE (the compared side, r2).  MEASURED: the
     *     only opcode that would build one is string `+` (`s = "a" + "b"`), and
     *     that is outside this subset for a reason that PRE-DATES this arm --
     *     the generated program dies in mv_add, before any comparison:
     *     `say s` after `s = "a" + "b"` exits 134 (SIGABRT, empty stdout) on
     *     this tree, because mv_as_nv has no MV_STR arm.  So a non-resident
     *     string cannot reach r2 here; if a later opcode produces one, mv_eq's
     *     impossible-state guard abort()s instead of answering.
     *   - a POOL INDEX that is large.  MEASURED and it WORKS: a program with
     *     5000 string literals before the comparison puts the constant at pool
     *     index ~5001, i.e. past REG_LIMIT (4096) -- the index a register bound
     *     would refuse -- and the comparison still answers byte-identically.
     *     This is why r3 is bounded by `node->string_count` in prescan and not
     *     by the register check.
     *   - a constant in a NON-ROOT container.  MEASURED and it WORKS: `func
     *     f(a) { return a == "hi" }` lowers the OP_EQK of the function body
     *     against the FUNCTION's own pool (prescan's bound is
     *     `node->string_count`, not the root's, and the emitter reads
     *     `c->node->string_pool`) and answers identically to the interpreter.
     *     It has no corpus case because it would move readings unrelated to
     *     this question; the boundary is recorded here instead of implied.
     *   - a constant against a value read from an UNRESOLVED global
     *     (`nosuchglobal == "hi"`, L_LOAD_GLOBAL's idx == -1).  MEASURED and
     *     it WORKS: nil against a string is false, and `!=` is true, on both
     *     routes -- mv_eq's MV_NIL arm, unchanged by this lowering.
     *
     * DELETE CONDITION (decidable, not a wish): delete these two cases together
     * with mv_eq's string arm, in the commit that gives the shared NV a string
     * tag and moves string comparison out of the container-pool encoding (the
     * P3 step named in mv_eq's own note).  Then the compiler stops emitting
     * OP_EQK (`grep -c OP_EQK src/compiler/compiler.c` is 0) and the two
     * spellings take one path by construction.  Deleting them earlier puts the
     * asymmetry back exactly as it was: one spelling answered, the other
     * refused.
     *
     * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_eqk_string,
     * mir_eqk_string_ne and mir_eqk_string_cross compare stdout BYTES against
     * the interpreter; all three were red before this arm as
     * `IR-MIR-MIR-ONLY|case=...|mir=refused|why=unsupported-opcode`.  The
     * fourth, mir_eqk_number, is the necessary condition: numeric and boolean
     * constant comparison still lowers through OP_EQ and must not move --
     * it is what stops this arm from being written into the numeric path.  And
     * tools/aot_mir_cover.test.py counts OP_EQK/OP_NEQK into handles=34, and
     * because these cases read a pool its operand detector requires the prescan
     * case above: removing that check reddens the cover test rather than
     * silently letting an out-of-range pool index reach generated C. */
    case OP_EQK: {
        buf_fmt(b, "    v[%d] = mv_eq(v[%d], mv_str(\"", r1, r2);
        buf_c_string(b, c->node->string_pool[w3]);
        buf_str(b, "\"));");
        return 0;
    }
    case OP_NEQK: {
        buf_fmt(b, "    v[%d] = mv_ne(v[%d], mv_str(\"", r1, r2);
        buf_c_string(b, c->node->string_pool[w3]);
        buf_str(b, "\"));");
        return 0;
    }
    /* THE CONTAINER PAIR -- the first two opcodes on this route whose answer
     * lives in the heap instead of in a register, and the two that a reader
     * should be able to check against the interpreter's handlers line by line.
     *
     * WHY IT EXISTS.  Before this commit both opcodes were un-emittable, so
     * `say [1, 2, 3][1]` was refused whole.  Worse than un-emittable: the
     * elements arrive as OP_PUSH_REG (src/compiler/compiler.c:1042-1045) and
     * nothing consumed them, so prescan's balance check refused the program on
     * the argument stack before it ever reached "unsupported opcode" --
     * measured, `IR-MIR-MIR-ONLY|case=mir_container_new_array|mir=refused|`
     * `why=illegal-operand`, detail `container=0 leaves the argument stack
     * unbalanced: asp=3`, while the EMPTY literal (no pushes) was refused as
     * `why=unsupported-opcode (OP_NEW_ARRAY:1,OP_INDEX_GET:1)`.  One refusal
     * for a reason that named the wrong thing is exactly the shape this file's
     * notes exist to pin.
     *
     * WHAT IT ADDS.  OP_NEW_ARRAY consumes w3 slots of mvargs, in push order,
     * into mv_arr_new -- element 0 is the FIRST push, which is the interpreter's
     * order (`base = t->sp - (n - 1)`, src/vm/vm.c:3637-3642).  OP_INDEX_GET is
     * one call to mv_arr_get, which carries the interpreter's classification
     * (mv_index_i32) and both its container arms.  r2 is NOT the count: op_regs'
     * note records that r3 is, and that is why this arm reads w3 where a
     * register number would be expected.
     *
     * WHAT IT DOES NOT COVER.  OP_INDEX_SET, OP_NEW_DICT and OP_NEW_SET stay
     * un-emitted and are named in docs/STATUS.md's boundary section for this
     * step; the container block above says what the array runtime does not
     * reproduce (interning, dict containers, printing a container).
     *
     * MEASURED BY.  tools/aot_mir.test.py's container cases compare stdout
     * BYTES against the interpreter (mir_container_new_array, _index_get,
     * _index_edges, _string_index, _index_type_mismatch), and the refusal
     * criterion mir_container_const_oob demands the prescan refusal with
     * `why=index-out-of-range-constant`.  Before this arm the four accepted
     * ones were red in the two shapes quoted above; after it they are
     * `mir=accepted|exec=same`.  ALLOCS_EXPECT's container entries are the pool
     * bytes, measured and then written down. */
    case OP_NEW_ARRAY: {
        int n = w3;                     /* the element COUNT: see op_regs' note */
        if (n == 0) { buf_fmt(b, "    v[%d] = mv_arr_new(0, 0);", r1); return 0; }
        buf_fmt(b, "    v[%d] = mv_arr_new(%d, mvargs + asp - %d); asp -= %d;",
                r1, n, n, n);
        return 0;
    }
    case OP_INDEX_GET: buf_fmt(b, "    v[%d] = mv_arr_get(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_LT:           buf_fmt(b, "    v[%d] = mv_lt(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_GT:           buf_fmt(b, "    v[%d] = mv_gt(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_LE:           buf_fmt(b, "    v[%d] = mv_le(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_GE:           buf_fmt(b, "    v[%d] = mv_ge(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_AND:          buf_fmt(b, "    v[%d] = mv_boo(mv_tru(v[%d]) && mv_tru(v[%d]));", r1, r2, r3); return 0;
    case OP_OR:           buf_fmt(b, "    v[%d] = mv_boo(mv_tru(v[%d]) || mv_tru(v[%d]));", r1, r2, r3); return 0;
    case OP_NEG:          buf_fmt(b, "    v[%d] = mv_neg(v[%d]);", r1, r2); return 0;
    case OP_NOT:          buf_fmt(b, "    v[%d] = mv_not(v[%d]);", r1, r2); return 0;
    case OP_IS_NIL:       buf_fmt(b, "    v[%d] = mv_is_nil(v[%d]);", r1, r2); return 0;
    case OP_LOAD_GLOBAL:
        /* Index -1 has no name to look up: gnames[] is built from the globals
           prescan saw REFERENCED, and an unresolved global is referenced by
           nobody.  Indexing it would be the emitter guessing, so the value is
           spelled out instead -- nil, which is what the interpreter answers. */
        if (w2 == -1) { buf_fmt(b, "    v[%d] = mv_nil();", r1); return 0; }
        buf_fmt(b, "    v[%d] = %s;", r1, c->gnames[w2]); return 0;
    case OP_STORE_GLOBAL: buf_fmt(b, "    %s = v[%d];", c->gnames[r1], r2); return 0;
    case OP_JUMP:         buf_fmt(b, "    goto L%d;", w2); return 0;
    case OP_JUMP_IF_FALSE: buf_fmt(b, "    if (!mv_tru(v[%d])) goto L%d;", r1, w2); return 0;
    case OP_JUMP_IF_TRUE:  buf_fmt(b, "    if (mv_tru(v[%d])) goto L%d;", r1, w2); return 0;
    case OP_PUSH_REG:     buf_fmt(b, "    mvargs[asp++] = v[%d];", r1); return 0;
    case OP_CALL_FUNC: {
        int fidx = r1, res = r2, argc = w3;
        int callee = c->nodes[0].child_ids ? c->nodes[0].child_ids[fidx] : -1;
        buf_fmt(b, "    v[%d] = %s(", res, callee >= 0 ? c->fnnames[callee] : "mv_missing_callee");
        for (int k = 0; k < argc; k++)
            buf_fmt(b, "%smvargs[asp-%d]", k ? ", " : "", argc - k);
        buf_str(b, ");");
        if (argc > 0) buf_fmt(b, " asp -= %d;", argc);
        return 0;
    }
    /* THE IMPLICIT RETURN IS nil, NOT INTEGER 0 -- the debt this arm used to
     * pay for by answering a question nobody asked.  Five parts, same shape as
     * the mv_eq note in the block above: WHY / WHAT IT ADDS / WHAT IT DOES NOT
     * COVER / DELETE CONDITION / MEASURED BY.
     *
     * WHY.  `func n() { x = 1 }` with `say n()` does NOT refuse: the compiler
     * emits OP_RETURN with r1 == 0 (measured, 6 instructions), and the old arm
     * below emitted `return mv_int(0);` for that shape.  The interpreter
     * answers NIL for it -- src/vm/vm.c:4126-4127, the L_RETURN test is
     * `ins.r1 > 0 && ins.r1 < FRAME_REGS`, and both a bare `return` and
     * falling out of the function end take the else branch that sets
     * VAL_NIL.  So this was a SILENT wrong answer: `cc` succeeded, the
     * binary exited 0, and the only visible difference from the interpreter
     * was one stdout byte (`0` instead of `nil`).  A crash announces itself; a
     * wrong value with a success exit code does not, and that is the whole
     * reason this entry exists.  The C epilogue emitted at the end of every
     * function (emit_container, below) carried the SAME answer and is fixed
     * here in the same commit: it is the fall-through defence for a body whose
     * last instruction is not OP_RETURN, and a defence that answers the wrong
     * value is not a defence.
     *
     * WHY IT CANNOT BE "THE REGISTER IS ALREADY ZERO".  Every function starts
     * with `for (i...) v[i] = mv_int(0)` and mv_from_nv leaves sval/ptr zero
     * and copies the tag into .type -- and MV_INT is the tag whose numeric
     * value is 0.  So a MirVal that was never written READS as integer 0, and
     * the old arm's `mv_int(0)` was indistinguishable from reading an empty
     * register.  That coincidence is exactly what a silent-default bug is
     * made of (docs/STATUS.md 10.104 closed the same shape for GLOBALS).  The
     * fix therefore does not touch the register initialiser -- it stays
     * mv_int(0) so an unwritten register stays deterministic -- and it emits
     * an explicit `return mv_nil();` so that the RETURN VALUE no longer
     * depends on which bits a zeroed slot happens to mean.
     *
     * WHAT IT ADDS.  Two emit sites change answer, no new opcode, no new
     * print exit, no change to what any other operation means:
     *   - this arm, for OP_RETURN with r1 == 0 (implicit end and bare return);
     *   - the non-main epilogue in emit_container, for a body with no trailing
     *     OP_RETURN at all.
     * Paths WITH a value (r1 > 0, `return e`) are untouched: `return x` was
     * always right and the arm above still copies the register as-is.
     *
     * WHAT IT DOES NOT COVER.  Deliberately:
     *   - main's `return 0;` is int main(void)'s PROCESS EXIT CODE, not an
     *     Inimerse value; it does not go through MirVal and is not this hole.
     *   - The register initialiser stays mv_int(0) (see above).  If a future
     *     opcode reads a register the emitter never wrote, that read still
     *     means integer 0; nothing in the current subset does, and making the
     *     slots nil instead would change a question this entry did not ask.
     *   - `func m() { }` and `func b() { return }` are the same OP_RETURN r1=0
     *     shape and are covered by the same change; they are in the criterion
     *     precisely so that "only the non-empty body got fixed" cannot pass.
     *
      * DELETE CONDITION (decidable): the commit that gives the shared NV a nil
     * tag and moves the return value's nil into the IR, in which case this arm
     * emits `return mv_from_nv(nv_nil())` / delegates like every numeric
     * operation.  Mechanical check for "the old answer is gone": a `grep -n`
     * over src/compilation/aot_mir.c for the emitted-Cstring prefix of a
     * function return of integer 0 -- a double quote, four spaces, then that
     * return -- returns ZERO lines today (this note does not spell the pattern
     * out, so it cannot match itself).  Any hit is an emitted function return
     * of the old answer, i.e. the condition is not met.  The register
     * initialiser writes `v[i] = mv_int(0);`, which has no closing quote where
     * the prefix needs one, so it cannot mask a returning site.
     *
     * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY case mir_implicit_return
     * (non-empty body, empty body, bare return) compares BYTES against the
     * interpreter.  Before this change it reddened as
     * interp-out=b'nil\nnil\nnil\n' vs mir-out=b'0\n0\n0\n' with BOTH rc=0 --
     * the receipt for "an exit code cannot see this hole". */
    case OP_RETURN:
        if (c->nodes[c->id].is_main) { buf_str(b, "    return 0;"); return 0; }
        if (r1 > 0) buf_fmt(b, "    return v[%d];", r1);
        else buf_str(b, "    return mv_nil();");
        return 0;
    case OP_SAY:          buf_fmt(b, "    mv_say(v[%d]);", r1); return 0;
    case OP_HALT:
        if (c->nodes[c->id].is_main) buf_str(b, "    return 0;");
        else buf_str(b, "    fflush(stdout); exit(0);");
        return 0;
    default:
        return 1;
    }
}

static int emit_container(Buf *out, const IrModule *m, const Node *nodes,
                          char **gnames, char **fnnames, int id) {
    const IrFunc *fn = &m->funcs[id];
    const Bytecode *node = nodes[id].node;
    int argc = nodes[id].argc;
    if (argc < 0 || argc >= REG_LIMIT) return bad("container=%d argc=%d out of range", id, argc);

    int nregs = container_nregs(fn, argc);
    if (nregs > g_regs_max) g_regs_max = nregs;

    /* The argument stack is sized here by the DEEPEST it gets, not by nregs:
       nregs bounds the operand of any single push, while `mvargs` has to hold
       every push not yet consumed.  OP_NEW_ARRAY consumes w3 of them
       (prescan proved it cannot underflow) and it also makes has_args true
       even when no OP_PUSH_REG appears in a different container's arm -- an
       array literal's elements ARE argument-stack entries. */
    int has_args = 0;
    int asp_deepest = 0, asp_now = 0;
    for (int i = 0; i < fn->ninsns; i++) {
        const IrInsn *in = &fn->insns[i];
        int op = in->op;
        if (op == OP_PUSH_REG) {
            has_args = 1;
            asp_now++;
            if (asp_now > asp_deepest) asp_deepest = asp_now;
            continue;
        }
        if (op == OP_CALL_FUNC || op == OP_NEW_ARRAY) {
            int n = ir_insn_r3(in);
            if (n > 0) {
                if (op == OP_NEW_ARRAY) has_args = 1;
                asp_now -= n;
                if (asp_now < 0) asp_now = 0;   /* prescan already refused this */
            }
        }
    }

    unsigned char *need = calloc((size_t)fn->ninsns + 1, 1);
    if (!need) return bad("container=%d: out of memory", id);
    need[0] = 1;
    for (int b = 0; b < fn->nblocks; b++) {
        int s = fn->blocks[b].start;
        if (s >= 0 && s <= fn->ninsns) need[s] = 1;
    }
    for (int i = 0; i < fn->ninsns; i++) {
        int op = fn->insns[i].op;
        if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_JUMP_IF_TRUE) {
            int32_t t = ir_insn_r2(&fn->insns[i]);
            if (t >= 0 && t <= fn->ninsns) need[t] = 1;
        }
    }

    Ctx c;
    c.m = m; c.root = m->bc; c.node = node; c.nodes = nodes;
    c.gnames = gnames; c.fnnames = fnnames; c.id = id;

    Buf body;
    buf_init(&body);
    int emitted = 0;
    for (int i = 0; i < fn->ninsns; i++) {
        if (need[i]) buf_fmt(&body, "L%d:" NL, i);
        size_t before = body.len;
        int rc = emit_one(&c, &body, i);
        if (rc != 0) {
            free(need); buf_free(&body);
            return bad("container=%d insn=%d op=%s was accepted by the pre-scan but not emitted",
                       id, i, op_name(fn->insns[i].op));
        }
        if (body.len == before) {
            free(need); buf_free(&body);
            return bad("container=%d insn=%d op=%s emitted nothing, so the conservation equation could not see it",
                       id, i, op_name(fn->insns[i].op));
        }
        buf_str(&body, NL);
        emitted++;
        g_used_by_op[fn->insns[i].op < 256 ? fn->insns[i].op : 0] = 1;
    }
    if (need[fn->ninsns]) buf_fmt(&body, "L%d: ;" NL, fn->ninsns);
    free(need);

    if (nodes[id].is_main) {
        buf_str(out, "int main(void) {" NL);
    } else {
        buf_fmt(out, "static MirVal %s(", fnnames[id]);
        if (argc == 0) buf_str(out, "void");
        for (int k = 0; k < argc; k++) buf_fmt(out, "%sMirVal a%d", k ? ", " : "", k);
        buf_str(out, ") {" NL);
    }
    buf_fmt(out, "    MirVal v[%d];" NL, nregs);
    buf_fmt(out, "    for (int i = 0; i < %d; i++) v[i] = mv_int(0);" NL, nregs);
    /* The allocation reading (IR-MIR-ALLOCS) is armed here, in main only:
       mv_free_all() is the exit sweep, so one registration reports exactly one
       line, and every exit path a main can take -- return 0, OP_HALT's return,
       OP_HALT's exit(0) -- runs it.  A non-main container is not a program and
       must not arm it, or a call would register a second sweep. */
    if (nodes[id].is_main) buf_str(out, "    atexit(mv_free_all);" NL);
    if (has_args) {
        int nargs = asp_deepest > nregs ? asp_deepest : nregs;
        buf_fmt(out, "    MirVal mvargs[%d];" NL, nargs);
        buf_str(out, "    int asp = 0;" NL);
    }
    for (int k = 0; k < argc; k++) buf_fmt(out, "    v[%d] = a%d;" NL, k + 1, k);
    if (body.d) buf_str(out, body.d);
    g_body_bytes += body.len;
    buf_free(&body);

    if (nodes[id].is_main) buf_str(out, "    return 0;" NL "}" NL);
    else buf_str(out, "    return mv_nil();" NL "}" NL NL);   /* fall-through: nil, see the OP_RETURN note */
    g_funcs++;
    g_emitted += emitted;
    return 0;
}

/* --------------------------------------------------------------- interface */

int aot_mir_translate(const IrModule *m, const char *out_path) {
    memset(g_notyet_by_op, 0, sizeof g_notyet_by_op);
    memset(g_used_by_op, 0, sizeof g_used_by_op);
    g_insns = g_emitted = g_notyet = g_funcs = g_regs_max = 0;
    g_body_bytes = 0;
    g_detail[0] = 0;
    g_prescan_why = "illegal-operand";   /* every refusal starts in the family */
    snprintf(g_err, sizeof g_err, "no error");

    if (!out_path) { set_err("no output path"); return 0; }
    if (!m) { refuse("not-a-module", "the module is NULL"); print_conserve(); return 0; }
    if (!m->bc || m->nfuncs <= 0) {
        refuse("not-a-module", "the module carries no container or no functions");
        print_conserve();
        return 0;
    }

    Node *nodes = calloc((size_t)m->nfuncs + 1, sizeof(Node));
    char **gnames = NULL, **fnnames = NULL;
    Buf out;
    buf_init(&out);
    int ok = 0;
    int nn = 0;
    const Bytecode *root = m->bc;

    int brc = build_nodes(root, NULL, 0, 1, nodes, &nn, m->nfuncs + 1);
    if (brc == -2) {
        refuse("threads", "the container carries a thread section, which this route does not emit");
        goto done;
    }
    if (brc != 0 || nn != m->nfuncs) {
        refuse("container-shape", "the container tree does not match the module's function count");
        goto done;
    }

    /* The pre-scan runs first because it is what decides which globals are
       referenced at all; names are built from that answer, not beside it. */
    g_gref_n = root->global_name_count;
    if (g_gref_n > 0) {
        g_gref = calloc((size_t)g_gref_n, 1);
        if (!g_gref) { refuse("out-of-memory", "the global reference table"); goto done; }
    }

    int kinds = prescan(m, nodes, root, NULL, NULL);
    if (kinds < 0) {
        /* The family name unless a check named its own reason (see
           g_prescan_why): "index-out-of-range-constant" is the one refusal this
           step adds, and the criterion compares that word, not the detail. */
        refuse(g_prescan_why, g_detail);
        print_conserve();
        goto done;
    }
    if (kinds > 0) {
        print_not_yet();
        refuse("unsupported-opcode", g_detail[0] ? g_detail : "see IR-MIR-NOT-YET");
        print_conserve();
        goto done;
    }

    gnames = build_global_names(root);
    if (!gnames) {
        refuse("global-name", g_namedetail[0] ? g_namedetail : "two global names collide after sanitizing");
        goto done;
    }
    fnnames = build_func_names(nodes, m->nfuncs);
    if (!fnnames) {
        refuse("function-name", g_namedetail[0] ? g_namedetail : "two function names collide after sanitizing");
        goto done;
    }

    buf_str(&out, aot_native_preamble());
    buf_str(&out, NL);
    /* Appended, never spliced: the shared preamble stays byte-identical in
       both routes, so classify() in tools/aot_mir.test.py still finds its
       common prefix and the first difference lands on the register type. */
    buf_str(&out, kMirValBlock);

    /* nil, not 0.  The interpreter creates every global as VAL_NIL
       (src/vm/vm.c: value_set(..., VAL_NIL, ...) when the globals table is
       built), and this file used to spell that default { MV_INT, 0, 0 } -- a
       FALSE EQUIVALENCE, dated here: a global read before its first store
       answered '0' here and 'nil' in the interpreter, so `x == nil` and
       `x == 0` came back swapped for the same x and `say x` printed the wrong
       word.  Zero is falsy like nil, which is exactly why the mistake survived
       comparison-only programs and needed mir_nil_vs_zero to be written down. */
    for (int i = 0; i < root->global_name_count; i++) {
        if (!gnames[i]) continue;
        buf_fmt(&out, "static MirVal %s = { MV_NIL, 0, 0 };" NL, gnames[i]);
    }
    buf_str(&out, NL);

    for (int i = 1; i < m->nfuncs; i++) {
        int argc = nodes[i].argc;
        buf_fmt(&out, "static MirVal %s(", fnnames[i]);
        if (argc == 0) buf_str(&out, "void");
        for (int k = 0; k < argc; k++) buf_fmt(&out, "%sMirVal", k ? ", " : "");
        buf_str(&out, ");" NL);
    }
    buf_str(&out, NL);

    for (int i = 1; i < m->nfuncs; i++) {
        if (emit_container(&out, m, nodes, gnames, fnnames, i) != 0) {
            refuse("emit-failed", g_detail);
            goto done;
        }
    }
    if (emit_container(&out, m, nodes, gnames, fnnames, 0) != 0) {
        refuse("emit-failed", g_detail);
        goto done;
    }

    if (g_emitted + g_notyet != g_insns) {
        snprintf(g_detail, sizeof g_detail,
                 "emitted=%d not-yet=%d insns=%d", g_emitted, g_notyet, g_insns);
        refuse("conservation", g_detail);
        print_conserve();
        goto done;
    }

    {
        FILE *fp = fopen(out_path, "wb");
        if (!fp) {
            refuse("write-failed", out_path);
            print_conserve();
            goto done;
        }
        size_t n = out.len;
        if (n > 0 && fwrite(out.d, 1, n, fp) != n) {
            fclose(fp);
            remove(out_path);
            refuse("write-failed", out_path);
            print_conserve();
            goto done;
        }
        fclose(fp);
    }

    {
        char list[1024];
        int o = 0;
        for (int op = 0; op < 256; op++) {
            if (!g_used_by_op[op]) continue;
            int n = snprintf(list + o, sizeof list - (size_t)o, "%s%s", o ? "," : "", op_name(op));
            if (n < 0) break;
            o += n;
        }
        printf("IR-MIR-CODEGEN|file=%s|insns=%d|functions=%d|regs-max=%d|emits=%d|globals-renamed=%d|generated=(%s)" NL,
               out_path, g_insns, g_funcs, g_regs_max, aot_mir_emitted_count(), g_globals_renamed, list);
    }
    print_conserve();
    ok = 1;

done:
    buf_free(&out);
    free(g_gref); g_gref = NULL; g_gref_n = 0;
    free_names(gnames, root ? root->global_name_count : 0);
    free_names(fnnames, m->nfuncs);
    if (nodes) {
        for (int i = 0; i < nn; i++) free(nodes[i].child_ids);
        free(nodes);
    }
    return ok;   /* g_err already says why, if anything did */
}

const char *aot_mir_last_error(void) { return g_err[0] ? g_err : "no error"; }
int aot_mir_last_insns(void) { return g_insns; }
int aot_mir_last_not_yet(void) { return g_notyet; }
