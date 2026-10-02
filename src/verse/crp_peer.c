/* crp_peer.c - the client half of the two-process CRP closed loop
 *
 *   crp-peer <port> <secret-hex> <now-ms> [peer-id]
 *
 * Connects to a `crp-hub` over a real TCP socket and walks the FIND / PORTAL /
 * SIGNAL / resume / revoke scenarios from tools/crp_relay.test.js against it.
 * Every exchange is printed verbatim (request then response) so the transcript
 * is the evidence; the assertions decide the exit code.
 *
 * The frames this peer puts on the wire are built by crp_frame_* and encoded by
 * crp_encode, and the hub decodes them again: the closed loop therefore
 * exercises the frame builders, the encoder and the decoder across a process
 * boundary rather than inside one address space.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "crp.h"

#define PEER_MAX_LINE (CRP_MAX_FRAME_BYTES + 65536)

typedef struct {
    int fd;
    int exchanges;
    int failures;
    CrpBuf pending;
    CrpBuf raw;
} Peer;

static int write_all(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int next_line(int fd, CrpBuf *buf, CrpBuf *out) {
    for (;;) {
        char *nl = buf->data ? memchr(buf->data, '\n', buf->len) : NULL;
        if (nl) {
            size_t n = (size_t)(nl - buf->data);
            out->len = 0;
            if (out->data) out->data[0] = '\0';
            if (upp_buf_putn(out, buf->data, n) != 0) return -1;
            if (out->len && out->data[out->len - 1] == '\r') {
                out->len--;
                out->data[out->len] = '\0';
            }
            size_t rest = buf->len - n - 1;
            memmove(buf->data, nl + 1, rest);
            buf->len = rest;
            buf->data[rest] = '\0';
            return 1;
        }
        if (buf->len > PEER_MAX_LINE) return -1;
        char tmp[4096];
        ssize_t got = read(fd, tmp, sizeof tmp);
        if (got < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (got == 0) return buf->len ? -1 : 0;
        if (upp_buf_putn(buf, tmp, (size_t)got) != 0) return -1;
    }
}

static int connect_local(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* --------------------------------------------------------------- assertions */

static void expect(Peer *p, int cond, const char *label, const char *detail) {
    if (cond) return;
    p->failures++;
    printf("FAIL %s: %s\n", label, detail ? detail : "");
}

static int want_status(Peer *p, VjVal *resp, long long want, const char *label) {
    if (!resp) return 0; /* peer_call already recorded the failure */
    long long got = vj_int(vj_get(resp, "status"), -1);
    if (got == want) return 1;
    p->failures++;
    printf("FAIL %s: status %lld, want %lld\n", label, got, want);
    return 0;
}

static const VjVal *rbody(VjVal *resp) { return vj_get(resp, "body"); }
static const char *body_str(VjVal *resp, const char *k) { return vj_str(vj_get(rbody(resp), k), NULL); }
static long long body_int(VjVal *resp, const char *k) { return vj_int(vj_get(rbody(resp), k), -999999); }

static void want_error(Peer *p, VjVal *resp, const char *want, const char *label) {
    if (!resp) return;
    const char *got = body_str(resp, "error");
    if (got && strcmp(got, want) == 0) return;
    p->failures++;
    printf("FAIL %s: error <%s>, want <%s>\n", label, got ? got : "(none)", want);
}

/* ------------------------------------------------------------ request builder */

typedef struct { CrpBuf b; int n; } JObj;

static void jo_open(JObj *o) {
    upp_buf_init(&o->b);
    upp_buf_putc(&o->b, '{');
    o->n = 0;
}
static void jo_key(JObj *o, const char *k) {
    if (o->n++) upp_buf_putc(&o->b, ',');
    upp_json_write_string(&o->b, k);
    upp_buf_putc(&o->b, ':');
}
static void jo_str(JObj *o, const char *k, const char *v) {
    jo_key(o, k);
    if (v) upp_json_write_string(&o->b, v);
    else upp_buf_puts(&o->b, "null");
}
static void jo_int(JObj *o, const char *k, long long v) { jo_key(o, k); upp_buf_put_ll(&o->b, v); }
static void jo_bool(JObj *o, const char *k, int v) { jo_key(o, k); upp_buf_puts(&o->b, v ? "true" : "false"); }
static void jo_raw(JObj *o, const char *k, const char *raw) { jo_key(o, k); upp_buf_puts(&o->b, raw); }
static const char *jo_done(JObj *o) {
    upp_buf_putc(&o->b, '}');
    return o->b.data ? o->b.data : "{}";
}

/* Assembles {"op":..,"payload":..[,"frameText":..]} */
static void jreq(CrpBuf *out, const char *op, const char *payload, const char *frame_text) {
    upp_buf_init(out);
    upp_buf_puts(out, "{\"op\":");
    upp_json_write_string(out, op);
    upp_buf_puts(out, ",\"payload\":");
    upp_buf_puts(out, payload ? payload : "{}");
    if (frame_text) {
        upp_buf_puts(out, ",\"frameText\":");
        upp_json_write_string(out, frame_text);
    }
    upp_buf_putc(out, '}');
}

/* crp_encode appends the newline the line protocol wants; the frame is embedded
 * as a JSON string, so drop it. */
static const char *frame_text_of(CrpBuf *raw) {
    if (raw->len && raw->data[raw->len - 1] == '\n') {
        raw->len--;
        raw->data[raw->len] = '\0';
    }
    return raw->data;
}

static VjVal *parse_lit(const char *json) {
    char err[128];
    VjVal *v = vj_parse(json, err, sizeof err);
    if (!v) {
        printf("FAIL peer: literal %s does not parse: %s\n", json, err);
        exit(2);
    }
    return v;
}

/* ------------------------------------------------------------------ exchange */

static VjVal *peer_call(Peer *p, const char *req, const char *label) {
    p->exchanges++;
    printf(">> %s\n", req);
    fflush(stdout);
    if (write_all(p->fd, req, strlen(req)) != 0 || write_all(p->fd, "\n", 1) != 0) {
        p->failures++;
        printf("FAIL %s: cannot write to the hub\n", label);
        return NULL;
    }
    if (next_line(p->fd, &p->pending, &p->raw) != 1) {
        p->failures++;
        printf("FAIL %s: no response from the hub\n", label);
        return NULL;
    }
    printf("<< %s\n", p->raw.data ? p->raw.data : "");
    fflush(stdout);
    char err[128];
    VjVal *resp = vj_parse(p->raw.data, err, sizeof err);
    if (!resp) {
        p->failures++;
        printf("FAIL %s: response is not JSON: %s\n", label, err);
    }
    return resp;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: crp-peer <port> <secret-hex> <now-ms> [peer-id]\n");
        return 2;
    }
    int port = atoi(argv[1]);
    const char *secret = argv[2];
    long long now = atoll(argv[3]);
    const char *self = argc > 4 ? argv[4] : "peer-a";

    signal(SIGPIPE, SIG_IGN);
    Peer p;
    memset(&p, 0, sizeof p);
    upp_buf_init(&p.pending);
    upp_buf_init(&p.raw);
    p.fd = connect_local(port);
    if (p.fd < 0) {
        fprintf(stderr, "crp-peer: cannot connect to 127.0.0.1:%d: %s\n", port, strerror(errno));
        return 2;
    }
    printf("# crp-peer (pid %ld) -> 127.0.0.1:%d, secret %zu bytes, clock %lld\n",
           (long)getpid(), port, strlen(secret), now);

    char err[CRP_ERR_MAX];
    CrpBuf req, raw;
    upp_buf_init(&req);
    upp_buf_init(&raw);
    JObj pl;
    VjVal *resp = NULL;

    /* --- 1..3 register ---------------------------------------------------- */
    jo_open(&pl);
    jo_str(&pl, "id", "demo");
    jo_str(&pl, "name", "Demo Verse");
    jo_str(&pl, "endpoint", "local");
    jreq(&req, "register", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "register demo");
    if (want_status(&p, resp, 200, "register demo")) {
        expect(&p, vj_bool(vj_get(rbody(resp), "ok"), 0), "register demo: ok true", NULL);
    }
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "id", "second");
    jo_str(&pl, "name", "Second");
    jo_str(&pl, "endpoint", "remote");
    jreq(&req, "register", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "register second");
    want_status(&p, resp, 200, "register second");
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "name", "x");
    jreq(&req, "register", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "register without id");
    if (want_status(&p, resp, 400, "register without id"))
        want_error(&p, resp, "id and endpoint are required", "register without id");
    vj_free(resp);
    upp_buf_free(&req);

    /* --- 4..7 find -------------------------------------------------------- */
    {
        CrpBuf frame;
        upp_buf_init(&frame);
        if (crp_frame_find(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"" }, NULL, NULL, "", err, sizeof err) != 0)
            printf("FAIL peer: find frame: %s\n", err);
        jo_open(&pl);
        jo_str(&pl, "query", "");
        jreq(&req, "find", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "find all");
        if (want_status(&p, resp, 200, "find all")) {
            const VjVal *items = vj_get(rbody(resp), "items");
            expect(&p, items && items->type == VJ_ARR && items->n == 2, "find all: two verses", NULL);
            if (items && items->n == 2) {
                expect(&p, strcmp(vj_str(vj_get(items->items[0], "id"), ""), "demo") == 0,
                       "find all: insertion order", NULL);
                expect(&p, vj_get(items->items[0], "updated") == NULL,
                       "find all: updated is stripped", NULL);
                expect(&p, strcmp(vj_str(vj_get(items->items[0], "name"), ""), "Demo Verse") == 0,
                       "find all: name preserved", NULL);
            }
        }
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_find(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"DEM" }, NULL, NULL, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "query", "DEM");
        jreq(&req, "find", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "find DEM");
        if (want_status(&p, resp, 200, "find DEM")) {
            const VjVal *items = vj_get(rbody(resp), "items");
            expect(&p, items && items->n == 1, "find DEM: one match", NULL);
            if (items && items->n == 1)
                expect(&p, strcmp(vj_str(vj_get(items->items[0], "id"), ""), "demo") == 0,
                       "find DEM: matched on the lowercased name", NULL);
        }
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_find(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"zzz" }, NULL, NULL, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "query", "zzz");
        jreq(&req, "find", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "find zzz");
        if (want_status(&p, resp, 200, "find zzz")) {
            const VjVal *items = vj_get(rbody(resp), "items");
            expect(&p, items && items->type == VJ_ARR && items->n == 0, "find zzz: no match", NULL);
        }
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    /* --- 8..10 portal ----------------------------------------------------- */
    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_portal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"nope" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)self }, NULL, NULL, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "nope");
        jo_str(&pl, "peer", self);
        jreq(&req, "portal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "portal unknown");
        if (want_status(&p, resp, 404, "portal unknown"))
            want_error(&p, resp, "verse not found", "portal unknown");
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jreq(&req, "portal", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "portal without peer");
    want_status(&p, resp, 404, "portal without peer");
    vj_free(resp);
    upp_buf_free(&req);

    char token[CRP_TOKEN_MAX];
    token[0] = '\0';
    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_portal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)self }, NULL, NULL, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "peer", self);
        jreq(&req, "portal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "portal demo");
        if (want_status(&p, resp, 200, "portal demo")) {
            const char *t = body_str(resp, "token");
            expect(&p, t && t[0], "portal demo: token issued", NULL);
            if (t) snprintf(token, sizeof token, "%s", t);
            expect(&p, body_int(resp, "expires") == now + 300000,
                   "portal demo: expires = now + tokenTtlMs", NULL);
            expect(&p, strcmp(vj_str(vj_get(rbody(resp), "verse"), ""), "demo") == 0 &&
                           strcmp(vj_str(vj_get(rbody(resp), "peer"), ""), self) == 0,
                   "portal demo: verse and peer echoed", NULL);
        }
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    /* --- 11..16 signal ---------------------------------------------------- */
    const char *DATA = "{\"note\":\"hello\"}";
    VjVal *dataobj = parse_lit(DATA);

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal anonymous");
        if (want_status(&p, resp, 202, "signal anonymous")) {
            expect(&p, body_int(resp, "seq") == 1, "signal anonymous: server assigned seq 1", NULL);
            expect(&p, vj_bool(vj_get(rbody(resp), "accepted"), 0), "signal anonymous: accepted", NULL);
        }
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jo_str(&pl, "token", "garbage");
        jo_str(&pl, "peer", self);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal bad token");
        if (want_status(&p, resp, 403, "signal bad token"))
            want_error(&p, resp, "invalid capability token", "signal bad token");
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jo_str(&pl, "token", token);
        jo_str(&pl, "peer", self);
        jo_int(&pl, "seq", 5);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal explicit seq");
        if (want_status(&p, resp, 202, "signal explicit seq"))
            expect(&p, body_int(resp, "seq") == 5, "signal explicit seq: honoured", NULL);
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jo_str(&pl, "peer", self);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal auto seq");
        if (want_status(&p, resp, 202, "signal auto seq"))
            expect(&p, body_int(resp, "seq") == 6, "signal auto seq: continues from 5", NULL);
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"nope" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "nope");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal unknown verse");
        if (want_status(&p, resp, 404, "signal unknown verse"))
            want_error(&p, resp, "verse not found", "signal unknown verse");
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jreq(&req, "signal", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "signal without event");
    if (want_status(&p, resp, 400, "signal without event"))
        want_error(&p, resp, "verse and event are required", "signal without event");
    vj_free(resp);
    upp_buf_free(&req);

    /* --- 17..22 resume ---------------------------------------------------- */
    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", self);
    jo_str(&pl, "token", token);
    jo_int(&pl, "seq", 0);
    /* The stored sequence is already 6, so a bare seq 0 is refused; replay:true
     * is what makes the relay hand the older events back. */
    jo_bool(&pl, "replay", 1);
    jreq(&req, "resume", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "resume from 0 (replay)");
    if (want_status(&p, resp, 200, "resume from 0")) {
        expect(&p, body_int(resp, "lastSeq") == 6, "resume from 0: lastSeq 6", NULL);
        const VjVal *replay = vj_get(rbody(resp), "replay");
        expect(&p, replay && replay->type == VJ_ARR && replay->n == 2,
               "resume from 0: replays both events of this session", NULL);
        if (replay && replay->n == 2) {
            expect(&p, vj_int(vj_get(replay->items[0], "seq"), -1) == 5 &&
                           vj_int(vj_get(replay->items[1], "seq"), -1) == 6,
                   "resume from 0: replayed seqs are 5 and 6", NULL);
            expect(&p, strcmp(vj_str(vj_get(replay->items[0], "event"), ""), "join") == 0,
                   "resume from 0: replayed event name", NULL);
        }
    }
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", self);
    jo_str(&pl, "token", token);
    jo_int(&pl, "seq", 6);
    jreq(&req, "resume", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "resume from 6");
    if (want_status(&p, resp, 200, "resume from 6")) {
        const VjVal *replay = vj_get(rbody(resp), "replay");
        expect(&p, replay && replay->n == 0, "resume from 6: nothing to replay", NULL);
    }
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", self);
    jo_str(&pl, "token", token);
    jo_int(&pl, "seq", 2);
    jreq(&req, "resume", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "resume out of order");
    if (want_status(&p, resp, 409, "resume out of order")) {
        want_error(&p, resp, "session sequence out of order", "resume out of order");
        expect(&p, body_int(resp, "lastSeq") == 6, "resume out of order: reports lastSeq", NULL);
    }
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", self);
    jo_str(&pl, "token", token);
    jo_int(&pl, "seq", 2);
    jo_bool(&pl, "replay", 1);
    jreq(&req, "resume", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "resume replay:true");
    want_status(&p, resp, 200, "resume replay:true");
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", self);
    jo_str(&pl, "token", "garbage");
    jreq(&req, "resume", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "resume bad token");
    if (want_status(&p, resp, 403, "resume bad token"))
        want_error(&p, resp, "invalid capability token", "resume bad token");
    vj_free(resp);
    upp_buf_free(&req);

    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", self);
    jreq(&req, "resume", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "resume without token");
    if (want_status(&p, resp, 400, "resume without token"))
        want_error(&p, resp, "verse, peer and token are required", "resume without token");
    vj_free(resp);
    upp_buf_free(&req);

    /* --- 23..24 a token is bound to one peer ------------------------------ */
    char other[CRP_TOKEN_MAX];
    other[0] = '\0';
    jo_open(&pl);
    jo_str(&pl, "verse", "demo");
    jo_str(&pl, "peer", "peer-b");
    jreq(&req, "portal", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "portal peer-b");
    if (want_status(&p, resp, 200, "portal peer-b")) {
        const char *t = body_str(resp, "token");
        if (t) snprintf(other, sizeof other, "%s", t);
        expect(&p, t && token[0] && strcmp(t, token) != 0,
               "portal peer-b: a different peer gets a different token", NULL);
    }
    vj_free(resp);
    upp_buf_free(&req);

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jo_str(&pl, "token", other);
        jo_str(&pl, "peer", self);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal with another peer's token");
        if (want_status(&p, resp, 403, "signal with another peer's token"))
            want_error(&p, resp, "invalid capability token", "signal with another peer's token");
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    /* --- 25..26 revoke ---------------------------------------------------- */
    jo_open(&pl);
    jo_str(&pl, "token", token);
    jreq(&req, "revoke", jo_done(&pl), NULL);
    resp = peer_call(&p, req.data, "revoke");
    if (want_status(&p, resp, 200, "revoke"))
        expect(&p, vj_bool(vj_get(rbody(resp), "revoked"), 0), "revoke: revoked true", NULL);
    vj_free(resp);
    upp_buf_free(&req);

    {
        CrpBuf frame;
        upp_buf_init(&frame);
        crp_frame_signal(&frame, &(VjVal){ .type = VJ_STR, .s = (char *)"demo" },
                         &(VjVal){ .type = VJ_STR, .s = (char *)"join" }, dataobj,
                         &(VjVal){ .type = VJ_INT, .i = now }, "", err, sizeof err);
        jo_open(&pl);
        jo_str(&pl, "verse", "demo");
        jo_str(&pl, "event", "join");
        jo_raw(&pl, "data", DATA);
        jo_str(&pl, "token", token);
        jo_str(&pl, "peer", self);
        jreq(&req, "signal", jo_done(&pl), frame_text_of(&frame));
        resp = peer_call(&p, req.data, "signal after revoke");
        if (want_status(&p, resp, 403, "signal after revoke"))
            want_error(&p, resp, "invalid capability token", "signal after revoke");
        vj_free(resp);
        upp_buf_free(&req);
        upp_buf_free(&frame);
    }

    /* --- 27 status: the engine's own view of the same state --------------- */
    jreq(&req, "status", NULL, NULL);
    resp = peer_call(&p, req.data, "status");
    if (want_status(&p, resp, 200, "status")) {
        expect(&p, body_int(resp, "verses") == 2, "status: two verses registered", NULL);
        /* peer-a, the anonymous peer and peer-b each hold a session. */
        expect(&p, body_int(resp, "sessions") == 3, "status: three sessions", NULL);
        expect(&p, body_int(resp, "revoked") == 1, "status: one revoked token", NULL);
        const VjVal *reg = vj_get(rbody(resp), "registry");
        expect(&p, reg && reg->type == VJ_OBJ, "status: registry introspection present", NULL);
    }
    vj_free(resp);
    upp_buf_free(&req);

    /* --- 28 bye ----------------------------------------------------------- */
    jreq(&req, "bye", NULL, NULL);
    resp = peer_call(&p, req.data, "bye");
    want_status(&p, resp, 200, "bye");
    vj_free(resp);
    upp_buf_free(&req);

    vj_free(dataobj);
    upp_buf_free(&p.pending);
    upp_buf_free(&p.raw);
    close(p.fd);

    printf("crp_peer: %d exchanges, %d failures\n", p.exchanges, p.failures);
    return p.failures == 0 ? 0 : 1;
}
