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
"#define MV_ARR 6   /* = VAL_ARRAY -- ival is a POOL INDEX, as Value's row has.\n"
"                      DEFINED HERE, next to the other tags and ABOVE its first\n"
"                      reader, because generated C is compiled in block order:\n"
"                      the equality pair (mv_eq/mv_ne) names this tag and the\n"
"                      pool and its readers sit further down.  The container\n"
"                      block's own note explains what the tag means. */\n"
"#define MV_DIC 7   /* = VAL_DICT -- ival is a POOL INDEX, the same pool MV_ARR\n"
"                      names, because the interpreter keeps a dict in its ARRAY\n"
"                      pool (L_NEW_DICT calls vm_array_new then push_n of the\n"
"                      key/value pairs, src/vm/vm.c:3648-3673: the dict's items\n"
"                      ARE an ArrayObj's items and the dict handle is\n"
"                      `aidx + 1` exactly as an array's is).  DEFINED HERE, above\n"
"                      every reader, for the reason MV_ARR's note gives: generated\n"
"                      C is compiled in block order and the equality pair comes\n"
"                      first.  Read-only this step: no write path builds or\n"
"                      mutates one. */\n"
"#define MV_SET 8   /* = VAL_SET -- ival is a POOL INDEX, the same pool MV_ARR and\n"
"                      MV_DIC name.  That is NOT the interpreter's storage: it\n"
"                      keeps a set in vm->sets[] (a SetObj with a sorted i64\n"
"                      half and a Value items half, src/vm/vm.h:60-69) while a\n"
"                      dict rides in its array pool.  What the two DO share is\n"
"                      the property a handle needs -- the storage is reachable\n"
"                      only through the handle, and a handle is stable for as\n"
"                      long as the value that carries it -- so the region is\n"
"                      built ONCE, complete, and never moved.  The tag value is\n"
"                      VAL_SET itself (8), so a MirVal and a Value agree about\n"
"                      what a set is even though only one of them can hold it.\n"
"                      DEFINED HERE, above every reader, for the reason MV_ARR's\n"
"                      note gives.  Read-only once built: no lowered opcode adds\n"
"                      to or removes from one (see mv_set_new's note). */\n"
"#define MV_FUN 9   /* = VAL_FUNCTION -- ival is a MODULE INDEX into this\n"
"                      route's own function table (the switch mv_callv is),\n"
"                      NOT the interpreter's root-bytecode function index that\n"
"                      Value's VAL_FUNCTION carries: every lambda is registered\n"
"                      into mainBC (src/compiler/compiler.c:1246), so the\n"
"                      bytecode operand is main's child slot while this route\n"
"                      numbers containers depth-first, and build_nodes'\n"
"                      child_ids[] is the one translation between the two\n"
"                      (emit_one's OP_MAKE_FUNC arm performs it, once, at\n"
"                      translation time).  ptr is the CAPTURE ENVIRONMENT\n"
"                      HANDLE, which is the job Value.ptr gives it -- it carries\n"
"                      the ImClosureFunction there (value_set(..., VAL_FUNCTION,\n"
"                      ins.r2, 0, NULL, fn), src/vm/vm.c:4040-4054) -- and this\n"
"                      route fills it with the environment mv_fun_cap allocates\n"
"                      when the capture count is above zero (NULL for the\n"
"                      capture-free form, and NULL is also what a container gets\n"
"                      when it is called as a plain function: its capture reads\n"
"                      then answer nil, which is exactly what the interpreter's\n"
"                      no-environment read answers).  Printing is\n"
"                      `<function>` (src/vm/vm.c:1379) and truthiness is true\n"
"                      (vm_truthy's `default: return 1`, src/vm/vm.c:400), which\n"
"                      is the whole of what mir_closure_value_nocapture and\n"
"                      mir_closure_capture_read ask.\n"
"                      DEFINED HERE, above every reader, for the reason MV_ARR's\n"
"                      note gives. */\n"
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
"/* TWO DECLARATIONS THAT HAVE TO COME BEFORE THE COMPARISON HELPERS, and the\n"
"   reason is the order this file is COMPILED in, not style.  mv_lt and its\n"
"   three siblings now refuse an unorderable pair the way the interpreter\n"
"   does (`mv_throw(mv_str(\"type_mismatch\"))`), and both of those functions\n"
"   are defined further down -- measured: without these lines every generated\n"
"   program fails on cc with \"implicit declaration of function mv_throw\", so\n"
"   the whole shared corpus reddens on the compiler instead of on an answer.\n"
"   The seam itself is defined with the try state (10.137); mv_str is defined\n"
"   with the string block. */\n"
"static void   mv_throw(MirVal a);\n"
"static inline MirVal mv_str(const char *s);\n"
"static inline int mv_numeric(MirVal a) {\n"
"    return a.type == MV_INT || a.type == MV_FLT || a.type == MV_BOO;   /* :323-334 */\n"
"}\n"
"static inline int mv_str_cmp(MirVal a, MirVal b) {\n"
"    /* BOTH STRINGS IS A PRECONDITION, not a hope: the four ordering arms call\n"
"       mv_orderable first and this function second, and the string arm only\n"
"       when both sides are MV_STR.  A mixed pair can no longer reach here, so\n"
"       the guard is kept as a stop (a future caller that skips the guard is a\n"
"       defect, and a defect should stop loudly) but it is no longer the ROUTE's\n"
"       answer to the shape -- mv_orderable is. */\n"
"    if (a.type != MV_STR || b.type != MV_STR) abort();\n"
"    if (!a.sval || !b.sval) abort();  /* payload guard only: ival is the free\n"
"                                         marker, not a residency claim (see\n"
"                                         mv_str_concat, which relaxed it) */\n"
"    if (a.sval == b.sval) return 0;   /* val_cmp:450 pointer fast path */\n"
"    return strcmp(a.sval, b.sval);    /* val_cmp:451 */\n"
"}\n"
"static inline MirVal mv_nil(void) {\n"
"    MirVal r;\n"
"    r.type = MV_NIL;\n"
"    r.ival = 0;\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    return r;\n"
"}\n"
"\n"
"/* MV_FUN -- the first value in this route that is not DATA, and the whole of\n"
" * the function form this route lowers: capture-free values and the ONE-CAPTURE\n"
" * read form.  Same structure as the notes around it: WHY IT\n"
" * EXISTS / WHAT IT ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION.\n"
" *\n"
" * WHY IT EXISTS.  `f = () -> 42` / `say f` / `if f {}` / `g = f` / `say g()`\n"
" * refused to translate at all: OP_MAKE_FUNC and OP_CALL_VALUE had no entry in\n"
" * op_regs, so both counted as not-yet and the whole program was\n"
" * `why=unsupported-opcode (OP_MAKE_FUNC:1,OP_CALL_VALUE:1)`.  The value those\n"
" * two opcodes move exists in the interpreter -- a VAL_FUNCTION with the\n"
" * function's index in ival and an ImClosureFunction in ptr -- and MirVal has a\n"
" * 32-byte shape with a ptr field, so this is a missing TAG and a missing\n"
" * dispatch, not a missing representation.  No layout change, no third value\n"
" * representation: docs/DECFY_DESIGN.md:118 freezes Value at 32 bytes and\n"
" * docs/DECFY_DESIGN.md:583 is the A' ruling, and both hold here by\n"
" * construction (the six _Static_assert lines above are the executable claim).\n"
" *\n"
" * WHAT IT ADDS, exactly five pieces and nothing else:\n"
" *   - `#define MV_FUN 9` (= VAL_FUNCTION, the number the interpreter gives it)\n"
" *     and this constructor.  ival is the MODULE INDEX of the container, which\n"
" *     is NOT the bytecode operand: a lambda is registered into mainBC\n"
" *     (src/compiler/compiler.c:1246), so OP_MAKE_FUNC's operand counts main's\n"
" *     children while this route numbers every container depth-first, and\n"
" *     nodes[0].child_ids[] is the one translation (performed at translation\n"
" *     time in emit_one's OP_MAKE_FUNC arm).\n"
" *   - arms in mv_tru (true, vm_truthy's `default: return 1`) and in mv_say\n"
" *     (`<function>`, src/vm/vm.c:1379).\n"
" *   - mv_callv, the dispatch table emitted only for a module that actually\n"
" *     moves a function value, and the function-value emitter arms that fill and\n"
" *     read it (OP_MAKE_FUNC, whose r3 is a CAPTURE COUNT consumed off the\n"
" *     argument stack, and OP_LOAD_CAPTURE, whose r2 is an ENVIRONMENT SLOT and\n"
" *     not a register -- both are R1-only rows in op_regs).\n"
" *   - the CAPTURE ENVIRONMENT, the piece this cell adds: mv_fun_cap allocates\n"
" *     one slot array per creation when the capture count is above zero,\n"
" *     mv_capture reads a slot of it, and a container's signature carries a\n"
" *     hidden leading `self` parameter when -- and ONLY when -- its body reads a\n"
" *     slot inside the environment its creator built (see func_cap's note: a\n"
" *     parameter on every container would move the generated text of programs\n"
" *     that never touch a closure).\n"
" * ptr is NULL for the capture-free form (there is nothing to point at) and,\n"
" * above a capture count of zero, it is mv_fun_cap's environment -- the one\n"
" * field that makes two values built by the same OP_MAKE_FUNC independent.\n"
" *\n"
" * WHAT IT DOES NOT COVER -- a boundary, deliberately, and the next grid's\n"
" * payload:\n"
" *   - A CLOSURE THAT READS A CAPTURE ITSELF PRODUCED BY A CLOSURE.  This\n"
" *     bullet used to say such a program was out of reach; it is MEASURED to\n"
" *     be answered by this block instead: `outer(1)` returning `b -> (c ->\n"
" *     a + b + c)` prints 6 for `g(2)(3)`, the interpreter's own number, because\n"
" *     the compiler RE-LOADS the parent capture as an ordinary value before\n"
" *     the inner OP_MAKE_FUNC (src/compiler/compiler.c:1297-1302), and that\n"
" *     load is the same read this block already answers -- one environment\n"
" *     per creation, no second mechanism.  What is still out of reach is\n"
" *     stated below and is about FUNCTIONS, not about capture depth.\n"
" *     What IS answered, and was the point of the environment: every creation\n"
" *     site gets its OWN environment (copied from the argument stack at\n"
" *     OP_MAKE_FUNC, as the interpreter's L_MAKE_FUNC copies it), and a capture\n"
" *     read inside a slot the environment has answers that value even after\n"
" *     the creating frame has returned.\n"
" *   - Comparing two functions (`f == g`, `f != g`).  This bullet used to\n"
" *     say the comparison had no mv_eq row and stopped the program.  It HAS\n"
" *     a row now (see mv_eq's FUNCTION note), and the answer is FALSE for\n"
" *     every pair with a function on either side -- the interpreter's own\n"
" *     answer, because val_eq's FUNCTION row compares the ImClosureFunction\n"
" *     pointer while every read CLONES the closure (value_copy\n"
" *     src/vm/vm.c:530-537), so no shape sees one pointer twice.  MEASURED by\n"
" *     mir_function_equality_nocapture and _captured; the note there names the\n"
" *     delete condition.  A capture READ whose slot is outside the\n"
" *     environment is not a gap either: the interpreter\n"
" *     answers nil there (src/vm/vm.c:4033-4039) and the emitting arm answers\n"
" *     mv_nil() for exactly that shape.\n"
" *   - Ordering, arithmetic, container membership on a function.  Same class:\n"
" *     mv_as_nv stops the program rather than inventing a number.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish).  Delete the tag and the\n"
" * constructor in the commit that gives the shared NV a function/payload tag,\n"
" * at which point this becomes mv_from_nv(nv_fun(idx)) like every other\n"
" * constructor, and the test is mechanical: MV_FUN no longer appears in this\n"
" * block outside mv_tru/mv_say/mv_callv's arms.  Deleting it earlier puts\n"
" * OP_MAKE_FUNC back to zero answer for a program the interpreter answers.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases\n"
" * mir_closure_value_nocapture and mir_closure_capture_read compare stdout\n"
" * BYTES and the exit code against the interpreter: the first over `say f` /\n"
" * `if f` / `say g()` (its ALLOCS reading is 0|0|0|0 because a capture-free\n"
" * function value is a tag plus an index and nothing is allocated for one), the\n"
" * second over `make(10)(5)` = 15 and `make(3)(7)` = 10, which differ only if\n"
" * each value carries its own environment (2 allocations, 2*32 bytes). */\n"
"static inline MirVal mv_fun(int idx) {\n"
"    MirVal r;\n"
"    r.type = MV_FUN;\n"
"    r.ival = (long long)idx;   /* module index; mv_callv is the only reader */\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;                /* the capture environment handle: NULL here */\n"
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
"    if (a.type == MV_FUN) return 1;   /* vm_truthy's `default: return 1`\n"
"                                         (src/vm/vm.c:400): a VAL_FUNCTION is\n"
"                                         TRUE, `if f {}` taken.  Truthiness\n"
"                                         reads no payload, so no guard.\n"
"                                         MV_FUN's row -- together with mv_say's\n"
"                                         and mv_callv's -- is the two-line\n"
"                                         answer mir_closure_value_nocapture\n"
"                                         asks for; see mv_fun's note. */\n"
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
"static inline MirVal mv_neg(MirVal a) {\n"
"    if (!mv_numeric(a)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }   /* :323-334 */\n"
"    return mv_from_nv(nv_neg(mv_as_nv(a)));\n"
"}\n"
"static inline MirVal mv_not(MirVal a) { return mv_boo(!mv_tru(a)); }\n"
"/* THE SET-REDUCTION ARM -- what OP_MIN/OP_MAX answer on this route, and why\n"
" * the whole answer is nil.  Same structure as the notes here: WHY / WHAT IT\n"
" * ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  `say min(5)` refused to translate ('unsupported-opcode',\n"
" * IR-MIR-NOT-YET|op=OP_MIN|n=1) -- not because a lowering was missing in the\n"
" * usual sense, but because what the opcode MEANS here had not been read.\n"
" * Read it in the interpreter: L_MIN/L_MAX (src/vm/vm.c:4357) hand\n"
" * R[ins.r2] and R[ins.r1] to set_minmax() (src/vm/vm.c:2476), whose FIRST\n"
" * statement is `dst->type = VAL_NIL`; the fold over integers (exact long\n"
" * long), floats and strings (strcmp) happens ONLY under\n"
" * `if (src->type != VAL_SET) return;`.  So OP_MIN is a SET REDUCTION, not an\n"
" * arithmetic pair: `min(1, 2)` is the interval-set spelling (OP_SET_INTERVAL)\n"
" * and there is no two-operand min anywhere in the compiler.\n"
" *\n"
" * WHY THE ANSWER IS nil ON EVERY STREAM THIS ROUTE ACCEPTS.  MirVal has no\n"
" * set tag (MV_INT/MV_FLT/MV_BOO/MV_NIL/MV_STR), and EVERY producer of a set\n"
" * is refused by prescan before OP_MIN is reached: OP_NEW_SET and\n"
" * OP_SET_INTERVAL are not lowered, the preset set globals are refused by\n"
" * name (see kPresetGlobals above), and a literal `min(1, 2)` leaves the\n"
" * argument stack unbalanced.  Measured, not recalled: `say min(Z)` refuses\n"
" * with 'global 1 is the preset set global', and `p = 1, 2, 3` + `say\n"
" * min(p)` with 'leaves the argument stack unbalanced: asp=3'.  That is the\n"
" * value-domain argument: for every stream that REACHES this arm the operand\n"
" * is a non-set, and the interpreter's own answer for a non-set is VAL_NIL by\n"
" * its first statement.  Answering 0 instead would be the same FALSE\n"
" * EQUIVALENCE this block already fixed once for locals (mv_nil's note): nil\n"
" * and 0 are two answers -- `say min(5)` prints 'nil', and a route that\n"
" * printed '0' would be silently wrong with a success exit code.\n"
" *\n"
" * WHAT IT DOES NOT COVER -- and the reason isMax is not a TODO but a NOPE:\n"
" *  - The FOLD ITSELF (min versus max of a set's elements, including the\n"
" *    string/number refusal rule).  A set cannot arrive; there is no value to\n"
" *    fold.  Note that -0.0 as an ELEMENT is unreachable twice over: the\n"
" *    lexer rejects `p = -0.0, 0.0` outright (parse error, measured), and a\n"
" *    set holding a variable is refused with asp imbalance.\n"
" *  - NaN.  It exists in the interpreter (`sqrt(-1)` prints nan) but only as a\n"
" *    builtin call result, and OP_CALL_BUILTIN is outside the subset, so a\n"
" *    NaN can never be the operand of this opcode here.\n"
" *  - `min(x)` where x is a CONTAINER global (Z, Z+, 1..Z, ...).  prescan\n"
" *    refuses the preset set globals by name; a user container cannot be made\n"
" *    without OP_NEW_SET/OP_SET_INTERVAL, also refused.\n"
" *\n"
" * WHY THE TYPE TEST IS THERE AT ALL.  It is the DELETE-DETECTOR.  The five\n"
" * scalar tags compared are the tags this opcode has an answer for; a set is\n"
" * the ONE value it is defined on here, and falling through to nil for it\n"
" * would be the first silent wrong answer instead of a loud stop.  (MV_FUN, added\n"
" * later, is not a set either: a min/max operand that is a FUNCTION has no\n"
" * answer on this route, so the guard's abort is the right answer for it --\n"
" * the list stops at the scalar tags on purpose, and a set tag would still be\n"
" * the one tag this body must learn.)  So the guard is not a defensive branch\n"
" * -- it is the value-domain claim, executable, and it is what makes adding a\n"
" * set tag impossible without coming back to this function.\n"
" *\n"
" * DELETE CONDITION (decidable): the commit that gives MirVal a set tag (or\n"
" * that lowers OP_NEW_SET/OP_SET_INTERVAL and the preset globals, whichever\n"
" * comes first) must replace this body with the interpreter's fold, reusing\n"
" * val_is_num/val_orderable's rule rather than writing a second one.  Test:\n"
" * the six MIR_ONLY cases below are named minmax; the first one with a real\n"
" * set operand has to change this body, and grep -c mv_minmax == 0 in the\n"
" * emitter means the opcode was removed rather than this value redefined.\n"
" *\n"
" * ZERO ALLOCATION, and not by accident: the arm returns an existing tag with\n"
" * a constant payload (mv_nil), so IR-MIR-ALLOCS must read allocs=0|freed=0|\n"
" * live=0|bytes=0 on every minmax case, exactly like the arithmetic arms.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_minmax_int,\n"
" * mir_minmax_boundary, mir_minmax_float, mir_minmax_string, mir_minmax_nil\n"
" * and mir_minmax_var compare stdout bytes against the interpreter.\n"
" *\n"
" * MEASURED ON.  cdd5a11 (the tree this arm was written against). */\n"
"static inline MirVal mv_minmax(MirVal a, int isMax) {\n"
"    (void)isMax;\n"
"    if (a.type != MV_INT && a.type != MV_FLT && a.type != MV_BOO &&\n"
"        a.type != MV_NIL && a.type != MV_STR)\n"
"        abort();   /* a tag this block has never seen: not a scalar, so a set */\n"
"    return mv_nil();   /* set_minmax's first statement, for a non-set operand */\n"
"}\n"
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
"    if (!mv_numeric(a) || !mv_numeric(b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
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
"static inline MirVal mv_sub(MirVal a, MirVal b) {\n"
"    if (!mv_numeric(a) || !mv_numeric(b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    return mv_from_nv(nv_sub(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_mul(MirVal a, MirVal b) {\n"
"    if (!mv_numeric(a) || !mv_numeric(b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    return mv_from_nv(nv_mul(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_div(MirVal a, MirVal b) {\n"
"    if (!mv_numeric(a) || !mv_numeric(b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    return mv_from_nv(nv_div(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_mod(MirVal a, MirVal b) {\n"
"    if (!mv_numeric(a) || !mv_numeric(b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    return mv_from_nv(nv_mod(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
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
"\n"
"/* THE ARRAY ROW OF THE SAME PAIR -- one comparison, exposed by OP_IN and\n"
" * closed here because the subset arm CANNOT be written without it.\n"
" *\n"
" * WHY IT EXISTS.  OP_IN's array-vs-array arm compares ELEMENTS with val_eq\n"
" * (src/vm/vm.c:2408-2413), and an element can itself be an array: this\n"
" * emitter builds those (`[[1], [2]]`), so mv_eq reached\n"
" * nv_eq(mv_as_nv(...)) and mv_as_nv stopped the program on MV_ARR -- the\n"
" * ACCEPTED-then-killed shape the note above calls worse than a refusal.  The\n"
" * same row was missing for the two spellings users write directly: `say\n"
" * [1] == [1]` and `say a != b` both aborted with empty stdout (measured on\n"
" * this tree).  One row closes all three because it IS the interpreter's rule.\n"
" *\n"
" * WHAT IT ADDS.  val_eq's array row, copied: `if (a->type == VAL_ARRAY)\n"
" * return a->ival == b->ival;` (src/vm/vm.c:420) -- array equality is IDENTITY\n"
" * of the handle, NOT element-wise value equality.  `[1] == [1]` is FALSE and\n"
" * `x == x` is true; an element-wise arm would answer the opposite on both and\n"
" * on mir_in_computed's `[x - 1] in c` line.  A cross-tag pair is false by\n"
" * val_eq's same-tag gate (:414) and by its coercion block, which lists only\n"
" * INT/FLOAT/BOOL (:425-427) -- hence one expression:\n"
" * a.type == b.type && a.ival == b.ival.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  Printing an array, ordering two of them (val_cmp's\n"
" * rows are strings and numbers only, mv_lt still calls mv_as_nv), and\n"
" * mv_tru(MV_ARR) -- `if a {}` with an array operand still stops the program.\n"
" * Those are OP_SAY / OP_LT / OP_JUMP_IF's debts, not equality's, and none of\n"
" * them is reachable from a membership test.\n"
" *\n"
" * DELETE CONDITION.  The note above's (P3: one shared NV carrying the tags and\n"
" * the comparison semantics); the check is the same -- after it, MV_ARR does\n"
" * not appear in this equality pair at all.\n"
" *\n"
" * MEASURED BY.  mir_in_computed is the case that cannot pass without it (its\n"
" * subset line compares array elements), and its last line is the OP_NEQ\n"
" * spelling of the same row.  Reverting the arm to a constant `mv_boo(0)`\n"
" * reddens that case while mir_in_array_hit stays green -- measured twice;\n"
" * docs/STATUS.md 10.115 records the transcript. */\n"
"static int mv_set_eq(int ia, int ib);  /* defined with the set helpers below */\n"
"static inline int mv_arr_live(int iv);  /* the forwarding-head resolver, defined below: mv_set_add\n"
"                                             and the set readers need it before its body */\n"
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
"    /* SETS ARE COMPARED BY CONTENT, HALF FOR HALF -- and NOT by identity.\n"
"       `L_EQ` (src/vm/vm.c:3539-3543) sends SET/SET to set_equal()\n"
"       (src/vm/vm.c:2221-2260), which checks iCount and count of EACH half and\n"
"       then matches the sorted i64 half against itself and the item half against\n"
"       itself; a set against anything else falls to val_eq()\n"
"       (src/vm/vm.c:413-430), whose rows have no VAL_SET and answer FALSE.\n"
"       Copying the dict IDENTITY row below would be a wrong answer, not a stop:\n"
"       val_eq:421 answers a dict by `ival` but answers a set FALSE, so identity\n"
"       would answer false for two equal sets.  MEASURED BY mir_set_eq_same,\n"
"       _diff, _mixed_order, _int_vs_float, _vs_scalar, _dedup; the half-for-half\n"
"       rule is what mir_set_eq_int_vs_float separates from one numeric equality.\n"
"       DELETE CONDITION: none -- this is the query's semantics, not a scaffold. */\n"
"    if (a.type == MV_SET || b.type == MV_SET) {\n"
"        if (a.type != b.type) return mv_boo(0);   /* val_eq:414 -- no set row */\n"
"        return mv_boo(mv_set_eq(a.ival, b.ival));\n"
"    }\n"
"    /* IDENTITY, not content: val_eq:421 answers a dict the same way it answers\n"
"       an array -- `a->ival == b->ival` -- and a dict handle is stable across a\n"
"       move for the same reason an ArrayObj aidx is (mv_arr_live resolves through\n"
"       the forwarding head).  Reaching mv_as_nv here ABORTED: rc 134 where the\n"
"       interpreter answers false/true/true. */\n"
"    if (a.type == MV_ARR || b.type == MV_ARR ||\n"
"        a.type == MV_DIC || b.type == MV_DIC)   /* val_eq:414/420/421/425-427 */\n"
"        return mv_boo(a.type == b.type && a.ival == b.ival);\n"
"    /* FUNCTIONS: THE ANSWER IS ALWAYS FALSE, and that is the interpreter's\n"
"       answer, not a simplification.  val_eq's FUNCTION row (src/vm/vm.c:419)\n"
"       IS `a->ptr == b->ptr`, comparing the ImClosureFunction object -- but\n"
"       every read of a function value CLONES it (value_copy, src/vm/vm.c:\n"
"       530-537, im_closure_function_clone), so `f == f` loads the global twice\n"
"       into two distinct clones, `h = f` clones on assignment, and an index\n"
"       read clones again.  MEASURED: `say f == f`, `say h == f`, `say f != f`\n"
"       and `say d[0] == d[0]` all answer false/false/true/false in the\n"
"       interpreter; no shape that names a value twice can see one pointer\n"
"       twice, so the row is in effect dead.  This route copies MirVal structs\n"
"       WITHOUT cloning (and MV_FUN.ptr is the ENVIRONMENT, NULL for the\n"
"       capture-free form), so reproducing the row's intent here would answer\n"
"       TRUE where the interpreter answers FALSE -- a silent disagreement.\n"
"       The comparison is cross-type false in val_eq too (a function is not a\n"
"       number, so the coercion tail does not reach it), which is why one row\n"
"       covers `f == 42`, `f == nil` and `f == g` alike.  Measured by\n"
"       mir_function_equality_nocapture and _captured, both red before this\n"
"       row as IR-MIR-EXEC-DIFF with rc -6 and empty stdout where the\n"
"       interpreter printed false/false/.../true.\n"
"       DELETE CONDITION: the commit where the interpreter stops cloning on\n"
"       read, because then identity becomes observable and this row must grow\n"
"       an identity object to carry it. */\n"
"    if (a.type == MV_FUN || b.type == MV_FUN) return mv_boo(0);\n"
"    return mv_from_nv(nv_eq(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_ne(MirVal a, MirVal b) {\n"
"    if (a.type == MV_NIL || b.type == MV_NIL) return mv_boo(a.type != b.type);\n"
"    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(!mv_tru(mv_eq(a, b)));\n"
"    if (a.type == MV_ARR || b.type == MV_ARR ||\n"
"        a.type == MV_DIC || b.type == MV_DIC) return mv_boo(!mv_tru(mv_eq(a, b)));\n"
"    if (a.type == MV_SET || b.type == MV_SET) return mv_boo(!mv_tru(mv_eq(a, b)));\n"
"    /* The negation of mv_eq's FUNCTION row, for the same reason (see the\n"
"       note there): the interpreter answers TRUE for every `!=` between\n"
"       function values, because it never sees one closure object twice. */\n"
"    if (a.type == MV_FUN || b.type == MV_FUN) return mv_boo(1);\n"
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
"/* THE ORDERING GUARD.  src/vm/vm.c's L_LT/L_LE/L_GT/L_GE all open with the\n"
" * same test -- `if (!val_orderable(&R[r2], &R[r3])) { vm_throw_kind(\n"
" * \"type_mismatch\"); ... }` -- and val_orderable (:442-445) admits exactly two\n"
" * shapes: TWO STRINGS, or two NUMBERS (val_is_num: int, float AND bool).\n"
" * Everything else is REFUSED, and the refusal is a catchable exception, not a\n"
" * crash.\n"
" *\n"
" * WHY THIS FUNCTION EXISTS.  This route used to answer a mixed pair by\n"
" * aborting (rc 134, no message), which is the one shape a reader cannot act\n"
" * on.  Measured on the interpreter, all six of these print\n"
" * `[exception] uncaught: type_mismatch`, nothing on stdout, and exit 1:\n"
" *     \"a\" < 1      x > 2 (x a string)      true > 1 is NOT one of them\n"
" *     c > 1 (c a set)     a >= 1 (array)     d < 1 (dict)      nil > 1\n"
" * -- and `true > 1` answers false, because a BOOL is numeric.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  It returns the comparison's sign for a pair that\n"
" * HAS an order and 0 otherwise; the caller MUST consult mv_orderable first,\n"
" * because 0 is also a legitimate answer (`\"a\" == \"a\"` orders as 0).  Two\n"
" * functions rather than one because a single one returning 0 for both would be\n"
" * the ambiguity this whole block exists to avoid.\n"
" *\n"
" * MEASURED BY the eight mir_order_* criteria; the two controls are\n"
" * `true > 1` (must keep answering) and two strings (must keep answering). */\n"
"static inline int mv_orderable(MirVal a, MirVal b) {\n"
"    if (a.type == MV_STR && b.type == MV_STR) return 1;          /* :443 */\n"
"    return (a.type == MV_INT || a.type == MV_FLT || a.type == MV_BOO)\n"
"        && (b.type == MV_INT || b.type == MV_FLT || b.type == MV_BOO);   /* :444 */\n"
"}\n"
"/* THE ARITHMETIC GUARD.  val_as_double (src/vm/vm.c:323-334) admits int, float"
" * and bool, and REFUSES everything else with vm_throw_kind(type_mismatch) --"
" * so every arithmetic opcode has the same precondition the ordering arms have,"
" * and this route answered the failure with mv_as_nv -> abort() (rc 134, no"
" * message)."
" *"
" * MEASURED, interpreter side, all of these print [exception] uncaught:"
" * type_mismatch, nothing on stdout, exit 1:"
" *     1 + nil     nil - 1     1 / nil     nil % 2     -nil"
" * and true * 2 answers 2 -- a BOOL is numeric, and that is the control that"
" * stops this from becoming the rule refuse-anything-not-int-or-float."
" * An ARRAY operand is a different shape (L_ADD has its own stderr line and"
" * exit 1); this guard refuses it with type_mismatch, which is what the OTHER"
" * arithmetic opcodes give it (measured: a list plus one refuses for addition"
" * only)."
" *"
" * WHAT IT DOES NOT COVER: it does not make the failure catchable on its own --"
" * mv_throw sets a flag and the ARMS carry the check (OP_LT note records the"
" * measurement that made that distinction expensive to learn). */"
"/* The four ordering arms, each: the interpreter's guard FIRST, then the\n"
"   comparison.  mv_throw is a stop with the interpreter's own message when no\n"
"   handler is open, and the catchable seam when one is (10.137) -- so these\n"
"   shapes are now CATCHABLE here exactly as they are there. */\n"
"static inline MirVal mv_lt(MirVal a, MirVal b) {\n"
"    if (!mv_orderable(a, b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    if (a.type == MV_STR) return mv_boo(mv_str_cmp(a, b) < 0);\n"
"    return mv_from_nv(nv_lt(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_le(MirVal a, MirVal b) {\n"
"    if (!mv_orderable(a, b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    if (a.type == MV_STR) return mv_boo(mv_str_cmp(a, b) <= 0);\n"
"    return mv_from_nv(nv_le(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_gt(MirVal a, MirVal b) {\n"
"    if (!mv_orderable(a, b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    if (a.type == MV_STR) return mv_boo(mv_str_cmp(a, b) > 0);\n"
"    return mv_from_nv(nv_gt(mv_as_nv(a), mv_as_nv(b)));\n"
"}\n"
"static inline MirVal mv_ge(MirVal a, MirVal b) {\n"
"    if (!mv_orderable(a, b)) { mv_throw(mv_str(\"type_mismatch\")); return mv_boo(0); }\n"
"    if (a.type == MV_STR) return mv_boo(mv_str_cmp(a, b) >= 0);\n"
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
" * value.  mv_arr_new, mv_arr_get, mv_arr_set and mv_arr_live are the four\n"
" * readers of that slot -- the write path (mv_arr_set) is the one that WRITES\n"
" * it, extending the count and, when a region moves, replacing it with a\n"
" * forwarding head (mv_arr_live's note).\n"
" * Growth doubles into a new mv_alloc block and leaves the old block to the\n"
" * exit sweep; there is no free list yet and this route reclaims nothing\n"
" * mid-run, which is mv_str_concat's debt word for word (a loop that builds N\n"
" * arrays allocates as the pool grows and frees once, at exit).\n"
" *\n"
" * WHAT IT DOES NOT COVER, each named by the thing that would need it:\n"
" *   - OP_INDEX_SET IS emitted (mv_arr_set), so a literal is no longer\n"
" *     read-only.  What the write does NOT reproduce is written in mv_arr_set's\n"
" *     own note: a DICT container, the interning on the way out, the memory\n"
" *     accounting of the slot it overwrote, and the whole dict/set half of\n"
" *     L_INDEX_SET.  The pool still moves a region only at the high-water mark\n"
" *     or behind a forwarding head -- it is never re-indexed under a handle a\n"
" *     program still holds, which is the property that makes an array aliasable.\n"
" *   - OP_NEW_SET is not emitted, and prescan refuses the preset `set` global\n"
" *     by name, so no SET can reach this pool.  OP_NEW_DICT now IS emitted\n"
" *     (this file's constructor note, above mv_dic_new), and a dict reaching\n"
" *     mv_arr_get is answered by that function's own MV_DIC arm rather than\n"
" *     by the nil tail -- the first version of this route had no such arm and\n"
" *     answered every dict read `nil` silently, see that member's note.\n"
" *     The nil tail now stands for the tags the route cannot build at all\n"
" *     (set, function, closure): the interpreter's else branch\n"
" *     (src/vm/vm.c:3712-3714) and the right answer for nil, an int, a float\n"
" *     and a bool.\n"
" *   - `say a` -- printing a container -- used to be a STOP, not an answer:\n"
" *     mv_say handed every tag but nil and string to nv_say through mv_as_nv,\n"
" *     and mv_tag_to_nv aborts on MV_ARR where the interpreter prints a\n"
" *     bracketed list.  Both are history: mv_vts, the port of\n"
" *     value_to_string beside mv_say, prints the bracketed list, the braced\n"
" *     pair list and set(N) at the MirVal level, and the six mir_print_*\n"
" *     criteria compare its bytes.  The bullet stays because the STOP is why\n"
" *     the function exists and the criteria are what keep it from coming back.\n"
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
"/* MV_ARR's #define is above, beside the other tags: generated C is compiled\n"
"   in block order, and the equality pair names the tag before this block. */\n"
"\n"
"static MirVal *mv_arrmem = 0;   /* the item pool: one flat block, doubled */\n"
"static int mv_arrcap = 0, mv_arrused = 0;\n"
"\n"
"/* Make the pool hold slots [0 .. want-1].  ONE mv_alloc per doubling, plus one\n"
"   pass over what is already live -- that is what keeps the (allocs, bytes) pair\n"
"   a number a reader can predict instead of one this file reports after the\n"
"   fact.  Two callers, one rule: a fresh literal (mv_arr_new) and a write that\n"
"   outgrows its region (mv_arr_set). */\n"
"static inline void mv_arr_need(int want) {\n"
"    if (want <= mv_arrcap) return;\n"
"    int cap = mv_arrcap ? mv_arrcap : 8;\n"
"    while (cap < want) cap *= 2;\n"
"    MirVal *fresh = (MirVal *)mv_alloc(sizeof(MirVal) * (size_t)cap);\n"
"    for (int i = 0; i < mv_arrused; i++) fresh[i] = mv_arrmem[i];\n"
"    mv_arrmem = fresh;                     /* old block: swept at exit */\n"
"    mv_arrcap = cap;\n"
"}\n"
"\n"
"static inline MirVal mv_arr_new(int n, const MirVal *items) {\n"
"    int want = mv_arrused + n + 1;             /* 1 count slot, then n items */\n"
"    mv_arr_need(want);\n"
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
"/* THE SECOND CONTAINER THIS ROUTE BUILDS -- a heap DICT, built once and read\n"
" * only (OP_NEW_DICT + the dict branch of OP_INDEX_GET).  Same note structure\n"
" * as the array block above: WHY / WHAT IT ADDS / WHAT IT DOES NOT COVER /\n"
" * DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  The array block's note ends with the reason this one does:\n"
" * `x = {\"a\": 1}` followed by `say x[\"a\"]` was refused WHOLE -- OP_NEW_DICT\n"
" * had no entry in op_regs and its 2*n argument-stack slots were never\n"
" * consumed, so prescan refused the program before the interpreter's answer\n"
" * (1) could be reached (measured: `IR-MIR-MIR-ONLY|case=mir_dict_new_get|\n"
" * mir=refused|why=illegal-operand`, detail `leaves the argument stack\n"
" * unbalanced: asp=4`).  A dict is the second container a program can spell,\n"
" * and it is the first one whose VALUE is a pair list, so the lowering has to\n"
" * say out loud how a key is found.\n"
" *\n"
" * WHAT IT ADDS.  One tag, MV_DIC = VAL_DICT, and ONE POOL: mv_arrmem is the\n"
" * dict's storage too, because the interpreter keeps a dict in its array pool\n"
" * (L_NEW_DICT calls vm_array_new then pushes 2*n values, src/vm/vm.c:3648-3673,\n"
" * and the handle it writes is `aidx + 1`) -- the same handle an MV_ARR value\n"
" * carries.  The count header is the ITEM count, 2*pairs, which is ArrayObj's\n"
" * `count` for a dict and is why `i * 2 < count` below is the position bound.\n"
" * mv_dic_new lays the pairs out in PUSH ORDER: key0, value0, key1, value1 ...\n"
" * the order L_NEW_DICT's push_n writes (src/vm/vm.c:3666) and the order the\n"
" * interpreter's items[] keeps, because its hash table is an INDEX over items,\n"
" * never a reordering of them.  That is what lets an int subscript mean `pair i`\n"
" * here exactly as it does there.\n"
" * Lookup is a LINEAR SCAN over the pairs with mv_eq, not a hash table.  The\n"
" * interpreter builds an open-addressing index and still resolves every\n"
" * candidate with val_eq (src/vm/vm.c:1138-1146 dict_hash_find), so a probe\n"
" * that compares keys with val_eq -- mv_eq here -- visits exactly the pair\n"
" * that probe would answer, in the same order.  The hash is a SPEED structure\n"
" * on this route it would be a second owner of one answer, and it would also\n"
" * have to reproduce dict_hash_key's cross-type rule (src/vm/vm.c:1038-1072:\n"
" * 1 and 1.0 hash alike) or the index and mv_eq would disagree about what\n"
" * equal means.  O(n) per read is the price of one equality rule; the pool is\n"
" * tiny and this step is the closed loop, not the fast one.\n"
" *\n"
" * WHAT IT DOES NOT COVER, each named by the thing that would need it:\n"
" *   - OP_INDEX_SET on a dict.  There is no writer here.  mv_arr_set returns\n"
" *     immediately for a non-array container (its own fact 1), so `d[\"a\"] = 9`\n"
" *     is a SILENT no-op on this route while vm_dict_set overwrites the pair\n"
" *     (src/vm/vm.c:1184-1222).  Measured, and named in docs/STATUS.md's\n"
" *     boundary section for this step -- a silent no-op is exactly the shape\n"
" *     that must not be mistaken for an answer.\n"
" *   - Removing a pair (vm_dict_remove), and anything that grows a dict.\n"
" *     A dict region is built complete and never moved, so mv_arr_live's\n"
" *     forwarding head is unnecessary here (and harmless: a dict's header is\n"
" *     MV_INT, never MV_ARR).\n"
" *   - The interpreter's interning on the way out of vm_dict_get\n"
" *     (src/vm/vm.c:1163-1166: a non-resident string read back becomes the\n"
" *     interned one).  This route has no intern table; the bytes printed are\n"
" *     identical and identity is unobservable through the lowered opcodes, the\n"
" *     same debt mv_arr_get's note records for the array arm.\n"
" *   - A dict as a KEY, or two dicts compared: mv_eq has an array-identity row\n"
" *     and no dict row, so a dict operand reaches mv_as_nv and STOPS the\n"
" *     program (abort) where val_eq compares `a->ival == b->ival`\n"
" *     (src/vm/vm.c:421).  Out of this step's scope on purpose: no lowered\n"
" *     opcode asks for it, and naming it is cheaper than half-answering it.\n"
" *\n"
" * DELETE CONDITION (decidable).  Delete this pair in the commit that gives\n"
" * this route a dict/value runtime shared with the interpreter's vm_dict_*\n"
" * (docs/streams/aot-mir-value.md section 4): the check is\n"
" * `grep -c mv_dic_new src/compilation/aot_mir.c` falling to 0 and the dict\n"
" * branch of mv_arr_get collapsing into a call to vm_dict_get.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_dict_new_get,\n"
" * mir_dict_missing_key, mir_dict_key_register and mir_dict_position_key\n"
" * compare stdout BYTES against the interpreter; all four were red before\n"
" * this pair as IR-MIR-MIR-ONLY|case=...|mir=refused|why=illegal-operand.\n"
" * The two reverse criteria in docs/STATUS.md 10.117 flip the missing-key\n"
" * answer and the key comparison separately.  ALLOCS_EXPECT's dict entries\n"
" * are the pool blocks, measured first. */\n"
"static inline MirVal mv_dic_new(int n, const MirVal *items) {\n"
"    int want = mv_arrused + 2 * n + 1;   /* 1 count slot, then 2n items */\n"
"    mv_arr_need(want);\n"
"    MirVal r;\n"
"    r.type = MV_DIC;\n"
"    r.ival = mv_arrused + 1;             /* pair 0 sits at pool[ival] */\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    mv_arrmem[r.ival - 1] = mv_int(2 * n);  /* the ITEM count, as ArrayObj holds */\n"
"    for (int i = 0; i < 2 * n; i++) mv_arrmem[r.ival + i] = items[i];\n"
"    mv_arrused = want;\n"
"    return r;\n"
"}\n"
"\n"
"/* THE THIRD CONTAINER THIS ROUTE BUILDS -- a heap SET, built once and never\n"
" * written (OP_NEW_SET, plus the set arms of mv_in and mv_len).  Same note\n"
" * structure as the array and dict blocks above: WHY / WHAT IT ADDS / WHAT IT\n"
" * DOES NOT COVER / DELETE CONDITION / MEASURED BY.\n"
" *\n"
" * WHY IT EXISTS.  `s = 1, 2` was refused WHOLE: OP_NEW_SET had no entry in\n"
" * op_regs, so its r3 count of pushes was never consumed and prescan refused\n"
" * the program before the interpreter's answer could be reached (measured:\n"
" * IR-MIR-MIR-ONLY|case=mir_set_new_len_in|mir=refused|why=illegal-operand,\n"
" * detail `leaves the argument stack unbalanced: asp=2`) -- the OP_NEW_DICT\n"
" * shape, one container later.\n"
" *\n"
" * WHAT IT ADDS.  One tag, MV_SET = VAL_SET, and the SAME POOL: mv_arrmem\n"
" * holds a set's elements because a set here IS [count][element...], laid out\n"
" * in PUSH ORDER, which is the order L_NEW_SET reads its stack in\n"
" * (src/vm/vm.c:4286-4313).  The count header is the DEDUPED element count,\n"
" * which is also what len() reads (src/runtime/runtime_posix.c:13-19 sums\n"
" * SetObj's iCount and count for a finite set with no components -- the two\n"
" * halves this region keeps interleaved).\n"
" *\n"
" * THE MERGE IS THE INTERPRETER'S, HALF FOR HALF.  vm_set_add\n"
" * (src/vm/vm.c:1872-1920) is asymmetric, and this loop copies the asymmetry\n"
" * instead of running one equality over every pushed pair:\n"
" *   - an INT element binary-searches the sorted i64 half and never looks at\n"
" *     items, so `s = 1.0, 1` is TWO elements (measured on the interpreter:\n"
" *     len 2, `1 in s` true, `1.0 in s` true);\n"
" *   - every other tag scans items with val_eq -- mv_eq here -- and never\n"
" *     looks at i64, which is why the reverse spelling `s = 1, 1.0` is two\n"
" *     elements as well, and why `s = 1, 1` is one.\n"
" * One numeric equality over all pushed pairs answers len 1 for `1.0, 1`;\n"
" * that is the silent-wrong shape this note exists to name, and\n"
" * mir_set_new_membership pins BOTH orders.\n"
" *\n"
" * DEDUP IS ALSO A COMPACTION.  A repeat takes no slot: the live extent is\n"
" * 1 + (distinct count) and the pool's used pointer ends there, so the dropped\n"
" * slots stay free for the next literal.  The interpreter does not store the\n"
" * duplicate either (its i64/items hold distinct values only), so the two\n"
" * agree about what the set CONTAINS; the footprint is this route's own\n"
" * business and is what ALLOCS_EXPECT reads.\n"
" *\n"
" * WHAT IT DOES NOT COVER, each named by the thing that would need it:\n"
" *   - OP_SET_ADD and OP_SET_INTERVAL.  No emitter arm and no runtime: a set\n"
" *     is built complete by its literal and never extended.  A set as an\n"
" *     ELEMENT (`q = p, 4`) reaches this loop as an MV_SET item and mv_eq\n"
" *     stops it, where the interpreter merges p's two halves\n"
" *     (src/vm/vm.c:1921-1930 vm_set_add_comp).\n"
" *   - Set EQUALITY.  This note used to say that `s == t` handed a set to\n"
" *     mv_as_nv and STOPPED the program (abort, rc 134), and to name content\n"
" *     equality as the next cell.  Both are history: mv_eq now has an MV_SET\n"
" *     row that compares BY CONTENT, the same answer the interpreter's L_EQ\n"
" *     gives for a SET/SET pair (src/vm/vm.c:3539-3544 calls set_equal, which\n"
" *     compares the two halves, src/vm/vm.c:2310).  The row does not borrow\n"
" *     MV_DIC's identity shortcut: val_eq IS identity for a dict\n"
" *     (src/vm/vm.c:421) and is FALSE for a set, so an identity row here would\n"
" *     answer false for two equal sets -- a wrong answer, not a stop.  The six\n"
" *     criteria are mir_set_eq_* in tools/aot_mir.test.py; docs/STATUS.md\n"
" *     10.127 records the cell.\n"
" *   - A set as a dict KEY, or as an operand of + / < / min / not: those\n"
" *     reach mv_as_nv or mv_tru's fall-through and abort, which is the same\n"
" *     STOP a dict already takes on this route.  PRINTING a set is no longer\n"
" *     in this list: mv_vts prints `set(N)` with the deduped count, and\n"
" *     mir_print_set_whole pins that `1, 2, 2.0` is three elements.\n"
" *   - `s in arr` (a SET probed against an ARRAY).  The interpreter answers\n"
" *     FALSE for every finite set here, and that is a defect rather than a\n"
" *     rule: the set-vs-array subset branch (src/vm/vm.c:2414-2423) starts\n"
" *     from res = 0, walks a set's ITEMS half only, and never sets res back\n"
" *     to 1 -- so a set made of integers (their values live in the sorted\n"
" *     i64 half) and a set made of floats answer alike.  Measured on the\n"
" *     interpreter: `s = 1, 2` then `s in [1, 2]` is false; `t = 1.0, 2.0`\n"
" *     then `t in [1, 2]` is false too.  A constant false is not a semantic\n"
" *     this route will not reproduce silently.  It does not have to stop any\n"
" *     more either: with mv_eq's set arm a set against an array now answers\n"
" *     FALSE on both sides (measured: `s = 1, 2` / `t = 1.0, 2.0`, both false\n"
" *     against [1, 2], interpreter and MIR alike), so the agreement here is\n"
" *     the measured answer and the DEFECT, stated as such in docs/STATUS.md\n"
" *     10.127 -- not a semantic this route claims to have got right.\n"
" *   - The interpreter's hash structure.  A SetObj has NO hash table at all\n"
" *     (a sorted i64 half plus an items list, src/vm/vm.h:60-69), so the O(n)\n"
" *     scans here are what set_contains itself does: the same answer by the\n"
" *     same route, not a speed trade.\n"
" *\n"
" * DELETE CONDITION (decidable).  Delete this function and the tag in the\n"
" * commit that gives this route a shared set runtime with the interpreter's\n"
" * vm_set_*: the check is `grep -c mv_set_new src/compilation/aot_mir.c`\n"
" * falling to 0 and mv_in's set arm collapsing into a call to set_contains.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_set_new_len_in,\n"
" * mir_set_new_single, mir_set_new_dedup, mir_set_new_membership and\n"
" * mir_set_new_register_elements compare stdout BYTES against the interpreter;\n"
" * all five were red before this function as\n"
" * IR-MIR-MIR-ONLY|case=...|mir=refused|why=illegal-operand.  The two reverse\n"
" * criteria in docs/STATUS.md 10.125 flip the dedup and the membership\n"
" * separately.  ALLOCS_EXPECT's set entries are the pool blocks, measured\n"
" * first: the pool is MV_SLOTS slots of 32 bytes, allocated on first use and\n"
" * doubled after that.\n"
" *\n"
" * THE ENUMERATION THIS REGION IS READ BY is mv_list (its note is beside\n"
" * mv_len): `list(s)` walks the region as the interpreter's two halves, ints\n"
" * ascending first and items in push order after, which is the ONLY reader\n"
" * that observes this region's order at all.  tools/aot_mir.test.py pins it in\n"
" * mir_list_of_a_set, mir_list_mixed_set, mir_list_of_scalars,\n"
" * mir_list_of_an_interval and mir_list_narrows_wide_ints. */\n"
"/* ONE SET IS AN INTERVAL -- the fourth kind of value in the pool, and the only\n"
"   one whose answer is a PREDICATE rather than a list.  The interpreter keeps\n"
"   it as SetObj.kind 2 (src/vm/vm.h:57-69): a domain name, two double bounds and\n"
"   two inclusivity bits, and NOTHING to enumerate -- which is why len() of an\n"
"   interval is 0 and its print is `set(R interval)` rather than a count.\n"
" *\n"
" * STORAGE.  The same pool, four slots, with a header that says what it is:\n"
" *   header slot (ival - 1) : MV_INT, ival == -1      <- -1 means INTERVAL;\n"
" *                                                      a count >= 0 means a\n"
" *                                                      finite set (mv_set_new)\n"
" *   ival + 0 : MV_FLT  lo\n"
" *   ival + 1 : MV_FLT  hi\n"
" *   ival + 2 : MV_INT  flags (1 = lower bound inclusive, 2 = upper inclusive)\n"
" * That -1 is the discriminator every existing set reader already looks at: they\n"
" * all start with `int n = (int)mv_arrmem[iv - 1].ival;` and a negative count is\n"
" * not a count, so no reader can mistake an interval for a finite set by\n"
" * accident.  It also cannot collide with a forwarding head, which is MV_ARR and\n"
" * lives in the same slot for the same reason (mv_arr_live's note).\n"
" *\n"
" * WHY NOT A THIRD TAG.  A tag would be a second classification of the same\n"
" * pool slot, and this file already argues twice against two owners of one\n"
" * answer (the dict's hash table, the set's halves).  The header slot IS the\n"
" * answer to \"what is in this region\", and it is the slot that already carries\n"
" * the count.\n"
" *\n"
" * THE DOMAIN IS ALWAYS R.  The compiler defaults the base name to \"R\"\n"
" * (src/parser/parser.c:514) and the only other spellings the parser reaches are\n"
" * the builtin set names, whose intervals are a DIFFERENT shape -- measured:\n"
" * `Z[0~3]` is a FINITE set (kind 0 with one component, `set(4)`, members\n"
" * enumerated) while `R[0~3)` is kind 2 (`set(0)`, no members).  Only the second\n"
" * is OP_SET_INTERVAL's answer, so a name other than R is refused by name rather\n"
" * than answered as if it were R.\n"
" */\n"
"static inline int mv_set_is_interval(int iv) {\n"
"    return mv_arrmem[iv - 1].type == MV_INT && (int)mv_arrmem[iv - 1].ival < 0;\n"
"}\n"
"static inline MirVal mv_set_interval(double lo, double hi, int flags) {\n"
"    mv_arr_need(mv_arrused + 4);            /* 1 header slot, then 3 payload */\n"
"    MirVal r;\n"
"    r.type = MV_SET;\n"
"    r.ival = mv_arrused + 1;\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    mv_arrmem[r.ival - 1] = mv_int(-1);     /* the INTERVAL discriminator */\n"
"    mv_arrmem[r.ival + 0] = mv_flt(lo);\n"
"    mv_arrmem[r.ival + 1] = mv_flt(hi);\n"
"    mv_arrmem[r.ival + 2] = mv_int(flags);\n"
"    mv_arrused = r.ival + 3;\n"
"    return r;\n"
"}\n"
"/* THE MEMBERSHIP PREDICATE, copied from set_contains's kind-2 arm\n"
"   (src/vm/vm.c:2046-2053): the DOMAIN decides first, then the bounds.  On this\n"
"   route the domain is R and R answers `return 1` for any number\n"
"   (src/vm/vm.c:1998), so what is left is: a non-number is not a member (that\n"
"   test is builtin_contains' first line), NaN is not a member either, and then\n"
"   the two bounds with their inclusivity bits.  A BOOL probe is REFUSED by that\n"
"   first line -- `true in p` is false even though val_is_num accepts a bool\n"
"   elsewhere -- which is why this is written as an explicit tag test and not by\n"
"   calling val_is_num.\n"
" * MEASURED BY mir_interval_membership (five probes in one program). */\n"
"static inline int mv_interval_has(int iv, MirVal x) {\n"
"    if (x.type != MV_INT && x.type != MV_FLT) return 0;\n"
"    double d = (x.type == MV_INT) ? (double)x.ival : x.fval;\n"
"    if (d != d) return 0;                   /* NaN */\n"
"    double lo = mv_arrmem[iv + 0].fval;\n"
"    double hi = mv_arrmem[iv + 1].fval;\n"
"    int flags = (int)mv_arrmem[iv + 2].ival;\n"
"    int loInc = flags & 1, hiInc = (flags >> 1) & 1;\n"
"    if (d < lo || (d == lo && !loInc)) return 0;\n"
"    if (d > hi || (d == hi && !hiInc)) return 0;\n"
"    return 1;\n"
"}\n"
"static inline MirVal mv_set_new(int n, const MirVal *items) {\n"
"    int want = mv_arrused + n + 1;             /* 1 count slot, then n pushes */\n"
"    mv_arr_need(want);\n"
"    MirVal r;\n"
"    r.type = MV_SET;\n"
"    r.ival = mv_arrused + 1;                   /* element 0 sits at pool[ival] */\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    int k = 0;\n"
"    for (int i = 0; i < n; i++) {\n"
"        MirVal x = items[i];\n"
"        int dup = 0;\n"
"        for (int j = 0; j < k; j++) {\n"
"            MirVal y = mv_arrmem[r.ival + j];\n"
"            if (x.type == MV_INT) {\n"
"                if (y.type == MV_INT && y.ival == x.ival) { dup = 1; break; }\n"
"            } else if (y.type != MV_INT && mv_tru(mv_eq(y, x))) { dup = 1; break; }\n"
"        }\n"
"        if (!dup) mv_arrmem[r.ival + k++] = x;\n"
"    }\n"
"    mv_arrmem[r.ival - 1] = mv_int(k);         /* the ELEMENT count, deduped */\n"
"    mv_arrused = r.ival + k;                   /* a dropped slot stays free */\n"
"    return r;\n"
"}\n"
"\n"
"/* ADDING ONE ELEMENT TO A SET -- OP_SET_ADD, the first WRITE this route gives a\n"
" * container other than an array (and the opcode the comprehension lowering\n"
" * needs: `{ x in S | cond }` builds an empty set and adds every member whose\n"
" * condition holds).\n"
" *\n"
" * WHAT IT COPIES.  vm_set_add (src/vm/vm.c:1877-1920) is ASYMMETRIC, and the\n"
" * asymmetry is the whole reason a set has two halves:\n"
" *   - an INT element binary-searches the sorted i64 half, INSERTS in place if\n"
" *     absent (`memmove` + store) and never looks at items;\n"
" *   - every other tag scans `items` with val_eq and APPENDS if absent, never\n"
" *     looking at i64.\n"
" * This route's set region is one flat [count][elem...] run, so \"the int half\"\n"
" * is not a separate array -- the observable consequence is the ORDER, which\n"
" * mv_list already reproduces by sorting the int elements at read time.  What\n"
" * matters here is the MEMBERSHIP RULE, which is the same one mv_set_has uses\n"
" * (cf. `1 in {1.0}` is true, `1.0 in {1}` is false) -- an append that tested\n"
" * one uniform equality would collapse pairs the interpreter keeps apart.\n"
" *\n"
" * GROWTH.  A set region is [count][elem...] and its handle is `ival`; there is\n"
" * no capacity slot, so growing means the region must MOVE.  The move is the\n"
" * array's move, for the array's reason (mv_arr_live's note): a region that is\n"
" * not at the pool's high-water mark cannot extend in place, because the slots\n"
" * above it belong to a later region.  So:\n"
" *   - at the frontier  -> extend in place, the handle stands;\n"
" *   - elsewhere        -> copy to the frontier and leave a FORWARDING HEAD\n"
" *                         behind, exactly as mv_arr_set does.\n"
" * THE FORWARDING HEAD IS WHY EVERY SET READER NOW RESOLVES `ival` FIRST: until\n"
" * this commit a set could not move, so mv_set_has/mv_set_eq read the handle\n"
" * directly, and a moved set would have made an old alias read stale garbage.\n"
" * Those two gained `mv_arr_live` in this commit; mv_len and mv_list already had\n"
" * it because an ARRAY could already move through them.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  A set that is not kind 0 (a finite literal): the\n"
" * interpreter's `if (!s || s->kind != 0) return;` means an INTERVAL silently\n"
" * ignores an add, and this arm does the same -- an interval region has the -1\n"
" * discriminator, so the test is `mv_set_is_interval`.\n"
" *\n"
" * MEASURED BY mir_set_add_* in tools/aot_mir.test.py (a comprehension's\n"
" * membership, dedup, two-element order, and an add that must MOVE the region\n"
" * because a container was built after it). */\n"
"static inline void mv_set_add(MirVal sv, MirVal x) {\n"
"    if (sv.type != MV_SET) return;               /* src/vm/vm.c:4455 */\n"
"    int iv = mv_arr_live((int)sv.ival);\n"
"    if (iv < 1 || iv > mv_arrused) return;\n"
"    if (mv_set_is_interval(iv)) return;          /* :1879 `kind != 0` -> silent */\n"
"    int n = (int)mv_arrmem[iv - 1].ival;\n"
"    for (int j = 0; j < n; j++) {                /* the membership rule, once */\n"
"        MirVal y = mv_arrmem[iv + j];\n"
"        if (x.type == MV_INT) {\n"
"            if (y.type == MV_INT) { if (y.ival == x.ival) return; }\n"
"            else if (mv_tru(mv_eq(y, x))) return;\n"
"        } else if (y.type != MV_INT) {\n"
"            if (mv_tru(mv_eq(y, x))) return;\n"
"        }\n"
"    }\n"
"    int want = n + 1;\n"
"    if (iv + n == mv_arrused) {\n"
"        mv_arr_need(iv + want);\n"
"        mv_arrmem[iv + n] = x;\n"
"        mv_arrmem[iv - 1] = mv_int(want);\n"
"        if (iv + want > mv_arrused) mv_arrused = iv + want;\n"
"        return;\n"
"    }\n"
"    mv_arr_need(mv_arrused + want + 1);\n"
"    int nv = mv_arrused + 1;\n"
"    for (int k = 0; k < n; k++) mv_arrmem[nv + k] = mv_arrmem[iv + k];\n"
"    mv_arrmem[nv + n] = x;\n"
"    mv_arrmem[nv - 1] = mv_int(want);\n"
"    mv_arrused += want + 1;\n"
"    mv_arrmem[iv - 1].type = MV_ARR;   /* the forwarding head (see mv_arr_live) */\n"
"    mv_arrmem[iv - 1].ival = nv;       /* fval SHARES this union slot: writing it\n"
"                                          here would zero the head. */\n"
"    mv_arrmem[iv - 1].sval = 0;\n"
"    mv_arrmem[iv - 1].ptr  = 0;\n"
"    /* The CALLER's register is deliberately left alone: it still holds the old\n"
"       handle, which now names the head, and mv_arr_live resolves it -- the same\n"
"       arrangement mv_dict_set's move relies on.  Rewriting the register instead\n"
"       would be a second answer to the question where-is-my-set, and the second\n"
"       one is the one a stale ALIAS would not see. */\n"
"}\n"
"/* ONE SET'S MEMBERSHIP TEST, half for half, copied from set_contains\n"
"   (src/vm/vm.c:2020-2054) for the only kind this route can build: a finite\n"
"   set with no interval components.  The asymmetry IS the point -- an INT probe\n"
"   binary-searches the int half AND THEN still scans the non-int items (so\n"
"   `1 in {1.0}` is TRUE), while every other tag looks at items only (so\n"
"   `1.0 in {1}` is FALSE).  Scanning by the region's count is why a set's\n"
"   header is the deduped element count and not the push count.  It allocates\n"
"   nothing. */\n"
"static inline int mv_set_has(int iv, MirVal x) {\n"
"    iv = mv_arr_live(iv);   /* sets can GROW now (mv_set_add), so a handle may name a head */\n"
"    if (mv_set_is_interval(iv)) return mv_interval_has(iv, x);   /* kind 2: a predicate */\n"
"    int n = (int)mv_arrmem[iv - 1].ival;\n"
"    for (int j = 0; j < n; j++) {\n"
"        MirVal y = mv_arrmem[iv + j];\n"
"        if (x.type == MV_INT) {\n"
"            if (y.type == MV_INT) { if (y.ival == x.ival) return 1; }\n"
"            else if (mv_tru(mv_eq(y, x))) return 1;\n"
"        } else if (y.type != MV_INT) {\n"
"            if (mv_tru(mv_eq(y, x))) return 1;\n"
"        }\n"
"    }\n"
"    return 0;\n"
"}\n"
"/* set_equal (src/vm/vm.c:2221-2260) compares the two halves SEPARATELY: the\n"
"   count of the sorted i64 half and the count of the item half must EACH match\n"
"   (:2226-2229), then the i64 halves are matched member-for-member and the item\n"
"   halves by val_eq.  The flattened pool this route keeps (mv_set_new dedupes\n"
"   with the same asymmetric rule) does not record which half a slot came from,\n"
"   but it does not have to: an INT element can only have come from the i64 half\n"
"   and anything else only from the items half, so \"same element count and every\n"
"   element has a partner OF ITS OWN CLASS\" is that same question.  An INT here\n"
"   pairs an INT only -- unlike mv_set_has above, whose cross-class mv_eq call is\n"
"   set_contains's rule, NOT set_equal's.\n"
"   MEASURED BY mir_set_eq_int_vs_float, which `1, 1` against `1.0, 1.0` answers\n"
"   FALSE through exactly this class check while mir_set_eq_mixed_order answers\n"
"   TRUE through it; one numeric equality for everything gets both wrong. */\n"
"static int mv_set_eq(int ia, int ib) {\n"
"    /* set_equal's kind-2 row (src/vm/vm.c:2225-2227): two intervals are equal\n"
"       when their DOMAIN, bounds and inclusivity all match -- and the domain is\n"
"       always R here, so the comparison is the three payload slots.  An interval\n"
"       against a finite set is FALSE, not a stop: set_equal's kind-0/kind-2\n"
"       crossings require an empty literal with exactly one component, which\n"
"       mv_set_new never builds (it stores components as flattened elements). */\n"
"    ia = mv_arr_live(ia); ib = mv_arr_live(ib);   /* see mv_set_has' note */\n"
"    int iai = mv_set_is_interval(ia), ibi = mv_set_is_interval(ib);\n"
"    if (iai || ibi) {\n"
"        if (!iai || !ibi) return 0;\n"
"        return mv_arrmem[ia + 0].fval == mv_arrmem[ib + 0].fval\n"
"            && mv_arrmem[ia + 1].fval == mv_arrmem[ib + 1].fval\n"
"            && mv_arrmem[ia + 2].ival == mv_arrmem[ib + 2].ival;\n"
"    }\n"
"    int na = (int)mv_arrmem[ia - 1].ival;\n"
"    int nb = (int)mv_arrmem[ib - 1].ival;\n"
"    if (na != nb) return 0;              /* :2226-2229 iCount and count */\n"
"    for (int i = 0; i < na; i++) {\n"
"        MirVal x = mv_arrmem[ia + i];\n"
"        int found = 0;\n"
"        for (int j = 0; j < nb; j++) {\n"
"            MirVal y = mv_arrmem[ib + j];\n"
"            if (x.type == MV_INT) {\n"
"                if (y.type == MV_INT && y.ival == x.ival) { found = 1; break; }\n"
"            } else if (y.type != MV_INT) {\n"
"                if (mv_tru(mv_eq(y, x))) { found = 1; break; }\n"
"            }\n"
"        }\n"
"        if (!found) return 0;\n"
"    }\n"
"    return 1;\n"
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
"\n"
"/* THE TRY STATE AND ITS ONE SEAM.  A `try` opens a frame, a throw closes the\n"
" * innermost one, and control reaches the catch by the CALLER's check rather\n"
" * than by unwinding the C stack.  That is the whole reason this is not a\n"
" * setjmp/longjmp lowering, and the reason is measurable:\n"
" *\n"
" *   gcc -O2, `int f(void) { MirVal v[4]; ... if (setjmp(jb)) { say(v[1]); }\n"
" *   v[1] = mv_int(2); longjmp(jb, 1); }` prints the SETJMP-TIME value of\n"
" *   v[1], not the one written before the longjmp -- the C standard lets the\n"
" *   compiler keep a non-volatile local in a register, and longjmp restores\n"
" *   registers.  The interpreter's throw does NOT roll registers back: a\n"
" *   throw in a try body leaves every register as the body left it (vm_throw\n"
" *   restores code/ip/sp/base and keeps the frame's own registers).\n"
" *   tools/aot_mir.test.py's mir_try_keeps_a_register_written_in_the_body\n"
" *   pins that with a value only the written register can produce.\n"
" *\n"
" * `volatile MirVal v[]` would fix the rollback and break the emitter: OP_CONCAT\n"
" * hands `&v[r2]` to mv_concat_n as a `MirVal *`, and volatile does not convert.\n"
" * So the frame records the ADDRESS of the catch variable (a static global, not\n"
" * a local) and the throwing site sets a flag; the instruction that could throw\n"
" * carries a check that jumps to its own catch label.\n"
" *\n"
" * mv_try_var[] is 64 slots because the STATIC nesting is known at emit time but\n"
" * recursion re-enters the same frame, so the dynamic depth is not.  A program\n"
" * that nests deeper than 64 at run time stops loudly there (mv_try_new); the\n"
" * interpreter grows its exc_stack instead, so this is a boundary, not an answer.\n"
" */\n"
"static int    mv_try_depth;\n"
"static MirVal *mv_try_var[64];\n"
"static MirVal mv_err;\n"
"static int    mv_throwing;\n"

"static void   mv_die_uncaught(MirVal a);   /* defined below; mv_try_new stops there */\n"
"static void   mv_try_new(MirVal *var) {\n"
"    if (mv_try_depth >= 64) mv_die_uncaught(mv_str(\"try nesting deeper than 64\"));\n"
"    mv_try_var[mv_try_depth] = var;\n"
"    mv_try_depth++;\n"
"}\n"
"\n"
"static inline int mv_index_i32(MirVal a) {\n"
"    switch (a.type) {\n"
"    case MV_INT:   return (int)a.ival;\n"
"    case MV_FLT:   return (int)a.fval;\n"
"    case MV_BOO:   return a.ival ? 1 : 0;\n"
"    default:       break;            /* MV_NIL / MV_STR / MV_ARR / MV_DIC:\n"
"                                        vm.c:328 */\n"
"    }\n"
"    mv_throw(mv_str(\"type_mismatch\"));\n"
"    return 0;   /* not reached: the instruction that called this is followed by\n"
"                   its own `if (mv_throwing)` check, which jumps to the catch (or\n"
"                   returns to the caller's check).  Spelled as a 0 so a compiler\n"
"                   that cannot see that still cannot hand back a silent index. */\n"
"}\n"
"\n"
"/* THE UNCAUGHT-THROW EXIT.  src/vm/vm.c:2639-2643 is the rule, read there:\n"
" * when no handler is open, vm_throw prints the thrown VALUE on stderr and\n"
" * stops the thread with rc 1 --\n"
" *   `fprintf(stderr, \"\\n[exception] uncaught: %s\\n\", (err->type ==\n"
" *    VAL_STRING && err->sval) ? err->sval : \"(non-string value)\")`,\n"
" * then the `  at ip=... frames=...` ledger, then `vm->last_error = 1`.\n"
" * This route has no handler stack yet -- the try/catch half is refused\n"
" * WHOLE by the pre-scan (OP_TRY_START/OP_TRY_END have no case in op_regs) --\n"
" * so every throw it can emit is uncaught, and the answer that is observable\n"
" * is exactly this one: the message on stderr, stdout left where the program\n"
" * got to, exit code 1.\n"
" *\n"
" * WHY IT TAKES A MirVal AND NOT A STRING.  The message is the VALUE IN THE\n"
" * REGISTER, not a category word.  The two stops this block already prints\n"
" * (type_mismatch in mv_index_i32 above, index_out_of_range in mv_arr_set\n"
" * below) are CONSTANTS, and copying that template is the cheap, wrong\n"
" * answer: `throw \"boom\"` and `throw \"from-func\"` would print the same\n"
" * word.  The emitted arm hands v[r1] here for that reason, and a case whose\n"
" * only throw said \"boom\" could not see the difference.\n"
" *\n"
" * WHY THIS LIVES INSIDE kMirValBlock AND NOT IN THE SHARED PREAMBLE.\n"
" * aot_native_preamble() is byte-identical in both routes and\n"
" * tools/aot_mir.test.py's classify() takes the first differing line and\n"
" * requires the common prefix to still carry nv_say, nv_fmt_double and\n"
" * nv_die_division_by_zero; a helper spliced between them would make every\n"
" * c-diff unclassifiable.  Appending inside this block is the one safe place\n"
" * (see kMirValBlock's own header note above).\n"
" *\n"
" * WHAT IT DOES NOT REPRODUCE.  The leading blank line, the two ledger lines\n"
" * `  at ip=...` / ` frames=...` and `--err-json`'s `{\"error\":\"exception\",...}`\n"
" * shape (src/vm/vm.c:2617-2636): this route has no bytecode ip, no frame\n"
" * ledger and no such flag.  So the criterion on stderr is a CONTAINMENT\n"
" * predicate (tools/aot_mir.test.py's STDERR_EXPECT) -- these named\n"
" * substrings, not byte equality -- and the ALLOCS line atexit prints after\n"
" * this exit is a third, deliberate difference from the interpreter's tail.\n"
" */\n"
"static void mv_die_uncaught(MirVal a) {\n"
"    fflush(stdout);   /* the lines say() already wrote stay written */\n"
"    fprintf(stderr, \"[exception] uncaught: %s\\n\",\n"
"            (a.type == MV_STR && a.sval) ? a.sval : \"(non-string value)\");\n"
"    exit(1);          /* atexit(mv_free_all) still reports the pool */\n"
"}\n"
"\n"
"/* THE SEAM ITSELF.  Called by a throw, by an out-of-range subscript, and by the\n"
" * two OP_BIND stops -- every site the interpreter routes through vm_throw.\n"
" * With a frame open it writes the thrown value into the catch variable (the\n"
" * frame's ADDRESS, so a `catch { }` with no variable records (MirVal*)0),\n"
" * remembers the value, and sets the flag; with none open it is exactly\n"
" * mv_die_uncaught.  mv_throw does not allocate and does not leave the frame,\n"
" * so every register keeps the value the body wrote.\n"
" */\n"
"static void mv_throw(MirVal a) {\n"
"    if (mv_try_depth > 0) {\n"
"        MirVal *var = mv_try_var[--mv_try_depth];\n"
"        if (var) *var = a;\n"
"        mv_err = a;\n"
"        mv_throwing = 1;\n"
"        return;\n"
"    }\n"
"    mv_die_uncaught(a);\n"
"}\n"
"\n"
"/* THE FORWARDING HEAD.  A live array's count header is an MV_INT (the count).\n"
"   A region that had to MOVE leaves `{MV_ARR, new_index}` in that slot instead,\n"
"   and every reader resolves through it here: the array keeps ONE handle for\n"
"   its whole life even after the allocator copies its items, which is the\n"
"   property the interpreter gets from its ArrayObj (src/vm/vm.c:976-998 keeps\n"
"   `aidx` fixed while only `a->items` moves).  Without this head, `b = a;`\n"
"   followed by a growing write through `a` would leave `b` pointing at the OLD\n"
"   length -- the pool would be re-indexed under a handle the program still\n"
"   holds.  A chain is bounded by the number of moves of one array; no cycle is\n"
"   constructible (a head always names a region created LATER), and the hop\n"
"   budget below turns a corrupt pool into a stop instead of a hang. */\n"
"static inline int mv_arr_live(int iv) {\n"
"    int hops = 0;\n"
"    while (iv >= 1 && iv <= mv_arrused && mv_arrmem[iv - 1].type == MV_ARR) {\n"
"        int nv = (int)mv_arrmem[iv - 1].ival;\n"
"        if (nv == iv || ++hops > 64) break;\n"
"        iv = nv;\n"
"    }\n"
"    return iv;\n"
"}\n"
"\n"
"/* THE DICT READ MEMBERS -- the dict branch of OP_INDEX_GET, transcribed from\n"
" * the two functions the interpreter's L_INDEX_GET calls, IN THE ORDER\n"
" * L_INDEX_GET asks them (src/vm/vm.c:3675-3705): the INT-subscript test first\n"
" * (src/vm/vm.c:3683), the key lookup everywhere else (src/vm/vm.c:3698).\n"
" *\n"
" * WHY IT EXISTS.  `d = {\"a\": 1}; say d[\"a\"]` compiles OP_NEW_DICT then\n"
" * OP_INDEX_GET, and before this pair the read fell into mv_arr_get's first test\n"
" * (obj.type == MV_ARR), which a dict handle fails, so every dict read answered\n"
" * `nil` -- silently, for every key, on a program the route accepted.  That\n"
" * silent nil is the wrong-answer shape this member removes; it is not the\n"
" * interning debt, which the block's constructor note already names.\n"
" *\n"
" * WHAT IT ADDS.  The interpreter's two-level dispatch, branch by branch:\n"
" *   - a subscript whose type is INT is a POSITION, not a key\n"
" *     (src/vm/vm.c:3683 tests `idxv->type == VAL_INT` BEFORE any lookup).\n"
" *     The pair at position i is read out as `items[i * 2]`, i.e. the KEY, and\n"
" *     the bound is `i >= 0 && i * 2 < item_count` (src/vm/vm.c:3688) -- note\n"
" *     that is NOT a `len / 2` count with a negative-from-end rule -- this\n"
" *     route's own helper has one, vm.c has none: 3688 IS the whole bound;\n"
" *     non-negative position read and a negative subscript answers nil, exactly\n"
" *     as the string container arm's `i < 0` rule does further down.  A position\n"
" *     past the last pair is nil (src/vm/vm.c:3688-3696).\n"
" *   - every other tag is a KEY LOOKUP through vm_dict_get\n"
" *     (src/vm/vm.c:3697-3698): a linear probe whose acceptance test is\n"
" *     val_eq (src/vm/vm.c:1138-1146 dict_hash_find), whose absent answer is\n"
" *     nil (src/vm/vm.c:1158 + :1171), and whose hit is a COPY of the value (\n"
" *     src/vm/vm.c:1162-1168, the same value_copy/strdup shape the array arm's\n"
" *     note records).  mv_eq is this route's val_eq, so the scan below visits\n"
" *     exactly the pairs that probe would, in the same order, with one equality\n"
" *     rule; the hash table is a speed structure and a second owner of one\n"
" *     answer (the constructor note's argument).\n"
" *\n"
" * WHAT IT DOES NOT COVER (the read's own share; the writer's share is the\n"
" * constructor note's):\n"
" *   - `mv_eq` has no MV_DIC row, so a DICT-KEYED lookup reaches mv_as_nv and\n"
" *     STOPS the program where val_eq compares `a->ival == b->ival`\n"
" *     (src/vm/vm.c:421).  Measured and listed in docs/STATUS.md 10.117's\n"
" *     honesty section; a dict key is not reachable from any lowered case, and\n"
" *     answering it through mv_as_nv would be the half-answer the constructor\n"
" *     note refuses.\n"
" *   - The interning on the way out (src/vm/vm.c:3689-3693 for the position\n"
" *     arm, src/vm/vm.c:1163-1166 for the key arm): bytes identical, identity\n"
" *     unobservable through the lowered opcodes -- the array arm's same debt.\n"
" *\n"
" * DELETE CONDITION (decidable).  Same as the constructor pair's: the commit\n"
" * that gives this route a shared value runtime drops these two functions and\n"
" * calls vm_dict_get; the check is `grep -c mv_dict_get src/compilation/aot_mir.c`\n"
" * falling to 0 while the dict MIR_ONLY cases stay green.\n"
" *\n"
" * MEASURED BY.  mir_dict_position_key is the case the POSITION branch cannot\n"
" * pass without (it reads keys `a`, `b` and then nil from position 2), and\n"
" * mir_dict_key_register is the case the KEY branch cannot (two resident keys,\n"
" * both ival == 1, so an ival comparison answers `1` for every read).  The\n"
" * two reverse criteria in docs/STATUS.md 10.117 flip each branch separately. */\n"
"static inline MirVal mv_dict_get_at(MirVal obj, MirVal idxv) {\n"
"    int iv = mv_arr_live((int)obj.ival);        /* a dict never moves, so the\n"
"                                                   forwarding head is a no-op\n"
"                                                   here; through it anyway so\n"
"                                                   both arms share one range\n"
"                                                   rule (the array arm's) */\n"
"    if (iv < 1 || iv > mv_arrused) return mv_nil();   /* not a live region */\n"
"    int n = (int)mv_arrmem[iv - 1].ival;        /* the ITEM count, 2*pairs */\n"
"    int k = (int)idxv.ival;\n"
"    if (k < 0 || k * 2 >= n) return mv_nil();   /* src/vm/vm.c:3685/3689-3691 */\n"
"    return mv_arrmem[iv + k * 2];               /* the KEY at pair k */\n"
"}\n"
"/* THE KEY COMPARATOR.  A dict key is accepted by the INTERPRETER's `val_eq`\n"
"   (src/vm/vm.c:1144 dict_hash_find, and the same test in vm_dict_set's\n"
"   lookup), NOT by the equality opcode's rule -- and `val_eq` has no VAL_SET\n"
"   row (its same-type chain, src/vm/vm.c:413-428, lists INT/FLOAT/STRING/BOOL/\n"
"   NIL/ARRAY/DICT/FUNCTION and its numeric tail wants two numbers), so a set\n"
"   answers false against a set, against its own handle and against anything\n"
"   else.  A program therefore INSERTS a pair it can never find again: `d[s] = 7;\n"
"   say d[s]` prints nil, and two content-identical sets as separate keys stay\n"
"   two pairs.  mv_eq answers sets by CONTENT (10.127, mv_set_eq) because\n"
"   OP_EQ/OP_NEQ must; a dict key must NOT share that answer, so this member\n"
"   is the dict's rule: NO for an MV_SET on either side, everything else to\n"
"   mv_eq.  It AGREES WITH A DEFECT -- the target is `print what the interpreter\n"
"   prints`, and the interpreter's key rule has no set row.  Measured by\n"
"   mir_set_dict_key_lookup and mir_set_dict_key_append, both red before this\n"
"   member as IR-MIR-EXEC-DIFF: lookup 7/7/1 against nil/nil/1, append 1/9\n"
"   against 2/nil (the write side matched CONTENT and overwrote pair 0 where\n"
"   the interpreter appended pair 1). */\n"
"static inline MirVal mv_key_eq(MirVal a, MirVal b) {\n"
"    if (a.type == MV_SET || b.type == MV_SET) return mv_boo(0);\n"
"    return mv_eq(a, b);\n"
"}\n"
"static inline MirVal mv_dict_get(MirVal obj, MirVal idxv) {\n"
"    /* src/vm/vm.c:3683: the INT test comes first, BEFORE the region check,\n"
"       because a bad position answer is nil and not a stop. */\n"
"    if (idxv.type == MV_INT) return mv_dict_get_at(obj, idxv);\n"
"    int iv = mv_arr_live((int)obj.ival);        /* same rule as the position arm */\n"
"    if (iv < 1 || iv > mv_arrused) return mv_nil();\n"
"    int n = (int)mv_arrmem[iv - 1].ival;\n"
"    for (int p = 0; p + 1 < n; p += 2)          /* key, value, key, value ... */\n"
"        if (mv_tru(mv_key_eq(mv_arrmem[iv + p], idxv)))\n"
"            return mv_arrmem[iv + p + 1];       /* src/vm/vm.c:1162 value_copy */\n"
"    return mv_nil();                            /* src/vm/vm.c:1158-1161 */\n"
"}\n"
"\n"
"/* THE CONTAINER READER.  Named after its first member; it now answers the\n"
"   three containers one OP_INDEX_GET can be handed (array, dict, string),\n"
"   dispersed the way the interpreter's L_INDEX_GET disperses them.  The\n"
"   subscript conversion MOVED INSIDE the array and string arms when the dict\n"
"   arm arrived: it used to sit above the first test, which was right while\n"
"   every container in the function converted, and is wrong for the dict arm,\n"
"   whose non-INT subscripts are KEY LOOKUPS and are never converted -- a\n"
"   STRING subscript through mv_index_i32 would print type_mismatch and exit\n"
"   where the interpreter answers d[\"a\"]'s value. */\n"
"static inline MirVal mv_arr_get(MirVal obj, MirVal idxv) {\n"
"    if (obj.type == MV_ARR) {\n"
"        int i = mv_index_i32(idxv);   /* a bad subscript is type_mismatch even\n"
"                                         when the array is empty\n"
"                                         (src/vm/vm.c:3678-3680) */\n"
"        int iv = mv_arr_live((int)obj.ival);              /* through the head */\n"
"        if (iv < 1 || iv > mv_arrused) return mv_nil();   /* not a live slot */\n"
"        int n = (int)mv_arrmem[iv - 1].ival;              /* the count header */\n"
"        if (i < 0 || i >= n) return mv_nil();             /* src/vm/vm.c:1002 */\n"
"        return mv_arrmem[iv + i];\n"
"    }\n"
"    if (obj.type == MV_DIC) return mv_dict_get(obj, idxv);  /* src/vm/vm.c:3681 */\n"
"    if (obj.type == MV_STR) {\n"
"        /* The other container this subset can hold, answered the way\n"
"           src/vm/vm.c:3700-3711 answers it: a negative subscript counts from\n"
"           the end, out of range on either side is nil, and a hit is a NEW\n"
"           one-character string (strdup(buf) there, mv_alloc(2) here -- ival 0,\n"
"           heap-owned, swept at exit).  A loop of these allocates per read\n"
"           exactly as the interpreter does. */\n"
"        int i = mv_index_i32(idxv);  /* the conversion the array arm does */\n"
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
"/* THE MEMBERSHIP ARM -- OP_IN, answered branch by branch from the one\n"
" * function the interpreter's L_IN calls.\n"
" *\n"
" * WHY IT EXISTS.  `x in a` is OP_IN, and before this arm the opcode had no\n"
" * emitter, so `say 2 in [1, 2]` was refused WHOLE even though the array\n"
" * literal beside it lowered.  The operand order is the compiler's,\n"
" * `emit(comp->curBC, OP_IN, result, left, right)`\n"
" * (src/compiler/compiler.c:727): r2 is the ELEMENT, r3 the CONTAINER, and r1\n"
" * receives a bool -- `set_contains_or_subset(vm, &R[ins.r2], &R[ins.r3])`\n"
" * writing R[ins.r1] (src/vm/vm.c:4351-4356).\n"
" *\n"
" * WHAT IT ADDS.  The handler's arms in the handler's order, on this route's\n"
" * one value representation:\n"
" *   - b ARRAY, a ARRAY: SUBSET by val_eq -- every element of a must appear\n"
" *     among b's (:2408-2413); true for an EMPTY a.  This is the arm that\n"
" *     needs mv_eq's array row above; there is no other way to write it.\n"
" *   - b ARRAY, a anything else: MEMBERSHIP by val_eq (:2425-2428).\n"
" *   - b SET, a ARRAY: SUBSET -- every element of a must be in b\n"
" *     (:2383-2393); an EMPTY or invalid a is TRUE (the interpreter's own\n"
" *     `return 1`, commented `empty array: subset is true`).\n"
" *   - b SET, a SET: the same loop, because the interpreter calls set_subset\n"
" *     (:2394), which walks a's elements and asks set_contains(b) for each\n"
" *     (src/vm/vm.c:2340-2374); mv_set_has is that question.  An invalid a is\n"
" *     FALSE here (set_subset's `if (!a || !b) return 0`) where an invalid\n"
" *     ARRAY a is TRUE -- the handler really does answer those two malformed\n"
" *     handles differently, and the guards below keep them apart.\n"
" *   - b SET, a anything else: MEMBERSHIP (:2395, set_contains -- mv_set_has).\n"
" *     This is the arm `2 in p` takes, and the one the reverse criterion\n"
" *     mir_set_new_membership flips.  The interpreter's half-for-half rule --\n"
" *     an INT probe searches the int half and THEN still scans the items, a\n"
" *     non-INT probe scans the items only -- is mv_set_has's, so this arm does\n"
" *     not re-state it.\n"
" *   - b anything else: FALSE (:2432) -- a dict, a string, a number,\n"
" *     nil.  This is the branch a reader is most likely to want to fix:\n"
" *     there is no substring test (a string inside a string answers FALSE,\n"
" *     measured on this tree) and no key test.  The interpreter has no such\n"
" *     rule either, so inventing one would make this route DISAGREE with the\n"
" *     answer it exists to reproduce.\n"
" * Zero allocation: it reads the pool and calls mv_eq; it never builds a value.\n"
" *\n"
" * WHAT IT DOES NOT COVER, each a decision:\n"
" *   - The SET container arm's OWN uncovered half.  The b SET arms above cover\n"
" *     a finite set with no interval components -- every set this route can\n"
" *     build -- for a SET, an ARRAY or a scalar.  What is NOT covered: the\n"
" *     interval components (:2396-2404, builtin_contains over comps), the\n"
" *     `set`/`Z` preset globals (still refused by name in prescan: `say 2 in Z`\n"
" *     refuses on the Z name, measured), and a SET probed against an ARRAY\n"
" *     (`s in arr`), which the interpreter answers FALSE for every set -- a\n"
" *     defect, not a rule: the branch at src/vm/vm.c:2414-2423 initialises\n"
" *     res to 0 and never sets it to 1, so even a set whose elements are all\n"
" *     present reads false (measured: `s = 1, 2` / `t = 1.0, 2.0`, both\n"
" *     false against [1, 2]).  This route reaches the\n"
" *     scalar loop below with an MV_SET and answers what the loop answers --\n"
" *     false, the same value as the interpreter, because mv_eq's set arm\n"
" *     answers false for a set against a non-set.  It used to STOP the program\n"
" *     (abort, rc 134) instead; that stop is gone with the set arm, and\n"
" *     docs/STATUS.md 10.127 says plainly that the agreement is with a defect.\n"
" *   - OP_SET_ADD / OP_SET_INTERVAL.  OP_SET_ADD is not lowered, so a set is\n"
" *     built whole by its literal and never extended; the interpreter's\n"
" *     extending forms (a comprehension's OP_SET_ADD, `q = p, 4`'s\n"
" *     vm_set_add_comp) now stop AT OP_SET_ADD -- the comprehension used to\n"
" *     stop one opcode earlier, at the OP_CALL_BUILTIN `list` that is its\n"
" *     first bytecode, and that opcode is emitted now (mv_list's note).\n"
" *     MEASURED: `{ x in s | x > 1 }` reports IR-MIR-NOT-YET|op=OP_SET_ADD|n=1.\n"
" *   - A DICT container used to refuse BEFORE OP_IN, because OP_NEW_DICT's\n"
" *     pops were not counted and prescan stopped at the unbalanced argument\n"
" *     stack (asp=2).  With OP_NEW_DICT emitted that program now lowers, and\n"
" *     what it must answer is the interpreter's FALSE (:2432 -- a dict is not\n"
" *     a key set there), which is the b-non-array-answers-false line below\n"
" *     extended to a tag that was unreachable when this note was written.\n"
" *   - The malformed-handle answers (:2399 for b, :2406 for a).  mv_arr_new\n"
" *     never builds one, so both branches here are unreachable GUARDS mirroring\n"
" *     the interpreter's own live-slot test (the same check mv_arr_get's line\n"
" *     above performs), not semantics this route produces.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish): delete when either (a) set and\n"
" * dict containers get real MV tags and their arms are shared with the\n"
" * interpreter, or (b) OP_IN is lowered through a shared NV -- the check is\n"
" * `grep -c mv_in src/compilation/aot_mir.c` falling to 2.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py MIR_ONLY mir_in_array_hit, _miss,\n"
" * mir_in_string, mir_in_scalar, mir_in_computed compare stdout BYTES against\n"
" * the interpreter; all five were red before this arm as\n"
" * IR-MIR-MIR-ONLY|case=...|mir=refused|why=unsupported-opcode.  The two\n"
" * reverse criteria in docs/STATUS.md 10.115 flip the hit and the miss arm\n"
" * separately; this function allocates nothing. */\n"
"static inline MirVal mv_in(MirVal a, MirVal b) {\n"
"    if (b.type == MV_ARR) {\n"
"        int bi = (int)b.ival;\n"
"        if (bi < 1 || bi > mv_arrused) return mv_boo(0);   /* :2399 */\n"
"        int bn = (int)mv_arrmem[bi - 1].ival;\n"
"        if (a.type == MV_ARR) {\n"
"            int ai = (int)a.ival;\n"
"            if (ai < 1 || ai > mv_arrused) return mv_boo(1);  /* :2406 */\n"
"            int an = (int)mv_arrmem[ai - 1].ival;\n"
"            for (int i = 0; i < an; i++) {\n"
"                int found = 0;\n"
"                for (int j = 0; j < bn; j++)\n"
"                    if (mv_tru(mv_eq(mv_arrmem[ai + i], mv_arrmem[bi + j]))) {\n"
"                        found = 1; break;\n"
"                    }\n"
"                if (!found) return mv_boo(0);\n"
"            }\n"
"            return mv_boo(1);\n"
"        }\n"
"        for (int j = 0; j < bn; j++)\n"
"            if (mv_tru(mv_eq(a, mv_arrmem[bi + j]))) return mv_boo(1);\n"
"        return mv_boo(0);\n"
"    }\n"
"    if (b.type == MV_SET) {\n"
"        int bi = (int)b.ival;\n"
"        if (bi < 1 || bi > mv_arrused || !mv_arrmem) return mv_boo(0);\n"
"        if (a.type == MV_ARR) {\n"
"            int ai = (int)a.ival;\n"
"            if (ai < 1 || ai > mv_arrused) return mv_boo(1);  /* :2386 */\n"
"            int an = (int)mv_arrmem[ai - 1].ival;\n"
"            for (int i = 0; i < an; i++)\n"
"                if (!mv_set_has(bi, mv_arrmem[ai + i])) return mv_boo(0);\n"
"            return mv_boo(1);\n"
"        }\n"
"        if (a.type == MV_SET) {\n"
"            int ai = (int)a.ival;\n"
"            if (ai < 1 || ai > mv_arrused) return mv_boo(0);  /* set_subset */\n"
"            int an = (int)mv_arrmem[ai - 1].ival;\n"
"            for (int i = 0; i < an; i++)\n"
"                if (!mv_set_has(bi, mv_arrmem[ai + i])) return mv_boo(0);\n"
"            return mv_boo(1);\n"
"        }\n"
"        return mv_boo(mv_set_has(bi, a));   /* :2395 set_contains */\n"
"    }\n"
"    return mv_boo(0);   /* :2432 -- dict, string, number, nil: false */\n"
"}\n"

"/* len(): the interpreter's own domain, read off src/runtime/runtime_posix.c:11-27,\n"
"   which tests ARRAY, SET, DICT, STRING, INT, FLOAT in that order and falls\n"
"   through to 0 for everything else (nil and bools).  Reproduced arm for arm,\n"
"   SET INCLUDED: for a finite set with no interval components the interpreter\n"
"   sums SetObj's iCount and count, and this route's region stores exactly those\n"
"   distinct elements behind one count slot -- so the slot IS that sum, read\n"
"   without building the array vm_set_to_array would need.  A component-bearing\n"
"   set (an interval) cannot be built here, which is mv_set_new's OP_SET_INTERVAL\n"
"   boundary rather than a hole in this arm.  The DICT arm divides by two\n"
"   because a dict region stores PAIRS, the same /2 src/vm/vm.c:3693 does; FLOAT\n"
"   truncates toward zero, as `(long long)v->fval` does.  It allocates nothing.\n"
"   THE HANDLE IS RESOLVED FIRST (mv_arr_live), like every other reader of a\n"
"   container: a region that was copied to the pool frontier keeps a FORWARDING\n"
"   HEAD at its old handle -- an MV_ARR pool slot whose ival is the NEW handle\n"
"   -- while a live region's header is MV_INT carrying the item count.  Reading\n"
"   a.ival without that hop reads the head as a count: a moved dict answered\n"
"   len 3 for a one-pair dict, and a moved array answered len 7 for a\n"
"   two-element array (measured, tools/aot_mir.test.py's two mir_len_after_*\n"
"   cases: interp 2/3, the route 3/7).  Only ARR and DIC have such heads --\n"
"   they are the two regions that can grow -- and MV_SET's header is MV_INT,\n"
"   so the hop is a no-op on the set arm. */\n"
"static inline MirVal mv_len(MirVal a) {\n"
"    if (a.type == MV_ARR || a.type == MV_DIC || a.type == MV_SET) {\n"
"        int i = mv_arr_live((int)a.ival);   /* through the head, like mv_arr_get */\n"
"        if (i < 1 || i > mv_arrused || !mv_arrmem) return mv_int(0);\n"
"        /* An INTERVAL has nothing to enumerate: the interpreter's len() sums\n"
"           SetObj's iCount + count for a finite set and answers 0 for kind 2 --\n"
"           measured: `len([0, 3))` is 0 while `1 in [0, 3)` is true.  Its header\n"
"           slot holds -1, which is also why the negative test is the arm. */\n"
"        if (a.type == MV_SET && mv_set_is_interval(i)) return mv_int(0);   /* i is already resolved */\n"
"        long long n = (long long)mv_arrmem[i - 1].ival;\n"
"        return mv_int(a.type == MV_DIC ? n / 2 : n);\n"
"    }\n"
"    if (a.type == MV_STR) return mv_int(a.sval ? (long long)strlen(a.sval) : 0);\n"
"    if (a.type == MV_INT) return mv_int(a.ival);\n"
"    if (a.type == MV_FLT) return mv_int((long long)a.fval);\n"
"    return mv_int(0);   /* MV_NIL, MV_BOO -- the interpreter's fall-through */\n"
"}\n"
"\n"
"/* list(): the SECOND builtin this route emits, and the first whose answer is a\n"
"   CONTAINER.  Read src/runtime/runtime_posix.c:326-336 beside it -- the whole\n"
"   function is three answers, and all three are reproduced here rather than\n"
"   inferred:\n"
"     an ARRAY is pushed back AS THE SAME VALUE (pop, then push the same Value),\n"
"       so this returns `a` itself: not a copy, which is observable --\n"
"       `list(a) == a` is true in the interpreter because val_eq's array row\n"
"       compares `ival` (mv_eq's row here does the same) and would be FALSE for\n"
"       a correct-looking copy.  MEASURED: mir_list_of_an_array prints\n"
"       `true` then `[1, 2]`.\n"
"     a SET becomes a NEW array via vm_set_to_array (src/vm/vm.c:2312-2340).\n"
"       Its order is SEGMENTED and that is the fact this function exists to get\n"
"       right: the sorted i64 half is walked FIRST, then SetObj's items in\n"
"       insertion order.  So `3, 1, 2` enumerates [1, 2, 3] while `1, 2.5, 3`\n"
"       enumerates [1, 3, 2.5] and `2.5, 1, 3` enumerates the SAME [1, 3, 2.5],\n"
"       whatever the push order.  A whole-region sort would answer\n"
"       [1, 2.5, 3] -- sorted, plausible, and a silent disagreement with the\n"
"       oracle; that is why this is two passes and not one qsort.\n"
"       The CROSS-HALF dedup is array_has's (src/vm/vm.c:2254-2261, val_eq on\n"
"       each element already emitted), and it is reachable: an int 1 and a bool\n"
"       true are ONE entry of `list` -- MEASURED, `s = 1, true` answers\n"
"       len 2 / list [1] on the interpreter, because val_eq promotes a mixed\n"
"       numeric pair to double.  mv_eq is that row here, so the second pass\n"
"       tests every non-int item against the ints already emitted.\n"
"     EVERYTHING ELSE is nil, and that includes an INTERVAL set: the\n"
"       interpreter's answer for list([0, 3)) is nil (MEASURED) even though\n"
"       vm_set_to_array would enumerate kind 2, because this route's\n"
"       mv_set_interval carries BOUNDS and no members; mv_set_is_interval is\n"
"       the discriminator, the same one mv_len uses for the same header slot.\n"
"   ALLOCATION.  Only the set arm allocates, and it allocates exactly what\n"
"   mv_arr_new allocates for n items (one pool block when the pool has to grow,\n"
"   none when it does not).  The layout is written here rather than handed to\n"
"   mv_arr_new because the order is not the source order: the count header is\n"
"   rewritten at the end to the EMITTED count, which is the compaction mv_set_new\n"
"   already performs when it drops a duplicate.  The int half is emitted by\n"
"   repeated minimum-above-last selection -- O(k^2) over a literal's handful of\n"
"   elements, and it needs no scratch buffer, which keeps this arm's allocation\n"
"   reading a single number a reader can predict. */\n"
"static inline MirVal mv_list(MirVal a) {\n"
"    if (a.type == MV_ARR) return a;            /* :328 -- the same Value back */\n"
"    if (a.type != MV_SET) return mv_nil();     /* :330 idx < 0 -> push_nil */\n"
"    int iv = mv_arr_live((int)a.ival);\n"
"    if (iv < 1 || iv > mv_arrused || !mv_arrmem) return mv_nil();\n"
"    if (mv_set_is_interval(iv)) return mv_nil();   /* kind 2: nothing to enumerate */\n"
"    int n = (int)mv_arrmem[iv - 1].ival;\n"
"    if (n < 0) return mv_nil();\n"
"    mv_arr_need(mv_arrused + n + 1);           /* the destination region */\n"
"    MirVal r;\n"
"    r.type = MV_ARR;\n"
"    r.ival = mv_arrused + 1;\n"
"    r.sval = 0;\n"
"    r.ptr  = 0;\n"
"    int w = 0;\n"
"    /* PASS 1 -- the int half, ascending.  TWO details are the interpreter's and\n"
"       neither is obvious, so both were measured rather than reasoned:\n"
"       (a) the SELECTION order is the raw i64 half's (SetObj keeps it sorted),\n"
"       (b) the EMITTED value is `(int)s->i64[i]` -- vm_set_to_array:2325\n"
"           narrows to int and stores that in the 64-bit slot.  So\n"
"           `s = 5000000000, 1` enumerates [1, 705032704], not [1, 5000000000]\n"
"           (MEASURED on the interpreter), and the narrowing can COLLIDE: \n"
"           `s = 5000000000, 705032704` is len 2 but list [705032704], because\n"
"           array_has sees the second truncated value already present.  That is\n"
"           why the dup test below runs against what was EMITTED and not\n"
"           against raw ivalues -- the same array_has the items pass leans on. */\n"
"    long long last = 0;\n"
"    int first = 1;\n"
"    for (int i = 0; i < n; i++) {\n"
"        long long pick = 0;\n"
"        int have = 0;\n"
"        for (int j = 0; j < n; j++) {\n"
"            MirVal y = mv_arrmem[iv + j];\n"
"            if (y.type != MV_INT) continue;\n"
"            if (!first && y.ival <= last) continue;\n"
"            if (!have || y.ival < pick) { pick = y.ival; have = 1; }\n"
"        }\n"
"        if (!have) break;\n"
"        last = pick;\n"
"        first = 0;\n"
"        MirVal e = mv_int((long long)(int)pick);   /* :2325 -- the (int) narrow */\n"
"        int dup = 0;\n"
"        for (int t = 0; t < w; t++) {\n"
"            if (mv_tru(mv_eq(mv_arrmem[r.ival + t], e))) { dup = 1; break; }\n"
"        }\n"
"        if (!dup) mv_arrmem[r.ival + w++] = e;\n"
"    }\n"
"    /* PASS 2 -- everything else, in the region's (push) order, skipping what\n"
"       array_has would: an item equal to an int already emitted. */\n"
"    for (int j = 0; j < n; j++) {\n"
"        MirVal x = mv_arrmem[iv + j];\n"
"        if (x.type == MV_INT) continue;\n"
"        int dup = 0;\n"
"        for (int t = 0; t < w; t++) {\n"
"            if (mv_arrmem[r.ival + t].type != MV_INT) continue;\n"
"            if (mv_tru(mv_eq(mv_arrmem[r.ival + t], x))) { dup = 1; break; }\n"
"        }\n"
"        if (!dup) mv_arrmem[r.ival + w++] = x;\n"
"    }\n"
"    mv_arrmem[r.ival - 1] = mv_int(w);         /* the EMITTED count */\n"
"    mv_arrused = r.ival + w;\n"
"    return r;\n"
"}\n"
"\n"
"/* THE WRITE HALF of the container step.  Read src/vm/vm.c:3717-3734 (L_INDEX_SET)\n"
" * beside this: r1 is the CONTAINER, r2 the SUBSCRIPT, r3 the VALUE, and r1 is\n"
" * NOT a destination -- the container register keeps its own value across the\n"
" * write (src/compiler/bytecode.h:16 spells the instruction\n"
" * `reg[r1][r2] = reg[r3]`).  The emitter therefore calls this for SIDE EFFECTS\n"
" * only, and the four facts below are copied from that handler and from\n"
" * vm_array_set (src/vm/vm.c:976-998) rather than invented:\n"
" *\n"
" * 1. THE SUBSCRIPT IS CONVERTED INSIDE THE ARRAY BRANCH.  `if (obj->type ==\n"
" *    VAL_ARRAY) { int i = ... }` -- so `x = 5; x[nosuchglobal] = 7` is a SILENT\n"
" *    no-op and never reaches val_as_double, while the same bad subscript on the\n"
" *    READ side throws (mv_arr_get converts first, src/vm/vm.c:3678-3680).  That\n"
" *    asymmetry is the interpreter's, measured (tools/aot_mir.test.py's\n"
" *    mir_index_set_edges: `say x` still prints 5), and it is the one place\n"
" *    where OP_INDEX_SET and OP_INDEX_GET must NOT share an arm.\n"
" * 2. A NEGATIVE SUBSCRIPT THROWS, a too-large one DOES NOT: `if (i < 0)` calls\n"
" *    vm_throw_kind with index_out_of_range (src/vm/vm.c:3721-3724), and then the\n"
" *    write EXTENDS the array with nil fill (`while (a->count <= i)\n"
" *    value_copy(...)`, src/vm/vm.c:987-990).  So `say a[4]` after `a[5] = 9`\n"
" *    prints nil, not an error -- the read side's answer for the same index, for\n"
" *    a different reason.\n"
" * 3. THE VALUE IS THE VALUE.  Nothing coerces it (`tmp` is a value_copy, not an\n"
" *    int): storing the string hi into the second slot of [1, 2, 3] makes that\n"
" *    slot a string.  The clone-before-store ORDER in vm_array_set is what makes\n"
" *    `a[0] = a[2]` safe across a reallocating growth; this route has no clone to\n"
" *    order -- it copies four fields of a register that nothing else moves.\n"
" * 4. A DEAD SLOT IS A no-op (src/vm/vm.c:977), reproduced for the same reason\n"
" *    the read side reproduces it: an out-of-range handle is not an error there.\n"
" *\n"
" * ALLOCATION.  An in-range write is 32 bytes of assignment and ZERO mv_alloc --\n"
" * ALLOCS_EXPECT measures that.  A growing write allocates only when the POOL\n"
" * block has to double, or when the region is not at the pool high-water mark\n"
" * and has to move (the lower half of this function); both numbers are measured\n"
" * and written down in tools/aot_mir.test.py, not estimated here.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  A DICT container (src/vm/vm.c:3727-3728) is not\n"
" * reachable on this route, and the silent no-op it would get here is the right\n"
" * answer only by accident of being unreachable; the intern step vm_array_get\n"
" * performs on the way OUT (src/vm/vm.c:1006-1009) is not reproduced, and neither\n"
" * is the memory ACCOUNTING the interpreter does on the slot it overwrote (the\n"
" * value_free of the old element and vm->used_mem): this route never frees\n"
" * mid-run, so an overwritten string's bytes stay in the exit sweep.  A CONSTANT\n"
" * subscript on an array a write has GROWN is still refused by the pre-scan\n"
" * (index-out-of-range-constant) -- its length knowledge predates the write and\n"
" * is not revived, and docs/STATUS.md section 10.114 carries the measured\n"
" * reading.  The whole dict/set half of L_INDEX_SET is un-emitted. */\n"
"/* THE DICT WRITE MEMBER -- the dict branch of OP_INDEX_SET.\n"
" *\n"
" * WHY IT EXISTS.  `d[\"a\"] = 9` compiled OP_INDEX_SET, and the emitted C handed\n"
" * it to mv_arr_set, whose first fact returns immediately for a non-array\n"
" * container.  So the write was a SILENT no-op: the program printed the OLD\n"
" * value and exited 0, which is the one shape that must never be mistaken for an\n"
" * answer.  Measured before this member existed:\n"
" * `IR-MIR-EXEC-DIFF|case=mir_dict_set_overwrite|interp-out=b'1\\n9\\n2\\n'|mir-out=b'1\\n1\\n2\\n'`.\n"
" *\n"
" * WHAT IT REPRODUCES.  vm_dict_set (src/vm/vm.c:1184-1222) in the order it does\n"
" * things: look the key up with the SAME equality the read arm uses (val_eq there,\n"
" * mv_eq here -- src/vm/vm.c:1138-1146 resolves every hash candidate with val_eq),\n"
" * overwrite the value in place when the key is found (src/vm/vm.c:1200-1204), and\n"
" * append the pair when it is not (src/vm/vm.c:1208-1218).  Position matters and\n"
" * is preserved: an overwrite leaves the pair where it is, an append goes to the\n"
" * end, so an INT subscript (the read arm's pair position) keeps naming the same\n"
" * pair the interpreter names.\n"
" *\n"
" * THE GROWTH PATH IS THE ARRAY PATH, for the array path's reason: a dict region\n"
" * that ends at the pool frontier extends where it is, and one that does not is\n"
" * COPIED to the frontier with a forwarding head left behind (mv_arr_live), so the\n"
" * handle the program holds keeps naming the items.  The one difference is what\n"
" * the header counts: an array's header is the element count, a dict's is the ITEM\n"
" * count (2*pairs), which is why the append adds two.\n"
" *\n"
" * WHAT IT DOES NOT COVER, named by the thing that would need it:\n"
" *   - Removing a pair (vm_dict_remove) -- no opcode reaches it, same as the read\n"
" *     arm's note.\n"
" *   - vm_dict_ensure/growth beyond what one append needs: this appends exactly\n"
" *     one pair per call, which is what L_INDEX_SET does.\n"
" *   - Dropping a heap-owned string VALUE on overwrite (vm_dict_set calls\n"
" *     value_free on the old value, src/vm/vm.c:1201).  This route's strings are\n"
" *     pool-resident or swept at exit, and it has no free for either, so the\n"
" *     reference is dropped without a free.  Measured by ALLOCS_EXPECT, which\n"
" *     counts pool blocks and heap strings actually allocated.\n"
" *\n"
" * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases mir_dict_set_overwrite,\n"
" * mir_dict_set_append and mir_dict_set_grow_after_neighbour, all three red\n"
" * before this member as IR-MIR-EXEC-DIFF (bytes, not a crash) and all three\n"
" * with the same interpreter answer printed next to the route's.  ALLOCS_EXPECT\n"
" * carries what each one allocates, measured first.\n"
" */\n"
"static inline void mv_dict_set(MirVal obj, MirVal idxv, MirVal valv) {\n"
"    int iv = mv_arr_live((int)obj.ival);\n"
"    if (iv < 1 || iv > mv_arrused) return;   /* src/vm/vm.c:1185 */\n"
"    int n = (int)mv_arrmem[iv - 1].ival;     /* the ITEM count, 2*pairs */\n"
"    for (int p = 0; p + 1 < n; p += 2)\n"
"        if (mv_tru(mv_key_eq(mv_arrmem[iv + p], idxv))) {\n"
"            mv_arrmem[iv + p + 1] = valv;    /* the pair stays where it is */\n"
"            return;\n"
"        }\n"
"    int want = n + 2;                        /* one more key/value pair */\n"
"    if (iv + n == mv_arrused) {\n"
"        /* Ends at the frontier: extend in place, the handle is unchanged. */\n"
"        mv_arr_need(iv + want);\n"
"        mv_arrmem[iv + n] = idxv;\n"
"        mv_arrmem[iv + n + 1] = valv;\n"
"        mv_arrmem[iv - 1] = mv_int(want);\n"
"        if (iv + want > mv_arrused) mv_arrused = iv + want;\n"
"        return;\n"
"    }\n"
"    /* Not at the frontier: a LATER region owns the slots above, so copy this\n"
"       dict to the frontier and leave the forwarding head behind. */\n"
"    mv_arr_need(mv_arrused + want + 1);\n"
"    int nv = mv_arrused + 1;\n"
"    for (int k = 0; k < n; k++) mv_arrmem[nv + k] = mv_arrmem[iv + k];\n"
"    mv_arrmem[nv + n] = idxv;\n"
"    mv_arrmem[nv + n + 1] = valv;\n"
"    mv_arrmem[nv - 1] = mv_int(want);\n"
"    mv_arrused += want + 1;\n"
"    mv_arrmem[iv - 1].type = MV_ARR;   /* the forwarding head (see mv_arr_live) */\n"
"    mv_arrmem[iv - 1].ival = nv;       /* fval SHARES this union slot: writing it\n"
"                                          here would zero the head. */\n"
"    mv_arrmem[iv - 1].sval = 0;\n"
"    mv_arrmem[iv - 1].ptr  = 0;\n"
"}\n"
"static inline void mv_arr_set(MirVal obj, MirVal idxv, MirVal valv) {\n"
"    if (obj.type == MV_DIC) return mv_dict_set(obj, idxv, valv);\n"
"    if (obj.type != MV_ARR) return;   /* src/vm/vm.c:3726-3733, fact 1 */\n"
"    int i = mv_index_i32(idxv);       /* fact 1: after the container check */\n"
"    if (i < 0) {                      /* fact 2: src/vm/vm.c:3721-3724 */\n"
"        fprintf(stderr, \"[exception] uncaught: index_out_of_range\\n\");\n"
"        exit(1);\n"
"    }\n"
"    int iv = mv_arr_live((int)obj.ival);\n"
"    if (iv < 1 || iv > mv_arrused) return;       /* fact 4: src/vm/vm.c:977 */\n"
"    int n = (int)mv_arrmem[iv - 1].ival;         /* the count header */\n"
"    if (i < n) {                                 /* overwrite: no allocation */\n"
"        mv_arrmem[iv + i] = valv;                /* fact 3 */\n"
"        return;\n"
"    }\n"
"    int want = i + 1;\n"
"    if (iv + n == mv_arrused) {\n"
"        /* The region ends at the pool high-water mark, so nothing lives in the\n"
"           slots above it: it extends where it is, and the handle -- with every\n"
"           alias -- keeps naming the same items.  ZERO mv_alloc unless the pool\n"
"           block itself doubles; the measured pair is in ALLOCS_EXPECT. */\n"
"        mv_arr_need(iv + want);\n"
"        for (int k = n; k < i; k++) mv_arrmem[iv + k] = mv_nil();   /* fact 2 */\n"
"        mv_arrmem[iv + i] = valv;                                  /* fact 3 */\n"
"        mv_arrmem[iv - 1] = mv_int(want);\n"
"        if (iv + want > mv_arrused) mv_arrused = iv + want;\n"
"        return;\n"
"    }\n"
"    /* NOT at the high-water mark: the slots after this region belong to a LATER\n"
"       array, so it cannot extend in place.  Copy it to the frontier and leave a\n"
"       forwarding head behind (mv_arr_live's note): one mv_alloc if the pool\n"
"       block doubles, and nothing per element -- a move is a copy, not a\n"
"       reallocation of the items. */\n"
"    mv_arr_need(mv_arrused + want + 1);\n"
"    int nv = mv_arrused + 1;\n"
"    for (int k = 0; k < n; k++) mv_arrmem[nv + k] = mv_arrmem[iv + k];\n"
"    for (int k = n; k < i; k++) mv_arrmem[nv + k] = mv_nil();\n"
"    mv_arrmem[nv + i] = valv;\n"
"    mv_arrmem[nv - 1] = mv_int(want);\n"
"    mv_arrused += want + 1;\n"
"    mv_arrmem[iv - 1].type = MV_ARR;   /* the forwarding head */\n"
"    mv_arrmem[iv - 1].ival = nv;      /* fval SHARES this union slot: writing it\n"
"                                         here would zero the head. */\n"
"    mv_arrmem[iv - 1].sval = 0;\n"
"    mv_arrmem[iv - 1].ptr  = 0;\n"
"}\n"
"/* THE SCALAR PRINT EXIT -- one of the two print exits in this block.  (It used\n"
" * to be the only one; mv_vts below is the second, for containers, and each\n"
" * note names the condition that deletes it.)\n"
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
"/* THE SECOND PRINT EXIT -- `say` of a CONTAINER, and of the containers nested\n"
" * in it.  `say a` was a STOP before this function: mv_say handed every tag but\n"
" * nil, string and function to nv_say through mv_as_nv, and mv_tag_to_nv aborts\n"
" * on MV_ARR/MV_DIC/MV_SET, so a program whose last line is `say a` printed\n"
" * nothing and died with rc 134.  MEASURED before this cell by the six\n"
" * mir_print_* criteria in tools/aot_mir.test.py, each red as\n"
" * `IR-MIR-EXEC-DIFF|interp-rc=0|mir-rc=-6|mir-out=b''`.\n"
" *\n"
" * WHY A SEPARATE FORMATTER AND NOT nv_say.  NV is {int t; long long i; double\n"
" * f;} -- it has no pointer to a pool region, so there is no way to hand it an\n"
" * array.  Mirroring value_to_string at the MirVal level is the only route that\n"
" * keeps the BYTES the interpreter prints; nv_fmt_double is reused for floats\n"
" * so the numeric spelling still has one implementation.\n"
" *\n"
" * WHAT IT COPIES EXACTLY (src/vm/vm.c's value_to_string, by reading it):\n"
" *   - depth expands only `depth < 2`, so `[[1, 2], [3]]` prints whole while\n"
" *     `[[[1]]]` prints `[[[]]]` (depth 2 renders the empty brackets);\n"
" *   - a set prints `set(N)`, N being the DEDUPED element count read from the\n"
" *     region's header -- `1, 2, 2.0` is three, because the int half never\n"
" *     dedups a float;\n"
" *   - array items go through a 256-byte scratch, dict keys through 128 and\n"
" *     values through 256, and an item that would not FIT is dropped with the\n"
" *     body closed normally (`used + need < bufsz`), which is the interpreter's\n"
" *     own truncation rule;\n"
" *   - scalars use the same spellings the say exit already prints (nil, true,\n"
" *     false, <function>, an int through %lld, a float through nv_fmt_double,\n"
" *     a string as its own bytes).\n"
" *\n"
" * WHAT IT DOES NOT COVER.  Interval components (`set(Z interval)`), because no\n"
" * emitter arm and no runtime can build one on this route (see mv_set_new's\n"
" * note).  A region whose handle no longer resolves -- only reachable if a\n"
" * program prints a dict while another write moves it -- prints `set(?)` for a\n"
" * set and `[]`/`{}` for the container arms, the same \"unknown\" the interpreter\n"
" * prints for a dead set slot, rather than reading freed memory.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish).  Delete this function in the\n"
" * commit where the shared preamble's nv_say() learns the container tags --\n"
" * i.e. where NV grows a pointer and a pool -- and the two exits collapse into\n"
" * one printer for both routes.  Until then this is the print exit that exists\n"
" * because NV cannot carry a container.\n"
" *\n"
" * MEASURED BY.  mir_print_array_whole, mir_print_dict_whole,\n"
" * mir_print_nested_depth, mir_print_set_whole, mir_print_mixed_tags and\n"
" * mir_print_empty compare stdout bytes against the interpreter, and each was\n"
" * red before this cell as an empty aborted run.  It allocates nothing. */\n"
"static void mv_vts(MirVal v, char *buf, int bufsz, int depth) {\n"
"    if (v.type == MV_STR) {\n"
"        int sl = (int)strlen(v.sval ? v.sval : \"\");\n"
"        if (sl > bufsz - 1) sl = bufsz - 1;\n"
"        memcpy(buf, v.sval ? v.sval : \"\", (size_t)sl);\n"
"        buf[sl] = 0;\n"
"        return;\n"
"    }\n"
"    if (v.type == MV_FUN) { snprintf(buf, (size_t)bufsz, \"<function>\"); return; }\n"
"    if (v.type == MV_NIL) { snprintf(buf, (size_t)bufsz, \"nil\"); return; }\n"
"    if (v.type == MV_BOO) { snprintf(buf, (size_t)bufsz, \"%s\", v.ival ? \"true\" : \"false\"); return; }\n"
"    if (v.type == MV_INT) { snprintf(buf, (size_t)bufsz, \"%lld\", (long long)v.ival); return; }\n"
"    if (v.type == MV_FLT) { nv_fmt_double(buf, (size_t)bufsz, v.fval); return; }\n"
"    if (v.type == MV_SET) {\n"
"        int iv = mv_arr_live((int)v.ival);\n"
"        if (iv < 1 || iv > mv_arrused) { snprintf(buf, (size_t)bufsz, \"set(?)\"); return; }\n"
"        /* value_to_string's kind-2 row (src/vm/vm.c:1330) prints the DOMAIN and\n"
"           the word interval, never a count: `set(R interval)`.  The domain is\n"
"           always R on this route (mv_set_interval's note), so the name is a\n"
"           literal here instead of a table lookup. */\n"
"        if (mv_set_is_interval(iv)) { snprintf(buf, (size_t)bufsz, \"set(R interval)\"); return; }\n"
"        snprintf(buf, (size_t)bufsz, \"set(%d)\", (int)mv_arrmem[iv - 1].ival);\n"
"        return;\n"
"    }\n"
"    if (v.type == MV_ARR || v.type == MV_DIC) {\n"
"        int used = 0;\n"
"        int iv = mv_arr_live((int)v.ival);\n"
"        used += snprintf(buf + used, (size_t)(bufsz - used), \"%s\", v.type == MV_ARR ? \"[\" : \"{\");\n"
"        if (depth < 2 && iv >= 1 && iv <= mv_arrused) {\n"
"            int n = (int)mv_arrmem[iv - 1].ival;\n"
"            if (v.type == MV_ARR) {\n"
"                for (int i = 0; i < n; i++) {\n"
"                    char tmp[256];\n"
"                    mv_vts(mv_arrmem[iv + i], tmp, 256, depth + 1);\n"
"                    int need = (i > 0 ? 2 : 0) + (int)strlen(tmp) + 1;\n"
"                    if (used + need < bufsz) {\n"
"                        if (i > 0) used += snprintf(buf + used, (size_t)(bufsz - used), \", \");\n"
"                        used += snprintf(buf + used, (size_t)(bufsz - used), \"%s\", tmp);\n"
"                    } else break;\n"
"                }\n"
"            } else {\n"
"                for (int i = 0; i < n / 2; i++) {\n"
"                    char k[128], vv[256];\n"
"                    mv_vts(mv_arrmem[iv + 2 * i], k, 128, depth + 1);\n"
"                    mv_vts(mv_arrmem[iv + 2 * i + 1], vv, 256, depth + 1);\n"
"                    int need = (i > 0 ? 2 : 0) + (int)strlen(k) + 2 + (int)strlen(vv) + 1;\n"
"                    if (used + need < bufsz) {\n"
"                        if (i > 0) used += snprintf(buf + used, (size_t)(bufsz - used), \", \");\n"
"                        used += snprintf(buf + used, (size_t)(bufsz - used), \"%s: %s\", k, vv);\n"
"                    } else break;\n"
"                }\n"
"            }\n"
"        }\n"
"        snprintf(buf + used, (size_t)(bufsz - used), \"%s\", v.type == MV_ARR ? \"]\" : \"}\");\n"
"        return;\n"
"    }\n"
"    snprintf(buf, (size_t)bufsz, \"<unknown>\");\n"
"}\n"
"static inline void mv_say(MirVal a) {\n"
"    if (a.type == MV_NIL) { printf(\"nil\\n\"); return; }\n"
"    /* A function prints the ONE word the interpreter prints for one\n"
"     * (src/vm/vm.c:1379, value_to_string's VAL_FUNCTION arm).  It ignores the\n"
"     * payload on purpose: `<function>` is the whole spelling in both routes,\n"
"     * the closure's captures included, so nothing here has to read ptr. */\n"
"    if (a.type == MV_FUN) { printf(\"<function>\\n\"); return; }\n"
"    if (a.type == MV_ARR || a.type == MV_DIC || a.type == MV_SET) {\n"
"        char buf[1024];\n"
"        mv_vts(a, buf, 1024, 0);\n"
"        printf(\"%s\\n\", buf);\n"
"        return;\n"
"    }\n"
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
/* 1 when the program declares a constraint (`name: set [= init]`).  The
   declaration is what makes a plain global store a CHECKED store
   (src/vm/vm.c's L_STORE_GLOBAL consults vm->global_bound), so the emitter
   guards its store arm on this program-level fact: a program with no
   declaration keeps the C bytes it had before this grid, and the cover test's
   c-identical/c-diff classification does not move for it.  One declaration
   anywhere in the program turns the guard on everywhere, which is exactly the
   interpreter's rule -- the table is global, not per-store. */
static int   g_bound_used;
/* Set by the pre-scan when the program carries a try pair.  Two things follow
   from it and both are measured rather than assumed: the emitter adds the
   `if (mv_throwing)` check after every instruction that can throw, and non-main
   containers get an `mv_try_mark` so a returning function pops the frames that
   belong to it (src/vm/vm.c:4142 is the interpreter's version of that pop).
   Nothing else in the emitted text moves when no try pair is present. */
static int   g_try_used;
/* Set by a whole-program scan when any container carries OP_WAIT.  `wait`'s
   answer is TIME, not a value, so what the flag buys is the runtime helper that
   performs the delay (mv_wait) -- emitted only when a program can reach it,
   which keeps every program without a `wait` byte-identical in generated C.
   The scan is done here rather than in the pre-scan because this opcode reads
   NO pool, NO global and NO table: a pre-scan case would be an arm with nothing
   to validate, and the cover instrument counts exactly that as a dead label
   (see its operand detector). */
static int   g_wait_used;

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
    /* OP_NEW_DICT's operand layout is OP_NEW_ARRAY's, read out of its own
       handler: r1 is the register the dict value lands in, r3 is the PAIR
       COUNT (a NUMBER, not a register: src/compiler/compiler.c:1060 emits
       `OP_NEW_DICT, r, 0, expr->dict.count`, and src/vm/vm.c:3648-3673 reads
       ins.r1 and ins.r3 only), r2 is not read at all.  The 2*n pushed slots
       are not registers of this instruction either, for the same reason the
       array's elements are not -- the compiler pushes them
       (src/compiler/compiler.c:1054-1056) and the emitter collects them off
       mvargs -- so the mask is r1 alone and the count's own bound lives in the
       pre-scan, next to OP_NEW_ARRAY's. */
    case OP_NEW_DICT: return R1;
    /* OP_NEW_SET's operand layout is OP_NEW_ARRAY's a third time, read out of
       its own handler: r1 is the register the set value lands in, r3 is the
       element COUNT (a NUMBER, not a register: src/compiler/compiler.c:566
       emits `OP_NEW_SET, r, 0, expr->list.count`, and src/vm/vm.c:4286-4313
       reads ins.r1 and ins.r3 only -- its `int n = ins.r3` is the width of the
       stack mouth), r2 is not read at all.  The n pushed slots are not
       registers of this instruction either (src/compiler/compiler.c:562-565
       pushes one OP_PUSH_REG per element), so the mask is r1 alone and the
       count's own bound lives in the pre-scan, next to OP_NEW_ARRAY's and
       OP_NEW_DICT's.  ONE READING DIFFERS FROM THE ARRAY'S: the pushes are not
       n slots of output, they are n CANDIDATES for one set, so the mouth
       consumes n while the value keeps only the DISTINCT ones -- the dedup is
       vm_set_add's (src/vm/vm.c:1872-1920) and mv_set_new's note has it. */
    case OP_NEW_SET: return R1;
    /* OP_INDEX_GET reads r2 (the container) and r3 (the subscript) and writes
       r1: `R[ins.r1] = ...`, src/vm/vm.c:3675-3716, where the handler's own
       first two lines are `Value *obj = &R[ins.r2]; Value *idxv = &R[ins.r3];`.
       The compiler emits `emit(OP_INDEX_GET, r, arr, idx)`
       (src/compiler/compiler.c:1067-1068) with idx the register the subscript
       was compiled into, and it is r3 that the handler reads; bytecode.h:15
       spells the same three operands in one line of comment.  So r3 is a real
       register bound here even though that comment's bracket writes r1. */
    case OP_INDEX_GET: return R1 | R2 | R3;
    /* OP_IN reads r2 (the ELEMENT) and r3 (the CONTAINER) and writes r1 with a
       bool: `Value la = R[ins.r2], rb = R[ins.r3]; int res =
       set_contains_or_subset(vm, &la, &rb); value_set(&R[ins.r1], VAL_BOOL,
       res, ...)` (src/vm/vm.c:4351-4356), emitted as
       `emit(comp->curBC, OP_IN, result, left, right)`
       (src/compiler/compiler.c:727).  bytecode.h:48 spells the same three
       operands.  ALL THREE are registers -- unlike OP_NEW_ARRAY's count -- so
       the generic bound loop below covers them and prescan needs no case for
       this opcode (and must not be given one: the cover test's operand
       detector reads a prescan case as "this opcode reads an index", and
       OP_IN's third operand is a container register, not an index). */
    case OP_IN: return R1 | R2 | R3;
    /* OP_INDEX_SET is the WRITE half of the index pair, and it does NOT share
       OP_INDEX_GET's operand ORDER: the handler's first three lines are
       `Value *obj = &R[ins.r1]; Value *idxv = &R[ins.r2]; Value *valv = &R[ins.r3];`
       (src/vm/vm.c:3718-3720), i.e. r1 is the CONTAINER, r2 the SUBSCRIPT and r3
       the VALUE, and bytecode.h:16 spells the same order (`index set:
       reg[r1][r2] = reg[r3]`).  One trap sits inside that comment: it writes
       `reg[r1] = ...`, which reads like a destination, but r1 is read, never
       written -- there is no `R[ins.r1] =` anywhere in L_INDEX_SET, the write
       lands in the container's items, and the compiler emits
       `emit(OP_INDEX_SET, arr, idx, val)` (src/compiler/compiler.c:2371-2379)
       with `arr` the register the container was compiled into.  So NOTHING
       here is a destination and all three are still registers: the mask bounds
       the container, the subscript and the value, and the generic bound loop
       below is the only thing that does. */
    case OP_INDEX_SET: return R1 | R2 | R3;
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
    /* The SET REDUCTIONS.  `r1 = min(r2)` (src/compiler/bytecode.h:49-50):
       ONE operand, in r2; r3 is not an operand and must not be read as a
       register (reading it would index v[0]).  The emitter answers nil for
       every non-set operand, which is the interpreter's own first statement --
       see mv_minmax's note in kMirValBlock for the value-domain argument. */
    case OP_MIN: case OP_MAX: return R1 | R2;
    case OP_LOAD_GLOBAL: return R1;
    case OP_STORE_GLOBAL: return R2;
    /* OP_BIND -- `name: set [= init]`, the only declaration shape the parser
       has (src/parser/parser.c, the `looks_like_set_start` branch).  The
       compiler emits it as emit(comp->curBC, OP_BIND, g, setReg, initReg)
       (src/compiler/compiler.c, STMT_BIND), so r1 is a GLOBAL id and not a
       register, r2 is the register holding the declared constraint, and r3 is
       the initialiser register -- or -1 when the declaration had no `= value`.
       That -1 is why the mask says R2 and not R2|R3: a mask is a property of
       the OPCODE, while "is r3 an operand" is a property of THIS instruction.
       The pre-scan's own arm checks r3 instead (a negative or over-limit r3 is
       refused there), which is the same arrangement OP_STORE_GLOBAL uses for
       its global id. */
    case OP_BIND: return R2;
    /* OP_RECORD -- `record name = value [, tags]`, and `record default store =
       value`.  Emitted as emit(curBC, OP_RECORD, g, (meta << 16) | sidx, rs)
       (src/compiler/compiler.c, STMT_RECORD), so r1 is a GLOBAL id (or -1 for
       the default-store form, which has no name at all), r2 packs the name's
       string-pool index under the meta byte, and r3 is the register holding the
       value -- the register the interpreter's arm MAY overwrite with a value
       found in a loaded save file.
       The mask is R3 and not 0: r3 is a register in the IR's own terms, and a
       route that one day carries the store would write exactly there.  This
       route does not carry one, so its arm reads r3 and writes nothing (see the
       emitter's arm and IR-MIR-RECORD's criteria); r1/r2 are immediates here,
       not registers, which is why neither appears in the mask. */
    case OP_RECORD: return R3;
    case OP_JUMP: return 0;
    case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: return R1;
    case OP_PUSH_REG: return R1;
    /* OP_SET_INTERVAL -- `[lo, hi)` / `(lo, hi]`, the interval-set literal.
       Operands read off the compiler (src/compiler/compiler.c, EXPR_SETINTERVAL):
       the two BOUNDS are compiled first and pushed as OP_PUSH_REG, then this is
       emitted as `emit(curBC, OP_SET_INTERVAL, r, (nameIdx & 0xFFFFFF) | (flags
       << 24), 0)`.  So r1 is the destination register, r2 is a PACKED IMMEDIATE
       (string-pool index of the domain name in the low 24 bits, the two
       inclusivity bits on top) and r3 is unused -- the first opcode in this
       route whose registers are NOT all in the operand word, which is why its
       arm reads mvargs.
       The mask is R1 alone: a bit for r2 would have the generic bound loop read
       a string-pool index as a register number. */
    case OP_SET_INTERVAL: return R1;
    /* OP_LOCK reads NO register and writes none.  Its two fields are a MUTEX
       index (r1, into the container's `mutexes` table -- a run-time object, not
       a pool slot) and a DIRECTION bit (r2: 0 = acquire, 1 = release); the
       compiler emits `emit(comp->curBC, OP_LOCK, midx, 1, 0)` for the release
       and `..., midx, 0, 0` for the acquire.  So the mask is zero, and the
       mutex index is not an operand the pre-scan can bound -- this route does
       not carry a mutex table, and an out-of-range index would be the
       interpreter's own container-shape problem, not an emitted-C hazard.
       The zero mask is also the claim to IR-MIR-CONSERVE: the instruction is
       emitted (as a note), not counted not-yet. */
    case OP_LOCK: return 0;
    /* OP_SET_ADD -- `set_add`, emitted by the COMPREHENSION lowering as
       `emit(comp->curBC, OP_SET_ADD, result, xReg, 0)` (src/compiler/compiler.c,
       EXPR_SETCOMP): r1 is the SET being grown, r2 the element, r3 unused.
       r1 is NOT a destination -- the instruction mutates the container in place,
       and the register keeps its own value (the same convention OP_INDEX_SET
       established), so the mask names r1 because the emitted C READS it, and r2
       because it reads that too. */
    case OP_SET_ADD: return R1 | R2;
    /* OP_RECV -- `recv name [, timeout]`, emitted as
       `emit(comp->curBC, OP_RECV, result, timeout_reg, 0)` (src/compiler/
       compiler.c, STMT_RECV), where timeout_reg is -1 when the source has no
       timeout.  r1 is the DESTINATION register, r2 is the timeout register or
       -1, and r3 is unused.  The -1 is why the mask says R1 only: a mask bit
       for r2 would have the generic bound loop reject a legitimate -1, which is
       the same arrangement OP_BIND uses for its initialiser. */
    case OP_RECV: return R1;
    case OP_CALL_FUNC: return R2;
    /* THE FUNCTION VALUE -- the two opcodes that make a function a VALUE rather
       than a callee the emitter names.  Operands read off the compiler, not
       guessed: OP_MAKE_FUNC is emitted as emit(curBC, OP_MAKE_FUNC, out, fidx,
       fbc->capture_count) (src/compiler/compiler.c:1303) -- r1 is the DESTINATION
       register, r2 the function index (a pool/function index, checked in
       prescan's arm) and r3 the capture COUNT, so only r1 is a register.
       OP_CALL_VALUE is emitted with the callee in r1, the result in r2 and the
       argument count in r3 (src/compiler/compiler.c:883/892/907), the mirror
       image of OP_CALL_FUNC's R2-only row, so BOTH r1 and r2 are registers here
       and r3 is a count that prescan's argument-stack balance consumes.
       Neither opcode reads a compile-time index in its EMITTER (the callee's
       index travels in a register and mv_callv dispatches on it at run time), but
       OP_MAKE_FUNC's emitter DOES read nodes[0].child_ids[fidx] -- the
       translation from the bytecode's main-relative slot to this route's
       depth-first module index -- so it is "indexed" in the coverage test's
       sense and its prescan arm is a real case label; OP_CALL_VALUE is not,
       which is why its bound lives outside the switch beside OP_NEW_ARRAY's
       (see that block's note). */
    case OP_MAKE_FUNC: return R1;
    case OP_CALL_VALUE: return R1 | R2;
    /* THE CAPTURE READ -- one register and one slot, and only the first is a
       register.  Read off the handler, not guessed: OP_LOAD_CAPTURE's whole
       body is `if (!t->closure_env || ins.r2 < 0 ||
       (size_t)ins.r2 >= im_closure_env_size(t->closure_env)) R[r1] = nil;
       else value_assign(&R[r1], env_get(env, r2))` (src/vm/vm.c:4033-4039), so
       r1 is the DESTINATION and r2 is an ENVIRONMENT SLOT INDEX -- never
       v[r2].  The compiler emits it as emit(comp->curBC, OP_LOAD_CAPTURE, r, ci,
       0) (src/compiler/compiler.c:547), ci being the lambda's own capture
       number.  It has NO case label in prescan's switch on purpose: that
       switch's labels are the opcodes whose EMITTER reads a pool / global /
       function / label index, and this arm reads a slot of the environment
       instead (the bound it needs is against a count the bytecode does not
       carry -- see func_cap's note -- and a slot outside it is the interpreter's
       own nil, answered by the arm rather than refused).  A label here would be
       read as a dead check by tools/aot_mir_cover.test.py. */
    case OP_LOAD_CAPTURE: return R1;
    /* r1 is the RESULT register; r2 is a STRING-POOL INDEX naming the builtin
       (compiler.c:464) and r3 is the argument count, so neither is a register.
       Before 10.123 this opcode had no entry and every builtin call was counted
       not-emittable; `len` is emitted now and the rest are refused BY NAME in
       prescan. */
    case OP_CALL_BUILTIN: return R1;
    case OP_RETURN: return R1;
    case OP_SAY: return R1;
    /* OP_THROW reads exactly ONE register, r1, and THAT REGISTER IS THE THROWN
       VALUE.  Two facts, both read off the handler and the compiler rather than
       guessed: the handler's whole body is `vm_throw(vm, t, &R[ins.r1])`
       (src/vm/vm.c:4448-4449), and the three emit sites all pass the expression
       register -- `emit(comp->curBC, OP_THROW, errReg, 0, 0)`
       (src/compiler/compiler.c:616), `..., rethrow, 0, 0` (:2316) and
       `..., e, 0, 0` (:2328).  So r2 and r3 are not operands (the emitter
       reads nothing else), and this mask says precisely which register the
       generated line will index.  It is a one-way READ -- the throw does not
       return, so nothing is written back -- and the mask has no direction bit;
       R1 here means "r1 is a live register", which is all prescan bounds.
       This opcode needs NO case in prescan: r1 is checked by the generic
       bound loop for every mask bit, and there is no pool, global, function
       or label index here for the operand instrument to see. */
    case OP_THROW: return R1;
    case OP_HALT: return 0;
    /* OP_STOP reads no operand and writes none: the interpreter's arm never
       touches R[] (src/vm/vm.c, L_STOP: `t->running = false; continue;`), and
       the compiler emits it with all three fields zero
       (src/compiler/compiler.c, STMT_STOP: `emit(comp->curBC, OP_STOP, 0, 0, 0)`).
       The zero mask is also the claim this route makes to IR-MIR-CONSERVE --
       the instruction is emitted rather than counted not-yet. */
    case OP_STOP: return 0;
    /* OP_YIELD reads no operand and writes none: the compiler emits it with
       all three fields zero (`emit(comp->curBC, OP_YIELD, 0, 0, 0)`), and the
       interpreter's arm touches no register -- its whole work is a guarded
       fiber hand-off (`if (t->is_task && t->fiber_sched) SwitchToFiber(...);`).
       The zero mask is also the claim this route makes to IR-MIR-CONSERVE: the
       instruction is emitted, not counted not-yet. */
    case OP_YIELD: return 0;
    /* OP_TRY_START's r2 is a GLOBAL INDEX -- the catch variable -- not a
       register, and OP_TRY_END's three fields are zero (src/compiler/compiler.c,
       STMT_TRY: `emit(..., OP_TRY_START, 0, varIdx, 0)`).  So both masks are
       zero and the operand checks are their prescan arms' job: a mask bit here
       would have the generic loop read a global index as a register number.
       The zero mask is also the claim to IR-MIR-CONSERVE (both are emitted). */
    case OP_TRY_START: return 0;
    case OP_TRY_END: return 0;
    /* OP_WAIT's r1 is the delay REGISTER and r2/r3 are zero (src/compiler/
       compiler.c, STMT_WAIT: `emit(comp->curBC, OP_WAIT, r, 0, 0)`).  It reads
       no pool and no global, so unlike OP_BIND there is nothing for the pre-scan
       to range-check; whether the helper is emitted is decided by g_wait_used. */
    case OP_WAIT: return R1;
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
    /* 2 KB, and the bound below is not decoration.  This buffer was 512 bytes
       until section 10.137 added two opcodes, and adding them made the list
       longer than the buffer: o += n on a TRUNCATED snprintf returned the
       length the list WANTED, so the next call was handed sizeof desc - o as a
       size_t -- negative once o passed the buffer, i.e. huge -- and wrote past
       the end.  The symptom was not in this function: `aot-mir subset` died with
       "*** buffer overflow detected ***" and the coverage instrument reported
       `REFUSED|the tool does not report its emittable set (IR-MIR-SUBSET)`.
       The description is ~1.2 KB for the current 67 opcodes, so 2048 is
       headroom, and the early break keeps a future addition from repeating the
       same mistake. */
    static char desc[2048];
    if (!desc[0]) {
        size_t o = 0;
        for (int op = 0; op < 256; op++) {
            if (op_regs(op) < 0) continue;
            size_t room = sizeof desc - o;
            int n = snprintf(desc + o, room, "%s%s", o ? "," : "", op_name(op));
            if (n < 0 || (size_t)n >= room) break;   /* would have truncated: stop */
            o += (size_t)n;
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
            /* The container family is checked HERE and not in the switch below,
               and that placement is itself a claim worth stating: every case
               label in this switch exists because its emitter reads a POOL,
               GLOBAL, FUNCTION or LABEL index, and the coverage test's operand
               detector (tools/aot_mir_cover.test.py:288-327) reads the switch's
               labels against the emitter's indexed arms in both directions.
               None of these opcodes reads such an index -- OP_NEW_ARRAY's,
               OP_NEW_DICT's and OP_NEW_SET's r3 are COUNTS (op_regs' notes; the
               emitter turns them into stack offsets) and OP_INDEX_GET's three
               operands are plain registers -- so a case label here would be a
               label the detector calls dead, which is a true reading of the
               wrong question.  The bounds they do need are the arm's own: the
               count versus the pool's register bound, and the count versus the
               argument stack.
               OP_NEW_ARRAY, OP_NEW_DICT and OP_NEW_SET are the argument
               stack's consumers, and the three that make OP_PUSH_REG a
               CONSUMED operand rather than a leak: the elements were pushed one
               at a time by the compiler
               (src/compiler/compiler.c:1042-1045, `emit(OP_PUSH_REG, ...)` per
               element; for a dict 2*count times, key0,value0,key1,value1 ...
               src/compiler/compiler.c:1054-1056; for a set once per element,
               src/compiler/compiler.c:562-565) and the count in r3 says how
               many slots this instruction takes (src/vm/vm.c:3637-3642 reads
               them back as `base = t->sp - (n - 1)`, so the FIRST push is
               element 0; src/vm/vm.c:3658-3666 does the same with 2*n slots,
               and src/vm/vm.c:4286-4313 does it with n for a set).
               The dict consumes `2 * n` slots, the array and the set `n`; all
               are bounded against the SAME r3 bound so the emitted offset
               cannot go absurd.  For the set the consumption is n even though
               the VALUE keeps only the distinct elements (duplicates are
               merged, not stored), so this count is the stack's width and NOT
               the value's element count -- the two are different numbers for
               `s = 1, 1`.
               Bound first, then consume -- a count larger than asp is a
               refusal, not a negative stack. */
            /* OP_SET_INTERVAL is the fourth producer that CONSUMES the argument
               stack, and the first whose operands live ONLY there: the two
               bounds were pushed by the compiler (EXPR_SETINTERVAL) and nothing
               in the operand word names them.  Two slots, always -- both bounds
               are pushed even when the source omits them, because the compiler
               substitutes +/-1.0e308 rather than skipping the push.
               The domain name is checked here and refused BY NAME when it is not
               R: the parser reaches `R[0~3)` (kind 2, the shape this arm
               answers) and `Z[0~3]` (a FINITE set, a different shape this route
               does not build through this opcode) by the same syntax, so
               answering a Z interval as though it were R would be a wrong answer
               rather than a missing one.
               r2 packs the name index and the flags, so the emitter must not
               read it as a register; op_regs' note says the mask is R1 alone. */
            if (op == OP_SET_INTERVAL) {
                int32_t packed = ir_insn_r2(in);
                int32_t nix = packed & 0xFFFFFF;
                if (nix < 0 || nix >= node->string_count)
                    return bad("container=%d insn=%d interval domain name index %d out of range (count=%d)",
                               f, i, nix, node->string_count);
                const char *base = node->string_pool ? node->string_pool[nix] : NULL;
                if (!base || strcmp(base, "R") != 0) {
                    g_prescan_why = "set-interval-domain";
                    return bad("container=%d insn=%d interval over domain \'%s\' -- this route emits R "
                               "intervals only (the builtin domains spell a FINITE set through the same "
                               "syntax and a different opcode shape)", f, i, base ? base : "(null)");
                }
                if (asp < 2)
                    return bad("container=%d insn=%d op=%s argument stack underflows: asp=%d slots=2",
                               f, i, op_name(op), asp);
                asp -= 2;
                continue;
            }
            if (op == OP_NEW_ARRAY || op == OP_NEW_DICT || op == OP_NEW_SET) {
                int n = ir_insn_r3(in);
                int slots = (op == OP_NEW_DICT) ? 2 * n : n;
                if (n < 0 || n >= REG_LIMIT)
                    return bad("container=%d insn=%d op=%s count %d out of range",
                               f, i, op_name(op), n);
                if (asp < slots)
                    return bad("container=%d insn=%d op=%s argument stack underflows: asp=%d slots=%d",
                               f, i, op_name(op), asp, slots);
                asp -= slots;
                continue;
            }
            /* OP_CALL_VALUE CONSUMES THE ARGUMENT STACK WITHOUT READING ANY INDEX,
               so it lives HERE and not as a case label -- the same placement, and
               the same reason, as the container family above (its emitter's
               callee index travels in a register and mv_callv dispatches on it at
               run time, so a case label for it would be one the detector at
               tools/aot_mir_cover.test.py:288-327 reports as dead -- the label
               is left out for that reading, not because the check was skipped;
               this note does not spell the label out, so it cannot match itself).
               The count is r3, the operand the compiler writes as the last
               argument of emit(..., OP_CALL_VALUE, out, callee, res, argCount)
               (src/compiler/compiler.c:883/892/907), and the arguments were
               pushed one at a time by OP_PUSH_REG -- exactly OP_CALL_FUNC's
               convention (the emitter's arm reads them back as
               mvargs[asp-argc..asp-1]).  The balance is kept HONEST rather than
               skipped: a call with arguments that the emitted C could not index
               is a REFUSAL here, not an unbalanced stack downstream, and the
               0-argument form (this cell's criterion) is the case where the
               emitted arm passes the literal 0 instead of mvargs. */
            if (op == OP_CALL_VALUE) {
                int n = ir_insn_r3(in);
                if (n < 0 || n >= REG_LIMIT)
                    return bad("container=%d insn=%d op=%s argument count %d out of range",
                               f, i, op_name(op), n);
                if (asp < n)
                    return bad("container=%d insn=%d op=%s argument stack underflows: asp=%d argc=%d",
                               f, i, op_name(op), asp, n);
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
            /* OP_INDEX_SET is the same shape from the pool's point of view: r1
               is a CONTAINER (bounded by the generic mask loop already run
               above), r2 is a SUBSCRIPT VALUE and r3 is a VALUE -- no pool index,
               no global, no function, no child id.  It is a plain `if` and not
               a `case` on purpose: tools/aot_mir_cover.test.py's operand
               detector reads every opcode `case` label in this function as a
               claim that the opcode checks a pool/global/function/label index and
               compares it two-way against the emitter, where this arm emits
               OP_INDEX_SET with no index read at all -- a `case` here would be
               reported `dead` and `agree=no`.  The runtime half is mv_arr_set's
               by design: the interpreter answers a negative subscript by
               throwing index_out_of_range, a non-numeric one with type_mismatch,
               and a subscript past the end by GROWING the array, so none of the
               three is statically refusable, and a constant subscript that
               prescan_constant_subscripts has not invalidated is still
               refused there (see its note). */
            if (op == OP_INDEX_SET) continue;
            if (op == OP_LOCK) {
                /* THE PAIRED FORM ONLY.  `lock m { ... }` is emitted as
                   acquire(m) ... release(m) -- the same opcode twice with
                   r2 = 0 then 1 -- so the two can be checked against each other
                   statically, and that check is the whole reason this arm exists:
                   the BARE form (`lock m`, r2 = 0 with no release) takes the
                   mutex and never gives it back, and the INTERPRETER HANGS on it
                   (measured: the block form printed `a\nin\nb`, the bare probe
                   never returned and was killed after two minutes).  Reproducing
                   a hang is not agreement, and answering it as "another no-op"
                   would hide exactly the shape a reader most needs told.
                   `unlock m` (r2 = 1 with no acquire) is refused for the mirror
                   reason: it would release a mutex this route never took.
                   The pairing is per MUTEX INDEX within one container, which is
                   enough for the shape the parser writes: a block is contiguous.
                   A mutex index is not a pool slot, so there is nothing to
                   range-check -- op_regs' zero mask says the same thing. */
                int midx = in->dst, dir = in->a & 1;
                int want = -1;
                for (int j = 0; j < fn->ninsns; j++) {   /* find the last acquire of this mutex */
                    const IrInsn *o = &fn->insns[j];
                    if (j == 0) continue;
                    if (o->op != OP_LOCK || o->dst != midx) continue;
                    if ((o->a & 1) == 0) want = j;
                }
                if (dir == 0) {
                    /* an acquire needs a later release in this container */
                    int found = 0;
                    for (int j = i + 1; j < fn->ninsns && !found; j++)
                        if (fn->insns[j].op == OP_LOCK && fn->insns[j].dst == midx
                            && (fn->insns[j].a & 1)) found = 1;
                    if (!found) {
                        g_prescan_why = "lock-unpaired";
                        return bad("container=%d insn=%d acquires mutex %d and never releases it in this "
                                   "container -- the interpreter deadlocks on that shape (measured), so it "
                                   "is refused rather than copied", f, i, midx);
                    }
                }
                if (dir == 1) {
                    /* a release needs an EARLIER acquire of the same mutex */
                    int found = 0;
                    for (int j = 0; j < i && !found; j++)
                        if (fn->insns[j].op == OP_LOCK && fn->insns[j].dst == midx
                            && !(fn->insns[j].a & 1)) found = 1;
                    if (!found) {
                        g_prescan_why = "lock-unpaired";
                        return bad("container=%d insn=%d releases mutex %d without acquiring it in this "
                                   "container -- `unlock m` on its own (the compiler emits the release as the "
                                   "same opcode with r2 = 1)", f, i, midx);
                    }
                }
                (void)want;
                /* `continue`, NOT `break`: this block sits OUTSIDE the operand
                   switch (so the cover instrument's detector does not count it
                   as a dead index case), and a bare `break` would leave the
                   instruction loop before the rest of the per-instruction pass
                   -- measured: the generated C contained `(null) = v[1];` for a
                   global the pass had never marked referenced. */
                continue;
            }
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
            case OP_BIND: {
                /* r1 is the GLOBAL being declared (op_regs' note), r2 the
                   register holding the constraint (checked by the generic mask
                   loop), r3 the initialiser register or -1.  No mask bit
                   covers r3 -- that -1 is legal here -- so the operand check
                   is this arm's job, and it must reject a negative other than
                   -1 as well: the emitter would otherwise write v[-2]. */
                int ix = in->dst, init = ir_insn_r3(in);
                if (ix < 0 || ix >= root->global_name_count || ix >= g_gref_n)
                    return bad("container=%d insn=%d global index %d out of range (count=%d)",
                               f, i, ix, root->global_name_count);
                const char *gname = root->global_names ? root->global_names[ix] : NULL;
                if (is_preset_global(gname))
                    return bad("container=%d insn=%d global %d is the preset set global '%s', whose value is a set, "
                               "not an NV int/float/bool -- outside this subset",
                               f, i, ix, gname);
                if (init < -1 || init >= REG_LIMIT)
                    return bad("container=%d insn=%d initialiser register %d out of range (-1 for none, limit=%d)",
                               f, i, init, REG_LIMIT);
                g_gref[ix] = 1;
                g_bound_used = 1;
                break;
            }
            case OP_TRY_START: {
                /* THE TRY FRAME, opened where the interpreter opens it.  Two
                   claims are worth spelling out because both are measured:
                   (1) the frame is matched to its metadata entry by
                   `start_off == t->ip - 1` (src/vm/vm.c:4420-4433) and an
                   instruction with NO matching entry is a pure no-op there --
                   IR instructions are indexed exactly like the bytecode offsets
                   (build_insns copies one for one), so the test here is
                   `start_off == i`, and the emitter writes only a comment for
                   the no-entry case (see its arm);
                   (2) r2 is the catch VARIABLE, a GLOBAL index -- not a
                   register -- so it is checked here against the same two rules
                   every global arm uses (inside the table, and not the preset
                   set global).
                   r2 may be -1: `catch { }` has no variable and the
                   interpreter's frame then answers `f->var_idx >= 0` with no
                   store (src/vm/vm.c:2580-2585).  A finally-only handler
                   registers the phantom global __inimerse_finally_error so the
                   emitted handler can re-throw it (src/compiler/compiler.c,
                   STMT_TRY), and that global is refused here for the same
                   reason the preset one is: its value is not something this
                   subset can hold.
                   THE ESCAPE SCAN.  A region is the half-open byte range
                   [start_off, end_off].  A jump with BOTH ends inside it is
                   fine.  A jump out of it is refused (`why=try-escapes`)
                   because the interpreter LEAKS the frame there -- measured:
                   `.scratch/probe3/e5.im` (a bare `break` out of a try inside a
                   while loop) leaves exc_depth set, and the throw after the
                   loop is then caught by that stale entry, printing
                   `2\nlate\n` and exiting 0.  There is no answer to copy there,
                   so the shape is refused by name rather than guessed at.
                   A jump from outside INTO the region is refused (`why=try-enters`)
                   for the mirror reason: the frame such a jump would need was
                   never opened. */
                int ix = in->a;
                if (ix >= root->global_name_count || ix >= g_gref_n)
                    return bad("container=%d insn=%d catch variable global index %d out of range (count=%d)",
                               f, i, ix, root->global_name_count);
                if (ix >= 0) {
                    const char *gname = root->global_names ? root->global_names[ix] : NULL;
                    if (is_preset_global(gname))
                        return bad("container=%d insn=%d catch variable %d is the preset set global '%s', "
                                   "whose value is a set, not an NV int/float/bool -- outside this subset",
                                   f, i, ix, gname);
                    g_gref[ix] = 1;
                }
                const TryEntry *te = NULL;
                for (int k = node->try_count - 1; k >= 0; k--)
                    if (node->try_entries[k].start_off == i) { te = &node->try_entries[k]; break; }
                if (!te) break;      /* no entry: the interpreter's arm is a no-op */
                g_try_used = 1;
                int rs = te->start_off, re = te->end_off;
                for (int j = 0; j < fn->ninsns; j++) {
                    const IrInsn *jmp = &fn->insns[j];
                    if (jmp->op != OP_JUMP && jmp->op != OP_JUMP_IF_FALSE
                        && jmp->op != OP_JUMP_IF_TRUE) continue;
                    int32_t t = ir_insn_r2(jmp);
                    int in_src = (j >= rs && j <= re), in_dst = (t >= rs && t <= re);
                    if (in_src && !in_dst) {
                        g_prescan_why = "try-escapes";
                        return bad("container=%d insn=%d leaves the try region [%d,%d] by jumping to %d; "
                                   "the interpreter leaks the frame on that shape (measured, so there is "
                                   "no answer to copy)", f, j, rs, re, (int)t);
                    }
                    if (!in_src && in_dst) {
                        g_prescan_why = "try-enters";
                        return bad("container=%d insn=%d jumps INTO the try region [%d,%d] at %d, whose "
                                   "frame was never opened", f, j, rs, re, (int)t);
                    }
                }
                break;
            }
            case OP_CALL_BUILTIN: {
                /* r2 indexes the CONTAINER's string pool, exactly as
                   OP_LOADK_STRING's r2 does, and r3 is the argument count; the
                   arguments themselves were pushed by OP_PUSH_REG, so this arm
                   CONSUMES them from the argument stack.
                   TWO names are emitted (10.123, and `list` beside it): `len`,
                   whose whole domain is a length or a number and which
                   allocates nothing, and `list`, whose whole domain is an
                   array (returned as-is), a finite set (enumerated) or nil --
                   see mv_list's note for why those three are the answers the
                   interpreter gives.  Every other name is refused BY NAME --
                   the registry, its capability flags and the safe-mode rule
                   live in the VM and the mods, so calling one here would be a
                   second copy of that dispatch (10.122). */
                int32_t ix = ir_insn_r2(in);
                if (ix < 0 || ix >= node->string_count)
                    return bad("container=%d insn=%d builtin name index %d out of range (count=%d)",
                               f, i, ix, node->string_count);
                const char *bn = node->string_pool ? node->string_pool[ix] : NULL;
                if (!bn)
                    return bad("container=%d insn=%d builtin name entry %d is NULL", f, i, ix);
                /* The NAME is decided first: a builtin this route does not
                   emit is refused as a builtin call, whatever its arity, so a
                   zero-argument one is not reported as if len had been called
                   wrongly (measured: with the arity test first, 'say dbg_ip()'
                   came back as illegal-operand). */
                if (strcmp(bn, "len") != 0 && strcmp(bn, "list") != 0) {
                    g_prescan_why = "builtin-call";
                    return bad("container=%d insn=%d builtin call '%s' -- this route emits len,"
                               " list and no other builtin; the registry, its capability flags and"
                               " the safe-mode rule live in the VM and the mods", f, i, bn);
                }
                int argc = ir_insn_r3(in);
                if (argc != 1)
                    return bad("container=%d insn=%d builtin '%s' is called with %d argument(s);"
                               " this route emits len and list, both of which take one",
                               f, i, bn, argc);
                if (asp < argc)
                    return bad("container=%d insn=%d builtin '%s' argument stack underflows:"
                               " asp=%d argc=%d", f, i, bn, asp, argc);
                asp -= argc;
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
            /* THE FUNCTION LITERAL.  Two operands and both are checked here
               because the emitter reads them: r2 is the FUNCTION INDEX the
               compiler wrote (emit(curBC, OP_MAKE_FUNC, out, fidx,
               fbc->capture_count), src/compiler/compiler.c:1303), and the
               emitter turns it into this route's module index through
               nodes[0].child_ids[fidx] -- the same main-relative slot OP_CALL_FUNC
               above reads from the same table, which is why the bound is
               root->func_count and not node->something.
               r3 is the CAPTURE COUNT and, since the capture-read grid, it is
               an ARGUMENT-STACK COUNT: the compiler pushes one OP_PUSH_REG per
               capture immediately before this instruction
               (src/compiler/compiler.c:1297-1303), and the emitter reads those
               slots back as mvargs[asp-ncap..asp-1] exactly the way OP_NEW_ARRAY
               reads its elements.  So it is consumed here, bounded against the
               stack rather than dropped -- a count the emitted C could not index
               is a REFUSAL, not an unbounded read.  The old blanket refusal
               for any nonzero count is gone for the same reason the environment
               exists: mv_fun_cap builds it and the container's hidden self
               parameter reads it.
               One thing is still checked and deliberately NOT here: whether a
               container reads a slot INSIDE the environment it was created with.
               That is func_cap's job (it is a cross-container fact, not a per
               instruction one), and a slot outside the environment is the
               interpreter's own nil, so the emitting arm answers it instead of
               refusing it. */
            case OP_MAKE_FUNC: {
                int fidx = in->a;
                int ncap = ir_insn_r3(in);
                if (fidx < 0 || fidx >= root->func_count)
                    return bad("container=%d insn=%d function index %d out of range (count=%d)",
                               f, i, fidx, root->func_count);
                if (ncap < 0 || ncap >= REG_LIMIT)
                    return bad("container=%d insn=%d function index %d capture count %d out of range",
                               f, i, fidx, ncap);
                if (asp < ncap)
                    return bad("container=%d insn=%d function index %d captures %d value(s)"
                               " but the argument stack holds %d: the compiler pushes one"
                               " register per capture before this instruction",
                               f, i, fidx, ncap, asp);
                asp -= ncap;
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
    /* Per-instruction catch label, or NULL when the program has no try pair.
       Built once per container by emit_container; emit_one reads it through
       emit_try_check.  See that function for what the text does with it. */
    const int      *try_catch_at;
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

/* THE CAPTURE TABLE: how many values a container was created with, and whether
   it reads any of them.  Same structure as the notes around it: WHY / WHAT IT
   ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.

   WHY IT EXISTS.  The environment is built by the CREATOR (OP_MAKE_FUNC, whose
   r3 is the count) and read by the CONTAINER (OP_LOAD_CAPTURE, whose r2 is a
   slot), so the two facts live in two different instruction streams and one of
   them has to be looked up ACROSS the module: a container's signature carries
   the hidden `self` parameter only when a creation site gives it an
   environment AND its body reads a slot inside that environment.  Without the
   lookup the route would have to put `self` on EVERY container -- which moves
   the generated text of every accepted program, the 86-case corpus included,
   for a parameter those programs never use -- or on none, leaving a captured
   read no answer.

   WHAT IT ADDS.  func_cap returns the capture count of the ONE
   OP_MAKE_FUNC that builds this container: -1 when no instruction in the
   module creates it (a plain `func` container, or a value only reachable
   through an opcode this route counts not-yet), and -2 when two creation
   sites disagree -- which the compiler cannot produce (one lambda literal has
   one capture count) and which is therefore a refusal at the emitting site
   rather than a guess here.  func_reads_capture is the predicate the
   signature follows: an environment exists (cap > 0) and the body reads a
   slot BELOW its size, so the emitted read can never index past the slots
   mv_fun_cap allocated.

   WHAT IT DOES NOT COVER.  A container created WITH captures whose body reads
   none keeps the plain signature: the environment is still built, because
   the creator cannot see the callee's body and dropping it would need the
   same scan answered in the opposite direction (one table, one direction,
   the cheap one).  A read whose slot is OUTSIDE the environment is not
   covered here either, and it is not a gap: the interpreter answers nil for
   it (src/vm/vm.c:4033-4039 bounds the slot against im_closure_env_size), so
   the emitting arm answers mv_nil() for exactly that shape and the two agree
   byte for byte.

   DELETE CONDITION (decidable).  When the module carries each container's
   capture count directly (a field beside `argc`, filled by the compiler that
   already knows it), these two become field reads and the scan goes away in
   the same commit; the test is mechanical: no OP_MAKE_FUNC walk in this file
   outside the emitter's own arm.

   MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY case
   mir_closure_capture_read: `make(10)` then f(5) prints 15 and `make(3)` then
   g(7) prints 10, which can only be right if the slot read came from the
   value's OWN environment; mir_closure_value_nocapture keeps cap == 0, so its
   signature and its generated text do not move. */
static int func_cap(const IrModule *m, const Node *nodes, int id) {
    if (!m || !nodes || !nodes[0].child_ids || id <= 0 || id >= m->nfuncs) return -1;
    int cap = -1;
    for (int f = 0; f < m->nfuncs; f++) {
        const IrFunc *fn = &m->funcs[f];
        for (int i = 0; i < fn->ninsns; i++) {
            const IrInsn *in = &fn->insns[i];
            if (in->op != OP_MAKE_FUNC) continue;
            int fidx = in->a;
            if (fidx < 0 || fidx >= nodes[0].nchild) continue;
            if (nodes[0].child_ids[fidx] != id) continue;
            int ncap = (int)ir_insn_r3(in);
            if (cap == -1) cap = ncap;
            else if (cap != ncap) return -2;
        }
    }
    return cap;
}

static int func_reads_capture(const IrModule *m, const Node *nodes, int id) {
    int cap = func_cap(m, nodes, id);
    if (cap <= 0) return 0;
    const IrFunc *fn = &m->funcs[id];
    for (int i = 0; i < fn->ninsns; i++) {
        const IrInsn *in = &fn->insns[i];
        if (in->op != OP_LOAD_CAPTURE) continue;
        int32_t slot = ir_insn_r2(in);
        if (slot >= 0 && slot < cap) return 1;
    }
    return 0;
}

/* THE RUN-TIME CHECK, AND WHY IT IS A CHECK RATHER THAN AN UNWIND.
 *
 * A throw does not leave the throwing instruction by itself: mv_throw records
 * the value and sets mv_throwing, and the instruction is FOLLOWED by this text,
 * which is what actually moves control.  Two shapes, chosen statically:
 *
 *   - inside a try region: `goto L<catch_off>` -- the catch is a block start in
 *     this container (src/compiler/mir.c:82-88 marks the three try offsets as
 *     block starts, so the label exists; the emitter also marks catch_off in
 *     need[] so the pair cannot depend on that).
 *     The flag is cleared here because it has been consumed: the interpreter's
 *     vm_throw pops the frame and leaves `t->ip` at the catch, and the handler
 *     runs with that slot already off the exception stack.
 *
 *   - outside every region and not in main: return to the caller with nil.  The
 *     thrown frame may belong to a CALLER (a callee with no try of its own
 *     throws into the first frame the thread has, src/vm/vm.c:2554-2560), so the
 *     return is not "swallow it" -- the flag stays set and the caller's own
 *     check jumps.  mv_try_mark is the depth the container entered with, and
 *     restoring it here is the interpreter's return rule read as a compile-time
 *     fact (src/vm/vm.c:4142 pops every frame with frame_count >= this frame's),
 *     spelled as a MIN so a frame mv_throw already popped is not resurrected.
 *
 * In main, outside a region, no text is emitted: mv_throw with no frame open
 * calls mv_die_uncaught, which is exactly the interpreter's uncaught path and
 * does not return. */
static void emit_try_check(Buf *b, const Ctx *c, int i) {
    const IrFunc *fn = &c->m->funcs[c->id];
    if (!c->try_catch_at || i < 0 || i >= fn->ninsns) return;
    int catch_off = c->try_catch_at[i];
    if (catch_off >= 0) {
        buf_fmt(b, " if (mv_throwing) { mv_throwing = 0; goto L%d; }", catch_off);
        return;
    }
    if (!c->nodes[c->id].is_main)
        buf_str(b, " if (mv_throwing) { if (mv_try_depth > mv_try_mark) mv_try_depth = mv_try_mark; return mv_nil(); }");
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
    case OP_ADD:
        /* THE ARITHMETIC STOPS, FUNNELLED -- only for a program that carries a
           try pair, because the stop itself lives in the shared preamble
           (nv_add's __builtin_add_overflow, nv_div's `y.i == 0`) and that
           preamble is byte-identical in both routes by construction (see
           classify()).  So the check cannot live THERE; it lives here, in front
           of the call, and only in text that a try program gets.  Every reader
           of the decision should keep the two halves in view: the helper's
           predicate is nv_add's predicate copied (not "any overflow is an
           exception": a float add never overflows, and nv_div answers inf for
           a float zero divisor rather than stopping), and the message is the
           one the preamble would have printed -- the same string the catch
           variable then holds.  Measured by the three criteria named in
           mir_try_catches_an_arm_stop's note; the cases they replace are the
           ones the interpreter answers `type_mismatch`, `division_by_zero` and
           `numeric_overflow` CATCHABLY (`.scratch/probe3` e1/div1/ovf1). */
        if (g_try_used) {
            buf_fmt(b, "    v[%d] = mv_try_add(v[%d], v[%d]);", r1, r2, r3);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_add(v[%d], v[%d]);", r1, r2, r3); return 0;
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
    case OP_SUB:
        if (g_try_used) {
            buf_fmt(b, "    v[%d] = mv_try_sub(v[%d], v[%d]);", r1, r2, r3);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_sub(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_MUL:
        if (g_try_used) {
            buf_fmt(b, "    v[%d] = mv_try_mul(v[%d], v[%d]);", r1, r2, r3);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_mul(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_DIV:
        if (g_try_used) {
            buf_fmt(b, "    v[%d] = mv_try_div(v[%d], v[%d]);", r1, r2, r3);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_div(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_MOD:
        if (g_try_used) {
            buf_fmt(b, "    v[%d] = mv_try_mod(v[%d], v[%d]);", r1, r2, r3);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_mod(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_EQ:           buf_fmt(b, "    v[%d] = mv_eq(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_NEQ:          buf_fmt(b, "    v[%d] = mv_ne(v[%d], v[%d]);", r1, r2, r3); return 0;
    /* THE MEMBERSHIP ARM.  One call to mv_in, which carries the interpreter's
       own classification (b is an ARRAY -> subset-or-member by val_eq; b is
       anything else -> false, src/vm/vm.c:2381-2420).  The operand order is
       op_regs' note above, not a guess: r2 is the element, r3 the container.
       The reasoning, the arms this route cannot be asked for, and the delete
       condition are in mv_in's note in kMirValBlock. */
    case OP_IN:           buf_fmt(b, "    v[%d] = mv_in(v[%d], v[%d]);", r1, r2, r3); return 0;
    /* THE INTERVAL LITERAL.  Two bounds off the argument stack (lo was pushed
       first, so it is at asp-2), the domain name and the two inclusivity bits
       out of r2, and the answer is one pool region -- see mv_set_interval's note
       for the layout and mv_interval_has's for the predicate.  A non-number bound
       is NOT a refusal: the interpreter answers NIL for the whole value
       (src/vm/vm.c, L_SET_INTERVAL's `!val_is_num` arm), so this arm answers
       mv_nil() and the program keeps going -- mir_interval_a_non_number_bound_makes_nil
       is the criterion, and its second line (`1 in p`) then reads false because
       membership of nil is false.
       The domain is always R here: prescan refused anything else by name. */
    case OP_RECV:
        /* THE MESSAGE QUEUE THIS ROUTE CANNOT FILL.  The interpreter's arm polls
           `t->msg_q` under `t->msg_lock` and waits for a message; both of those
           objects are created ONLY in the thread-spawn path (src/vm/vm.c:4532:
           `ImMutex *ml = im_mutex_new(); nt->msg_lock = ml;`), and a container
           carrying a thread section is refused earlier (why=threads).  So on the
           single thread this route accepts, `t->msg_lock` is NULL, the whole
           poll is skipped, and the answer is `out` initialised to VAL_NIL
           (src/vm/vm.c, L_RECV's `Value out = { VAL_NIL, ... }`).
           That is what this arm writes: nil, and the timeout register is not
           even read -- the interpreter does not wait either when there is no
           queue to wait on (measured: `recv m, 0.5` returns nil immediately, it
           does not sleep for half a second).
           A message can only come from another thread, so this is not a
           simplification of the opcode; it is the only answer these programs can
           reach, and it agrees with the interpreter byte for byte. */
        buf_fmt(b, "    v[%d] = mv_nil();   /* OP_RECV: no thread can fill this queue"
                   " (see IR-MIR-RECV's criteria) */", r1);
        return 0;
    case OP_SET_ADD: {
        /* One call, for side effects only: mv_set_add takes the set by VALUE and
           grows the region (in place at the frontier, or by moving it and
           leaving a forwarding head -- see its note).  No assignment to r1: the
           register keeps the handle it had, which is what makes an alias that
           was taken before the add still resolve. */
        buf_fmt(b, "    mv_set_add(v[%d], v[%d]);", r1, r2);
        emit_try_check(b, c, i);
        return 0;
    }
    case OP_SET_INTERVAL: {
        int32_t packed = ir_insn_r2(in);
        int flags = (int)((packed >> 24) & 0xFF);
        buf_fmt(b, "    if (!((mvargs[asp-2].type == MV_INT || mvargs[asp-2].type == MV_FLT)"
                   " && (mvargs[asp-1].type == MV_INT || mvargs[asp-1].type == MV_FLT))) {"
                   " v[%d] = mv_nil(); }" NL, r1);
        buf_fmt(b, "    else { double lo_ = (mvargs[asp-2].type == MV_INT) ? (double)mvargs[asp-2].ival : mvargs[asp-2].fval;"
                   " double hi_ = (mvargs[asp-1].type == MV_INT) ? (double)mvargs[asp-1].ival : mvargs[asp-1].fval;"
                   " v[%d] = mv_set_interval(lo_, hi_, %d); } asp -= 2;" NL, r1, flags);
        return 0;
    }
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
     * WHAT IT DOES NOT COVER.  OP_INDEX_SET on a DICT (the array write is
     * lowered; the dict write has no runtime -- see mv_dic_new's note) and
     * OP_NEW_SET stay un-emitted and are named in docs/STATUS.md's boundary
     * section for this step; the container block above says what the array runtime does not
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
    /* THE DICT CONSTRUCTOR -- OP_NEW_DICT, the same argument-stack mouth as
       OP_NEW_ARRAY with 2*n slots instead of n.  WHY / WHAT IT ADDS / WHAT IT
       DOES NOT COVER / DELETE CONDITION / MEASURED BY all live in
       kMirValBlock's constructor note above mv_dic_new; what belongs HERE is
       the operand reading.  r3 is the PAIR count (op_regs' note), so the slots
       this instruction consumes are 2*r3, and the first push is items[0]:
       `mv_dic_new(n, mvargs + asp - 2*n)` hands key0,value0,key1,value1 in the
       compiler's push order (src/compiler/compiler.c:1054-1056), which is the
       items[] order L_NEW_DICT writes (src/vm/vm.c:3663-3666).  The empty
       literal consumes nothing and must not read mvargs at 0 (the same
       spelling OP_NEW_ARRAY's arm uses for the same reason). */
    case OP_NEW_DICT: {
        int n = w3;                    /* the PAIR count: see op_regs' note */
        if (n == 0) { buf_fmt(b, "    v[%d] = mv_dic_new(0, 0);", r1); return 0; }
        buf_fmt(b, "    v[%d] = mv_dic_new(%d, mvargs + asp - %d); asp -= %d;",
                r1, n, 2 * n, 2 * n);
        return 0;
    }
    /* THE SET CONSTRUCTOR -- OP_NEW_SET, the third argument-stack mouth and the
       only one that DEDUPS what it pops.  Same shape as OP_NEW_ARRAY: r3 is the
       element COUNT (op_regs' note; the operand reading is the same one), so
       the mouth is n slots of mvargs in push order -- `mv_set_new(n, mvargs +
       asp - n)` -- and the value keeps only the distinct elements, which is the
       merge rule in kMirValBlock's mv_set_new note.  WHAT BELONGS HERE is that
       the consumption is n even when the value is shorter: the slots are the
       compiler's pushes (src/compiler/compiler.c:562-565), not the set's
       contents, so `s = 1, 1` pops two and stores one.  The empty literal
       consumes nothing and must not read mvargs at 0 (the same spelling the two
       arms above use for the same reason); it is not reachable from source
       today -- the only OP_NEW_SET with count 0 is a comprehension's
       (src/compiler/compiler.c:582), and that program is still refused, one
       opcode later: it now stops at OP_SET_ADD, the arm below, rather than at
       the OP_CALL_BUILTIN `list` that used to precede it -- but r3 == 0 is a
       legal instruction and this arm answers it instead of reading a null. */
    case OP_NEW_SET: {
        int n = w3;                     /* the element COUNT: see op_regs' note */
        if (n == 0) { buf_fmt(b, "    v[%d] = mv_set_new(0, 0);", r1); return 0; }
        buf_fmt(b, "    v[%d] = mv_set_new(%d, mvargs + asp - %d); asp -= %d;",
                r1, n, n, n);
        return 0;
    }
    case OP_INDEX_GET:
        buf_fmt(b, "    v[%d] = mv_arr_get(v[%d], v[%d]);", r1, r2, r3);
        /* mv_arr_get answers a non-numeric SUBSCRIPT through mv_index_i32, whose
           default arm is mv_throw ("type_mismatch"); the interpreter answers the
           same case the same way and CATCHES it (`.scratch/probe3/idx1`), so
           this site carries the check. */
        emit_try_check(b, c, i);
        return 0;
    /* OP_INDEX_SET is a CALL WITH NO ASSIGNMENT: the handler reads r1/r2/r3 and
       writes into the container, so unlike every arm above this one leaves the
       destination register alone (v[r1] already holds the container and must
       still hold it afterwards -- an assignment here would overwrite the handle
       with a void).  The order handed to mv_arr_set is the handler's, not
       OP_INDEX_GET's: container, subscript, value. */
    case OP_INDEX_SET:
        buf_fmt(b, "    mv_arr_set(v[%d], v[%d], v[%d]);", r1, r2, r3);
        emit_try_check(b, c, i);
        return 0;
    /* THE ORDERING PAIR, and the ONE THING they must do that the other arms do
       not: carry the check.  Since this cell an unorderable pair is a THROW
       (mv_orderable's note), and mv_throw only SETS THE FLAG -- the jump to the
       catch happens in the check emitted after the instruction.  Measured
       without it: `try { say ("hi" < c) }` printed `false` and carried on,
       because the throw had been recorded and nothing acted on the record; the
       interpreter prints `caught`.  Found by the fuzzer, on the same day the
       guard was added. */
    case OP_LT:           buf_fmt(b, "    v[%d] = mv_lt(v[%d], v[%d]);", r1, r2, r3); emit_try_check(b, c, i); return 0;
    case OP_GT:           buf_fmt(b, "    v[%d] = mv_gt(v[%d], v[%d]);", r1, r2, r3); emit_try_check(b, c, i); return 0;
    case OP_LE:           buf_fmt(b, "    v[%d] = mv_le(v[%d], v[%d]);", r1, r2, r3); emit_try_check(b, c, i); return 0;
    case OP_GE:           buf_fmt(b, "    v[%d] = mv_ge(v[%d], v[%d]);", r1, r2, r3); emit_try_check(b, c, i); return 0;
    case OP_AND:          buf_fmt(b, "    v[%d] = mv_boo(mv_tru(v[%d]) && mv_tru(v[%d]));", r1, r2, r3); return 0;
    case OP_OR:           buf_fmt(b, "    v[%d] = mv_boo(mv_tru(v[%d]) || mv_tru(v[%d]));", r1, r2, r3); return 0;
    case OP_NEG:
        if (g_try_used) {
            buf_fmt(b, "    v[%d] = mv_try_neg(v[%d]);", r1, r2);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_neg(v[%d]);", r1, r2); return 0;
    case OP_NOT:          buf_fmt(b, "    v[%d] = mv_not(v[%d]);", r1, r2); return 0;
    case OP_IS_NIL:       buf_fmt(b, "    v[%d] = mv_is_nil(v[%d]);", r1, r2); return 0;
    /* The set reductions, lowered as what they answer for a non-set operand --
       nil, in ZERO allocations.  `isMax` is not a direction the fold can take
       here: there is no set to fold.  The reasoning, the measured refusals
       that make the operand provably a non-set, and the delete condition are
       in mv_minmax's note in kMirValBlock; the op_regs entry above says why r2
       is the only operand. */
    case OP_MIN:          buf_fmt(b, "    v[%d] = mv_minmax(v[%d], 0);", r1, r2); return 0;
    case OP_MAX:          buf_fmt(b, "    v[%d] = mv_minmax(v[%d], 1);", r1, r2); return 0;
    case OP_LOAD_GLOBAL:
        /* Index -1 has no name to look up: gnames[] is built from the globals
           prescan saw REFERENCED, and an unresolved global is referenced by
           nobody.  Indexing it would be the emitter guessing, so the value is
           spelled out instead -- nil, which is what the interpreter answers. */
        if (w2 == -1) { buf_fmt(b, "    v[%d] = mv_nil();", r1); return 0; }
        buf_fmt(b, "    v[%d] = %s;", r1, c->gnames[w2]); return 0;
    case OP_STORE_GLOBAL: {
        /* THE CHECKED STORE, and the half of OP_BIND that is not a declaration.
           `vm->global_bound[g] > 0` is checked on EVERY store to that global
           (src/vm/vm.c, L_STORE_GLOBAL: it value_copy's the source, and when
           the bound is set and `set_contains` says no it throws
           `type_mismatch`), so an emitter that records the declaration and
           then stores unchecked answers `x = 5` with 5 while the interpreter
           prints nothing, exits 1 and says type_mismatch -- the disagreement
           mir_bind_out_of_range_store exists to catch.
           The guard is emitted only when the program declares a constraint
           (g_bound_used, see its note): a program with no declaration has an
           all-zero table, and its store keeps the exact bytes it had. */
        if (g_bound_used)
            buf_fmt(b, "    if (mv_bound[%d] && !mv_set_has(mv_bound[%d] - 1, v[%d]))\n"
                       "        mv_throw(mv_str(\"type_mismatch\"));" NL,
                    r1, r1, r2);
        buf_fmt(b, "    %s = v[%d];", c->gnames[r1], r2);
        /* The store is the OTHER half of the bound rule and its stop is the same
           exception the interpreter raises (src/vm/vm.c, L_STORE_GLOBAL's
           vm_throw_kind), so a `try { x = 5 } catch (e)` catches it here too --
           `.scratch/probe3/e1` is the measured receipt, and
           mir_try_catches_an_arm_stop is the criterion.  Note the ORDER the text
           keeps: the throw happens BEFORE the store writes, exactly as
           value_assign is only reached after the membership test passed. */
        emit_try_check(b, c, i);
        return 0;
    }
    case OP_BIND: {
        /* THE DECLARATION.  L_BIND does two things and this arm does both, in
           the interpreter's order: record the bound, then check the
           initialiser -- with the SAME membership test a later store uses,
           and with a DIFFERENT message (`initial value out of range`, not
           type_mismatch), which is why the two cases come in pairs.
           The table stores what the interpreter stores: the set's region
           index plus one, zero meaning "no bound" (vm->global_bound[g] in
           src/vm/vm.c).  A constraint that is not a set records no bound
           (L_BIND's `sidx = (R[setReg].type == VAL_SET) ? ... : -1`), so
           `x: 5` leaves every store unchecked -- mir_bind_plain_constraint.
           w3 is -1 for a declaration without an initial value, and that is a
           compile-time operand, so the branch is C text chosen here rather
           than a run-time test: L_BIND's no-init path leaves the global nil. */
        int gid = r1, sreg = r2, init = w3;
        buf_fmt(b, "    mv_bound[%d] = (v[%d].type == MV_SET) ? (int)v[%d].ival + 1 : 0;" NL,
                gid, sreg, sreg);
        if (init >= 0) {
            buf_fmt(b, "    if (mv_bound[%d] && !mv_set_has(mv_bound[%d] - 1, v[%d]))\n"
                       "        mv_throw(mv_str(\"initial value out of range\"));" NL,
                    gid, gid, init);
            buf_fmt(b, "    %s = v[%d];", c->gnames[gid], init);
            emit_try_check(b, c, i);
        } else {
            buf_fmt(b, "    %s = mv_nil();", c->gnames[gid]);
        }
        return 0;
    }
    case OP_RECORD: {
        /* THE METADATA STATEMENT.  What the interpreter does here is write a
           record table entry (name, store, scope, merge) and then, ONLY if a
           save file was loaded at startup, REPLACE this register with the value
           stored under that name (src/vm/vm.c, L_RECORD's
           `vm->record_loaded_dict > 0` branch).
           This route keeps no record store: it loads no save file, writes no
           metadata and creates none.  So the register keeps what the compiler's
           preceding MOV put in it, and the STORE_GLOBAL that follows writes the
           program's own value -- which is what the interpreter prints in a first
           run in a fresh directory.
           The divergence is real and is recorded rather than papered over: the
           interpreter WRITES save.dat at exit, and a later program in that same
           directory reads the stored value instead of its own operand (measured:
           one run stores 5, then `record a = 9 / say a` in that directory prints
           5 where this route prints 9).  Record is the first statement whose
           interpreter output is a function of the host's disk rather than of the
           program; the criteria stay in the only deterministic region and
           STATUS.md carries the transcript.
           The operand is NAMED in the emitted text on purpose: the register is
           the one the store path would have written, and a reader of the C
           should be able to see that this route deliberately left it alone
           rather than that it forgot. */
        buf_fmt(b, "    /* OP_RECORD: no record store on this route -- v[%d] keeps"
                   " what the preceding MOV put there */" NL, r3);
        (void)r1;
        return 0;
    }
    case OP_JUMP:         buf_fmt(b, "    goto L%d;", w2); return 0;
    case OP_JUMP_IF_FALSE: buf_fmt(b, "    if (!mv_tru(v[%d])) goto L%d;", r1, w2); return 0;
    case OP_JUMP_IF_TRUE:  buf_fmt(b, "    if (mv_tru(v[%d])) goto L%d;", r1, w2); return 0;
    case OP_PUSH_REG:     buf_fmt(b, "    mvargs[asp++] = v[%d];", r1); return 0;
    case OP_CALL_BUILTIN: {
        /* The name is resolved HERE, off the container's string pool, so the
           emitter's index read and prescan's bound are the same claim (the
           operands instrument compares the two).  Anything but `len` and `list`
           returns -1: prescan refuses those by name, and this is the second
           lock.  The ARITY is one for both, so the pop below is shared and only
           the call differs -- and the piece of knowledge worth stating twice is
           which RUNTIME function the name maps to, because `list` answers a new
           container where `len` answers a number. */
        const Bytecode *bc = c->node;
        int32_t ix = r2;
        const char *bn = (ix >= 0 && ix < bc->string_count && bc->string_pool)
                         ? bc->string_pool[ix] : NULL;
        if (!bn || (strcmp(bn, "len") != 0 && strcmp(bn, "list") != 0)) return -1;
        if (strcmp(bn, "list") == 0)
            buf_fmt(b, "    v[%d] = mv_list(mvargs[asp-1]);", r1);
        else
            buf_fmt(b, "    v[%d] = mv_len(mvargs[asp-1]);", r1);
        buf_str(b, " asp -= 1;");
        /* len answers nil/garbage for a non-container and never throws, so this
           check exists for the CALLER's flag rather than len's own: a callee
           reached through a builtin cannot set it, but keeping every call site
           identical is cheaper than arguing which ones can.  `list` allocates
           for the set arm, and that allocation is mv_alloc's, which does not
           throw either. */
        emit_try_check(b, c, i);
        return 0;
    }
    case OP_CALL_FUNC: {
        int fidx = r1, res = r2, argc = w3;
        int callee = c->nodes[0].child_ids ? c->nodes[0].child_ids[fidx] : -1;
        /* A callee that reads a capture slot has a hidden leading `self`
           parameter (see func_cap's note).  Called BY NAME there is no function
           VALUE to take the environment from, and the interpreter's answer for
           that frame is a NULL environment -- every slot then reads nil
           (src/vm/vm.c:4033-4039).  mv_nil() IS that environment: its ptr is
           NULL, so no slot is ever read. */
        int self = (callee > 0 && callee < c->m->nfuncs)
                   ? func_reads_capture(c->m, c->nodes, callee) : 0;
        buf_fmt(b, "    v[%d] = %s(", res, callee >= 0 ? c->fnnames[callee] : "mv_missing_callee");
        if (self) buf_str(b, "mv_nil()");
        for (int k = 0; k < argc; k++)
            buf_fmt(b, "%smvargs[asp-%d]", (k || self) ? ", " : "", argc - k);
        buf_str(b, ");");
        if (argc > 0) buf_fmt(b, " asp -= %d;", argc);
        /* THE CALLEE'S THROW LANDS HERE.  A callee with no try of its own throws
           into the frame this call site owns, and returns nil with the flag
           still set; this check is what turns that into the catch (inside a
           region) or into this container's own return (outside one). */
        emit_try_check(b, c, i);
        return 0;
    }
    /* THE FUNCTION VALUE -- the arms that move a function as a VALUE rather
       than naming a callee here, and the capture read that makes such a value
       more than an index.  Same structure as the notes around them: WHY /
       WHAT IT ADDS / WHAT IT DOES NOT COVER / DELETE CONDITION / MEASURED BY.
       *
       * WHY.  Both function-value opcodes were unknown to this route at first:
       * `f = () -> 42` came back unsupported-opcode with
       * OP_MAKE_FUNC:1,OP_CALL_VALUE:1, so a function could not even be PRINTED,
       * let alone called through a variable.  The next grid then hit the wall
       * this cell crosses: a lambda whose body READS its capture was refused by
       * name, because a function value with a ptr field but no environment behind
       * it cannot answer `f(5)` -- and answering it with a made-up value is the
       * class of error the route refuses to print.
       *
       * WHAT IT ADDS.  Three arms and one dispatch table in aot_mir_translate:
       *   - OP_MAKE_FUNC stores `<module index>` in r1.  The index is
       *     nodes[0].child_ids[fidx] -- the SAME table OP_CALL_FUNC's arm reads,
       *     and the same reason: every lambda is registered into mainBC
       *     (src/compiler/compiler.c:1246), so the bytecode's function index counts
       *     main's children while this route numbers containers depth-first.  A
       *     capture count above zero goes through mv_fun_cap, which COPIES the
       *     r3 slots that were pushed onto the argument stack into a fresh
       *     environment and puts it in ptr; the count then comes off the stack
       *     (`asp -= ncap`), exactly as OP_NEW_ARRAY's element count does.
       *   - OP_LOAD_CAPTURE reads slot r2 of the calling value's environment
       *     through the hidden `self` parameter.  r2 is NOT a register: it is
       *     an index into the environment the value was built with.
       *   - OP_CALL_VALUE reads the callee OUT OF A REGISTER and lets mv_callv
       *     pick the C function, the runtime mirror of the interpreter's
       *     `call_fidx_override = R[ins.r1].ival` (src/vm/vm.c:4056-4057).  argc ==
       *     0 passes the literal 0 because a container with no OP_PUSH_REG never
       *     declares mvargs -- the first criterion's shape.
       *   - each call site is matched to the signature by func_reads_capture:
       *     mv_callv passes the VALUE as `self` (it carries the environment),
       *     and a by-name call passes mv_nil() (no environment, so every read
       *     answers nil -- the interpreter's own answer for a NULL env).
       *
       * WHAT IT DOES NOT COVER -- each a named refusal upstream or a boundary
       * stated in the MV_FUN note, not a silent answer here:
       *   - a capture count the argument stack cannot supply, or one outside
       *     REG_LIMIT.  prescan refuses it; the emitter's own bounds check is
       *     the second lock.
       *   - a callee whose function index does not resolve in child_ids.  bad(), as
       *     OP_CONCAT's arm does, rather than the `mv_missing_callee` identifier
       *     OP_CALL_FUNC falls back to -- a function VALUE that cannot name itself
       *     is a translation defect, not a run-time answer.
       *   - two creation sites that disagree about a container's capture count.
       *     The compiler cannot produce it (one literal, one count); func_cap
       *     answers -2 for it and OP_LOAD_CAPTURE's arm stops the translation
       *     rather than reading a slot of an environment whose size is a guess.
       *   - an arity mismatch.  mv_callv's generated switch checks it and stops
       *     the program (see that emitter); the interpreter's own tolerance for
       *     filling missing arguments with nil is not reproduced here, and no
       *     accepted stream reaches it.
       *
       * DELETE CONDITION (decidable).  The nocapture-only restriction this
       * block used to carry is GONE, and its own test was mechanical: the
       * `ncap != 0` refusal is no longer in prescan's arm, and
       * mir_closure_capture_read went from mir=refused to accepted/same.  What
       * would delete the REST is a per-container capture count in the module
       * (then func_cap's scan goes, see its note) and a value-domain environment
       * the shared NV can carry (then mv_fun_cap folds into mv_from_nv).
       *
       * MEASURED BY.  tools/aot_mir.test.py's MIR_ONLY cases:
       * mir_closure_value_nocapture (refused before these arms;
       * unsupported-opcode) and mir_closure_capture_read (`make(10)`, `f(5)`
       * printing 15, `make(3)`, `g(7)` printing 10 -- the two environments are
       * 2 allocs/64 bytes and the answers differ, which is the whole point). */
    case OP_MAKE_FUNC: {
        int fidx = r2;                       /* r2 = FUNCTION INDEX, not a register */
        int ncap = w3;                       /* r3 = CAPTURE COUNT */
        int callee = c->nodes[0].child_ids ? c->nodes[0].child_ids[fidx] : -1;
        if (ncap < 0 || ncap >= REG_LIMIT)
            return bad("container=%d insn=%d function index %d capture count %d out of range",
                       c->id, i, fidx, ncap);
        if (callee < 0 || callee >= c->m->nfuncs)
            return bad("container=%d insn=%d function index %d does not resolve to a"
                       " container", c->id, i, fidx);
        if (ncap == 0) {
            buf_fmt(b, "    v[%d] = mv_fun(%d);", r1, callee);
            return 0;
        }
        /* The environment is built from the r3 slots the compiler pushed
           immediately before this instruction; the first push is slot 0
           (src/vm/vm.c:4040-4054 pops them in order). */
        buf_fmt(b, "    v[%d] = mv_fun_cap(%d, mvargs + asp - %d, %d); asp -= %d;",
                r1, callee, ncap, ncap, ncap);
        return 0;
    }
    case OP_LOAD_CAPTURE: {
        int slot = w2;                       /* r2 = ENVIRONMENT SLOT, not a register */
        int cap = func_cap(c->m, c->nodes, c->id);
        if (cap == -2)
            return bad("container=%d insn=%d reads capture slot %d but two creation sites"
                       " give this container different capture counts", c->id, i, slot);
        /* The interpreter's own three-way answer (src/vm/vm.c:4033-4039):
           no environment, or a slot outside it, is nil; inside it is the
           captured value.  The emitting side picks the arm it can prove. */
        if (cap > 0 && slot >= 0 && slot < cap)
            buf_fmt(b, "    v[%d] = mv_capture(self, %d);", r1, slot);
        else
            buf_fmt(b, "    v[%d] = mv_nil();", r1);
        return 0;
    }
    case OP_CALL_VALUE: {
        int callee_reg = r1, res = r2, argc = w3;
        if (argc < 0 || argc >= REG_LIMIT)
            return bad("container=%d insn=%d OP_CALL_VALUE argument count %d out of range",
                       c->id, i, argc);
        if (argc == 0) {
            buf_fmt(b, "    v[%d] = mv_callv(v[%d], 0, 0);", res, callee_reg);
            emit_try_check(b, c, i);
            return 0;
        }
        buf_fmt(b, "    v[%d] = mv_callv(v[%d], mvargs + asp - %d, %d); asp -= %d;",
                res, callee_reg, argc, argc, argc);
        emit_try_check(b, c, i);
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
        /* A RETURN OUT OF A try POPS THAT FUNCTION'S FRAMES, and the interpreter
           spells the rule as `while (exc_depth > 0 && top.frame_count >=
           t->frame_count) exc_depth--` (src/vm/vm.c:4142).  Statically the
           container knows only the depth it ENTERED with, so the reset is a MIN
           against mv_try_mark: a frame opened by this function goes away, and a
           frame mv_throw already popped (the caller's) is not resurrected.
           mir_try_finally_keeps_return and mir_try_return_from_inside are the two
           criteria; the second one only passes if the frames of the returned-out
           try are gone, because it throws AFTER the return. */
        if (g_try_used) buf_str(b, "    if (mv_try_depth > mv_try_mark) mv_try_depth = mv_try_mark;" NL);
        if (r1 > 0) buf_fmt(b, "    return v[%d];", r1);
        else buf_str(b, "    return mv_nil();");
        return 0;
    case OP_SAY:          buf_fmt(b, "    mv_say(v[%d]);", r1); return 0;
    /* THE FIRST EXCEPTION OPCODE this route answers, and the answer stops at
       the UNCAUGHT half: no handler is open, so the value goes to stderr and
       the process exits 1 (mv_die_uncaught, in kMirValBlock).
       THAT WAS THE WHOLE ARM, and the paragraph that used to stand here said
       the handler half was refused whole: `OP_TRY_START`/`OP_TRY_END` had no
       case anywhere, so a `try` program never reached this one, and the
       MIR_ONLY_REFUSE entry mir_try_no_throw pinned the refusal by demanding
       why=unsupported-opcode with the pair on the not-yet line and OP_THROW
       absent from it.  That prediction was honest about the direction and is
       now ANSWERED rather than quietly dropped: the pair has masks and arms
       (op_regs and this switch), the refusal entry retired into MIR_ONLY as
       an EXECUTING case (mir_try_body_finishes -- running the same program
       and comparing bytes is the stronger assertion), and this arm calls
       mv_throw, which is mv_die_uncaught when no frame is open and the seam
       when one is.  The uncaught path is therefore unchanged byte for byte
       (the same message, the same exit 1, the same atexit line).
       What did NOT change: an uncaught throw still does not look like a
       handler.  Ir-MIR-ALLOCS on stderr is atexit(mv_free_all)'s and runs for
       both paths. */
    case OP_TRY_START: {
        /* OPENS THE FRAME THE CATCH BELONGS TO.  The entry is matched exactly as
           the interpreter matches it (`start_off == this instruction`), and when
           none matches, both routes do nothing: the arm still writes text, because
           emit_container refuses an instruction whose body did not grow ("emitted
           nothing, so the conservation equation could not see it") -- the same
           reason OP_YIELD's arm is a comment (10.136).  That is a fact about this
           emitter, not about try, and it is why a no-op here cannot be silence.
           The frame records the ADDRESS of the catch variable, which is a global
           (the compiler emits `emit(..., OP_TRY_START, 0, varIdx, 0)` with a
           global index, src/compiler/compiler.c, STMT_TRY), so the address
           outlives this frame -- the closure programs of 10.130/10.131 taught the
           same lesson about what kind of value may cross a frame boundary.
           r2 == -1 is `catch { }` with no variable: mv_try_new(NULL) and the
           thrown value is dropped, which is the interpreter's `f->var_idx >= 0`
           test answered by absence. */
        const Bytecode *node = c->node;
        const TryEntry *te = NULL;
        for (int k = node->try_count - 1; k >= 0; k--)
            if (node->try_entries[k].start_off == i) { te = &node->try_entries[k]; break; }
        if (!te || !g_try_used) {
            buf_str(b, "    /* OP_TRY_START: no try entry starts here, so both routes do nothing"
                       " (src/vm/vm.c:4426-4433); the comment is text IR-MIR-CONSERVE can see */");
            return 0;
        }
        if (r2 >= 0 && r2 < c->root->global_name_count && c->gnames[r2])
            buf_fmt(b, "    mv_try_new(&%s);", c->gnames[r2]);
        else
            buf_str(b, "    mv_try_new((MirVal *)0);");
        return 0;
    }
    case OP_TRY_END:
        /* One frame, one pop -- `if (t->exc_depth > 0) t->exc_depth--;` in
           src/vm/vm.c, including the guard: a program whose TRY_END has no
           matching TRY_START must not drive the depth below zero, because
           negative depth would make the next mv_throw call mv_try_var[-1]. */
        if (g_try_used) buf_str(b, "    if (mv_try_depth > 0) mv_try_depth--;");
        else buf_str(b, "    /* OP_TRY_END with no try pair in this program: the arm is a no-op */");
        return 0;
    /* OP_WAIT -- the second opcode here whose answer is not a value (YIELD was
       the first).  The interpreter's arm turns the operand into milliseconds
       with its own ladder (INT through ival, FLOAT through fval, anything else
       as 0.0) and, in the CLI shape this route accepts, hands it to a blocking
       sleep; the value it leaves behind is nothing, so the emitted C REPRODUCES
       the delay instead of pretending there is none.  What it must not do is
       what the criteria say: change the output, drop a statement, or allocate.
       A negative delay is the one shape this arm deliberately does not copy:
       the interpreter converts it to an unsigned sleep and blocks for days
       (measured, section 10.138), and reproducing a hang is not agreement. */
    case OP_WAIT:
        if (g_wait_used) buf_fmt(b, "    mv_wait(v[%d]);", r1);
        else buf_str(b, "    /* OP_WAIT with no wait in this program: unreachable, kept for conservation */");
        return 0;
    /* OP_LOCK.  `lock m { ... }` is acquire, body, release -- the same opcode
       twice, distinguished by r2 (0 = acquire, 1 = release; src/compiler/
       compiler.c, STMT_LOCK).  The interpreter's arm looks the name up in
       vm->mutexes, creates an ImMutex on first use and calls im_mutex_lock or
       im_mutex_unlock.
       THIS ROUTE HAS ONE THREAD.  A container carrying a thread section is
       refused earlier (why=threads), so a mutex has nothing to contend with:
       both calls are unobservable, and the arm is a note -- the same honest
       no-op as OP_YIELD (10.136), and a note rather than silence because
       emit_container refuses an instruction whose body did not grow
       ("emitted nothing, so the conservation equation could not see it").
       The BARE form (`lock m` with no block, OP_LOCK with isBlock 0 and no
       matching release) is REFUSED by the pre-scan instead: it takes the mutex
       and never gives it back, and the interpreter measures as a HANG --
       measured, the block form printed `a\nin\nb` while the bare probe never
       returned and was killed after two minutes.  A hang is not an answer to
       copy. */
    case OP_LOCK:
        buf_fmt(b, "    /* OP_LOCK %s: one thread, so there is nothing to contend"
                   " (see IR-MIR-LOCK's criteria) */" NL,
                (r2 & 1) ? "release" : "acquire");
        (void)r1;
        return 0;
    case OP_THROW:
        buf_fmt(b, "    mv_throw(v[%d]);", r1);
        emit_try_check(b, c, i);
        return 0;
    /* OP_STOP shares OP_HALT's two exits, for a different reason.  The
       interpreter's arm is `t->running = false; continue;` (src/vm/vm.c, L_STOP)
       -- the RUNNING thread stops -- while HALT ends the program outright.
       This route emits one thread per program (a container carrying a thread
       section is refused earlier, why=threads), so "this thread stops" and "the
       program ends" are the same event and the same bytes: return from main, or
       exit(0) out of a container.  What they are NOT is a `break` or a flag:
       `stop all` reached from inside a while must leave the whole program
       (mir_stop_mid_loop), and a container's `stop all` must not let the call
       site continue with mv_nil() (mir_stop_inside_a_function).
       The exit is unconditional, so the atexit(mv_free_all) registration still
       runs and the allocation reading is unchanged -- mir_stop_after_allocating
       pins that with one block allocated before the stop.
       IR-MIR-CONSERVE counts this as emitted, which is why op_regs carries the
       case; the absence showed up as four refusals naming OP_STOP:1. */
    case OP_STOP: case OP_HALT:
        if (c->nodes[c->id].is_main) buf_str(b, "    return 0;");
        else buf_str(b, "    fflush(stdout); exit(0);");
        return 0;
    case OP_YIELD:
        /* THE EMPTY ARM, ON PURPOSE.  The interpreter's arm is one guarded call
           -- `if (t->is_task && t->fiber_sched) SwitchToFiber(t->fiber_sched)`
           -- a cooperative hand-off to a fiber scheduler.  This route refuses a
           container with a thread section by name (why=threads), so every
           program it accepts runs on one plain thread with no scheduler: both
           conditions are false, and the opcode does nothing a program can see.
           There is no statement to emit, and the note is not decoration: an arm
           that emitted NOTHING would trip the conservation check ("emitted
           nothing, so the conservation equation could not see it") and turn a
           legitimate no-op into a refusal, so the note is what makes the
           instruction visible to IR-MIR-CONSERVE while writing no code.
           What the arm must not be is the whole content of the four criteria:
           not a stop (mir_yield_at_toplevel), not a loop restart
           (mir_yield_mid_loop), not a frame change
           (mir_yield_inside_a_function), and not an allocation
           (mir_yield_around_allocating). */
        buf_str(b, "    /* OP_YIELD: this thread is not a task, so the arm is the no-op "
                   "the interpreter's guard already is (see IR-MIR-OP-YIELD's criteria) */");
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
       array literal's elements ARE argument-stack entries.  OP_MAKE_FUNC is
       the fourth consumer for the same shape: its r3 is the CAPTURE COUNT and
       the compiler pushed one register per capture just before it, so the
       deepest point of a container that only builds a closure is above zero
       even though nothing in it looks like a call. */
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
        if (op == OP_CALL_FUNC || op == OP_NEW_ARRAY || op == OP_NEW_DICT || op == OP_NEW_SET
            || op == OP_MAKE_FUNC) {
            int n = ir_insn_r3(in);
            int slots = (op == OP_NEW_DICT) ? 2 * n : n;
            if (slots > 0) {
                if (op == OP_NEW_ARRAY || op == OP_NEW_DICT || op == OP_NEW_SET) has_args = 1;
                asp_now -= slots;
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
    /* THE STATIC HALF OF try/catch.  For every instruction, the catch label of
       the INNERMOST region enclosing it (-1 outside every region; regions here
       never cross containers, because bytecode try entries are per-container).
       The innermost one is the entry with the LARGEST start_off, and the
       entries are written in ascending start order by the compiler, so
       overwriting in that order lands on the innermost.  The catch labels are
       ALSO marked in need[] even though src/compiler/mir.c:82-88 already turns
       every try offset into a block start: the emitter's jump must not depend
       on a property of another pass to find its label. */
    int *try_catch_at = NULL;
    if (g_try_used) {
        size_t n = (size_t)(fn->ninsns > 0 ? fn->ninsns : 1);
        try_catch_at = malloc(sizeof(int) * n);
        if (!try_catch_at) { free(need); return bad("container=%d: out of memory", id); }
        for (int k = 0; k < node->try_count; k++) {
            const TryEntry *te = &node->try_entries[k];
            if (te->catch_off >= 0 && te->catch_off <= fn->ninsns) need[te->catch_off] = 1;
        }
        /* INNERMOST, and NOT "last one written": the compiler's entry order is
           not a nesting order (measured: with the loop that let a later entry
           overwrite an earlier one, `try { try { throw } catch (e) { ... } }`
           emitted its inner throw as a jump to the OUTER catch, and
           mir_try_nested printed `inner\nafter` where the interpreter prints
           `inner\nmid\nafter`).  The region with the LARGEST start_off that
           encloses the instruction is the innermost, which is a property of the
           numbers and not of the array's order. */
        for (int j = 0; j < fn->ninsns; j++) {
            int best = -1, best_start = -1;
            for (int k = 0; k < node->try_count; k++) {
                const TryEntry *te = &node->try_entries[k];
                if (j >= te->start_off && j <= te->end_off && te->start_off > best_start) {
                    best_start = te->start_off;
                    best = te->catch_off;
                }
            }
            try_catch_at[j] = best;
        }
    }

    Ctx c;
    c.m = m; c.root = m->bc; c.node = node; c.nodes = nodes;
    c.gnames = gnames; c.fnnames = fnnames; c.id = id;
    c.try_catch_at = try_catch_at;

    Buf body;
    buf_init(&body);
    int emitted = 0;
    for (int i = 0; i < fn->ninsns; i++) {
        if (need[i]) buf_fmt(&body, "L%d:" NL, i);
        size_t before = body.len;
        int rc = emit_one(&c, &body, i);
        if (rc != 0) {
            free(need); free(try_catch_at); buf_free(&body);
            return bad("container=%d insn=%d op=%s was accepted by the pre-scan but not emitted",
                       id, i, op_name(fn->insns[i].op));
        }
        if (body.len == before) {
            free(need); free(try_catch_at); buf_free(&body);
            return bad("container=%d insn=%d op=%s emitted nothing, so the conservation equation could not see it",
                       id, i, op_name(fn->insns[i].op));
        }
        buf_str(&body, NL);
        emitted++;
        g_used_by_op[fn->insns[i].op < 256 ? fn->insns[i].op : 0] = 1;
    }
    if (need[fn->ninsns]) buf_fmt(&body, "L%d: ;" NL, fn->ninsns);
    free(need); free(try_catch_at);

    if (nodes[id].is_main) {
        buf_str(out, "int main(void) {" NL);
    } else {
        /* The hidden `self` parameter: FIRST, so the user arguments keep their
           numbers (a0 is still the first argument, and v[k+1] = a_k below is
           unchanged), and only on a container that reads a capture slot inside
           the environment its creator built -- see func_cap's note for why
           unconditional would move every other program's text. */
        int self = func_reads_capture(m, nodes, id);
        buf_fmt(out, "static MirVal %s(", fnnames[id]);
        if (argc == 0 && !self) buf_str(out, "void");
        if (self) buf_str(out, "MirVal self");
        for (int k = 0; k < argc; k++)
            buf_fmt(out, "%sMirVal a%d", (k || self) ? ", " : "", k);
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
    /* THE DEPTH THIS CONTAINER ENTERED WITH.  A returning function pops the
       frames that belong to it (src/vm/vm.c:4142); mv_try_mark is that rule
       turned into a number the function can restore, and every `return` and
       every "return to the caller's check" path resets to it as a MIN (see
       emit_try_check). */
    if (g_try_used && !nodes[id].is_main)
        buf_str(out, "    int mv_try_mark = mv_try_depth;   /* frames open at entry */" NL);
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
    g_bound_used = 0;   /* a fresh pre-scan decides this, not the last call */
    g_wait_used = 0;    /* a whole-program scan decides this one; see its note */
    if (g_gref_n > 0) {
        g_gref = calloc((size_t)g_gref_n, 1);
        if (!g_gref) { refuse("out-of-memory", "the global reference table"); goto done; }
    }

    for (int f = 0; f < m->nfuncs && !g_wait_used; f++) {
        const IrFunc *wf = &m->funcs[f];
        for (int i = 0; i < wf->ninsns; i++)
            if (wf->insns[i].op == OP_WAIT) { g_wait_used = 1; break; }
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

    /* THE BOUND TABLE, emitted only for a program that declares a constraint
       (`name: set [= init]`) -- g_bound_used's note says why the guard is a
       program-level switch rather than an always-on one.  The entry stores
       what the interpreter stores in vm->global_bound: the set's region index
       plus one, with zero meaning "no bound" (src/vm/vm.c, L_BIND:
       `vm->global_bound[g] = sidx >= 0 ? sidx + 1 : 0`).  Storing the index
       itself would make set 0 indistinguishable from no bound, and the pool's
       handles start at 1, so +1 loses nothing.
       The stored index is NOT resolved through mv_arr_live, and that is a
       claim, not an oversight: a set has no write path in this route (OP_SET_ADD
       is not emitted), and the only copy path that moves a region --
       mv_dict_set's non-frontier branch -- copies the dict it was told to write
       to the FRONTIER and leaves every other region where it is, so a handle
       taken from a set stays valid for the life of the pool.  If a set ever
       grows a write path, this entry has to start resolving (the array case in
       mv_len, §10.129, is the same mistake in a different arm). */
    if (g_bound_used) {
        int nb = root->global_name_count > 0 ? root->global_name_count : 1;
        buf_fmt(&out, "static int mv_bound[%d];   /* +1 per global, 0 = no bound */" NL, nb);
        buf_str(&out,
            "static void mv_bind_stop(const char *msg) {\n"
            "    fflush(stdout);   /* the lines say() already wrote stay written */\n"
            "    fprintf(stderr, \"[exception] uncaught: %s\\n\", msg);\n"
            "    exit(1);          /* atexit(mv_free_all) still reports the pool */\n"
            "}\n"
            NL);
    }
    /* THE ARITHMETIC STOPS, RE-EXPRESSED AS THROWS -- and emitted only for a
       program that carries a try pair.  The stops themselves live in
       aot_native_preamble (nv_die_division_by_zero, nv_die_numeric_overflow) and
       that preamble is byte-identical in both routes by construction
       (classify() reads it), so nothing in it may change.  What CAN change is
       which function the emitted instruction calls: with a try in the program,
       OP_ADD/OP_SUB/OP_MUL/OP_DIV/OP_MOD/OP_NEG call these wrappers, which
       pre-test the preamble's own condition and route the failure through
       mv_throw instead of exit(1).  Every predicate here is a COPY of the
       preamble's, which is why each one names the line it copies; a wrapper that
       invented a wider predicate would raise a catchable exception where the
       interpreter answers a value (`1.0/0.0` prints inf, `.scratch/probe3/fdiv`).
       The wrappers call the same mv_* they wrap on every path that does NOT
       stop, so the answer and the allocation reading of an arithmetic result are
       unchanged. */
    if (g_try_used) {
        buf_str(&out,
            "static inline MirVal mv_try_add(MirVal a, MirVal b) {\n"
            "    NV x, y; long long r;\n"
            "    if (a.type == MV_STR || b.type == MV_STR) return mv_add(a, b);\n"
            "    x = mv_as_nv(a); y = mv_as_nv(b);\n"
            "    if (x.t == NV_FLT || y.t == NV_FLT) return mv_add(a, b);\n"
            "    if (__builtin_add_overflow(x.i, y.i, &r)) { mv_throw(mv_str(\"numeric_overflow\")); return mv_nil(); }\n"
            "    return mv_add(a, b);\n"
            "}\n"
            "static inline MirVal mv_try_sub(MirVal a, MirVal b) {\n"
            "    NV x = mv_as_nv(a), y = mv_as_nv(b); long long r;\n"
            "    if (x.t != NV_FLT && y.t != NV_FLT && __builtin_sub_overflow(x.i, y.i, &r)) { mv_throw(mv_str(\"numeric_overflow\")); return mv_nil(); }\n"
            "    return mv_sub(a, b);\n"
            "}\n"
            "static inline MirVal mv_try_mul(MirVal a, MirVal b) {\n"
            "    NV x = mv_as_nv(a), y = mv_as_nv(b); long long r;\n"
            "    if (x.t != NV_FLT && y.t != NV_FLT && __builtin_mul_overflow(x.i, y.i, &r)) { mv_throw(mv_str(\"numeric_overflow\")); return mv_nil(); }\n"
            "    return mv_mul(a, b);\n"
            "}\n"
            "static inline MirVal mv_try_div(MirVal a, MirVal b) {\n"
            "    NV x = mv_as_nv(a), y = mv_as_nv(b);\n"
            "    if (x.t != NV_FLT && y.t != NV_FLT && y.i == 0) { mv_throw(mv_str(\"division_by_zero\")); return mv_nil(); }\n"
            "    return mv_div(a, b);\n"
            "}\n"
            "static inline MirVal mv_try_mod(MirVal a, MirVal b) {\n"
            "    if (nv_asi(mv_as_nv(b)) == 0) { mv_throw(mv_str(\"division_by_zero\")); return mv_nil(); }\n"
            "    return mv_mod(a, b);\n"
            "}\n"
            "static inline MirVal mv_try_neg(MirVal a) {\n"
            "    NV x = mv_as_nv(a); long long r;\n"
            "    if (x.t != NV_FLT && __builtin_sub_overflow(0LL, x.i, &r)) { mv_throw(mv_str(\"numeric_overflow\")); return mv_nil(); }\n"
            "    return mv_neg(a);\n"
            "}\n"
            NL);
    }
    /* THE DELAY ITSELF, emitted only for a program that carries a `wait`.
       Why a helper at all: the interpreter's arm is a blocking sleep in the CLI
       shape (src/vm/vm.c, L_WAIT's final else), and a route that dropped the
       delay would be answering with no delay while claiming the same bytes.
       The type ladder below is a COPY of the interpreter's, down to the
       non-number case: INT reads ival, FLOAT reads fval, EVERYTHING ELSE is 0.0.
       mv_as_nv is deliberately NOT used here -- it aborts on a string
       (mv_tag_to_nv's default), while the interpreter reads a string operand as
       zero seconds and carries on (measured, .scratch/probe3).
       The include is emitted here and not in the preamble: aot_native_preamble()
       is byte-identical in both routes by construction, so a MIR-only include
       may only live after it.
       `ms <= 0` returns at once.  That is a KNOWN DIVERGENCE on the negative
       side and the boundary section says so: the interpreter hands -1 second
       to an unsigned sleep and never returns (measured by watching a probe for
       two minutes, then killing it), and a hang is not an answer to copy. */
    if (g_wait_used) {
        buf_str(&out,
            "#if defined(_WIN32)\n"
            "#include <windows.h>\n"
            "#else\n"
            "#include <time.h>\n"
            "#endif\n"
            "static void mv_wait(MirVal v) {\n"
            "    double seconds = (v.type == MV_INT) ? (double)v.ival\n"
            "                   : (v.type == MV_FLT) ? v.fval : 0.0;\n"
            "    int ms = (int)(seconds * 1000.0);\n"
            "    if (ms <= 0) return;\n"
            "#if defined(_WIN32)\n"
            "    Sleep((unsigned)ms);\n"
            "#else\n"
            "    struct timespec ts;\n"
            "    ts.tv_sec = ms / 1000;\n"
            "    ts.tv_nsec = (long)(ms % 1000) * 1000000L;\n"
            "    nanosleep(&ts, (struct timespec *)0);\n"
            "#endif\n"
            "}\n"
            NL);
    }

    for (int i = 1; i < m->nfuncs; i++) {
        int argc = nodes[i].argc;
        int self = func_reads_capture(m, nodes, i);
        buf_fmt(&out, "static MirVal %s(", fnnames[i]);
        if (argc == 0 && !self) buf_str(&out, "void");
        if (self) buf_str(&out, "MirVal self");
        for (int k = 0; k < argc; k++)
            buf_fmt(&out, "%sMirVal", (k || self) ? ", " : "");
        buf_str(&out, ");" NL);
    }
    buf_str(&out, NL);

    /* THE CAPTURE ENVIRONMENT, the same shape and the same reason as the table
       below: emitted ONLY for a module that actually builds or reads one.
       Why it is not part of kMirValBlock: that block is appended to every
       generated file, so a runtime function no program references would move
       the generated text -- and therefore the classify()/IR-MIR-C-SAME
       readings -- of the 86-case corpus, which has no closure in it.  A
       closure program is the only program whose text changes here, and its
       readings are what the capture cell is measured by.
       What the two functions are NOT: a reference-counted environment.  The
       slot array is COPIED at creation (the interpreter's L_MAKE_FUNC copies
       the pushed values into im_closure_env_new the same way, src/vm/vm.c:4040-4054),
       registered so the process's exit sweep can free it, and never shared
       with the creator's stack -- so a value that outlives the frame that
       built it still reads its own slots.  No bound is checked in mv_capture:
       the emitting arm only calls it with a slot the container's environment
       provably has, and answers mv_nil() for every other shape (which is the
       interpreter's own answer for a missing environment or an out-of-range
       slot, src/vm/vm.c:4033-4039). */
    int needs_cap = 0;
    for (int f = 0; f < m->nfuncs && !needs_cap; f++) {
        const IrFunc *fn = &m->funcs[f];
        for (int i = 0; i < fn->ninsns; i++) {
            const IrInsn *in = &fn->insns[i];
            if (in->op == OP_LOAD_CAPTURE) { needs_cap = 1; break; }
            if (in->op == OP_MAKE_FUNC && ir_insn_r3(in) > 0) { needs_cap = 1; break; }
        }
    }
    if (needs_cap) {
        buf_str(&out, "static inline MirVal mv_fun_cap(int idx, const MirVal *slots, int ncap) {" NL);
        buf_str(&out, "    MirVal r = mv_fun(idx);" NL);
        buf_str(&out, "    if (ncap > 0) {" NL);
        buf_str(&out, "        MirVal *env = (MirVal *)mv_alloc(sizeof(MirVal) * (size_t)ncap);" NL);
        buf_str(&out, "        for (int i = 0; i < ncap; i++) env[i] = slots[i];" NL);
        buf_str(&out, "        r.ptr = env;" NL);
        buf_str(&out, "    }" NL);
        buf_str(&out, "    return r;" NL);
        buf_str(&out, "}" NL);
        buf_str(&out, "static inline MirVal mv_capture(MirVal self, int slot) {" NL);
        buf_str(&out, "    if (!self.ptr) return mv_nil();" NL);
        buf_str(&out, "    return ((MirVal *)self.ptr)[slot];" NL);
        buf_str(&out, "}" NL);
        buf_str(&out, NL);
    }

    /* THE FUNCTION TABLE'S CALLABLE HALF, emitted only for a module that
       actually moves a function VALUE, which is why this is a scan and not part
       of kMirValBlock:
         - it emits the dispatch whose `case` labels are THIS route's module
           indices and whose arms call the containers by their translation-
           unit names (the forward declarations above are exactly the prototypes
           it needs, and it is emitted between them and the bodies so main can
           call it);
         - it is emitted ONLY when OP_MAKE_FUNC or OP_CALL_VALUE occurs, because
           a table no container references would change the generated text of
           every accepted case that has nothing to do with functions -- the
           C-mirror comparison in tools/aot_mir.test.py (classify(), the
           `IR-MIR-C-SAME`/`c-identical` readings) would then move for programs
           this cell does not touch.  A closure program is the only program
           whose emitted text changes here, and its readings are what this cell
           is measured by.
       What the dispatch is NOT: a second interpreter.  It carries the interpreter's
       L_CALL_VALUE guard verbatim -- a non-function callee is `error: value is not
       callable` on stderr with rc 1 -- because that is the answer the criterion
       compares, and it stops the program on an arity mismatch instead of
       fabricating an answer the interpreter would fill differently. */
    int needs_callv = 0;
    for (int f = 0; f < m->nfuncs && !needs_callv; f++) {
        const IrFunc *fn = &m->funcs[f];
        for (int i = 0; i < fn->ninsns; i++) {
            int op = fn->insns[i].op;
            if (op == OP_MAKE_FUNC || op == OP_CALL_VALUE) { needs_callv = 1; break; }
        }
    }
    if (needs_callv) {
        buf_str(&out, "static MirVal mv_callv(MirVal f, MirVal *args, int nargs) {" NL);
        buf_str(&out, "    if (f.type != MV_FUN) {" NL);
        buf_str(&out, "        fprintf(stderr, \"error: value is not callable\\n\");" NL);
        buf_str(&out, "        abort();" NL);
        buf_str(&out, "    }" NL);
        buf_str(&out, "    switch ((int)f.ival) {" NL);
        for (int i = 1; i < m->nfuncs; i++) {
            int argc = nodes[i].argc;
            buf_fmt(&out, "    case %d:" NL, i);
            buf_str(&out, "        if (nargs != ");
            buf_fmt(&out, "%d", argc);
            buf_str(&out, ") {" NL);
            buf_fmt(&out, "            fprintf(stderr, \"error: function takes %d argument(s),"
                          " got %%d\\n\", nargs);" NL, argc);
            buf_str(&out, "            abort();" NL);
            buf_str(&out, "        }" NL);
            buf_fmt(&out, "        return %s(", fnnames[i]);
            /* A container that reads its environment needs the VALUE back, not
               a bare call: mv_callv was handed the function value itself, and
               that value carries the environment ptr the read goes through.
               `f` is mv_callv's own parameter, so the emitted call is
               fn(f, args[0], ..., args[argc-1]) -- one extra leading argument. */
            if (func_reads_capture(m, nodes, i)) buf_str(&out, "f");
            for (int k = 0; k < argc; k++)
                buf_fmt(&out, "%sargs[%d]",
                        (k || func_reads_capture(m, nodes, i)) ? ", " : "", k);
            buf_str(&out, ");" NL);
        }
        buf_str(&out, "    default:" NL);
        buf_str(&out, "        fprintf(stderr, \"error: function index %d has no container\\n\"," NL);
        buf_str(&out, "                (int)f.ival);" NL);
        buf_str(&out, "        abort();" NL);
        buf_str(&out, "    }" NL);
        buf_str(&out, "}" NL);
        buf_str(&out, NL);
    }

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
