/* eventlog.h - minimal Layer event log and commit contract (P1 increment 1)
 *
 * Encodes the invariants required by the P1 "minimal Layer closed loop":
 *   - canonical JSON: deterministic key order, no insignificant whitespace
 *   - state_hash = SHA-256(canonical JSON)
 *   - append-only hash-chained log with an explicit durable flush step
 *   - a 10-step commit whose step-6 (durability) failure MUST NOT run steps 7-10,
 *     and whose step-7 (apply) failure MUST yield VL_ERR_RECOVERY_REQUIRED,
 *     never a "committed" result
 *   - client-supplied actor/role are untrusted (max_depth = 0)
 *   - idempotency keys: a repeated request must not append twice
 */
#ifndef INIMERSE_VERSE_EVENTLOG_H
#define INIMERSE_VERSE_EVENTLOG_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* canonical JSON writer                                               */
/* ------------------------------------------------------------------ */

#define VL_CJSON_MAX_DEPTH 16

typedef struct {
    char  *buf;
    size_t len, cap;
    int    depth;                        /* 0 = top level                    */
    int    is_obj[VL_CJSON_MAX_DEPTH];   /* container kind per level          */
    int    first[VL_CJSON_MAX_DEPTH];    /* nothing emitted in this container */
    int    after_key[VL_CJSON_MAX_DEPTH];/* last token was a key              */
    int    err;                          /* sticky: 1 = unusable              */
} VlCjson;

void        vl_cjson_init(VlCjson *j);
void        vl_cjson_free(VlCjson *j);
int         vl_cjson_ok(const VlCjson *j);
const char *vl_cjson_data(const VlCjson *j);   /* NULL when !ok, else NUL-terminated */

void vl_cjson_obj_begin(VlCjson *j);
void vl_cjson_obj_end(VlCjson *j);
void vl_cjson_arr_begin(VlCjson *j);
void vl_cjson_arr_end(VlCjson *j);
void vl_cjson_key(VlCjson *j, const char *k);   /* object members only */
void vl_cjson_str(VlCjson *j, const char *s);
void vl_cjson_int(VlCjson *j, int64_t v);
void vl_cjson_bool(VlCjson *j, int b);
void vl_cjson_null(VlCjson *j);

/* Sort a key array the way canonical JSON orders object members (byte order). */
void vl_cjson_sort_keys(char **keys, size_t n);

/* SHA-256 of already-canonical text, lowercase hex into out[65]. */
void vl_state_hash(const char *canonical_json, char out[65]);

/* ------------------------------------------------------------------ */
/* event log                                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    VL_OK                     =  0,
    VL_ERR_ARG                = -1,
    VL_ERR_IO                 = -2,
    VL_ERR_CONFLICT           = -3,  /* conditional_append: head mismatch        */
    VL_ERR_DURABILITY         = -4,  /* durable flush failed                     */
    VL_ERR_RECOVERY_REQUIRED  = -5,  /* state inconsistent, must not claim OK    */
    VL_ERR_REJECTED           = -6,  /* validation / authorization refused       */
    VL_ERR_NOT_FOUND          = -7
} VlStatus;

const char *vl_status_name(VlStatus s);

#define VL_COMMIT_STEPS 10

typedef struct VlEventLog VlEventLog;

/* Empty (or missing) log gets head = SHA-256 of the empty string. */
VlEventLog *vl_eventlog_open(const char *path);
void        vl_eventlog_close(VlEventLog *log);

size_t      vl_eventlog_count(const VlEventLog *log);
const char *vl_eventlog_head(const VlEventLog *log);      /* 64 lowercase hex */
VlStatus    vl_eventlog_last_status(const VlEventLog *log);

/* Append canon_json as one record. out_head (optional) receives the new head. */
VlStatus vl_eventlog_append(VlEventLog *log, const char *canon_json, char out_head[65]);

/* Append only if the current head equals expected_head (optimistic concurrency). */
VlStatus vl_eventlog_conditional_append(VlEventLog *log, const char *expected_head,
                                        const char *canon_json, char out_head[65]);

/* Durability barrier: flush and fsync the log file. */
VlStatus vl_eventlog_durable_flush(VlEventLog *log);

/* Recompute the chain from disk and compare with the in-memory head. */
VlStatus vl_eventlog_verify(VlEventLog *log);

/* Recompute from disk and compare with expected_head (from a snapshot/manifest).
 * A mismatch is VL_ERR_RECOVERY_REQUIRED, never VL_OK. */
VlStatus vl_eventlog_recover(VlEventLog *log, const char *expected_head);

/* ------------------------------------------------------------------ */
/* 10-step commit                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *idempotency_key;   /* required; "" is rejected              */
    const char *actor;             /* client-supplied, UNTRUSTED            */
    const char *role;              /* client-supplied, UNTRUSTED            */
    const char *canon_json;        /* record body, already canonical        */
    /* Optional step-7 hook: mutate the authoritative state.  Returning a
     * status other than VL_OK makes the commit RECOVERY_REQUIRED. */
    VlStatus  (*apply)(void *ctx, const char *canon_json);
    void       *apply_ctx;
} VlIntent;

/* steps[] must hold VL_COMMIT_STEPS ints; 1 = step executed.
 *
 *   1 validate intent        (rejects empty idempotency key, NULL body)
 *   2 idempotency lookup     (repeat -> VL_OK, no second append)
 *   3 authorize              (P1 stub: allow)
 *   4 stage record
 *   5 conditional append     (conflict -> VL_ERR_CONFLICT, steps 6-10 skipped)
 *   6 durable flush          (failure -> VL_ERR_DURABILITY, steps 7-10 skipped)
 *   7 apply to state         (failure -> VL_ERR_RECOVERY_REQUIRED, NOT committed)
 *   8 update head/snapshot
 *   9 record idempotency result
 *  10 return committed
 */
VlStatus vl_commit(VlEventLog *log, const VlIntent *in, int steps[VL_COMMIT_STEPS]);

/* Test hook: make the given 1-based commit step fail. 0 disables. */
void vl_eventlog_set_fault_step(VlEventLog *log, int step);

#endif /* INIMERSE_VERSE_EVENTLOG_H */
