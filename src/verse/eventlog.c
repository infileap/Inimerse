/* eventlog.c - minimal Layer event log and commit contract (P1 increment 1) */

#include "eventlog.h"
#include "../common/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <io.h>
#  define vl_fileno(f)  _fileno(f)
#  define vl_fsync(fd)  _commit(fd)
#  define vl_truncate(fd, n) _chsize((fd), (n))
#else
#  include <unistd.h>
#  define vl_fileno(f)  fileno(f)
#  define vl_fsync(fd)  fsync(fd)
#  define vl_truncate(fd, n) ftruncate((fd), (n))
#endif

/* ================================================================== */
/* canonical JSON writer                                               */
/* ================================================================== */

static void vl_cjson_ensure(VlCjson *j, size_t extra) {
    if (j->err) return;
    if (j->len + extra + 1 <= j->cap) return;
    size_t cap = j->cap ? j->cap : 256;
    while (cap < j->len + extra + 1) cap *= 2;
    char *nb = (char *)realloc(j->buf, cap);
    if (!nb) { j->err = 1; return; }
    j->buf = nb;
    j->cap = cap;
}

static void vl_cjson_put(VlCjson *j, const char *s, size_t n) {
    if (j->err) return;
    vl_cjson_ensure(j, n);
    if (j->err) return;
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void vl_cjson_puts(VlCjson *j, const char *s) { vl_cjson_put(j, s, strlen(s)); }

/* Emit the separator demanded by the current container, before a member.
 * A value that directly follows a key must not emit a comma. */
static void vl_cjson_sep(VlCjson *j) {
    if (j->err) return;
    if (j->depth <= 0) return;
    if (j->after_key[j->depth]) { j->after_key[j->depth] = 0; return; }
    if (j->first[j->depth]) { j->first[j->depth] = 0; return; }
    vl_cjson_puts(j, ",");
}

void vl_cjson_init(VlCjson *j) {
    memset(j, 0, sizeof *j);
    j->first[0] = 1;
}

void vl_cjson_free(VlCjson *j) {
    if (!j) return;
    free(j->buf);
    memset(j, 0, sizeof *j);
}

int vl_cjson_ok(const VlCjson *j) {
    if (!j || j->err || j->depth != 0) return 0;
    return 1;
}

const char *vl_cjson_data(const VlCjson *j) {
    if (!vl_cjson_ok(j)) return NULL;
    return j->buf ? j->buf : "";
}

static void vl_cjson_cont_begin(VlCjson *j, int is_obj, char open) {
    if (j->err) return;
    if (j->depth >= VL_CJSON_MAX_DEPTH - 1) { j->err = 1; return; }
    vl_cjson_sep(j);
    char b[2] = { open, '\0' };
    vl_cjson_puts(j, b);
    j->depth++;
    j->is_obj[j->depth] = is_obj;
    j->first[j->depth] = 1;
}

void vl_cjson_obj_begin(VlCjson *j) { vl_cjson_cont_begin(j, 1, '{'); }
void vl_cjson_arr_begin(VlCjson *j) { vl_cjson_cont_begin(j, 0, '['); }

static void vl_cjson_cont_end(VlCjson *j, char close) {
    if (j->err) return;
    if (j->depth <= 0) { j->err = 1; return; }
    if (j->after_key[j->depth]) { j->err = 1; return; }   /* key with no value */
    j->depth--;
    char b[2] = { close, '\0' };
    vl_cjson_puts(j, b);
}

void vl_cjson_obj_end(VlCjson *j) { vl_cjson_cont_end(j, '}'); }
void vl_cjson_arr_end(VlCjson *j) { vl_cjson_cont_end(j, ']'); }

static void vl_cjson_escaped(VlCjson *j, const char *s) {
    if (j->err || !s) { j->err = 1; return; }
    vl_cjson_put(j, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':  vl_cjson_puts(j, "\\\""); break;
        case '\\': vl_cjson_puts(j, "\\\\"); break;
        case '\b': vl_cjson_puts(j, "\\b");  break;
        case '\f': vl_cjson_puts(j, "\\f");  break;
        case '\n': vl_cjson_puts(j, "\\n");  break;
        case '\r': vl_cjson_puts(j, "\\r");  break;
        case '\t': vl_cjson_puts(j, "\\t");  break;
        default:
            if (c < 0x20) {
                char esc[7];
                snprintf(esc, sizeof esc, "\\u%04x", (unsigned)c);
                vl_cjson_puts(j, esc);
            } else {
                vl_cjson_put(j, (const char *)&c, 1);
            }
        }
        if (j->err) return;
    }
    vl_cjson_put(j, "\"", 1);
}

void vl_cjson_key(VlCjson *j, const char *k) {
    if (j->err) return;
    if (j->depth <= 0 || !j->is_obj[j->depth]) { j->err = 1; return; }
    if (j->after_key[j->depth]) { j->err = 1; return; }   /* key without a value */
    vl_cjson_sep(j);
    vl_cjson_escaped(j, k);
    vl_cjson_puts(j, ":");
    j->after_key[j->depth] = 1;
}

void vl_cjson_str(VlCjson *j, const char *s) {
    if (j->err) return;
    if (j->depth <= 0) { j->err = 1; return; }
    vl_cjson_sep(j);
    vl_cjson_escaped(j, s);
}

void vl_cjson_int(VlCjson *j, int64_t v) {
    if (j->err) return;
    if (j->depth <= 0) { j->err = 1; return; }
    vl_cjson_sep(j);
    char b[32];
    snprintf(b, sizeof b, "%lld", (long long)v);
    vl_cjson_puts(j, b);
}

void vl_cjson_bool(VlCjson *j, int b) {
    if (j->err) return;
    if (j->depth <= 0) { j->err = 1; return; }
    vl_cjson_sep(j);
    vl_cjson_puts(j, b ? "true" : "false");
}

void vl_cjson_null(VlCjson *j) {
    if (j->err) return;
    if (j->depth <= 0) { j->err = 1; return; }
    vl_cjson_sep(j);
    vl_cjson_puts(j, "null");
}

static int vl_key_cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void vl_cjson_sort_keys(char **keys, size_t n) { qsort(keys, n, sizeof *keys, vl_key_cmp); }

void vl_state_hash(const char *canonical_json, char out[65]) {
    if (!canonical_json) canonical_json = "";
    sha256_hex(canonical_json, strlen(canonical_json), out);
}

const char *vl_status_name(VlStatus s) {
    switch (s) {
    case VL_OK:                    return "ok";
    case VL_ERR_ARG:               return "arg";
    case VL_ERR_IO:                return "io";
    case VL_ERR_CONFLICT:          return "conflict";
    case VL_ERR_DURABILITY:        return "durability_failed";
    case VL_ERR_RECOVERY_REQUIRED: return "recovery_required";
    case VL_ERR_REJECTED:          return "rejected";
    case VL_ERR_NOT_FOUND:         return "not_found";
    }
    return "unknown";
}

/* ================================================================== */
/* event log                                                           */
/* ================================================================== */

#define VL_MAX_IDEMPOTENCY 64
#define VL_KEY_MAX         128

typedef struct {
    char key[VL_KEY_MAX];
    char head[65];
    long seq;
    int  used;
} VlIdemEntry;

struct VlEventLog {
    FILE        *fp;
    char         path[512];
    char         head[65];        /* head after the last COMMITTED record */
    long         count;
    long         end_off;         /* file offset of end-of-log            */
    VlStatus     last_status;
    int          fault_step;      /* 1-based commit step to fail; 0 = off  */
    VlIdemEntry  idem[VL_MAX_IDEMPOTENCY];
};

static void vl_head_of(const char *prev_head, const char *canon, char out[65]) {
    Sha256Ctx ctx;
    uint8_t   d[32];
    sha256_init(&ctx);
    sha256_update(&ctx, prev_head, strlen(prev_head));
    sha256_update(&ctx, "\n", 1);
    sha256_update(&ctx, canon, strlen(canon));
    sha256_final(&ctx, d);
    sha256_hex_of_digest(d, out);
}

static int vl_read_whole(FILE *fp, char **out, size_t *out_len) {
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    long sz = ftell(fp);
    if (sz < 0) return -1;
    if (fseek(fp, 0, SEEK_SET) != 0) return -1;
    char *b = (char *)malloc((size_t)sz + 1);
    if (!b) return -1;
    size_t got = sz > 0 ? fread(b, 1, (size_t)sz, fp) : 0;
    b[got] = '\0';
    if (fseek(fp, 0, SEEK_END) != 0) { free(b); return -1; }
    *out = b;
    *out_len = got;
    return 0;
}

/* Recompute the chain from the records currently in the file. */
static VlStatus vl_recompute(VlEventLog *log, char out_head[65], long *out_count) {
    char *body = NULL;
    size_t len = 0;
    if (vl_read_whole(log->fp, &body, &len) != 0) return VL_ERR_IO;

    char head[65];
    vl_state_hash("", head);
    long n = 0;

    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && body[i] != '\n') i++;
        if (i >= len) { /* trailing partial line: not a committed record */
            break;
        }
        size_t rl = i - start;
        if (rl == 0) { i++; continue; }   /* blank line: ignore */
        char *rec = (char *)malloc(rl + 1);
        if (!rec) { free(body); return VL_ERR_IO; }
        memcpy(rec, body + start, rl);
        rec[rl] = '\0';
        char nh[65];
        vl_head_of(head, rec, nh);
        memcpy(head, nh, sizeof head);
        free(rec);
        n++;
        i++;
    }
    free(body);
    if (out_head)  memcpy(out_head, head, 65);
    if (out_count) *out_count = n;
    return VL_OK;
}

VlEventLog *vl_eventlog_open(const char *path) {
    if (!path || !*path) return NULL;
    VlEventLog *log = (VlEventLog *)calloc(1, sizeof *log);
    if (!log) return NULL;
    snprintf(log->path, sizeof log->path, "%s", path);

    FILE *fp = fopen(path, "r+b");
    if (!fp) fp = fopen(path, "w+b");
    if (!fp) { free(log); return NULL; }
    log->fp = fp;

    char head[65];
    long n = 0;
    VlStatus st = vl_recompute(log, head, &n);
    if (st != VL_OK) { fclose(fp); free(log); return NULL; }
    memcpy(log->head, head, sizeof head);
    log->count = n;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); free(log); return NULL; }
    log->end_off  = ftell(fp);
    log->last_status = VL_OK;
    return log;
}

void vl_eventlog_close(VlEventLog *log) {
    if (!log) return;
    if (log->fp) fclose(log->fp);
    free(log);
}

size_t      vl_eventlog_count(const VlEventLog *log) { return log ? (size_t)log->count : 0; }
const char *vl_eventlog_head(const VlEventLog *log)  { return log ? log->head : NULL; }
VlStatus    vl_eventlog_last_status(const VlEventLog *log) { return log ? log->last_status : VL_ERR_ARG; }

void vl_eventlog_set_fault_step(VlEventLog *log, int step) {
    if (log) log->fault_step = step;
}

/* Write one record without committing it to the in-memory head. */
static VlStatus vl_write_record(VlEventLog *log, const char *canon, char out_head[65]) {
    long off = log->end_off;
    if (fseek(log->fp, off, SEEK_SET) != 0) return VL_ERR_IO;
    size_t n = strlen(canon);
    if (fwrite(canon, 1, n, log->fp) != n) { fseek(log->fp, off, SEEK_SET); return VL_ERR_IO; }
    if (fwrite("\n", 1, 1, log->fp) != 1) { fseek(log->fp, off, SEEK_SET); return VL_ERR_IO; }
    char nh[65];
    vl_head_of(log->head, canon, nh);
    if (out_head) memcpy(out_head, nh, 65);
    if (fseek(log->fp, off, SEEK_SET) != 0) return VL_ERR_IO;   /* keep the position for rollback */
    return VL_OK;
}

VlStatus vl_eventlog_durable_flush(VlEventLog *log) {
    if (!log || !log->fp) return VL_ERR_ARG;
    long off = ftell(log->fp);
    if (off < 0) return VL_ERR_IO;
    /* Under the staged-record protocol the cursor sits at the rollback point;
     * flush whatever is already durable at end_off instead. */
    if (fseek(log->fp, 0, SEEK_END) != 0) return VL_ERR_IO;
    if (fflush(log->fp) != 0) return VL_ERR_DURABILITY;
    int fd = vl_fileno(log->fp);
    if (fd < 0) return VL_ERR_DURABILITY;
    if (vl_fsync(fd) != 0) return VL_ERR_DURABILITY;
    log->last_status = VL_OK;
    return VL_OK;
}

/* Append path shared by append/conditional_append. */
static VlStatus vl_append_common(VlEventLog *log, const char *expected_head,
                                 const char *canon, char out_head[65]) {
    if (!log || !canon || !*canon) return VL_ERR_ARG;
    if (expected_head && strcmp(expected_head, log->head) != 0) {
        log->last_status = VL_ERR_CONFLICT;
        return VL_ERR_CONFLICT;
    }
    long off = log->end_off;
    if (fseek(log->fp, off, SEEK_SET) != 0) { log->last_status = VL_ERR_IO; return VL_ERR_IO; }
    size_t n = strlen(canon);
    if (fwrite(canon, 1, n, log->fp) != n || fwrite("\n", 1, 1, log->fp) != 1) {
        fseek(log->fp, off, SEEK_SET);
        log->last_status = VL_ERR_IO;
        return VL_ERR_IO;
    }
    if (fflush(log->fp) != 0) { fseek(log->fp, off, SEEK_SET); log->last_status = VL_ERR_DURABILITY; return VL_ERR_DURABILITY; }
    char nh[65];
    vl_head_of(log->head, canon, nh);
    int fd = vl_fileno(log->fp);
    if (fd < 0 || vl_fsync(fd) != 0) {
        fseek(log->fp, off, SEEK_SET);
        log->last_status = VL_ERR_DURABILITY;
        return VL_ERR_DURABILITY;
    }
    memcpy(log->head, nh, sizeof nh);
    log->count++;
    log->end_off = off + (long)n + 1;
    if (fseek(log->fp, log->end_off, SEEK_SET) != 0) { log->last_status = VL_ERR_IO; return VL_ERR_IO; }
    if (out_head) memcpy(out_head, nh, 65);
    log->last_status = VL_OK;
    return VL_OK;
}

VlStatus vl_eventlog_append(VlEventLog *log, const char *canon_json, char out_head[65]) {
    return vl_append_common(log, NULL, canon_json, out_head);
}

VlStatus vl_eventlog_conditional_append(VlEventLog *log, const char *expected_head,
                                        const char *canon_json, char out_head[65]) {
    if (!expected_head) return VL_ERR_ARG;
    return vl_append_common(log, expected_head, canon_json, out_head);
}

VlStatus vl_eventlog_verify(VlEventLog *log) {
    if (!log) return VL_ERR_ARG;
    if (fflush(log->fp) != 0) return VL_ERR_IO;
    char head[65];
    long n = 0;
    VlStatus st = vl_recompute(log, head, &n);
    if (st != VL_OK) return st;
    if (strcmp(head, log->head) != 0 || n != log->count) {
        log->last_status = VL_ERR_RECOVERY_REQUIRED;
        return VL_ERR_RECOVERY_REQUIRED;
    }
    log->last_status = VL_OK;
    return VL_OK;
}

VlStatus vl_eventlog_recover(VlEventLog *log, const char *expected_head) {
    if (!log || !expected_head) return VL_ERR_ARG;
    if (fflush(log->fp) != 0) return VL_ERR_IO;
    char head[65];
    long n = 0;
    VlStatus st = vl_recompute(log, head, &n);
    if (st != VL_OK) { log->last_status = VL_ERR_RECOVERY_REQUIRED; return VL_ERR_RECOVERY_REQUIRED; }
    if (strcmp(head, expected_head) != 0) {   /* a mismatch is never "recovered" */
        log->last_status = VL_ERR_RECOVERY_REQUIRED;
        return VL_ERR_RECOVERY_REQUIRED;
    }
    memcpy(log->head, head, sizeof head);
    log->count = n;
    if (fseek(log->fp, 0, SEEK_END) != 0) { log->last_status = VL_ERR_IO; return VL_ERR_IO; }
    log->end_off = ftell(log->fp);
    log->last_status = VL_OK;
    return VL_OK;
}

/* ================================================================== */
/* 10-step commit                                                      */
/* ================================================================== */

static VlIdemEntry *vl_idem_find(VlEventLog *log, const char *key) {
    for (int i = 0; i < VL_MAX_IDEMPOTENCY; i++)
        if (log->idem[i].used && strcmp(log->idem[i].key, key) == 0) return &log->idem[i];
    return NULL;
}

static VlIdemEntry *vl_idem_slot(VlEventLog *log) {
    for (int i = 0; i < VL_MAX_IDEMPOTENCY; i++)
        if (!log->idem[i].used) return &log->idem[i];
    return NULL;
}

VlStatus vl_commit(VlEventLog *log, const VlIntent *in, int steps[VL_COMMIT_STEPS]) {
    if (steps) for (int i = 0; i < VL_COMMIT_STEPS; i++) steps[i] = 0;
    if (!log || !in || steps == NULL) return VL_ERR_ARG;

    const int fault = log->fault_step;
    log->fault_step = 0;   /* one-shot */

    /* --- 1 validate intent ---------------------------------------- */
    if (!in->idempotency_key || !*in->idempotency_key || strlen(in->idempotency_key) >= VL_KEY_MAX ||
        !in->canon_json || !*in->canon_json) {
        log->last_status = VL_ERR_ARG;
        return VL_ERR_ARG;
    }
    steps[0] = 1;

    /* --- 2 idempotency lookup ------------------------------------- */
    steps[1] = 1;
    VlIdemEntry *prev = vl_idem_find(log, in->idempotency_key);
    if (prev) {                     /* replay: never append a second time */
        steps[1] = 1;
        log->last_status = VL_OK;
        return VL_OK;
    }

    /* --- 3 authorize ---------------------------------------------- */
    /* Client-supplied actor/role are untrusted: recorded, never trusted for
     * authorization. P1 stub allows every intent. */
    steps[2] = 1;
    if (fault == 3) { log->last_status = VL_ERR_REJECTED; return VL_ERR_REJECTED; }

    /* --- 4 stage --------------------------------------------------- */
    char staged_head[65];
    vl_head_of(log->head, in->canon_json, staged_head);
    steps[3] = 1;
    if (fault == 4) { log->last_status = VL_ERR_IO; return VL_ERR_IO; }

    /* --- 5 append to the log file (not yet visible in head) -------- */
    long off = log->end_off;
    if (fseek(log->fp, off, SEEK_SET) != 0) { log->last_status = VL_ERR_IO; return VL_ERR_IO; }
    size_t n = strlen(in->canon_json);
    if (fwrite(in->canon_json, 1, n, log->fp) != n ||
        fwrite("\n", 1, 1, log->fp) != 1) {
        fseek(log->fp, off, SEEK_SET);
        log->last_status = VL_ERR_IO;
        return VL_ERR_IO;
    }
    steps[4] = 1;

    /* --- 6 durable flush ------------------------------------------- */
    /* Failure here MUST NOT run steps 7-10, and MUST NOT report committed. */
    int flush_ok = (fflush(log->fp) == 0);
    if (flush_ok) {
        int fd = vl_fileno(log->fp);
        flush_ok = (fd >= 0 && vl_fsync(fd) == 0);
    }
    if (fault == 6) flush_ok = 0;
    if (!flush_ok) {
        /* Roll the staged record back out of the file.  Step 5 already wrote
           it, so leaving it there makes the file disagree with head/count and
           the very next verify() answers RECOVERY_REQUIRED.  Windows has no
           ftruncate; _chsize is its equivalent.  This rollback used to be
           compiled out on Windows, which is why this probe failed there only. */
        if (vl_truncate(vl_fileno(log->fp), off) != 0) { /* best effort */ }
        fseek(log->fp, off, SEEK_SET);
        log->last_status = VL_ERR_DURABILITY;
        return VL_ERR_DURABILITY;      /* steps 5..9 remain 0 */
    }
    steps[5] = 1;

    /* --- 7 apply to state ------------------------------------------ */
    /* The record is durable now. If applying it fails, the log and the
     * materialized state disagree: that is RECOVERY_REQUIRED, never committed. */
    if (fault == 7 || (in->apply && in->apply(in->apply_ctx, in->canon_json) != VL_OK)) {
        fseek(log->fp, off + (long)n + 1, SEEK_SET);
        log->end_off = off + (long)n + 1;
        log->last_status = VL_ERR_RECOVERY_REQUIRED;
        return VL_ERR_RECOVERY_REQUIRED;   /* steps 7..9 remain 0 */
    }
    steps[6] = 1;

    /* --- 8 update head/snapshot ------------------------------------ */
    memcpy(log->head, staged_head, sizeof staged_head);
    log->count++;
    log->end_off = off + (long)n + 1;
    if (fseek(log->fp, log->end_off, SEEK_SET) != 0) { log->last_status = VL_ERR_IO; return VL_ERR_IO; }
    steps[7] = 1;

    /* --- 9 record idempotency result -------------------------------- */
    VlIdemEntry *slot = vl_idem_slot(log);
    if (slot) {
        snprintf(slot->key, sizeof slot->key, "%s", in->idempotency_key);
        memcpy(slot->head, log->head, 65);
        slot->seq  = log->count;
        slot->used = 1;
    }
    steps[8] = 1;

    /* --- 10 committed ----------------------------------------------- */
    steps[9] = 1;
    log->last_status = VL_OK;
    return VL_OK;
}
