/* upp_probe.c - engine-side UPP boundaries
 *
 * Two jobs:
 *
 *   1. boundary assertions for the rules in docs/streams/upp-in-engine.md §3
 *      (wire format) and §4 (state machine);
 *   2. `--transcript <corpus.jsonl>`: replay a corpus through the engine and
 *      print one canonical record per op on stdout.  tools/upp_engine_crosscheck.js
 *      generates the same corpus for tools/upp_session.js and diffs the two
 *      transcripts byte for byte, which is what proves the engine and the JS
 *      reference agree event for event.
 *
 * The record shape is a contract with that driver: keep the key order, keep it
 * one line per op.  The probe writes nothing else to stdout in transcript mode
 * (diagnostics go to stderr) so a diff points at the offending op.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "upp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "probe_compat.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define MANIFEST_JSON \
    "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\"," \
    "\"engine\":\"inimerse\",\"entry\":\"main.im\",\"abi\":1}"

/* --------------------------------------------------------------- test utils */

static VjVal *J(const char *text) {
    char err[UPP_ERR_MAX];
    VjVal *v = vj_parse(text, err, sizeof err);
    if (!v) {
        checks++;
        failures++;
        printf("FAIL probe JSON literal did not parse (%s): %s\n", err, text);
    }
    return v;
}

static void breset(UppBuf *b) {
    if (b->data) b->data[0] = '\0';
    b->len = 0;
}

static const char *B(const UppBuf *b) { return b->data ? b->data : ""; }

static int expect_text(const char *got, const char *want, int line) {
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %s:%d:\n  want %s\n  got  %s\n", __FILE__, line, want, got);
        return 0;
    }
    return 1;
}

static int expect_error(const char *got, const char *want, int line) {
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %s:%d: want error \"%s\", got \"%s\"\n", __FILE__, line, want, got);
        return 0;
    }
    return 1;
}

#define EXPECT_TEXT(got, want) expect_text(got, want, __LINE__)
#define EXPECT_ERR(got, want)  expect_error(got, want, __LINE__)

static int apply_text(UppSession *s, const char *json, char *err, size_t errlen) {
    VjVal *m = J(json);
    if (!m) return -1;
    int rc = upp_session_apply(s, m, err, errlen);
    vj_free(m);
    return rc;
}

/* UTF-8 code point count: bytes that are not continuation bytes. */
static size_t utf8_chars(const char *s) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

static void session_init(UppSession *s, const char *role, const char *manifest_json) {
    VjVal *m = manifest_json ? J(manifest_json) : NULL;
    char err[UPP_ERR_MAX];
    if (upp_session_init(s, role, m, err, sizeof err) != 0) printf("FAIL session init: %s\n", err);
    vj_free(m);
}

/* A host session in `running` with one heartbeat already recorded. */
static void running_session(UppSession *s) {
    session_init(s, "host", MANIFEST_JSON);
    char err[UPP_ERR_MAX];
    if (apply_text(s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                   err, sizeof err) != 0)
        printf("FAIL start: %s\n", err);
    if (apply_text(s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":1,\"timestamp\":1000}}",
                   err, sizeof err) != 0)
        printf("FAIL heartbeat: %s\n", err);
}

/* -------------------------------------------------------------- wire format */

static void test_frame_shape(void) {
    UppBuf b;
    upp_buf_init(&b);
    char err[UPP_ERR_MAX];
    VjVal *args;

    /* `id` empty means the key is gone, not an empty string */
    CHECK(upp_frame_build(&b, "log", "", "{}", err, sizeof err) == 0, "build: %s", err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"log\",\"payload\":{}}");
    breset(&b);
    upp_frame_build(&b, "log", NULL, "{}", err, sizeof err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"log\",\"payload\":{}}");
    breset(&b);
    upp_frame_build(&b, "log", "abc", "{}", err, sizeof err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"log\",\"id\":\"abc\",\"payload\":{}}");
    breset(&b);
    CHECK(upp_frame_build(&b, "", NULL, "{}", err, sizeof err) == -1, "empty type refused");
    EXPECT_ERR(err, "frame type is required");

    /* control frames, in the reference's key order */
    breset(&b);
    CHECK(upp_frame_heartbeat(&b, 1, 123, err, sizeof err) == 0, "heartbeat: %s", err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":1,\"timestamp\":123}}");

    breset(&b);
    CHECK(upp_frame_start(&b, "main.im", NULL, err, sizeof err) == 0, "start: %s", err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\",\"args\":[]}}");
    breset(&b);
    args = J("[\"a\",\"b\"]");
    CHECK(upp_frame_start(&b, "main.im", args, err, sizeof err) == 0, "start args: %s", err);
    EXPECT_TEXT(B(&b),
                "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\",\"args\":[\"a\",\"b\"]}}");
    vj_free(args);

    breset(&b);
    CHECK(upp_frame_stop(&b, "requested", err, sizeof err) == 0, "stop: %s", err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"stop\",\"payload\":{\"reason\":\"requested\"}}");

    breset(&b);
    CHECK(upp_frame_crash(&b, "boom", 3, 1, 99, err, sizeof err) == 0, "crash: %s", err);
    EXPECT_TEXT(B(&b),
                "{\"upp\":1,\"type\":\"crash\",\"payload\":{\"error\":\"boom\",\"exitCode\":3,\"timestamp\":99}}");
    breset(&b);
    CHECK(upp_frame_crash(&b, "boom", 0, 0, 99, err, sizeof err) == 0, "crash null exit code: %s", err);
    EXPECT_TEXT(B(&b),
                "{\"upp\":1,\"type\":\"crash\",\"payload\":{\"error\":\"boom\",\"exitCode\":null,\"timestamp\":99}}");

    breset(&b);
    CHECK(upp_frame_incompatible(&b, 2, 1, err, sizeof err) == 0, "incompatible: %s", err);
    EXPECT_TEXT(B(&b), "{\"upp\":1,\"type\":\"incompatible\",\"payload\":{\"required\":2,\"actual\":1}}");

    breset(&b);
    CHECK(upp_frame_log(&b, "info", "hi", 7, err, sizeof err) == 0, "log: %s", err);
    EXPECT_TEXT(B(&b),
                "{\"upp\":1,\"type\":\"log\",\"payload\":{\"level\":\"info\",\"message\":\"hi\",\"timestamp\":7}}");

    /* argument-level refusals, message for message */
    breset(&b);
    CHECK(upp_frame_control(&b, "bogus", "{}", NULL, err, sizeof err) == -1, "unknown control type refused");
    EXPECT_ERR(err, "unsupported UPP control type: bogus");
    breset(&b);
    CHECK(upp_frame_heartbeat(&b, -1, 1, err, sizeof err) == -1, "negative seq refused");
    EXPECT_ERR(err, "heartbeat sequence must be a non-negative integer");
    CHECK(upp_frame_heartbeat(&b, 9007199254740992LL, 1, err, sizeof err) == -1,
          "seq beyond Number.MAX_SAFE_INTEGER refused");
    EXPECT_ERR(err, "heartbeat sequence must be a non-negative integer");
    breset(&b);
    CHECK(upp_frame_start(&b, "   ", NULL, err, sizeof err) == -1, "blank entry refused");
    EXPECT_ERR(err, "start entry is required");
    args = J("[\"a\",1]");
    CHECK(upp_frame_start(&b, "main.im", args, err, sizeof err) == -1, "non-string arg refused");
    EXPECT_ERR(err, "start args must be strings");
    vj_free(args);
    breset(&b);
    CHECK(upp_frame_stop(&b, "", err, sizeof err) == -1, "blank stop reason refused");
    EXPECT_ERR(err, "stop reason is required");
    CHECK(upp_frame_log(&b, "trace", "x", 1, err, sizeof err) == -1, "bad log level refused");
    EXPECT_ERR(err, "invalid UPP log level");
    CHECK(upp_frame_crash(&b, " ", 0, 0, 1, err, sizeof err) == -1, "blank crash error refused");
    EXPECT_ERR(err, "crash error is required");
    CHECK(upp_frame_crash(&b, "boom", -1, 1, 1, err, sizeof err) == -1, "negative exit code refused");
    EXPECT_ERR(err, "invalid crash exit code");
    args = J(MANIFEST_JSON);
    CHECK(upp_frame_hello(&b, "nobody", args, NULL, err, sizeof err) == -1, "bad role refused");
    EXPECT_ERR(err, "invalid role: nobody");
    vj_free(args);

    upp_buf_free(&b);
}

static void test_manifest_validation(void) {
    static const struct { const char *json; const char *want; } cases[] = {
        { "{\"id\":\"demo\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"inimerse\",\"entry\":\"main.im\"}", NULL },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\",\"abi\":1,\"abiRange\":\"1..3\"}", NULL },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0-alpha.1\",\"engine\":\"e\",\"entry\":\"m\"}", NULL },
        { "{\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\"}", "manifest.id is required" },
        { "{\"id\":\"a\",\"name\":\"  \",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\"}", "manifest.name is required" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0\",\"engine\":\"e\",\"entry\":\"m\"}", "manifest.version must be semver" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"\",\"entry\":\"m\"}", "manifest.engine is required" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":5}", "manifest.entry is required" },
        { "{\"id\":\"-bad\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\"}", "manifest.id has invalid characters" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\",\"files\":[]}", "manifest.files must be an object" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\",\"capabilities\":[1]}", "manifest.capabilities must be an array of strings" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\",\"abi\":0}", "manifest.abi must be a positive integer" },
        { "{\"id\":\"a\",\"name\":\"D\",\"version\":\"1.0.0\",\"engine\":\"e\",\"entry\":\"m\",\"abiRange\":\"1..\"}", "manifest.abiRange must be N or N..M" },
        { "[]", "manifest must be an object" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        VjVal *m = J(cases[i].json);
        char err[UPP_ERR_MAX];
        int rc = upp_validate_manifest(m, err, sizeof err);
        checks++;
        if (cases[i].want == NULL) {
            if (rc != 0) {
                failures++;
                printf("FAIL manifest case %zu should be valid, got \"%s\"\n", i, err);
            }
        } else if (rc == 0 || strcmp(err, cases[i].want) != 0) {
            failures++;
            printf("FAIL manifest case %zu: want \"%s\", got rc=%d \"%s\"\n", i, cases[i].want, rc, err);
        }
        vj_free(m);
    }
}

/* The cap is measured in UTF-8 BYTES, not characters. */
static void test_frame_size_limit(void) {
    const char *prefix = "{\"upp\":1,\"type\":\"log\",\"payload\":{\"message\":\"";
    const char *suffix = "\"}}";
    size_t base = strlen(prefix) + strlen(suffix);
    CHECK(base < UPP_MAX_FRAME_BYTES, "prefix fits");

    size_t exact = UPP_MAX_FRAME_BYTES - base;
    char *text = (char *)malloc(exact + base + 2);
    CHECK(text != NULL, "alloc exact frame");
    if (!text) return;
    snprintf(text, exact + 1, "%s", prefix);
    memset(text + strlen(prefix), 'x', exact);
    memcpy(text + strlen(prefix) + exact, suffix, strlen(suffix) + 1);
    CHECK(strlen(text) == UPP_MAX_FRAME_BYTES, "exact frame is %d bytes, got %zu",
          UPP_MAX_FRAME_BYTES, strlen(text));

    UppBuf out;
    upp_buf_init(&out);
    char err[UPP_ERR_MAX];
    CHECK(upp_encode_text(&out, text, err, sizeof err) == 0, "exactly 1 MiB is accepted: %s", err);
    CHECK(out.len == UPP_MAX_FRAME_BYTES + 1, "encoded line adds the newline, got %zu", out.len);
    VjVal *decoded = upp_decode_line(text, strlen(text), err, sizeof err);
    CHECK(decoded != NULL, "exactly 1 MiB decodes: %s", err);
    vj_free(decoded);

    /* one byte more is refused by both the encoder and the decoder */
    char *over = (char *)malloc(exact + base + 3);
    CHECK(over != NULL, "alloc over frame");
    if (over) {
        snprintf(over, exact + 2, "%s", prefix);
        memset(over + strlen(prefix), 'x', exact + 1);
        memcpy(over + strlen(prefix) + exact + 1, suffix, strlen(suffix) + 1);
        CHECK(strlen(over) == UPP_MAX_FRAME_BYTES + 1, "overlong frame is 1 MiB + 1");
        breset(&out);
        CHECK(upp_encode_text(&out, over, err, sizeof err) == -1, "1 MiB + 1 refused");
        EXPECT_ERR(err, "UPP frame exceeds 1 MiB");
        CHECK(upp_decode_line(over, strlen(over), err, sizeof err) == NULL, "1 MiB + 1 refused on decode");
        EXPECT_ERR(err, "UPP frame exceeds 1 MiB");
        free(over);
    }
    breset(&out);

    /* multibyte: 700000 two-byte characters = 1.4 MB, well under 1 MiB chars */
    size_t nchars = 700000;
    char *wide = (char *)malloc(strlen(prefix) + nchars * 2 + strlen(suffix) + 1);
    CHECK(wide != NULL, "alloc wide frame");
    if (wide) {
        char *w = wide;
        memcpy(w, prefix, strlen(prefix));
        w += strlen(prefix);
        for (size_t i = 0; i < nchars; i++) { *w++ = (char)0xC3; *w++ = (char)0xA9; }
        memcpy(w, suffix, strlen(suffix) + 1);
        CHECK(utf8_chars(wide) < UPP_MAX_FRAME_BYTES, "wide frame is under 1 MiB characters (%zu)",
              utf8_chars(wide));
        CHECK(strlen(wide) > UPP_MAX_FRAME_BYTES, "wide frame is over 1 MiB bytes (%zu)", strlen(wide));
        breset(&out);
        CHECK(upp_encode_text(&out, wide, err, sizeof err) == -1, "multibyte frame over 1 MiB refused");
        EXPECT_ERR(err, "UPP frame exceeds 1 MiB");
        free(wide);
    }

    /* encode() keeps the reference's error for a bad version */
    breset(&out);
    CHECK(upp_encode_text(&out, "{\"type\":\"log\",\"payload\":{}}", err, sizeof err) == -1, "missing upp refused");
    EXPECT_ERR(err, "unsupported UPP version: undefined");
    CHECK(upp_encode_text(&out, "{\"upp\":2,\"type\":\"log\",\"payload\":{}}", err, sizeof err) == -1, "upp 2 refused");
    EXPECT_ERR(err, "unsupported UPP version: 2");
    CHECK(upp_encode(&out, NULL, err, sizeof err) == -1, "encode null refused");
    EXPECT_ERR(err, "message must be an object");

    upp_buf_free(&out);
    free(text);
}

/* ------------------------------------------------------------ decode/decoder */

typedef struct {
    int  count;
    char lines[8][256];
} DecoderSink;

static void sink_on_message(const VjVal *message, void *user) {
    DecoderSink *sink = (DecoderSink *)user;
    const VjVal *type = vj_get(message, "type");
    if (sink->count < 8)
        snprintf(sink->lines[sink->count], sizeof sink->lines[0], "%s",
                 (type && type->type == VJ_STR && type->s) ? type->s : "?");
    sink->count++;
}

static void test_decoder(void) {
    char err[UPP_ERR_MAX];
    const char *hello_line = "{\"upp\":1,\"type\":\"hello\",\"payload\":{}}";
    VjVal *v = upp_decode_line(hello_line, strlen(hello_line), err, sizeof err);
    CHECK(v != NULL, "decode hello: %s", err);
    vj_free(v);

    CHECK(upp_decode_line("[1]", 3, err, sizeof err) == NULL, "array is not a frame");
    EXPECT_ERR(err, "message must be an object");
    const char *bad_version = "{\"upp\":2,\"type\":\"hello\"}";
    CHECK(upp_decode_line(bad_version, strlen(bad_version), err, sizeof err) == NULL, "upp 2 rejected");
    EXPECT_ERR(err, "invalid UPP frame header");
    CHECK(upp_decode_line("{\"upp\":1}", 9, err, sizeof err) == NULL, "missing type rejected");
    EXPECT_ERR(err, "invalid UPP frame header");

    DecoderSink sink;
    memset(&sink, 0, sizeof sink);
    UppDecoder d;
    upp_decoder_init(&d, sink_on_message, &sink);

    /* a frame split across two pushes, CRLF line endings, and blank lines */
    const char *a = "{\"upp\":1,\"type\":\"one\"}\r\n\r\n{\"upp\":1,\"type\":\"tw";
    const char *b = "o\"}\n{\"upp\":1,\"type\":\"three\"}\n";
    CHECK(upp_decoder_push(&d, a, strlen(a)) == 0, "push a: %s", d.error);
    CHECK(sink.count == 1, "only the complete line was dispatched, got %d", sink.count);
    CHECK(upp_decoder_push(&d, b, strlen(b)) == 0, "push b: %s", d.error);
    CHECK(sink.count == 3, "three frames so far, got %d", sink.count);
    if (sink.count >= 3) {
        EXPECT_TEXT(sink.lines[0], "one");
        EXPECT_TEXT(sink.lines[1], "two");
        EXPECT_TEXT(sink.lines[2], "three");
    }
    /* end() flushes a trailing line with no newline */
    const char *four = "{\"upp\":1,\"type\":\"four\"}";
    CHECK(upp_decoder_push(&d, four, strlen(four)) == 0, "push four");
    CHECK(sink.count == 3, "unterminated line is held back, got %d", sink.count);
    CHECK(upp_decoder_end(&d) == 0, "end: %s", d.error);
    CHECK(sink.count == 4, "end flushed the trailing frame, got %d", sink.count);
    if (sink.count == 4) EXPECT_TEXT(sink.lines[3], "four");
    upp_decoder_free(&d);

    /* a malformed line fails loudly instead of being skipped */
    memset(&sink, 0, sizeof sink);
    upp_decoder_init(&d, sink_on_message, &sink);
    CHECK(upp_decoder_push(&d, "not json\n", 9) == -1, "malformed line reported");
    CHECK(d.failed == 1, "decoder marked failed");
    upp_decoder_free(&d);

    /* the reference caps the pending buffer at 2 * MAX_FRAME_BYTES */
    size_t n = UPP_DECODER_MAX_PENDING + 8;
    char *spaces = (char *)malloc(n + 1);
    CHECK(spaces != NULL, "alloc flood");
    if (spaces) {
        memset(spaces, ' ', n);
        spaces[n] = '\0';
        memset(&sink, 0, sizeof sink);
        upp_decoder_init(&d, sink_on_message, &sink);
        CHECK(upp_decoder_push(&d, spaces, n) == -1, "2 MiB pending refused");
        EXPECT_ERR(d.error, "UPP input buffer is too large");
        upp_decoder_free(&d);
        free(spaces);
    }
}

/* ------------------------------------------------------- hello / negotiate */

static void test_accept_hello(void) {
    UppBuf welcome;
    upp_buf_init(&welcome);
    char err[UPP_ERR_MAX];
    UppSession s, t;
    VjVal *remote;

    session_init(&s, "host", MANIFEST_JSON);
    remote = J("{\"upp\":1,\"type\":\"hello\",\"payload\":{\"role\":\"client\",\"manifest\":"
               "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"engine\":\"inimerse\","
               "\"entry\":\"main.im\",\"abi\":1},\"capabilities\":[\"heartbeat\"]}}");
    CHECK(upp_session_accept_hello(&s, remote, &welcome, err, sizeof err) == 0, "acceptHello: %s", err);
    EXPECT_TEXT(B(&welcome),
                "{\"upp\":1,\"type\":\"welcome\",\"payload\":{\"protocol\":1,\"peerRole\":\"client\","
                "\"capabilities\":[],\"abi\":1}}");
    CHECK(s.state == UPP_STATE_IDLE, "negotiation does not change the state");
    CHECK(s.has_abi && s.abi == 1, "abi recorded");
    vj_free(remote);

    /* same role: the reference refuses and parks in incompatible */
    remote = J("{\"upp\":1,\"type\":\"hello\",\"payload\":{\"role\":\"host\",\"manifest\":"
               "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"engine\":\"inimerse\","
               "\"entry\":\"main.im\",\"abi\":1}}}");
    session_init(&t, "host", MANIFEST_JSON);
    CHECK(upp_session_accept_hello(&t, remote, &welcome, err, sizeof err) == -1, "same role refused");
    EXPECT_ERR(err, "UPP peers must use different roles");
    CHECK(t.state == UPP_STATE_INCOMPATIBLE, "same role moves to incompatible");
    CHECK(t.error_set && strcmp(t.error, "UPP peers must use different roles") == 0, "error recorded");
    CHECK(upp_session_recover(&t, err, sizeof err) == -1, "incompatible cannot recover");
    EXPECT_ERR(err, "cannot recover from incompatible");
    upp_session_reset(&t);
    CHECK(t.state == UPP_STATE_IDLE && !t.error_set, "reset leaves incompatible");
    vj_free(remote);

    /* a remote that is not an object is refused before anything else */
    remote = J("\"nope\"");
    session_init(&t, "host", MANIFEST_JSON);
    CHECK(upp_session_accept_hello(&t, remote, &welcome, err, sizeof err) == -1, "non-object hello refused");
    EXPECT_ERR(err, "remote hello must be an object");
    vj_free(remote);

    /* disjoint ABI ranges are refused, with both sides quoted */
    remote = J("{\"upp\":1,\"type\":\"hello\",\"payload\":{\"role\":\"client\",\"manifest\":"
               "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"engine\":\"inimerse\","
               "\"entry\":\"main.im\",\"abiRange\":\"5\"}}}");
    session_init(&t, "host", "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\","
                             "\"engine\":\"inimerse\",\"entry\":\"main.im\",\"abi\":1}");
    CHECK(upp_session_accept_hello(&t, remote, &welcome, err, sizeof err) == -1, "ABI clash refused");
    EXPECT_ERR(err, "incompatible ABI ranges: 1 vs 5");
    CHECK(t.state == UPP_STATE_INCOMPATIBLE, "ABI clash moves to incompatible");
    vj_free(remote);

    /* overlapping ranges negotiate the higher lower bound */
    remote = J("{\"upp\":1,\"type\":\"hello\",\"payload\":{\"role\":\"client\",\"manifest\":"
               "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"engine\":\"inimerse\","
               "\"entry\":\"main.im\",\"abiRange\":\"3..9\"}}}");
    session_init(&t, "host", "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\","
                             "\"engine\":\"inimerse\",\"entry\":\"main.im\",\"abiRange\":\"1..4\"}");
    breset(&welcome);
    CHECK(upp_session_accept_hello(&t, remote, &welcome, err, sizeof err) == 0, "overlap negotiates: %s", err);
    CHECK(t.abi == 3, "negotiated abi is 3, got %lld", t.abi);
    vj_free(remote);

    /* a frame with no payload falls back to the frame itself: no role, so
     * JSON.stringify omits the peerRole key entirely */
    remote = J("{\"upp\":1,\"type\":\"hello\"}");
    session_init(&t, "host", MANIFEST_JSON);
    breset(&welcome);
    CHECK(upp_session_accept_hello(&t, remote, &welcome, err, sizeof err) == 0, "payload-less hello: %s", err);
    EXPECT_TEXT(B(&welcome),
                "{\"upp\":1,\"type\":\"welcome\",\"payload\":{\"protocol\":1,"
                "\"capabilities\":[],\"abi\":1}}");
    vj_free(remote);

    upp_buf_free(&welcome);
}

/* ----------------------------------------------------------- state machine */

static void test_full_sequence(void) {
    UppSession s;
    session_init(&s, "host", MANIFEST_JSON);
    char err[UPP_ERR_MAX];
    UppBuf welcome;
    upp_buf_init(&welcome);

    VjVal *remote = J("{\"upp\":1,\"type\":\"hello\",\"payload\":{\"role\":\"client\",\"manifest\":"
                      "{\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"engine\":\"inimerse\","
                      "\"entry\":\"main.im\",\"abi\":1}}}");
    CHECK(upp_session_accept_hello(&s, remote, &welcome, err, sizeof err) == 0, "hello: %s", err);
    vj_free(remote);
    CHECK(s.state == UPP_STATE_IDLE, "idle after hello");

    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                     err, sizeof err) == 0, "start: %s", err);
    CHECK(s.state == UPP_STATE_RUNNING, "running after start");

    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":1,\"timestamp\":1000}}",
                     err, sizeof err) == 0, "heartbeat: %s", err);
    CHECK(s.last_heartbeat == 1 && s.last_heartbeat_at == 1000, "heartbeat recorded");

    /* the peer goes dark: 15 s without a heartbeat is a crash */
    CHECK(upp_session_check_heartbeat(&s, 1000 + UPP_HEARTBEAT_TIMEOUT_MS, UPP_HEARTBEAT_TIMEOUT_MS) == 0,
          "exactly at the timeout is not stale");
    CHECK(s.state == UPP_STATE_RUNNING, "still running at exactly 15 s");
    CHECK(upp_session_check_heartbeat(&s, 1001 + UPP_HEARTBEAT_TIMEOUT_MS, UPP_HEARTBEAT_TIMEOUT_MS) == -1,
          "one ms past the timeout is stale");
    CHECK(s.state == UPP_STATE_CRASHED, "crashed after the timeout");
    CHECK(s.error_set && strcmp(s.error, "heartbeat timeout") == 0, "error is 'heartbeat timeout'");

    /* crashed -> recover -> start again */
    CHECK(upp_session_recover(&s, err, sizeof err) == 0, "recover: %s", err);
    CHECK(s.state == UPP_STATE_IDLE && !s.error_set && s.last_heartbeat == 0 && s.last_heartbeat_at == 0,
          "recover clears error and heartbeat");
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                     err, sizeof err) == 0, "start after recover: %s", err);
    CHECK(s.state == UPP_STATE_RUNNING, "running again");

    /* explicit crash, the illegal start, then reset */
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"crash\",\"payload\":{\"error\":\"boom\",\"exitCode\":3,"
                         "\"timestamp\":2000}}", err, sizeof err) == 0, "crash: %s", err);
    CHECK(s.state == UPP_STATE_CRASHED, "crashed");
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                     err, sizeof err) == -1, "start from crashed refused");
    EXPECT_ERR(err, "cannot start from crashed");
    CHECK(upp_session_recover(&s, err, sizeof err) == 0, "recover after crash: %s", err);
    upp_session_reset(&s);
    CHECK(s.state == UPP_STATE_IDLE && !s.error_set, "reset");

    upp_buf_free(&welcome);
}

static void test_session_boundaries(void) {
    UppSession s;
    char err[UPP_ERR_MAX];

    /* start is idempotent while running */
    running_session(&s);
    CHECK(s.state == UPP_STATE_RUNNING, "running");
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                     err, sizeof err) == 0, "second start is not an error");
    CHECK(s.state == UPP_STATE_RUNNING, "still running");
    CHECK(s.last_heartbeat == 1 && s.last_heartbeat_at == 1000, "idempotent start keeps the heartbeat");

    /* start from incompatible is refused */
    session_init(&s, "host", MANIFEST_JSON);
    s.state = UPP_STATE_INCOMPATIBLE;
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                     err, sizeof err) == -1, "start from incompatible refused");
    EXPECT_ERR(err, "cannot start from incompatible");

    /* start from stopped IS allowed */
    session_init(&s, "host", MANIFEST_JSON);
    s.state = UPP_STATE_STOPPED;
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"start\",\"payload\":{\"entry\":\"main.im\"}}",
                     err, sizeof err) == 0, "start from stopped allowed");
    CHECK(s.state == UPP_STATE_RUNNING, "running after start from stopped");

    /* stop is unconditional */
    running_session(&s);
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"stop\",\"payload\":{\"reason\":\"requested\"}}",
                     err, sizeof err) == 0, "stop: %s", err);
    CHECK(s.state == UPP_STATE_STOPPED, "stopped");
    session_init(&s, "host", MANIFEST_JSON);
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"stop\",\"payload\":{}}", err, sizeof err) == 0,
          "stop from idle");
    CHECK(s.state == UPP_STATE_STOPPED, "idle -> stopped");
    s.state = UPP_STATE_CRASHED;
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"stop\",\"payload\":{}}", err, sizeof err) == 0,
          "stop from crashed");
    CHECK(s.state == UPP_STATE_STOPPED, "crashed -> stopped");
    CHECK(upp_session_recover(&s, err, sizeof err) == 0, "recover from stopped: %s", err);
    CHECK(s.state == UPP_STATE_IDLE, "stopped -> idle");

    /* recover is only legal from crashed/stopped */
    session_init(&s, "host", MANIFEST_JSON);
    CHECK(upp_session_recover(&s, err, sizeof err) == -1, "recover from idle refused");
    EXPECT_ERR(err, "cannot recover from idle");
    running_session(&s);
    CHECK(upp_session_recover(&s, err, sizeof err) == -1, "recover from running refused");
    EXPECT_ERR(err, "cannot recover from running");
    s.state = UPP_STATE_INCOMPATIBLE;
    CHECK(upp_session_recover(&s, err, sizeof err) == -1, "recover from incompatible refused");
    EXPECT_ERR(err, "cannot recover from incompatible");

    /* crash error defaults */
    session_init(&s, "host", MANIFEST_JSON);
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"crash\",\"payload\":{}}", err, sizeof err) == 0, "crash: %s", err);
    CHECK(s.state == UPP_STATE_CRASHED && strcmp(s.error, "unknown crash") == 0,
          "missing error -> unknown crash, got %s", s.error);
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"crash\",\"payload\":{\"error\":\"\"}}", err, sizeof err) == 0,
          "empty error crash");
    CHECK(strcmp(s.error, "unknown crash") == 0, "empty error -> unknown crash, got %s", s.error);
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"crash\",\"payload\":{\"error\":\"real\"}}", err, sizeof err) == 0,
          "real error crash");
    CHECK(strcmp(s.error, "real") == 0, "payload.error wins, got %s", s.error);

    /* an unknown frame type is accepted and changes nothing */
    session_init(&s, "host", MANIFEST_JSON);
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"mystery\",\"payload\":{}}", err, sizeof err) == 0,
          "unknown type is not an error");
    CHECK(s.state == UPP_STATE_IDLE, "unknown type changes nothing");
    CHECK(upp_session_apply(&s, NULL, err, sizeof err) == -1, "missing message refused");
    EXPECT_ERR(err, "UPP message required");
}

static void test_heartbeat_ordering(void) {
    UppSession s;
    running_session(&s);
    char err[UPP_ERR_MAX];

    /* equal seq and equal timestamp are both allowed */
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":1,\"timestamp\":1000}}",
                     err, sizeof err) == 0, "equal seq/timestamp accepted: %s", err);
    CHECK(s.last_heartbeat == 1 && s.last_heartbeat_at == 1000, "unchanged");

    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":2,\"timestamp\":1000}}",
                     err, sizeof err) == 0, "higher seq, equal timestamp accepted");
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":1,\"timestamp\":1001}}",
                     err, sizeof err) == -1, "lower seq refused");
    EXPECT_ERR(err, "heartbeat sequence out of order");
    CHECK(s.last_heartbeat == 2 && s.last_heartbeat_at == 1000, "refusal did not mutate the session");

    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":3,\"timestamp\":999}}",
                     err, sizeof err) == -1, "lower timestamp refused");
    EXPECT_ERR(err, "heartbeat timestamp out of order");
    CHECK(s.last_heartbeat == 2, "rejected timestamp did not advance seq");

    /* a non-integer or missing seq is out of order, not a crash */
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"timestamp\":1000}}",
                     err, sizeof err) == -1, "missing seq refused");
    EXPECT_ERR(err, "heartbeat sequence out of order");
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":\"3\"}}",
                     err, sizeof err) == -1, "string seq refused");
    EXPECT_ERR(err, "heartbeat sequence out of order");
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":9007199254740992}}",
                     err, sizeof err) == -1, "unsafe seq refused");
    EXPECT_ERR(err, "heartbeat sequence out of order");

    /* a missing timestamp falls back to the wall clock and does NOT trip the
     * ordering check (Number.isFinite guard in the reference) */
    CHECK(apply_text(&s, "{\"upp\":1,\"type\":\"heartbeat\",\"payload\":{\"seq\":4}}", err, sizeof err) == 0,
          "missing timestamp accepted: %s", err);
    CHECK(s.last_heartbeat == 4, "seq advanced");
    CHECK(s.last_heartbeat_at >= 1000, "wall clock used for the missing timestamp");

    /* checkHeartbeat only judges while running */
    session_init(&s, "host", MANIFEST_JSON);
    s.last_heartbeat_at = 1000; /* stale by the clock, but the session never ran */
    CHECK(upp_session_is_heartbeat_stale(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 1, "stale by the clock");
    CHECK(upp_session_check_heartbeat(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 0, "idle is not judged");
    CHECK(s.state == UPP_STATE_IDLE, "idle stayed idle");
    s.state = UPP_STATE_STOPPED;
    CHECK(upp_session_check_heartbeat(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 0, "stopped is not judged");
    CHECK(s.state == UPP_STATE_STOPPED, "stopped stayed stopped");
    s.state = UPP_STATE_CRASHED;
    CHECK(upp_session_check_heartbeat(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 0, "crashed is not judged");
    s.state = UPP_STATE_INCOMPATIBLE;
    CHECK(upp_session_check_heartbeat(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 0, "incompatible is not judged");

    /* no heartbeat yet: lastHeartbeatAt == 0 is never stale */
    session_init(&s, "host", MANIFEST_JSON);
    s.state = UPP_STATE_RUNNING;
    CHECK(upp_session_is_heartbeat_stale(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 0,
          "no heartbeat yet is not stale");
    CHECK(upp_session_check_heartbeat(&s, 100000000, UPP_HEARTBEAT_TIMEOUT_MS) == 0, "not judged before the first beat");
    CHECK(s.state == UPP_STATE_RUNNING, "still running");

    /* the default timeout is 15 s */
    CHECK(UPP_HEARTBEAT_TIMEOUT_MS == 15000, "15 s timeout");
}

static void test_session_init_rules(void) {
    UppSession s;
    char err[UPP_ERR_MAX];
    VjVal *m = J(MANIFEST_JSON);
    CHECK(upp_session_init(&s, "bad", m, err, sizeof err) == -1, "bad role refused");
    EXPECT_ERR(err, "invalid role");
    CHECK(upp_session_init(&s, NULL, m, err, sizeof err) == -1, "missing role refused");
    EXPECT_ERR(err, "invalid role");
    CHECK(upp_session_init(&s, "verse", m, err, sizeof err) == 0, "verse role accepted");
    CHECK(upp_session_init(&s, "client", m, err, sizeof err) == 0, "client role accepted");
    CHECK(upp_session_init(&s, "host", NULL, err, sizeof err) == 0, "manifest-less session accepted");
    CHECK(s.state == UPP_STATE_IDLE, "starts idle");
    vj_free(m);
}

/* ------------------------------------------------------- transcript harness */

/* Replays a corpus through the engine and prints one canonical record per op.
 * tools/upp_engine_crosscheck.js generates the same corpus for the JS
 * reference and diffs the two transcripts.  The record is deliberately flat
 * and single-line so a diff points at the exact op.
 *
 * Builder ops (hello/start/stop/heartbeat/incompatible) only build a frame and
 * change no state -- `frame` carries the serialized frame.  Session ops
 * (accept/apply/recover/reset/check/snapshot) drive the state machine; the
 * corpus passes `check`'s `now`/`timeoutMs` explicitly so nothing depends on
 * the wall clock.
 */
typedef struct {
    int      ok;                 /* 1 when the op completed */
    char     error[UPP_ERR_MAX]; /* "" when ok */
    UppBuf   frame;              /* serialized frame when has_frame */
    int      has_frame;
    int      result;             /* -1 null, 0 false, 1 true */
} OpOutcome;

static const char *vstr(const VjVal *v) {
    return (v && v->type == VJ_STR && v->s) ? v->s : NULL;
}

static const VjVal *op_field(const VjVal *op, const char *key) {
    return (op && op->type == VJ_OBJ) ? vj_get(op, key) : NULL;
}

static void emit_record(UppBuf *out, int n, const char *opname, const OpOutcome *o,
                        const UppSession *s, int have_session) {
    upp_buf_puts(out, "{\"n\":");
    upp_buf_put_ll(out, n);
    upp_buf_puts(out, ",\"op\":");
    upp_json_write_string(out, opname ? opname : "");
    upp_buf_puts(out, ",\"ok\":");
    upp_buf_puts(out, o->ok ? "true" : "false");
    upp_buf_puts(out, ",\"error\":");
    if (o->ok) upp_buf_puts(out, "null");
    else upp_json_write_string(out, o->error);
    /* `frame` is the frame *text*, so it nests as a JSON string exactly like
     * JSON.stringify(frame) on the reference side. */
    upp_buf_puts(out, ",\"frame\":");
    if (o->has_frame) upp_json_write_string(out, B(&o->frame));
    else upp_buf_puts(out, "null");
    upp_buf_puts(out, ",\"result\":");
    if (o->result < 0) upp_buf_puts(out, "null");
    else upp_buf_puts(out, o->result ? "true" : "false");
    upp_buf_puts(out, ",\"state\":");
    if (!have_session) upp_buf_puts(out, "null");
    else upp_json_write_string(out, upp_state_name(s->state));
    upp_buf_puts(out, ",\"lastHeartbeat\":");
    if (!have_session) upp_buf_puts(out, "null");
    else upp_buf_put_ll(out, s->last_heartbeat);
    upp_buf_puts(out, ",\"lastHeartbeatAt\":");
    if (!have_session) upp_buf_puts(out, "null");
    else upp_buf_put_ll(out, s->last_heartbeat_at);
    upp_buf_puts(out, ",\"abi\":");
    if (!have_session || !s->has_abi) upp_buf_puts(out, "null");
    else upp_buf_put_ll(out, s->abi);
    upp_buf_puts(out, ",\"sessionError\":");
    if (!have_session || !s->error_set) upp_buf_puts(out, "null");
    else upp_json_write_string(out, s->error);
    upp_buf_puts(out, "}\n");
}

static void outcome_fail(OpOutcome *o, const char *msg) {
    o->ok = 0;
    snprintf(o->error, sizeof o->error, "%s", msg ? msg : "");
}

/* The harness mirrors the reference's argument-level TypeErrors for fields the
 * engine takes as C integers, so a corpus typo fails the diff loudly instead of
 * quietly agreeing. */
static void transcript_op(const VjVal *op, UppSession *s, OpOutcome *o) {
    const char *name = vstr(op_field(op, "op"));
    char err[UPP_ERR_MAX];
    const VjVal *v;

    if (!name) {
        outcome_fail(o, "corpus op is missing a string `op`");
        return;
    }
    if (strcmp(name, "hello") == 0) {
        if (upp_frame_hello(&o->frame, vstr(op_field(op, "role")), op_field(op, "manifest"),
                            op_field(op, "capabilities"), err, sizeof err) != 0)
            outcome_fail(o, err);
        else
            o->has_frame = 1;
        return;
    }
    if (strcmp(name, "start") == 0) {
        if (upp_frame_start(&o->frame, vstr(op_field(op, "entry")), op_field(op, "args"),
                            err, sizeof err) != 0)
            outcome_fail(o, err);
        else
            o->has_frame = 1;
        return;
    }
    if (strcmp(name, "stop") == 0) {
        /* reference: stop(reason = 'requested') -- the default applies only when
         * the argument is omitted, so an explicit `"reason":null` must still be
         * refused.  Mirroring the call site keeps the builder itself strict. */
        const VjVal *r = op_field(op, "reason");
        const char *reason = r ? vstr(r) : "requested";
        if (upp_frame_stop(&o->frame, reason, err, sizeof err) != 0)
            outcome_fail(o, err);
        else
            o->has_frame = 1;
        return;
    }
    if (strcmp(name, "heartbeat") == 0) {
        const VjVal *seq = op_field(op, "seq");
        const VjVal *at = op_field(op, "timestamp");
        if (!seq || seq->type != VJ_INT || !at || at->type != VJ_INT) {
            outcome_fail(o, "corpus heartbeat requires integer `seq` and `timestamp`");
            return;
        }
        if (upp_frame_heartbeat(&o->frame, seq->i, at->i, err, sizeof err) != 0)
            outcome_fail(o, err);
        else
            o->has_frame = 1;
        return;
    }
    if (strcmp(name, "incompatible") == 0) {
        /* reference: incompatible(required, actual = VERSION) -- same
         * omitted-vs-null distinction as `stop` above. */
        const VjVal *req = op_field(op, "required");
        const VjVal *act = op_field(op, "actual");
        if (!req || req->type != VJ_INT) { outcome_fail(o, "required protocol must be an integer"); return; }
        if (act && act->type != VJ_INT) { outcome_fail(o, "actual protocol must be an integer"); return; }
        if (upp_frame_incompatible(&o->frame, req->i, act ? act->i : UPP_VERSION, err, sizeof err) != 0)
            outcome_fail(o, err);
        else
            o->has_frame = 1;
        return;
    }
    if (strcmp(name, "accept") == 0) {
        v = op_field(op, "frame");
        if (!v) { outcome_fail(o, "accept requires `frame`"); return; }
        if (upp_session_accept_hello(s, v, &o->frame, err, sizeof err) != 0) outcome_fail(o, err);
        else o->has_frame = 1;
        return;
    }
    if (strcmp(name, "apply") == 0) {
        v = op_field(op, "frame");
        if (!v) { outcome_fail(o, "apply requires `frame`"); return; }
        if (upp_session_apply(s, v, err, sizeof err) != 0) outcome_fail(o, err);
        return;
    }
    if (strcmp(name, "recover") == 0) {
        if (upp_session_recover(s, err, sizeof err) != 0) outcome_fail(o, err);
        return;
    }
    if (strcmp(name, "reset") == 0) {
        upp_session_reset(s);
        return;
    }
    if (strcmp(name, "check") == 0) {
        const VjVal *now = op_field(op, "now");
        const VjVal *to = op_field(op, "timeoutMs");
        if (!now || now->type != VJ_INT || !to || to->type != VJ_INT) {
            outcome_fail(o, "corpus check requires integer `now` and `timeoutMs`");
            return;
        }
        o->result = upp_session_check_heartbeat(s, now->i, to->i) == 0 ? 1 : 0;
        return;
    }
    if (strcmp(name, "snapshot") == 0) return; /* the record already carries the snapshot */

    outcome_fail(o, "corpus op is not one the harness understands");
}

static int run_transcript(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "upp probe: cannot open corpus %s\n", path);
        return 1;
    }
    UppSession s;
    memset(&s, 0, sizeof s);
    int have_session = 0;
    UppBuf out;
    upp_buf_init(&out);

    char *line = NULL;
    size_t cap = 0;
    ssize_t got;
    int n = 0;
    int bad = 0;
    while ((got = getline(&line, &cap, f)) >= 0) {
        while (got > 0 && (line[got - 1] == '\n' || line[got - 1] == '\r')) line[--got] = '\0';
        if (!line[0]) continue;
        char perr[UPP_ERR_MAX];
        VjVal *op = vj_parse(line, perr, sizeof perr);
        if (!op) {
            fprintf(stderr, "upp probe: corpus line %d does not parse: %s\n", n + 1, perr);
            bad = 1;
            break;
        }
        n++;
        const char *name = vstr(op_field(op, "op"));
        OpOutcome o;
        memset(&o, 0, sizeof o);
        o.ok = 1;
        o.result = -1;
        upp_buf_init(&o.frame);
        if (name && strcmp(name, "init") == 0) {
            /* `session = new UppSession(...)` keeps the previous session when
             * the constructor throws, so only commit on success. */
            UppSession fresh;
            char err[UPP_ERR_MAX];
            if (upp_session_init(&fresh, vstr(op_field(op, "role")), op_field(op, "manifest"),
                                 err, sizeof err) != 0) {
                outcome_fail(&o, err);
            } else {
                s = fresh;
                have_session = 1;
            }
        } else if (!have_session) {
            outcome_fail(&o, "corpus used an op before init");
        } else {
            transcript_op(op, &s, &o);
        }
        if (!bad) emit_record(&out, n, name, &o, &s, have_session);
        upp_buf_free(&o.frame);
        vj_free(op);
    }
    free(line);
    fclose(f);
    if (!bad) fputs(B(&out), stdout);
    upp_buf_free(&out);
    return bad ? 1 : 0;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "--transcript") == 0) return run_transcript(argv[2]);

    test_frame_shape();
    test_manifest_validation();
    test_frame_size_limit();
    test_decoder();
    test_accept_hello();
    test_full_sequence();
    test_session_boundaries();
    test_heartbeat_ordering();
    test_session_init_rules();

    if (failures) {
        printf("upp probe: %d/%d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("upp probe: ok (%d checks)\n", checks);
    return 0;
}
