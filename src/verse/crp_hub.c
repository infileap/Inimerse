/* crp_hub.c - the CRP registry as a real listening process
 *
 * This is the server half of the two-process closed loop.  It is deliberately
 * the same shape as `inim-server`: a separate binary that owns all state, is
 * started by a python driver, and is driven over a socket by another process
 * (`crp-peer`).  Nothing is linked in-process by the driver, so the closed loop
 * cannot pass through shared memory.
 *
 *   crp-hub <port> <secret-hex> [now-ms]
 *
 * `port` 0 binds an ephemeral port; the chosen port is announced as the first
 * line on stdout: {"ok":true,"port":NNNNN}.  That removes the port race the
 * suite is prone to (docs/STATUS.md 2.9): the driver never guesses a port.
 *
 * Wire protocol: one JSON object per line, request then response.
 *   {"op":"register","payload":{id,name,endpoint,...}}
 *   {"op":"find","payload":{"query":"..."}}
 *   {"op":"portal","payload":{"verse":..,"peer":..}}
 *   {"op":"signal","payload":{"verse":..,"event":..,"data":..,"token":..,"peer":..,"seq":..}}
 *   {"op":"resume","payload":{"verse":..,"peer":..,"token":..,"seq":..,"replay":..}}
 *   {"op":"revoke","payload":{"token":"..."}}
 *   {"op":"status"}                    engine-only introspection
 *   {"op":"now","ms":N}                engine-only clock pin
 *   {"op":"bye"}
 * Any request may carry "frameText": the CRP frame the peer actually put on the
 * wire.  The hub decodes it with crp_decode_line and refuses the request if the
 * frame's type or payload disagrees with the request, so a malformed frame is a
 * test failure rather than a silently ignored string.
 *
 * Responses are {"status":N,"body":{...}} -- the HTTP status and body the JS
 * reference relay would have produced.
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
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "crp.h"

#define HUB_MAX_LINE (CRP_MAX_FRAME_BYTES + 65536)

/* JS truthiness: false, 0, "", null and undefined are falsy (as in crp.c). */
static int vj_truthy(const VjVal *v) {
    if (!v) return 0;
    switch (v->type) {
    case VJ_NULL: return 0;
    case VJ_BOOL: return v->b != 0;
    case VJ_INT:  return v->i != 0;
    case VJ_STR:  return v->s && v->s[0] != '\0';
    default:      return 1;
    }
}

static void buf_clear(CrpBuf *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

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

/* Reads one '\n'-terminated line out of `buf`, refilling from fd as needed.
 * Returns 1 on a line, 0 on a clean EOF, -1 on a protocol/IO error. */
static int next_line(int fd, CrpBuf *buf, CrpBuf *out) {
    for (;;) {
        char *nl = buf->data ? memchr(buf->data, '\n', buf->len) : NULL;
        if (nl) {
            size_t n = (size_t)(nl - buf->data);
            buf_clear(out);
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
        if (buf->len > HUB_MAX_LINE) return -1;
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

static void respond(CrpBuf *out, int status, const char *body) {
    buf_clear(out);
    upp_buf_puts(out, "{\"status\":");
    upp_buf_put_ll(out, status);
    upp_buf_puts(out, ",\"body\":");
    upp_buf_puts(out, body);
    upp_buf_putc(out, '}');
}

static void respond_result(CrpBuf *out, CrpResult *res) {
    respond(out, res->status, res->body.data ? res->body.data : "{}");
    crp_result_free(res);
}

/* ---------------------------------------------------------- frame agreement */

static const char *type_for_op(const char *op) {
    if (strcmp(op, "find") == 0) return "FIND";
    if (strcmp(op, "portal") == 0) return "PORTAL";
    if (strcmp(op, "signal") == 0) return "SIGNAL";
    return NULL;
}

static int val_text_eq(const VjVal *a, const VjVal *b) {
    CrpBuf x, y;
    upp_buf_init(&x);
    upp_buf_init(&y);
    if (a) crp_json_write(&x, a);
    else upp_buf_puts(&x, "undefined");
    if (b) crp_json_write(&y, b);
    else upp_buf_puts(&y, "undefined");
    int eq = x.data && y.data && strcmp(x.data, y.data) == 0;
    upp_buf_free(&x);
    upp_buf_free(&y);
    return eq;
}

/* The fields that identify a request, per op.  A field the peer did not send is
 * skipped: the frame only has to agree where both sides carry the value. */
static const char *const FIELD_TABLE[][4] = {
    { "find",   "query", "event", NULL },
    { "portal", "verse", "peer",  NULL },
    { "signal", "verse", "event", "data" },
};

static int verify_frame(const char *text, const char *op, const VjVal *payload,
                        char *err, size_t errlen) {
    if (!text || !type_for_op(op)) return 0; /* no frame on this request */
    VjVal *f = crp_decode_line(text, strlen(text), err, errlen);
    if (!f) return -1;
    const char *want = type_for_op(op);
    const char *got = vj_str(vj_get(f, "type"), "");
    if (strcmp(got, want) != 0) {
        snprintf(err, errlen, "frame type %s does not match op %s", got, op);
        vj_free(f);
        return -1;
    }
    const VjVal *fp = vj_get(f, "payload");
    for (size_t t = 0; t < sizeof FIELD_TABLE / sizeof FIELD_TABLE[0]; t++) {
        if (strcmp(FIELD_TABLE[t][0], op) != 0) continue;
        for (size_t k = 1; k < 4 && FIELD_TABLE[t][k]; k++) {
            const VjVal *pv = vj_get(payload, FIELD_TABLE[t][k]);
            if (!pv) continue;
            const VjVal *fv = vj_get(fp, FIELD_TABLE[t][k]);
            if (!val_text_eq(pv, fv)) {
                snprintf(err, errlen, "frame payload.%s disagrees with the request",
                         FIELD_TABLE[t][k]);
                vj_free(f);
                return -1;
            }
        }
    }
    vj_free(f);
    return 0;
}

/* ------------------------------------------------------------------ dispatch */

static void handle(CrpRegistry *r, const VjVal *req, CrpBuf *out) {
    const char *op = vj_str(vj_get(req, "op"), "");
    const VjVal *payload = vj_get(req, "payload");
    const char *frametext = vj_str(vj_get(req, "frameText"), NULL);

    char ferr[CRP_ERR_MAX];
    ferr[0] = '\0';
    if (verify_frame(frametext, op, payload, ferr, sizeof ferr) != 0) {
        char body[CRP_ERR_MAX + 64];
        snprintf(body, sizeof body, "{\"error\":\"frame rejected: %s\"}", ferr);
        respond(out, 400, body);
        return;
    }

    if (strcmp(op, "now") == 0) {
        crp_registry_set_now(r, vj_int(vj_get(req, "ms"), 0));
        respond(out, 200, "{\"ok\":true}");
        return;
    }
    if (strcmp(op, "status") == 0) {
        CrpBuf st;
        upp_buf_init(&st);
        crp_registry_status_json(r, &st);
        CrpBuf body;
        upp_buf_init(&body);
        upp_buf_puts(&body, "{\"ok\":true,\"verses\":");
        upp_buf_put_ll(&body, crp_registry_verse_count(r));
        upp_buf_puts(&body, ",\"sessions\":");
        upp_buf_put_ll(&body, crp_registry_session_count(r));
        upp_buf_puts(&body, ",\"revoked\":");
        upp_buf_put_ll(&body, crp_registry_revoked_count(r));
        upp_buf_puts(&body, ",\"registry\":");
        upp_buf_puts(&body, st.data ? st.data : "{}");
        upp_buf_putc(&body, '}');
        respond(out, 200, body.data);
        upp_buf_free(&body);
        upp_buf_free(&st);
        return;
    }

    CrpResult res;
    if (strcmp(op, "register") == 0) {
        static const VjVal EMPTY = { .type = VJ_OBJ };
        res = crp_registry_register(r, payload ? payload : &EMPTY);
    } else if (strcmp(op, "find") == 0) {
        char qbuf[512];
        const VjVal *q = vj_get(payload, "query");
        const char *qs = vj_truthy(q) ? crp_js_string(q, qbuf, sizeof qbuf) : "";
        res = crp_registry_find(r, qs);
    } else if (strcmp(op, "portal") == 0) {
        res = crp_registry_portal(r, vj_get(payload, "verse"), vj_get(payload, "peer"));
    } else if (strcmp(op, "signal") == 0) {
        res = crp_registry_signal(r, vj_get(payload, "verse"), vj_get(payload, "event"),
                                  vj_get(payload, "data"), vj_get(payload, "token"),
                                  vj_get(payload, "peer"), vj_get(payload, "seq"));
    } else if (strcmp(op, "resume") == 0) {
        res = crp_registry_resume(r, vj_get(payload, "verse"), vj_get(payload, "peer"),
                                  vj_get(payload, "token"), vj_get(payload, "seq"),
                                  vj_get(payload, "replay"));
    } else if (strcmp(op, "revoke") == 0) {
        res = crp_registry_revoke(r, vj_get(payload, "token"));
    } else {
        respond(out, 400, "{\"error\":\"unknown op\"}");
        return;
    }
    respond_result(out, &res);
}

/* Returns 1 when the connection ended with an explicit bye. */
static int serve_connection(int fd, CrpRegistry *r, int *exchanges) {
    CrpBuf pending, line, out;
    upp_buf_init(&pending);
    upp_buf_init(&line);
    upp_buf_init(&out);
    int bye = 0;
    for (;;) {
        int rc = next_line(fd, &pending, &line);
        if (rc <= 0) break;
        if (!line.data || line.len == 0) continue;
        char perr[128];
        VjVal *req = vj_parse(line.data, perr, sizeof perr);
        if (!req) {
            char body[192];
            snprintf(body, sizeof body, "{\"error\":\"request is not JSON: %s\"}", perr);
            respond(&out, 400, body);
            if (write_all(fd, out.data, out.len) != 0) break;
            continue;
        }
        const char *op = vj_str(vj_get(req, "op"), "");
        if (strcmp(op, "bye") == 0) {
            respond(&out, 200, "{\"ok\":true}");
            write_all(fd, out.data, out.len);
            write_all(fd, "\n", 1);
            vj_free(req);
            bye = 1;
            break;
        }
        handle(r, req, &out);
        vj_free(req);
        if (write_all(fd, out.data, out.len) != 0) break;
        if (write_all(fd, "\n", 1) != 0) break;
        (*exchanges)++;
    }
    upp_buf_free(&pending);
    upp_buf_free(&line);
    upp_buf_free(&out);
    return bye;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: crp-hub <port> <secret-hex> [now-ms]\n");
        return 2;
    }
    int port = atoi(argv[1]);
    const char *secret = argv[2];
    long long now = argc > 3 ? atoll(argv[3]) : 0;

    signal(SIGPIPE, SIG_IGN);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("crp-hub: socket");
        return 2;
    }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        perror("crp-hub: bind");
        return 2;
    }
    if (listen(lfd, 4) != 0) {
        perror("crp-hub: listen");
        return 2;
    }
    socklen_t alen = sizeof addr;
    if (getsockname(lfd, (struct sockaddr *)&addr, &alen) != 0) {
        perror("crp-hub: getsockname");
        return 2;
    }
    printf("{\"ok\":true,\"port\":%d}\n", (int)ntohs(addr.sin_port));
    fflush(stdout);

    CrpRegistryConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.secret = secret;
    cfg.now_ms = now;
    CrpRegistry *r = crp_registry_new(&cfg);
    if (!r) {
        fprintf(stderr, "crp-hub: cannot create the registry\n");
        return 2;
    }

    int served = 0;
    int exchanges = 0;
    int bye = 0;
    while (!bye) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(lfd, &set);
        struct timeval tv;
        tv.tv_sec = 20;
        tv.tv_usec = 0;
        int sel = select(lfd + 1, &set, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            perror("crp-hub: select");
            break;
        }
        if (sel == 0) break; /* nobody connected: do not hang the test forever */
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("crp-hub: accept");
            break;
        }
        served++;
        bye = serve_connection(cfd, r, &exchanges);
        close(cfd);
    }
    close(lfd);

    printf("{\"ok\":true,\"served\":%d,\"exchanges\":%d,\"bye\":%s}\n",
           served, exchanges, bye ? "true" : "false");
    fflush(stdout);
    crp_registry_free(r);
    if (!bye) {
        fprintf(stderr, "crp-hub: no client sent bye\n");
        return 3;
    }
    return 0;
}
