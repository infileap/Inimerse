/* lint_mod.c - static linter for .im scripts (deterministic rules).
 *
 * Rules derived from measured benchmarks (2026-08-16):
 *   [SEVERE] arr = arr + [x]  -> array '+' concatenation silently yields empty
 *   [SEVERE] task/thread defined inside a loop/if block -> body never runs
 *   [SEVERE] s = s + x inside a loop -> O(n^2) string concat disaster
 *   [INFO]   global read/write in hot loop (27%-58% slower than local)
 *   [INFO]   for v in arr iterator (44% slower than index)
 *   [INFO]   atomic_add in single-thread context (3.5x slower)
 *   [INFO]   len(arr) re-evaluated in loop condition (call overhead)
 *
 * Text-level scanning (line numbers preserved; strings/comments skipped).
 * Heuristic: may produce false positives, warnings never block execution.
 */
#include "vm.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LINT_MAX_WARN 64

typedef struct {
    int count;
    char lines[LINT_MAX_WARN][200];
} LintBuf;

/* strip strings and comments from a line (stateful across lines for #[...]) */
static void lint_strip(char *dst, const char *src, int *in_block) {
    int d = 0;
    const char *p = src;
    while (*p) {
        if (*in_block) {
            if (p[0] == ']') { *in_block = 0; p++; continue; }
            p++;
            continue;
        }
        if (p[0] == '#' && p[1] == '[') { *in_block = 1; p += 2; continue; }
        if (p[0] == '#') { break; }               /* line comment */
        if (p[0] == '/' && p[1] == '/') { break; } /* line comment */
        if (p[0] == '"' || p[0] == '\'') {         /* string literal */
            char q = p[0];
            p++;
            while (*p && *p != q) p++;
            if (*p) p++;
            continue;
        }
        dst[d++] = *p;
        p++;
    }
    dst[d] = 0;
}

static void lint_add(LintBuf *lb, int ln, const char *tag, const char *msg) {
    if (lb->count >= LINT_MAX_WARN) return;
    snprintf(lb->lines[lb->count], sizeof lb->lines[lb->count], "[lint] line %d [%s] %s", ln, tag, msg);
    lb->count++;
}

/* does line contain a loop opener (while/for/repeat) or if/else? */
static int line_opens_block(const char *s, int *is_loop) {
    if (strncmp(s, "while", 5) == 0 && !isalnum((unsigned char)s[5])) { *is_loop = 1; return 1; }
    if (strncmp(s, "for", 3) == 0 && !isalnum((unsigned char)s[3])) { *is_loop = 1; return 1; }
    if (strncmp(s, "repeat", 6) == 0 && !isalnum((unsigned char)s[6])) { *is_loop = 1; return 1; }
    if (strncmp(s, "if", 2) == 0 && !isalnum((unsigned char)s[2])) { *is_loop = 0; return 1; }
    if (strncmp(s, "else", 4) == 0 && !isalnum((unsigned char)s[4])) { *is_loop = 0; return 1; }
    return 0;
}

static int line_is_task_thread(const char *s) {
    if (strncmp(s, "task", 4) == 0 && !isalnum((unsigned char)s[4])) return 1;
    if (strncmp(s, "thread", 6) == 0 && !isalnum((unsigned char)s[6])) return 1;
    return 0;
}

/* main scan: one pass over cleaned lines */
static int lint_scan(const char *path, LintBuf *lb) {
    FILE *f = fopen(path, "rb");
    if (!f) { lint_add(lb, 0, "IO", "cannot read script"); return -1; }
    char raw[8192];
    int ln = 0, in_block = 0;
    int block_depth = 0;          /* brace depth (cleaned lines only) */
    int in_loop = 0;              /* currently inside a loop block */
    int loop_brace = -1;          /* brace depth at loop open */
    int case_brace = -1;
    int case_wildcard_line = 0;
    int case_start_line = 0;
    char prev_clean[4096] = "";
    while (fgets(raw, sizeof raw, f)) {
        ln++;
        char clean[4096];
        lint_strip(clean, raw, &in_block);
        /* trim */
        char *s = clean;
        while (*s == ' ' || *s == '\t') s++;
        int len = (int)strlen(s);
        while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = 0;
        if (!*s) { strcpy(prev_clean, s); continue; }

        /* update loop context: count braces first */
        int opens = 0, closes = 0;
        for (const char *b = s; *b; b++) { if (*b == '{') opens++; if (*b == '}') closes++; }
        if (strncmp(s, "case ", 5) == 0 && strchr(s, '{')) {
            case_brace = block_depth;
            case_wildcard_line = 0;
            case_start_line = ln;
        } else if (case_brace >= 0 && block_depth == case_brace + 1) {
            if ((s[0] == '_' && s[1] == ':' ) || strncmp(s, "else:", 5) == 0)
                case_wildcard_line = ln;
            else if (case_wildcard_line && strchr(s, ':')) {
                lint_add(lb, ln, "WARN",
                    "case branch is unreachable: wildcard '_'/'else' appears before this branch");
            }
        }
        if (case_brace >= 0 && closes > 0 && block_depth - closes <= case_brace) {
            if (!case_wildcard_line)
                lint_add(lb, case_start_line, "WARN",
                    "case has no wildcard '_'/'else' branch; exhaustive coverage cannot be proven for open or infinite sets");
            case_brace = -1;
            case_wildcard_line = 0;
            case_start_line = 0;
        }
        if (closes > 0 && in_loop && block_depth - closes < loop_brace) in_loop = 0;

        /* rule: task/thread defined inside a block */
        if (line_is_task_thread(s)) {
            if (block_depth > 0) {
                lint_add(lb, ln, "SEVERE",
                    "task/thread defined inside a block: body may never run - move definition to top level, start/join in loop");
            }
        }
        /* rule: array + [x] concatenation */
        if (strstr(s, "+ [") && strchr(s, '=')) {
            lint_add(lb, ln, "SEVERE",
                "array '+' concatenation (arr = arr + [x]) silently yields empty - use push(arr, x)");
        }
        /* rule: string concat in loop (s = s + ...) */
        if (in_loop && strstr(s, "= s +")) {
            lint_add(lb, ln, "SEVERE",
                "string concat in loop is O(n^2) - collect parts in array then join, or use a local buffer");
        }
        if (in_loop && strstr(s, "= s +") == NULL) {
            /* generic self-assign += in loop (global/locals) */
            if (strstr(s, "= ") && strstr(s, " + 1") && strstr(s, " +1") == NULL) {
                /* too noisy: skip generic pattern */
            }
        }
        /* rule: global-ish self assign in loop */
        if (in_loop && (strstr(s, " + 1") || strstr(s, " +1")) && strstr(s, "= ")) {
            lint_add(lb, ln, "INFO",
                "counter in loop: globals are 27%-58% slower than locals - accumulate in a local, write back after loop");
        }
        /* rule: iterator */
        if (strncmp(s, "for ", 4) == 0 && strstr(s, " in ") && !strstr(s, " in range") && !strstr(s, "..")) {
            lint_add(lb, ln, "INFO",
                "for v in a iterator is ~44% slower than indexed loop - prefer for i in 0..len(a) in hot paths");
        }
        /* rule: atomic in single-thread context */
        if (strstr(s, "atomic_add(") || strstr(s, "atomic_set(")) {
            lint_add(lb, ln, "INFO",
                "atomic ops are 3.5x slower than plain assignment - only needed for cross-thread shared counters");
        }
        /* rule: len() in loop condition */
        if ((strncmp(s, "while ", 6) == 0 || strncmp(s, "for ", 4) == 0) && strstr(s, "len(")) {
            lint_add(lb, ln, "INFO",
                "len(arr) in loop condition is re-evaluated each pass - cache it: n = len(arr)");
        }
        /* rule: line starts with a continuation token after a non-block line
           -> newline is just a space, so this line merges into the previous statement
           (classic AI-generated multi-line style: x = f\n  (3) or x = 5\n  + 3) */
        if (prev_clean[0] && (s[0] == '(' || s[0] == '+' || s[0] == '-' || s[0] == '*' || s[0] == '/' || s[0] == '.')) {
            int plen = (int)strlen(prev_clean);
            if (plen > 0 && prev_clean[plen - 1] != '{' && prev_clean[plen - 1] != '}') {
                lint_add(lb, ln, "WARN",
                    "line starts with a continuation token: newline is a space, so it merges into the previous statement (x = f\\n(3) becomes f(3)) - put the operator/paren at the end of the previous line");
            }
        }

        /* open new block context */
        int is_loop = 0;
        if (line_opens_block(s, &is_loop)) {
            if (opens > 0) { in_loop = is_loop; loop_brace = block_depth; }
            else if (is_loop) { in_loop = 1; loop_brace = block_depth; } /* single-statement loop body */
            else if (!is_loop) { /* if/else: not a loop */ }
        }
        block_depth += opens - closes;
        if (block_depth < 0) block_depth = 0;
        if (in_loop && block_depth <= loop_brace && !is_loop) { /* loop block ended without close? */ }
        strcpy(prev_clean, s);
    }
    fclose(f);
    return lb->count;
}

int lint_check(const char *path, char *out, int cap) {
    LintBuf lb;
    memset(&lb, 0, sizeof lb);
    int n = lint_scan(path, &lb);
    if (n < 0) { snprintf(out, cap, "lint: cannot read %s", path ? path : "?"); return -1; }
    int used = 0;
    for (int i = 0; i < lb.count && used < cap - 2; i++) {
        int need = (int)strlen(lb.lines[i]) + 2;
        if (used + need >= cap) break;
        memcpy(out + used, lb.lines[i], strlen(lb.lines[i]));
        used += (int)strlen(lb.lines[i]);
        out[used++] = '\n';
    }
    out[used] = 0;
    return lb.count;
}

/* builtin: lint_check(script_path) -> int warnings count (also prints) */
static int builtin_lint_check(VM *vm) {
    const char *path = "";
    if (vm_cur_sp(vm) >= 0) {
        Value a = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (a.type == VAL_STRING && a.sval) path = a.sval;
    }
    vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    char buf[8192];
    int n = lint_check(path, buf, sizeof buf);
    if (n > 0) fprintf(stderr, "%s", buf);
    Value v; v.type = VAL_INT; v.ival = n < 0 ? -1 : n; v.fval = 0; v.sval = NULL;
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
    vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
    return 1;
}

void lint_mod_register(VM *vm) {
    vm_register_builtin(vm, "lint_check", builtin_lint_check);
}
