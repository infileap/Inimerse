/* crp.c - engine-side CRP wire layer: framing, capability tokens, FIND registry
 *
 * Mirrors tools/crp_reference.js and the relay subset of tools/crp_relay.js.
 * Those two files are read-only references; this module must not drift from
 * them, so every object key is written in the reference's insertion order and
 * every refusal carries the reference's exact message text.
 * tools/crp_engine_crosscheck.js enforces that by diffing transcripts, and the
 * two-process path is driven by crp_hub.c / crp_peer.c.
 *
 * The session layer underneath (src/platform/crp_session.c) is NOT
 * reimplemented here; it is called.  Where the JS relay is looser than the
 * §55.6 state machine (its sequence store accepts a backwards or gapped seq),
 * the conflict is resolved explicitly at the call site -- never by silently
 * keeping a second counter.
 */
/* clock_gettime() is POSIX; make it visible even when the build uses strict
 * -std=c11 without GNU extensions.  Must precede every system header. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "crp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crp_session.h"
#include "sha256.h"

/* --------------------------------------------------------------- utilities */

static int crp_fail(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* String.prototype.trim() emptiness (ASCII whitespace, like the UPP layer). */
static int str_is_blank(const char *s) {
    if (!s) return 1;
    for (const char *p = s; *p; p++)
        if (!ascii_space((unsigned char)*p)) return 0;
    return 1;
}

/* JS `x || fallback` truthiness. */
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

/* The JS string "" produced by `p.peer || ''` / String(p.verse). */
static const VjVal *vj_empty_string(void) {
    static const VjVal EMPTY_STR = { .type = VJ_STR, .s = (char *)"" };
    return &EMPTY_STR;
}

/* JSON.stringify(v) for one value; a NULL pointer means `undefined`, which
 * JSON.stringify cannot represent inside an object -- every call site decides
 * explicitly whether that omits the key or writes null. */
int crp_json_write(CrpBuf *out, const VjVal *v) {
    if (!v) return upp_buf_puts(out, "null");
    return upp_json_write(out, v);
}

const char *crp_js_string(const VjVal *v, char *scratch, size_t cap) {
    if (!cap) return scratch;
    scratch[0] = '\0';
    if (!v) { snprintf(scratch, cap, "undefined"); return scratch; }
    switch (v->type) {
    case VJ_NULL: snprintf(scratch, cap, "null"); break;
    case VJ_BOOL: snprintf(scratch, cap, "%s", v->b ? "true" : "false"); break;
    case VJ_INT:  snprintf(scratch, cap, "%lld", v->i); break;
    case VJ_STR:  snprintf(scratch, cap, "%s", v->s ? v->s : ""); break;
    case VJ_ARR: {
        /* String([a,b]) is Array.prototype.join(','): nested arrays flatten,
         * null and undefined become "". */
        CrpBuf joined;
        upp_buf_init(&joined);
        if (v->items && v->n) {
            for (size_t i = 0; i < v->n; i++) {
                if (i) upp_buf_putc(&joined, ',');
                const VjVal *e = v->items[i];
                if (!e || e->type == VJ_NULL) continue;
                char tmp[128];
                upp_buf_puts(&joined, crp_js_string(e, tmp, sizeof tmp));
            }
        }
        snprintf(scratch, cap, "%s", joined.data ? joined.data : "");
        upp_buf_free(&joined);
        break;
    }
    case VJ_OBJ: snprintf(scratch, cap, "[object Object]"); break;
    }
    scratch[cap - 1] = '\0';
    return scratch;
}

int crp_strict_equal(const VjVal *a, const VjVal *b) {
    if (!a || !b) return a == b; /* undefined === undefined, otherwise false */
    if (a->type != b->type) return 0;
    switch (a->type) {
    case VJ_NULL: return 1;
    case VJ_BOOL: return a->b == b->b;
    case VJ_INT:  return a->i == b->i;
    case VJ_STR:  return strcmp(a->s ? a->s : "", b->s ? b->s : "") == 0;
    default:      return 0; /* two distinct arrays/objects are never === */
    }
}

/* Deep copy of a parsed value (json_min has no constructors; the registry has
 * to outlive the request it stored). */
static VjVal *vj_clone(const VjVal *v) {
    if (!v) return NULL;
    VjVal *n = (VjVal *)calloc(1, sizeof *n);
    if (!n) return NULL;
    n->type = v->type;
    n->b = v->b;
    n->i = v->i;
    switch (v->type) {
    case VJ_NULL:
    case VJ_BOOL:
    case VJ_INT:
        break;
    case VJ_STR:
        n->s = (char *)malloc(strlen(v->s ? v->s : "") + 1);
        if (!n->s) { free(n); return NULL; }
        strcpy(n->s, v->s ? v->s : "");
        break;
    case VJ_ARR:
        n->n = v->n;
        if (n->n) {
            n->items = (VjVal **)calloc(n->n, sizeof *n->items);
            if (!n->items) { free(n); return NULL; }
            for (size_t i = 0; i < n->n; i++) {
                n->items[i] = vj_clone(v->items ? v->items[i] : NULL);
                if (v->items && v->items[i] && !n->items[i]) { vj_free(n); return NULL; }
            }
        }
        break;
    case VJ_OBJ:
        n->nkv = v->nkv;
        if (n->nkv) {
            n->keys = (char **)calloc(n->nkv, sizeof *n->keys);
            n->vals = (VjVal **)calloc(n->nkv, sizeof *n->vals);
            if (!n->keys || !n->vals) { vj_free(n); return NULL; }
            for (size_t i = 0; i < n->nkv; i++) {
                const char *k = v->keys && v->keys[i] ? v->keys[i] : "";
                n->keys[i] = (char *)malloc(strlen(k) + 1);
                if (!n->keys[i]) { vj_free(n); return NULL; }
                strcpy(n->keys[i], k);
                n->vals[i] = vj_clone(v->vals ? v->vals[i] : NULL);
                if (v->vals && v->vals[i] && !n->vals[i]) { vj_free(n); return NULL; }
            }
        }
        break;
    }
    return n;
}

/* --------------------------------------------------------------- base64url */

static const char CRP_B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64url_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

int crp_b64url_encode(const unsigned char *in, size_t n, CrpBuf *out) {
    size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8) | (unsigned)in[i + 2];
        upp_buf_putc(out, CRP_B64URL[(v >> 18) & 63]);
        upp_buf_putc(out, CRP_B64URL[(v >> 12) & 63]);
        upp_buf_putc(out, CRP_B64URL[(v >> 6) & 63]);
        upp_buf_putc(out, CRP_B64URL[v & 63]);
    }
    if (n - i == 1) {
        unsigned v = (unsigned)in[i] << 16;
        upp_buf_putc(out, CRP_B64URL[(v >> 18) & 63]);
        upp_buf_putc(out, CRP_B64URL[(v >> 12) & 63]);
    } else if (n - i == 2) {
        unsigned v = ((unsigned)in[i] << 16) | ((unsigned)in[i + 1] << 8);
        upp_buf_putc(out, CRP_B64URL[(v >> 18) & 63]);
        upp_buf_putc(out, CRP_B64URL[(v >> 12) & 63]);
        upp_buf_putc(out, CRP_B64URL[(v >> 6) & 63]);
    }
    return out->oom ? -1 : 0;
}

int crp_b64url_decode(const char *in, size_t n, unsigned char **out, size_t *outlen) {
    if (out) *out = NULL;
    if (outlen) *outlen = 0;
    if (!in || n == 0) return 0;
    /* Stricter than Node's lenient base64 decoder: a length of 4k+1 and any
     * byte outside the alphabet are refused.  Canonical tokens are unaffected. */
    if (n % 4 == 1) return -1;
    unsigned char *buf = (unsigned char *)malloc((n / 4) * 3 + 3);
    if (!buf) return -1;
    size_t len = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int d = b64url_value((unsigned char)in[i]);
        if (d < 0) { free(buf); return -1; }
        acc = (acc << 6) | (unsigned)d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[len++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    /* Unused trailing bits must be zero, as in a canonical base64url encoder. */
    if (bits > 0 && (acc & ((1u << bits) - 1u)) != 0) { free(buf); return -1; }
    /* The caller may treat the decoded bytes as a C string (token bodies are
     * JSON text); the allocation always has room for the terminator. */
    buf[len] = '\0';
    if (out) *out = buf;
    else free(buf);
    if (outlen) *outlen = len;
    return 0;
}

/* ------------------------------------------------------------- HMAC-SHA256 */

void crp_hmac_sha256(const void *key, size_t keylen, const void *msg, size_t msglen,
                     unsigned char out[32]) {
    const unsigned char *k = (const unsigned char *)(key ? key : "");
    unsigned char block[64];
    unsigned char inner[32];
    unsigned char pad[64];
    Sha256Ctx ctx;

    if (keylen > sizeof block) {
        sha256_digest(k, keylen, block);
        keylen = 32;
    } else {
        memcpy(block, k, keylen);
    }
    memset(block + keylen, 0, sizeof block - keylen);

    for (size_t i = 0; i < sizeof pad; i++) pad[i] = (unsigned char)(block[i] ^ 0x36);
    sha256_init(&ctx);
    sha256_update(&ctx, pad, sizeof pad);
    if (msglen) sha256_update(&ctx, (const unsigned char *)(msg ? msg : ""), msglen);
    sha256_final(&ctx, inner);

    for (size_t i = 0; i < sizeof pad; i++) pad[i] = (unsigned char)(block[i] ^ 0x5c);
    sha256_init(&ctx);
    sha256_update(&ctx, pad, sizeof pad);
    sha256_update(&ctx, inner, sizeof inner);
    sha256_final(&ctx, out);
}

/* ------------------------------------------------------- capability tokens */

/* `JSON.stringify({verse, peer, capabilities, exp})` with the reference's key
 * order; `undefined` members are omitted, exactly like JSON.stringify. */
static int token_body_build(const VjVal *verse, const VjVal *peer,
                            const VjVal *caps, const char *const *cap_strings,
                            size_t ncap_strings, long long exp_ms, CrpBuf *out) {
    upp_buf_putc(out, '{');
    int first = 1;
    if (verse) {
        upp_buf_puts(out, "\"verse\":");
        crp_json_write(out, verse);
        first = 0;
    }
    if (peer) {
        if (!first) upp_buf_putc(out, ',');
        upp_buf_puts(out, "\"peer\":");
        crp_json_write(out, peer);
        first = 0;
    }
    if (!first) upp_buf_putc(out, ',');
    upp_buf_puts(out, "\"capabilities\":");
    if (caps) {
        crp_json_write(out, caps);
    } else {
        upp_buf_putc(out, '[');
        for (size_t i = 0; i < ncap_strings; i++) {
            if (i) upp_buf_putc(out, ',');
            upp_json_write_string(out, cap_strings[i] ? cap_strings[i] : "");
        }
        upp_buf_putc(out, ']');
    }
    upp_buf_puts(out, ",\"exp\":");
    upp_buf_put_ll(out, exp_ms);
    upp_buf_putc(out, '}');
    return out->oom ? -1 : 0;
}

static int token_make_impl(const char *secret, const VjVal *verse, const VjVal *peer,
                           const VjVal *caps, const char *const *cap_strings,
                           size_t ncap_strings, long long exp_ms,
                           CrpBuf *out, char *err, size_t errlen) {
    if (!secret) return crp_fail(err, errlen, "secret is required");
    CrpBuf body;
    upp_buf_init(&body);
    if (token_body_build(verse, peer, caps, cap_strings, ncap_strings, exp_ms, &body) != 0) {
        upp_buf_free(&body);
        return crp_fail(err, errlen, "out of memory");
    }
    CrpBuf b64body;
    upp_buf_init(&b64body);
    crp_b64url_encode((const unsigned char *)body.data, body.len, &b64body);
    unsigned char mac[32];
    crp_hmac_sha256(secret, strlen(secret),
                    b64body.data ? b64body.data : "", b64body.len, mac);
    CrpBuf b64sig;
    upp_buf_init(&b64sig);
    crp_b64url_encode(mac, sizeof mac, &b64sig);
    upp_buf_putn(out, b64body.data ? b64body.data : "", b64body.len);
    upp_buf_putc(out, '.');
    upp_buf_putn(out, b64sig.data ? b64sig.data : "", b64sig.len);
    int rc = out->oom ? -1 : 0;
    upp_buf_free(&body);
    upp_buf_free(&b64body);
    upp_buf_free(&b64sig);
    if (rc != 0) return crp_fail(err, errlen, "out of memory");
    return 0;
}

int crp_token_make(const char *secret, const VjVal *verse, const VjVal *peer,
                   const VjVal *caps, long long exp_ms,
                   CrpBuf *out, char *err, size_t errlen) {
    static const char *DEFAULT_CAPS[] = { "signal" };
    if (caps) return token_make_impl(secret, verse, peer, caps, NULL, 0, exp_ms, out, err, errlen);
    return token_make_impl(secret, verse, peer, NULL, DEFAULT_CAPS, 1, exp_ms, out, err, errlen);
}

int crp_token_make_str(const char *secret, const char *verse, const char *peer,
                       const char *const *capabilities, size_t ncapabilities,
                       long long exp_ms, CrpBuf *out, char *err, size_t errlen) {
    static const char *DEFAULT_CAPS[] = { "signal" };
    VjVal v, p;
    if (!capabilities) { /* mirror makeToken's `capabilities = ['signal']` default */
        capabilities = DEFAULT_CAPS;
        ncapabilities = 1;
    }
    memset(&v, 0, sizeof v);
    memset(&p, 0, sizeof p);
    v.type = VJ_STR;
    v.s = (char *)(verse ? verse : "");
    p.type = VJ_STR;
    p.s = (char *)(peer ? peer : "");
    return token_make_impl(secret, &v, &p, NULL, capabilities, ncapabilities, exp_ms,
                           out, err, errlen);
}

/* JS `p.exp > now`: numeric comparison with ToNumber coercion. */
static int token_exp_gt(const VjVal *exp, long long now) {
    if (!exp) return 0;
    switch (exp->type) {
    case VJ_INT:  return exp->i > now;
    case VJ_STR: {
        const char *s = exp->s ? exp->s : "";
        char *end = NULL;
        double d = strtod(s, &end);
        if (end == s) return 0; /* NaN comparison is false */
        return d > (double)now;
    }
    case VJ_BOOL: return (exp->b ? 1.0 : 0.0) > (double)now;
    case VJ_NULL: return 0 > now;
    default:      return 0; /* array/object ToPrimitive is not modelled */
    }
}

/* Array.prototype.includes for the capability list (strict equality). */
static int caps_include(const VjVal *caps, const char *capability) {
    if (!caps || caps->type != VJ_ARR || !capability) return 0;
    for (size_t i = 0; i < caps->n; i++) {
        const VjVal *e = caps->items ? caps->items[i] : NULL;
        if (e && e->type == VJ_STR && strcmp(e->s ? e->s : "", capability) == 0) return 1;
    }
    return 0;
}

int crp_token_check(const char *secret, const char *token,
                    const VjVal *verse, const VjVal *peer,
                    const char *capability, long long now_ms) {
    if (!secret || !token) return 0;
    /* `const [body, sig] = String(token).split('.')` -- the first two segments. */
    const char *dot = strchr(token, '.');
    if (!dot) return 0;
    size_t bodylen = (size_t)(dot - token);
    const char *sig = dot + 1;
    if (bodylen == 0 || !*sig) return 0;

    unsigned char mac[32];
    crp_hmac_sha256(secret, strlen(secret), token, bodylen, mac);
    CrpBuf expected;
    upp_buf_init(&expected);
    crp_b64url_encode(mac, sizeof mac, &expected);
    size_t siglen = strlen(sig);
    /* timingSafeEqual throws on a length mismatch, which the reference catches. */
    int sig_ok = (siglen == expected.len) &&
                 memcmp(sig, expected.data ? expected.data : "", siglen) == 0;
    upp_buf_free(&expected);
    if (!sig_ok) return 0;

    unsigned char *raw = NULL;
    size_t rawlen = 0;
    if (crp_b64url_decode(token, bodylen, &raw, &rawlen) != 0) return 0;
    char parse_err[128];
    VjVal *p = vj_parse((const char *)(raw ? (const char *)raw : ""), parse_err, sizeof parse_err);
    free(raw);
    if (!p) return 0;
    int ok = 0;
    if (p->type == VJ_OBJ) {
        const VjVal *bv = vj_get(p, "verse");
        const VjVal *bp = vj_get(p, "peer");
        const VjVal *be = vj_get(p, "exp");
        const VjVal *bc = vj_get(p, "capabilities");
        ok = crp_strict_equal(bv, verse) && crp_strict_equal(bp, peer) &&
             token_exp_gt(be, now_ms) && caps_include(bc, capability);
    }
    vj_free(p);
    return ok;
}

int crp_token_check_str(const char *secret, const char *token,
                        const char *verse, const char *peer,
                        const char *capability, long long now_ms) {
    VjVal v, p;
    memset(&v, 0, sizeof v);
    memset(&p, 0, sizeof p);
    v.type = VJ_STR;
    v.s = (char *)(verse ? verse : "");
    p.type = VJ_STR;
    p.s = (char *)(peer ? peer : "");
    return crp_token_check(secret, token, &v, &p, capability, now_ms);
}

VjVal *crp_token_body(const char *token, char *err, size_t errlen) {
    if (!token) { crp_fail(err, errlen, "token is required"); return NULL; }
    const char *dot = strchr(token, '.');
    if (!dot) { crp_fail(err, errlen, "token has no signature"); return NULL; }
    size_t bodylen = (size_t)(dot - token);
    unsigned char *raw = NULL;
    size_t rawlen = 0;
    if (crp_b64url_decode(token, bodylen, &raw, &rawlen) != 0) {
        crp_fail(err, errlen, "token body is not base64url");
        return NULL;
    }
    VjVal *p = vj_parse((const char *)(raw ? (const char *)raw : ""), err, errlen);
    free(raw);
    return p;
}

/* ----------------------------------------------------------------- framing */

/* The reference reads Date.now() while building a SIGNAL frame.  This is that
 * one reading, so a test can freeze it; <= 0 means the wall clock. */
static long long g_crp_frozen_now = 0;

void crp_set_now(long long now_ms) { g_crp_frozen_now = now_ms > 0 ? now_ms : 0; }

long long crp_now(void) {
    return g_crp_frozen_now > 0 ? g_crp_frozen_now : upp_now_ms();
}

int crp_type_is_valid(const char *type) {
    return type && (strcmp(type, "FIND") == 0 || strcmp(type, "PORTAL") == 0 ||
                    strcmp(type, "SIGNAL") == 0);
}

static int frame_head(CrpBuf *out, const char *type) {
    upp_buf_puts(out, "{\"crp\":");
    upp_buf_put_ll(out, CRP_VERSION);
    upp_buf_puts(out, ",\"type\":");
    upp_json_write_string(out, type);
    upp_buf_puts(out, ",\"payload\":");
    return out->oom ? -1 : 0;
}

/* Closes the payload object, then appends `id` only when it is truthy (the
 * reference appends it after the payload, so the key order is crp/type/payload/id). */
static int frame_tail(CrpBuf *out, const char *id, char *err, size_t errlen) {
    /* The id is appended AFTER the payload and BEFORE the frame closes, exactly
     * as the reference does: {"crp":1,"type":T,"payload":{..},"id":"x"} */
    if (id && *id) {
        upp_buf_puts(out, ",\"id\":");
        upp_json_write_string(out, id);
    }
    upp_buf_putc(out, '}');
    if (out->oom) return crp_fail(err, errlen, "out of memory");
    return 0;
}

int crp_frame(CrpBuf *out, const VjVal *type, const VjVal *payload,
              const char *id, char *err, size_t errlen) {
    static const VjVal EMPTY_OBJ = { .type = VJ_OBJ };
    const char *tname = (type && type->type == VJ_STR && crp_type_is_valid(type->s))
                            ? type->s : NULL;
    if (!tname) {
        char scratch[128];
        return crp_fail(err, errlen, "unsupported CRP type: %s",
                        crp_js_string(type, scratch, sizeof scratch));
    }
    const VjVal *pl = payload ? payload : &EMPTY_OBJ;
    if (pl->type != VJ_OBJ) return crp_fail(err, errlen, "payload must be an object");
    if (frame_head(out, tname) != 0) return crp_fail(err, errlen, "out of memory");
    crp_json_write(out, pl);
    return frame_tail(out, id, err, errlen);
}

int crp_frame_find(CrpBuf *out, const VjVal *query, const VjVal *limit,
                   const VjVal *cursor, const char *id, char *err, size_t errlen) {
    if (query && query->type != VJ_STR)
        return crp_fail(err, errlen, "FIND query must be a string");
    long long lim = 50;
    if (limit && limit->type != VJ_NULL) {
        if (limit->type != VJ_INT) return crp_fail(err, errlen, "FIND limit must be 1..1000");
        lim = limit->i;
    }
    if (lim < 1 || lim > 1000) return crp_fail(err, errlen, "FIND limit must be 1..1000");
    if (cursor && cursor->type != VJ_NULL && cursor->type != VJ_STR)
        return crp_fail(err, errlen, "FIND cursor must be a string or null");

    CrpBuf payload;
    upp_buf_init(&payload);
    upp_buf_putc(&payload, '{');
    upp_buf_puts(&payload, "\"query\":");
    upp_json_write_string(&payload, (query && query->s) ? query->s : "");
    upp_buf_puts(&payload, ",\"limit\":");
    upp_buf_put_ll(&payload, lim);
    upp_buf_puts(&payload, ",\"cursor\":");
    if (cursor && cursor->type == VJ_STR) upp_json_write_string(&payload, cursor->s ? cursor->s : "");
    else upp_buf_puts(&payload, "null");
    upp_buf_putc(&payload, '}');

    int rc = 0;
    if (frame_head(out, "FIND") != 0) rc = -1;
    else if (payload.data && upp_buf_putn(out, payload.data, payload.len) != 0) rc = -1;
    upp_buf_free(&payload);
    if (rc != 0) return crp_fail(err, errlen, "out of memory");
    return frame_tail(out, id, err, errlen);
}

int crp_frame_portal(CrpBuf *out, const VjVal *verse, const VjVal *peer,
                     const VjVal *token, const VjVal *expires, const char *id,
                     char *err, size_t errlen) {
    /* Object.entries({ verse, peer }) order: verse first, then peer. */
    const VjVal *vals[2] = { verse, peer };
    const char *names[2] = { "verse", "peer" };
    for (int i = 0; i < 2; i++) {
        const VjVal *v = vals[i];
        if (!v || v->type != VJ_STR || str_is_blank(v->s))
            return crp_fail(err, errlen, "PORTAL %s is required", names[i]);
    }
    CrpBuf payload;
    upp_buf_init(&payload);
    upp_buf_putc(&payload, '{');
    upp_buf_puts(&payload, "\"verse\":");
    upp_json_write_string(&payload, verse->s ? verse->s : "");
    upp_buf_puts(&payload, ",\"peer\":");
    upp_json_write_string(&payload, peer->s ? peer->s : "");
    upp_buf_puts(&payload, ",\"token\":");
    if (vj_truthy(token)) crp_json_write(&payload, token);
    else upp_buf_puts(&payload, "null");
    upp_buf_puts(&payload, ",\"expires\":");
    if (!expires || expires->type == VJ_NULL) upp_buf_puts(&payload, "null");
    else crp_json_write(&payload, expires);
    upp_buf_putc(&payload, '}');

    int rc = 0;
    if (frame_head(out, "PORTAL") != 0) rc = -1;
    else if (payload.data && upp_buf_putn(out, payload.data, payload.len) != 0) rc = -1;
    upp_buf_free(&payload);
    if (rc != 0) return crp_fail(err, errlen, "out of memory");
    return frame_tail(out, id, err, errlen);
}

int crp_frame_signal(CrpBuf *out, const VjVal *verse, const VjVal *event,
                     const VjVal *data, const VjVal *timestamp, const char *id,
                     char *err, size_t errlen) {
    if (!verse || verse->type != VJ_STR || str_is_blank(verse->s))
        return crp_fail(err, errlen, "SIGNAL verse is required");
    if (!event || event->type != VJ_STR || str_is_blank(event->s))
        return crp_fail(err, errlen, "SIGNAL event is required");
    static const VjVal EMPTY_OBJ = { .type = VJ_OBJ };
    const VjVal *d = data ? data : &EMPTY_OBJ;
    if (d->type != VJ_OBJ) return crp_fail(err, errlen, "SIGNAL data must be an object");

    CrpBuf payload;
    upp_buf_init(&payload);
    upp_buf_putc(&payload, '{');
    upp_buf_puts(&payload, "\"verse\":");
    upp_json_write_string(&payload, verse->s ? verse->s : "");
    upp_buf_puts(&payload, ",\"event\":");
    upp_json_write_string(&payload, event->s ? event->s : "");
    upp_buf_puts(&payload, ",\"data\":");
    crp_json_write(&payload, d);
    upp_buf_puts(&payload, ",\"timestamp\":");
    if (!timestamp || timestamp->type == VJ_NULL) upp_buf_put_ll(&payload, crp_now());
    else crp_json_write(&payload, timestamp);
    upp_buf_putc(&payload, '}');

    int rc = 0;
    if (frame_head(out, "SIGNAL") != 0) rc = -1;
    else if (payload.data && upp_buf_putn(out, payload.data, payload.len) != 0) rc = -1;
    upp_buf_free(&payload);
    if (rc != 0) return crp_fail(err, errlen, "out of memory");
    return frame_tail(out, id, err, errlen);
}

int crp_encode(CrpBuf *out, const VjVal *message, char *err, size_t errlen) {
    if (!message || message->type != VJ_OBJ)
        return crp_fail(err, errlen, "message must be an object");
    const VjVal *crp = vj_get(message, "crp");
    const VjVal *type = vj_get(message, "type");
    if (!crp || crp->type != VJ_INT || crp->i != CRP_VERSION ||
        !type || type->type != VJ_STR || !crp_type_is_valid(type->s))
        return crp_fail(err, errlen, "invalid CRP frame");
    CrpBuf line;
    upp_buf_init(&line);
    upp_json_write(&line, message);
    if (line.oom) {
        upp_buf_free(&line);
        return crp_fail(err, errlen, "out of memory");
    }
    if (line.len > CRP_MAX_FRAME_BYTES) {
        upp_buf_free(&line);
        return crp_fail(err, errlen, "CRP frame exceeds 1 MiB");
    }
    int rc = upp_buf_putn(out, line.data ? line.data : "", line.len);
    upp_buf_free(&line);
    if (rc != 0) return crp_fail(err, errlen, "out of memory");
    if (upp_buf_putc(out, '\n') != 0) return crp_fail(err, errlen, "out of memory");
    return 0;
}

VjVal *crp_decode_line(const char *line, size_t len, char *err, size_t errlen) {
    if (len == (size_t)-1) len = line ? strlen(line) : 0;
    if (len > CRP_MAX_FRAME_BYTES) {
        crp_fail(err, errlen, "CRP frame exceeds 1 MiB");
        return NULL;
    }
    char *copy = (char *)malloc(len + 1);
    if (!copy) { crp_fail(err, errlen, "out of memory"); return NULL; }
    if (len) memcpy(copy, line, len);
    copy[len] = '\0';
    VjVal *v = vj_parse(copy, err, errlen);
    free(copy);
    if (!v) return NULL;
    if (v->type != VJ_OBJ) {
        vj_free(v);
        crp_fail(err, errlen, "message must be an object");
        return NULL;
    }
    const VjVal *crp = vj_get(v, "crp");
    const VjVal *type = vj_get(v, "type");
    if (!crp || crp->type != VJ_INT || crp->i != CRP_VERSION ||
        !type || type->type != VJ_STR || !crp_type_is_valid(type->s)) {
        vj_free(v);
        crp_fail(err, errlen, "invalid CRP frame");
        return NULL;
    }
    return v;
}

/* ---------------------------------------------------------------- registry */

typedef struct {
    long long seq;
    VjVal    *event;
    VjVal    *data;
} CrpEvent;

typedef struct {
    char       *verse;  /* owned */
    char       *peer;   /* owned; "" mirrors the reference's `p.peer || ''` */
    ImCrpSession sess;  /* the existing §55.6 platform session layer */
    CrpEvent    events[CRP_EVENT_WINDOW];
    int         nevents;
    int         last_verdict;   /* im_crp_session_accept() result of the last op */
    int         replay_from;    /* im_crp_session_resume_plan() */
    int         needs_snapshot;
    int         started;        /* im_crp_session_apply("start") was applied */
} CrpSession;

typedef struct {
    VjVal    *obj;      /* owned clone of the registration body */
    long long updated;
} CrpVerse;

struct CrpRegistry {
    char       *secret;
    long long   token_ttl_ms;
    long long   registry_ttl_ms;
    long long   max_revoked;
    long long   frozen_now;     /* <= 0: wall clock */
    CrpVerse   *verses;
    size_t      nverses, cap_verses;
    CrpSession *sessions;
    size_t      nsessions, cap_sessions;
    char      **revoked;        /* insertion order, like a JS Set */
    size_t      nrevoked, cap_revoked;
};

void crp_result_free(CrpResult *r) {
    if (!r) return;
    upp_buf_free(&r->body);
    r->status = 0;
}

static CrpResult res_text(int status, const char *text) {
    CrpResult r;
    r.status = status;
    upp_buf_init(&r.body);
    upp_buf_puts(&r.body, text ? text : "");
    return r;
}

static CrpResult res_error(int status, const char *msg) {
    CrpResult r;
    r.status = status;
    upp_buf_init(&r.body);
    upp_buf_puts(&r.body, "{\"error\":");
    upp_json_write_string(&r.body, msg ? msg : "");
    upp_buf_puts(&r.body, "}");
    return r;
}

static const char *crp_state_name(ImCrpState state) {
    switch (state) {
    case IM_CRP_IDLE: return "idle";
    case IM_CRP_RUNNING: return "running";
    case IM_CRP_STOPPED: return "stopped";
    case IM_CRP_CRASHED: return "crashed";
    case IM_CRP_INCOMPATIBLE: return "incompatible";
    case IM_CRP_DEGRADED: return "degraded";
    case IM_CRP_DISCONNECTED_GRACE: return "disconnected_grace";
    case IM_CRP_REATTACHING: return "reattaching";
    case IM_CRP_RESUMED: return "resumed";
    case IM_CRP_READ_ONLY: return "read_only";
    case IM_CRP_EXPIRED: return "expired";
    }
    return "unknown";
}

CrpRegistry *crp_registry_new(const CrpRegistryConfig *cfg) {
    if (!cfg || !cfg->secret) return NULL;
    CrpRegistry *r = (CrpRegistry *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->secret = (char *)malloc(strlen(cfg->secret) + 1);
    if (!r->secret) { free(r); return NULL; }
    strcpy(r->secret, cfg->secret);
    r->token_ttl_ms = cfg->token_ttl_ms > 0 ? cfg->token_ttl_ms : CRP_DEFAULT_TOKEN_TTL_MS;
    r->registry_ttl_ms = cfg->registry_ttl_ms > 0 ? cfg->registry_ttl_ms : CRP_DEFAULT_REGISTRY_TTL_MS;
    r->max_revoked = cfg->max_revoked > 0 ? cfg->max_revoked : CRP_DEFAULT_MAX_REVOKED;
    r->frozen_now = cfg->now_ms > 0 ? cfg->now_ms : 0;
    return r;
}

void crp_registry_free(CrpRegistry *r) {
    if (!r) return;
    free(r->secret);
    for (size_t i = 0; i < r->nverses; i++) vj_free(r->verses[i].obj);
    free(r->verses);
    for (size_t i = 0; i < r->nsessions; i++) {
        CrpSession *s = &r->sessions[i];
        free(s->verse);
        free(s->peer);
        for (int e = 0; e < s->nevents; e++) {
            vj_free(s->events[e].event);
            vj_free(s->events[e].data);
        }
    }
    free(r->sessions);
    for (size_t i = 0; i < r->nrevoked; i++) free(r->revoked[i]);
    free(r->revoked);
    free(r);
}

void crp_registry_set_now(CrpRegistry *r, long long now_ms) {
    if (r) r->frozen_now = now_ms > 0 ? now_ms : 0;
}

long long crp_registry_now(const CrpRegistry *r) {
    if (r && r->frozen_now > 0) return r->frozen_now;
    return upp_now_ms();
}

const char *crp_registry_secret(const CrpRegistry *r) { return r ? r->secret : NULL; }

int crp_registry_verse_count(const CrpRegistry *r) { return r ? (int)r->nverses : 0; }
int crp_registry_session_count(const CrpRegistry *r) { return r ? (int)r->nsessions : 0; }
long long crp_registry_revoked_count(const CrpRegistry *r) {
    return r ? (long long)r->nrevoked : 0;
}

/* pruneRegistry(): `(v.updated || 0) < Date.now() - registryTtlMs`. */
static void registry_prune(CrpRegistry *r, long long now) {
    long long cutoff = now - r->registry_ttl_ms;
    size_t out = 0;
    for (size_t i = 0; i < r->nverses; i++) {
        if (r->verses[i].updated < cutoff) {
            vj_free(r->verses[i].obj);
            continue;
        }
        if (out != i) r->verses[out] = r->verses[i];
        out++;
    }
    r->nverses = out;
}

/* Map.set(String(id), item): an existing key keeps its insertion position. */
static void registry_put_verse(CrpRegistry *r, const char *id, VjVal *obj, long long now) {
    for (size_t i = 0; i < r->nverses; i++) {
        const VjVal *stored = vj_get(r->verses[i].obj, "id");
        char scratch[256];
        if (stored && strcmp(crp_js_string(stored, scratch, sizeof scratch), id) == 0) {
            vj_free(r->verses[i].obj);
            r->verses[i].obj = obj;
            r->verses[i].updated = now;
            return;
        }
    }
    if (r->nverses == r->cap_verses) {
        size_t cap = r->cap_verses ? r->cap_verses * 2 : 8;
        CrpVerse *grown = (CrpVerse *)realloc(r->verses, cap * sizeof *grown);
        if (!grown) { vj_free(obj); return; }
        r->verses = grown;
        r->cap_verses = cap;
    }
    r->verses[r->nverses].obj = obj;
    r->verses[r->nverses].updated = now;
    r->nverses++;
}

static int registry_has_verse(const CrpRegistry *r, const char *id) {
    for (size_t i = 0; i < r->nverses; i++) {
        const VjVal *stored = vj_get(r->verses[i].obj, "id");
        char scratch[256];
        if (stored && strcmp(crp_js_string(stored, scratch, sizeof scratch), id) == 0) return 1;
    }
    return 0;
}

static CrpSession *registry_find_session(const CrpRegistry *r, const char *verse,
                                         const char *peer) {
    for (size_t i = 0; i < r->nsessions; i++) {
        if (strcmp(r->sessions[i].verse, verse) == 0 && strcmp(r->sessions[i].peer, peer) == 0)
            return &r->sessions[i];
    }
    return NULL;
}

static CrpSession *registry_get_session(CrpRegistry *r, const char *verse, const char *peer) {
    CrpSession *s = registry_find_session(r, verse, peer);
    if (s) return s;
    if (r->nsessions == r->cap_sessions) {
        size_t cap = r->cap_sessions ? r->cap_sessions * 2 : 8;
        CrpSession *grown = (CrpSession *)realloc(r->sessions, cap * sizeof *grown);
        if (!grown) return NULL;
        r->sessions = grown;
        r->cap_sessions = cap;
    }
    CrpSession *n = &r->sessions[r->nsessions++];
    memset(n, 0, sizeof *n);
    n->verse = (char *)malloc(strlen(verse) + 1);
    n->peer = (char *)malloc(strlen(peer) + 1);
    if (!n->verse || !n->peer) {
        free(n->verse);
        free(n->peer);
        r->nsessions--;
        return NULL;
    }
    strcpy(n->verse, verse);
    strcpy(n->peer, peer);
    im_crp_session_init(&n->sess, CRP_VERSION);
    return n;
}

/* Store an application-level sequence, as `sessions.set(key, seq)` does.
 *
 * The §55.6 platform verdict is always asked for first (and recorded); the
 * reference relay's store is LOOSER than im_crp_session_accept() -- an explicit
 * seq may move the counter backwards and a gap is stored as-is -- so the wire
 * contract is reproduced by applying the reference's rule to the platform
 * session's own `last_applied` field, not by keeping a parallel counter. */
static long long session_store_seq(CrpSession *s, long long seq) {
    s->last_verdict = im_crp_session_accept(&s->sess, (uint64_t)seq);
    s->sess.last_applied = (uint64_t)seq;
    return (long long)s->sess.last_applied;
}

static int revoked_has(const CrpRegistry *r, const char *token) {
    for (size_t i = 0; i < r->nrevoked; i++)
        if (strcmp(r->revoked[i], token) == 0) return 1;
    return 0;
}

static void revoked_add(CrpRegistry *r, const char *token) {
    if (revoked_has(r, token)) return; /* Set.add on an existing member */
    if (r->nrevoked == r->cap_revoked) {
        size_t cap = r->cap_revoked ? r->cap_revoked * 2 : 16;
        char **grown = (char **)realloc(r->revoked, cap * sizeof *grown);
        if (!grown) return;
        r->revoked = grown;
        r->cap_revoked = cap;
    }
    char *copy = (char *)malloc(strlen(token) + 1);
    if (!copy) return;
    strcpy(copy, token);
    r->revoked[r->nrevoked++] = copy;
    /* while (revoked.size > maxRevoked) revoked.delete(oldest) */
    while (r->nrevoked > (size_t)r->max_revoked) {
        free(r->revoked[0]);
        memmove(r->revoked, r->revoked + 1, (r->nrevoked - 1) * sizeof *r->revoked);
        r->nrevoked--;
    }
}

CrpResult crp_registry_register(CrpRegistry *r, const VjVal *p) {
    const VjVal *id = pget(p, "id");
    const VjVal *endpoint = pget(p, "endpoint");
    if (!vj_truthy(id) || !vj_truthy(endpoint))
        return res_error(400, "id and endpoint are required");
    char scratch[256];
    const char *key = crp_js_string(id, scratch, sizeof scratch);
    VjVal *obj = vj_clone(p);
    if (!obj) return res_error(500, "out of memory");
    registry_put_verse(r, key, obj, crp_registry_now(r));
    return res_text(200, "{\"ok\":true}");
}

/* String.prototype.includes (byte substring search). */
static int str_includes(const char *hay, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!hay) return 0;
    return strstr(hay, needle) != NULL;
}

static void ascii_lower_into(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (cap) {
        for (; src && src[i] && i + 1 < cap; i++) {
            unsigned char c = (unsigned char)src[i];
            dst[i] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
        }
        dst[i] = '\0';
    }
}

CrpResult crp_registry_find(CrpRegistry *r, const char *q) {
    const char *query = q ? q : "";
    registry_prune(r, crp_registry_now(r));
    CrpResult res;
    res.status = 200;
    upp_buf_init(&res.body);
    upp_buf_puts(&res.body, "{\"items\":[");
    int first = 1;
    for (size_t i = 0; i < r->nverses; i++) {
        const VjVal *v = r->verses[i].obj;
        char idbuf[256];
        const char *id = crp_js_string(vj_get(v, "id"), idbuf, sizeof idbuf);
        const VjVal *namev = vj_get(v, "name");
        char namebuf[512];
        const char *name = (namev && vj_truthy(namev))
                               ? crp_js_string(namev, namebuf, sizeof namebuf) : "";
        char lower_query[512], lower_name[512];
        ascii_lower_into(lower_query, sizeof lower_query, query);
        ascii_lower_into(lower_name, sizeof lower_name, name);
        int match = !query[0] || str_includes(id, query) || str_includes(lower_name, lower_query);
        if (!match) continue;
        if (!first) upp_buf_putc(&res.body, ',');
        first = 0;
        /* `({ updated, ...v }) => v`: every key except `updated`, in order. */
        upp_buf_putc(&res.body, '{');
        int first_key = 1;
        for (size_t k = 0; v && v->type == VJ_OBJ && k < v->nkv; k++) {
            const char *key = v->keys && v->keys[k] ? v->keys[k] : "";
            if (strcmp(key, "updated") == 0) continue;
            if (!first_key) upp_buf_putc(&res.body, ',');
            first_key = 0;
            upp_json_write_string(&res.body, key);
            upp_buf_putc(&res.body, ':');
            crp_json_write(&res.body, v->vals ? v->vals[k] : NULL);
        }
        upp_buf_putc(&res.body, '}');
    }
    upp_buf_puts(&res.body, "]}");
    return res;
}

CrpResult crp_registry_portal(CrpRegistry *r, const VjVal *verse, const VjVal *peer) {
    const char *vstr = (verse && verse->type == VJ_STR) ? verse->s : NULL;
    if (!vstr || !registry_has_verse(r, vstr) || !vj_truthy(peer))
        return res_error(404, "verse not found");
    long long now = crp_registry_now(r);
    long long expires = now + r->token_ttl_ms;

    CrpBuf token;
    upp_buf_init(&token);
    char err[CRP_ERR_MAX];
    if (crp_token_make(r->secret, verse, peer, NULL, expires, &token, err, sizeof err) != 0) {
        upp_buf_free(&token);
        return res_error(500, "token refused");
    }

    /* The portal opened a session for this peer: bind it onto the platform
     * session layer so the lease in crp_session.c is the one that moves. */
    char peerbuf[512];
    const char *pstr = crp_js_string(peer, peerbuf, sizeof peerbuf);
    CrpSession *s = registry_get_session(r, vstr, pstr);
    if (s) {
        im_crp_session_apply(&s->sess, "start", 0, 0, NULL);
        s->started = 1;
        im_crp_session_lease_begin(&s->sess, (uint64_t)now, (uint64_t)r->token_ttl_ms);
    }

    CrpResult res;
    res.status = 200;
    upp_buf_init(&res.body);
    upp_buf_puts(&res.body, "{\"token\":");
    upp_json_write_string(&res.body, token.data ? token.data : "");
    upp_buf_puts(&res.body, ",\"verse\":");
    crp_json_write(&res.body, verse);
    upp_buf_puts(&res.body, ",\"peer\":");
    crp_json_write(&res.body, peer);
    upp_buf_puts(&res.body, ",\"expires\":");
    upp_buf_put_ll(&res.body, expires);
    upp_buf_putc(&res.body, '}');
    upp_buf_free(&token);
    return res;
}

/* The key the reference builds as `verse + "\0" + (peer || "")`, i.e. a
 * NUL-separated pair; (verse, peer) is compared instead -- equivalent in C. */
static void session_key_of(const VjVal *peer, char *out, size_t cap) {
    if (!vj_truthy(peer)) { if (cap) out[0] = '\0'; return; }
    char scratch[512];
    snprintf(out, cap, "%s", crp_js_string(peer, scratch, sizeof scratch));
}

CrpResult crp_registry_signal(CrpRegistry *r, const VjVal *verse, const VjVal *event,
                              const VjVal *data, const VjVal *token,
                              const VjVal *peer, const VjVal *seqv) {
    if (!vj_truthy(verse) || !vj_truthy(event))
        return res_error(400, "verse and event are required");
    /* Map.has(p.verse): a non-string never matches a string key. */
    const char *vstr = (verse->type == VJ_STR) ? verse->s : NULL;
    if (!vstr || !registry_has_verse(r, vstr)) return res_error(404, "verse not found");

    long long now = crp_registry_now(r);
    if (vj_truthy(token)) {
        char tokbuf[CRP_TOKEN_MAX];
        const char *tok = crp_js_string(token, tokbuf, sizeof tokbuf);
        if (revoked_has(r, tok) ||
            !crp_token_check(r->secret, tok, verse, vj_truthy(peer) ? peer : vj_empty_string(),
                             "signal", now))
            return res_error(403, "invalid capability token");
    }

    char peerbuf[512];
    session_key_of(peer, peerbuf, sizeof peerbuf);
    CrpSession *s = registry_get_session(r, vstr, peerbuf);
    if (!s) return res_error(500, "out of memory");

    long long seq;
    if (seqv && seqv->type == VJ_INT && upp_is_safe_int(seqv->i) && seqv->i >= 0)
        seq = seqv->i;
    else
        seq = (long long)s->sess.last_applied + 1;
    session_store_seq(s, seq);
    if (s->sess.lease_id[0]) im_crp_session_lease_touch(&s->sess, (uint64_t)now, 0);

    /* events.push({seq, event, data: data || {}}); while (length > 64) shift() */
    static const VjVal EMPTY_OBJ = { .type = VJ_OBJ };
    const VjVal *d = vj_truthy(data) ? data : &EMPTY_OBJ;
    if (s->nevents == CRP_EVENT_WINDOW) {
        vj_free(s->events[0].event);
        vj_free(s->events[0].data);
        memmove(&s->events[0], &s->events[1], (CRP_EVENT_WINDOW - 1) * sizeof s->events[0]);
        s->nevents--;
    }
    CrpEvent *ev = &s->events[s->nevents];
    ev->seq = seq;
    ev->event = vj_clone(event);
    ev->data = vj_clone(d);
    if (!ev->event || !ev->data) {
        vj_free(ev->event);
        vj_free(ev->data);
        return res_error(500, "out of memory");
    }
    s->nevents++;

    CrpResult res;
    res.status = 202;
    upp_buf_init(&res.body);
    upp_buf_puts(&res.body, "{\"accepted\":true,\"verse\":");
    crp_json_write(&res.body, verse);
    upp_buf_puts(&res.body, ",\"event\":");
    crp_json_write(&res.body, event);
    upp_buf_puts(&res.body, ",\"seq\":");
    upp_buf_put_ll(&res.body, seq);
    upp_buf_putc(&res.body, '}');
    return res;
}

CrpResult crp_registry_resume(CrpRegistry *r, const VjVal *verse, const VjVal *peer,
                              const VjVal *token, const VjVal *seqv,
                              const VjVal *replayv) {
    if (!vj_truthy(verse) || !vj_truthy(peer) || !vj_truthy(token))
        return res_error(400, "verse, peer and token are required");

    long long now = crp_registry_now(r);
    char vbuf[512], pbuf[512], tbuf[CRP_TOKEN_MAX];
    const char *vstr = crp_js_string(verse, vbuf, sizeof vbuf);
    const char *pstr = crp_js_string(peer, pbuf, sizeof pbuf);
    const char *tok = crp_js_string(token, tbuf, sizeof tbuf);
    /* This path calls checkToken(String(p.verse), String(p.peer)) -- the only
     * place the reference coerces, unlike /signal which passes p.verse raw. */
    if (!registry_has_verse(r, vstr) ||
        !crp_token_check_str(r->secret, tok, vstr, pstr, "signal", now) ||
        revoked_has(r, tok))
        return res_error(403, "invalid capability token");

    CrpSession *s = registry_get_session(r, vstr, pstr);
    if (!s) return res_error(500, "out of memory");
    long long prev = (long long)s->sess.last_applied;
    long long seq = (seqv && seqv->type == VJ_INT && upp_is_safe_int(seqv->i) && seqv->i >= 0)
                        ? seqv->i : 0;
    if (seq < prev && !vj_truthy(replayv)) {
        CrpResult res;
        res.status = 409;
        upp_buf_init(&res.body);
        upp_buf_puts(&res.body, "{\"error\":\"session sequence out of order\",\"lastSeq\":");
        upp_buf_put_ll(&res.body, prev);
        upp_buf_putc(&res.body, '}');
        return res;
    }
    long long last_seq = prev > seq ? prev : seq;
    if (seq > prev) {
        session_store_seq(s, seq);
    } else {
        /* duplicate: ask the platform session layer anyway, so the verdict is
         * the one §55.6 would give (0 = duplicate), and record it. */
        s->last_verdict = im_crp_session_accept(&s->sess, (uint64_t)seq);
    }
    im_crp_session_resume_plan(&s->sess, (uint64_t)last_seq, CRP_EVENT_WINDOW,
                               &s->replay_from, &s->needs_snapshot);
    if (s->sess.lease_id[0]) im_crp_session_lease_touch(&s->sess, (uint64_t)now, 0);

    CrpResult res;
    res.status = 200;
    upp_buf_init(&res.body);
    upp_buf_puts(&res.body, "{\"resumed\":true,\"verse\":");
    crp_json_write(&res.body, verse);
    upp_buf_puts(&res.body, ",\"peer\":");
    crp_json_write(&res.body, peer);
    upp_buf_puts(&res.body, ",\"lastSeq\":");
    upp_buf_put_ll(&res.body, last_seq);
    upp_buf_puts(&res.body, ",\"replay\":[");
    int first = 1;
    for (int i = 0; i < s->nevents; i++) {
        if (s->events[i].seq <= seq) continue;
        if (!first) upp_buf_putc(&res.body, ',');
        first = 0;
        upp_buf_puts(&res.body, "{\"seq\":");
        upp_buf_put_ll(&res.body, s->events[i].seq);
        upp_buf_puts(&res.body, ",\"event\":");
        crp_json_write(&res.body, s->events[i].event);
        upp_buf_puts(&res.body, ",\"data\":");
        crp_json_write(&res.body, s->events[i].data);
        upp_buf_putc(&res.body, '}');
    }
    upp_buf_puts(&res.body, "]}");
    return res;
}

CrpResult crp_registry_revoke(CrpRegistry *r, const VjVal *token) {
    if (!vj_truthy(token)) return res_error(400, "token is required");
    char scratch[CRP_TOKEN_MAX];
    revoked_add(r, crp_js_string(token, scratch, sizeof scratch));
    /* A revoked capability closes the session it was minted for. */
    for (size_t i = 0; i < r->nsessions; i++)
        if (r->sessions[i].started) im_crp_session_apply(&r->sessions[i].sess, "stop", 0, 0, NULL);
    return res_text(200, "{\"revoked\":true}");
}

void crp_registry_status_json(const CrpRegistry *r, CrpBuf *out) {
    if (!r) { upp_buf_puts(out, "{}"); return; }
    long long now = crp_registry_now(r);
    upp_buf_puts(out, "{\"verses\":[");
    for (size_t i = 0; i < r->nverses; i++) {
        if (i) upp_buf_putc(out, ',');
        upp_json_write(out, r->verses[i].obj);
    }
    upp_buf_puts(out, "],\"sessions\":[");
    for (size_t i = 0; i < r->nsessions; i++) {
        const CrpSession *s = &r->sessions[i];
        if (i) upp_buf_putc(out, ',');
        upp_buf_puts(out, "{\"verse\":");
        upp_json_write_string(out, s->verse);
        upp_buf_puts(out, ",\"peer\":");
        upp_json_write_string(out, s->peer);
        upp_buf_puts(out, ",\"lastSeq\":");
        upp_buf_put_ll(out, (long long)s->sess.last_applied);
        upp_buf_puts(out, ",\"acceptVerdict\":");
        upp_buf_put_ll(out, s->last_verdict);
        upp_buf_puts(out, ",\"state\":");
        upp_json_write_string(out, crp_state_name(s->sess.state));
        upp_buf_puts(out, ",\"generation\":");
        upp_buf_put_ll(out, (long long)s->sess.generation);
        upp_buf_puts(out, ",\"leaseId\":");
        if (s->sess.lease_id[0]) upp_json_write_string(out, s->sess.lease_id);
        else upp_buf_puts(out, "null");
        upp_buf_puts(out, ",\"leaseExpires\":");
        if (s->sess.lease_id[0]) upp_buf_put_ll(out, (long long)s->sess.lease_expires_ms);
        else upp_buf_puts(out, "null");
        upp_buf_puts(out, ",\"leaseExpired\":");
        if (s->sess.lease_id[0]) upp_buf_puts(out, im_crp_session_lease_expired(&s->sess, (uint64_t)now) ? "true" : "false");
        else upp_buf_puts(out, "null");
        upp_buf_puts(out, ",\"replayFrom\":");
        upp_buf_put_ll(out, s->replay_from);
        upp_buf_puts(out, ",\"needsSnapshot\":");
        upp_buf_puts(out, s->needs_snapshot ? "true" : "false");
        upp_buf_puts(out, ",\"events\":");
        upp_buf_put_ll(out, s->nevents);
        upp_buf_putc(out, '}');
    }
    upp_buf_puts(out, "],\"revoked\":");
    upp_buf_put_ll(out, (long long)r->nrevoked);
    upp_buf_putc(out, '}');
}
