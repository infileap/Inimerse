/* verse_dist_mod.c - VDP: Verse Distribution Protocol
 * Distribute verses (games/worlds) like URLs.
 *
 * URI format:  verse://<hub>[:port]/<verse-id>[?law=...]
 *              verse://local/<package-file>            (offline test)
 *
 * Verse package (.vverse) = JSON document:
 *   { "id": "...", "name": "...", "author": "...", "version": 1,
 *     "files": { "<name>": "<base64>", ... },
 *     "hash": "<crc32 of json body>" }
 *
 * API (arg order: first = r_arg(argc-1), last = r_arg(0)):
 *   verse_open(uri)            download/read -> verify -> unpack -> launch game
 *   verse_pack(dir, outfile)   pack a folder into a .vverse package
 *   verse_share(id, hub)       -> "verse://hub/id" shareable link
 *   verse_hub_list(url)        -> verse manifest array from hub
 *   verse_list()               -> locally installed verses (universe/)
 *   verse_remove(id)           uninstall
 *
 * Launch model: a verse is a self-contained game dir (main.im + assets);
 * opening it spawns a fresh inimerse process like opening a URL.
 */
#include "sha256.h"
#include "vverse_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "child_proc.h"
#include <winhttp.h>
#else
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <dirent.h>
#include "platform/http_client.h"
#define _strdup strdup
#endif
#include "vm.h"
#include "platform/platform.h"
#include "platform/dir.h"

extern Value json_parse_value_text(VM *vm, const char *s, int *ok);

/* ---------- arg helpers (io-style; r_arg(0) = LAST arg) ---------- */
static Value r_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static const char *r_str(VM *vm, int i) {
    Value v = r_arg(vm, i);
    return (v.type == VAL_STRING && v.sval) ? v.sval : "";
}
static void r_popn(VM *vm, int n) {
    while (n-- > 0 && vm_cur_sp(vm) >= 0) {
        value_free(&vm_cur_stack(vm)[vm_cur_sp(vm)]);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
}
static void r_push(VM *vm, Value v) {
    if (!vm_push_value(vm, &v)) value_free(&v);
}
static void r_push_int(VM *vm, int n) {
    Value v; v.type = VAL_INT; v.ival = n;  v.sval = NULL;
    r_push(vm, v);
}
static void r_push_str(VM *vm, const char *s) {
    Value v; v.type = VAL_STRING; v.ival = 0;  v.sval = (char*)s;
    r_push(vm, v);
}
static void r_push_nil(VM *vm) {
    Value v; v.type = VAL_NIL; v.ival = 0;  v.sval = NULL;
    r_push(vm, v);
}

/* ---------- base64 ---------- */
static const char B64C[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *b64_encode(const unsigned char *in, int len) {
    char *out = malloc((size_t)(len / 3 + 1) * 4 + 8);
    int o = 0;
    for (int i = 0; i < len; i += 3) {
        int n = len - i; unsigned v = in[i] << 16;
        if (n > 1) v |= in[i + 1] << 8;
        if (n > 2) v |= in[i + 2];
        out[o++] = B64C[(v >> 18) & 63];
        out[o++] = B64C[(v >> 12) & 63];
        out[o++] = (n > 1) ? B64C[(v >> 6) & 63] : '=';
        out[o++] = (n > 2) ? B64C[v & 63] : '=';
    }
    out[o] = 0;
    return out;
}
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static unsigned char *b64_decode(const char *in, int *out_len) {
    int len = (int)strlen(in);
    unsigned char *out = malloc((size_t)len * 3 / 4 + 4);
    int o = 0, buf = 0, bits = 0;
    for (int i = 0; i < len; i++) {
        if (in[i] == '=' || in[i] == '\n' || in[i] == '\r') continue;
        int v = b64_val(in[i]);
        if (v < 0) continue;
        buf = (buf << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)((buf >> bits) & 0xFF); }
    }
    *out_len = o;
    return out;
}

/* ---------- crc32 ---------- */
static unsigned int crc32_buf(const unsigned char *d, int len) {
    unsigned int c = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return c ^ 0xFFFFFFFF;
}

/* ---------- http GET (sync, full body, binary-safe) ---------- */
#ifdef _WIN32
static char *http_get_body(const char *url, int *out_len) {
    *out_len = 0;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return NULL;
    const char *host = url + (strncmp(url, "https://", 8) == 0 ? 8 : 7);
    const char *path = strchr(host, '/');
    char hostbuf[256];
    if (path) {
        int hl = (int)(path - host); if (hl > 255) hl = 255;
        memcpy(hostbuf, host, hl); hostbuf[hl] = 0;
    } else {
        snprintf(hostbuf, sizeof hostbuf, "%s", host);
        path = "/";
    }
    /* strip port for WinHttpConnect */
    char hostname[256]; int port = 80;
    snprintf(hostname, sizeof hostname, "%s", hostbuf);
    char *colon = strchr(hostname, ':');
    if (colon) { *colon = 0; port = atoi(colon + 1); }
    BOOL https = (strncmp(url, "https://", 8) == 0);

    HINTERNET h = WinHttpOpen(L"Inimerse-VDP/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!h) return NULL;
    wchar_t wh[256], wp[1024];
    MultiByteToWideChar(CP_ACP, 0, hostname, -1, wh, 256);
    MultiByteToWideChar(CP_ACP, 0, path, -1, wp, 1024);
    HINTERNET c = WinHttpConnect(h, wh, (INTERNET_PORT)port, 0);
    if (!c) { WinHttpCloseHandle(h); return NULL; }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", wp, NULL, NULL, NULL,
                                     https ? WINHTTP_FLAG_SECURE : 0);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(h); return NULL; }
    char *body = NULL; int blen = 0, bcap = 0;
    if (WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
            if (blen + (int)avail + 1 > bcap) {
                bcap = (bcap == 0 ? 65536 : bcap * 2);
                while (bcap < blen + (int)avail + 1) bcap *= 2;
                body = realloc(body, (size_t)bcap);
            }
            DWORD rd = 0;
            WinHttpReadData(r, body + blen, avail, &rd);
            blen += (int)rd;
        }
    }
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(h);
    if (body) body[blen] = 0;
    *out_len = blen;
    return body;
}

/* ---------- http POST (sync, body) ---------- */
static char *http_post_body(const char *url, const char *postdata, int *out_len) {
    *out_len = 0;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return NULL;
    const char *host = url + (strncmp(url, "https://", 8) == 0 ? 8 : 7);
    const char *path = strchr(host, '/');
    char hostbuf[256];
    if (path) {
        int hl = (int)(path - host); if (hl > 255) hl = 255;
        memcpy(hostbuf, host, hl); hostbuf[hl] = 0;
    } else {
        snprintf(hostbuf, sizeof hostbuf, "%s", host);
        path = "/";
    }
    char hostname[256]; int port = 80;
    snprintf(hostname, sizeof hostname, "%s", hostbuf);
    char *colon = strchr(hostname, ':');
    if (colon) { *colon = 0; port = atoi(colon + 1); }
    BOOL https = (strncmp(url, "https://", 8) == 0);
    HINTERNET h = WinHttpOpen(L"Inimerse-VDP/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!h) return NULL;
    wchar_t wh[256], wp[1024];
    MultiByteToWideChar(CP_ACP, 0, hostname, -1, wh, 256);
    MultiByteToWideChar(CP_ACP, 0, path, -1, wp, 1024);
    HINTERNET c = WinHttpConnect(h, wh, (INTERNET_PORT)port, 0);
    if (!c) { WinHttpCloseHandle(h); return NULL; }
    HINTERNET r = WinHttpOpenRequest(c, L"POST", wp, NULL, NULL, NULL,
                                     https ? WINHTTP_FLAG_SECURE : 0);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(h); return NULL; }
    DWORD plen = postdata ? (DWORD)strlen(postdata) : 0;
    char *body = NULL; int blen = 0, bcap = 0;
    LPCWSTR headers = L"Content-Type: application/octet-stream\r\n";
    if (WinHttpSendRequest(r, headers, -1L, (LPVOID)postdata, plen, plen, 0) &&
        WinHttpReceiveResponse(r, NULL)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
            if (blen + (int)avail + 1 > bcap) {
                bcap = (bcap == 0 ? 65536 : bcap * 2);
                while (bcap < blen + (int)avail + 1) bcap *= 2;
                body = realloc(body, (size_t)bcap);
            }
            DWORD rd = 0;
            WinHttpReadData(r, body + blen, avail, &rd);
            blen += (int)rd;
        }
    }
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(h);
    if (body) body[blen] = 0;
    *out_len = blen;
    return body;
}

#else
/* POSIX: use the portable HTTP client (fixed 8MB response budget) */
#define VDP_HTTP_CAP (8 << 20)
static char *http_get_body(const char *url, int *out_len) {
    char *resp = (char*)malloc(VDP_HTTP_CAP);
    if (!resp) return NULL;
    int status = 0;
    if (im_http_request("GET", url, NULL, resp, VDP_HTTP_CAP, &status) != 0 || status >= 400) {
        free(resp); *out_len = 0; return NULL;
    }
    *out_len = (int)strlen(resp);
    return resp;
}
static char *http_post_body(const char *url, const char *postdata, int *out_len) {
    char *resp = (char*)malloc(VDP_HTTP_CAP);
    if (!resp) return NULL;
    int status = 0;
    if (im_http_request("POST", url, postdata, resp, VDP_HTTP_CAP, &status) != 0) {
        free(resp); *out_len = 0; return NULL;
    }
    /* error responses (4xx/5xx) carry the reason; hand the body back so
       callers can surface it instead of guessing why a publish failed */
    if (status >= 400) fprintf(stderr, "[VDP] POST %s -> HTTP %d: %s\n", url, status, resp);
    *out_len = (int)strlen(resp);
    return resp;
}
#endif

/* ---------- verse home dir ---------- */
static char self_dir[1024] = {0};
static const char *home_dir(void) {
    if (self_dir[0]) return self_dir;
    /* explicit override first (deployment profiles, tests, read-only bins) */
    const char *env = getenv("INIMERSE_HOME");
    if (env && *env) {
        snprintf(self_dir, sizeof self_dir, "%s", env);
        return self_dir;
    }
    if (im_platform_executable_path(self_dir, sizeof self_dir) < 0) self_dir[0] = 0;
    /* strip the executable name: POSIX uses '/', Windows may use either */
    char *s = strrchr(self_dir, '/');
    char *b = strrchr(self_dir, '\\');
    if (b && (!s || b > s)) s = b;
    if (s) *s = 0;
    return self_dir;
}
static void mk_universe_dir(const char *id) {
    char p[1200];
    snprintf(p, sizeof p, "%s/universe", home_dir());
    im_platform_mkdirs(p);
    snprintf(p, sizeof p, "%s/universe/%s", home_dir(), id);
    im_platform_mkdirs(p);
}

/* ---------- content-addressed asset cache (ref://sha256) ---------- */
static char *read_file_buf(const char *path, int *len);  /* defined below */
static const char *cache_dir(void) {
    static char p[1400];
    snprintf(p, sizeof p, "%s/universe/_cache", home_dir());
    return p;
}
static void mk_cache_dir(void) {
    im_platform_mkdirs(cache_dir());
}
static int cache_has(const char *hex) {
    char fp[1500];
    snprintf(fp, sizeof fp, "%s/%s", cache_dir(), hex);
    FILE *f = fopen(fp, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}
static void cache_put(const char *hex, const unsigned char *data, int len) {
    mk_cache_dir();
    char fp[1500];
    snprintf(fp, sizeof fp, "%s/%s", cache_dir(), hex);
    FILE *f = fopen(fp, "wb");
    if (f) { fwrite(data, 1, (size_t)len, f); fclose(f); }
}
static unsigned char *cache_get(const char *hex, int *len) {
    char fp[1500];
    snprintf(fp, sizeof fp, "%s/%s", cache_dir(), hex);
    return read_file_buf(fp, len);
}
static char *read_file_buf(const char *path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t got = (size_t)fread(b, 1, (size_t)n, f); b[got] = 0; fclose(f);
    *len = (int)got;
    return b;
}

/* ---------- 12.3: multi-source auto-update ---------- */
static int verse_ver_cmp(const char *a, const char *b) {
    /* semantic-ish version compare: split '.', numeric per segment, missing = 0 */
    const char *pa = a ? a : "", *pb = b ? b : "";
    while (*pa || *pb) {
        int va = 0, vb = 0;
        while (*pa && *pa != '.') { if (*pa >= '0' && *pa <= '9') va = va * 10 + (*pa - '0'); pa++; }
        while (*pb && *pb != '.') { if (*pb >= '0' && *pb <= '9') vb = vb * 10 + (*pb - '0'); pb++; }
        if (va != vb) return va < vb ? -1 : 1;
        if (*pa) pa++;
        if (*pb) pb++;
    }
    return 0;
}
/* minimal STORE-only zip reader (for .imjar sources; shares layout with main.c) */
static void verse_jar_mkdir_p(const char *path) {
    (void)im_platform_mkdirs(path);
}
static int verse_zip_extract(const char *zipPath, const char *outDir) {
    FILE *f = fopen(zipPath, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    if (fsz < 22) { fclose(f); return -1; }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc((size_t)fsz);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsz, f) != (size_t)fsz) { free(buf); fclose(f); return -1; }
    fclose(f);
    int eocd = -1;
    for (long i = fsz - 22; i >= 0; i--) {
        if (buf[i]==0x50 && buf[i+1]==0x4b && buf[i+2]==0x05 && buf[i+3]==0x06) { eocd = (int)i; break; }
    }
    if (eocd < 0) { free(buf); return -1; }
    uint32_t cdCount = *(uint32_t*)(buf + eocd + 10);
    uint32_t cdSize  = *(uint32_t*)(buf + eocd + 12);
    uint32_t cdOff   = *(uint32_t*)(buf + eocd + 16);
    if (cdOff + cdSize > (uint32_t)fsz) { free(buf); return -1; }
    int extracted = 0;
    uint32_t pos = cdOff;
    for (uint32_t e = 0; e < cdCount; e++) {
        if (pos + 46 > (uint32_t)fsz) break;
        if (!(buf[pos]==0x50 && buf[pos+1]==0x4b && buf[pos+2]==0x01 && buf[pos+3]==0x02)) break;
        uint16_t method = *(uint16_t*)(buf + pos + 10);
        uint32_t csize  = *(uint32_t*)(buf + pos + 20);
        uint16_t nl = *(uint16_t*)(buf + pos + 28);
        uint16_t el = *(uint16_t*)(buf + pos + 30);
        uint16_t cl = *(uint16_t*)(buf + pos + 32);
        uint32_t lho = *(uint32_t*)(buf + pos + 42);
        char name[512];
        size_t ncopy = nl < sizeof(name)-1 ? nl : sizeof(name)-1;
        memcpy(name, buf + pos + 46, ncopy); name[ncopy] = 0;
        pos += 46 + nl + el + cl;
        if (method != 0) { fprintf(stderr, "[VDP] skip compressed entry '%s' (method %d)\n", name, method); continue; }
        if (lho + 30 > (uint32_t)fsz) continue;
        uint16_t lnl = *(uint16_t*)(buf + lho + 26);
        uint16_t lel = *(uint16_t*)(buf + lho + 28);
        uint32_t dataOff = lho + 30 + lnl + lel;
        if (dataOff + csize > (uint32_t)fsz) continue;
        char outPath[1024];
        snprintf(outPath, sizeof outPath, "%s/%s", outDir, name);
#ifdef _WIN32
        for (char *p = outPath; *p; p++) if (*p == '/') *p = '\\';
#endif
        char *slash = strrchr(outPath, '/');
#ifdef _WIN32
        { char *bs = strrchr(outPath, '\\'); if (bs && (!slash || bs > slash)) slash = bs; }
#endif
        if (slash) { char sep = *slash; *slash = 0; verse_jar_mkdir_p(outPath); *slash = sep; }
        FILE *w = fopen(outPath, "wb");
        if (!w) continue;
        fwrite(buf + dataOff, 1, csize, w);
        fclose(w);
        extracted++;
    }
    free(buf);
    return extracted;
}
/* fetch package json by uri (local file / http hub) */
/* ---------- 12.x ed25519 identity / verse signing ---------- */
#include "ed25519.h"
static const char *identity_seed_path(void) {
    static char p[1400];
    snprintf(p, sizeof p, "%s/universe/identity.seed", home_dir());
    return p;
}
static int identity_pubkey(char pubhex[65]) {
    int len = 0;
    char *seed = read_file_buf(identity_seed_path(), &len);
    if (!seed || len < 64) { free(seed); return 0; }
    unsigned char seedb[32], pub[32];
    for (int i = 0; i < 32; i++) {
        int hi = seed[i*2] >= 'a' ? seed[i*2]-'a'+10 : seed[i*2]-'0';
        int lo = seed[i*2+1] >= 'a' ? seed[i*2+1]-'a'+10 : seed[i*2+1]-'0';
        seedb[i] = (unsigned char)((hi << 4) | lo);
    }
    ed25519_pubkey(seedb, pub);
    static const char *hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) { pubhex[i*2] = hx[pub[i] >> 4]; pubhex[i*2+1] = hx[pub[i] & 15]; }
    pubhex[64] = 0;
    free(seed);
    return 1;
}
static int b_verse_identity_new(VM *vm) {
    unsigned char seed[32];
    unsigned char digest[64];
    uint64_t t0 = im_platform_now_ms();
    unsigned long long pcq = (unsigned long long)im_platform_now_ms();
    unsigned char inp[64];
    memcpy(inp, &t0, 8); memcpy(inp + 8, &pcq, 8);
    for (int i = 16; i < 64; i++) inp[i] = (unsigned char)((t0 >> (i % 8)) & 0xff) ^ (unsigned char)(i * 31);
    /* sha512_buf always writes 64 bytes; keep the Ed25519 seed at 32 bytes. */
    sha512_buf(inp, sizeof inp, digest);
    memcpy(seed, digest, sizeof seed);
    mk_universe_dir("_id");
    static const char *hx = "0123456789abcdef";
    char hex[65];
    for (int i = 0; i < 32; i++) { hex[i*2] = hx[seed[i] >> 4]; hex[i*2+1] = hx[seed[i] & 15]; }
    hex[64] = 0;
    {   /* the identity lives under <home>/universe: create it on first use */
        char idir[1400];
        snprintf(idir, sizeof idir, "%s/universe", home_dir());
        (void)im_platform_mkdirs(idir);
    }
    FILE *f = fopen(identity_seed_path(), "wb");
    if (f) { fwrite(hex, 1, 64, f); fclose(f); }
    char pubhex[65];
    identity_pubkey(pubhex);
    int aidx = vm_array_new(vm);
    Value v; v.type = VAL_STRING; v.ival = 0;  v.sval = _strdup(pubhex);
    vm_array_push(vm, aidx, &v);
    v.ival = 0; v.sval = _strdup(hex);
    vm_array_push(vm, aidx, &v);
    Value rv; rv.type = VAL_ARRAY; rv.ival = aidx + 1;  rv.sval = NULL;
    r_push(vm, rv);
    return 1;
}
static int b_verse_identity_pubkey(VM *vm) {
    char pubhex[65];
    if (!identity_pubkey(pubhex)) { r_push_str(vm, _strdup("")); return 1; }
    r_push_str(vm, _strdup(pubhex));
    return 1;
}
static int b_verse_sign(VM *vm) {
    int argc = vm->cur_argc;
    char *data = _strdup(r_str(vm, argc - 1));
    if (!data) data = _strdup("");
    r_popn(vm, argc);
    int len = 0;
    char *seed = read_file_buf(identity_seed_path(), &len);
    if (!seed || len < 64) { free(seed); free(data); r_push_str(vm, _strdup("")); return 1; }
    unsigned char seedb[32], sig[64];
    for (int i = 0; i < 32; i++) {
        int hi = seed[i*2] >= 'a' ? seed[i*2]-'a'+10 : seed[i*2]-'0';
        int lo = seed[i*2+1] >= 'a' ? seed[i*2+1]-'a'+10 : seed[i*2+1]-'0';
        seedb[i] = (unsigned char)((hi << 4) | lo);
    }
    ed25519_sign(seedb, (const unsigned char*)data, strlen(data), sig);
    free(seed);
    free(data);
    static const char *hx = "0123456789abcdef";
    char hex[129];
    for (int i = 0; i < 64; i++) { hex[i*2] = hx[sig[i] >> 4]; hex[i*2+1] = hx[sig[i] & 15]; }
    hex[128] = 0;
    r_push_str(vm, _strdup(hex));
    return 1;
}
static int b_verse_verify(VM *vm) {
    int argc = vm->cur_argc;
    char *data = _strdup(r_str(vm, argc - 1));
    char *sighex = _strdup(r_str(vm, argc - 2));
    char *pubhex = _strdup(r_str(vm, argc - 3));
    if (!data) data = _strdup("");
    if (!sighex) sighex = _strdup("");
    if (!pubhex) pubhex = _strdup("");
    r_popn(vm, argc);
    if (strlen(pubhex) != 64 || strlen(sighex) != 128) { free(data); free(sighex); free(pubhex); r_push_int(vm, 0); return 1; }
    unsigned char pub[32], sig[64];
    for (int i = 0; i < 32; i++) {
        int hi = pubhex[i*2] >= 'a' ? pubhex[i*2]-'a'+10 : pubhex[i*2]-'0';
        int lo = pubhex[i*2+1] >= 'a' ? pubhex[i*2+1]-'a'+10 : pubhex[i*2+1]-'0';
        pub[i] = (unsigned char)((hi << 4) | lo);
    }
    for (int i = 0; i < 64; i++) {
        int hi = sighex[i*2] >= 'a' ? sighex[i*2]-'a'+10 : sighex[i*2]-'0';
        int lo = sighex[i*2+1] >= 'a' ? sighex[i*2+1]-'a'+10 : sighex[i*2+1]-'0';
        sig[i] = (unsigned char)((hi << 4) | lo);
    }
    int ok = ed25519_verify(pub, (const unsigned char*)data, strlen(data), sig);
    free(data); free(sighex); free(pubhex);
    r_push_int(vm, ok);
    return 1;
}

/* ---------- 12.2 global hub list (persisted to universe/hubs.json) ---------- */
#define MAX_HUBS 16
static char g_hubs[MAX_HUBS][512];
static int g_hub_count = -1; /* -1 = not loaded yet */
static void hubs_load(void) {
    if (g_hub_count >= 0) return;
    g_hub_count = 0;
    char hp[1200];
    snprintf(hp, sizeof hp, "%s/universe/hubs.json", home_dir());
    int len = 0;
    char *j = read_file_buf(hp, &len);
    if (j) {
        const char *p = j;
        while (*p && g_hub_count < MAX_HUBS) {
            const char *q = strchr(p, '"');
            if (!q) break;
            const char *e = strchr(q + 1, '"');
            if (!e) break;
            int nl = (int)(e - q - 1);
            if (nl > 0 && nl < 511) {
                memcpy(g_hubs[g_hub_count], q + 1, (size_t)nl);
                g_hubs[g_hub_count][nl] = 0;
                g_hub_count++;
            }
            p = e + 1;
        }
        free(j);
    }
}
static void hubs_save(void) {
    char hp[1200];
    snprintf(hp, sizeof hp, "%s/universe/hubs.json", home_dir());
    FILE *f = fopen(hp, "wb");
    if (!f) return;
    fputs("[", f);
    for (int i = 0; i < g_hub_count; i++)
        fprintf(f, "%s\"%s\"", i ? "," : "", g_hubs[i]);
    fputs("]", f);
    fclose(f);
}

static char *verse_udp_fetch(const char *host, int port, const char *id, int *out_len);
static char *verse_fetch(const char *uri, int *out_len) {
    *out_len = 0;
    if (strncmp(uri, "verse://local/", 14) == 0)
        return read_file_buf(uri + 14, out_len);
    if (strncmp(uri, "verse://", 8) == 0) {
        const char *rest = uri + 8;
        char url[1200];
        char hp[512];
        
        if (strncmp(rest, "udp://", 6) == 0) {
            const char *r2 = rest + 6;
            const char *s2 = strchr(r2, 47);
            if (s2) {
                char hp[512];
                int hl2 = (int)(s2 - r2); if (hl2 > 511) hl2 = 511;
                memcpy(hp, r2, (size_t)hl2); hp[hl2] = 0;
                char *cp = strchr(hp, 58);
                int pt = cp ? atoi(cp + 1) : 11460;
                if (cp) *cp = 0;
                return verse_udp_fetch(hp, pt, s2 + 1, out_len);
            }
        }const char *sl = strchr(rest, 47);
        if (!sl) {
            /* bare id: resolve through the global hub list (12.2) */
            hubs_load();
            for (int hi = 0; hi < g_hub_count; hi++) {
                char u2[1200];
                if (strncmp(g_hubs[hi], "http://", 7) != 0 && strncmp(g_hubs[hi], "https://", 8) != 0)
                    snprintf(u2, sizeof u2, "http://%s/v/%s", g_hubs[hi], rest);
                else
                    snprintf(u2, sizeof u2, "%s/v/%s", g_hubs[hi], rest);
                fprintf(stderr, "[VDP] resolve %s via hub %s\n", rest, g_hubs[hi]);
                char *bb = http_get_body(u2, out_len);
                if (bb) return bb;
            }
            fprintf(stderr, "[VDP] no reachable hub for %s\n", rest);
            return NULL;
        }        int hl = (int)(sl - rest); if (hl > 511) hl = 511;
        memcpy(hp, rest, (size_t)hl); hp[hl] = 0;
        snprintf(url, sizeof url, "http://%s/v/%s", hp, sl + 1);
        return http_get_body(url, out_len);
    }
    fprintf(stderr, "[VDP] unsupported source: %s\n", uri);
    return NULL;
}
typedef struct {
    const char *id, *hash, *sha256hex, *mainf, *version, *min_version, *publisher, *signature;
} VerseMeta;
static int verse_parse_meta(VM *vm, Value pkg, VerseMeta *m) {
    memset(m, 0, sizeof *m);
    m->mainf = "main.im"; m->version = "1.0.0";
    if (pkg.type != VAL_DICT) return 0;
    ArrayObj *a = vm_pool_slot(vm, pkg.ival - 1);
    if (!a) return 0;
    for (int i = 0; i + 1 < a->count; i += 2) {
        Value *k = &a->items[i], *v = &a->items[i + 1];
        if (k->type != VAL_STRING || v->type != VAL_STRING) continue;
        if (strcmp(k->sval, "id") == 0) m->id = v->sval;
        else if (strcmp(k->sval, "hash") == 0) m->hash = v->sval;
        else if (strcmp(k->sval, "sha256") == 0) m->sha256hex = v->sval;
        else if (strcmp(k->sval, "main") == 0) m->mainf = v->sval;
        else if (strcmp(k->sval, "version") == 0) m->version = v->sval;
        else if (strcmp(k->sval, "min_version") == 0) m->min_version = v->sval;
        else if (strcmp(k->sval, "publisher") == 0) m->publisher = v->sval;
        else if (strcmp(k->sval, "signature") == 0) m->signature = v->sval;
    }
    return m->id != NULL;
}
/* verify crc32 + sha256 over embedded b64 payloads */
static int verse_verify(VM *vm, Value pkg, const VerseMeta *m) {
    ArrayObj *a = vm_pool_slot(vm, pkg.ival - 1);
    if (!a) return 0;
    char *b64all = malloc(16384); int ballen = 0, bacap = 16384;
    for (int i = 0; i + 1 < a->count; i += 2) {
        Value *k = &a->items[i], *v = &a->items[i + 1];
        if (k->type == VAL_STRING && v->type == VAL_DICT &&
            (strcmp(k->sval, "files") == 0 || strcmp(k->sval, "file") == 0)) {
            ArrayObj *fa = vm_pool_slot(vm, v->ival - 1);
            if (!fa) continue;
            for (int j = 0; j + 1 < fa->count; j += 2) {
                Value *fv = &fa->items[j + 1];
                if (fv->type != VAL_STRING) continue;
                if (strncmp(fv->sval, "ref://sha256:", 13) == 0) continue;
                int nl = (int)strlen(fv->sval);
                while (ballen + nl + 1 > bacap) { bacap *= 2; b64all = realloc(b64all, (size_t)bacap); }
                memcpy(b64all + ballen, fv->sval, (size_t)nl); ballen += nl;
            }
        }
    }
    unsigned int crc = crc32_buf((unsigned char*)b64all, ballen);
    char crcbuf[16]; snprintf(crcbuf, sizeof crcbuf, "%08x", crc);
    if (m->hash && strcmp(m->hash, crcbuf) != 0) {
        fprintf(stderr, "[VDP] hash mismatch (got %s, want %s)\n", crcbuf, m->hash);
        free(b64all); return 0;
    }
    if (m->sha256hex && strlen(m->sha256hex) == 64) {
        char calc[65]; sha256_hex(b64all, (size_t)ballen, calc);
        if (strcmp(calc, m->sha256hex) != 0) {
            fprintf(stderr, "[VDP] sha256 mismatch (got %s, want %s)\n", calc, m->sha256hex);
            free(b64all); return 0;
        }
    }
    /* ed25519 signature check (when publisher/signature present) */
    if (m->publisher && m->signature && strlen(m->publisher) == 64 && strlen(m->signature) == 128) {
        unsigned char pub[32], sig[64];
        for (int si = 0; si < 32; si++) {
            int hi = m->publisher[si*2] >= 97 ? m->publisher[si*2]-97+10 : m->publisher[si*2]-48;
            int lo = m->publisher[si*2+1] >= 97 ? m->publisher[si*2+1]-97+10 : m->publisher[si*2+1]-48;
            pub[si] = (unsigned char)((hi << 4) | lo);
        }
        for (int si = 0; si < 64; si++) {
            int hi = m->signature[si*2] >= 97 ? m->signature[si*2]-97+10 : m->signature[si*2]-48;
            int lo = m->signature[si*2+1] >= 97 ? m->signature[si*2+1]-97+10 : m->signature[si*2+1]-48;
            sig[si] = (unsigned char)((hi << 4) | lo);
        }


        if (!ed25519_verify(pub, (const unsigned char*)b64all, (size_t)ballen, sig)) {
            fprintf(stderr, "[VDP] signature mismatch (publisher %s) - package rejected\n", m->publisher);
            free(b64all); return 0;
        }
    }
    free(b64all);
    return 1;
}
/* unpack files to universe/<id>/; save manifest for later updates */
static int verse_unpack(VM *vm, Value pkg, const char *id, const char *mainf, const char *pkg_json_save) {
    (void)mainf;
    ArrayObj *a = vm_pool_slot(vm, pkg.ival - 1);
    if (!a) return 0;
    mk_universe_dir(id);
    char base[1200];
    snprintf(base, sizeof base, "%s/universe/%s/", home_dir(), id);
    int any = 0;
    for (int i = 0; i + 1 < a->count; i += 2) {
        Value *k = &a->items[i], *v = &a->items[i + 1];
        if (k->type == VAL_STRING && v->type == VAL_DICT &&
            (strcmp(k->sval, "files") == 0 || strcmp(k->sval, "file") == 0)) {
            ArrayObj *fa = vm_pool_slot(vm, v->ival - 1);
            if (!fa) continue;
            for (int j = 0; j + 1 < fa->count; j += 2) {
                Value *fk = &fa->items[j], *fv = &fa->items[j + 1];
                if (fk->type != VAL_STRING || fv->type != VAL_STRING) continue;
                if (strchr(fk->sval, '\\') || strchr(fk->sval, '/') || strstr(fk->sval, "..")) continue;
                int dlen = 0;
                unsigned char *data = NULL;
                if (strncmp(fv->sval, "ref://sha256:", 13) == 0) {
                    const char *hex = fv->sval + 13;
                    data = cache_get(hex, &dlen);
                    if (data) {
                        char calc[65]; sha256_hex(data, (size_t)dlen, calc);
                        if (strcmp(calc, hex) != 0) { fprintf(stderr, "[VDP] cache %s failed content check\n", hex); free(data); data = NULL; }
                    } else fprintf(stderr, "[VDP] missing cached asset %s\n", hex);
                } else {
                    data = b64_decode(fv->sval, &dlen);
                    if (data) { char hex[65]; sha256_hex(data, (size_t)dlen, hex); cache_put(hex, data, dlen); }
                }
                if (!data) continue;
                char fp[1400];
                snprintf(fp, sizeof fp, "%s%s", base, fk->sval);
                FILE *f = fopen(fp, "wb");
                if (f) { fwrite(data, 1, (size_t)dlen, f); fclose(f); any = 1; }
                free(data);
            }
        }
    }
    if (!any) { fprintf(stderr, "[VDP] package has no files\n"); return 0; }
    if (pkg_json_save) {
        char mp[1400];
        snprintf(mp, sizeof mp, "%sverse.manifest", base);
        FILE *f = fopen(mp, "wb");
        if (f) { fwrite(pkg_json_save, 1, strlen(pkg_json_save), f); fclose(f); }
    }
    return 1;
}
/* parse "sources" array from a parsed package dict; returns malloc'd array (caller frees) */
static char **verse_parse_sources(VM *vm, Value pkg, int *out_n) {
    *out_n = 0;
    if (pkg.type != VAL_DICT) return NULL;
    ArrayObj *a = vm_pool_slot(vm, pkg.ival - 1);
    if (!a) return NULL;
    for (int i = 0; i + 1 < a->count; i += 2) {
        Value *k = &a->items[i], *v = &a->items[i + 1];
        if (k->type == VAL_STRING && strcmp(k->sval, "sources") == 0 && v->type == VAL_ARRAY) {
            ArrayObj *sa = vm_pool_slot(vm, v->ival - 1);
            if (!sa) return NULL;
            int n = 0;
            for (int j = 0; j < sa->count; j++)
                if (sa->items[j].type == VAL_STRING && sa->items[j].sval) n++;
            char **arr = n > 0 ? malloc(sizeof(char*) * n) : NULL;
            int k2 = 0;
            for (int j = 0; j < sa->count; j++)
                if (sa->items[j].type == VAL_STRING && sa->items[j].sval)
                    arr[k2++] = _strdup(sa->items[j].sval);
            *out_n = n;
            return arr;
        }
    }
    return NULL;
}
/* update: check every source (override sources, or those saved in the local
   manifest), verify sha256, compare versions, honor min_version; unpack newest. */
static int verse_do_update(VM *vm, const char *id, char **srcs, int nsrcs) {
    char mp[1400];
    snprintf(mp, sizeof mp, "%s/universe/%s/verse.manifest", home_dir(), id);
    int llen = 0;
    char *local = read_file_buf(mp, &llen);
    char localVer[64] = "0.5.0";
    char **localSrcs = NULL; int nlocalSrcs = 0;
    if (local) {
        int ok = 0;
        Value lp = json_parse_value_text(vm, local, &ok);
        if (ok && lp.type == VAL_DICT) {
            VerseMeta lm;
            if (verse_parse_meta(vm, lp, &lm) && lm.version)
                snprintf(localVer, sizeof localVer, "%s", lm.version);
            localSrcs = verse_parse_sources(vm, lp, &nlocalSrcs);
        }
        free(local);
    } else {
        fprintf(stderr, "[VDP] update: '%s' not installed (no local manifest)\n", id);
        return 0;
    }
    char **slist = srcs; int nlist = nsrcs;
    int freeList = 0;
    if (nlist == 0 && nlocalSrcs > 0) { slist = localSrcs; nlist = nlocalSrcs; freeList = 1; }
    if (nlist == 0) { fprintf(stderr, "[VDP] update: no sources for '%s'\n", id); return 0; }
    int rc = 0;
    for (int si = 0; si < nlist; si++) {
        const char *src = slist[si];
        fprintf(stderr, "[VDP] update: checking source %s\n", src);
        char *pkg = NULL; int plen = 0;
        char *tmpdir = NULL;
        if (strlen(src) > 6 && strcmp(src + strlen(src) - 6, ".imjar") == 0) {
            if (strncmp(src, "verse://local/", 14) != 0) {
                fprintf(stderr, "[VDP] .imjar source must be local for now: %s\n", src);
                continue;
            }
            char tmp[1200];
            snprintf(tmp, sizeof tmp, "%s/_upd_tmp", home_dir());
            verse_zip_extract(src + 14, tmp);
            char mp2[1400];
            snprintf(mp2, sizeof mp2, "%s/verse.manifest", tmp);
            pkg = read_file_buf(mp2, &plen);
            tmpdir = _strdup(tmp);
            if (!pkg) { fprintf(stderr, "[VDP] .imjar has no verse.manifest\n"); continue; }
        } else {
            pkg = verse_fetch(src, &plen);
            if (!pkg) { fprintf(stderr, "[VDP] fetch failed: %s\n", src); continue; }
        }
        int ok = 0;
        Value pv = json_parse_value_text(vm, pkg, &ok);
        if (!ok || pv.type != VAL_DICT) { fprintf(stderr, "[VDP] bad package from %s\n", src); free(pkg); continue; }
        VerseMeta m;
        if (!verse_parse_meta(vm, pv, &m) || !m.id) { fprintf(stderr, "[VDP] package missing id\n"); free(pkg); continue; }
        if (strcmp(m.id, id) != 0) { fprintf(stderr, "[VDP] id mismatch (%s != %s)\n", m.id, id); free(pkg); continue; }
        if (m.min_version && m.min_version[0]) {
            if (verse_ver_cmp(INFIVERSE_VERSION, m.min_version) < 0) {
                fprintf(stderr, "[VDP] '%s' needs infiverse >= %s (current %s) - skipped\n", id, m.min_version, INFIVERSE_VERSION);
                free(pkg); continue;
            }
        }
        if (verse_ver_cmp(m.version ? m.version : "1.0.0", localVer) <= 0) {
            fprintf(stderr, "[VDP] '%s' version %s not newer than local %s\n", id, m.version, localVer);
            free(pkg); continue;
        }
        if (!verse_verify(vm, pv, &m)) { fprintf(stderr, "[VDP] verify failed from %s\n", src); free(pkg); continue; }
        if (tmpdir) {
            mk_universe_dir(id);
            char base[1200];
            snprintf(base, sizeof base, "%s/universe/%s/", home_dir(), id);
            ImDir *dir = im_dir_open(tmpdir);
            char entry[1024]; int entry_is_dir = 0;
            if (dir) {
                while (im_dir_next_ex(dir, entry, sizeof entry, &entry_is_dir)) {
                    if (entry_is_dir) continue;
                    if (strstr(entry, "manifest") && strcmp(entry, "verse.manifest") != 0) continue;
                    char sf[1400], df[1400];
                    snprintf(sf, sizeof sf, "%s/%s", tmpdir, entry);
                    snprintf(df, sizeof df, "%s%s", base, entry);
                    int slen = 0; char *raw = read_file_buf(sf, &slen);
                    if (raw) { FILE *w = fopen(df, "wb"); if (w) { fwrite(raw, 1, (size_t)slen, w); fclose(w); } free(raw); }
                }
                im_dir_close(dir);
            }
            char mp3[1400];
            snprintf(mp3, sizeof mp3, "%sverse.manifest", base);
            FILE *w = fopen(mp3, "wb");
            if (w) { fwrite(pkg, 1, strlen(pkg), w); fclose(w); }
            fprintf(stderr, "[VDP] updated '%s' to %s (from %s)\n", id, m.version, src);
            rc = 1;
            free(pkg);
            break;
        }
        if (!verse_unpack(vm, pv, id, m.mainf, pkg)) { fprintf(stderr, "[VDP] unpack failed from %s\n", src); free(pkg); continue; }
        fprintf(stderr, "[VDP] updated '%s' to %s (from %s)\n", id, m.version, src);
        rc = 1;
        free(pkg);
        break;
    }
    if (freeList) {
        for (int i = 0; i < nlocalSrcs; i++) free(localSrcs[i]);
        free(localSrcs);
    }
    if (!rc) fprintf(stderr, "[VDP] update: no usable source for '%s'\n", id);
    return rc;
}
static int b_verse_update(VM *vm) {
    int argc = vm->cur_argc;
    char *id = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    int nsrcs = argc - 1 > 0 ? argc - 1 : 0;
    char **srcs = nsrcs > 0 ? malloc(sizeof(char*) * nsrcs) : NULL;
    for (int i = 0; i < nsrcs; i++)
        srcs[i] = _strdup(r_str(vm, argc - 2 - i) ? r_str(vm, argc - 2 - i) : "");
    r_popn(vm, argc);
    int rc = verse_do_update(vm, id, srcs, nsrcs);
    r_push_int(vm, rc);
    for (int i = 0; i < nsrcs; i++) free(srcs[i]);
    free(srcs);
    free(id);
    return 1;
}

static int b_verse_hub_add(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    hubs_load();
    for (int i = 0; i < g_hub_count; i++)
        if (strcmp(g_hubs[i], uri) == 0) { r_push_int(vm, 1); free(uri); return 1; }
    if (g_hub_count < MAX_HUBS && uri[0]) {
        snprintf(g_hubs[g_hub_count], sizeof g_hubs[0], "%s", uri);
        g_hub_count++;
        hubs_save();
        r_push_int(vm, 1);
    } else r_push_int(vm, 0);
    free(uri);
    return 1;
}
static int b_verse_hub_remove(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    hubs_load();
    int rc = 0;
    for (int i = 0; i < g_hub_count; i++) {
        if (strcmp(g_hubs[i], uri) == 0) {
            for (int j = i; j < g_hub_count - 1; j++) snprintf(g_hubs[j], sizeof g_hubs[0], "%s", g_hubs[j + 1]);
            g_hub_count--;
            hubs_save();
            rc = 1;
            break;
        }
    }
    r_push_int(vm, rc);
    free(uri);
    return 1;
}
static int b_verse_hubs(VM *vm) {
    hubs_load();
    int aidx = vm_array_new(vm);
    for (int i = 0; i < g_hub_count; i++) {
        Value v; v.type = VAL_STRING; v.ival = 1;  v.sval = g_hubs[i];
        vm_array_push(vm, aidx, &v);
    }
    Value rv; rv.type = VAL_ARRAY; rv.ival = aidx + 1;  rv.sval = NULL;
    r_push(vm, rv);
    return 1;
}
static int b_verse_hub_ping(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/ping", uri);
    else
        snprintf(url, sizeof url, "%s/ping", uri);
    uint64_t t0 = im_platform_now_ms();
    int len = 0;
    char *b = http_get_body(url, &len);
    uint64_t dt = im_platform_now_ms() - t0;
    int ok = (b && len >= 4 && strncmp(b, "pong", 4) == 0);
    free(b);
    r_push_int(vm, ok ? (int)dt : 0);
    free(uri);
    return 1;
}
static int b_verse_public_ip(VM *vm) {
    char ip[128] = "";
    char hn[256] = "";
    gethostname(hn, sizeof hn - 1);
    struct addrinfo hints; memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(hn, NULL, &hints, &res) == 0) {
        for (struct addrinfo *p = res; p; p = p->ai_next) {
            struct sockaddr_in *sa = (struct sockaddr_in*)p->ai_addr;
            char tmp[64];
            inet_ntop(AF_INET, &sa->sin_addr, tmp, sizeof tmp);
            if (strncmp(tmp, "127.", 4) != 0 && strncmp(tmp, "169.254.", 8) != 0) {
                snprintf(ip, sizeof ip, "%s", tmp);
                break;
            }
        }
        freeaddrinfo(res);
    }
    if (!ip[0]) snprintf(ip, sizeof ip, "127.0.0.1");
    r_push_str(vm, _strdup(ip));
    return 1;
}
static int b_verse_publish(VM *vm) {
    int argc = vm->cur_argc;
    char *id = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *hub = _strdup(argc >= 2 ? (r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "") : "");
    r_popn(vm, argc);
    char mp[1400];
    snprintf(mp, sizeof mp, "%s/universe/%s/verse.manifest", home_dir(), id);
    int len = 0;
    char *m = read_file_buf(mp, &len);
    if (!m) {
        fprintf(stderr, "[VDP] publish: '%s' not installed (no verse.manifest)\n", id);
        r_push_int(vm, 0);
        free(id); free(hub);
        return 1;
    }
    char url[1300];
    if (strncmp(hub, "http://", 7) != 0 && strncmp(hub, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/publish?id=%s", hub, id);
    else
        snprintf(url, sizeof url, "%s/publish?id=%s", hub, id);
    int rlen = 0;
    char *resp = http_post_body(url, m, &rlen);
    int ok = (resp && strstr(resp, "\"ok\":1") != NULL);
    fprintf(stderr, "[VDP] publish '%s' to %s -> %s\n", id, hub, ok ? "ok" : "failed");
    free(resp);
    free(m);
    r_push_int(vm, ok);
    free(id); free(hub);
    return 1;
}

/* address parser: verse://<pubkey64hex>/<id>[@version][#sha256] or host:port/local */
static void verse_parse_uri(const char *uri, char *fetch_uri, int fus,
                            char *pub, int pubsz, char *ver, int versz,
                            char *hash, int hashsz) {
    pub[0] = ver[0] = hash[0] = 0;
    snprintf(fetch_uri, fus, "%s", uri);
    if (strncmp(uri, "verse://", 8) != 0) return;
    const char *u = uri + 8;
    const char *sl = strchr(u, '/');
    if (!sl) return; /* bare id: hub list resolves it, no pinning */
    int hl = (int)(sl - u);
    if (hl == 64) {
        /* pubkey form */
        memcpy(pub, u, 64); pub[64] = 0;
        char idcore[256];
        snprintf(idcore, sizeof idcore, "%s", sl + 1);
        char *hs = strchr(idcore, '#');
        if (hs) {
            snprintf(hash, hashsz, "%s", hs + 1);
            *hs = 0;
        }
        char *at = strchr(idcore, '@');
        if (at) {
            snprintf(ver, versz, "%s", at + 1);
            *at = 0;
        }
        snprintf(fetch_uri, fus, "verse://%s", idcore);
    } else if (strncmp(u, "local/", 6) != 0) {
        /* host:port form - also parse @/# in the id segment */
        char idcore[256];
        snprintf(idcore, sizeof idcore, "%s", sl + 1);
        char *hs = strchr(idcore, '#');
        if (hs) {
            snprintf(hash, hashsz, "%s", hs + 1);
            *hs = 0;
        }
        char *at = strchr(idcore, '@');
        if (at) {
            snprintf(ver, versz, "%s", at + 1);
            *at = 0;
        }
        if (hs || at) {
            char hp[512];
            int h2 = (int)(sl - u); if (h2 > 511) h2 = 511;
            memcpy(hp, u, (size_t)h2); hp[h2] = 0;
            snprintf(fetch_uri, fus, "verse://%s/%s", hp, idcore);
        }
    }
}
/* ---------- verse_open(uri): download/verify/unpack/launch ---------- */
/* fire-and-forget launch of a verse's entry point.  `base` is the directory
 * the caller unpacked into, trailing separator already included. */
static int verse_launch(const char *base, const char *entry) {
    char cmd[1600];
    int ok = 0;
#ifdef _WIN32
    snprintf(cmd, sizeof cmd, "cmd /c start \"\" \"%s\\inimerse.exe\" \"%s%s\"", home_dir(), base, entry);
    DWORD cpid = child_proc_spawn(cmd, "verse", 0);
    ok = cpid != 0;
#else
    snprintf(cmd, sizeof cmd, "cd \"%s\" && nohup \"%s/inimerse\" \"%s%s\" >/dev/null 2>&1 &", home_dir(), home_dir(), base, entry);
    ok = system(cmd) == 0; /* fire-and-forget launch */
#endif
    if (!ok) fprintf(stderr, "[VDP] launch failed: %s\n", cmd);
    return ok;
}

/* ---------- verse_open on a real `.vverse` (gzip container) ----------
 * A real verse keeps its id/version/entry in its OWN manifest.json, not in the
 * container, so none of the legacy meta checks in do_open() apply to it.  The
 * artifact is verified by src/common/vverse_pack.c instead -- format tag,
 * digest table, every packaged file covered, optional ed25519 -- which is the
 * same code that wrote it, so writer and reader cannot drift apart again.
 * The tree lands in universe/<name>/ where <name> is the package file's
 * basename, and the entry point is read from the manifest that just landed. */
static int do_open_vverse(VM *vm, const char *fetch_uri, const void *pkg, size_t pkg_len) {
    const char *tail = fetch_uri;
    if (strncmp(tail, "verse://local/", 14) == 0) tail += 14;
    const char *slash = strrchr(tail, '/');
    if (slash && slash[1]) tail = slash + 1;
    char name[256];
    snprintf(name, sizeof name, "%.255s", tail);
    size_t nlen = strlen(name);
    if (nlen > 7 && strcmp(name + nlen - 7, ".vverse") == 0) name[nlen - 7] = 0;
    if (!name[0]) snprintf(name, sizeof name, "package");

    char dest[1200];
    snprintf(dest, sizeof dest, "%s/universe/%s", home_dir(), name);
    char err[512];
    if (vverse_unpack_mem(pkg, pkg_len, dest, err, sizeof err)) {
        fprintf(stderr, "[VDP] %s\n", err[0] ? err : "package rejected");
        return 0;
    }

    char mpath[1300];
    snprintf(mpath, sizeof mpath, "%s/manifest.json", dest);
    int mlen = 0;
    char *text = read_file_buf(mpath, &mlen);
    if (!text || mlen <= 0) { fprintf(stderr, "[VDP] package has no manifest.json\n"); free(text); return 0; }
    int jok = 0;
    Value man = json_parse_value_text(vm, text, &jok);
    free(text);
    char entry[512] = "";
    if (jok && man.type == VAL_DICT) {
        ArrayObj *a = vm_pool_slot(vm, man.ival - 1);
        for (int i = 0; a && i + 1 < a->count; i += 2) {
            Value *k = &a->items[i], *v = &a->items[i + 1];
            if (k->type == VAL_STRING && v->type == VAL_STRING && strcmp(k->sval, "entry") == 0) {
                snprintf(entry, sizeof entry, "%s", v->sval);
                break;
            }
        }
    }
    /* vverse_unpack_mem() already ran vverse_validate(), which requires
     * manifest.json to declare a safe relative `entry` that exists -- so this
     * is a guard, not a second validation. */
    if (!entry[0]) { fprintf(stderr, "[VDP] package manifest declares no entry\n"); return 0; }

    char base[1300];
#ifdef _WIN32
    snprintf(base, sizeof base, "%s\\", dest);
#else
    snprintf(base, sizeof base, "%s/", dest);
#endif
    return verse_launch(base, entry);
}

static int do_open(VM *vm, const char *uri) {
    char fetch_uri[1200], pub[65], ver[64], hash[65];
    verse_parse_uri(uri, fetch_uri, sizeof fetch_uri, pub, sizeof pub, ver, sizeof ver, hash, sizeof hash);
    int pkg_len = 0;
    char *pkg_json = verse_fetch(fetch_uri, &pkg_len);
    if (!pkg_json) { fprintf(stderr, "[VDP] download failed: %s\n", fetch_uri); return 0; }
    /* Two containers, told apart by their first bytes and nothing else: a real
     * `.vverse` is a gzip member (1f 8b), the legacy one is bare JSON ('{').
     * The two cannot collide, so the legacy path below is untouched. */
    if (pkg_len >= 2 && (unsigned char)pkg_json[0] == 0x1f && (unsigned char)pkg_json[1] == 0x8b) {
        int rc = do_open_vverse(vm, fetch_uri, pkg_json, (size_t)pkg_len);
        free(pkg_json);
        return rc;
    }
    int ok = 0;
    Value pkg = json_parse_value_text(vm, pkg_json, &ok);
    if (!ok || pkg.type != VAL_DICT) { fprintf(stderr, "[VDP] bad package json\n"); free(pkg_json); return 0; }
    VerseMeta m;
    if (!verse_parse_meta(vm, pkg, &m) || !m.id) { fprintf(stderr, "[VDP] package missing id\n"); free(pkg_json); return 0; }
    if (pub[0] && (!m.publisher || strcmp(m.publisher, pub) != 0)) {
        fprintf(stderr, "[VDP] publisher mismatch (address %s, package %s) - rejected\n", pub, m.publisher ? m.publisher : "?");
        free(pkg_json); return 0;
    }
    if (ver[0] && (!m.version || strcmp(m.version, ver) != 0)) {
        fprintf(stderr, "[VDP] version mismatch (wanted %s, got %s) - rejected\n", ver, m.version ? m.version : "?");
        free(pkg_json); return 0;
    }
    if (hash[0] && (!m.sha256hex || strcmp(m.sha256hex, hash) != 0)) {
        fprintf(stderr, "[VDP] hash mismatch (wanted %s, got %s) - rejected\n", hash, m.sha256hex ? m.sha256hex : "?");
        free(pkg_json); return 0;
    }
    if (m.min_version && m.min_version[0] && verse_ver_cmp(INFIVERSE_VERSION, m.min_version) < 0) {
        fprintf(stderr, "[VDP] id %s needs infiverse >= %s (current %s)\n", m.id, m.min_version, INFIVERSE_VERSION);
        free(pkg_json); return 0;
    }
    if (!verse_verify(vm, pkg, &m)) { free(pkg_json); return 0; }
    if (!verse_unpack(vm, pkg, m.id, m.mainf, pkg_json)) { free(pkg_json); return 0; }
    free(pkg_json);
    char base[1200];
#ifdef _WIN32
    snprintf(base, sizeof base, "%s/universe/%s\\", home_dir(), m.id);
#else
    snprintf(base, sizeof base, "%s/universe/%s/", home_dir(), m.id);
#endif
    return verse_launch(base, m.mainf);
}
static int b_verse_open(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    r_push_int(vm, do_open(vm, uri));
    free(uri);
    return 1;
}

/* ---------- verse_pack(dir, out): pack folder -> .vverse ---------- */
/* The container is NOT built here.  src/common/vverse_pack.c owns the
 * `gzip({"format":"vverse-1","files":{<path>:<base64>}})` format and is
 * cross-validated against the read-only reference tools/vverse_pack.js by
 * tools/vverse_cross.test.py; tools/vverse_cli.test.py drives THIS builtin
 * through a real script and judges the artifact with the same reference.
 * This builtin is only the `.im` -> C adapter.
 *
 * Call shape is unchanged: verse_pack(root, out), with any trailing legacy
 * arguments (ref-mode, version, min_version, sources) accepted and ignored --
 * the format now takes its metadata from the tree's manifest.json instead of
 * from the call site, which is exactly what makes the output a pure function
 * of the tree.  The return value is unchanged in kind: the output path on
 * success, a falsy value on failure (the old writer returned "" or 0), except
 * that the reason is now reported on stderr so a script that ignores the
 * return value can still see the failure.
 *
 * Deliberately NOT done here: signing with the local identity seed.  The old
 * bare-JSON writer auto-signed whenever a local identity file existed, which
 * made the package a function of (tree, local identity) rather than of the
 * tree.  The real format's ed25519 block is optional and vverse_pack() takes a
 * seed for it; passing NULL keeps re-packs of one tree byte-identical across
 * machines.  Wiring the identity seed in is a separate, explicit decision.
 */
static int b_verse_pack(VM *vm) {
    int argc = vm->cur_argc;
    if (argc < 2) { r_push_str(vm, _strdup("")); return 1; }
    char *out = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *dir = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    char err[512];
    err[0] = 0;
    if (vverse_pack(dir, out, NULL, err, sizeof err) != 0) {
        fprintf(stderr, "[verse_pack] %s: %s\n", dir, err[0] ? err : "pack failed");
        r_push_str(vm, _strdup(""));
    } else {
        r_push_str(vm, _strdup(out));
    }
    free(out);
    free(dir);
    return 1;
}

/* ---------- verse_share(id, hub) -> link ---------- */
static int b_verse_share(VM *vm) {
    int argc = vm->cur_argc;
    char *hub = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *id = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    /* 鍔ㄦ€佸垎锟? 閬垮�?static 缂撳啿琚悗缁皟鐢ㄨ�?閾炬帴鍙兘琚繚锟?澶嶅埗鍒板壀璐存�? */
    char *link = malloc(1200);
    snprintf(link, 1200, "verse://%s/%s", hub, id);
    push_string(vm, link);
    free(link);
    free(hub);
    free(id);
    return 1;
}

/* ---------- verse_hub_list(url) -> manifest array ---------- */
static int b_verse_hub_list(VM *vm) {
    int argc = vm->cur_argc;
    char *url = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    int len = 0;
    char *body = http_get_body(url, &len);
    free(url);
    if (!body) { r_push_nil(vm); return 1; }
    int ok = 0;
    Value v = json_parse_value_text(vm, body, &ok);
    free(body);
    if (!ok) { r_push_nil(vm); return 1; }
    r_push(vm, v);
    return 1;
}

/* ---------- verse_list() / verse_remove(id) ---------- */
static int b_verse_list(VM *vm) {
    r_popn(vm, vm->cur_argc);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return 1; }
    char universe[1200];
    snprintf(universe, sizeof universe, "%s/universe", home_dir());
    ImDir *dir = im_dir_open(universe);
    char name[1024]; int is_dir = 0;
    if (dir) {
        while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
            if (is_dir) {
                const char *s = vm_intern(vm, name);
                Value v; v.type=VAL_STRING; v.ival=1;  v.sval=(char*)(s?s:name);
                vm_array_push(vm, aidx, &v);
            }
        }
        im_dir_close(dir);
    }
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1;  a.sval = NULL;
    r_push(vm, a);
    return 1;
}

static int b_verse_remove(VM *vm) {
    int argc = vm->cur_argc;
    char *id = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    char path[1200];
    snprintf(path, sizeof path, "%s/universe/%s", home_dir(), id);
    /* simple recursive delete via SHFileOperation or manual */
    ImDir *dir = im_dir_open(path);
    char name[1024]; int is_dir = 0;
    if (!dir) { free(id); r_push_int(vm, 0); return 1; }
    while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
        if (is_dir) continue;
        char fp[1400]; snprintf(fp, sizeof fp, "%s/%s", path, name);
        remove(fp);
    }
    im_dir_close(dir);
#ifdef _WIN32
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif
    free(id);
    r_push_int(vm, 1);
    return 1;
}

/* ---------- register ---------- */

/* ---------- verse_listen(port): local HTTP server for packages ---------- */
#ifdef _WIN32
static SOCKET g_listen_sock = INVALID_SOCKET;
static int g_listen_port = 0;
static volatile int g_listen_run = 0;

/* build a minimal HTTP response for path; return malloc'd body */
static char *http_resp(const char *req_path, int *out_len) {
    *out_len = 0;
    /* GET /v/<id> -> universe/<id>/<id>.vverse  (fallback: <id>.vverse) */
    if (strncmp(req_path, "/api/forge", 10) == 0) {
        const char *q = strchr(req_path, '?');
        char proj[256] = "demo";
        if (q && strncmp(q, "?p=", 3) == 0) {
            char *d = proj;
            for (const char *c = q + 3; *c && d < proj + 250; c++) {
                if (*c == '%' && c[1] && c[2]) { int v; sscanf(c + 1, "%2x", &v); *d++ = (char)v; c += 2; }
                else if (*c != ' ' && *c != '\r' && *c != '\n') *d++ = *c;
            }
            *d = 0;
        }
        char fp[1200];
        snprintf(fp, sizeof fp, "%s\\projects\\%s\\verse_config.json", home_dir(), proj);
        FILE *f = fopen(fp, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
            char *body2 = malloc((size_t)len + 1);
            size_t rd = fread(body2, 1, (size_t)len, f); body2[rd] = 0; fclose(f);
            if (rd > 0) { *out_len = (int)rd; return body2; }
            free(body2);
        }
        char *body2 = malloc(2048);
        int n2 = snprintf(body2, 2048, "{\"world\":{\"w\":900,\"h\":640},\"physics\":{\"meteor\":4,\"player\":8},\"avatar\":{\"life\":3},\"ecology\":{\"stars\":20}}");
        *out_len = n2;
        return body2;
    }
    /* ---- M2: workbench API ---- */
    if (strncmp(req_path, "/api/projects", 13) == 0) {
        char *body = malloc(65536);
        int n = 0;
        n += snprintf(body + n, 65536 - n, "[");
        char projects[1200]; snprintf(projects, sizeof projects, "%s\\projects", home_dir());
        ImDir *dir = im_dir_open(projects); char name[1024]; int is_dir = 0; int first = 1;
        if (dir) {
            while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
                if (is_dir) { n += snprintf(body+n, 65536-n, "%s\"%s\"", first ? "" : ",", name); first = 0; }
            }
            im_dir_close(dir);
        }
        n += snprintf(body + n, 65536 - n, "]");
        *out_len = n;
        return body;
    }
    if (strncmp(req_path, "/api/file", 9) == 0) {
        /* /api/file?p=projects/xxx/main.im  -> file content as UTF-8 */
        const char *q = strchr(req_path, '?');
        char rel[1024] = "";
        if (q && strncmp(q, "?p=", 3) == 0) {
            char *dst = rel;
            for (const char *c = q + 3; *c && dst < rel + 1000; c++) {
                if (*c == '\0' || *c == '\r' || *c == '\n' || *c == ' ') break;
                if (*c == '%' && c[1] && c[2]) { /* percent-decode */
                    int v; sscanf(c + 1, "%2x", &v); *dst++ = (char)v; c += 2;
                } else *dst++ = *c;
            }
            *dst = 0;
        }
        char fp[1200];
        snprintf(fp, sizeof fp, "%s/%s", home_dir(), rel[0] ? rel : "projects");
        FILE *f = fopen(fp, "rb");
        if (!f) { *out_len = 0; char *b = malloc(1); b[0] = 0; return b; }
        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
        char *raw = malloc((size_t)len + 1);
        size_t rd = fread(raw, 1, (size_t)len, f);
        raw[rd] = 0; fclose(f);
        /* GBK -> UTF-8 */
        char *body = malloc((size_t)len * 3 + 16);
        int n = 0;
        {
            wchar_t wbuf[32768];
            int wlen = MultiByteToWideChar(936, 0, raw, (int)rd, wbuf, 32768);
            if (wlen > 0) {
                n = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, body, (int)len * 3, NULL, NULL);
            }
            if (n <= 0) { memcpy(body, raw, rd); n = (int)rd; }
            body[n] = 0;
        }
        free(raw);
        *out_len = n;
        return body;
    }
    if (strncmp(req_path, "/ping", 5) == 0) {
        char *pong = malloc(8);
        snprintf(pong, 8, "pong");
        *out_len = 4;
        return pong;
    }

    const char *id = NULL;
    if (strncmp(req_path, "/v/", 3) == 0) id = req_path + 3;
    else if (strncmp(req_path, "/hub", 4) == 0) {
        /* simple hub manifest: list universe dirs as json array of names */
        char *body = malloc(8192);
        int n = 0;
        n += snprintf(body + n, 8192 - n, "[");
        char universe[1200]; snprintf(universe, sizeof universe, "%s/universe", home_dir());
        ImDir *dir = im_dir_open(universe); char name[1024]; int is_dir = 0; int first = 1;
        if (dir) {
            while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
                if (is_dir) { n += snprintf(body+n, 8192-n, "%s\"%s\"", first ? "" : ",", name); first = 0; }
            }
            im_dir_close(dir);
        }
        n += snprintf(body + n, 8192 - n, "]");
        *out_len = n;
        return body;
    }
    if (!id || !*id) return NULL;
    /* strip trailing garbage */
    char *slash = strchr((char*)id, ' ');
    if (slash) *slash = 0;
    char fp[1600];
    snprintf(fp, sizeof fp, "%s/universe/%s\\%s.vverse", home_dir(), id, id);
    FILE *f = fopen(fp, "rb");
    if (!f) {
        snprintf(fp, sizeof fp, "%s/%s.vverse", home_dir(), id);
        f = fopen(fp, "rb");
    }
        if (!f) {
            snprintf(fp, sizeof fp, "%s/universe/_hub\\%s.vverse", home_dir(), id);
            f = fopen(fp, "rb");
        }

    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *body = malloc((size_t)len + 1);
    size_t rd = fread(body, 1, (size_t)len, f);
    body[rd] = 0; fclose(f);
    *out_len = (int)rd;
    return body;
}

/* ---- WebSocket (RFC6455) core: SHA-1, handshake, frames ---- */
static void ws_sha1(const unsigned char *in, int inlen, unsigned char out[20]) {
    unsigned int h[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    unsigned char m[128];
    int blen = (inlen + 8 + 64) / 64 * 64;
    unsigned char *buf = malloc((size_t)blen);
    memset(buf, 0, (size_t)blen);
    memcpy(buf, in, (size_t)inlen);
    buf[inlen] = 0x80;
    unsigned long long bits = (unsigned long long)inlen * 8;
    for (int i = 0; i < 8; i++) buf[blen - 1 - i] = (unsigned char)(bits >> (8*i));
    for (int off = 0; off < blen; off += 64) {
        unsigned int w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned int)buf[off + i*4] << 24) | ((unsigned int)buf[off + i*4+1] << 16) |
                   ((unsigned int)buf[off + i*4+2] << 8) | buf[off + i*4+3];
        for (int i = 16; i < 80; i++) {
            unsigned int x = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (x << 1) | (x >> 31);
        }
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            unsigned int f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            unsigned int tmp = (((a << 5) | (a >> 27)) + f + e + k + w[i]);
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    free(buf);
    for (int i = 0; i < 5; i++) { out[i*4] = (unsigned char)(h[i] >> 24); out[i*4+1] = (unsigned char)(h[i] >> 16); out[i*4+2] = (unsigned char)(h[i] >> 8); out[i*4+3] = (unsigned char)h[i]; }
}

/* base64 (standard) */
static void ws_b64(const unsigned char *in, int inlen, char *out) {
    static const char *B = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < inlen; i += 3) {
        unsigned int v = (unsigned int)in[i] << 16;
        if (i+1 < inlen) v |= (unsigned int)in[i+1] << 8;
        if (i+2 < inlen) v |= in[i+2];
        out[o++] = B[(v >> 18) & 63];
        out[o++] = B[(v >> 12) & 63];
        out[o++] = (i+1 < inlen) ? B[(v >> 6) & 63] : '=';
        out[o++] = (i+2 < inlen) ? B[v & 63] : '=';
    }
    out[o] = 0;
}

/* server handshake: returns 1 on success (sends 101), -1 on failure */
static int ws_handshake(SOCKET s, const char *key) {
    unsigned char dig[20];
    char concat[256];
    snprintf(concat, sizeof concat, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    ws_sha1((const unsigned char*)concat, (int)strlen(concat), dig);
    char accept[64];
    ws_b64(dig, 20, accept);
    char resp[512];
    int n = snprintf(resp, sizeof resp,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
    return send(s, resp, n, 0) == n ? 1 : -1;
}

/* read one frame; returns payload length (>0), 0 = closed, -1 = error; unmasked out */
static int ws_read_frame(SOCKET s, char *out, int outcap, int *is_text) {
    unsigned char hdr[2];
    int got = 0;
    while (got < 2) {
        int n = recv(s, (char*)hdr + got, 2 - got, 0);
        if (n <= 0) return 0;
        got += n;
    }
    int fin = hdr[0] & 0x80;
    int opcode = hdr[0] & 0x0F;
    int masked = hdr[1] & 0x80;
    unsigned long long len = hdr[1] & 0x7F;
    if (len == 126) {
        unsigned char e[2]; got = 0;
        while (got < 2) { int n = recv(s, (char*)e + got, 2 - got, 0); if (n <= 0) return 0; got += n; }
        len = ((unsigned long long)e[0] << 8) | e[1];
    } else if (len == 127) {
        unsigned char e[8]; got = 0;
        while (got < 8) { int n = recv(s, (char*)e + got, 8 - got, 0); if (n <= 0) return 0; got += n; }
        len = 0;
        for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
    }
    unsigned char mask[4];
    if (masked) {
        got = 0;
        while (got < 4) { int n = recv(s, (char*)mask + got, 4 - got, 0); if (n <= 0) return 0; got += n; }
    }
    if (len > (unsigned long long)outcap) len = (unsigned long long)outcap;
    got = 0;
    while (got < (int)len) {
        int n = recv(s, out + got, (int)len - got, 0);
        if (n <= 0) return 0;
        got += n;
    }
    if (masked)
        for (int i = 0; i < got; i++) out[i] ^= mask[i & 3];
    if (is_text) *is_text = (opcode == 1);
    if (opcode == 8) return 0;
    return got;
}

/* send text frame (server->client, unmasked) */
static int ws_send_text(SOCKET s, const char *data, int len) {
    char hdr[14];
    int hn = 0;
    hdr[hn++] = 0x81;
    if (len < 126) hdr[hn++] = (char)len;
    else if (len < 65536) { hdr[hn++] = 126; hdr[hn++] = (char)(len >> 8); hdr[hn++] = (char)len; }
    else { hdr[hn++] = 127; for (int i = 7; i >= 0; i--) hdr[hn++] = (char)(((unsigned long long)len >> (8*i)) & 0xFF); }
    if (send(s, hdr, hn, 0) != hn) return -1;
    return send(s, data, len, 0) == len ? 1 : -1;
}
static DWORD WINAPI http_server_thread(LPVOID arg) {
    (void)arg;
    while (g_listen_run) {
        SOCKET cs = accept(g_listen_sock, NULL, NULL);
        if (cs == INVALID_SOCKET) { Sleep(50); continue; }
        char buf[65536];
        int n = 0;
        {
            int isGet = 0;
            for (int phase = 0; phase < 2; phase++) {
                fd_set rds; FD_ZERO(&rds); FD_SET(cs, &rds);
                struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 300000;
                if (select(0, &rds, NULL, NULL, &tv) <= 0) break;
                int r = recv(cs, buf + n, (int)sizeof buf - 1 - n, 0);
                if (r <= 0) break;
                n += r; buf[n] = 0;
                if (strncmp(buf, "GET ", 4) == 0) isGet = 1;
                if (n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0) {
                    if (isGet) break;
                    /* POST: body may arrive in next packet */
                    Sleep(200);
                }
            }
        }        if (n > 0) {
            buf[n] = 0;
            char method[16], path[512];
            path[0] = 0;
            
            if (strstr(buf, "Upgrade: websocket") || strstr(buf, "upgrade: websocket")) {
                const char *key = strstr(buf, "Sec-WebSocket-Key:");
                char kbuf[128] = "";
                if (key) {
                    key += 18;
                    while (*key == 32) key++;
                    int j = 0;
                    while (*key && *key != 13 && *key != 10 && j < 120) kbuf[j++] = *key++;
                    kbuf[j] = 0;
                }
                if (ws_handshake(cs, kbuf) == 1) {
                    char wbuf[65536];
                    for (;;) {
                        int is_text = 0;
                        int rl = ws_read_frame(cs, wbuf, sizeof wbuf - 1, &is_text);
                        if (rl <= 0) break;
                        wbuf[rl] = 0;
                        ws_send_text(cs, wbuf, rl);
                    }
                }
                closesocket(cs);
                continue;
            }sscanf(buf, "%15s %511s", method, path);
            /* extract POST body after \r\n\r\n */
            const char *hb = strstr(buf, "\r\n\r\n");
            const char *postbody = hb ? hb + 4 : "";
            int blen = 0;
            char *body = NULL;
            if (strcmp(method, "POST") == 0 && strncmp(path, "/publish", 8) == 0) {
                /* 12.2 hub publish: POST /publish?id=<id> body=.vverse -> universe/_hub/<id>.vverse */
                const char *qid = strstr(path, "id=");
                char pid[128] = "pkg";
                if (qid) {
                    const char *v = qid + 3;
                    char *dst = pid;
                    while (*v && *v != 0 && *v != 13 && *v != 10 && *v != 32 && dst < pid + 120) {
                        if (*v == 37 && v[1] && v[2]) { int hh; sscanf(v + 1, "%2x", &hh); *dst++ = (char)hh; v += 2; }
                        else *dst++ = *v;
                        v++;
                    }
                    *dst = 0;
                }
                for (char *fp2 = pid; *fp2; fp2++) if (*fp2 == 47 || *fp2 == 92) *fp2 = 95;
                char hubdir[1200];
                snprintf(hubdir, sizeof hubdir, "%s/universe/_hub", home_dir());
                im_platform_mkdirs(hubdir);
                char hfp[1400];
                snprintf(hfp, sizeof hfp, "%s/%s.vverse", hubdir, pid);
                FILE *hf = fopen(hfp, "wb");
                int pok = 0;
                if (hf) {
                    fwrite(postbody, 1, (int)strlen(postbody), hf);
                    fclose(hf);
                    pok = 1;
                }
                body = malloc(32);
                blen = snprintf(body, 32, "{\"ok\":%d}", pok);
            } else
            if (strcmp(method, "POST") == 0 && strncmp(path, "/api/", 5) == 0) {
                /* api POST: path in ?p=..., content in body (UTF-8) */
                char rel[1024] = "";
                const char *q = strchr(path, '?');
                if (q && strncmp(q, "?p=", 3) == 0) {
                    char *dst = rel;
                    for (const char *c = q + 3; *c && dst < rel + 1000; c++) {
                        if (*c == '\0' || *c == '\r' || *c == '\n' || *c == ' ') break;
                        if (*c == '%' && c[1] && c[2]) { int v; sscanf(c + 1, "%2x", &v); *dst++ = (char)v; c += 2; }
                        else *dst++ = *c;
                    }
                    *dst = 0;
                }
                if (strncmp(path, "/api/save", 9) == 0) {
                    char fp[1200];
                    snprintf(fp, sizeof fp, "%s/%s", home_dir(), rel[0] ? rel : "x.im");
                    for (char *fp2 = fp; *fp2; fp2++) if (*fp2 == '/') *fp2 = '\\';
                    /* ensure dir exists */
                    char dir[1200]; snprintf(dir, sizeof dir, "%s", fp);
                    char *d = strrchr(dir, '\\'); if (d) { *d = 0; im_platform_mkdirs(dir); }
                    /* UTF-8 -> GBK then write */
                    fprintf(stderr, "[api] save body len=%d n=%d\n", (int)strlen(postbody), n);
                    int ulen = (int)strlen(postbody);
                    wchar_t wbuf[65536];
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, postbody, ulen, wbuf, 65536);
                    char *gbk = malloc((size_t)ulen + 16);
                    int glen = 0;
                    if (wlen > 0) glen = WideCharToMultiByte(936, 0, wbuf, wlen, gbk, (int)ulen + 16, NULL, NULL);
                    if (glen <= 0) { memcpy(gbk, postbody, (size_t)ulen); glen = ulen; }
                    FILE *f = fopen(fp, "wb");
                    int ok = 0;
                    if (f) { fwrite(gbk, 1, (size_t)glen, f); fclose(f); ok = 1; }
                    free(gbk);
                    body = malloc(32); int bn = snprintf(body, 32, "{\"ok\":%d}", ok); blen = bn;
                } else if (strncmp(path, "/api/forge", 10) == 0) {
                    /* save config: POST body is JSON, path ?p=<proj> */
                    char relw[1024] = "";
                    const char *qw = strchr(path, '?');
                    if (qw && strncmp(qw, "?p=", 3) == 0) {
                        char *dst = relw;
                        for (const char *c = qw + 3; *c && dst < relw + 1000; c++) {
                            if (*c == '%' && c[1] && c[2]) { int v; sscanf(c + 1, "%2x", &v); *dst++ = (char)v; c += 2; }
                            else if (*c != ' ' && *c != '\r' && *c != '\n') *dst++ = (*c == '/') ? '\\' : *c;
                        }
                        *dst = 0;
                    }
                    char fw[1200];
                    snprintf(fw, sizeof fw, "%s\\projects\\%s\\verse_config.json", home_dir(), relw[0] ? relw : "demo");
                    FILE *fw2 = fopen(fw, "wb");
                    int wok2 = 0;
                    if (fw2) {
                        fwrite(postbody, 1, (int)strlen(postbody), fw2);
                        fclose(fw2);
                        wok2 = 1;
                    }
                    body = malloc(32);
                    int bn2 = snprintf(body, 32, "{\"ok\":%d}", wok2);
                    blen = bn2;
                } else if (strncmp(path, "/api/genworld", 13) == 0) {
                    /* parse config from POST body, generate main.im with params */
                    char relg[1024] = "";
                    const char *qg = strchr(path, '?');
                    if (qg && strncmp(qg, "?p=", 3) == 0) {
                        char *dst = relg;
                        for (const char *c = qg + 3; *c && dst < relg + 1000; c++) {
                            if (*c == '%' && c[1] && c[2]) { int v; sscanf(c + 1, "%2x", &v); *dst++ = (char)v; c += 2; }
                            else if (*c != ' ' && *c != '\r' && *c != '\n') *dst++ = (*c == '/') ? '\\' : *c;
                        }
                        *dst = 0;
                    }
                    int ww = 900, wh = 640, pspeed = 8, mspd = 4, life_n = 3, stars = 20;
                    const char *wk = strstr(postbody, "\"w\":");
                    if (wk) ww = atoi(wk + 4);
                    const char *hk = strstr(postbody, "\"h\":");
                    if (hk) wh = atoi(hk + 4);
                    const char *pk = strstr(postbody, "\"player\":");
                    if (pk) pspeed = atoi(pk + 9);
                    const char *mk = strstr(postbody, "\"meteor\":");
                    if (mk) mspd = atoi(mk + 9);
                    const char *lk = strstr(postbody, "\"life\":");
                    if (lk) life_n = atoi(lk + 7);
                    const char *sk = strstr(postbody, "\"stars\":");
                    if (sk) stars = atoi(sk + 8);
                    if (ww < 200) ww = 900; if (wh < 200) wh = 640;
                    if (pspeed < 1) pspeed = 8; if (mspd < 1) mspd = 4;
                    if (life_n < 1) life_n = 3; if (stars < 1) stars = 20;
                    char tpl[1200];
                    snprintf(tpl, sizeof tpl, "%s\\main.im", home_dir()); { FILE *tfchk = fopen(tpl, "rb"); if (!tfchk) snprintf(tpl, sizeof tpl, "%s\\templates\\main.tpl", home_dir()); else fclose(tfchk); }
                    FILE *tf = fopen(tpl, "rb");
                    if (!tf) {
                        body = malloc(64); strcpy(body, "{\"ok\":0,\"err\":\"no main.im\"}"); blen = 34;
                    } else {
                        fseek(tf, 0, SEEK_END); long tl = ftell(tf); fseek(tf, 0, SEEK_SET);
                        char *tb = malloc((size_t)tl + 1);
                        size_t tr = fread(tb, 1, (size_t)tl, tf); tb[tr] = 0; fclose(tf);
                        /* strip previous Forge generated blocks (between markers) */
                        {
                            char *tmp = malloc((size_t)tl + 1);
                            char *dst2 = tmp;
                            char *src2 = tb;
                            int inBlock = 0;
                            while (*src2) {
                                if (!inBlock && strncmp(src2, "# === Verse Forge generated ===", 31) == 0) { inBlock = 1; }
                                else if (inBlock && strncmp(src2, "# === Forge overrides ===", 25) == 0) {
                                    /* overrides is the LAST block: drop everything from here */
                                    break;
                                }
                                if (!inBlock) *dst2++ = *src2;
                                src2++;
                            }
                            *dst2 = 0;
                            strcpy(tb, tmp);
                            free(tmp);
                        }
                        char *gen = malloc((size_t)tl + 4096);
                        int gn = 0;
                        gn += snprintf(gen + gn, 4096, "# === Verse Forge generated ===\n");
                        gn += snprintf(gen + gn, 4096, "# world %dx%d player=%d meteor=%d life=%d stars=%d\n", ww, wh, pspeed, mspd, life_n, stars);
                        gn += snprintf(gen + gn, 4096, "gui_stage(%d, %d)\n", ww, wh);
                        /* copy template lines, skipping the template's own gui_stage( line */
                        {
                            char *srcx = tb, *dstx = tb;
                            while (*srcx) {
                                char *nl = srcx; while (*nl && *nl != '\n') nl++;
                                int isGs = (nl - srcx >= 11 && strncmp(srcx, "gui_stage(", 10) == 0);
                                if (isGs) { srcx = (*nl == '\n') ? nl + 1 : nl; continue; }
                                int llen = (int)(nl - srcx);
                                memmove(dstx, srcx, (size_t)llen);
                                dstx += llen;
                                if (*nl == '\n') { *dstx++ = '\n'; srcx = nl + 1; } else { srcx = nl; }
                            }
                            *dstx = 0;
                        }
                        { size_t tlen = strlen(tb); memcpy(gen + gn, tb, tlen); gn += (int)tlen; gen[gn] = 0; }
                        gn += snprintf(gen + gn, 4096, "\n# === Forge overrides ===\n");
                        gn += snprintf(gen + gn, 4096, "life = %d\n", life_n);
                        gn += snprintf(gen + gn, 4096, "stars_n = %d\n", stars);
                        char outfp[1200];
                        snprintf(outfp, sizeof outfp, "%s\\projects\\%s\\main.im", home_dir(), relg[0] ? relg : "demo");
                        FILE *of = fopen(outfp, "wb");
                        int wok3 = 0;
                        if (of) { fwrite(gen, 1, (size_t)gn, of); fclose(of); wok3 = 1; }
                        free(tb); free(gen);
                        body = malloc(64);
                        int bn3 = snprintf(body, 64, "{\"ok\":%d,\"w\":%d,\"h\":%d}", wok3, ww, wh);
                        blen = bn3;
                    }
                } else if (strncmp(path, "/api/run", 8) == 0) {
                    /* launch inimerse.exe with the project main.im. ?mode=headless -> no window (phone plays in browser) */
                    char fp[1200];
                    snprintf(fp, sizeof fp, "%s/%s", home_dir(), rel[0] ? rel : "x.im");
                    for (char *fp2 = fp; *fp2; fp2++) if (*fp2 == '/') *fp2 = '\\';
                    char exe[1200]; snprintf(exe, sizeof exe, "%s\\inimerse.exe", home_dir());
                    char cmd[2400];
                    int hmode = (strstr(path, "mode=headless") != NULL);
                    if (hmode) {
                        /* headless: use cmd /c start so the child detaches from the HTTP thread's console */
                        snprintf(cmd, sizeof cmd, "cmd /c start \"\" \"%s\" --headless --port 11490 --http-port 11495 \"%s\"", exe, fp);
                    } else {
                        snprintf(cmd, sizeof cmd, "\"%s\" --gui \"%s\"", exe, fp);
                    }
                    int pid = child_proc_spawn(cmd, hmode ? "headless-game" : "web", 1);
                    int al = 0;
                    if (hmode && pid) { Sleep(1500); al = child_proc_is_alive(pid); }
                    body = malloc(96); int bn = snprintf(body, 96, "{\"pid\":%d,\"mode\":\"%s\",\"alive\":%d}", pid, hmode ? "headless" : "gui", al);
                    blen = bn;
                } else {
                    body = malloc(32); strcpy(body, "{\"ok\":0}"); blen = 8;
                }
            } else {
                body = http_resp(path, &blen);
            }
            char hdr[512];
            if (body) {
                snprintf(hdr, sizeof hdr,
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", blen);
                send(cs, hdr, (int)strlen(hdr), 0);
                send(cs, body, blen, 0);
                free(body);
            } else {
                /* root or unknown path: show a redirect guide page (avoid 404 confusion) */
                char gpage[900];
                snprintf(gpage, sizeof gpage,
                    "<html><body style=\"font-family:sans-serif;background:#111;color:#eee;padding:40px\">"
                    "<h2>Inimerse</h2>"
                    "<p><b>Wrong port!</b> This is the engine API port (11470).</p>"
                    "<p>Change the port to <b>11461</b> in the address bar to open the web pages:</p>"
                    "<p style=\"font-size:18px\">http://<i>your-ip</i>:11461/ &nbsp;(game)<br>"
                    "http://<i>your-ip</i>:11461/wb &nbsp;(workbench)<br>"
                    "http://<i>your-ip</i>:11461/forge &nbsp;(verse forge)</p>"
                    "</body></html>");
                snprintf(hdr, sizeof hdr, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", (int)strlen(gpage));
                send(cs, hdr, (int)strlen(hdr), 0);
                send(cs, gpage, (int)strlen(gpage), 0);
            }
        }
        closesocket(cs);
    }
    return 0;
}
/* exported: start verse HTTP server on port (returns 1 ok) */
int verse_http_start(int port) {
    if (g_listen_run) return 1;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock == INVALID_SOCKET) return 0;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((unsigned short)port);
    if (bind(g_listen_sock, (struct sockaddr*)&sa, sizeof sa) != 0) { closesocket(g_listen_sock); g_listen_sock = INVALID_SOCKET; return 0; }
    if (listen(g_listen_sock, 8) != 0) { closesocket(g_listen_sock); g_listen_sock = INVALID_SOCKET; return 0; }
    g_listen_port = port;
    g_listen_run = 1;
    HANDLE h = CreateThread(NULL, 0, http_server_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    return 1;
}
void verse_http_stop(void) {
    g_listen_run = 0;
    if (g_listen_sock != INVALID_SOCKET) { closesocket(g_listen_sock); g_listen_sock = INVALID_SOCKET; }
}

/* ---- UDP hub service (same port as HTTP hub) ---- */
static SOCKET g_udp_listen_sock = INVALID_SOCKET;
static char *verse_udp_fetch(const char *host, int port, const char *id, int *out_len);

static char *verse_udp_fetch(const char *host, int port, const char *id, int *out_len) {
    *out_len = 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return NULL;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { WSACleanup(); return NULL; }
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(host);
        if (!he) { closesocket(s); WSACleanup(); return NULL; }
        memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    }
    char req[600];
    int rl = snprintf(req, sizeof req, "GET /v/%s", id);
    sendto(s, req, rl, 0, (struct sockaddr*)&sa, sizeof sa);
    char *buf = malloc(65536);
    unsigned long long t0 = GetTickCount64();
    int got = 0;
    while (GetTickCount64() - t0 < 3000) {
        struct sockaddr_in from;
        int flen = sizeof from;
        int n = recvfrom(s, buf, 65535, 0, (struct sockaddr*)&from, &flen);
        if (n > 0) { buf[n] = 0; got = n; break; }
        Sleep(5);
    }
    closesocket(s);
    WSACleanup();
    if (!got) { free(buf); return NULL; }
    *out_len = got;
    return buf;
}

static DWORD WINAPI udp_server_thread(LPVOID arg) {
    (void)arg;
    char buf[65536];
    while (g_listen_run && g_udp_listen_sock != INVALID_SOCKET) {
        struct sockaddr_in from;
        int flen = sizeof from;
        int n = recvfrom(g_udp_listen_sock, buf, sizeof buf - 1, 0, (struct sockaddr*)&from, &flen);
        if (n <= 0) { Sleep(2); continue; }
        buf[n] = 0;
        if (strncmp(buf, "GET /v/", 7) != 0) continue;
        const char *id = buf + 7;
        for (const char *p = id; *p; p++) if (*p == '\r' || *p == '\n' || *p == ' ') { ((char*)p)[0] = 0; break; }
        char req_path[600];
        snprintf(req_path, sizeof req_path, "/v/%s", id);
        int blen = 0;
        char *body = http_resp(req_path, &blen);
        if (!body) continue;
        if (blen <= 60000)
            sendto(g_udp_listen_sock, body, blen, 0, (struct sockaddr*)&from, flen);
        free(body);
    }
    return 0;
}
static int b_verse_listen(VM *vm) {
    int argc = vm->cur_argc;
    Value pv = r_arg(vm, argc - 1);
    int port = (int)val_as_int(&pv);
    r_popn(vm, argc);
    if (g_listen_run) { r_push_int(vm, g_listen_port); return 1; }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { r_push_int(vm, 0); return 1; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { r_push_int(vm, 0); return 1; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr*)&sa, sizeof sa) != 0) {
        closesocket(s); r_push_int(vm, 0); return 1;
    }
    if (listen(s, 8) != 0) { closesocket(s); r_push_int(vm, 0); return 1; }

    g_udp_listen_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp_listen_sock != INVALID_SOCKET) {
        struct sockaddr_in us;
        memset(&us, 0, sizeof us);
        us.sin_family = AF_INET;
        us.sin_port = htons((unsigned short)port);
        us.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(g_udp_listen_sock, (struct sockaddr*)&us, sizeof us) == 0) {
            HANDLE uth = CreateThread(NULL, 0, udp_server_thread, NULL, 0, NULL);
            if (uth) CloseHandle(uth);
        } else { closesocket(g_udp_listen_sock); g_udp_listen_sock = INVALID_SOCKET; }
    }    g_listen_sock = s;
    g_listen_port = port;
    g_listen_run = 1;
    HANDLE th = CreateThread(NULL, 0, http_server_thread, NULL, 0, NULL);
    if (th) CloseHandle(th);
    r_push_int(vm, port);
    return 1;
}

static int b_verse_stop(VM *vm) {
    r_popn(vm, vm->cur_argc);
    if (g_listen_run) {
        g_listen_run = 0;

        if (g_udp_listen_sock != INVALID_SOCKET) { closesocket(g_udp_listen_sock); g_udp_listen_sock = INVALID_SOCKET; }        if (g_listen_sock != INVALID_SOCKET) {
            closesocket(g_listen_sock);
            g_listen_sock = INVALID_SOCKET;
        }
        WSACleanup();
    }
    r_push_int(vm, 1);
    return 1;
}
#else
/* POSIX embedded hub: TCP + UDP package distribution lives in
   platform/http_posix.c (verse_http_start/stop); this file provides the
   script-facing commands and the UDP client transport. */
int verse_http_start(int port);
void verse_http_stop(void);

/* UDP hub client: send `GET /v/<id>` and collect the package body (3s cap,
   matching the Windows implementation's deadline). */
static char *verse_udp_fetch(const char *host, int port, const char *id, int *out_len) {
    *out_len = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) { close(fd); return NULL; }
        sa.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    char req[600];
    int rl = snprintf(req, sizeof req, "GET /v/%s", id);
    (void)sendto(fd, req, (size_t)rl, 0, (struct sockaddr *)&sa, sizeof sa);
    struct timeval tv = { 3, 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char *buf = (char *)malloc(65536);
    ssize_t got = recvfrom(fd, buf, 65535, 0, NULL, NULL);
    close(fd);
    if (got <= 0) { free(buf); return NULL; }
    buf[got] = 0;
    *out_len = (int)got;
    return buf;
}

static int b_verse_listen(VM *vm) {
    int argc = vm->cur_argc;
    Value pv = r_arg(vm, argc - 1);
    int port = (int)val_as_int(&pv);
    r_popn(vm, argc);
    int rc = verse_http_start(port);
    r_push_int(vm, rc ? port : 0);
    return 1;
}

static int b_verse_stop(VM *vm) {
    r_popn(vm, vm->cur_argc);
    verse_http_stop();
    r_push_int(vm, 1);
    return 1;
}
#endif /* _WIN32 embedded server */


/* helpers shared with the §55.2 advertisement code below */
static int vd_json_str(const char *json, const char *key, char *out, size_t cap);
static void rp_set_entry(VM *vm, int aidx, const char *key, Value val);

/* ---------- §43 economy domains, settlement and audit ---------- */

/* Declare a currency domain; the definition is signed with the local identity
   (which becomes the issuer). */
static int b_verse_econ_domain(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *domain = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *kind = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "utility");
    char *denom = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "unit");
    char *transfer = _strdup(r_str(vm, argc - 5) ? r_str(vm, argc - 5) : "domain-only");
    r_popn(vm, argc);
    char issuer[65] = "";
    if (!identity_pubkey(issuer)) { free(uri); free(domain); free(kind); free(denom); free(transfer); r_push_str(vm, _strdup("")); return 1; }
    char canon[1024];
    snprintf(canon, sizeof canon, "%s|%s|%s|%s|%s|%s", domain, issuer, kind, "", denom, transfer);
    char sig[129] = "";
    {
        int slen = 0;
        char *seed = read_file_buf(identity_seed_path(), &slen);
        if (seed && slen >= 64) {
            unsigned char seedb[32], sigb[64];
            for (int si = 0; si < 32; si++) {
                int hi = seed[si*2] >= 'a' ? seed[si*2]-'a'+10 : seed[si*2]-'0';
                int lo = seed[si*2+1] >= 'a' ? seed[si*2+1]-'a'+10 : seed[si*2+1]-'0';
                seedb[si] = (unsigned char)((hi << 4) | lo);
            }
            ed25519_sign(seedb, (const unsigned char *)canon, strlen(canon), sigb);
            static const char *hx = "0123456789abcdef";
            for (int si = 0; si < 64; si++) { sig[si*2] = hx[sigb[si] >> 4]; sig[si*2+1] = hx[sigb[si] & 15]; }
            sig[128] = 0;
        }
        free(seed);
    }
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/economy/domain", uri);
    else
        snprintf(url, sizeof url, "%s/economy/domain", uri);
    char body[2048];
    snprintf(body, sizeof body,
             "{\"domain_id\":\"%s\",\"issuer\":\"%s\",\"value_kind\":\"%s\","
             "\"supply_rule\":\"\",\"denomination\":\"%s\",\"transfer_policy\":\"%s\",\"signature\":\"%s\"}",
             domain, issuer, kind, denom, transfer, sig);
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    char cid[65] = "";
    if (resp) { (void)vd_json_str(resp, "currency_id", cid, sizeof cid); free(resp); }
    Value s; s.type = VAL_STRING; s.ival = 0;  s.ptr = NULL; s.sval = strdup(cid);
    r_push(vm, s);
    free(uri); free(domain); free(kind); free(denom); free(transfer);
    return 1;
}

/* Settle (transfer) within one domain, with an idempotency key. */
static int b_verse_econ_settle(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *cid = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *from = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    char *to = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "");
    char *amt = _strdup(r_str(vm, argc - 5) ? r_str(vm, argc - 5) : "0");
    char *key = _strdup(r_str(vm, argc - 6) ? r_str(vm, argc - 6) : "");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/economy/settle", uri);
    else
        snprintf(url, sizeof url, "%s/economy/settle", uri);
    char body[2048];
    snprintf(body, sizeof body,
             "{\"currency_id\":\"%s\",\"from\":\"%s\",\"to\":\"%s\",\"amount\":%s,\"idempotency_key\":\"%s\"}",
             cid, from, to, amt, key);
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    char status[32] = "error", reason[64] = "";
    int balance_to = 0, version_to = 0, event_seq = 0;
    if (resp) {
        (void)vd_json_str(resp, "status", status, sizeof status);
        (void)vd_json_str(resp, "error", reason, sizeof reason);
        const char *bt = strstr(resp, "\"balance_to\"");
        if (bt) { const char *c = strchr(bt, ':'); if (c) balance_to = atoi(c + 1); }
        const char *vt = strstr(resp, "\"version_to\"");
        if (vt) { const char *c = strchr(vt, ':'); if (c) version_to = atoi(c + 1); }
        const char *es = strstr(resp, "\"event_seq\"");
        if (es) { const char *c = strchr(es, ':'); if (c) event_seq = atoi(c + 1); }
        free(resp);
    }
    int out = vm_array_new(vm);
    Value kv; kv.type = VAL_STRING; kv.fval = 0; kv.ptr = NULL; 
    kv.sval = status;    rp_set_entry(vm, out, "status", kv);
    kv.sval = reason;    rp_set_entry(vm, out, "reason", kv);
    Value iv; iv.type = VAL_INT; iv.fval = 0; iv.sval = NULL; iv.ptr = NULL;
    iv.ival = balance_to; rp_set_entry(vm, out, "balance_to", iv);
    iv.ival = version_to; rp_set_entry(vm, out, "version_to", iv);
    iv.ival = event_seq;  rp_set_entry(vm, out, "event_seq", iv);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(cid); free(from); free(to); free(amt); free(key);
    return 1;
}

/* Mint is an auditable event and must be signed by the currency issuer. */
static int b_verse_econ_mint(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *cid = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *to = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    char *amt = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "0");
    char *key = _strdup(r_str(vm, argc - 5) ? r_str(vm, argc - 5) : "");
    r_popn(vm, argc);
    char canon[768], sig[129] = "";
    snprintf(canon, sizeof canon, "mint|%s|%s|%s|%s", cid, to, amt, key);
    {
        int slen = 0;
        char *seed = read_file_buf(identity_seed_path(), &slen);
        if (seed && slen >= 64) {
            unsigned char seedb[32], sigb[64];
            for (int si = 0; si < 32; si++) {
                int hi = seed[si*2] >= 'a' ? seed[si*2]-'a'+10 : seed[si*2]-'0';
                int lo = seed[si*2+1] >= 'a' ? seed[si*2+1]-'a'+10 : seed[si*2+1]-'0';
                seedb[si] = (unsigned char)((hi << 4) | lo);
            }
            ed25519_sign(seedb, (const unsigned char *)canon, strlen(canon), sigb);
            static const char *hx = "0123456789abcdef";
            for (int si = 0; si < 64; si++) { sig[si*2] = hx[sigb[si] >> 4]; sig[si*2+1] = hx[sigb[si] & 15]; }
            sig[128] = 0;
        }
        free(seed);
    }
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/economy/mint", uri);
    else
        snprintf(url, sizeof url, "%s/economy/mint", uri);
    char body[2048];
    snprintf(body, sizeof body,
             "{\"currency_id\":\"%s\",\"to\":\"%s\",\"amount\":%s,\"idempotency_key\":\"%s\",\"signature\":\"%s\"}",
             cid, to, amt, key, sig);
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    int ok = (resp && strstr(resp, "\"status\":\"settled\"") != NULL);
    free(resp);
    r_push_int(vm, ok ? 1 : 0);
    free(uri); free(cid); free(to); free(amt); free(key);
    return 1;
}

/* Balance and audit views. */
static int b_verse_econ_balance(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *cid = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *account = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    r_popn(vm, argc);
    char url[1400];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/economy/balance?currency_id=%s&account=%s", uri, cid, account);
    else
        snprintf(url, sizeof url, "%s/economy/balance?currency_id=%s&account=%s", uri, cid, account);
    int len = 0;
    char *body = http_get_body(url, &len);
    int amount = 0, version = 0;
    if (body) {
        const char *a = strstr(body, "\"amount\"");
        if (a) { const char *c = strchr(a, ':'); if (c) amount = atoi(c + 1); }
        const char *v = strstr(body, "\"version\"");
        if (v) { const char *c = strchr(v, ':'); if (c) version = atoi(c + 1); }
        free(body);
    }
    int out = vm_array_new(vm);
    Value iv; iv.type = VAL_INT; iv.sval = NULL; iv.ptr = NULL; iv.fval = 0;
    iv.ival = amount;  rp_set_entry(vm, out, "amount", iv);
    iv.ival = version; rp_set_entry(vm, out, "version", iv);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(cid); free(account);
    return 1;
}

static int b_verse_econ_audit(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *cid = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/economy/audit?currency_id=%s", uri, cid);
    else
        snprintf(url, sizeof url, "%s/economy/audit?currency_id=%s", uri, cid);
    int len = 0;
    char *body = http_get_body(url, &len);
    int count = 0, chain_ok = 0;
    if (body) {
        const char *c = strstr(body, "\"count\"");
        if (c) { const char *x = strchr(c, ':'); if (x) count = atoi(x + 1); }
        const char *k = strstr(body, "\"chain_ok\"");
        if (k) { const char *x = strchr(k, ':'); if (x) chain_ok = atoi(x + 1); }
        free(body);
    }
    int out = vm_array_new(vm);
    Value iv; iv.type = VAL_INT; iv.sval = NULL; iv.ptr = NULL; iv.fval = 0;
    iv.ival = count;    rp_set_entry(vm, out, "count", iv);
    iv.ival = chain_ok; rp_set_entry(vm, out, "chain_ok", iv);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(cid);
    return 1;
}

/* ---------- §55.6 reconnect, state and idempotency ---------- */

/* Reconnect with the client's generation/sequence view (§55.6 fields). */
static int b_verse_session_reattach(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *verse = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *peer = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    char *gen = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "0");
    char *recv = _strdup(r_str(vm, argc - 5) ? r_str(vm, argc - 5) : "0");
    char *comm = _strdup(r_str(vm, argc - 6) ? r_str(vm, argc - 6) : "0");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/session/reattach", uri);
    else
        snprintf(url, sizeof url, "%s/session/reattach", uri);
    char body[2048];
    snprintf(body, sizeof body,
             "{\"verse\":\"%s\",\"peer\":\"%s\",\"generation\":%s,"
             "\"last_received_sequence\":%s,\"last_committed_sequence\":%s}",
             verse, peer, gen, recv, comm);
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    char resume[32] = "unknown", reason[64] = "";
    int last_applied = 0, generation = 0;
    if (resp) {
        (void)vd_json_str(resp, "resume", resume, sizeof resume);
        (void)vd_json_str(resp, "reason", reason, sizeof reason);
        const char *la = strstr(resp, "\"last_applied\"");
        if (la) { const char *c = strchr(la, ':'); if (c) last_applied = atoi(c + 1); }
        const char *gg = strstr(resp, "\"generation\"");
        if (gg) { const char *c = strchr(gg, ':'); if (c) generation = atoi(c + 1); }
        free(resp);
    }
    int out = vm_array_new(vm);
    Value kv; kv.type = VAL_STRING; kv.fval = 0; kv.ptr = NULL; 
    kv.sval = resume;     rp_set_entry(vm, out, "resume", kv);
    kv.sval = reason;     rp_set_entry(vm, out, "reason", kv);
    Value iv; iv.type = VAL_INT; iv.fval = 0; iv.sval = NULL; iv.ptr = NULL;
    iv.ival = last_applied; rp_set_entry(vm, out, "last_applied", iv);
    iv.ival = generation;   rp_set_entry(vm, out, "generation", iv);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(verse); free(peer); free(gen); free(recv); free(comm);
    return 1;
}

/* Read the server-side session state (§55.6 lifecycle). */
static int b_verse_session_state(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *verse = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *peer = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    r_popn(vm, argc);
    char url[1400];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/session/state?verse=%s&peer=%s", uri, verse, peer);
    else
        snprintf(url, sizeof url, "%s/session/state?verse=%s&peer=%s", uri, verse, peer);
    int len = 0;
    char *body = http_get_body(url, &len);
    char state[32] = "unknown", authority[128] = "none";
    int generation = 0, pending = 0;
    if (body) {
        (void)vd_json_str(body, "state", state, sizeof state);
        (void)vd_json_str(body, "authority", authority, sizeof authority);
        const char *g = strstr(body, "\"generation\"");
        if (g) { const char *c = strchr(g, ':'); if (c) generation = atoi(c + 1); }
        const char *p2 = strstr(body, "\"pending_inputs\"");
        if (p2) { const char *c = strchr(p2, ':'); if (c) pending = atoi(c + 1); }
        free(body);
    }
    int out = vm_array_new(vm);
    Value kv; kv.type = VAL_STRING; kv.fval = 0; kv.ptr = NULL; 
    kv.sval = state;      rp_set_entry(vm, out, "state", kv);
    kv.sval = authority;  rp_set_entry(vm, out, "authority", kv);
    Value iv; iv.type = VAL_INT; iv.fval = 0; iv.sval = NULL; iv.ptr = NULL;
    iv.ival = generation; rp_set_entry(vm, out, "generation", iv);
    iv.ival = pending;    rp_set_entry(vm, out, "pending_inputs", iv);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(verse); free(peer);
    return 1;
}

/* Idempotency gate for side-effecting requests: "new" or "replay". */
static int b_verse_idem_begin(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *verse = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *peer = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    char *key = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/session/idem", uri);
    else
        snprintf(url, sizeof url, "%s/session/idem", uri);
    char body[1024];
    snprintf(body, sizeof body, "{\"verse\":\"%s\",\"peer\":\"%s\",\"key\":\"%s\"}", verse, peer, key);
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    char status[32] = "unknown";
    if (resp) { (void)vd_json_str(resp, "status", status, sizeof status); free(resp); }
    Value s; s.type = VAL_STRING; s.ival = 0;  s.ptr = NULL; s.sval = strdup(status);
    r_push(vm, s);
    free(uri); free(verse); free(peer); free(key);
    return 1;
}

/* ---------- §55.5 scheduling, authority and handoff ---------- */

/* Scheduler view: only fresh + healthy nodes (what was excluded is reported). */
static int b_verse_node_schedule(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *want = argc >= 2 ? _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "") : _strdup("");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/node/schedule%s%s", uri, want[0] ? "?caps=" : "", want);
    else
        snprintf(url, sizeof url, "%s/node/schedule%s%s", uri, want[0] ? "?caps=" : "", want);
    int len = 0;
    char *body = http_get_body(url, &len);
    int aidx = vm_array_new(vm);
    int count = 0;
    if (body) {
        const char *p = body;
        while ((p = strstr(p, "\"node_id\"")) != NULL) {
            char node_id[128] = "", endpoint[256] = "", caps[128] = "", health[32] = "";
            (void)vd_json_str(p, "node_id", node_id, sizeof node_id);
            (void)vd_json_str(p, "endpoint", endpoint, sizeof endpoint);
            (void)vd_json_str(p, "caps", caps, sizeof caps);
            (void)vd_json_str(p, "health", health, sizeof health);
            int entry = vm_array_new(vm);
            Value kv; kv.type = VAL_STRING; kv.fval = 0; kv.ptr = NULL;
            kv.ival = 0; kv.sval = node_id;   rp_set_entry(vm, entry, "node_id", kv);
            kv.sval = endpoint;               rp_set_entry(vm, entry, "endpoint", kv);
            kv.sval = caps;                   rp_set_entry(vm, entry, "caps", kv);
            kv.sval = health;                 rp_set_entry(vm, entry, "health", kv);
            Value ev; ev.type = VAL_DICT; ev.ival = entry + 1;  ev.sval = NULL; ev.ptr = NULL;
            vm_array_push(vm, aidx, &ev);
            count++;
            p += 9;
        }
        free(body);
    }
    int out = vm_array_new(vm);
    Value nv; nv.type = VAL_INT; nv.ival = count;  nv.sval = NULL; nv.ptr = NULL;
    rp_set_entry(vm, out, "count", nv);
    Value arr; arr.type = VAL_ARRAY; arr.ival = aidx + 1;  arr.sval = NULL; arr.ptr = NULL;
    rp_set_entry(vm, out, "nodes", arr);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(want);
    return 1;
}

/* Read the stable session authority record (§55.5 stable session object). */
static int b_verse_session_authority(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *verse = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *peer = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    r_popn(vm, argc);
    char url[1400];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/session/authority?verse=%s&peer=%s", uri, verse, peer);
    else
        snprintf(url, sizeof url, "%s/session/authority?verse=%s&peer=%s", uri, verse, peer);
    int len = 0;
    char *body = http_get_body(url, &len);
    char authority[128] = "none", tail[65] = "", frozen[8] = "0";
    int generation = 0;
    if (body) {
        (void)vd_json_str(body, "authority", authority, sizeof authority);
        (void)vd_json_str(body, "event_tail", tail, sizeof tail);
        const char *g = strstr(body, "\"generation\"");
        if (g) { const char *c = strchr(g, ':'); if (c) generation = atoi(c + 1); }
        const char *fz = strstr(body, "\"frozen\"");
        if (fz) { const char *c = strchr(fz, ':'); if (c) snprintf(frozen, sizeof frozen, "%d", atoi(c + 1)); }
        free(body);
    }
    int out = vm_array_new(vm);
    Value kv; kv.type = VAL_STRING; kv.fval = 0; kv.ptr = NULL; 
    kv.sval = authority;  rp_set_entry(vm, out, "authority", kv);
    kv.sval = tail;       rp_set_entry(vm, out, "event_tail", kv);
    kv.sval = frozen;     rp_set_entry(vm, out, "frozen", kv);
    Value iv; iv.type = VAL_INT; iv.ival = generation;  iv.sval = NULL; iv.ptr = NULL;
    rp_set_entry(vm, out, "generation", iv);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri); free(verse); free(peer);
    return 1;
}

/* Request a handoff to a healthy target; the hub verifies the checkpoint and
   event tail before transferring authority (§55.5). */
static int b_verse_node_handoff(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *verse = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *peer = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    char *to = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "");
    char *snap = _strdup(r_str(vm, argc - 5) ? r_str(vm, argc - 5) : "");
    char *tail = _strdup(r_str(vm, argc - 6) ? r_str(vm, argc - 6) : "");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/node/handoff", uri);
    else
        snprintf(url, sizeof url, "%s/node/handoff", uri);
    char body[2048];
    snprintf(body, sizeof body,
             "{\"verse\":\"%s\",\"peer\":\"%s\",\"to_authority\":\"%s\",\"snapshot_hash\":\"%s\",\"event_tail_hash\":\"%s\"}",
             verse, peer, to, snap, tail);
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    int ok = (resp && strstr(resp, "\"ok\":true") != NULL);
    free(resp); free(uri); free(verse); free(peer); free(to); free(snap); free(tail);
    r_push_int(vm, ok ? 1 : 0);
    return 1;
}

/* ---------- §55.2 node advertisements ---------- */
/* hex -> bytes; returns 1 when exactly want bytes were decoded */
static int hex_decode_len(const char *hex, unsigned char *out, int want) {
    for (int i = 0; i < want; i++) {
        if (!hex[i * 2] || !hex[i * 2 + 1]) return 0;
        int hi = hex[i*2] >= 'a' ? hex[i*2]-'a'+10 : hex[i*2] >= 'A' ? hex[i*2]-'A'+10 : hex[i*2]-'0';
        int lo = hex[i*2+1] >= 'a' ? hex[i*2+1]-'a'+10 : hex[i*2+1] >= 'A' ? hex[i*2+1]-'A'+10 : hex[i*2+1]-'0';
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return 0;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return hex[want * 2] == 0 || 1; /* longer trailing text is tolerated */
}

/* tolerant JSON string field reader: accepts "key":"v" and "key": "v" */
static int vd_json_str(const char *json, const char *key, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (p[i] && p[i] != '"' && i + 1 < cap) { out[i] = p[i]; i++; }
    out[i] = 0;
    return 1;
}

static void rp_set_entry(VM *vm, int aidx, const char *key, Value val) {
    Value k; k.type = VAL_STRING; k.ival = 1;  k.sval = (char *)key; k.ptr = NULL;
    vm_dict_set(vm, aidx, &k, &val);
}

/* Publish a signed, expiring capability claim to a hub directory. */
static int b_verse_node_advertise(VM *vm) {
    int argc = vm->cur_argc;
    /* argument convention: r_str(argc-1) is the FIRST argument */
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    char *sig = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *exp = _strdup(r_str(vm, argc - 3) ? r_str(vm, argc - 3) : "");
    char *endpoint = _strdup(r_str(vm, argc - 4) ? r_str(vm, argc - 4) : "");
    char *caps = _strdup(r_str(vm, argc - 5) ? r_str(vm, argc - 5) : "");
    r_popn(vm, argc);
    char pubhex[65] = "";
    if (!identity_pubkey(pubhex)) { free(uri); free(sig); free(exp); free(endpoint); free(caps); r_push_int(vm, 0); return 1; }
    /* the signed payload is the canonical "endpoint|caps|expires_at" text:
       a delimiter that survives JSON round-trips unchanged (newlines would
       be truncated by the directory's JSON parser) */
    char payload[512];
    snprintf(payload, sizeof payload, "%s|%s|%s", endpoint, caps, exp);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/node/advertise", uri);
    else
        snprintf(url, sizeof url, "%s/node/advertise", uri);
    char body[2048];
    snprintf(body, sizeof body,
             "{\"node_id\":\"%s\",\"payload\":\"%s\",\"signature\":\"%s\",\"endpoint\":\"%s\",\"caps\":\"%s\",\"expires_at\":%s}",
             pubhex, payload, sig, endpoint, caps, exp[0] ? exp : "0");
    int len = 0;
    char *resp = http_post_body(url, body, &len);
    int ok = (resp && strstr(resp, "\"ok\":true") != NULL);
    free(resp); free(uri); free(sig); free(exp); free(endpoint); free(caps);
    r_push_int(vm, ok ? 1 : 0);
    return 1;
}

/* Discover nodes from a hub and re-verify every claim locally: a directory
   is a hint, the signature decides trust (§55.2).  Each entry carries
   verified (1/0) and reason; nothing is dropped silently. */
static int b_verse_node_discover(VM *vm) {
    int argc = vm->cur_argc;
    char *uri = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    char url[1200];
    if (strncmp(uri, "http://", 7) != 0 && strncmp(uri, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/node/discover", uri);
    else
        snprintf(url, sizeof url, "%s/node/discover", uri);
    int len = 0;
    char *body = http_get_body(url, &len);
    int aidx = vm_array_new(vm);
    int count = 0;
    if (body) {
        const char *p = body;
        uint64_t now = (uint64_t)im_platform_now_ms();
        while ((p = strstr(p, "\"node_id\"")) != NULL) {
            /* keep p at the key: vd_json_str locates fields relative to it */
            char node_id[128] = "", payload[512] = "", signature[160] = "", endpoint[256] = "", caps[128] = "";
            (void)vd_json_str(p, "node_id", node_id, sizeof node_id);
            (void)vd_json_str(p, "payload", payload, sizeof payload);
            (void)vd_json_str(p, "signature", signature, sizeof signature);
            (void)vd_json_str(p, "endpoint", endpoint, sizeof endpoint);
            (void)vd_json_str(p, "caps", caps, sizeof caps);
            uint64_t expires_at = 0;
            {
                const char *ex = strstr(p, "\"expires_at\"");
                if (ex) { const char *c = strchr(ex, ':'); if (c) expires_at = strtoull(c + 1, NULL, 10); }
            }
            /* local re-verification: signature over payload + freshness */
            unsigned char pub[32], sig[64];
            int verified = 0;
            const char *reason = "unverified";
            if (strlen(node_id) == 64 && strlen(signature) == 128 &&
                hex_decode_len(node_id, pub, 32) && hex_decode_len(signature, sig, 64)) {
                if (ed25519_verify(pub, (const unsigned char *)payload, strlen(payload), sig)) {
                    if (expires_at == 0 || expires_at > now) { verified = 1; reason = "ok"; }
                    else reason = "expired";
                } else reason = "bad_signature";
            } else reason = "malformed";
            int entry = vm_array_new(vm);
            Value kv; kv.type = VAL_STRING; kv.fval = 0; kv.ptr = NULL;
            kv.ival = 0; kv.sval = (char *)node_id;    rp_set_entry(vm, entry, "node_id", kv);
            kv.sval = (char *)endpoint;                rp_set_entry(vm, entry, "endpoint", kv);
            kv.sval = (char *)caps;                    rp_set_entry(vm, entry, "caps", kv);
            Value iv; iv.type = VAL_INT; iv.fval = 0; iv.sval = NULL; iv.ptr = NULL;
            iv.ival = verified;                        rp_set_entry(vm, entry, "verified", iv);
            iv.ival = (int)expires_at;                 rp_set_entry(vm, entry, "expires_at", iv);
            kv.type = VAL_STRING; kv.ival = 1; kv.sval = (char *)reason; rp_set_entry(vm, entry, "reason", kv);
            Value ev; ev.type = VAL_DICT; ev.ival = entry + 1;  ev.sval = NULL; ev.ptr = NULL;
            vm_array_push(vm, aidx, &ev);
            count++;
            p += 9; /* advance past this entry's key and keep scanning */
        }
        free(body);
    }
    int out = vm_array_new(vm);
    Value nv; nv.type = VAL_INT; nv.ival = count;  nv.sval = NULL; nv.ptr = NULL;
    rp_set_entry(vm, out, "count", nv);
    Value arr; arr.type = VAL_ARRAY; arr.ival = aidx + 1;  arr.sval = NULL; arr.ptr = NULL;
    rp_set_entry(vm, out, "nodes", arr);
    Value d; d.type = VAL_DICT; d.ival = out + 1;  d.sval = NULL; d.ptr = NULL;
    r_push(vm, d);
    free(uri);
    return 1;
}

void verse_dist_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "verse_open", b_verse_open, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_pack", b_verse_pack, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_share", b_verse_share, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_listen", b_verse_listen, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_stop", b_verse_stop, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_hub_list", b_verse_hub_list, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_update", b_verse_update, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hub_add", b_verse_hub_add, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hub_remove", b_verse_hub_remove, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hubs", b_verse_hubs, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hub_ping", b_verse_hub_ping, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_node_advertise", b_verse_node_advertise, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_node_discover", b_verse_node_discover, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_node_schedule", b_verse_node_schedule, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_session_authority", b_verse_session_authority, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_node_handoff", b_verse_node_handoff, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_session_reattach", b_verse_session_reattach, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_session_state", b_verse_session_state, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_idem_begin", b_verse_idem_begin, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_econ_domain", b_verse_econ_domain, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_econ_settle", b_verse_econ_settle, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_econ_mint", b_verse_econ_mint, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_econ_balance", b_verse_econ_balance, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_econ_audit", b_verse_econ_audit, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_public_ip", b_verse_public_ip, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_publish", b_verse_publish, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_identity_new", b_verse_identity_new, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_identity_pubkey", b_verse_identity_pubkey, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_sign", b_verse_sign, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_verify", b_verse_verify, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_list", b_verse_list, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_remove", b_verse_remove, 1|CAP_VERSE, 0);
    fprintf(stderr, "[verse_dist mod] VDP loaded (verse://<hub>/<id>)\n");
}
