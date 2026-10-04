/* parser_member_safe_probe -- the `.` and `?.` member paths must not share
   state through uninitialised memory.

   Background. `struct Expr` (src/parser/ast.h) is a tag plus a union, and the
   plain `.` path in src/parser/parser.c used to allocate its node with
   `malloc`, writing only `type`, `member.object` and `member.member`.
   `member.safe` was therefore whatever byte the allocator handed back. When
   that byte happened to be non-zero, src/compiler/compiler.c took the
   `expr->member.safe` branch and compiled `a.b` as an *index get* -- so a
   dotted global (for instance a parameter named `player.max_hp`) silently
   evaluated to nil. The failure looked like a file-size threshold because the
   byte depended on the allocator's history.

   The fix is that every AST node is allocated zeroed, so `safe` is false by
   construction on the `.` path and explicitly true on the `?.` path.

   This probe does not rely on the allocator's history: it poisons the heap
   with a non-zero pattern first, so a node that is not zero-initialised is
   guaranteed to carry a non-zero `safe` and the assertion fails loudly. That
   is what makes this a regression pin rather than a coincidence. */

#include "ast.h"
#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

/* Fill the small-object heap with 0x01 so that any allocation which is not
   zeroed comes back non-zero. Same size class as an AST node, freed in LIFO
   order so the very next allocation of that size reuses poisoned memory. */
static void poison_heap(size_t n, int rounds) {
    void **blocks = (void **)malloc(sizeof(void *) * (size_t)rounds);
    if (!blocks) return;
    for (int i = 0; i < rounds; i++) {
        blocks[i] = malloc(n);
        if (blocks[i]) memset(blocks[i], 0x01, n);
    }
    for (int i = 0; i < rounds; i++) free(blocks[i]);
    free(blocks);
}

static Expr *first_expr(const char *source) {
    Program *prog = parse_program(source);
    if (!prog || prog->count < 1) return NULL;
    if (prog->stmts[0]->type != STMT_EXPR) return NULL;
    return prog->stmts[0]->exprStmt.expr;
}

static void check(const char *source, const char *what, int want_safe, Expr *got) {
    if (!got || got->type != EXPR_MEMBER) {
        printf("FAIL %-10s %-28s expected an EXPR_MEMBER, got %s\n",
               source, what, got ? "another node kind" : "no expression");
        failures++;
        return;
    }
    int safe = got->member.safe ? 1 : 0;
    if (safe != want_safe) {
        printf("FAIL %-10s %-28s safe=%d want %d\n", source, what, safe, want_safe);
        failures++;
        return;
    }
    printf("ok   %-10s %-28s safe=%d\n", source, what, safe);
}

int main(void) {
    /* Poison before every parse: `parse_program` allocates fresh nodes each
       time, so each case gets its own poisoned heap. */
    Expr *e;

    poison_heap(sizeof(Expr), 128);
    e = first_expr("x.y");
    check("x.y", "plain dot is not safe", 0, e);
    if (e) {
        int len = (int)e->member.member.length;
        if (len != 1 || e->member.member.start[0] != 'y') {
            printf("FAIL %-10s %-28s member='%.*s'\n", "x.y", "member name is 'y'",
                   len, e->member.member.start);
            failures++;
        } else {
            printf("ok   %-10s %-28s member='y'\n", "x.y", "member name is 'y'");
        }
    }

    poison_heap(sizeof(Expr), 128);
    e = first_expr("x?.y");
    check("x?.y", "safe access is safe", 1, e);

    poison_heap(sizeof(Expr), 128);
    e = first_expr("x.y.z");
    check("x.y.z", "outer dot is not safe", 0, e);
    if (e) check("x.y.z", "inner dot is not safe", 0, e->member.object);

    poison_heap(sizeof(Expr), 128);
    e = first_expr("x?.y.z");
    check("x?.y.z", "outer dot is not safe", 0, e);
    if (e) check("x?.y.z", "inner safe access is safe", 1, e->member.object);

    poison_heap(sizeof(Expr), 128);
    e = first_expr("x.y?.z");
    check("x.y?.z", "outer safe access is safe", 1, e);
    if (e) check("x.y?.z", "inner dot is not safe", 0, e->member.object);

    /* A non-member expression must still parse, so the probe fails on a
       parser that stopped working rather than passing vacuously. */
    poison_heap(sizeof(Expr), 128);
    e = first_expr("x[0]");
    if (!e || e->type != EXPR_INDEX) {
        printf("FAIL %-10s %-28s expected an EXPR_INDEX\n", "x[0]", "index is an index");
        failures++;
    } else {
        printf("ok   %-10s %-28s\n", "x[0]", "index is an index");
    }

    if (failures) {
        printf("parser_member_safe_probe: %d failure(s)\n", failures);
        return 1;
    }
    printf("parser_member_safe_probe: ok\n");
    return 0;
}
