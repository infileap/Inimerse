/* crp.h - engine-side CRP wire layer (能力中继协议, Capability Relay Protocol)
 *
 * The CRP *session* layer already exists in the engine
 * (src/platform/crp_session.{h,c}): 11 states, version/capability negotiation,
 * leases, `accept()` dedup/gap verdicts and `resume_plan()` replay-vs-snapshot.
 * This module does NOT reimplement any of that.  It adds the layer outside it,
 * which until now only existed as the read-only JS reference
 * (tools/crp_reference.js, tools/crp_relay.js):
 *
 *   - JSONL framing `{ crp: 1, type, payload, id? }` for FIND / PORTAL / SIGNAL,
 *     one frame per line, 1 MiB cap measured in UTF-8 BYTES
 *   - base64url (RFC 4648 §5, unpadded) + HMAC-SHA256 capability tokens
 *     (`<base64url(body)>.<base64url(sig)>`, body = {verse,peer,capabilities,exp})
 *   - the FIND registry (register / find / prune) and PORTAL issuance
 *   - the relay's /signal, /session/resume and /revoke semantics
 *
 * Byte-for-byte compatibility with the reference is checked by
 * tools/crp_engine_crosscheck.js, which feeds one corpus to both sides and
 * diffs the transcripts (registered in CMake as verse_crp_crosscheck).  The
 * two-process path is exercised by crp-hub / crp-peer and
 * tools/crp_closed_loop.test.py.
 *
 * ARGUMENT CONVENTION (load-bearing): a NULL `VjVal *` argument means the JS
 * value `undefined` (the key is absent), while a non-NULL `VJ_NULL` value means
 * JSON `null`.  The reference distinguishes them (default parameters and `??`
 * only fall back for undefined, `||` also falls back for null) so this module
 * must too: `data = {}` uses {} for absent but throws for `null`.
 *
 * Inherited limit: JSON is read through src/verse/json_min.c, which accepts
 * integers only (a float anywhere rejects the whole line with
 * "non-integer number unsupported") and cannot represent a string containing
 * U+0000 or a lone surrogate.  The JS reference would accept a float
 * `timestamp`/`limit`/`exp`.  Same deliberate deviation the UPP stream
 * documents; see docs/streams/crp-in-engine.md §6.
 */
#ifndef INIMERSE_VERSE_CRP_H
#define INIMERSE_VERSE_CRP_H

#include <stddef.h>

#include "json_min.h"

/* The growable buffer and the JSON.stringify subset already exist and are
 * shared with the UPP layer: upp_buf_init/free/puts/putn/putc/put_ll,
 * upp_json_write, upp_json_write_string, upp_is_safe_int, upp_now_ms. */
#include "upp.h"

#define CRP_VERSION                 1
#define CRP_MAX_FRAME_BYTES         (1024 * 1024)
#define CRP_ERR_MAX                 256
#define CRP_TOKEN_MAX               4096
#define CRP_EVENT_WINDOW            64
#define CRP_DEFAULT_TOKEN_TTL_MS    (5 * 60 * 1000)
#define CRP_DEFAULT_REGISTRY_TTL_MS (30 * 60 * 1000)
#define CRP_DEFAULT_MAX_REVOKED     10000

/* Same buffer type, same helpers; the alias exists so CRP code reads in its
 * own vocabulary without a second escaping implementation that could drift. */
typedef UppBuf CrpBuf;

/* --------------------------------------------------------------------- json */

/* JSON.stringify-style text for one value (no trailing newline). */
int crp_json_write(CrpBuf *out, const VjVal *v);

/* JS String(value) coercion, used only for error messages ("unsupported CRP
 * type: ${type}") and for map keys built as `${x}`.  Never NULL. */
const char *crp_js_string(const VjVal *v, char *scratch, size_t cap);

/* JS `a === b` for parsed values: strings compare by content, numbers by
 * value, null/undefined only to themselves, arrays/objects never (identity). */
int crp_strict_equal(const VjVal *a, const VjVal *b);

/* --------------------------------------------------------------- base64url */

/* RFC 4648 §5 alphabet (A-Za-z0-9-_), no padding, for arbitrary bytes. */
int crp_b64url_encode(const unsigned char *in, size_t n, CrpBuf *out);
/* Strict: rejects '=' padding, whitespace, and any byte outside the alphabet,
 * and a length of 4k+1.  *out is malloc'd (NULL when outlen == 0) and owned by
 * the caller.  Returns 0 on success, -1 on refusal. */
int crp_b64url_decode(const char *in, size_t n, unsigned char **out, size_t *outlen);

/* ----------------------------------------------------------- HMAC-SHA256 */

/* Standard HMAC (RFC 2104) with SHA-256, matching
 * crypto.createHmac('sha256', key).update(msg).digest(): a key longer than the
 * 64-byte block is hashed first, a shorter key is zero-padded. */
void crp_hmac_sha256(const void *key, size_t keylen, const void *msg, size_t msglen,
                     unsigned char out[32]);

/* ------------------------------------------------------ capability tokens */

/* makeToken(verse, peer, capabilities = ['signal']).
 * `capabilities` is the raw JSON array (may hold any JSON value, as in the
 * reference) or NULL for the default ['signal']; `exp_ms` is the absolute
 * expiry in ms.  Writes the token into `out`. */
int crp_token_make(const char *secret, const VjVal *verse, const VjVal *peer,
                   const VjVal *capabilities, long long exp_ms,
                   CrpBuf *out, char *err, size_t errlen);

/* Convenience form for C callers: string verse/peer and a NULL-terminated
 * array of capability strings (NULL derives ['signal']). */
int crp_token_make_str(const char *secret, const char *verse, const char *peer,
                       const char *const *capabilities, size_t ncapabilities,
                       long long exp_ms, CrpBuf *out, char *err, size_t errlen);

/* checkToken(token, verse, peer, capability): 1 when the signature verifies,
 * the body decodes, verse/peer match by JS `===`, exp > now_ms and the
 * capability list contains `capability`; 0 otherwise (never throws). */
int crp_token_check(const char *secret, const char *token,
                    const VjVal *verse, const VjVal *peer,
                    const char *capability, long long now_ms);

/* Same, with string verse/peer (compared as strings). */
int crp_token_check_str(const char *secret, const char *token,
                        const char *verse, const char *peer,
                        const char *capability, long long now_ms);

/* Engine-only introspection: decode the body of a token (signature NOT
 * checked).  Returns NULL on any refusal.  Caller frees with vj_free. */
VjVal *crp_token_body(const char *token, char *err, size_t errlen);

/* Enrollment proof for POST /portal, byte-identical to the reference relay's
 * enrollProof(): base64url(HMAC-SHA256(enroll_secret, String(verse) + "\0" +
 * String(peer))).  `verse`/`peer` are coerced exactly like JS String(), so a
 * missing member reads "undefined" and a number reads its decimal text.
 * Returns 0 and fills `out` on success; -1 when `enroll_secret` is unset/empty
 * or `out` is NULL.  The caller frees `out` with upp_buf_free. */
int crp_enroll_proof(const char *enroll_secret, const VjVal *verse,
                     const VjVal *peer, CrpBuf *out);

/* 1 when `auth` is the enrollment proof for this exact (verse, peer), 0
 * otherwise -- including when `enroll_secret` is unset/empty, `auth` is missing
 * or not a string, or the two differ in length.  This is the whole of the
 * /portal precondition, exported so every listener asks the same question the
 * same way instead of re-deriving the proof; the comparison is constant time.
 * `auth` is the request's `auth` member (NULL when absent). */
int crp_enroll_check(const char *enroll_secret, const VjVal *verse,
                     const VjVal *peer, const VjVal *auth);

/* -------------------------------------------------------------- framing */

int crp_type_is_valid(const char *type);

/* frame(type, payload = {}, id = ''): `payload` must be an object (NULL means
 * the {} default) and `id` is written last, and only when non-empty. */
int crp_frame(CrpBuf *out, const VjVal *type, const VjVal *payload,
              const char *id, char *err, size_t errlen);

/* find(query = '', {limit, cursor, id} = {}) */
int crp_frame_find(CrpBuf *out, const VjVal *query, const VjVal *limit,
                   const VjVal *cursor, const char *id, char *err, size_t errlen);

/* portal(verse, peer, {token, expires, id} = {}) */
int crp_frame_portal(CrpBuf *out, const VjVal *verse, const VjVal *peer,
                     const VjVal *token, const VjVal *expires, const char *id,
                     char *err, size_t errlen);

/* signal(verse, event, data = {}, {timestamp, id} = {}) */
int crp_frame_signal(CrpBuf *out, const VjVal *verse, const VjVal *event,
                     const VjVal *data, const VjVal *timestamp, const char *id,
                     char *err, size_t errlen);

/* encode(message): serialize + one trailing '\n', refusing > 1 MiB (bytes). */
int crp_encode(CrpBuf *out, const VjVal *message, char *err, size_t errlen);

/* decodeLine(line): size check + parse + `{crp:1, type}` header check.
 * `len` may be (size_t)-1 to mean strlen(line).  Caller frees with vj_free. */
VjVal *crp_decode_line(const char *line, size_t len, char *err, size_t errlen);

/* ----------------------------------------------------------------- registry */

/* HTTP-shaped result: a status code and a JSON body (no trailing newline).
 * 0 status means "no response" (only used internally, never returned). */
typedef struct {
    int    status;
    CrpBuf body;
} CrpResult;

void crp_result_free(CrpResult *r);

typedef struct {
    const char *secret;          /* required; its UTF-8 bytes are the HMAC key */
    /* Optional; enables POST /portal.  The pre-shared enrollment secret a
     * caller must prove possession of before a capability token is minted for
     * (verse, peer).  Deliberately distinct from `secret` so that leaking the
     * token key does not also grant portal enrollment.  NULL or empty means
     * fail-closed: every /portal request is refused (403), never opened. */
    const char *enroll_secret;
    long long   token_ttl_ms;    /* <= 0: CRP_DEFAULT_TOKEN_TTL_MS   */
    long long   registry_ttl_ms; /* <= 0: CRP_DEFAULT_REGISTRY_TTL_MS */
    long long   max_revoked;     /* <= 0: CRP_DEFAULT_MAX_REVOKED     */
    long long   now_ms;          /* <= 0: wall clock, otherwise frozen */
} CrpRegistryConfig;

typedef struct CrpRegistry CrpRegistry;

CrpRegistry *crp_registry_new(const CrpRegistryConfig *cfg);
void         crp_registry_free(CrpRegistry *r);

/* Test hook: pin the clock the frame builders read where the reference calls
 * Date.now() (the SIGNAL frame's default timestamp).  <= 0 restores the wall
 * clock; the default is the wall clock. */
void      crp_set_now(long long now_ms);
long long crp_now(void);

/* Test hook: pin/advance the registry clock (<= 0 returns to the wall clock).
 * Every Date.now() in the reference relay maps to this one reading. */
void      crp_registry_set_now(CrpRegistry *r, long long now_ms);
long long crp_registry_now(const CrpRegistry *r);
const char *crp_registry_secret(const CrpRegistry *r);

/* POST /register */
CrpResult crp_registry_register(CrpRegistry *r, const VjVal *p);
/* GET /find?q= */
CrpResult crp_registry_find(CrpRegistry *r, const char *q);
/* POST /portal - the response body is {token,verse,peer,expires}.
 * `auth` is the caller's enrollment proof; see crp_enroll_proof.  A request is
 * refused (403) before the registry is looked at when no enrollment secret is
 * configured, or when `auth` is absent, not a string, or does not match. */
CrpResult crp_registry_portal(CrpRegistry *r, const VjVal *verse, const VjVal *peer,
                              const VjVal *auth);
/* POST /signal.  verse/event/data usually come from a decoded SIGNAL frame;
 * token/peer/seq are the HTTP-shaped fields. */
CrpResult crp_registry_signal(CrpRegistry *r, const VjVal *verse, const VjVal *event,
                              const VjVal *data, const VjVal *token,
                              const VjVal *peer, const VjVal *seq);
/* POST /session/resume */
CrpResult crp_registry_resume(CrpRegistry *r, const VjVal *verse, const VjVal *peer,
                              const VjVal *token, const VjVal *seq,
                              const VjVal *replay);
/* POST /revoke */
CrpResult crp_registry_revoke(CrpRegistry *r, const VjVal *token);

/* Engine-only introspection (the JS reference has no /status).  It reports the
 * ImCrpSession state/lease that the existing platform session layer keeps for
 * each (verse, peer) pair. */
void      crp_registry_status_json(const CrpRegistry *r, CrpBuf *out);
int       crp_registry_verse_count(const CrpRegistry *r);
int       crp_registry_session_count(const CrpRegistry *r);
long long crp_registry_revoked_count(const CrpRegistry *r);

#endif /* INIMERSE_VERSE_CRP_H */
