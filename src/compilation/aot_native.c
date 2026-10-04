/* aot_native — lower the numeric subset of the AST to standalone C.
 *
 * See aot_native.h for what this is and why it exists.  Design notes:
 *
 *  - Values use a small tagged struct (NV) rather than raw longs, because the
 *    language is dynamically typed and `say` must print ints, floats and
 *    bools the way the interpreter does.  -O2 inlines every helper below, so
 *    the tag costs a predictable branch per operation instead of a dispatch.
 *
 *  - Everything outside the subset is REFUSED (translate returns 0) with a
 *    message.  There is no "best effort" path: silently mis-compiling a
 *    construct would produce a native binary that disagrees with the
 *    interpreter, which is strictly worse than declining to compile.
 *
 *  - Local variables are declared at function scope and zero-initialised.
 *    The interpreter raises "undefined variable" when reading an unassigned
 *    name; the generated code instead reads 0.  That difference is reachable
 *    only by programs that raise, so it does not affect well-formed programs,
 *    and it is recorded in the stream report as a known divergence.
 */
#include "aot_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ buffer */

typedef struct {
    char  *d;
    size_t len, cap;
} Buf;

static void buf_init(Buf *b) { b->cap = 4096; b->len = 0; b->d = malloc(b->cap); if (b->d) b->d[0] = '\0'; }

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
    b->d[b->len] = '\0';
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

/* ----------------------------------------------------------------- symbols */

enum { SYM_GLOBAL = 0, SYM_FUNC = 1 };

typedef struct {
    char name[128];    /* source name */
    char cname[136];   /* mangled C identifier */
    int  kind;
    int  arity;        /* SYM_FUNC */
} Sym;

typedef struct {
    Sym  syms[512];
    int  nsyms;

    char locals[512][136];
    int  nlocals;

    const char *extern_name;   /* source global defined by the harness, or NULL */
    const char *entry_name;    /* generated entry point; "main" by default */

    int    failed;
    char   err[256];
} Gen;

static void fail(Gen *g, const char *fmt, ...) {
    if (g->failed) return;             /* keep the FIRST reason */
    g->failed = 1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->err, sizeof g->err, fmt, ap);
    va_end(ap);
}

static Sym *sym_find(Gen *g, StringView name) {
    for (int i = 0; i < g->nsyms; i++) {
        Sym *s = &g->syms[i];
        size_t n = strlen(s->name);
        if ((size_t)name.length == n && name.start && memcmp(name.start, s->name, n) == 0)
            return s;
    }
    return NULL;
}

static Sym *sym_add(Gen *g, StringView name, int kind, int arity) {
    Sym *existing = sym_find(g, name);
    if (existing) {
        if (kind == SYM_FUNC) { existing->kind = SYM_FUNC; existing->arity = arity; }
        return existing;
    }
    if (g->nsyms >= 512) { fail(g, "too many top-level names (limit 512)"); return NULL; }
    if (name.length <= 0 || name.length >= (int)sizeof(((Sym *)0)->name)) {
        fail(g, "identifier too long");
        return NULL;
    }
    Sym *s = &g->syms[g->nsyms++];
    memcpy(s->name, name.start, (size_t)name.length);
    s->name[name.length] = '\0';
    s->kind = kind;
    s->arity = arity;
    /* Hand-rolled rather than snprintf: both fields live in the same object,
       so a formatted write makes the compiler warn about overlap. */
    const char *prefix = (kind == SYM_FUNC) ? "fn_" : "g_";
    size_t plen = strlen(prefix);
    memcpy(s->cname, prefix, plen);
    memcpy(s->cname + plen, s->name, (size_t)name.length + 1);
    return s;
}

static int local_find(Gen *g, StringView name) {
    for (int i = 0; i < g->nlocals; i++) {
        size_t n = strlen(g->locals[i]);
        if ((size_t)name.length == n && name.start && memcmp(name.start, g->locals[i], n) == 0)
            return i;
    }
    return -1;
}

static int local_add(Gen *g, StringView name) {
    if (local_find(g, name) >= 0) return 0;
    if (g->nlocals >= 512) { fail(g, "too many locals in one function (limit 512)"); return -1; }
    if (name.length <= 0 || name.length >= (int)sizeof(g->locals[0])) {
        fail(g, "identifier too long");
        return -1;
    }
    memcpy(g->locals[g->nlocals], name.start, (size_t)name.length);
    g->locals[g->nlocals][name.length] = '\0';
    return g->nlocals++;
}

/* ---------------------------------------------------- the generated runtime
 *
 * Emitted verbatim into every generated translation unit.  `NV` is the tagged
 * value; the helpers are static inline so the host compiler folds them. */
static const char *kPreamble =
"/* Generated by inimerse aot_native.  Do not edit; regenerate instead. */\n"
"#include <stdio.h>\n"
"#include <stdlib.h>\n"
"#include <string.h>\n"
"\n"
"typedef struct { int t; long long i; double f; } NV;\n"
"#define NV_INT 0\n"
"#define NV_FLT 1\n"
"#define NV_BOO 2\n"
"\n"
"static inline NV nv_int(long long x) { NV r; r.t = NV_INT; r.i = x;  r.f = 0; return r; }\n"
"static inline NV nv_flt(double x)    { NV r; r.t = NV_FLT; r.i = 0;  r.f = x; return r; }\n"
"static inline NV nv_boo(int x)       { NV r; r.t = NV_BOO; r.i = x ? 1 : 0; r.f = 0; return r; }\n"
"\n"
"static inline double nv_asf(NV a) { return a.t == NV_FLT ? a.f : (double)a.i; }\n"
"/* Saturating float->int64: the plain C cast is UB outside the int64 range\n"
"   (C11 6.3.1.4p1), and x86-64 cvttsd2si silently returns INT64_MIN -- the\n"
"   same trap the interpreter fell into.  See docs/AUDIT.md §1.6. */\n"
"static inline long long nv_asi(NV a) {\n"
"    if (a.t != NV_FLT) return a.i;\n"
"    if (a.f >= 9223372036854775808.0) return 9223372036854775807LL;\n"
"    if (a.f < -9223372036854775808.0) return (-9223372036854775807LL - 1);\n"
"    return (long long)a.f;\n"
"}\n"
"static inline int nv_tru(NV a) { return a.t == NV_FLT ? (int)(a.f != 0.0) : (int)(a.i != 0); }\n"
"\n"
"/* Unifying division-by-zero across the three backends (docs/AUDIT.md §1.6):\n"
"   the interpreter and the wasm backend both raise division_by_zero, so the\n"
"   native backend must refuse too instead of quietly yielding inf or 0. */\n"
"static void nv_die_division_by_zero(void) {\n"
"    fflush(stdout);\n"
"    fprintf(stderr, \"[exception] uncaught: division_by_zero\\n\");\n"
"    exit(1);\n"
"}\n"
"/* Integer overflow is an error, not a silent wrap (docs/AUDIT.md §1.14): the\n"
"   interpreter raises numeric_overflow, so the native backend must refuse too\n"
"   instead of wrapping, which is also signed-overflow UB in C. */\n"
"static void nv_die_numeric_overflow(void) {\n"
"    fflush(stdout);\n"
"    fprintf(stderr, \"[exception] uncaught: numeric_overflow\\n\");\n"
"    exit(1);\n"
"}\n"
"\n"
"static inline NV nv_neg(NV a) {\n"
"    if (a.t == NV_FLT) return nv_flt(-a.f);\n"
"    long long r;\n"
"    if (__builtin_sub_overflow(0LL, a.i, &r)) nv_die_numeric_overflow();\n"
"    return nv_int(r);\n"
"}\n"
"static inline NV nv_not(NV a) { return nv_boo(!nv_tru(a)); }\n"
"\n"
"static inline NV nv_add(NV a, NV b) {\n"
"    if (a.t == NV_FLT || b.t == NV_FLT) return nv_flt(nv_asf(a) + nv_asf(b));\n"
"    long long r;\n"
"    if (__builtin_add_overflow(a.i, b.i, &r)) nv_die_numeric_overflow();\n"
"    return nv_int(r);\n"
"}\n"
"static inline NV nv_sub(NV a, NV b) {\n"
"    if (a.t == NV_FLT || b.t == NV_FLT) return nv_flt(nv_asf(a) - nv_asf(b));\n"
"    long long r;\n"
"    if (__builtin_sub_overflow(a.i, b.i, &r)) nv_die_numeric_overflow();\n"
"    return nv_int(r);\n"
"}\n"
"static inline NV nv_mul(NV a, NV b) {\n"
"    if (a.t == NV_FLT || b.t == NV_FLT) return nv_flt(nv_asf(a) * nv_asf(b));\n"
"    long long r;\n"
"    if (__builtin_mul_overflow(a.i, b.i, &r)) nv_die_numeric_overflow();\n"
"    return nv_int(r);\n"
"}\n"
"/* The interpreter yields an int when the division is exact and a float\n"
"   otherwise (4/2 prints 2, 7/2 prints 3.5, 6/4 prints 1.5). */\n"
"static inline NV nv_div(NV a, NV b) {\n"
"    if (a.t == NV_FLT || b.t == NV_FLT) return nv_flt(nv_asf(a) / nv_asf(b));\n"
"    if (b.i == 0) nv_die_division_by_zero();\n"
"    if (b.i != -1 && a.i % b.i == 0) return nv_int(a.i / b.i);\n"
"    return nv_flt((double)a.i / (double)b.i);\n"
"}\n"
"static inline NV nv_mod(NV a, NV b) {\n"
"    long long x = nv_asi(a), y = nv_asi(b);\n"
"    if (y == 0) nv_die_division_by_zero();\n"
"    if (y == -1) return nv_int(0);          /* x % -1 is 0, and LLONG_MIN % -1 is UB */\n"
"    return nv_int(x % y);\n"
"}\n"
"\n"
"static inline NV nv_lt(NV a, NV b) { return nv_boo(a.t == NV_FLT || b.t == NV_FLT ? nv_asf(a) <  nv_asf(b) : a.i <  b.i); }\n"
"static inline NV nv_le(NV a, NV b) { return nv_boo(a.t == NV_FLT || b.t == NV_FLT ? nv_asf(a) <= nv_asf(b) : a.i <= b.i); }\n"
"static inline NV nv_gt(NV a, NV b) { return nv_boo(a.t == NV_FLT || b.t == NV_FLT ? nv_asf(a) >  nv_asf(b) : a.i >  b.i); }\n"
"static inline NV nv_ge(NV a, NV b) { return nv_boo(a.t == NV_FLT || b.t == NV_FLT ? nv_asf(a) >= nv_asf(b) : a.i >= b.i); }\n"
"/* Equality mirrors the interpreter's val_eq exactly (docs/AUDIT.md §1.14):\n"
"   same tag compares in that tag, and only a MIXED numeric pair is promoted to\n"
"   double.  The old rule -- 'either side is a bool -> compare as integers, else\n"
"   compare as doubles' -- was wrong twice.  `true == 1.5` truncated 1.5 through\n"
"   nv_asi and answered true where the interpreter answers false, and two ints\n"
"   above 2^53 rounded to the same double, so 9007199254740993 == 9007199254740992\n"
"   answered true here and false in both other backends.  A mixed pair still goes\n"
"   through double, so `1 == 1.0` and `true == 1` stay true. */\n"
"static inline NV nv_eq(NV a, NV b) {\n"
"    if (a.t == b.t) return nv_boo(a.t == NV_FLT ? a.f == b.f : a.i == b.i);\n"
"    return nv_boo(nv_asf(a) == nv_asf(b));\n"
"}\n"
"static inline NV nv_ne(NV a, NV b) {\n"
"    if (a.t == b.t) return nv_boo(a.t == NV_FLT ? a.f != b.f : a.i != b.i);\n"
"    return nv_boo(nv_asf(a) != nv_asf(b));\n"
"}\n"
"\n"
"/* `say` prints ints, bools and floats exactly as the interpreter does.\n"
"   Floats go through nv_fmt_double, a port of `vts_double` in src/vm/vm.c.\n"
"   That format is fully deterministic: integral floats print as integers, a\n"
"   float within +-1e15 prints as up to six decimals with trailing zeros\n"
"   trimmed (0.9999999 carries to \"1\", -0.5 keeps its sign, 1e-20 rounds to\n"
"   \"0\"), and anything larger prints as %.17g.  `say` used plain %g here,\n"
"   which lost digits the interpreter keeps, so printed floats diverged. */\n"
"static void nv_fmt_double(char *buf, size_t bufsz, double dv) {\n"
"    if (dv != dv) { snprintf(buf, bufsz, \"nan\"); return; }\n"
"    if (dv == 0.0) { snprintf(buf, bufsz, \"0\"); return; }   /* also -0.0 */\n"
"    if (dv >= -9223372036854775808.0 && dv < 9223372036854775808.0\n"
"        && dv == (double)(long long)dv) {\n"
"        snprintf(buf, bufsz, \"%lld\", (long long)dv);\n"
"        return;\n"
"    }\n"
"    if (dv >= 1e15 || dv <= -1e15) { snprintf(buf, bufsz, \"%.17g\", dv); return; }\n"
"    int neg = dv < 0;\n"
"    double a = neg ? -dv : dv;                 /* |dv| < 1e15 here */\n"
"    long long ip = (long long)a;\n"
"    long long frac = (long long)((a - (double)ip) * 1000000.0 + 0.5);\n"
"    if (frac == 1000000) { ip += 1; frac = 0; }   /* 0.9999999 prints \"1\", not \"0.1\" */\n"
"    size_t pos = 0;\n"
"    if (neg && ip == 0 && pos < bufsz - 1) buf[pos++] = '-';\n"
"    {\n"
"        char ib[32]; snprintf(ib, sizeof ib, \"%lld\", neg ? -ip : ip);\n"
"        for (int i = 0; ib[i] && pos < bufsz - 1; i++) buf[pos++] = ib[i];\n"
"    }\n"
"    if (frac == 0) { buf[pos] = '\\0'; return; }   /* no dangling '.' when it rounds away */\n"
"    if (pos < bufsz - 1) buf[pos++] = '.';\n"
"    buf[pos] = '\\0';\n"
"    char fb[32];\n"
"    snprintf(fb, sizeof fb, \"%06lld\", frac);\n"
"    int flen = (int)strlen(fb);\n"
"    while (flen > 0 && fb[flen - 1] == '0') fb[--flen] = '\\0';\n"
"    snprintf(buf + pos, bufsz - pos, \"%s\", fb);\n"
"}\n"
"static void nv_say(NV a) {\n"
"    if (a.t == NV_BOO)      printf(\"%s\\n\", a.i ? \"true\" : \"false\");\n"
"    else if (a.t == NV_FLT) { char b[64]; nv_fmt_double(b, sizeof b, a.f); printf(\"%s\\n\", b); }\n"
"    else                    printf(\"%lld\\n\", a.i);\n"
"}\n"
"\n";

/* -------------------------------------------------------------- pre-passes */

static void collect_locals(Gen *g, Stmt **body, int n);

static void collect_locals_stmt(Gen *g, Stmt *s) {
    if (!s || g->failed) return;
    switch (s->type) {
    case STMT_ASSIGN:
        if (s->assignStmt.target && s->assignStmt.target->type == EXPR_IDENT) {
            StringView n = s->assignStmt.target->identName;
            if (!sym_find(g, n)) local_add(g, n);
        }
        break;
    case STMT_IF:
        collect_locals(g, s->ifStmt.thenBody, s->ifStmt.thenCount);
        collect_locals(g, s->ifStmt.elseBody, s->ifStmt.elseCount);
        break;
    case STMT_WHILE:  collect_locals(g, s->whileStmt.body, s->whileStmt.bodyCount); break;
    case STMT_REPEAT: collect_locals(g, s->repeatStmt.body, s->repeatStmt.bodyCount); break;
    default: break;
    }
}

static void collect_locals(Gen *g, Stmt **body, int n) {
    for (int i = 0; i < n && !g->failed; i++) collect_locals_stmt(g, body[i]);
}

/* Top-level assignment targets become globals. */
static void collect_globals(Gen *g, Stmt **body, int n) {
    for (int i = 0; i < n && !g->failed; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        if (s->type == STMT_GLOBAL) {
            for (int k = 0; k < s->globalStmt.nameCount; k++)
                sym_add(g, s->globalStmt.names[k], SYM_GLOBAL, 0);
        } else if (s->type == STMT_ASSIGN && s->assignStmt.target &&
                   s->assignStmt.target->type == EXPR_IDENT) {
            sym_add(g, s->assignStmt.target->identName, SYM_GLOBAL, 0);
        } else if (s->type == STMT_FUNC) {
            sym_add(g, s->funcDef.name, SYM_FUNC, s->funcDef.paramCount);
        }
    }
}

/* ---------------------------------------------------------------- emission */

static void emit_expr(Gen *g, Buf *b, Expr *e);

static const char *binop_c(InimerseTokenType op) {
    switch (op) {
    case TOK_PLUS:    return "nv_add";
    case TOK_MINUS:   return "nv_sub";
    case TOK_STAR:    return "nv_mul";
    case TOK_SLASH:   return "nv_div";
    case TOK_PERCENT: return "nv_mod";
    case TOK_LT:      return "nv_lt";
    case TOK_LE:      return "nv_le";
    case TOK_GT:      return "nv_gt";
    case TOK_GE:      return "nv_ge";
    case TOK_EQEQ:    return "nv_eq";
    case TOK_NEQ:     return "nv_ne";
    default:          return NULL;
    }
}

static void emit_var(Gen *g, Buf *b, StringView name) {
    Sym *s = sym_find(g, name);
    if (s) {
        if (s->kind == SYM_FUNC) {
            fail(g, "'%.*s' names a function, not a value", name.length, name.start);
            buf_str(b, "nv_int(0)");
            return;
        }
        buf_str(b, s->cname);
        return;
    }
    if (local_find(g, name) >= 0) { buf_fmt(b, "v_%.*s", name.length, name.start); return; }
    fail(g, "variable '%.*s' is used but never assigned in this program", name.length, name.start);
    buf_str(b, "nv_int(0)");
}

static void emit_expr(Gen *g, Buf *b, Expr *e) {
    if (!e || g->failed) { buf_str(b, "nv_int(0)"); return; }
    switch (e->type) {
    case EXPR_NUMBER:
        buf_fmt(b, "nv_int(%lldLL)", (long long)e->intVal);
        return;
    case EXPR_FLOAT:
        buf_fmt(b, "nv_flt(%.17g)", e->floatVal);
        return;
    case EXPR_BOOL:
        buf_fmt(b, "nv_boo(%d)", e->boolVal ? 1 : 0);
        return;
    case EXPR_IDENT:
        emit_var(g, b, e->identName);
        return;
    case EXPR_UNARY:
        if (e->unary.op == TOK_MINUS) { buf_str(b, "nv_neg("); emit_expr(g, b, e->unary.operand); buf_str(b, ")"); return; }
        if (e->unary.op == TOK_NOT)   { buf_str(b, "nv_not("); emit_expr(g, b, e->unary.operand); buf_str(b, ")"); return; }
        fail(g, "unary operator %d is outside the numeric subset", (int)e->unary.op);
        return;
    case EXPR_BINARY: {
        InimerseTokenType op = e->binary.op;
        if (op == TOK_AND || op == TOK_OR) {
            /* C's && and || short-circuit exactly as the interpreter's do, so
               the right operand is not evaluated when the left decides it. */
            buf_str(b, "nv_boo(nv_tru(");
            emit_expr(g, b, e->binary.left);
            buf_str(b, op == TOK_AND ? ") && nv_tru(" : ") || nv_tru(");
            emit_expr(g, b, e->binary.right);
            buf_str(b, "))");
            return;
        }
        const char *fn = binop_c(op);
        if (!fn) { fail(g, "binary operator %d is outside the numeric subset", (int)op); buf_str(b, "nv_int(0)"); return; }
        buf_fmt(b, "%s(", fn);
        emit_expr(g, b, e->binary.left);
        buf_str(b, ", ");
        emit_expr(g, b, e->binary.right);
        buf_str(b, ")");
        return;
    }
    case EXPR_CHAIN_COMPARE: {
        /* The parser routes every comparison, including a plain `i < n`,
           through a chain node rather than EXPR_BINARY.  `a < b < c` means
           `a < b AND b < c`, exactly as in the interpreter. */
        int n = e->chain.count;
        if (n < 2) { fail(g, "malformed comparison chain"); buf_str(b, "nv_int(0)"); return; }
        const char *fn0 = binop_c(e->chain.ops[0]);
        if (!fn0) { fail(g, "comparison operator %d is outside the numeric subset", (int)e->chain.ops[0]); buf_str(b, "nv_int(0)"); return; }
        if (n == 2) {
            buf_fmt(b, "%s(", fn0);
            emit_expr(g, b, e->chain.operands[0]);
            buf_str(b, ", ");
            emit_expr(g, b, e->chain.operands[1]);
            buf_str(b, ")");
            return;
        }
        buf_str(b, "nv_boo(");
        for (int i = 0; i + 1 < n; i++) {
            const char *fn = binop_c(e->chain.ops[i]);
            if (!fn) { fail(g, "comparison operator %d is outside the numeric subset", (int)e->chain.ops[i]); return; }
            if (i) buf_str(b, " && ");
            buf_str(b, "nv_tru(");
            buf_fmt(b, "%s(", fn);
            emit_expr(g, b, e->chain.operands[i]);
            buf_str(b, ", ");
            emit_expr(g, b, e->chain.operands[i + 1]);
            buf_str(b, "))");
        }
        buf_str(b, ")");
        return;
    }
    case EXPR_CALL: {
        if (!e->call.callee || e->call.callee->type != EXPR_IDENT) {
            fail(g, "only direct calls to user functions are supported by the numeric subset");
            buf_str(b, "nv_int(0)");
            return;
        }
        Sym *s = sym_find(g, e->call.callee->identName);
        if (!s || s->kind != SYM_FUNC) {
            fail(g, "call to '%.*s' is not a user-defined function in this program",
                 e->call.callee->identName.length, e->call.callee->identName.start);
            buf_str(b, "nv_int(0)");
            return;
        }
        if (e->call.argCount != s->arity) {
            fail(g, "function '%s' takes %d argument(s) but is called with %d",
                 s->name, s->arity, e->call.argCount);
            buf_str(b, "nv_int(0)");
            return;
        }
        buf_fmt(b, "%s(", s->cname);
        for (int i = 0; i < e->call.argCount; i++) {
            if (i) buf_str(b, ", ");
            emit_expr(g, b, e->call.args[i]);
        }
        buf_str(b, ")");
        return;
    }
    default:
        fail(g, "expression kind %d is outside the numeric subset", (int)e->type);
        buf_str(b, "nv_int(0)");
        return;
    }
}

static void emit_indent(Buf *b, int n) { for (int i = 0; i < n; i++) buf_str(b, "    "); }

static void emit_stmt(Gen *g, Buf *b, Stmt *s, int ind);

static void emit_body(Gen *g, Buf *b, Stmt **body, int n, int ind) {
    for (int i = 0; i < n && !g->failed; i++) emit_stmt(g, b, body[i], ind);
}

static void emit_stmt(Gen *g, Buf *b, Stmt *s, int ind) {
    if (!s || g->failed) return;
    switch (s->type) {
    case STMT_ASSIGN: {
        Expr *t = s->assignStmt.target;
        if (!t || t->type != EXPR_IDENT) {
            fail(g, "only assignment to a plain name is supported by the numeric subset");
            return;
        }
        emit_indent(b, ind);
        emit_var(g, b, t->identName);
        buf_str(b, " = ");
        emit_expr(g, b, s->assignStmt.value);
        buf_str(b, ";\n");
        return;
    }
    case STMT_SAY:
        emit_indent(b, ind);
        buf_str(b, "nv_say(");
        emit_expr(g, b, s->sayStmt.message);
        buf_str(b, ");\n");
        return;
    case STMT_EXPR:
        emit_indent(b, ind);
        buf_str(b, "(void)");
        emit_expr(g, b, s->exprStmt.expr);
        buf_str(b, ";\n");
        return;
    case STMT_RETURN:
        emit_indent(b, ind);
        if (s->returnStmt.value) {
            buf_str(b, "return ");
            emit_expr(g, b, s->returnStmt.value);
            buf_str(b, ";\n");
        } else {
            buf_str(b, "return nv_int(0);\n");
        }
        return;
    case STMT_BREAK:
        emit_indent(b, ind);
        buf_str(b, "break;\n");
        return;
    case STMT_IF:
        emit_indent(b, ind);
        buf_str(b, "if (nv_tru(");
        emit_expr(g, b, s->ifStmt.condition);
        buf_str(b, ")) {\n");
        emit_body(g, b, s->ifStmt.thenBody, s->ifStmt.thenCount, ind + 1);
        emit_indent(b, ind);
        if (s->ifStmt.elseCount > 0) {
            buf_str(b, "} else {\n");
            emit_body(g, b, s->ifStmt.elseBody, s->ifStmt.elseCount, ind + 1);
            emit_indent(b, ind);
        }
        buf_str(b, "}\n");
        return;
    case STMT_WHILE:
        emit_indent(b, ind);
        buf_str(b, "while (nv_tru(");
        emit_expr(g, b, s->whileStmt.condition);
        buf_str(b, ")) {\n");
        emit_body(g, b, s->whileStmt.body, s->whileStmt.bodyCount, ind + 1);
        emit_indent(b, ind);
        buf_str(b, "}\n");
        return;
    case STMT_REPEAT: {
        buf_fmt(b, "%*s{ long long nv_n = nv_asi(", ind * 4, "");
        emit_expr(g, b, s->repeatStmt.count);
        buf_str(b, "); for (long long nv_r = 0; nv_r < nv_n; nv_r++) {\n");
        emit_body(g, b, s->repeatStmt.body, s->repeatStmt.bodyCount, ind + 1);
        emit_indent(b, ind);
        buf_str(b, "} }\n");
        return;
    }
    case STMT_FUNC:
    case STMT_GLOBAL:
        return;                     /* hoisted */
    case STMT_BLOCK_DEF:
        return;                     /* block/prototype declarations carry no numeric behaviour */
    default:
        fail(g, "statement kind %d is outside the numeric subset", (int)s->type);
        return;
    }
}

/* --------------------------------------------------------------- driver */

static const char *kSubset =
    "int/float/bool values; + - * / %, < <= > >= == !=, and/or/not; "
    "if/else, while, repeat, break; assignment; user functions; globals; say";

const char *aot_native_subset_description(void) { return kSubset; }

static char g_err[256] = "no error";

const char *aot_native_last_error(void) { return g_err; }

int aot_native_translate_ex(Program *prog, const char *out_path,
                            const char *extern_name, const char *entry_name) {
    Gen g;
    memset(&g, 0, sizeof g);
    g_err[0] = '\0';

    if (!prog) { snprintf(g_err, sizeof g_err, "no program"); return 0; }
    if (!out_path || !*out_path) { snprintf(g_err, sizeof g_err, "no output path"); return 0; }
    g.extern_name = (extern_name && *extern_name) ? extern_name : NULL;
    g.entry_name = (entry_name && *entry_name) ? entry_name : "main";

    collect_globals(&g, prog->stmts, prog->count);

    /* A harness-supplied global is legal to READ without ever being assigned,
       so register it before anything walks the program. */
    if (g.extern_name) {
        StringView sv;
        sv.start = g.extern_name;
        sv.length = (int)strlen(g.extern_name);
        sym_add(&g, sv, SYM_GLOBAL, 0);
    }

    Buf b;
    buf_init(&b);
    if (!b.d) { snprintf(g_err, sizeof g_err, "out of memory"); return 0; }

    buf_str(&b, kPreamble);
    buf_str(&b, "\n");

    /* globals */
    for (int i = 0; i < g.nsyms && !g.failed; i++) {
        if (g.syms[i].kind != SYM_GLOBAL) continue;
        if (g.extern_name && strcmp(g.syms[i].name, g.extern_name) == 0)
            buf_fmt(&b, "extern NV %s;   /* supplied by the linking harness */\n",
                    g.syms[i].cname);
        else
            buf_fmt(&b, "static NV %s = { NV_INT, 0, 0 };\n", g.syms[i].cname);
    }
    buf_str(&b, "\n");

    /* forward declarations so functions may call each other in any order */
    for (int i = 0; i < g.nsyms && !g.failed; i++) {
        if (g.syms[i].kind != SYM_FUNC) continue;
        buf_fmt(&b, "static NV %s(", g.syms[i].cname);
        if (g.syms[i].arity == 0) {
            buf_str(&b, "void");
        } else {
            for (int k = 0; k < g.syms[i].arity; k++)
                buf_fmt(&b, "%sNV", k ? ", " : "");
        }
        buf_str(&b, ");\n");
    }
    buf_str(&b, "\n");

    /* function bodies */
    for (int i = 0; i < prog->count && !g.failed; i++) {
        Stmt *s = prog->stmts[i];
        if (!s || s->type != STMT_FUNC) continue;
        Sym *sym = sym_find(&g, s->funcDef.name);
        if (!sym) continue;

        g.nlocals = 0;
        for (int k = 0; k < s->funcDef.paramCount; k++) local_add(&g, s->funcDef.params[k]);
        int nparams = g.nlocals;   /* parameters arrive as C arguments, not locals */
        collect_locals(&g, s->funcDef.body, s->funcDef.bodyCount);

        buf_fmt(&b, "static NV %s(", sym->cname);
        if (s->funcDef.paramCount == 0) {
            buf_str(&b, "void");
        } else {
            for (int k = 0; k < s->funcDef.paramCount; k++)
                buf_fmt(&b, "%sNV v_%.*s", k ? ", " : "",
                        s->funcDef.params[k].length, s->funcDef.params[k].start);
        }
        buf_str(&b, ") {\n");
        for (int k = nparams; k < g.nlocals; k++)
            buf_fmt(&b, "    NV v_%s = { NV_INT, 0, 0 };\n", g.locals[k]);
        emit_body(&g, &b, s->funcDef.body, s->funcDef.bodyCount, 1);
        buf_str(&b, "    return nv_int(0);\n}\n\n");
    }

    /* top level becomes the entry point */
    g.nlocals = 0;
    collect_locals(&g, prog->stmts, prog->count);
    buf_fmt(&b, "int %s(void) {\n", g.entry_name);
    for (int k = 0; k < g.nlocals; k++)
        buf_fmt(&b, "    NV v_%s = { NV_INT, 0, 0 };\n", g.locals[k]);
    emit_body(&g, &b, prog->stmts, prog->count, 1);
    buf_str(&b, "    return 0;\n}\n");

    int ok = !g.failed;
    if (ok) {
        FILE *fp = fopen(out_path, "wb");
        if (!fp) {
            snprintf(g_err, sizeof g_err, "cannot write '%s'", out_path);
            ok = 0;
        } else {
            fwrite(b.d, 1, b.len, fp);
            fclose(fp);
        }
    }
    buf_free(&b);

    if (!ok) {
        if (g.failed) snprintf(g_err, sizeof g_err, "%s", g.err);
        return 0;
    }
    snprintf(g_err, sizeof g_err, "no error");
    return 1;
}

int aot_native_translate(Program *prog, const char *out_path) {
    return aot_native_translate_ex(prog, out_path, NULL, "main");
}
