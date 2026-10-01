/* layer.h - authoritative Layer: verse_id, manifest, state, replay (P1 increment 2)
 *
 * On-disk layout under <root>:
 *   verse/<verse_id>/manifest.json          canonical, signed-by-hash
 *   verse/<verse_id>/events.log             append-only hash-chained records
 *   verse/<verse_id>/snapshots/state.json   canonical state snapshot
 *
 * The client never supplies position/balance/sequence as final values: the
 * record's seq and rev are assigned by the server right before commit, and the
 * state is only ever advanced through vl_commit's step 7.
 */
#ifndef INIMERSE_VERSE_LAYER_H
#define INIMERSE_VERSE_LAYER_H

#include "eventlog.h"

#define VL_CELL_KEY_MAX 64
#define VL_MAX_CELLS    256
#define VL_MAX_HIST     512
#define VL_VERSE_ID_MAX 128

typedef struct {
    char verse_id[VL_VERSE_ID_MAX];
    char abi[32];
    int  schema_version;
    long long created_at;
} VlManifest;

typedef struct VlLayer VlLayer;

/* Create the Layer directory tree and write manifest.json.
 * Fails with VL_ERR_CONFLICT if the manifest already exists. */
VlStatus vl_layer_create(const char *root, const char *verse_id, VlManifest *out);

/* Open (never creates) an existing Layer: verifies the manifest hash chain
 * head and replays events.log into memory. */
VlLayer *vl_layer_open(const char *root, const char *verse_id);
void     vl_layer_close(VlLayer *l);

const VlManifest *vl_layer_manifest(const VlLayer *l);
const char       *vl_layer_dir(const VlLayer *l);
long              vl_layer_seq(const VlLayer *l);
const char       *vl_layer_head(const VlLayer *l);

/* Canonical rendering of the authoritative state, then its SHA-256. */
void vl_layer_state_canon(const VlLayer *l, VlCjson *out);
void vl_layer_state_hash(const VlLayer *l, char out[65]);

/* Commit an authoritative "put".  seq/rev are assigned here, not by the caller. */
VlStatus vl_layer_put(VlLayer *l, const char *idempotency_key,
                      const char *actor, const char *role,
                      const char *cell, long long value,
                      int steps[VL_COMMIT_STEPS]);

/* Commit an "undo" that restores the value a previous record overwrote.
 * Repeating the same idempotency key must not undo twice. */
VlStatus vl_layer_undo(VlLayer *l, const char *idempotency_key,
                       const char *actor, const char *role,
                       long long target_seq, int steps[VL_COMMIT_STEPS]);

/* apply_record() exposed for replay and for tests. */
VlStatus vl_layer_apply_record(VlLayer *l, const char *canon_json, long long seq);

/* Re-read events.log from disk and rebuild the state independently.
 * out_hash receives the resulting state hash; it must equal the live one.
 * Returns VL_ERR_RECOVERY_REQUIRED when the durable commit pointer does not
 * match the recomputed chain (a tampered or half-committed log). */
VlStatus vl_layer_replay(VlLayer *l, char out_hash[65], long *out_applied);

/* Compare the durable commit pointer (commit.head) with the chain recomputed
 * from events.log.  The pointer is written after every successful commit, so
 * a mismatch means the log cannot be trusted. */
VlStatus vl_layer_anchor_check(const VlLayer *l);

/* Explicit operator action: re-anchor the commit pointer to whatever the log
 * currently recomputes to.  Needed after a crash between the record flush and
 * the pointer write.  Deliberately NOT automatic. */
VlStatus vl_layer_repair(VlLayer *l);

VlStatus vl_layer_snapshot_save(VlLayer *l);
VlStatus vl_layer_snapshot_load(VlLayer *l);

/* ---- cell accessors (read-only view for callers) ---- */
long long vl_layer_cell_value(const VlLayer *l, const char *cell, int *found);
long long vl_layer_cell_rev(const VlLayer *l, const char *cell);
size_t    vl_layer_cell_count(const VlLayer *l);
const char *vl_layer_cell_at(const VlLayer *l, size_t i);
long long vl_layer_cell_value_at(const VlLayer *l, size_t i);

#endif /* INIMERSE_VERSE_LAYER_H */
