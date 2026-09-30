/* profiler.c - minimal function-level profiler */
#include "profiler.h"
#include "../vm/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static unsigned long long prof_now_ns(void) {
    static LARGE_INTEGER freq;
    static int init = 0;
    LARGE_INTEGER c;
    if (!init) { QueryPerformanceFrequency(&freq); init = 1; }
    QueryPerformanceCounter(&c);
    return (unsigned long long)(c.QuadPart * 1000000000ULL / freq.QuadPart);
}
#else
#include <time.h>
static unsigned long long prof_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
}
#endif

#define PROF_STACK_CAP 8192
#define PROF_MAX_FN_NAME 96

typedef struct {
    char name[PROF_MAX_FN_NAME];
    unsigned long long calls;
    unsigned long long total_ns;
    unsigned long long max_ns;
} ProfFn;

typedef struct {
    VmThread *t;            /* owning thread (calls from different OS threads interleave) */
    int depth;              /* frame_count at entry */
    int fn_idx;
    unsigned long long start_ns;
} ProfFrame;

typedef struct {
    char *path;             /* call path, ';' separated (root first) */
    unsigned long long calls;
    unsigned long long ns;  /* inclusive time attributed to the full path */
} ProfPath;

typedef struct {
    ProfFn *fns; int fn_count, fn_cap;
    ProfFrame *stack; int stack_count; /* [0, PROF_STACK_CAP) */
    ProfPath *paths; int path_count, path_cap;
} ProfState;

static void prof_record_path(ProfState *st, unsigned long long calls, unsigned long long ns) {
    char path[2048];
    size_t used = 0;
    for (int i = 0; i < st->stack_count && used < sizeof(path) - 1; i++) {
        used += (size_t)snprintf(path + used, sizeof(path) - used, "%s%s",
                                 used ? ";" : "", st->fns[st->stack[i].fn_idx].name);
    }
    for (int i = 0; i < st->path_count; i++) {
        if (strcmp(st->paths[i].path, path) == 0) {
            st->paths[i].calls += calls;
            st->paths[i].ns += ns;
            return;
        }
    }
    if (st->path_count == st->path_cap) {
        st->path_cap = st->path_cap ? st->path_cap * 2 : 64;
        st->paths = (ProfPath*)realloc(st->paths, st->path_cap * sizeof(ProfPath));
    }
    ProfPath *pp = &st->paths[st->path_count++];
    pp->path = strdup(path);
    pp->calls = calls;
    pp->ns = ns;
}

static int prof_fn_slot(ProfState *st, const char *name) {
    for (int i = 0; i < st->fn_count; i++)
        if (strncmp(st->fns[i].name, name, PROF_MAX_FN_NAME) == 0) return i;
    if (st->fn_count == st->fn_cap) {
        st->fn_cap = st->fn_cap ? st->fn_cap * 2 : 64;
        st->fns = (ProfFn*)realloc(st->fns, st->fn_cap * sizeof(ProfFn));
    }
    ProfFn *fn = &st->fns[st->fn_count];
    snprintf(fn->name, PROF_MAX_FN_NAME, "%s", name);
    fn->calls = 0; fn->total_ns = 0; fn->max_ns = 0;
    return st->fn_count++;
}

void prof_enable(VM *vm) {
    if (vm->prof_state) return; /* already enabled */
    ProfState *st = (ProfState*)calloc(1, sizeof(ProfState));
    st->stack = (ProfFrame*)malloc(PROF_STACK_CAP * sizeof(ProfFrame));
    vm->prof_state = st;
    vm->prof_enabled = 1;
}

void prof_record_call(VM *vm, int depth, const char *func_name) {
    ProfState *st = (ProfState*)vm->prof_state;
    if (!st || st->stack_count >= PROF_STACK_CAP) return;
    char name[PROF_MAX_FN_NAME];
    snprintf(name, sizeof(name), "%s", func_name ? func_name : "<anon>");
    ProfFrame *fr = &st->stack[st->stack_count++];
    fr->t = vm_get_cur_thread();
    fr->depth = depth;
    fr->fn_idx = prof_fn_slot(st, name);
    fr->start_ns = prof_now_ns();
}

void prof_record_return(VM *vm, int depth) {
    ProfState *st = (ProfState*)vm->prof_state;
    if (!st || st->stack_count <= 0) return;
    VmThread *t = vm_get_cur_thread();
    unsigned long long now = prof_now_ns();
    /* pop frames of this thread at or above the returning depth (a normal
       return matches exactly the top; exception unwinds pop several) */
    while (st->stack_count > 0) {
        ProfFrame *fr = &st->stack[st->stack_count - 1];
        if (fr->t != t || fr->depth < depth) break;
        ProfFn *fn = &st->fns[fr->fn_idx];
        unsigned long long dt = now > fr->start_ns ? now - fr->start_ns : 0;
        fn->calls++;
        fn->total_ns += dt;
        if (dt > fn->max_ns) fn->max_ns = dt;
        prof_record_path(st, 1, dt); /* full stack (incl. this frame) = one call path */
        st->stack_count--;
    }
}

static int prof_cmp(const void *a, const void *b) {
    const ProfFn *fa = (const ProfFn*)a, *fb = (const ProfFn*)b;
    if (fa->total_ns != fb->total_ns) return fa->total_ns > fb->total_ns ? -1 : 1;
    return 0;
}

void prof_finish(VM *vm, const char *out_path) {
    ProfState *st = (ProfState*)vm->prof_state;
    if (!st) return;
    vm->prof_enabled = 0;
    vm->prof_state = NULL;

    qsort(st->fns, st->fn_count, sizeof(ProfFn), prof_cmp);

    FILE *fp = out_path ? fopen(out_path, "w") : NULL;
    if (out_path && !fp) fprintf(stderr, "error: cannot write profile '%s'\n", out_path);
    if (fp) fprintf(fp, "# function calls total_ms max_ms\n");
    int shown = 0;
    for (int i = 0; i < st->fn_count; i++) {
        ProfFn *fn = &st->fns[i];
        if (!fn->calls) continue; /* entered but never returned (still on stack) */
        if (fp) fprintf(fp, "%s %llu %.3f %.3f\n", fn->name, fn->calls,
                        fn->total_ns / 1e6, fn->max_ns / 1e6);
        if (shown++ < 20)
            printf("  %-32s %8llu calls  %10.3f ms total  %10.3f ms max\n",
                   fn->name, fn->calls, fn->total_ns / 1e6, fn->max_ns / 1e6);
    }
    if (fp) fclose(fp);
    if (out_path) printf("profile: %s (%d functions, %d recorded frames)\n", out_path, st->fn_count, st->stack_count);
    else printf("profile: %d functions, %d recorded frames\n", st->fn_count, st->stack_count);

    /* folded call stacks (Brendan Gregg collapsed format, two views):
       "<stack> <count>" and "<stack_us> <microseconds>" — feed .folded to
       tools/prof2flame.py or flamegraph.pl directly. */
    if (out_path && st->path_count > 0) {
        char folded_path[2100];
        snprintf(folded_path, sizeof(folded_path), "%s.folded", out_path);
        FILE *ff = fopen(folded_path, "w");
        if (ff) {
            for (int i = 0; i < st->path_count; i++)
                fprintf(ff, "%s %llu\n", st->paths[i].path, st->paths[i].calls);
            fprintf(ff, "\n");
            for (int i = 0; i < st->path_count; i++)
                fprintf(ff, "%s_us %llu\n", st->paths[i].path, st->paths[i].ns / 1000ULL);
            fclose(ff);
        }
    }

    for (int i = 0; i < st->path_count; i++) free(st->paths[i].path);
    free(st->paths);
    free(st->fns);
    free(st->stack);
    free(st);
}
