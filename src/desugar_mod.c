/* desugar_mod.c - one-click desugar: sugar syntax -> canonical style.
 *
 * Usage:  inimerse --desugar in.im out.im   (out.im optional: stdout)
 *
 * Rules (code regions only; strings/comments preserved verbatim):
 *   print  -> say          (x++ -> x = x + 1)
 *   &&     -> and          (x-- -> x = x - 1)
 *   ||     -> or           (fn   -> func)
 *   trailing ';' removed   (// comments -> # comments)
 *
 * Keeps the codebase canonical: the engine accepts the sugar (humans/AI
 * write faster), repositories keep the desugared form.
 */
#include "vm.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>

#define DS_MAX_LINE 16384

static void ds_emit(char *dst, int *d, int cap, const char *s) {
    while (*s && *d < cap - 1) dst[(*d)++] = *s++;
}
static void ds_emit_char(char *dst, int *d, int cap, char c) { if (*d < cap - 1) dst[(*d)++] = c; }

static void desugar_line(const char *src, char *dst, int cap, int *in_block) {
    int d = 0;
    const char *p = src;
    int in_str = 0;
    char q = 0;
    int last_code_semi = -1;   /* dst index of last code-region ';' */
    int line_code_start = -1;  /* first non-space code char in dst */
    int say_wrap = 0;
    /* unless condition { ... } -> if !(condition) { ... } */
    {
        const char *u = src; while (*u == ' ' || *u == '\t') u++;
        if (strncmp(u, "unless", 6) == 0 && (u[6] == ' ' || u[6] == '\t')) {
            const char *brace = strchr(u + 6, '{');
            if (brace) {
                int prefix = (int)(u - src);
                for (int i = 0; i < prefix && d < cap - 1; i++) dst[d++] = src[i];
                ds_emit(dst, &d, cap, "if !(");
                for (const char *q = u + 6; q < brace && d < cap - 1; q++) dst[d++] = *q;
                ds_emit(dst, &d, cap, ") ");
                p = brace; line_code_start = d;
            }
        }
    }
    while (*p && d < cap - 1) {
        if (*in_block) {
            if (p[0] == ']') { *in_block = 0; dst[d++] = *p++; continue; }
            dst[d++] = *p++;
            continue;
        }
        if (p[0] == '#' && p[1] == '[') { *in_block = 1; dst[d++] = *p++; dst[d++] = *p++; continue; }
        if (in_str) {
            dst[d++] = *p;
            if (*p == q) in_str = 0;
            p++;
            continue;
        }
        if (p[0] == '#' || (p[0] == '/' && p[1] == '/')) {
            if (p[0] == '/') { dst[d++] = '#'; p += 2; }
            while (*p && *p != '\n' && *p != '\r' && d < cap - 1) dst[d++] = *p++;
            continue;
        }
        if (p[0] == '"' || p[0] == '\'') {
            q = p[0]; in_str = 1;
            if (line_code_start < 0) line_code_start = d;
            dst[d++] = *p++;
            continue;
        }
        /* word-boundary helpers */
        int prev_is_word = (d > 0) && (isalnum((unsigned char)dst[d - 1]) || dst[d - 1] == '_');
        /* say@target expr -> say_target("target", expr) */
        if (!prev_is_word && strncmp(p, "say@", 4) == 0) {
            const char *t = p + 4; size_t tn = 0; while (isalnum((unsigned char)t[tn]) || t[tn] == '_') tn++;
            if (tn > 0 && tn < 32 && (t[tn] == ' ' || t[tn] == '\t')) {
                ds_emit(dst, &d, cap, "say_target(\""); for (size_t i = 0; i < tn; ++i) ds_emit_char(dst, &d, cap, t[i]); ds_emit(dst, &d, cap, "\", "); p = t + tn; while (*p == ' ' || *p == '\t') p++; say_wrap = 1; continue;
            }
        }
        /* print -> say */
        if (!prev_is_word && strncmp(p, "print", 5) == 0 && (p[5] == ' ' || p[5] == '\t' || p[5] == '(')) {
            ds_emit(dst, &d, cap, "say");
            p += 5;
            if (line_code_start < 0) line_code_start = d;
            continue;
        }
        /* fn -> func */
        if (!prev_is_word && strncmp(p, "fn", 2) == 0 && (p[2] == ' ' || p[2] == '\t' || p[2] == '(')) {
            ds_emit(dst, &d, cap, "func");
            p += 2;
            if (line_code_start < 0) line_code_start = d;
            continue;
        }
        /* && -> and, || -> or */
        if (p[0] == '&' && p[1] == '&') { ds_emit(dst, &d, cap, "and"); p += 2; if (line_code_start < 0) line_code_start = d; continue; }
        if (p[0] == '|' && p[1] == '|') { ds_emit(dst, &d, cap, "or");  p += 2; if (line_code_start < 0) line_code_start = d; continue; }
        /* x++ / x-- -> x = x +/- 1 */
        if ((p[0] == '+' && p[1] == '+') || (p[0] == '-' && p[1] == '-')) {
            int k = d;
            while (k > 0 && (isalnum((unsigned char)dst[k - 1]) || dst[k - 1] == '_')) k--;
            if (k < d) {
                char ident[256];
                int idlen = d - k;
                if (idlen < 250) {
                    memcpy(ident, dst + k, idlen); ident[idlen] = 0;
                    d = k;
                    /* Emit the expansion through the bounded helper.  The
                     * previous sprintf could overrun DS_MAX_LINE when a
                     * nearly-full source line contained ++/--. */
                    ds_emit(dst, &d, cap, ident);
                    ds_emit(dst, &d, cap, " = ");
                    ds_emit(dst, &d, cap, ident);
                    ds_emit(dst, &d, cap, p[0] == '+' ? " + 1" : " - 1");
                    p += 2;
                    if (line_code_start < 0) line_code_start = d;
                    continue;
                }
            }
        }
        if (p[0] == ';') {
            if (say_wrap) { ds_emit(dst, &d, cap, ")"); say_wrap = 0; }
            dst[d++] = *p++;
            last_code_semi = d - 1;
            if (line_code_start < 0) line_code_start = d;
            continue;
        }
        dst[d++] = *p++;
        if (line_code_start < 0 && !isspace((unsigned char)p[-1])) line_code_start = d;
    }
    /* remove a code-region trailing ';' (keep the newline) */
    if (last_code_semi >= 0) {
        int e = d;
        while (e > last_code_semi + 1 && (dst[e - 1] == ' ' || dst[e - 1] == '\t' || dst[e - 1] == '\n' || dst[e - 1] == '\r')) e--;
        if (e == last_code_semi + 1) {
            memmove(dst + last_code_semi, dst + last_code_semi + 1, (size_t)(d - last_code_semi - 1));
            d--;
        }
    }
    if (say_wrap) {
        int e = d; while (e > 0 && (dst[e - 1] == '\n' || dst[e - 1] == '\r' || dst[e - 1] == ' ' || dst[e - 1] == '\t')) e--;
        if (e < d && d < cap - 1) memmove(dst + e + 1, dst + e, (size_t)(d - e));
        if (e < cap - 1) { dst[e] = ')'; d++; }
    }
    dst[d] = 0;
}

typedef struct { char *data; size_t len, cap; } DsBuf;
typedef struct { char *name; char *expr; } EidosField;
typedef struct { char *name; char **params; int param_count; char *body; int expression; int accessor; char *prop; } EidosMethod;
typedef struct { EidosField *fields; int field_count; EidosMethod *methods; int method_count; } EidosMembers;
typedef struct { char *name; EidosField *fields; int field_count; EidosMethod *methods; int method_count; char **owners; } EidosClass;

static void dsb_init(DsBuf *b) { b->cap = 1024; b->len = 0; b->data = malloc(b->cap); if (b->data) b->data[0] = 0; }
static void dsb_reserve(DsBuf *b, size_t add) {
    if (!b->data) return;
    if (b->len + add + 1 <= b->cap) return;
    size_t nc = b->cap;
    while (b->len + add + 1 > nc) nc *= 2;
    char *n = realloc(b->data, nc);
    if (!n) { free(b->data); b->data = NULL; b->len = b->cap = 0; return; }
    b->data = n; b->cap = nc;
}
static void dsb_n(DsBuf *b, const char *s, size_t n) { dsb_reserve(b, n); if (!b->data) return; memcpy(b->data + b->len, s, n); b->len += n; b->data[b->len] = 0; }
static void dsb_s(DsBuf *b, const char *s) { dsb_n(b, s, strlen(s)); }
static void dsb_c(DsBuf *b, char c) { dsb_reserve(b, 1); if (!b->data) return; b->data[b->len++] = c; b->data[b->len] = 0; }
static void dsb_f(DsBuf *b, const char *fmt, ...) {
    va_list ap, ap2; va_start(ap, fmt); va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n <= 0) { va_end(ap2); return; }
    dsb_reserve(b, (size_t)n);
    if (b->data) { vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap2); b->len += (size_t)n; }
    va_end(ap2);
}
static char *ds_xstrndup(const char *s, size_t n) { char *p = malloc(n + 1); if (!p) return NULL; memcpy(p, s, n); p[n] = 0; return p; }
static char *ds_trimdup(const char *s, size_t n) {
    while (n && isspace((unsigned char)*s)) { s++; n--; }
    while (n && isspace((unsigned char)s[n - 1])) n--;
    return ds_xstrndup(s, n);
}
static int ds_is_word0(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int ds_is_word(char c) { return isalnum((unsigned char)c) || c == '_'; }
static int ds_word_at(const char *s, size_t i, const char *w) {
    size_t n = strlen(w);
    return strncmp(s + i, w, n) == 0 &&
           (i == 0 || !ds_is_word(s[i - 1])) &&
           !ds_is_word(s[i + n]);
}
static size_t ds_skip_space_comments(const char *s, size_t i) {
    for (;;) {
        while (s[i] && isspace((unsigned char)s[i])) i++;
        if (s[i] == '#' && s[i + 1] == '[') {
            const char *e = strstr(s + i + 2, "]#");
            if (!e) return i;
            i = (size_t)(e - s) + 2;
            continue;
        }
        if (s[i] == '#' || (s[i] == '/' && s[i + 1] == '/')) {
            while (s[i] && s[i] != '\n' && s[i] != '\r') i++;
            continue;
        }
        return i;
    }
}
static size_t ds_word_len(const char *s, size_t i) { size_t j = i; if (!ds_is_word0(s[j])) return 0; while (ds_is_word(s[j])) j++; return j - i; }
static size_t ds_line_end(const char *s, size_t i) {
    int depth = 0; char quote = 0;
    for (; s[i]; i++) {
        char c = s[i];
        if (quote) { if (c == '\\' && s[i + 1]) i++; else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if ((c == ')' || c == ']' || c == '}') && depth > 0) depth--;
        else if ((c == '\n' || c == '\r') && depth == 0) return i;
    }
    return i;
}
static size_t ds_matching_pair(const char *s, size_t open, char left, char right) {
    int depth = 0; char quote = 0;
    for (size_t i = open; s[i]; i++) {
        char c = s[i];
        if (quote) { if (c == '\\' && s[i + 1]) i++; else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == left) depth++;
        else if (c == right && --depth == 0) return i;
    }
    return (size_t)-1;
}
static size_t ds_matching_brace(const char *s, size_t open) {
    int depth = 0, line_comment = 0, block_comment = 0; char quote = 0;
    for (size_t i = open; s[i]; i++) {
        char c = s[i], n = s[i + 1];
        if (block_comment) { if (c == ']' && n == '#') { block_comment = 0; i++; } continue; }
        if (line_comment) { if (c == '\n' || c == '\r') line_comment = 0; continue; }
        if (quote) { if (c == '\\' && n) i++; else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '#' && n == '[') { block_comment = 1; i++; continue; }
        if (c == '#' || (c == '/' && n == '/')) { line_comment = 1; if (c == '/') i++; continue; }
        if (c == '{') depth++;
        else if (c == '}' && --depth == 0) return i;
    }
    return (size_t)-1;
}
static int ds_field_index(EidosField *fields, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(fields[i].name, name) == 0) return i;
    return -1;
}
static int ds_method_index(EidosMethod *methods, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(methods[i].name, name) == 0) return i;
    return -1;
}
static int ds_method_noarg(EidosMethod *methods, int n, const char *name) {
    int ix = ds_method_index(methods, n, name);
    return ix >= 0 && methods[ix].param_count == 0;
}
static int ds_param_has(EidosMethod *m, const char *name) {
    for (int i = 0; i < m->param_count; i++) if (strcmp(m->params[i], name) == 0) return 1;
    return 0;
}
static EidosClass *ds_find_class(EidosClass *classes, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(classes[i].name, name) == 0) return &classes[i];
    return NULL;
}
static EidosMethod *ds_find_named_method(EidosClass *classes, int n, const char *name) {
    for (int i = 0; i < n; i++) {
        int ix = ds_method_index(classes[i].methods, classes[i].method_count, name);
        if (ix >= 0) return &classes[i].methods[ix];
    }
    return NULL;
}
static EidosMethod *ds_find_accessor(EidosClass *classes, int n, const char *kind, const char *prop) {
    char name[256];
    snprintf(name, sizeof(name), "%s_%s", kind, prop);
    return ds_find_named_method(classes, n, name);
}
static EidosMethod *ds_class_method(EidosClass *klass, const char *name) {
    if (!klass) return NULL;
    int ix = ds_method_index(klass->methods, klass->method_count, name);
    return ix >= 0 ? &klass->methods[ix] : NULL;
}
static EidosMethod *ds_class_accessor(EidosClass *klass, const char *kind, const char *prop) {
    char name[256];
    snprintf(name, sizeof(name), "%s_%s", kind, prop);
    return ds_class_method(klass, name);
}
static const char *ds_operator_method(const char *op) {
    if (strcmp(op, "+") == 0) return "__op_add";
    if (strcmp(op, "-") == 0) return "__op_sub";
    if (strcmp(op, "*") == 0) return "__op_mul";
    if (strcmp(op, "/") == 0) return "__op_div";
    if (strcmp(op, "%") == 0) return "__op_mod";
    if (strcmp(op, "==") == 0) return "__op_eq";
    if (strcmp(op, "!=") == 0) return "__op_ne";
    if (strcmp(op, "<") == 0) return "__op_lt";
    if (strcmp(op, ">") == 0) return "__op_gt";
    if (strcmp(op, "<=") == 0) return "__op_le";
    if (strcmp(op, ">=") == 0) return "__op_ge";
    return NULL;
}
static EidosClass *ds_binding_class(char **names, EidosClass **bindings, int count, const char *name) {
    for (int i = 0; i < count; i++) if (strcmp(names[i], name) == 0) return bindings[i];
    return NULL;
}
static size_t ds_operator_at(const char *text, size_t i, char *op, size_t cap);

static int ds_split_args(const char *text, size_t begin, size_t end, char ***out_args) {
    char **args = NULL; int count = 0; size_t start = begin;
    int paren = 0, bracket = 0, brace = 0; char quote = 0;
    for (size_t i = begin; i <= end; i++) {
        char c = text[i];
        if (quote) {
            if (c == '\\' && i < end) i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '(') paren++;
        else if (c == ')' && paren > 0) paren--;
        else if (c == '[') bracket++;
        else if (c == ']' && bracket > 0) bracket--;
        else if (c == '{') brace++;
        else if (c == '}' && brace > 0) brace--;
        else if ((c == ',' || i == end) && paren == 0 && bracket == 0 && brace == 0) {
            size_t stop = (c == ',') ? i : i + 1;
            char *arg = ds_trimdup(text + start, stop - start);
            if (arg && arg[0]) {
                args = realloc(args, (size_t)(count + 1) * sizeof(*args));
                args[count++] = arg;
            } else free(arg);
            start = i + 1;
        }
    }
    *out_args = args;
    return count;
}

static char *ds_rewrite_constructor_args(const char *text, EidosClass *classes, int class_count) {
    DsBuf out; dsb_init(&out);
    int line_comment = 0, block_comment = 0; char quote = 0;
    for (size_t i = 0; text[i];) {
        char c = text[i], n = text[i + 1];
        if (block_comment) { dsb_c(&out, c); i++; if (c == ']' && n == '#') { dsb_c(&out, n); i++; block_comment = 0; } continue; }
        if (line_comment) { dsb_c(&out, c); i++; if (c == '\n' || c == '\r') line_comment = 0; continue; }
        if (quote) { dsb_c(&out, c); i++; if (c == '\\' && text[i]) dsb_c(&out, text[i++]); else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; dsb_c(&out, c); i++; continue; }
        if (c == '#' && n == '[') { dsb_s(&out, "#["); i += 2; block_comment = 1; continue; }
        if (c == '#' || (c == '/' && n == '/')) { dsb_c(&out, c); i++; if (c == '/') dsb_c(&out, text[i++]); line_comment = 1; continue; }
        size_t wl = ds_word_len(text, i);
        if (wl) {
            char *word = ds_xstrndup(text + i, wl);
            EidosClass *klass = ds_find_class(classes, class_count, word);
            size_t open = ds_skip_space_comments(text, i + wl);
            if (klass && text[open] == '(') {
                size_t close = ds_matching_pair(text, open, '(', ')');
                if (close != (size_t)-1) {
                    char **args = NULL;
                    int argc = close > open + 1
                        ? ds_split_args(text, open + 1, close - 1, &args)
                        : 0;
                    int has_named = 0;
                    for (int a = 0; a < argc; a++) {
                        char *eq = strchr(args[a], '=');
                        if (eq && eq[1] != '=') { has_named = 1; break; }
                    }
                    if (has_named) {
                        char **ordered = calloc((size_t)klass->field_count, sizeof(*ordered));
                        int positional = 0;
                        for (int a = 0; a < argc; a++) {
                            char *eq = strchr(args[a], '=');
                            if (eq && eq[1] != '=') {
                                *eq = 0;
                                char *key = ds_trimdup(args[a], strlen(args[a]));
                                int fi = ds_field_index(klass->fields, klass->field_count, key ? key : "");
                                free(key);
                                *eq = '=';
                                if (fi >= 0) ordered[fi] = ds_trimdup(eq + 1, strlen(eq + 1));
                            } else {
                                while (positional < klass->field_count && ordered[positional]) positional++;
                                if (positional < klass->field_count) {
                                    ordered[positional++] = strdup(args[a]);
                                }
                            }
                        }
                        dsb_n(&out, text + i, wl);
                        dsb_c(&out, '(');
                        for (int f = 0; f < klass->field_count; f++) {
                            if (f) dsb_s(&out, ", ");
                            dsb_s(&out, ordered[f] ? ordered[f] : "nil");
                            free(ordered[f]);
                        }
                        dsb_c(&out, ')');
                        free(ordered);
                        for (int a = 0; a < argc; a++) free(args[a]);
                        free(args); free(word);
                        i = close + 1;
                        continue;
                    }
                    for (int a = 0; a < argc; a++) free(args[a]);
                    free(args);
                }
            }
            dsb_s(&out, word); free(word); i += wl; continue;
        }
        dsb_c(&out, c); i++;
    }
    return out.data;
}

static char *ds_rewrite_member_syntax(const char *text, EidosClass *classes, int class_count) {
    DsBuf out; dsb_init(&out);
    int line_comment = 0, block_comment = 0; char quote = 0;
    char **binding_names = NULL;
    EidosClass **binding_classes = NULL;
    int binding_count = 0;
    for (size_t i = 0; text[i];) {
        char c = text[i], n = text[i + 1];
        if (block_comment) { dsb_c(&out, c); i++; if (c == ']' && n == '#') { dsb_c(&out, n); i++; block_comment = 0; } continue; }
        if (line_comment) { dsb_c(&out, c); i++; if (c == '\n' || c == '\r') line_comment = 0; continue; }
        if (quote) { dsb_c(&out, c); i++; if (c == '\\' && text[i]) dsb_c(&out, text[i++]); else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; dsb_c(&out, c); i++; continue; }
        if (c == '#' && n == '[') { dsb_s(&out, "#["); i += 2; block_comment = 1; continue; }
        if (c == '#' || (c == '/' && n == '/')) { dsb_c(&out, c); i++; if (c == '/') dsb_c(&out, text[i++]); line_comment = 1; continue; }
        size_t wl = ds_word_len(text, i);
        if (wl) {
            char *object_name = ds_xstrndup(text + i, wl);
            size_t after_word = ds_skip_space_comments(text, i + wl);
            if (object_name && text[after_word] == '=' && text[after_word + 1] != '=') {
                size_t rhs = ds_skip_space_comments(text, after_word + 1);
                size_t cl = ds_word_len(text, rhs);
                if (cl) {
                    char *class_name = ds_xstrndup(text + rhs, cl);
                    EidosClass *klass = ds_find_class(classes, class_count, class_name ? class_name : "");
                    if (!klass && class_name) {
                        klass = ds_binding_class(binding_names, binding_classes, binding_count, class_name);
                        size_t op_pos = ds_skip_space_comments(text, rhs + cl);
                        char op[4] = {0};
                        size_t op_len = ds_operator_at(text, op_pos, op, sizeof(op));
                        if (klass && (!op_len || !ds_operator_method(op) ||
                                      !ds_class_method(klass, ds_operator_method(op)))) {
                            klass = NULL;
                        }
                    }
                    if (klass) {
                        int bi = -1;
                        for (int b = 0; b < binding_count; b++) {
                            if (strcmp(binding_names[b], object_name) == 0) { bi = b; break; }
                        }
                        if (bi < 0) {
                            binding_names = realloc(binding_names, (size_t)(binding_count + 1) * sizeof(*binding_names));
                            binding_classes = realloc(binding_classes, (size_t)(binding_count + 1) * sizeof(*binding_classes));
                            if (binding_names && binding_classes) {
                                binding_names[binding_count] = strdup(object_name);
                                binding_classes[binding_count++] = klass;
                            }
                        } else {
                            binding_classes[bi] = klass;
                        }
                    }
                    free(class_name);
                }
            }
            size_t dot = ds_skip_space_comments(text, i + wl);
            if (text[dot] == '.') {
                size_t mi = ds_skip_space_comments(text, dot + 1);
                size_t ml = ds_word_len(text, mi);
                if (ml) {
                    char *member = ds_xstrndup(text + mi, ml);
                    EidosClass *klass = ds_binding_class(binding_names, binding_classes, binding_count,
                                                         object_name ? object_name : "");
                    EidosMethod *method = ds_class_method(klass, member ? member : "");
                    EidosMethod *getter = ds_class_accessor(klass, "get", member ? member : "");
                    EidosMethod *setter = ds_class_accessor(klass, "set", member ? member : "");
                    int field = klass && member && ds_field_index(klass->fields, klass->field_count, member) >= 0;
                    size_t after = ds_skip_space_comments(text, mi + ml);
                    if (setter && text[after] == '=') {
                        size_t end = ds_line_end(text, after + 1);
                        dsb_n(&out, text + i, wl);
                        dsb_f(&out, "[\"set_%s\"](", member);
                        size_t rhs = after + 1;
                        while (rhs < end && isspace((unsigned char)text[rhs])) rhs++;
                        dsb_n(&out, text + rhs, end - rhs);
                        dsb_c(&out, ')');
                        if (klass && ds_class_method(klass, "invariant"))
                            dsb_f(&out, "\n%.*s[\"invariant\"]()", (int)wl, text + i);
                        free(member); free(object_name); i = end; continue;
                    }
                    if (field && text[after] == '=') {
                        size_t end = ds_line_end(text, after + 1);
                        dsb_n(&out, text + i, wl);
                        dsb_f(&out, "[\"%s\"] = ", member);
                        size_t rhs = after + 1;
                        while (rhs < end && isspace((unsigned char)text[rhs])) rhs++;
                        dsb_n(&out, text + rhs, end - rhs);
                        if (klass && ds_class_method(klass, "invariant"))
                            dsb_f(&out, "\n%.*s[\"invariant\"]()", (int)wl, text + i);
                        free(member); free(object_name); i = end; continue;
                    }
                    if (method || getter || field) {
                        dsb_n(&out, text + i, wl);
                        if (getter && !method) dsb_f(&out, "[\"get_%s\"]()", member);
                        else if (method) {
                            dsb_f(&out, "[\"%s\"]", member);
                            if (text[after] != '(' && method->param_count == 0) dsb_s(&out, "()");
                        } else {
                            dsb_f(&out, "[\"%s\"]", member);
                        }
                        free(member); free(object_name); i = mi + ml; continue;
                    }
                    free(member);
                }
            }
            free(object_name);
            dsb_n(&out, text + i, wl); i += wl; continue;
        }
        dsb_c(&out, c); i++;
    }
    for (int b = 0; b < binding_count; b++) free(binding_names[b]);
    free(binding_names);
    free(binding_classes);
    return out.data;
}

static size_t ds_operator_at(const char *text, size_t i, char *op, size_t cap) {
    const char *ops[] = { "==", "!=", "<=", ">=", "+", "-", "*", "/", "%", "<", ">", NULL };
    for (int k = 0; ops[k]; k++) {
        size_t n = strlen(ops[k]);
        if (strncmp(text + i, ops[k], n) == 0) {
            if (n + 1 > cap) return 0;
            memcpy(op, ops[k], n + 1);
            return n;
        }
    }
    return 0;
}

static size_t ds_operator_operand_end(const char *text, size_t i) {
    i = ds_skip_space_comments(text, i);
    if (!text[i]) return i;
    if (text[i] == '"' || text[i] == '\'') {
        char q = text[i++];
        while (text[i]) {
            if (text[i] == '\\' && text[i + 1]) { i += 2; continue; }
            if (text[i++] == q) break;
        }
        return i;
    }
    size_t wl = ds_word_len(text, i);
    if (wl) {
        i += wl;
        for (;;) {
            size_t j = ds_skip_space_comments(text, i);
            if (text[j] == '.') {
                size_t ml = ds_word_len(text, ds_skip_space_comments(text, j + 1));
                if (!ml) break;
                i = ds_skip_space_comments(text, j + 1) + ml;
            } else if (text[j] == '[') {
                size_t end = ds_matching_pair(text, j, '[', ']');
                if (end == (size_t)-1) break;
                i = end + 1;
            } else {
                break;
            }
        }
        return i;
    }
    while (text[i] && !isspace((unsigned char)text[i]) &&
           text[i] != ',' && text[i] != ')' && text[i] != ']' && text[i] != '}') i++;
    return i;
}

static char *ds_rewrite_operator_syntax(const char *text, EidosClass *classes, int class_count) {
    DsBuf out; dsb_init(&out);
    int line_comment = 0, block_comment = 0; char quote = 0;
    char **binding_names = NULL;
    EidosClass **binding_classes = NULL;
    int binding_count = 0;
    for (size_t i = 0; text[i];) {
        char c = text[i], n = text[i + 1];
        if (block_comment) {
            dsb_c(&out, c); i++;
            if (c == ']' && n == '#') { dsb_c(&out, n); i++; block_comment = 0; }
            continue;
        }
        if (line_comment) {
            dsb_c(&out, c); i++;
            if (c == '\n' || c == '\r') line_comment = 0;
            continue;
        }
        if (quote) {
            dsb_c(&out, c); i++;
            if (c == '\\' && text[i]) dsb_c(&out, text[i++]);
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; dsb_c(&out, c); i++; continue; }
        if (c == '#' && n == '[') { dsb_s(&out, "#["); i += 2; block_comment = 1; continue; }
        if (c == '#' || (c == '/' && n == '/')) {
            dsb_c(&out, c); i++;
            if (c == '/') dsb_c(&out, text[i++]);
            line_comment = 1;
            continue;
        }
        size_t wl = ds_word_len(text, i);
        if (wl) {
            char *word = ds_xstrndup(text + i, wl);
            size_t after = ds_skip_space_comments(text, i + wl);
            if (word && text[after] == '=' && text[after + 1] != '=') {
                size_t rhs = ds_skip_space_comments(text, after + 1);
                size_t cl = ds_word_len(text, rhs);
                if (cl) {
                    char *class_name = ds_xstrndup(text + rhs, cl);
                    EidosClass *klass = ds_find_class(classes, class_count, class_name ? class_name : "");
                    if (klass) {
                        int bi = -1;
                        for (int b = 0; b < binding_count; b++)
                            if (strcmp(binding_names[b], word) == 0) { bi = b; break; }
                        if (bi < 0) {
                            char **nn = realloc(binding_names, (size_t)(binding_count + 1) * sizeof(*nn));
                            EidosClass **nc = realloc(binding_classes, (size_t)(binding_count + 1) * sizeof(*nc));
                            if (nn && nc) {
                                binding_names = nn; binding_classes = nc;
                                binding_names[binding_count] = strdup(word);
                                binding_classes[binding_count++] = klass;
                            }
                        } else binding_classes[bi] = klass;
                    }
                    free(class_name);
                }
            }
            EidosClass *klass = ds_binding_class(binding_names, binding_classes, binding_count, word ? word : "");
            size_t op_pos = ds_skip_space_comments(text, i + wl);
            char op[4] = {0};
            size_t op_len = ds_operator_at(text, op_pos, op, sizeof(op));
            const char *method_name = op_len ? ds_operator_method(op) : NULL;
            if (klass && method_name && ds_class_method(klass, method_name)) {
                size_t rhs = ds_skip_space_comments(text, op_pos + op_len);
                size_t end = ds_operator_operand_end(text, rhs);
                if (end > rhs) {
                    dsb_n(&out, text + i, wl);
                    dsb_f(&out, "[\"%s\"](", method_name);
                    dsb_n(&out, text + rhs, end - rhs);
                    dsb_c(&out, ')');
                    free(word);
                    i = end;
                    continue;
                }
            }
            dsb_n(&out, text + i, wl);
            free(word);
            i += wl;
            continue;
        }
        dsb_c(&out, c); i++;
    }
    for (int b = 0; b < binding_count; b++) free(binding_names[b]);
    free(binding_names);
    free(binding_classes);
    return out.data;
}

static char *ds_replace_eidos_refs(const char *text, EidosField *fields, int field_count,
                                   EidosMethod *methods, int method_count, EidosMethod *method) {
    DsBuf out; dsb_init(&out);
    int line_comment = 0, block_comment = 0; char quote = 0;
    for (size_t i = 0; text[i];) {
        char c = text[i], n = text[i + 1];
        if (block_comment) { dsb_c(&out, c); i++; if (c == ']' && n == '#') { dsb_c(&out, n); i++; block_comment = 0; } continue; }
        if (line_comment) { dsb_c(&out, c); i++; if (c == '\n' || c == '\r') line_comment = 0; continue; }
        if (quote) { dsb_c(&out, c); i++; if (c == '\\' && text[i]) dsb_c(&out, text[i++]); else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; dsb_c(&out, c); i++; continue; }
        if (c == '#' && n == '[') { dsb_s(&out, "#["); i += 2; block_comment = 1; continue; }
        if (c == '#' || (c == '/' && n == '/')) { dsb_c(&out, c); i++; if (c == '/') dsb_c(&out, text[i++]); line_comment = 1; continue; }
        size_t wl = ds_word_len(text, i);
        if (wl) {
            char *w = ds_xstrndup(text + i, wl);
            if (w && (strcmp(w, "this") == 0 || strcmp(w, "self") == 0)) {
                dsb_s(&out, "__eidos_obj");
                free(w);
                i += wl;
                continue;
            }
            int is_field = ds_field_index(fields, field_count, w) >= 0;
            int is_param = method && ds_param_has(method, w);
            int is_method = ds_method_index(methods, method_count, w) >= 0;
            char prev = i ? text[i - 1] : 0;
            size_t next = ds_skip_space_comments(text, i + wl);
            if (is_param && text[next] == '.') {
                size_t mi = ds_skip_space_comments(text, next + 1);
                size_t ml = ds_word_len(text, mi);
                if (ml) {
                    char *member = ds_xstrndup(text + mi, ml);
                    if (member && ds_field_index(fields, field_count, member) >= 0) {
                        dsb_f(&out, "%s[\"%s\"]", w, member);
                        free(member);
                        free(w);
                        i = mi + ml;
                        continue;
                    }
                    free(member);
                }
            }
            if (is_method && !is_param && prev != '.') {
                dsb_f(&out, "__eidos_obj[\"%s\"]", w);
                if (text[next] != '(' && ds_method_noarg(methods, method_count, w)) dsb_s(&out, "()");
            }
            else if (is_field && !is_param && prev != '.') dsb_f(&out, "__eidos_obj[\"%s\"]", w);
            else dsb_s(&out, w);
            free(w); i += wl; continue;
        }
        dsb_c(&out, c); i++;
    }
    return out.data;
}

static char *ds_replace_field_refs(const char *text, EidosField *fields, int field_count, EidosMethod *method) {
    return ds_replace_eidos_refs(text, fields, field_count, NULL, 0, method);
}

static void ds_emit_invariant_checks(DsBuf *out, const char *body, int has_violation) {
    size_t i = 0;
    while (body && body[i]) {
        i = ds_skip_space_comments(body, i);
        if (!body[i]) break;
        size_t end = ds_line_end(body, i);
        char *cond = ds_trimdup(body + i, end - i);
        if (cond && cond[0]) {
            dsb_f(out, "if !(%s) { ", cond);
            if (has_violation) dsb_s(out, "__eidos_obj[\"on_violation\"]()");
            else dsb_s(out, "throw \"eidos invariant failed\"");
            dsb_s(out, " }\n");
        }
        free(cond);
        i = end;
        while (body[i] == '\n' || body[i] == '\r' || body[i] == ';') i++;
    }
    dsb_s(out, "return true\n");
}

static char *ds_replace_super_refs(const char *text, const char *parent) {
    DsBuf out; dsb_init(&out);
    int line_comment = 0, block_comment = 0; char quote = 0;
    for (size_t i = 0; text[i];) {
        char c = text[i], n = text[i + 1];
        if (block_comment) { dsb_c(&out, c); i++; if (c == ']' && n == '#') { dsb_c(&out, n); i++; block_comment = 0; } continue; }
        if (line_comment) { dsb_c(&out, c); i++; if (c == '\n' || c == '\r') line_comment = 0; continue; }
        if (quote) { dsb_c(&out, c); i++; if (c == '\\' && text[i]) dsb_c(&out, text[i++]); else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; dsb_c(&out, c); i++; continue; }
        if (c == '#' && n == '[') { dsb_s(&out, "#["); i += 2; block_comment = 1; continue; }
        if (c == '#' || (c == '/' && n == '/')) { dsb_c(&out, c); i++; if (c == '/') dsb_c(&out, text[i++]); line_comment = 1; continue; }
        if (ds_word_at(text, i, "super")) {
            size_t j = ds_skip_space_comments(text, i + 5);
            if (text[j] == '.') {
                j = ds_skip_space_comments(text, j + 1);
                size_t ml = ds_word_len(text, j);
                if (ml) {
                    char *m = ds_xstrndup(text + j, ml);
                    size_t k = ds_skip_space_comments(text, j + ml);
                    dsb_f(&out, "%s__%s(__eidos_obj", parent, m);
                    free(m);
                    if (text[k] == '(') {
                        i = k + 1;
                        if (text[i] == ')') { i++; dsb_c(&out, ')'); }
                        else dsb_s(&out, ", ");
                    } else { i = j + ml; dsb_c(&out, ')'); }
                    continue;
                }
            }
        }
        dsb_c(&out, c); i++;
    }
    return out.data;
}

static char *ds_replace_eidos_aliases(const char *text) {
    DsBuf out; dsb_init(&out);
    int line_comment = 0, block_comment = 0; char quote = 0;
    for (size_t i = 0; text[i];) {
        char c = text[i], n = text[i + 1];
        if (block_comment) { dsb_c(&out, c); i++; if (c == ']' && n == '#') { dsb_c(&out, n); i++; block_comment = 0; } continue; }
        if (line_comment) { dsb_c(&out, c); i++; if (c == '\n' || c == '\r') line_comment = 0; continue; }
        if (quote) { dsb_c(&out, c); i++; if (c == '\\' && text[i]) dsb_c(&out, text[i++]); else if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; dsb_c(&out, c); i++; continue; }
        if (c == '#' && n == '[') { dsb_s(&out, "#["); i += 2; block_comment = 1; continue; }
        if (c == '#' || (c == '/' && n == '/')) { dsb_c(&out, c); i++; if (c == '/') dsb_c(&out, text[i++]); line_comment = 1; continue; }
        if (ds_word_at(text, i, "eidos")) { dsb_s(&out, "record"); i += 5; continue; }
        if (ds_word_at(text, i, "ed")) { dsb_s(&out, "record"); i += 2; continue; }
        dsb_c(&out, c); i++;
    }
    return out.data;
}

static size_t ds_member_name(const char *body, size_t i, char **out_name) {
    size_t wl = ds_word_len(body, i);
    if (wl) {
        *out_name = ds_xstrndup(body + i, wl);
        return wl;
    }
    const char *ops[] = { "==", "!=", "<=", ">=", "+", "-", "*", "/", "%", "<", ">", NULL };
    for (int k = 0; ops[k]; k++) {
        size_t n = strlen(ops[k]);
        if (strncmp(body + i, ops[k], n) == 0) {
            const char *mapped = ds_operator_method(ops[k]);
            if (mapped) *out_name = strdup(mapped);
            return mapped ? n : 0;
        }
    }
    return 0;
}

static EidosMembers ds_parse_members(const char *body) {
    EidosMembers out = {0};
    size_t i = 0;
    while (body[i]) {
        i = ds_skip_space_comments(body, i);
        if (!body[i]) break;
        char *name = NULL;
        size_t nl = ds_member_name(body, i, &name);
        if (!nl) break;
        i = ds_skip_space_comments(body, i + nl);
        int accessor = 0;
        char *prop = NULL;
        if ((strcmp(name, "get") == 0 || strcmp(name, "set") == 0) && ds_word_len(body, i) > 0) {
            accessor = strcmp(name, "get") == 0 ? 1 : 2;
            size_t pl = ds_word_len(body, i);
            prop = ds_xstrndup(body + i, pl);
            free(name);
            size_t nn = (accessor == 1 ? 4 : 4) + pl;
            name = malloc(nn + 1);
            if (!name) return out;
            snprintf(name, nn + 1, "%s_%s", accessor == 1 ? "get" : "set", prop);
            i = ds_skip_space_comments(body, i + pl);
        }
        if (body[i] == '=') {
            if (accessor) { free(name); free(prop); break; }
            size_t end = ds_line_end(body, i + 1);
            out.fields = realloc(out.fields, (size_t)(out.field_count + 1) * sizeof(*out.fields));
            out.fields[out.field_count].name = name;
            out.fields[out.field_count].expr = ds_trimdup(body + i + 1, end - i - 1);
            out.field_count++;
            i = end;
            continue;
        }
        char **params = NULL; int pc = 0;
        if (body[i] == '(') {
            size_t end = ds_matching_pair(body, i, '(', ')');
            if (end == (size_t)-1) { free(name); break; }
            size_t p = i + 1, start = p;
            while (p < end) {
                if (body[p] == ',') {
                    char *param = ds_trimdup(body + start, p - start);
                    if (param[0]) { params = realloc(params, (size_t)(pc + 1) * sizeof(*params)); params[pc++] = param; } else free(param);
                    start = p + 1;
                }
                p++;
            }
            char *param = ds_trimdup(body + start, end - start);
            if (param[0]) { params = realloc(params, (size_t)(pc + 1) * sizeof(*params)); params[pc++] = param; } else free(param);
            i = ds_skip_space_comments(body, end + 1);
        }
        if (body[i] == '{') {
            size_t end = ds_matching_brace(body, i);
            if (end == (size_t)-1) { free(name); break; }
            out.methods = realloc(out.methods, (size_t)(out.method_count + 1) * sizeof(*out.methods));
            out.methods[out.method_count++] = (EidosMethod){ name, params, pc, ds_xstrndup(body + i + 1, end - i - 1), 0, accessor, prop };
            i = end + 1;
            continue;
        }
        if (body[i] == '-' && body[i + 1] == '>') {
            size_t end = ds_line_end(body, i + 2);
            out.methods = realloc(out.methods, (size_t)(out.method_count + 1) * sizeof(*out.methods));
            out.methods[out.method_count++] = (EidosMethod){ name, params, pc, ds_trimdup(body + i + 2, end - i - 2), 1, accessor, prop };
            i = end;
            continue;
        }
        free(name);
        free(prop);
        break;
    }
    return out;
}

static char *desugar_eidos_source(const char *src) {
    DsBuf generated, remainder; dsb_init(&generated); dsb_init(&remainder);
    EidosClass *classes = NULL; int class_count = 0;
    size_t i = 0;
    while (src[i]) {
        size_t j = i, start = (size_t)-1; const char *kw = NULL;
        int line_comment = 0, block_comment = 0; char quote = 0;
        for (; src[j]; j++) {
            char c = src[j], n = src[j + 1];
            if (block_comment) { if (c == ']' && n == '#') { block_comment = 0; j++; } continue; }
            if (line_comment) { if (c == '\n' || c == '\r') line_comment = 0; continue; }
            if (quote) { if (c == '\\' && n) j++; else if (c == quote) quote = 0; continue; }
            if (c == '"' || c == '\'') { quote = c; continue; }
            if (c == '#' && n == '[') { block_comment = 1; j++; continue; }
            if (c == '#' || (c == '/' && n == '/')) { line_comment = 1; if (c == '/') j++; continue; }
            if (ds_word_at(src, j, "eidos")) { start = j; kw = "eidos"; break; }
            if (ds_word_at(src, j, "ed")) { start = j; kw = "ed"; break; }
        }
        if (start == (size_t)-1) { dsb_s(&remainder, src + i); break; }
        dsb_n(&remainder, src + i, start - i);
        size_t cur = ds_skip_space_comments(src, start + strlen(kw));
        size_t nl = ds_word_len(src, cur);
        if (!nl) { dsb_n(&remainder, src + start, strlen(kw)); i = start + strlen(kw); continue; }
        char *name = ds_xstrndup(src + cur, nl);
        cur = ds_skip_space_comments(src, cur + nl);
        char **parents = NULL; int parent_count = 0;
        if (src[cur] == ':') {
            do {
                cur = ds_skip_space_comments(src, cur + 1);
                size_t pl = ds_word_len(src, cur);
                if (!pl) { fprintf(stderr, "Eidos %s inheritance needs a parent name\n", name); free(name); return NULL; }
                char *parent_name = ds_xstrndup(src + cur, pl);
                if (!ds_find_class(classes, class_count, parent_name)) {
                    fprintf(stderr, "Eidos parent %s must be declared before %s\n", parent_name, name);
                    free(name); free(parent_name);
                    for (int pi = 0; pi < parent_count; pi++) free(parents[pi]);
                    free(parents);
                    return NULL;
                }
                parents = realloc(parents, (size_t)(parent_count + 1) * sizeof(*parents));
                parents[parent_count++] = parent_name;
                cur = ds_skip_space_comments(src, cur + pl);
            } while (src[cur] == '+');
        }
        if (src[cur] != '{') {
            dsb_s(&remainder, "record");
            free(name);
            for (int pi = 0; pi < parent_count; pi++) free(parents[pi]);
            free(parents);
            i = start + strlen(kw);
            continue;
        }
        size_t end = ds_matching_brace(src, cur);
        if (end == (size_t)-1) {
            fprintf(stderr, "Eidos class %s has an unterminated body\n", name);
            free(name);
            for (int pi = 0; pi < parent_count; pi++) free(parents[pi]);
            free(parents);
            return NULL;
        }
        char *body_src = ds_xstrndup(src + cur + 1, end - cur - 1);
        EidosMembers own = ds_parse_members(body_src ? body_src : "");
        free(body_src);
        EidosField *fields = NULL; int field_count = 0;
        EidosMethod *methods = NULL; char **owners = NULL; int method_count = 0;
        for (int pi = 0; pi < parent_count; pi++) {
            EidosClass *pc = ds_find_class(classes, class_count, parents[pi]);
            if (!pc) continue;
            for (int f = 0; f < pc->field_count; f++) {
                int ix = ds_field_index(fields, field_count, pc->fields[f].name);
                EidosField inherited_field = { strdup(pc->fields[f].name), strdup(pc->fields[f].expr) };
                if (ix >= 0) fields[ix] = inherited_field;
                else {
                    fields = realloc(fields, (size_t)(field_count + 1) * sizeof(*fields));
                    fields[field_count++] = inherited_field;
                }
            }
            for (int m = 0; m < pc->method_count; m++) {
                int ix = ds_method_index(methods, method_count, pc->methods[m].name);
                if (ix < 0) {
                    methods = realloc(methods, (size_t)(method_count + 1) * sizeof(*methods));
                    owners = realloc(owners, (size_t)(method_count + 1) * sizeof(*owners));
                    ix = method_count++;
                }
                methods[ix] = pc->methods[m];
                owners[ix] = strdup(pc->owners[m]);
            }
        }
        for (int f = 0; f < own.field_count; f++) {
            int ix = ds_field_index(fields, field_count, own.fields[f].name);
            if (ix >= 0) fields[ix] = own.fields[f];
            else { fields = realloc(fields, (size_t)(field_count + 1) * sizeof(*fields)); fields[field_count++] = own.fields[f]; }
        }
        for (int m = 0; m < own.method_count; m++) {
            int ix = ds_method_index(methods, method_count, own.methods[m].name);
            if (ix < 0) {
                methods = realloc(methods, (size_t)(method_count + 1) * sizeof(*methods));
                owners = realloc(owners, (size_t)(method_count + 1) * sizeof(*owners));
                ix = method_count++;
            }
            methods[ix] = own.methods[m]; owners[ix] = strdup(name);
        }
        for (int m = 0; m < method_count; m++) {
            EidosMethod *method = &methods[m];
            char *body = ds_replace_eidos_refs(method->body, fields, field_count, methods, method_count, method);
            if (parent_count > 0) { char *tmp = ds_replace_super_refs(body, parents[0]); free(body); body = tmp; }
            int has_violation = ds_method_index(methods, method_count, "on_violation") >= 0;
            dsb_f(&generated, "func %s__%s(__eidos_obj", name, method->name);
            for (int p = 0; p < method->param_count; p++) dsb_f(&generated, ", %s", method->params[p]);
            dsb_s(&generated, ") {\n");
            if (strcmp(method->name, "invariant") == 0)
                ds_emit_invariant_checks(&generated, body, has_violation);
            else if (method->expression) dsb_f(&generated, "return %s\n", body);
            else dsb_s(&generated, body);
            dsb_s(&generated, "\n}\n");
            free(body);
        }
        dsb_f(&generated, "func %s(", name);
        for (int f = 0; f < field_count; f++) dsb_f(&generated, "%s__eidos_arg%d", f ? ", " : "", f);
        dsb_f(&generated, ") {\n    __eidos_obj = __eidos_new(\"%s\")\n", name);
        for (int f = 0; f < field_count; f++) {
            char *init = ds_replace_field_refs(fields[f].expr, fields, f, NULL);
            dsb_f(&generated, "    __eidos_obj[\"%s\"] = __eidos_arg%d ?? (%s)\n", fields[f].name, f, init);
            free(init);
        }
        for (int m = 0; m < method_count; m++) {
            dsb_f(&generated, "    __eidos_obj[\"%s\"] = (", methods[m].name);
            if (methods[m].param_count == 0) dsb_s(&generated, "()");
            else for (int p = 0; p < methods[m].param_count; p++) dsb_f(&generated, "%s%s", p ? ", " : "", methods[m].params[p]);
            dsb_f(&generated, " -> %s__%s(__eidos_obj", name, methods[m].name);
            for (int p = 0; p < methods[m].param_count; p++) dsb_f(&generated, ", %s", methods[m].params[p]);
            dsb_s(&generated, "))\n");
        }
        int init_ix = ds_method_index(methods, method_count, "init");
        if (init_ix >= 0 && methods[init_ix].param_count == 0) dsb_s(&generated, "    __eidos_obj[\"init\"]()\n");
        int spawn_ix = ds_method_index(methods, method_count, "on_spawn");
        if (spawn_ix >= 0 && methods[spawn_ix].param_count == 0)
            dsb_s(&generated, "    __eidos_obj[\"on_spawn\"]()\n");
        int invariant_ix = ds_method_index(methods, method_count, "invariant");
        if (invariant_ix >= 0 && methods[invariant_ix].param_count == 0)
            dsb_s(&generated, "    __eidos_obj[\"invariant\"]()\n");
        dsb_s(&generated, "    return __eidos_obj\n}\n\n");
        classes = realloc(classes, (size_t)(class_count + 1) * sizeof(*classes));
        classes[class_count++] = (EidosClass){ strdup(name), fields, field_count, methods, method_count, owners };
        free(name);
        for (int pi = 0; pi < parent_count; pi++) free(parents[pi]);
        free(parents);
        i = end + 1;
    }
    dsb_s(&generated, remainder.data ? remainder.data : "");
    free(remainder.data);
    char *ctors = ds_rewrite_constructor_args(generated.data ? generated.data : "", classes, class_count);
    char *members = ds_rewrite_member_syntax(ctors ? ctors : "", classes, class_count);
    free(ctors);
    char *operators = ds_rewrite_operator_syntax(members ? members : "", classes, class_count);
    free(members);
    char *aliased = ds_replace_eidos_aliases(operators ? operators : "");
    free(operators);
    free(generated.data);
    return aliased;
}

char *desugar_source(const char *src) {
    if (!src) return NULL;
    DsBuf line_pass; dsb_init(&line_pass);
    int in_block = 0;
    const char *p = src;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p + 1) : strlen(p);
        char *raw = ds_xstrndup(p, n);
        char clean[DS_MAX_LINE];
        desugar_line(raw, clean, sizeof clean, &in_block);
        dsb_s(&line_pass, clean);
        free(raw);
        p += n;
    }
    char *eidos = desugar_eidos_source(line_pass.data ? line_pass.data : "");
    free(line_pass.data);
    return eidos;
}

int desugar_file(const char *in, const char *out) {
    FILE *fi = fopen(in, "rb");
    if (!fi) { fprintf(stderr, "desugar: cannot read %s\n", in); return 1; }
    FILE *fo = out ? fopen(out, "wb") : stdout;
    if (!fo) { fprintf(stderr, "desugar: cannot write %s\n", out); fclose(fi); return 1; }
    fseek(fi, 0, SEEK_END);
    long len = ftell(fi);
    fseek(fi, 0, SEEK_SET);
    char *raw = malloc((size_t)len + 1);
    if (!raw) { fclose(fi); if (fo != stdout) fclose(fo); return 1; }
    size_t rd = fread(raw, 1, (size_t)len, fi);
    raw[rd] = 0;
    char *clean = desugar_source(raw);
    if (!clean) {
        free(raw);
        fclose(fi);
        if (fo != stdout) fclose(fo);
        return 1;
    }
    fputs(clean, fo);
    free(clean);
    free(raw);
    fclose(fi);
    if (fo != stdout) fclose(fo);
    fprintf(stderr, "desugar: %s -> %s\n", in, out ? out : "(stdout)");
    return 0;
}

void desugar_mod_register(VM *vm) {
    (void)vm; /* CLI-only: inimerse --desugar in.im out.im */
}
