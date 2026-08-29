#include "say_stream.h"
#include <stdio.h>
#include <string.h>

/* A deliberately small, synchronous OutputStream PAL.  Modules can expose
 * richer transports later, while scripts get identical bounded semantics on
 * every host today.  A write is delivered immediately; queued tracks the
 * number of in-flight writes and is released after delivery. */
#define SAY_STREAM_MAX 64
#define SAY_STREAM_QUEUE 16
typedef struct {
    int used;
    int limit;
    int queued;
    int priority;
    int cancelled;
    char target[32];
    char format[16];
    char error[32];
    char queue[SAY_STREAM_QUEUE][1024];
    char qmeta[SAY_STREAM_QUEUE][512];
    int qhead, qtail, qcount;
} SayStream;
static SayStream g_streams[SAY_STREAM_MAX];

static Value sarg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static void drop(VM *vm) { int n = vm->cur_argc; while (n-- > 0 && vm_cur_sp(vm) >= 0) vm_cur_set_sp(vm, vm_cur_sp(vm) - 1); }
static int stream_id(VM *vm) { Value v = sarg(vm, 0); return v.type == VAL_INT ? v.ival : (int)v.fval; }
static void emit_target_meta(const char *target, const char *text, const char *meta) {
    if ((!strcmp(target, "dialogue") || !strcmp(target, "character")) && meta && *meta) {
        printf("{\"target\":\"%s\",\"text\":\"", target);
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); }
        printf("\",\"meta\":%s}\n", meta); fflush(stdout); return;
    }
    if (!strcmp(target, "log")) fprintf(stderr, "[info] %s\n", text);
    else if (!strcmp(target, "json")) fprintf(stdout, "%s\n", text);
    else if (!strcmp(target, "console")) fprintf(stdout, "%s\n", text);
    else fprintf(stdout, "[%s] %s\n", target, text);
    fflush(!strcmp(target, "log") ? stderr : stdout);
}
static void emit_target(const char *target, const char *text) { emit_target_meta(target, text, NULL); }
static int stream_open(VM *vm) {
    const char *target = "console"; int limit = 1024;
    if (vm->cur_argc > 0) { Value v = sarg(vm, vm->cur_argc - 1); if (v.type == VAL_STRING && v.sval && *v.sval) target = v.sval; }
    if (vm->cur_argc > 1) { Value v = sarg(vm, vm->cur_argc - 2); limit = v.type == VAL_INT ? v.ival : (int)v.fval; }
    if (limit < 1) limit = 1;
    if (limit > 65536) limit = 65536;
    int slot = -1; for (int i = 0; i < SAY_STREAM_MAX; ++i) if (!g_streams[i].used) { slot = i; break; }
    drop(vm); if (slot < 0) { push_int(vm, -1); return 1; }
    g_streams[slot].used = 1; g_streams[slot].limit = limit; g_streams[slot].queued = 0;
    g_streams[slot].priority = 0; g_streams[slot].cancelled = 0;
    snprintf(g_streams[slot].target, sizeof g_streams[slot].target, "%s", target);
    snprintf(g_streams[slot].format, sizeof g_streams[slot].format, "%s", "text");
    g_streams[slot].error[0] = 0;
    /* Optional third/fourth arguments are format and priority metadata. */
    if (vm->cur_argc > 2) { Value v = sarg(vm, vm->cur_argc - 3); if (v.type == VAL_STRING && v.sval && *v.sval) snprintf(g_streams[slot].format, sizeof g_streams[slot].format, "%s", v.sval); }
    if (vm->cur_argc > 3) { Value v = sarg(vm, vm->cur_argc - 4); g_streams[slot].priority = v.type == VAL_INT ? v.ival : (int)v.fval; }
    push_int(vm, slot + 1); return 1;
}
static int stream_write(VM *vm) {
    if (vm->cur_argc < 2) { drop(vm); push_int(vm, 0); return 1; }
    int offset = vm->cur_argc > 2 ? 1 : 0;
    Value iv = sarg(vm, 1 + offset); int id = iv.type == VAL_INT ? iv.ival : (int)iv.fval;
    Value tv = sarg(vm, offset); const char *text = tv.type == VAL_STRING && tv.sval ? tv.sval : "";
    const char *meta = ""; if (offset) { Value mv = sarg(vm, 0); if (mv.type == VAL_STRING && mv.sval) meta = mv.sval; }
    int ok = id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used && !g_streams[id - 1].cancelled;
    if (!ok && id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used) snprintf(g_streams[id - 1].error, sizeof g_streams[id - 1].error, "%s", g_streams[id - 1].cancelled ? "cancelled" : "invalid");
    if (ok) {
        SayStream *s = &g_streams[id - 1];
        if (!strcmp(s->target, "ai") && !strstr(meta, "\"source\":\"ai\"")) { snprintf(s->error, sizeof s->error, "%s", "ai_boundary"); ok = 0; }
    }
    if (ok) {
        SayStream *s = &g_streams[id - 1];
        if (s->queued >= s->limit) { ok = 0; snprintf(s->error, sizeof s->error, "%s", "backpressure"); }
        else { s->queued++; emit_target_meta(s->target, text, meta); s->queued--; }
    }
    drop(vm); push_int(vm, ok ? 1 : 0); return 1;
}
static int stream_flush(VM *vm) {
    int id = vm->cur_argc ? stream_id(vm) : 0;
    int ok = id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used && !g_streams[id - 1].cancelled;
    if (ok) {
        SayStream *s = &g_streams[id - 1];
        while (s->qcount > 0) { emit_target_meta(s->target, s->queue[s->qhead], s->qmeta[s->qhead]); s->qhead = (s->qhead + 1) % SAY_STREAM_QUEUE; s->qcount--; }
        s->qtail = s->qhead; s->error[0] = 0;
    }
    drop(vm); push_int(vm, ok ? 1 : 0); return 1;
}
static int stream_enqueue(VM *vm) {
    if (vm->cur_argc < 2) { drop(vm); push_int(vm, 0); return 1; }
    int offset = vm->cur_argc > 2 ? 1 : 0;
    Value iv = sarg(vm, 1 + offset), tv = sarg(vm, offset); int id = iv.type == VAL_INT ? iv.ival : (int)iv.fval;
    const char *text = tv.type == VAL_STRING && tv.sval ? tv.sval : ""; const char *meta = "";
    if (offset) { Value mv = sarg(vm, 0); if (mv.type == VAL_STRING && mv.sval) meta = mv.sval; }
    int ok = id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used && !g_streams[id - 1].cancelled;
    if (ok) {
        SayStream *s = &g_streams[id - 1];
        if (!strcmp(s->target, "ai") && !strstr(meta, "\"source\":\"ai\"")) { snprintf(s->error, sizeof s->error, "%s", "ai_boundary"); ok = 0; }
        else if (s->qcount >= SAY_STREAM_QUEUE) { snprintf(s->error, sizeof s->error, "%s", "backpressure"); ok = 0; }
        else { snprintf(s->queue[s->qtail], sizeof s->queue[0], "%s", text); snprintf(s->qmeta[s->qtail], sizeof s->qmeta[0], "%s", meta); s->qtail = (s->qtail + 1) % SAY_STREAM_QUEUE; s->qcount++; }
    }
    drop(vm); push_int(vm, ok ? 1 : 0); return 1;
}
static int stream_last_error(VM *vm) {
    int id = vm->cur_argc ? stream_id(vm) : 0; const char *err = "invalid";
    if (id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used) err = g_streams[id - 1].error;
    drop(vm); push_string(vm, err); return 1;
}
static int stream_cancel(VM *vm) {
    int id = vm->cur_argc ? stream_id(vm) : 0;
    int ok = id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used;
    if (ok) { g_streams[id - 1].cancelled = 1; g_streams[id - 1].queued = 0; g_streams[id - 1].qcount = 0; }
    drop(vm); push_int(vm, ok ? 1 : 0); return 1;
}
static int stream_status(VM *vm) {
    int id = vm->cur_argc ? stream_id(vm) : 0;
    int state = (id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used) ? (g_streams[id - 1].cancelled ? -1 : 1) : 0;
    drop(vm); push_int(vm, state); return 1;
}
static int stream_close(VM *vm) {
    int id = vm->cur_argc ? stream_id(vm) : 0; int ok = id > 0 && id <= SAY_STREAM_MAX && g_streams[id - 1].used;
    if (ok) memset(&g_streams[id - 1], 0, sizeof g_streams[0]);
    drop(vm); push_int(vm, ok ? 1 : 0); return 1;
}
void say_stream_register(VM *vm) {
    vm_register_builtin(vm, "output_stream_open", stream_open);
    vm_register_builtin(vm, "output_stream_write", stream_write);
    vm_register_builtin(vm, "output_stream_close", stream_close);
    vm_register_builtin(vm, "output_stream_cancel", stream_cancel);
    vm_register_builtin(vm, "output_stream_status", stream_status);
    vm_register_builtin(vm, "output_stream_flush", stream_flush);
    vm_register_builtin(vm, "output_stream_last_error", stream_last_error);
    vm_register_builtin(vm, "output_stream_enqueue", stream_enqueue);
    vm_register_builtin(vm, "output_stream_write_async", stream_enqueue);
    vm_register_builtin(vm, "say_stream_open", stream_open);
    vm_register_builtin(vm, "say_stream_write", stream_write);
    vm_register_builtin(vm, "say_stream_close", stream_close);
    vm_register_builtin(vm, "say_stream_cancel", stream_cancel);
    vm_register_builtin(vm, "say_stream_status", stream_status);
    vm_register_builtin(vm, "say_stream_flush", stream_flush);
    vm_register_builtin(vm, "say_stream_last_error", stream_last_error);
    vm_register_builtin(vm, "say_stream_enqueue", stream_enqueue);
    vm_register_builtin(vm, "say_stream_write_async", stream_enqueue);
}
