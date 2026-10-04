/* layer.c - authoritative Layer: verse_id, manifest, state, replay (P1 increment 2) */

#include "layer.h"
#include "json_min.h"
#include "../common/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#  include <direct.h>
#  include <io.h>
#  define vl_mkdir(p) _mkdir(p)
#  define vl_fsync(fd) _commit(fd)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define vl_mkdir(p) mkdir((p), 0755)
#  define vl_fsync(fd) fsync(fd)
#endif

#define VL_MANIFEST_ABI "infiverse.mv1/abi/1.0"
#define VL_SCHEMA_VER   1
#define VL_LINE_MAX     4096

typedef struct {
    char      cell[VL_CELL_KEY_MAX];
    long long value;
    long long rev;
} VlCell;

typedef struct {
    char      cell[VL_CELL_KEY_MAX];
    long long prev, value, seq;
} VlHist;

struct VlLayer {
    char        root[512];
    char        dir[640];
    char        events[768];
    char        snap[768];
    char        anchor[768];
    int         anchor_ok;      /* durable commit pointer matched at open */
    VlManifest  man;
    VlEventLog *log;
    VlCell      cells[VL_MAX_CELLS];
    size_t      ncells;
    VlHist      hist[VL_MAX_HIST];
    size_t      nhist;
    long        seq;
};

/* ------------------------------------------------------------------ */
/* small filesystem helpers                                            */
/* ------------------------------------------------------------------ */

static void vl_empty_head(char out[65]);

/* Create every component of `path`.  An already-existing directory is success.
   The existence test is `errno == EEXIST`, which both CRTs answer for a
   directory that is already there.  It used to be `fopen(tmp, "r")`, and on
   Windows a directory cannot be opened as a file: opening an existing
   directory returned NULL, vl_mkdir_p answered -1, vl_layer_create returned
   VL_ERR_IO instead of VL_ERR_CONFLICT, and the manifest was never written --
   which is why the layer tests failed on Windows only.  This mirrors
   im_platform_mkdirs (src/platform/platform.c:99-120), which is the engine's
   one production point for this; the verse core stays free of src/platform on
   purpose, so the rule is repeated here rather than shared. */
/* "C:" and "c:" are drive designators, not directories.  On Windows
   _mkdir("C:") answers EACCES when the drive has no current directory and
   EEXIST only when it happens to have one, so passing it to vl_mkdir is either
   a spurious failure or an accident of the process's cwd -- and since the scan
   below splits on '\\' as well, every absolute Windows path starts with it.
   Skip it as a prefix and treat it as already present as the tail. */
static int vl_is_drive_root(const char *p) {
    size_t n = strlen(p);
    return n == 2 && p[1] == ':' &&
           ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'));
}

static int vl_mkdir_p(const char *path) {
    char tmp[768];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p; *p = '\0';
            if (*tmp && !vl_is_drive_root(tmp) && vl_mkdir(tmp) != 0 && errno != EEXIST) { *p = saved; return -1; }
            *p = saved;
        }
    }
    if (vl_is_drive_root(tmp)) return 0;
    return (vl_mkdir(tmp) == 0 || errno == EEXIST) ? 0 : -1;
}

static int vl_write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    size_t n = strlen(text);
    int ok = (fwrite(text, 1, n, fp) == n);
    if (ok) ok = (fflush(fp) == 0);
    if (ok) {
        int fd = fileno(fp);
        ok = (fd >= 0 && vl_fsync(fd) == 0);
    }
    if (fclose(fp) != 0) ok = 0;
    return ok ? 0 : -1;
}

static char *vl_read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    char *b = (char *)malloc((size_t)sz + 1);
    if (!b) { fclose(fp); return NULL; }
    size_t got = sz > 0 ? fread(b, 1, (size_t)sz, fp) : 0;
    b[got] = '\0';
    fclose(fp);
    if (out_len) *out_len = got;
    return b;
}

/* ------------------------------------------------------------------ */
/* manifest                                                            */
/* ------------------------------------------------------------------ */

static void vl_manifest_canon(const VlManifest *m, VlCjson *j) {
    vl_cjson_init(j);
    vl_cjson_obj_begin(j);
    vl_cjson_key(j, "abi");            vl_cjson_str(j, m->abi);
    vl_cjson_key(j, "created_at");     vl_cjson_int(j, m->created_at);
    vl_cjson_key(j, "schema_version"); vl_cjson_int(j, m->schema_version);
    vl_cjson_key(j, "verse_id");       vl_cjson_str(j, m->verse_id);
    vl_cjson_obj_end(j);
}

VlStatus vl_layer_create(const char *root, const char *verse_id, VlManifest *out) {
    if (!root || !*root || !verse_id || !*verse_id) return VL_ERR_ARG;
    if (strlen(verse_id) >= VL_VERSE_ID_MAX) return VL_ERR_ARG;

    char dir[640];
    snprintf(dir, sizeof dir, "%s/verse/%s", root, verse_id);
    if (vl_mkdir_p(dir) != 0) return VL_ERR_IO;

    char snapdir[768];
    snprintf(snapdir, sizeof snapdir, "%s/snapshots", dir);
    if (vl_mkdir_p(snapdir) != 0) return VL_ERR_IO;

    char mpath[768];
    snprintf(mpath, sizeof mpath, "%s/manifest.json", dir);
    FILE *fp = fopen(mpath, "rb");
    if (fp) { fclose(fp); return VL_ERR_CONFLICT; }   /* never silently overwrite */

    VlManifest m;
    memset(&m, 0, sizeof m);
    snprintf(m.verse_id, sizeof m.verse_id, "%s", verse_id);
    snprintf(m.abi, sizeof m.abi, "%s", VL_MANIFEST_ABI);
    m.schema_version = VL_SCHEMA_VER;
    m.created_at = (long long)time(NULL);

    VlCjson j;
    vl_manifest_canon(&m, &j);
    if (!vl_cjson_ok(&j)) { vl_cjson_free(&j); return VL_ERR_ARG; }
    int rc = vl_write_file(mpath, vl_cjson_data(&j));
    vl_cjson_free(&j);
    if (rc != 0) return VL_ERR_IO;

    /* events.log must exist so the log head is well-defined from the start */
    char epath[768];
    snprintf(epath, sizeof epath, "%s/events.log", dir);
    fp = fopen(epath, "ab");
    if (!fp) return VL_ERR_IO;
    fclose(fp);

    /* the durable commit pointer starts at the empty-log head */
    char apath[768];
    snprintf(apath, sizeof apath, "%s/commit.head", dir);
    char empty[65];
    vl_empty_head(empty);
    VlCjson a;
    vl_cjson_init(&a);
    vl_cjson_obj_begin(&a);
    vl_cjson_key(&a, "head"); vl_cjson_str(&a, empty);
    vl_cjson_key(&a, "seq");  vl_cjson_int(&a, 0);
    vl_cjson_obj_end(&a);
    if (!vl_cjson_ok(&a)) { vl_cjson_free(&a); return VL_ERR_ARG; }
    rc = vl_write_file(apath, vl_cjson_data(&a));
    vl_cjson_free(&a);
    if (rc != 0) return VL_ERR_IO;

    if (out) *out = m;
    return VL_OK;
}

static VlStatus vl_manifest_load(const char *path, VlManifest *m) {
    size_t len = 0;
    char *text = vl_read_file(path, &len);
    if (!text) return VL_ERR_NOT_FOUND;
    char err[128];
    VjVal *v = vj_parse(text, err, sizeof err);
    free(text);
    if (!v || v->type != VJ_OBJ) { vj_free(v); return VL_ERR_ARG; }

    memset(m, 0, sizeof *m);
    const char *vid = vj_str(vj_get(v, "verse_id"), NULL);
    const char *abi = vj_str(vj_get(v, "abi"), NULL);
    if (!vid || !abi) { vj_free(v); return VL_ERR_ARG; }
    snprintf(m->verse_id, sizeof m->verse_id, "%s", vid);
    snprintf(m->abi, sizeof m->abi, "%s", abi);
    m->schema_version = (int)vj_int(vj_get(v, "schema_version"), 0);
    m->created_at     = vj_int(vj_get(v, "created_at"), 0);
    vj_free(v);
    return VL_OK;
}

/* ------------------------------------------------------------------ */
/* durable commit pointer (anchor)                                     */
/* ------------------------------------------------------------------ */

static void vl_empty_head(char out[65]) { vl_state_hash("", out); }

static VlStatus vl_anchor_write(VlLayer *l) {
    const char *head = vl_eventlog_head(l->log);
    if (!head) return VL_ERR_ARG;
    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "head"); vl_cjson_str(&j, head);
    vl_cjson_key(&j, "seq");  vl_cjson_int(&j, (long long)l->seq);
    vl_cjson_obj_end(&j);
    if (!vl_cjson_ok(&j)) { vl_cjson_free(&j); return VL_ERR_ARG; }
    int rc = vl_write_file(l->anchor, vl_cjson_data(&j));
    vl_cjson_free(&j);
    return rc == 0 ? VL_OK : VL_ERR_IO;
}

/* Returns 0 on success; out_head receives the anchored chain head. */
static int vl_anchor_read(const VlLayer *l, char out_head[65], long *out_seq) {
    size_t len = 0;
    char *text = vl_read_file(l->anchor, &len);
    if (!text) return -1;
    VjVal *v = vj_parse(text, NULL, 0);
    free(text);
    if (!v || v->type != VJ_OBJ) { vj_free(v); return -1; }
    const char *h = vj_str(vj_get(v, "head"), NULL);
    if (!h || strlen(h) != 64) { vj_free(v); return -1; }
    snprintf(out_head, 65, "%s", h);
    if (out_seq) *out_seq = (long)vj_int(vj_get(v, "seq"), 0);
    vj_free(v);
    return 0;
}

VlStatus vl_layer_anchor_check(const VlLayer *l) {
    if (!l) return VL_ERR_ARG;
    return l->anchor_ok ? VL_OK : VL_ERR_RECOVERY_REQUIRED;
}

VlStatus vl_layer_repair(VlLayer *l) {
    if (!l) return VL_ERR_ARG;
    VlStatus st = vl_anchor_write(l);
    if (st != VL_OK) return st;
    l->anchor_ok = 1;
    return VL_OK;
}

/* ------------------------------------------------------------------ */
/* state                                                               */
/* ------------------------------------------------------------------ */

static VlCell *vl_cell_find(VlLayer *l, const char *cell) {
    for (size_t i = 0; i < l->ncells; i++)
        if (strcmp(l->cells[i].cell, cell) == 0) return &l->cells[i];
    return NULL;
}

static const VlCell *vl_cell_find_c(const VlLayer *l, const char *cell) {
    for (size_t i = 0; i < l->ncells; i++)
        if (strcmp(l->cells[i].cell, cell) == 0) return &l->cells[i];
    return NULL;
}

static VlStatus vl_state_reset(VlLayer *l) {
    l->ncells = 0;
    l->nhist  = 0;
    l->seq    = 0;
    return VL_OK;
}

void vl_layer_state_canon(const VlLayer *l, VlCjson *out) {
    vl_cjson_init(out);
    vl_cjson_obj_begin(out);

    /* cells sorted by key so the hash does not depend on insertion order */
    const VlCell *sorted[VL_MAX_CELLS];
    size_t n = l->ncells;
    for (size_t i = 0; i < n; i++) sorted[i] = &l->cells[i];
    for (size_t i = 1; i < n; i++) {          /* insertion sort: n is small */
        const VlCell *key = sorted[i];
        size_t j = i;
        while (j > 0 && strcmp(sorted[j - 1]->cell, key->cell) > 0) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }

    vl_cjson_key(out, "cells");
    vl_cjson_arr_begin(out);
    for (size_t i = 0; i < n; i++) {
        vl_cjson_obj_begin(out);
        vl_cjson_key(out, "cell");  vl_cjson_str(out, sorted[i]->cell);
        vl_cjson_key(out, "rev");   vl_cjson_int(out, sorted[i]->rev);
        vl_cjson_key(out, "value"); vl_cjson_int(out, sorted[i]->value);
        vl_cjson_obj_end(out);
    }
    vl_cjson_arr_end(out);

    vl_cjson_key(out, "seq"); vl_cjson_int(out, l->seq);
    vl_cjson_obj_end(out);
}

void vl_layer_state_hash(const VlLayer *l, char out[65]) {
    VlCjson j;
    vl_layer_state_canon(l, &j);
    if (vl_cjson_ok(&j)) vl_state_hash(vl_cjson_data(&j), out);
    else vl_state_hash("", out);
    vl_cjson_free(&j);
}

size_t      vl_layer_cell_count(const VlLayer *l) { return l ? l->ncells : 0; }
const char *vl_layer_cell_at(const VlLayer *l, size_t i) { return (l && i < l->ncells) ? l->cells[i].cell : NULL; }
long long   vl_layer_cell_value_at(const VlLayer *l, size_t i) { return (l && i < l->ncells) ? l->cells[i].value : 0; }

long long vl_layer_cell_value(const VlLayer *l, const char *cell, int *found) {
    const VlCell *c = vl_cell_find_c(l, cell);
    if (found) *found = c ? 1 : 0;
    return c ? c->value : 0;
}

long long vl_layer_cell_rev(const VlLayer *l, const char *cell) {
    const VlCell *c = vl_cell_find_c(l, cell);
    return c ? c->rev : 0;
}

/* ------------------------------------------------------------------ */
/* apply                                                               */
/* ------------------------------------------------------------------ */

/* Applies one canonical record.  seq is the record's server-assigned
 * sequence number; it is carried in the record so replay is independent of
 * arrival order in the file. */
VlStatus vl_layer_apply_record(VlLayer *l, const char *canon_json, long long seq) {
    if (!l || !canon_json) return VL_ERR_ARG;
    VjVal *v = vj_parse(canon_json, NULL, 0);
    if (!v || v->type != VJ_OBJ) { vj_free(v); return VL_ERR_ARG; }

    const char *op = vj_str(vj_get(v, "op"), NULL);
    if (!op) { vj_free(v); return VL_ERR_ARG; }

    VlStatus st = VL_OK;

    if (strcmp(op, "put") == 0) {
        const char *cell = vj_str(vj_get(v, "cell"), NULL);
        long long value = vj_int(vj_get(v, "value"), 0);
        long long prev  = vj_int(vj_get(v, "prev"), 0);
        if (!cell || strlen(cell) >= VL_CELL_KEY_MAX) { vj_free(v); return VL_ERR_ARG; }

        VlCell *c = vl_cell_find(l, cell);
        if (!c) {
            if (l->ncells >= VL_MAX_CELLS) { vj_free(v); return VL_ERR_REJECTED; }
            c = &l->cells[l->ncells++];
            memset(c, 0, sizeof *c);
            snprintf(c->cell, sizeof c->cell, "%s", cell);
        }
        c->value = value;
        c->rev   = seq;

        if (l->nhist >= VL_MAX_HIST) { vj_free(v); return VL_ERR_REJECTED; }
        VlHist *h = &l->hist[l->nhist++];
        snprintf(h->cell, sizeof h->cell, "%s", cell);
        h->prev  = prev;
        h->value = value;
        h->seq   = seq;
    } else if (strcmp(op, "undo") == 0) {
        long long target = vj_int(vj_get(v, "target"), -1);
        VlHist *h = NULL;
        for (size_t i = 0; i < l->nhist; i++)
            if (l->hist[i].seq == target) { h = &l->hist[i]; break; }
        if (!h) { vj_free(v); return VL_ERR_NOT_FOUND; }

        VlCell *c = vl_cell_find(l, h->cell);
        if (!c) {
            if (l->ncells >= VL_MAX_CELLS) { vj_free(v); return VL_ERR_REJECTED; }
            c = &l->cells[l->ncells++];
            memset(c, 0, sizeof *c);
            snprintf(c->cell, sizeof c->cell, "%s", h->cell);
        }
        c->value = h->prev;
        c->rev   = seq;
    } else {
        st = VL_ERR_REJECTED;
    }

    vj_free(v);
    if (st != VL_OK) return st;
    if (seq > l->seq) l->seq = seq;
    return VL_OK;
}

/* step-7 hook: only ever called by vl_commit, after the record is durable. */
static VlStatus vl_layer_apply_hook(void *ctx, const char *canon_json) {
    VlLayer *l = (VlLayer *)ctx;
    long long seq = 0;
    VjVal *v = vj_parse(canon_json, NULL, 0);
    if (v) {
        seq = vj_int(vj_get(v, "seq"), 0);
        vj_free(v);
    }
    return vl_layer_apply_record(l, canon_json, seq);
}

/* ------------------------------------------------------------------ */
/* open / close / replay                                               */
/* ------------------------------------------------------------------ */

static VlStatus vl_layer_load_events(VlLayer *l) {
    vl_state_reset(l);
    FILE *fp = fopen(l->events, "rb");
    if (!fp) return VL_ERR_IO;
    char line[VL_LINE_MAX];
    long count = 0;
    while (fgets(line, sizeof line, fp)) {
        size_t n = strlen(line);
        if (n == 0) continue;
        if (line[n - 1] != '\n') break;    /* partial tail: not committed */
        line[n - 1] = '\0';
        if (line[0] == '\0') continue;
        count++;
        VlStatus st = vl_layer_apply_record(l, line, count);
        if (st != VL_OK) { fclose(fp); return st; }
    }
    fclose(fp);
    l->seq = count;
    return VL_OK;
}

VlLayer *vl_layer_open(const char *root, const char *verse_id) {
    if (!root || !verse_id) return NULL;
    VlLayer *l = (VlLayer *)calloc(1, sizeof *l);
    if (!l) return NULL;
    snprintf(l->root, sizeof l->root, "%s", root);
    snprintf(l->dir, sizeof l->dir, "%s/verse/%s", root, verse_id);
    snprintf(l->events, sizeof l->events, "%s/events.log", l->dir);
    snprintf(l->snap, sizeof l->snap, "%s/snapshots/state.json", l->dir);
    snprintf(l->anchor, sizeof l->anchor, "%s/commit.head", l->dir);

    char mpath[768];
    snprintf(mpath, sizeof mpath, "%s/manifest.json", l->dir);
    if (vl_manifest_load(mpath, &l->man) != VL_OK) { free(l); return NULL; }

    l->log = vl_eventlog_open(l->events);
    if (!l->log) { free(l); return NULL; }

    if (vl_layer_load_events(l) != VL_OK) { vl_eventlog_close(l->log); free(l); return NULL; }

    /* The chain recomputed from disk must match the durable commit pointer. */
    char anchored[65];
    long anchored_seq = 0;
    l->anchor_ok = 0;
    if (vl_anchor_read(l, anchored, &anchored_seq) == 0 &&
        strcmp(anchored, vl_eventlog_head(l->log)) == 0) {
        l->anchor_ok = 1;
    }
    return l;
}

void vl_layer_close(VlLayer *l) {
    if (!l) return;
    if (l->log) vl_eventlog_close(l->log);
    free(l);
}

const VlManifest *vl_layer_manifest(const VlLayer *l) { return l ? &l->man : NULL; }
const char       *vl_layer_dir(const VlLayer *l) { return l ? l->dir : NULL; }
long              vl_layer_seq(const VlLayer *l) { return l ? l->seq : 0; }
const char       *vl_layer_head(const VlLayer *l) { return l ? vl_eventlog_head(l->log) : NULL; }

static VlStatus vl_layer_commit_record(VlLayer *l, const char *idem,
                                       const char *actor, const char *role,
                                       const char *record, int steps[VL_COMMIT_STEPS]) {
    /* Structural rule: the client cannot choose the sequence number.  The
     * record must carry exactly the next sequence the server is about to
     * assign, otherwise it is refused before anything becomes durable. */
    VjVal *v = vj_parse(record, NULL, 0);
    if (!v || v->type != VJ_OBJ) { vj_free(v); return VL_ERR_ARG; }
    long long claimed = vj_int(vj_get(v, "seq"), -1);
    long long rev     = vj_int(vj_get(v, "rev"), -1);
    vj_free(v);
    if (claimed != (long long)l->seq + 1 || rev != claimed) return VL_ERR_REJECTED;

    static int scratch[VL_COMMIT_STEPS];
    VlIntent in;
    memset(&in, 0, sizeof in);
    in.idempotency_key = idem;
    in.actor           = actor;
    in.role            = role;
    in.canon_json      = record;
    in.apply           = vl_layer_apply_hook;
    in.apply_ctx       = l;
    VlStatus st = vl_commit(l->log, &in, steps ? steps : scratch);
    /* Advance the durable commit pointer only after a real commit. */
    if (st == VL_OK) vl_anchor_write(l);
    return st;
}

/* Build {"actor":..,"cell":..,"op":..,"prev":..,"rev":..,"role":..,"seq":..,"value":..} */
static VlStatus vl_build_put(VlCjson *j, VlLayer *l, const char *actor, const char *role,
                             const char *cell, long long value) {
    int found = 0;
    long long prev = vl_layer_cell_value(l, cell, &found);
    long long seq  = (long long)l->seq + 1;

    vl_cjson_init(j);
    vl_cjson_obj_begin(j);
    vl_cjson_key(j, "actor"); vl_cjson_str(j, actor ? actor : "");
    vl_cjson_key(j, "cell");  vl_cjson_str(j, cell);
    vl_cjson_key(j, "op");    vl_cjson_str(j, "put");
    vl_cjson_key(j, "prev");  vl_cjson_int(j, prev);
    vl_cjson_key(j, "rev");   vl_cjson_int(j, seq);
    vl_cjson_key(j, "role");  vl_cjson_str(j, role ? role : "");
    vl_cjson_key(j, "seq");   vl_cjson_int(j, seq);
    vl_cjson_key(j, "value"); vl_cjson_int(j, value);
    vl_cjson_obj_end(j);
    return vl_cjson_ok(j) ? VL_OK : VL_ERR_ARG;
}

VlStatus vl_layer_put(VlLayer *l, const char *idempotency_key,
                      const char *actor, const char *role,
                      const char *cell, long long value,
                      int steps[VL_COMMIT_STEPS]) {
    if (!l || !cell || !*cell) return VL_ERR_ARG;
    if (strlen(cell) >= VL_CELL_KEY_MAX) return VL_ERR_ARG;
    VlCjson j;
    VlStatus st = vl_build_put(&j, l, actor, role, cell, value);
    if (st != VL_OK) { vl_cjson_free(&j); return st; }
    st = vl_layer_commit_record(l, idempotency_key, actor, role, vl_cjson_data(&j), steps);
    vl_cjson_free(&j);
    return st;
}

VlStatus vl_layer_undo(VlLayer *l, const char *idempotency_key,
                       const char *actor, const char *role,
                       long long target_seq, int steps[VL_COMMIT_STEPS]) {
    if (!l) return VL_ERR_ARG;
    /* Validate BEFORE anything becomes durable: a record that cannot be
     * applied would otherwise poison every future replay. */
    int known = 0;
    for (size_t i = 0; i < l->nhist; i++)
        if (l->hist[i].seq == target_seq) { known = 1; break; }
    if (!known) return VL_ERR_NOT_FOUND;

    long long seq = (long long)l->seq + 1;
    VlCjson j;
    vl_cjson_init(&j);
    vl_cjson_obj_begin(&j);
    vl_cjson_key(&j, "actor");  vl_cjson_str(&j, actor ? actor : "");
    vl_cjson_key(&j, "op");     vl_cjson_str(&j, "undo");
    vl_cjson_key(&j, "rev");    vl_cjson_int(&j, seq);
    vl_cjson_key(&j, "role");   vl_cjson_str(&j, role ? role : "");
    vl_cjson_key(&j, "seq");    vl_cjson_int(&j, seq);
    vl_cjson_key(&j, "target"); vl_cjson_int(&j, target_seq);
    vl_cjson_obj_end(&j);
    if (!vl_cjson_ok(&j)) { vl_cjson_free(&j); return VL_ERR_ARG; }
    VlStatus st = vl_layer_commit_record(l, idempotency_key, actor, role, vl_cjson_data(&j), steps);
    vl_cjson_free(&j);
    return st;
}

VlStatus vl_layer_replay(VlLayer *l, char out_hash[65], long *out_applied) {
    if (!l) return VL_ERR_ARG;

    /* 0. the durable commit pointer must match the chain recomputed from disk;
     *    without this a rewritten record is self-consistent and undetectable */
    if (vl_layer_anchor_check(l) != VL_OK) return VL_ERR_RECOVERY_REQUIRED;

    /* 1. the durable chain must still verify against the live head */
    VlStatus st = vl_eventlog_verify(l->log);
    if (st != VL_OK) return st;

    /* 2. rebuild the state from disk into a scratch Layer */
    VlLayer scratch;
    memset(&scratch, 0, sizeof scratch);
    scratch.man = l->man;
    snprintf(scratch.root, sizeof scratch.root, "%s", l->root);
    snprintf(scratch.dir, sizeof scratch.dir, "%s", l->dir);
    snprintf(scratch.events, sizeof scratch.events, "%s", l->events);
    snprintf(scratch.snap, sizeof scratch.snap, "%s", l->snap);
    st = vl_layer_load_events(&scratch);
    if (st != VL_OK) return st;

    /* 3. the replay must reproduce the live state hash exactly */
    char live[65], replayed[65];
    vl_layer_state_hash(l, live);
    vl_layer_state_hash(&scratch, replayed);
    if (out_applied) *out_applied = (long)scratch.seq;
    if (out_hash) memcpy(out_hash, replayed, 65);
    if (strcmp(live, replayed) != 0) return VL_ERR_RECOVERY_REQUIRED;
    return VL_OK;
}

VlStatus vl_layer_snapshot_save(VlLayer *l) {
    if (!l) return VL_ERR_ARG;
    VlCjson j;
    vl_layer_state_canon(l, &j);
    if (!vl_cjson_ok(&j)) { vl_cjson_free(&j); return VL_ERR_ARG; }
    int rc = vl_write_file(l->snap, vl_cjson_data(&j));
    vl_cjson_free(&j);
    return rc == 0 ? VL_OK : VL_ERR_IO;
}

VlStatus vl_layer_snapshot_load(VlLayer *l) {
    if (!l) return VL_ERR_ARG;
    size_t len = 0;
    char *text = vl_read_file(l->snap, &len);
    if (!text) return VL_ERR_NOT_FOUND;
    char err[128];
    VjVal *v = vj_parse(text, err, sizeof err);
    free(text);
    if (!v || v->type != VJ_OBJ) { vj_free(v); return VL_ERR_ARG; }

    long long seq = vj_int(vj_get(v, "seq"), -1);
    const VjVal *cells = vj_get(v, "cells");
    if (seq < 0 || !cells || cells->type != VJ_ARR || cells->n > VL_MAX_CELLS) {
        vj_free(v);
        return VL_ERR_ARG;
    }

    /* Replace the in-memory state with the snapshot's. */
    l->ncells = 0;
    l->nhist  = 0;
    for (size_t i = 0; i < cells->n; i++) {
        const VjVal *c = cells->items[i];
        if (!c || c->type != VJ_OBJ) { vj_free(v); return VL_ERR_ARG; }
        const char *key = vj_str(vj_get(c, "cell"), NULL);
        if (!key || strlen(key) >= VL_CELL_KEY_MAX) { vj_free(v); return VL_ERR_ARG; }
        VlCell *dst = &l->cells[l->ncells++];
        memset(dst, 0, sizeof *dst);
        snprintf(dst->cell, sizeof dst->cell, "%s", key);
        dst->value = vj_int(vj_get(c, "value"), 0);
        dst->rev   = vj_int(vj_get(c, "rev"), 0);
    }
    l->seq = seq;
    vj_free(v);
    return VL_OK;
}
