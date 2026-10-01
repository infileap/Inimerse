/* layer_probe.c - P1 increment 2 regression probe
 *
 * Exercises the authoritative Layer: creation, commit, idempotency, undo,
 * snapshot, replay determinism and tamper detection.
 */
#include "layer.h"
#include "json_min.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "verse_layer_probe_root"

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static void clean_root(void) {
    remove(ROOT "/verse/main/manifest.json");
    remove(ROOT "/verse/main/events.log");
    remove(ROOT "/verse/main/commit.head");
    remove(ROOT "/verse/main/snapshots/state.json");
}

static long long log_lines(void) {
    FILE *fp = fopen(ROOT "/verse/main/events.log", "rb");
    if (!fp) return -1;
    long n = 0;
    int c, prev = '\n';
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') n++;
        prev = c;
    }
    (void)prev;
    fclose(fp);
    return n;
}

/* ------------------------------------------------------------------ */

static void test_create_and_open(void) {
    clean_root();
    VlManifest m;
    CHECK(vl_layer_create(ROOT, "main", &m) == VL_OK);
    CHECK(strcmp(m.verse_id, "main") == 0);
    CHECK(strcmp(m.abi, "infiverse.mv1/abi/1.0") == 0);
    CHECK(m.schema_version == 1);

    /* creating again must not silently overwrite an existing Layer */
    VlManifest m2;
    CHECK(vl_layer_create(ROOT, "main", &m2) == VL_ERR_CONFLICT);

    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;
    CHECK(vl_layer_seq(l) == 0);
    CHECK(vl_layer_cell_count(l) == 0);
    const VlManifest *lm = vl_layer_manifest(l);
    CHECK(lm && strcmp(lm->verse_id, "main") == 0);
    vl_layer_close(l);

    /* an unknown verse is not a Layer */
    CHECK(vl_layer_open(ROOT, "does-not-exist") == NULL);
}

static void test_commit_assigns_seq(void) {
    clean_root();
    vl_layer_create(ROOT, "main", NULL);
    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;

    int steps[VL_COMMIT_STEPS];
    CHECK(vl_layer_put(l, "k1", "alice", "builder", "0,0,0", 7, steps) == VL_OK);
    CHECK(steps[9] == 1);
    CHECK(vl_layer_seq(l) == 1);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 7);
    CHECK(vl_layer_cell_rev(l, "0,0,0") == 1);

    CHECK(vl_layer_put(l, "k2", "alice", "builder", "0,0,0", 9, steps) == VL_OK);
    CHECK(vl_layer_seq(l) == 2);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 9);
    CHECK(vl_layer_cell_rev(l, "0,0,0") == 2);

    CHECK(vl_layer_put(l, "k3", "bob", "viewer", "1,0,0", 3, steps) == VL_OK);
    CHECK(vl_layer_seq(l) == 3);
    CHECK(vl_layer_cell_count(l) == 2);

    /* the server, not the caller, decided every seq/rev above */
    char head1[65];
    memcpy(head1, vl_layer_head(l), 65);
    CHECK(strlen(head1) == 64);
    vl_layer_close(l);
}

static void test_idempotent_retry(void) {
    clean_root();
    vl_layer_create(ROOT, "main", NULL);
    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;

    int steps[VL_COMMIT_STEPS];
    CHECK(vl_layer_put(l, "same", "alice", "builder", "0,0,0", 5, steps) == VL_OK);
    CHECK(vl_layer_seq(l) == 1);
    long long lines = log_lines();

    /* a retried request must not append or re-apply */
    CHECK(vl_layer_put(l, "same", "alice", "builder", "0,0,0", 5, steps) == VL_OK);
    CHECK(vl_layer_seq(l) == 1);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 5);
    CHECK(log_lines() == lines);

    /* a different key with the same payload is a genuinely new record */
    CHECK(vl_layer_put(l, "other", "alice", "builder", "0,0,0", 5, steps) == VL_OK);
    CHECK(vl_layer_seq(l) == 2);
    CHECK(log_lines() == lines + 1);
    vl_layer_close(l);
}

static void test_undo(void) {
    clean_root();
    vl_layer_create(ROOT, "main", NULL);
    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;

    int steps[VL_COMMIT_STEPS];
    CHECK(vl_layer_put(l, "p1", "alice", "builder", "0,0,0", 7, steps) == VL_OK);
    CHECK(vl_layer_put(l, "p2", "alice", "builder", "0,0,0", 9, steps) == VL_OK);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 9);

    /* undo record 2 restores the value record 1 left behind */
    CHECK(vl_layer_undo(l, "u2", "alice", "builder", 2, steps) == VL_OK);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 7);
    CHECK(vl_layer_seq(l) == 3);

    /* repeating the same undo key must not undo twice */
    CHECK(vl_layer_undo(l, "u2", "alice", "builder", 2, steps) == VL_OK);
    CHECK(vl_layer_seq(l) == 3);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 7);

    /* an unknown target is refused, and refuses BEFORE writing anything */
    long long lines = log_lines();
    CHECK(vl_layer_undo(l, "u-bad", "alice", "builder", 999, steps) == VL_ERR_NOT_FOUND);
    CHECK(vl_layer_seq(l) == 3);
    CHECK(log_lines() == lines);
    vl_layer_close(l);
}

static void test_replay_and_reopen(void) {
    clean_root();
    vl_layer_create(ROOT, "main", NULL);
    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;

    int steps[VL_COMMIT_STEPS];
    CHECK(vl_layer_put(l, "a", "alice", "builder", "0,0,0", 1, steps) == VL_OK);
    CHECK(vl_layer_put(l, "b", "alice", "builder", "1,0,0", 2, steps) == VL_OK);
    CHECK(vl_layer_put(l, "c", "alice", "builder", "0,0,0", 3, steps) == VL_OK);
    CHECK(vl_layer_undo(l, "d", "alice", "builder", 3, steps) == VL_OK);

    char live[65];
    vl_layer_state_hash(l, live);

    /* deterministic replay reproduces the identical state hash */
    char replayed[65];
    long applied = 0;
    CHECK(vl_layer_replay(l, replayed, &applied) == VL_OK);
    CHECK(strcmp(live, replayed) == 0);
    CHECK(applied == 4);

    char head[65];
    memcpy(head, vl_layer_head(l), 65);
    vl_layer_close(l);

    /* a fresh open recovers the same state and head from disk alone */
    l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    char again[65];
    vl_layer_state_hash(l, again);
    CHECK(strcmp(live, again) == 0);
    CHECK(strcmp(vl_layer_head(l), head) == 0);
    CHECK(vl_layer_seq(l) == 4);
    /* undo of record 3 restored the value record 2 left behind, which is 1 */
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 1);

    /* replay is a pure function: same input, same hash, twice */
    CHECK(vl_layer_replay(l, replayed, &applied) == VL_OK);
    CHECK(strcmp(live, replayed) == 0);
    vl_layer_close(l);
}

static void test_snapshot(void) {
    clean_root();
    vl_layer_create(ROOT, "main", NULL);
    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;

    int steps[VL_COMMIT_STEPS];
    CHECK(vl_layer_put(l, "s1", "alice", "builder", "0,0,0", 11, steps) == VL_OK);
    CHECK(vl_layer_put(l, "s2", "alice", "builder", "2,0,0", 22, steps) == VL_OK);
    char snap_hash[65];
    vl_layer_state_hash(l, snap_hash);
    CHECK(vl_layer_snapshot_save(l) == VL_OK);
    char snap_head[65];
    memcpy(snap_head, vl_layer_head(l), 65);

    /* more traffic after the snapshot */
    CHECK(vl_layer_put(l, "s3", "alice", "builder", "0,0,0", 99, steps) == VL_OK);
    char moved[65], head_after_s3[65];
    vl_layer_state_hash(l, moved);
    memcpy(head_after_s3, vl_layer_head(l), 65);
    CHECK(strcmp(moved, snap_hash) != 0);

    CHECK(vl_layer_snapshot_load(l) == VL_OK);
    char restored[65];
    vl_layer_state_hash(l, restored);
    CHECK(strcmp(restored, snap_hash) == 0);
    CHECK(vl_layer_cell_value(l, "0,0,0", NULL) == 11);
    CHECK(vl_layer_cell_count(l) == 2);

    /* a snapshot restores state, it does not rewind the log: the head still
     * reflects every committed record, including s3. */
    CHECK(strcmp(vl_layer_head(l), snap_head) != 0);
    CHECK(strcmp(vl_layer_head(l), head_after_s3) == 0);
    vl_layer_close(l);
}

static void test_tamper_is_recovery_required(void) {
    clean_root();
    vl_layer_create(ROOT, "main", NULL);
    VlLayer *l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (!l) return;
    int steps[VL_COMMIT_STEPS];
    CHECK(vl_layer_put(l, "t1", "alice", "builder", "0,0,0", 4, steps) == VL_OK);
    CHECK(vl_layer_put(l, "t2", "alice", "builder", "0,0,0", 5, steps) == VL_OK);
    CHECK(vl_layer_replay(l, NULL, NULL) == VL_OK);
    vl_layer_close(l);

    /* rewrite a byte inside a record: replay must refuse to certify it */
    FILE *fp = fopen(ROOT "/verse/main/events.log", "r+b");
    CHECK(fp != NULL);
    if (fp) {
        fseek(fp, 12, SEEK_SET);
        fputc('Z', fp);
        fclose(fp);
    }
    l = vl_layer_open(ROOT, "main");
    CHECK(l != NULL);
    if (l) {
        /* the rewritten record still recomputes to a self-consistent chain,
         * so only the durable commit pointer can expose it */
        CHECK(vl_layer_anchor_check(l) == VL_ERR_RECOVERY_REQUIRED);
        CHECK(vl_layer_replay(l, NULL, NULL) == VL_ERR_RECOVERY_REQUIRED);
        /* an explicit operator repair re-anchors and makes replay possible
         * again; it is never automatic */
        CHECK(vl_layer_repair(l) == VL_OK);
        CHECK(vl_layer_anchor_check(l) == VL_OK);
        CHECK(vl_layer_replay(l, NULL, NULL) == VL_OK);
        vl_layer_close(l);
    }
}

int main(void) {
    test_create_and_open();
    test_commit_assigns_seq();
    test_idempotent_retry();
    test_undo();
    test_replay_and_reopen();
    test_snapshot();
    test_tamper_is_recovery_required();

    if (failures) {
        fprintf(stderr, "verse_layer_probe: %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("verse_layer_probe: all checks passed\n");
    return 0;
}
