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
"#define MV_STR 2   /* = VAL_STRING -- constructible only as a resident constant */\n"
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
"                                         arm needs no residency guard; see the\n"
"                                         debt note under mv_eq. */\n"
"    return nv_tru(mv_as_nv(a));\n"
"}\n"
"static inline MirVal mv_neg(MirVal a) { return mv_from_nv(nv_neg(mv_as_nv(a))); }\n"
"static inline MirVal mv_not(MirVal a) { return mv_boo(!mv_tru(a)); }\n"
"static inline MirVal mv_add(MirVal a, MirVal b) { return mv_from_nv(nv_add(mv_as_nv(a), mv_as_nv(b))); }\n"
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
" * so the three arms are one change.  Note the asymmetry that remains and is\n"
" * written up in docs/STATUS.md 10.105: the CONSTANT form `s == \"hi\"` still\n"
" * refuses safely (OP_EQK, unsupported here), so the same question is refused\n"
" * in one spelling and answered correctly in the other.\n"
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
" * Zero allocation: the payload is the resident literal, never copied.  No new\n"
" * print exit, and no change to what an operation means beyond the tags that\n"
" * genuinely had no answer.\n"
" *\n"
" * WHAT IT DOES NOT COVER.  Deliberately, and each is a decision:\n"
" *   - ORDERING on strings (`s < t`, `<=`, `>`, `>=`).  This WAS a live hole\n"
" *     of exactly this shape and this note carried it as the next entry.  It\n"
" *     is CLOSED now: mv_str_cmp below reads src/vm/vm.c:443 and :449-451 the\n"
" *     way val_cmp does (pointer fast path, then strcmp), and\n"
" *     docs/STATUS.md 10.107 writes the mixed-type boundary down.  The bullet\n"
" *     stays in past tense on purpose -- it is a record of what this note did\n"
" *     NOT cover at the time, not a claim about today.\n"
" *   - A string that is not resident (`ival != 1`, or `sval` NULL).  No\n"
" *     emittable opcode builds one (mv_str interns), so this route cannot have\n"
" *     produced one.  The arm STOPS the program instead of reading whatever\n"
" *     happens to sit in `sval`: the abort below is an IMPOSSIBLE-STATE GUARD,\n"
" *     not a missing branch's capability, and it is spelled as an abort rather\n"
" *     than a silent answer so that a future producer of such a value cannot\n"
" *     make this arm lie.\n"
" *   - A string against an array/dict/set, a string as a set or dict key,\n"
" *     case folding, Unicode normalisation, locale collation.  Those opcodes\n"
" *     are outside this subset.\n"
" *\n"
" * DELETE CONDITION (decidable, not a wish): delete the three arms in the\n"
" * commit that gives the shared NV a string tag and moves Value's comparison\n"
" * semantics into the IR -- the P3 step.  Then mv_eq/mv_ne/mv_tru go back to\n"
" * delegating to nv_* like every operation around them, and the check is\n"
" * mechanical: `grep -c 'a.ival != 1'` over kMirValBlock is 0 and MV_STR no\n"
" * longer appears in this block outside the tag #define, mv_str() and mv_say's\n"
" * print branch.  Deleting them earlier, while NV still carries no string\n"
" * payload, would leave OP_EQ with no answer for a value the emitter does\n"
" * produce -- which is precisely the crash this note exists to name.\n"
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
"        if (a.ival != 1 || b.ival != 1 || !a.sval || !b.sval) abort();  /* guard */\n"
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
" * Zero allocation: strcmp reads the resident literal, no string is created.\n"
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
"    if (a.ival != 1 || b.ival != 1 || !a.sval || !b.sval) abort();  /* impossible-state guard */\n"
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
" * WHAT IT DOES NOT COVER.  A string that is not resident (ival != 1).  No\n"
" * emittable opcode builds one -- CONCAT, INDEX_GET, NEW_ARRAY and the rest\n"
" * are outside this subset -- so an MV_STR with ival != 1 is a value this\n"
" * route cannot have produced, and it stops the program instead of printing\n"
" * whatever integer happens to sit in ival.\n"
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
"        if (a.ival == 1 && a.sval) { printf(\"%s\\n\", a.sval); return; }\n"
"        abort();   /* a dynamic string: nothing in this subset constructs one */\n"
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
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
    case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
    case OP_AND: case OP_OR: return R1 | R2 | R3;
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
    case OP_SUB:          buf_fmt(b, "    v[%d] = mv_sub(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_MUL:          buf_fmt(b, "    v[%d] = mv_mul(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_DIV:          buf_fmt(b, "    v[%d] = mv_div(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_MOD:          buf_fmt(b, "    v[%d] = mv_mod(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_EQ:           buf_fmt(b, "    v[%d] = mv_eq(v[%d], v[%d]);", r1, r2, r3); return 0;
    case OP_NEQ:          buf_fmt(b, "    v[%d] = mv_ne(v[%d], v[%d]);", r1, r2, r3); return 0;
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

    int has_args = 0;
    for (int i = 0; i < fn->ninsns; i++) {
        int op = fn->insns[i].op;
        if (op == OP_PUSH_REG || op == OP_CALL_FUNC) has_args = 1;
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
        buf_fmt(out, "    MirVal mvargs[%d];" NL, nregs);
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
        refuse("illegal-operand", g_detail);
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
