/* upp.c - engine-side UPP v1: wire format + session state machine
 *
 * Mirrors tools/upp_reference.js and tools/upp_session.js.  Those two files are
 * the read-only reference; this module must not drift from them, so every
 * object key is written in the reference's insertion order and every refusal
 * carries the reference's exact message text.  tools/upp_engine_crosscheck.js
 * enforces that by diffing transcripts byte for byte.
 */
/* clock_gettime() is POSIX; make it visible even when the build uses strict
 * -std=c11 without GNU extensions.  Must precede every system header. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "upp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/* --------------------------------------------------------------- utilities */

static int upp_fail(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

int upp_is_safe_int(long long value) {
    const long long MAX_SAFE = 9007199254740991LL; /* Number.MAX_SAFE_INTEGER */
    return value >= -MAX_SAFE && value <= MAX_SAFE;
}

long long upp_now_ms(void) {
#if defined(_WIN32)
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* 100 ns ticks since 1601-01-01; convert to ms since the Unix epoch. */
    return (long long)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return (long long)time(NULL) * 1000;
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
#endif
}

static int ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* String.prototype.trim() emptiness (ASCII whitespace; see upp.h note). */
static int str_is_blank(const char *s) {
    if (!s) return 1;
    for (const char *p = s; *p; p++)
        if (!ascii_space((unsigned char)*p)) return 0;
    return 1;
}

static int ascii_alnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* `payload || {}` semantics: JS falsy values fall back. */
static int vj_truthy(const VjVal *v) {
    if (!v) return 0;
    switch (v->type) {
    case VJ_NULL: return 0;
    case VJ_BOOL: return v->b ? 1 : 0;
    case VJ_INT:  return v->i != 0;
    case VJ_STR:  return v->s && v->s[0] ? 1 : 0;
    default:      return 1; /* arrays and objects are truthy, even empty */
    }
}

/* Member lookup on a value that may be absent or not an object. */
static const VjVal *pget(const VjVal *obj, const char *key) {
    if (!obj || obj->type != VJ_OBJ) return NULL;
    return vj_get(obj, key);
}

/* ----------------------------------------------------------------- buffers */

void upp_buf_init(UppBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->oom = 0;
}

void upp_buf_free(UppBuf *b) {
    free(b->data);
    upp_buf_init(b);
}

static int buf_reserve(UppBuf *b, size_t extra) {
    if (b->oom) return -1;
    if (b->len + extra + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + extra + 1) cap *= 2;
        char *grown = (char *)realloc(b->data, cap);
        if (!grown) {
            b->oom = 1;
            return -1;
        }
        b->data = grown;
        b->cap = cap;
    }
    return 0;
}

int upp_buf_putn(UppBuf *b, const char *s, size_t n) {
    if (buf_reserve(b, n) != 0) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

int upp_buf_puts(UppBuf *b, const char *s) {
    return upp_buf_putn(b, s, strlen(s));
}

int upp_buf_putc(UppBuf *b, int c) {
    if (buf_reserve(b, 1) != 0) return -1;
    b->data[b->len++] = (char)c;
    b->data[b->len] = '\0';
    return 0;
}

int upp_buf_put_ll(UppBuf *b, long long value) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", value);
    return upp_buf_puts(b, tmp);
}

/* JSON.stringify escapes: quote, backslash, the five short escapes, and
 * \u00xx for the remaining C0 controls.  Bytes >= 0x80 pass through as UTF-8,
 * which is what JSON.stringify does for non-ASCII. */
int upp_json_write_string(UppBuf *b, const char *s) {
    if (upp_buf_putc(b, '"') != 0) return -1;
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        switch (*p) {
        case '"':  if (upp_buf_puts(b, "\\\"") != 0) return -1; break;
        case '\\': if (upp_buf_puts(b, "\\\\") != 0) return -1; break;
        case '\b': if (upp_buf_puts(b, "\\b") != 0) return -1; break;
        case '\f': if (upp_buf_puts(b, "\\f") != 0) return -1; break;
        case '\n': if (upp_buf_puts(b, "\\n") != 0) return -1; break;
        case '\r': if (upp_buf_puts(b, "\\r") != 0) return -1; break;
        case '\t': if (upp_buf_puts(b, "\\t") != 0) return -1; break;
        default:
            if (*p < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof tmp, "\\u%04x", *p);
                if (upp_buf_puts(b, tmp) != 0) return -1;
            } else if (upp_buf_putc(b, *p) != 0) {
                return -1;
            }
        }
    }
    return upp_buf_putc(b, '"');
}

static int json_write_depth(UppBuf *b, const VjVal *v, int depth) {
    if (!v || depth > 32) return upp_buf_puts(b, "null");
    switch (v->type) {
    case VJ_NULL: return upp_buf_puts(b, "null");
    case VJ_BOOL: return upp_buf_puts(b, v->b ? "true" : "false");
    case VJ_INT:  return upp_buf_put_ll(b, v->i);
    case VJ_STR:  return upp_json_write_string(b, v->s);
    case VJ_ARR:
        if (upp_buf_putc(b, '[') != 0) return -1;
        for (size_t i = 0; i < v->n; i++) {
            if (i && upp_buf_putc(b, ',') != 0) return -1;
            if (json_write_depth(b, v->items[i], depth + 1) != 0) return -1;
        }
        return upp_buf_putc(b, ']');
    case VJ_OBJ:
        if (upp_buf_putc(b, '{') != 0) return -1;
        for (size_t i = 0; i < v->nkv; i++) {
            if (i && upp_buf_putc(b, ',') != 0) return -1;
            if (upp_json_write_string(b, v->keys[i]) != 0) return -1;
            if (upp_buf_putc(b, ':') != 0) return -1;
            if (json_write_depth(b, v->vals[i], depth + 1) != 0) return -1;
        }
        return upp_buf_putc(b, '}');
    }
    return upp_buf_puts(b, "null");
}

int upp_json_write(UppBuf *b, const VjVal *v) {
    return json_write_depth(b, v, 0);
}

/* ------------------------------------------------------------ manifest rules */

static int match_manifest_id(const char *s) {
    size_t n = strlen(s);
    if (n < 1 || n > 64) return 0;
    if (!ascii_alnum((unsigned char)s[0])) return 0;
    for (size_t i = 1; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(ascii_alnum(c) || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/* ^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$ */
static int match_semver(const char *s) {
    size_t i = 0;
    for (int part = 0; part < 3; part++) {
        size_t start = i;
        while (s[i] >= '0' && s[i] <= '9') i++;
        if (i == start) return 0;
        if (part < 2) {
            if (s[i] != '.') return 0;
            i++;
        }
    }
    if (s[i] == '\0') return 1;
    if (s[i] != '-' && s[i] != '+') return 0;
    i++;
    if (s[i] == '\0') return 0;
    for (; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!(ascii_alnum(c) || c == '.' || c == '-')) return 0;
    }
    return 1;
}

/* ^\d+(?:\.\.\d+)?$ */
static int match_abi_range(const char *s) {
    size_t i = 0;
    if (s[0] < '0' || s[0] > '9') return 0;
    while (s[i] >= '0' && s[i] <= '9') i++;
    if (s[i] == '\0') return 1;
    if (s[i] != '.' || s[i + 1] != '.') return 0;
    i += 2;
    if (s[i] < '0' || s[i] > '9') return 0;
    while (s[i] >= '0' && s[i] <= '9') i++;
    return s[i] == '\0';
}

int upp_validate_manifest(const VjVal *manifest, char *err, size_t errlen) {
    if (!manifest || manifest->type != VJ_OBJ) return upp_fail(err, errlen, "manifest must be an object");
    static const char *const required[] = { "id", "name", "version", "engine", "entry" };
    for (size_t i = 0; i < sizeof required / sizeof required[0]; i++) {
        const VjVal *v = vj_get(manifest, required[i]);
        if (!v || v->type != VJ_STR || !v->s || str_is_blank(v->s))
            return upp_fail(err, errlen, "manifest.%s is required", required[i]);
    }
    if (!match_manifest_id(vj_get(manifest, "id")->s))
        return upp_fail(err, errlen, "manifest.id has invalid characters");
    if (!match_semver(vj_get(manifest, "version")->s))
        return upp_fail(err, errlen, "manifest.version must be semver");

    const VjVal *files = vj_get(manifest, "files");
    if (files && files->type != VJ_OBJ) return upp_fail(err, errlen, "manifest.files must be an object");

    const VjVal *caps = vj_get(manifest, "capabilities");
    if (caps) {
        int bad = caps->type != VJ_ARR;
        for (size_t i = 0; !bad && i < caps->n; i++)
            if (!caps->items[i] || caps->items[i]->type != VJ_STR) bad = 1;
        if (bad) return upp_fail(err, errlen, "manifest.capabilities must be an array of strings");
    }

    const VjVal *abi = vj_get(manifest, "abi");
    if (abi && (abi->type != VJ_INT || abi->i < 1))
        return upp_fail(err, errlen, "manifest.abi must be a positive integer");

    const VjVal *range = vj_get(manifest, "abiRange");
    if (range && (range->type != VJ_STR || !match_abi_range(range->s)))
        return upp_fail(err, errlen, "manifest.abiRange must be N or N..M");
    return 0;
}

/* m?.abi || 1 -- validated manifests have abi >= 1, but acceptHello() reads a
 * remote manifest that was never validated here, so keep the reference's
 * coercion (0 and absent both mean 1). */
static long long abi_or_default(long long abi, int has_abi) {
    return (has_abi && abi) ? abi : 1;
}

typedef struct {
    long long lo;
    long long hi;
    int       has_range;
    char      text[UPP_ABI_RANGE_MAX];
} UppAbiRange;

/* const range = m => { const v = m?.abi || 1, r = m?.abiRange || String(v),
 *   [lo,hi] = (r.includes('..') ? r : `${r}..${r}`).split('..').map(Number); } */
static void resolve_abi_range(long long abi, int has_abi, const char *abi_range_text, UppAbiRange *out) {
    memset(out, 0, sizeof *out);
    long long v = abi_or_default(abi, has_abi);
    if (abi_range_text && *abi_range_text) {
        snprintf(out->text, sizeof out->text, "%s", abi_range_text);
        out->has_range = 1;
        const char *r = out->text;
        const char *p = r;
        long long a = 0, b = 0, seen = 0;
        while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; seen = 1; }
        if (!seen) a = 0;
        if (p[0] == '.' && p[1] == '.') {
            p += 2;
            while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
        } else {
            b = a;
        }
        out->lo = a;
        out->hi = b;
        return;
    }
    out->lo = v;
    out->hi = v;
}

static void manifest_abi_facts(const VjVal *manifest, long long *abi, int *has_abi,
                               char *range, size_t rangecap, int *has_range) {
    *abi = 1;
    *has_abi = 0;
    *has_range = 0;
    if (range && rangecap) range[0] = '\0';
    if (!manifest || manifest->type != VJ_OBJ) return;
    const VjVal *a = vj_get(manifest, "abi");
    if (a && a->type == VJ_INT) {
        *abi = a->i;
        *has_abi = 1;
    }
    const VjVal *r = vj_get(manifest, "abiRange");
    if (r && r->type == VJ_STR && r->s && range && rangecap) {
        snprintf(range, rangecap, "%s", r->s);
        *has_range = 1;
    }
}

/* ----------------------------------------------------------- frame builders */

static const char *const UPP_ROLES[] = { "host", "verse", "client" };
static const char *const UPP_CONTROL_TYPES[] = {
    "heartbeat", "start", "stop", "log", "crash", "incompatible"
};
static const char *const UPP_LOG_LEVELS[] = { "debug", "info", "warn", "error" };

static int list_has(const char *const *list, size_t n, const char *value) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(list[i], value) == 0) return 1;
    return 0;
}

#define UPP_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* { upp: 1, type, id?, payload: ... } -- key order is the reference's. */
static void frame_prefix(UppBuf *b, const char *type, const char *id) {
    upp_buf_puts(b, "{\"upp\":");
    upp_buf_put_ll(b, UPP_VERSION);
    upp_buf_puts(b, ",\"type\":");
    upp_json_write_string(b, type);
    if (id && *id) {
        upp_buf_puts(b, ",\"id\":");
        upp_json_write_string(b, id);
    }
    upp_buf_puts(b, ",\"payload\":");
}

static int frame_end(UppBuf *b, char *err, size_t errlen) {
    if (upp_buf_putc(b, '}') != 0 || b->oom) return upp_fail(err, errlen, "out of memory");
    return 0;
}

int upp_frame_build(UppBuf *out, const char *type, const char *id,
                    const char *payload_json, char *err, size_t errlen) {
    if (!type || !*type) return upp_fail(err, errlen, "frame type is required");
    frame_prefix(out, type, id);
    upp_buf_puts(out, payload_json ? payload_json : "{}");
    return frame_end(out, err, errlen);
}

int upp_frame_control(UppBuf *out, const char *type, const char *payload_json,
                      const char *id, char *err, size_t errlen) {
    if (!type || !list_has(UPP_CONTROL_TYPES, UPP_ARRAY_LEN(UPP_CONTROL_TYPES), type))
        return upp_fail(err, errlen, "unsupported UPP control type: %s", type ? type : "undefined");
    return upp_frame_build(out, type, id, payload_json, err, errlen);
}

int upp_frame_heartbeat(UppBuf *out, long long seq, long long timestamp,
                        char *err, size_t errlen) {
    if (!upp_is_safe_int(seq) || seq < 0)
        return upp_fail(err, errlen, "heartbeat sequence must be a non-negative integer");
    /* A C long long is always finite; the reference's Number.isFinite guard has
     * no analogue here because json_min only ever yields integers. */
    frame_prefix(out, "heartbeat", NULL);
    upp_buf_puts(out, "{\"seq\":");
    upp_buf_put_ll(out, seq);
    upp_buf_puts(out, ",\"timestamp\":");
    upp_buf_put_ll(out, timestamp);
    upp_buf_puts(out, "}");
    return frame_end(out, err, errlen);
}

int upp_frame_start(UppBuf *out, const char *entry, const VjVal *args,
                    char *err, size_t errlen) {
    if (!entry || str_is_blank(entry)) return upp_fail(err, errlen, "start entry is required");
    if (args) {
        int bad = args->type != VJ_ARR;
        for (size_t i = 0; !bad && i < args->n; i++)
            if (!args->items[i] || args->items[i]->type != VJ_STR) bad = 1;
        if (bad) return upp_fail(err, errlen, "start args must be strings");
    }
    frame_prefix(out, "start", NULL);
    upp_buf_puts(out, "{\"entry\":");
    upp_json_write_string(out, entry);
    upp_buf_puts(out, ",\"args\":[");
    if (args)
        for (size_t i = 0; i < args->n; i++) {
            if (i) upp_buf_putc(out, ',');
            upp_json_write_string(out, args->items[i]->s);
        }
    upp_buf_puts(out, "]}");
    return frame_end(out, err, errlen);
}

int upp_frame_stop(UppBuf *out, const char *reason, char *err, size_t errlen) {
    if (!reason || str_is_blank(reason)) return upp_fail(err, errlen, "stop reason is required");
    frame_prefix(out, "stop", NULL);
    upp_buf_puts(out, "{\"reason\":");
    upp_json_write_string(out, reason);
    upp_buf_puts(out, "}");
    return frame_end(out, err, errlen);
}

int upp_frame_log(UppBuf *out, const char *level, const char *message,
                  long long timestamp, char *err, size_t errlen) {
    if (!level || !list_has(UPP_LOG_LEVELS, UPP_ARRAY_LEN(UPP_LOG_LEVELS), level))
        return upp_fail(err, errlen, "invalid UPP log level");
    if (!message) return upp_fail(err, errlen, "log message must be a string");
    frame_prefix(out, "log", NULL);
    upp_buf_puts(out, "{\"level\":");
    upp_json_write_string(out, level);
    upp_buf_puts(out, ",\"message\":");
    upp_json_write_string(out, message);
    upp_buf_puts(out, ",\"timestamp\":");
    upp_buf_put_ll(out, timestamp);
    upp_buf_puts(out, "}");
    return frame_end(out, err, errlen);
}

int upp_frame_crash(UppBuf *out, const char *error, long long exit_code,
                    int has_exit_code, long long timestamp, char *err, size_t errlen) {
    if (!error || str_is_blank(error)) return upp_fail(err, errlen, "crash error is required");
    if (has_exit_code && exit_code < 0) return upp_fail(err, errlen, "invalid crash exit code");
    frame_prefix(out, "crash", NULL);
    upp_buf_puts(out, "{\"error\":");
    upp_json_write_string(out, error);
    upp_buf_puts(out, ",\"exitCode\":");
    if (has_exit_code) upp_buf_put_ll(out, exit_code);
    else upp_buf_puts(out, "null");
    upp_buf_puts(out, ",\"timestamp\":");
    upp_buf_put_ll(out, timestamp);
    upp_buf_puts(out, "}");
    return frame_end(out, err, errlen);
}

int upp_frame_incompatible(UppBuf *out, long long required, long long actual,
                           char *err, size_t errlen) {
    if (!upp_is_safe_int(required)) return upp_fail(err, errlen, "required protocol must be an integer");
    if (!upp_is_safe_int(actual)) return upp_fail(err, errlen, "actual protocol must be an integer");
    frame_prefix(out, "incompatible", NULL);
    upp_buf_puts(out, "{\"required\":");
    upp_buf_put_ll(out, required);
    upp_buf_puts(out, ",\"actual\":");
    upp_buf_put_ll(out, actual);
    upp_buf_puts(out, "}");
    return frame_end(out, err, errlen);
}

int upp_frame_welcome(UppBuf *out, const char *peer_role, int has_peer_role,
                      long long abi, char *err, size_t errlen) {
    frame_prefix(out, "welcome", NULL);
    upp_buf_puts(out, "{\"protocol\":");
    upp_buf_put_ll(out, UPP_VERSION);
    if (has_peer_role) {
        upp_buf_puts(out, ",\"peerRole\":");
        upp_json_write_string(out, peer_role);
    }
    /* acceptHello() negotiates with local capabilities [], so the intersection
     * the reference computes is always empty. */
    upp_buf_puts(out, ",\"capabilities\":[],\"abi\":");
    upp_buf_put_ll(out, abi);
    upp_buf_puts(out, "}");
    return frame_end(out, err, errlen);
}

static int cmp_cstr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int upp_frame_hello(UppBuf *out, const char *role, const VjVal *manifest,
                    const VjVal *capabilities, char *err, size_t errlen) {
    if (!role || !list_has(UPP_ROLES, UPP_ARRAY_LEN(UPP_ROLES), role))
        return upp_fail(err, errlen, "invalid role: %s", role ? role : "undefined");
    if (upp_validate_manifest(manifest, err, errlen) != 0) return -1;

    /* const caps = [...new Set(capabilities.filter(x => typeof x === 'string'))].sort();
     * A non-array `capabilities` behaves as [] here, matching Array.prototype
     * access on a non-array value in the reference. */
    const char **unique = NULL;
    size_t n = 0;
    if (capabilities && capabilities->type == VJ_ARR) {
        for (size_t i = 0; i < capabilities->n; i++) {
            const VjVal *item = capabilities->items[i];
            if (!item || item->type != VJ_STR || !item->s) continue;
            int seen = 0;
            for (size_t j = 0; j < n; j++)
                if (strcmp(unique[j], item->s) == 0) seen = 1;
            if (seen) continue;
            const char **grown = (const char **)realloc(unique, (n + 1) * sizeof *grown);
            if (!grown) {
                free(unique);
                return upp_fail(err, errlen, "out of memory");
            }
            unique = grown;
            unique[n++] = item->s;
        }
    }
    if (n > 1) qsort(unique, n, sizeof *unique, cmp_cstr);

    frame_prefix(out, "hello", NULL);
    upp_buf_puts(out, "{\"role\":");
    upp_json_write_string(out, role);
    upp_buf_puts(out, ",\"manifest\":");
    upp_json_write(out, manifest);
    upp_buf_puts(out, ",\"capabilities\":[");
    for (size_t i = 0; i < n; i++) {
        if (i) upp_buf_putc(out, ',');
        upp_json_write_string(out, unique[i]);
    }
    upp_buf_puts(out, "]}");
    free(unique);
    return frame_end(out, err, errlen);
}

/* ------------------------------------------------------------- encode/decode */

static const char *describe_upp_value(const VjVal *v, char *scratch, size_t cap) {
    if (!v) {
        snprintf(scratch, cap, "undefined");
        return scratch;
    }
    switch (v->type) {
    case VJ_INT:  snprintf(scratch, cap, "%lld", v->i); break;
    case VJ_STR:  snprintf(scratch, cap, "%s", v->s ? v->s : ""); break;
    case VJ_BOOL: snprintf(scratch, cap, "%s", v->b ? "true" : "false"); break;
    case VJ_NULL: snprintf(scratch, cap, "null"); break;
    default:      snprintf(scratch, cap, "[object Object]"); break;
    }
    return scratch;
}

int upp_encode(UppBuf *out, const VjVal *message, char *err, size_t errlen) {
    if (!message || message->type != VJ_OBJ) return upp_fail(err, errlen, "message must be an object");
    const VjVal *upp = vj_get(message, "upp");
    if (!upp || upp->type != VJ_INT || upp->i != UPP_VERSION) {
        char scratch[64];
        return upp_fail(err, errlen, "unsupported UPP version: %s",
                        describe_upp_value(upp, scratch, sizeof scratch));
    }
    UppBuf line;
    upp_buf_init(&line);
    upp_json_write(&line, message);
    if (line.oom) {
        upp_buf_free(&line);
        return upp_fail(err, errlen, "out of memory");
    }
    if (line.len > UPP_MAX_FRAME_BYTES) {
        upp_buf_free(&line);
        return upp_fail(err, errlen, "UPP frame exceeds 1 MiB");
    }
    int rc = upp_buf_putn(out, line.data ? line.data : "", line.len);
    upp_buf_free(&line);
    if (rc != 0) return upp_fail(err, errlen, "out of memory");
    if (upp_buf_putc(out, '\n') != 0) return upp_fail(err, errlen, "out of memory");
    return 0;
}

int upp_encode_text(UppBuf *out, const char *json, char *err, size_t errlen) {
    char parse_err[UPP_ERR_MAX];
    VjVal *v = vj_parse(json, parse_err, sizeof parse_err);
    if (!v) return upp_fail(err, errlen, "%s", parse_err[0] ? parse_err : "invalid JSON");
    int rc = upp_encode(out, v, err, errlen);
    vj_free(v);
    return rc;
}

VjVal *upp_decode_line(const char *line, size_t len, char *err, size_t errlen) {
    if (len > UPP_MAX_FRAME_BYTES) {
        upp_fail(err, errlen, "UPP frame exceeds 1 MiB");
        return NULL;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        upp_fail(err, errlen, "out of memory");
        return NULL;
    }
    memcpy(copy, line, len);
    copy[len] = '\0';
    char parse_err[UPP_ERR_MAX];
    VjVal *v = vj_parse(copy, parse_err, sizeof parse_err);
    free(copy);
    if (!v) {
        upp_fail(err, errlen, "%s", parse_err[0] ? parse_err : "invalid JSON");
        return NULL;
    }
    if (v->type != VJ_OBJ) {
        vj_free(v);
        upp_fail(err, errlen, "message must be an object");
        return NULL;
    }
    const VjVal *upp = vj_get(v, "upp");
    const VjVal *type = vj_get(v, "type");
    if (!upp || upp->type != VJ_INT || upp->i != UPP_VERSION || !type || type->type != VJ_STR) {
        vj_free(v);
        upp_fail(err, errlen, "invalid UPP frame header");
        return NULL;
    }
    return v;
}

static int decoder_append(UppDecoder *d, const char *chunk, size_t len) {
    if (d->len + len + 1 > d->cap) {
        size_t cap = d->cap ? d->cap : 256;
        while (cap < d->len + len + 1) cap *= 2;
        char *grown = (char *)realloc(d->pending, cap);
        if (!grown) return -1;
        d->pending = grown;
        d->cap = cap;
    }
    memcpy(d->pending + d->len, chunk, len);
    d->len += len;
    d->pending[d->len] = '\0';
    return 0;
}

static int decoder_fail(UppDecoder *d, const char *msg) {
    d->failed = 1;
    snprintf(d->error, sizeof d->error, "%s", msg);
    return -1;
}

void upp_decoder_init(UppDecoder *d, void (*on_message)(const VjVal *, void *), void *user) {
    memset(d, 0, sizeof *d);
    d->on_message = on_message;
    d->user = user;
}

void upp_decoder_free(UppDecoder *d) {
    free(d->pending);
    memset(d, 0, sizeof *d);
}

static int decoder_dispatch(UppDecoder *d, const char *line, size_t len) {
    char err[UPP_ERR_MAX];
    VjVal *v = upp_decode_line(line, len, err, sizeof err);
    if (!v) return decoder_fail(d, err);
    d->on_message(v, d->user);
    vj_free(v);
    return 0;
}

/* `line.replace(/\r$/, '')` then `if (line.trim())`. */
static int decoder_emit_line(UppDecoder *d, const char *line, size_t len) {
    if (len > 0 && line[len - 1] == '\r') len--;
    char *copy = (char *)malloc(len + 1);
    if (!copy) return decoder_fail(d, "out of memory");
    memcpy(copy, line, len);
    copy[len] = '\0';
    int blank = str_is_blank(copy);
    int rc = 0;
    if (!blank) rc = decoder_dispatch(d, line, len);
    free(copy);
    return rc;
}

int upp_decoder_push(UppDecoder *d, const char *chunk, size_t len) {
    if (d->failed) return -1;
    if (decoder_append(d, chunk, len) != 0) return decoder_fail(d, "out of memory");
    if (d->len > UPP_DECODER_MAX_PENDING) return decoder_fail(d, "UPP input buffer is too large");
    size_t start = 0;
    for (size_t i = 0; i < d->len; i++) {
        if (d->pending[i] != '\n') continue;
        if (decoder_emit_line(d, d->pending + start, i - start) != 0) return -1;
        start = i + 1;
    }
    if (start) {
        memmove(d->pending, d->pending + start, d->len - start);
        d->len -= start;
        d->pending[d->len] = '\0';
    }
    return 0;
}

int upp_decoder_end(UppDecoder *d) {
    if (d->failed) return -1;
    if (d->len && !str_is_blank(d->pending))
        return decoder_emit_line(d, d->pending, d->len);
    d->len = 0;
    if (d->pending) d->pending[0] = '\0';
    return 0;
}

/* ------------------------------------------------------------ state machine */

const char *upp_state_name(UppState state) {
    switch (state) {
    case UPP_STATE_IDLE:         return "idle";
    case UPP_STATE_RUNNING:      return "running";
    case UPP_STATE_STOPPED:      return "stopped";
    case UPP_STATE_CRASHED:      return "crashed";
    case UPP_STATE_INCOMPATIBLE: return "incompatible";
    }
    return "unknown";
}

int upp_state_from_name(const char *name) {
    for (int i = 0; i <= (int)UPP_STATE_INCOMPATIBLE; i++)
        if (name && strcmp(name, upp_state_name((UppState)i)) == 0) return i;
    return -1;
}

static void session_set_error(UppSession *s, const char *msg) {
    s->error_set = 1;
    snprintf(s->error, sizeof s->error, "%s", msg ? msg : "");
}

int upp_session_init(UppSession *s, const char *role, const VjVal *manifest,
                     char *err, size_t errlen) {
    memset(s, 0, sizeof *s);
    s->state = UPP_STATE_IDLE;
    if (!role || !list_has(UPP_ROLES, UPP_ARRAY_LEN(UPP_ROLES), role))
        return upp_fail(err, errlen, "invalid role");
    snprintf(s->role, sizeof s->role, "%s", role);
    long long abi;
    int has_abi;
    manifest_abi_facts(manifest, &abi, &has_abi, s->local_abi_range,
                       sizeof s->local_abi_range, &s->has_local_abi_range);
    s->local_abi = has_abi ? abi : 1;
    return 0;
}

int upp_session_apply(UppSession *s, const VjVal *message, char *err, size_t errlen) {
    if (!message || message->type != VJ_OBJ) return upp_fail(err, errlen, "UPP message required");
    const VjVal *type = vj_get(message, "type");
    if (!type || type->type != VJ_STR || !type->s)
        return upp_fail(err, errlen, "UPP message required");
    const VjVal *p = vj_get(message, "payload");
    const char *t = type->s;

    if (strcmp(t, "heartbeat") == 0) {
        const VjVal *seq = pget(p, "seq");
        if (!seq || seq->type != VJ_INT || !upp_is_safe_int(seq->i))
            return upp_fail(err, errlen, "heartbeat sequence out of order");
        if (seq->i < s->last_heartbeat)
            return upp_fail(err, errlen, "heartbeat sequence out of order");
        const VjVal *at = pget(p, "timestamp");
        int finite = at && at->type == VJ_INT && upp_is_safe_int(at->i);
        if (finite && s->last_heartbeat_at > 0 && at->i < s->last_heartbeat_at)
            return upp_fail(err, errlen, "heartbeat timestamp out of order");
        s->last_heartbeat = seq->i;
        s->last_heartbeat_at = finite ? at->i : upp_now_ms();
        return 0;
    }
    if (strcmp(t, "start") == 0) {
        if (s->state == UPP_STATE_RUNNING) return 0; /* idempotent, not an error */
        if (s->state == UPP_STATE_CRASHED || s->state == UPP_STATE_INCOMPATIBLE)
            return upp_fail(err, errlen, "cannot start from %s", upp_state_name(s->state));
        s->state = UPP_STATE_RUNNING;
        return 0;
    }
    if (strcmp(t, "stop") == 0) {
        s->state = UPP_STATE_STOPPED; /* unconditional */
        return 0;
    }
    if (strcmp(t, "crash") == 0) {
        s->state = UPP_STATE_CRASHED;
        const VjVal *e = pget(p, "error");
        if (e && e->type == VJ_STR && e->s && *e->s) {
            session_set_error(s, e->s);
        } else if (e && e->type == VJ_INT && e->i != 0) {
            char scratch[32];
            snprintf(scratch, sizeof scratch, "%lld", e->i);
            session_set_error(s, scratch);
        } else {
            session_set_error(s, "unknown crash");
        }
        return 0;
    }
    return 0; /* log / incompatible / anything else: no state change */
}

int upp_session_accept_hello(UppSession *s, const VjVal *remote_hello,
                             UppBuf *welcome_out, char *err, size_t errlen) {
    char msg[UPP_ERR_MAX];
    msg[0] = '\0';
    long long abi = 0;
    int ok = 0;
    const char *peer_role = NULL;
    int has_peer_role = 0;

    do {
        if (!remote_hello || remote_hello->type != VJ_OBJ) {
            snprintf(msg, sizeof msg, "remote hello must be an object");
            break;
        }
        /* b = remote.payload || remote */
        const VjVal *remote_payload = vj_get(remote_hello, "payload");
        const VjVal *b = vj_truthy(remote_payload) ? remote_payload : remote_hello;

        const VjVal *role = pget(b, "role");
        if (role && role->type == VJ_STR && role->s) {
            peer_role = role->s;
            has_peer_role = 1;
            if (strcmp(peer_role, s->role) == 0) {
                snprintf(msg, sizeof msg, "UPP peers must use different roles");
                break;
            }
        }

        UppAbiRange ra, rb;
        resolve_abi_range(s->local_abi, 1,
                          s->has_local_abi_range ? s->local_abi_range : NULL, &ra);
        long long rabi = 1;
        int has_rabi = 0;
        char rrange[UPP_ABI_RANGE_MAX];
        int has_rrange = 0;
        manifest_abi_facts(pget(b, "manifest"), &rabi, &has_rabi, rrange, sizeof rrange, &has_rrange);
        resolve_abi_range(rabi, has_rabi, has_rrange ? rrange : NULL, &rb);

        abi = ra.lo > rb.lo ? ra.lo : rb.lo;
        long long hi = ra.hi < rb.hi ? ra.hi : rb.hi;
        if (abi > hi) {
            char sa[UPP_ABI_RANGE_MAX + 16], sb[UPP_ABI_RANGE_MAX + 16];
            if (ra.has_range) snprintf(sa, sizeof sa, "%s", ra.text);
            else snprintf(sa, sizeof sa, "%lld", ra.lo);
            if (rb.has_range) snprintf(sb, sizeof sb, "%s", rb.text);
            else snprintf(sb, sizeof sb, "%lld", rb.lo);
            snprintf(msg, sizeof msg, "incompatible ABI ranges: %s vs %s", sa, sb);
            break;
        }
        ok = 1;
    } while (0);

    if (!ok) {
        s->state = UPP_STATE_INCOMPATIBLE;
        session_set_error(s, msg);
        if (err && errlen) snprintf(err, errlen, "%s", msg);
        return -1;
    }

    s->abi = abi;
    s->has_abi = 1;
    if (welcome_out) {
        char ferr[UPP_ERR_MAX];
        if (upp_frame_welcome(welcome_out, peer_role, has_peer_role, abi, ferr, sizeof ferr) != 0) {
            if (err && errlen) snprintf(err, errlen, "%s", ferr);
            return -1;
        }
    }
    return 0;
}

int upp_session_recover(UppSession *s, char *err, size_t errlen) {
    if (s->state != UPP_STATE_CRASHED && s->state != UPP_STATE_STOPPED)
        return upp_fail(err, errlen, "cannot recover from %s", upp_state_name(s->state));
    s->state = UPP_STATE_IDLE;
    s->error_set = 0;
    s->error[0] = '\0';
    s->last_heartbeat = 0;
    s->last_heartbeat_at = 0;
    return 0;
}

void upp_session_reset(UppSession *s) {
    s->state = UPP_STATE_IDLE;
    s->error_set = 0;
    s->error[0] = '\0';
    s->last_heartbeat = 0;
    s->last_heartbeat_at = 0;
}

int upp_session_is_heartbeat_stale(const UppSession *s, long long now, long long timeout_ms) {
    return s->last_heartbeat_at > 0 && now - s->last_heartbeat_at > timeout_ms;
}

int upp_session_check_heartbeat(UppSession *s, long long now, long long timeout_ms) {
    if (s->state == UPP_STATE_RUNNING && upp_session_is_heartbeat_stale(s, now, timeout_ms)) {
        s->state = UPP_STATE_CRASHED;
        session_set_error(s, "heartbeat timeout");
        return -1;
    }
    return 0;
}
