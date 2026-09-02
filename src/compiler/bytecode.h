#ifndef BYTECODE_H
#define BYTECODE_H

#include "common.h"

/* ---------- register opcodes ---------- */
typedef enum {
    OP_MOV,
    OP_LOADK_INT, OP_LOADK_FLOAT, OP_LOADK_STRING, OP_LOADK_BOOL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_NEG,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_AND, OP_OR,
    OP_NOT,                 /* logical not: r1 = !r2 */
    OP_NEW_ARRAY,           /* new array: r1 = [reg[r2 .. r2+r3-1]] */
    OP_INDEX_GET,           /* index get: r1 = reg[r2][r3] */
    OP_INDEX_SET,           /* index set: reg[r1][r2] = reg[r3] */
    OP_LOAD_GLOBAL, OP_STORE_GLOBAL,
    OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE,
    OP_CALL_BUILTIN, OP_PUSH_REG, OP_POP_REG,
    OP_SAY, OP_WAIT, OP_STOP,
    OP_HALT,
    /* custom functions (appended at end to keep old opcode numbers, DLL ABI compatible) */
    OP_CALL_FUNC,   /* call func: r1=func idx, r2=result reg, r3=argc (args popped from stack) */
    OP_RETURN,      /* return: r1=result reg (-1 or 0 means nil) */
    OP_IS_NIL,      /* r1 = (r2 == nil) */
    /* thread instructions (appended at end) */
    OP_THREAD_START,  /* start thread: r1=thread idx, r2=result reg, r3=argc */
    OP_THREAD_CTRL,   /* thread ctrl: r1=thread idx(-1=all), r2=op(ThreadOp), r3=0 */
    OP_THREAD_JOIN,   /* join: r1=thread idx, r2=timeout reg(-1=infinite), r3=0 */
    OP_THREAD_WAIT,   /* thread wait: r1=thread idx, r2=cond reg(-1=seconds), r3=timeout reg(-1=none) */
    OP_THREAD_STATE,  /* thread state: r1=result reg, r2=thread idx, r3=attr(0run/1pause/2stop/3done) */
    OP_LOCK,          /* mutex: r1=lock idx, r2=0 lock / 1 unlock, r3=0 */
    OP_SEND,          /* send: r1=thread idx, r2=msg reg, r3=0 */
    OP_RECV,          /* recv: r1=result reg, r2=timeout reg(-1=block), r3=0 */
    OP_NEW_DICT,      /* new dict: r1=result, r2=0, r3=pair count (2*r3 values from stack) */
    OP_EQK,           /* string const compare: r1 = (R[r2] == string_pool[r3]) */
    OP_NEQK,          /* string const not-equal: r1 = (R[r2] != string_pool[r3]) */
    OP_DECLARE,
    OP_RECORD        /* resource declare: r1=kind(0=mem,1=threads,2=time,3=inst), r2=float_pool idx, r3=0 */,
    OP_MOD,          /* modulo: r1 = r2 % r3 (appended at end for ABI compat) */
    OP_NEW_SET,       /* new set: r1 = merge(stack[sp-1 .. sp-r3]) into a set object */
    OP_SET_INTERVAL,  /* interval: r1=result, r2=(string_pool idx of base set name)|(flags<<24), lo/hi popped from stack */
    OP_IN,            /* in: r1 = (r2 in r3) membership/subset */
    OP_MIN,           /* min: r1 = min(r2) */
    OP_MAX,           /* max: r1 = max(r2) */
    OP_BE,            /* bounded assign: r1=global idx, r2=set reg, r3=init reg(-1 = uninitialized) */
    OP_TRY_START, OP_TRY_END, OP_THROW,
    OP_SET_ADD,
    OP_THREAD_GOTO,
    OP_CONCAT,       /* concat chain: r1 = left-assoc fold(+) of R[r2 .. r2+r3-1] (single alloc fast path) */
    OP_YIELD,        /* yield: task cooperatively hands control back to the scheduler */
    OP_MAKE_FUNC, OP_CALL_VALUE
} OpCode;

/* ---------- register instruction (fixed size) ---------- */
typedef struct {
    OpCode op;
    int r1;            /* target or source1 */
    int r2;            /* source2 or immediate/index */
    int r3;            /* source3 or extra data */
} RegInstruction;

typedef struct {
    int start_off;
    int end_off;
    int catch_off;
    int var_idx;
    int ignore;   /* 1 = bare try (no catch): exceptions are swallowed and recorded (runtime-only, not serialized) */
} TryEntry;

typedef struct { char *name; int off; } LabelEntry;

/* ---------- bytecode block ---------- */
typedef struct Bytecode {
    RegInstruction *code;
    int count;
    int capacity;

    char **string_pool;      /* string constants pool */
    int string_count;

    double *float_pool;      /* float constants pool */
    int float_count;

    /* custom function section (appended at end, keeps field offsets for old DLLs) */
    struct Bytecode *funcs[1024];
    int func_count;
    int func_argc[1024];       /* argc of each function */
    char *func_names[1024];    /* name of each function */

    /* thread section (also appended at end, old DLLs never touch) */
    struct Bytecode *threads[32];
    int thread_count;
    int thread_argc[32];     /* argc of each thread */
    char *thread_names[32];  /* name of each thread */

    /* string constant intern cache (filled by VM at runtime) */
    char **str_interned;
    unsigned char thread_flags[32]; /* per-thread labels (THREAD_FLAG_*, memory-only, appended) */
    int main_flags;
    TryEntry *try_entries;
    int try_count;
    int try_cap; /* appended last (ABI-safe) */
    LabelEntry *labels;   /* label table (name->offset) for thread-goto / debug (memory-only) */
    char **global_names;     /* global variable names (index-aligned with VM globals) */
    int global_name_count;
    /* Closure metadata: names are ordered capture slots for this function. */
    char **capture_names;
    int capture_count;
    int label_count;                /* main-thread labels (memory-only, appended) */
} Bytecode;

void bytecode_init(Bytecode *bc);
void bytecode_add(Bytecode *bc, OpCode op, int r1, int r2, int r3);
void bytecode_add_try(Bytecode *bc, int start_off, int end_off, int catch_off, int var_idx, int ignore);
int  bytecode_add_string(Bytecode *bc, const char *str);
int  bytecode_add_float(Bytecode *bc, double val);
int  bytecode_add_capture(Bytecode *bc, const char *name);
int  bytecode_capture_index(const Bytecode *bc, const char *name);
int  bytecode_current_offset(Bytecode *bc);
void bytecode_patch(Bytecode *bc, int offset, int r2);
void bytecode_free(Bytecode *bc);

/* serialize & packaging */
int  bytecode_write(Bytecode *bc, FILE *f);
Bytecode *bytecode_read(FILE *f);
int  bytecode_write_file(const char *path, Bytecode *bc);   /* .inim with magic+version */
Bytecode *bytecode_read_file(const char *path);              /* .inim (NULL if bad/old version) */
int  bytecode_append_to_exe(const char *exePath, Bytecode *bc, const char *outputExe);
Bytecode *bytecode_load_from_exe(const char *exePath);

/* embed mod resources into exe tail */
int  bytecode_append_mods_to_exe(const char *exePath, const char *modsDir,
                                 const char *modNames, const char *outputExe);
unsigned char *bytecode_extract_mods(const char *exePath, long *outLen);
int bytecode_release_mods(const char *exePath, const char *destDir);

#endif
