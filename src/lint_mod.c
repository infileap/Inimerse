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

typedef struct {
    char name[64];
    char members[32][96];
    int count;
} LintFiniteType;

typedef struct {
    char variant[8];
    char type_name[64];
    char member[96];
    int complete;
} LintResultCoverage;

static int lint_extract_quoted(const char *s, char out[][96], int cap) {
    int n = 0;
    while (*s && n < cap) {
        if (*s != '"' && *s != '\'') { s++; continue; }
        char q = *s++;
        int k = 0;
        while (*s && *s != q && k < 95) out[n][k++] = *s++;
        out[n][k] = 0;
        if (*s == q) s++;
        if (k > 0) n++;
    }
    return n;
}

static LintFiniteType *lint_find_type(LintFiniteType *types, int count, const char *name) {
    for (int i = 0; i < count; i++) if (strcmp(types[i].name, name) == 0) return &types[i];
    return NULL;
}

static LintFiniteType *lint_find_unique_type_for_member(LintFiniteType *types, int count,
                                                        const char *member) {
    LintFiniteType *found = NULL;
    if (!member || !*member) return NULL;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < types[i].count; j++) {
            if (strcmp(types[i].members[j], member) != 0) continue;
            if (found) return NULL; /* ambiguous: stay conservative */
            found = &types[i];
            break;
        }
    }
    return found;
}

static int lint_extract_case_try_coverage(const char *s, const char *raw,
                                          char *variant, size_t variant_cap,
                                          char *type_name, size_t type_cap,
                                          char *member, size_t member_cap) {
    const char *p = s;
    member[0] = 0;
    if (strncmp(p, "ok", 2) == 0 && (p[2] == ':' || p[2] == '(' || p[2] == ' ')) {
        snprintf(variant, variant_cap, "ok");
        p += 2;
    } else if (strncmp(p, "err", 3) == 0 && (p[3] == ':' || p[3] == '(' || p[3] == ' ')) {
        snprintf(variant, variant_cap, "err");
        p += 3;
    } else {
        return 0;
    }
    const char *in = strstr(p, " in ");
    if (!in) {
        /*
         * A literal err("member") covers one error member, not the whole
         * Result.err variant.  The cleaned line has strings removed, so
         * recover only quoted text before the branch colon from the raw line;
         * strings in the action must not be mistaken for the pattern.
         */
        if (strcmp(variant, "err") == 0 && raw) {
            const char *start = strstr(raw, "err");
            const char *colon = start ? strchr(start, ':') : NULL;
            if (start && colon) {
                const char *q = start;
                while (q < colon && *q != '"' && *q != '\'') q++;
                if (q < colon) {
                    char quote = *q++;
                    size_t n = 0;
                    while (q < colon && *q != quote && n + 1 < member_cap)
                        member[n++] = *q++;
                    member[n] = 0;
                    if (n > 0 && *q == quote) return 3;
                }
            }
        }
        return 1;
    }
    in += 4;
    size_t n = 0;
    while (in[n] && in[n] != ':' && in[n] != '|' && in[n] != ' ' && in[n] != '\t' && n + 1 < type_cap) n++;
    memcpy(type_name, in, n);
    type_name[n] = 0;
    return 2;
}

static void lint_add_member(char members[][96], int *count, const char *member) {
    if (!member || !*member || *count >= 32) return;
    for (int i = 0; i < *count; i++)
        if (strcmp(members[i], member) == 0) return;
    snprintf(members[(*count)++], 96, "%s", member);
}

static int lint_collect_type_refs(const char *rhs, LintFiniteType *types, int type_count,
                                  char members[][96], int *member_count) {
    int refs = 0;
    const char *p = rhs;
    while (*p) {
        while (*p && !(isalnum((unsigned char)*p) || *p == '_')) p++;
        if (!*p) break;
        char name[64];
        size_t n = 0;
        while ((isalnum((unsigned char)p[n]) || p[n] == '_') && n + 1 < sizeof name) n++;
        memcpy(name, p, n);
        name[n] = 0;
        LintFiniteType *source = lint_find_type(types, type_count, name);
        if (source) {
            refs++;
            for (int i = 0; i < source->count; i++)
                lint_add_member(members, member_count, source->members[i]);
        }
        p += n;
    }
    return refs;
}

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
    int case_complete_line = 0;
    int case_start_line = 0;
    int case_is_try = 0;
    int case_try_ok_open = 0;
    int case_try_err_open = 0;
    int case_try_err_complete = 0;
    LintResultCoverage case_try_coverage[32];
    int case_try_coverage_count = 0;
    char case_try_err_members[32][96];
    int case_try_err_member_count = 0;
    LintFiniteType *case_try_err_type = NULL;
    char case_subject[64] = "";
    LintFiniteType *case_type = NULL;
    char case_covered[32][96];
    int case_covered_count = 0;
    LintFiniteType types[32];
    int type_count = 0;
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

        /* Register finite string types for conservative case exhaustiveness. */
        if (strncmp(s, "type ", 5) == 0 && type_count < 32) {
            char tn[64] = "";
            if (sscanf(s + 5, "%63s", tn) == 1) {
                char *eq = strchr(tn, '=');
                if (eq) *eq = 0;
                const char *raw_eq = strchr(raw, '=');
                int nmem = raw_eq ? lint_extract_quoted(raw_eq + 1, types[type_count].members, 32) : 0;
                int refmem = 0;
                const char *clean_eq = strchr(s, '=');
                if (nmem == 0 && clean_eq) {
                    refmem = lint_collect_type_refs(clean_eq + 1, types, type_count,
                                                    types[type_count].members, &nmem);
                }
                if (nmem > 0 || refmem > 0) {
                    snprintf(types[type_count].name, sizeof types[type_count].name, "%s", tn);
                    types[type_count].count = nmem;
                    type_count++;
                }
            }
        }

        /* update loop context: count braces first */
        int opens = 0, closes = 0;
        for (const char *b = s; *b; b++) { if (*b == '{') opens++; if (*b == '}') closes++; }
        if (strncmp(s, "case ", 5) == 0 && strchr(s, '{')) {
            case_brace = block_depth;
            case_wildcard_line = 0;
            case_complete_line = 0;
            case_start_line = ln;
            case_is_try = (strncmp(s + 5, "try ", 4) == 0);
            case_try_ok_open = 0;
            case_try_err_open = 0;
            case_try_err_complete = 0;
            memset(case_try_coverage, 0, sizeof case_try_coverage);
            case_try_coverage_count = 0;
            memset(case_try_err_members, 0, sizeof case_try_err_members);
            case_try_err_member_count = 0;
            case_try_err_type = NULL;
            case_covered_count = 0;
            case_subject[0] = 0;
            case_type = NULL;
            char subject[64] = "";
            if (sscanf(s + 5, "%63s", subject) == 1) {
                char *brace = strchr(subject, '{');
                if (brace) *brace = 0;
                snprintf(case_subject, sizeof case_subject, "%s", subject);
                /* A variable is associated with a finite type when declared
                   as `x be Type` earlier in the same file. */
                FILE *decl = fopen(path, "rb");
                if (decl) {
                    char dl[4096];
                    while (fgets(dl, sizeof dl, decl)) {
                        char dn[64], dt[64];
                        if (sscanf(dl, "%63s be %63s", dn, dt) == 2 && strcmp(dn, case_subject) == 0) {
                            char *nl = strpbrk(dt, "\r\n;{}"); if (nl) *nl = 0;
                            case_type = lint_find_type(types, type_count, dt);
                            break;
                        }
                    }
                    fclose(decl);
                }
            }
        } else if (case_brace >= 0 && block_depth == case_brace + 1 && *s != '}') {
            if ((s[0] == '_' && s[1] == ':' ) || strncmp(s, "else:", 5) == 0)
                case_wildcard_line = ln;
            else if (case_is_try) {
                char variant[8] = "", type_name[64] = "", member[96] = "";
                int coverage = lint_extract_case_try_coverage(s, raw, variant, sizeof variant,
                                                               type_name, sizeof type_name,
                                                               member, sizeof member);
                if (coverage > 0) {
                    if (coverage == 1) {
                        if (strcmp(variant, "ok") == 0) case_try_ok_open = 1;
                        if (strcmp(variant, "err") == 0) case_try_err_open = 1;
                    } else if (case_try_coverage_count < 32) {
                        LintResultCoverage *rc = &case_try_coverage[case_try_coverage_count++];
                        snprintf(rc->variant, sizeof rc->variant, "%s", variant);
                        snprintf(rc->type_name, sizeof rc->type_name, "%s", type_name);
                        snprintf(rc->member, sizeof rc->member, "%s", member);
                        rc->complete = 0;
                        if (strcmp(variant, "err") == 0) {
                            if (type_name[0]) {
                                LintFiniteType *guard_type = lint_find_type(types, type_count, type_name);
                                if (guard_type) {
                                    case_try_err_type = guard_type;
                                    for (int mi = 0; mi < guard_type->count; mi++)
                                        lint_add_member(case_try_err_members, &case_try_err_member_count,
                                                        guard_type->members[mi]);
                                }
                            } else if (member[0]) {
                                lint_add_member(case_try_err_members, &case_try_err_member_count, member);
                                if (!case_try_err_type) {
                                    case_try_err_type = lint_find_unique_type_for_member(
                                        types, type_count, member);
                                }
                            }
                        }
                    }
                }
            }
            else if (case_complete_line) {
                lint_add(lb, ln, "WARN",
                    "case branch is unreachable: a finite-set membership branch already covers the subject");
            }
            else if (case_wildcard_line && strchr(s, ':')) {
                lint_add(lb, ln, "WARN",
                    "case branch is unreachable: wildcard '_'/'else' appears before this branch");
            }
            else if (strncmp(s, "in ", 3) == 0) {
                char type_name[64] = "";
                const char *p = s + 3;
                size_t n = 0;
                while (p[n] && p[n] != ':' && p[n] != ' ' &&
                       p[n] != '\t' && n + 1 < sizeof type_name) n++;
                memcpy(type_name, p, n);
                type_name[n] = 0;
                const char *tail = p + n;
                while (*tail == ' ' || *tail == '\t') tail++;
                LintFiniteType *membership_type =
                    lint_find_type(types, type_count, type_name);
                if (membership_type && *tail == ':') {
                    case_type = membership_type;
                    for (int mi = 0; mi < membership_type->count; mi++)
                        lint_add_member(case_covered, &case_covered_count,
                                        membership_type->members[mi]);
                    case_complete_line = ln;
                }
            }
            else if (case_type && case_covered_count < 32) {
                char found_members[32][96];
                int n = lint_extract_quoted(raw, found_members, 32);
                for (int i = 0; i < n && case_covered_count < 32; i++) {
                    int seen = 0;
                    for (int j = 0; j < case_covered_count; j++)
                        if (strcmp(case_covered[j], found_members[i]) == 0) seen = 1;
                    if (!seen) snprintf(case_covered[case_covered_count++], 96, "%s", found_members[i]);
                }
            }
        }
        if (case_brace >= 0 && closes > 0 && block_depth - closes <= case_brace) {
            if (case_try_err_open) case_try_err_complete = 1;
            if (!case_try_err_open && case_try_err_type) {
                int all_members = 1;
                for (int mi = 0; mi < case_try_err_type->count; mi++) {
                    int found = 0;
                    for (int ci = 0; ci < case_try_err_member_count; ci++) {
                        if (strcmp(case_try_err_type->members[mi], case_try_err_members[ci]) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        all_members = 0;
                        break;
                    }
                }
                case_try_err_complete = all_members;
            }
            if (!case_wildcard_line && !case_type &&
                (!case_is_try || !case_try_ok_open || !case_try_err_complete))
                lint_add(lb, case_start_line, "WARN",
                    "case has no wildcard '_'/'else' branch; exhaustive coverage cannot be proven for open or infinite sets");
            if (case_is_try && !case_wildcard_line &&
                (!case_try_ok_open || (!case_try_err_open && !case_try_err_complete))) {
                char missing[32] = "";
                if (!case_try_ok_open) strncat(missing, "ok", sizeof(missing) - strlen(missing) - 1);
                if (!case_try_err_open) {
                    if (missing[0]) strncat(missing, ", ", sizeof(missing) - strlen(missing) - 1);
                    strncat(missing, "err", sizeof(missing) - strlen(missing) - 1);
                }
                char msg[320];
                snprintf(msg, sizeof msg, "case try is missing Result branch(es): %s", missing);
                lint_add(lb, case_start_line, "WARN", msg);
            }
            if (case_is_try && !case_wildcard_line) {
                for (int ri = 0; ri < case_try_coverage_count; ri++) {
                    LintResultCoverage *rc = &case_try_coverage[ri];
                    int open = strcmp(rc->variant, "ok") == 0 ? case_try_ok_open : case_try_err_open;
                    if (strcmp(rc->variant, "err") == 0 && case_try_err_complete) continue;
                    if (open) continue;
                    char msg[320];
                    if (rc->type_name[0]) {
                        snprintf(msg, sizeof msg,
                            "case try guarded %s coverage via '%s' does not prove complete %s coverage; add %s(...) or '_'",
                            rc->variant, rc->type_name, rc->variant, rc->variant);
                    } else {
                        snprintf(msg, sizeof msg,
                            "case try guarded %s coverage does not prove complete %s coverage; add %s(...) or '_'",
                            rc->variant, rc->variant, rc->variant);
                    }
                    lint_add(lb, case_start_line, "WARN", msg);
                }
                if (!case_try_err_open && !case_try_err_complete && case_try_err_type) {
                    char missing[160] = "";
                    for (int mi = 0; mi < case_try_err_type->count; mi++) {
                        int found = 0;
                        for (int ci = 0; ci < case_try_err_member_count; ci++)
                            if (strcmp(case_try_err_type->members[mi], case_try_err_members[ci]) == 0)
                                found = 1;
                        if (!found) {
                            if (missing[0]) strncat(missing, ", ", sizeof(missing) - strlen(missing) - 1);
                            strncat(missing, case_try_err_type->members[mi],
                                    sizeof(missing) - strlen(missing) - 1);
                        }
                    }
                    if (missing[0]) {
                        char msg[320];
                        snprintf(msg, sizeof msg,
                                 "case try finite err type '%s' is missing members: %s",
                                 case_try_err_type->name, missing);
                        lint_add(lb, case_start_line, "WARN", msg);
                    }
                }
            }
            if (case_type && !case_wildcard_line) {
                char missing[160] = "";
                for (int i = 0; i < case_type->count; i++) {
                    int found = 0;
                    for (int j = 0; j < case_covered_count; j++)
                        if (strcmp(case_type->members[i], case_covered[j]) == 0) found = 1;
                    if (!found) {
                        if (missing[0]) strncat(missing, ", ", sizeof(missing) - strlen(missing) - 1);
                        strncat(missing, case_type->members[i], sizeof(missing) - strlen(missing) - 1);
                    }
                }
                if (missing[0]) {
                    char msg[320];
                    snprintf(msg, sizeof msg, "finite case type '%s' is missing members: %s", case_type->name, missing);
                    lint_add(lb, case_start_line, "WARN", msg);
                }
            }
            case_brace = -1;
            case_wildcard_line = 0;
            case_complete_line = 0;
            case_start_line = 0;
            case_type = NULL;
            case_subject[0] = 0;
            case_is_try = 0;
            case_try_ok_open = case_try_err_open = 0;
            case_try_err_complete = 0;
            case_try_coverage_count = 0;
            case_try_err_member_count = 0;
            case_try_err_type = NULL;
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
    char *path = NULL;
    if (vm_cur_sp(vm) >= 0) {
        Value a = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (a.type == VAL_STRING && a.sval) path = strdup(a.sval);
    }
    if (!path) path = strdup("");
    if (vm_cur_sp(vm) >= 0) {
        value_free(&vm_cur_stack(vm)[vm_cur_sp(vm)]);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
    char buf[8192];
    int n = lint_check(path, buf, sizeof buf);
    if (n > 0) fprintf(stderr, "%s", buf);
    free(path);
    Value v; v.type = VAL_INT; v.ival = n < 0 ? -1 : n; v.fval = 0; v.sval = NULL;
    vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
    vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
    return 1;
}

void lint_mod_register(VM *vm) {
    vm_register_builtin(vm, "lint_check", builtin_lint_check);
}
