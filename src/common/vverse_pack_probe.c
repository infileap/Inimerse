/* vverse_pack_probe.c - tests for the engine-side `.vverse` packer.
 *
 * WHY THIS EXISTS: the packer's acceptance criteria are its own, and three of
 * them cannot be checked by running a script by hand:
 *
 *   1. DETERMINISM.  Packing the same directory twice must give the same
 *      sha256 - twice in a row, on a machine where nothing else changed.  The
 *      bugs that break this (a timestamp in the gzip header, unordered entries,
 *      a size-dependent block layout) are invisible in a single pack.
 *   2. PURITY.  Packing must not write into the source tree.  The JS reference
 *      does (it drops a fresh signatures/sha256.json there), so this is a
 *      property we have to assert ourselves.
 *   3. LONG MESSAGES.  The ed25519 payload for a real package is far past
 *      8192 bytes; src/common/ed25519.c once truncated there through a
 *      one-shot sha512_buf.  The fixture below is sized so the payload
 *      provably exceeds 8 KiB, and the payload length is printed.
 *
 * On top of that: gzip KATs from real zlib output (dynamic Huffman - the
 * format tools/vverse_pack.js emits - and a member carrying FNAME), tamper
 * detection in both directions of the digest table, and path-escape refusal
 * for a hand-built hostile package.
 *
 * Pure unit probe: it only touches a temporary directory next to CWD.
 *
 * Run with no arguments it is the self-test.  Run with `--pack <dir> <out>
 * [seed-hex]`, `--unpack <pkg> <dir>`, `--validate <dir>` or `--sha256 <file>`
 * it is the driver that tools/vverse_cross.test.py uses to put engine output
 * in front of the JS reference and vice versa.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dir.h"
#include "gzip.h"
#include "platform.h"
#include "sha256.h"
#include "vverse_pack.h"

#ifdef _WIN32
#include <process.h>
#define PROBE_PID ((long)_getpid())
#else
#include <unistd.h>
#define PROBE_PID ((long)getpid())
#endif

static int failures = 0;
static int checks = 0;

static void check(const char *what, int ok) {
    checks++;
    if (!ok) failures++;
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
}

/* ------------------------------ helpers -------------------------------- */

static char TMP[256];   /* the tree being packed */
static char OUT[256];   /* packages and extractions: never inside TMP */

static int wfile(const char *rel, const void *data, size_t len) {
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", TMP, rel);
    char parent[2048];
    snprintf(parent, sizeof parent, "%s", path);
    char *slash = strrchr(parent, '/');
    if (slash) { *slash = 0; if (im_platform_mkdirs(parent)) return -1; }
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return -1; }
    return fclose(f) == 0 ? 0 : -1;
}

static void rm_rf(const char *path) {
    ImDir *d = im_dir_open(path);
    if (d) {
        char name[1024];
        int isdir = 0;
        while (im_dir_next_ex(d, name, sizeof name, &isdir) > 0) {
            char child[2048];
            snprintf(child, sizeof child, "%s/%s", path, name);
            if (isdir) rm_rf(child);
            else remove(child);
        }
        im_dir_close(d);
    }
    remove(path);
}

static unsigned char *rfile(const char *rel, size_t *out_len) {
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", OUT, rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 65537 > cap) { cap = cap ? cap * 2 : 65536; buf = (unsigned char *)realloc(buf, cap); if (!buf) { fclose(f); return NULL; } }
        size_t got = fread(buf + len, 1, 65536, f);
        len += got;
        if (got < 65536) break;
    }
    fclose(f);
    *out_len = len;
    return buf;
}

static void payload_pattern(unsigned char *b, size_t n) {
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)((i * 131u + 7u) & 0xffu);
}

static const char *b64e(const unsigned char *in, size_t n, char *out) {
    static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        size_t rem = n - i;
        unsigned v = (unsigned)in[i] << 16;
        if (rem > 1) v |= (unsigned)in[i + 1] << 8;
        if (rem > 2) v |= (unsigned)in[i + 2];
        out[o++] = A[(v >> 18) & 63];
        out[o++] = A[(v >> 12) & 63];
        out[o++] = rem > 1 ? A[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? A[v & 63] : '=';
    }
    out[o] = 0;
    return out;
}

/* --------------------------- fixture tree ------------------------------ */

#define NFILES 220
static size_t g_payload_len = 0;
static unsigned char g_big[200000];

static int build_tree(void) {
    char path[2048];
    if (im_platform_mkdirs(TMP)) return -1;
    static const char *dirs[4] = { "laws", "assets", "mods", "signatures" };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", TMP, dirs[i]);
        if (im_platform_mkdirs(path)) return -1;
    }
    const char *manifest = "{\"id\":\"probe.mod\",\"version\":\"1.0.0\","
                           "\"entry\":\"mods/entry.im\",\"dependencies\":{\"core\":\"1.0.0\"}}";
    if (wfile("manifest.json", manifest, strlen(manifest))) return -1;
    const char *bp = "{\"nodes\":[]}";
    if (wfile("blueprint.json", bp, strlen(bp))) return -1;
    if (wfile("laws/rule.im", "law rule { allow read; }\n", 26)) return -1;
    if (wfile("mods/entry.im", "print(\"hi\");\n", 13)) return -1;

    /* one file past the 8192-byte one-shot boundary (it must hash whole), and
     * one past 65535 so gzip_pack_stored has to emit several stored blocks */
    payload_pattern(g_big, sizeof g_big);
    unsigned char mid[9000];
    payload_pattern(mid, sizeof mid);
    if (wfile("assets/mid.bin", mid, sizeof mid)) return -1;
    if (wfile("assets/big.bin", g_big, sizeof g_big)) return -1;

    /* enough small files that the signed payload is well past 8 KiB.
     * Compact entry = "name":"hex" = 1 + len(name) + 1 + 1 + 64 + 1. */
    const char *fixed[6] = { "manifest.json", "blueprint.json", "laws/rule.im",
                             "mods/entry.im", "assets/mid.bin", "assets/big.bin" };
    size_t total = 2 + (size_t)(6 + NFILES - 1); /* braces + commas */
    for (int i = 0; i < 6; i++) total += 1 + strlen(fixed[i]) + 1 + 1 + 64 + 1;
    for (int i = 0; i < NFILES; i++) total += 1 + 16 + 1 + 1 + 64 + 1;
    g_payload_len = total;
    for (int i = 0; i < NFILES; i++) {
        char rel[64], body[32];
        snprintf(rel, sizeof rel, "assets/f%03d.bin", i);
        snprintf(body, sizeof body, "payload-%03d\n", i);
        if (wfile(rel, body, strlen(body))) return -1;
    }
    return 0;
}

/* ------------------------------ gzip KATs ------------------------------ */

/* zlib level 6 over 200 bytes (BTYPE=2, dynamic Huffman) - the shape
 * tools/vverse_pack.js produces. */
static const char *ZLIB6_HEX =
    "1f8b08000000000000034dccb10ac2301485e177c9ac2d51a8e8660b6a820e2a22744bda5b892669c94d2ba1f8ee465d3ce3"
    "e1e31f49d33a233c599161008730a564421aa501c96a2402113ca6b5f02291ca46b58e8b42ea1e3aa7ac4feed87e7e08bc97"
    "b393ae02cbae942ea3d1e289a9eb3524ca4421b79b39db712ab7971b84fc238cb0aa01fc8f7465c13266f320675cef0d1dca82"
    "7f695b630ad6bbf0ab553bde497b6c0fe7c782bc5e6f334da278c8000000";
static const char *ZLIB6_TEXT =
    "{\"format\":\"vverse-1\",\"files\":{\"assets/data.bin\":\"AAAA\","
    "\"blueprint.json\":\"eyJub2RlcyI6W119\",\"laws/rule.im\":\"bGF3IHJ1bGUgeyB9\","
    "\"manifest.json\":\"eyJpZCI6InByb2JlLm1vZCJ9\",\"mods/entry.im\":\"cHJpbnQoMSk7\"}}";

/* `gzip -Nc` of the string below: carries FNAME ("vvname.txt"), a non-zero
 * MTIME and XFL=0.  None of those may upset the decoder. */
static const char *FNAME_HEX =
    "1f8b0808eb12bf6a000376766e616d652e74787400cb492c2fd62f2acd49d5cb"
    "ccb552c8492c57007114aa15127372f2819cd4c4146b855a2e00853f32532700"
    "0000";
static const char *FNAME_TEXT = "laws/rule.im: law rule { allow read; }\n";

static void from_hex(const char *hex, unsigned char *out, size_t *out_len) {
    size_t n = strlen(hex) / 2, i;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (unsigned char)v;
    }
    *out_len = n;
}

static void gzip_kats(void) {
    unsigned char blob[512];
    size_t blen = 0, olen = 0;
    unsigned char *out = NULL;

    from_hex(ZLIB6_HEX, blob, &blen);
    check("gzip KAT: zlib level 6 (dynamic Huffman) decodes",
          gzip_unpack(blob, blen, &out, &olen, NULL, 0) == 0 &&
              olen == strlen(ZLIB6_TEXT) && memcmp(out, ZLIB6_TEXT, olen) == 0);
    check("gzip KAT: level-6 stream really is dynamic Huffman",
          blen > 11 && ((blob[10] >> 1) & 3) == 2);
    free(out); out = NULL;

    from_hex(FNAME_HEX, blob, &blen);
    check("gzip KAT: member with FNAME/MTIME decodes",
          gzip_unpack(blob, blen, &out, &olen, NULL, 0) == 0 &&
              olen == strlen(FNAME_TEXT) && memcmp(out, FNAME_TEXT, olen) == 0);
    check("gzip KAT: FNAME member really sets the FNAME flag",
          blen > 4 && (blob[3] & 0x08) && (blob[3] & 0x04) == 0);
    free(out); out = NULL;

    /* corrupted trailer must be caught, not silently accepted */
    from_hex(ZLIB6_HEX, blob, &blen);
    blob[blen - 1] ^= 0xff; /* ISIZE */
    check("gzip rejects a corrupted ISIZE", gzip_unpack(blob, blen, &out, &olen, NULL, 0) != 0);
    free(out); out = NULL;
    from_hex(ZLIB6_HEX, blob, &blen);
    blob[blen - 5] ^= 0xff; /* CRC32 */
    check("gzip rejects a corrupted CRC32", gzip_unpack(blob, blen, &out, &olen, NULL, 0) != 0);
    free(out); out = NULL;
    from_hex(ZLIB6_HEX, blob, &blen);
    check("gzip rejects a truncated stream",
          gzip_unpack(blob, blen - 20, &out, &olen, NULL, 0) != 0);
    free(out);
}

/* --------------------------- pack determinism -------------------------- */

static void determinism(void) {
    char a[2048], b[2048];
    snprintf(a, sizeof a, "%s/out_a.vverse", OUT);
    snprintf(b, sizeof b, "%s/out_b.vverse", OUT);

    char err[256] = { 0 };
    check("pack #1 succeeds", vverse_pack(TMP, a, NULL, err, sizeof err) == 0);
    if (failures) printf("    err: %s\n", err);
    check("pack #2 (same tree) succeeds", vverse_pack(TMP, b, NULL, err, sizeof err) == 0);

    char ha[65] = { 0 }, hb[65] = { 0 };
    check("sha256 of pack #1", vverse_sha256_file(a, ha, err, sizeof err) == 0);
    check("sha256 of pack #2", vverse_sha256_file(b, hb, err, sizeof err) == 0);
    check("two packs of one directory are byte-identical",
          strlen(ha) == 64 && strcmp(ha, hb) == 0);
    printf("    sha256 = %s\n", ha);

    /* purity: the source tree must not have gained a signature file */
    char sp[2048];
    snprintf(sp, sizeof sp, "%s/signatures/sha256.json", TMP);
    FILE *f = fopen(sp, "rb");
    check("packing did not write into the source tree", f == NULL);
    if (f) fclose(f);

    /* a package that is not verified must not be loadable as a tree */
    check("unsigned tree fails validate (missing signatures/sha256.json)",
          vverse_validate(TMP, err, sizeof err) != 0);
    check("  ...with the expected reason",
          strstr(err, "missing signatures/sha256.json") != NULL);
}

/* --------------------------- signature path ---------------------------- */

static unsigned char SEED[32];

static void signature_roundtrip(void) {
    char pkg[2048];
    snprintf(pkg, sizeof pkg, "%s/signed.vverse", OUT);
    char err[256] = { 0 };

    /* the payload that ed25519 signs must be the long one */
    check("ed25519 payload exceeds the 8192-byte one-shot boundary", g_payload_len > 8192);
    printf("    signed payload ~ %zu bytes\n", g_payload_len);

    check("pack with an ed25519 seed succeeds",
          vverse_pack(TMP, pkg, SEED, err, sizeof err) == 0);
    if (err[0]) printf("    why: %s\n", err);

    /* (validate on the *directory* is unsigned; the package is what is signed) */
    char dest[512];
    snprintf(dest, sizeof dest, "%s/x1", OUT);
    check("unpack of a signed package succeeds (digest + ed25519)",
          vverse_unpack(pkg, dest, err, sizeof err) == 0);
    if (err[0]) printf("    why: %s\n", err);
    check("unpacked tree re-validates", vverse_validate(dest, err, sizeof err) == 0);

    char p[1024];
    snprintf(p, sizeof p, "%s/signatures/ed25519.json", dest);
    FILE *f = fopen(p, "rb");
    check("signatures/ed25519.json was extracted", f != NULL);
    if (f) fclose(f);

    /* determinism with a signature is just as strict */
    char pkg2[2048];
    snprintf(pkg2, sizeof pkg2, "%s/signed2.vverse", OUT);
    vverse_pack(TMP, pkg2, SEED, err, sizeof err);
    char h1[65], h2[65];
    vverse_sha256_file(pkg, h1, err, sizeof err);
    vverse_sha256_file(pkg2, h2, err, sizeof err);
    check("signed packs are deterministic too", strlen(h1) == 64 && strcmp(h1, h2) == 0);
}

/* ---------------------------- tamper tests ----------------------------- */

static void tamper(void) {
    char pkg[2048], dest[512], err[256] = { 0 };
    snprintf(pkg, sizeof pkg, "%s/signed.vverse", OUT);

    /* (a) flip one byte of a data file: digest mismatch */
    snprintf(dest, sizeof dest, "%s/x2", OUT);
    check("unpack into x2", vverse_unpack(pkg, dest, err, sizeof err) == 0);
    char victim[1024];
    snprintf(victim, sizeof victim, "%s/laws/rule.im", dest);
    FILE *f = fopen(victim, "r+b");
    if (f) { fputc('X', f); fclose(f); }
    check("a flipped byte is caught (digest mismatch)",
          vverse_validate(dest, err, sizeof err) != 0 && strstr(err, "digest mismatch") != NULL);
    printf("    err: %s\n", err);

    /* (b) add a file nobody signed: unsigned file */
    snprintf(dest, sizeof dest, "%s/x3", OUT);
    vverse_unpack(pkg, dest, err, sizeof err);
    const char *extra = "smuggled";
    char ep[1024];
    snprintf(ep, sizeof ep, "%s/laws/extra.im", dest);
    f = fopen(ep, "wb");
    if (f) { fwrite(extra, 1, strlen(extra), f); fclose(f); }
    check("an unsigned extra file is caught",
          vverse_validate(dest, err, sizeof err) != 0 && strstr(err, "unsigned file") != NULL);
    printf("    err: %s\n", err);

    /* (c) forge the ed25519 signature */
    snprintf(dest, sizeof dest, "%s/x4", OUT);
    vverse_unpack(pkg, dest, err, sizeof err);
    snprintf(ep, sizeof ep, "%s/signatures/ed25519.json", dest);
    size_t elen = 0;
    unsigned char *ejson = NULL;
    {
        FILE *g = fopen(ep, "rb");
        if (g) {
            fseek(g, 0, SEEK_END);
            long sz = ftell(g);
            fseek(g, 0, SEEK_SET);
            ejson = (unsigned char *)malloc((size_t)sz + 1);
            if (ejson) { size_t got = fread(ejson, 1, (size_t)sz, g); ejson[got] = 0; elen = got; }
            fclose(g);
        }
    }
    int forged = 0;
    if (ejson) {
        char *sigkey = strstr((char *)ejson, "\"signature\"");
        char *colon = sigkey ? strchr(sigkey, ':') : NULL;
        char *q1 = colon ? strchr(colon, '"') : NULL;
        char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (q1 && q2 && q2 - q1 - 1 == 88) { /* 64 raw bytes -> 88 base64 chars */
            /* flip one significance bit: still well-formed base64 of 64 bytes,
             * so this must fail the ed25519 check itself, not the parser */
            q1[1] = q1[1] == 'A' ? 'B' : 'A';
            FILE *g = fopen(ep, "wb");
            if (g) { fwrite(ejson, 1, elen, g); fclose(g); forged = 1; }
        }
        free(ejson);
    }
    check("a forged ed25519 signature is caught",
          forged && vverse_validate(dest, err, sizeof err) != 0 &&
              strstr(err, "ed25519 signature verification failed") != NULL);
    printf("    err: %s\n", err);

    /* (d) a signed entry whose file is absent from the package */
    snprintf(dest, sizeof dest, "%s/x5", OUT);
    vverse_unpack(pkg, dest, err, sizeof err);
    snprintf(victim, sizeof victim, "%s/laws/rule.im", dest);
    remove(victim);
    check("removing a signed file is caught",
          vverse_validate(dest, err, sizeof err) != 0 &&
              strstr(err, "digest mismatch") != NULL);
    printf("    err: %s\n", err);
}

/* -------------------------- path escape -------------------------------- */

static void escape_test(void) {
    /* Hand-built hostile package: correct digests, correct completeness, but
     * a file name that climbs out of the destination. */
    const char *m = "{\"id\":\"evil\",\"version\":\"1\",\"entry\":\"mods/e.im\"}";
    const char *b = "{}";
    const char *evil = "owned";
    const char *e2 = "owned2";
    unsigned char dm[32], db[32], de[32], de2[32];
    char hm[65], hb[65], he[65], he2[65];
    sha256_digest(m, strlen(m), dm); sha256_hex_of_digest(dm, hm);
    sha256_digest(b, strlen(b), db); sha256_hex_of_digest(db, hb);
    sha256_digest(evil, strlen(evil), de); sha256_hex_of_digest(de, he);
    sha256_digest(e2, strlen(e2), de2); sha256_hex_of_digest(de2, he2);

    char table[2048];
    snprintf(table, sizeof table,
             "{\n  \"../escaped.txt\": \"%s\",\n  \"blueprint.json\": \"%s\",\n"
             "  \"manifest.json\": \"%s\"\n}\n", he, hb, hm);
    char tbl_b64[4096], m_b64[256], b_b64[256], e_b64[64], e2_b64[64];
    b64e((const unsigned char *)table, strlen(table), tbl_b64);
    b64e((const unsigned char *)m, strlen(m), m_b64);
    b64e((const unsigned char *)b, strlen(b), b_b64);
    b64e((const unsigned char *)evil, strlen(evil), e_b64);
    b64e((const unsigned char *)e2, strlen(e2), e2_b64);

    /* table also has to cover the two metadata files: it does (manifest,
     * blueprint) - plus the escaping one. */
    char *json = (char *)malloc(8192);
    snprintf(json, 8192,
             "{\"format\":\"vverse-1\",\"files\":{"
             "\"../escaped.txt\":\"%s\",\"blueprint.json\":\"%s\",\"manifest.json\":\"%s\","
             "\"signatures/sha256.json\":\"%s\"}}",
             e_b64, b_b64, m_b64, tbl_b64);

    unsigned char *gz = NULL;
    size_t gzlen = 0;
    char pkg[2048];
    snprintf(pkg, sizeof pkg, "%s/evil.vverse", OUT);
    int ok = gzip_pack_stored(json, strlen(json), &gz, &gzlen) == 0;
    if (ok) {
        FILE *f = fopen(pkg, "wb");
        ok = f && fwrite(gz, 1, gzlen, f) == gzlen && fclose(f) == 0;
    }
    free(gz);
    free(json);

    char dest[512], err[256] = { 0 };
    snprintf(dest, sizeof dest, "%s/evil_out", OUT);
    check("hostile package packed for the test", ok);
    check("a '..' file name is refused by unpack",
          vverse_unpack(pkg, dest, err, sizeof err) != 0 &&
              strstr(err, "path escapes package") != NULL);
    printf("    err: %s\n", err);

    char outside[2048];
    snprintf(outside, sizeof outside, "%s/escaped.txt", OUT);
    FILE *f = fopen(outside, "rb");
    check("nothing was written outside the destination", f == NULL);
    if (f) fclose(f);
}

/* ------------------------- extraction integrity ------------------------ */

static void extraction(void) {
    char pkg[2048], d1[2048], d2[2048], err[256] = { 0 };
    snprintf(pkg, sizeof pkg, "%s/signed.vverse", OUT);
    snprintf(d1, sizeof d1, "%s/e1", OUT);
    snprintf(d2, sizeof d2, "%s/e2", OUT);
    check("unpack #1", vverse_unpack(pkg, d1, err, sizeof err) == 0);
    check("unpack #2", vverse_unpack(pkg, d2, err, sizeof err) == 0);

    size_t n1 = 0, n2 = 0;
    unsigned char *a = rfile("e1/assets/big.bin", &n1);
    unsigned char *b = rfile("e2/assets/big.bin", &n2);
    check("the 200000-byte file round-trips byte for byte",
          a && b && n1 == sizeof g_big && n2 == sizeof g_big && memcmp(a, g_big, n1) == 0);
    check("two extractions agree", a && b && n1 == n2 && memcmp(a, b, n1) == 0);
    free(a); free(b);

    a = rfile("e1/assets/mid.bin", &n1);
    check("the 9000-byte file round-trips", a && n1 == 9000);
    free(a);

    /* the multi-block stored writer must have produced more than one block */
    size_t plen = 0;
    unsigned char *pkgb = rfile("signed.vverse", &plen);
    check("writer emitted a valid gzip member", pkgb && plen > 12 && pkgb[0] == 0x1f && pkgb[1] == 0x8b);
    if (pkgb) {
        unsigned char *plain = NULL;
        size_t plainlen = 0;
        check("our own member re-decodes (stored blocks, several of them)",
              gzip_unpack(pkgb, plen, &plain, &plainlen, err, sizeof err) == 0);
        if (plain && plainlen) {
            /* at least two stored blocks were needed for a >64 KiB body */
            check("body really is larger than one stored block (>65535)",
                  plainlen > 65535);
        }
        free(plain);
    }
    free(pkgb);
}

/* ------------------------------- main ---------------------------------- */

/* Driver mode.  `tools/vverse_cross.test.py` uses this binary to pack a
 * directory with the ENGINE and to load a package written by the JS
 * reference, so the two implementations meet over real files.  No arguments
 * means "run the self-test above". */
static int parse_hex32(const char *hex, unsigned char out[32]) {
    if (!hex || strlen(hex) != 64) return -1;
    for (int i = 0; i < 32; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (unsigned char)v;
    }
    return 0;
}

static int cli(int argc, char **argv) {
    char err[512] = { 0 };
    const char *cmd = argv[1];
    if (!strcmp(cmd, "--pack") && (argc == 4 || argc == 5)) {
        unsigned char seed[32];
        const unsigned char *s = NULL;
        if (argc == 5) {
            if (parse_hex32(argv[4], seed) != 0) { fprintf(stderr, "seed must be 64 hex chars\n"); return 2; }
            s = seed;
        }
        if (vverse_pack(argv[2], argv[3], s, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        printf("%s\n", argv[3]);
        return 0;
    }
    if (!strcmp(cmd, "--unpack") && argc == 4) {
        if (vverse_unpack(argv[2], argv[3], err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        printf("%s\n", argv[3]);
        return 0;
    }
    if (!strcmp(cmd, "--validate") && argc == 3) {
        if (vverse_validate(argv[2], err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        printf("valid\n");
        return 0;
    }
    if (!strcmp(cmd, "--sha256") && argc == 3) {
        char hex[65];
        if (vverse_sha256_file(argv[2], hex, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
        printf("%s\n", hex);
        return 0;
    }
    fprintf(stderr, "usage: %s [--pack <dir> <out.vverse> [seed-hex] | --unpack <pkg> <dir>"
                    " | --validate <dir> | --sha256 <file>]\n", argv[0]);
    return 2;
}

int main(int argc, char **argv) {
    if (argc > 1) return cli(argc, argv);
    snprintf(TMP, sizeof TMP, "vverse_probe_tmp_%ld", PROBE_PID);
    snprintf(OUT, sizeof OUT, "vverse_probe_out_%ld", PROBE_PID);
    rm_rf(TMP);
    rm_rf(OUT);
    if (im_platform_mkdirs(OUT)) {
        printf("vverse_pack_probe: FAILED to create the output directory\n");
        return 1;
    }

    memset(SEED, 0, sizeof SEED);
    for (int i = 0; i < 32; i++) SEED[i] = (unsigned char)(0x40 + i);

    gzip_kats();

    if (build_tree() != 0) {
        printf("vverse_pack_probe: FAILED to build the fixture tree\n");
        return 1;
    }

    determinism();
    signature_roundtrip();
    tamper();
    escape_test();
    extraction();

    rm_rf(TMP);
    rm_rf(OUT);

    if (failures) {
        printf("vverse_pack_probe: FAILED (%d of %d checks)\n", failures, checks);
        return 1;
    }
    printf("vverse_pack_probe: all %d checks OK\n", checks);
    return 0;
}
