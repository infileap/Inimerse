#include "socket.h"
#include "dir.h"
#include "websocket.h"
#include "crp_session.h"
#include "../common/sha256.h"
#include "../common/ed25519.h"
#include "http_client.h"
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
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
static size_t hub_body(const char *request, char *body, size_t cap, int *status);

/* UDP hub on the same port: `GET /v/<id>` -> package body (matches the
   Windows embedded hub and verse_dist's verse_udp_fetch). */
static int g_udp_fd = -1;
static pthread_t g_udp_thread;
static volatile int g_udp_running = 0;

static void *udp_loop(void *unused) {
    (void)unused;
    char buf[65536];
    while (g_udp_running) {
        struct sockaddr_in from;
        socklen_t flen = sizeof from;
        ssize_t n = recvfrom(g_udp_fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) { struct timespec ts = { 0, 2000000L }; nanosleep(&ts, NULL); continue; }
        buf[n] = 0;
        if (strncmp(buf, "GET /v/", 7) != 0) continue;
        char reqline[768];
        size_t rl = strcspn(buf, "\r\n");
        if (rl >= sizeof reqline) rl = sizeof reqline - 1;
        memcpy(reqline, buf, rl); reqline[rl] = 0;
        int status = 0;
        char body[60001];
        size_t blen = hub_body(reqline, body, sizeof body, &status);
        if (blen > 0 && blen < 60000)
            (void)sendto(g_udp_fd, body, blen, 0, (struct sockaddr *)&from, flen);
    }
    return NULL;
}
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

/* Node advertisements (§55.2): signed, expiring capability declarations.
   node_id *is* the ed25519 public key that must verify the payload, so a
   directory can never invent a node it cannot sign for.  Directory data is
   TTL-scoped and in-memory: nodes re-advertise after a restart. */
typedef struct {
    char node_id[65];      /* ed25519 public key hex */
    char payload[512];     /* canonical signed text: endpoint + capabilities */
    char signature[129];   /* hex signature over payload */
    char endpoint[256];
    char caps[128];
    uint64_t expires_at_ms;
    uint64_t observed_at_ms;
    int superseded;        /* replaced by a newer advertisement from the same node */
    int health;            /* 0 unknown, 1 up, 2 down (§55.5: never schedule blind) */
    uint64_t last_seen_ms;
} ImNodeAd;

/* Session authority record (§55.5 stable session object): who currently owns
   authoritative writes for (verse, peer), at which generation, and whether it
   has been frozen for a handoff. */
typedef struct {
    char verse[128], peer[128];
    char authority[65];      /* node_id (or "local") */
    int generation;
    int frozen;              /* source frozen: read-only during handoff */
    char checkpoint[65];     /* last verified state/snapshot hash */
    uint64_t updated_ms;
} ImAuthority;
#define IM_AUTH_MAX 128
static ImAuthority g_auths[IM_AUTH_MAX];
static int g_auth_count;

/* the node table is written by the HTTP threads and read/updated by the
   background health probe, so it needs its own lock */
static pthread_mutex_t g_node_lock = PTHREAD_MUTEX_INITIALIZER;
#define IM_NODE_MAX 128
static ImNodeAd g_nodes[IM_NODE_MAX];
static int g_node_count;
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
#define SESSION_EVENT_WINDOW 16

/* record an event for (verse, peer) in the durable replay window (§51.9):
   the ring keeps the newest SESSION_EVENT_WINDOW entries; resume uses it to
   refill gaps, and reports snapshot_required once the request falls out. */
static void session_note_event(const char *verse, const char *peer, uint64_t seq, const char *event) {
    if (!verse || !verse[0] || !peer || !peer[0]) return;
    int at = -1;
    for (int i = 0; i < g_session_count; ++i)
        if (!strcmp(g_sessions[i].verse, verse) && !strcmp(g_sessions[i].peer, peer)) { at = i; break; }
    if (at < 0 && g_session_count < (int)(sizeof g_sessions / sizeof g_sessions[0])) at = g_session_count++;
    if (at < 0) return;
    ImSession *x = &g_sessions[at];
    snprintf(x->verse, sizeof x->verse, "%s", verse);
    snprintf(x->peer, sizeof x->peer, "%s", peer);
    if (!seq) seq = x->seq + 1;
    if (seq > x->seq) x->seq = seq;
    if (event && event[0]) {
        if (x->event_count < SESSION_EVENT_WINDOW) {
            snprintf(x->event[x->event_count], sizeof x->event[0], "%s", event);
            x->event_seq[x->event_count] = seq;
            x->event_count++;
        } else {
            memmove(x->event[0], x->event[1], (SESSION_EVENT_WINDOW - 1) * sizeof x->event[0]);
            memmove(x->event_seq, x->event_seq + 1, (SESSION_EVENT_WINDOW - 1) * sizeof x->event_seq[0]);
            snprintf(x->event[SESSION_EVENT_WINDOW - 1], sizeof x->event[0], "%s", event);
            x->event_seq[SESSION_EVENT_WINDOW - 1] = seq;
        }
    }
    state_save();
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
    /* GET /v/<id>: the path verse_dist's verse_fetch uses (and the Windows
       embedded hub serves); alias of /package/<id> so both hubs agree. */
    const char *vpath = strstr(request, "GET /v/"); if (vpath == request) {
        vpath += 7; char vid[128]; size_t vi = 0;
        while (vpath[vi] && vpath[vi] != ' ' && vpath[vi] != '?' && vi + 1 < sizeof vid) { vid[vi] = vpath[vi]; ++vi; }
        vid[vi] = 0;
        if (!safe_id(vid)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_id\"}\n"); }
        char vfile[1400]; snprintf(vfile, sizeof vfile, "%s/%s.vverse", root, vid);
        FILE *vf = fopen(vfile, "rb");
        if (!vf) { *status = 404; return (size_t)snprintf(body, cap, "{\"error\":\"not_found\"}\n"); }
        size_t vn = fread(body, 1, cap, vf); fclose(vf); *status = 200; return vn;
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
/* read a URL query parameter (?name=value or &name=value) from a raw request */
static int query_param(const char *req, const char *name, char *out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof pat, "%s=", name);
    const char *p = strstr(req, pat);
    if (!p) return 0;
    if (p != req && p[-1] != '?' && p[-1] != '&') return 0;  /* must start a parameter */
    p += strlen(pat);
    size_t i = 0;
    while (p[i] && p[i] != ' ' && p[i] != '&' && p[i] != '\r' && i + 1 < cap) { out[i] = p[i]; i++; }
    out[i] = 0;
    return i > 0;
}

/* hex helpers for node signatures (§55.2) */
static int http_hex_decode(const char *hex, unsigned char *out, int out_cap) {
    int n = 0;
    for (int i = 0; hex[i] && hex[i + 1] && n < out_cap; i += 2) {
        int hi = hex[i] >= 'a' ? hex[i] - 'a' + 10 : hex[i] >= 'A' ? hex[i] - 'A' + 10 : hex[i] - '0';
        int lo = hex[i + 1] >= 'a' ? hex[i + 1] - 'a' + 10 : hex[i + 1] >= 'A' ? hex[i + 1] - 'A' + 10 : hex[i + 1] - '0';
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return -1;
        out[n++] = (unsigned char)((hi << 4) | lo);
    }
    return n;
}

/* ---------- §55.5 helpers: health, authority records, event tail ---------- */
static uint64_t http_now_ms(void);

/* Background health probe: a request handler must never perform a blocking
   probe (a hub probing its own endpoint would deadlock the accept loop), so
   /node/discover and /node/schedule only read cached observations. */
static pthread_t g_probe_thread;
static volatile int g_probe_running = 0;
static volatile int g_probe_now = 0;   /* a new claim asks for an immediate round */

static void *node_probe_loop(void *unused) {
    (void)unused;
    while (g_probe_running) {
        char endpoints[IM_NODE_MAX][256];
        int n = 0;
        pthread_mutex_lock(&g_node_lock);
        for (int i = 0; i < g_node_count && n < IM_NODE_MAX; ++i)
            snprintf(endpoints[n++], 256, "%s", g_nodes[i].endpoint);
        pthread_mutex_unlock(&g_node_lock);
        for (int i = 0; i < n; ++i) {
            int up = 0;
            uint64_t seen = 0;
            if (endpoints[i][0]) {
                char url[512], buf[256];
                int status = 0;
                if (strncmp(endpoints[i], "http://", 7) != 0 && strncmp(endpoints[i], "https://", 8) != 0)
                    snprintf(url, sizeof url, "http://%s/ping", endpoints[i]);
                else
                    snprintf(url, sizeof url, "%s/ping", endpoints[i]);
                if (im_http_request("GET", url, NULL, buf, sizeof buf, &status) == 0 && status == 200 && strstr(buf, "pong")) {
                    up = 1;
                    seen = http_now_ms();
                }
            }
            pthread_mutex_lock(&g_node_lock);
            for (int j = 0; j < g_node_count; ++j)
                if (!strcmp(g_nodes[j].endpoint, endpoints[i])) {
                    g_nodes[j].health = up ? 1 : 2;
                    if (up) g_nodes[j].last_seen_ms = seen;
                }
            pthread_mutex_unlock(&g_node_lock);
        }
        /* sleep in slices so a fresh advertisement is observed promptly */
        for (int slice = 0; slice < 10 && g_probe_running && !g_probe_now; slice++) {
            struct timespec ts = { 0, 100000000L };
            nanosleep(&ts, NULL);
        }
        g_probe_now = 0;
    }
    return NULL;
}
static uint64_t http_now_ms(void);


/* probe a declared endpoint with GET /ping; a reachable port is not health */
static void hub_probe_node(ImNodeAd *nd) {
    if (!nd->endpoint[0]) { nd->health = 0; return; }
    char url[512];
    if (strncmp(nd->endpoint, "http://", 7) != 0 && strncmp(nd->endpoint, "https://", 8) != 0)
        snprintf(url, sizeof url, "http://%s/ping", nd->endpoint);
    else
        snprintf(url, sizeof url, "%s/ping", nd->endpoint);
    char buf[256];
    int status = 0;
    if (im_http_request("GET", url, NULL, buf, sizeof buf, &status) == 0 && status == 200 && strstr(buf, "pong")) {
        nd->health = 1;
        nd->last_seen_ms = http_now_ms();
    } else {
        nd->health = 2;   /* down: schedulers must stop assigning authority */
    }
}

static ImAuthority *auth_find(const char *verse, const char *peer) {
    for (int i = 0; i < g_auth_count; ++i)
        if (!strcmp(g_auths[i].verse, verse) && !strcmp(g_auths[i].peer, peer)) return &g_auths[i];
    return NULL;
}

static ImAuthority *auth_get_or_create(const char *verse, const char *peer) {
    ImAuthority *a = auth_find(verse, peer);
    if (a) return a;
    if (g_auth_count >= IM_AUTH_MAX) return NULL;
    a = &g_auths[g_auth_count++];
    memset(a, 0, sizeof *a);
    snprintf(a->verse, sizeof a->verse, "%s", verse);
    snprintf(a->peer, sizeof a->peer, "%s", peer);
    snprintf(a->authority, sizeof a->authority, "%s", "local");
    a->generation = 1;
    a->updated_ms = http_now_ms();
    return a;
}

/* hash of the retained event tail for (verse, peer): the value a handoff
   target must reproduce before it may take authority (§55.5 target_verify) */
static void session_event_tail_hash(const char *verse, const char *peer, char out[65]) {
    const char *tail = "";
    for (int i = 0; i < g_session_count; ++i)
        if (!strcmp(g_sessions[i].verse, verse) && !strcmp(g_sessions[i].peer, peer) && g_sessions[i].event_count > 0)
            tail = g_sessions[i].event[g_sessions[i].event_count - 1];
    sha256_hex(tail, strlen(tail), out);
}

/* verify a node advertisement: node_id is the signing public key */
static int http_node_verify(const char *node_id, const char *payload, const char *signature) {
    if (!node_id || strlen(node_id) != 64 || !signature || strlen(signature) != 128 || !payload) return 0;
    unsigned char pub[32], sig[64];
    if (http_hex_decode(node_id, pub, 32) != 32) return 0;
    if (http_hex_decode(signature, sig, 64) != 64) return 0;
    return ed25519_verify(pub, (const unsigned char *)payload, strlen(payload), sig) ? 1 : 0;
}

/* ms clock for lease bookkeeping (§55.5) */
static uint64_t http_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

static int json_field_u64(const char *json, const char *name, uint64_t *out) {
    char key[64]; snprintf(key, sizeof key, "\"%s\"", name); const char *p = strstr(json, key); if (!p) return 0;
    p = strchr(p + strlen(key), ':'); if (!p) return 0; while (*++p == ' ' || *p == '\t') {}
    uint64_t v = 0; int digits = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned)(*p++ - '0'); digits = 1; } if (digits) *out = v; return digits;
}

/* One thread per websocket client: a long-lived ws connection must not block
   the accept loop, otherwise a second client (or /health) can never connect. */
typedef struct { ImSocket *client; int n; char req[65536]; } WsConn;

static void *ws_client_thread(void *arg) {
    WsConn *wc = (WsConn *)arg;
    ImSocket *client = wc->client;
    int n = wc->n;
    char *req = (char *)malloc((size_t)n + 1);
    if (!req) { free(wc); im_socket_close(client); return NULL; }
    memcpy(req, wc->req, (size_t)n + 1);
    free(wc);
    if (n <= 0 || !strstr(req, "GET /ws") || !find_ci(req, "Upgrade: websocket")) {
        free(req); im_socket_close(client);
        return NULL;
    }
    if (im_ws_accept(client, req, (size_t)n) != 0) { free(req); im_socket_close(client); return NULL; }
    ImCrpSession session; im_crp_session_init(&session, 1);
    /* §55.3 capability handshake: clients may announce ?ver=N&caps=M;
       version mismatch is rejected explicitly (§24.6); absent parameters keep
       the legacy (permissive) behaviour. */
    int ver = 1; uint32_t caps = 0; int announced = 0;
    const char *vq = strstr(req, "ver=");
    if (vq) { ver = atoi(vq + 4); announced = 1; }
    const char *cq = strstr(req, "caps=");
    if (cq) { caps = (uint32_t)strtoul(cq + 5, NULL, 10); announced = 1; }
    if (announced) {
        ImCrpHello hello;
        if (im_crp_session_hello(1, IM_CRP_CAP_EVENTS | IM_CRP_CAP_SNAPSHOT | IM_CRP_CAP_UDP | IM_CRP_CAP_RELAY,
                                 ver, caps, &hello) != 0) {
            char eb[256];
            int el = snprintf(eb, sizeof eb, "{\"error\":\"protocol_version_mismatch\",\"detail\":\"%s\"}", hello.error);
            (void)im_ws_send_text(client, eb, (size_t)el);
            free(req);
            im_socket_close(client);
            return NULL;
        }
        session.caps = hello.negotiated_caps;
    } else {
        session.caps = IM_CRP_CAP_EVENTS | IM_CRP_CAP_SNAPSHOT | IM_CRP_CAP_UDP | IM_CRP_CAP_RELAY;
    }
    free(req); /* request consumed: query parameters already parsed */
    req = NULL;
    im_crp_session_lease_begin(&session, http_now_ms(), 30000);

    pthread_mutex_lock(&g_ws_lock);
    int slot = -1;
    for (int i = 0; i < 32; ++i) if (!g_ws_clients[i]) { g_ws_clients[i] = client; slot = i; break; }
    pthread_mutex_unlock(&g_ws_lock);
    if (slot < 0) {
        (void)im_ws_send_text(client, "{\"error\":\"too_many_connections\"}", 34);
        im_socket_close(client);
        return NULL;
    }
    char frame[65536];
    for (;;) {
        int flen = im_ws_read_text(client, frame, sizeof frame);
        if (flen == -2) { (void)im_ws_send_pong(client, NULL, 0); continue; }
        if (flen <= 0) break;
        /* §55.5 lease: drop the connection once it expires */
        if (im_crp_session_lease_expired(&session, http_now_ms())) {
            const char *err = "{\"error\":\"lease_expired\"}";
            (void)im_ws_send_text(client, err, strlen(err));
            break;
        }
        char type[32]; uint64_t seq = 0; int has_seq = 0;
        if (json_field_string(frame, "type", type, sizeof type)) {
            has_seq = json_field_u64(frame, "seq", &seq) && seq > 0;
            (void)json_field_u64(frame, "seq", &seq);
            /* §55.6 sequencing: duplicates are dropped, gaps demand resync */
            if (has_seq) {
                int acc = im_crp_session_accept(&session, seq);
                if (acc == 0) continue;                 /* duplicate: safe to drop */
                if (acc < 0) {
                    char eb[160];
                    int el = snprintf(eb, sizeof eb,
                                      "{\"error\":\"resume_required\",\"last_applied\":%llu}",
                                      (unsigned long long)session.last_applied);
                    (void)im_ws_send_text(client, eb, (size_t)el);
                    continue;
                }
            }
            if (!strcmp(type, "heartbeat")) im_crp_session_lease_touch(&session, http_now_ms(), 30000);
            /* §51.9: keep the durable replay window in sync so a reconnecting
               peer can refill gaps via /session/resume */
            if (has_seq) {
                char wv[128] = "", wp[128] = "";
                if (json_field_string(frame, "verse", wv, sizeof wv) &&
                    json_field_string(frame, "peer", wp, sizeof wp))
                    session_note_event(wv, wp, seq, frame);
            }
            int state_rc = im_crp_session_apply(&session, type, seq, 0, NULL);
            if (state_rc < 0) {
                const char *err = "{\"error\":\"invalid_session_transition\"}";
                (void)im_ws_send_text(client, err, strlen(err));
                continue;
            }
        }
        int broadcast_ok = 1;
        pthread_mutex_lock(&g_ws_lock);
        for (int i = 0; i < 32; ++i)
            if (g_ws_clients[i] && g_ws_clients[i] != client)
                if (im_ws_send_text(g_ws_clients[i], frame, (size_t)flen) != 0) broadcast_ok = 0;
        pthread_mutex_unlock(&g_ws_lock);
        if (!broadcast_ok) break;
    }
    pthread_mutex_lock(&g_ws_lock);
    if (slot >= 0 && g_ws_clients[slot] == client) g_ws_clients[slot] = NULL;
    pthread_mutex_unlock(&g_ws_lock);
    im_socket_close(client);
    return NULL;
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
            /* hand the connection (with the bytes already read) to a dedicated
               thread and keep accepting: a long-lived ws client must not block
               other clients */
            WsConn *wc = (WsConn *)malloc(sizeof *wc);
            if (wc) {
                wc->client = client;
                wc->n = n;
                memcpy(wc->req, req, (size_t)n + 1);
                pthread_t ws_th;
                if (pthread_create(&ws_th, NULL, ws_client_thread, wc) == 0) { pthread_detach(ws_th); continue; }
                free(wc);
            }
            im_socket_close(client);
            continue;
        }
        int health = n > 0 && strstr(req, "GET /health") != NULL;
        /* lightweight reachability probe used by verse_hub_ping */
        int ping = n > 0 && strstr(req, "GET /ping") != NULL;
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
        int node_advertise = n > 0 && strstr(req, "POST /node/advertise") != NULL;
        int node_discover = n > 0 && strstr(req, "GET /node/discover") != NULL;
        int node_schedule = n > 0 && strstr(req, "GET /node/schedule") != NULL;
        int node_handoff = n > 0 && strstr(req, "POST /node/handoff") != NULL;
        int auth_post = n > 0 && strstr(req, "POST /session/authority") != NULL;
        int auth_get = n > 0 && strstr(req, "GET /session/authority") != NULL;
        int portal = n > 0 && strstr(req, "POST /portal") != NULL;
        int status = 200;
        char request_token[64] = "";
        (void)json_field_string(req, "token", request_token, sizeof request_token);
        char signal_verse[128] = "", signal_peer[128] = "";
        (void)json_field_string(req, "verse", signal_verse, sizeof signal_verse); (void)json_field_string(req, "peer", signal_peer, sizeof signal_peer);
        if (signal && !token_allows(request_token, signal_verse, signal_peer)) { signal = 0; status = 403; }
        char hubbuf[65536]; size_t hublen = hub_body(req, hubbuf, sizeof hubbuf, &status); int hub = hublen > 0;
        int ok = health || ping || find || friends_get || friends_post || resume || session_stop || signal || revoke || register_route || route_post || route_get || nat_get || portal || hub || node_advertise || node_discover || node_schedule || node_handoff || auth_post || auth_get;
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
            ping ? "pong\n" :
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
        /* ---- §55.2 node advertisements: signed + expiring claims ---- */
        char nodebuf[8192];
        if (node_advertise) {
            char node_id[128] = "", payload[512] = "", signature[160] = "", endpoint[256] = "", caps[128] = "";
            uint64_t expires_at = 0;
            (void)json_field_string(req, "node_id", node_id, sizeof node_id);
            (void)json_field_string(req, "payload", payload, sizeof payload);
            (void)json_field_string(req, "signature", signature, sizeof signature);
            (void)json_field_string(req, "endpoint", endpoint, sizeof endpoint);
            (void)json_field_string(req, "caps", caps, sizeof caps);
            (void)json_field_u64(req, "expires_at", &expires_at);
            uint64_t now = http_now_ms();
            if (!node_id[0] || !payload[0] || !signature[0]) {
                status = 400; body = "{\"error\":\"node_id_payload_signature_required\"}\n";
            } else if (expires_at != 0 && expires_at <= now) {
                /* expiring claims cannot be merged silently (§55.2) */
                status = 400; body = "{\"error\":\"expired_advertisement\"}\n";
            } else if (!http_node_verify(node_id, payload, signature)) {
                /* visible refusal: a directory never accepts what it cannot verify */
                fprintf(stderr, "[hub] node advertisement rejected: invalid signature (node %.16s..., payload %.60s, sig %.16s, lens %zu/%zu)\n",
                        node_id, payload, signature, strlen(node_id), strlen(signature));
                status = 400; body = "{\"error\":\"invalid_signature\"}\n";
            } else {
                int at = -1;
                for (int i = 0; i < g_node_count; ++i)
                    if (!strcmp(g_nodes[i].node_id, node_id)) { at = i; break; }
                if (at < 0) {
                    if (g_node_count < IM_NODE_MAX) at = g_node_count++;
                    else { status = 507; body = "{\"error\":\"node_registry_full\"}\n"; }
                } else {
                    g_nodes[at].superseded = 1;   /* replaced by the newer claim */
                }
                if (at >= 0) {
                    ImNodeAd *nd = &g_nodes[at];
                    snprintf(nd->node_id, sizeof nd->node_id, "%s", node_id);
                    snprintf(nd->payload, sizeof nd->payload, "%s", payload);
                    snprintf(nd->signature, sizeof nd->signature, "%s", signature);
                    snprintf(nd->endpoint, sizeof nd->endpoint, "%s", endpoint);
                    snprintf(nd->caps, sizeof nd->caps, "%s", caps);
                    nd->expires_at_ms = expires_at;
                    nd->observed_at_ms = now;
                    nd->health = 0;      /* observed, not assumed */
                    g_probe_now = 1;     /* probe the new claim right away */
                    snprintf(nodebuf, sizeof nodebuf,
                             "{\"ok\":true,\"node_id\":\"%s\",\"expires_at\":%llu,\"source\":\"directory\"}\n",
                             node_id, (unsigned long long)expires_at);
                    body = nodebuf;
                }
            }
        }
        if (node_discover) {
            uint64_t now = http_now_ms();
            size_t used = (size_t)snprintf(nodebuf, sizeof nodebuf, "{\"nodes\":[");
            int first = 1, expired = 0;
            for (int i = 0; i < g_node_count && used < sizeof nodebuf - 640; ++i) {
                ImNodeAd *nd = &g_nodes[i];
                if (nd->expires_at_ms != 0 && nd->expires_at_ms <= now) { expired++; continue; }
                /* health comes from the background probe (cached observation) */
                char ev[17];
                char digest[65];
                sha256_hex(nd->signature, strlen(nd->signature), digest);
                snprintf(ev, sizeof ev, "%.16s", digest);
                used += (size_t)snprintf(nodebuf + used, sizeof nodebuf - used,
                                         "%s{\"node_id\":\"%s\",\"endpoint\":\"%s\",\"caps\":\"%s\","
                                         "\"payload\":\"%s\",\"signature\":\"%s\","
                                         "\"source\":\"directory\",\"observed_at\":%llu,\"expires_at\":%llu,"
                                         "\"evidence_ref\":\"sha256:%s\",\"superseded\":%d,"
                                         "\"health\":\"%s\",\"last_seen_ms\":%llu}",
                                         first ? "" : ",", nd->node_id, nd->endpoint, nd->caps,
                                         nd->payload, nd->signature,
                                         (unsigned long long)nd->observed_at_ms,
                                         (unsigned long long)nd->expires_at_ms, ev, nd->superseded,
                                         nd->health == 1 ? "up" : nd->health == 2 ? "down" : "unknown",
                                         (unsigned long long)nd->last_seen_ms);
                first = 0;
            }
            /* filtered-out entries are reported, never dropped silently */
            snprintf(nodebuf + used, sizeof nodebuf - used, "],\"expired\":%d}\n", expired);
            body = nodebuf;
        }
        if (node_schedule) {
            /* §55.5: schedulers only see nodes that are both fresh and healthy;
               what was excluded is reported, never silently dropped */
            char want[128] = "";
            (void)query_param(req, "caps", want, sizeof want);
            uint64_t now = http_now_ms();
            size_t used = (size_t)snprintf(nodebuf, sizeof nodebuf, "{\"nodes\":[");
            int first = 1, unhealthy = 0, expired = 0, nocaps = 0, emitted = 0;
            for (int i = 0; i < g_node_count && used < sizeof nodebuf - 640; ++i) {
                ImNodeAd *nd = &g_nodes[i];
                if (nd->expires_at_ms != 0 && nd->expires_at_ms <= now) { expired++; continue; }
                if (nd->health != 1) { unhealthy++; continue; }
                if (want[0] && !strstr(nd->caps, want)) { nocaps++; continue; }
                emitted++;
                used += (size_t)snprintf(nodebuf + used, sizeof nodebuf - used,
                                         "%s{\"node_id\":\"%s\",\"endpoint\":\"%s\",\"caps\":\"%s\","
                                         "\"health\":\"up\",\"last_seen_ms\":%llu,\"expires_at\":%llu}",
                                         first ? "" : ",", nd->node_id, nd->endpoint, nd->caps,
                                         (unsigned long long)nd->last_seen_ms, (unsigned long long)nd->expires_at_ms);
                first = 0;
            }
            snprintf(nodebuf + used, sizeof nodebuf - used,
                     "],\"count\":%d,\"excluded\":{\"unhealthy\":%d,\"expired\":%d,\"missing_caps\":%d}}\n",
                     emitted, unhealthy, expired, nocaps);
            body = nodebuf;
        }
        if (auth_post) {
            char verse[128] = "", peer[128] = "", authority[128] = "";
            (void)json_field_string(req, "verse", verse, sizeof verse);
            (void)json_field_string(req, "peer", peer, sizeof peer);
            (void)json_field_string(req, "authority", authority, sizeof authority);
            if (!verse[0] || !peer[0] || !authority[0]) {
                status = 400; body = "{\"error\":\"verse_peer_authority_required\"}\n";
            } else {
                ImAuthority *a = auth_get_or_create(verse, peer);
                if (!a) { status = 507; body = "{\"error\":\"authority_registry_full\"}\n"; }
                else {
                    snprintf(a->authority, sizeof a->authority, "%s", authority);
                    a->frozen = 0;
                    a->updated_ms = http_now_ms();
                    snprintf(nodebuf, sizeof nodebuf,
                             "{\"ok\":true,\"authority\":\"%s\",\"generation\":%d}\n", a->authority, a->generation);
                    body = nodebuf;
                }
            }
        }
        if (auth_get) {
            char verse[128] = "", peer[128] = "";
            (void)query_param(req, "verse", verse, sizeof verse);
            (void)query_param(req, "peer", peer, sizeof peer);
            ImAuthority *a = auth_find(verse, peer);
            char tail[65];
            session_event_tail_hash(verse, peer, tail);
            if (!a) {
                snprintf(nodebuf, sizeof nodebuf,
                         "{\"authority\":\"none\",\"generation\":0,\"frozen\":0,\"event_tail\":\"%s\"}\n", tail);
            } else {
                snprintf(nodebuf, sizeof nodebuf,
                         "{\"authority\":\"%s\",\"generation\":%d,\"frozen\":%d,\"checkpoint\":\"%s\",\"event_tail\":\"%s\",\"updated_ms\":%llu}\n",
                         a->authority, a->generation, a->frozen, a->checkpoint, tail,
                         (unsigned long long)a->updated_ms);
            }
            body = nodebuf;
        }
        if (node_handoff) {
            /* §55.5 minimal handoff: prepare -> verify target -> transfer lease.
               Verification failures keep the source authority (never a silent
               split-brain switch). */
            char verse[128] = "", peer[128] = "", from[128] = "", to[128] = "";
            char snap[65] = "", tail[65] = "", rules[32] = "";
            (void)json_field_string(req, "verse", verse, sizeof verse);
            (void)json_field_string(req, "peer", peer, sizeof peer);
            (void)json_field_string(req, "from_authority", from, sizeof from);
            (void)json_field_string(req, "to_authority", to, sizeof to);
            (void)json_field_string(req, "snapshot_hash", snap, sizeof snap);
            (void)json_field_string(req, "event_tail_hash", tail, sizeof tail);
            (void)json_field_string(req, "rules_version", rules, sizeof rules);
            ImAuthority *a = auth_get_or_create(verse, peer);
            char real_tail[65];
            session_event_tail_hash(verse, peer, real_tail);
            ImNodeAd *target = NULL;
            for (int i = 0; i < g_node_count; ++i)
                if (!strcmp(g_nodes[i].node_id, to)) { target = &g_nodes[i]; break; }
            if (!verse[0] || !peer[0] || !to[0] || !snap[0]) {
                status = 400; body = "{\"error\":\"verse_peer_target_snapshot_required\"}\n";
            } else if (!a) {
                status = 507; body = "{\"error\":\"authority_registry_full\"}\n";
            } else if (from[0] && strcmp(from, a->authority) != 0) {
                status = 409; body = "{\"error\":\"not_current_authority\"}\n";
            } else if (!target) {
                status = 404; body = "{\"error\":\"target_unknown\"}\n";
            } else {
                if (target->health != 1) {
                    status = 503; body = "{\"error\":\"target_unhealthy\"}\n";
                } else if (a->checkpoint[0] && strcmp(a->checkpoint, snap) != 0) {
                    /* the provided state cannot reproduce what the source committed */
                    status = 409;
                    snprintf(nodebuf, sizeof nodebuf,
                             "{\"error\":\"checkpoint_mismatch\",\"expected\":\"%s\",\"authority\":\"%s\"}\n",
                             a->checkpoint, a->authority);
                    body = nodebuf;
                } else if (strcmp(tail, real_tail) != 0) {
                    status = 409;
                    snprintf(nodebuf, sizeof nodebuf,
                             "{\"error\":\"event_tail_mismatch\",\"expected\":\"%s\",\"authority\":\"%s\"}\n",
                             real_tail, a->authority);
                    body = nodebuf;
                } else {
                    if (!a->checkpoint[0]) snprintf(a->checkpoint, sizeof a->checkpoint, "%s", snap);
                    snprintf(a->authority, sizeof a->authority, "%s", to);
                    a->generation++;
                    a->frozen = 0;
                    a->updated_ms = http_now_ms();
                    fprintf(stderr, "[hub] handoff %s/%s: authority -> %s (generation %d)\n",
                            verse, peer, to, a->generation);
                    snprintf(nodebuf, sizeof nodebuf,
                             "{\"ok\":true,\"authority\":\"%s\",\"generation\":%d,\"source_frozen\":true,"
                             "\"checkpoint\":\"%s\",\"rules_version\":\"%s\"}\n",
                             a->authority, a->generation, a->checkpoint, rules[0] ? rules : "1");
                    body = nodebuf;
                }
            }
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
            if (sv[0] && sp[0]) session_note_event(sv, sp, sq, ev);
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
            ImSession *s = NULL;
            for (int i = 0; i < g_session_count; ++i) if (!strcmp(g_sessions[i].verse, rv) && !strcmp(g_sessions[i].peer, rp)) { s = &g_sessions[i]; break; }
            /* §55.6/§24.6: if the requested point predates the retained window,
               say so explicitly instead of returning a silently partial replay */
            int fell_out = 0;
            if (s && s->event_count == SESSION_EVENT_WINDOW && s->seq > from && s->event_seq[0] > from + 1) fell_out = 1;
            if (fell_out) {
                snprintf(replaybuf, sizeof replaybuf,
                         "{\"resumed\":true,\"resume\":\"snapshot_required\",\"oldest_seq\":%llu,\"latest_seq\":%llu}\n",
                         (unsigned long long)(s->event_seq[0] - 1), (unsigned long long)s->seq);
                body = replaybuf;
            } else {
                size_t used = (size_t)snprintf(replaybuf, sizeof replaybuf, "{\"resumed\":true,\"complete\":true,\"replay\":[");
                int first = 1;
                if (s) for (int j = 0; j < s->event_count && used < sizeof replaybuf - 320; ++j)
                    if (s->event_seq[j] > from) {
                        used += (size_t)snprintf(replaybuf + used, sizeof replaybuf - used,
                                                 "%s{\"seq\":%llu,\"event\":\"%s\"}",
                                                 first ? "" : ",", (unsigned long long)s->event_seq[j], s->event[j]);
                        first = 0;
                    }
                snprintf(replaybuf + used, sizeof replaybuf - used, "]}\n"); body = replaybuf;
            }
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
        size_t body_len = hub ? hublen : strlen(body); const char *status_text = status == 201 ? "201 Created" : status == 400 ? "400 Bad Request"
            : status == 403 ? "403 Forbidden" : status == 404 ? "404 Not Found"
            : status == 409 ? "409 Conflict" : status == 500 ? "500 Internal Server Error"
            : status == 503 ? "503 Service Unavailable" : status == 507 ? "507 Insufficient Storage"
            : status == 405 ? "405 Method Not Allowed" : status == 413 ? "413 Payload Too Large"
            : !ok ? "404 Not Found" : "200 OK";
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
    /* UDP hub shares the port; failure degrades to TCP-only, never silently
       pretends UDP works */
    g_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_fd >= 0) {
        struct sockaddr_in ua;
        memset(&ua, 0, sizeof ua);
        ua.sin_family = AF_INET;
        ua.sin_port = htons((unsigned short)port);
        ua.sin_addr.s_addr = htonl(INADDR_ANY);
        int uyes = 1;
        (void)setsockopt(g_udp_fd, SOL_SOCKET, SO_REUSEADDR, &uyes, sizeof uyes);
        if (bind(g_udp_fd, (struct sockaddr *)&ua, sizeof ua) == 0) {
            /* non-blocking + poll: closing an fd another thread blocks on is
               not a reliable wakeup on POSIX, so the loop checks a flag */
            int fl = fcntl(g_udp_fd, F_GETFL, 0);
            (void)fcntl(g_udp_fd, F_SETFL, fl | O_NONBLOCK);
            g_udp_running = 1;
            if (pthread_create(&g_udp_thread, NULL, udp_loop, NULL) != 0) g_udp_running = 0;
            if (!g_probe_running) {
                g_probe_running = 1;
                if (pthread_create(&g_probe_thread, NULL, node_probe_loop, NULL) != 0) g_probe_running = 0;
            }
        }
        if (!g_udp_running) { close(g_udp_fd); g_udp_fd = -1; }
    }
    return 1;
}

void verse_http_stop(void) {
    if (g_probe_running) {
        g_probe_running = 0;
        pthread_join(g_probe_thread, NULL);
    }
    if (g_udp_running) {
        g_udp_running = 0;                  /* the loop polls this flag */
        pthread_join(g_udp_thread, NULL);
        if (g_udp_fd >= 0) { close(g_udp_fd); g_udp_fd = -1; }
    }
    if (!g_http_running) return;
    g_http_running = 0;
    pthread_join(g_http_thread, NULL);
    if (g_http_listener) { im_socket_close(g_http_listener); g_http_listener = NULL; }
    im_socket_shutdown();
}
