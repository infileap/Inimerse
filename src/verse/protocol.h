/* protocol.h - Layer session protocol (P1 increment 3)
 *
 * One canonical-JSON object per line in each direction.  The session logic
 * lives here (not in main) so it can be tested in-process, while
 * src/verse/server.c only pumps lines between stdin/stdout and this API.
 */
#ifndef INIMERSE_VERSE_PROTOCOL_H
#define INIMERSE_VERSE_PROTOCOL_H

#include "layer.h"

#define VL_PROTOCOL_VERSION 1
#define VL_KEY_LEN          128
#define VL_CAP_LEN          32
#define VL_MAX_CAPS         8

/* Capabilities this server implements.  A client that requires one of these
 * is refused explicitly; it is never silently downgraded. */
extern const char *const VL_SERVER_CAPS[];
extern const size_t      VL_SERVER_CAP_COUNT;

typedef enum {
    VL_CMD_BAD = 0,
    VL_CMD_HELLO,
    VL_CMD_PUT,
    VL_CMD_UNDO,
    VL_CMD_DRAIN,
    VL_CMD_STATUS,
    VL_CMD_BYE
} VlCmdKind;

typedef struct {
    VlCmdKind kind;
    int       protocol;
    char      client[64];
    char      caps[VL_MAX_CAPS][VL_CAP_LEN];
    size_t    ncaps;
    char      key[VL_KEY_LEN];
    char      cell[VL_CELL_KEY_MAX];
    long long value;
    long long target;
    /* authority fields a client must never supply */
    int       has_client_authority;
    char      authority_field[32];
    char      parse_error[128];
} VlRequest;

/* Parse one request line.  Returns 0 on success, -1 on a malformed line
 * (out->parse_error explains). */
int vl_request_parse(const char *line, VlRequest *out);

typedef struct VlServer VlServer;

VlServer *vl_server_open(const char *root, const char *verse_id);
void      vl_server_close(VlServer *s);

const char *vl_server_verse_id(const VlServer *s);
int         vl_server_negotiated(const VlServer *s);
size_t      vl_server_agreed_caps(const VlServer *s, const char *out[VL_MAX_CAPS]);

/* 1 when the durable commit pointer still matches the chain recomputed from the
 * on-disk log.  Checked on exit so a session cannot end in a half-committed
 * state without saying so. */
int vl_server_anchor_ok(const VlServer *s);

/* Handle one line; writes the response line (no trailing newline) into out.
 * Returns 0 to continue, 1 when the session must end (bye). */
int vl_server_handle(VlServer *s, const char *line, char *out, size_t outcap);

/* Durability barrier: flush the log, write a snapshot, re-verify the anchor.
 * Reports drained=false with a code when anything is inconsistent. */
int vl_server_drain(VlServer *s, char *out, size_t outcap);

#endif /* INIMERSE_VERSE_PROTOCOL_H */
