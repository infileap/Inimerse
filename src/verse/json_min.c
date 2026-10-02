/* json_min.c - minimal strict JSON value tree for the Layer (P1) */

#include "json_min.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VJ_MAX_DEPTH 32

typedef struct {
    const char *p;
    const char *start;
    char       *err;
    size_t      errlen;
    int         depth;
    int         failed;
} VjParser;

static void vj_fail_at(VjParser *P, const char *msg, const char *at) {
    if (!P->failed && P->err && P->errlen)
        snprintf(P->err, P->errlen, "%s at offset %ld", msg, (long)(at - P->start));
    P->failed = 1;
}

static void vj_fail(VjParser *P, const char *msg) { vj_fail_at(P, msg, P->p); }

static void vj_ws(VjParser *P) {
    while (*P->p == ' ' || *P->p == '\t' || *P->p == '\n' || *P->p == '\r') P->p++;
}

static VjVal *vj_new(VjType t) {
    VjVal *v = (VjVal *)calloc(1, sizeof *v);
    if (v) v->type = t;
    return v;
}

static void vj_push(VjVal *v, VjVal *child, const char *key) {
    if (v->type == VJ_ARR) {
        VjVal **ni = (VjVal **)realloc(v->items, (v->n + 1) * sizeof *ni);
        if (!ni) return;
        v->items = ni;
        v->items[v->n] = child;
        v->n++;
    } else {
        char **nk = (char **)realloc(v->keys, (v->nkv + 1) * sizeof *nk);
        if (!nk) return;
        v->keys = nk;
        VjVal **nv = (VjVal **)realloc(v->vals, (v->nkv + 1) * sizeof *nv);
        if (!nv) return;
        v->vals = nv;
        v->keys[v->nkv] = key ? strdup(key) : NULL;
        v->vals[v->nkv] = child;
        v->nkv++;
    }
}

#define VJ_UTF8_REPLACEMENT "\xEF\xBF\xBD"     /* U+FFFD */

static int vj_is_hi_surrogate(unsigned cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
static int vj_is_lo_surrogate(unsigned cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }

/* Write one code point as UTF-8.  Every caller has already reserved four bytes
 * of headroom, which is the widest encoding possible. */
static void vj_put_utf8(char *out, size_t *len, unsigned cp) {
    if (cp < 0x80) {
        out[(*len)++] = (char)cp;
    } else if (cp < 0x800) {
        out[(*len)++] = (char)(0xC0 | (cp >> 6));
        out[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[(*len)++] = (char)(0xE0 | (cp >> 12));
        out[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*len)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[(*len)++] = (char)(0xF0 | (cp >> 18));
        out[(*len)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*len)++] = (char)(0x80 | (cp & 0x3F));
    }
}

/* Read the four hex digits of a `\u` escape.  P->p is the first digit on entry.
 * Returns the code point, or -1 after failing the parse.  The explicit NUL test
 * keeps the read inside the NUL-terminated input: p[0..3] would otherwise be
 * read unconditionally, which is only in bounds for a caller that happens to
 * leave slack past the terminator. */
static int vj_read_hex4(VjParser *P) {
    unsigned cp = 0;
    for (int k = 0; k < 4; k++) {
        char h = P->p[k];
        if (h == '\0') { vj_fail(P, "bad \\u escape"); return -1; }
        if (h >= '0' && h <= '9') cp = cp * 16 + (unsigned)(h - '0');
        else if (h >= 'a' && h <= 'f') cp = cp * 16 + (unsigned)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') cp = cp * 16 + (unsigned)(h - 'A' + 10);
        else { vj_fail(P, "bad \\u escape"); return -1; }
    }
    return (int)cp;
}

/* Append U+FFFD for a surrogate that is not part of a well-formed pair, which
 * is what JSON.parse does. */
static void vj_put_replacement(char *out, size_t *len) {
    memcpy(out + *len, VJ_UTF8_REPLACEMENT, 3);
    *len += 3;
}

static char *vj_parse_string_raw(VjParser *P) {
    if (*P->p != '"') { vj_fail(P, "expected string"); return NULL; }
    P->p++;
    size_t cap = 32, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) { vj_fail(P, "oom"); return NULL; }
    while (*P->p && *P->p != '"') {
        unsigned char c = (unsigned char)*P->p;
        if (len + 4 >= cap) {
            cap *= 2;
            char *nb = (char *)realloc(out, cap);
            if (!nb) { free(out); vj_fail(P, "oom"); return NULL; }
            out = nb;
        }
        if (c == '\\') {
            P->p++;
            char e = *P->p;
            switch (e) {
            case '"':  out[len++] = '"';  P->p++; break;
            case '\\': out[len++] = '\\'; P->p++; break;
            case '/':  out[len++] = '/';  P->p++; break;
            case 'b':  out[len++] = '\b'; P->p++; break;
            case 'f':  out[len++] = '\f'; P->p++; break;
            case 'n':  out[len++] = '\n'; P->p++; break;
            case 'r':  out[len++] = '\r'; P->p++; break;
            case 't':  out[len++] = '\t'; P->p++; break;
            case 'u': {
                P->p++;
                int cp = vj_read_hex4(P);
                if (cp < 0) { free(out); return NULL; }
                P->p += 4;
                if (cp == 0x0000) {
                    /* A NUL byte cannot be represented: VjVal's string is a
                     * length-less char *, so every strlen/strcmp/%s consumer
                     * would see a silently truncated value.  No writer in the
                     * engine can ever produce this escape either -- they all
                     * loop over *p and stop at a NUL -- so refusing it does not
                     * narrow anything the engine can generate.  Documented
                     * divergence from JSON.parse, which yields "x\0y". */
                    free(out);
                    vj_fail(P, "\\u0000 is not representable");
                    return NULL;
                }
                unsigned u = (unsigned)cp;
                if (vj_is_hi_surrogate(u)) {
                    /* A pair is two escapes back to back.  Peek at the second
                     * one: if the next two bytes really are `\u` then its four
                     * hex digits must be read and validated (a malformed
                     * escape stays an error), otherwise this is a lone high
                     * surrogate. */
                    const char *save = P->p;
                    unsigned lo = 0;
                    if (P->p[0] == '\\' && P->p[1] == 'u') {
                        P->p += 2;
                        int lo_cp = vj_read_hex4(P);
                        if (lo_cp < 0) { free(out); return NULL; }
                        P->p += 4;
                        lo = (unsigned)lo_cp;
                    }
                    if (vj_is_lo_surrogate(lo)) {
                        u = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u);
                    } else {
                        P->p = save;
                        vj_put_replacement(out, &len);
                        break;
                    }
                } else if (vj_is_lo_surrogate(u)) {
                    vj_put_replacement(out, &len);
                    break;
                }
                vj_put_utf8(out, &len, u);
                break;
            }
            default:
                free(out);
                vj_fail(P, "bad escape");
                return NULL;
            }
        } else if (c < 0x20) {
            free(out);
            vj_fail(P, "raw control character in string");
            return NULL;
        } else {
            out[len++] = (char)c;
            P->p++;
        }
    }
    if (*P->p != '"') { free(out); vj_fail(P, "unterminated string"); return NULL; }
    P->p++;
    out[len] = '\0';
    return out;
}

static VjVal *vj_parse_value(VjParser *P);

static VjVal *vj_parse_object(VjParser *P) {
    VjVal *obj = vj_new(VJ_OBJ);
    if (!obj) { vj_fail(P, "oom"); return NULL; }
    P->p++;                       /* '{' */
    vj_ws(P);
    if (*P->p == '}') { P->p++; return obj; }
    for (;;) {
        vj_ws(P);
        char *key = vj_parse_string_raw(P);
        if (!key) { vj_free(obj); return NULL; }
        vj_ws(P);
        if (*P->p != ':') { free(key); vj_free(obj); vj_fail(P, "expected ':'"); return NULL; }
        P->p++;
        vj_ws(P);
        VjVal *val = vj_parse_value(P);
        if (!val) { free(key); vj_free(obj); return NULL; }
        vj_push(obj, val, key);
        free(key);
        vj_ws(P);
        if (*P->p == ',') { P->p++; continue; }
        if (*P->p == '}') { P->p++; return obj; }
        vj_free(obj);
        vj_fail(P, "expected ',' or '}'");
        return NULL;
    }
}

static VjVal *vj_parse_array(VjParser *P) {
    VjVal *arr = vj_new(VJ_ARR);
    if (!arr) { vj_fail(P, "oom"); return NULL; }
    P->p++;                       /* '[' */
    vj_ws(P);
    if (*P->p == ']') { P->p++; return arr; }
    for (;;) {
        vj_ws(P);
        VjVal *val = vj_parse_value(P);
        if (!val) { vj_free(arr); return NULL; }
        vj_push(arr, val, NULL);
        vj_ws(P);
        if (*P->p == ',') { P->p++; continue; }
        if (*P->p == ']') { P->p++; return arr; }
        vj_free(arr);
        vj_fail(P, "expected ',' or ']'");
        return NULL;
    }
}

static VjVal *vj_parse_number(VjParser *P) {
    const char *start = P->p;
    if (*P->p == '-') P->p++;
    if (*P->p < '0' || *P->p > '9') { vj_fail_at(P, "bad number", start); return NULL; }
    long long v = 0;
    while (*P->p >= '0' && *P->p <= '9') {
        v = v * 10 + (*P->p - '0');
        P->p++;
    }
    /* integers only: a fractional or exponent part would not round-trip
     * through the canonical writer, so reject it outright. */
    if (*P->p == '.' || *P->p == 'e' || *P->p == 'E') {
        vj_fail_at(P, "non-integer number unsupported", start);
        return NULL;
    }
    VjVal *n = vj_new(VJ_INT);
    if (!n) { vj_fail(P, "oom"); return NULL; }
    n->i = (start[0] == '-') ? -v : v;
    return n;
}

static VjVal *vj_parse_lit(VjParser *P, const char *lit, VjType t, int b) {
    size_t n = strlen(lit);
    if (strncmp(P->p, lit, n) != 0) { vj_fail(P, "bad literal"); return NULL; }
    P->p += n;
    VjVal *v = vj_new(t);
    if (!v) { vj_fail(P, "oom"); return NULL; }
    v->b = b;
    return v;
}

static VjVal *vj_parse_value(VjParser *P) {
    if (P->failed) return NULL;
    if (++P->depth > VJ_MAX_DEPTH) { vj_fail(P, "nesting too deep"); return NULL; }
    vj_ws(P);
    VjVal *v = NULL;
    switch (*P->p) {
    case '{': v = vj_parse_object(P); break;
    case '[': v = vj_parse_array(P);  break;
    case '"': {
        char *s = vj_parse_string_raw(P);
        if (s) { v = vj_new(VJ_STR); if (v) v->s = s; else free(s); }
        break;
    }
    case 't': v = vj_parse_lit(P, "true",  VJ_BOOL, 1); break;
    case 'f': v = vj_parse_lit(P, "false", VJ_BOOL, 0); break;
    case 'n': v = vj_parse_lit(P, "null",  VJ_NULL, 0); break;
    default:
        if (*P->p == '-' || (*P->p >= '0' && *P->p <= '9')) v = vj_parse_number(P);
        else vj_fail(P, "unexpected character");
        break;
    }
    P->depth--;
    return v;
}

VjVal *vj_parse(const char *text, char *err, size_t errlen) {
    if (err && errlen) err[0] = '\0';
    if (!text) return NULL;
    VjParser P;
    memset(&P, 0, sizeof P);
    P.p = text;
    P.start = text;
    P.err = err;
    P.errlen = errlen;
    VjVal *v = vj_parse_value(&P);
    if (!v || P.failed) { vj_free(v); return NULL; }
    vj_ws(&P);
    if (*P.p != '\0') { vj_free(v); vj_fail(&P, "trailing content"); return NULL; }
    return v;
}

void vj_free(VjVal *v) {
    if (!v) return;
    free(v->s);
    for (size_t i = 0; i < v->n; i++) vj_free(v->items[i]);
    free(v->items);
    for (size_t i = 0; i < v->nkv; i++) {
        free(v->keys[i]);
        vj_free(v->vals[i]);
    }
    free(v->keys);
    free(v->vals);
    free(v);
}

const VjVal *vj_get(const VjVal *v, const char *key) {
    if (!v || v->type != VJ_OBJ || !key) return NULL;
    for (size_t i = 0; i < v->nkv; i++)
        if (v->keys[i] && strcmp(v->keys[i], key) == 0) return v->vals[i];
    return NULL;
}

long long vj_int(const VjVal *v, long long dflt) {
    return (v && v->type == VJ_INT) ? v->i : dflt;
}

int vj_bool(const VjVal *v, int dflt) {
    return (v && v->type == VJ_BOOL) ? v->b : dflt;
}

const char *vj_str(const VjVal *v, const char *dflt) {
    return (v && v->type == VJ_STR && v->s) ? v->s : dflt;
}
