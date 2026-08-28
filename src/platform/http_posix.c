#include "socket.h"
#include "websocket.h"
#include "crp_session.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#endif

static ImSocket *g_http_listener;
static pthread_t g_http_thread;
static volatile int g_http_running;
static ImSocket *g_ws_clients[32];
static pthread_mutex_t g_ws_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_token_counter;
static char g_tokens[128][64];
static char g_revoked[128][64];
static int g_token_count, g_revoked_count;
static char g_verses[128][128];
static int g_verse_count;
static int token_known(const char *token) { if (!token || !*token) return 0; for (int i = 0; i < g_token_count; ++i) if (!strcmp(g_tokens[i], token)) return 1; return 0; }
static int token_revoked(const char *token) { for (int i = 0; i < g_revoked_count; ++i) if (!strcmp(g_revoked[i], token)) return 1; return 0; }
static void token_register(const char *token) { if (g_token_count < 128) snprintf(g_tokens[g_token_count++], sizeof g_tokens[0], "%s", token); }
static void token_revoke(const char *token) { if (!token_revoked(token) && g_revoked_count < 128) snprintf(g_revoked[g_revoked_count++], sizeof g_revoked[0], "%s", token); }

static int safe_id(const char *id) { return id && *id && !strstr(id, "..") && !strchr(id, '/') && !strchr(id, '\\') && !strchr(id, ':'); }
static const char *find_ci(const char *haystack, const char *needle) {
    size_t n = strlen(needle); if (!n) return haystack;
    for (const char *p = haystack; *p; ++p) { size_t i = 0; while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i; if (i == n) return p; }
    return NULL;
}
static void ensure_dir(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}
static int b64val(char c) { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; return c == '+' ? 62 : c == '/' ? 63 : -1; }
static size_t b64decode(const char *s, unsigned char *out, size_t cap) {
    size_t n = 0; int val = 0, bits = -8; int invalid = 0;
    for (; *s; ++s) {
        if (*s == '=') break;
        int x = b64val(*s); if (x < 0) { invalid = 1; continue; }
        val = (val << 6) | x; bits += 6;
        if (bits >= 0) { if (n >= cap) return 0; out[n++] = (unsigned char)((val >> bits) & 255); bits -= 8; }
    }
    return invalid || bits > -2 ? 0 : n;
}
static const char *json_value(const char *json, const char *name, char *out, size_t cap) { char key[64]; snprintf(key, sizeof key, "\"%s\"", name); const char *p = strstr(json, key); if (!p) return NULL; p = strchr(p + strlen(key), ':'); if (!p) return NULL; while (*++p == ' ' || *p == '\t') {} if (*p != '"') return NULL; ++p; size_t i = 0; while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++; out[i] = 0; return *p == '"' ? out : NULL; }
static int write_atomic(const char *path, const unsigned char *data, size_t len) { char tmp[1500]; snprintf(tmp, sizeof tmp, "%s.tmp.%lu", path, (unsigned long)getpid()); FILE *f = fopen(tmp, "wb"); if (!f) return 0; int ok = fwrite(data, 1, len, f) == len; if (fclose(f) != 0) ok = 0; if (ok) ok = rename(tmp, path) == 0; if (!ok) remove(tmp); return ok; }
static size_t hub_body(const char *request, char *body, size_t cap, int *status) {
    const char *root = getenv("INIMERSE_HUB_DIR"); if (!root || !*root) root = "./universe";
    if (strstr(request, "GET /packages") == request) {
        DIR *d = opendir(root); size_t used = 0; body[used++] = '[';
        if (d) { struct dirent *e; int first = 1; while ((e = readdir(d)) && used + 80 < cap) { const char *dot = strstr(e->d_name, ".vverse"); if (!dot || dot[7]) continue; used += (size_t)snprintf(body + used, cap - used, "%s\"%.*s\"", first ? "" : ",", (int)(dot - e->d_name), e->d_name); first = 0; } closedir(d); }
        if (used + 2 < cap) { body[used++] = ']'; body[used] = 0; } *status = 200; return used;
    }
    const char *p = strstr(request, "GET /package/"); if (p == request) {
        p += 13; char id[128]; size_t i = 0; while (p[i] && p[i] != ' ' && p[i] != '?' && i + 1 < sizeof id) { id[i] = p[i]; ++i; } id[i] = 0;
        if (!safe_id(id)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_id\"}\n"); }
        char path[1400]; snprintf(path, sizeof path, "%s/%s.vverse", root, id); FILE *f = fopen(path, "rb"); if (!f) { *status = 404; return (size_t)snprintf(body, cap, "{\"error\":\"not_found\"}\n"); }
        size_t n = fread(body, 1, cap, f); fclose(f); *status = 200; return n;
    }
    if (strstr(request, "POST /package/fork") == request) {
        char source[128], id[128]; if (!json_value(request, "source", source, sizeof source) || !json_value(request, "id", id, sizeof id) || !safe_id(source) || !safe_id(id)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"source_and_id_required\"}\n"); }
        char src[1400], dst[1400]; snprintf(src, sizeof src, "%s/%s.vverse", root, source); snprintf(dst, sizeof dst, "%s/%s.vverse", root, id); FILE *f = fopen(src, "rb"); if (!f) { *status = 404; return (size_t)snprintf(body, cap, "{\"error\":\"source_not_found\"}\n"); }
        unsigned char data[65536]; size_t len = fread(data, 1, sizeof data, f); fclose(f); if (!write_atomic(dst, data, len)) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"write_failed\"}\n"); }
        *status = 201; return (size_t)snprintf(body, cap, "{\"id\":\"%s\",\"forkOf\":\"%s\",\"size\":%zu}\n", id, source, len);
    }
    if (strstr(request, "POST /package") == request) {
        char id[128], encoded[65536]; if (!json_value(request, "id", id, sizeof id) || !json_value(request, "data", encoded, sizeof encoded) || !safe_id(id)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"id_and_data_required\"}\n"); }
        unsigned char data[49152]; size_t len = b64decode(encoded, data, sizeof data); if (!len) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_data\"}\n"); }
        char path[1400]; snprintf(path, sizeof path, "%s/%s.vverse", root, id); ensure_dir(root); if (!write_atomic(path, data, len)) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"write_failed\"}\n"); }
        *status = 201; return (size_t)snprintf(body, cap, "{\"id\":\"%s\",\"size\":%zu}\n", id, len);
    }
    if (strstr(request, "DELETE /package/") == request) {
        p = strstr(request, "/package/") + 9; char id[128]; size_t i = 0; while (p[i] && p[i] != ' ' && i + 1 < sizeof id) { id[i] = p[i]; ++i; } id[i] = 0; if (!safe_id(id)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_id\"}\n"); }
        char path[1400]; snprintf(path, sizeof path, "%s/%s.vverse", root, id); *status = remove(path) == 0 ? 200 : 404; return (size_t)snprintf(body, cap, *status == 200 ? "{\"deleted\":true}\n" : "{\"error\":\"not_found\"}\n");
    }
    return 0;
}

static int json_field_string(const char *json, const char *name, char *out, size_t cap) {
    char key[64]; snprintf(key, sizeof key, "\"%s\"", name); const char *p = strstr(json, key); if (!p) return 0;
    p = strchr(p + strlen(key), ':'); if (!p) return 0; while (*++p == ' ' || *p == '\t') {}
    if (*p != '"') return 0;
    ++p; size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++;
    out[i] = 0; return *p == '"';
}
static int json_field_u64(const char *json, const char *name, uint64_t *out) {
    char key[64]; snprintf(key, sizeof key, "\"%s\"", name); const char *p = strstr(json, key); if (!p) return 0;
    p = strchr(p + strlen(key), ':'); if (!p) return 0; while (*++p == ' ' || *p == '\t') {}
    uint64_t v = 0; int digits = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned)(*p++ - '0'); digits = 1; } if (digits) *out = v; return digits;
}

static void *http_loop(void *unused) {
    (void)unused;
    while (g_http_running) {
        ImSocket *client = im_socket_accept(g_http_listener);
        if (!client) { struct timespec ts = {0, 20000000L}; nanosleep(&ts, NULL); continue; }
        (void)im_socket_set_nonblocking(client, 0);
        char req[65536]; int n = 0, header_end = -1, content_len = 0;
        for (;;) {
            int k = im_socket_recv(client, req + n, sizeof(req) - 1 - (size_t)n);
            if (k <= 0) break;
            n += k; req[n] = 0;
            char *end = strstr(req, "\r\n\r\n");
            if (end && header_end < 0) {
                header_end = (int)(end - req) + 4;
                const char *cl = find_ci(req, "Content-Length:");
                if (cl && cl < end) content_len = atoi(cl + 15);
                if (content_len < 0 || content_len > (int)sizeof(req) - header_end - 1) { n = -1; break; }
            }
            if (header_end >= 0 && n >= header_end + content_len) break;
            if (n >= (int)sizeof(req) - 1) { n = -1; break; }
        }
        if (n <= 0) { im_socket_close(client); continue; }
        req[n] = 0;
        if (n > 0 && strstr(req, "GET /ws") && find_ci(req, "Upgrade: websocket")) {
            if (im_ws_accept(client, req, (size_t)n) == 0) {
                ImCrpSession session; im_crp_session_init(&session, 1);
                pthread_mutex_lock(&g_ws_lock); int slot = -1; for (int i = 0; i < 32; ++i) if (!g_ws_clients[i]) { g_ws_clients[i] = client; slot = i; break; } pthread_mutex_unlock(&g_ws_lock);
                if (slot < 0) { im_ws_send_text(client, "{\"error\":\"too_many_connections\"}", 34); im_socket_close(client); continue; }
                char frame[65536];
                for (;;) {
                    int flen = im_ws_read_text(client, frame, sizeof frame);
                    if (flen == -2) { (void)im_ws_send_pong(client, NULL, 0); continue; }
                    if (flen <= 0) break;
                    char type[32]; uint64_t seq = 0;
                    if (json_field_string(frame, "type", type, sizeof type)) {
                        (void)json_field_u64(frame, "seq", &seq);
                        int state_rc = im_crp_session_apply(&session, type, seq, 0, NULL);
                        if (state_rc < 0) {
                            const char *err = "{\"error\":\"invalid_session_transition\"}";
                            (void)im_ws_send_text(client, err, strlen(err));
                            continue;
                        }
                    }
                    int broadcast_ok = 1; pthread_mutex_lock(&g_ws_lock);
                    for (int i = 0; i < 32; ++i) if (g_ws_clients[i] && g_ws_clients[i] != client) if (im_ws_send_text(g_ws_clients[i], frame, (size_t)flen) != 0) broadcast_ok = 0;
                    pthread_mutex_unlock(&g_ws_lock);
                    if (!broadcast_ok) break;
                }
                pthread_mutex_lock(&g_ws_lock); if (slot >= 0 && g_ws_clients[slot] == client) g_ws_clients[slot] = NULL; pthread_mutex_unlock(&g_ws_lock);
            }
            im_socket_close(client);
            continue;
        }
        int health = n > 0 && strstr(req, "GET /health") != NULL;
        int find = n > 0 && strstr(req, "GET /find") != NULL;
        int signal = n > 0 && strstr(req, "POST /signal") != NULL;
        int revoke = n > 0 && strstr(req, "POST /revoke") != NULL;
        int register_route = n > 0 && strstr(req, "POST /register") != NULL;
        int portal = n > 0 && strstr(req, "POST /portal") != NULL;
        int status = 200;
        char request_token[64] = "";
        (void)json_field_string(req, "token", request_token, sizeof request_token);
        if (signal && (!token_known(request_token) || token_revoked(request_token))) { signal = 0; status = 403; }
        char hubbuf[65536]; size_t hublen = hub_body(req, hubbuf, sizeof hubbuf, &status); int hub = hublen > 0;
        int ok = health || find || signal || revoke || register_route || portal || hub;
        char findbuf[4096];
        if (find) {
            size_t used = 0; used += (size_t)snprintf(findbuf + used, sizeof findbuf - used, "{\"items\":[");
            for (int i = 0; i < g_verse_count && used + 160 < sizeof findbuf; ++i) used += (size_t)snprintf(findbuf + used, sizeof findbuf - used, "%s{\"id\":\"%s\",\"endpoint\":\"local\"}", i ? "," : "", g_verses[i]);
            snprintf(findbuf + used, sizeof findbuf - used, "]}\n");
        }
        const char *body = health ? "{\"ok\":true,\"service\":\"inimerse\"}\n" :
            find ? findbuf :
            signal ? "{\"ok\":true,\"accepted\":true}\n" :
            revoke ? "{\"ok\":true,\"revoked\":true}\n" : hub ? hubbuf : "{\"error\":\"not_found\"}\n";
        char portalbuf[256];
        if (register_route) {
            char vid[128];
            if (json_field_string(req, "id", vid, sizeof vid) && safe_id(vid)) {
                int seen = 0; for (int i = 0; i < g_verse_count; ++i) if (!strcmp(g_verses[i], vid)) seen = 1;
                if (!seen && g_verse_count < 128) snprintf(g_verses[g_verse_count++], sizeof g_verses[0], "%s", vid);
                body = "{\"ok\":true}\n";
            } else { status = 400; body = "{\"error\":\"id_required\"}\n"; }
        }
        if (portal) {
            unsigned long seed = (unsigned long)time(NULL) ^ ++g_token_counter;
            snprintf(portalbuf, sizeof portalbuf, "{\"token\":\"posix-%lx\",\"expires\":%lu}\n", seed, (unsigned long)time(NULL) + 300);
            char issued[64]; snprintf(issued, sizeof issued, "posix-%lx", seed); token_register(issued);
            body = portalbuf;
        }
        if (revoke) { if (!request_token[0]) { status = 400; body = "{\"error\":\"token_required\"}\n"; } else { token_revoke(request_token); body = "{\"revoked\":true}\n"; } }
        size_t body_len = hub ? hublen : strlen(body); const char *status_text = status == 201 ? "201 Created" : status == 400 ? "400 Bad Request" : status == 403 ? "403 Forbidden" : (status == 404 || !ok) ? "404 Not Found" : "200 OK";
        const char *content_type = (hub && strstr(req, "GET /package/") == req) ? "application/octet-stream" : "application/json";
        char out[512]; int len = snprintf(out, sizeof out, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", status_text, content_type, body_len);
        for (int sent = 0; len > 0 && sent < len;) {
            int nout = im_socket_send(client, out + sent, (size_t)(len - sent));
            if (nout <= 0) break;
            sent += nout;
        }
        for (size_t sent = 0; sent < body_len;) { int nout = im_socket_send(client, body + sent, body_len - sent); if (nout <= 0) break; sent += (size_t)nout; }
        im_socket_close(client);
    }
    return NULL;
}

int verse_http_start(int port) {
    if (g_http_running || port < 1 || port > 65535 || im_socket_init() != 0) return g_http_running ? 1 : 0;
    g_http_listener = im_socket_listen("127.0.0.1", (uint16_t)port, 16);
    if (!g_http_listener || im_socket_set_nonblocking(g_http_listener, 1) != 0) { if (g_http_listener) im_socket_close(g_http_listener); g_http_listener = NULL; im_socket_shutdown(); return 0; }
    g_http_running = 1;
    if (pthread_create(&g_http_thread, NULL, http_loop, NULL) != 0) { g_http_running = 0; im_socket_close(g_http_listener); g_http_listener = NULL; im_socket_shutdown(); return 0; }
    return 1;
}

void verse_http_stop(void) {
    if (!g_http_running) return;
    g_http_running = 0;
    pthread_join(g_http_thread, NULL);
    if (g_http_listener) { im_socket_close(g_http_listener); g_http_listener = NULL; }
    im_socket_shutdown();
}
