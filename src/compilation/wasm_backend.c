/* wasm_backend.c - minimal WebAssembly MVP output backend
 *
 * Emits a standalone .wasm binary (no external toolchain needed).  Every
 * variable/temp is an 8-byte linear-memory slot [tag: i32][payload: i64],
 * tags NIL=0 INT=1 FLOAT=2 BOOL=3 (STRING=4 traps at runtime).  Arithmetic,
 * comparison and truthiness semantics mirror the C VM exactly:
 *   - L_ADD/L_SUB/L_MUL: int ops are 32-bit, overflow -> float
 *   - L_DIV: always double; int/0 throws division_by_zero
 *   - L_MOD: int%int, else (int)as_double(a) % (int)as_double(b)
 *   - val_cmp: ordering comparisons go through the double path
 *   - val_eq: same-tag payload compare; cross int/float via double
 *   - L_JUMP_IF_FALSE truthiness (0 / 0.0 / nil are false)
 * Output goes through the fixed import table (docs/WASM.md PAL principle):
 *   env.im_print_int(i64) / env.im_print_float(f64) / env.im_print_bool(i32)
 *   env.im_print_nil() / env.im_error(i32)
 * Unsupported constructs are rejected at compile time with line numbers. */
#include "wasm_backend.h"
#include "../parser/ast.h"
#include "../parser/parser.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- binary buffer ---------- */
typedef struct { unsigned char *b; size_t n, cap; int blocks; } Buf;

static void bput(Buf *w, unsigned char c) {
    if (w->n == w->cap) { w->cap = w->cap ? w->cap * 2 : 4096; w->b = realloc(w->b, w->cap); }
    w->b[w->n++] = c;
}
static void bword(Buf *w, const char *s) {
    size_t len = strlen(s);
    if (len > 127) len = 127;
    bput(w, (unsigned char)len);
    for (size_t i = 0; i < len; i++) bput(w, (unsigned char)s[i]);
}
static void bleb_u(Buf *w, unsigned long v) {
    do { unsigned char c = v & 0x7F; v >>= 7; if (v) c |= 0x80; bput(w, c); } while (v);
}
static void bleb_s(Buf *w, long long v) {
    int more = 1;
    while (more) {
        unsigned char c = v & 0x7F;
        v >>= 7;
        if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) more = 0;
        else c |= 0x80;
        bput(w, c);
    }
}
static void bf64(Buf *w, double d) {
    unsigned char *p = (unsigned char *)&d;
    for (int i = 0; i < 8; i++) bput(w, p[i]);
}

/* ---------- layout ---------- */
#define TAG_NIL 0
#define TAG_INT 1
#define TAG_FLOAT 2
#define TAG_BOOL 3
#define TAG_STR 4
#define TAG_ARR 5                          /* heap-allocated array (see below) */

#define FRAME_SLOTS 256
#define SLOT_BYTES 16                      /* [tag i32 @+0][pad][i64 payload @+8] */
#define FRAME_BYTES (FRAME_SLOTS * SLOT_BYTES) /* 4096 */
#define LOCAL_MAX 120
#define TEMP_BASE 128
#define TEMP_MAX 64
#define STACK_FRAMES 1024
#define GLOBALS_BASE (FRAME_BYTES + STACK_FRAMES * FRAME_BYTES) /* 4198400 */
#define MEM_PAGES 128

/* ---- linear-memory heap (docs/WASM.md "Heap") --------------------------
   Fixed-size arena carved out of the unused tail of linear memory.  Blocks
   carry a 16-byte header: [size i32 @+0][refs i32 @+4][next_free i32 @+8]
   [elements i32 @+12]; payloads are 16-byte aligned, which matches one boxed
   value slot.  Reclamation is deterministic (refcounts), never a tracing GC:
   the WebAssembly GC proposal is a different feature and is NOT implemented
   here.  Exhaustion is explicit - im_error(ERR_HEAP_EXHAUSTED) + trap.
   Allocation strategy is first-fit over a LIFO free list with a bump
   fallback; a freed block that abuts the bump top is trimmed back.        */
#define HEAP_HEADER 16
#define HEAP_BASE_RAW (GLOBALS_BASE + 512 * SLOT_BYTES)
#define HEAP_BASE (((HEAP_BASE_RAW) + 15) & ~15)
#define HEAP_END ((long long)MEM_PAGES * 65536)
#define HEAP_BYTES (HEAP_END - HEAP_BASE)
#define ARRAY_MAX_ELEMS ((HEAP_BYTES - HEAP_HEADER) / SLOT_BYTES)
/* host-visible error codes handed to env.im_error (tools/wasm_run.js) */
#define ERR_HEAP_EXHAUSTED 4
#define ERR_ARRAY_INDEX 5
#define ERR_ARRAY_OP 6
/* heap helper/benchmark functions are appended after the three ABI probes so
   that every existing function index stays valid */
#define HFN_ALLOC 0
#define HFN_FREE 1
#define HFN_RETAIN 2
#define HFN_RELEASE 3
#define HFN_COUNT 4
#define FUNC_HELPER(h) (FUNC_MAIN + 4 + g_func_count + (h))
#define FUNC_BENCH_SCALAR (FUNC_MAIN + 4 + g_func_count + HFN_COUNT)
#define FUNC_BENCH_SIMD (FUNC_MAIN + 5 + g_func_count + HFN_COUNT)

/* ---------- opcodes ---------- */
#define W_UNREACHABLE 0x00
#define W_BLOCK 0x02
#define W_LOOP 0x03
#define W_IF 0x04
#define W_ELSE 0x05
#define W_END 0x0B
#define W_BR 0x0C
#define W_BR_IF 0x0D
#define W_RETURN 0x0F
#define W_CALL 0x10
#define W_LOCAL_GET 0x20
#define W_LOCAL_SET 0x21
#define W_LOCAL_TEE 0x22
#define W_GLOBAL_GET 0x23
#define W_GLOBAL_SET 0x24
#define W_I32_LOAD 0x28
#define W_I64_LOAD 0x29
#define W_I32_STORE 0x36
#define W_I64_STORE 0x37
#define W_I32_EQZ 0x45
#define W_I32_EQ 0x46
#define W_I32_NE 0x47
#define W_I32_LT_S 0x48
#define W_I32_LT_U 0x49
#define W_I32_GT_S 0x4A
#define W_I32_GT_U 0x4B
#define W_I32_GE_S 0x4E
#define W_I32_GE_U 0x4F
#define W_I64_EQ 0x51
#define W_I64_NE 0x52
#define W_I64_LT_S 0x53
#define W_I64_GT_S 0x55   /* 53=lt_s 54=lt_u 55=gt_s 56=gt_u */
#define W_F64_EQ 0x61
#define W_F64_LT 0x63
#define W_F64_GT 0x64
#define W_F64_LE 0x65
#define W_F64_GE 0x66
#define W_I32_ADD 0x6A
#define W_I32_SUB 0x6B
#define W_I32_MUL 0x6C
#define W_I32_AND 0x71
#define W_I32_OR 0x72
#define W_I32_XOR 0x73
#define W_I32_SHL 0x74
#define W_I64_ADD 0x7C
#define W_I64_SUB 0x7D
#define W_I64_MUL 0x7E
#define W_I64_REM_S 0x81
#define W_F64_ADD 0xA0
#define W_F64_SUB 0xA1
#define W_F64_MUL 0xA2
#define W_F64_DIV 0xA3
#define W_F64_NEG 0x9A
#define W_I32_CONST 0x41
#define W_I64_CONST 0x42
#define W_F64_CONST 0x44
#define W_I32_WRAP_I64 0xA7
/* SIMD (v128) - the 0xFD prefix takes a u32 LEB opcode (f64x2.add = FD F0 01) */
#define W_SIMD 0xFD
#define S_F64X2_SPLAT 0x14
#define S_F64X2_ADD 0xF0
#define S_F64X2_EXTRACT_LANE 0x21

#define W_I64_EXTEND_I32_S 0xAC
#define W_F64_CONVERT_I32_S 0xB7
#define W_F64_CONVERT_I64_S 0xB9   /* f64.convert_i64_s: B7=i32_s, B8=i32_u, B9=i64_s */
#define W_I64_REINTERPRET_F64 0xBD
#define W_F64_REINTERPRET_I64 0xBF

/* func indices: 0..4 imports, 5 = main, 6.. = user funcs */
#define IMP_PRINT_INT 0
#define IMP_PRINT_FLOAT 1
#define IMP_PRINT_BOOL 2
#define IMP_PRINT_NIL 3
#define IMP_ERROR 4
#define FUNC_MAIN 5
#define GIDX_SP 0                          /* $sp global (mut i32) */
#define GIDX_HBUMP 1                       /* heap bump pointer (mut i32) */
#define GIDX_HFREE 2                       /* heap free-list head (mut i32) */

/* locals: 0 = base param; declared groups follow (see emit_locals_decl) */
#define LOC_TAGA 1
#define LOC_TAGB 2
#define LOC_RES 3
#define LOC_CNT 4
#define LOC_FBASE 5
#define LOC_IA 6
#define LOC_IB 7
#define LOC_ARR 9                          /* i32: array base during indexing */
#define LOC_IDX 10                         /* i32: decoded array index */

/* slot address kinds */
#define KIND_FRAME 0                       /* via local 0 (own frame base) */
#define KIND_CALLEE 1                      /* via LOC_FBASE (callee frame) */
#define KIND_GLOBAL 2

/* ---------- context ---------- */
typedef struct { char *name; int slot; } Named;
typedef struct {
    Named locals[LOCAL_MAX];
    int local_count;
    const char *gdecls[LOCAL_MAX];
    int gdecl_count;
    int in_function;
    int nparams;                           /* params are released by the caller */
    int line;
} FnEnv;

typedef struct {
    Named globals[512];
    int global_count;
    struct { char *name; int params; } funcs[256];
    int func_count;
    char err[512];
    int line;
} Cg;

static char g_err[512];
const char *wasm_backend_last_error(void) { return g_err; }

static int fail(Cg *cg, FnEnv *env, const char *fmt, ...) {
    if (cg->err[0]) return -1;
    char msg[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(cg->err, sizeof(cg->err), "wasm MVP subset: %s (line %d)", msg, env ? env->line : cg->line);
    return -1;
}

/* ---------- emit helpers ---------- */
static void e(Buf *w, unsigned char op) {
    if (op == 0x02 || op == 0x03 || op == 0x04) w->blocks++;
    else if (op == 0x0B) w->blocks--;
    bput(w, op);
}
static void e_u(Buf *w, unsigned long v) { bleb_u(w, v); }
static void e_i(Buf *w, long long v) { bleb_s(w, v); }
static void e_i32c(Buf *w, long long v) { e(w, W_I32_CONST); e_i(w, v); }
static void e_i64c(Buf *w, long long v) { e(w, W_I64_CONST); e_i(w, v); }
static void e_f64c(Buf *w, double v) { e(w, W_F64_CONST); bf64(w, v); }
static void e_trunc_sat_i32(Buf *w) { e(w, 0xFC); e_u(w, 2); } /* i32.trunc_sat_f64_s: FC 02 */

static void e_addr(Buf *w, int kind, int slot) {
    if (kind == KIND_GLOBAL) {
        e_i32c(w, GLOBALS_BASE + (long long)slot * SLOT_BYTES);
    } else {
        e(w, W_LOCAL_GET); e_u(w, kind == KIND_CALLEE ? LOC_FBASE : 0);
        e_i32c(w, (long long)slot * SLOT_BYTES);
        e(w, W_I32_ADD);
    }
}

/* ---------- ownership: deterministic refcount reclamation ----------------
   docs/WASM.md "Ownership".  A slot that OWNS a reference is a named local,
   a global, a callee parameter/return slot, or an array element slot; each
   owning slot counts exactly one reference.  Frame temporaries (slot >=
   TEMP_BASE) only BORROW, so a store into a temporary emits no refcount code
   at all.  Every store into an owning slot retains the new object and then
   releases the object it replaces; retain comes first so that 'a = a[0]'
   cannot free the object it is about to store.                              */
static int g_func_count;                    /* set before codegen, for FUNC_HELPER */

static void e_call_helper(Buf *w, int h) {
    e(w, W_CALL); e_u(w, (unsigned long)FUNC_HELPER(h));
}
/* if the value held in the LOC_TAGA/LOC_IA pair is an array, retain it */
static void e_ret_loaded(Buf *w) {
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_ARR);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA); e(w, W_I32_WRAP_I64);
    e_call_helper(w, HFN_RETAIN);
    e(w, W_END);
}
/* if the slot holds an array, call retain/release on its pointer */
static void e_ref_slot(Buf *w, int kind, int slot, int h) {
    e_addr(w, kind, slot);
    e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);
    e_i32c(w, TAG_ARR);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e_addr(w, kind, slot);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
    e(w, W_I32_WRAP_I64);
    e_call_helper(w, h);
    e(w, W_END);
}
static int is_owned_slot(int kind, int slot) {
    if (kind == KIND_FRAME) return slot < TEMP_BASE;
    return 1;                              /* callee slot, global */
}

static void e_store_tag(Buf *w, int kind, int slot, int tag) {
    if (is_owned_slot(kind, slot)) e_ref_slot(w, kind, slot, HFN_RELEASE);
    e_addr(w, kind, slot);
    e_i32c(w, tag);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
}
static void e_store_payload_i64_from_stack(Buf *w, int kind, int slot) {
    /* stack: [value]; wasm store wants addr below value -> stage the value */
    e(w, W_LOCAL_SET); e_u(w, LOC_IB);
    e_addr(w, kind, slot);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_LOCAL_GET); e_u(w, LOC_IB);
    e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
}
static void e_store_payload_f64_from_stack(Buf *w, int kind, int slot) {
    e(w, W_I64_REINTERPRET_F64);
    e_store_payload_i64_from_stack(w, kind, slot);
}
static void e_store_int_const(Buf *w, int kind, int slot, long long v) {
    e_store_tag(w, kind, slot, TAG_INT);
    e_i64c(w, v);
    e_store_payload_i64_from_stack(w, kind, slot);
}
static void e_store_float_const(Buf *w, int kind, int slot, double v) {
    e_store_tag(w, kind, slot, TAG_FLOAT);
    e_f64c(w, v);
    e(w, W_I64_REINTERPRET_F64);
    e_store_payload_i64_from_stack(w, kind, slot);
}
static void e_store_bool_const(Buf *w, int kind, int slot, int v) {
    e_store_tag(w, kind, slot, TAG_BOOL);
    e_i64c(w, v ? 1 : 0);
    e_store_payload_i64_from_stack(w, kind, slot);
}
/* store BOOL from an i32 on the stack */
static void e_store_bool_from_stack(Buf *w, int kind, int slot) {
    e_store_tag(w, kind, slot, TAG_BOOL);
    e(w, W_I64_EXTEND_I32_S);
    e_store_payload_i64_from_stack(w, kind, slot);
}

static void e_load_val(Buf *w, int which, int kind, int slot) {
    int tloc = which ? LOC_TAGB : LOC_TAGA;
    int iloc = which ? LOC_IB : LOC_IA;
    e_addr(w, kind, slot);
    e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);
    e(w, W_LOCAL_SET); e_u(w, tloc);
    e_addr(w, kind, slot);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
    e(w, W_LOCAL_SET); e_u(w, iloc);
}

/* push f64 interpretation of the loaded value (val_as_double) */
static void e_push_as_double(Buf *w, int which) {
    int tloc = which ? LOC_TAGB : LOC_TAGA;
    int iloc = which ? LOC_IB : LOC_IA;
    e(w, W_LOCAL_GET); e_u(w, tloc);
    e(w, W_IF); e_u(w, 0x7C);                        /* f64 result; not NIL */
    e(w, W_LOCAL_GET); e_u(w, tloc);
    e_i32c(w, TAG_INT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7C);
    e(w, W_LOCAL_GET); e_u(w, iloc);
    e(w, W_F64_CONVERT_I64_S);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, tloc);
    e_i32c(w, TAG_FLOAT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7C);
    e(w, W_LOCAL_GET); e_u(w, iloc);
    e(w, W_F64_REINTERPRET_I64);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, tloc);
    e_i32c(w, TAG_BOOL);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7C);
    e(w, W_LOCAL_GET); e_u(w, iloc);                 /* BOOL payload is 0/1 */
    e(w, W_F64_CONVERT_I64_S);
    e(w, W_ELSE);                                    /* STR/ARR: refuse loudly */
    e_i32c(w, ERR_ARRAY_OP);
    e(w, W_CALL); e_u(w, IMP_ERROR);
    e(w, W_UNREACHABLE);
    e(w, W_END);
    e(w, W_END);
    e(w, W_END);
    e(w, W_ELSE);
    e_f64c(w, 0.0);                                  /* NIL */
    e(w, W_END);
}

static void e_trap_on_string(Buf *w) {
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_STR);
    e(w, W_I32_EQ);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
    e_i32c(w, TAG_STR);
    e(w, W_I32_EQ);
    e(w, W_I32_OR);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_UNREACHABLE);
    e(w, W_END);
}

static void e_copy_val(Buf *w, int sk, int ss, int dk, int ds) {
    if (sk == dk && ss == ds) return;      /* self copy: nothing to move */
    if (is_owned_slot(dk, ds)) {
        e_ref_slot(w, sk, ss, HFN_RETAIN);  /* keep the source alive first */
        e_ref_slot(w, dk, ds, HFN_RELEASE);
    }
    /* wasm stores take [addr, value] (value on top): push dst first, then
       src, and let the load place the value above the dst address */
    e_addr(w, dk, ds);
    e_addr(w, sk, ss);
    e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
    e_addr(w, dk, ds);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e_addr(w, sk, ss);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
    e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
}

/* ---------- raw i32 staging in frame temporaries ------------------------
   Scratch stays in memory (not in a wasm local) so that a nested array
   literal or index expression can never clobber an outer one.             */
static void e_push_slot_i32(Buf *w, int slot) {
    e_addr(w, KIND_FRAME, slot);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
    e(w, W_I32_WRAP_I64);
}
/* store the i32 currently on the stack into a frame temporary */
static void e_pop_slot_i32(Buf *w, int slot) {
    e(w, W_I64_EXTEND_I32_S);
    e_store_payload_i64_from_stack(w, KIND_FRAME, slot);
}

/* trap unless the value in the LOC_TAGA pair has the given tag */
static void e_expect_tag_a(Buf *w, int tag, int err) {
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, tag);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_ELSE);
    e_i32c(w, err);
    e(w, W_CALL); e_u(w, IMP_ERROR);
    e(w, W_UNREACHABLE);
    e(w, W_END);
}

/* ---------- name resolution ---------- */
static Named *global_lookup(Cg *cg, const char *name) {
    for (int i = 0; i < cg->global_count; i++)
        if (strcmp(cg->globals[i].name, name) == 0) return &cg->globals[i];
    return NULL;
}
static Named *local_lookup(FnEnv *env, const char *name) {
    for (int i = 0; i < env->local_count; i++)
        if (strcmp(env->locals[i].name, name) == 0) return &env->locals[i];
    return NULL;
}
static int is_gdecl(FnEnv *env, const char *name) {
    for (int i = 0; i < env->gdecl_count; i++)
        if (strcmp(env->gdecls[i], name) == 0) return 1;
    return 0;
}
static int new_global(Cg *cg, FnEnv *env, const char *name, int *slot) {
    if (cg->global_count >= 512) { fail(cg, env, "too many globals (max 512)"); return -1; }
    cg->globals[cg->global_count].name = strdup(name);
    cg->globals[cg->global_count].slot = cg->global_count;
    *slot = cg->global_count++;
    return 0;
}
static int resolve_var(Cg *cg, FnEnv *env, const char *name, int *kind, int *slot) {
    if (env->in_function) {
        Named *l = local_lookup(env, name);
        if (l && !is_gdecl(env, name)) { *kind = KIND_FRAME; *slot = l->slot; return 0; }
    }
    Named *g = global_lookup(cg, name);
    if (!g) {
        if (new_global(cg, env, name, slot) != 0) return -1;
        *kind = KIND_GLOBAL;
        return 0;
    }
    *kind = KIND_GLOBAL; *slot = g->slot;
    return 0;
}
static int resolve_read(Cg *cg, FnEnv *env, const char *name, int *kind, int *slot) {
    if (env->in_function) {
        Named *l = local_lookup(env, name);
        if (l && !is_gdecl(env, name)) { *kind = KIND_FRAME; *slot = l->slot; return 0; }
    }
    Named *g = global_lookup(cg, name);
    if (!g) {
        fail(cg, env, "variable '%s' used before assignment", name);
        return -1;
    }
    *kind = KIND_GLOBAL; *slot = g->slot;
    return 0;
}

/* ---------- codegen ---------- */
static void cg_expr(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth);
static void cg_stmt(Cg *cg, FnEnv *env, Buf *w, Stmt *s);
static void e_epilogue_release(Buf *w, FnEnv *env);

/* ---------- arrays (heap-backed, docs/WASM.md "Arrays") ------------------
   An array value is TAG_ARR with the block pointer as its i64 payload; the
   block itself is [len i32 @+0][12 bytes pad][element slots 16B each @+16].
   Element slots are ordinary boxed value slots, so an element may itself be
   an array.  Out-of-range reads yield nil, matching the interpreter;
   out-of-range writes are REFUSED with im_error(ERR_ARRAY_INDEX) because the
   interpreter silently grows the array and a silent wasm-side shrink/grow
   would be a silent divergence.  The block header keeps the element count so
   release() can recursively drop element references.                       */

/* element address base + constant index (i, compile time) */
static void e_addr_elem_k(Buf *w, int base_slot, int i) {
    e_push_slot_i32(w, base_slot);
    e_i32c(w, (long long)i * SLOT_BYTES);
    e(w, W_I32_ADD);
}
/* element address base + runtime index held in LOC_IDX */
static void e_addr_elem_dyn(Buf *w, int base_slot) {
    e_push_slot_i32(w, base_slot);
    e(w, W_LOCAL_GET); e_u(w, LOC_IDX);
    e_i32c(w, 4);
    e(w, W_I32_SHL);
    e(w, W_I32_ADD);
}

/* store LOC_TAGA/LOC_IA into the constant element index i of base slot */
static void e_store_elem_k(Buf *w, int base_slot, int i) {
    e_addr_elem_k(w, base_slot, i);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
    e_addr_elem_k(w, base_slot, i);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
}

/* new array literal; dst may be an owning slot or a temporary */
static void cg_array_literal(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth) {
    int n = x->list.count;
    int t_ptr = TEMP_BASE + depth + 1, t_el = TEMP_BASE + depth + 2;
    if (n > ARRAY_MAX_ELEMS) {
        fail(cg, env, "array literal too large (%d elements, heap allows %d)", n, (int)ARRAY_MAX_ELEMS);
        return;
    }
    /* allocate: header + n element slots; fresh blocks start with refs == 0.
       Payload layout: [element 0][element 1]... with the count in the header
       at payload-4. */
    e_i32c(w, HEAP_HEADER + (long long)n * SLOT_BYTES);
    e_call_helper(w, HFN_ALLOC);
    e_pop_slot_i32(w, t_ptr);
    /* element count in the block header (payload-4); it is also the length
       len() reports, so elements start at payload+0 and no slot is wasted */
    e_push_slot_i32(w, t_ptr);
    e_i32c(w, -4); e(w, W_I32_ADD);
    e_i32c(w, n);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
    /* evaluate and store the elements left to right */
    for (int i = 0; i < n; i++) {
        cg_expr(cg, env, w, x->list.items[i], KIND_FRAME, t_el, depth + 3);
        if (cg->err[0]) return;
        e_load_val(w, 0, KIND_FRAME, t_el);
        e_ret_loaded(w);                     /* the element slot owns a reference */
        e_store_elem_k(w, t_ptr, i);
    }
    /* publish into dst: release the old value, then take one reference */
    if (is_owned_slot(dst_kind, dst_slot)) e_ref_slot(w, dst_kind, dst_slot, HFN_RELEASE);
    e_addr(w, dst_kind, dst_slot);
    e_i32c(w, TAG_ARR);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
    e_addr(w, dst_kind, dst_slot);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e_push_slot_i32(w, t_ptr);
    e(w, W_I64_EXTEND_I32_S);
    e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
    if (is_owned_slot(dst_kind, dst_slot)) e_ref_slot(w, dst_kind, dst_slot, HFN_RETAIN);
}

/* resolve the ident an index expression is rooted at; 0 on success */
/* Evaluate the base of a[i] and leave the raw array pointer in the local i32
   temp t_ptr.  Any expression may be a base: the interpreter allows
   say g()[1], so make() [1] etc. are accepted.  Returns -1 after fail(). */
static int cg_index_base_ptr(Cg *cg, FnEnv *env, Buf *w, Expr *x, int t_ptr, int depth) {
    if (x->index.object->type == EXPR_IDENT) {
        char name[256];
        int kind, slot;
        snprintf(name, sizeof(name), "%.*s", (int)x->index.object->identName.length,
                 x->index.object->identName.start);
        if (resolve_read(cg, env, name, &kind, &slot) != 0) return -1;
        e_load_val(w, 0, kind, slot);
    } else {
        cg_expr(cg, env, w, x->index.object, KIND_FRAME, t_ptr, depth);
        if (cg->err[0]) return -1;
        e_load_val(w, 0, KIND_FRAME, t_ptr);
    }
    e_expect_tag_a(w, TAG_ARR, ERR_ARRAY_OP);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA); e(w, W_I32_WRAP_I64);
    e_pop_slot_i32(w, t_ptr);
    return 0;
}

/* encode the int/float value in the B pair as an i32 index in LOC_IDX */
static void e_index_to_i32(Buf *w) {
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
    e_i32c(w, TAG_INT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7F);
    e(w, W_LOCAL_GET); e_u(w, LOC_IB);
    e(w, W_I32_WRAP_I64);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
    e_i32c(w, TAG_FLOAT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7F);
    e(w, W_LOCAL_GET); e_u(w, LOC_IB);
    e(w, W_F64_REINTERPRET_I64);
    e_trunc_sat_i32(w);
    e(w, W_ELSE);
    e_i32c(w, ERR_ARRAY_OP);
    e(w, W_CALL); e_u(w, IMP_ERROR);
    e(w, W_UNREACHABLE);
    e(w, W_END);
    e(w, W_END);
    e(w, W_LOCAL_SET); e_u(w, LOC_IDX);
}

/* push (idx < 0 || idx >= len) for the array whose pointer is in slot */
static void e_oob_test(Buf *w, int base_slot) {
    e(w, W_LOCAL_GET); e_u(w, LOC_IDX);
    e_i32c(w, 0);
    e(w, W_I32_LT_S);
    e(w, W_LOCAL_GET); e_u(w, LOC_IDX);
    e_push_slot_i32(w, base_slot);
    e_i32c(w, -4); e(w, W_I32_ADD);
    e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);      /* element count lives in the header */
    e(w, W_I32_GE_S);
    e(w, W_I32_OR);
}

/* zero a destination slot (nil) */
static void e_store_nil(Buf *w, int kind, int slot) {
    e_addr(w, kind, slot);
    e_i32c(w, TAG_NIL);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
    e_addr(w, kind, slot);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e_i64c(w, 0);
    e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
}

/* a[i] as a value; a[i] must be readable (out of range yields nil) */
static void cg_index_read(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth) {
    int t_ptr = TEMP_BASE + depth + 1, t_idx = TEMP_BASE + depth + 2, t_el = TEMP_BASE + depth + 3;
    if (cg_index_base_ptr(cg, env, w, x, t_ptr, depth + 6) != 0) return;
    cg_expr(cg, env, w, x->index.index, KIND_FRAME, t_idx, depth + 3);
    if (cg->err[0]) return;
    e_load_val(w, 1, KIND_FRAME, t_idx);
    e_index_to_i32(w);
    int owned = is_owned_slot(dst_kind, dst_slot);
    e_oob_test(w, t_ptr);
    e(w, W_IF); e_u(w, 0x40);
    {
        if (owned) e_ref_slot(w, dst_kind, dst_slot, HFN_RELEASE);
        e_store_nil(w, dst_kind, dst_slot);
    }
    e(w, W_ELSE);
    {
        e_addr_elem_dyn(w, t_ptr);
        e_pop_slot_i32(w, t_el);
        e_push_slot_i32(w, t_el);
        e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);
        e(w, W_LOCAL_SET); e_u(w, LOC_TAGA);
        e_push_slot_i32(w, t_el);
        e_i32c(w, 8); e(w, W_I32_ADD);
        e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
        e(w, W_LOCAL_SET); e_u(w, LOC_IA);
        if (owned) {
            e_ret_loaded(w);
            e_ref_slot(w, dst_kind, dst_slot, HFN_RELEASE);
        }
        e_addr(w, dst_kind, dst_slot);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e_store_payload_i64_from_stack(w, dst_kind, dst_slot);
    }
    e(w, W_END);
}

/* a[i] = value; out of range is refused (the interpreter grows instead) */
static void cg_index_write(Cg *cg, FnEnv *env, Buf *w, Expr *x, Expr *value, int depth) {
    int t_ptr = TEMP_BASE + depth + 1, t_idx = TEMP_BASE + depth + 2;
    int t_el = TEMP_BASE + depth + 3, t_val = TEMP_BASE + depth + 4;
    if (cg_index_base_ptr(cg, env, w, x, t_ptr, depth + 6) != 0) return;
    /* Pin the base block: evaluating the value may reassign the variable the
       base came from (a[0] = (a = [9])), which would free the block we are
       about to write into.  Released again below, after the store. */
    e_push_slot_i32(w, t_ptr);
    e_call_helper(w, HFN_RETAIN);
    cg_expr(cg, env, w, x->index.index, KIND_FRAME, t_idx, depth + 3);
    if (cg->err[0]) return;
    e_load_val(w, 1, KIND_FRAME, t_idx);
    e_index_to_i32(w);
    e_oob_test(w, t_ptr);
    e(w, W_IF); e_u(w, 0x40);
    e_i32c(w, ERR_ARRAY_INDEX);
    e(w, W_CALL); e_u(w, IMP_ERROR);
    e(w, W_UNREACHABLE);
    e(w, W_END);
    /* element address survives arbitrary nested evaluation: it is in memory */
    e_addr_elem_dyn(w, t_ptr);
    e_pop_slot_i32(w, t_el);
    cg_expr(cg, env, w, value, KIND_FRAME, t_val, depth + 5);
    if (cg->err[0]) return;
    e_load_val(w, 0, KIND_FRAME, t_val);
    e_ret_loaded(w);                         /* the element slot owns a reference */
    /* release whatever the element held before */
    e_push_slot_i32(w, t_el);
    e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);
    e_i32c(w, TAG_ARR);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e_push_slot_i32(w, t_el);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
    e(w, W_I32_WRAP_I64);
    e_call_helper(w, HFN_RELEASE);
    e(w, W_END);
    /* store */
    e_push_slot_i32(w, t_el);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
    e_push_slot_i32(w, t_el);
    e_i32c(w, 8); e(w, W_I32_ADD);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
    e_push_slot_i32(w, t_ptr);
    e_call_helper(w, HFN_RELEASE);
}

/* len(a) - the only collection builtin in the wasm subset */
static void cg_len_builtin(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth) {
    if (x->call.argCount != 1) { fail(cg, env, "len() takes exactly one argument"); return; }
    cg_expr(cg, env, w, x->call.args[0], KIND_FRAME, TEMP_BASE + depth, depth + 1);
    if (cg->err[0]) return;
    e_load_val(w, 0, KIND_FRAME, TEMP_BASE + depth);
    e_expect_tag_a(w, TAG_ARR, ERR_ARRAY_OP);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA); e(w, W_I32_WRAP_I64);
    e_i32c(w, -4); e(w, W_I32_ADD);
    e(w, W_I32_LOAD); e_u(w, 2); e_u(w, 0);      /* length lives in the header; read before releasing dst */
    e(w, W_I64_EXTEND_I32_S);
    e(w, W_LOCAL_SET); e_u(w, LOC_IB);
    e_store_tag(w, dst_kind, dst_slot, TAG_INT);
    e(w, W_LOCAL_GET); e_u(w, LOC_IB);
    e_store_payload_i64_from_stack(w, dst_kind, dst_slot);
}

/* emit truthiness of expr as i32 on the stack (short-circuit and/or) */
static void cg_cond(Cg *cg, FnEnv *env, Buf *w, Expr *x, int depth) {
    if (cg->err[0]) return;
    if (x->type == EXPR_BINARY && x->binary.op == TOK_AND) {
        cg_cond(cg, env, w, x->binary.left, depth);
        e(w, W_IF); e_u(w, 0x7F);
        cg_cond(cg, env, w, x->binary.right, depth);
        e(w, W_ELSE);
        e_i32c(w, 0);
        e(w, W_END);
        return;
    }
    if (x->type == EXPR_BINARY && x->binary.op == TOK_OR) {
        cg_cond(cg, env, w, x->binary.left, depth);
        e(w, W_IF); e_u(w, 0x7F);
        e_i32c(w, 1);
        e(w, W_ELSE);
        cg_cond(cg, env, w, x->binary.right, depth);
        e(w, W_END);
        return;
    }
    if (x->type == EXPR_UNARY && x->unary.op == TOK_NOT) {
        cg_cond(cg, env, w, x->unary.operand, depth);
        e(w, W_I32_EQZ);
        return;
    }
    if (depth >= TEMP_MAX) { fail(cg, env, "expression too deep (max %d)", TEMP_MAX); return; }
    cg_expr(cg, env, w, x, KIND_FRAME, TEMP_BASE + depth, depth + 1);
    if (cg->err[0]) return;
    e_load_val(w, 0, KIND_FRAME, TEMP_BASE + depth);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e(w, W_IF); e_u(w, 0x7F);                        /* tag != NIL */
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_INT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7F);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e_i64c(w, 0);
    e(w, W_I64_NE);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);             /* BOOL branch */
    e_i32c(w, TAG_BOOL);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7F);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e_i64c(w, 0);
    e(w, W_I64_NE);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_FLOAT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7F);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e(w, W_F64_REINTERPRET_I64);
    e_f64c(w, 0.0);
    e(w, W_F64_EQ);
    e(w, W_I32_EQZ);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_NIL);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x7F);
    e_i32c(w, 0);
    e(w, W_ELSE);
    e_i32c(w, 1);
    e(w, W_END);
    e(w, W_END);
    e(w, W_END);
    e(w, W_END);
    e(w, W_ELSE);
    e_i32c(w, 0);                                    /* NIL -> false */
    e(w, W_END);
}

static void cg_arith(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth, InimerseTokenType op) {
    if (depth + 1 >= TEMP_MAX) { fail(cg, env, "expression too deep"); return; }
    cg_expr(cg, env, w, x->binary.left, KIND_FRAME, TEMP_BASE + depth, depth + 1);
    if (cg->err[0]) return;
    cg_expr(cg, env, w, x->binary.right, KIND_FRAME, TEMP_BASE + depth + 1, depth + 2);
    if (cg->err[0]) return;
    e_load_val(w, 0, KIND_FRAME, TEMP_BASE + depth);
    e_load_val(w, 1, KIND_FRAME, TEMP_BASE + depth + 1);
    e_trap_on_string(w);

    if (op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR) {
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_I32_AND);
        e(w, W_IF); e_u(w, 0x40);                    /* int x int */
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e(w, W_LOCAL_GET); e_u(w, LOC_IB);
        if (op == TOK_PLUS) e(w, W_I64_ADD);
        else if (op == TOK_MINUS) e(w, W_I64_SUB);
        else e(w, W_I64_MUL);
        e(w, W_LOCAL_SET); e_u(w, LOC_IA);
        /* int32 overflow -> float of the exact i64 result */
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e_i64c(w, 2147483647);
        e(w, W_I64_GT_S);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e_i64c(w, -2147483648LL);
        e(w, W_I64_LT_S);
        e(w, W_I32_OR);
        e(w, W_IF); e_u(w, 0x40);
        e_store_tag(w, dst_kind, dst_slot, TAG_FLOAT);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e(w, W_F64_CONVERT_I64_S);
        e_store_payload_f64_from_stack(w, dst_kind, dst_slot);
        e(w, W_ELSE);
        e_store_tag(w, dst_kind, dst_slot, TAG_INT);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e_store_payload_i64_from_stack(w, dst_kind, dst_slot);
        e(w, W_END);
        e(w, W_ELSE);                                /* mixed/non-int: double path */
        e_push_as_double(w, 0);
        e_push_as_double(w, 1);
        if (op == TOK_PLUS) e(w, W_F64_ADD);
        else if (op == TOK_MINUS) e(w, W_F64_SUB);
        else e(w, W_F64_MUL);
        e_store_tag(w, dst_kind, dst_slot, TAG_FLOAT);
        e_store_payload_f64_from_stack(w, dst_kind, dst_slot);
        e(w, W_END);
        return;
    }
    if (op == TOK_SLASH) {
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_I32_AND);
        e(w, W_LOCAL_GET); e_u(w, LOC_IB);
        e_i64c(w, 0);
        e(w, W_I64_EQ);
        e(w, W_I32_AND);
        e(w, W_IF); e_u(w, 0x40);
        e_i32c(w, 1);                                /* error 1: division_by_zero */
        e(w, W_CALL); e_u(w, IMP_ERROR);
        e(w, W_UNREACHABLE);
        e(w, W_END);
        e_push_as_double(w, 0);
        e_push_as_double(w, 1);
        e(w, W_F64_DIV);
        e_store_tag(w, dst_kind, dst_slot, TAG_FLOAT);
        e_store_payload_f64_from_stack(w, dst_kind, dst_slot);
        return;
    }
    if (op == TOK_PERCENT) {
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_I32_AND);
        e(w, W_IF); e_u(w, 0x40);                    /* int % int */
        e(w, W_LOCAL_GET); e_u(w, LOC_IB);
        e_i64c(w, 0);
        e(w, W_I64_EQ);
        e(w, W_IF); e_u(w, 0x40);
        e_i32c(w, 1);
        e(w, W_CALL); e_u(w, IMP_ERROR);
        e(w, W_UNREACHABLE);
        e(w, W_END);
        e_store_tag(w, dst_kind, dst_slot, TAG_INT);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e(w, W_LOCAL_GET); e_u(w, LOC_IB);
        e(w, W_I64_REM_S);
        e_store_payload_i64_from_stack(w, dst_kind, dst_slot);
        e(w, W_ELSE);                                /* general: (int)da % (int)db */
        e_push_as_double(w, 0);
        e_trunc_sat_i32(w);
        e(w, W_I64_EXTEND_I32_S);
        e(w, W_LOCAL_SET); e_u(w, LOC_IA);
        e_push_as_double(w, 1);
        e_trunc_sat_i32(w);
        e(w, W_LOCAL_SET); e_u(w, LOC_RES);          /* RES: i32 (int)db */
        e(w, W_LOCAL_GET); e_u(w, LOC_RES);
        e_i32c(w, 0);
        e(w, W_I32_EQ);
        e(w, W_IF); e_u(w, 0x40);
        e_i32c(w, 1);
        e(w, W_CALL); e_u(w, IMP_ERROR);
        e(w, W_UNREACHABLE);
        e(w, W_END);
        e_store_tag(w, dst_kind, dst_slot, TAG_INT);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e(w, W_LOCAL_GET); e_u(w, LOC_RES);
        e(w, W_I64_EXTEND_I32_S);
        e(w, W_I64_REM_S);
        e_store_payload_i64_from_stack(w, dst_kind, dst_slot);
        e(w, W_END);
        return;
    }
    fail(cg, env, "operator not supported");
}

/* compare the already-loaded values (LOC_TAGA/IA vs LOC_TAGB/IB) and store
   the BOOL result; val_eq / val_cmp semantics */
static void cg_compare_loaded(Cg *cg, FnEnv *env, Buf *w, int dst_kind, int dst_slot, InimerseTokenType op) {
    if (op == TOK_EQEQ || op == TOK_NEQ) {
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
        e(w, W_I32_EQ);
        e(w, W_IF); e_u(w, 0x7F);                    /* same tag */
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_FLOAT);
        e(w, W_I32_EQ);
        e(w, W_IF); e_u(w, 0x7F);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);
        e(w, W_F64_REINTERPRET_I64);
        e(w, W_LOCAL_GET); e_u(w, LOC_IB);
        e(w, W_F64_REINTERPRET_I64);
        e(w, W_F64_EQ);
        e(w, W_ELSE);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_NIL);
        e(w, W_I32_EQ);
        e(w, W_IF); e_u(w, 0x7F);
        e_i32c(w, 1);                                /* nil == nil */
        e(w, W_ELSE);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_STR);
        e(w, W_I32_EQ);
        e(w, W_IF); e_u(w, 0x40);
        e(w, W_UNREACHABLE);                         /* string equality unsupported */
        e(w, W_END);
        e(w, W_LOCAL_GET); e_u(w, LOC_IA);           /* int/bool payload compare */
        e(w, W_LOCAL_GET); e_u(w, LOC_IB);
        e(w, W_I64_EQ);
        e(w, W_END);
        e(w, W_END);
        e(w, W_ELSE);                                /* different tags: numeric cross-compare */
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
        e_i32c(w, TAG_FLOAT);
        e(w, W_I32_EQ);
        e(w, W_I32_OR);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
        e_i32c(w, TAG_INT);
        e(w, W_I32_EQ);
        e(w, W_LOCAL_GET); e_u(w, LOC_TAGB);
        e_i32c(w, TAG_FLOAT);
        e(w, W_I32_EQ);
        e(w, W_I32_OR);
        e(w, W_I32_AND);
        e(w, W_IF); e_u(w, 0x7F);
        e_push_as_double(w, 0);
        e_push_as_double(w, 1);
        e(w, W_F64_EQ);
        e(w, W_ELSE);
        e_i32c(w, 0);
        e(w, W_END);                                 /* cross-compare if */
        e(w, W_END);                                 /* outer same-tag if */
        if (op == TOK_NEQ) e(w, W_I32_EQZ);
        e_store_bool_from_stack(w, dst_kind, dst_slot);
        return;
    }
    e_push_as_double(w, 0);
    e_push_as_double(w, 1);
    if (op == TOK_LT) e(w, W_F64_LT);
    else if (op == TOK_GT) e(w, W_F64_GT);
    else if (op == TOK_LE) e(w, W_F64_LE);
    else e(w, W_F64_GE);
    e_store_bool_from_stack(w, dst_kind, dst_slot);
}

/* core comparison of two operand expressions */
static void cg_compare_core(Cg *cg, FnEnv *env, Buf *w, Expr *lhs, Expr *rhs, int dst_kind, int dst_slot, int depth, InimerseTokenType op) {
    if (depth + 1 >= TEMP_MAX) { fail(cg, env, "expression too deep"); return; }
    cg_expr(cg, env, w, lhs, KIND_FRAME, TEMP_BASE + depth, depth + 1);
    if (cg->err[0]) return;
    cg_expr(cg, env, w, rhs, KIND_FRAME, TEMP_BASE + depth + 1, depth + 2);
    if (cg->err[0]) return;
    e_load_val(w, 0, KIND_FRAME, TEMP_BASE + depth);
    e_load_val(w, 1, KIND_FRAME, TEMP_BASE + depth + 1);
    e_trap_on_string(w);
    cg_compare_loaded(cg, env, w, dst_kind, dst_slot, op);
}

/* chained comparison a op1 b op2 c — the parser emits EXPR_CHAIN_COMPARE
   (chain.count = OPERAND count, ops has count-1 entries) even for single
   comparisons.  Matches the interpreter: AND of steps, short-circuit (later
   operands are not evaluated once a step is false). */
static void cg_chain_compare(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth) {
    int nops = x->chain.count - 1;
    if (nops < 1) { fail(cg, env, "empty comparison chain"); return; }
    if (depth + 2 >= TEMP_MAX) { fail(cg, env, "expression too deep"); return; }
    cg_expr(cg, env, w, x->chain.operands[0], KIND_FRAME, TEMP_BASE + depth, depth + 1);
    if (cg->err[0]) return;
    e_i32c(w, 1);
    e(w, W_LOCAL_SET); e_u(w, LOC_RES);
    for (int k = 0; k < nops; k++) {
        /* short-circuit: skip the rest once a step was false */
        e(w, W_LOCAL_GET); e_u(w, LOC_RES);
        e(w, W_IF); e_u(w, 0x40);
        cg_expr(cg, env, w, x->chain.operands[k + 1], KIND_FRAME, TEMP_BASE + depth + 1, depth + 2);
        if (cg->err[0]) return;
        e_load_val(w, 0, KIND_FRAME, TEMP_BASE + depth);
        e_load_val(w, 1, KIND_FRAME, TEMP_BASE + depth + 1);
        e_trap_on_string(w);
        cg_compare_loaded(cg, env, w, KIND_FRAME, TEMP_BASE + depth + 2, x->chain.ops[k]);
        /* RES = RES && payload(step bool) */
        e_addr(w, KIND_FRAME, TEMP_BASE + depth + 2);
        e_i32c(w, 8); e(w, W_I32_ADD);
        e(w, W_I64_LOAD); e_u(w, 3); e_u(w, 0);
        e(w, W_I32_WRAP_I64);
        e(w, W_LOCAL_GET); e_u(w, LOC_RES);
        e(w, W_I32_AND);
        e(w, W_LOCAL_SET); e_u(w, LOC_RES);
        /* carry: running left = this right */
        e_copy_val(w, KIND_FRAME, TEMP_BASE + depth + 1, KIND_FRAME, TEMP_BASE + depth);
        e(w, W_END);
    }
    e(w, W_LOCAL_GET); e_u(w, LOC_RES);
    e_store_bool_from_stack(w, dst_kind, dst_slot);
}

static int find_func(Cg *cg, const char *name) {
    for (int i = 0; i < cg->func_count; i++)
        if (strcmp(cg->funcs[i].name, name) == 0) return i;
    return -1;
}

static void cg_call(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth) {
    char name[256];
    snprintf(name, sizeof(name), "%.*s", (int)x->call.callee->identName.length, x->call.callee->identName.start);
    int fi = find_func(cg, name);
    if (fi < 0) {
        if (strcmp(name, "len") == 0) {              /* the one collection builtin */
            cg_len_builtin(cg, env, w, x, dst_kind, dst_slot, depth);
            return;
        }
        fail(cg, env, "function '%s' not found (builtins are not in the wasm MVP subset)", name);
        return;
    }
    int params = cg->funcs[fi].params;
    if (x->call.argCount > params) {
        fail(cg, env, "too many arguments for '%s' (max %d)", name, params);
        return;
    }
    /* callee frame: $fbase = $sp; $sp += FRAME_BYTES */
    e(w, W_GLOBAL_GET); e_u(w, GIDX_SP);
    e(w, W_LOCAL_SET); e_u(w, LOC_FBASE);
    e(w, W_GLOBAL_GET); e_u(w, GIDX_SP);
    e_i32c(w, FRAME_BYTES);
    e(w, W_I32_ADD);
    e(w, W_GLOBAL_SET); e_u(w, GIDX_SP);
    e(w, W_GLOBAL_GET); e_u(w, GIDX_SP);
    e_i32c(w, GLOBALS_BASE);
    e(w, W_I32_GT_S);
    e(w, W_IF); e_u(w, 0x40);
    e_i32c(w, 3);                                    /* error 3: stack overflow */
    e(w, W_CALL); e_u(w, IMP_ERROR);
    e(w, W_UNREACHABLE);
    e(w, W_END);
    /* nil-fill param slots, then evaluate args into them */
    for (int i = 0; i < params; i++) {
        e_addr(w, KIND_CALLEE, 1 + i);
        e_i32c(w, TAG_NIL);
        e(w, W_I32_STORE); e_u(w, 2); e_u(w, 0);
        e_addr(w, KIND_CALLEE, 1 + i);
        e_i32c(w, 8); e(w, W_I32_ADD);
        e_i64c(w, 0);
        e(w, W_I64_STORE); e_u(w, 3); e_u(w, 0);
    }
    for (int i = 0; i < x->call.argCount; i++) {
        cg_expr(cg, env, w, x->call.args[i], KIND_CALLEE, 1 + i, depth + 1);
        if (cg->err[0]) return;
    }
    e(w, W_LOCAL_GET); e_u(w, LOC_FBASE);
    e(w, W_CALL); e_u(w, (unsigned long)(FUNC_MAIN + 1 + fi));
    /* copy return value (callee slot 0) to dst, then free the frame */
    e_copy_val(w, KIND_CALLEE, 0, dst_kind, dst_slot);
    /* the caller owns the argument references and the returned one.  A
       borrowed temporary cannot be released (its lifetime is not tracked),
       so in that case the reference stays with the temporary - bounded and
       documented in docs/WASM.md. */
    if (is_owned_slot(dst_kind, dst_slot)) e_ref_slot(w, KIND_CALLEE, 0, HFN_RELEASE);
    for (int i = 0; i < params; i++) e_ref_slot(w, KIND_CALLEE, 1 + i, HFN_RELEASE);
    e(w, W_GLOBAL_GET); e_u(w, GIDX_SP);
    e_i32c(w, FRAME_BYTES);
    e(w, W_I32_SUB);
    e(w, W_GLOBAL_SET); e_u(w, GIDX_SP);
}

static void cg_expr(Cg *cg, FnEnv *env, Buf *w, Expr *x, int dst_kind, int dst_slot, int depth) {
    if (cg->err[0]) return;
    if (depth >= TEMP_MAX) { fail(cg, env, "expression too deep (max %d)", TEMP_MAX); return; }
    switch (x->type) {
        case EXPR_NUMBER: {
            long long v = x->intVal;
            if (v > 2147483647LL || v < -2147483648LL)
                e_store_float_const(w, dst_kind, dst_slot, (double)v);
            else
                e_store_int_const(w, dst_kind, dst_slot, v);
            return;
        }
        case EXPR_FLOAT:
            e_store_float_const(w, dst_kind, dst_slot, x->floatVal);
            return;
        case EXPR_BOOL:
            e_store_bool_const(w, dst_kind, dst_slot, x->boolVal ? 1 : 0);
            return;
        case EXPR_IDENT: {
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)x->identName.length, x->identName.start);
            int kind, slot;
            if (resolve_read(cg, env, name, &kind, &slot) != 0) return;
            e_copy_val(w, kind, slot, dst_kind, dst_slot);
            return;
        }
        case EXPR_UNARY: {
            if (x->unary.op == TOK_MINUS) {
                cg_expr(cg, env, w, x->unary.operand, KIND_FRAME, TEMP_BASE + depth, depth + 1);
                if (cg->err[0]) return;
                e_load_val(w, 0, KIND_FRAME, TEMP_BASE + depth);
                e_trap_on_string(w);
                e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
                e_i32c(w, TAG_INT);
                e(w, W_I32_EQ);
                e(w, W_IF); e_u(w, 0x40);
                e(w, W_LOCAL_GET); e_u(w, LOC_IA);
                e_i64c(w, -2147483648LL);
                e(w, W_I64_EQ);
                e(w, W_IF); e_u(w, 0x40);            /* INT_MIN -> 2147483648.0 */
                e_store_float_const(w, dst_kind, dst_slot, 2147483648.0);
                e(w, W_ELSE);
                e_store_tag(w, dst_kind, dst_slot, TAG_INT);
                e(w, W_LOCAL_GET); e_u(w, LOC_IA);
                e_i64c(w, -1);
                e(w, W_I64_MUL);
                e_store_payload_i64_from_stack(w, dst_kind, dst_slot);
                e(w, W_END);
                e(w, W_ELSE);
                e_push_as_double(w, 0);
                e(w, W_F64_NEG);
                e_store_tag(w, dst_kind, dst_slot, TAG_FLOAT);
                e_store_payload_f64_from_stack(w, dst_kind, dst_slot);
                e(w, W_END);
                return;
            }
            if (x->unary.op == TOK_NOT) {
                cg_cond(cg, env, w, x, depth);
                if (cg->err[0]) return;
                e(w, W_I32_EQZ);
                e_store_bool_from_stack(w, dst_kind, dst_slot);
                return;
            }
            fail(cg, env, "unary operator not supported");
            return;
        }
        case EXPR_BINARY: {
            InimerseTokenType op = x->binary.op;
            if (op == TOK_PLUS || op == TOK_MINUS || op == TOK_STAR || op == TOK_SLASH || op == TOK_PERCENT) {
                cg_arith(cg, env, w, x, dst_kind, dst_slot, depth, op);
                return;
            }
            if (op == TOK_EQEQ || op == TOK_NEQ || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE) {
                cg_compare_core(cg, env, w, x->binary.left, x->binary.right, dst_kind, dst_slot, depth, op);
                return;
            }
            if (op == TOK_AND || op == TOK_OR) {
                fail(cg, env, "'and'/'or' outside a condition is not supported (use it in if/while)");
                return;
            }
            fail(cg, env, "operator not supported by wasm MVP subset");
            return;
        }
        case EXPR_CHAIN_COMPARE: {
            cg_chain_compare(cg, env, w, x, dst_kind, dst_slot, depth);
            return;
        }
        case EXPR_LIST: {
            cg_array_literal(cg, env, w, x, dst_kind, dst_slot, depth);
            return;
        }
        case EXPR_INDEX: {
            cg_index_read(cg, env, w, x, dst_kind, dst_slot, depth);
            return;
        }
        case EXPR_CALL: {
            if (x->call.callee->type != EXPR_IDENT) {
                fail(cg, env, "only direct function calls are supported");
                return;
            }
            cg_call(cg, env, w, x, dst_kind, dst_slot, depth);
            return;
        }
        default:
            fail(cg, env, "expression type %d not supported by wasm MVP subset (strings/collections need the interpreter)", (int)x->type);
    }
}

/* print one value: dispatch on tag to the print imports.
   Tag 0 (nil) is handled first - the pre-array code skipped it and printed
   nothing at all, which silently disagreed with the interpreter. */
static void cg_print_slot(Cg *cg, FnEnv *env, Buf *w, int kind, int slot) {
    e_load_val(w, 0, kind, slot);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e(w, W_I32_EQZ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_CALL); e_u(w, IMP_PRINT_NIL);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_INT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e(w, W_CALL); e_u(w, IMP_PRINT_INT);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_FLOAT);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e(w, W_F64_REINTERPRET_I64);
    e(w, W_CALL); e_u(w, IMP_PRINT_FLOAT);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_BOOL);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_LOCAL_GET); e_u(w, LOC_IA);
    e(w, W_I32_WRAP_I64);
    e(w, W_CALL); e_u(w, IMP_PRINT_BOOL);
    e(w, W_ELSE);
    e(w, W_LOCAL_GET); e_u(w, LOC_TAGA);
    e_i32c(w, TAG_STR);
    e(w, W_I32_EQ);
    e(w, W_IF); e_u(w, 0x40);
    e(w, W_UNREACHABLE);                             /* strings are refused earlier */
    e(w, W_ELSE);
    /* the interpreter prints [1, 2, 3]; the wasm subset refuses instead of
       printing something different (docs/WASM.md "divergences") */
    e_i32c(w, ERR_ARRAY_OP);
    e(w, W_CALL); e_u(w, IMP_ERROR);
    e(w, W_UNREACHABLE);
    e(w, W_END);
    e(w, W_END);
    e(w, W_END);
    e(w, W_END);
    e(w, W_END);
}

static void cg_body(Cg *cg, FnEnv *env, Buf *w, Stmt **body, int count) {
    for (int i = 0; i < count; i++) {
        int before = w->blocks;
        int err_before = cg->err[0];
        cg_stmt(cg, env, w, body[i]);
        if (cg->err[0] && !err_before) return;
        if (w->blocks != before && !cg->err[0]) {
            snprintf(cg->err, sizeof(cg->err),
                     "wasm backend: statement at line %d left %d unclosed block(s) (internal)",
                     body[i]->line, w->blocks - before);
            return;
        }
    }
}

static void cg_stmt(Cg *cg, FnEnv *env, Buf *w, Stmt *s) {
    if (cg->err[0]) return;
    if (s->line > 0) env->line = s->line;
    switch (s->type) {
        case STMT_SAY: {
            if (s->sayStmt.message->type == EXPR_STRING) {
                fail(cg, env, "strings are not supported by the wasm MVP subset");
                return;
            }
            int b0 = w->blocks;
            cg_expr(cg, env, w, s->sayStmt.message, KIND_FRAME, TEMP_BASE, 1);
            if (cg->err[0]) return;
            if (w->blocks != b0) {
                fail(cg, env, "internal: expression left %d unclosed blocks", w->blocks - b0);
                return;
            }
            cg_print_slot(cg, env, w, KIND_FRAME, TEMP_BASE);
            if (w->blocks != b0) {
                fail(cg, env, "internal: print left %d unclosed blocks", w->blocks - b0);
                return;
            }
            return;
        }
        case STMT_ASSIGN: {
            if (s->assignStmt.target->type == EXPR_INDEX) {
                if (!s->assignStmt.value) {
                    fail(cg, env, "assignment without a value is not supported by the wasm MVP subset");
                    return;
                }
                cg_index_write(cg, env, w, s->assignStmt.target, s->assignStmt.value, 1);
                return;
            }
            if (s->assignStmt.target->type != EXPR_IDENT) {
                fail(cg, env, "only simple variable assignment is supported (a = value)");
                return;
            }
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)s->assignStmt.target->identName.length, s->assignStmt.target->identName.start);
            int kind, slot;
            if (resolve_var(cg, env, name, &kind, &slot) != 0) return;
            if (!s->assignStmt.value) {
                fail(cg, env, "assignment without a value is not supported by the wasm MVP subset");
                return;
            }
            cg_expr(cg, env, w, s->assignStmt.value, kind, slot, 1);
            return;
        }
        case STMT_IF: {
            cg_cond(cg, env, w, s->ifStmt.condition, 1);
            if (cg->err[0]) return;
            e(w, W_IF); e_u(w, 0x40);
            cg_body(cg, env, w, s->ifStmt.thenBody, s->ifStmt.thenCount);
            if (cg->err[0]) return;
            if (s->ifStmt.elseCount > 0) {
                e(w, W_ELSE);
                cg_body(cg, env, w, s->ifStmt.elseBody, s->ifStmt.elseCount);
                if (cg->err[0]) return;
            }
            e(w, W_END);
            return;
        }
        case STMT_WHILE: {
            e(w, W_BLOCK); e_u(w, 0x40);             /* depth 1 = exit */
            e(w, W_LOOP); e_u(w, 0x40);              /* depth 0 = top */
            cg_cond(cg, env, w, s->whileStmt.condition, 1);
            if (cg->err[0]) return;
            e(w, W_I32_EQZ);
            e(w, W_BR_IF); e_u(w, 1);
            cg_body(cg, env, w, s->whileStmt.body, s->whileStmt.bodyCount);
            if (cg->err[0]) return;
            e(w, W_BR); e_u(w, 0);
            e(w, W_END);
            e(w, W_END);
            return;
        }
        case STMT_REPEAT: {
            /* interpreter: counter starts 0, count expr re-evaluated every
               iteration, exits when counter >= count (OP_LT double compare) */
            e_i32c(w, 0);
            e(w, W_LOCAL_SET); e_u(w, LOC_CNT);
            e(w, W_BLOCK); e_u(w, 0x40);
            e(w, W_LOOP); e_u(w, 0x40);
            /* cond: (double)cnt < as_double(count) */
            e(w, W_LOCAL_GET); e_u(w, LOC_CNT);
            e(w, W_F64_CONVERT_I32_S);
            cg_expr(cg, env, w, s->repeatStmt.count, KIND_FRAME, TEMP_BASE, 1);
            if (cg->err[0]) return;
            e_load_val(w, 0, KIND_FRAME, TEMP_BASE);
            e_trap_on_string(w);
            e_push_as_double(w, 0);
            e(w, W_F64_LT);
            e(w, W_I32_EQZ);
            e(w, W_BR_IF); e_u(w, 1);
            cg_body(cg, env, w, s->repeatStmt.body, s->repeatStmt.bodyCount);
            if (cg->err[0]) return;
            e(w, W_LOCAL_GET); e_u(w, LOC_CNT);
            e_i32c(w, 1);
            e(w, W_I32_ADD);
            e(w, W_LOCAL_SET); e_u(w, LOC_CNT);
            e(w, W_BR); e_u(w, 0);
            e(w, W_END);
            e(w, W_END);
            return;
        }
        case STMT_FUNC:
            return;                                  /* compiled separately */
        case STMT_GLOBAL: {
            if (!env->in_function) return;           /* top-level: no-op */
            for (int i = 0; i < s->globalStmt.nameCount; i++) {
                if (env->gdecl_count >= LOCAL_MAX) { fail(cg, env, "too many global declarations"); return; }
                char name[256];
                snprintf(name, sizeof(name), "%.*s", (int)s->globalStmt.names[i].length, s->globalStmt.names[i].start);
                env->gdecls[env->gdecl_count++] = strdup(name);
            }
            return;
        }
        case STMT_RETURN: {
            if (s->returnStmt.value) {
                cg_expr(cg, env, w, s->returnStmt.value, KIND_FRAME, 0, 1);
                if (cg->err[0]) return;
            } else {
                e_store_tag(w, KIND_FRAME, 0, TAG_NIL);
                e_i64c(w, 0);
                e_store_payload_i64_from_stack(w, KIND_FRAME, 0);
            }
            e_epilogue_release(w, env);
            e(w, W_RETURN);
            return;
        }
        case STMT_EXPR: {
            if (s->exprStmt.expr->type != EXPR_CALL) {
                fail(cg, env, "only call expressions may stand alone");
                return;
            }
            cg_expr(cg, env, w, s->exprStmt.expr, KIND_FRAME, TEMP_BASE, 1);
            return;
        }
        default:
            fail(cg, env, "statement type not supported by wasm MVP subset");
    }
}

/* release the owning named locals this function is responsible for:
   slot 0 (the return value) and the parameters belong to the caller */
static void e_epilogue_release(Buf *w, FnEnv *env) {
    for (int i = env->nparams; i < env->local_count; i++)
        e_ref_slot(w, KIND_FRAME, env->locals[i].slot, HFN_RELEASE);
}

/* ---------- module assembly ---------- */
static void emit_func_body(Cg *cg, Buf *code, Stmt **body, int count, int nparams, char **params) {
    Buf fb = {0};
    FnEnv env;
    memset(&env, 0, sizeof(env));
    env.in_function = 1;
    env.nparams = nparams;
    for (int i = 0; i < nparams; i++) {
        env.locals[env.local_count].name = strdup(params[i]);
        env.locals[env.local_count].slot = 1 + i;
        env.local_count++;
    }
    cg_body(cg, &env, &fb, body, count);
    e_epilogue_release(&fb, &env);           /* deterministic reclamation */
    /* implicit end */
    bput(&fb, W_END);
    /* locals declaration: (5 x i32)(2 x i64)(1 x f64)(2 x i32) */
    Buf out = {0};
    bleb_u(&out, 4);
    bleb_u(&out, LOC_IA - 1); bleb_u(&out, 0x7F);    /* locals 1..5: i32 */
    bleb_u(&out, 2); bleb_u(&out, 0x7E);             /* i64 */
    bleb_u(&out, 1); bleb_u(&out, 0x7C);             /* f64 */
    bleb_u(&out, LOC_IDX - LOC_ARR + 1); bleb_u(&out, 0x7F);  /* LOC_ARR/LOC_IDX */
    /* entry prologue: nil-fill the return slot and every declared local; a
       local assigned only inside a branch must not read stale frame memory */
    e_store_nil(&out, KIND_FRAME, 0);
    for (int i = nparams + 1; i <= env.local_count; i++) e_store_nil(&out, KIND_FRAME, i);
    /* append size-prefixed body to code section */
    bleb_u(code, (unsigned long)(out.n + fb.n));
    for (size_t i = 0; i < out.n; i++) bput(code, out.b[i]);
    for (size_t i = 0; i < fb.n; i++) bput(code, fb.b[i]);
    free(out.b);
    free(fb.b);
}

/* ---------- heap helper + benchmark bodies ------------------------------
   These are hand-emitted wasm functions appended after the probes, so every
   index that existed before stays valid.  They are ordinary wasm: no host
   support is needed beyond the existing im_error import.                  */

static void emit_body(Buf *code, Buf *fb) {
    bleb_u(code, (unsigned long)fb->n);
    for (size_t i = 0; i < fb->n; i++) bput(code, fb->b[i]);
}

/* im_heap_alloc(total_bytes i32) -> payload i32
   first-fit over the LIFO free list, else bump $hbump; exhaustion is
   im_error(ERR_HEAP_EXHAUSTED) + unreachable, never a silent continue. */
static void emit_heap_alloc_body(Buf *code) {
    Buf w = {0};
    bleb_u(&w, 1); bleb_u(&w, 4); bleb_u(&w, 0x7F);  /* 1 need 2 prev 3 cur 4 res */
    /* need = (arg + 15) & ~15 */
    e(&w, W_LOCAL_GET); e_u(&w, 0);
    e_i32c(&w, 15); e(&w, W_I32_ADD);
    e_i32c(&w, -16); e(&w, W_I32_AND);
    e(&w, W_LOCAL_SET); e_u(&w, 1);
    /* if (need < HEAP_HEADER) need = HEAP_HEADER */
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_LT_U);
    e(&w, W_IF); e_u(&w, 0x40);
    e_i32c(&w, HEAP_HEADER); e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_END);
    /* prev = -1; cur = $hfree */
    e_i32c(&w, -1); e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_GLOBAL_GET); e_u(&w, GIDX_HFREE); e(&w, W_LOCAL_SET); e_u(&w, 3);
    e(&w, W_BLOCK); e_u(&w, 0x40);
    e(&w, W_LOOP); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_I32_EQZ); e(&w, W_BR_IF); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 0);
    e(&w, W_LOCAL_GET); e_u(&w, 1);
    e(&w, W_I32_GE_U);
    e(&w, W_IF); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 2); e_i32c(&w, -1); e(&w, W_I32_EQ);
    e(&w, W_IF); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 8);
    e(&w, W_GLOBAL_SET); e_u(&w, GIDX_HFREE);
    e(&w, W_ELSE);
    e(&w, W_LOCAL_GET); e_u(&w, 2);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 8);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 8);
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_LOCAL_SET); e_u(&w, 4);
    e(&w, W_BR); e_u(&w, 2);
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 8);
    e(&w, W_LOCAL_SET); e_u(&w, 3);
    e(&w, W_BR); e_u(&w, 0);
    e(&w, W_END);
    e(&w, W_END);
    /* if (res == 0) bump */
    e(&w, W_LOCAL_GET); e_u(&w, 4); e(&w, W_I32_EQZ);
    e(&w, W_IF); e_u(&w, 0x40);
    e(&w, W_GLOBAL_GET); e_u(&w, GIDX_HBUMP); e(&w, W_LOCAL_SET); e_u(&w, 3);
    /* if (cur + need > HEAP_END) trap */
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_I32_ADD);
    e_i32c(&w, HEAP_END);
    e(&w, W_I32_GT_U);
    e(&w, W_IF); e_u(&w, 0x40);
    e_i32c(&w, ERR_HEAP_EXHAUSTED); e(&w, W_CALL); e_u(&w, IMP_ERROR);
    e(&w, W_UNREACHABLE);
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_I32_ADD);
    e(&w, W_GLOBAL_SET); e_u(&w, GIDX_HBUMP);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_LOCAL_SET); e_u(&w, 4);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e(&w, W_LOCAL_GET); e_u(&w, 1);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 0);      /* block size */
    e(&w, W_END);
    /* header: refs = 0, elements = 0 (both paths); the receiving owning slot
       takes one reference when the value is published */
    e(&w, W_LOCAL_GET); e_u(&w, 4); e_i32c(&w, 0);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 4);
    e(&w, W_LOCAL_GET); e_u(&w, 4); e_i32c(&w, 0);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 12);
    e(&w, W_LOCAL_GET); e_u(&w, 4); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_ADD);
    e(&w, W_END);                                     /* end of function body */
    emit_body(code, &w);
    free(w.b);
}

/* im_heap_free(payload i32): trim $hbump when the block is on top,
   otherwise push it on the free list */
static void emit_heap_free_body(Buf *code) {
    Buf w = {0};
    bleb_u(&w, 1); bleb_u(&w, 1); bleb_u(&w, 0x7F);  /* 1 t */
    e(&w, W_LOCAL_GET); e_u(&w, 0); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_SUB);
    e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 0);
    e(&w, W_I32_ADD);
    e(&w, W_GLOBAL_GET); e_u(&w, GIDX_HBUMP);
    e(&w, W_I32_EQ);
    e(&w, W_IF); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_GLOBAL_SET); e_u(&w, GIDX_HBUMP);
    e(&w, W_ELSE);
    e(&w, W_LOCAL_GET); e_u(&w, 1);
    e(&w, W_GLOBAL_GET); e_u(&w, GIDX_HFREE);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 8);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_GLOBAL_SET); e_u(&w, GIDX_HFREE);
    e(&w, W_END);
    e(&w, W_END);                                     /* end of function body */
    emit_body(code, &w);
    free(w.b);
}

/* im_retain(payload i32) */
static void emit_heap_retain_body(Buf *code) {
    Buf w = {0};
    bleb_u(&w, 0);
    e(&w, W_LOCAL_GET); e_u(&w, 0); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_SUB);
    e_i32c(&w, 4); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_GET); e_u(&w, 0); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_SUB);
    e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 4);
    e_i32c(&w, 1); e(&w, W_I32_ADD);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 0);
    e(&w, W_END);
    emit_body(code, &w);
    free(w.b);
}

/* im_release(payload i32): drop a reference; at zero, recursively release
   array elements and hand the block back to the allocator */
static void emit_heap_release_body(Buf *code) {
    Buf w = {0};
    bleb_u(&w, 1); bleb_u(&w, 3); bleb_u(&w, 0x7F);  /* 1 t 2 i 3 tag */
    e(&w, W_LOCAL_GET); e_u(&w, 0); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_SUB);
    e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, 4); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 4);
    e_i32c(&w, 1); e(&w, W_I32_SUB);
    e(&w, W_I32_STORE); e_u(&w, 2); e_u(&w, 0);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 4);
    e(&w, W_I32_EQZ);
    e(&w, W_IF); e_u(&w, 0x40);
    e_i32c(&w, 0);
    e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_BLOCK); e_u(&w, 0x40);
    e(&w, W_LOOP); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 2);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 12);
    e(&w, W_I32_GE_U);
    e(&w, W_BR_IF); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_GET); e_u(&w, 2); e_i32c(&w, SLOT_BYTES); e(&w, W_I32_MUL);
    e(&w, W_I32_ADD);
    e(&w, W_I32_LOAD); e_u(&w, 2); e_u(&w, 0);
    e(&w, W_LOCAL_SET); e_u(&w, 3);
    e(&w, W_LOCAL_GET); e_u(&w, 3); e_i32c(&w, TAG_ARR); e(&w, W_I32_EQ);
    e(&w, W_IF); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_GET); e_u(&w, 2); e_i32c(&w, SLOT_BYTES); e(&w, W_I32_MUL);
    e(&w, W_I32_ADD);
    e(&w, W_I64_LOAD); e_u(&w, 3); e_u(&w, 8);
    e(&w, W_I32_WRAP_I64);
    e(&w, W_CALL); e_u(&w, FUNC_HELPER(HFN_RELEASE));
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 2); e_i32c(&w, 1); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_BR); e_u(&w, 0);
    e(&w, W_END);
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, HEAP_HEADER); e(&w, W_I32_ADD);
    e(&w, W_CALL); e_u(&w, FUNC_HELPER(HFN_FREE));
    e(&w, W_END);
    e(&w, W_END);                                     /* end of function body */
    emit_body(code, &w);
    free(w.b);
}

/* v128 literal with two f64 lanes */
static void e_v128c(Buf *w, double a, double b) {
    e(w, W_SIMD); e_u(w, 0x0C);
    bf64(w, a);
    bf64(w, b);
}

/* benchmark pair: sum 1.0 n times, scalar and two-lane (v128).
   Every partial sum is an exact integer <= 2^53, so both forms are
   bit-identical to n - that is what lets the test assert equality. */
static void emit_bench_scalar_body(Buf *code) {
    Buf w = {0};
    bleb_u(&w, 2); bleb_u(&w, 1); bleb_u(&w, 0x7F);  /* 1 i */
    bleb_u(&w, 1); bleb_u(&w, 0x7C);                 /* 2 acc */
    e_f64c(&w, 0.0); e(&w, W_LOCAL_SET); e_u(&w, 2);
    e_i32c(&w, 0); e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_BLOCK); e_u(&w, 0x40);
    e(&w, W_LOOP); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_LOCAL_GET); e_u(&w, 0);
    e(&w, W_I32_GE_S);
    e(&w, W_BR_IF); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 2); e_f64c(&w, 1.0); e(&w, W_F64_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, 1); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_BR); e_u(&w, 0);
    e(&w, W_END);
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 2);
    e(&w, W_END);
    emit_body(code, &w);
    free(w.b);
}

static void emit_bench_simd_body(Buf *code) {
    Buf w = {0};
    bleb_u(&w, 3); bleb_u(&w, 1); bleb_u(&w, 0x7F);  /* 1 i */
    bleb_u(&w, 1); bleb_u(&w, 0x7C);                 /* 2 acc (f64 sum) */
    bleb_u(&w, 1); bleb_u(&w, 0x7B);                 /* 3 v (v128) */
    e_f64c(&w, 0.0);
    e(&w, W_SIMD); e_u(&w, S_F64X2_SPLAT);
    e(&w, W_LOCAL_SET); e_u(&w, 3);
    e_i32c(&w, 0); e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_BLOCK); e_u(&w, 0x40);
    e(&w, W_LOOP); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, 2); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_GET); e_u(&w, 0);
    e(&w, W_I32_GT_S);                              /* exit when i+2 > n */
    e(&w, W_BR_IF); e_u(&w, 1);
    e(&w, W_LOCAL_GET); e_u(&w, 3);
    e_v128c(&w, 1.0, 1.0);
    e(&w, W_SIMD); e_u(&w, S_F64X2_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 3);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e_i32c(&w, 2); e(&w, W_I32_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 1);
    e(&w, W_BR); e_u(&w, 0);
    e(&w, W_END);
    e(&w, W_END);
    /* horizontal add of the two lanes, then the possible odd leftover */
    e(&w, W_LOCAL_GET); e_u(&w, 3);
    e(&w, W_SIMD); e_u(&w, S_F64X2_EXTRACT_LANE); e_u(&w, 0);
    e(&w, W_LOCAL_GET); e_u(&w, 3);
    e(&w, W_SIMD); e_u(&w, S_F64X2_EXTRACT_LANE); e_u(&w, 1);
    e(&w, W_F64_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_LOCAL_GET); e_u(&w, 1); e(&w, W_LOCAL_GET); e_u(&w, 0); e(&w, W_I32_LT_S);
    e(&w, W_IF); e_u(&w, 0x40);
    e(&w, W_LOCAL_GET); e_u(&w, 2); e_f64c(&w, 1.0); e(&w, W_F64_ADD);
    e(&w, W_LOCAL_SET); e_u(&w, 2);
    e(&w, W_END);
    e(&w, W_LOCAL_GET); e_u(&w, 2);
    e(&w, W_END);
    emit_body(code, &w);
    free(w.b);
}

int wasm_compile_program(Program *prog, const char *output_path) {
    Cg cg;
    memset(&cg, 0, sizeof(cg));
    g_err[0] = 0;
    cg.line = 1;

    /* pass 1: collect functions */
    for (int i = 0; i < prog->count; i++) {
        Stmt *s = prog->stmts[i];
        if (s->type != STMT_FUNC) continue;
        if (cg.func_count >= 256) { snprintf(g_err, sizeof(g_err), "wasm MVP subset: too many functions (max 256)"); return -1; }
        char name[256];
        snprintf(name, sizeof(name), "%.*s", (int)s->funcDef.name.length, s->funcDef.name.start);
        if (find_func(&cg, name) >= 0) {
            snprintf(g_err, sizeof(g_err), "wasm MVP subset: duplicate function '%s'", name);
            return -1;
        }
        cg.funcs[cg.func_count].name = strdup(name);
        cg.funcs[cg.func_count].params = s->funcDef.paramCount;
        cg.func_count++;
    }
    g_func_count = cg.func_count;

    Buf mod = {0};
    /* magic + version */
    bput(&mod, 0x00); bput(&mod, 'a'); bput(&mod, 's'); bput(&mod, 'm');
    bput(&mod, 1); bput(&mod, 0); bput(&mod, 0); bput(&mod, 0);

    /* type section: 0:(i64)->() 1:(f64)->() 2:(i32)->() 3:()->() 4:()->i32
       5:(i32)->i32 (heap helpers) 6:(i32)->f64 (benchmarks) */
    {
        Buf t = {0};
        bleb_u(&t, 7);
        bput(&t, 0x60); bleb_u(&t, 1); bleb_u(&t, 0x7E); bleb_u(&t, 0);
        bput(&t, 0x60); bleb_u(&t, 1); bleb_u(&t, 0x7C); bleb_u(&t, 0);
        bput(&t, 0x60); bleb_u(&t, 1); bleb_u(&t, 0x7F); bleb_u(&t, 0);
        bput(&t, 0x60); bleb_u(&t, 0); bleb_u(&t, 0);
        bput(&t, 0x60); bleb_u(&t, 0); bleb_u(&t, 1); bleb_u(&t, 0x7F);
        bput(&t, 0x60); bleb_u(&t, 1); bleb_u(&t, 0x7F); bleb_u(&t, 1); bleb_u(&t, 0x7F);
        bput(&t, 0x60); bleb_u(&t, 1); bleb_u(&t, 0x7F); bleb_u(&t, 1); bleb_u(&t, 0x7C);
        bput(&mod, 1); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }
    /* import section */
    {
        Buf t = {0};
        bleb_u(&t, 5);
        bword(&t, "env"); bword(&t, "im_print_int");   bput(&t, 0x00); bleb_u(&t, 0);
        bword(&t, "env"); bword(&t, "im_print_float"); bput(&t, 0x00); bleb_u(&t, 1);
        bword(&t, "env"); bword(&t, "im_print_bool");  bput(&t, 0x00); bleb_u(&t, 2);
        bword(&t, "env"); bword(&t, "im_print_nil");   bput(&t, 0x00); bleb_u(&t, 3);
        bword(&t, "env"); bword(&t, "im_error");       bput(&t, 0x00); bleb_u(&t, 2);
        bput(&mod, 2); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }
    /* function section: main + user funcs (type 2: (i32)->()) + 3 probes
       (type 4: ()->(i32)) + 4 heap helpers + 2 benchmarks (type 6)
       body order must match exactly */
    {
        Buf t = {0};
        bleb_u(&t, 4 + cg.func_count + HFN_COUNT + 2);
        bleb_u(&t, 2);
        for (int i = 0; i < cg.func_count; i++) bleb_u(&t, 2);
        bleb_u(&t, 4); bleb_u(&t, 4); bleb_u(&t, 4);
        bleb_u(&t, 5);                     /* im_heap_alloc  */
        bleb_u(&t, 2); bleb_u(&t, 2); bleb_u(&t, 2);  /* free/retain/release */
        bleb_u(&t, 6); bleb_u(&t, 6);      /* bench_sum_scalar / _simd */
        bput(&mod, 3); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }
    /* memory section */
    {
        Buf t = {0};
        bleb_u(&t, 1);
        bput(&t, 0x00); bleb_u(&t, MEM_PAGES);
        bput(&mod, 5); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }
    /* global section: $sp, $hbump, $hfree */
    {
        Buf t = {0};
        bleb_u(&t, 3);
        bput(&t, 0x7F); bput(&t, 0x01);              /* i32 mutable */
        bput(&t, W_I32_CONST); bleb_s(&t, FRAME_BYTES);
        bput(&t, W_END);
        bput(&t, 0x7F); bput(&t, 0x01);
        bput(&t, W_I32_CONST); bleb_s(&t, HEAP_BASE);
        bput(&t, W_END);
        bput(&t, 0x7F); bput(&t, 0x01);
        bput(&t, W_I32_CONST); bleb_s(&t, 0);
        bput(&t, W_END);
        bput(&mod, 6); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }
    /* export section */
    {
        Buf t = {0};
        bleb_u(&t, 7);
        bword(&t, "memory");                bput(&t, 0x02); bleb_u(&t, 0);
        bword(&t, "inimerse_run");          bput(&t, 0x00); bleb_u(&t, FUNC_MAIN);
        bword(&t, "inimerse_probe");        bput(&t, 0x00); bleb_u(&t, FUNC_MAIN + 1 + cg.func_count);
        bword(&t, "inimerse_abi_version");  bput(&t, 0x00); bleb_u(&t, FUNC_MAIN + 2 + cg.func_count);
        bword(&t, "inimerse_capabilities"); bput(&t, 0x00); bleb_u(&t, FUNC_MAIN + 3 + cg.func_count);
        bword(&t, "bench_sum_scalar");      bput(&t, 0x00); bleb_u(&t, FUNC_BENCH_SCALAR);
        bword(&t, "bench_sum_simd");        bput(&t, 0x00); bleb_u(&t, FUNC_BENCH_SIMD);
        bput(&mod, 7); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }
    /* code section: main, user funcs, 3 probe funcs, 4 heap helpers,
       2 benchmark funcs */
    {
        Buf t = {0};
        bleb_u(&t, 4 + cg.func_count + HFN_COUNT + 2);

        /* main body */
        {
            Buf fb = {0};
            bleb_u(&fb, 4);
            bleb_u(&fb, LOC_IA - 1); bleb_u(&fb, 0x7F);
            bleb_u(&fb, 2); bleb_u(&fb, 0x7E);
            bleb_u(&fb, 1); bleb_u(&fb, 0x7C);
            bleb_u(&fb, LOC_IDX - LOC_ARR + 1); bleb_u(&fb, 0x7F);
            FnEnv env;
            memset(&env, 0, sizeof(env));
            env.in_function = 0;
            cg_body(&cg, &env, &fb, prog->stmts, prog->count);
            int had_err = cg.err[0] != 0;
            if (!had_err && fb.blocks != 0) {
                snprintf(g_err, sizeof(g_err), "wasm backend: unbalanced control flow in main (%d unclosed blocks) — please report", fb.blocks);
                free(fb.b); free(t.b); free(mod.b);
                return -1;
            }
            bput(&fb, W_END);
            bleb_u(&t, (unsigned long)fb.n);
            for (size_t i = 0; i < fb.n; i++) bput(&t, fb.b[i]);
            free(fb.b);
            if (had_err) {
                snprintf(g_err, sizeof(g_err), "%s", cg.err);
                free(t.b);
                free(mod.b);
                return -1;
            }
        }
        /* user funcs */
        for (int i = 0; i < cg.func_count && !cg.err[0]; i++) {
            Stmt *fn = NULL;
            for (int j = 0; j < prog->count; j++) {
                Stmt *s = prog->stmts[j];
                if (s->type != STMT_FUNC) continue;
                char name[256];
                snprintf(name, sizeof(name), "%.*s", (int)s->funcDef.name.length, s->funcDef.name.start);
                if (strcmp(name, cg.funcs[i].name) == 0) { fn = s; break; }
            }
            if (!fn) break;
            char *params[64];
            int np = fn->funcDef.paramCount > 64 ? 64 : fn->funcDef.paramCount;
            for (int k = 0; k < np; k++) {
                params[k] = malloc(128);
                snprintf(params[k], 128, "%.*s", (int)fn->funcDef.params[k].length, fn->funcDef.params[k].start);
            }
            emit_func_body(&cg, &t, fn->funcDef.body, fn->funcDef.bodyCount, np, params);
            for (int k = 0; k < np; k++) free(params[k]);
            if (cg.err[0]) { free(t.b); snprintf(g_err, sizeof(g_err), "%s", cg.err); free(mod.b); return -1; }
        }
        /* probe funcs */
        struct { const char *name; long long val; } probes[3] = {
            {"probe", 0x0500}, {"abi", 1}, {"caps", 0}
        };
        for (int i = 0; i < 3; i++) {
            (void)probes[i].name;
            Buf fb = {0};
            bleb_u(&fb, 0);                          /* no locals */
            bput(&fb, W_I32_CONST); bleb_s(&fb, probes[i].val);
            bput(&fb, W_END);
            bleb_u(&t, (unsigned long)fb.n);
            for (size_t j = 0; j < fb.n; j++) bput(&t, fb.b[j]);
            free(fb.b);
        }
        if (cg.err[0]) { free(t.b); free(mod.b); return -1; }
        /* heap helpers + benchmarks (same order as the function section) */
        emit_heap_alloc_body(&t);
        emit_heap_free_body(&t);
        emit_heap_retain_body(&t);
        emit_heap_release_body(&t);
        emit_bench_scalar_body(&t);
        emit_bench_simd_body(&t);
        bput(&mod, 10); bleb_u(&mod, (unsigned long)t.n);
        for (size_t i = 0; i < t.n; i++) bput(&mod, t.b[i]);
        free(t.b);
    }

    FILE *f = fopen(output_path, "wb");
    if (!f) {
        snprintf(g_err, sizeof(g_err), "wasm MVP subset: cannot write '%s'", output_path);
        free(mod.b);
        return -1;
    }
    fwrite(mod.b, 1, mod.n, f);
    fclose(f);
    free(mod.b);
    return 0;
}
