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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "child_proc.h"
#include <winhttp.h>
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
        Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (v.type == VAL_STRING && v.ival != 1 && v.sval) free(v.sval);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
}
static void r_push(VM *vm, Value v) {
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
    }
}
static void r_push_int(VM *vm, int n) {
    Value v; v.type = VAL_INT; v.ival = n; v.fval = 0; v.sval = NULL;
    r_push(vm, v);
}
static void r_push_str(VM *vm, const char *s) {
    Value v; v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = (char*)s;
    r_push(vm, v);
}
static void r_push_nil(VM *vm) {
    Value v; v.type = VAL_NIL; v.ival = 0; v.fval = 0; v.sval = NULL;
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

/* ---------- verse home dir ---------- */
static char self_dir[1024] = {0};
static const char *home_dir(void) {
    if (self_dir[0]) return self_dir;
    if (im_platform_executable_path(self_dir, sizeof self_dir) < 0) self_dir[0] = 0;
    char *s = strrchr(self_dir, '\\');
    if (s) *s = 0;
    return self_dir;
}
static void mk_universe_dir(const char *id) {
    char p[1200];
    snprintf(p, sizeof p, "%s\\universe", home_dir());
    im_platform_mkdirs(p);
    snprintf(p, sizeof p, "%s\\universe\\%s", home_dir(), id);
    im_platform_mkdirs(p);
}

/* ---------- content-addressed asset cache (ref://sha256) ---------- */
static char *read_file_buf(const char *path, int *len);  /* defined below */
static const char *cache_dir(void) {
    static char p[1400];
    snprintf(p, sizeof p, "%s\\universe\\_cache", home_dir());
    return p;
}
static void mk_cache_dir(void) {
    im_platform_mkdirs(cache_dir());
}
static int cache_has(const char *hex) {
    char fp[1500];
    snprintf(fp, sizeof fp, "%s\\%s", cache_dir(), hex);
    FILE *f = fopen(fp, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}
static void cache_put(const char *hex, const unsigned char *data, int len) {
    mk_cache_dir();
    char fp[1500];
    snprintf(fp, sizeof fp, "%s\\%s", cache_dir(), hex);
    FILE *f = fopen(fp, "wb");
    if (f) { fwrite(data, 1, (size_t)len, f); fclose(f); }
}
static unsigned char *cache_get(const char *hex, int *len) {
    char fp[1500];
    snprintf(fp, sizeof fp, "%s\\%s", cache_dir(), hex);
    return read_file_buf(fp, len);
}
static char *read_file_buf(const char *path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    fread(b, 1, (size_t)n, f); b[n] = 0; fclose(f);
    *len = (int)n;
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
        snprintf(outPath, sizeof outPath, "%s\\%s", outDir, name);
        for (char *p = outPath; *p; p++) if (*p == '/') *p = '\\';
        char *slash = strrchr(outPath, '\\');
        if (slash) { *slash = 0; verse_jar_mkdir_p(outPath); *slash = '\\'; }
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
    snprintf(p, sizeof p, "%s\\universe\\identity.seed", home_dir());
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
    LARGE_INTEGER pc; QueryPerformanceCounter(&pc);
    ULONGLONG t0 = GetTickCount64();
    unsigned char inp[64];
    memcpy(inp, &t0, 8); memcpy(inp + 8, &pc.QuadPart, 8);
    for (int i = 16; i < 64; i++) inp[i] = (unsigned char)((t0 >> (i % 8)) & 0xff) ^ (unsigned char)(i * 31);
    /* sha512_buf always writes 64 bytes; keep the Ed25519 seed at 32 bytes. */
    sha512_buf(inp, sizeof inp, digest);
    memcpy(seed, digest, sizeof seed);
    mk_universe_dir("_id");
    static const char *hx = "0123456789abcdef";
    char hex[65];
    for (int i = 0; i < 32; i++) { hex[i*2] = hx[seed[i] >> 4]; hex[i*2+1] = hx[seed[i] & 15]; }
    hex[64] = 0;
    FILE *f = fopen(identity_seed_path(), "wb");
    if (f) { fwrite(hex, 1, 64, f); fclose(f); }
    char pubhex[65];
    identity_pubkey(pubhex);
    int aidx = vm_array_new(vm);
    Value v; v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = _strdup(pubhex);
    vm_array_push(vm, aidx, &v);
    v.sval = _strdup(hex);
    vm_array_push(vm, aidx, &v);
    Value rv; rv.type = VAL_ARRAY; rv.ival = aidx + 1; rv.fval = 0; rv.sval = NULL;
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
    const char *data = r_str(vm, argc - 1);
    r_popn(vm, argc);
    int len = 0;
    char *seed = read_file_buf(identity_seed_path(), &len);
    if (!seed || len < 64) { free(seed); r_push_str(vm, _strdup("")); return 1; }
    unsigned char seedb[32], sig[64];
    for (int i = 0; i < 32; i++) {
        int hi = seed[i*2] >= 'a' ? seed[i*2]-'a'+10 : seed[i*2]-'0';
        int lo = seed[i*2+1] >= 'a' ? seed[i*2+1]-'a'+10 : seed[i*2+1]-'0';
        seedb[i] = (unsigned char)((hi << 4) | lo);
    }
    ed25519_sign(seedb, (const unsigned char*)data, strlen(data), sig);
    free(seed);
    static const char *hx = "0123456789abcdef";
    char hex[129];
    for (int i = 0; i < 64; i++) { hex[i*2] = hx[sig[i] >> 4]; hex[i*2+1] = hx[sig[i] & 15]; }
    hex[128] = 0;
    r_push_str(vm, _strdup(hex));
    return 1;
}
static int b_verse_verify(VM *vm) {
    int argc = vm->cur_argc;
    const char *data = r_str(vm, argc - 1);
    const char *sighex = r_str(vm, argc - 2);
    const char *pubhex = r_str(vm, argc - 3);
    r_popn(vm, argc);
    if (strlen(pubhex) != 64 || strlen(sighex) != 128) { r_push_int(vm, 0); return 1; }
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
    r_push_int(vm, ed25519_verify(pub, (const unsigned char*)data, strlen(data), sig));
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
    snprintf(hp, sizeof hp, "%s\\universe\\hubs.json", home_dir());
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
    snprintf(hp, sizeof hp, "%s\\universe\\hubs.json", home_dir());
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
    snprintf(base, sizeof base, "%s\\universe\\%s\\", home_dir(), id);
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
    snprintf(mp, sizeof mp, "%s\\universe\\%s\\verse.manifest", home_dir(), id);
    int llen = 0;
    char *local = read_file_buf(mp, &llen);
    char localVer[64] = "0.0.0";
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
            snprintf(tmp, sizeof tmp, "%s\\_upd_tmp", home_dir());
            verse_zip_extract(src + 14, tmp);
            char mp2[1400];
            snprintf(mp2, sizeof mp2, "%s\\verse.manifest", tmp);
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
            snprintf(base, sizeof base, "%s\\universe\\%s\\", home_dir(), id);
            ImDir *dir = im_dir_open(tmpdir);
            char entry[1024]; int entry_is_dir = 0;
            if (dir) {
                while (im_dir_next_ex(dir, entry, sizeof entry, &entry_is_dir)) {
                    if (entry_is_dir) continue;
                    if (strstr(entry, "manifest") && strcmp(entry, "verse.manifest") != 0) continue;
                    char sf[1400], df[1400];
                    snprintf(sf, sizeof sf, "%s\\%s", tmpdir, entry);
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
        Value v; v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = g_hubs[i];
        vm_array_push(vm, aidx, &v);
    }
    Value rv; rv.type = VAL_ARRAY; rv.ival = aidx + 1; rv.fval = 0; rv.sval = NULL;
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
    ULONGLONG t0 = GetTickCount64();
    int len = 0;
    char *b = http_get_body(url, &len);
    ULONGLONG dt = GetTickCount64() - t0;
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
    snprintf(mp, sizeof mp, "%s\\universe\\%s\\verse.manifest", home_dir(), id);
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
static int do_open(VM *vm, const char *uri) {
    char fetch_uri[1200], pub[65], ver[64], hash[65];
    verse_parse_uri(uri, fetch_uri, sizeof fetch_uri, pub, sizeof pub, ver, sizeof ver, hash, sizeof hash);
    int pkg_len = 0;
    char *pkg_json = verse_fetch(fetch_uri, &pkg_len);
    if (!pkg_json) { fprintf(stderr, "[VDP] download failed: %s\n", fetch_uri); return 0; }
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
    snprintf(base, sizeof base, "%s\\universe\\%s\\", home_dir(), m.id);
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "cmd /c start \"\" \"%s\\inimerse.exe\" \"%s%s\"", home_dir(), base, m.mainf);
    DWORD cpid = child_proc_spawn(cmd, "verse", 0);
    if (cpid) return 1;
    fprintf(stderr, "[VDP] launch failed: %s\n", cmd);
    return 0;
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
static int b_verse_pack(VM *vm) {
    int argc = vm->cur_argc;
    char *out = _strdup(r_str(vm, argc - 2) ? r_str(vm, argc - 2) : "");
    char *dir = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    int refMode = 0;
    if (argc >= 3) {
        Value rv = r_arg(vm, argc - 3);
        if (rv.type == VAL_INT) refMode = (int)rv.ival;
        else if (rv.type == VAL_STRING && rv.sval) refMode = atoi(rv.sval);
    }
        /* 12.3 meta: version (4th), min_version (5th), sources (6th+) - read BEFORE popn */
    char metaVer[128] = "1.0.0", metaMin[128] = "";
    char metaSrcs[16][512]; int nMetaSrcs = 0;
    if (argc >= 4) { const char *v4 = r_str(vm, argc - 4); if (v4 && v4[0]) snprintf(metaVer, sizeof metaVer, "%s", v4); }
    if (argc >= 5) { const char *v5 = r_str(vm, argc - 5); if (v5 && v5[0]) snprintf(metaMin, sizeof metaMin, "%s", v5); }
    for (int i = argc - 6; i >= 0 && nMetaSrcs < 16; i--) { const char *s = r_str(vm, i); if (s && s[0]) snprintf(metaSrcs[nMetaSrcs++], sizeof metaSrcs[0], "%s", s); }
    r_popn(vm, argc);
    /* gather files (dir\*.*) */
    char pat[1200];
    snprintf(pat, sizeof pat, "%s\\*.*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA(pat, &fd);
    if (hf == INVALID_HANDLE_VALUE) { r_push_int(vm, 0); return 1; }
    /* build files dict json manually to avoid dependency on VM during scan */
    char *files_json = malloc(65536); int fp = 0, fcap = 65536;
    char *b64all = malloc(65536); int ballen = 0, bacap = 65536;
    strcpy(files_json, "\"files\":{");
    fp = (int)strlen(files_json);
    int first = 1;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (strstr(fd.cFileName, ".vverse")) continue;
        char fpath[1300];
        snprintf(fpath, sizeof fpath, "%s\\%s", dir, fd.cFileName);
        int blen = 0;
        char *raw = read_file_buf(fpath, &blen);
        if (!raw) continue;
        char hex[65];
        sha256_hex(raw, (size_t)blen, hex);
        int refed = 0;
        if (refMode && cache_has(hex)) refed = 1;  /* already in shared cache: emit ref:// */
        if (!refed) cache_put(hex, (unsigned char*)raw, blen);  /* fill asset cache */
        int need = (int)strlen(fd.cFileName) + (int)strlen(hex) + 48;
        while (fp + need > fcap) {
            fcap *= 2; files_json = realloc(files_json, (size_t)fcap);
        }
        if (refed) {
            int n = snprintf(files_json + fp, (size_t)(fcap - fp), "%s\"%s\":\"ref://sha256:%s\"", first ? "" : ",", fd.cFileName, hex);
            fp += n;
        } else {
            char *b64 = b64_encode((unsigned char*)raw, blen);
            need = (int)strlen(fd.cFileName) + (int)strlen(b64) + 24;
            while (fp + need > fcap) {
                fcap *= 2; files_json = realloc(files_json, (size_t)fcap);
            }
            int n = snprintf(files_json + fp, (size_t)(fcap - fp), "%s\"%s\":\"%s\"", first ? "" : ",", fd.cFileName, b64);
            fp += n;
            size_t bl = strlen(b64);
            while (ballen + (int)bl + 1 > bacap) {
                bacap *= 2; b64all = realloc(b64all, (size_t)bacap);
            }
            memcpy(b64all + ballen, b64, bl); ballen += (int)bl;
            free(b64);
        }
        first = 0;
        free(raw);
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);
    if (fp + 8 > fcap) { fcap += 16; files_json = realloc(files_json, (size_t)fcap); }
    strcpy(files_json + fp, "}"); fp += 2;

    /* 12.x ed25519 signing (publisher identity) */
    char pubhex[65] = "", sighex[129] = "";
    if (identity_pubkey(pubhex)) {
        int slen = 0;
        char *seed = read_file_buf(identity_seed_path(), &slen);
        if (seed && slen >= 64) {
            unsigned char seedb[32], sigb[64];
            for (int si = 0; si < 32; si++) {
                int hi = seed[si*2] >= 97 ? seed[si*2]-97+10 : seed[si*2]-48;
                int lo = seed[si*2+1] >= 97 ? seed[si*2+1]-97+10 : seed[si*2+1]-48;
                seedb[si] = (unsigned char)((hi << 4) | lo);
            }
            
ed25519_sign(seedb, (const unsigned char*)b64all, (size_t)ballen, sigb);
            static const char *hx = "0123456789abcdef";
            for (int si = 0; si < 64; si++) { sighex[si*2] = hx[sigb[si] >> 4]; sighex[si*2+1] = hx[sigb[si] & 15]; }
            
sighex[128] = 0;
        }
        free(seed);
    }

    /* assemble meta json from values captured before popn */
    char metaJson[4096] = "";
    int mj = 0;
    mj += snprintf(metaJson + mj, sizeof metaJson - mj, ",\"version\":\"%s\"", metaVer);
    if (sighex[0]) {
        mj += snprintf(metaJson + mj, sizeof metaJson - mj, ",\"publisher\":\"%s\",\"signature\":\"%s\"", pubhex, sighex);
    }
    if (metaMin[0]) mj += snprintf(metaJson + mj, sizeof metaJson - mj, ",\"min_version\":\"%s\"", metaMin);
    if (nMetaSrcs > 0) {
        mj += snprintf(metaJson + mj, sizeof metaJson - mj, ",\"sources\":[");
        for (int i = 0; i < nMetaSrcs; i++) mj += snprintf(metaJson + mj, sizeof metaJson - mj, "%s\"%s\"", i == 0 ? "" : ",", metaSrcs[i]);
        mj += snprintf(metaJson + mj, sizeof metaJson - mj, "]");
    }

    /* assemble package json (hash = crc32, sha256 = strong digest of embedded b64s) */
    unsigned int crc = crc32_buf((unsigned char*)b64all, ballen);
    char sha256s[65];
    sha256_hex(b64all, (size_t)ballen, sha256s);
    free(b64all);
    char crcbuf[16]; snprintf(crcbuf, sizeof crcbuf, "%08x", crc);
    int pkgcap = fcap + 1024 + (int)strlen(metaJson) + 64;
    char *pkg = malloc(pkgcap);
    char idbuf[600];
    snprintf(idbuf, sizeof idbuf, "%s", strrchr(dir, '/') ? strrchr(dir, '/') + 1 : (strrchr(dir, '\\') ? strrchr(dir, '\\') + 1 : dir));
    int pn = snprintf(pkg, pkgcap, "{\"id\":\"%s\"%s,\"main\":\"main.im\",%s,\"hash\":\"%s\",\"sha256\":\"%s\"}",
                      idbuf, metaJson, files_json, crcbuf, sha256s);
    while (pn >= pkgcap) {
        pkgcap *= 2;
        pkg = realloc(pkg, pkgcap);
        pn = snprintf(pkg, pkgcap, "{\"id\":\"%s\"%s,\"main\":\"main.im\",%s,\"hash\":\"%s\",\"sha256\":\"%s\"}",
                      idbuf, metaJson, files_json, crcbuf, sha256s);
    }
    free(files_json);

    FILE *f = fopen(out, "wb");
    if (f) { fwrite(pkg, 1, strlen(pkg), f); fclose(f); }
    free(pkg);
    if (f) { r_push_str(vm, _strdup(out)); }
    else { r_push_str(vm, _strdup("")); }
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
    snprintf(universe, sizeof universe, "%s\\universe", home_dir());
    ImDir *dir = im_dir_open(universe);
    char name[1024]; int is_dir = 0;
    if (dir) {
        while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
            if (is_dir) {
                const char *s = vm_intern(vm, name);
                Value v; v.type=VAL_STRING; v.ival=1; v.fval=0; v.sval=(char*)(s?s:name);
                vm_array_push(vm, aidx, &v);
            }
        }
        im_dir_close(dir);
    }
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1; a.fval = 0; a.sval = NULL;
    r_push(vm, a);
    return 1;
}

static int b_verse_remove(VM *vm) {
    int argc = vm->cur_argc;
    char *id = _strdup(r_str(vm, argc - 1) ? r_str(vm, argc - 1) : "");
    r_popn(vm, argc);
    char path[1200];
    snprintf(path, sizeof path, "%s\\universe\\%s", home_dir(), id);
    /* simple recursive delete via SHFileOperation or manual */
    ImDir *dir = im_dir_open(path);
    char name[1024]; int is_dir = 0;
    if (!dir) { free(id); r_push_int(vm, 0); return 1; }
    while (im_dir_next_ex(dir, name, sizeof name, &is_dir)) {
        if (is_dir) continue;
        char fp[1400]; snprintf(fp, sizeof fp, "%s\\%s", path, name);
        remove(fp);
    }
    im_dir_close(dir);
    RemoveDirectoryA(path);
    free(id);
    r_push_int(vm, 1);
    return 1;
}

/* ---------- register ---------- */

/* ---------- verse_listen(port): local HTTP server for packages ---------- */
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
        snprintf(fp, sizeof fp, "%s\\%s", home_dir(), rel[0] ? rel : "projects");
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
        char universe[1200]; snprintf(universe, sizeof universe, "%s\\universe", home_dir());
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
    snprintf(fp, sizeof fp, "%s\\universe\\%s\\%s.vverse", home_dir(), id, id);
    FILE *f = fopen(fp, "rb");
    if (!f) {
        snprintf(fp, sizeof fp, "%s\\%s.vverse", home_dir(), id);
        f = fopen(fp, "rb");
    }
        if (!f) {
            snprintf(fp, sizeof fp, "%s\\universe\\_hub\\%s.vverse", home_dir(), id);
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
                snprintf(hubdir, sizeof hubdir, "%s\\universe\\_hub", home_dir());
                im_platform_mkdirs(hubdir);
                char hfp[1400];
                snprintf(hfp, sizeof hfp, "%s\\%s.vverse", hubdir, pid);
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
                    snprintf(fp, sizeof fp, "%s\\%s", home_dir(), rel[0] ? rel : "x.im");
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
                    snprintf(fp, sizeof fp, "%s\\%s", home_dir(), rel[0] ? rel : "x.im");
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
    int port = (pv.type == VAL_INT) ? pv.ival : (int)pv.fval;
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

void verse_dist_mod_register(VM *vm) {
    vm_register_builtin_full(vm, "verse_open", b_verse_open, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_pack", b_verse_pack, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_share", b_verse_share, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_listen", b_verse_listen, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_stop", b_verse_stop, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_hub_list", b_verse_hub_list, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_update", b_verse_update, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hub_add", b_verse_hub_add, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hub_remove", b_verse_hub_remove, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hubs", b_verse_hubs, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_hub_ping", b_verse_hub_ping, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_public_ip", b_verse_public_ip, 1|CAP_VERSE|CAP_NET, 0);    vm_register_builtin_full(vm, "verse_publish", b_verse_publish, 1|CAP_VERSE|CAP_NET, 0);
    vm_register_builtin_full(vm, "verse_identity_new", b_verse_identity_new, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_identity_pubkey", b_verse_identity_pubkey, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_sign", b_verse_sign, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_verify", b_verse_verify, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_list", b_verse_list, 1|CAP_VERSE, 0);
    vm_register_builtin_full(vm, "verse_remove", b_verse_remove, 1|CAP_VERSE, 0);
    printf("[verse_dist mod] VDP loaded (verse://<hub>/<id>)\n");
}
