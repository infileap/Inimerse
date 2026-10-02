/* upp.h - engine-side UPP v1 (宇宙进程协议, Universe Process Protocol)
 *
 * The wire format and the session state machine were, until this file, only
 * present as a JS reference implementation (tools/upp_reference.js and
 * tools/upp_session.js).  "The tests pass" therefore proved that the reference
 * was self-consistent, not that the engine could speak UPP at all.  This
 * module is the engine-side counterpart:
 *
 *   - JSONL framing (one frame per line, newline-delimited, no length prefix),
 *     `{ upp: 1, type, id?, payload }`, 1 MiB cap measured in UTF-8 BYTES
 *   - the control frames (heartbeat/start/stop/log/crash/incompatible)
 *   - manifest validation and hello negotiation
 *   - the session state machine idle/running/stopped/crashed/incompatible
 *
 * Byte-for-byte compatibility with the reference is checked by
 * tools/upp_engine_crosscheck.js, which feeds one corpus to both sides and
 * diffs the transcripts (registered in CMake as verse_upp_crosscheck).
 *
 * Deliberate deviation: the engine reads JSON through src/verse/json_min.c,
 * which accepts integers only, so an UPP `timestamp` must be an integer number
 * of milliseconds.  The reference implementation would accept a float.  See
 * docs/streams/upp-in-engine.md §6 ("JSON 解析器只收整数").
 */
#ifndef INIMERSE_VERSE_UPP_H
#define INIMERSE_VERSE_UPP_H

#include <stddef.h>

#include "json_min.h"

#define UPP_VERSION                 1
#define UPP_MAX_FRAME_BYTES         (1024 * 1024)
#define UPP_HEARTBEAT_TIMEOUT_MS    15000
#define UPP_DECODER_MAX_PENDING     (UPP_MAX_FRAME_BYTES * 2)
#define UPP_ERR_MAX                 256
#define UPP_ID_MAX                  128
#define UPP_ABI_RANGE_MAX           32

/* STATES = { idle, running, stopped, crashed, incompatible } */
typedef enum {
    UPP_STATE_IDLE = 0,
    UPP_STATE_RUNNING,
    UPP_STATE_STOPPED,
    UPP_STATE_CRASHED,
    UPP_STATE_INCOMPATIBLE
} UppState;

const char *upp_state_name(UppState state);
int         upp_state_from_name(const char *name);

/* ------------------------------------------------------------------ buffers */

/* Growable text buffer.  Every builder writes into one; the caller owns it and
 * must call upp_buf_free. */
typedef struct {
    char  *data;   /* always NUL-terminated once something was written */
    size_t len;
    size_t cap;
    int    oom;
} UppBuf;

void upp_buf_init(UppBuf *b);
void upp_buf_free(UppBuf *b);
int  upp_buf_putc(UppBuf *b, int c);
int  upp_buf_puts(UppBuf *b, const char *s);
int  upp_buf_putn(UppBuf *b, const char *s, size_t n);
int  upp_buf_put_ll(UppBuf *b, long long value);

/* JSON serialization.  upp_json_write reproduces JSON.stringify for the subset
 * the wire format uses (no floats; non-ASCII passes through as UTF-8). */
int upp_json_write(UppBuf *b, const VjVal *v);
int upp_json_write_string(UppBuf *b, const char *s);

/* ----------------------------------------------------------------- framing */

/* `{ upp: 1, type, id?, payload }`.  `id` is dropped entirely when NULL or
 * empty -- never written as "".  `payload_json` is raw JSON text. */
int upp_frame_build(UppBuf *out, const char *type, const char *id,
                    const char *payload_json, char *err, size_t errlen);

/* encode(message): serialize + one trailing '\n', refusing > 1 MiB (UTF-8
 * bytes).  Returns -1 and fills err on refusal. */
int upp_encode(UppBuf *out, const VjVal *message, char *err, size_t errlen);
int upp_encode_text(UppBuf *out, const char *json, char *err, size_t errlen);

/* decodeLine(line): size check + parse + header check.  Caller frees. */
VjVal *upp_decode_line(const char *line, size_t len, char *err, size_t errlen);

/* createDecoder(onMessage): streaming line splitter.  `push` strips one
 * trailing '\r' per line and skips blank lines, exactly like the reference. */
typedef struct {
    char   *pending;
    size_t  len;
    size_t  cap;
    int     failed;
    char    error[UPP_ERR_MAX];
    void  (*on_message)(const VjVal *message, void *user);
    void   *user;
} UppDecoder;

void upp_decoder_init(UppDecoder *d, void (*on_message)(const VjVal *, void *), void *user);
void upp_decoder_free(UppDecoder *d);
/* Returns 0 on success, -1 when the pending buffer overflowed or a line was
 * malformed (d->failed is set and d->error explains). */
int  upp_decoder_push(UppDecoder *d, const char *chunk, size_t len);
int  upp_decoder_end(UppDecoder *d);

/* ------------------------------------------------------- control builders */

/* hello(role, manifest, capabilities): `capabilities` is the raw JSON array or
 * NULL for []; non-string elements are dropped and the rest are deduplicated
 * and sorted, as in the reference. */
int upp_frame_hello(UppBuf *out, const char *role, const VjVal *manifest,
                    const VjVal *capabilities, char *err, size_t errlen);
int upp_frame_control(UppBuf *out, const char *type, const char *payload_json,
                      const char *id, char *err, size_t errlen);
int upp_frame_heartbeat(UppBuf *out, long long seq, long long timestamp,
                        char *err, size_t errlen);
int upp_frame_start(UppBuf *out, const char *entry, const VjVal *args,
                    char *err, size_t errlen);
int upp_frame_stop(UppBuf *out, const char *reason, char *err, size_t errlen);
int upp_frame_log(UppBuf *out, const char *level, const char *message,
                  long long timestamp, char *err, size_t errlen);
int upp_frame_crash(UppBuf *out, const char *error, long long exit_code,
                    int has_exit_code, long long timestamp, char *err, size_t errlen);
int upp_frame_incompatible(UppBuf *out, long long required, long long actual,
                           char *err, size_t errlen);
int upp_frame_welcome(UppBuf *out, const char *peer_role, int has_peer_role,
                      long long abi, char *err, size_t errlen);

/* validateManifest.  Returns 0 when valid, -1 with the reference's exact
 * message in err otherwise. */
int upp_validate_manifest(const VjVal *manifest, char *err, size_t errlen);

/* ------------------------------------------------------------- state machine */

typedef struct {
    UppState  state;
    char      role[16];
    long long last_heartbeat;
    long long last_heartbeat_at;
    long long abi;              /* resolved ABI, 0 == not negotiated (JS null) */
    int       has_abi;
    int       error_set;
    char      error[UPP_ERR_MAX];
    /* local manifest facts acceptHello() needs to build the ABI range */
    long long local_abi;
    char      local_abi_range[UPP_ABI_RANGE_MAX];
    int       has_local_abi_range;
} UppSession;

/* new UppSession(role, manifest): refuses an unknown role with the
 * reference's message ("invalid role").  The manifest is NOT validated here;
 * acceptHello() only ever reads abi/abiRange. */
int  upp_session_init(UppSession *s, const char *role, const VjVal *manifest,
                      char *err, size_t errlen);

/* apply(message): heartbeat ordering, start idempotency, stop, crash.  Returns
 * 0 on success, -1 with the thrown message in err. */
int  upp_session_apply(UppSession *s, const VjVal *message, char *err, size_t errlen);

/* acceptHello(remoteHello): negotiate, or move to `incompatible` and record the
 * error before reporting it.  welcome_out receives the welcome frame. */
int  upp_session_accept_hello(UppSession *s, const VjVal *remote_hello,
                              UppBuf *welcome_out, char *err, size_t errlen);

int  upp_session_recover(UppSession *s, char *err, size_t errlen);
void upp_session_reset(UppSession *s);
int  upp_session_is_heartbeat_stale(const UppSession *s, long long now, long long timeout_ms);
/* Returns 0 when healthy, -1 (and crashed) on timeout -- mirrors the boolean. */
int  upp_session_check_heartbeat(UppSession *s, long long now, long long timeout_ms);

/* Wall clock in ms; only needed where the reference falls back to Date.now(). */
long long upp_now_ms(void);

/* True for values the reference would accept as Number.isSafeInteger. */
int upp_is_safe_int(long long value);

#endif /* INIMERSE_VERSE_UPP_H */
