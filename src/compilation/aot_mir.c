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
"#define MV_STR 2   /* = VAL_STRING -- numbered, not yet constructible */\n"
"#define MV_BOO 3   /* = VAL_BOOL -- 3, not NV's 2; see the tag pair below */\n"
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
"/* The tag pair is the only place where the two numberings meet: NV_INT 0 /\n"
"   NV_FLT 1 / NV_BOO 2 (aot_native.c:175-177) against MV_INT 0 / MV_FLT 1 /\n"
"   MV_BOO 3.  int and float agree; bool does not, so it is translated here\n"
"   and nowhere else.  A tag with no NV counterpart stops the program rather\n"
"   than answering with NV_BOO's number, which would be a silent wrong\n"
"   answer -- OP_LOADK_STRING must add its own direction to this pair. */\n"
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
"/* Every operation below is the nv_* one, reached through the field moves\n"
"   above: the delegating body IS the claim that the runtime is single. */\n"
"static inline MirVal mv_int(long long x) { return mv_from_nv(nv_int(x)); }\n"
"static inline MirVal mv_flt(double x)    { return mv_from_nv(nv_flt(x)); }\n"
"static inline MirVal mv_boo(int x)       { return mv_from_nv(nv_boo(x)); }\n"
"static inline MirVal mv_neg(MirVal a) { return mv_from_nv(nv_neg(mv_as_nv(a))); }\n"
"static inline MirVal mv_not(MirVal a) { return mv_from_nv(nv_not(mv_as_nv(a))); }\n"
"static inline MirVal mv_add(MirVal a, MirVal b) { return mv_from_nv(nv_add(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_sub(MirVal a, MirVal b) { return mv_from_nv(nv_sub(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_mul(MirVal a, MirVal b) { return mv_from_nv(nv_mul(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_div(MirVal a, MirVal b) { return mv_from_nv(nv_div(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_mod(MirVal a, MirVal b) { return mv_from_nv(nv_mod(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_eq(MirVal a, MirVal b) { return mv_from_nv(nv_eq(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_ne(MirVal a, MirVal b) { return mv_from_nv(nv_ne(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_lt(MirVal a, MirVal b) { return mv_from_nv(nv_lt(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_le(MirVal a, MirVal b) { return mv_from_nv(nv_le(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_gt(MirVal a, MirVal b) { return mv_from_nv(nv_gt(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline MirVal mv_ge(MirVal a, MirVal b) { return mv_from_nv(nv_ge(mv_as_nv(a), mv_as_nv(b))); }\n"
"static inline int mv_tru(MirVal a) { return nv_tru(mv_as_nv(a)); }\n"
"static inline void mv_say(MirVal a) { nv_say(mv_as_nv(a)); }\n"
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
   targets).  Returns -1 for an opcode this generator does not emit. */
enum { R1 = 1, R2 = 2, R3 = 4 };

static int op_regs(int op) {
    switch (op) {
    case OP_MOV: return R1 | R2;
    case OP_LOADK_INT: case OP_LOADK_FLOAT: case OP_LOADK_BOOL: case OP_LOADK_I64: return R1;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
    case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT: case OP_LE: case OP_GE:
    case OP_AND: case OP_OR: return R1 | R2 | R3;
    case OP_NEG: case OP_NOT: return R1 | R2;
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
            case OP_LOAD_GLOBAL:
            case OP_STORE_GLOBAL: {
                int ix = (op == OP_LOAD_GLOBAL) ? ir_insn_r2(in) : in->dst;
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
    case OP_LOAD_GLOBAL:  buf_fmt(b, "    v[%d] = %s;", r1, c->gnames[w2]); return 0;
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
    case OP_RETURN:
        if (c->nodes[c->id].is_main) { buf_str(b, "    return 0;"); return 0; }
        if (r1 > 0) buf_fmt(b, "    return v[%d];", r1);
        else buf_str(b, "    return mv_int(0);");
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
    if (has_args) {
        buf_fmt(out, "    MirVal mvargs[%d];" NL, nregs);
        buf_str(out, "    int asp = 0;" NL);
    }
    for (int k = 0; k < argc; k++) buf_fmt(out, "    v[%d] = a%d;" NL, k + 1, k);
    if (body.d) buf_str(out, body.d);
    g_body_bytes += body.len;
    buf_free(&body);

    if (nodes[id].is_main) buf_str(out, "    return 0;" NL "}" NL);
    else buf_str(out, "    return mv_int(0);" NL "}" NL NL);
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

    for (int i = 0; i < root->global_name_count; i++) {
        if (!gnames[i]) continue;
        buf_fmt(&out, "static MirVal %s = { MV_INT, 0, 0 };" NL, gnames[i]);
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
