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

#define DS_MAX_LINE 16384

static void ds_emit(char *dst, int *d, int cap, const char *s) {
    while (*s && *d < cap - 1) dst[(*d)++] = *s++;
}

static void desugar_line(const char *src, char *dst, int cap, int *in_block) {
    int d = 0;
    const char *p = src;
    int in_str = 0;
    char q = 0;
    int last_code_semi = -1;   /* dst index of last code-region ';' */
    int line_code_start = -1;  /* first non-space code char in dst */
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
        int next_is_word = isalnum((unsigned char)p[5]) || p[5] == '_';
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
    dst[d] = 0;
}

int desugar_file(const char *in, const char *out) {
    FILE *fi = fopen(in, "rb");
    if (!fi) { fprintf(stderr, "desugar: cannot read %s\n", in); return 1; }
    FILE *fo = out ? fopen(out, "wb") : stdout;
    if (!fo) { fprintf(stderr, "desugar: cannot write %s\n", out); fclose(fi); return 1; }
    char raw[DS_MAX_LINE];
    char clean[DS_MAX_LINE];
    int in_block = 0;
    int lineno = 0;
    while (fgets(raw, sizeof raw, fi)) {
        lineno++;
        desugar_line(raw, clean, sizeof clean, &in_block);
        fputs(clean, fo);
    }
    fclose(fi);
    if (fo != stdout) fclose(fo);
    fprintf(stderr, "desugar: %d lines processed -> %s\n", lineno, out ? out : "(stdout)");
    return 0;
}

void desugar_mod_register(VM *vm) {
    (void)vm; /* CLI-only: inimerse --desugar in.im out.im */
}
