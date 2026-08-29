#include "socket.h"
#include "dir.h"
#include "websocket.h"
#include "crp_session.h"
#include "../common/sha256.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
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
typedef struct { char value[64], verse[128], peer[128]; time_t expires; } ImToken;
static ImToken g_tokens[128];
static char g_revoked[128][64];
static int g_token_count, g_revoked_count;
static char g_verses[128][128];
static int g_verse_count;
typedef struct { char id[128], verse[128], name[128], endpoint[256]; } ImFriend;
typedef struct { char verse[128], peer[128]; uint64_t seq; int stopped; char event[16][256]; uint64_t event_seq[16]; int event_count; } ImSession;
static ImFriend g_friends[256]; static int g_friend_count;
static ImSession g_sessions[256]; static int g_session_count;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_state_loaded;

/* Durable peer/session registry.  The file is deliberately line-oriented so
 * a partially written record can be ignored safely after a crash. */
static const char *state_path(void) {
    const char *p = getenv("INIMERSE_STATE_FILE");
    return (p && *p) ? p : "./universe/.inimerse_state";
}
static void state_save(void) {
    char tmp[1400]; snprintf(tmp, sizeof tmp, "%s.tmp.%lu", state_path(), (unsigned long)getpid());
    FILE *f = fopen(tmp, "wb"); if (!f) return;
    pthread_mutex_lock(&g_state_lock);
    for (int i = 0; i < g_friend_count; ++i) fprintf(f, "F\t%s\t%s\t%s\t%s\n", g_friends[i].id, g_friends[i].verse, g_friends[i].name, g_friends[i].endpoint);
    for (int i = 0; i < g_session_count; ++i) fprintf(f, "S\t%s\t%s\t%llu\t%d\n", g_sessions[i].verse, g_sessions[i].peer, (unsigned long long)g_sessions[i].seq, g_sessions[i].stopped);
    for (int i = 0; i < g_session_count; ++i) for (int j = 0; j < g_sessions[i].event_count; ++j) fprintf(f, "E\t%s\t%s\t%llu\t%s\n", g_sessions[i].verse, g_sessions[i].peer, (unsigned long long)g_sessions[i].event_seq[j], g_sessions[i].event[j]);
    pthread_mutex_unlock(&g_state_lock);
    if (fclose(f) == 0) rename(tmp, state_path()); else remove(tmp);
}
static void state_load(void) {
    FILE *f = fopen(state_path(), "rb"); if (!f) return;
    char line[768];
    while (fgets(line, sizeof line, f)) {
        char *p = strchr(line, '\n'); if (p) *p = 0;
        char *a = strtok(line, "\t"), *b = strtok(NULL, "\t"), *c = strtok(NULL, "\t"), *d = strtok(NULL, "\t"), *e = strtok(NULL, "\t");
        if (!a || !b || !c) continue;
        pthread_mutex_lock(&g_state_lock);
        if (!strcmp(a, "F") && d && g_friend_count < 256) {
            ImFriend *x = &g_friends[g_friend_count++]; snprintf(x->id, sizeof x->id, "%s", b); snprintf(x->verse, sizeof x->verse, "%s", c);
            snprintf(x->name, sizeof x->name, "%s", d); snprintf(x->endpoint, sizeof x->endpoint, "%s", e ? e : "");
        } else if (!strcmp(a, "S") && d && g_session_count < 256) {
            ImSession *x = &g_sessions[g_session_count++]; snprintf(x->verse, sizeof x->verse, "%s", b); snprintf(x->peer, sizeof x->peer, "%s", c); x->seq = strtoull(d, NULL, 10); x->stopped = e ? atoi(e) != 0 : 0;
        } else if (!strcmp(a, "E") && d) {
            int at = -1; for (int i = 0; i < g_session_count; ++i) if (!strcmp(g_sessions[i].verse, b) && !strcmp(g_sessions[i].peer, c)) { at = i; break; }
            if (at >= 0 && g_sessions[at].event_count < 16) { ImSession *x = &g_sessions[at]; x->event_seq[x->event_count] = strtoull(d, NULL, 10); snprintf(x->event[x->event_count], sizeof x->event[0], "%s", e ? e : ""); x->event_count++; }
        }
        pthread_mutex_unlock(&g_state_lock);
    }
    fclose(f);
}
static int token_known(const char *token) {
    if (!token || !*token) return 0;
    time_t now = time(NULL);
    for (int i = 0; i < g_token_count; ++i)
        if (!strcmp(g_tokens[i].value, token) && g_tokens[i].expires > now) return 1;
    return 0;
}
static int token_revoked(const char *token) { for (int i = 0; i < g_revoked_count; ++i) if (!strcmp(g_revoked[i], token)) return 1; return 0; }
static void token_register(const char *token, time_t expires, const char *verse, const char *peer) {
    if (g_token_count >= 128 || !token || !*token) return;
    snprintf(g_tokens[g_token_count].value, sizeof g_tokens[0].value, "%s", token);
    g_tokens[g_token_count].expires = expires;
    snprintf(g_tokens[g_token_count].verse, sizeof g_tokens[0].verse, "%s", verse ? verse : "");
    snprintf(g_tokens[g_token_count].peer, sizeof g_tokens[0].peer, "%s", peer ? peer : "");
    g_token_count++;
}
static int token_allows(const char *token, const char *verse, const char *peer) {
    if (!token_known(token) || token_revoked(token)) return 0;
    for (int i = 0; i < g_token_count; ++i) if (!strcmp(g_tokens[i].value, token))
        return (!g_tokens[i].verse[0] || (verse && !strcmp(g_tokens[i].verse, verse))) && (!g_tokens[i].peer[0] || (peer && !strcmp(g_tokens[i].peer, peer)));
    return 0;
}
static void token_revoke(const char *token) { if (!token_revoked(token) && g_revoked_count < 128) snprintf(g_revoked[g_revoked_count++], sizeof g_revoked[0], "%s", token); }

static int safe_id(const char *id) { return id && *id && !strstr(id, "..") && !strchr(id, '/') && !strchr(id, '\\') && !strchr(id, ':'); }
static int valid_hash(const char *h) { if (!h || strlen(h) != 64) return 0; for (int i = 0; i < 64; ++i) if (!((h[i] >= '0' && h[i] <= '9') || (h[i] >= 'a' && h[i] <= 'f'))) return 0; return 1; }
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
static const char *json_value(const char *json, const char *name, char *out, size_t cap);
static size_t hub_body(const char *request, char *body, size_t cap, int *status) {
    const char *root = getenv("INIMERSE_HUB_DIR"); if (!root || !*root) root = "./universe";
    if (strstr(request, "POST /content") == request) {
        char encoded[65536]; if (!json_value(request, "data", encoded, sizeof encoded)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"data_required\"}\n"); }
        unsigned char data[49152]; size_t len = b64decode(encoded, data, sizeof data); if (!len) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_data\"}\n"); }
        char hash[65]; sha256_hex(data, len, hash); char path[1400]; snprintf(path, sizeof path, "%s/content-%s", root, hash); ensure_dir(root);
        if (!write_atomic(path, data, len)) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"write_failed\"}\n"); }
        *status = 201; return (size_t)snprintf(body, cap, "{\"hash\":\"%s\",\"size\":%zu,\"uri\":\"ref://sha256:%s\"}\n", hash, len, hash);
    }
    const char *content = strstr(request, "GET /content/"); if (content == request) {
        content += 13; char hash[65]; size_t i = 0; while (content[i] && content[i] != ' ' && i < 64) { hash[i] = content[i]; ++i; } hash[i] = 0;
        if (!valid_hash(hash)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_hash\"}\n"); }
        char path[1400]; snprintf(path, sizeof path, "%s/content-%s", root, hash); FILE *f = fopen(path, "rb"); if (!f) { *status = 404; return (size_t)snprintf(body, cap, "{\"error\":\"content_not_found\"}\n"); }
        size_t len = fread(body, 1, cap, f); fclose(f); unsigned char got[32]; sha256_digest(body, len, got); char check[65]; sha256_hex_of_digest(got, check); if (strcmp(check, hash)) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"content_corrupt\"}\n"); } *status = 200; return len;
    }
    if (strstr(request, "GET /packages") == request) {
        char query[128] = ""; const char *qp = strstr(request, "?q="); if (qp) { qp += 3; size_t qi = 0; while (qp[qi] && qp[qi] != ' ' && qp[qi] != '&' && qi + 1 < sizeof query) { query[qi] = qp[qi]; qi++; } query[qi] = 0; }
        ImDir *d = im_dir_open(root); size_t used = 0; body[used++] = '[';
        if (d) { char entry[256]; int isdir = 0, first = 1; while (im_dir_next_ex(d, entry, sizeof entry, &isdir) > 0 && used + 80 < cap) { if (isdir) continue; const char *dot = strstr(entry, ".vverse"); if (!dot || dot[7]) continue; char id[128]; snprintf(id, sizeof id, "%.*s", (int)(dot - entry), entry); if (*query && !strstr(id, query)) continue; used += (size_t)snprintf(body + used, cap - used, "%s\"%s\"", first ? "" : ",", id); first = 0; } im_dir_close(d); }
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
            /* Authentication is opt-in for backwards compatibility; deployments can
             * require a portal-issued token with CRP_REQUIRE_AUTH=1. */
            const char *auth_required = getenv("CRP_REQUIRE_AUTH");
            if (auth_required && strcmp(auth_required, "1") == 0) {
                char ws_token[64] = ""; const char *q = strstr(req, "token=");
                if (q) { q += 6; size_t j = 0; while (q[j] && q[j] != '&' && q[j] != ' ' && j + 1 < sizeof ws_token) { ws_token[j] = q[j]; j++; } ws_token[j] = 0; }
                if (!token_known(ws_token) || token_revoked(ws_token)) {
                    const char *deny = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    (void)im_socket_send(client, deny, strlen(deny)); im_socket_close(client); continue;
                }
            }
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
        int friends_get = n > 0 && strstr(req, "GET /friends") != NULL;
        int friends_post = n > 0 && strstr(req, "POST /friends") != NULL;
        int resume = n > 0 && strstr(req, "POST /session/resume") != NULL;
        int session_start = n > 0 && strstr(req, "POST /session/start") != NULL;
        int session_stop = n > 0 && strstr(req, "POST /session/stop") != NULL;
        int signal = n > 0 && (strstr(req, "POST /signal") != NULL || strstr(req, "POST /session/heartbeat") != NULL);
        if (session_start) resume = 1;
        int revoke = n > 0 && strstr(req, "POST /revoke") != NULL;
        int register_route = n > 0 && strstr(req, "POST /register") != NULL;
        int route_post = n > 0 && (strstr(req, "POST /route") != NULL || strstr(req, "POST /nat/candidate") != NULL);
        int route_get = n > 0 && strstr(req, "GET /route/") != NULL;
        int nat_get = n > 0 && strstr(req, "GET /nat/candidates") != NULL;
        int portal = n > 0 && strstr(req, "POST /portal") != NULL;
        int status = 200;
        char request_token[64] = "";
        (void)json_field_string(req, "token", request_token, sizeof request_token);
        char signal_verse[128] = "", signal_peer[128] = "";
        (void)json_field_string(req, "verse", signal_verse, sizeof signal_verse); (void)json_field_string(req, "peer", signal_peer, sizeof signal_peer);
        if (signal && !token_allows(request_token, signal_verse, signal_peer)) { signal = 0; status = 403; }
        char hubbuf[65536]; size_t hublen = hub_body(req, hubbuf, sizeof hubbuf, &status); int hub = hublen > 0;
        int ok = health || find || friends_get || friends_post || resume || session_stop || signal || revoke || register_route || route_post || route_get || nat_get || portal || hub;
        char findbuf[4096];
        if (find) {
            size_t used = 0; used += (size_t)snprintf(findbuf + used, sizeof findbuf - used, "{\"items\":[");
            for (int i = 0; i < g_verse_count && used + 160 < sizeof findbuf; ++i) used += (size_t)snprintf(findbuf + used, sizeof findbuf - used, "%s{\"id\":\"%s\",\"endpoint\":\"local\"}", i ? "," : "", g_verses[i]);
            snprintf(findbuf + used, sizeof findbuf - used, "]}\n");
        }
        char friendsbuf[8192];
        if (friends_get) {
            char query[128] = ""; const char *q = strstr(req, "?verse=");
            if (q) { q += 7; size_t j = 0; while (q[j] && q[j] != ' ' && q[j] != '&' && j + 1 < sizeof query) { query[j] = q[j]; j++; } query[j] = 0; }
            size_t used = 0; used += (size_t)snprintf(friendsbuf + used, sizeof friendsbuf - used, "{\"items\":[");
            int first = 1; for (int i = 0; i < g_friend_count && used + 420 < sizeof friendsbuf; ++i) {
                ImFriend *f = &g_friends[i]; if (*query && strcmp(query, f->verse) != 0) continue;
                used += (size_t)snprintf(friendsbuf + used, sizeof friendsbuf - used, "%s{\"id\":\"%s\",\"verse\":\"%s\",\"name\":\"%s\",\"endpoint\":\"%s\"}", first ? "" : ",", f->id, f->verse, f->name, f->endpoint); first = 0;
            }
            snprintf(friendsbuf + used, sizeof friendsbuf - used, "]}\n");
        }
        const char *body = health ? "{\"ok\":true,\"service\":\"inimerse\"}\n" :
            find ? findbuf :
            friends_get ? friendsbuf :
            friends_post ? "{\"ok\":true}\n" :
            resume ? "{\"resumed\":true}\n" :
            session_stop ? "{\"stopped\":true}\n" :
            signal ? "{\"ok\":true,\"accepted\":true}\n" :
            revoke ? "{\"ok\":true,\"revoked\":true}\n" : hub ? hubbuf : "{\"error\":\"not_found\"}\n";
        char routebuf[512];
        if (route_get) {
            const char *rp = strstr(req, "GET /route/") + 11; char rid[128] = ""; size_t ri = 0;
            while (rp[ri] && rp[ri] != ' ' && ri + 1 < sizeof rid) { rid[ri] = rp[ri]; ri++; } rid[ri] = 0;
            int found = 0; for (int i = 0; i < g_friend_count; ++i) if (!strcmp(g_friends[i].id, rid)) { snprintf(routebuf, sizeof routebuf, "{\"id\":\"%s\",\"endpoint\":\"%s\"}\n", rid, g_friends[i].endpoint); found = 1; break; }
            if (!found) { status = 404; snprintf(routebuf, sizeof routebuf, "{\"error\":\"route_not_found\"}\n"); } body = routebuf;
        }
        if (route_post) {
            char rid[128] = "", endpoint[256] = "";
            if (!json_field_string(req, "id", rid, sizeof rid) || !json_field_string(req, "endpoint", endpoint, sizeof endpoint) || !safe_id(rid) || !endpoint[0]) { status = 400; body = "{\"error\":\"id_and_endpoint_required\"}\n"; }
            else { int at = -1; for (int i = 0; i < g_friend_count; ++i) if (!strcmp(g_friends[i].id, rid)) { at = i; break; } if (at < 0 && g_friend_count < 256) at = g_friend_count++; if (at < 0) { status = 507; body = "{\"error\":\"route_registry_full\"}\n"; } else { ImFriend *f = &g_friends[at]; snprintf(f->id, sizeof f->id, "%s", rid); snprintf(f->endpoint, sizeof f->endpoint, "%s", endpoint); state_save(); body = "{\"ok\":true}\n"; } }
        }
        char natbuf[4096];
        if (nat_get) {
            size_t used = (size_t)snprintf(natbuf, sizeof natbuf, "{\"candidates\":["); int first = 1;
            for (int i = 0; i < g_friend_count && used < sizeof natbuf - 320; ++i) if (g_friends[i].endpoint[0]) used += (size_t)snprintf(natbuf + used, sizeof natbuf - used, "%s{\"id\":\"%s\",\"endpoint\":\"%s\"}", first ? "" : ",", g_friends[i].id, g_friends[i].endpoint), first = 0;
            snprintf(natbuf + used, sizeof natbuf - used, "]}\n"); body = natbuf;
        }
        char portalbuf[512];
        if (register_route) {
            char vid[128];
            if (json_field_string(req, "id", vid, sizeof vid) && safe_id(vid)) {
                int seen = 0; for (int i = 0; i < g_verse_count; ++i) if (!strcmp(g_verses[i], vid)) seen = 1;
                if (!seen && g_verse_count < 128) snprintf(g_verses[g_verse_count++], sizeof g_verses[0], "%s", vid);
                body = "{\"ok\":true}\n"; state_save();
            } else { status = 400; body = "{\"error\":\"id_required\"}\n"; }
        }
        if (friends_post) {
            char id[128] = "", verse[128] = "", name[128] = "", endpoint[256] = "";
            if (!json_field_string(req, "id", id, sizeof id) || !json_field_string(req, "verse", verse, sizeof verse) || !safe_id(id) || !safe_id(verse)) {
                status = 400; body = "{\"error\":\"id_and_verse_required\"}\n";
            } else {
                (void)json_field_string(req, "name", name, sizeof name); (void)json_field_string(req, "endpoint", endpoint, sizeof endpoint);
                int at = -1; for (int i = 0; i < g_friend_count; ++i) if (!strcmp(g_friends[i].id, id)) { at = i; break; }
                if (at < 0 && g_friend_count < (int)(sizeof g_friends / sizeof g_friends[0])) at = g_friend_count++;
                if (at < 0) { status = 507; body = "{\"error\":\"friend_registry_full\"}\n"; }
                else { ImFriend *f = &g_friends[at]; snprintf(f->id, sizeof f->id, "%s", id); snprintf(f->verse, sizeof f->verse, "%s", verse); snprintf(f->name, sizeof f->name, "%s", name[0] ? name : id); snprintf(f->endpoint, sizeof f->endpoint, "%s", endpoint); state_save(); char *p = (char *)"{\"ok\":true}\n"; body = p; }
            }
        }
        if (signal && status == 200) {
            char sv[128] = "", sp[128] = "", ev[256] = ""; uint64_t sq = 0;
            (void)json_field_string(req, "verse", sv, sizeof sv); (void)json_field_string(req, "peer", sp, sizeof sp); (void)json_field_string(req, "event", ev, sizeof ev); (void)json_field_u64(req, "seq", &sq);
            if (sv[0] && sp[0]) {
                int at = -1; for (int i = 0; i < g_session_count; ++i) if (!strcmp(g_sessions[i].verse, sv) && !strcmp(g_sessions[i].peer, sp)) { at = i; break; }
                if (at < 0 && g_session_count < 256) at = g_session_count++;
                if (at >= 0) { ImSession *s = &g_sessions[at]; snprintf(s->verse, sizeof s->verse, "%s", sv); snprintf(s->peer, sizeof s->peer, "%s", sp); if (!sq) sq = s->seq + 1; if (sq > s->seq) s->seq = sq; if (ev[0]) { int k = s->event_count < 16 ? s->event_count++ : 15; if (s->event_count == 16) { memmove(s->event[0], s->event[1], 15 * sizeof s->event[0]); memmove(s->event_seq, s->event_seq + 1, 15 * sizeof s->event_seq[0]); } snprintf(s->event[k], sizeof s->event[0], "%s", ev); s->event_seq[k] = sq; } state_save(); }
            }
        }
        if (resume) {
            char verse[128] = "", peer[128] = "", tok[128] = ""; uint64_t seq = 0;
            if (!json_field_string(req, "verse", verse, sizeof verse) || !json_field_string(req, "peer", peer, sizeof peer) || !json_field_string(req, "token", tok, sizeof tok) || !token_allows(tok, verse, peer)) { status = 403; body = "{\"error\":\"invalid_capability_token\"}\n"; }
            else { (void)json_field_u64(req, "seq", &seq); int replay = strstr(req, "\"replay\":true") != NULL; int at = -1; for (int i = 0; i < g_session_count; ++i) if (!strcmp(g_sessions[i].verse, verse) && !strcmp(g_sessions[i].peer, peer)) { at = i; break; } if (at < 0 && g_session_count < (int)(sizeof g_sessions / sizeof g_sessions[0])) at = g_session_count++; if (at < 0) { status = 507; body = "{\"error\":\"session_registry_full\"}\n"; } else if (at >= 0 && g_sessions[at].seq > seq && !replay) { status = 409; body = "{\"error\":\"session_sequence_out_of_order\"}\n"; } else { snprintf(g_sessions[at].verse, sizeof g_sessions[at].verse, "%s", verse); snprintf(g_sessions[at].peer, sizeof g_sessions[at].peer, "%s", peer); if (seq > g_sessions[at].seq) g_sessions[at].seq = seq; state_save(); char *p = (char *)"{\"resumed\":true}\n"; body = p; } }
        }
        if (session_stop) {
            char verse[128] = "", peer[128] = "", tok[128] = ""; (void)json_field_string(req, "verse", verse, sizeof verse); (void)json_field_string(req, "peer", peer, sizeof peer); (void)json_field_string(req, "token", tok, sizeof tok);
            if (!verse[0] || !peer[0] || !token_allows(tok, verse, peer)) { status = 403; body = "{\"error\":\"invalid_capability_token\"}\n"; }
            else { for (int i = 0; i < g_session_count; ++i) if (!strcmp(g_sessions[i].verse, verse) && !strcmp(g_sessions[i].peer, peer)) { g_sessions[i].stopped = 1; break; } state_save(); }
        }
        char replaybuf[4096];
        if (resume && status == 200) {
            char rv[128] = "", rp[128] = ""; uint64_t from = 0; (void)json_field_string(req, "verse", rv, sizeof rv); (void)json_field_string(req, "peer", rp, sizeof rp); (void)json_field_u64(req, "seq", &from);
            size_t used = (size_t)snprintf(replaybuf, sizeof replaybuf, "{\"resumed\":true,\"replay\":["); int first = 1;
            for (int i = 0; i < g_session_count && used < sizeof replaybuf - 320; ++i) if (!strcmp(g_sessions[i].verse, rv) && !strcmp(g_sessions[i].peer, rp)) { ImSession *s = &g_sessions[i]; for (int j = 0; j < s->event_count; ++j) if (s->event_seq[j] > from) { used += (size_t)snprintf(replaybuf + used, sizeof replaybuf - used, "%s{\"seq\":%llu,\"event\":\"%s\"}", first ? "" : ",", (unsigned long long)s->event_seq[j], s->event[j]); first = 0; } }
            snprintf(replaybuf + used, sizeof replaybuf - used, "]}\n"); body = replaybuf;
        }
        if (portal) {
            unsigned long seed = (unsigned long)time(NULL) ^ ++g_token_counter;
            time_t now = time(NULL); unsigned long ttl = 300;
            const char *ttl_env = getenv("CRP_TOKEN_TTL");
            if (ttl_env && *ttl_env) { char *end = NULL; unsigned long v = strtoul(ttl_env, &end, 10); if (end != ttl_env && v > 0 && v <= 86400) ttl = v; }
            char scope_verse[128] = "", scope_peer[128] = "";
            (void)json_field_string(req, "verse", scope_verse, sizeof scope_verse); (void)json_field_string(req, "peer", scope_peer, sizeof scope_peer);
            snprintf(portalbuf, sizeof portalbuf, "{\"token\":\"posix-%lx\",\"expires\":%lu,\"verse\":\"%s\",\"peer\":\"%s\"}\n", seed, (unsigned long)now + ttl, scope_verse, scope_peer);
            char issued[64]; snprintf(issued, sizeof issued, "posix-%lx", seed); token_register(issued, now + (time_t)ttl, scope_verse, scope_peer);
            body = portalbuf;
        }
        if (revoke) { if (!request_token[0]) { status = 400; body = "{\"error\":\"token_required\"}\n"; } else { token_revoke(request_token); body = "{\"revoked\":true}\n"; } }
        size_t body_len = hub ? hublen : strlen(body); const char *status_text = status == 201 ? "201 Created" : status == 400 ? "400 Bad Request" : status == 403 ? "403 Forbidden" : status == 409 ? "409 Conflict" : status == 507 ? "507 Insufficient Storage" : (status == 404 || !ok) ? "404 Not Found" : "200 OK";
        const char *content_type = (hub && (strstr(req, "GET /package/") == req || strstr(req, "GET /content/") == req)) ? "application/octet-stream" : "application/json";
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
    if (!g_state_loaded) { state_load(); g_state_loaded = 1; }
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
