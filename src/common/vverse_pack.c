/* vverse_pack.c - engine-side `.vverse` package writer and reader.
 *
 * See vverse_pack.h for the format contract and the two deliberate
 * differences from `tools/vverse_pack.js`.  In one line: this file makes the
 * ENGINE able to produce a package (it could previously only be produced by
 * the 60-line JS reference), and able to read both its own packages and the
 * JS reference's (zlib-compressed) ones.
 *
 * Notes that matter while reading the code:
 *
 *   - The digest table and the ed25519 payload are the SAME bytes, built by
 *     one function (build_sorted_object): the table pretty-printed with two
 *     spaces and a trailing newline, the payload compact with sorted keys.
 *     That is exactly what the reference does, and it is the reason a package
 *     signed here verifies under `crypto.verify(null, msg, key, sig)`.
 *   - File digests go through the streaming SHA-256 interface.  There is no
 *     one-shot buffer anywhere in this file, so nothing can silently truncate
 *     at 8 KiB (the failure mode documented in STATUS.md §2.4 for SHA-512).
 *   - Packing never writes into `root`.  The reference's writer drops a fresh
 *     signatures/sha256.json into the source tree; we build it in memory, so
 *     packing is a pure function of the tree's contents.
 */
#include "vverse_pack.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dir.h"
#include "ed25519.h"
#include "gzip.h"
#include "json_min.h"
#include "platform.h"
#include "sha256.h"

#ifdef _WIN32
#include <sys/stat.h>
#define VV_STAT _stat
#define VV_ISDIR(m) (((m) & _S_IFDIR) != 0)
#else
#include <sys/stat.h>
#define VV_STAT stat
#define VV_ISDIR(m) S_ISDIR(m)
#endif

#define VV_MAX_PATH 4096

static int vfail(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* ------------------------------- buffers ------------------------------- */

typedef struct {
    unsigned char *p;
    size_t len, cap;
} Buf;

static void buf_free(Buf *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static int buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 0;
    size_t nc = b->cap ? b->cap : 256;
    while (nc < b->len + extra + 1) nc *= 2;
    unsigned char *np = (unsigned char *)realloc(b->p, nc);
    if (!np) return -1;
    b->p = np;
    b->cap = nc;
    return 0;
}
static int buf_add(Buf *b, const void *d, size_t n) {
    if (buf_reserve(b, n)) return -1;
    if (n) memcpy(b->p + b->len, d, n);
    b->len += n;
    b->p[b->len] = 0;
    return 0;
}
static int buf_adds(Buf *b, const char *s) { return buf_add(b, s, strlen(s)); }
static int buf_addc(Buf *b, char c) { return buf_add(b, &c, 1); }

/* JSON string escaping, byte-for-byte what JSON.stringify does for the
 * characters that can appear in a file name: `"` and `\` get backslashes,
 * C0 controls get the short escapes or \u00xx, everything >= 0x20 (including
 * raw multi-byte UTF-8) passes through unchanged. */
static int buf_add_escaped(Buf *b, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        int r = 0;
        switch (c) {
        case '"':  r = buf_adds(b, "\\\""); break;
        case '\\': r = buf_adds(b, "\\\\"); break;
        case '\b': r = buf_adds(b, "\\b"); break;
        case '\f': r = buf_adds(b, "\\f"); break;
        case '\n': r = buf_adds(b, "\\n"); break;
        case '\r': r = buf_adds(b, "\\r"); break;
        case '\t': r = buf_adds(b, "\\t"); break;
        default:
            if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); r = buf_adds(b, t); }
            else r = buf_addc(b, (char)c);
            break;
        }
        if (r) return -1;
    }
    return 0;
}

/* ------------------------------ file I/O ------------------------------- */

static unsigned char *file_read_all(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 65536 + 1 > cap) {
            size_t nc = cap ? cap * 2 : 65536;
            while (nc < len + 65536 + 1) nc *= 2;
            unsigned char *nb = (unsigned char *)realloc(buf, nc);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb; cap = nc;
        }
        size_t got = fread(buf + len, 1, 65536, f);
        len += got;
        if (got < 65536) break;
    }
    if (ferror(f)) { free(buf); fclose(f); return NULL; }
    fclose(f);
    if (!buf) { buf = (unsigned char *)malloc(1); if (!buf) return NULL; }
    buf[len] = 0;
    *out_len = len;
    return buf;
}

static int file_write_all(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return -1; }
    if (fclose(f) != 0) return -1;
    return 0;
}

static int path_is_dir(const char *p) {
    struct stat st;
    if (VV_STAT(p, &st) != 0) return 0;
    return VV_ISDIR(st.st_mode) ? 1 : 0;
}
static int path_is_file(const char *p) {
    struct stat st;
    if (VV_STAT(p, &st) != 0) return 0;
    return VV_ISDIR(st.st_mode) ? 0 : 1;
}

static int path_join(char *out, size_t cap, const char *a, const char *b) {
    int n;
    if (!a || !*a) n = snprintf(out, cap, "%s", b && *b ? b : "");
    else if (!b || !*b) n = snprintf(out, cap, "%s", a);
    else n = snprintf(out, cap, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

/* ------------------------------ base64 -------------------------------- */

static const char B64_ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Buffer.toString('base64'): standard alphabet, '=' padding, no line breaks. */
static char *b64_encode(const unsigned char *in, size_t len) {
    size_t olen = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(olen + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        size_t rem = len - i;
        unsigned v = (unsigned)in[i] << 16;
        if (rem > 1) v |= (unsigned)in[i + 1] << 8;
        if (rem > 2) v |= (unsigned)in[i + 2];
        out[o++] = B64_ALPHA[(v >> 18) & 63];
        out[o++] = B64_ALPHA[(v >> 12) & 63];
        out[o++] = rem > 1 ? B64_ALPHA[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? B64_ALPHA[v & 63] : '=';
    }
    out[o] = 0;
    return out;
}

/* Strict base64.  Buffer.from(s, 'base64') is lenient (it skips characters it
 * does not recognise and stops at the first '='), which would let a hostile
 * package smuggle bytes past a digest table.  We accept only well-formed
 * canonical base64 with correct padding, and reject everything else. */
static int b64_index(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

static unsigned char *b64_decode(const char *s, size_t *out_len) {
    size_t n = s ? strlen(s) : 0;
    if (n % 4 != 0) return NULL;
    unsigned char *out = (unsigned char *)malloc(n / 4 * 3 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i += 4) {
        int v[4];
        for (int k = 0; k < 4; k++) v[k] = b64_index((unsigned char)s[i + k]);
        if (v[0] < 0 || v[1] < 0) { free(out); return NULL; }
        if (v[2] == -1 || v[3] == -1) { free(out); return NULL; }
        if (v[2] == -2 && v[3] != -2) { free(out); return NULL; }
        if ((v[2] == -2 || v[3] == -2) && i + 4 != n) { free(out); return NULL; } /* padding only at the end */
        out[o++] = (unsigned char)((v[0] << 2) | (v[1] >> 4));
        if (v[2] != -2) out[o++] = (unsigned char)(((v[1] & 15) << 4) | (v[2] >> 2));
        if (v[3] != -2) out[o++] = (unsigned char)(((v[2] & 3) << 6) | v[3]);
    }
    *out_len = o;
    return out;
}

/* --------------------------- name safety ------------------------------ */

/* Mirrors the reference's escape check (which is an absolute-path check on
 * the resolved target) but rejects up front: absolute paths, Windows drive /
 * backslash forms, empty components, and any "." / ".." component. */
static int name_is_safe(const char *name) {
    if (!name || !*name) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    if (strchr(name, '\\')) return 0;
    if (strstr(name, ":")) return 0;
    const char *p = name;
    while (*p) {
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - seg);
        if (n == 0) return 0;
        if (n == 1 && seg[0] == '.') return 0;
        if (n == 2 && seg[0] == '.' && seg[1] == '.') return 0;
        if (*p == '/') p++;
    }
    return 1;
}

/* ------------------------- file set + digests ------------------------- */

typedef struct {
    char *name;
    char hex[65];
} VEntry;

typedef struct {
    VEntry *v;
    size_t n, cap;
} VList;

static void vlist_free(VList *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i].name);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

static int vlist_push(VList *l, const char *name) {
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 32;
        VEntry *nv = (VEntry *)realloc(l->v, nc * sizeof(VEntry));
        if (!nv) return -1;
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n].name = strdup(name);
    if (!l->v[l->n].name) return -1;
    l->v[l->n].hex[0] = 0;
    l->n++;
    return 0;
}

static int ventry_cmp(const void *a, const void *b) {
    return strcmp(((const VEntry *)a)->name, ((const VEntry *)b)->name);
}

static const VEntry *vlist_find(const VList *l, const char *name) {
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i].name, name) == 0) return &l->v[i];
    return NULL;
}

/* Recursive collect.  `signatures` is skipped by NAME at every depth, which is
 * what the reference's walk() does; the two signature files are re-added
 * explicitly at pack time. */
static int walk_collect(const char *root, const char *rel, VList *out, char *err, size_t errlen) {
    char full[VV_MAX_PATH];
    if (path_join(full, sizeof full, root, rel)) return vfail(err, errlen, "path too long: %s", rel);
    ImDir *d = im_dir_open(full);
    if (!d) return vfail(err, errlen, "cannot read directory: %s", full);
    char name[1024];
    int isdir = 0;
    while (im_dir_next_ex(d, name, sizeof name, &isdir) > 0) {
        if (strcmp(name, "signatures") == 0) continue;
        char child[VV_MAX_PATH];
        if (path_join(child, sizeof child, rel, name)) {
            im_dir_close(d);
            return vfail(err, errlen, "path too long: %s/%s", rel, name);
        }
        if (isdir) {
            if (walk_collect(root, child, out, err, errlen)) { im_dir_close(d); return -1; }
        } else if (vlist_push(out, child)) {
            im_dir_close(d);
            return vfail(err, errlen, "out of memory");
        }
    }
    im_dir_close(d);
    return 0;
}

/* sha256 of a file's raw bytes, streamed. */
static int digest_file(const char *path, char out_hex[65], char *err, size_t errlen) {
    size_t len = 0;
    unsigned char *data = file_read_all(path, &len);
    if (!data) return vfail(err, errlen, "cannot read file: %s", path);
    Sha256Ctx c;
    sha256_init(&c);
    sha256_update(&c, data, len);
    uint8_t dg[32];
    sha256_final(&c, dg);
    sha256_hex_of_digest(dg, out_hex);
    free(data);
    return 0;
}

int vverse_sha256_file(const char *path, char out_hex[65], char *err, size_t errlen) {
    return digest_file(path, out_hex, err, errlen);
}

/* `files` must already be sorted by name.  pretty=1 reproduces
 * JSON.stringify(obj, null, 2); pretty=0 reproduces JSON.stringify(obj). */
static int build_sorted_object(const VList *files, int pretty, Buf *out) {
    if (files->n == 0) return buf_adds(out, "{}");
    if (buf_adds(out, "{")) return -1;
    for (size_t i = 0; i < files->n; i++) {
        if (i) { if (buf_adds(out, pretty ? ",\n" : ",")) return -1; }
        else if (pretty) { if (buf_adds(out, "\n")) return -1; }
        if (pretty && buf_adds(out, "  ")) return -1;
        if (buf_addc(out, '"')) return -1;
        if (buf_add_escaped(out, files->v[i].name)) return -1;
        if (buf_adds(out, pretty ? "\": \"" : "\":\"")) return -1;
        if (buf_adds(out, files->v[i].hex)) return -1;
        if (buf_addc(out, '"')) return -1;
    }
    if (pretty && buf_adds(out, "\n")) return -1;
    return buf_adds(out, "}");
}

/* ----------------------------- manifest ------------------------------- */

static int check_manifest_fields(const char *root, const char *text, char *err, size_t errlen) {
    char jerr[128];
    VjVal *m = vj_parse(text, jerr, sizeof jerr);
    if (!m) return vfail(err, errlen, "invalid manifest.json: %s", jerr);
    int rc = 0;
    const char *id = vj_str(vj_get(m, "id"), NULL);
    const char *version = vj_str(vj_get(m, "version"), NULL);
    const char *entry = vj_str(vj_get(m, "entry"), NULL);
    if (!id || !*id) rc = vfail(err, errlen, "manifest.id is required");
    else if (!version || !*version) rc = vfail(err, errlen, "manifest.version is required");
    else if (!entry || !*entry) rc = vfail(err, errlen, "manifest.entry is required");
    if (rc == 0) {
        const VjVal *deps = vj_get(m, "dependencies");
        if (deps && deps->type != VJ_OBJ) {
            rc = vfail(err, errlen, "manifest.dependencies must be an object");
        } else if (deps) {
            for (size_t i = 0; i < deps->nkv && rc == 0; i++) {
                const char *k = deps->keys[i];
                const char *v = vj_str(deps->vals[i], NULL);
                int ok = k && *k;
                for (const char *p = k; ok && *p; p++) {
                    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                          (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '-')) ok = 0;
                }
                if (!ok || !v || !*v) rc = vfail(err, errlen, "invalid dependency: %s", k ? k : "(non-string key)");
            }
        }
    }
    if (rc == 0) {
        if (!name_is_safe(entry)) {
            rc = vfail(err, errlen, "manifest.entry escapes package");
        } else {
            char full[VV_MAX_PATH];
            if (path_join(full, sizeof full, root, entry)) rc = vfail(err, errlen, "manifest.entry too long");
            else if (!path_is_file(full)) rc = vfail(err, errlen, "manifest.entry not found: %s", entry);
        }
    }
    vj_free(m);
    return rc;
}

/* strictStructure + required metadata, i.e. everything the reference's
 * validate() checks before it looks at digests. */
static int check_structure(const char *root, char *err, size_t errlen) {
    if (!path_is_dir(root)) return vfail(err, errlen, "not a directory: %s", root);
    static const char *dirs[4] = { "laws", "assets", "mods", "signatures" };
    for (int i = 0; i < 4; i++) {
        char full[VV_MAX_PATH];
        if (path_join(full, sizeof full, root, dirs[i])) return vfail(err, errlen, "path too long");
        if (!path_is_dir(full)) return vfail(err, errlen, "missing directory: %s", dirs[i]);
    }
    char mpath[VV_MAX_PATH], bpath[VV_MAX_PATH];
    if (path_join(mpath, sizeof mpath, root, "manifest.json")) return vfail(err, errlen, "path too long");
    if (path_join(bpath, sizeof bpath, root, "blueprint.json")) return vfail(err, errlen, "path too long");
    if (!path_is_file(mpath)) return vfail(err, errlen, "missing manifest.json");
    if (!path_is_file(bpath)) return vfail(err, errlen, "missing blueprint.json");
    size_t mlen = 0;
    unsigned char *mt = file_read_all(mpath, &mlen);
    if (!mt) return vfail(err, errlen, "cannot read manifest.json");
    int rc = check_manifest_fields(root, (const char *)mt, err, errlen);
    free(mt);
    return rc;
}

/* ---------------------------- ed25519 --------------------------------- */

static const unsigned char ED25519_SPKI_PREFIX[12] = {
    0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00
};

static int spki_to_pubkey(const unsigned char *der, size_t derlen, unsigned char pub[32], char *err, size_t errlen) {
    if (derlen != 44 || memcmp(der, ED25519_SPKI_PREFIX, 12) != 0)
        return vfail(err, errlen, "invalid ed25519 public key (expected DER SPKI)");
    memcpy(pub, der + 12, 32);
    return 0;
}

/* Verify an {"algorithm","publicKey","signature"} JSON object against the
 * sorted, signatures/-free digest table `signed_files`. */
static int verify_ed25519(const char *json_text, const VList *signed_files, char *err, size_t errlen) {
    char jerr[128];
    VjVal *ed = vj_parse(json_text, jerr, sizeof jerr);
    if (!ed) return vfail(err, errlen, "invalid ed25519 signature: %s", jerr);
    int rc = 0;
    const char *alg = vj_str(vj_get(ed, "algorithm"), NULL);
    const char *pk = vj_str(vj_get(ed, "publicKey"), NULL);
    const char *sig = vj_str(vj_get(ed, "signature"), NULL);
    if (!alg || strcmp(alg, "ed25519") != 0 || !pk || !sig) {
        rc = vfail(err, errlen, "invalid ed25519 signature");
    } else {
        size_t pklen = 0, siglen = 0;
        unsigned char *pkraw = b64_decode(pk, &pklen);
        unsigned char *sigraw = b64_decode(sig, &siglen);
        if (!pkraw || !sigraw || siglen != 64) {
            rc = vfail(err, errlen, "invalid ed25519 signature");
        } else {
            unsigned char pub[32];
            rc = spki_to_pubkey(pkraw, pklen, pub, err, errlen);
            if (rc == 0) {
                Buf payload;
                memset(&payload, 0, sizeof payload);
                if (build_sorted_object(signed_files, 0, &payload)) {
                    rc = vfail(err, errlen, "out of memory");
                } else if (!ed25519_verify(pub, payload.p, payload.len, sigraw)) {
                    rc = vfail(err, errlen, "ed25519 signature verification failed");
                }
                buf_free(&payload);
            }
        }
        free(pkraw);
        free(sigraw);
    }
    vj_free(ed);
    return rc;
}

/* ================================ PACK ================================= */

int vverse_pack(const char *root, const char *out_path, const unsigned char *seed,
                char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    if (!root || !out_path) return vfail(err, errlen, "vverse_pack: missing argument");

    /* (1) the same pre-flight the reference runs before it writes anything */
    if (check_structure(root, err, errlen)) return -1;

    /* (2) file set, sorted - signatures/ is not part of the signed content */
    VList files;
    memset(&files, 0, sizeof files);
    if (walk_collect(root, "", &files, err, errlen)) { vlist_free(&files); return -1; }
    if (files.n == 0) { vlist_free(&files); return vfail(err, errlen, "nothing to pack"); }
    qsort(files.v, files.n, sizeof files.v[0], ventry_cmp);

    /* (3) digests of the raw bytes */
    for (size_t i = 0; i < files.n; i++) {
        char full[VV_MAX_PATH];
        if (path_join(full, sizeof full, root, files.v[i].name)) { vlist_free(&files); return vfail(err, errlen, "path too long"); }
        if (digest_file(full, files.v[i].hex, err, errlen)) { vlist_free(&files); return -1; }
    }

    /* (4) signatures/sha256.json - pretty, keys sorted, trailing newline */
    Buf sha_text, ed_text;
    memset(&sha_text, 0, sizeof sha_text);
    memset(&ed_text, 0, sizeof ed_text);
    if (build_sorted_object(&files, 1, &sha_text) || buf_addc(&sha_text, '\n')) {
        buf_free(&sha_text); buf_free(&ed_text); vlist_free(&files);
        return vfail(err, errlen, "out of memory");
    }

    /* (5) optional ed25519 over the compact form of the same table */
    int have_ed = 0;
    if (seed) {
        Buf payload;
        memset(&payload, 0, sizeof payload);
        unsigned char pub[32], sig[64];
        if (build_sorted_object(&files, 0, &payload)) {
            buf_free(&payload); buf_free(&sha_text); vlist_free(&files);
            return vfail(err, errlen, "out of memory");
        }
        ed25519_pubkey(seed, pub);
        ed25519_sign(seed, payload.p, payload.len, sig);
        buf_free(&payload);

        unsigned char spki[44];
        memcpy(spki, ED25519_SPKI_PREFIX, 12);
        memcpy(spki + 12, pub, 32);
        char *pkb64 = b64_encode(spki, sizeof spki);
        char *sgb64 = b64_encode(sig, sizeof sig);
        if (!pkb64 || !sgb64) {
            free(pkb64); free(sgb64); buf_free(&sha_text); vlist_free(&files);
            return vfail(err, errlen, "out of memory");
        }
        if (buf_adds(&ed_text, "{\n  \"algorithm\": \"ed25519\",\n  \"publicKey\": \"") ||
            buf_adds(&ed_text, pkb64) ||
            buf_adds(&ed_text, "\",\n  \"signature\": \"") ||
            buf_adds(&ed_text, sgb64) ||
            buf_adds(&ed_text, "\"\n}\n")) {
            free(pkb64); free(sgb64); buf_free(&ed_text); buf_free(&sha_text); vlist_free(&files);
            return vfail(err, errlen, "out of memory");
        }
        free(pkb64);
        free(sgb64);
        have_ed = 1;
    }

    /* (6) assemble {"format":"vverse-1","files":{...}} in the reference's key
     *     order: sorted files, then sha256.json, then ed25519.json. */
    Buf pkg;
    memset(&pkg, 0, sizeof pkg);
    int oom = 0;
    oom |= buf_adds(&pkg, "{\"format\":\"vverse-1\",\"files\":{");
    for (size_t i = 0; i < files.n && !oom; i++) {
        char full[VV_MAX_PATH];
        if (path_join(full, sizeof full, root, files.v[i].name)) { oom = 1; break; }
        size_t len = 0;
        unsigned char *raw = file_read_all(full, &len);
        char *b64 = raw ? b64_encode(raw, len) : NULL;
        free(raw);
        if (!b64) { oom = 1; break; }
        if (i) oom |= buf_addc(&pkg, ','); /* JSON.stringify has no leading space */
        oom |= buf_addc(&pkg, '"');
        oom |= buf_add_escaped(&pkg, files.v[i].name);
        oom |= buf_adds(&pkg, "\":\"");
        oom |= buf_adds(&pkg, b64);
        oom |= buf_addc(&pkg, '"');
        free(b64);
    }
    if (!oom) {
        char *b64 = b64_encode(sha_text.p, sha_text.len);
        if (!b64) oom = 1;
        else {
            oom |= buf_adds(&pkg, ",\"signatures/sha256.json\":\"");
            oom |= buf_adds(&pkg, b64);
            oom |= buf_addc(&pkg, '"');
            free(b64);
        }
    }
    if (!oom && have_ed) {
        char *b64 = b64_encode(ed_text.p, ed_text.len);
        if (!b64) oom = 1;
        else {
            oom |= buf_adds(&pkg, ",\"signatures/ed25519.json\":\"");
            oom |= buf_adds(&pkg, b64);
            oom |= buf_addc(&pkg, '"');
            free(b64);
        }
    }
    oom |= buf_adds(&pkg, "}}");

    unsigned char *gz = NULL;
    size_t gzlen = 0;
    if (oom || gzip_pack_stored(pkg.p, pkg.len, &gz, &gzlen)) {
        free(gz); buf_free(&pkg); buf_free(&sha_text); buf_free(&ed_text); vlist_free(&files);
        return vfail(err, errlen, "out of memory");
    }
    int rc = 0;
    if (file_write_all(out_path, gz, gzlen)) rc = vfail(err, errlen, "cannot write package: %s", out_path);

    free(gz);
    buf_free(&pkg);
    buf_free(&sha_text);
    buf_free(&ed_text);
    vlist_free(&files);
    return rc;
}

/* =============================== UNPACK ================================ */

static int obj_lookup(const VjVal *obj, const char *key) {
    if (!obj || obj->type != VJ_OBJ) return -1;
    for (size_t i = 0; i < obj->nkv; i++)
        if (obj->keys[i] && strcmp(obj->keys[i], key) == 0) return (int)i;
    return -1;
}

static int is_signature_path(const char *name) {
    return strncmp(name, "signatures/", 11) == 0;
}

int vverse_unpack(const char *pkg_path, const char *dest_dir, char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    if (!pkg_path || !dest_dir) return vfail(err, errlen, "vverse_unpack: missing argument");

    size_t plen = 0;
    unsigned char *pkg = file_read_all(pkg_path, &plen);
    if (!pkg) return vfail(err, errlen, "cannot read package: %s", pkg_path);

    unsigned char *raw = NULL;
    size_t rawlen = 0;
    int rc = 0;
    if (gzip_unpack(pkg, plen, &raw, &rawlen, err, errlen)) { free(pkg); return -1; }
    free(pkg);

    char *text = (char *)malloc(rawlen + 1);
    VList signed_files;
    memset(&signed_files, 0, sizeof signed_files);
    VjVal *root = NULL, *sig = NULL;
    if (!text) {
        free(raw);
        return vfail(err, errlen, "out of memory");
    }
    memcpy(text, raw, rawlen);
    text[rawlen] = 0;
    free(raw);

    char jerr[128];
    root = vj_parse(text, jerr, sizeof jerr);
    if (!root) { rc = vfail(err, errlen, "invalid vverse package: %s", jerr); goto done; }

    const VjVal *fmt = vj_get(root, "format");
    if (!fmt || fmt->type != VJ_STR || strcmp(fmt->s, "vverse-1") != 0) {
        rc = vfail(err, errlen, "invalid vverse package: unsupported format");
        goto done;
    }
    const VjVal *files = vj_get(root, "files");
    if (!files || files->type != VJ_OBJ || files->nkv == 0) {
        rc = vfail(err, errlen, "invalid vverse package: no files");
        goto done;
    }

    /* required metadata, same three as the reference's preview(); naming the
     * missing one matters, because "missing metadata" is a confusing thing to
     * read when manifest.json is present and only the signature is absent. */
    {
        static const char *const required_meta[] = {
            "manifest.json", "blueprint.json", "signatures/sha256.json",
        };
        for (size_t i = 0; i < sizeof required_meta / sizeof required_meta[0]; i++) {
            if (obj_lookup(files, required_meta[i]) < 0) {
                rc = vfail(err, errlen, "package missing required metadata: %s", required_meta[i]);
                goto done;
            }
        }
    }

    /* --- the sha256 table: every entry except signatures/ is recomputed --- */
    {
        const VjVal *tv = files->vals[obj_lookup(files, "signatures/sha256.json")];
        if (tv->type != VJ_STR) { rc = vfail(err, errlen, "invalid signatures/sha256.json"); goto done; }
        size_t tlen = 0;
        unsigned char *traw = b64_decode(tv->s, &tlen);
        if (!traw) { rc = vfail(err, errlen, "invalid signatures/sha256.json (base64)"); goto done; }
        char *ttext = (char *)malloc(tlen + 1);
        if (!ttext) { free(traw); rc = vfail(err, errlen, "out of memory"); goto done; }
        memcpy(ttext, traw, tlen);
        ttext[tlen] = 0;
        free(traw);
        sig = vj_parse(ttext, jerr, sizeof jerr);
        free(ttext);
        if (!sig || sig->type != VJ_OBJ) { rc = vfail(err, errlen, "invalid signatures/sha256.json: %s", jerr); goto done; }
    }

    for (size_t i = 0; i < sig->nkv; i++) {
        const char *name = sig->keys[i];
        const char *want = vj_str(sig->vals[i], NULL);
        if (!name || !want) { rc = vfail(err, errlen, "invalid signatures/sha256.json"); goto done; }
        if (is_signature_path(name)) continue; /* never covered, never checked */
        int fi = obj_lookup(files, name);
        if (fi < 0) { rc = vfail(err, errlen, "signed file missing: %s", name); goto done; }
        if (files->vals[fi]->type != VJ_STR) { rc = vfail(err, errlen, "signed file missing: %s", name); goto done; }
        size_t blen = 0;
        unsigned char *bytes = b64_decode(files->vals[fi]->s, &blen);
        if (!bytes) { rc = vfail(err, errlen, "invalid base64 for file: %s", name); goto done; }
        Sha256Ctx c;
        sha256_init(&c);
        sha256_update(&c, bytes, blen);
        uint8_t dg[32];
        sha256_final(&c, dg);
        char hex[65];
        sha256_hex_of_digest(dg, hex);
        free(bytes);
        if (strcmp(hex, want) != 0) { rc = vfail(err, errlen, "digest mismatch: %s", name); goto done; }
        if (vlist_push(&signed_files, name)) { rc = vfail(err, errlen, "out of memory"); goto done; }
        memcpy(signed_files.v[signed_files.n - 1].hex, hex, 65);
    }
    qsort(signed_files.v, signed_files.n, sizeof signed_files.v[0], ventry_cmp);

    /* --- reverse direction: nothing may ride along unsigned --- */
    for (size_t i = 0; i < files->nkv; i++) {
        const char *name = files->keys[i];
        if (!name) { rc = vfail(err, errlen, "invalid vverse package: non-string key"); goto done; }
        if (is_signature_path(name)) {
            if (strcmp(name, "signatures/sha256.json") != 0 && strcmp(name, "signatures/ed25519.json") != 0) {
                rc = vfail(err, errlen, "unsigned file: %s", name);
                goto done;
            }
            continue;
        }
        if (!vlist_find(&signed_files, name)) { rc = vfail(err, errlen, "unsigned file: %s", name); goto done; }
    }

    /* --- optional ed25519 --- */
    {
        int ei = obj_lookup(files, "signatures/ed25519.json");
        if (ei >= 0) {
            const VjVal *ev = files->vals[ei];
            if (ev->type != VJ_STR) { rc = vfail(err, errlen, "invalid ed25519 signature"); goto done; }
            size_t elen = 0;
            unsigned char *eraw = b64_decode(ev->s, &elen);
            if (!eraw) { rc = vfail(err, errlen, "invalid ed25519 signature (base64)"); goto done; }
            char *etext = (char *)malloc(elen + 1);
            if (!etext) { free(eraw); rc = vfail(err, errlen, "out of memory"); goto done; }
            memcpy(etext, eraw, elen);
            etext[elen] = 0;
            free(eraw);
            rc = verify_ed25519(etext, &signed_files, err, errlen);
            free(etext);
            if (rc) goto done;
        }
    }

    /* --- extract --- */
    static const char *dirs[4] = { "laws", "assets", "mods", "signatures" };
    for (int i = 0; i < 4; i++) {
        char full[VV_MAX_PATH];
        if (path_join(full, sizeof full, dest_dir, dirs[i]) || im_platform_mkdirs(full)) {
            rc = vfail(err, errlen, "cannot create %s", dirs[i]);
            goto done;
        }
    }
    if (im_platform_mkdirs(dest_dir)) { rc = vfail(err, errlen, "cannot create destination"); goto done; }

    for (size_t i = 0; i < files->nkv; i++) {
        const char *name = files->keys[i];
        if (!name_is_safe(name)) { rc = vfail(err, errlen, "path escapes package: %s", name); goto done; }
        if (files->vals[i]->type != VJ_STR) { rc = vfail(err, errlen, "invalid file entry: %s", name); goto done; }
        size_t blen = 0;
        unsigned char *bytes = b64_decode(files->vals[i]->s, &blen);
        if (!bytes) { rc = vfail(err, errlen, "invalid base64 for file: %s", name); goto done; }
        char full[VV_MAX_PATH];
        if (path_join(full, sizeof full, dest_dir, name)) { free(bytes); rc = vfail(err, errlen, "path too long"); goto done; }
        char parent[VV_MAX_PATH];
        snprintf(parent, sizeof parent, "%s", full);
        char *slash = strrchr(parent, '/');
        if (slash) { *slash = 0; if (*parent && im_platform_mkdirs(parent)) { free(bytes); rc = vfail(err, errlen, "cannot create %s", parent); goto done; } }
        if (file_write_all(full, bytes, blen)) { free(bytes); rc = vfail(err, errlen, "cannot write file: %s", name); goto done; }
        free(bytes);
    }

    /* Same closing step as the reference's unpack(): validate what landed. */
    rc = vverse_validate(dest_dir, err, errlen);

done:
    if (sig) vj_free(sig);
    if (root) vj_free(root);
    vlist_free(&signed_files);
    free(text);
    return rc;
}

/* ============================== VALIDATE =============================== */

int vverse_validate(const char *root, char *err, size_t errlen) {
    if (err && errlen) err[0] = 0;
    if (!root) return vfail(err, errlen, "vverse_validate: missing argument");
    if (check_structure(root, err, errlen)) return -1;

    VList files;
    memset(&files, 0, sizeof files);
    if (walk_collect(root, "", &files, err, errlen)) { vlist_free(&files); return -1; }
    qsort(files.v, files.n, sizeof files.v[0], ventry_cmp);
    for (size_t i = 0; i < files.n; i++) {
        char full[VV_MAX_PATH];
        if (path_join(full, sizeof full, root, files.v[i].name)) { vlist_free(&files); return vfail(err, errlen, "path too long"); }
        if (digest_file(full, files.v[i].hex, err, errlen)) { vlist_free(&files); return -1; }
    }

    char spath[VV_MAX_PATH];
    if (path_join(spath, sizeof spath, root, "signatures/sha256.json")) { vlist_free(&files); return vfail(err, errlen, "path too long"); }
    if (!path_is_file(spath)) { vlist_free(&files); return vfail(err, errlen, "missing signatures/sha256.json"); }

    size_t slen = 0;
    unsigned char *stext = file_read_all(spath, &slen);
    if (!stext) { vlist_free(&files); return vfail(err, errlen, "cannot read signatures/sha256.json"); }
    char jerr[128];
    VjVal *sig = vj_parse((const char *)stext, jerr, sizeof jerr);
    free(stext);
    if (!sig || sig->type != VJ_OBJ) {
        if (sig) vj_free(sig);
        vlist_free(&files);
        return vfail(err, errlen, "invalid signatures/sha256.json: %s", jerr);
    }

    int rc = 0;
    for (size_t i = 0; i < sig->nkv && rc == 0; i++) {
        const char *name = sig->keys[i];
        const char *want = vj_str(sig->vals[i], NULL);
        if (!name || !want) { rc = vfail(err, errlen, "invalid signatures/sha256.json"); break; }
        if (is_signature_path(name)) continue;
        const VEntry *e = vlist_find(&files, name);
        if (!e || strcmp(e->hex, want) != 0) rc = vfail(err, errlen, "digest mismatch: %s", name);
    }
    /* requireCompleteSignature */
    for (size_t i = 0; i < files.n && rc == 0; i++) {
        int found = 0;
        for (size_t j = 0; j < sig->nkv; j++)
            if (sig->keys[j] && strcmp(sig->keys[j], files.v[i].name) == 0) { found = 1; break; }
        if (!found) rc = vfail(err, errlen, "unsigned file: %s", files.v[i].name);
    }

    if (rc == 0) {
        char epath[VV_MAX_PATH];
        if (path_join(epath, sizeof epath, root, "signatures/ed25519.json") == 0 && path_is_file(epath)) {
            size_t elen = 0;
            unsigned char *etext = file_read_all(epath, &elen);
            if (!etext) rc = vfail(err, errlen, "cannot read signatures/ed25519.json");
            else {
                rc = verify_ed25519((const char *)etext, &files, err, errlen);
                free(etext);
            }
        }
    }

    vj_free(sig);
    vlist_free(&files);
    return rc;
}
