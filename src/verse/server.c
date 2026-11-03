/* server.c - inim-server: authoritative Layer session over stdin/stdout (P1 ①②④)
 *
 * Usage: inim-server [--upp] <root> [verse_id]
 *
 * Reads one canonical-JSON request per line from stdin and writes exactly one
 * response line to stdout, flushed after each.  Diagnostics go to stderr so
 * stdout stays a pure protocol stream.  Exit codes:
 *   0  clean end of session (bye seen, or EOF with a healthy anchor)
 *   2  the closing drain reported an inconsistent Layer
 *   3  the Layer could not be opened or created
 *   4  a UPP frame was malformed, of an unknown type, or out of order
 *   5  the UPP hello could not be negotiated
 *
 * `--upp` puts the same session behind a UPP transport (src/verse/upp.h):
 * every line on stdin and stdout becomes `{upp:1,type,id?,payload}`.  The
 * Layer protocol is unchanged underneath -- its requests travel as the raw
 * JSON text of a `verse` frame's `payload`, and its responses travel back the
 * same way -- so the two modes differ in the envelope and in nothing else.
 *
 * Three things this transport decides that upp.h does not, all of them
 * refusals rather than best-effort interpretations:
 *
 *   1. `verse` frames are only accepted while the session is running, so a
 *      client has to send `hello` and then `start`.  A session that has not
 *      been started has no agreed ABI, and carrying requests over it would
 *      make the hello look decorative.
 *   2. An unknown frame type is refused.  upp_session_apply() ignores types
 *      it does not know (the reference does the same), which is fine for a
 *      state machine and wrong for a transport: a typo in a type string would
 *      otherwise be dropped in silence and the client would wait for a reply
 *      that is never coming.
 *   3. A refused hello ends the process with exit 5 instead of emitting an
 *      `incompatible` frame.  The reference's incompatible frame carries
 *      {required, actual} ABI numbers; upp_session_accept_hello() records the
 *      refusal as a message and does not keep the two numbers, and deriving
 *      them here would put a second copy of the negotiation arithmetic in a
 *      file that owns none of it.  The refusal is named on stderr instead.
 */
#include "protocol.h"
#include "eventlog.h"
#include "upp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VL_LINE_MAX (64 * 1024)

/* The manifest this server announces in a hello.  Only abi and abiRange are
 * ever read -- by the peer's negotiation, and by our own
 * upp_session_accept_hello() -- so the other fields are what a reference
 * client needs to consider the frame well-formed, not facts we act on. */
static const char *UPP_LOCAL_MANIFEST =
    "{\"id\":\"inim-server\",\"name\":\"inim-server\",\"version\":\"1.0.0\","
    "\"engine\":\"inimerse\",\"entry\":\"verse\",\"abi\":1,\"abiRange\":\"1\","
    "\"files\":{},\"capabilities\":[]}";

#define UPP_EXIT_REFUSED 4
#define UPP_EXIT_HELLO   5

static int vl_ensure_layer(const char *root, const char *verse_id) {
    VlManifest m;
    VlStatus st = vl_layer_create(root, verse_id, &m);
    if (st == VL_OK) {
        fprintf(stderr, "inim-server: created verse '%s' under %s\n", verse_id, root);
        return 0;
    }
    if (st == VL_ERR_CONFLICT) return 0;         /* already exists: fine */
    fprintf(stderr, "inim-server: cannot create verse '%s': %s\n", verse_id, vl_status_name(st));
    return -1;
}

/* --------------------------------------------------------------- plain mode */

static int run_plain(VlServer *s) {
    char *line = (char *)malloc(VL_LINE_MAX);
    char *resp = (char *)malloc(VL_LINE_MAX);
    if (!line || !resp) { free(line); free(resp); return 3; }

    for (;;) {
        if (!fgets(line, VL_LINE_MAX, stdin)) break;   /* EOF */
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;

        int stop = vl_server_handle(s, line, resp, VL_LINE_MAX);
        printf("%s\n", resp);
        fflush(stdout);
        if (stop) break;
    }

    free(line);
    free(resp);
    return 0;
}

/* ----------------------------------------------------------------- UPP mode */

static int upp_refuse(const char *reason) {
    fprintf(stderr, "inim-server: upp refused: %s\n", reason);
    return UPP_EXIT_REFUSED;
}

static int run_upp(VlServer *s) {
    /* A frame may be up to 1 MiB, far past VL_LINE_MAX, so the envelope and
     * the Layer request need different buffers: the request that comes out of
     * a payload is bounded by VL_LINE_MAX, the frame that carries it is not. */
    char *line = (char *)malloc(UPP_MAX_FRAME_BYTES + 2);
    char *resp = (char *)malloc(VL_LINE_MAX);
    if (!line || !resp) { free(line); free(resp); return 3; }

    char err[UPP_ERR_MAX];
    err[0] = '\0';

    VjVal *manifest = vj_parse(UPP_LOCAL_MANIFEST, err, sizeof err);
    if (!manifest) {
        fprintf(stderr, "inim-server: upp manifest is unparseable: %s\n", err);
        free(line); free(resp);
        return 3;
    }

    UppSession session;
    if (upp_session_init(&session, "host", manifest, err, sizeof err) != 0) {
        fprintf(stderr, "inim-server: upp session refused: %s\n", err);
        vj_free(manifest);
        free(line); free(resp);
        return 3;
    }
    vj_free(manifest);

    int rc = 0;
    for (;;) {
        if (!fgets(line, UPP_MAX_FRAME_BYTES + 2, stdin)) break;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;

        VjVal *msg = upp_decode_line(line, n, err, sizeof err);
        if (!msg) { rc = upp_refuse(err); break; }

        const VjVal *typev = vj_get(msg, "type");
        const char *type = (typev && typev->type == VJ_STR && typev->s) ? typev->s : "";
        const VjVal *idv = vj_get(msg, "id");
        const char *id = (idv && idv->type == VJ_STR && idv->s && *idv->s) ? idv->s : NULL;

        if (strcmp(type, "hello") == 0) {
            UppBuf welcome;
            upp_buf_init(&welcome);
            if (upp_session_accept_hello(&session, msg, &welcome, err, sizeof err) != 0) {
                fprintf(stderr, "inim-server: upp hello refused: %s\n", err);
                upp_buf_free(&welcome);
                vj_free(msg);
                rc = UPP_EXIT_HELLO;
                break;
            }
            printf("%s\n", welcome.data ? welcome.data : "");
            fflush(stdout);
            upp_buf_free(&welcome);
        } else if (strcmp(type, "start") == 0 || strcmp(type, "stop") == 0 ||
                   strcmp(type, "heartbeat") == 0 || strcmp(type, "log") == 0 ||
                   strcmp(type, "crash") == 0) {
            /* Control frames carry no response: a reply would itself have to
             * be a control frame, and the reference defines none for these. */
            if (upp_session_apply(&session, msg, err, sizeof err) != 0) {
                char why[UPP_ERR_MAX + 32];
                snprintf(why, sizeof why, "frame '%s': %s", type, err);
                vj_free(msg);
                rc = upp_refuse(why);
                break;
            }
        } else if (strcmp(type, "verse") == 0) {
            if (session.state != UPP_STATE_RUNNING) {
                char why[UPP_ERR_MAX + 16];
                snprintf(why, sizeof why,
                         "'verse' frame while the session is %s, not running",
                         upp_state_name(session.state));
                vj_free(msg);
                rc = upp_refuse(why);
                break;
            }
            const VjVal *payload = vj_get(msg, "payload");
            if (!payload) {
                vj_free(msg);
                rc = upp_refuse("'verse' frame without a payload");
                break;
            }
            /* The Layer parser wants the request as JSON text, and the frame
             * carried it as JSON text; re-serializing here would be a second
             * writer with its own opinions about order and spacing. */
            UppBuf req;
            upp_buf_init(&req);
            if (upp_json_write(&req, payload) != 0) {
                upp_buf_free(&req);
                vj_free(msg);
                rc = upp_refuse("cannot serialize the 'verse' payload");
                break;
            }
            /* The plain transport reads a request with fgets() bounded by
             * VL_LINE_MAX, so a Layer request has a bound whether or not the
             * envelope has room for more.  Accepting a 400 KiB request here
             * only because this path can read it would make the envelope more
             * than a carrier, and the same scenario would answer differently
             * depending on which transport read it.  The refusal is named
             * because the plain path's is not: fgets() there truncates at the
             * reader and the Layer reports a parse error at an offset nobody
             * wrote (docs/STATUS.md #10.10 found the same shape for replies). */
            size_t reqlen = strlen(req.data ? req.data : "{}");
            if (reqlen >= VL_LINE_MAX) {
                char why[160];
                snprintf(why, sizeof why,
                         "'verse' payload is %zu bytes, past the Layer's %d-byte "
                         "request bound", reqlen, VL_LINE_MAX - 1);
                upp_buf_free(&req);
                vj_free(msg);
                rc = upp_refuse(why);
                break;
            }
            int stop = vl_server_handle(s, req.data, resp, VL_LINE_MAX);
            upp_buf_free(&req);

            UppBuf frame;
            upp_buf_init(&frame);
            if (upp_frame_build(&frame, "verse", id, resp, err, sizeof err) != 0) {
                upp_buf_free(&frame);
                vj_free(msg);
                rc = upp_refuse(err);
                break;
            }
            printf("%s\n", frame.data ? frame.data : "");
            fflush(stdout);
            upp_buf_free(&frame);
            if (stop) { vj_free(msg); break; }
        } else {
            char why[UPP_ERR_MAX + 64];
            snprintf(why, sizeof why, "unknown frame type '%s'", type);
            vj_free(msg);
            rc = upp_refuse(why);
            break;
        }
        vj_free(msg);
    }

    free(line);
    free(resp);
    return rc;
}

/* -------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    int upp_mode = 0;
    int argi = 1;
    if (argc > argi && strcmp(argv[argi], "--upp") == 0) { upp_mode = 1; argi++; }

    const char *root = (argc > argi) ? argv[argi] : ".";
    const char *verse_id = (argc > argi + 1) ? argv[argi + 1] : "main";

    if (vl_ensure_layer(root, verse_id) != 0) return 3;

    VlServer *s = vl_server_open(root, verse_id);
    if (!s) {
        fprintf(stderr, "inim-server: cannot open verse '%s' under %s\n", verse_id, root);
        return 3;
    }

    fprintf(stderr, "inim-server: verse '%s' protocol %d ready on stdin/stdout%s\n",
            verse_id, VL_PROTOCOL_VERSION, upp_mode ? " (UPP transport)" : "");

    int rc = upp_mode ? run_upp(s) : run_plain(s);

    /* A session that ends without an explicit bye still has to report whether
     * the Layer was left consistent. */
    if (!vl_server_anchor_ok(s)) {
        fprintf(stderr, "inim-server: anchor check failed on exit; recovery required\n");
        if (rc == 0) rc = 2;
    }

    vl_server_close(s);
    return rc;
}
