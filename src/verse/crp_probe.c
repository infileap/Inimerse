/* crp_probe.c - offline assertions for the engine-side CRP layer
 *
 * Two modes:
 *
 *   crp_probe
 *       Runs the built-in assertions (frame refusals, base64url and HMAC-SHA256
 *       known-answer tests, capability tokens, and the FIND/PORTAL/SIGNAL
 *       registry semantics from tools/crp_relay.test.js) against a frozen
 *       clock, then prints "crp_probe: N checks, 0 failures".
 *
 *   crp_probe --transcript <corpus.jsonl> --secret <hex> --now <ms>
 *       Replays a corpus of operations, one record per line on stdout, for
 *       tools/crp_engine_crosscheck.js.  The JS side replays the SAME corpus
 *       through tools/crp_reference.js / tools/crp_relay.js and the two
 *       transcripts must be byte-identical.
 *
 * No assertion lives in assert(): the release build compiles with -DNDEBUG.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "probe_compat.h"

#include "crp.h"
#include "sha256.h"

#define PROBE_SECRET "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"
/* The portal enrollment secret, deliberately different from PROBE_SECRET: the
 * proof key and the token-signing key must not be the same secret. */
#define PROBE_ENROLL_SECRET "ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"
/* 2026-01-01T00:00:00Z; the same instant the crosscheck driver freezes. */
#define PROBE_NOW    1767225600000LL

static int g_checks = 0;
static int g_failures = 0;

static void check(int cond, const char *label, const char *detail) {
    g_checks++;
    if (cond) return;
    g_failures++;
    printf("FAIL %s%s%s\n", label, detail && *detail ? ": " : "", detail ? detail : "");
}

static void check_str(const char *got, const char *want, const char *label) {
    g_checks++;
    if (got && want && strcmp(got, want) == 0) return;
    g_failures++;
    printf("FAIL %s: got <%s> want <%s>\n", label, got ? got : "(null)", want ? want : "(null)");
}

static void check_int(long long got, long long want, const char *label) {
    g_checks++;
    if (got == want) return;
    g_failures++;
    printf("FAIL %s: got %lld want %lld\n", label, got, want);
}

/* Runs a frame builder that is expected to refuse, and compares the exact text
 * with the reference's message. */
static void check_refusal(int rc, const char *err, const char *want, const char *label) {
    g_checks++;
    if (rc == 0) {
        g_failures++;
        printf("FAIL %s: expected refusal <%s>, builder succeeded\n", label, want);
        return;
    }
    if (err && strcmp(err, want) == 0) return;
    g_failures++;
    printf("FAIL %s: got error <%s> want <%s>\n", label, err ? err : "(null)", want);
}

static VjVal vj_str_v(const char *s) {
    VjVal v;
    memset(&v, 0, sizeof v);
    v.type = VJ_STR;
    v.s = (char *)(s ? s : "");
    return v;
}

static VjVal vj_int_v(long long i) {
    VjVal v;
    memset(&v, 0, sizeof v);
    v.type = VJ_INT;
    v.i = i;
    return v;
}


/* Builds a one-key object around `val` (keys/vals are not owned by the probe). */
static VjVal vj_obj1_v(const char *key, VjVal *val) {
    VjVal v;
    memset(&v, 0, sizeof v);
    v.type = VJ_OBJ;
    v.nkv = 1;
    v.keys = (char **)malloc(sizeof(char *));
    v.vals = (VjVal **)malloc(sizeof(VjVal *));
    v.keys[0] = (char *)key;
    v.vals[0] = val;
    return v;
}

static void vj_obj1_free(VjVal *v) {
    free(v->keys);
    free(v->vals);
    memset(v, 0, sizeof *v);
}

/* Builds a parsed value from JSON text; the probe owns it. */
static VjVal *parse_or_die(const char *json) {
    char err[128];
    VjVal *v = vj_parse(json, err, sizeof err);
    if (!v) {
        g_failures++;
        g_checks++;
        printf("FAIL probe corpus parse: %s\n", err);
    }
    return v;
}

/* ------------------------------------------------------------------ asserts */

static void test_base64url(void) {
    /* RFC 4648 §10 test vectors, in the URL alphabet (no padding). */
    static const char *IN[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
    static const char *OUT[] = { "", "Zg", "Zm8", "Zm9v", "Zm9vYg", "Zm9vYmE", "Zm9vYmFy" };
    for (size_t i = 0; i < sizeof IN / sizeof *IN; i++) {
        CrpBuf b;
        upp_buf_init(&b);
        crp_b64url_encode((const unsigned char *)IN[i], strlen(IN[i]), &b);
        check_str(b.data ? b.data : "", OUT[i], "base64url encode vector");
        unsigned char *raw = NULL;
        size_t rawlen = 0;
        int rc = crp_b64url_decode(OUT[i], strlen(OUT[i]), &raw, &rawlen);
        check(rc == 0, "base64url decode vector accepted", OUT[i]);
        if (rc == 0) {
            check(rawlen == strlen(IN[i]) && (rawlen == 0 || memcmp(raw, IN[i], rawlen) == 0),
                  "base64url decode round-trip", OUT[i]);
        }
        free(raw);
        upp_buf_free(&b);
    }
    unsigned char *raw = NULL;
    size_t rawlen = 0;
    check(crp_b64url_decode("a", 1, &raw, &rawlen) != 0, "base64url rejects 4k+1 length", NULL);
    check(crp_b64url_decode("****", 4, &raw, &rawlen) != 0, "base64url rejects bad alphabet", NULL);
    check(crp_b64url_decode("Zg==", 4, &raw, &rawlen) != 0, "base64url rejects padding", NULL);
    check(crp_b64url_decode("Zg", 2, &raw, &rawlen) == 0 && rawlen == 1 && raw[0] == 'f',
          "base64url decodes 2 chars to 1 byte", NULL);
    free(raw);
    /* every 2-byte input round-trips */
    int all = 1;
    for (int a = 0; a < 256 && all; a++) {
        for (int b = 0; b < 256; b++) {
            unsigned char in[2] = { (unsigned char)a, (unsigned char)b };
            CrpBuf e;
            upp_buf_init(&e);
            crp_b64url_encode(in, 2, &e);
            unsigned char *d = NULL;
            size_t dl = 0;
            if (crp_b64url_decode(e.data, e.len, &d, &dl) != 0 || dl != 2 || memcmp(d, in, 2) != 0)
                all = 0;
            free(d);
            upp_buf_free(&e);
            if (!all) break;
        }
    }
    check(all, "base64url round-trips all 65536 two-byte inputs", NULL);
}

static void hex_of(const unsigned char *in, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", in[i]);
}

static void test_hmac(void) {
    /* RFC 4231 test cases 1, 2, 6 and 7 (SHA-256). */
    unsigned char key1[20];
    memset(key1, 0x0b, sizeof key1);
    unsigned char mac[32];
    char hex[65];
    crp_hmac_sha256(key1, sizeof key1, "Hi There", 8, mac);
    hex_of(mac, 32, hex);
    check_str(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "RFC4231 case 1");

    crp_hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, mac);
    hex_of(mac, 32, hex);
    check_str(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "RFC4231 case 2");

    unsigned char key6[131];
    memset(key6, 0xaa, sizeof key6);
    crp_hmac_sha256(key6, sizeof key6, "Test Using Larger Than Block-Size Key - Hash Key First", 54, mac);
    hex_of(mac, 32, hex);
    check_str(hex, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "RFC4231 case 6");

    const char *msg7 = "This is a test using a larger than block-size key and a larger than "
                       "block-size data. The key needs to be hashed before being used by the "
                       "HMAC algorithm.";
    crp_hmac_sha256(key6, sizeof key6, msg7, strlen(msg7), mac);
    hex_of(mac, 32, hex);
    check_str(hex, "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2", "RFC4231 case 7");

    /* A key of exactly the 64-byte block size must not be hashed. */
    char key64[65];
    memset(key64, 'k', 64);
    key64[64] = '\0';
    unsigned char a[32], b[32];
    crp_hmac_sha256(key64, 64, "x", 1, a);
    Sha256Ctx c;
    unsigned char inner[32], block[64], pad[64], out[32];
    memcpy(block, key64, 64);
    for (int i = 0; i < 64; i++) pad[i] = (unsigned char)(block[i] ^ 0x36);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, "x", 1);
    sha256_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = (unsigned char)(block[i] ^ 0x5c);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
    crp_hmac_sha256(key64, 64, "x", 1, b);
    check(memcmp(a, b, 32) == 0 && memcmp(a, out, 32) == 0, "64-byte key is not hashed first", NULL);
}

static void test_tokens(void) {
    CrpBuf tok;
    upp_buf_init(&tok);
    char err[CRP_ERR_MAX];
    int rc = crp_token_make_str(PROBE_SECRET, "demo", "peer-1", NULL, 0,
                                PROBE_NOW + 300000, &tok, err, sizeof err);
    check(rc == 0, "token minted", err);
    /* Pinned against Node:
     *   body = Buffer.from(JSON.stringify({verse,peer,capabilities,exp}))
     *   token = body.toString('base64url') + '.' +
     *           crypto.createHmac('sha256', secret).update(body.toString('base64url'))
     *                 .digest('base64url') */
    check_str(tok.data,
              "eyJ2ZXJzZSI6ImRlbW8iLCJwZWVyIjoicGVlci0xIiwiY2FwYWJpbGl0aWVzIjpbInNpZ25hbCJdLCJleHAiOjE3NjcyMjU5MDAwMDB9."
              "dCdjTpv1uLtSe_ed6Px19ikVpL3Q_IVbNx_tZ_SIKVE",
              "token string KAT (Node crypto)");
    /* The body is 78 JSON bytes -> 104 base64url chars, plus '.' plus 43. */
    check_int((long long)strlen(tok.data), 148, "token length");
    check(crp_token_check_str(PROBE_SECRET, tok.data, "demo", "peer-1", "signal",
                              PROBE_NOW + 299999) == 1, "token accepted before exp", NULL);
    check(crp_token_check_str(PROBE_SECRET, tok.data, "demo", "peer-1", "signal",
                              PROBE_NOW + 300000) == 0, "token refused at exp (strict >)", NULL);
    check(crp_token_check_str(PROBE_SECRET, tok.data, "other", "peer-1", "signal",
                              PROBE_NOW) == 0, "token refused for another verse", NULL);
    check(crp_token_check_str(PROBE_SECRET, tok.data, "demo", "peer-2", "signal",
                              PROBE_NOW) == 0, "token refused for another peer", NULL);
    check(crp_token_check_str(PROBE_SECRET, tok.data, "demo", "peer-1", "events",
                              PROBE_NOW) == 0, "token refused for another capability", NULL);
    check(crp_token_check_str("other-secret", tok.data, "demo", "peer-1", "signal",
                              PROBE_NOW) == 0, "token refused under another secret", NULL);
    /* tampered signature (last byte flipped) */
    char *tampered = strdup(tok.data);
    size_t tl = strlen(tampered);
    tampered[tl - 1] = tampered[tl - 1] == 'A' ? 'B' : 'A';
    check(crp_token_check_str(PROBE_SECRET, tampered, "demo", "peer-1", "signal", PROBE_NOW) == 0,
          "tampered signature refused", NULL);
    free(tampered);
    /* malformed shapes */
    check(crp_token_check_str(PROBE_SECRET, "nodot", "demo", "peer-1", "signal", PROBE_NOW) == 0,
          "token without a dot refused", NULL);
    check(crp_token_check_str(PROBE_SECRET, ".sig", "demo", "peer-1", "signal", PROBE_NOW) == 0,
          "empty body refused", NULL);
    check(crp_token_check_str(PROBE_SECRET, "body.", "demo", "peer-1", "signal", PROBE_NOW) == 0,
          "empty signature refused", NULL);
    check(crp_token_check_str(PROBE_SECRET, "", "demo", "peer-1", "signal", PROBE_NOW) == 0,
          "empty token refused", NULL);
    check(crp_token_check_str(PROBE_SECRET, NULL, "demo", "peer-1", "signal", PROBE_NOW) == 0,
          "NULL token refused", NULL);
    /* valid signature over a body that is not JSON */
    {
        CrpBuf b;
        upp_buf_init(&b);
        crp_b64url_encode((const unsigned char *)"not json", 8, &b);
        unsigned char mac[32];
        crp_hmac_sha256(PROBE_SECRET, strlen(PROBE_SECRET), b.data, b.len, mac);
        CrpBuf sig;
        upp_buf_init(&sig);
        crp_b64url_encode(mac, 32, &sig);
        CrpBuf full;
        upp_buf_init(&full);
        upp_buf_putn(&full, b.data, b.len);
        upp_buf_putc(&full, '.');
        upp_buf_putn(&full, sig.data, sig.len);
        check(crp_token_check_str(PROBE_SECRET, full.data, "demo", "peer-1", "signal", PROBE_NOW) == 0,
              "signed non-JSON body refused", NULL);
        /* signed JSON array body */
        upp_buf_free(&b);
        upp_buf_init(&b);
        crp_b64url_encode((const unsigned char *)"[1,2]", 5, &b);
        crp_hmac_sha256(PROBE_SECRET, strlen(PROBE_SECRET), b.data, b.len, mac);
        upp_buf_free(&sig);
        upp_buf_init(&sig);
        crp_b64url_encode(mac, 32, &sig);
        upp_buf_free(&full);
        upp_buf_init(&full);
        upp_buf_putn(&full, b.data, b.len);
        upp_buf_putc(&full, '.');
        upp_buf_putn(&full, sig.data, sig.len);
        check(crp_token_check_str(PROBE_SECRET, full.data, "demo", "peer-1", "signal", PROBE_NOW) == 0,
              "signed array body refused", NULL);
        upp_buf_free(&b);
        upp_buf_free(&sig);
        upp_buf_free(&full);
    }
    /* a string exp coerces (JS ToNumber) and a non-numeric one is NaN */
    {
        VjVal caps = { .type = VJ_ARR };
        VjVal expv = vj_str_v("9999999999999");
        VjVal verse = vj_str_v("demo"), peer = vj_str_v("p");
        CrpBuf t2;
        upp_buf_init(&t2);
        check(crp_token_make(PROBE_SECRET, &verse, &peer, &caps, 0, &t2, err, sizeof err) == 0,
              "token with empty capability list minted", err);
        /* mint with a string exp by hand: rebuild the body with exp as a string */
        CrpBuf body;
        upp_buf_init(&body);
        upp_buf_puts(&body, "{\"verse\":\"demo\",\"peer\":\"p\",\"capabilities\":[\"signal\"],\"exp\":\"9999999999999\"}");
        CrpBuf b64;
        upp_buf_init(&b64);
        crp_b64url_encode((const unsigned char *)body.data, body.len, &b64);
        unsigned char mac[32];
        crp_hmac_sha256(PROBE_SECRET, strlen(PROBE_SECRET), b64.data, b64.len, mac);
        CrpBuf sig;
        upp_buf_init(&sig);
        crp_b64url_encode(mac, 32, &sig);
        CrpBuf strtok;
        upp_buf_init(&strtok);
        upp_buf_putn(&strtok, b64.data, b64.len);
        upp_buf_putc(&strtok, '.');
        upp_buf_putn(&strtok, sig.data, sig.len);
        check(crp_token_check_str(PROBE_SECRET, strtok.data, "demo", "p", "signal", PROBE_NOW) == 1,
              "string exp is coerced numerically", NULL);
        upp_buf_free(&body);
        upp_buf_free(&b64);
        upp_buf_free(&sig);
        upp_buf_free(&strtok);
        upp_buf_free(&t2);
        (void)expv;
    }
    /* strict `===` on verse/peer: a number never equals a string */
    {
        VjVal num = vj_int_v(5);
        check(crp_token_check_str(PROBE_SECRET, tok.data, "demo", "peer-1", "signal", PROBE_NOW) == 1,
              "string verse matches", NULL);
        check(crp_token_check(PROBE_SECRET, tok.data, &num, &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              "signal", PROBE_NOW) == 0,
              "numeric verse never matches a string verse", NULL);
    }
    /* body introspection */
    {
        VjVal *body = crp_token_body(tok.data, err, sizeof err);
        check(body != NULL, "token body parses", err);
        if (body) {
            check_str(vj_str(vj_get(body, "verse"), NULL), "demo", "token body verse");
            check_str(vj_str(vj_get(body, "peer"), NULL), "peer-1", "token body peer");
            check_int(vj_int(vj_get(body, "exp"), -1), PROBE_NOW + 300000, "token body exp");
            const VjVal *caps = vj_get(body, "capabilities");
            check(caps && caps->type == VJ_ARR && caps->n == 1 &&
                      strcmp(vj_str(caps->items[0], ""), "signal") == 0,
                  "token body capabilities", NULL);
            vj_free(body);
        }
        check(crp_token_body("nope", err, sizeof err) == NULL, "body of a dotless token refused", NULL);
    }
    upp_buf_free(&tok);
}

static void test_frames(void) {
    CrpBuf out;
    char err[CRP_ERR_MAX];

    /* exact wire text, key order crp/type/payload */
    upp_buf_init(&out);
    check(crp_frame_find(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" }, &(VjVal){ .type = VJ_INT, .i = 10 },
                         NULL, "", err, sizeof err) == 0, "find frame built", err);
    check_str(out.data, "{\"crp\":1,\"type\":\"FIND\",\"payload\":{\"query\":\"demo\",\"limit\":10,\"cursor\":null}}",
              "find frame text");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check(crp_frame_find(&out, NULL, NULL, NULL, "id1", err, sizeof err) == 0, "find defaults built", err);
    check_str(out.data, "{\"crp\":1,\"type\":\"FIND\",\"payload\":{\"query\":\"\",\"limit\":50,\"cursor\":null},\"id\":\"id1\"}",
              "find defaults + id appended after payload");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check(crp_frame_find(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"q" }, &(VjVal){ .type = VJ_NULL },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"cur" }, "", err, sizeof err) == 0,
          "find with null limit and string cursor built", err);
    check_str(out.data, "{\"crp\":1,\"type\":\"FIND\",\"payload\":{\"query\":\"q\",\"limit\":50,\"cursor\":\"cur\"}}",
              "find null limit falls back to 50");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check(crp_frame_portal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                           &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                           &(VjVal){ .type = VJ_STR, .s = (char *)"tok" }, &(VjVal){ .type = VJ_INT, .i = 7 },
                           "", err, sizeof err) == 0, "portal frame built", err);
    check_str(out.data,
              "{\"crp\":1,\"type\":\"PORTAL\",\"payload\":{\"verse\":\"demo\",\"peer\":\"peer-1\",\"token\":\"tok\",\"expires\":7}}",
              "portal frame text");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check(crp_frame_signal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                           &(VjVal){ .type = VJ_STR, .s = (char *)"join" },
                           &(VjVal){ .type = VJ_OBJ }, &(VjVal){ .type = VJ_INT, .i = 42 },
                           "", err, sizeof err) == 0, "signal with object data built", err);
    check_str(out.data,
              "{\"crp\":1,\"type\":\"SIGNAL\",\"payload\":{\"verse\":\"demo\",\"event\":\"join\",\"data\":{},\"timestamp\":42}}",
              "signal frame text");
    upp_buf_free(&out);

    /* data must be a real object: null, arrays and scalars are all refused */
    upp_buf_init(&out);
    check_refusal(crp_frame_signal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"e" },
                                   &(VjVal){ .type = VJ_INT, .i = 0 }, NULL, "", err, sizeof err), err,
                  "SIGNAL data must be an object", "signal scalar data");
    upp_buf_free(&out);

    /* refusals, verbatim */
    upp_buf_init(&out);
    check_refusal(crp_frame(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"NOPE" }, NULL, "", err, sizeof err), err,
                  "unsupported CRP type: NOPE", "frame unknown type");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame(&out, NULL, NULL, "", err, sizeof err), err,
                  "unsupported CRP type: undefined", "frame undefined type");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame(&out, &(VjVal){ .type = VJ_INT, .i = 5 }, NULL, "", err, sizeof err), err,
                  "unsupported CRP type: 5", "frame numeric type");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"FIND" }, &(VjVal){ .type = VJ_ARR },
                            "", err, sizeof err), err,
                  "payload must be an object", "frame array payload");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"FIND" }, &(VjVal){ .type = VJ_NULL },
                            "", err, sizeof err), err,
                  "payload must be an object", "frame null payload");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check_refusal(crp_frame_find(&out, &(VjVal){ .type = VJ_INT, .i = 1 }, NULL, NULL, "", err, sizeof err), err,
                  "FIND query must be a string", "find numeric query");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_find(&out, NULL, &(VjVal){ .type = VJ_INT, .i = 0 }, NULL, "", err, sizeof err), err,
                  "FIND limit must be 1..1000", "find limit 0");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_find(&out, NULL, &(VjVal){ .type = VJ_INT, .i = 1001 }, NULL, "", err, sizeof err), err,
                  "FIND limit must be 1..1000", "find limit 1001");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_find(&out, NULL, &(VjVal){ .type = VJ_BOOL, .b = 1 }, NULL, "", err, sizeof err), err,
                  "FIND limit must be 1..1000", "find boolean limit");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_find(&out, NULL, NULL, &(VjVal){ .type = VJ_INT, .i = 5 }, "", err, sizeof err), err,
                  "FIND cursor must be a string or null", "find numeric cursor");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check_refusal(crp_frame_portal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"" },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"p" }, NULL, NULL, "", err, sizeof err), err,
                  "PORTAL verse is required", "portal blank verse");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_portal(&out, NULL, &(VjVal){ .type = VJ_STR, .s = (char *)"p" }, NULL, NULL, "", err, sizeof err),
                  err, "PORTAL verse is required", "portal missing verse");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_portal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"   " },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"p" }, NULL, NULL, "", err, sizeof err), err,
                  "PORTAL verse is required", "portal whitespace verse");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_portal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"" }, NULL, NULL, "", err, sizeof err), err,
                  "PORTAL peer is required", "portal blank peer");
    upp_buf_free(&out);

    upp_buf_init(&out);
    check_refusal(crp_frame_signal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"" }, NULL, NULL, "", err, sizeof err), err,
                  "SIGNAL event is required", "signal blank event");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_signal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"e" }, &(VjVal){ .type = VJ_NULL },
                                   NULL, "", err, sizeof err), err,
                  "SIGNAL data must be an object", "signal null data");
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_frame_signal(&out, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                                   &(VjVal){ .type = VJ_STR, .s = (char *)"e" }, &(VjVal){ .type = VJ_ARR },
                                   NULL, "", err, sizeof err), err,
                  "SIGNAL data must be an object", "signal array data");
    upp_buf_free(&out);

    /* encode / decode */
    const char *text = "{\"crp\":1,\"type\":\"SIGNAL\",\"payload\":{\"verse\":\"v\",\"event\":\"e\","
                       "\"data\":{\"user\":\"a\"},\"timestamp\":7},\"id\":\"x\"}";
    VjVal *msg = parse_or_die(text);
    upp_buf_init(&out);
    check(crp_encode(&out, msg, err, sizeof err) == 0, "encode accepts a valid frame", err);
    check(out.len == strlen(text) + 1 && out.data[out.len - 1] == '\n' && memcmp(out.data, text, strlen(text)) == 0,
          "encode appends exactly one newline", NULL);
    upp_buf_free(&out);
    upp_buf_init(&out);
    check_refusal(crp_encode(&out, &(VjVal){ .type = VJ_ARR }, err, sizeof err), err,
                  "message must be an object", "encode array");
    upp_buf_free(&out);
    upp_buf_init(&out);
    {
        VjVal *badmsg = parse_or_die("{\"crp\":2,\"type\":\"FIND\"}");
        check_refusal(crp_encode(&out, badmsg, err, sizeof err), err,
                      "invalid CRP frame", "encode wrong version");
        vj_free(badmsg);
    }
    upp_buf_free(&out);
    upp_buf_init(&out);
    {
        VjVal *badmsg = parse_or_die("{\"crp\":1,\"type\":\"OTHER\"}");
        check_refusal(crp_encode(&out, badmsg, err, sizeof err), err,
                      "invalid CRP frame", "encode unknown type");
        vj_free(badmsg);
    }
    upp_buf_free(&out);
    upp_buf_init(&out);
    {
        VjVal *badmsg = parse_or_die("{\"crp\":1,\"type\":5}");
        check_refusal(crp_encode(&out, badmsg, err, sizeof err), err,
                      "invalid CRP frame", "encode numeric type");
        vj_free(badmsg);
    }
    upp_buf_free(&out);

    VjVal *back = crp_decode_line(text, (size_t)-1, err, sizeof err);
    check(back != NULL, "decode accepts a valid frame", err);
    if (back) {
        upp_buf_init(&out);
        check(crp_encode(&out, back, err, sizeof err) == 0, "re-encode of a decoded frame", err);
        check(out.len == strlen(text) + 1 && memcmp(out.data, text, strlen(text)) == 0,
              "decode -> encode is byte-preserving", NULL);
        upp_buf_free(&out);
        vj_free(back);
    }
    check(crp_decode_line("[]", 2, err, sizeof err) == NULL && strcmp(err, "message must be an object") == 0,
          "decode of an array is refused", err);
    check(crp_decode_line("{\"crp\":1,\"type\":\"NOPE\"}", (size_t)-1, err, sizeof err) == NULL &&
              strcmp(err, "invalid CRP frame") == 0,
          "decode of an unknown type is refused", err);
    check(crp_decode_line("not json", (size_t)-1, err, sizeof err) == NULL,
          "decode of non-JSON is refused (json_min message)", err);
    /* the 1 MiB cap is measured in bytes, on both sides of the boundary */
    {
        size_t n = CRP_MAX_FRAME_BYTES - 200;
        char *big = (char *)malloc(n + 1);
        memset(big, 'x', n);
        big[n] = '\0';
        VjVal sv = { .type = VJ_STR, .s = big };
        VjVal dobj = vj_obj1_v("blob", &sv);
        VjVal ts = vj_int_v(0);
        CrpBuf frame;
        upp_buf_init(&frame);
        check(crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                               &(VjVal){ .type = VJ_STR, .s = (char *)"e" }, &dobj, &ts, "", err, sizeof err) == 0,
              "1 MiB-adjacent frame built", err);
        VjVal *parsed = crp_decode_line(frame.data, (size_t)-1, err, sizeof err);
        upp_buf_init(&out);
        check(parsed && crp_encode(&out, parsed, err, sizeof err) == 0,
              "frame just under 1 MiB encodes", err);
        upp_buf_free(&out);
        vj_free(parsed);
        upp_buf_free(&frame);
        upp_buf_init(&out);
        size_t m = CRP_MAX_FRAME_BYTES + 1;
        char *huge = (char *)malloc(m + 1);
        memset(huge, 'x', m);
        huge[m] = '\0';
        VjVal hv = { .type = VJ_STR, .s = huge };
        VjVal hobj = vj_obj1_v("blob", &hv);
        CrpBuf hframe;
        upp_buf_init(&hframe);
        check(crp_frame_signal(&hframe, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                               &(VjVal){ .type = VJ_STR, .s = (char *)"e" }, &hobj, &ts, "", err, sizeof err) == 0,
              "oversized frame built", err);
        VjVal *hparsed = crp_decode_line(hframe.data, (size_t)-1, err, sizeof err);
        check(hparsed == NULL && strcmp(err, "CRP frame exceeds 1 MiB") == 0,
              "decode over the 1 MiB cap refused", err);
        upp_buf_free(&hframe);
        /* The oversized message object is built by hand: decode already refused
         * the wire form, and encode must refuse on its own size check. */
        CrpBuf htext;
        upp_buf_init(&htext);
        upp_buf_puts(&htext, "{\"crp\":1,\"type\":\"SIGNAL\",\"payload\":{\"blob\":");
        upp_json_write_string(&htext, huge);
        upp_buf_puts(&htext, "}}");
        VjVal *hmsg = parse_or_die(htext.data);
        upp_buf_init(&out);
        check_refusal(crp_encode(&out, hmsg, err, sizeof err), err,
                      "CRP frame exceeds 1 MiB", "encode over the 1 MiB cap");
        vj_free(hmsg);
        upp_buf_free(&htext);
        upp_buf_free(&out);
        vj_obj1_free(&hobj);
        free(huge);
        vj_obj1_free(&dobj);
        free(big);
    }
    vj_free(msg);
}

/* ---------------------------------------------------------------- registry */

static char *body_of(CrpResult *r) { return r->body.data ? r->body.data : (char *)""; }

/* Fill `out` with the enrollment proof for a (verse, peer) pair -- the same
 * string tools/crp_relay.js computes as enrollProof(). */
static void proof_into(char *out, size_t cap, const char *secret,
                       const VjVal *verse, const VjVal *peer) {
    CrpBuf b;
    upp_buf_init(&b);
    int rc = crp_enroll_proof(secret, verse, peer, &b);
    snprintf(out, cap, "%s", rc == 0 && b.data ? b.data : "");
    upp_buf_free(&b);
}

static void test_registry(void) {
    CrpRegistryConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.secret = PROBE_SECRET;
    cfg.enroll_secret = PROBE_ENROLL_SECRET;
    cfg.now_ms = PROBE_NOW;
    CrpRegistry *r = crp_registry_new(&cfg);
    check(r != NULL, "registry created", NULL);
    if (!r) return;

    /* register */
    VjVal *reg = parse_or_die("{\"id\":\"demo\",\"name\":\"Demo\",\"endpoint\":\"local\"}");
    CrpResult res = crp_registry_register(r, reg);
    check_int(res.status, 200, "register status");
    check_str(body_of(&res), "{\"ok\":true}", "register body");
    crp_result_free(&res);
    VjVal *no_id = parse_or_die("{\"name\":\"x\",\"endpoint\":\"e\"}");
    CrpResult bad = crp_registry_register(r, no_id);
    vj_free(no_id);
    check_int(bad.status, 400, "register without id");
    check_str(body_of(&bad), "{\"error\":\"id and endpoint are required\"}", "register without id body");
    crp_result_free(&bad);

    /* find: `updated` is stripped, key order preserved */
    res = crp_registry_find(r, "");
    check_int(res.status, 200, "find status");
    check_str(body_of(&res), "{\"items\":[{\"id\":\"demo\",\"name\":\"Demo\",\"endpoint\":\"local\"}]}",
              "find strips updated");
    crp_result_free(&res);
    res = crp_registry_find(r, "dem");
    check_str(body_of(&res), "{\"items\":[{\"id\":\"demo\",\"name\":\"Demo\",\"endpoint\":\"local\"}]}",
              "find matches a substring of id");
    crp_result_free(&res);
    res = crp_registry_find(r, "dEmO");
    check_str(body_of(&res), "{\"items\":[{\"id\":\"demo\",\"name\":\"Demo\",\"endpoint\":\"local\"}]}",
              "find matches name case-insensitively");
    crp_result_free(&res);
    res = crp_registry_find(r, "zzz");
    check_str(body_of(&res), "{\"items\":[]}", "find with no match");
    crp_result_free(&res);

    /* register a second verse keeps insertion order; re-register keeps position */
    VjVal *reg2 = parse_or_die("{\"id\":\"second\",\"name\":\"Second\",\"endpoint\":\"e2\"}");
    res = crp_registry_register(r, reg2);
    crp_result_free(&res);
    VjVal *reg3 = parse_or_die("{\"id\":\"demo\",\"name\":\"Demo2\",\"endpoint\":\"local2\"}");
    res = crp_registry_register(r, reg3);
    crp_result_free(&res);
    res = crp_registry_find(r, "");
    check_str(body_of(&res),
              "{\"items\":[{\"id\":\"demo\",\"name\":\"Demo2\",\"endpoint\":\"local2\"},"
              "{\"id\":\"second\",\"name\":\"Second\",\"endpoint\":\"e2\"}]}",
              "re-register keeps insertion position");
    crp_result_free(&res);

    /* portal: minting requires the caller to prove it may open a portal for
     * (verse, peer), and the proof is checked before the registry is touched,
     * so an unproven caller cannot even learn which verses exist. */
    char auth_demo[256], auth_nope[256], auth_no_peer[256];
    VjVal V_DEMO = { .type = VJ_STR, .s = (char *)"demo" };
    VjVal V_NOPE = { .type = VJ_STR, .s = (char *)"nope" };
    VjVal V_PEER1 = { .type = VJ_STR, .s = (char *)"peer-1" };
    VjVal V_WRONG = { .type = VJ_STR, .s = (char *)"not-a-proof" };
    proof_into(auth_demo, sizeof auth_demo, PROBE_ENROLL_SECRET, &V_DEMO, &V_PEER1);
    proof_into(auth_nope, sizeof auth_nope, PROBE_ENROLL_SECRET, &V_NOPE, &V_PEER1);
    proof_into(auth_no_peer, sizeof auth_no_peer, PROBE_ENROLL_SECRET, &V_DEMO, NULL);
    VjVal A_DEMO = { .type = VJ_STR, .s = auth_demo };
    VjVal A_NOPE = { .type = VJ_STR, .s = auth_nope };
    VjVal A_NO_PEER = { .type = VJ_STR, .s = auth_no_peer };

    int sessions_before = crp_registry_session_count(r);
    res = crp_registry_portal(r, &V_DEMO, &V_PEER1, NULL);
    check_int(res.status, 403, "portal without auth refused");
    check_str(body_of(&res), "{\"error\":\"invalid enrollment proof\"}", "portal without auth body");
    crp_result_free(&res);
    res = crp_registry_portal(r, &V_DEMO, &V_PEER1, &V_WRONG);
    check_int(res.status, 403, "portal with wrong auth refused");
    check_str(body_of(&res), "{\"error\":\"invalid enrollment proof\"}", "portal with wrong auth body");
    crp_result_free(&res);
    res = crp_registry_portal(r, &V_DEMO, &V_PEER1, &A_NOPE);
    check_int(res.status, 403, "portal with another pair's auth refused");
    crp_result_free(&res);
    /* the refusals must have had NO side effect: no session, no lease */
    check_int(crp_registry_session_count(r), sessions_before, "refused portals created no session");
    check_int(crp_registry_session_count(r), 0, "refused portals left the session count at zero");

    /* FAIL-CLOSED default: a registry with no enrollment secret never mints,
     * even for a registered verse and a well-formed proof. */
    {
        CrpRegistryConfig nocfg;
        memset(&nocfg, 0, sizeof nocfg);
        nocfg.secret = PROBE_SECRET;
        nocfg.now_ms = PROBE_NOW;
        CrpRegistry *rn = crp_registry_new(&nocfg);
        check(rn != NULL, "registry created without an enrollment secret", NULL);
        CrpResult nr = crp_registry_portal(rn, &V_DEMO, &V_PEER1, &A_DEMO);
        check_int(nr.status, 403, "portal fail-closed without an enrollment secret");
        check_str(body_of(&nr), "{\"error\":\"portal enrollment is not configured\"}",
                  "portal fail-closed body");
        crp_result_free(&nr);
        check_int(crp_registry_session_count(rn), 0, "fail-closed portal created no session");
        crp_registry_free(rn);
    }

    res = crp_registry_portal(r, &V_NOPE, &V_PEER1, &A_NOPE);
    check_int(res.status, 404, "portal unknown verse");
    check_str(body_of(&res), "{\"error\":\"verse not found\"}", "portal unknown verse body");
    crp_result_free(&res);
    res = crp_registry_portal(r, &V_DEMO, NULL, &A_NO_PEER);
    check_int(res.status, 404, "portal without peer");
    check_int(crp_registry_session_count(r), 0, "portal without peer created no session");
    crp_result_free(&res);
    res = crp_registry_portal(r, &V_DEMO, &V_PEER1, &A_DEMO);
    check_int(res.status, 200, "portal status");
    VjVal *portal_body = parse_or_die(body_of(&res));
    check(portal_body != NULL, "portal body parses", NULL);
    const char *token = portal_body ? vj_str(vj_get(portal_body, "token"), "") : "";
    check_int(portal_body ? vj_int(vj_get(portal_body, "expires"), 0) : 0, PROBE_NOW + 300000,
              "portal expires = now + tokenTtlMs");
    check(crp_token_check_str(PROBE_SECRET, token, "demo", "peer-1", "signal", PROBE_NOW) == 1,
          "portal token verifies", NULL);
    /* the portal bound the session onto the platform session layer */
    check_int(crp_registry_session_count(r), 1, "portal created one session");
    {
        CrpBuf st;
        upp_buf_init(&st);
        crp_registry_status_json(r, &st);
        check(strstr(st.data, "\"state\":\"running\"") != NULL, "session state is running after portal", st.data);
        check(strstr(st.data, "\"leaseExpired\":false") != NULL, "lease is live after portal", st.data);
        check(strstr(st.data, "\"leaseId\":\"") != NULL, "lease id assigned", st.data);
        upp_buf_free(&st);
    }
    crp_result_free(&res);

    /* signal: server-assigned and explicit sequences */
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" },
                              &(VjVal){ .type = VJ_OBJ }, &(VjVal){ .type = VJ_NULL },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_int(res.status, 202, "signal without token status");
    check_str(body_of(&res), "{\"accepted\":true,\"verse\":\"demo\",\"event\":\"join\",\"seq\":1}",
              "signal seq 1");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL, NULL,
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_str(body_of(&res), "{\"accepted\":true,\"verse\":\"demo\",\"event\":\"join\",\"seq\":2}",
              "signal auto-increments");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" },
                              &(VjVal){ .type = VJ_OBJ }, &(VjVal){ .type = VJ_NULL },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, &(VjVal){ .type = VJ_INT, .i = 5 });
    check_str(body_of(&res), "{\"accepted\":true,\"verse\":\"demo\",\"event\":\"join\",\"seq\":5}",
              "signal honours an explicit seq");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL, NULL,
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_str(body_of(&res), "{\"accepted\":true,\"verse\":\"demo\",\"event\":\"join\",\"seq\":6}",
              "signal continues from an explicit seq");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" }, NULL, NULL, NULL,
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_int(res.status, 400, "signal without event");
    check_str(body_of(&res), "{\"error\":\"verse and event are required\"}", "signal without event body");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"nope" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL, NULL, NULL, NULL);
    check_int(res.status, 404, "signal unknown verse");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL,
                              &(VjVal){ .type = VJ_STR, .s = (char *)"garbage" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_int(res.status, 403, "signal with a bad token");
    check_str(body_of(&res), "{\"error\":\"invalid capability token\"}", "signal bad token body");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL,
                              &(VjVal){ .type = VJ_STR, .s = (char *)token },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_int(res.status, 202, "signal with a valid token");
    crp_result_free(&res);

    /* resume: after the signals above the stored sequence is 7 and the event
     * log holds seq 1,2,5,6,7 -- refusals never reach the log. */
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL, NULL, NULL);
    check_int(res.status, 400, "resume without token");
    check_str(body_of(&res), "{\"error\":\"verse, peer and token are required\"}", "resume without token body");
    crp_result_free(&res);
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"garbage" }, NULL, NULL);
    check_int(res.status, 403, "resume with a bad token");
    check_str(body_of(&res), "{\"error\":\"invalid capability token\"}", "resume bad token body");
    crp_result_free(&res);
    /* seq is absent, so it defaults to 0 while prev is 7: out of order. */
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, NULL, NULL);
    check_int(res.status, 409, "resume with the default seq is out of order");
    check_str(body_of(&res), "{\"error\":\"session sequence out of order\",\"lastSeq\":7}",
              "resume out of order body");
    crp_result_free(&res);
    /* replay:true with seq 0 replays every recorded event */
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, NULL,
                              &(VjVal){ .type = VJ_BOOL, .b = 1 });
    check_int(res.status, 200, "resume with replay:true accepts the default seq");
    check(strstr(body_of(&res), "\"lastSeq\":7") != NULL, "resume reports lastSeq", body_of(&res));
    {
        VjVal *rb = parse_or_die(body_of(&res));
        const VjVal *replay = rb ? vj_get(rb, "replay") : NULL;
        check(replay && replay->type == VJ_ARR && replay->n == 5, "resume seq 0 replays every event", NULL);
        if (replay && replay->n) {
            check_int(vj_int(vj_get(replay->items[0], "seq"), -1), 1, "first replayed seq");
            check_str(vj_str(vj_get(replay->items[0], "event"), NULL), "join", "replayed event name");
            check(vj_get(replay->items[0], "timestamp") == NULL, "replayed event carries no timestamp", NULL);
        }
        vj_free(rb);
    }
    crp_result_free(&res);
    /* replay:true with seq 5 replays only the newer events and keeps lastSeq */
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, &(VjVal){ .type = VJ_INT, .i = 5 },
                              &(VjVal){ .type = VJ_BOOL, .b = 1 });
    check_int(res.status, 200, "resume seq 5 with replay:true");
    {
        VjVal *rb = parse_or_die(body_of(&res));
        const VjVal *replay = rb ? vj_get(rb, "replay") : NULL;
        check(replay && replay->type == VJ_ARR && replay->n == 2, "resume seq 5 replays only newer events", NULL);
        if (replay && replay->n) check_int(vj_int(vj_get(replay->items[0], "seq"), -1), 6, "replay starts after seq");
        check_int(rb ? vj_int(vj_get(rb, "lastSeq"), -1) : -1, 7, "resume keeps the highest seq");
        vj_free(rb);
    }
    crp_result_free(&res);
    /* without replay the older seq is still refused */
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, &(VjVal){ .type = VJ_INT, .i = 2 }, NULL);
    check_int(res.status, 409, "resume out of order");
    check_str(body_of(&res), "{\"error\":\"session sequence out of order\",\"lastSeq\":7}",
              "resume out of order body");
    crp_result_free(&res);
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, &(VjVal){ .type = VJ_INT, .i = 2 },
                              &(VjVal){ .type = VJ_BOOL, .b = 1 });
    check_int(res.status, 200, "resume with replay:true accepts an older seq");
    check(strstr(body_of(&res), "\"lastSeq\":7") != NULL, "an older replay does not move lastSeq", body_of(&res));
    crp_result_free(&res);
    /* a newer seq advances the stored sequence */
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, &(VjVal){ .type = VJ_INT, .i = 10 }, NULL);
    check_int(res.status, 200, "resume with a newer seq");
    {
        VjVal *rb = parse_or_die(body_of(&res));
        check_int(rb ? vj_int(vj_get(rb, "lastSeq"), -1) : -1, 10, "resume advances lastSeq");
        const VjVal *replay = rb ? vj_get(rb, "replay") : NULL;
        check(replay && replay->type == VJ_ARR && replay->n == 0, "nothing replays past the newest seq", NULL);
        vj_free(rb);
    }
    crp_result_free(&res);
    res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)token }, &(VjVal){ .type = VJ_INT, .i = 9 }, NULL);
    check_int(res.status, 409, "resume after the sequence advanced");
    check_str(body_of(&res), "{\"error\":\"session sequence out of order\",\"lastSeq\":10}",
              "out of order body after advancing");
    crp_result_free(&res);

    /* revoke */
    res = crp_registry_revoke(r, NULL);
    check_int(res.status, 400, "revoke without token");
    check_str(body_of(&res), "{\"error\":\"token is required\"}", "revoke without token body");
    crp_result_free(&res);
    res = crp_registry_revoke(r, &(VjVal){ .type = VJ_STR, .s = (char *)token });
    check_int(res.status, 200, "revoke status");
    check_str(body_of(&res), "{\"revoked\":true}", "revoke body");
    crp_result_free(&res);
    check_int(crp_registry_revoked_count(r), 1, "revoked count");
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL,
                              &(VjVal){ .type = VJ_STR, .s = (char *)token },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"peer-1" }, NULL);
    check_int(res.status, 403, "revoked token is refused");
    crp_result_free(&res);

    /* the event ring keeps the newest 64 */
    for (int i = 0; i < 70; i++) {
        VjVal ev = vj_str_v("bulk");
        res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" }, &ev, NULL, NULL,
                                  &(VjVal){ .type = VJ_STR, .s = (char *)"ring" }, NULL);
        crp_result_free(&res);
    }
    {
        CrpBuf st;
        upp_buf_init(&st);
        crp_registry_status_json(r, &st);
        check(strstr(st.data, "\"events\":64") != NULL, "event ring capped at 64", st.data);
        upp_buf_free(&st);
        res = crp_registry_resume(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                                  &(VjVal){ .type = VJ_STR, .s = (char *)"ring" },
                                  &(VjVal){ .type = VJ_STR, .s = (char *)token }, NULL, NULL);
        /* the token is bound to peer-1, so this must be refused */
        check_int(res.status, 403, "token is bound to one peer");
        crp_result_free(&res);
    }

    /* registry TTL pruning */
    crp_registry_set_now(r, PROBE_NOW + 31 * 60 * 1000);
    res = crp_registry_find(r, "");
    check_str(body_of(&res), "{\"items\":[]}", "registry prunes after registryTtlMs");
    crp_result_free(&res);
    res = crp_registry_signal(r, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                              &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, NULL, NULL, NULL, NULL);
    check_int(res.status, 404, "signal after the verse was pruned");
    crp_result_free(&res);

    /* lease expiry is visible through the platform session layer */
    crp_registry_set_now(r, PROBE_NOW);
    VjVal *reg4 = parse_or_die("{\"id\":\"lease\",\"endpoint\":\"e\"}");
    res = crp_registry_register(r, reg4);
    crp_result_free(&res);
    {
        char auth_lease[256];
        VjVal V_LEASE = { .type = VJ_STR, .s = (char *)"lease" };
        VjVal V_P = { .type = VJ_STR, .s = (char *)"p" };
        VjVal A_LEASE = { .type = VJ_STR, .s = auth_lease };
        proof_into(auth_lease, sizeof auth_lease, PROBE_ENROLL_SECRET, &V_LEASE, &V_P);
        res = crp_registry_portal(r, &V_LEASE, &V_P, &A_LEASE);
        crp_result_free(&res);
    }
    crp_result_free(&res);
    crp_registry_set_now(r, PROBE_NOW + 300001);
    {
        CrpBuf st;
        upp_buf_init(&st);
        crp_registry_status_json(r, &st);
        check(strstr(st.data, "\"leaseExpired\":true") != NULL, "lease expires after the TTL", st.data);
        upp_buf_free(&st);
    }

    /* revoked-set eviction honours maxRevokedTokens */
    {
        CrpRegistryConfig cfg2;
        memset(&cfg2, 0, sizeof cfg2);
        cfg2.secret = PROBE_SECRET;
        cfg2.now_ms = PROBE_NOW;
        cfg2.max_revoked = 2;
        CrpRegistry *r2 = crp_registry_new(&cfg2);
        for (int i = 0; i < 3; i++) {
            char name[16];
            snprintf(name, sizeof name, "t%d", i);
            CrpResult rr = crp_registry_revoke(r2, &(VjVal){ .type = VJ_STR, .s = name });
            crp_result_free(&rr);
        }
        check_int(crp_registry_revoked_count(r2), 2, "revoked set is capped");
        crp_registry_free(r2);
    }

    vj_free(portal_body);
    vj_free(reg);
    vj_free(reg2);
    vj_free(reg3);
    vj_free(reg4);
    crp_registry_free(r);
}

/* --------------------------------------------------------------- transcript */

static void emit_record(int n, const char *op, int ok, const char *error,
                        const char *out, int has_result, int result) {
    CrpBuf r;
    upp_buf_init(&r);
    upp_buf_puts(&r, "{\"n\":");
    upp_buf_put_ll(&r, n);
    upp_buf_puts(&r, ",\"op\":");
    upp_json_write_string(&r, op);
    upp_buf_puts(&r, ",\"ok\":");
    upp_buf_puts(&r, ok ? "true" : "false");
    upp_buf_puts(&r, ",\"error\":");
    if (error) upp_json_write_string(&r, error);
    else upp_buf_puts(&r, "null");
    upp_buf_puts(&r, ",\"out\":");
    if (out) upp_json_write_string(&r, out);
    else upp_buf_puts(&r, "null");
    upp_buf_puts(&r, ",\"result\":");
    if (!has_result) upp_buf_puts(&r, "null");
    else upp_buf_puts(&r, result ? "true" : "false");
    upp_buf_putc(&r, '}');
    if (r.data) fwrite(r.data, 1, r.len, stdout);
    fputc('\n', stdout);
    upp_buf_free(&r);
}

/* ------------------------------------------------------ transcript: registry */

/* One corpus is one session: the registry is created on first use and then
 * mutated by every registry op, exactly like the relay's Maps. */
typedef struct {
    const char *secret;
    const char *enroll_secret;  /* NULL/empty: every relay_portal is fail-closed */
    long long   now;
    CrpRegistry *reg;
} TranscriptCtx;

static CrpRegistry *ctx_registry(TranscriptCtx *ctx) {
    if (!ctx->reg) {
        CrpRegistryConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.secret = ctx->secret;
        cfg.enroll_secret = ctx->enroll_secret;
        cfg.now_ms = ctx->now;
        ctx->reg = crp_registry_new(&cfg);
    }
    return ctx->reg;
}

/* The reference answers these ops over HTTP, so the status code is part of the
 * comparable text: "<status> <body>". */
static void emit_http(int n, const char *op, const CrpResult *res) {
    CrpBuf b;
    upp_buf_init(&b);
    upp_buf_put_ll(&b, res->status);
    upp_buf_putc(&b, ' ');
    upp_buf_puts(&b, res->body.data ? res->body.data : "");
    emit_record(n, op, 1, NULL, b.data, 0, 0);
    upp_buf_free(&b);
}

/* `{op:"frame", type, payload, id}` and friends.  A field that is absent in
 * the corpus stays a NULL pointer, i.e. JS `undefined`. */
static void run_op(const VjVal *op, TranscriptCtx *ctx, int n) {
    const char *name = vj_str(vj_get(op, "op"), "");
    const char *id = vj_str(vj_get(op, "id"), "");
    char err[CRP_ERR_MAX];
    err[0] = '\0';
    CrpBuf out;
    upp_buf_init(&out);

    if (strcmp(name, "frame") == 0) {
        int rc = crp_frame(&out, vj_get(op, "type"), vj_get(op, "payload"), id, err, sizeof err);
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? out.data : NULL, 0, 0);
    } else if (strcmp(name, "find") == 0) {
        int rc = crp_frame_find(&out, vj_get(op, "query"), vj_get(op, "limit"),
                                vj_get(op, "cursor"), id, err, sizeof err);
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? out.data : NULL, 0, 0);
    } else if (strcmp(name, "portal") == 0) {
        int rc = crp_frame_portal(&out, vj_get(op, "verse"), vj_get(op, "peer"),
                                  vj_get(op, "token"), vj_get(op, "expires"), id, err, sizeof err);
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? out.data : NULL, 0, 0);
    } else if (strcmp(name, "signal") == 0) {
        int rc = crp_frame_signal(&out, vj_get(op, "verse"), vj_get(op, "event"),
                                  vj_get(op, "data"), vj_get(op, "timestamp"), id, err, sizeof err);
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? out.data : NULL, 0, 0);
    } else if (strcmp(name, "encode") == 0) {
        int rc = crp_encode(&out, vj_get(op, "message"), err, sizeof err);
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? out.data : NULL, 0, 0);
    } else if (strcmp(name, "encode_big") == 0) {
        size_t size = (size_t)vj_int(vj_get(op, "n"), 0);
        char *big = (char *)malloc(size + 1);
        memset(big, 'x', size);
        big[size] = '\0';
        VjVal sv = { .type = VJ_STR, .s = big };
        VjVal dobj = vj_obj1_v("blob", &sv);
        VjVal ts = vj_int_v(0);
        CrpBuf frame;
        upp_buf_init(&frame);
        int rc = crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"v" },
                                  &(VjVal){ .type = VJ_STR, .s = (char *)"e" }, &dobj, &ts, "", err, sizeof err);
        VjVal *parsed = rc == 0 ? crp_decode_line(frame.data, (size_t)-1, err, sizeof err) : NULL;
        CrpBuf line;
        upp_buf_init(&line);
        rc = parsed ? crp_encode(&line, parsed, err, sizeof err) : -1;
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? line.data : NULL, 0, 0);
        upp_buf_free(&line);
        vj_free(parsed);
        upp_buf_free(&frame);
        vj_obj1_free(&dobj);
        free(big);
    } else if (strcmp(name, "decode") == 0) {
        const VjVal *line = vj_get(op, "line");
        VjVal *decoded = crp_decode_line(vj_str(line, ""), (size_t)-1, err, sizeof err);
        if (!decoded) {
            emit_record(n, name, 0, err, NULL, 0, 0);
        } else {
            CrpBuf re;
            upp_buf_init(&re);
            int rc = crp_encode(&re, decoded, err, sizeof err);
            emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? re.data : NULL, 0, 0);
            upp_buf_free(&re);
            vj_free(decoded);
        }
    } else if (strcmp(name, "token_make") == 0) {
        int rc = crp_token_make(ctx->secret, vj_get(op, "verse"), vj_get(op, "peer"),
                                vj_get(op, "capabilities"), vj_int(vj_get(op, "exp"), 0),
                                &out, err, sizeof err);
        emit_record(n, name, rc == 0, rc == 0 ? NULL : err, rc == 0 ? out.data : NULL, 0, 0);
    } else if (strcmp(name, "token_check") == 0) {
        int ok = crp_token_check(ctx->secret, vj_str(vj_get(op, "token"), NULL),
                                 vj_get(op, "verse"), vj_get(op, "peer"),
                                 vj_str(vj_get(op, "capability"), NULL),
                                 vj_int(vj_get(op, "now"), ctx->now));
        emit_record(n, name, 1, NULL, NULL, 1, ok);
    } else if (strcmp(name, "register") == 0) {
        CrpResult res = crp_registry_register(ctx_registry(ctx), vj_get(op, "payload"));
        emit_http(n, name, &res);
        crp_result_free(&res);
    } else if (strcmp(name, "relay_find") == 0) {
        CrpResult res = crp_registry_find(ctx_registry(ctx), vj_str(vj_get(op, "q"), ""));
        emit_http(n, name, &res);
        crp_result_free(&res);
    } else if (strcmp(name, "relay_portal") == 0) {
        CrpResult res = crp_registry_portal(ctx_registry(ctx), vj_get(op, "verse"),
                                            vj_get(op, "peer"), vj_get(op, "auth"));
        emit_http(n, name, &res);
        crp_result_free(&res);
    } else if (strcmp(name, "relay_signal") == 0) {
        CrpResult res = crp_registry_signal(ctx_registry(ctx), vj_get(op, "verse"),
                                            vj_get(op, "event"), vj_get(op, "data"),
                                            vj_get(op, "token"), vj_get(op, "peer"),
                                            vj_get(op, "seq"));
        emit_http(n, name, &res);
        crp_result_free(&res);
    } else if (strcmp(name, "relay_resume") == 0) {
        CrpResult res = crp_registry_resume(ctx_registry(ctx), vj_get(op, "verse"),
                                            vj_get(op, "peer"), vj_get(op, "token"),
                                            vj_get(op, "seq"), vj_get(op, "replay"));
        emit_http(n, name, &res);
        crp_result_free(&res);
    } else if (strcmp(name, "relay_revoke") == 0) {
        CrpResult res = crp_registry_revoke(ctx_registry(ctx), vj_get(op, "token"));
        emit_http(n, name, &res);
        crp_result_free(&res);
    } else if (strcmp(name, "now") == 0) {
        /* The relay reads Date.now() everywhere; this is that clock. */
        CrpRegistry *r = ctx_registry(ctx);
        ctx->now = vj_int(vj_get(op, "ms"), ctx->now);
        crp_registry_set_now(r, ctx->now);
        crp_set_now(ctx->now);
        emit_record(n, name, 1, NULL, NULL, 0, 0);
    } else {
        emit_record(n, name, 0, "unknown probe op", NULL, 0, 0);
    }
    upp_buf_free(&out);
}

static int transcript(const char *path, const char *secret, const char *enroll_secret,
                      long long now) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "crp_probe: cannot open corpus %s\n", path);
        return 1;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int n = 0;
    TranscriptCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.secret = secret;
    ctx.enroll_secret = enroll_secret;
    ctx.now = now;
    crp_set_now(now);
    while ((len = getline(&line, &cap, f)) > 0) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        char perr[128];
        VjVal *op = vj_parse(line, perr, sizeof perr);
        if (!op) {
            fprintf(stderr, "crp_probe: corpus line %d is not JSON: %s\n", n + 1, perr);
            free(line);
            fclose(f);
            return 1;
        }
        n++;
        run_op(op, &ctx, n);
        vj_free(op);
    }
    crp_registry_free(ctx.reg);
    free(line);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    const char *corpus = NULL;
    const char *secret = PROBE_SECRET;
    const char *enroll_secret = PROBE_ENROLL_SECRET;
    long long now = PROBE_NOW;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--transcript") == 0 && i + 1 < argc) corpus = argv[++i];
        else if (strcmp(argv[i], "--secret") == 0 && i + 1 < argc) secret = argv[++i];
        else if (strcmp(argv[i], "--enroll-secret") == 0 && i + 1 < argc) enroll_secret = argv[++i];
        else if (strcmp(argv[i], "--now") == 0 && i + 1 < argc) now = atoll(argv[++i]);
        else {
            fprintf(stderr, "usage: crp_probe [--transcript <corpus.jsonl>] [--secret <hex>]"
                            " [--enroll-secret <hex>] [--now <ms>]\n");
            return 2;
        }
    }
    if (corpus) return transcript(corpus, secret, enroll_secret, now);

    test_base64url();
    test_hmac();
    test_tokens();
    test_frames();
    test_registry();

    printf("crp_probe: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
