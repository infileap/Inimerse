/* protocol.c - Layer session protocol (P1 increment 3) */

#include "protocol.h"
#include "json_min.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const VL_SERVER_CAPS[] = { "drain", "put", "status", "undo" };
const size_t      VL_SERVER_CAP_COUNT = sizeof VL_SERVER_CAPS / sizeof VL_SERVER_CAPS[0];

struct VlServer {
    VlLayer *layer;
    int      negotiated;      /* 0 until a successful hello */
    int      closed;
    long long rejected;       /* requests refused for authority reasons */
};

/* ------------------------------------------------------------------ */
/* request parsing                                                     */
/* ------------------------------------------------------------------ */

/* Fields a client must never use to assert final state.  The server assigns
 * all of these; a request carrying one is refused, not quietly ignored. */
static const char *const VL_AUTHORITY_FIELDS[] = {
    "seq", "rev", "head", "balance", "state_hash", "committed"
};

void vl_request_init(VlRequest *r) { memset(r, 0, sizeof *r); }

int vl_request_parse(const char *line, VlRequest *out) {
    vl_request_init(out);
    VjVal *v = vj_parse(line, out->parse_error, sizeof out->parse_error);
    if (!v) {
        out->kind = VL_CMD_BAD;
        if (out->parse_error[0] == '\0') snprintf(out->parse_error, sizeof out->parse_error, "malformed json");
        return -1;
    }
    if (v->type != VJ_OBJ) {
        vj_free(v);
        out->kind = VL_CMD_BAD;
        snprintf(out->parse_error, sizeof out->parse_error, "request must be an object");
        return -1;
    }

    const char *op = vj_str(vj_get(v, "op"), NULL);
    if (!op) {
        vj_free(v);
        out->kind = VL_CMD_BAD;
        snprintf(out->parse_error, sizeof out->parse_error, "missing op");
        return -1;
    }

    for (size_t i = 0; i < sizeof VL_AUTHORITY_FIELDS / sizeof VL_AUTHORITY_FIELDS[0]; i++) {
        if (vj_get(v, VL_AUTHORITY_FIELDS[i])) {
            out->has_client_authority = 1;
            snprintf(out->authority_field, sizeof out->authority_field, "%s", VL_AUTHORITY_FIELDS[i]);
            break;
        }
    }

    if (strcmp(op, "hello") == 0) {
        out->kind = VL_CMD_HELLO;
        out->protocol = (int)vj_int(vj_get(v, "protocol"), 0);
        snprintf(out->client, sizeof out->client, "%s", vj_str(vj_get(v, "client"), "anonymous"));
        const VjVal *caps = vj_get(v, "caps");
        if (caps && caps->type == VJ_ARR) {
            for (size_t i = 0; i < caps->n && out->ncaps < VL_MAX_CAPS; i++) {
                const char *c = vj_str(caps->items[i], NULL);
                if (!c || !*c) continue;
                snprintf(out->caps[out->ncaps], VL_CAP_LEN, "%s", c);
                out->ncaps++;
            }
        }
    } else if (strcmp(op, "put") == 0) {
        out->kind = VL_CMD_PUT;
        snprintf(out->key, sizeof out->key, "%s", vj_str(vj_get(v, "key"), ""));
        snprintf(out->cell, sizeof out->cell, "%s", vj_str(vj_get(v, "cell"), ""));
        out->value = vj_int(vj_get(v, "value"), 0);
    } else if (strcmp(op, "undo") == 0) {
        out->kind = VL_CMD_UNDO;
        snprintf(out->key, sizeof out->key, "%s", vj_str(vj_get(v, "key"), ""));
        out->target = vj_int(vj_get(v, "target"), -1);
    } else if (strcmp(op, "drain") == 0) {
        out->kind = VL_CMD_DRAIN;
    } else if (strcmp(op, "status") == 0) {
        out->kind = VL_CMD_STATUS;
    } else if (strcmp(op, "bye") == 0) {
        out->kind = VL_CMD_BYE;
    } else {
        out->kind = VL_CMD_BAD;
        snprintf(out->parse_error, sizeof out->parse_error, "unknown op '%s'", op);
        vj_free(v);
        return -1;
    }

    vj_free(v);
    return 0;
}

/* ------------------------------------------------------------------ */
/* response helpers                                                    */
/* ------------------------------------------------------------------ */

static int vl_emit(char *out, size_t cap, VlCjson *j) {
    if (!vl_cjson_ok(j) || cap == 0) { vl_cjson_free(j); return -1; }
    snprintf(out, cap, "%s", vl_cjson_data(j));
    vl_cjson_free(j);
    return 0;
}

static int vl_respond_error(char *out, size_t cap, const char *code, const char *msg) {
    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "code");  vl_cjson_str(&j, code);
    vl_cjson_key(&j, "error"); vl_cjson_str(&j, msg);
    vl_cjson_key(&j, "ok");    vl_cjson_bool(&j, 0);
    vl_cjson_obj_end(&j);
    return vl_emit(out, cap, &j);
}

static int vl_respond_ok_seq(char *out, size_t cap, VlServer *s, long long seq) {
    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "head"); vl_cjson_str(&j, vl_layer_head(s->layer));
    vl_cjson_key(&j, "ok");   vl_cjson_bool(&j, 1);
    vl_cjson_key(&j, "seq");  vl_cjson_int(&j, seq);
    vl_cjson_obj_end(&j);
    return vl_emit(out, cap, &j);
}

static const char *vl_status_code(VlStatus st) {
    switch (st) {
    case VL_OK:                    return "ok";
    case VL_ERR_ARG:               return "bad_request";
    case VL_ERR_IO:                return "io";
    case VL_ERR_CONFLICT:          return "conflict";
    case VL_ERR_DURABILITY:        return "durability_failed";
    case VL_ERR_RECOVERY_REQUIRED: return "recovery_required";
    case VL_ERR_REJECTED:          return "rejected";
    case VL_ERR_NOT_FOUND:         return "not_found";
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* session                                                             */
/* ------------------------------------------------------------------ */

VlServer *vl_server_open(const char *root, const char *verse_id) {
    if (!root || !verse_id) return NULL;
    VlLayer *l = vl_layer_open(root, verse_id);
    if (!l) return NULL;
    VlServer *s = (VlServer *)calloc(1, sizeof *s);
    if (!s) { vl_layer_close(l); return NULL; }
    s->layer = l;
    return s;
}

void vl_server_close(VlServer *s) {
    if (!s) return;
    vl_layer_close(s->layer);
    free(s);
}

const char *vl_server_verse_id(const VlServer *s) {
    return s ? vl_layer_manifest(s->layer)->verse_id : NULL;
}

int vl_server_negotiated(const VlServer *s) { return s ? s->negotiated : 0; }

int vl_server_anchor_ok(const VlServer *s) {
    return (s && vl_layer_anchor_check(s->layer) == VL_OK) ? 1 : 0;
}

size_t vl_server_agreed_caps(const VlServer *s, const char *out[VL_MAX_CAPS]) {
    (void)s;
    for (size_t i = 0; i < VL_SERVER_CAP_COUNT && i < VL_MAX_CAPS; i++) out[i] = VL_SERVER_CAPS[i];
    return VL_SERVER_CAP_COUNT;
}

static int vl_cap_supported(const char *cap) {
    for (size_t i = 0; i < VL_SERVER_CAP_COUNT; i++)
        if (strcmp(VL_SERVER_CAPS[i], cap) == 0) return 1;
    return 0;
}

static int vl_handle_hello(VlServer *s, const VlRequest *r, char *out, size_t cap) {
    if (r->protocol != VL_PROTOCOL_VERSION) {
        char msg[96];
        snprintf(msg, sizeof msg, "unsupported protocol version %d (server speaks %d)",
                 r->protocol, VL_PROTOCOL_VERSION);
        return vl_respond_error(out, cap, "protocol_mismatch", msg);
    }
    /* A Layer whose durable pointer does not match its log must not accept a
     * session: handing out a sequence number would let a client commit on top
     * of state nobody can vouch for. */
    if (!vl_server_anchor_ok(s)) {
        return vl_respond_error(out, cap, "recovery_required",
                                "the durable commit pointer does not match the log; "
                                "run recovery before opening a session");
    }
    /* A capability the server does not implement is an explicit refusal: the
     * session either gets what it asked for, or it is told why not. */
    for (size_t i = 0; i < r->ncaps; i++) {
        if (!vl_cap_supported(r->caps[i])) {
            char msg[128];
            snprintf(msg, sizeof msg, "unsupported capability '%s'", r->caps[i]);
            return vl_respond_error(out, cap, "capability_refused", msg);
        }
    }

    s->negotiated = 1;

    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "abi");      vl_cjson_str(&j, vl_layer_manifest(s->layer)->abi);
    vl_cjson_key(&j, "caps");     vl_cjson_arr_begin(&j);
    for (size_t i = 0; i < VL_SERVER_CAP_COUNT; i++) vl_cjson_str(&j, VL_SERVER_CAPS[i]);
    vl_cjson_arr_end(&j);
    vl_cjson_key(&j, "head");     vl_cjson_str(&j, vl_layer_head(s->layer));
    vl_cjson_key(&j, "ok");       vl_cjson_bool(&j, 1);
    vl_cjson_key(&j, "protocol"); vl_cjson_int(&j, VL_PROTOCOL_VERSION);
    vl_cjson_key(&j, "seq");      vl_cjson_int(&j, vl_layer_seq(s->layer));
    vl_cjson_key(&j, "verse_id"); vl_cjson_str(&j, vl_layer_manifest(s->layer)->verse_id);
    vl_cjson_obj_end(&j);
    return vl_emit(out, cap, &j);
}

/* Returns 0 when the session is ready, 1 after writing a refusal response. */
static int vl_require_session(VlServer *s, char *out, size_t cap) {
    if (s->negotiated) return 0;
    vl_respond_error(out, cap, "no_session", "hello must succeed before any other op");
    return 1;
}

int vl_server_drain(VlServer *s, char *out, size_t outcap) {
    if (!s) return -1;

    VlStatus anchor = vl_layer_anchor_check(s->layer);
    VlStatus snapst = VL_OK;
    if (anchor == VL_OK) snapst = vl_layer_snapshot_save(s->layer);

    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "drained"); vl_cjson_bool(&j, anchor == VL_OK && snapst == VL_OK);
    vl_cjson_key(&j, "head");    vl_cjson_str(&j, vl_layer_head(s->layer));
    vl_cjson_key(&j, "ok");      vl_cjson_bool(&j, 1);
    vl_cjson_key(&j, "pending"); vl_cjson_int(&j, 0);
    vl_cjson_key(&j, "seq");     vl_cjson_int(&j, vl_layer_seq(s->layer));
    if (anchor != VL_OK || snapst != VL_OK) {
        vl_cjson_key(&j, "code");
        vl_cjson_str(&j, vl_status_code(anchor != VL_OK ? anchor : snapst));
    }
    vl_cjson_obj_end(&j);
    return vl_emit(out, outcap, &j);
}

int vl_server_handle(VlServer *s, const char *line, char *out, size_t outcap) {
    if (!s || !line || !out) return 1;
    out[0] = '\0';

    VlRequest r;
    if (vl_request_parse(line, &r) != 0) {
        return vl_respond_error(out, outcap, "malformed", r.parse_error), 0;
    }

    /* Structural rule: the client cannot assert final state.  Any authority
     * field makes the request invalid regardless of op. */
    if (r.has_client_authority && r.kind != VL_CMD_STATUS) {
        s->rejected++;
        char msg[96];
        snprintf(msg, sizeof msg, "client must not supply '%s'; the server assigns it",
                 r.authority_field);
        return vl_respond_error(out, outcap, "client_authority", msg), 0;
    }

    switch (r.kind) {
    case VL_CMD_HELLO:
        return vl_handle_hello(s, &r, out, outcap), 0;

    case VL_CMD_PUT: {
        if (vl_require_session(s, out, outcap)) return 0;
        if (!r.key[0]) return vl_respond_error(out, outcap, "bad_request", "put requires a key"), 0;
        if (!r.cell[0]) return vl_respond_error(out, outcap, "bad_request", "put requires a cell"), 0;
        long long before = vl_layer_seq(s->layer);
        VlStatus st = vl_layer_put(s->layer, r.key, r.client, "client", r.cell, r.value, NULL);
        if (st != VL_OK) return vl_respond_error(out, outcap, vl_status_code(st), vl_status_name(st)), 0;
        long long seq = vl_layer_seq(s->layer);
        /* an idempotent retry returns the original sequence, not a new one */
        return vl_respond_ok_seq(out, outcap, s, seq <= before ? before : seq), 0;
    }

    case VL_CMD_UNDO: {
        if (vl_require_session(s, out, outcap)) return 0;
        if (!r.key[0]) return vl_respond_error(out, outcap, "bad_request", "undo requires a key"), 0;
        if (r.target < 0) return vl_respond_error(out, outcap, "bad_request", "undo requires target"), 0;
        long long before = vl_layer_seq(s->layer);
        VlStatus st = vl_layer_undo(s->layer, r.key, r.client, "client", r.target, NULL);
        if (st != VL_OK) return vl_respond_error(out, outcap, vl_status_code(st), vl_status_name(st)), 0;
        long long seq = vl_layer_seq(s->layer);
        return vl_respond_ok_seq(out, outcap, s, seq <= before ? before : seq), 0;
    }

    case VL_CMD_DRAIN:
        /* Deliberately reachable without a session: drain is the operation an
         * operator needs exactly when a session cannot be opened (an anchor
         * mismatch blocks hello), and it mutates no application state.  Every
         * other op stays behind the handshake. */
        return vl_server_drain(s, out, outcap), 0;

    case VL_CMD_STATUS: {
        /* Reading authoritative state out of an unanchored log would present
         * tampered values as if they were committed.  Refuse instead. */
        if (!vl_server_anchor_ok(s)) {
            return vl_respond_error(out, outcap, "recovery_required",
                                    "the durable commit pointer does not match the log"), 0;
        }
        VlCjson j;
        vl_cjson_init(&j);
        vl_cjson_obj_begin(&j);
        vl_cjson_key(&j, "cells"); vl_cjson_arr_begin(&j);
        size_t n = vl_layer_cell_count(s->layer);
        for (size_t i = 0; i < n; i++) {
            vl_cjson_obj_begin(&j);
            vl_cjson_key(&j, "cell");  vl_cjson_str(&j, vl_layer_cell_at(s->layer, i));
            vl_cjson_key(&j, "value"); vl_cjson_int(&j, vl_layer_cell_value_at(s->layer, i));
            vl_cjson_obj_end(&j);
        }
        vl_cjson_arr_end(&j);
        vl_cjson_key(&j, "head");     vl_cjson_str(&j, vl_layer_head(s->layer));
        vl_cjson_key(&j, "ok");       vl_cjson_bool(&j, 1);
        vl_cjson_key(&j, "rejected"); vl_cjson_int(&j, s->rejected);
        vl_cjson_key(&j, "seq");      vl_cjson_int(&j, vl_layer_seq(s->layer));
        vl_cjson_key(&j, "verse_id"); vl_cjson_str(&j, vl_layer_manifest(s->layer)->verse_id);
        vl_cjson_obj_end(&j);
        return vl_emit(out, outcap, &j), 0;
    }

    case VL_CMD_BYE: {
        char tmp[1024];
        vl_server_drain(s, tmp, sizeof tmp);
        s->closed = 1;
        /* the client is told the outcome of the closing drain */
        snprintf(out, outcap, "%s", tmp);
        return 1;
    }

    default:
        return vl_respond_error(out, outcap, "unknown_op", "unrecognised op"), 0;
    }
}
