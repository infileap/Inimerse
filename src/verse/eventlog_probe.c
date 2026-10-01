/* eventlog_probe.c - P1 increment 1 regression probe
 *
 * Asserts the commit-contract invariants that the P1 "minimal Layer closed
 * loop" depends on.  Every check fails the process with a non-zero status.
 */
#include "eventlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_PATH "verse_eventlog_probe.log"

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static int all_zero(const int *steps, int from) {
    for (int i = from; i < VL_COMMIT_STEPS; i++)
        if (steps[i]) return 0;
    return 1;
}

static void reset_log(void) {
    remove(LOG_PATH);
}

/* ------------------------------------------------------------------ */

static void test_canonical_json(void) {
    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "a"); vl_cjson_int(&j, 1);
    vl_cjson_key(&j, "b"); vl_cjson_str(&j, "x\"y");
    vl_cjson_key(&j, "c"); vl_cjson_bool(&j, 1);
    vl_cjson_key(&j, "d"); vl_cjson_null(&j);
    vl_cjson_key(&j, "e"); vl_cjson_arr_begin(&j);
    vl_cjson_int(&j, 2); vl_cjson_int(&j, -3);
    vl_cjson_arr_end(&j);
    vl_cjson_obj_end(&j);
    CHECK(vl_cjson_ok(&j));
    const char *got = vl_cjson_data(&j);
    const char *want = "{\"a\":1,\"b\":\"x\\\"y\",\"c\":true,\"d\":null,\"e\":[2,-3]}";
    if (!got || strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL canonical json: got %s want %s\n", got ? got : "(null)", want);
        failures++;
    }
    vl_cjson_free(&j);

    /* key order is normalised by sorting, not by emission order */
    char *keys_a[] = { (char *)"b", (char *)"a", (char *)"c" };
    char *keys_b[] = { (char *)"a", (char *)"b", (char *)"c" };
    vl_cjson_sort_keys(keys_a, 3);
    for (int i = 0; i < 3; i++) CHECK(strcmp(keys_a[i], keys_b[i]) == 0);

    /* malformed usage is sticky and yields no data */
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    CHECK(!vl_cjson_ok(&j));      /* left open */
    CHECK(vl_cjson_data(&j) == NULL);
    vl_cjson_free(&j);

    /* state_hash is a pure function of the canonical text */
    char h1[65], h2[65];
    vl_state_hash(want, h1);
    vl_state_hash(want, h2);
    CHECK(strcmp(h1, h2) == 0);
    CHECK(strlen(h1) == 64);
    char h3[65];
    vl_state_hash("{}", h3);
    CHECK(strcmp(h1, h3) != 0);
}

static void test_append_and_verify(void) {
    reset_log();
    VlEventLog *log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    if (!log) return;
    CHECK(vl_eventlog_count(log) == 0);
    char base[65];
    memcpy(base, vl_eventlog_head(log), 65);

    char h1[65];
    CHECK(vl_eventlog_append(log, "{\"n\":1}", h1) == VL_OK);
    CHECK(vl_eventlog_count(log) == 1);
    CHECK(strcmp(vl_eventlog_head(log), h1) == 0);
    CHECK(strcmp(h1, base) != 0);

    CHECK(vl_eventlog_append(log, "{\"n\":2}", NULL) == VL_OK);
    CHECK(vl_eventlog_count(log) == 2);

    /* conditional append with the live head succeeds ... */
    char live[65];
    memcpy(live, vl_eventlog_head(log), 65);
    CHECK(vl_eventlog_conditional_append(log, live, "{\"n\":3}", NULL) == VL_OK);
    CHECK(vl_eventlog_count(log) == 3);

    /* ... a stale head is a conflict and appends nothing */
    CHECK(vl_eventlog_conditional_append(log, base, "{\"n\":4}", NULL) == VL_ERR_CONFLICT);
    CHECK(vl_eventlog_count(log) == 3);

    CHECK(vl_eventlog_verify(log) == VL_OK);
    vl_eventlog_close(log);

    /* reopening rebuilds the same chain */
    log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    CHECK(vl_eventlog_count(log) == 3);
    CHECK(strcmp(vl_eventlog_head(log), live) != 0);   /* live == head after n:3 */
    CHECK(vl_eventlog_verify(log) == VL_OK);
    vl_eventlog_close(log);
    reset_log();
}

static void test_commit_step6_no_partial_commit(void) {
    reset_log();
    VlEventLog *log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    if (!log) return;

    VlIntent in = { "idem-1", "alice", "builder", "{\"op\":\"a\"}" };
    int steps[VL_COMMIT_STEPS];
    CHECK(vl_commit(log, &in, steps) == VL_OK);
    CHECK(steps[9] == 1);                     /* step 10 = committed */
    CHECK(vl_eventlog_count(log) == 1);
    char head_before[65];
    memcpy(head_before, vl_eventlog_head(log), 65);

    /* durability failure: steps 7-10 MUST NOT run, nothing may be committed */
    VlIntent in2 = { "idem-2", "alice", "builder", "{\"op\":\"b\"}" };
    vl_eventlog_set_fault_step(log, 6);
    int st2[VL_COMMIT_STEPS];
    CHECK(vl_commit(log, &in2, st2) == VL_ERR_DURABILITY);
    CHECK(vl_eventlog_last_status(log) == VL_ERR_DURABILITY);
    CHECK(st2[5] == 0);                       /* step 6 did not succeed   */
    CHECK(all_zero(st2, 6));                  /* steps 7..10 never ran    */
    CHECK(vl_eventlog_count(log) == 1);       /* sequence did not advance */
    CHECK(strcmp(vl_eventlog_head(log), head_before) == 0);
    CHECK(vl_eventlog_verify(log) == VL_OK);  /* log and head still agree */

    /* the same intent can be retried after the fault clears */
    CHECK(vl_commit(log, &in2, st2) == VL_OK);
    CHECK(vl_eventlog_count(log) == 2);
    vl_eventlog_close(log);
    reset_log();
}

static void test_commit_step7_recovery_required(void) {
    reset_log();
    VlEventLog *log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    if (!log) return;

    VlIntent in = { "idem-a", "alice", "builder", "{\"op\":\"a\"}" };
    int steps[VL_COMMIT_STEPS];
    CHECK(vl_commit(log, &in, steps) == VL_OK);
    CHECK(vl_eventlog_count(log) == 1);

    /* apply failure after a durable write: MUST NOT report committed */
    VlIntent in2 = { "idem-b", "alice", "builder", "{\"op\":\"b\"}" };
    int st2[VL_COMMIT_STEPS];
    vl_eventlog_set_fault_step(log, 7);
    CHECK(vl_commit(log, &in2, st2) == VL_ERR_RECOVERY_REQUIRED);
    CHECK(vl_eventlog_last_status(log) == VL_ERR_RECOVERY_REQUIRED);
    CHECK(st2[6] == 0);                       /* step 7 did not succeed */
    CHECK(st2[9] == 0);                       /* never committed        */
    CHECK(vl_eventlog_count(log) == 1);       /* head not advanced      */

    /* the durable record is ahead of the head: that is exactly the
     * RECOVERY_REQUIRED condition, and verify must say so. */
    CHECK(vl_eventlog_verify(log) == VL_ERR_RECOVERY_REQUIRED);
    vl_eventlog_close(log);
    reset_log();
}

static void test_idempotency(void) {
    reset_log();
    VlEventLog *log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    if (!log) return;

    VlIntent in = { "key-once", "alice", "builder", "{\"op\":\"move\"}" };
    int steps[VL_COMMIT_STEPS];
    CHECK(vl_commit(log, &in, steps) == VL_OK);
    CHECK(vl_eventlog_count(log) == 1);

    /* a repeated request must not append a second record */
    CHECK(vl_commit(log, &in, steps) == VL_OK);
    CHECK(vl_eventlog_count(log) == 1);

    /* but a different key does append */
    VlIntent other = { "key-two", "alice", "builder", "{\"op\":\"move\"}" };
    CHECK(vl_commit(log, &other, steps) == VL_OK);
    CHECK(vl_eventlog_count(log) == 2);

    /* missing / empty key is rejected before anything is written */
    VlIntent bad = { "", "alice", "builder", "{\"op\":\"x\"}" };
    CHECK(vl_commit(log, &bad, steps) == VL_ERR_ARG);
    CHECK(all_zero(steps, 1));
    CHECK(vl_eventlog_count(log) == 2);
    vl_eventlog_close(log);
    reset_log();
}

static void test_recover_detects_tampering(void) {
    reset_log();
    VlEventLog *log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    if (!log) return;

    CHECK(vl_eventlog_append(log, "{\"n\":1}", NULL) == VL_OK);
    CHECK(vl_eventlog_append(log, "{\"n\":2}", NULL) == VL_OK);
    char good[65];
    memcpy(good, vl_eventlog_head(log), 65);
    vl_eventlog_close(log);

    /* recovery against the committed head succeeds */
    log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    CHECK(vl_eventlog_recover(log, good) == VL_OK);

    /* recovery against a wrong head is RECOVERY_REQUIRED, never OK */
    CHECK(vl_eventlog_recover(log, "0000000000000000000000000000000000000000000000000000000000000000")
          == VL_ERR_RECOVERY_REQUIRED);
    CHECK(vl_eventlog_last_status(log) == VL_ERR_RECOVERY_REQUIRED);
    vl_eventlog_close(log);

    /* rewrite a record in place: the recomputed head no longer matches */
    FILE *fp = fopen(LOG_PATH, "r+b");
    CHECK(fp != NULL);
    if (fp) {
        fseek(fp, 0, SEEK_SET);
        fputc('X', fp);        /* corrupt the first record's opening brace */
        fclose(fp);
    }
    log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    CHECK(vl_eventlog_recover(log, good) == VL_ERR_RECOVERY_REQUIRED);
    vl_eventlog_close(log);
    reset_log();
}

static void test_recovery_tolerates_partial_tail(void) {
    reset_log();
    VlEventLog *log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    if (!log) return;
    CHECK(vl_eventlog_append(log, "{\"n\":1}", NULL) == VL_OK);
    char good[65];
    memcpy(good, vl_eventlog_head(log), 65);
    vl_eventlog_close(log);

    /* simulate a crash mid-record: a newline-less tail must be ignored */
    FILE *fp = fopen(LOG_PATH, "ab");
    CHECK(fp != NULL);
    if (fp) {
        fwrite("{\"n\":2", 1, 6, fp);
        fclose(fp);
    }
    log = vl_eventlog_open(LOG_PATH);
    CHECK(log != NULL);
    CHECK(vl_eventlog_count(log) == 1);
    CHECK(vl_eventlog_recover(log, good) == VL_OK);
    vl_eventlog_close(log);
    reset_log();
}

int main(void) {
    test_canonical_json();
    test_append_and_verify();
    test_commit_step6_no_partial_commit();
    test_commit_step7_recovery_required();
    test_idempotency();
    test_recover_detects_tampering();
    test_recovery_tolerates_partial_tail();

    if (failures) {
        fprintf(stderr, "verse_eventlog_probe: %d check(s) FAILED\n", failures);
        return 1;
    }
    printf("verse_eventlog_probe: all checks passed\n");
    return 0;
}
