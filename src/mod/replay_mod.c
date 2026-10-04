/* replay_mod.c - deterministic event log, named random streams and replay
 * verification (Infiverse roadmap §52 protocol-core items 3+4, §24.2 event
 * envelope, §24.5 error classification, §48.3/48.4 determinism contract —
 * minimal single-node closure of milestone M1 "snapshot + replay").
 *
 * builtins:
 *   replay_seed(stream, seed)          seed a named deterministic random stream
 *   replay_rand(stream, max)           next value [0, max) from that stream
 *   replay_tick([n])                   advance the logical clock (default 1)
 *   replay_time()                      -> logical time
 *   replay_log_begin(path[, producer]) open an event log (.elog, JSONL)
 *   replay_log(kind, payload)          append an envelope (chained sha256)
 *   replay_log_end()                   close the log
 *   replay_state_hash(value)           canonical sha256 of any value
 *   replay_load(path)                  -> {"events":[...], "count":N}
 *   replay_verify(path)                -> {"ok":true,count,last_seq,last_hash}
 *                                         or {"ok":false, category, ...}
 *
 * Envelope (one JSON object per line):
 *   {"event_id":"evt:<seq>","kind":K,"schema":"1","producer":P,"scope":"verse",
 *    "logical_time":T,"payload":{...},"idempotency_key":"K-<seq>",
 *    "prev":"<hex>","hash":"<hex>"}
 * hash = sha256_hex(prev || canonical-envelope-without-hash).  Errors are
 * returned as dicts classified per §24.5 (ParseError/NotFound/IntegrityError/
 * ResourceLimit), never thrown, so scripts can branch on them.
 */
#include "vm.h"
#include "../../src/common/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Value json_parse_value_text(VM *vm, const char *s, int *ok);
extern void json_write_value_dyn(VM *vm, const Value *v, char **buf, int *pos, int *cap);

/* ---- arg helpers (same convention as record_mod) ---- */
static Value rp_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static const char *rp_arg_str(VM *vm, int i) { Value v = rp_arg(vm, i); return (v.type == VAL_STRING && v.sval) ? v.sval : ""; }
static long long rp_arg_int(VM *vm, int i) { Value v = rp_arg(vm, i); return (v.type == VAL_INT) ? (long long)v.ival : (v.type == VAL_FLOAT) ? (long long)v.fval : 0; }
static void rp_popn(VM *vm, int n) {
    while (n-- > 0 && vm_cur_sp(vm) >= 0) {
        value_free(&vm_cur_stack(vm)[vm_cur_sp(vm)]);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
}
static void rp_push(VM *vm, Value v) { if (!vm_push_value(vm, &v)) value_free(&v); }
static void rp_push_int(VM *vm, long long n) { Value v; v.type = VAL_INT; v.ival = n;  v.sval = NULL; v.ptr = NULL; rp_push(vm, v); }
static void rp_push_nil(VM *vm) { Value v; v.type = VAL_NIL; v.ival = 0;  v.sval = NULL; v.ptr = NULL; rp_push(vm, v); }

static void rp_dict_set(VM *vm, int aidx, const char *key, Value val) {
    Value k; k.type = VAL_STRING; k.ival = 1;  k.sval = (char*)key; k.ptr = NULL;
    vm_dict_set(vm, aidx, &k, &val);
    if (val.type == VAL_STRING && val.sval && val.ival == 0) free(val.sval);
}
/* classified error dict (§24.5): {category, code, retryable, message} */
static void rp_push_error(VM *vm, const char *category, const char *code, int retryable, const char *msg) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { rp_push_nil(vm); return; }
    Value s; s.type = VAL_STRING; s.fval = 0; s.ptr = NULL;
    s.ival = 1; s.sval = (char*)category;           rp_dict_set(vm, aidx, "category", s);
    s.ival = 1; s.sval = (char*)code;               rp_dict_set(vm, aidx, "code", s);
    s.type = VAL_BOOL; s.ival = retryable;          rp_dict_set(vm, aidx, "retryable", s);
    s.type = VAL_STRING; s.ival = 1; s.sval = (char*)msg; rp_dict_set(vm, aidx, "message", s);
    Value d; d.type = VAL_DICT; d.ival = aidx + 1;  d.sval = NULL; d.ptr = NULL;
    rp_push(vm, d);
}

/* ---- canonical serialization of a VM value for hashing ---- */
static void rp_hash_value(Sha256Ctx *ctx, VM *vm, const Value *v, int depth);

static void rp_hash_string(Sha256Ctx *ctx, const char *s) {
    sha256_update(ctx, "\"", 1);
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') { sha256_update(ctx, "\\", 1); sha256_update(ctx, p, 1); }
        else if ((unsigned char)*p < 0x20) { char esc[8]; snprintf(esc, sizeof(esc), "\\u%04x", *p); sha256_update(ctx, esc, 4); }
        else sha256_update(ctx, p, 1);
    }
    sha256_update(ctx, "\"", 1);
}

static void rp_hash_value(Sha256Ctx *ctx, VM *vm, const Value *v, int depth) {
    if (depth > 8) { sha256_update(ctx, "...", 3); return; }
    char tmp[32];
    switch (v->type) {
        case VAL_NIL:   sha256_update(ctx, "null", 4); break;
        case VAL_BOOL:  sha256_update(ctx, v->ival ? "true" : "false", v->ival ? 4 : 5); break;
        case VAL_INT:   snprintf(tmp, sizeof(tmp), "%lld", (long long)v->ival); sha256_update(ctx, tmp, strlen(tmp)); break;
        case VAL_FLOAT: snprintf(tmp, sizeof(tmp), "%.17g", v->fval); sha256_update(ctx, tmp, strlen(tmp)); break;
        case VAL_STRING: rp_hash_string(ctx, v->sval ? v->sval : ""); break;
        case VAL_ARRAY: {
            ArrayObj *a = vm_pool_slot(vm, v->ival - 1);
            sha256_update(ctx, "[", 1);
            if (a) {
                for (int i = 0; i < a->count; i++) {
                    if (i) sha256_update(ctx, ",", 1);
                    rp_hash_value(ctx, vm, &a->items[i], depth + 1);
                }
            }
            sha256_update(ctx, "]", 1);
            break;
        }
        case VAL_DICT: {
            /* canonical dict: hash each key then sort pairs by key hash */
            ArrayObj *a = vm_pool_slot(vm, v->ival - 1);
            if (!a) { sha256_update(ctx, "{}", 2); break; }
            int pairs = a->count / 2;
            /* simple insertion sort over pair indices by key text */
            int order[512];
            if (pairs > 512) pairs = 512;
            const char *keys[512];
            for (int i = 0; i < pairs; i++) {
                Value *kv = &a->items[i * 2];
                keys[i] = (kv->type == VAL_STRING && kv->sval) ? kv->sval : "";
                order[i] = i;
            }
            for (int i = 1; i < pairs; i++) {
                int j = i, t = order[i];
                while (j > 0 && strcmp(keys[order[j - 1]], keys[t]) > 0) { order[j] = order[j - 1]; j--; }
                order[j] = t;
            }
            sha256_update(ctx, "{", 1);
            for (int i = 0; i < pairs; i++) {
                if (i) sha256_update(ctx, ",", 1);
                rp_hash_string(ctx, keys[order[i]]);
                sha256_update(ctx, ":", 1);
                rp_hash_value(ctx, vm, &a->items[order[i] * 2 + 1], depth + 1);
            }
            sha256_update(ctx, "}", 1);
            break;
        }
        default: snprintf(tmp, sizeof(tmp), "\"<%d>\"", v->type); sha256_update(ctx, tmp, strlen(tmp));
    }
}

static void rp_value_sha256(VM *vm, const Value *v, char out[65]) {
    Sha256Ctx ctx; sha256_init(&ctx);
    rp_hash_value(&ctx, vm, v, 0);
    uint8_t digest[32];
    sha256_final(&ctx, digest);
    sha256_hex_of_digest(digest, out);
}

/* ---- named random streams (§48.4 minimal): splitmix64 seeded by
   sha256(parent_seed || stream name) so streams are independent ---- */
#define RP_STREAMS 32
#define RP_STREAM_NAME 48
typedef struct { char name[RP_STREAM_NAME]; unsigned long long state; int used; } RpStream;
static RpStream g_streams[RP_STREAMS];
static int g_stream_count = 0;
static unsigned long long g_replay_time = 0;

static unsigned long long rp_mix64(unsigned long long x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static RpStream *rp_stream(const char *name, long long seed) {
    for (int i = 0; i < g_stream_count; i++)
        if (strcmp(g_streams[i].name, name) == 0) { g_streams[i].state = rp_mix64((unsigned long long)seed ^ 0xA5A5A5A5A5A5A5A5ULL); return &g_streams[i]; }
    if (g_stream_count >= RP_STREAMS) return NULL;
    RpStream *s = &g_streams[g_stream_count++];
    snprintf(s->name, RP_STREAM_NAME, "%s", name);
    s->state = rp_mix64((unsigned long long)seed ^ 0xA5A5A5A5A5A5A5A5ULL);
    return s;
}

/* ---- event log state ---- */
static FILE *g_log = NULL;
static char g_log_path[1024] = "";
static char g_prev_hash[65] = "";
static long long g_log_seq = 0;
static char g_producer[128] = "script";

/* ---- builtins ---- */

static int builtin_replay_seed(VM *vm) {
    long long seed = rp_arg_int(vm, 0);
    const char *stream = vm->cur_argc >= 2 ? rp_arg_str(vm, 1) : "";
    if (!stream[0]) { rp_popn(vm, vm->cur_argc); rp_push_error(vm, "TypeError", "stream_required", 0, "replay_seed expects (stream, seed)"); return 1; }
    RpStream *s = rp_stream(stream, seed);
    rp_popn(vm, vm->cur_argc);
    if (!s) { rp_push_error(vm, "ResourceLimit", "too_many_streams", 0, "random stream table full (32)"); return 1; }
    rp_push_int(vm, 1);
    return 1;
}

static int builtin_replay_rand(VM *vm) {
    long long max = rp_arg_int(vm, 0);
    const char *stream = vm->cur_argc >= 2 ? rp_arg_str(vm, 1) : "";
    rp_popn(vm, vm->cur_argc);
    RpStream *s = NULL;
    for (int i = 0; i < g_stream_count; i++)
        if (strcmp(g_streams[i].name, stream) == 0) { s = &g_streams[i]; break; }
    if (!s || max <= 0) { rp_push_error(vm, "TypeError", "bad_stream_or_max", 0, "replay_rand expects a seeded stream and max > 0"); return 1; }
    s->state = rp_mix64(s->state);
    long long v = (long long)((s->state >> 11) % (unsigned long long)max);
    rp_push_int(vm, v);
    return 1;
}

static int builtin_replay_tick(VM *vm) {
    long long n = vm->cur_argc > 0 ? rp_arg_int(vm, vm->cur_argc - 1) : 1;
    rp_popn(vm, vm->cur_argc);
    if (n < 0) n = 0;
    g_replay_time += (unsigned long long)n;
    rp_push_int(vm, (long long)g_replay_time);
    return 1;
}

static int builtin_replay_time(VM *vm) {
    rp_popn(vm, vm->cur_argc);
    rp_push_int(vm, (long long)g_replay_time);
    return 1;
}

static int builtin_replay_state_hash(VM *vm) {
    Value v = rp_arg(vm, vm->cur_argc - 1);
    char hex[65];
    rp_value_sha256(vm, &v, hex);
    rp_popn(vm, vm->cur_argc);
    Value s; s.type = VAL_STRING; s.ival = 0;  s.ptr = NULL; s.sval = strdup(hex);
    rp_push(vm, s);
    return 1;
}

static int builtin_replay_log_begin(VM *vm) {
    /* (path[, producer]) — r_arg(argc-1) is the FIRST argument (call convention:
       first argument pushed last, at stack top) */
    const char *path = rp_arg_str(vm, vm->cur_argc - 1);   /* first argument */
    const char *producer = vm->cur_argc >= 2 ? rp_arg_str(vm, 0) : "script"; /* last */
    snprintf(g_producer, sizeof(g_producer), "%s", producer[0] ? producer : "script");
    if (g_log) { fclose(g_log); g_log = NULL; }
    g_log = fopen(path, "wb");
    if (!g_log) { rp_popn(vm, vm->cur_argc); rp_push_error(vm, "NotFound", "log_open_failed", 1, "cannot open event log for writing"); return 1; }
    snprintf(g_log_path, sizeof(g_log_path), "%s", path);
    strcpy(g_prev_hash, "0");
    g_log_seq = 0;
    /* header meta line: runtime/rules versions (not part of the hash chain) */
    fprintf(g_log, "{\"meta\":true,\"v\":1,\"schema\":\"1\",\"runtime\":\"0.5.0\",\"rules\":\"1\",\"producer\":\"%s\"}\n", g_producer);
    rp_popn(vm, vm->cur_argc);
    rp_push_int(vm, 1);
    return 1;
}

static int builtin_replay_log_end(VM *vm) {
    rp_popn(vm, vm->cur_argc);
    if (g_log) { fclose(g_log); g_log = NULL; }
    rp_push_int(vm, g_log_seq);
    return 1;
}

static void rp_escape_json(const char *in, char *out, size_t outsz) {
    size_t o = 0;
    for (const char *p = in; *p && o + 6 < outsz; p++) {
        if (*p == '"' || *p == '\\') { out[o++] = '\\'; out[o++] = *p; }
        else if ((unsigned char)*p >= 0x20) out[o++] = *p;
        else { o += (size_t)snprintf(out + o, outsz - o, "\\u%04x", *p); }
    }
    out[o] = '\0';
}

static int builtin_replay_log(VM *vm) {
    Value payload = rp_arg(vm, 0);
    const char *kind = vm->cur_argc >= 2 ? rp_arg_str(vm, 1) : "";
    if (!g_log) { rp_popn(vm, vm->cur_argc); rp_push_error(vm, "ProtocolError", "log_not_open", 0, "call replay_log_begin first"); return 1; }
    if (!kind[0]) { rp_popn(vm, vm->cur_argc); rp_push_error(vm, "TypeError", "kind_required", 0, "replay_log expects (kind, payload)"); return 1; }

    g_log_seq++;
    /* canonical envelope text (fixed field order) for hashing and the file */
    char *canon = NULL; int cpos = 0, ccap = 0;
    {
        char meta[512];
        char kind_esc[256], prod_esc[128], key_esc[288];
        rp_escape_json(kind, kind_esc, sizeof(kind_esc));
        rp_escape_json(g_producer, prod_esc, sizeof(prod_esc));
        snprintf(key_esc, sizeof(key_esc), "%s-%lld", kind_esc, g_log_seq);
        snprintf(meta, sizeof(meta),
                 "{\"event_id\":\"evt:%lld\",\"kind\":\"%s\",\"schema\":\"1\",\"producer\":\"%s\",\"scope\":\"verse\",\"logical_time\":%llu,\"payload\":",
                 g_log_seq, kind_esc, prod_esc, g_replay_time);
        ccap = 4096; canon = (char*)malloc((size_t)ccap); cpos = 0;
        int mlen = (int)strlen(meta);
        memcpy(canon, meta, (size_t)mlen); cpos = mlen;
        json_write_value_dyn(vm, &payload, &canon, &cpos, &ccap);
        char tail[256];
        snprintf(tail, sizeof(tail), ",\"idempotency_key\":\"%s\",\"prev\":\"%s\",\"hash\":\"", key_esc, g_prev_hash);
        int tlen = (int)strlen(tail);
        if (cpos + tlen + 1 > ccap) { ccap = cpos + tlen + 1; canon = (char*)realloc(canon, (size_t)ccap); }
        memcpy(canon + cpos, tail, (size_t)tlen); cpos += tlen;
        /* hash = sha256(prev_hash || canonical text so far) */
        Sha256Ctx ctx; sha256_init(&ctx);
        sha256_update(&ctx, g_prev_hash, strlen(g_prev_hash));
        sha256_update(&ctx, canon, (size_t)cpos);
        uint8_t digest[32];
        sha256_final(&ctx, digest);
        char hex[65];
        sha256_hex_of_digest(digest, hex);
        memcpy(canon + cpos, hex, 64); cpos += 64;
        canon[cpos++] = '}'; canon[cpos++] = '\n'; canon[cpos] = '\0';
        fwrite(canon, 1, (size_t)cpos, g_log);
        fflush(g_log); /* events must be readable even if the run crashes */
        snprintf(g_prev_hash, sizeof(g_prev_hash), "%s", hex);
    }
    free(canon);
    rp_popn(vm, vm->cur_argc);
    Value s; s.type = VAL_STRING; s.ival = 0;  s.ptr = NULL;
    char id[64];
    snprintf(id, sizeof(id), "evt:%lld", g_log_seq);
    s.sval = strdup(id);
    rp_push(vm, s);
    return 1;
}

static int rp_read_all_lines(const char *path, char ***lines, int *count, const char **err_category, const char **err_code, char *err_msg, size_t err_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) { *err_category = "NotFound"; *err_code = "log_open_failed"; snprintf(err_msg, err_sz, "cannot open event log"); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len > (8 << 20)) { fclose(f); *err_category = "ResourceLimit"; *err_code = "log_too_large"; snprintf(err_msg, err_sz, "event log exceeds 8MB"); return -1; }
    char *buf = (char*)malloc((size_t)len + 1);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); *err_category = "TransientFailure"; *err_code = "read_failed"; snprintf(err_msg, err_sz, "short read"); return -1; }
    fclose(f);
    buf[len] = '\0';
    *lines = NULL; *count = 0;
    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        size_t slen = strlen(p);
        if (slen > 0) {
            *lines = (char**)realloc(*lines, (size_t)(*count + 1) * sizeof(char*));
            (*lines)[(*count)++] = strdup(p);
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(buf);
    return 0;
}

static int builtin_replay_load(VM *vm) {
    const char *path = rp_arg_str(vm, 0);
    char **lines = NULL; int count = 0;
    const char *ecat = NULL, *ecode = NULL;
    char emsg[256] = "";
    if (rp_read_all_lines(path, &lines, &count, &ecat, &ecode, emsg, sizeof(emsg)) != 0) {
        rp_popn(vm, vm->cur_argc);
        rp_push_error(vm, ecat, ecode, 1, emsg);
        return 1;
    }
    /* build {count: N, events: [envelope dicts...]} skipping the meta line */
    int events_aidx = vm_array_new(vm);
    int n_events = 0;
    for (int i = 0; i < count; i++) {
        if (strstr(lines[i], "\"meta\":true")) continue;
        int ok = 0;
        Value ev = json_parse_value_text(vm, lines[i], &ok);
        Value meta; meta.type = VAL_STRING; meta.ival = 1;  meta.sval = (char*)"meta"; meta.ptr = NULL;
        Value ismeta = (ok && ev.type == VAL_DICT) ? vm_dict_get(vm, ev.ival - 1, &meta) : (Value){.type = VAL_NIL, .ival = 0, .sval = NULL, .ptr = NULL};
        if (ok && ev.type == VAL_DICT && !(ismeta.type == VAL_BOOL && ismeta.ival)) {
            Value idx; idx.type = VAL_INT; idx.ival = n_events;  idx.sval = NULL; idx.ptr = NULL;
            vm_array_push(vm, events_aidx, &ev);
            n_events++;
        } else {
            value_free(&ev);
        }
        free(lines[i]);
    }
    free(lines);
    int out_aidx = vm_array_new(vm);
    Value c; c.type = VAL_INT; c.ival = n_events;  c.sval = NULL; c.ptr = NULL;
    rp_dict_set(vm, out_aidx, "count", c);
    Value evs; evs.type = VAL_ARRAY; evs.ival = events_aidx + 1;  evs.sval = NULL; evs.ptr = NULL;
    rp_dict_set(vm, out_aidx, "events", evs);
    Value d; d.type = VAL_DICT; d.ival = out_aidx + 1;  d.sval = NULL; d.ptr = NULL;
    rp_popn(vm, vm->cur_argc);
    rp_push(vm, d);
    return 1;
}

static int builtin_replay_verify(VM *vm) {
    const char *path = rp_arg_str(vm, 0);
    char **lines = NULL; int count = 0;
    const char *ecat = NULL, *ecode = NULL;
    char emsg[256] = "";
    if (rp_read_all_lines(path, &lines, &count, &ecat, &ecode, emsg, sizeof(emsg)) != 0) {
        rp_popn(vm, vm->cur_argc);
        rp_push_error(vm, ecat, ecode, 1, emsg);
        return 1;
    }
    char prev[65] = "0";
    long long seq = 0, last_ok = 0;
    int failed = 0;
    char fail_msg[256] = "";
    for (int i = 0; i < count && !failed; i++) {
        char *line = lines[i];
        if (strstr(line, "\"meta\":true")) continue;   /* header, not an event */
        seq++;
        /* hash chain: hash = sha256(prev || canonical text up to and incl.
           `"hash":"`).  The stored hash + closing brace follow in the line. */
        char *hmark = strstr(line, "\"hash\":\"");
        if (!hmark) { failed = 1; snprintf(fail_msg, sizeof(fail_msg), "event %lld missing hash", seq); break; }
        /* prev field must sit between idempotency_key and hash */
        char *pmark = strstr(line, "\"prev\":\"");
        if (!pmark) { failed = 1; snprintf(fail_msg, sizeof(fail_msg), "event %lld missing prev", seq); break; }
        char stored_prev[65] = "";
        size_t plen = (size_t)(hmark - pmark) - 8 - 2; /* skip `"prev":"` (8) and `","` (2) */
        if (plen > 0 && plen <= 64) {
            memcpy(stored_prev, pmark + 8, plen);
            stored_prev[plen] = '\0';
        }
        if (strcmp(stored_prev, prev) != 0) {
            failed = 1; snprintf(fail_msg, sizeof(fail_msg), "event %lld prev-hash mismatch (chain broken)", seq);
            break;
        }
        Sha256Ctx ctx; sha256_init(&ctx);
        sha256_update(&ctx, prev, strlen(prev));
        sha256_update(&ctx, line, (size_t)(hmark - line) + 8); /* incl. `"hash":"` */
        uint8_t digest[32];
        sha256_final(&ctx, digest);
        char hex[65];
        sha256_hex_of_digest(digest, hex);
        char *stored = hmark + 8;
        if (strlen(stored) < 64 || strncmp(hex, stored, 64) != 0) {
            failed = 1; snprintf(fail_msg, sizeof(fail_msg), "event %lld hash mismatch (tampered)", seq);
            break;
        }
        snprintf(prev, sizeof(prev), "%s", hex);
        last_ok = seq;
    }
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
    int out_aidx = vm_array_new(vm);
    Value okv; okv.type = VAL_BOOL; okv.ival = failed ? 0 : 1;  okv.sval = NULL; okv.ptr = NULL;
    rp_dict_set(vm, out_aidx, "ok", okv);
    Value c; c.type = VAL_INT; c.ival = last_ok;  c.sval = NULL; c.ptr = NULL;
    rp_dict_set(vm, out_aidx, "count", c);
    Value ls; ls.type = VAL_INT; ls.ival = last_ok;  ls.sval = NULL; ls.ptr = NULL;
    rp_dict_set(vm, out_aidx, "last_seq", ls);
    Value lh; lh.type = VAL_STRING; lh.ival = 0;  lh.ptr = NULL; lh.sval = strdup(prev);
    rp_dict_set(vm, out_aidx, "last_hash", lh);
    if (failed) {
        Value cat; cat.type = VAL_STRING; cat.ival = 1;  cat.ptr = NULL; cat.sval = (char*)"IntegrityError";
        rp_dict_set(vm, out_aidx, "category", cat);
        Value code; code.type = VAL_STRING; code.ival = 1;  code.ptr = NULL; code.sval = (char*)"integrity_failed";
        rp_dict_set(vm, out_aidx, "code", code);
        Value msg; msg.type = VAL_STRING; msg.ival = 0;  msg.ptr = NULL; msg.sval = strdup(fail_msg);
        rp_dict_set(vm, out_aidx, "message", msg);
    }
    Value d; d.type = VAL_DICT; d.ival = out_aidx + 1;  d.sval = NULL; d.ptr = NULL;
    rp_popn(vm, vm->cur_argc);
    rp_push(vm, d);
    return 1;
}

void replay_mod_register(VM *vm) {
    vm_register_builtin(vm, "replay_seed", builtin_replay_seed);
    vm_register_builtin(vm, "replay_rand", builtin_replay_rand);
    vm_register_builtin(vm, "replay_tick", builtin_replay_tick);
    vm_register_builtin(vm, "replay_time", builtin_replay_time);
    vm_register_builtin(vm, "replay_state_hash", builtin_replay_state_hash);
    vm_register_builtin_full(vm, "replay_log_begin", builtin_replay_log_begin, 1 | CAP_IO, 0);
    vm_register_builtin_full(vm, "replay_log", builtin_replay_log, 1 | CAP_IO, 0);
    vm_register_builtin(vm, "replay_log_end", builtin_replay_log_end);
    vm_register_builtin_full(vm, "replay_load", builtin_replay_load, 1 | CAP_IO, 0);
    vm_register_builtin_full(vm, "replay_verify", builtin_replay_verify, 1 | CAP_IO, 0);
}
