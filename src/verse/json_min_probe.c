/* json_min_probe.c - offline assertions for the `\u` decoder in json_min.c
 *
 * Three defects used to make every one of these fail:
 *
 *   \uD83D\uDE00  the two halves were encoded separately as CESU-8
 *                 (ED A0 BD ED B8 80) instead of the combined U+1F600
 *                 (F0 9F 98 80), which is not even valid UTF-8;
 *   \uD83D        a lone surrogate was passed through as its 3-byte
 *                 "encoding" instead of U+FFFD (EF BF BD);
 *   \u0000        a raw NUL was written into a length-less `char *s`, so
 *                 "x\u0000y" and "alice\u0000A" both came back as "x" and
 *                 "alice" -- two different JSON strings collapsed onto the
 *                 same C string.
 *
 * The first two already violated the byte-identical-with-Node contract that
 * tools/crp_engine_crosscheck.js enforces; that corpus simply never contained
 * a surrogate.  The NUL case is refused on purpose: no writer in this repo can
 * emit it (they all loop `for (p = s; *p; p++)`, so the smallest escape they
 * can produce is \u0001), and VjVal has no length field to represent it with.
 * See docs/streams/json-min-nul-escape.md and docs/STATUS.md §10.6.
 *
 * No assertion lives in assert(): the release build compiles with -DNDEBUG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_min.h"
#include "upp.h"

#define U_EMOJI   "\xF0\x9F\x98\x80"   /* U+1F600, what \uD83D\uDE00 must become */
#define U_REPLACE "\xEF\xBF\xBD"       /* U+FFFD, what a lone surrogate becomes */

static int g_checks = 0;
static int g_failures = 0;

static void fail(const char *label, const char *detail) {
    printf("FAIL %s: %s\n", label, detail);
}

/* Parses `json`, expects `vj_str` to be exactly the bytes of `want`, and
 * returns the value on success (caller frees) or NULL after recording a
 * failure.  `want` is a hex byte string, e.g. "f09f9880". */
static VjVal *expect_str(const char *json, const char *want, const char *label) {
    char err[128];
    err[0] = '\0';
    VjVal *v = vj_parse(json, err, sizeof err);
    g_checks++;
    if (!v) { g_failures++; fail(label, err[0] ? err : "parse returned NULL"); return NULL; }
    const char *s = vj_str(v, NULL);
    if (!s) { g_failures++; fail(label, "value is not a string"); vj_free(v); return NULL; }
    char hex[256];
    size_t n = strlen(s);
    if (n * 2 + 1 > sizeof hex) { g_failures++; fail(label, "string too long"); vj_free(v); return NULL; }
    for (size_t i = 0; i < n; i++) snprintf(hex + i * 2, 3, "%02x", (unsigned char)s[i]);
    if (strcmp(hex, want) != 0) {
        char detail[384];
        snprintf(detail, sizeof detail, "got %s want %s", hex, want);
        g_failures++;
        fail(label, detail);
    }
    return v;
}

/* A parse that must be refused: NULL value and a non-empty error. */
static void expect_refusal(const char *json, const char *detail, const char *label) {
    char err[128];
    err[0] = '\0';
    VjVal *v = vj_parse(json, err, sizeof err);
    g_checks++;
    if (v) { g_failures++; fail(label, "expected NULL, parse succeeded"); vj_free(v); return; }
    if (err[0] == '\0') { g_failures++; fail(label, "refused with an empty error"); return; }
    if (detail && *detail && strncmp(err, detail, strlen(detail)) != 0) {
        g_failures++;
        fail(label, err);
    }
}

/* The bytes a string must carry as an object KEY rather than as a value:
 * vj_get is keyed by strcmp, so it only finds the member when the stored key
 * is exactly `want`. */
static void expect_key(const char *json, const char *want, const char *want_hex, const char *label) {
    char err[128];
    err[0] = '\0';
    VjVal *v = vj_parse(json, err, sizeof err);
    g_checks++;
    if (!v) { g_failures++; fail(label, err[0] ? err : "parse returned NULL"); return; }
    const VjVal *child = vj_get(v, want);
    const char *s = child ? vj_str(child, NULL) : NULL;
    if (!s || strcmp(s, "v") != 0) {
        g_failures++;
        fail(label, "key not found with the expected bytes");
        vj_free(v);
        return;
    }
    g_checks++;
    if (v->nkv != 1 || !v->keys[0]) {
        g_failures++;
        fail(label, "object has no single stored key");
        vj_free(v);
        return;
    }
    char hex[64];
    size_t n = strlen(v->keys[0]);
    for (size_t i = 0; i < n; i++) snprintf(hex + i * 2, 3, "%02x", (unsigned char)v->keys[0][i]);
    if (strcmp(hex, want_hex) != 0) {
        char detail[160];
        snprintf(detail, sizeof detail, "stored key is %s want %s", hex, want_hex);
        g_failures++;
        fail(label, detail);
    }
    vj_free(v);
}

/* parse(write(s)) == s for every string the engine's own writer can produce.
 * The writer is the shipped one (upp_json_write_string), not a copy, so this
 * locks the reader against the exact bytes that reach the wire. */
static void round_trip(const char *s, const char *label) {
    UppBuf b;
    upp_buf_init(&b);
    upp_json_write_string(&b, s);
    char err[128];
    err[0] = '\0';
    VjVal *v = vj_parse(b.data, err, sizeof err);
    g_checks++;
    if (!v) { g_failures++; fail(label, err[0] ? err : "parse returned NULL"); upp_buf_free(&b); return; }
    const char *back = vj_str(v, NULL);
    if (!back || strcmp(back, s) != 0) { g_failures++; fail(label, "round trip differs"); }
    vj_free(v);
    upp_buf_free(&b);
}

static void test_values(void) {
    VjVal *v;

    /* the 9-row table from docs/streams/json-min-nul-escape.md §1 */
    v = expect_str("\"\\uD83D\\uDE00\"", "f09f9880", "pair U+1F600");
    vj_free(v);
    v = expect_str("\"\\uD83D\"", "efbfbd", "lone high surrogate");
    vj_free(v);
    v = expect_str("\"\\uDE00\"", "efbfbd", "lone low surrogate");
    vj_free(v);
    v = expect_str("\"\\u00e9\"", "c3a9", "U+00E9 unchanged");
    vj_free(v);
    v = expect_str("\"\\u4e2d\"", "e4b8ad", "U+4E2D unchanged");
    vj_free(v);
    v = expect_str("\"\\u0001\"", "01", "U+0001 unchanged");
    vj_free(v);

    /* \u0000 is the deliberate divergence: refused, not truncated */
    expect_refusal("\"x\\u0000y\"", "\\u0000 is not representable", "x\\u0000y refused");
    expect_refusal("\"alice\\u0000A\"", "\\u0000 is not representable", "alice\\u0000A refused");
    expect_refusal("\"alice\\u0000B\"", "\\u0000 is not representable", "alice\\u0000B refused");
    /* ... and the two collapsed inputs are no longer two different values */
    {
        char e1[128], e2[128];
        e1[0] = e2[0] = '\0';
        VjVal *a = vj_parse("\"alice\\u0000A\"", e1, sizeof e1);
        VjVal *b = vj_parse("\"alice\\u0000B\"", e2, sizeof e2);
        g_checks++;
        if (a || b) { g_failures++; fail("alice\\u0000A/B", "one of the two parses succeeded"); }
        vj_free(a);
        vj_free(b);
    }

    /* pairs across the whole surrogate range and their boundaries */
    v = expect_str("\"\\uD800\\uDC00\"", "f0908080", "pair U+10000 (lowest)");
    vj_free(v);
    v = expect_str("\"\\uDBFF\\uDFFF\"", "f48fbfbf", "pair U+10FFFF (highest)");
    vj_free(v);
    v = expect_str("\"\\uD83D\\uDE00\\uD83D\\uDE00\"", "f09f9880f09f9880", "two pairs back to back");
    vj_free(v);
    v = expect_str("\"\\uD83D\\uDE00x\"", "f09f988078", "pair then plain ASCII");
    vj_free(v);

    /* a high surrogate followed by something that is NOT a low surrogate is
     * lone, and the following content must still be decoded */
    v = expect_str("\"\\uD83D\\uD83D\"", "efbfbdefbfbd", "two high surrogates are both lone");
    vj_free(v);
    v = expect_str("\"\\uD83DA\"", "efbfbd41", "high surrogate then ASCII");
    vj_free(v);
    v = expect_str("\"\\uD83D\\n\"", "efbfbd0a", "high surrogate then short escape");
    vj_free(v);
    v = expect_str("\"\\uDE00\\uD83D\\uDE00\"", "efbfbdf09f9880", "lone low then a valid pair");
    vj_free(v);

    /* a `\u` that is not followed by four hex digits is still a bad escape,
     * not a lone surrogate */
    expect_refusal("\"\\uD83D\\uZZ\"", "bad \\u escape", "malformed second escape");
    expect_refusal("\"\\uD83D\\u00\"", "bad \\u escape", "short second escape");
    expect_refusal("\"\\uD83D\\uD8\"", "bad \\u escape", "truncated second escape");
    expect_refusal("\"\\uD800\\u0000\"", NULL, "pair second half is NUL");
    expect_refusal("\"\\u00\"", "bad \\u escape", "short escape, still refused");
    expect_refusal("\"\\uZZZZ\"", "bad \\u escape", "non-hex escape, still refused");

    /* the escape must not swallow the byte after it */
    v = expect_str("\"\\u0041\\u0042\"", "4142", "two BMP escapes");
    vj_free(v);
    v = expect_str("\"\\u0041x\"", "4178", "BMP escape then ASCII");
    vj_free(v);
}

static void test_keys(void) {
    /* object keys go through the same decoder, so a pair in a key must
     * combine and a NUL in a key must be refused too */
    expect_key("{\"\\uD83D\\uDE00\":\"v\"}", U_EMOJI, "f09f9880", "pair in key");
    expect_key("{\"\\uD83D\":\"v\"}", U_REPLACE, "efbfbd", "lone surrogate in key");
    expect_key("{\"\\u00e9\":\"v\"}", "\xC3\xA9", "c3a9", "U+00E9 in key");
    expect_refusal("{\"\\u0000\":\"v\"}", "\\u0000 is not representable", "NUL in key refused");
    expect_refusal("{\"a\\u0000b\":1}", "\\u0000 is not representable", "NUL inside key refused");
}

static void test_round_trip(void) {
    /* every escape the engine writers can emit: the 7 short escapes and
     * \u0001-\u001f.  \u0000 is deliberately absent -- no writer can produce
     * it, because they all stop at *p. */
    round_trip("a\"b\\c/d", "short escapes: quote backslash slash");
    round_trip("a\bb\fc\nd\re\tf", "short escapes: b f n r t");
    for (unsigned cp = 1; cp <= 0x1f; cp++) {
        char s[2] = { (char)cp, '\0' };
        char label[64];
        snprintf(label, sizeof label, "\\u%04x round trip", cp);
        round_trip(s, label);
    }
    round_trip("\x7f", "DEL passes through");
    round_trip("\xc3\xa9\xe4\xb8\xad", "multibyte text passes through");
    round_trip(U_EMOJI, "4-byte UTF-8 passes through");
    /* a 4-byte write right where the buffer growth check sits: 32 bytes of
     * headroom is consumed, so the realloc path is exercised mid-string */
    round_trip(U_EMOJI U_EMOJI U_EMOJI U_EMOJI U_EMOJI U_EMOJI
               U_EMOJI U_EMOJI U_EMOJI U_EMOJI U_EMOJI U_EMOJI,
               "4-byte writes across a realloc boundary");
}

/* A `\uXXXX` flush against the last byte of the input must refuse, not read
 * past the terminator.  The heap buffer is exactly strlen+1, so under ASan any
 * read past the NUL is a heap-buffer-overflow. */
static void test_flush_at_end(void) {
    static const char *tails[] = { "\"\\u004", "\"\\u0041", "\"\\uD83D", "\"\\uD83D\\u", "\"\\u" };
    for (size_t i = 0; i < sizeof tails / sizeof tails[0]; i++) {
        size_t n = strlen(tails[i]);
        char *buf = (char *)malloc(n + 1);
        memcpy(buf, tails[i], n + 1);
        char err[128];
        err[0] = '\0';
        VjVal *v = vj_parse(buf, err, sizeof err);
        g_checks++;
        if (v) { g_failures++; fail("flush-at-end", "expected a refusal"); vj_free(v); }
        else if (err[0] == '\0') { g_failures++; fail("flush-at-end", "refused with an empty error"); }
        free(buf);
    }
}

int main(void) {
    test_values();
    test_keys();
    test_round_trip();
    test_flush_at_end();
    printf("json_min_probe: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
