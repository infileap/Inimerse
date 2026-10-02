#include "socket.h"
#include "dir.h"
#include "websocket.h"
#include "crp_session.h"
#include "../common/sha256.h"
#include "../common/ed25519.h"
#include "../verse/json_min.h"
/* crp_enroll_proof()/crp_enroll_check(): /portal asks the registry's own
 * precondition instead of keeping a second copy of the proof derivation. */
#include "../verse/crp.h"
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
        /* Only a satisfied request is a package body.  verse_udp_fetch()
           (src/mod/verse_dist_mod.c) hands the reply straight back as the
           package bytes without inspecting them, so an error document sent
           here would be installed as package content; dropping the datagram
           makes the fetch fail honestly instead. */
        if (status == 200 && blen > 0 && blen < 60000)
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
    ImCrpSession sess;       /* §55.6 lifecycle + idempotency for this session */
} ImAuthority;
#define IM_AUTH_MAX 128
static ImAuthority g_auths[IM_AUTH_MAX];
static int g_auth_count;

/* the node table is written by the HTTP threads and read/updated by the
   background health probe, so it needs its own lock */
static pthread_mutex_t g_node_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- §43 economy: currency domains, balances, audit chain ----------
   Definitions, balances, issuance and settlement are deliberately separate
   objects (§43.2).  Every unit in circulation arrives through an auditable
   mint/settle event in a hash-chained ledger; nothing can create currency by
   editing a balance. */
#define IM_ECON_DOMAINS 32
#define IM_ECON_BALANCES 256
#define IM_ECON_EVENTS 512
#define IM_ECON_BRIDGES 16

typedef struct {
    char currency_id[65];    /* sha256 of the canonical signed definition */
    char domain_id[64];
    char issuer[65];         /* ed25519 public key of the issuing authority */
    char value_kind[24];     /* utility | reputation | symbolic | real */
    char supply_rule[128];
    char denomination[32];
    char transfer_policy[64];
    char definition[1024];   /* the exact signed text */
    char signature[129];
    uint64_t expires_at_ms;
} ImCurrency;

typedef struct {
    char currency_id[65];
    char account[160];       /* "<domain_id>/<name>" */
    long long amount;
    int version;
    char custody[64];
    uint64_t last_settlement_ms;
} ImBalance;

typedef struct {
    uint64_t seq;
    char currency_id[65];
    char from[160], to[160];
    long long amount;
    char kind[16];           /* transfer | mint | burn */
    char idem[96];
    char prev[65], hash[65];
} ImEconEvent;

typedef struct {
    char source_domain[64], target_domain[64];
    char rate[64], fee[64], limit[64], oracle[64], rollback[128];
    char signature[129];
    int paused;
} ImBridge;

static ImCurrency g_currencies[IM_ECON_DOMAINS];
static int g_currency_count;
static ImBalance g_balances[IM_ECON_BALANCES];
static int g_balance_count;
static ImEconEvent g_econ_events[IM_ECON_EVENTS];
static int g_econ_event_count;
static char g_econ_tail[65] = "0";
static ImBridge g_bridges[IM_ECON_BRIDGES];
static int g_bridge_count;
static ImCrpSession g_econ_idem;      /* idempotency keys for settlements */
static int g_econ_idem_ready;
/* migration packages accepted by /economy/import: kept apart from the local
   ledger so an imported history cannot masquerade as locally committed */
typedef struct {
    char currency_id[65];
    char content_hash[65];
    char ledger_tail[65];
    int  partial_slice;   /* the slice's origin prev lives outside the package */
    int balance_count;
    char accounts[64][160];
    long long amounts[64];
    int versions[64];
} ImImportedLedger;
#define IM_IMPORTS 16
static ImImportedLedger g_imports[IM_IMPORTS];
static int g_import_count;
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
/* Read at most `cap` bytes of `f` into `buf`, sizing the file first.
   hub_body() serves whole files out of fixed stack buffers, so a file longer
   than the buffer must be refused rather than silently cut: a truncated body
   is not a short answer, it is a corrupt one.  Measured on the unpatched code
   with a 66560-byte package: GET /package/<id> and GET /v/<id> answered 200
   with a 65536-byte prefix, GET /content/<hash> answered 500
   content_corrupt for a perfectly good file, and POST /package/fork wrote
   the 65536-byte prefix to disk as a new package and answered 201.
   Returns 0 = *out_len holds the whole file; 1 = the file is larger than cap
   (*out_len = 0, *out_size = the real size, untouched buffer); 2 = I/O error
   (including a non-seekable source, which cannot be sized and is therefore
   refused instead of guessed at). */
static int read_file_capped(FILE *f, void *buf, size_t cap, size_t *out_len, unsigned long long *out_size) {
    *out_len = 0;
    if (out_size) *out_size = 0;
    if (fseek(f, 0, SEEK_END) != 0) return 2;
    long end = ftell(f);
    if (end < 0 || fseek(f, 0, SEEK_SET) != 0) return 2;
    unsigned long long size = (unsigned long long)end;
    if (out_size) *out_size = size;
    if (size > (unsigned long long)cap) return 1;
    size_t got = fread(buf, 1, cap, f);
    if (got != (size_t)size) return 2;
    *out_len = got;
    return 0;
}
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
        size_t len = 0; unsigned long long fsize = 0; int rrc = read_file_capped(f, body, cap, &len, &fsize); fclose(f);
        if (rrc == 1) { *status = 413; return (size_t)snprintf(body, cap, "{\"error\":\"content_too_large\",\"size\":%llu,\"limit\":%zu}\n", fsize, cap); }
        if (rrc == 2) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"read_failed\"}\n"); }
        unsigned char got[32]; sha256_digest(body, len, got); char check[65]; sha256_hex_of_digest(got, check); if (strcmp(check, hash)) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"content_corrupt\"}\n"); } *status = 200; return len;
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
        size_t vn = 0; unsigned long long vsize = 0; int vrc = read_file_capped(vf, body, cap, &vn, &vsize); fclose(vf);
        if (vrc == 1) { *status = 413; return (size_t)snprintf(body, cap, "{\"error\":\"package_too_large\",\"size\":%llu,\"limit\":%zu}\n", vsize, cap); }
        if (vrc == 2) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"read_failed\"}\n"); }
        *status = 200; return vn;
    }
    const char *p = strstr(request, "GET /package/"); if (p == request) {
        p += 13; char id[128]; size_t i = 0; while (p[i] && p[i] != ' ' && p[i] != '?' && i + 1 < sizeof id) { id[i] = p[i]; ++i; } id[i] = 0;
        if (!safe_id(id)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"invalid_id\"}\n"); }
        char path[1400]; snprintf(path, sizeof path, "%s/%s.vverse", root, id); FILE *f = fopen(path, "rb"); if (!f) { *status = 404; return (size_t)snprintf(body, cap, "{\"error\":\"not_found\"}\n"); }
        size_t n = 0; unsigned long long psize = 0; int prc = read_file_capped(f, body, cap, &n, &psize); fclose(f);
        if (prc == 1) { *status = 413; return (size_t)snprintf(body, cap, "{\"error\":\"package_too_large\",\"size\":%llu,\"limit\":%zu}\n", psize, cap); }
        if (prc == 2) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"read_failed\"}\n"); }
        *status = 200; return n;
    }
    if (strstr(request, "POST /package/fork") == request) {
        char source[128], id[128]; if (!json_value(request, "source", source, sizeof source) || !json_value(request, "id", id, sizeof id) || !safe_id(source) || !safe_id(id)) { *status = 400; return (size_t)snprintf(body, cap, "{\"error\":\"source_and_id_required\"}\n"); }
        char src[1400], dst[1400]; snprintf(src, sizeof src, "%s/%s.vverse", root, source); snprintf(dst, sizeof dst, "%s/%s.vverse", root, id); FILE *f = fopen(src, "rb"); if (!f) { *status = 404; return (size_t)snprintf(body, cap, "{\"error\":\"source_not_found\"}\n"); }
        unsigned char data[65536]; size_t len = 0; unsigned long long ssize = 0; int frc = read_file_capped(f, data, sizeof data, &len, &ssize); fclose(f);
        if (frc == 1) { *status = 413; return (size_t)snprintf(body, cap, "{\"error\":\"package_too_large\",\"source\":\"%s\",\"size\":%llu,\"limit\":%zu}\n", source, ssize, sizeof data); }
        if (frc == 2) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"read_failed\"}\n"); }
        if (!write_atomic(dst, data, len)) { *status = 500; return (size_t)snprintf(body, cap, "{\"error\":\"write_failed\"}\n"); }
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

/* --------------------------------------------------------- portal enrollment
   /portal is this hub's only authorization entry point: the token it mints is
   what /signal and /session/resume later trust.  So the caller must prove it
   may open a portal for the (verse, peer) it names, by presenting
   base64url(HMAC-SHA256(CRP_ENROLL_SECRET, String(verse) + "\0" + String(peer)))
   -- the same proof tools/crp_relay.js expects.  With no enrollment secret
   configured /portal refuses outright, because a security default must not
   depend on the caller remembering to switch it on.  The check itself is
   crp_enroll_check() in src/verse/crp.c: this listener asks the registry's own
   question rather than re-deriving the proof, so the two paths cannot drift. */

/* Write a string's contents as JSON wants them (the quotes are the caller's).
 * The portal response echoes the (verse, peer) the caller just proved, and a
 * proven caller may have escaped either of them. */
static void portal_json_escape(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && o + 8 < cap; ++p) {
        switch (*p) {
        case '"':  out[o++] = '\\'; out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
        case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
        case '\t': out[o++] = '\\'; out[o++] = 't'; break;
        default:
            if (*p < 0x20) o += (size_t)snprintf(out + o, cap - o, "\\u%04x", (unsigned)*p);
            else out[o++] = (char)*p;
        }
    }
    out[o] = 0;
}

/* The proof /portal expects, for callers that already hold the members as
 * strings rather than as a parsed body (src/platform/http_probe.c drives the
 * real listener this way).  A thin wrapper over the registry's own
 * crp_enroll_proof(), so both entry points produce the same bytes. */
void verse_portal_proof(const char *enroll_secret, const char *verse, const char *peer,
                        char *out, size_t cap) {
    VjVal v, p, *vp = &v, *pp = &p;
    CrpBuf buf;
    memset(&v, 0, sizeof v); memset(&p, 0, sizeof p);
    if (!out || !cap) return;
    out[0] = 0;
    if (verse) { v.type = VJ_STR; v.s = (char *)verse; } else vp = NULL;
    if (peer)  { p.type = VJ_STR; p.s = (char *)peer; }  else pp = NULL;
    upp_buf_init(&buf);
    if (crp_enroll_proof(enroll_secret, vp, pp, &buf) == 0 && buf.data)
        snprintf(out, cap, "%s", buf.data);
    upp_buf_free(&buf);
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
    im_crp_session_init(&a->sess, 1);
    im_crp_session_set_generation(&a->sess, 1);
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

/* ---------- §43 economy helpers ---------- */

static ImCurrency *econ_currency(const char *currency_id) {
    for (int i = 0; i < g_currency_count; ++i)
        if (!strcmp(g_currencies[i].currency_id, currency_id)) return &g_currencies[i];
    return NULL;
}

static ImBalance *econ_balance(const char *currency_id, const char *account, int create) {
    for (int i = 0; i < g_balance_count; ++i)
        if (!strcmp(g_balances[i].currency_id, currency_id) && !strcmp(g_balances[i].account, account))
            return &g_balances[i];
    if (!create || g_balance_count >= IM_ECON_BALANCES) return NULL;
    ImBalance *b = &g_balances[g_balance_count++];
    memset(b, 0, sizeof *b);
    snprintf(b->currency_id, sizeof b->currency_id, "%s", currency_id);
    snprintf(b->account, sizeof b->account, "%s", account);
    return b;
}

/* the account's domain is its "<domain>/<name>" prefix (§43.2: different
   domains cannot transfer directly) */
static void econ_account_domain(const char *account, char *out, size_t cap) {
    const char *slash = strchr(account, '/');
    size_t n = slash ? (size_t)(slash - account) : strlen(account);
    if (n >= cap) n = cap - 1;
    memcpy(out, account, n);
    out[n] = 0;
}

/* append an auditable event: hash = sha256(prev || canonical fields) */
static void econ_append(uint64_t seq, const char *kind, const char *currency_id,
                        const char *from, const char *to, long long amount,
                        const char *idem, ImEconEvent *out) {
    ImEconEvent *ev;
    if (g_econ_event_count < IM_ECON_EVENTS) ev = &g_econ_events[g_econ_event_count++];
    else {
        memmove(g_econ_events, g_econ_events + 1, (IM_ECON_EVENTS - 1) * sizeof g_econ_events[0]);
        ev = &g_econ_events[IM_ECON_EVENTS - 1];
    }
    memset(ev, 0, sizeof *ev);
    ev->seq = seq;
    snprintf(ev->kind, sizeof ev->kind, "%s", kind);
    snprintf(ev->currency_id, sizeof ev->currency_id, "%s", currency_id);
    snprintf(ev->from, sizeof ev->from, "%s", from ? from : "");
    snprintf(ev->to, sizeof ev->to, "%s", to ? to : "");
    ev->amount = amount;
    snprintf(ev->idem, sizeof ev->idem, "%s", idem ? idem : "");
    snprintf(ev->prev, sizeof ev->prev, "%s", g_econ_tail);
    char canon[768];
    snprintf(canon, sizeof canon, "%llu|%s|%s|%s|%s|%lld|%s",
             (unsigned long long)seq, ev->kind, ev->currency_id, ev->from, ev->to,
             amount, ev->idem);
    /* hash = sha256(prev_hash || canonical fields): the chain commits to its
       predecessor, so rewriting history is detectable (§43.2 auditability) */
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ev->prev, strlen(ev->prev));
    sha256_update(&ctx, canon, strlen(canon));
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    sha256_hex_of_digest(digest, ev->hash);
    snprintf(g_econ_tail, sizeof g_econ_tail, "%s", ev->hash);
    if (out) *out = *ev;
}

/* ---------- §43.5 migration: export/import helpers ---------- */

/* one balance line: the unit both the local snapshot and an imported package
   are reduced to before hashing, so the two can never disagree by accident */
typedef struct { char account[160]; long long amount; int version; } EconBalLine;

/* sha256 over "<account>|<amount>|<version>\n" lines sorted by account.  The
   digest is order-independent only because the writer sorts first, so every
   producer of a balances_hash must go through here. */
static void econ_digest_lines(EconBalLine *items, int n, char out[65]) {
    for (int i = 1; i < n; ++i) {
        int j = i;
        while (j > 0 && strcmp(items[j - 1].account, items[j].account) > 0) {
            EconBalLine t = items[j - 1]; items[j - 1] = items[j]; items[j] = t;
            j--;
        }
    }
    Sha256Ctx ctx;
    sha256_init(&ctx);
    for (int i = 0; i < n; ++i) {
        char line[256];
        snprintf(line, sizeof line, "%s|%lld|%d\n", items[i].account, items[i].amount, items[i].version);
        sha256_update(&ctx, line, strlen(line));
    }
    uint8_t dg[32];
    sha256_final(&ctx, dg);
    sha256_hex_of_digest(dg, out);
}

/* hash of the local balance snapshot for a currency */
static void econ_balances_digest(const char *currency_id, char out[65]) {
    EconBalLine items[IM_ECON_BALANCES];
    int n = 0;
    for (int i = 0; i < g_balance_count && n < IM_ECON_BALANCES; ++i)
        if (!strcmp(g_balances[i].currency_id, currency_id)) {
            snprintf(items[n].account, sizeof items[n].account, "%s", g_balances[i].account);
            items[n].amount = g_balances[i].amount;
            items[n].version = g_balances[i].version;
            n++;
        }
    econ_digest_lines(items, n, out);
}

/* tail hash of the locally committed ledger for this currency */
static void econ_ledger_digest(const char *currency_id, char out[65]) {
    char tail[65] = "0";
    for (int i = 0; i < g_econ_event_count; ++i)
        if (!currency_id[0] || !strcmp(g_econ_events[i].currency_id, currency_id))
            snprintf(tail, sizeof tail, "%s", g_econ_events[i].hash);
    snprintf(out, 65, "%s", tail);
}

/* ---------- §43.5 migration: verify a package against its own bytes ----------

   An imported package is a claim, and every part of it must be reproduced by
   the package itself instead of taken on trust (§43.2 auditability):

     hash_i        = sha256(prev_i || seq|kind|currency|from|to|amount|idem)
     prev_i        = hash_(i-1)                       within the slice
     ledger_tail   = hash_(last)
     balances_hash = digest of the shipped balances array
     balances      = the replay of the shipped ledger slice from zero

   The last rule is what pins a slice whose origin lies outside the package.  A
   per-currency export is a slice of ONE global chain (g_econ_tail), so its
   first `prev` can name an event belonging to another currency and the chain
   cannot be traced to a genesis in isolation -- that case is reported as
   partial_slice.  Balances, however, are derived by applying exactly these
   events from zero, so a slice that is complete for its currency still
   reproduces the snapshot entry for entry, while one whose history was
   truncated at the head cannot.  That is what makes deleting the first ledger
   entry detectable even though the remaining tail still matches ledger_tail. */

static void econ_entry_hash(const char *prev, long long seq, const char *kind,
                            const char *currency_id, const char *from, const char *to,
                            long long amount, const char *idem, char out[65]) {
    char canon[768];
    snprintf(canon, sizeof canon, "%llu|%s|%s|%s|%s|%lld|%s",
             (unsigned long long)seq, kind, currency_id, from, to, amount, idem);
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, prev, strlen(prev));
    sha256_update(&ctx, canon, strlen(canon));
    uint8_t dg[32];
    sha256_final(&ctx, dg);
    sha256_hex_of_digest(dg, out);
}

/* balances replayed from a ledger slice, addressed by account */
typedef struct {
    char      account[IM_ECON_BALANCES][160];
    long long amount[IM_ECON_BALANCES];
    int       version[IM_ECON_BALANCES];
    int       n;
} EconReplay;

static int econ_replay_slot(EconReplay *r, const char *account) {
    for (int i = 0; i < r->n; ++i) if (!strcmp(r->account[i], account)) return i;
    if (r->n >= IM_ECON_BALANCES) return -1;
    int at = r->n++;
    snprintf(r->account[at], sizeof r->account[at], "%s", account);
    r->amount[at] = 0;
    r->version[at] = 0;
    return at;
}

/* digest of the balances array as shipped inside the package */
static void econ_pkg_balances_digest(const VjVal *pkg, char out[65]) {
    EconBalLine items[IM_ECON_BALANCES];
    int n = 0;
    const VjVal *bal = vj_get(pkg, "balances");
    if (bal && bal->type == VJ_ARR)
        for (size_t i = 0; i < bal->n && n < IM_ECON_BALANCES; ++i) {
            const VjVal *b = bal->items[i];
            const char *acct = b ? vj_str(vj_get(b, "account"), NULL) : NULL;
            if (!acct) continue;
            snprintf(items[n].account, sizeof items[n].account, "%s", acct);
            items[n].amount = vj_int(vj_get(b, "amount"), 0);
            items[n].version = (int)vj_int(vj_get(b, "version"), 0);
            n++;
        }
    econ_digest_lines(items, n, out);
}

/* the shipped snapshot must be entry-for-entry the replay of the ledger */
static int econ_snapshot_is_replay(const VjVal *pkg, const EconReplay *r) {
    const VjVal *bal = vj_get(pkg, "balances");
    int n = (bal && bal->type == VJ_ARR) ? (int)bal->n : 0;
    if (n != r->n) return 0;
    for (int i = 0; i < n; ++i) {
        const VjVal *b = bal->items[i];
        const char *acct = b ? vj_str(vj_get(b, "account"), NULL) : NULL;
        if (!acct) return 0;
        int at = -1;
        for (int j = 0; j < r->n; ++j) if (!strcmp(r->account[j], acct)) { at = j; break; }
        if (at < 0) return 0;
        if (vj_int(vj_get(b, "amount"), 0) != r->amount[at]) return 0;
        if ((int)vj_int(vj_get(b, "version"), 0) != r->version[at]) return 0;
    }
    return 1;
}

/* NULL when the package agrees with itself, otherwise the protocol error code
   to answer with.  *partial_slice is derived from the package's own bytes: a
   slice whose first prev is not the genesis marker cannot be traced to a
   genesis from this package alone. */
static const char *econ_verify_package(const VjVal *pkg, const char *cid,
                                       const char *ldig, const char *bdig,
                                       int *partial_slice, char *reason, size_t reasonlen) {
    *partial_slice = 0;
    const VjVal *ledger = vj_get(pkg, "ledger");
    if (ledger && ledger->type != VJ_ARR) {
        snprintf(reason, reasonlen, "ledger is not an array");
        return "ledger_chain_broken";
    }
    int n = (ledger && ledger->type == VJ_ARR) ? (int)ledger->n : 0;
    if (n > IM_ECON_EVENTS) {
        snprintf(reason, reasonlen, "ledger holds more entries than the hub retains");
        return "ledger_chain_broken";
    }

    EconReplay replay;
    memset(&replay, 0, sizeof replay);
    char prev[65] = "0";
    long long prev_seq = 0;
    for (int i = 0; i < n; ++i) {
        const VjVal *ev = ledger->items[i];
        if (!ev || ev->type != VJ_OBJ) {
            snprintf(reason, reasonlen, "ledger[%d] is not an object", i);
            return "ledger_chain_broken";
        }
        const char *kind  = vj_str(vj_get(ev, "kind"), NULL);
        const char *from  = vj_str(vj_get(ev, "from"), "");
        const char *to    = vj_str(vj_get(ev, "to"), "");
        const char *idem  = vj_str(vj_get(ev, "idem"), "");
        const char *eprev = vj_str(vj_get(ev, "prev"), NULL);
        const char *ehash = vj_str(vj_get(ev, "hash"), NULL);
        long long seq    = vj_int(vj_get(ev, "seq"), 0);
        long long amount = vj_int(vj_get(ev, "amount"), 0);
        if (!kind || !eprev || !ehash || seq < 1) {
            snprintf(reason, reasonlen, "ledger[%d] is missing seq/kind/prev/hash", i);
            return "ledger_chain_broken";
        }
        if (i == 0) {
            *partial_slice = strcmp(eprev, "0") != 0;
        } else if (seq <= prev_seq) {
            snprintf(reason, reasonlen, "ledger[%d].seq does not increase", i);
            return "ledger_chain_broken";
        } else if (seq == prev_seq + 1 && strcmp(eprev, prev) != 0) {
            /* the retained ledger is ONE chain shared by every currency, so a
               currency's slice can skip over another currency's events; only
               adjacent sequence numbers are guaranteed to link directly */
            snprintf(reason, reasonlen, "ledger[%d].prev does not link to ledger[%d].hash", i, i - 1);
            return "ledger_chain_broken";
        }
        char calc[65];
        econ_entry_hash(eprev, seq, kind, cid, from, to, amount, idem, calc);
        if (strcmp(calc, ehash) != 0) {
            snprintf(reason, reasonlen, "ledger[%d].hash does not commit to its own fields", i);
            return "ledger_chain_broken";
        }
        snprintf(prev, sizeof prev, "%s", ehash);
        prev_seq = seq;

        int at;
        if (!strcmp(kind, "mint")) {
            if (!to[0] || (at = econ_replay_slot(&replay, to)) < 0) {
                snprintf(reason, reasonlen, "ledger[%d] does not attribute its mint", i);
                return "ledger_chain_broken";
            }
            replay.amount[at] += amount;
            replay.version[at]++;
        } else if (!strcmp(kind, "transfer")) {
            if (!to[0] || (at = econ_replay_slot(&replay, to)) < 0) {
                snprintf(reason, reasonlen, "ledger[%d] does not attribute its credit", i);
                return "ledger_chain_broken";
            }
            replay.amount[at] += amount;
            replay.version[at]++;
            if (!from[0] || (at = econ_replay_slot(&replay, from)) < 0) {
                snprintf(reason, reasonlen, "ledger[%d] does not attribute its debit", i);
                return "ledger_chain_broken";
            }
            replay.amount[at] -= amount;
            replay.version[at]++;
        } else {
            snprintf(reason, reasonlen, "ledger[%d] has the unknown kind '%s'", i, kind);
            return "ledger_chain_broken";
        }
    }

    if (n == 0) {
        if (strcmp(ldig, "0") != 0) {
            snprintf(reason, reasonlen, "ledger_tail is not the genesis marker but the ledger is empty");
            return "ledger_chain_broken";
        }
    } else if (strcmp(prev, ldig) != 0) {
        snprintf(reason, reasonlen, "ledger_tail does not match the last entry's hash");
        return "ledger_chain_broken";
    }

    char bcalc[65];
    econ_pkg_balances_digest(pkg, bcalc);
    if (strcmp(bcalc, bdig) != 0) {
        snprintf(reason, reasonlen, "balances_hash does not recompute from the balance snapshot");
        return "integrity_failed";
    }
    if (!econ_snapshot_is_replay(pkg, &replay)) {
        snprintf(reason, reasonlen, "the balance snapshot is not the replay of the ledger slice");
        return "ledger_chain_broken";
    }
    return NULL;
}

/* keep the shipped snapshot with the import record: an imported history must
   stay distinguishable from locally committed events (§43.5) */
static void econ_record_snapshot(ImImportedLedger *il, const VjVal *pkg) {
    const VjVal *bal = vj_get(pkg, "balances");
    il->balance_count = 0;
    if (!bal || bal->type != VJ_ARR) return;
    for (size_t i = 0; i < bal->n && il->balance_count < 64; ++i) {
        const VjVal *b = bal->items[i];
        const char *acct = b ? vj_str(vj_get(b, "account"), NULL) : NULL;
        if (!acct) continue;
        int at = il->balance_count;
        snprintf(il->accounts[at], sizeof il->accounts[at], "%s", acct);
        il->amounts[at] = vj_int(vj_get(b, "amount"), 0);
        il->versions[at] = (int)vj_int(vj_get(b, "version"), 0);
        il->balance_count++;
    }
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
        int reattach = n > 0 && strstr(req, "POST /session/reattach") != NULL;
        int state_get = n > 0 && strstr(req, "GET /session/state") != NULL;
        int idem = n > 0 && strstr(req, "POST /session/idem") != NULL;
        /* §43 economy */
        int econ_domain = n > 0 && strstr(req, "POST /economy/domain") != NULL;
        int econ_domain_get = n > 0 && strstr(req, "GET /economy/domain/") != NULL;
        int econ_settle = n > 0 && strstr(req, "POST /economy/settle") != NULL;
        int econ_mint = n > 0 && strstr(req, "POST /economy/mint") != NULL;
        int econ_balance_get = n > 0 && strstr(req, "GET /economy/balance") != NULL;
        int econ_audit = n > 0 && strstr(req, "GET /economy/audit") != NULL;
        int econ_bridge_post = n > 0 && strstr(req, "POST /economy/bridge") != NULL;
        int econ_bridge_get = n > 0 && strstr(req, "GET /economy/bridge") != NULL;
        int econ_export = n > 0 && strstr(req, "POST /economy/export") != NULL;
        int econ_import = n > 0 && strstr(req, "POST /economy/import") != NULL;
        int portal = n > 0 && strstr(req, "POST /portal") != NULL;
        int status = 200;
        char request_token[64] = "";
        (void)json_field_string(req, "token", request_token, sizeof request_token);
        char signal_verse[128] = "", signal_peer[128] = "";
        (void)json_field_string(req, "verse", signal_verse, sizeof signal_verse); (void)json_field_string(req, "peer", signal_peer, sizeof signal_peer);
        if (signal && !token_allows(request_token, signal_verse, signal_peer)) { signal = 0; status = 403; }
        char hubbuf[65536]; size_t hublen = hub_body(req, hubbuf, sizeof hubbuf, &status); int hub = hublen > 0;
        int ok = health || ping || find || friends_get || friends_post || resume || session_stop || signal || revoke || register_route || route_post || route_get || nat_get || portal || hub || node_advertise || node_discover || node_schedule || node_handoff || auth_post || auth_get || reattach || state_get || idem || econ_domain || econ_domain_get || econ_settle || econ_mint || econ_balance_get || econ_audit || econ_bridge_post || econ_bridge_get || econ_export || econ_import;
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
                    im_crp_session_set_generation(&a->sess, (uint64_t)a->generation);
                    /* the previous authority's events belong to the old
                       generation: replaying them under the new authority
                       would resurrect stale state (§55.6) */
                    for (int si = 0; si < g_session_count; ++si)
                        if (!strcmp(g_sessions[si].verse, verse) && !strcmp(g_sessions[si].peer, peer)) {
                            g_sessions[si].event_count = 0;
                            g_sessions[si].seq = 0;   /* the sequence domain restarts */
                        }
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
        if (reattach) {
            /* §55.6 reconnect: the client sends its generation, last received
               and last committed sequences plus its snapshot hash.  After an
               authority change the only safe resume is a snapshot and
               last_applied restarts at 0. */
            char verse[128] = "", peer[128] = "", snap[65] = "";
            uint64_t gen = 0, last_received = 0, last_committed = 0;
            (void)json_field_string(req, "verse", verse, sizeof verse);
            (void)json_field_string(req, "peer", peer, sizeof peer);
            (void)json_field_string(req, "client_snapshot_hash", snap, sizeof snap);
            (void)json_field_u64(req, "generation", &gen);
            (void)json_field_u64(req, "last_received_sequence", &last_received);
            (void)json_field_u64(req, "last_committed_sequence", &last_committed);
            char tk[64] = "";
            (void)json_field_string(req, "token", tk, sizeof tk);
            if (tk[0] && !token_allows(tk, verse, peer)) {
                status = 403; body = "{\"error\":\"invalid_capability_token\"}\n";
                goto reattach_done;
            }
            ImAuthority *a = auth_find(verse, peer);
            uint64_t cur_gen = a ? (uint64_t)a->generation : 1;
            /* authority's view of the committed sequence comes from the
               retained event window (the same source /session/resume uses) */
            uint64_t have = 0;
            for (int i = 0; i < g_session_count; ++i)
                if (!strcmp(g_sessions[i].verse, verse) && !strcmp(g_sessions[i].peer, peer))
                    have = g_sessions[i].seq;
            ImCrpResumePlan plan;
            ImCrpSession tmp;
            im_crp_session_init(&tmp, 1);
            tmp.generation = cur_gen;
            tmp.last_applied = have;
            if (im_crp_session_reattach_plan(&tmp, gen ? gen : cur_gen,
                                             last_received, last_committed,
                                             SESSION_EVENT_WINDOW, &plan) != 0) {
                status = 500; body = "{\"error\":\"reattach_failed\"}\n";
            } else if (plan.needs_snapshot) {
                snprintf(nodebuf, sizeof nodebuf,
                         "{\"resume\":\"snapshot_required\",\"reason\":\"%s\",\"authority_changed\":%d,"
                         "\"last_applied\":0,\"generation\":%llu}\n",
                         plan.reason, plan.authority_changed, (unsigned long long)cur_gen);
                body = nodebuf;
            } else {
                size_t used = (size_t)snprintf(nodebuf, sizeof nodebuf,
                                               "{\"resume\":\"replay\",\"reason\":\"replay\",\"replay_from\":%llu,"
                                               "\"last_applied\":%llu,\"generation\":%llu,\"replay\":[",
                                               (unsigned long long)plan.replay_from,
                                               (unsigned long long)plan.last_applied,
                                               (unsigned long long)cur_gen);
                int first = 1;
                for (int i = 0; i < g_session_count && used < sizeof nodebuf - 320; ++i)
                    if (!strcmp(g_sessions[i].verse, verse) && !strcmp(g_sessions[i].peer, peer)) {
                        ImSession *s = &g_sessions[i];
                        for (int j = 0; j < s->event_count; ++j)
                            if (s->event_seq[j] >= plan.replay_from) {
                                used += (size_t)snprintf(nodebuf + used, sizeof nodebuf - used,
                                                         "%s{\"seq\":%llu,\"event\":\"%s\"}",
                                                         first ? "" : ",", (unsigned long long)s->event_seq[j], s->event[j]);
                                first = 0;
                            }
                    }
                snprintf(nodebuf + used, sizeof nodebuf - used, "]}\n");
                body = nodebuf;
            }
reattach_done: ;
        }
        if (state_get) {
            char verse[128] = "", peer[128] = "";
            (void)query_param(req, "verse", verse, sizeof verse);
            (void)query_param(req, "peer", peer, sizeof peer);
            ImAuthority *a = auth_find(verse, peer);
            const char *st = !a ? "idle" : a->frozen ? "read_only" : "connected";
            snprintf(nodebuf, sizeof nodebuf,
                     "{\"state\":\"%s\",\"generation\":%d,\"authority\":\"%s\","
                     "\"pending_inputs\":%d,\"rejected_inputs\":%d,\"event_tail\":\"",
                     st, a ? a->generation : 0, a ? a->authority : "none",
                     a ? a->sess.pending_inputs : 0, a ? a->sess.rejected_inputs : 0);
            char tail[65];
            session_event_tail_hash(verse, peer, tail);
            size_t su = strlen(nodebuf);
            snprintf(nodebuf + su, sizeof nodebuf - su, "%s\"}\n", tail);
            body = nodebuf;
        }
        if (idem) {
            /* §55.6: side-effecting requests carry an idempotency key; a
               repeat is reported instead of re-applied */
            char verse[128] = "", peer[128] = "", key[160] = "";
            (void)json_field_string(req, "verse", verse, sizeof verse);
            (void)json_field_string(req, "peer", peer, sizeof peer);
            (void)json_field_string(req, "key", key, sizeof key);
            char tk[64] = "";
            (void)json_field_string(req, "token", tk, sizeof tk);
            ImAuthority *a = auth_get_or_create(verse, peer);
            if (tk[0] && !token_allows(tk, verse, peer)) {
                status = 403; body = "{\"error\":\"invalid_capability_token\"}\n";
            } else if (!verse[0] || !peer[0] || !key[0]) {
                status = 400; body = "{\"error\":\"verse_peer_key_required\"}\n";
            } else if (!a) {
                status = 507; body = "{\"error\":\"authority_registry_full\"}\n";
            } else {
                int fresh = im_crp_session_idem_begin(&a->sess, key);
                (void)im_crp_session_idem_end(&a->sess, key, 1);
                snprintf(nodebuf, sizeof nodebuf, "{\"status\":\"%s\",\"key\":\"%s\"}\n",
                         fresh ? "new" : "replay", key);
                body = nodebuf;
            }
        }
        char econbuf[16384];
        if (econ_domain) {
            /* CurrencyDefinition must be signed by its issuer: currency_id is
               the hash of the canonical definition, so an issuer cannot be
               impersonated and the id is verifiable (§43.2 separate objects) */
            char domain_id[64] = "", issuer[128] = "", kind[24] = "";
            char supply[128] = "", denom[32] = "", transfer[64] = "", sig[160] = "";
            uint64_t expires = 0;
            (void)json_field_string(req, "domain_id", domain_id, sizeof domain_id);
            (void)json_field_string(req, "issuer", issuer, sizeof issuer);
            (void)json_field_string(req, "value_kind", kind, sizeof kind);
            (void)json_field_string(req, "supply_rule", supply, sizeof supply);
            (void)json_field_string(req, "denomination", denom, sizeof denom);
            (void)json_field_string(req, "transfer_policy", transfer, sizeof transfer);
            (void)json_field_string(req, "signature", sig, sizeof sig);
            (void)json_field_u64(req, "expires_at", &expires);
            int kind_ok = !strcmp(kind, "utility") || !strcmp(kind, "reputation") ||
                          !strcmp(kind, "symbolic") || !strcmp(kind, "real");
            if (!domain_id[0] || !issuer[0] || !denom[0] || !sig[0]) {
                status = 400; body = "{\"error\":\"domain_issuer_denomination_signature_required\"}\n";
            } else if (!kind_ok) {
                /* the four value kinds are distinct and never interchangeable
                   by default (§43.1) */
                status = 400; body = "{\"error\":\"invalid_value_kind\"}\n";
            } else {
                char canon[1024];
                snprintf(canon, sizeof canon, "%s|%s|%s|%s|%s|%s",
                         domain_id, issuer, kind, supply, denom, transfer);
                char cid[65];
                sha256_hex(canon, strlen(canon), cid);
                if (!http_node_verify(issuer, canon, sig)) {
                    fprintf(stderr, "[hub] currency definition rejected: invalid issuer signature (domain %.24s)\n", domain_id);
                    status = 400; body = "{\"error\":\"invalid_signature\"}\n";
                } else {
                    ImCurrency *c = econ_currency(cid);
                    if (!c && g_currency_count < IM_ECON_DOMAINS) {
                        c = &g_currencies[g_currency_count++];
                        memset(c, 0, sizeof *c);
                    }
                    if (!c) { status = 507; body = "{\"error\":\"currency_registry_full\"}\n"; }
                    else {
                        snprintf(c->currency_id, sizeof c->currency_id, "%s", cid);
                        snprintf(c->domain_id, sizeof c->domain_id, "%s", domain_id);
                        snprintf(c->issuer, sizeof c->issuer, "%s", issuer);
                        snprintf(c->value_kind, sizeof c->value_kind, "%s", kind);
                        snprintf(c->supply_rule, sizeof c->supply_rule, "%s", supply);
                        snprintf(c->denomination, sizeof c->denomination, "%s", denom);
                        snprintf(c->transfer_policy, sizeof c->transfer_policy, "%s", transfer);
                        snprintf(c->definition, sizeof c->definition, "%s", canon);
                        snprintf(c->signature, sizeof c->signature, "%s", sig);
                        c->expires_at_ms = expires;
                        snprintf(econbuf, sizeof econbuf,
                                 "{\"ok\":true,\"currency_id\":\"%s\",\"domain_id\":\"%s\",\"value_kind\":\"%s\"}\n",
                                 cid, domain_id, kind);
                        body = econbuf;
                    }
                }
            }
        }
        if (econ_domain_get) {
            /* "GET /economy/domain/" is 20 bytes long, so the id begins after
               the 20th character; the previous +19 left a leading '/' and the
               lookup could never match (see docs/STATUS.md §2.3) */
            const char *p = strstr(req, "GET /economy/domain/") + 20;
            char cid[80] = "";
            size_t i = 0;
            while (p[i] && p[i] != ' ' && p[i] != '?' && i + 1 < sizeof cid) { cid[i] = p[i]; i++; }
            cid[i] = 0;
            ImCurrency *c = econ_currency(cid);
            if (!c) { status = 404; body = "{\"error\":\"currency_not_found\"}\n"; }
            else {
                snprintf(econbuf, sizeof econbuf,
                         "{\"currency_id\":\"%s\",\"domain_id\":\"%s\",\"issuer\":\"%s\","
                         "\"value_kind\":\"%s\",\"denomination\":\"%s\",\"supply_rule\":\"%s\","
                         "\"transfer_policy\":\"%s\",\"expires_at\":%llu}\n",
                         c->currency_id, c->domain_id, c->issuer, c->value_kind, c->denomination,
                         c->supply_rule, c->transfer_policy, (unsigned long long)c->expires_at_ms);
                body = econbuf;
            }
        }
        if (econ_settle || econ_mint) {
            char cid[80] = "", from[160] = "", to[160] = "", key[128] = "", sig[160] = "";
            long long amount = 0;
            (void)json_field_string(req, "currency_id", cid, sizeof cid);
            (void)json_field_string(req, "from", from, sizeof from);
            (void)json_field_string(req, "to", to, sizeof to);
            (void)json_field_string(req, "idempotency_key", key, sizeof key);
            (void)json_field_string(req, "signature", sig, sizeof sig);
            { const char *a = strstr(req, "\"amount\""); if (a) { const char *c = strchr(a, ':'); if (c) amount = strtoll(c + 1, NULL, 10); } }
            ImCurrency *cur = econ_currency(cid);
            char from_dom[64] = "", to_dom[64] = "";
            if (to[0]) econ_account_domain(to, to_dom, sizeof to_dom);
            if (from[0]) econ_account_domain(from, from_dom, sizeof from_dom);
            if (!g_econ_idem_ready) { im_crp_session_init(&g_econ_idem, 1); g_econ_idem_ready = 1; }
            if (!cur) {
                status = 404; body = "{\"error\":\"currency_not_found\"}\n";
            } else if (cur->expires_at_ms && cur->expires_at_ms <= http_now_ms()) {
                status = 409; body = "{\"error\":\"currency_expired\"}\n";
            } else if (amount <= 0) {
                status = 400; body = "{\"error\":\"amount_must_be_positive\"}\n";
            } else if (!strchr(to, '/') || (!econ_mint && !strchr(from, '/'))) {
                /* accounts are "<domain_id>/<name>": the domain must be stated,
                   never inferred, so cross-domain checks cannot be bypassed */
                status = 400;
                body = "{\"error\":\"invalid_account_format\",\"expected\":\"<domain_id>/<name>\"}\n";
            } else if (!econ_mint && from_dom[0] && to_dom[0] && strcmp(from_dom, to_dom) != 0) {
                /* §43.2: different economic domains cannot transfer directly;
                   a bridge must be declared explicitly -- never a silent
                   conversion */
                fprintf(stderr, "[hub] cross-domain transfer denied (%s -> %s)\n", from_dom, to_dom);
                status = 409;
                snprintf(econbuf, sizeof econbuf,
                         "{\"error\":\"cross_domain_transfer_denied\",\"from_domain\":\"%s\",\"to_domain\":\"%s\","
                         "\"hint\":\"declare an explicit bridge (POST /economy/bridge)\"}\n", from_dom, to_dom);
                body = econbuf;
            } else {
                char canon[768];
                if (econ_mint)
                    snprintf(canon, sizeof canon, "mint|%s|%s|%lld|%s", cid, to, amount, key);
                else
                    snprintf(canon, sizeof canon, "transfer|%s|%s|%s|%lld|%s", cid, from, to, amount, key);
                int authorized = econ_mint ? http_node_verify(cur->issuer, canon, sig) : 1;
                if (econ_mint && !authorized) {
                    fprintf(stderr, "[hub] mint refused: not signed by the issuer of %.24s\n", cid);
                    status = 403; body = "{\"error\":\"unauthorized_mint\"}\n";
                } else {
                    int fresh = key[0] ? im_crp_session_idem_begin(&g_econ_idem, canon) : 1;
                    if (!fresh) {
                        /* a retry of an already applied settlement must not move
                           money a second time (§55.6 retry discipline) */
                        ImBalance *bf = from[0] ? econ_balance(cid, from, 0) : NULL;
                        ImBalance *bt = econ_balance(cid, to, 0);
                        snprintf(econbuf, sizeof econbuf,
                                 "{\"status\":\"replay\",\"currency_id\":\"%s\","
                                 "\"balance_from\":%lld,\"balance_to\":%lld,\"event_seq\":%llu}\n",
                                 cid, bf ? bf->amount : 0, bt ? bt->amount : 0,
                                 (unsigned long long)g_econ_event_count);
                        body = econbuf;
                    } else {
                        ImBalance *bt = econ_balance(cid, to, 1);
                        ImBalance *bf = (!econ_mint && from[0]) ? econ_balance(cid, from, 1) : NULL;
                        if (!bt || (!econ_mint && !bf)) {
                            status = 507; body = "{\"error\":\"balance_registry_full\"}\n";
                        } else if (!econ_mint && bf->amount < amount) {
                            if (key[0]) im_crp_session_idem_end(&g_econ_idem, canon, 0);
                            status = 409; body = "{\"error\":\"insufficient_balance\"}\n";
                        } else {
                            if (!econ_mint) { bf->amount -= amount; bf->version++; bf->last_settlement_ms = http_now_ms(); }
                            bt->amount += amount;
                            bt->version++;
                            bt->last_settlement_ms = http_now_ms();
                            if (key[0]) im_crp_session_idem_end(&g_econ_idem, canon, 1);
                            ImEconEvent ev;
                            econ_append((uint64_t)g_econ_event_count + 1, econ_mint ? "mint" : "transfer",
                                        cid, econ_mint ? "" : from, to, amount, key, &ev);
                            snprintf(econbuf, sizeof econbuf,
                                     "{\"status\":\"settled\",\"currency_id\":\"%s\",\"kind\":\"%s\","
                                     "\"balance_from\":%lld,\"balance_to\":%lld,\"version_from\":%d,\"version_to\":%d,"
                                     "\"event_seq\":%llu,\"event_hash\":\"%s\"}\n",
                                     cid, econ_mint ? "mint" : "transfer",
                                     bf ? bf->amount : 0, bt->amount, bf ? bf->version : 0, bt->version,
                                     (unsigned long long)ev.seq, ev.hash);
                            body = econbuf;
                        }
                    }
                }
            }
        }
        if (econ_balance_get) {
            char cid[80] = "", account[160] = "";
            (void)query_param(req, "currency_id", cid, sizeof cid);
            (void)query_param(req, "account", account, sizeof account);
            ImBalance *b = econ_balance(cid, account, 0);
            snprintf(econbuf, sizeof econbuf,
                     "{\"currency_id\":\"%s\",\"account\":\"%s\",\"amount\":%lld,\"version\":%d,"
                     "\"custody\":\"local\",\"last_settlement\":%llu}\n",
                     cid, account, b ? b->amount : 0, b ? b->version : 0,
                     (unsigned long long)(b ? b->last_settlement_ms : 0));
            body = econbuf;
        }
        if (econ_audit) {
            char cid[80] = "";
            (void)query_param(req, "currency_id", cid, sizeof cid);
            size_t used = (size_t)snprintf(econbuf, sizeof econbuf, "{\"events\":[");
            int first = 1, n = 0;
            char prev[65] = "0", calc[65];
            int chain_ok = 1;
            for (int i = 0; i < g_econ_event_count && used < sizeof econbuf - 512; ++i) {
                ImEconEvent *ev = &g_econ_events[i];
                if (cid[0] && strcmp(ev->currency_id, cid) != 0) continue;
                /* recompute the chain so the response is self-verifying */
                char canon[768];
                snprintf(canon, sizeof canon, "%llu|%s|%s|%s|%s|%lld|%s",
                         (unsigned long long)ev->seq, ev->kind, ev->currency_id, ev->from, ev->to,
                         ev->amount, ev->idem);
                Sha256Ctx ctx;
                sha256_init(&ctx);
                sha256_update(&ctx, ev->prev, strlen(ev->prev));
                sha256_update(&ctx, canon, strlen(canon));
                uint8_t dg[32];
                sha256_final(&ctx, dg);
                sha256_hex_of_digest(dg, calc);
                if (strcmp(calc, ev->hash) != 0 || strcmp(ev->prev, prev) != 0) chain_ok = 0;
                snprintf(prev, sizeof prev, "%s", ev->hash);
                used += (size_t)snprintf(econbuf + used, sizeof econbuf - used,
                                         "%s{\"seq\":%llu,\"kind\":\"%s\",\"currency_id\":\"%s\","
                                         "\"from\":\"%s\",\"to\":\"%s\",\"amount\":%lld,\"idem\":\"%s\","
                                         "\"prev\":\"%s\",\"hash\":\"%s\"}",
                                         first ? "" : ",", (unsigned long long)ev->seq, ev->kind,
                                         ev->currency_id, ev->from, ev->to, ev->amount, ev->idem,
                                         ev->prev, ev->hash);
                first = 0;
                n++;
            }
            snprintf(econbuf + used, sizeof econbuf - used, "],\"count\":%d,\"chain_ok\":%d}\n", n, chain_ok);
            body = econbuf;
        }
        if (econ_bridge_post) {
            char sd[64] = "", td[64] = "", rate[64] = "", fee[64] = "", limit[64] = "", oracle[64] = "", rollback[128] = "";
            (void)json_field_string(req, "source_domain", sd, sizeof sd);
            (void)json_field_string(req, "target_domain", td, sizeof td);
            (void)json_field_string(req, "rate", rate, sizeof rate);
            (void)json_field_string(req, "fee", fee, sizeof fee);
            (void)json_field_string(req, "limit", limit, sizeof limit);
            (void)json_field_string(req, "oracle", oracle, sizeof oracle);
            (void)json_field_string(req, "rollback_policy", rollback, sizeof rollback);
            if (!sd[0] || !td[0] || !rate[0]) {
                status = 400; body = "{\"error\":\"source_target_rate_required\"}\n";
            } else if (g_bridge_count >= IM_ECON_BRIDGES) {
                status = 507; body = "{\"error\":\"bridge_registry_full\"}\n";
            } else {
                ImBridge *b = &g_bridges[g_bridge_count++];
                memset(b, 0, sizeof *b);
                snprintf(b->source_domain, sizeof b->source_domain, "%s", sd);
                snprintf(b->target_domain, sizeof b->target_domain, "%s", td);
                snprintf(b->rate, sizeof b->rate, "%s", rate);
                snprintf(b->fee, sizeof b->fee, "%s", fee);
                snprintf(b->limit, sizeof b->limit, "%s", limit);
                snprintf(b->oracle, sizeof b->oracle, "%s", oracle);
                snprintf(b->rollback, sizeof b->rollback, "%s", rollback);
                b->paused = 0;
                snprintf(econbuf, sizeof econbuf,
                         "{\"ok\":true,\"source_domain\":\"%s\",\"target_domain\":\"%s\","
                         "\"rate\":\"%s\",\"paused\":0,\"execution\":\"not_implemented\"}\n", sd, td, rate);
                body = econbuf;
            }
        }
        if (econ_bridge_get) {
            char sd[64] = "", td[64] = "";
            (void)query_param(req, "source_domain", sd, sizeof sd);
            (void)query_param(req, "target_domain", td, sizeof td);
            size_t used = (size_t)snprintf(econbuf, sizeof econbuf, "{\"bridges\":[");
            int first = 1;
            for (int i = 0; i < g_bridge_count && used < sizeof econbuf - 400; ++i) {
                ImBridge *b = &g_bridges[i];
                if (sd[0] && strcmp(b->source_domain, sd) != 0) continue;
                if (td[0] && strcmp(b->target_domain, td) != 0) continue;
                used += (size_t)snprintf(econbuf + used, sizeof econbuf - used,
                                         "%s{\"source_domain\":\"%s\",\"target_domain\":\"%s\",\"rate\":\"%s\","
                                         "\"fee\":\"%s\",\"limit\":\"%s\",\"paused\":%d,\"execution\":\"not_implemented\"}",
                                         first ? "" : ",", b->source_domain, b->target_domain, b->rate, b->fee,
                                         b->limit, b->paused);
                first = 0;
            }
            snprintf(econbuf + used, sizeof econbuf - used, "]}\n");
            body = econbuf;
        }
        static char exportbuf[65536];
        if (econ_export) {
            /* §43.5 migration export: everything a compatible runtime needs to
               continue the economy offline -- the signed definition, a balance
               snapshot and the auditable ledger.  Integrity is self-contained
               (no server key required to verify). */
            char cid[80] = "";
            (void)json_field_string(req, "currency_id", cid, sizeof cid);
            ImCurrency *c = econ_currency(cid);
            if (!c) {
                status = 404; body = "{\"error\":\"currency_not_found\"}\n";
            } else {
                char bdig[65], ldig[65], ddig[65];
                econ_balances_digest(cid, bdig);
                econ_ledger_digest(cid, ldig);
                sha256_hex(c->definition, strlen(c->definition), ddig);
                uint64_t now = http_now_ms();
                char meta[512];
                snprintf(meta, sizeof meta, "1|local|%llu|%s|%s|%s|%s",
                         (unsigned long long)now, c->currency_id, ddig, ldig, bdig);
                char chash[65];
                sha256_hex(meta, strlen(meta), chash);
                size_t used = (size_t)snprintf(exportbuf, sizeof exportbuf,
                    "{\"ok\":true,\"format_version\":1,\"service_id\":\"local\",\"exported_at\":%llu,"
                    "\"currency\":{\"currency_id\":\"%s\",\"domain_id\":\"%s\",\"issuer\":\"%s\","
                    "\"value_kind\":\"%s\",\"denomination\":\"%s\",\"definition\":\"%s\",\"signature\":\"%s\"},"
                    "\"balances\":[",
                    (unsigned long long)now, c->currency_id, c->domain_id, c->issuer,
                    c->value_kind, c->denomination, c->definition, c->signature);
                int first = 1;
                for (int i = 0; i < g_balance_count && used < sizeof exportbuf - 400; ++i)
                    if (!strcmp(g_balances[i].currency_id, cid)) {
                        used += (size_t)snprintf(exportbuf + used, sizeof exportbuf - used,
                                                 "%s{\"account\":\"%s\",\"amount\":%lld,\"version\":%d}",
                                                 first ? "" : ",", g_balances[i].account,
                                                 g_balances[i].amount, g_balances[i].version);
                        first = 0;
                    }
                used += (size_t)snprintf(exportbuf + used, sizeof exportbuf - used, "],\"ledger\":[");
                first = 1;
                for (int i = 0; i < g_econ_event_count && used < sizeof exportbuf - 512; ++i) {
                    ImEconEvent *ev = &g_econ_events[i];
                    if (strcmp(ev->currency_id, cid) != 0) continue;
                    used += (size_t)snprintf(exportbuf + used, sizeof exportbuf - used,
                                             "%s{\"seq\":%llu,\"kind\":\"%s\",\"from\":\"%s\",\"to\":\"%s\","
                                             "\"amount\":%lld,\"idem\":\"%s\",\"prev\":\"%s\",\"hash\":\"%s\"}",
                                             first ? "" : ",", (unsigned long long)ev->seq, ev->kind,
                                             ev->from, ev->to, ev->amount, ev->idem, ev->prev, ev->hash);
                    first = 0;
                }
                snprintf(exportbuf + used, sizeof exportbuf - used,
                         "],\"balances_hash\":\"%s\",\"ledger_tail\":\"%s\",\"definition_hash\":\"%s\","
                         "\"content_hash\":\"%s\"}\n", bdig, ldig, ddig, chash);
                body = exportbuf;
            }
        }
        if (econ_import) {
            /* §43.5 migration import.  Every claim is re-derived from the
               package's own bytes: the content hash seals the meta, the issuer
               signature covers the definition, and the ledger slice must
               reproduce both its own chain and the snapshot it ships. */
            char jerr[128] = "";
            /* `req` holds the request line and headers too: parse the body only */
            const char *bodyp = strstr(req, "\r\n\r\n");
            bodyp = bodyp ? bodyp + 4 : req;
            VjVal *pkg = vj_parse(bodyp, jerr, sizeof jerr);
            if (!pkg || pkg->type != VJ_OBJ) {
                fprintf(stderr, "[hub] migration import rejected: malformed package (%s)\n", jerr);
                status = 400; body = "{\"error\":\"malformed\"}\n";
            } else {
                const VjVal *cur = vj_get(pkg, "currency");
                const char *chash  = vj_str(vj_get(pkg, "content_hash"), "");
                const char *bdig   = vj_str(vj_get(pkg, "balances_hash"), "");
                const char *ldig   = vj_str(vj_get(pkg, "ledger_tail"), "");
                const char *ddig   = vj_str(vj_get(pkg, "definition_hash"), "");
                const char *svc    = vj_str(vj_get(pkg, "service_id"), "");
                const char *cid    = vj_str(vj_get(cur, "currency_id"),
                                            vj_str(vj_get(pkg, "currency_id"), ""));
                const char *def    = vj_str(vj_get(cur, "definition"), "");
                const char *sig    = vj_str(vj_get(cur, "signature"), "");
                const char *issuer = vj_str(vj_get(cur, "issuer"),
                                            vj_str(vj_get(pkg, "issuer"), ""));
                long long exported = vj_int(vj_get(pkg, "exported_at"), 0);
                if (!svc[0]) svc = "local";
                if (!cid[0] || !chash[0] || !def[0]) {
                    status = 400; body = "{\"error\":\"currency_content_hash_definition_required\"}\n";
                } else {
                    /* the content hash seals meta + definition + ledger tail +
                       balance snapshot, so editing any of them changes it */
                    char meta[512];
                    snprintf(meta, sizeof meta, "1|%s|%lld|%s|%s|%s|%s",
                             svc, exported, cid, ddig, ldig, bdig);
                    char expect[65];
                    sha256_hex(meta, strlen(meta), expect);
                    char dcalc[65];
                    sha256_hex(def, strlen(def), dcalc);
                    int partial = 0;
                    char reason[192] = "";
                    const char *verr = NULL;
                    if (strcmp(expect, chash) != 0) {
                        fprintf(stderr, "[hub] migration import rejected: content hash mismatch\n");
                        status = 409; body = "{\"error\":\"integrity_failed\"}\n";
                    } else if (strcmp(dcalc, ddig) != 0) {
                        fprintf(stderr, "[hub] migration import rejected: definition hash mismatch\n");
                        status = 409; body = "{\"error\":\"integrity_failed\"}\n";
                    } else if (!http_node_verify(issuer, def, sig)) {
                        fprintf(stderr, "[hub] migration import rejected: bad definition signature\n");
                        status = 409; body = "{\"error\":\"invalid_signature\"}\n";
                    } else if ((verr = econ_verify_package(pkg, cid, ldig, bdig, &partial,
                                                            reason, sizeof reason)) != NULL) {
                        fprintf(stderr, "[hub] migration import rejected (%s): %s\n", verr, reason);
                        status = 409;
                        body = strcmp(verr, "integrity_failed") == 0
                             ? "{\"error\":\"integrity_failed\"}\n"
                             : "{\"error\":\"ledger_chain_broken\"}\n";
                    } else {
                        ImCurrency *existing = econ_currency(cid);
                        int already = 0;
                        for (int i = 0; i < g_import_count; ++i)
                            if (!strcmp(g_imports[i].currency_id, cid)) {
                                already = !strcmp(g_imports[i].content_hash, chash);
                                if (!already) {
                                    status = 409;
                                    body = "{\"error\":\"conflict\",\"detail\":\"a different migration package for this currency was already imported\"}\n";
                                }
                                break;
                            }
                        if (status != 409) {
                            if (!existing && g_currency_count < IM_ECON_DOMAINS) {
                                existing = &g_currencies[g_currency_count++];
                                memset(existing, 0, sizeof *existing);
                                snprintf(existing->currency_id, sizeof existing->currency_id, "%s", cid);
                                snprintf(existing->definition, sizeof existing->definition, "%s", def);
                                snprintf(existing->signature, sizeof existing->signature, "%s", sig);
                                snprintf(existing->issuer, sizeof existing->issuer, "%s", issuer);
                            }
                            if (already) {
                                snprintf(econbuf, sizeof econbuf,
                                         "{\"ok\":true,\"status\":\"already_present\",\"currency_id\":\"%s\","
                                         "\"partial_slice\":%s}\n", cid, partial ? "true" : "false");
                            } else if (g_import_count < IM_IMPORTS) {
                                ImImportedLedger *il = &g_imports[g_import_count++];
                                memset(il, 0, sizeof *il);
                                snprintf(il->currency_id, sizeof il->currency_id, "%s", cid);
                                snprintf(il->content_hash, sizeof il->content_hash, "%s", chash);
                                snprintf(il->ledger_tail, sizeof il->ledger_tail, "%s", ldig);
                                il->partial_slice = partial;
                                econ_record_snapshot(il, pkg);
                                snprintf(econbuf, sizeof econbuf,
                                         "{\"ok\":true,\"status\":\"imported\",\"currency_id\":\"%s\","
                                         "\"ledger_tail\":\"%s\",\"balances_hash\":\"%s\","
                                         "\"partial_slice\":%s}\n",
                                         cid, ldig, bdig, partial ? "true" : "false");
                            } else {
                                status = 507; body = "{\"error\":\"import_registry_full\"}\n";
                            }
                        }
                        if (status != 507 && status != 409) body = econbuf;
                    }
                }
                vj_free(pkg);
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
            /* No enrollment proof, no token: the caller has to prove it may open
               a portal for this exact (verse, peer) before anything is minted or
               registered.  Both refusals happen before the registry is touched,
               so a refused caller leaves no token and no session behind.
               The members are read from the parsed BODY with the engine's own
               reader: the request line and the headers are not part of the
               document the proof covers, and vj_parse decodes escapes and
               whitespace exactly as src/verse/crp.c does for the same bytes. */
            const char *enroll = getenv("CRP_ENROLL_SECRET");
            const char *bodyp = strstr(req, "\r\n\r\n");
            char jerr[128] = "";
            VjVal *pbody;
            bodyp = bodyp ? bodyp + 4 : req;
            pbody = vj_parse(bodyp, jerr, sizeof jerr);
            /* crp_enroll_check() is the registry's own precondition, so this
               listener and src/verse/crp.c cannot disagree about escapes,
               whitespace or String() coercion: a body that parses there parses
               here, and a proof accepted there is accepted here. */
            if (!enroll || !*enroll) { status = 403; body = "{\"error\":\"portal enrollment is not configured\"}\n"; }
            else if (!crp_enroll_check(enroll, vj_get(pbody, "verse"), vj_get(pbody, "peer"), vj_get(pbody, "auth"))) { status = 403; body = "{\"error\":\"invalid enrollment proof\"}\n"; }
            else {
                unsigned long seed = (unsigned long)time(NULL) ^ ++g_token_counter;
                time_t now = time(NULL); unsigned long ttl = 300;
                const char *ttl_env = getenv("CRP_TOKEN_TTL");
                if (ttl_env && *ttl_env) { char *end = NULL; unsigned long v = strtoul(ttl_env, &end, 10); if (end != ttl_env && v > 0 && v <= 86400) ttl = v; }
                char scope_verse[128] = "", scope_peer[128] = "", esc_verse[256] = "", esc_peer[256] = "";
                snprintf(scope_verse, sizeof scope_verse, "%s", vj_str(vj_get(pbody, "verse"), ""));
                snprintf(scope_peer, sizeof scope_peer, "%s", vj_str(vj_get(pbody, "peer"), ""));
                portal_json_escape(scope_verse, esc_verse, sizeof esc_verse);
                portal_json_escape(scope_peer, esc_peer, sizeof esc_peer);
                snprintf(portalbuf, sizeof portalbuf, "{\"token\":\"posix-%lx\",\"expires\":%lu,\"verse\":\"%s\",\"peer\":\"%s\"}\n", seed, (unsigned long)now + ttl, esc_verse, esc_peer);
                char issued[64]; snprintf(issued, sizeof issued, "posix-%lx", seed); token_register(issued, now + (time_t)ttl, scope_verse, scope_peer);
                body = portalbuf;
            }
            vj_free(pbody);
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

/* The port the HTTP listener is actually reachable on.  `--http-port 0` asks
   the kernel for a free port, which the caller cannot know in advance, so it
   is reported here and main.c prints it on the existing `http api:` line.
   Zero means "no HTTP listener running". */
static int g_http_port = 0;

int verse_http_bound_port(void) { return g_http_port; }

/* Bind the UDP half of the hub to `port` and start its loop.  Failure
   degrades to TCP-only -- never silently pretends UDP works. */
static void hub_udp_bind(int port) {
    g_udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_fd < 0) return;
    struct sockaddr_in ua;
    memset(&ua, 0, sizeof ua);
    ua.sin_family = AF_INET;
    ua.sin_port = htons((unsigned short)port);
    /* Loopback, matching the TCP listener: this hub is a local process
       service, so binding the wildcard address would only widen the set of
       sockets that can shadow the port. */
    ua.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_udp_fd, (struct sockaddr *)&ua, sizeof ua) == 0) {
        /* non-blocking + poll: closing an fd another thread blocks on is
           not a reliable wakeup on POSIX, so the loop checks a flag */
        int fl = fcntl(g_udp_fd, F_GETFL, 0);
        (void)fcntl(g_udp_fd, F_SETFL, fl | O_NONBLOCK);
        g_udp_running = 1;
        if (pthread_create(&g_udp_thread, NULL, udp_loop, NULL) != 0) g_udp_running = 0;
    }
    if (!g_udp_running) { close(g_udp_fd); g_udp_fd = -1; }
}

int verse_http_start(int port) {
    if (g_http_running || port < 0 || port > 65535 || im_socket_init() != 0) return g_http_running ? 1 : 0;
    if (!g_state_loaded) { state_load(); g_state_loaded = 1; }
    if (port != 0) {
        /* Explicit port: bind the UDP hub socket BEFORE the TCP listener
           accepts anything.  Ordering is load-bearing: callers (and the test
           suites) wait for the TCP port to accept as their readiness signal,
           so as long as TCP came up first there was a window in which the hub
           answered TCP but silently dropped UDP.  A one-shot datagram sent in
           that window is lost for good -- UDP does not retransmit -- which is
           exactly how hub_dist_regression failed under `ctest -j12` while
           passing in isolation.  Binding UDP first makes "TCP accepts" a
           truthful readiness signal for both transports. */
        hub_udp_bind(port);
        g_http_listener = im_socket_listen("127.0.0.1", (uint16_t)port, 16);
    } else {
        /* Kernel-assigned port.  Both transports must share ONE number
           (GET /v/<id> answers on both), so TCP is bound first to obtain a
           number, then UDP is asked for that same one: an ephemeral port can
           already be owned by another process's datagram socket even though
           it was free for TCP, and the loop then just picks a fresh TCP port.
           The ordering rule above cannot be reproduced here -- there is no
           number to give UDP before TCP has one -- but it does not need to
           be: the caller cannot know the port until this function returns and
           main.c has printed it, so no client can be probing TCP while UDP is
           still unbound. */
        for (int attempt = 0; attempt < 16; ++attempt) {
            g_http_listener = im_socket_listen("127.0.0.1", 0, 16);
            if (!g_http_listener) break;
            int bound = im_socket_local_port(g_http_listener);
            if (bound < 1) break;
            hub_udp_bind(bound);
            if (g_udp_running) break;
            im_socket_close(g_http_listener); g_http_listener = NULL;   /* UDP could not take it */
        }
    }
    if (!g_http_listener || im_socket_set_nonblocking(g_http_listener, 1) != 0) {
        if (g_http_listener) im_socket_close(g_http_listener); g_http_listener = NULL;
        if (g_udp_running) { g_udp_running = 0; pthread_join(g_udp_thread, NULL); if (g_udp_fd >= 0) { close(g_udp_fd); g_udp_fd = -1; } }
        im_socket_shutdown(); return 0;
    }
    g_http_port = im_socket_local_port(g_http_listener);
    if (g_http_port < 1) g_http_port = port;
    g_http_running = 1;
    if (pthread_create(&g_http_thread, NULL, http_loop, NULL) != 0) {
        g_http_running = 0; im_socket_close(g_http_listener); g_http_listener = NULL;
        if (g_udp_running) { g_udp_running = 0; pthread_join(g_udp_thread, NULL); if (g_udp_fd >= 0) { close(g_udp_fd); g_udp_fd = -1; } }
        im_socket_shutdown(); return 0;
    }
    if (!g_probe_running) {
        g_probe_running = 1;
        if (pthread_create(&g_probe_thread, NULL, node_probe_loop, NULL) != 0) g_probe_running = 0;
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
    g_http_port = 0;
    pthread_join(g_http_thread, NULL);
    if (g_http_listener) { im_socket_close(g_http_listener); g_http_listener = NULL; }
    im_socket_shutdown();
}
