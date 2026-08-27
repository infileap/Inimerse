#include "vm.h"
#include "../parser/parser.h"
#include "../compiler/compiler.h"
#include "../compiler/bytecode.h"
#include "../platform/platform.h"
#include "../platform/thread.h"
#include "../platform/fiber.h"
#include "../platform/sync.h"
#define ConvertThreadToFiber(x) im_fiber_convert_current()
#define CreateFiber(stack, proc, arg) im_fiber_create((stack), (ImFiberProc)(proc), (arg))
#define SwitchToFiber(fiber) im_fiber_switch((ImFiber *)(fiber))
#define DeleteFiber(fiber) im_fiber_destroy((ImFiber *)(fiber))
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

/* set support (forward declarations; defined before vm_execute_thread) */
static SetObj *vm_set_slot(VM *vm, int idx);
int vm_set_new(VM *vm);
static const char *builtin_set_name(int bi);

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <mmsystem.h>
#define sleep_ms(ms) Sleep(ms)

/* 锟竭程诧拷锟斤拷锟诫�??lexer.h ??ThreadOp 一锟铰ｏ拷 */
#ifndef THREAD_OP_STOP
#define THREAD_OP_STOP 0
#define THREAD_OP_PAUSE 1
#define THREAD_OP_RESUME 2
#define THREAD_OP_KILL 3
#define THREAD_OP_RESTART 4
#endif
#ifndef THREAD_FLAG_ENDLESS
#define THREAD_FLAG_ENDLESS (1 << 0)
#define THREAD_FLAG_DAEMON  (1 << 1)
#define THREAD_FLAG_RESTART (1 << 2)
#define THREAD_FLAG_SINGLE  (1 << 3)
#endif

/* 全锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷globals/arrays锟斤拷锟斤拷CRITICAL_SECTION 同一锟竭程匡拷锟斤拷锟诫（锟疥定锟斤拷锟?vm.h??*/

/* 锟斤拷前执锟斤拷锟竭程ｏ拷锟竭程局诧拷锟芥储锟斤拷锟斤拷锟斤拷锟矫猴拷锟斤拷通锟斤拷锟斤拷锟斤拷锟绞碉拷前锟竭程碉拷??*/
static _Thread_local VmThread *g_cur_thread = NULL;

VmThread *vm_get_cur_thread(void) { return g_cur_thread; }
void vm_set_cur_thread(VmThread *t) { g_cur_thread = t; }

int vm_cur_sp(VM *vm) { (void)vm; return g_cur_thread ? g_cur_thread->sp : -1; }
Value *vm_cur_stack(VM *vm) { (void)vm; return g_cur_thread ? g_cur_thread->stack : NULL; }
void vm_cur_set_sp(VM *vm, int sp) { (void)vm; if (g_cur_thread) g_cur_thread->sp = sp; }

#else
#include <unistd.h>
#include <stdint.h>
#define __stdcall
typedef unsigned int MMRESULT; static inline MMRESULT timeBeginPeriod(unsigned int x) { (void)x; return 0; }
typedef uint32_t DWORD; typedef unsigned long long ULONGLONG; typedef long LONG; typedef void *LPVOID; typedef void *HANDLE;
#define STD_OUTPUT_HANDLE 1
static inline HANDLE GetStdHandle(int x) { (void)x; return NULL; }
static inline int WriteFile(HANDLE h, const void *b, DWORD n, DWORD *w, void *o) { (void)h; (void)o; *w=(DWORD)fwrite(b,1,n,stdout); return 1; }
static inline LONG InterlockedIncrement(volatile LONG *p) { return __sync_add_and_fetch(p, 1); }
static inline LONG InterlockedDecrement(volatile LONG *p) { return __sync_sub_and_fetch(p, 1); }
static inline LONG InterlockedExchangeAdd(volatile LONG *p, LONG v) { return __sync_fetch_and_add(p, v); }
#define WaitForSingleObject(h, ms) ((void)(h), (void)(ms), 0)
static _Thread_local VmThread *g_cur_thread = NULL;
VmThread *vm_get_cur_thread(void) { return g_cur_thread; }
void vm_set_cur_thread(VmThread *t) { g_cur_thread = t; }
int vm_cur_sp(VM *vm) { (void)vm; return g_cur_thread ? g_cur_thread->sp : -1; }
Value *vm_cur_stack(VM *vm) { (void)vm; return g_cur_thread ? g_cur_thread->stack : NULL; }
void vm_cur_set_sp(VM *vm, int sp) { (void)vm; if (g_cur_thread) g_cur_thread->sp = sp; }
#define sleep_ms(ms) im_platform_sleep_ms((unsigned int)(ms))
#endif

#ifndef GetTickCount64
#define GetTickCount64 im_platform_now_ms
#endif
#ifndef Sleep
#define Sleep(ms) im_platform_sleep_ms((unsigned int)(ms))
#endif

double val_as_double(const Value *v) {
    switch (v->type) {
        case VAL_INT:   return v->ival;
        case VAL_FLOAT: return v->fval;
        case VAL_BOOL:  return v->ival ? 1.0 : 0.0;
        default:        return 0.0;
    }


}

bool val_eq(const Value *a, const Value *b) {
    if (a->type == b->type) {
        if (a->type == VAL_INT)    return a->ival == b->ival;
        if (a->type == VAL_FLOAT)  return a->fval == b->fval;
        if (a->type == VAL_STRING) return (a->sval == b->sval) ? true : strcmp(a->sval ? a->sval : "", b->sval ? b->sval : "") == 0;
        if (a->type == VAL_BOOL)   return a->ival == b->ival;
        if (a->type == VAL_NIL)    return true;
        if (a->type == VAL_ARRAY)  return a->ival == b->ival;  /* 同一锟斤拷锟斤拷锟斤拷锟??*/
        if (a->type == VAL_DICT)   return a->ival == b->ival;
    }
    /* int ??float 锟斤拷值锟饺较ｏ拷锟斤拷锟洁不同锟斤拷锟斤拷一锟缴诧拷锟斤拷龋锟斤拷薷锟斤拷锟??锟街碉拷??nil 锟饺较碉拷锟斤拷锟叫ｏ拷 */
    if ((a->type == VAL_INT || a->type == VAL_FLOAT) &&
        (b->type == VAL_INT || b->type == VAL_FLOAT))
        return val_as_double(a) == val_as_double(b);
    return false;
}

static int val_cmp(Value *a, Value *b) {
    /* 锟街凤拷锟斤拷锟斤拷锟街碉拷锟斤拷冉希锟絣exer 锟叫讹拷锟斤拷母锟斤拷锟斤拷瘸锟斤拷锟斤拷锟?*/
    if (a->type == VAL_STRING && b->type == VAL_STRING) {
        if (a->sval == b->sval) return 0;
        return strcmp(a->sval ? a->sval : "", b->sval ? b->sval : "");
    }
    double da = val_as_double(a), db = val_as_double(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* 栈锟斤拷锟斤拷锟斤拷为锟斤拷锟矫猴拷锟斤拷锟斤拷锟矫憋拷锟斤拷锟斤拷 */
void push_int(VM *vm, int v) {
    if (g_cur_thread->sp >= 1023) return;
    g_cur_thread->sp++;
    g_cur_thread->stack[g_cur_thread->sp].type = VAL_INT;
    g_cur_thread->stack[g_cur_thread->sp].ival = v;
}

void push_float(VM *vm, double v) {
    if (g_cur_thread->sp >= 1023) return;
    g_cur_thread->sp++;
    g_cur_thread->stack[g_cur_thread->sp].type = VAL_FLOAT;
    g_cur_thread->stack[g_cur_thread->sp].fval = v;
}

void push_string(VM *vm, const char *s) {
    if (g_cur_thread->sp >= 1023) return;
    g_cur_thread->sp++;
    g_cur_thread->stack[g_cur_thread->sp].type = VAL_STRING;
    g_cur_thread->stack[g_cur_thread->sp].ival = 0;
    g_cur_thread->stack[g_cur_thread->sp].sval = s ? strdup(s) : NULL;
}

void push_double(VM *vm, double d) {
    if (g_cur_thread->sp >= 1023) return;
    g_cur_thread->sp++;
    g_cur_thread->stack[g_cur_thread->sp].type = VAL_FLOAT;
    g_cur_thread->stack[g_cur_thread->sp].fval = d;
}

void push_bool(VM *vm, bool b) {
    if (g_cur_thread->sp >= 1023) return;
    g_cur_thread->sp++;
    g_cur_thread->stack[g_cur_thread->sp].type = VAL_BOOL;
    g_cur_thread->stack[g_cur_thread->sp].ival = b ? 1 : 0;
}

void push_nil(VM *vm) {
    if (g_cur_thread->sp >= 1023) return;
    g_cur_thread->sp++;
    g_cur_thread->stack[g_cur_thread->sp].type = VAL_NIL;
}

void pop(VM *vm) {
    if (g_cur_thread->sp >= 0) {
        if (g_cur_thread->stack[g_cur_thread->sp].type == VAL_STRING &&
            g_cur_thread->stack[g_cur_thread->sp].ival != 1)
            free(g_cur_thread->stack[g_cur_thread->sp].sval);
        g_cur_thread->sp--;
    }
}

/* ========== 锟斤拷锟斤拷??========== */
static void value_copy(Value *dst, const Value *src) {
    *dst = *src;
    if (src->type == VAL_STRING && src->sval && src->ival != 1)
        dst->sval = strdup(src->sval);
}

void limit_abort(VM *vm, const char *what, double used, double limit);

void value_free(Value *v) {
    if (v->type == VAL_STRING && v->sval && v->ival != 1) {
        free(v->sval);
        v->sval = NULL;
    }
}
/* ========== 锟街凤拷锟斤拷锟斤拷(interning) ==========
 * VAL_STRING ??ival==1 锟斤拷示锟斤拷锟街凤拷锟斤拷:锟斤拷锟斤拷指锟诫、锟斤拷锟斤拷锟酵放★拷锟斤拷指锟斤拷冉�??
 * 锟截伙拷??LOADK_STRING / LOAD_GLOBAL / STORE_GLOBAL / dict_get / builtin 锟斤�??
 */
struct StrPool { char **slots; int count; int cap; int mask; };

static unsigned long long fnv1a(const char *s) {
    unsigned long long h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

static void strpool_grow(VM *vm, struct StrPool *p) {
    int ncap = p->cap ? p->cap * 2 : 256;
    char **ns = calloc(ncap, sizeof(char*));
    int nmask = ncap - 1;
    for (int i = 0; i < p->cap; i++) {
        if (p->slots[i]) {
            int j = (int)(fnv1a(p->slots[i]) & nmask);
            while (ns[j]) j = (j + 1) & nmask;
            ns[j] = p->slots[i];
        }
    }
    free(p->slots);
    p->slots = ns;
    p->cap = ncap;
    p->mask = nmask;
}

const char *vm_intern(VM *vm, const char *s) {
    if (!s) return NULL;
    struct StrPool *p = vm->str_pool;
    if (!p) return NULL;
    int need_lock = (vm->active_threads > 1);
    if (need_lock) VM_LOCK(vm);
    if (p->count >= VM_STR_POOL_LIMIT) { if (need_lock) VM_UNLOCK(vm); return NULL; }  /* 锟斤拷锟斤拷:锟斤拷锟矫凤拷锟斤拷锟斤�?strdup */
    if (p->count * 2 >= p->cap) strpool_grow(vm, p);
    int i = (int)(fnv1a(s) & p->mask);
    while (p->slots[i]) {
        if (strcmp(p->slots[i], s) == 0) { if (need_lock) VM_UNLOCK(vm); return p->slots[i]; }
        i = (i + 1) & p->mask;
    }
    char *dup = strdup(s);
    p->slots[i] = dup;
    p->count++;
    vm->used_mem += (double)strlen(dup) + 1.0;
    int over = (vm->limit_mem > 0.0 && vm->used_mem > vm->limit_mem);
    if (need_lock) VM_UNLOCK(vm);
    if (over) limit_abort(vm, "memory", vm->used_mem, vm->limit_mem);
    return dup;
}

ArrayObj *vm_pool_slot(VM *vm, int idx) {
    if (idx >= 0 && idx < 4096) return &vm->arrays[idx];
    return &vm->arrays_big[idx - 4096];
}

int vm_array_new(VM *vm) {
    VM_LOCK(vm);
    int idx = -1;
    if (vm->array_free_n > 0) {
        idx = vm->array_free_list[--vm->array_free_n];
        ArrayObj *rf = vm_pool_slot(vm, idx);
        rf->items = NULL; rf->count = 0; rf->cap = 0;
    } else if (vm->arrayCount < 4096) {
        idx = vm->arrayCount++;
        vm_pool_slot(vm, idx)->items = NULL;
        vm_pool_slot(vm, idx)->count = 0;
        vm_pool_slot(vm, idx)->cap = 0;
    } else {
        /* 锟斤拷态锟斤拷锟捷ｏ拷锟斤拷锟斤拷锟斤拷嵌 4096 锟斤拷锟斤拷 arrays_big */
        int big = vm->arrayCount - 4096;
        if (big >= vm->bigCap) {
            int newCap = vm->bigCap == 0 ? 1024 : vm->bigCap * 2;
            ArrayObj *nb = realloc(vm->arrays_big, (size_t)newCap * sizeof(ArrayObj));
            if (!nb) { VM_UNLOCK(vm); return -1; }
            vm->arrays_big = nb;
            memset(vm->arrays_big + vm->bigCap, 0, (size_t)(newCap - vm->bigCap) * sizeof(ArrayObj));
            vm->bigCap = newCap;
        }
        idx = vm->arrayCount++;
        vm->arrays_big[big].items = NULL;
        vm->arrays_big[big].count = 0;
        vm->arrays_big[big].cap = 0;
    }
    VM_UNLOCK(vm);
    if (vm->gc_enabled && vm->used_mem > vm->gc_threshold) vm->gc_pending = 1;
    return idx;
}

static int array_ensure(VM *vm, int idx, int need) {
    ArrayObj *a = vm_pool_slot(vm, idx);
    if (need <= ARRAY_INLINE_CAP && a->cap <= ARRAY_INLINE_CAP) {
        /* L1: small array inline - no heap allocation */
        if (a->cap == 0) a->items = a->inline_buf;
        if (a->cap < need) a->cap = need;
        return 1;
    }
    if (need > a->cap) {
        int nc = a->cap == 0 ? 4 : a->cap * 2;
        if (nc < need) nc = need;
        if (a->cap <= ARRAY_INLINE_CAP) {
            /* first heap growth: move inline data out */
            Value *ni = malloc((size_t)nc * sizeof(Value));
            if (!ni) return 0;
            if (a->count > 0 && a->items == a->inline_buf)
                memcpy(ni, a->inline_buf, (size_t)a->count * sizeof(Value));
            a->items = ni;
            a->cap = nc;
        } else {
            Value *nr = realloc(a->items, (size_t)nc * sizeof(Value));
            if (!nr) return 0;
            a->items = nr;
            a->cap = nc;
        }
    }
    return 1;
}

void vm_array_push(VM *vm, int idx, const Value *v) {
    if (idx < 0 || idx >= vm->arrayCount) return;
    /* Clone before growing: v may point into this array and realloc can move it. */
    Value tmp; tmp.type = VAL_NIL; tmp.ival = 0; tmp.fval = 0; tmp.sval = NULL;
    value_copy(&tmp, v);
    int need_lock = (vm->active_threads > 1);
    if (need_lock) VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, idx);
    if (!array_ensure(vm, idx, a->count + 1)) {
        value_free(&tmp);
        if (need_lock) VM_UNLOCK(vm);
        return;
    }
    /* Move the clone into the array; avoid a second string allocation. */
    a->items[a->count++] = tmp;
    vm->used_mem += (double)sizeof(Value);
    int over = (vm->limit_mem > 0.0 && vm->used_mem > vm->limit_mem);
    if (need_lock) VM_UNLOCK(vm);
    if (over) limit_abort(vm, "memory", vm->used_mem, vm->limit_mem);
}

/* batch push: one lock + one growth for n elements (array literal / dict literal build) */
static void vm_array_push_n(VM *vm, int idx, const Value *items, int n) {
    if (idx < 0 || idx >= vm->arrayCount || n <= 0) return;
    /* Clone the source range before growth: callers may pass a->items. */
    Value *tmp = malloc((size_t)n * sizeof(Value));
    if (!tmp) return;
    for (int i = 0; i < n; i++) {
        tmp[i].type = VAL_NIL; tmp[i].ival = 0; tmp[i].fval = 0; tmp[i].sval = NULL;
        value_copy(&tmp[i], &items[i]);
    }
    int need_lock = (vm->active_threads > 1);
    if (need_lock) VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, idx);
    if (!array_ensure(vm, idx, a->count + n)) {
        for (int i = 0; i < n; i++) value_free(&tmp[i]);
        free(tmp);
        if (need_lock) VM_UNLOCK(vm);
        return;
    }
    for (int i = 0; i < n; i++) a->items[a->count + i] = tmp[i];
    a->count += n;
    free(tmp);
    vm->used_mem += (double)n * sizeof(Value);
    int over = (vm->limit_mem > 0.0 && vm->used_mem > vm->limit_mem);
    if (need_lock) VM_UNLOCK(vm);
    if (over) limit_abort(vm, "memory", vm->used_mem, vm->limit_mem);
}

void vm_array_set(VM *vm, int idx, int i, const Value *v) {
    if (idx < 0 || idx >= vm->arrayCount || i < 0) return;
    /* Clone first so self-assignment and reallocating growth are safe. */
    Value tmp; tmp.type = VAL_NIL; tmp.ival = 0; tmp.fval = 0; tmp.sval = NULL;
    value_copy(&tmp, v);
    int need_lock = (vm->active_threads > 1);
    if (need_lock) VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, idx);
    if (i >= a->count) {
        int old_count = a->count;
        if (!array_ensure(vm, idx, i + 1)) {
            value_free(&tmp);
            if (need_lock) VM_UNLOCK(vm);
            return;
        }
        Value nil; nil.type = VAL_NIL; nil.ival = 0; nil.fval = 0; nil.sval = NULL;
        while (a->count <= i) value_copy(&a->items[a->count++], &nil);
        vm->used_mem += (double)(a->count - old_count) * sizeof(Value);
    }
    value_free(&a->items[i]);
    a->items[i] = tmp;
    if (need_lock) VM_UNLOCK(vm);
}

Value vm_array_get(VM *vm, int idx, int i) {
    Value out; out.type = VAL_NIL; out.ival = 0; out.fval = 0; out.sval = NULL;
    if (idx < 0 || idx >= vm->arrayCount || i < 0 || i >= vm_pool_slot(vm, idx)->count) return out;
    int need_lock = (vm->active_threads > 1);
    if (need_lock) VM_LOCK(vm);
    value_copy(&out, &vm_pool_slot(vm, idx)->items[i]);
    if (out.type == VAL_STRING && out.sval && out.ival != 1) {
        const char *np = vm_intern(vm, out.sval);
        if (np) { free(out.sval); out.sval = (char*)np; out.ival = 1; }
    }
    if (need_lock) VM_UNLOCK(vm);
    return out;
}

Value vm_array_pop(VM *vm, int idx) {
    Value out; out.type = VAL_NIL; out.ival = 0; out.fval = 0; out.sval = NULL;
    if (idx < 0 || idx >= vm->arrayCount || vm_pool_slot(vm, idx)->count <= 0) return out;
    VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, idx);
    out = a->items[--a->count];
    vm->used_mem -= (double)sizeof(Value);
    if (vm->used_mem < 0.0) vm->used_mem = 0.0;
    VM_UNLOCK(vm);
    return out;
}

int vm_array_len(VM *vm, int idx) {
    if (idx < 0 || idx >= vm->arrayCount) return 0;
    VM_LOCK(vm);
    int n = vm_pool_slot(vm, idx)->count;
    VM_UNLOCK(vm);
    return n;
}

/* ---------- 锟街碉拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟截ｏ拷锟斤拷锟斤拷�??key/value??---------- */
/* ---------- dict: parallel key/value array + open-addressing hash index (L1, O(1) get/set) ---------- */

/* key hash, consistent with val_eq(): int/float cross-type equality must collide */
static unsigned dict_hash_key(const Value *k) {
    switch (k->type) {
        case VAL_INT: {
            unsigned h = (unsigned)k->ival;
            h ^= h >> 16;
            return h * 2654435761u;
        }
        case VAL_FLOAT: {
            double d = k->fval;
            /* integral floats hash like the equal int (val_eq: 1 == 1.0) */
            if (d >= -2147483648.0 && d < 2147483648.0 && d == (double)(int)d) {
                unsigned h = (unsigned)(int)d;
                h ^= h >> 16;
                return h * 2654435761u;
            }
            uint64_t bits; memcpy(&bits, &d, sizeof bits);
            return (unsigned)(bits ^ (bits >> 32)) * 2654435761u;
        }
        case VAL_STRING: {
            const char *s = k->sval ? k->sval : "";
            unsigned h = 5381;
            while (*s) h = h * 33 + (unsigned char)*s++;
            return h;
        }
        case VAL_BOOL:  return k->ival ? 0x85ebca6bu : 0xc2b2ae35u;
        case VAL_NIL:   return 0x27d4eb2fu;
        case VAL_ARRAY:
        case VAL_DICT:
        case VAL_SET:   return (unsigned)k->ival * 2654435761u;
        default:        return 0;
    }
}

/* ensure the parallel DictHash array covers aidx (zeroed = index not built) */
static DictHash *dict_hash_ensure(VM *vm, int aidx) {
    if (aidx >= vm->dict_hashes_cap) {
        int nc = vm->dict_hashes_cap == 0 ? 64 : vm->dict_hashes_cap * 2;
        while (nc <= aidx) nc *= 2;
        DictHash *nh = realloc(vm->dict_hashes, (size_t)nc * sizeof(DictHash));
        if (!nh) return NULL;
        memset(nh + vm->dict_hashes_cap, 0, (size_t)(nc - vm->dict_hashes_cap) * sizeof(DictHash));
        vm->dict_hashes = nh;
        vm->dict_hashes_cap = nc;
    }
    return &vm->dict_hashes[aidx];
}

/* build table from the pair array (cap >= 2*pairs, so no grow during build) */
static void dict_hash_build(VM *vm, int aidx) {
    ArrayObj *a = vm_pool_slot(vm, aidx);
    DictHash *h = dict_hash_ensure(vm, aidx);
    if (!h) return;
    int pairs = a->count / 2;
    int cap = 4;
    while (cap < pairs * 2) cap *= 2;
    free(h->slots);
    h->slots = malloc((size_t)cap * sizeof(DictSlot));
    if (!h->slots) { h->cap = h->mask = h->count = 0; return; }
    for (int i = 0; i < cap; i++) h->slots[i].pair_idx = -1;
    h->cap = cap; h->mask = cap - 1; h->count = 0;
    for (int i = 0; i + 1 < a->count; i += 2) {
        unsigned hk = dict_hash_key(&a->items[i]);
        int slot = (int)(hk & (unsigned)h->mask);
        while (h->slots[slot].pair_idx >= 0) slot = (slot + 1) & h->mask;
        h->slots[slot].pair_idx = i;
        h->slots[slot].hash = hk;
        h->count++;
    }
}

/* grow table (rehash using cached hashes; keys not needed) */
static void dict_hash_grow(DictHash *h) {
    int ncap = h->cap * 2;
    DictSlot *ns = malloc((size_t)ncap * sizeof(DictSlot));
    if (!ns) return;
    for (int i = 0; i < ncap; i++) ns[i].pair_idx = -1;
    for (int i = 0; i < h->cap; i++) {
        if (h->slots[i].pair_idx < 0) continue;
        int slot = (int)(h->slots[i].hash & (unsigned)(ncap - 1));
        while (ns[slot].pair_idx >= 0) slot = (slot + 1) & (ncap - 1);
        ns[slot] = h->slots[i];
    }
    free(h->slots);
    h->slots = ns;
    h->cap = ncap;
    h->mask = ncap - 1;
}

static void dict_hash_insert(DictHash *h, ArrayObj *a, int pair_idx, unsigned hk) {
    (void)a;
    if (h->count * 10 >= h->cap * 7) dict_hash_grow(h);
    int slot = (int)(hk & (unsigned)h->mask);
    while (h->slots[slot].pair_idx >= 0) slot = (slot + 1) & h->mask;
    h->slots[slot].pair_idx = pair_idx;
    h->slots[slot].hash = hk;
    h->count++;
}

/* probe: returns pair index (0,2,4..) or -1 */
static int dict_hash_find(DictHash *h, ArrayObj *a, const Value *key, unsigned hk) {
    if (!h->slots) return -1;
    int slot = (int)(hk & (unsigned)h->mask);
    for (;;) {
        int pi = h->slots[slot].pair_idx;
        if (pi < 0) return -1;
        if (h->slots[slot].hash == hk && val_eq(&a->items[pi], key)) return pi;
        slot = (slot + 1) & h->mask;
    }
}

Value vm_dict_get(VM *vm, int aidx, const Value *key) {
    Value nil; nil.type = VAL_NIL; nil.ival = 0; nil.fval = 0; nil.sval = NULL;
    if (aidx < 0 || aidx >= vm->arrayCount) return nil;
    VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, aidx);
    DictHash *h = dict_hash_ensure(vm, aidx);
    if (!h) { VM_UNLOCK(vm); return nil; }
    if (!h->slots && a->count > 0) dict_hash_build(vm, aidx);
    int i = h->slots ? dict_hash_find(h, a, key, dict_hash_key(key)) : -1;
    Value out = nil;
    if (i >= 0) {
        /* Always return an owned copy. A shallow string alias becomes unsafe when
           interning is unavailable (pool limit) or the caller later frees it. */
        value_copy(&out, &a->items[i + 1]);
        if (out.type == VAL_STRING && out.sval && out.ival != 1) {
            const char *np = vm_intern(vm, out.sval);
            if (np) { free(out.sval); out.sval = (char*)np; out.ival = 1; }
        }
    }
    VM_UNLOCK(vm);
    return out;
}

void vm_dict_set(VM *vm, int aidx, const Value *key, const Value *val) {
    if (aidx < 0 || aidx >= vm->arrayCount) return;
    /* Clone inputs before any array growth or destination release. This covers
       self-assignment (d[k] = d[k]) and keys/values backed by the same array. */
    Value key_copy; key_copy.type = VAL_NIL; key_copy.ival = 0; key_copy.fval = 0; key_copy.sval = NULL;
    Value val_copy; val_copy.type = VAL_NIL; val_copy.ival = 0; val_copy.fval = 0; val_copy.sval = NULL;
    value_copy(&key_copy, key);
    value_copy(&val_copy, val);
    VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, aidx);
    DictHash *h = dict_hash_ensure(vm, aidx);
    if (!h) { value_free(&key_copy); value_free(&val_copy); VM_UNLOCK(vm); return; }
    if (!h->slots && a->count > 0) dict_hash_build(vm, aidx);
    unsigned hk = dict_hash_key(&key_copy);
    int i = h->slots ? dict_hash_find(h, a, &key_copy, hk) : -1;
    if (i >= 0) {
        value_free(&a->items[i + 1]);
        a->items[i + 1] = val_copy;
        val_copy.type = VAL_NIL; val_copy.sval = NULL;
    } else {
        int pair_idx = a->count;
        vm_array_push(vm, aidx, &key_copy);
        vm_array_push(vm, aidx, &val_copy);
        if (!h->slots) dict_hash_build(vm, aidx);  /* first pair(s): table now includes them */
        else dict_hash_insert(h, a, pair_idx, hk);
    }
    value_free(&key_copy);
    value_free(&val_copy);
    VM_UNLOCK(vm);
}
bool vm_dict_remove(VM *vm, int aidx, const Value *key) {
    if (aidx < 0 || aidx >= vm->arrayCount) return false;
    VM_LOCK(vm);
    ArrayObj *a = vm_pool_slot(vm, aidx);
    DictHash *h = dict_hash_ensure(vm, aidx);
    if (!h) { VM_UNLOCK(vm); return false; }
    if (!h->slots && a->count > 0) dict_hash_build(vm, aidx);
    int i = h->slots ? dict_hash_find(h, a, key, dict_hash_key(key)) : -1;
    if (i < 0) { VM_UNLOCK(vm); return false; }
    value_free(&a->items[i]);
    value_free(&a->items[i + 1]);
    /* shift pairs left (iteration order stable), then rebuild the index */
    for (int k = i; k + 2 < a->count; k++) {
        a->items[k] = a->items[k + 2];
        a->items[k + 1] = a->items[k + 3];
    }
    a->count -= 2;
    vm->used_mem -= 2.0 * (double)sizeof(Value);
    if (vm->used_mem < 0.0) vm->used_mem = 0.0;
    if (a->count > 0) dict_hash_build(vm, aidx);
    else { free(h->slots); h->slots = NULL; h->cap = h->mask = h->count = 0; }
    VM_UNLOCK(vm);
    return true;
}

/* thread-safe number formatting (no msvcrt, usable from worker threads) */
static void vts_int(char *buf, int bufsz, long long v) {
    char tmp[32]; int n = 0;
    unsigned long long u;
    if (v < 0) u = (unsigned long long)(-(v + 1)) + 1ULL; else u = (unsigned long long)v;
    do { tmp[n++] = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
    int pos = 0;
    if (v < 0 && pos < bufsz - 1) buf[pos++] = '-';
    while (n > 0 && pos < bufsz - 1) buf[pos++] = tmp[--n];
    buf[pos] = '\0';
}
static void vts_double(char *buf, int bufsz, double dv) {
    if (dv != dv) { const char *s = "nan"; int i = 0; for (; s[i] && i < bufsz - 1; i++) buf[i] = s[i]; buf[i] = '\0'; return; }
    if (dv == 0.0) { vts_int(buf, bufsz, 0); return; }   /* also -0.0 */
    /* integral floats that fit int64 print as integers (range-checked BEFORE the cast) */
    if (dv >= -9223372036854775808.0 && dv < 9223372036854775808.0 && dv == (double)(long long)dv) {
        vts_int(buf, bufsz, (long long)dv);
        return;
    }
    if (dv >= 1e15 || dv <= -1e15) {                     /* huge: scientific, no int64 cast overflow */
        snprintf(buf, bufsz, "%.6g", dv);
        return;
    }
    long long ip = (long long)dv;
    double fp = dv - (double)ip;
    if (fp < 0) fp = -fp;
    char ib[32]; vts_int(ib, sizeof(ib), ip);
    int pos = 0;
    for (int i = 0; ib[i] && pos < bufsz - 1; i++) buf[pos++] = ib[i];
    if (pos < bufsz - 1) buf[pos++] = '.';
    long long frac = (long long)(fp * 1000000.0 + 0.5);
    if (frac == 0) { buf[pos] = '\0'; return; }
    char fb[32]; snprintf(fb, sizeof(fb), "%06lld", frac);  /* keep leading zeros: 5000 -> "005000" */
    int flen = (int)strlen(fb);
    while (flen > 0 && fb[flen - 1] == '0') fb[--flen] = '\0';  /* trim trailing zeros only */
    if (flen == 0) { buf[pos] = '\0'; return; }
    for (int i = 0; i < flen && pos < bufsz - 1; i++) buf[pos++] = fb[i];
    buf[pos] = '\0';
}
void value_to_string(VM *vm, const Value *v, char *buf, int bufsz, int depth) {
    if (v->type == VAL_STRING) { int sl = (int)strlen(v->sval ? v->sval : ""); if (sl > bufsz - 1) sl = bufsz - 1; memcpy(buf, v->sval ? v->sval : "", sl); buf[sl] = '\0'; }
    else if (v->type == VAL_INT) vts_int(buf, bufsz, (long long)v->ival);
    else if (v->type == VAL_FLOAT) vts_double(buf, bufsz, v->fval);
    else if (v->type == VAL_BOOL) { const char *s = v->ival ? "true" : "false"; int i = 0; for (; s[i] && i < bufsz - 1; i++) buf[i] = s[i]; buf[i] = '\0'; }
    else if (v->type == VAL_NIL) { buf[0] = 'n'; buf[1] = 'i'; buf[2] = 'l'; buf[3] = '\0'; }
    else if (v->type == VAL_SET) {
        int sidx = v->ival;
        if (sidx >= 0 && sidx < vm->setCount) {
            SetObj *s = vm_set_slot(vm, sidx);
            if (!s) snprintf(buf, bufsz, "set(?)");
            else if (s->kind == 0) snprintf(buf, bufsz, "set(%d)", s->iCount + s->count);
            else if (s->kind == 1) snprintf(buf, bufsz, "set(%s)", builtin_set_name(s->nameIdx));
            else snprintf(buf, bufsz, "set(%s interval)", builtin_set_name(s->nameIdx));
        } else snprintf(buf, bufsz, "set(?)");
    }
    else if (v->type == VAL_ARRAY) {
        int used = 0;
        used += snprintf(buf + used, bufsz - used, "[");
        if (depth < 2) {
            int aidx = v->ival - 1;
            if (aidx >= 0 && aidx < vm->arrayCount) {
                VM_LOCK(vm);
                ArrayObj *a = vm_pool_slot(vm, aidx);
                for (int i = 0; i < a->count; i++) {
                    char tmp[256];
                    value_to_string(vm, &a->items[i], tmp, sizeof(tmp), depth + 1);
                    int need = (i > 0 ? 2 : 0) + (int)strlen(tmp) + 1;
                    if (used + need < bufsz) {
                        if (i > 0) used += snprintf(buf + used, bufsz - used, ", ");
                        used += snprintf(buf + used, bufsz - used, "%s", tmp);
                    } else break;
                }
                VM_UNLOCK(vm);
            }
        }
        snprintf(buf + used, bufsz - used, "]");
    }
    else if (v->type == VAL_DICT) {
        int used = 0;
        used += snprintf(buf + used, bufsz - used, "{");
        if (depth < 2) {
            int aidx = v->ival - 1;
            if (aidx >= 0 && aidx < vm->arrayCount) {
                VM_LOCK(vm);
                ArrayObj *a = vm_pool_slot(vm, aidx);
                int pairs = a->count / 2;
                for (int i = 0; i < pairs; i++) {
                    char k[128], vv[256];
                    value_to_string(vm, &a->items[i * 2], k, sizeof(k), depth + 1);
                    value_to_string(vm, &a->items[i * 2 + 1], vv, sizeof(vv), depth + 1);
                    int need = (i > 0 ? 2 : 0) + (int)strlen(k) + 2 + (int)strlen(vv) + 1;
                    if (used + need < bufsz) {
                        if (i > 0) used += snprintf(buf + used, bufsz - used, ", ");
                        used += snprintf(buf + used, bufsz - used, "%s: %s", k, vv);
                    } else break;
                }
                VM_UNLOCK(vm);
            }
        }
        snprintf(buf + used, bufsz - used, "}");
    }
    else snprintf(buf, bufsz, "<unknown>");
}

void vm_value_to_string(VM *vm, const Value *v, char *buf, int bufsz) {
    value_to_string(vm, v, buf, bufsz, 0);
}

/* resource-limit abort: stop all threads and exit (no locks; flags only) */
void limit_abort(VM *vm, const char *what, double used, double limit) {
    fprintf(stderr, "\n[resource limit] %s used %.0f, exceeds declared limit %.0f, auto-exit.\n", what, used, limit);
    fprintf(stderr, "[dbg-abort] vm_used=%.1f vm_lim=%.1f\\n", vm->used_mem, vm->limit_mem);
    vm->last_error = 1;
    vm->running = false;
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (tt) { tt->stop_flag = true; tt->paused = false; }
    }
    VmThread *cur = vm_get_cur_thread();
    if (cur) cur->stop_flag = true;
}

extern void record_load_from_file(VM *vm, const char *path);
extern void record_save_to_file(VM *vm, const char *path);

/* ---------- dynamic globals ---------- */
/* grow the globals + be_bound arrays to fit index `need` (0-based); zero-fills new slots.
   Must be called with VM_LOCK held while other threads may be running (L_STORE_GLOBAL / L_BE / vm_throw). */
void vm_global_grow(VM *vm, int need) {
    /* 鎵╁绉诲姩 globals 鏁扮粍鎸囬拡锛岄』鐙崰鍏ㄩ儴 16 鎶婂垎鐗囬攣�?
       璋冪敤鏂瑰繀椤诲厛閲婃斁鑷繁鐨勫垎鐗囷紙閬垮厤浜ゅ弶绛夊緟姝婚攣锛夛紝grow 鍚庨噸鍙栥€?
       global_locks[0] 鍒ょ┖锛歷m_init 鏃╂湡锛堥攣鍒濆鍖栧墠锛夎皟鐢ㄦ椂淇濇寔鏃犻攣�?*/
    if (need < 0) need = 0;
    if (need < vm->globalCap) return;
    if (vm->active_threads > 1 && vm->global_locks[0]) {
        for (int i = 0; i < VM_GLOBAL_SHARDS; i++)
            im_mutex_lock((ImMutex*)vm->global_locks[i]);
    }
    int nc = vm->globalCap == 0 ? 64 : vm->globalCap;
    while (nc <= need) nc *= 2;
    vm->globals = realloc(vm->globals, (size_t)nc * sizeof(GlobalSlot));
    for (int i = vm->globalCap; i < nc; i++) {
        vm->globals[i].name = NULL;
        vm->globals[i].val.type = VAL_NIL;
        vm->globals[i].val.ival = 0;
        vm->globals[i].val.fval = 0;
        vm->globals[i].val.sval = NULL;
    }
    vm->be_bound = realloc(vm->be_bound, (size_t)nc * sizeof(int));
    for (int i = vm->be_bound_cap; i < nc; i++) vm->be_bound[i] = 0;
    vm->globalCap = nc;
    vm->be_bound_cap = nc;
    if (vm->active_threads > 1 && vm->global_locks[0]) {
        for (int i = VM_GLOBAL_SHARDS - 1; i >= 0; i--)
            im_mutex_unlock((ImMutex*)vm->global_locks[i]);
    }
}

/* swap the VM globals for an independent copy of the current table (names strdup'd, values shallow-copied so pool refs stay shared).
   Used by nested vm_exec runs so the outer table is untouched. Caller frees the old table after the nested run. */
void vm_global_clone(VM *vm) {
    GlobalSlot *src = vm->globals;
    int sc = vm->globalCount;
    int *sbe = vm->be_bound;
    int sbcap = vm->be_bound_cap;
    vm->globals = NULL; vm->globalCount = 0; vm->globalCap = 0;
    vm->be_bound = NULL; vm->be_bound_cap = 0;
    vm_global_grow(vm, sc - 1);
    for (int i = 0; i < sc; i++) {
        vm->globals[i] = src[i];
        vm->globals[i].name = src[i].name ? strdup(src[i].name) : NULL;
    }
    for (int i = 0; i < sbcap && i < vm->globalCap; i++) vm->be_bound[i] = sbe[i];
    vm->globalCount = sc;
}

void vm_init(VM *vm) {
    vm->limit_vram = 0;
    /* mark-sweep GC state */
    vm->gc_enabled = 0; vm->gc_pending = 0; vm->gc_threshold = 0;
    vm->gc_stop = 0; vm->gc_parked = 0;
    vm->array_free_list = NULL; vm->array_free_n = vm->array_free_cap = 0;
    vm->set_free_list = NULL; vm->set_free_n = vm->set_free_cap = 0;
    vm->gc_amark = NULL; vm->gc_amark_cap = 0;
    vm->gc_smark = NULL; vm->gc_smark_cap = 0;
    vm->gc_work = NULL; vm->gc_work_count = 0; vm->gc_work_cap = 0;
    vm->gc_runs = 0; vm->gc_freed = 0;

    MMRESULT tr = timeBeginPeriod(1); /* 1ms timer resolution */
    fprintf(stderr, "[TBP] timeBeginPeriod(1) result=%u (0=OK)\n", (unsigned)tr);
    vm->code = NULL;
    vm->ip = 0;
    vm->sp = -1;
    vm->active_threads = 0;   /* 蹇呴』鍦?grow63 涔嬪墠锛歡row 渚濊禆瀹冨垽鏂槸鍚﹀姞閿?*/
    vm->globals = NULL;
    vm->globalCount = 0;
    vm->globalCap = 0;
    vm->be_bound = NULL;
    vm->be_bound_cap = 0;
    vm_global_grow(vm, 63);  /* initial 64 slots (preset sets + user globals) */
    vm->builtinCount = 0;
    for (int i = 0; i < 512; i++) vm->builtin_hash[i] = 0;
    vm->running = false;
    vm->step_mode = false;
    vm->print_hook = NULL;
    vm->gui_run = NULL;
    vm->arrayCount = 0;
    vm->arrays_big = NULL;
    vm->bigCap = 0;
    vm->str_pool = NULL;
    vm->debug_hook = NULL;
    vm->gui_pump = NULL;          /* 锟斤�?gui_mod 锟节达拷锟斤拷锟斤拷台锟斤拷锟斤拷锟斤拷; 未锟斤拷=锟斤�?GUI 锟斤拷锟斤拷 */
    vm->exec_start_ms = 0;
    vm->exec_timeout_ms = 120000;
    /* declare resource limits (0 = unlimited) */
    vm->limit_mem = 0;
    vm->limit_threads = 0;
    vm->limit_time = 0;
    vm->limit_inst = 0;
    vm->used_mem = 0;
    vm->active_threads = 0;
    vm->t_start = 0;
    vm->record_default_store =0;
    vm->record_meta = NULL;
    vm->record_meta_count =0;
    vm->record_names = NULL;
    vm->record_names_cap =0;
    vm->record_loaded_dict =0;
    vm->record_save_path = strdup("save.dat");
    vm->record_autosave_interval = 0;
    vm->record_last_autosave = 0;   /* 默锟斤拷120??*/
    /* 锟竭筹拷锟斤拷锟斤拷 */
    ImMutex *gl = im_mutex_new();
    vm->global_lock = gl;
    for (int _si = 0; _si < VM_GLOBAL_SHARDS; _si++) {
        ImMutex *gs = im_mutex_new();
        vm->global_locks[_si] = gs;
    }
    vm->str_pool = calloc(1, sizeof(struct StrPool));
    if (vm->str_pool) {
        vm->str_pool->cap = 256;
        vm->str_pool->mask = 255;
        vm->str_pool->slots = calloc(256, sizeof(char*));
    }
    vm->mutex_count = 0;
    for (int i = 0; i < VM_MAX_THREADS; i++) vm->threads[i] = NULL;
    for (int i = 0; i < VM_MAX_TASKS; i++) vm->tasks[i] = NULL;
    vm->task_count = 0;
    vm->sched_running = 0;
    vm->sched_thread = NULL;
    vm->ent_x = NULL; vm->ent_y = NULL; vm->ent_vx = NULL; vm->ent_vy = NULL;
    vm->ent_hp = NULL; vm->ent_kind = NULL; vm->ent_cap = 0; vm->ent_count = 0;
    vm->ent_buckets = NULL; vm->ent_free = NULL;
    vm->ent_free_head = -1; vm->ent_grid_dirty = 0;
    for (int i = 0; i < 64; i++) vm->mutexes[i] = NULL;
    /* set support */
    vm->sets = NULL;
    vm->setCount = 0;
    vm->setCap = 0;
        /* preset builtin set globals: N Z Z+ Z- Float1..9 float1..9, z(null) */
    {
        static const char *bn[24] = {
            "N","Z","Z+","Z-",
            "Float1","float1","Float2","float2","Float3","float3","Float4","float4",
            "Float5","float5","Float6","float6","Float7","float7","Float8","float8","Float9","float9","",""
        };
        for (int i = 0; i < 22 && vm->globalCount < vm->globalCap; i++) {
            int sidx = vm_set_new(vm);
            if (sidx < 0) break;
            SetObj *s = vm_set_slot(vm, sidx);
            s->kind = 1;
            s->nameIdx = i;
            vm->globals[vm->globalCount].name = strdup(bn[i]);
            vm->globals[vm->globalCount].val.type = VAL_SET;
            vm->globals[vm->globalCount].val.ival = sidx;
            vm->globalCount++;
        }
        if (vm->globalCount < vm->globalCap) {
            vm->globals[vm->globalCount].name = strdup("\xE7\xA9\xBA"); /* kong (empty set): UTF-8 (E7A9BA); GBK sources are transcoded to UTF-8 at load */
            vm->globals[vm->globalCount].val.type = VAL_NIL;
            vm->globalCount++;
        }
    }
    /* script debugger / safe-mode state (appended fields must be zeroed) */
    vm->dbg_active = 0;
    vm->dbg_pause = 0;
    vm->dbg_at_boundary = 0;
    vm->dbg_boundary_count = 0;
    vm->mod_bc_count = 0;
    vm->safe_mode = 0;
    vm->last_ignored_exc = NULL;
    vm->ignored_exc_count = 0;
    vm->dict_hashes = NULL;
    vm->dict_hashes_cap = 0;
    vm->last_error = 0;
    vm->mod_caps = -1; /* unrestricted by default (platform) */
    vm->modCount = 0;
}

void vm_load_bytecode(VM *vm, Bytecode *bc) {
    vm->code = bc;
    vm->ip = 0;
    /* fill global names from bytecode table (debug var / error reporting) */
    if (bc->global_names && bc->global_name_count > 0) {
        vm_global_grow(vm, bc->global_name_count - 1);
        for (int i = 0; i < bc->global_name_count; i++) {
            if (vm->globals[i].name) free(vm->globals[i].name);
            vm->globals[i].name = strdup(bc->global_names[i]);
        }
        if (bc->global_name_count > vm->globalCount) vm->globalCount = bc->global_name_count;
    }
    /* 锟斤拷锟皆凤拷锟斤拷锟街凤拷锟斤拷锟斤拷锟斤�?intern 锟斤拷锟芥（锟斤拷锟竭程碉拷锟斤�?锟斤拷锟斤拷锟斤拷锟斤拷只锟斤拷??*/
    if (bc->str_interned == NULL && bc->string_count > 0)
        bc->str_interned = calloc(bc->string_count, sizeof(char*));
}

void vm_free(VM *vm) {
    /* spi event bus subscriptions */
    if (vm->spi_subs) { for (int _si = 0; _si < vm->spi_sub_count; _si++) free(vm->spi_subs[_si].event); free(vm->spi_subs); vm->spi_subs = NULL; vm->spi_sub_count = vm->spi_sub_cap = 0; }
    /* stop task scheduler and reclaim virtual threads */
    if (vm->sched_running) {
        vm->sched_running = 0;
        if (vm->sched_thread) {
            im_thread_join(vm->sched_thread, 2000);
            im_thread_close(vm->sched_thread);
        }
        vm->sched_thread = NULL;
    }
    for (int i = 0; i < vm->task_count; i++) {
        VmThread *tk = vm->tasks[i];
        if (!tk) continue;
        if (tk->fiber_self) { DeleteFiber(tk->fiber_self); tk->fiber_self = NULL; }
        free(tk->exc_stack);
        free(tk->reg);
        free(tk->frame_code); free(tk->frame_ip); free(tk->frame_base); free(tk->frame_res); free(tk->frame_sp);
        for (int _mj = 0; _mj < tk->msg_cap; _mj++) { Value _mv = tk->msg_q[_mj]; if (_mv.type == VAL_STRING && _mv.ival != 1 && _mv.sval) free(_mv.sval); }
        free(tk->msg_q);
        if (tk->msg_lock) { im_mutex_free((ImMutex*)tk->msg_lock); }
        free(tk);
        vm->tasks[i] = NULL;
    }
    vm->task_count = 0;
    free(vm->ent_x); free(vm->ent_y); free(vm->ent_vx); free(vm->ent_vy);
    if (vm->ent_buckets) {
        EntBucket *bks = (EntBucket*)vm->ent_buckets;
        for (int bi = 0; bi < ENTITY_GRID_W * ENTITY_GRID_H; bi++) free(bks[bi].ids);
        free(vm->ent_buckets);
        vm->ent_buckets = NULL;
    }
    free(vm->ent_hp); free(vm->ent_kind);
    vm->ent_cap = 0; vm->ent_count = 0; vm->ent_free_head = -1;
    if (vm->record_meta) { free(vm->record_meta); vm->record_meta = NULL; }
    if (vm->record_names) { for (int i =0; i < vm->record_names_cap; i++) free(vm->record_names[i]); free(vm->record_names); vm->record_names = NULL; }
    free(vm->record_save_path); vm->record_save_path = NULL;
    for (int i = 0; i < vm->globalCount; i++) {
        free(vm->globals[i].name);
        value_free(&vm->globals[i].val);
    }
    free(vm->globals); vm->globals = NULL; vm->globalCap = 0;
    free(vm->be_bound); vm->be_bound = NULL; vm->be_bound_cap = 0;
    /* builtins[i].name 锟窖筹拷??锟斤拷锟街凤拷锟斤拷锟斤拷统一锟酵凤拷 */
    for (int i = 0; i < vm->arrayCount; i++) {
        ArrayObj *a = vm_pool_slot(vm, i);
        for (int j = 0; j < a->count; j++)
            value_free(&a->items[j]);
        if (a->cap > ARRAY_INLINE_CAP) free(a->items);  /* inline arrays live in the struct */
        a->items = NULL;
    }
    vm->arrayCount = 0;
    free(vm->arrays_big);
    vm->arrays_big = NULL;
    for (int i = 0; i < vm->setCount; i++) {
        SetObj *s = vm_set_slot(vm, i);
        if (s && s->kind == 0) {
            for (int j = 0; j < s->count; j++) value_free(&s->items[j]);
            free(s->items);
            free(s->comps);
            free(s->i64);
        }
    }
    free(vm->sets);
    vm->sets = NULL;
    vm->setCount = 0;
    vm->bigCap = 0;
    /* 锟竭筹拷锟斤拷锟斤拷锟斤拷锟斤拷 */
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        if (vm->threads[i]) {
            free(vm->threads[i]->reg);
            for (int _mi=0; _mi<vm->threads[i]->msg_cap; _mi++) { Value _mv=vm->threads[i]->msg_q[_mi]; if (_mv.type==VAL_STRING && _mv.ival!=1 && _mv.sval) free(_mv.sval); } free(vm->threads[i]->msg_q);
            if (vm->threads[i]->msg_lock) {
                im_mutex_free((ImMutex*)vm->threads[i]->msg_lock);
                free(vm->threads[i]->msg_lock);
            }
            free(vm->threads[i]);
            vm->threads[i] = NULL;
        }
    }
    if (vm->global_lock) {
        im_mutex_free((ImMutex*)vm->global_lock);
        free(vm->global_lock);
        vm->global_lock = NULL;
    }
    for (int _si = 0; _si < VM_GLOBAL_SHARDS; _si++) {
        if (vm->global_locks[_si]) {
            im_mutex_free((ImMutex*)vm->global_locks[_si]);
            free(vm->global_locks[_si]);
            vm->global_locks[_si] = NULL;
        }
    }
    free(vm->last_ignored_exc); vm->last_ignored_exc = NULL; vm->ignored_exc_count = 0;
    if (vm->dict_hashes) {
        for (int _di = 0; _di < vm->dict_hashes_cap; _di++) free(vm->dict_hashes[_di].slots);
        free(vm->dict_hashes);
        vm->dict_hashes = NULL;
        vm->dict_hashes_cap = 0;
    }
    /* mod-script bytecodes kept alive for mod threads */
    for (int i = 0; i < vm->mod_bc_count; i++) {
        if (vm->mod_bcs[i]) { bytecode_free(vm->mod_bcs[i]); free(vm->mod_bcs[i]); vm->mod_bcs[i] = NULL; }
    }
    vm->mod_bc_count = 0;
    for (int i = 0; i < vm->mutex_count; i++) {
        if (vm->mutexes[i]) {
            im_mutex_free((ImMutex*)vm->mutexes[i]);
            free(vm->mutexes[i]);
            vm->mutexes[i] = NULL;
        }
    }
    vm->mutex_count = 0;
    /* 锟街凤拷锟斤拷锟斤拷锟斤拷锟斤拷�??锟斤拷锟街凤拷锟斤拷锟斤拷锟斤拷值锟斤拷??值锟酵凤拷时锟斤拷锟斤拷锟斤拷锟街凤拷锟斤拷) */
    if (vm->str_pool) {
        for (int i = 0; i < vm->str_pool->cap; i++)
            if (vm->str_pool->slots[i]) free(vm->str_pool->slots[i]);
        free(vm->str_pool->slots);
        free(vm->str_pool);
        vm->str_pool = NULL;
    }
}

/* ---- builtin fast lookup: open addressing, linear probe ----
   vm->builtin_hash[slot] = builtin index + 1 (0 = empty). Names are interned. */
static unsigned builtin_hash_fn(const char *s) {
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h & 511;
}
int builtin_lookup(VM *vm, const char *name) {
    unsigned h = builtin_hash_fn(name);
    for (int i = 0; i < 512; i++) {
        int slot = (int)((h + (unsigned)i) & 511);
        int bi = vm->builtin_hash[slot] - 1;
        if (bi < 0) return -1;
        if (vm->builtins[bi].name == name || strcmp(vm->builtins[bi].name, name) == 0) return bi;
    }
    return -1;
}
static void builtin_insert(VM *vm, int bi) {
    unsigned h = builtin_hash_fn(vm->builtins[bi].name);
    for (int i = 0; i < 512; i++) {
        int slot = (int)((h + (unsigned)i) & 511);
        if (vm->builtin_hash[slot] == 0) { vm->builtin_hash[slot] = bi + 1; return; }
    }
}

void vm_register_builtin(VM *vm, const char *name, BuiltinFunc func) {
    if (vm->builtinCount < 512) {
        const char *np = vm_intern(vm, name);
        vm->builtins[vm->builtinCount].name = np ? (char*)np : strdup(name);
        vm->builtins[vm->builtinCount].func = func;
        vm->builtins[vm->builtinCount].flags = 0;
        vm->builtinCount++;
        builtin_insert(vm, vm->builtinCount - 1);
    }
}

/* dangerous builtin: executes commands / touches fs / network / process control /
   code injection. Blocked when vm->safe_mode is set (code-injection guard). */
void vm_register_builtin_full(VM *vm, const char *name, BuiltinFunc func, int flags, int since) {
    if (vm->builtinCount < 512) {
        const char *np = vm_intern(vm, name);
        vm->builtins[vm->builtinCount].name = np ? (char*)np : strdup(name);
        vm->builtins[vm->builtinCount].func = func;
        vm->builtins[vm->builtinCount].flags = flags;
        vm->builtins[vm->builtinCount].since = since;
        vm->builtinCount++;
        builtin_insert(vm, vm->builtinCount - 1);
    }
}
void vm_register_builtin_safe(VM *vm, const char *name, BuiltinFunc func) {
    vm_register_builtin_full(vm, name, func, 1, 0); /* legacy: dangerous, no cap domain */
}
/* C mod meta (optional): declares id/version/api_min/caps for the SPI registry */
void vm_register_mod(VM *vm, const char *id, int version, int api_min, int caps) {
    if (vm->modCount < 32 && id && id[0]) {
        snprintf(vm->mods[vm->modCount].id, sizeof(vm->mods[0].id), "%s", id);
        vm->mods[vm->modCount].version = version;
        vm->mods[vm->modCount].api_min = api_min;
        vm->mods[vm->modCount].caps = caps;
        vm->modCount++;
    }
}

#ifdef _WIN32
unsigned __stdcall thread_entry(LPVOID arg);
#else
void *thread_entry(void *arg);
#endif
VmThread *vm_task_create(VM *vm, Bytecode *root, int tidx, VmThread *t, int argc);
#ifdef _WIN32
unsigned __stdcall task_scheduler_entry(LPVOID arg);
#else
void *task_scheduler_entry(void *arg);
#endif

/* ---------- set support ---------- */
static SetObj *vm_set_slot(VM *vm, int idx) {
    if (idx < 0 || idx >= vm->setCount) return NULL;
    return &vm->sets[idx];
}
int vm_set_new(VM *vm) {
    VM_LOCK(vm);
    if (vm->set_free_n > 0) {
        int fi = vm->set_free_list[--vm->set_free_n];
        SetObj *rf = vm_set_slot(vm, fi);
        memset(rf, 0, sizeof(SetObj));
        VM_UNLOCK(vm);
        return fi;
    }
    if (vm->setCount >= 65536) {
        fprintf(stderr, "[set] pool exhausted (65536 sets) - set allocation dropped\n");
        VM_UNLOCK(vm);
        return -1;
    }
    if (vm->setCount >= vm->setCap) {
        int nc = vm->setCap == 0 ? 64 : vm->setCap * 2;
        SetObj *ns = realloc(vm->sets, (size_t)nc * sizeof(SetObj));
        if (!ns) { VM_UNLOCK(vm); return -1; }
        vm->sets = ns;
        vm->setCap = nc;
    }
    int idx = vm->setCount++;
    SetObj *s = &vm->sets[idx];
    memset(s, 0, sizeof(SetObj));
    s->kind = 0;
    s->nameIdx = -1;
    s->loInc = s->hiInc = 1;
    s->comps = NULL;
    s->compCount = s->compCap = 0;
    VM_UNLOCK(vm);
    return idx;
}
static void vm_set_add(VM *vm, int idx, Value *v) {
    SetObj *s = vm_set_slot(vm, idx);
    if (!s || s->kind != 0) return;
    if (v->type == VAL_INT) {
        for (int i = 0; i < s->iCount; i++)
            if (s->i64[i] == v->ival) return;
        if (s->iCount >= s->iCap) {
            int nc = s->iCap == 0 ? 8 : s->iCap * 2;
            long long *ni = realloc(s->i64, (size_t)nc * sizeof(long long));
            if (!ni) return;
            s->i64 = ni;
            s->iCap = nc;
        }
        s->i64[s->iCount++] = v->ival;
        return;
    }
    for (int i = 0; i < s->count; i++)
        if (val_eq(&s->items[i], v)) return; /* dedup */
    if (s->count >= s->cap) {
        int nc = s->cap == 0 ? 8 : s->cap * 2;
        Value *ni = realloc(s->items, (size_t)nc * sizeof(Value));
        if (!ni) return;
        s->items = ni;
        s->cap = nc;
    }
    Value c = *v;
    if (c.type == VAL_STRING) {
        const char *np = vm_intern(vm, c.sval ? c.sval : "");
        if (np) { c.sval = (char*)np; c.ival = 1; }
    }
    s->items[s->count++] = c;
}
static void vm_set_add_comp(VM *vm, int idx, SetObj *src) {
    SetObj *s = vm_set_slot(vm, idx);
    if (!s || s->kind != 0 || !src) return;
    if (src->kind == 0) {
        for (int i = 0; i < src->count; i++) vm_set_add(vm, idx, &src->items[i]);
        for (int i = 0; i < src->compCount; i++) {
            SetComp c = src->comps[i];
            if (s->compCount >= s->compCap) {
                int nc = s->compCap == 0 ? 4 : s->compCap * 2;
                SetComp *ni = realloc(s->comps, (size_t)nc * sizeof(SetComp));
                if (!ni) return;
                s->comps = ni;
                s->compCap = nc;
            }
            s->comps[s->compCount++] = c;
        }
        return;
    }
    if (src->nameIdx < 0) return;
    if (s->compCount >= s->compCap) {
        int nc = s->compCap == 0 ? 4 : s->compCap * 2;
        SetComp *ni = realloc(s->comps, (size_t)nc * sizeof(SetComp));
        if (!ni) return;
        s->comps = ni;
        s->compCap = nc;
    }
    SetComp c;
    c.nameIdx = src->nameIdx;
    if (src->kind == 1) {
        c.lo = 0; c.hi = 0; c.loInc = 0; c.hiInc = 0;
    } else {
        c.lo = src->lo; c.hi = src->hi;
        c.loInc = src->loInc; c.hiInc = src->hiInc;
    }
    s->comps[s->compCount++] = c;
}
static const char *builtin_set_name(int bi) {
    static const char *names[25] = {
        "N","Z","Z+","Z-",
        "Float1","float1","Float2","float2","Float3","float3","Float4","float4",
        "Float5","float5","Float6","float6","Float7","float7","Float8","float8","Float9","float9","?","?","R"
    };
    if (bi < 0 || bi > 24) return "?";
    return names[bi];
}
static int builtin_set_index(const char *name) {
    if (!name) return -1;
    if (strcmp(name, "N") == 0) return 0;
    if (strcmp(name, "Z") == 0) return 1;
    if (strcmp(name, "Z+") == 0) return 2;
    if (strcmp(name, "Z-") == 0) return 3;
    if (strcmp(name, "R") == 0) return 24;
    if ((name[0] == 'F' || name[0] == 'f') && strlen(name) == 6) {
        if (strncmp(name + 1, "loat", 4) == 0) {
            int frac = name[5] - '0';
            if (frac >= 1 && frac <= 9) return 4 + (frac - 1) * 2 + (name[0] == 'f' ? 1 : 0);
        }
    }
    return -1;
}
static int val_is_integer(const Value *x) {
    if (x->type == VAL_INT) return 1;
    if (x->type == VAL_FLOAT) {
        double d = x->fval;
        return d < 9.0e18 && d == (double)(long long)d;
    }
    return 0;
}
static int builtin_contains(int bi, const Value *x) {
    if (x->type != VAL_INT && x->type != VAL_FLOAT) return 0;
    double d = (x->type == VAL_INT) ? (double)x->ival : x->fval;
    if (isnan(d)) return 0;
    int isInt = val_is_integer(x);
    switch (bi) {
        case 0: return isInt && d >= 0;                 /* N */
        case 1: return isInt;                           /* Z */
        case 2: return isInt && d > 0;                  /* Z+ */
        case 3: return isInt && d < 0;                  /* Z- */
        default: break;
    }
    if (bi == 24) return 1; /* R: all reals */
    if (bi < 4 || bi > 23) return 0;
    int frac = (bi - 4) / 2 + 1;
    int upper = (bi - 4) % 2; /* 1=floatN(to-N, incl ints) 0=FloatN(exactly-N, no ints) */
    if (!isInt) {
        double scaled = d;
        for (int i = 0; i < frac; i++) scaled *= 10.0;
        double r = round(scaled);
        if (fabs(scaled - r) < 1e-6) {
            if (upper) return 1;
            double prev = d;
            for (int i = 0; i < frac - 1; i++) prev *= 10.0;
            double rp = round(prev);
            return fabs(prev - rp) > 1e-6 ? 1 : 0;
        }
        return 0;
    }
    return upper ? 1 : 0;
}
static int set_contains(VM *vm, int sidx, const Value *x) {
    SetObj *s = vm_set_slot(vm, sidx);
    if (!s) return 0;
    if (s->kind == 0) {
        if (x->type == VAL_INT) {
            for (int i = 0; i < s->iCount; i++)
                if (s->i64[i] == x->ival) return 1;
        }
        for (int i = 0; i < s->count; i++)
            if (val_eq(&s->items[i], x)) return 1;
        for (int i = 0; i < s->compCount; i++) {
            SetComp *c = &s->comps[i];
            if (c->nameIdx < 0) continue;
            if (!builtin_contains(c->nameIdx, x)) continue;
            if (c->lo == 0 && c->hi == 0 && c->loInc == 0 && c->hiInc == 0)
                return 1; /* named set component: no bounds */
            double d = val_as_double((Value*)x);
            if (d < c->lo || (d == c->lo && !c->loInc)) continue;
            if (d > c->hi || (d == c->hi && !c->hiInc)) continue;
            return 1;
        }
        return 0;
    }
    if (s->nameIdx < 0) return 0;
    if (s->kind == 1) return builtin_contains(s->nameIdx, x);
    if (s->kind == 2) {
        if (!builtin_contains(s->nameIdx, x)) return 0;
        double d = val_as_double((Value*)x);
        if (d < s->lo || (d == s->lo && !s->loInc)) return 0;
        if (d > s->hi || (d == s->hi && !s->hiInc)) return 0;
        return 1;
    }
    return 0;
}
static int builtin_subset(int x, int y) {
    if (x == y) return 1;
    if (y == 24) return x <= 23;                            /* everything is a subset of R */
    if (x == 24) return 0;                                  /* R is not a proper subset of anything */
    if (y == 1) return x == 0 || x == 2 || x == 3;          /* N/Z+/Z- are subsets of Z */
    if (y == 0) return x == 2;                              /* Z+ subset of N */
    if (x >= 4 && x <= 23 && y >= 4 && y <= 23) {
        int xfrac = (x - 4) / 2 + 1, xupper = (x - 4) % 2;
        int yfrac = (y - 4) / 2 + 1, yupper = (y - 4) % 2;
        if (yupper == 1 && xfrac <= yfrac) return 1;        /* floatM contains floatN/FloatN when M>=N */
    }
    return 0;
}

static void vm_set_free_objs(VM *vm, int sidx) {
    SetObj *s = vm_set_slot(vm, sidx);
    if (!s) return;
    for (int i = 0; i < s->count; i++) value_free(&s->items[i]);
    free(s->items); s->items = NULL; s->count = 0; s->cap = 0;
    free(s->i64); s->i64 = NULL; s->iCount = 0; s->iCap = 0;
    free(s->comps); s->comps = NULL; s->compCount = 0; s->compCap = 0;
}
static int set_comp_equal(const SetComp *x, const SetComp *y) {
    return x->nameIdx == y->nameIdx && x->lo == y->lo && x->hi == y->hi &&
           x->loInc == y->loInc && x->hiInc == y->hiInc;
}
static void vm_set_add_comp_dedup(VM *vm, int dst, const SetComp *c) {
    SetObj *s = vm_set_slot(vm, dst);
    if (!s) return;
    for (int i = 0; i < s->compCount; i++)
        if (set_comp_equal(&s->comps[i], c)) return;
    if (s->compCount >= s->compCap) {
        int nc = s->compCap == 0 ? 4 : s->compCap * 2;
        SetComp *ni = realloc(s->comps, (size_t)nc * sizeof(SetComp));
        if (!ni) return;
        s->comps = ni;
        s->compCap = nc;
    }
    s->comps[s->compCount++] = *c;
}
static void comp_range(const SetComp *c, double *lo, double *hi, int *loInc, int *hiInc) {
    if (c->lo == 0 && c->hi == 0 && c->loInc == 0 && c->hiInc == 0) {
        switch (c->nameIdx) {
            case 0: *lo = 0; *hi = 1e308; *loInc = 1; *hiInc = 0; break;
            case 1: *lo = -1e308; *hi = 1e308; *loInc = 0; *hiInc = 0; break;
            case 2: *lo = 1; *hi = 1e308; *loInc = 1; *hiInc = 0; break;
            case 3: *lo = -1e308; *hi = -1; *loInc = 0; *hiInc = 1; break;
            default: *lo = -1e308; *hi = 1e308; *loInc = 0; *hiInc = 0; break;
        }
        return;
    }
    *lo = c->lo; *hi = c->hi; *loInc = c->loInc; *hiInc = c->hiInc;
}
/* 0=cannot decide, 1=out, 2=empty */
static int comp_intersect(const SetComp *ca, const SetComp *cb, SetComp *out) {
    int subAB = builtin_subset(ca->nameIdx, cb->nameIdx);
    int subBA = builtin_subset(cb->nameIdx, ca->nameIdx);
    if (!subAB && !subBA) return 2;
    int bi = subAB ? ca->nameIdx : cb->nameIdx;
    double loA, hiA, loB, hiB; int liA, hiA2, liB, hiB2;
    comp_range(ca, &loA, &hiA, &liA, &hiA2);
    comp_range(cb, &loB, &hiB, &liB, &hiB2);
    double lo, hi; int li, hi2;
    if (loA > loB) { lo = loA; li = liA; }
    else if (loA < loB) { lo = loB; li = liB; }
    else { lo = loA; li = liA && liB; }
    if (hiA < hiB) { hi = hiA; hi2 = hiA2; }
    else if (hiA > hiB) { hi = hiB; hi2 = hiB2; }
    else { hi = hiA; hi2 = hiA2 && hiB2; }
    if (lo > hi || (lo == hi && !(li && hi2))) return 2;
    out->nameIdx = bi;
    out->lo = lo; out->hi = hi;
    out->loInc = li; out->hiInc = hi2;
    return 1;
}
/* treat a kind1/kind2 set as a single component */
static int set_as_comp(SetObj *s, SetComp *out) {
    if (s->kind == 0) return 0;
    out->nameIdx = s->nameIdx;
    if (s->kind == 1) {
        out->lo = 0; out->hi = 0; out->loInc = 0; out->hiInc = 0;
    } else {
        out->lo = s->lo; out->hi = s->hi;
        out->loInc = s->loInc; out->hiInc = s->hiInc;
    }
    return 1;
}
static int set_union(VM *vm, int aidx, int bidx) {
    SetObj *a = vm_set_slot(vm, aidx), *b = vm_set_slot(vm, bidx);
    if (!a || !b) return -1;
    int n = vm_set_new(vm);
    if (n < 0) return -1;
    a = vm_set_slot(vm, aidx);
    b = vm_set_slot(vm, bidx);
    if (!a || !b) return -1;
    for (int i = 0; i < a->iCount; i++) {
        Value v; v.type = VAL_INT; v.ival = (int)a->i64[i];
        vm_set_add(vm, n, &v);
    }
    for (int i = 0; i < a->count; i++) vm_set_add(vm, n, &a->items[i]);
    SetComp compA, compB;
    int aAsComp = set_as_comp(a, &compA);
    int bAsComp = set_as_comp(b, &compB);
    if (aAsComp) vm_set_add_comp_dedup(vm, n, &compA);
    else for (int i = 0; i < a->compCount; i++) vm_set_add_comp_dedup(vm, n, &a->comps[i]);
    for (int i = 0; i < b->iCount; i++) {
        Value v; v.type = VAL_INT; v.ival = (int)b->i64[i];
        vm_set_add(vm, n, &v);
    }
    for (int i = 0; i < b->count; i++) vm_set_add(vm, n, &b->items[i]);
    if (bAsComp) vm_set_add_comp_dedup(vm, n, &compB);
    else for (int i = 0; i < b->compCount; i++) vm_set_add_comp_dedup(vm, n, &b->comps[i]);
    return n;
}
static int set_intersect(VM *vm, int aidx, int bidx) {
    SetObj *a = vm_set_slot(vm, aidx), *b = vm_set_slot(vm, bidx);
    if (!a || !b) return -1;
    int n = vm_set_new(vm);
    if (n < 0) return -1;
    a = vm_set_slot(vm, aidx);
    b = vm_set_slot(vm, bidx);
    if (!a || !b) return -1;
    for (int i = 0; i < a->iCount; i++) {
        Value v; Value tmpv; Value *pv = &v; (void)pv; (void)tmpv;
        v.type = VAL_INT; v.ival = (int)a->i64[i];
        if (set_contains(vm, bidx, &v)) vm_set_add(vm, n, &v);
    }
    for (int i = 0; i < a->count; i++)
        if (set_contains(vm, bidx, &a->items[i])) vm_set_add(vm, n, &a->items[i]);
    for (int i = 0; i < b->iCount; i++) {
        Value v; v.type = VAL_INT; v.ival = (int)b->i64[i];
        if (set_contains(vm, aidx, &v)) vm_set_add(vm, n, &v);
    }
    for (int i = 0; i < b->count; i++)
        if (set_contains(vm, aidx, &b->items[i])) vm_set_add(vm, n, &b->items[i]);
    SetComp compA, compB;
    int aAsComp = set_as_comp(a, &compA);
    int bAsComp = set_as_comp(b, &compB);
    int na = aAsComp ? 1 : a->compCount;
    int nb = bAsComp ? 1 : b->compCount;
    for (int ia = 0; ia < na; ia++) {
        const SetComp *ca = aAsComp ? &compA : &a->comps[ia];
        for (int ib = 0; ib < nb; ib++) {
            const SetComp *cb = bAsComp ? &compB : &b->comps[ib];
            SetComp out;
            int r = comp_intersect(ca, cb, &out);
            if (r == 0) { vm_set_free_objs(vm, n); return -1; }
            if (r == 1) vm_set_add_comp_dedup(vm, n, &out);
        }
    }
    return n;
}
static int set_diff(VM *vm, int aidx, int bidx) {
    SetObj *a = vm_set_slot(vm, aidx), *b = vm_set_slot(vm, bidx);
    if (!a || !b) return -1;
    if (a->kind != 0 || b->kind != 0 || a->compCount > 0 || b->compCount > 0) return -1;
    int n = vm_set_new(vm);
    if (n < 0) return -1;
    for (int i = 0; i < a->iCount; i++) {
        Value v; v.type = VAL_INT; v.ival = (int)a->i64[i];
        if (!set_contains(vm, bidx, &v)) vm_set_add(vm, n, &v);
    }
    for (int i = 0; i < a->count; i++)
        if (!set_contains(vm, bidx, &a->items[i])) vm_set_add(vm, n, &a->items[i]);
    return n;
}
static int set_equal(VM *vm, int aidx, int bidx) {
    SetObj *a = vm_set_slot(vm, aidx), *b = vm_set_slot(vm, bidx);
    if (!a || !b) return a == b;
    if (a->kind == 1 && b->kind == 1) return a->nameIdx == b->nameIdx;
    if (a->kind == 2 && b->kind == 2)
        return a->nameIdx == b->nameIdx && a->lo == b->lo && a->hi == b->hi &&
               a->loInc == b->loInc && a->hiInc == b->hiInc;
    /* kind0 with a single component == kind2 interval */
    if (a->kind == 0 && b->kind == 2 && a->iCount == 0 && a->count == 0 && a->compCount == 1)
        return a->comps[0].nameIdx == b->nameIdx && a->comps[0].lo == b->lo && a->comps[0].hi == b->hi &&
               a->comps[0].loInc == b->loInc && a->comps[0].hiInc == b->hiInc;
    if (b->kind == 0 && a->kind == 2 && b->iCount == 0 && b->count == 0 && b->compCount == 1)
        return b->comps[0].nameIdx == a->nameIdx && b->comps[0].lo == a->lo && b->comps[0].hi == a->hi &&
               b->comps[0].loInc == a->loInc && b->comps[0].hiInc == a->hiInc;
    if (a->kind != 0 || b->kind != 0) return 0;
    if (a->iCount != b->iCount || a->count != b->count || a->compCount != b->compCount) return 0;
    for (int i = 0; i < a->iCount; i++) {
        int f = 0;
        for (int j = 0; j < b->iCount; j++) if (a->i64[i] == b->i64[j]) { f = 1; break; }
        if (!f) return 0;
    }
    for (int i = 0; i < a->count; i++) {
        int f = 0;
        for (int j = 0; j < b->count; j++) if (val_eq(&a->items[i], &b->items[j])) { f = 1; break; }
        if (!f) return 0;
    }
    for (int i = 0; i < a->compCount; i++) {
        int f = 0;
        for (int j = 0; j < b->compCount; j++) if (set_comp_equal(&a->comps[i], &b->comps[j])) { f = 1; break; }
        if (!f) return 0;
    }
    return 1;
}
int vm_set_to_array(VM *vm, int sidx) {
    SetObj *s = vm_set_slot(vm, sidx);
    if (!s) return -1;
    if (s->kind == 2 && s->lo > -1e300 && s->hi < 1e300) {
        if (s->nameIdx == 24) return -1; /* real interval: not enumerable */
        int aidx = vm_array_new(vm);
        if (aidx < 0) return -1;
        double step = (s->nameIdx >= 0 && s->nameIdx <= 3) ? 1.0 : pow(10.0, -((s->nameIdx - 4) / 2 + 1));
        if (step <= 0) step = 1.0;
        double start = s->lo;
        if (!s->loInc) start = s->lo + step;
        int guard = 0;
        for (int k = 0; ; k++, guard++) {
            double x = start + (double)k * step;
            if (x > s->hi || (x == s->hi && !s->hiInc)) break;
            if (guard >= 10000000) break;
            Value v;
            if (s->nameIdx >= 0 && s->nameIdx <= 3) { v.type = VAL_INT; v.ival = (int)x; v.fval = 0; v.sval = NULL; }
            else { v.type = VAL_FLOAT; v.fval = x; v.ival = 0; v.sval = NULL; }
            if (builtin_contains(s->nameIdx, &v)) vm_array_push(vm, aidx, &v);
        }
        return aidx + 1;
    }
    if (s->kind != 0 || s->compCount > 0) return -1;
    int aidx = vm_array_new(vm);
    if (aidx < 0) return -1;
    for (int i = 0; i < s->iCount; i++) {
        Value v; v.type = VAL_INT; v.ival = (int)s->i64[i];
        vm_array_push(vm, aidx, &v);
    }
    for (int i = 0; i < s->count; i++) vm_array_push(vm, aidx, &s->items[i]);
    return aidx + 1;
}
static void named_range(int bi, double *lo, double *hi) {
    *lo = -1.0e308; *hi = 1.0e308;
    if (bi == 0) { *lo = 0; }
    else if (bi == 2) { *lo = 1; }
    else if (bi == 3) { *hi = -1; }
}
static int set_subset(VM *vm, int aidx, int bidx) {
    SetObj *a = vm_set_slot(vm, aidx), *b = vm_set_slot(vm, bidx);
    if (!a || !b) return 0;
    if (a->kind == 0) {
        for (int i = 0; i < a->iCount; i++) {
            Value iv;
            iv.type = VAL_INT; iv.ival = (int)a->i64[i]; iv.fval = 0; iv.sval = NULL;
            if (!set_contains(vm, bidx, &iv)) return 0;
        }
        for (int i = 0; i < a->count; i++)
            if (!set_contains(vm, bidx, &a->items[i])) return 0;
        for (int i = 0; i < a->compCount; i++) {
            SetComp *c = &a->comps[i];
            if (b->kind == 0) {
                /* component subset of b: try b's own components + named/interval membership */
                if (c->lo != c->hi || !c->loInc || !c->hiInc) return 0;
                Value probe;
                probe.type = VAL_FLOAT; probe.fval = c->lo; probe.ival = 0;
                if (!set_contains(vm, bidx, &probe)) return 0;
            } else {
                if (c->nameIdx < 0 || b->nameIdx < 0) return 0;
                if (!builtin_subset(c->nameIdx, b->nameIdx)) return 0;
                if (b->kind == 2) {
                    if (c->lo > -1e307 && (c->lo < b->lo || (c->lo == b->lo && !b->loInc))) return 0;
                    if (c->hi < 1e307 && (c->hi > b->hi || (c->hi == b->hi && !b->hiInc))) return 0;
                }
            }
        }
        return 1;
    }
    if (a->nameIdx < 0 || b->nameIdx < 0) return 0;
    if (!builtin_subset(a->nameIdx, b->nameIdx)) return 0;
    if (b->kind == 2) {
        double alo, ahi;
        if (a->kind == 1) named_range(a->nameIdx, &alo, &ahi);
        else { alo = a->lo; ahi = a->hi; }
        if (alo > -1e307 && (alo < b->lo || (alo == b->lo && !b->loInc))) return 0;
        if (ahi < 1e307 && (ahi > b->hi || (ahi == b->hi && !b->hiInc))) return 0;
    }
    return 1;
}
static int set_contains_or_subset(VM *vm, Value *a, Value *b) {
    if (b->type == VAL_SET) {
        if (a->type == VAL_ARRAY) {
            int aidx = a->ival - 1;
            if (aidx < 0 || aidx >= vm->arrayCount) return 1; /* empty array: subset is true */
            VM_LOCK(vm);
            ArrayObj *arr = vm_pool_slot(vm, aidx);
            int res = 1;
            for (int i = 0; i < arr->count; i++)
                if (!set_contains(vm, b->ival, &arr->items[i])) { res = 0; break; }
            VM_UNLOCK(vm);
            return res;
        }
        if (a->type == VAL_SET) return set_subset(vm, a->ival, b->ival);
        return set_contains(vm, b->ival, a);
    }
    if (b->type == VAL_ARRAY) {
        int bidx = b->ival - 1;
        if (bidx < 0 || bidx >= vm->arrayCount) return 0;
        VM_LOCK(vm);
        ArrayObj *arr = vm_pool_slot(vm, bidx);
        int res = 0;
        if (a->type == VAL_ARRAY) {
            int aidx = a->ival - 1;
            res = 1;
            if (aidx < 0 || aidx >= vm->arrayCount) { VM_UNLOCK(vm); return 1; }
            ArrayObj *aa = vm_pool_slot(vm, aidx);
            for (int i = 0; i < aa->count; i++) {
                int found = 0;
                for (int j = 0; j < arr->count; j++)
                    if (val_eq(&aa->items[i], &arr->items[j])) { found = 1; break; }
                if (!found) { res = 0; break; }
            }
        } else if (a->type == VAL_SET) {
            SetObj *sa = vm_set_slot(vm, a->ival);
            if (!sa) res = 0;
            else if (sa->kind == 0) {
                for (int i = 0; i < sa->count; i++) {
                    int found = 0;
                    for (int j = 0; j < arr->count; j++)
                        if (val_eq(&sa->items[i], &arr->items[j])) { found = 1; break; }
                    if (!found) { res = 0; break; }
                }
            } else res = 0; /* infinite set vs array: undecidable -> false */
        } else {
            for (int j = 0; j < arr->count; j++)
                if (val_eq(a, &arr->items[j])) { res = 1; break; }
        }
        VM_UNLOCK(vm);
        return res;
    }
    return 0;
}
static double set_scan_up(double start, double step, int bi, int *found) {
    for (int i = 0; i < 1000000; i++) {
        double x = start + (double)i * step;
        Value v; v.type = VAL_FLOAT; v.fval = x;
        if (builtin_contains(bi, &v)) { *found = 1; return x; }
        if (x > 1e300) break;
    }
    *found = 0; return 0;
}
static double set_scan_down(double start, double step, int bi, int *found) {
    for (int i = 0; i < 1000000; i++) {
        double x = start - (double)i * step;
        Value v; v.type = VAL_FLOAT; v.fval = x;
        if (builtin_contains(bi, &v)) { *found = 1; return x; }
        if (x < -1e300) break;
    }
    *found = 0; return 0;
}
static void set_comp_minmax(VM *vm, SetComp *c, Value *dst, int isMax) {
    dst->type = VAL_NIL; dst->ival = 0; dst->fval = 0; dst->sval = NULL;
    if (!c || c->nameIdx < 0) return;
    int bi = c->nameIdx;
    int isIntSet = (bi == 0 || bi == 1 || bi == 2 || bi == 3);
    double step = isIntSet ? 1.0 : pow(10.0, -((double)((bi - 4) / 2 + 1)));
    if (!isMax) {
        if (c->lo <= -1.0e307) return; /* unbounded below -> nil */
        double start = c->lo;
        if (!c->loInc) start = c->lo + step;
        int found = 0;
        double v = set_scan_up(start, step, bi, &found);
        if (found && (v > c->hi || (v == c->hi && !c->hiInc))) found = 0;
        if (found) { dst->type = VAL_FLOAT; dst->fval = v; }
    } else {
        if (c->hi >= 1.0e307) return; /* unbounded above -> nil */
        double start = c->hi;
        if (!c->hiInc) start = c->hi - step;
        int found = 0;
        double v = set_scan_down(start, step, bi, &found);
        if (found && (v < c->lo || (v == c->lo && !c->loInc))) found = 0;
        if (found) { dst->type = VAL_FLOAT; dst->fval = v; }
    }
}
static void set_minmax(VM *vm, Value *src, Value *dst, int isMax) {
    dst->type = VAL_NIL; dst->ival = 0; dst->fval = 0; dst->sval = NULL;
    if (src->type != VAL_SET) return;
    SetObj *s = vm_set_slot(vm, src->ival);
    if (!s) return;
    if (s->kind == 0) {
        Value best;
        int have = 0;
        int bestIsStr = 0;
        for (int i = 0; i < s->iCount; i++) {
            Value iv;
            iv.type = VAL_INT; iv.ival = (int)s->i64[i]; iv.fval = 0; iv.sval = NULL;
            if (!have) { best = iv; have = 1; bestIsStr = 0; continue; }
            if (bestIsStr) { have = -1; break; }
            int c = val_cmp(&iv, &best);
            if ((isMax && c > 0) || (!isMax && c < 0)) best = iv;
        }
        for (int i = 0; i < s->count; i++) {
            int itemIsStr = (s->items[i].type == VAL_STRING);
            if (!have) { best = s->items[i]; have = 1; bestIsStr = itemIsStr; continue; }
            if (itemIsStr != bestIsStr) { have = -1; break; }
            int c = val_cmp(&s->items[i], &best);
            if ((isMax && c > 0) || (!isMax && c < 0)) best = s->items[i];
        }
        for (int i = 0; i < s->compCount && have >= 0; i++) {
            SetComp *c = &s->comps[i];
            Value cv;
            cv.type = VAL_NIL; cv.ival = 0; cv.fval = 0; cv.sval = NULL;
            set_comp_minmax(vm, c, &cv, isMax);
            if (cv.type == VAL_NIL) { if (have == 0) have = -1; continue; }
            int cvIsStr = (cv.type == VAL_STRING);
            if (have == 1 && cvIsStr != bestIsStr) { have = -1; break; }
            if (!have) { best = cv; have = 1; bestIsStr = cvIsStr; continue; }
            int c2 = val_cmp(&cv, &best);
            if ((isMax && c2 > 0) || (!isMax && c2 < 0)) best = cv;
        }
        if (have == 1) *dst = best;
        return;
    }
    if (s->nameIdx < 0) return;
    if (s->kind == 1) {
        if (s->nameIdx == 0 && !isMax) { dst->type = VAL_INT; dst->ival = 0; return; }
        if (s->nameIdx == 2 && !isMax) { dst->type = VAL_INT; dst->ival = 1; return; }
        if (s->nameIdx == 3 && isMax) { dst->type = VAL_INT; dst->ival = -1; return; }
        return; /* unbounded -> nil */
    }
    if (s->kind == 2) {
        if (s->nameIdx == 24) return; /* real interval: no discrete min/max */
        if (s->nameIdx == 0 || s->nameIdx == 1 || s->nameIdx == 2 || s->nameIdx == 3) {
            double step = 1.0;
            int found = 0;
            double v = 0;
            if (!isMax) {
                if (s->lo <= -1.0e307) return; /* unbounded below -> nil */
                double start = s->lo;
                if (!s->loInc) start = s->lo + step;
                v = set_scan_up(start, step, s->nameIdx, &found);
                if (found && (v > s->hi || (v == s->hi && !s->hiInc))) found = 0;
            } else {
                if (s->hi >= 1.0e307) return; /* unbounded above -> nil */
                double start = s->hi;
                if (!s->hiInc) start = s->hi - step;
                v = set_scan_down(start, step, s->nameIdx, &found);
                if (found && (v < s->lo || (v == s->lo && !s->loInc))) found = 0;
            }
            if (found) { dst->type = VAL_FLOAT; dst->fval = v; }
            return;
        }
        /* FloatN/floatN interval */
        double step = pow(10.0, -((double)((s->nameIdx - 4) / 2 + 1)));
        int found = 0;
        double v = 0;
        if (!isMax) {
            if (s->lo <= -1.0e307) return;
            double start = s->lo;
            if (!s->loInc) start = s->lo + step;
            v = set_scan_up(start, step, s->nameIdx, &found);
            if (found && (v > s->hi || (v == s->hi && !s->hiInc))) found = 0;
        } else {
            if (s->hi >= 1.0e307) return;
            double start = s->hi;
            if (!s->hiInc) start = s->hi - step;
            v = set_scan_down(start, step, s->nameIdx, &found);
            if (found && (v < s->lo || (v == s->lo && !s->loInc))) found = 0;
        }
        if (found) { dst->type = VAL_FLOAT; dst->fval = v; }
    }
}

/* ---------- exception support ---------- */
static void vm_throw(VM *vm, VmThread *t, Value *err) {
    if (t->exc_depth > 0) {
        ExcFrame *f = &t->exc_stack[t->exc_depth - 1];
        t->code = f->code;
        t->ip = f->ip;
        t->sp = f->sp;
        t->base = f->base;
        t->frame_count = f->frame_count;
        if (f->var_idx >= 0) {
            im_mutex_lock((ImMutex*)VM_GSHARD(vm, f->var_idx));
            im_mutex_unlock((ImMutex*)VM_GSHARD(vm, f->var_idx));
            vm_global_grow(vm, f->var_idx);   /* 鐙崰鍏ㄩ儴閿佹墿瀹癸紙姝ゆ椂鏈寔浠讳綍鍒嗙墖锛?*/
            im_mutex_lock((ImMutex*)VM_GSHARD(vm, f->var_idx));
            if (f->var_idx >= vm->globalCount) {
                for (int i = vm->globalCount; i <= f->var_idx; i++) {
                    vm->globals[i].name = NULL;
                    vm->globals[i].val.type = VAL_NIL;
                }
                vm->globalCount = f->var_idx + 1;
            }
            value_free(&vm->globals[f->var_idx].val);
            vm->globals[f->var_idx].val = *err;
            if (err->type == VAL_STRING && err->sval && err->ival != 1) {
                const char *np = vm_intern(vm, err->sval);
                if (np) { vm->globals[f->var_idx].val.sval = (char*)np; vm->globals[f->var_idx].val.ival = 1; }
            }
            im_mutex_unlock((ImMutex*)VM_GSHARD(vm, f->var_idx));
        } else if (f->ignore) {
            /* bare try (no catch): swallow + record into the debug slot (visible via dbg_var) */
            char ebuf[256];
            value_to_string(vm, err, ebuf, sizeof ebuf, 0);
            VM_LOCK(vm);
            if (vm->last_ignored_exc) free(vm->last_ignored_exc);
            vm->last_ignored_exc = strdup(ebuf);
            vm->ignored_exc_count++;
            VM_UNLOCK(vm);
            if (vm->dbg_active)
                fprintf(stderr, "[try-ignore] swallowed: %s\n", ebuf);
        }
        t->exc_depth--;
        t->ip = f->catch_ip;
        return;
    }
    if (g_err_json) {
        char ebuf[256], eout[512];
        value_to_string(vm, err, ebuf, sizeof ebuf, 0);
        int oi = 0;
        for (int xi = 0; ebuf[xi] && oi < (int)sizeof(eout) - 2; xi++) {
            unsigned char cc = (unsigned char)ebuf[xi];
            if (cc == '"') { eout[oi++] = '\\'; eout[oi++] = '"'; }
            else if (cc == '\\') { eout[oi++] = '\\'; eout[oi++] = '\\'; }
            else if (cc == '\n') { eout[oi++] = '\\'; eout[oi++] = 'n'; }
            else if (cc == '\r') { eout[oi++] = '\\'; eout[oi++] = 'r'; }
            else if (cc == '\t') { eout[oi++] = '\\'; eout[oi++] = 't'; }
            else eout[oi++] = (char)cc;
        }
        eout[oi] = 0;
        fprintf(stderr, "{\"error\":\"exception\",\"message\":\"%s\",\"ip\":%d,\"frames\":[", eout, t->ip);
        for (int fi = t->frame_count - 1; fi >= 0; fi--)
            fprintf(stderr, "%s%d", (fi < t->frame_count - 1) ? "," : "", t->frame_ip[fi]);
        fprintf(stderr, "]}\n");
        vm->last_error = 1;
        t->running = false;
        return;
    }
    fprintf(stderr, "\n[exception] uncaught: %s\n",
        (err->type == VAL_STRING && err->sval) ? err->sval : "(non-string value)");
    fprintf(stderr, "  at ip=%d frames=%d", t->ip, t->frame_count);
    for (int fi = t->frame_count - 1; fi >= 0; fi--)
        fprintf(stderr, " -> ip=%d", t->frame_ip[fi]);
    fprintf(stderr, "\n");
    vm->last_error = 1;
    t->running = false;
}

void vm_throw_msg(VM *vm, const char *msg) {
    VmThread *t = vm_get_cur_thread();
    if (!t || !msg) return;
    Value err;
    err.type = VAL_STRING;
    err.sval = (char*)vm_intern(vm, msg);
    err.ival = 1;
    err.fval = 0;
    vm_throw(vm, t, &err);
}

#define CHECK_INTERVAL 256 /* fast-path checks every N instructions */
/* ================= Inimerse2D: script callbacks by name ================= */
static int vm_call_func_at(VM *vm, VmThread *t, int fidx, int argc, Value *argv) {
    Bytecode *root = vm->code;
    if (!root || !t) return 0;
    if (fidx < 0 || fidx >= root->func_count || !root->funcs[fidx]) return 0;
    if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap) return 0;
    t->frame_code[t->frame_count] = t->code;
    t->frame_ip[t->frame_count] = t->ip;
    t->frame_base[t->frame_count] = t->base;
    t->frame_res[t->frame_count] = 0;
    t->frame_count++;
    t->base += VM_FRAME_REGS;
    Value *FR = t->reg + t->base;
    for (int k = 0; k < argc; k++) FR[k + 1] = argv[k];
    t->frame_sp[t->frame_count - 1] = t->sp;
    t->code = root->funcs[fidx];
    t->ip = 0;
    return 1;
}

static int vm_call_func_by_name(VM *vm, VmThread *t, const char *name, int argc, Value *argv) {
    Bytecode *root = vm->code;
    if (!root || !t) return 0;
    for (int i = 0; i < root->func_count; i++)
        if (root->func_names[i] && strcmp(root->func_names[i], name) == 0 && root->funcs[i])
            return vm_call_func_at(vm, t, i, argc, argv);
    return 0;
}

/* cached callback lookup: returns func index, or -2 if absent (cached, no re-scan) */
static int vm_find_func(VM *vm, const char *name) {
    Bytecode *root = vm->code;
    if (!root) return -2;
    for (int i = 0; i < root->func_count; i++)
        if (root->func_names[i] && strcmp(root->func_names[i], name) == 0 && root->funcs[i]) return i;
    return -2;
}

/* per-frame Inimerse2D callback: on_load (first), scene_<name>(), on_update(dt), on_render().
   Returns 1 if a function frame was pushed (caller must set R = t->reg + t->base and continue). */
static int vm_frame_callback(VM *vm, VmThread *t) {
    if (vm->im2d_interval_ms <= 0) return 0;
    if (!vm->im2d_ready) {
        vm->im2d_ready = 1;
        vm->im2d_cb_load = vm->im2d_cb_update = vm->im2d_cb_render = -1; /* not yet looked up */
        if (vm->im2d_cb_load == -1) vm->im2d_cb_load = vm_find_func(vm, "on_load");
        if (vm->im2d_cb_load >= 0 && vm_call_func_at(vm, t, vm->im2d_cb_load, 0, NULL)) return 1;
    }
    if (vm->im2d_scene[0] && strcmp(vm->im2d_scene, vm->im2d_last_scene) != 0) {
        strncpy(vm->im2d_last_scene, vm->im2d_scene, 63);
        vm->im2d_last_scene[63] = '\0';
        char fn[96];
        snprintf(fn, sizeof fn, "scene_%s", vm->im2d_scene);
        if (vm_call_func_by_name(vm, t, fn, 0, NULL)) return 1;
    }
    {
        Value argv[1];
        vm->im2d_dt = (double)vm->im2d_interval_ms / 1000.0;
        argv[0].type = VAL_FLOAT;
        argv[0].fval = vm->im2d_dt;
        argv[0].ival = 0;
        argv[0].sval = NULL;
        if (vm->im2d_cb_update == -1) vm->im2d_cb_update = vm_find_func(vm, "on_update");
        if (vm->im2d_cb_update >= 0 && vm_call_func_at(vm, t, vm->im2d_cb_update, 1, argv)) return 1;
        if (vm->im2d_cb_render == -1) vm->im2d_cb_render = vm_find_func(vm, "on_render");
        if (vm->im2d_cb_render >= 0 && vm_call_func_at(vm, t, vm->im2d_cb_render, 0, NULL)) return 1;
    }
    return 0;
}


/* ---------- mark-sweep GC for pool slots (arrays/dicts/sets) ----------
 * Roots: globals + every thread's stack[0..sp] and reg[0..base+VM_FRAME_REGS]
 *        + record_loaded_dict + be_bound[] (C-side holders).
 * Runs at instruction safe points with cooperative stop-the-world (gc_stop/gc_parked). */
static void gc_ensure_mark(VM *vm, int cap, int is_array) {
    unsigned char **mk; int *mkcap;
    if (is_array) { mk = &vm->gc_amark; mkcap = &vm->gc_amark_cap; }
    else { mk = &vm->gc_smark; mkcap = &vm->gc_smark_cap; }
    if (cap > *mkcap) {
        int nc = *mkcap ? *mkcap : 256;
        while (nc < cap) nc *= 2;
        unsigned char *n = realloc(*mk, (size_t)nc);
        if (!n) return;
        memset(n + *mkcap, 0, (size_t)(nc - *mkcap));
        *mk = n; *mkcap = nc;
    }
}
static void gc_mark_value(VM *vm, const Value *val) {
    int idx, isa;
    if (val->type == VAL_ARRAY || val->type == VAL_DICT) { idx = val->ival - 1; isa = 1; }
    else if (val->type == VAL_SET) { idx = val->ival - 1; isa = 0; }
    else return;
    if (idx < 0) return;
    unsigned char *mk = isa ? vm->gc_amark : vm->gc_smark;
    int cap = isa ? vm->gc_amark_cap : vm->gc_smark_cap;
    int lim = isa ? vm->arrayCount : vm->setCount;
    if (idx >= cap || idx >= lim) return;
    if (mk[idx]) return;
    mk[idx] = 1;
    if (vm->gc_work_count >= vm->gc_work_cap) {
        int nc = vm->gc_work_cap ? vm->gc_work_cap * 2 : 256;
        int *n = realloc(vm->gc_work, (size_t)nc * sizeof(int));
        if (!n) return;
        vm->gc_work = n; vm->gc_work_cap = nc;
    }
    vm->gc_work[vm->gc_work_count++] = (idx << 1) | isa;
}
static void gc_mark(VM *vm) {
    /* globals */
    for (int i = 0; i < vm->globalCount; i++) gc_mark_value(vm, &vm->globals[i].val);
    /* every thread: builtin arg stack + live register file */
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (!tt) continue;
        int sp = tt->sp; if (sp < 0) sp = 0; if (sp > 1023) sp = 1023;
        for (int k = 0; k <= sp; k++) gc_mark_value(vm, &tt->stack[k]);
        int regEnd = tt->base + VM_FRAME_REGS;
        if (regEnd > tt->reg_cap) regEnd = tt->reg_cap;
        for (int k = 0; k < regEnd; k++) gc_mark_value(vm, &tt->reg[k]);
    }
    /* task (virtual thread) roots: all tasks are parked at switch points during GC */
    for (int i = 0; i < vm->task_count; i++) {
        VmThread *tk = vm->tasks[i];
        if (!tk || tk->finished) continue;
        int tsp = tk->sp; if (tsp < 0) tsp = 0; if (tsp > 1023) tsp = 1023;
        for (int k = 0; k <= tsp; k++) gc_mark_value(vm, &tk->stack[k]);
        int tregEnd = tk->base + VM_FRAME_REGS;
        if (tregEnd > tk->reg_cap) tregEnd = tk->reg_cap;
        for (int k = 0; k < tregEnd; k++) gc_mark_value(vm, &tk->reg[k]);
        for (int k = 0; k < tk->msg_cap; k++) { Value _mv = tk->msg_q[k]; gc_mark_value(vm, &_mv); }
    }
    /* C-side holders */
    if (vm->record_loaded_dict > 0) gc_mark_value(vm, &(Value){ .type = VAL_DICT, .ival = vm->record_loaded_dict, .fval = 0, .sval = NULL });
    for (int i = 0; i < vm->globalCount; i++) {
        if (vm->be_bound[i] > 0) {
            int sidx = vm->be_bound[i] - 1;
            unsigned char *mk = vm->gc_smark; int cap = vm->gc_smark_cap;
            if (sidx < cap && sidx < vm->setCount && !mk[sidx]) {
                mk[sidx] = 1;
                if (vm->gc_work_count >= vm->gc_work_cap) {
                    int nc = vm->gc_work_cap ? vm->gc_work_cap * 2 : 256;
                    int *n = realloc(vm->gc_work, (size_t)nc * sizeof(int));
                    if (!n) break;
                    vm->gc_work = n; vm->gc_work_cap = nc;
                }
                vm->gc_work[vm->gc_work_count++] = sidx << 1;
            }
        }
    }
    /* drain mark work stack (iterative, no C recursion) */
    while (vm->gc_work_count > 0) {
        int w = vm->gc_work[--vm->gc_work_count];
        int isa = w & 1, idx = w >> 1;
        if (isa) {
            ArrayObj *a = vm_pool_slot(vm, idx);
            for (int j = 0; j < a->count; j++) gc_mark_value(vm, &a->items[j]);
        } else {
            SetObj *s = vm_set_slot(vm, idx);
            for (int j = 0; j < s->count; j++) gc_mark_value(vm, &s->items[j]);
        }
    }
}
static void gc_sweep(VM *vm) {
    int freed = 0;
    /* arrays (+ dict hashes) */
    for (int i = 0; i < vm->arrayCount; i++) {
        ArrayObj *a = vm_pool_slot(vm, i);
        if (a->count == -1) continue; /* already on the free list */
        if (vm->gc_amark && vm->gc_amark_cap > i && vm->gc_amark[i]) continue;
        for (int j = 0; j < a->count; j++) {
            value_free(&a->items[j]);
            if (a->items[j].type == VAL_STRING && a->items[j].sval)
                vm->used_mem -= (double)strlen(a->items[j].sval);
        }
        vm->used_mem -= (double)a->count * sizeof(Value);
        if (vm->used_mem < 0) vm->used_mem = 0;
        if (a->items && a->items != a->inline_buf) free(a->items);
        if (i < vm->dict_hashes_cap && vm->dict_hashes && vm->dict_hashes[i].slots) {
            free(vm->dict_hashes[i].slots);
            vm->dict_hashes[i].slots = NULL;
            vm->dict_hashes[i].cap = vm->dict_hashes[i].mask = vm->dict_hashes[i].count = 0;
        }
        a->items = NULL; a->count = -1; /* sentinel: slot is on the free list */
        if (vm->array_free_n >= vm->array_free_cap) {
            int nc = vm->array_free_cap ? vm->array_free_cap * 2 : 256;
            int *nl = realloc(vm->array_free_list, (size_t)nc * sizeof(int));
            if (!nl) continue;
            vm->array_free_list = nl; vm->array_free_cap = nc;
        }
        vm->array_free_list[vm->array_free_n++] = i;
        freed++;
    }
    /* sets */
    for (int i = 0; i < vm->setCount; i++) {
        SetObj *s = vm_set_slot(vm, i);
        if (s->iCount == -1) continue; /* already on the free list */
        if (vm->gc_smark && vm->gc_smark_cap > i && vm->gc_smark[i]) continue;
        for (int j = 0; j < s->count; j++) {
            value_free(&s->items[j]);
            if (s->items[j].type == VAL_STRING && s->items[j].sval)
                vm->used_mem -= (double)strlen(s->items[j].sval);
        }
        free(s->i64); free(s->items); free(s->comps);
        s->i64 = NULL; s->items = NULL; s->comps = NULL;
        s->iCount = -1; /* sentinel: slot is on the free list */
        s->count = 0; s->compCount = 0;
        if (vm->set_free_n >= vm->set_free_cap) {
            int nc = vm->set_free_cap ? vm->set_free_cap * 2 : 256;
            int *nl = realloc(vm->set_free_list, (size_t)nc * sizeof(int));
            if (!nl) continue;
            vm->set_free_list = nl; vm->set_free_cap = nc;
        }
        vm->set_free_list[vm->set_free_n++] = i;
        freed++;
    }
    vm->gc_freed = freed;
    vm->gc_runs++;
    if (vm->gc_amark && vm->gc_amark_cap) memset(vm->gc_amark, 0, (size_t)vm->gc_amark_cap);
    if (vm->gc_smark && vm->gc_smark_cap) memset(vm->gc_smark, 0, (size_t)vm->gc_smark_cap);
    vm->gc_work_count = 0;
}
void gc_collect(VM *vm) {
    if (!vm->gc_enabled) return;
    gc_ensure_mark(vm, vm->arrayCount + 1, 1);
    gc_ensure_mark(vm, vm->setCount + 1, 0);
    gc_mark(vm);
    gc_sweep(vm);
    /* post-collect root validation (debug, silent) */
    {
        int bad = 0;
        for (int gi = 0; gi < vm->globalCount; gi++) {
            Value *gv = &vm->globals[gi].val;
            if ((gv->type == VAL_ARRAY || gv->type == VAL_DICT) && gv->ival > 0 && gv->ival - 1 < vm->arrayCount) {
                ArrayObj *ga = vm_pool_slot(vm, gv->ival - 1);
                if (ga->count == -1) { fprintf(stderr, "[gc] BADROOT global[%d] '%s' -> freed slot %d\n", gi, vm->globals[gi].name, gv->ival - 1); bad = 1; }
            }
            if (gv->type == VAL_SET && gv->ival > 0 && gv->ival - 1 < vm->setCount) {
                SetObj *gs = vm_set_slot(vm, gv->ival - 1);
                if (gs->iCount == -1) { fprintf(stderr, "[gc] BADROOT global[%d] '%s' -> freed set %d\n", gi, vm->globals[gi].name, gv->ival - 1); bad = 1; }
            }
        }
        for (int ti = 0; ti < VM_MAX_THREADS && !bad; ti++) {
            VmThread *tt = vm->threads[ti];
            if (!tt) continue;
            for (int si = 0; si <= tt->sp && si < 1024; si++) {
                Value *sv = &tt->stack[si];
                if ((sv->type == VAL_ARRAY || sv->type == VAL_DICT) && sv->ival > 0 && sv->ival - 1 < vm->arrayCount) {
                    ArrayObj *sa = vm_pool_slot(vm, sv->ival - 1);
                    if (sa->count == -1) { fprintf(stderr, "[gc] BADROOT stack t%d[%d] -> freed %d\n", ti, si, sv->ival - 1); bad = 1; }
                }
            }
            int regEnd = tt->base + VM_FRAME_REGS;
            if (regEnd > tt->reg_cap) regEnd = tt->reg_cap;
            for (int ri = 0; ri < regEnd && !bad; ri++) {
                Value *rv = &tt->reg[ri];
                if ((rv->type == VAL_ARRAY || rv->type == VAL_DICT) && rv->ival > 0 && rv->ival - 1 < vm->arrayCount) {
                    ArrayObj *ra = vm_pool_slot(vm, rv->ival - 1);
                    if (ra->count == -1) { fprintf(stderr, "[gc] BADROOT reg t%d[%d] -> freed %d\n", ti, ri, rv->ival - 1); bad = 1; }
                }
            }
        }
        /* tasks: virtual-thread roots (fiber reg/stack/msg) - same check */
        for (int ti = 0; ti < vm->task_count && !bad; ti++) {
            VmThread *tt = vm->tasks[ti];
            if (!tt || tt->finished) continue;
            for (int si = 0; si <= tt->sp && si < 1024; si++) {
                Value *sv = &tt->stack[si];
                if ((sv->type == VAL_ARRAY || sv->type == VAL_DICT) && sv->ival > 0 && sv->ival - 1 < vm->arrayCount) {
                    ArrayObj *sa = vm_pool_slot(vm, sv->ival - 1);
                    if (sa->count == -1) { fprintf(stderr, "[gc] BADROOT task%d stack[%d] -> freed %d\n", ti, si, sv->ival - 1); bad = 1; }
                }
            }
            int regEnd = tt->base + VM_FRAME_REGS;
            if (regEnd > tt->reg_cap) regEnd = tt->reg_cap;
            for (int ri = 0; ri < regEnd && !bad; ri++) {
                Value *rv = &tt->reg[ri];
                if ((rv->type == VAL_ARRAY || rv->type == VAL_DICT) && rv->ival > 0 && rv->ival - 1 < vm->arrayCount) {
                    ArrayObj *ra = vm_pool_slot(vm, rv->ival - 1);
                    if (ra->count == -1) { fprintf(stderr, "[gc] BADROOT task%d reg[%d] -> freed %d\n", ti, ri, rv->ival - 1); bad = 1; }
                }
            }
            for (int mi = 0; mi < tt->msg_cap && !bad; mi++) {
                Value *mv = &tt->msg_q[mi];
                if ((mv->type == VAL_ARRAY || mv->type == VAL_DICT) && mv->ival > 0 && mv->ival - 1 < vm->arrayCount) {
                    ArrayObj *ma = vm_pool_slot(vm, mv->ival - 1);
                    if (ma->count == -1) { fprintf(stderr, "[gc] BADROOT task%d msg[%d] -> freed %d\n", ti, mi, mv->ival - 1); bad = 1; }
                }
            }
        }
    }
    vm->gc_threshold = vm->used_mem * 3 + (1 << 20); /* next trigger: 3x post-GC usage */
}

static void vm_execute_thread(VmThread *t) {
    g_cur_thread = t;
    VM *vm = t->vm;
    Value *R = t->R;
    enum { FRAME_REGS = VM_FRAME_REGS };
    /* 锟斤拷锟街斤拷锟诫（锟斤拷锟斤拷锟津，猴拷全锟斤拷锟斤拷锟斤�??锟竭程憋拷锟斤拷 */
    Bytecode *root = vm->code;

    /* 锟斤拷锟斤拷循锟斤拷锟斤拷锟斤拷锟斤拷时锟斤拷锟斤拷锟睫ｏ拷默锟斤拷120锟诫�??--time-limit 锟斤拷锟斤拷?? 指锟斤拷锟斤拷锟斤拷??00锟节ｏ拷双锟斤拷锟斤�??
     * 锟皆举斤拷锟斤拷锟斤拷指锟斤拷锟斤拷远锟斤拷锟斤拷原锟斤拷执锟叫ｏ拷每锟斤拷锟斤拷汀�?0~100锟斤拷锟斤拷锟斤拷指锟斤拷锟斤拷锟睫伙拷锟斤拷锟剿达拷锟斤拷�?     * 锟斤拷循锟斤拷锟斤拷时锟斤拷锟斤拷锟睫讹拷锟阶ｏ拷锟斤拷时锟斤拷锟剿筹拷??*/
    const long long exec_limit = 50000000000LL;
    DWORD exec_t0 = GetTickCount64();
    long long exec_count = 0;

        t->running = true;
    t->exc_depth = 0;
    /* fast path: run CHECK_INTERVAL instructions between full checks */
    int fast_left = CHECK_INTERVAL;
    while (t->running && t->ip < t->code->count) {
        if (--fast_left <= 0) {
            fast_left = CHECK_INTERVAL;
            if (vm->step_mode && t->is_main) fast_left = 1;
            if (t->stop_flag) { t->running = false; break; }
            if (t->paused) {
                if (t->wake_at && GetTickCount64() >= t->wake_at) {
                    t->paused = false;
                    t->wake_at = 0;
                } else {
                    Sleep(1);
                    continue;
                }
            }
            if (t->stop_flag) { t->running = false; break; }
            /* GC safe point: cooperative stop-the-world */
            if (vm->gc_stop) {
                if (t->is_task) { SwitchToFiber(t->fiber_sched); continue; }  /* task: yield; scheduler runs GC */
                if (!t->gc_parked) { t->gc_parked = 1; InterlockedIncrement((volatile LONG*)&vm->gc_parked); }
                while (vm->gc_stop) Sleep(1);
                t->gc_parked = 0; InterlockedDecrement((volatile LONG*)&vm->gc_parked);
            } else if (vm->gc_pending) {
                if (t->is_task) { SwitchToFiber(t->fiber_sched); continue; }  /* task: yield; scheduler runs GC */
                vm->gc_pending = 0;
                if (vm->gc_enabled) {
                    vm->gc_stop = 1;
                    int want = 0;
                    for (int i = 0; i < VM_MAX_THREADS; i++) {
                        VmThread *tt = vm->threads[i];
                        if (tt && tt != t && tt->running) want++;
                    }
                    int spins = 0;
                    while (vm->gc_parked < want && spins < 20000) { Sleep(1); spins++; }
                    gc_collect(vm);
                    vm->gc_stop = 0;
                    fast_left = 1;
                }
            }
            if (t->jump_req >= 0) {
                /* thread jump request: redirect execution to label offset */
                t->ip = t->jump_req;
                t->jump_req = -1;
                t->sp = -1;
                t->base = 0;
                t->frame_count = 0;
                t->exc_depth = 0;
                t->paused = false;
                t->wake_at = 0;
            }
            exec_count += CHECK_INTERVAL;
            if (vm->record_autosave_interval > 0 && GetTickCount64() - vm->record_last_autosave >= vm->record_autosave_interval) {
                vm->record_last_autosave = GetTickCount64();
                record_save_to_file(vm, vm->record_save_path ? vm->record_save_path : "save.dat");
            }
            if ((exec_count & 0xFFFFF) == 0) {
                if (vm->record_autosave_interval > 0 && GetTickCount64() - vm->record_last_autosave >= vm->record_autosave_interval) {
                    vm->record_last_autosave = GetTickCount64();
                    record_save_to_file(vm, vm->record_save_path ? vm->record_save_path : "save.dat");
                }
                if (!(t->flags & THREAD_FLAG_ENDLESS) && vm->exec_timeout_ms > 0 && GetTickCount64() - exec_t0 > vm->exec_timeout_ms) {
                    fprintf(stderr, "\n[exec] timeout %lu ms, auto-exit.\n", vm->exec_timeout_ms);
                    t->running = false;
                    break;
                }
            }
            if (vm->limit_time > 0 && (double)(GetTickCount64() - vm->t_start) / 1000.0 > vm->limit_time) {
                fprintf(stderr, "\n[resource limit] time %.2fs exceeds declared limit %.2fs, auto-exit.\n",
                        (double)(GetTickCount64() - vm->t_start) / 1000.0, vm->limit_time);
                t->running = false;
                break;
            }
            if (vm->limit_inst > 0 && (double)exec_count > vm->limit_inst) {
                fprintf(stderr, "\n[resource limit] instructions %lld exceed declared limit %.0f, auto-exit.\n", exec_count, vm->limit_inst);
                t->running = false;
                break;
            }
            if (exec_count > exec_limit) {
                fprintf(stderr, "\n[exec] instruction limit %lld exceeded, auto-exit.\n", exec_limit);
                t->running = false;
                break;
            }
            if (vm->step_mode && t->is_main) {
                vm->step_mode = false;
                vm->ip = t->ip;
                exec_t0 = GetTickCount64();
                if (vm->debug_hook) {
                    vm->debug_hook(vm);
                } else if (vm->dbg_active) {
                    /* script debugger (.im) attached: stop at the boundary and
                       wait until the debugger thread clears dbg_pause.
                       Watchdog: if the debugger is unresponsive, release and
                       keep running (a broken debugger must never deadlock the VM). */
                    vm->dbg_at_boundary = 1;
                    vm->dbg_pause = 1;  /* auto-stop at every boundary */
                    vm->dbg_boundary_count++;  /* fresh-stop marker for the debugger */
                    unsigned long long dbg_wait_t0 = GetTickCount64();
                    while (vm->dbg_pause && t->running && !t->stop_flag) {
                        if (GetTickCount64() - dbg_wait_t0 > 15000) {
                            vm->dbg_pause = 0;
                            fprintf(stderr, "[dbg] watchdog: debugger unresponsive, continuing\n");
                            break;
                        }
                        Sleep(2);
                    }
                    vm->dbg_at_boundary = 0;
                    fast_left = 2;  /* resume: execute at least one instruction before the next boundary check */
                }
            }
        }

    


        RegInstruction ins = t->code->code[t->ip++];
        if (t->is_task && t->budget > 0 && --t->budget == 0) {
            t->budget = VM_TASK_BUDGET;
            SwitchToFiber(t->fiber_sched);
        }
        
        switch (ins.op) {
        case OP_MOV: goto L_MOV;
        case OP_LOADK_INT: goto L_LOADK_INT;
        case OP_LOADK_FLOAT: goto L_LOADK_FLOAT;
        case OP_LOADK_STRING: goto L_LOADK_STRING;
        case OP_LOADK_BOOL: goto L_LOADK_BOOL;
        case OP_ADD: goto L_ADD;
        case OP_CONCAT: goto L_CONCAT;
        case OP_SUB: goto L_SUB;
        case OP_MUL: goto L_MUL;
        case OP_DIV: goto L_DIV;
        case OP_NEG: goto L_NEG;
        case OP_EQ: goto L_EQ;
        case OP_NEQ: goto L_NEQ;
        case OP_LT: goto L_LT;
        case OP_GT: goto L_GT;
        case OP_LE: goto L_LE;
        case OP_GE: goto L_GE;
        case OP_AND: goto L_AND;
        case OP_OR: goto L_OR;
        case OP_NOT: goto L_NOT;
        case OP_NEW_ARRAY: goto L_NEW_ARRAY;
        case OP_INDEX_GET: goto L_INDEX_GET;
        case OP_INDEX_SET: goto L_INDEX_SET;
        case OP_LOAD_GLOBAL: goto L_LOAD_GLOBAL;
        case OP_STORE_GLOBAL: goto L_STORE_GLOBAL;
        case OP_JUMP: goto L_JUMP;
        case OP_JUMP_IF_FALSE: goto L_JUMP_IF_FALSE;
        case OP_JUMP_IF_TRUE: goto L_JUMP_IF_TRUE;
        case OP_CALL_BUILTIN: goto L_CALL_BUILTIN;
        case OP_PUSH_REG: goto L_PUSH_REG;
        case OP_POP_REG: goto L_POP_REG;
        case OP_SAY: goto L_SAY;
        case OP_WAIT: goto L_WAIT;
        case OP_STOP: goto L_STOP;
        case OP_HALT: goto L_HALT;
        case OP_YIELD: goto L_YIELD;
        case OP_CALL_FUNC: goto L_CALL_FUNC;
        case OP_RETURN: goto L_RETURN;
        case OP_IS_NIL: goto L_IS_NIL;
        case OP_THREAD_START: goto L_THREAD_START;
        case OP_THREAD_CTRL: goto L_THREAD_CTRL;
        case OP_THREAD_JOIN: goto L_THREAD_JOIN;
        case OP_THREAD_WAIT: goto L_THREAD_WAIT;
        case OP_THREAD_STATE: goto L_THREAD_STATE;
        case OP_LOCK: goto L_LOCK;
        case OP_SEND: goto L_SEND;
        case OP_RECV: goto L_RECV;
        case OP_NEW_DICT: goto L_NEW_DICT;
        case OP_EQK: goto L_EQK;
        case OP_NEQK: goto L_NEQK;
        case OP_DECLARE: goto L_DECLARE;
        case OP_RECORD: goto L_RECORD;
        case OP_MOD: goto L_MOD;
        case OP_NEW_SET: goto L_NEW_SET;
        case OP_SET_INTERVAL: goto L_SET_INTERVAL;
        case OP_IN: goto L_IN;
        case OP_MIN: goto L_MIN;
        case OP_MAX: goto L_MAX;
        case OP_BE: goto L_BE;
        case OP_TRY_START: goto L_TRY_START;
        case OP_TRY_END: goto L_TRY_END;
        case OP_THROW: goto L_THROW;
        case OP_SET_ADD: goto L_SET_ADD;
        case OP_THREAD_GOTO: goto L_THREAD_GOTO;
            default:
                fprintf(stderr, "[vm] bad opcode %d at ip %d\n", ins.op, t->ip - 1);
                t->running = false;
                continue;
        }

        /* ---------- 锟斤拷锟斤拷锟狡讹拷 ---------- */
        L_MOV:
            R[ins.r1] = R[ins.r2];
            continue;
        L_LOADK_INT:
            R[ins.r1].type = VAL_INT;
            R[ins.r1].ival = ins.r2;
            continue;
        L_LOADK_FLOAT:
            R[ins.r1].type = VAL_FLOAT;
            R[ins.r1].fval = t->code->float_pool[ins.r2];
            continue;
        L_LOADK_STRING: {
            int sidx = ins.r2;
            const char *s = NULL;
            if (sidx >= 0 && sidx < t->code->string_count && t->code->str_interned) {
                s = t->code->str_interned[sidx];
                if (!s) {
                    s = vm_intern(vm, t->code->string_pool[sidx]);
                    t->code->str_interned[sidx] = (char*)s;
                }
            } else {
                s = vm_intern(vm, t->code->string_pool[sidx]);
            }
            R[ins.r1].type = VAL_STRING;
            R[ins.r1].ival = (s != NULL) ? 1 : 0;  /* 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷通锟街凤拷锟斤拷 */
            R[ins.r1].sval = (char*)(s ? s : strdup(t->code->string_pool[sidx]));
            continue;
        }
        L_LOADK_BOOL:
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = ins.r2 ? 1 : 0;
            continue;

        /* ---------- 锟斤拷锟斤拷 ---------- */
        L_ADD: {
            Value *a = &R[ins.r2], *b = &R[ins.r3];
            if (a->type == VAL_ARRAY || b->type == VAL_ARRAY || a->type == VAL_DICT || b->type == VAL_DICT) {
                fprintf(stderr, "Error: '+' is not defined for arrays/dicts (arr + [x] silently loses data; use push(arr, x) instead)\n");
                exit(1);
            }
            if (a->type == VAL_SET || b->type == VAL_SET) {
                if (a->type == VAL_SET && b->type == VAL_SET) {
                    int n = set_union(vm, a->ival, b->ival);
                    if (n < 0) { R[ins.r1].type = VAL_NIL; }
                    else { R[ins.r1].type = VAL_SET; R[ins.r1].ival = n; }
                } else { R[ins.r1].type = VAL_NIL; }
                continue;
            }
            if (a->type == VAL_STRING || b->type == VAL_STRING) {
                const char *sa = a->sval?a->sval:"", *sb;
 char sbuf3[128];
 if (b->type == VAL_STRING) sb = b->sval?b->sval:"";
 else { value_to_string(vm, b, sbuf3, sizeof sbuf3, 0); sb = sbuf3; }
                size_t la = strlen(sa), lb = strlen(sb);
                char *nb = malloc(la + lb + 1);
                if (!nb) { R[ins.r1].type = VAL_STRING; R[ins.r1].ival = 0; R[ins.r1].sval = strdup(""); }
                else { memcpy(nb, sa, la); memcpy(nb+la, sb, lb); nb[la+lb] = 0; R[ins.r1].type = VAL_STRING; R[ins.r1].ival = 0; R[ins.r1].sval = nb; }
            } else if (a->type == VAL_INT && b->type == VAL_INT) {
                int64_t r64 = (int64_t)a->ival + (int64_t)b->ival;
                if (r64 > 2147483647LL || r64 < -2147483648LL) {
                    R[ins.r1].type = VAL_FLOAT;
                    R[ins.r1].fval = (double)r64;
                } else {
                    R[ins.r1].type = VAL_INT;
                    R[ins.r1].ival = (int)r64;
                }
            } else {
                double da = val_as_double(a), db = val_as_double(b);
                R[ins.r1].type = VAL_FLOAT;
                R[ins.r1].fval = da + db;
            }
            continue;
        }
        L_CONCAT: {
            /* chain concat: r1 = left-assoc fold(+) of R[r2 .. r2+r3-1].
               Same per-step semantics as L_ADD; all-string chains allocate once.
               Results are always marked heap-owned (ival=0), so later stack/global
               transfers cannot mistake a fresh concat buffer for an interned string. */
            int first = ins.r2, count = ins.r3, rres = ins.r1;
            if (count <= 0) { R[rres].type = VAL_NIL; R[rres].ival = 0; R[rres].fval = 0; R[rres].sval = NULL; continue; }
            if (count == 1) { R[rres] = R[first]; continue; }
            /* trap: array/dict operands in a + chain silently lose data (arr + [x]); reject */
            for (int _ci = 0; _ci < count; _ci++) {
                int _vt = R[first + _ci].type;
                if (_vt == VAL_ARRAY || _vt == VAL_DICT) {
                    fprintf(stderr, "Error: '+' is not defined for arrays/dicts (arr + [x] silently loses data; use push(arr, x) instead)\n");
                    exit(1);
                }
            }
            /* fast path: all operands are strings -> single allocation */
            {
                int all_str = 1;
                size_t total = 0;
                for (int i = 0; i < count; i++) {
                    Value *v = &R[first + i];
                    if (v->type != VAL_STRING) { all_str = 0; break; }
                    size_t l = strlen(v->sval ? v->sval : "");
                    if (total > (size_t)-1 - l - 1) { all_str = 0; total = 0; break; }
                    total += l;
                }
                if (all_str) {
                    char *nb = malloc(total + 1);
                    if (!nb) { R[rres].type = VAL_STRING; R[rres].ival = 0; R[rres].sval = strdup(""); continue; }
                    size_t off = 0;
                    for (int i = 0; i < count; i++) {
                        const char *s = R[first + i].sval ? R[first + i].sval : "";
                        size_t l = strlen(s);
                        memcpy(nb + off, s, l);
                        off += l;
                    }
                    nb[total] = 0;
                    R[rres].type = VAL_STRING;
                    R[rres].sval = nb;
                    continue;  /* ival intentionally left stale (see header note) */
                }
            }
            /* general path: left-assoc fold, identical semantics to repeated OP_ADD.
               Intermediate buffers allocated by this fold are freed here; the first
               operand's buffer belongs to its register and is left untouched. */
            {
                Value acc = R[first];
                int acc_fold_owned = 0;  /* acc.sval was allocated by this fold */
                for (int i = 1; i < count; i++) {
                    Value b = R[first + i];
                    if (acc.type == VAL_SET || b.type == VAL_SET) {
                        if (acc.type == VAL_SET && b.type == VAL_SET) {
                            int n = set_union(vm, acc.ival, b.ival);
                            if (n < 0) { acc.type = VAL_NIL; acc.ival = 0; acc.fval = 0; acc.sval = NULL; }
                            else { acc.type = VAL_SET; acc.ival = n; acc.fval = 0; acc.sval = NULL; }
                        } else {
                            if (acc_fold_owned && acc.type == VAL_STRING && acc.sval) free(acc.sval);
                            acc.type = VAL_NIL; acc.ival = 0; acc.fval = 0; acc.sval = NULL;
                        }
                        acc_fold_owned = 0;
                        continue;
                    }
                    if (acc.type == VAL_STRING || b.type == VAL_STRING) {
                        const char *sa = acc.sval ? acc.sval : "", *sb;
 char sbuf2[128];
 if (b.type == VAL_STRING) sb = b.sval ? b.sval : "";
 else { value_to_string(vm, &b, sbuf2, sizeof sbuf2, 0); sb = sbuf2; }
                        size_t la = strlen(sa), lb = strlen(sb);
                        char *nb = malloc(la + lb + 1);
                        if (!nb) {
                            if (acc_fold_owned && acc.sval) free(acc.sval);
                            acc.type = VAL_STRING; acc.sval = strdup(""); acc.ival = 0;
                            continue;
                        }
                        memcpy(nb, sa, la); memcpy(nb + la, sb, lb); nb[la + lb] = 0;
                        if (acc_fold_owned && acc.sval) free(acc.sval);
                        acc.type = VAL_STRING; acc.sval = nb; acc.ival = 0;
                        acc_fold_owned = 1;
                        continue;
                    }
                    if (acc.type == VAL_INT && b.type == VAL_INT) {
                        acc.type = VAL_INT;
                        acc.ival = acc.ival + b.ival;
                        acc_fold_owned = 0;
                        continue;
                    }
                    {
                        double da = val_as_double(&acc), db = val_as_double(&b);
                        acc.type = VAL_FLOAT;
                        acc.fval = da + db;
                        acc.ival = 0; acc.sval = NULL;
                        acc_fold_owned = 0;
                    }
                }
                if (acc.type == VAL_STRING) acc.ival = 0;
                R[rres] = acc;
            }
            continue;
        }
        L_SUB: {
            Value *a = &R[ins.r2], *b = &R[ins.r3];
            if (a->type == VAL_SET || b->type == VAL_SET) {
                if (a->type == VAL_SET && b->type == VAL_SET) {
                    int n = set_diff(vm, a->ival, b->ival);
                    if (n < 0) { R[ins.r1].type = VAL_NIL; }
                    else { R[ins.r1].type = VAL_SET; R[ins.r1].ival = n; }
                } else { R[ins.r1].type = VAL_NIL; }
                continue;
            }
            if (a->type == VAL_INT && b->type == VAL_INT) {
                int64_t r64 = (int64_t)a->ival - (int64_t)b->ival;
                if (r64 > 2147483647LL || r64 < -2147483648LL) {
                    R[ins.r1].type = VAL_FLOAT;
                    R[ins.r1].fval = (double)r64;
                } else {
                    R[ins.r1].type = VAL_INT;
                    R[ins.r1].ival = (int)r64;
                }
            } else {
                double res = val_as_double(a) - val_as_double(b);
                R[ins.r1].type = VAL_FLOAT;
                R[ins.r1].fval = res;
            }
            continue;
        }
        L_MUL: {
            Value *a = &R[ins.r2], *b = &R[ins.r3];
            if (a->type == VAL_SET || b->type == VAL_SET) {
                if (a->type == VAL_SET && b->type == VAL_SET) {
                    int n = set_intersect(vm, a->ival, b->ival);
                    if (n < 0) { R[ins.r1].type = VAL_NIL; }
                    else { R[ins.r1].type = VAL_SET; R[ins.r1].ival = n; }
                } else { R[ins.r1].type = VAL_NIL; }
                continue;
            }
            if (a->type == VAL_INT && b->type == VAL_INT) {
                int64_t r64 = (int64_t)a->ival * (int64_t)b->ival;
                if (r64 > 2147483647LL || r64 < -2147483648LL) {
                    R[ins.r1].type = VAL_FLOAT;
                    R[ins.r1].fval = (double)r64;
                } else {
                    R[ins.r1].type = VAL_INT;
                    R[ins.r1].ival = (int)r64;
                }
            } else {
                double res = val_as_double(a) * val_as_double(b);
                R[ins.r1].type = VAL_FLOAT;
                R[ins.r1].fval = res;
            }
            continue;
        }
        L_DIV: {
            Value *a = &R[ins.r2], *b = &R[ins.r3];
            if (a->type == VAL_INT && b->type == VAL_INT && b->ival == 0) {
                vm_throw_msg(vm, "division by zero");
                R = t->reg + t->base;
                continue;
            }
            double res = val_as_double(a) / val_as_double(b);
            R[ins.r1].type = VAL_FLOAT;
            R[ins.r1].fval = res;
            continue;
        }
        L_NEG: {
            Value *v = &R[ins.r2];
            if (v->type == VAL_INT) {
                if (v->ival == -2147483647 - 1) {
                    R[ins.r1].type = VAL_FLOAT;
                    R[ins.r1].fval = 2147483648.0;
                } else {
                    R[ins.r1].type = VAL_INT;
                    R[ins.r1].ival = -v->ival;
                }
            } else {
                R[ins.r1].type = VAL_FLOAT;
                R[ins.r1].fval = -val_as_double(v);
            }
            continue;
        }

        /* ---------- 锟饺斤拷 ---------- */
        L_EQ: {
            /* 注锟解：锟斤拷锟斤拷值锟斤拷写锟斤拷锟斤拷锟絩esult 锟斤拷锟杰革拷锟斤拷 left锟斤拷锟斤拷写锟斤拷锟斤拷染锟斤拷锟斤拷锟斤拷锟斤拷 */
            int eqres;
            if (R[ins.r2].type == VAL_SET && R[ins.r3].type == VAL_SET)
                eqres = set_equal(vm, R[ins.r2].ival, R[ins.r3].ival);
            else eqres = val_eq(&R[ins.r2], &R[ins.r3]) ? 1 : 0;
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = eqres;
            continue;
        }
        L_NEQ: {
            int eq;
            if (R[ins.r2].type == VAL_SET && R[ins.r3].type == VAL_SET)
                eq = set_equal(vm, R[ins.r2].ival, R[ins.r3].ival);
            else eq = val_eq(&R[ins.r2], &R[ins.r3]) ? 1 : 0;
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = !eq ? 1 : 0;
            continue;
        }
        L_LT: {
            int c = val_cmp(&R[ins.r2], &R[ins.r3]);
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (c < 0) ? 1 : 0;
            continue;
        }
        L_GT: {
            int c = val_cmp(&R[ins.r2], &R[ins.r3]);
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (c > 0) ? 1 : 0;
            continue;
        }
        L_LE: {
            int c = val_cmp(&R[ins.r2], &R[ins.r3]);
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (c <= 0) ? 1 : 0;
            continue;
        }
        L_GE: {
            int c = val_cmp(&R[ins.r2], &R[ins.r3]);
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (c >= 0) ? 1 : 0;
            continue;
        }

        /* ---------- 锟竭硷拷 ---------- */
        L_AND: {
            Value va = R[ins.r2], vb = R[ins.r3];
            int a = (va.type == VAL_BOOL) ? (va.ival != 0) : (va.type == VAL_INT) ? (va.ival != 0) : (va.type == VAL_FLOAT) ? (va.fval != 0.0) : (va.type == VAL_NIL) ? 0 : 1;
            int b = (vb.type == VAL_BOOL) ? (vb.ival != 0) : (vb.type == VAL_INT) ? (vb.ival != 0) : (vb.type == VAL_FLOAT) ? (vb.fval != 0.0) : (vb.type == VAL_NIL) ? 0 : 1;
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (a && b) ? 1 : 0;
            continue;
        }
        L_OR: {
            Value va = R[ins.r2], vb = R[ins.r3];
            int a = (va.type == VAL_BOOL) ? (va.ival != 0) : (va.type == VAL_INT) ? (va.ival != 0) : (va.type == VAL_FLOAT) ? (va.fval != 0.0) : (va.type == VAL_NIL) ? 0 : 1;
            int b = (vb.type == VAL_BOOL) ? (vb.ival != 0) : (vb.type == VAL_INT) ? (vb.ival != 0) : (vb.type == VAL_FLOAT) ? (vb.fval != 0.0) : (vb.type == VAL_NIL) ? 0 : 1;
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (a || b) ? 1 : 0;
            continue;
        }
        L_NOT: {
            Value *v = &R[ins.r2];
            int truth = (v->type == VAL_BOOL) ? (v->ival != 0) :
                        (v->type == VAL_INT)  ? (v->ival != 0) :
                        (v->type == VAL_FLOAT) ? (v->fval != 0.0) :
                        (v->type == VAL_NIL)  ? 0 : 1; /* 锟斤拷锟斤拷锟斤拷锟斤拷(锟街凤拷??锟斤拷锟斤拷/锟街碉拷)为锟斤拷,??JUMP_IF_FALSE 一??*/
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = truth ? 0 : 1;
            continue;
        }

        /* ---------- 锟斤拷锟斤拷 ---------- */
        L_NEW_ARRAY: {
            int aidx = vm_array_new(vm);
            if (aidx < 0) {
                /* 锟斤拷锟斤拷:锟斤拷锟斤拷栈锟斤拷元锟斤拷锟劫凤拷??nil,锟斤拷锟斤拷栈锟斤拷??*/
                int n = ins.r3;
                for (int i = 0; i < n; i++) {
                    if (t->sp >= 0) {
                        if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1) free(t->stack[t->sp].sval);
                        t->sp--;
                    }
                }
                R[ins.r1].type = VAL_NIL;
                continue;
            }
            int n = ins.r3;
            /* 元锟斤拷锟斤拷锟斤拷 PUSH_REG 压栈锟斤拷栈锟斤拷锟斤拷锟斤拷锟揭伙拷锟皆拷兀锟斤拷锟斤拷锟剿筹拷锟斤拷占�?*/
            if (n > 0 && t->sp >= n - 1) {
                int base = t->sp - (n - 1);
                vm_array_push_n(vm, aidx, &t->stack[base], n);
                /* 锟斤拷栈锟斤拷锟酵凤拷压栈时锟斤拷锟狡碉拷锟街凤拷锟斤拷??*/
                for (int i = 0; i < n; i++) {
                    if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                        free(t->stack[t->sp].sval);
                    t->sp--;
                }
            }
            R[ins.r1].type = VAL_ARRAY;
            R[ins.r1].ival = aidx + 1;
            continue;
        }
        L_NEW_DICT: {
            int aidx = vm_array_new(vm);
            if (aidx < 0) {
                /* 锟斤拷锟斤拷:锟斤拷锟斤拷栈锟斤拷 2*n 锟斤拷元锟斤拷锟劫凤拷锟斤拷 nil,锟斤拷锟斤拷栈锟斤拷??*/
                int n = ins.r3;
                for (int i = 0; i < 2 * n; i++) {
                    if (t->sp >= 0) {
                        if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                            free(t->stack[t->sp].sval);
                        t->sp--;
                    }
                }
                R[ins.r1].type = VAL_NIL;
                continue;
            }
            int n = ins.r3;
            /* 锟斤拷栈??2*n 锟斤拷锟斤拷锟斤拷锟斤拷 key/value锟斤拷栈锟斤拷锟斤拷锟斤拷锟斤拷??value??*/
            if (n > 0 && t->sp >= 2 * n - 1) {
                int base = t->sp - (2 * n - 1);
                vm_array_push_n(vm, aidx, &t->stack[base], 2 * n);
                for (int i = 0; i < 2 * n; i++) {
                    if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                        free(t->stack[t->sp].sval);
                    t->sp--;
                }
            }
            R[ins.r1].type = VAL_DICT;
            R[ins.r1].ival = aidx + 1;
            continue;
        }
        L_INDEX_GET: {
            Value *obj = &R[ins.r2];
            Value *idxv = &R[ins.r3];
            if (obj->type == VAL_ARRAY) {
                int i = (idxv->type == VAL_INT) ? idxv->ival : (int)val_as_double(idxv);
                R[ins.r1] = vm_array_get(vm, obj->ival - 1, i); /* out-of-range read stays nil (compat) */
            } else if (obj->type == VAL_DICT) {
                int aidx = obj->ival - 1;
                if (idxv->type == VAL_INT) {
                    /* 锟斤拷锟斤拷锟斤拷锟斤拷 ????i ??key锟斤拷锟斤拷 for x in d 锟斤拷锟斤拷锟斤拷锟斤拷 */
                    int i = idxv->ival;
                    VM_LOCK(vm);
                    Value v; v.type = VAL_NIL; v.ival = 0; v.fval = 0; v.sval = NULL;
                    if (aidx >= 0 && aidx < vm->arrayCount && i >= 0 && i * 2 < vm_pool_slot(vm, aidx)->count) {
                        value_copy(&v, &vm_pool_slot(vm, aidx)->items[i * 2]);
                        if (v.type == VAL_STRING && v.sval && v.ival != 1) {
                            const char *np = vm_intern(vm, v.sval);
                            if (np) { free(v.sval); v.sval = (char*)np; v.ival = 1; }
                        }
                    }
                    VM_UNLOCK(vm);
                    R[ins.r1] = v;
                } else {
                    R[ins.r1] = vm_dict_get(vm, aidx, idxv);
                }
            } else if (obj->type == VAL_STRING) {
                /* 锟街凤拷锟斤拷锟斤拷锟街斤拷锟斤拷锟斤拷 s[i] ??锟斤拷锟街凤拷锟街凤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷尾锟斤拷锟斤拷 */
                int i = (idxv->type == VAL_INT) ? idxv->ival : (int)val_as_double(idxv);
                const char *s = obj->sval ? obj->sval : "";
                int len = (int)strlen(s);
                if (i < 0) i = len + i;
                if (i >= 0 && i < len) {
                    char buf[2] = { s[i], '\0' };
                    R[ins.r1].type = VAL_STRING;
                    R[ins.r1].sval = strdup(buf);
                    R[ins.r1].ival = 0; R[ins.r1].fval = 0;
                } else {
                    R[ins.r1].type = VAL_NIL; R[ins.r1].ival = 0; R[ins.r1].fval = 0; R[ins.r1].sval = NULL;
                }
            } else {
                R[ins.r1].type = VAL_NIL; R[ins.r1].ival = 0; R[ins.r1].fval = 0; R[ins.r1].sval = NULL;
            }
            continue;
        }
        L_INDEX_SET: {
            Value *obj = &R[ins.r1];
            Value *idxv = &R[ins.r2];
            Value *valv = &R[ins.r3];
            if (obj->type == VAL_ARRAY) {
                int i = (idxv->type == VAL_INT) ? idxv->ival : (int)val_as_double(idxv);
                if (i < 0) {
                    vm_throw_msg(vm, "index out of range");
                    R = t->reg + t->base;
                    continue;
                }
                vm_array_set(vm, obj->ival - 1, i, valv);
            } else if (obj->type == VAL_DICT) {
                vm_dict_set(vm, obj->ival - 1, idxv, valv);
            }
            continue;
        }

        /* ---------- 全锟街憋拷锟斤拷 ---------- */
        L_LOAD_GLOBAL: {
            int idx = ins.r2;
            int need_lock = (vm->active_threads > 1);
            if (need_lock) im_mutex_lock((ImMutex*)VM_GSHARD(vm, idx));
            if (idx >= 0 && idx < vm->globalCount) {
                R[ins.r1] = vm->globals[idx].val;
                if (R[ins.r1].type == VAL_STRING && R[ins.r1].sval && R[ins.r1].ival != 1) {
                    const char *np = vm_intern(vm, R[ins.r1].sval);
                    if (np) { R[ins.r1].sval = (char*)np; R[ins.r1].ival = 1; }
                }
            } else {
                R[ins.r1].type = VAL_NIL;
            }
            if (need_lock) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
            continue;
        }
        L_STORE_GLOBAL: {
            int idx = ins.r1;
            int src_reg = ins.r2;
            int need_lock = (vm->active_threads > 1);
            if (need_lock) im_mutex_lock((ImMutex*)VM_GSHARD(vm, idx));
            if (idx >= vm->globalCap) {
                if (need_lock) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
                vm_global_grow(vm, idx);   /* 鐙崰鍏ㄩ儴閿佹墿瀹癸紙姝ゆ椂鏈寔浠讳綍鍒嗙墖锛?*/
                if (need_lock) im_mutex_lock((ImMutex*)VM_GSHARD(vm, idx));
            }
            if (idx >= vm->globalCount) {
                for (int i = vm->globalCount; i <= idx; i++) {
                    vm->globals[i].name = NULL;
                    vm->globals[i].val.type = VAL_NIL;
                }
                vm->globalCount = idx + 1;
            }
            Value newv = R[src_reg];
            if (idx >= 0 && vm->be_bound[idx] > 0) {
                int bidx = vm->be_bound[idx] - 1;
                if (!set_contains(vm, bidx, &newv)) {
                    if (need_lock) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
                    vm_throw_msg(vm, "be: value out of range");
                    R = t->reg + t->base;
                    continue;
                }
            }
            if (newv.type == VAL_STRING && newv.sval && newv.ival != 1) {
                const char *np = vm_intern(vm, newv.sval);
                if (np) { newv.sval = (char*)np; newv.ival = 1; }
            }
            value_free(&vm->globals[idx].val);
            vm->globals[idx].val = newv;
            if (idx < vm->record_meta_count) vm->record_meta[idx].dirty = 1;
            if (need_lock) im_mutex_unlock((ImMutex*)VM_GSHARD(vm, idx));
            continue;
        }

        /* ---------- 锟斤拷锟斤拷??---------- */
        L_JUMP:
            t->ip = ins.r2;
            continue;
        L_JUMP_IF_FALSE: {
            int cond = (R[ins.r1].type == VAL_BOOL) ? R[ins.r1].ival == 0 :
                       (R[ins.r1].type == VAL_INT)  ? R[ins.r1].ival == 0 :
                       (R[ins.r1].type == VAL_FLOAT) ? R[ins.r1].fval == 0.0 :
                       (R[ins.r1].type == VAL_NIL)  ? 1 : 0;
            if (cond) t->ip = ins.r2;
            continue;
        }
        L_JUMP_IF_TRUE: {
            int cond = (R[ins.r1].type == VAL_BOOL) ? R[ins.r1].ival != 0 :
                       (R[ins.r1].type == VAL_INT)  ? R[ins.r1].ival != 0 :
                       (R[ins.r1].type == VAL_FLOAT) ? R[ins.r1].fval != 0.0 :
                       (R[ins.r1].type == VAL_ARRAY) ? 1 :
                       (R[ins.r1].type == VAL_NIL)  ? 0 : 0;
            if (cond) t->ip = ins.r2;
            continue;
        }

        /* ---------- 栈锟斤拷??---------- */
        L_PUSH_REG: {
            Value *v = &R[ins.r1];
            switch (v->type) {
                case VAL_INT:   push_int(vm, v->ival); break;
                case VAL_FLOAT: push_float(vm, v->fval); break;
                case VAL_STRING: {
                    if (t->sp >= 1023) continue;
                    t->sp++;
                    t->stack[t->sp].type = VAL_STRING;
                    t->stack[t->sp].ival = v->ival;
                    t->stack[t->sp].sval = (v->ival == 1) ? v->sval : strdup(v->sval);
                    break;
                }
                case VAL_BOOL:  push_bool(vm, v->ival != 0); break;
                case VAL_ARRAY: {
                    if (t->sp >= 1023) continue;
                    t->sp++;
                    t->stack[t->sp] = *v; /* 浅锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷??VM 锟斤拷锟斤拷??*/
                    break;
                }
                case VAL_DICT: {
                    if (t->sp >= 1023) continue;
                    t->sp++;
                    t->stack[t->sp] = *v; /* 浅锟斤拷锟斤拷锟斤拷锟街碉拷??VM 锟斤拷锟斤拷??*/
                    break;
                }
                case VAL_SET: {
                    if (t->sp >= 1023) continue;
                    t->sp++;
                    t->stack[t->sp] = *v; /* shallow copy, set object lives in the vm set pool */
                    break;
                }
                default: push_nil(vm);
            }
            continue;
        }
        L_POP_REG: {
            if (t->sp >= 0) {
                R[ins.r1] = t->stack[t->sp];
                t->sp--;
            }
            continue;
        }

        /* ---------- 锟斤拷锟矫猴拷锟斤拷锟斤拷锟斤拷 ---------- */
        L_CALL_BUILTIN: {
            /* r2 = 锟斤拷锟斤拷锟斤拷锟斤拷锟街凤拷锟斤拷锟截碉拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街诧拷锟揭ｏ拷锟斤拷锟斤拷注锟斤拷顺锟斤拷影锟斤拷??*/
            const char *name = (ins.r2 >= 0 && ins.r2 < t->code->string_count)
                               ? t->code->string_pool[ins.r2] : NULL;
            if (name) {
                int nidx = ins.r2;
                if (t->code->str_interned && nidx >= 0 && nidx < t->code->string_count) {
                    const char *ni = t->code->str_interned[nidx];
                    if (!ni) {
                        ni = vm_intern(vm, name);
                        t->code->str_interned[nidx] = (char*)ni;
                    }
                    name = ni;
                } else {
                    const char *np = vm_intern(vm, name);
                if (np) name = np;  /* 锟斤拷锟斤拷时锟斤拷锟斤拷原指锟斤拷,??strcmp 锟斤拷锟斤拷 */
                }
                                {
                    int bi = builtin_lookup(vm, name);
                    if (bi >= 0) {
                        if (vm->safe_mode && (vm->builtins[bi].flags & 1)) {
                            /* code-injection guard: dangerous builtin blocked in safe mode */
                            char eb[256];
                            snprintf(eb, sizeof eb, "safe mode: builtin '%s' disabled", name);
                            vm_throw_msg(vm, eb);
                        } else if (vm->mod_caps >= 0 && (vm->builtins[bi].flags & CAP_MASK) &&
                                   (vm->mod_caps & vm->builtins[bi].flags) == 0) {
                            /* L1 minimal-permission: builtin needs a capability this mod did not declare */
                            char eb[256];
                            snprintf(eb, sizeof eb, "spi: builtin '%s' needs undeclared capability (declared 0x%x, need 0x%x)",
                                     name, vm->mod_caps, vm->builtins[bi].flags & CAP_MASK);
                            vm_throw_msg(vm, eb);
                        } else {
                            vm->cur_argc = ins.r3;
                            vm->builtins[bi].func(vm);
                        }
                    }
                }
            }
            if (t->sp >= 0) {
                R[ins.r1] = t->stack[t->sp];
                t->sp--;
            }
            continue;
        }

        /* ---------- 系统 ---------- */
        L_SAY: {
            char buf[1024];
            value_to_string(vm, &R[ins.r1], buf, sizeof(buf), 0);
            {
                char ob[1100];
                int on = snprintf(ob, sizeof(ob), "%s\n", buf);
                HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                DWORD wb = 0;
                WriteFile(hOut, ob, (DWORD)(on < 0 ? 0 : on), &wb, NULL);
            }
            continue;
        }
        L_WAIT: {
            Value *v = &R[ins.r1];
            double seconds = (v->type == VAL_INT) ? (double)v->ival :
                             (v->type == VAL_FLOAT) ? v->fval : 0.0;
            int ms = (int)(seconds * 1000);

            if (t->is_task && ms > 0) {
                /* task: hand control to scheduler until wake time */
                t->wake_at = GetTickCount64() + (unsigned long long)ms;
                t->blocked = 1;
                SwitchToFiber(t->fiber_sched);
                continue;
            }
            if (vm->step_mode && t->is_main && ms > 0) {
                /* debugger single-step: run the wait atomically so t->ip stays
                   stable (the frame-re-entry loop rewinds ip and confuses the
                   debugger display). The debugger blocks for the wait duration,
                   which matches the wait semantics. */
                sleep_ms(ms);
                ms = 0;
            } else if (vm->gui_pump && ms > 0) {
                /* GUI 模式: 锟斤拷片锟饺达拷锟斤拷锟饺达拷锟节硷拷锟斤拷锟竭筹拷也锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷�?锟斤拷锟津窗匡拷锟斤拷锟斤拷应锟斤拷
                   锟斤拷锟斤拷/X锟斤拷钮失效), 同时锟斤拷锟街达拷锟绞憋拷锟斤拷锟斤拷锟?--time-limit / declare time),
                   锟斤拷锟?GUI wait 循锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟街革拷畹硷拷鲁锟绞憋拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷�?*/
                ULONGLONG end = GetTickCount64() + (ULONGLONG)ms;
                                    if (t->wait_end == 0) t->wait_end = GetTickCount64() + (ULONGLONG)ms;
                    ULONGLONG now = GetTickCount64();
                    if (now < t->wait_end) {
                        if (vm->gui_pump()) { t->running = false; t->wait_end = 0; }
                        else if (vm->exec_timeout_ms > 0 && now - exec_t0 > vm->exec_timeout_ms) {
                            fprintf(stderr, "\n[timeout] execution exceeded %lu ms, auto-exit.\n", vm->exec_timeout_ms);
                            t->running = false; t->wait_end = 0;
                        }
                        else if (vm->limit_time > 0 && (double)(now - vm->t_start) / 1000.0 > vm->limit_time) {
                            fprintf(stderr, "\n[resource limit] time %.2fs exceeds declared limit %.2fs, auto-exit.\n",
                                    (double)(now - vm->t_start) / 1000.0, vm->limit_time);
                            t->running = false; t->wait_end = 0;
                        }
                        else {
                            /* Inimerse2D: per-frame callbacks (on_load / scene_x / on_update / on_render) */
                            int framePushed = 0;
                            if (vm->im2d_interval_ms > 0 && now >= vm->im2d_next_frame) {
                                vm->im2d_next_frame = now + (ULONGLONG)vm->im2d_interval_ms;
                                t->ip--;   /* re-enter L_WAIT after the callback frame returns */
                                framePushed = vm_frame_callback(vm, t);
                                if (framePushed) {
                                    R = t->reg + t->base;
                                    continue;
                                }
                                t->ip++;
                            }
                            Sleep(8);
                            t->ip--;
                            continue;
                        }
                    }
                    t->wait_end = 0;
            } else if (vm->im2d_interval_ms > 0 && ms > 0) {
                /* non-GUI + Inimerse2D: re-entrant wait with frame callbacks */
                if (t->wait_end == 0) t->wait_end = GetTickCount64() + (ULONGLONG)ms;
                ULONGLONG now2 = GetTickCount64();
                if (now2 < t->wait_end) {
                    int framePushed = 0;
                    if (now2 >= vm->im2d_next_frame) {
                        vm->im2d_next_frame = now2 + (ULONGLONG)vm->im2d_interval_ms;
                        t->ip--;
                        framePushed = vm_frame_callback(vm, t);
                        if (framePushed) {
                            R = t->reg + t->base;
                            continue;
                        }
                        t->ip++;
                    }
                    Sleep(8);
                    t->ip--;
                    continue;
                }
                t->wait_end = 0;
            } else {
                sleep_ms(ms);
            }
            if (vm->record_autosave_interval > 0 && GetTickCount64() - vm->record_last_autosave >= vm->record_autosave_interval) {
                vm->record_last_autosave = GetTickCount64();
                record_save_to_file(vm, vm->record_save_path ? vm->record_save_path : "save.dat");
            }
            continue;
        }
        L_STOP:
            t->running = false;
            continue;
        L_HALT:
            t->running = false;
            continue;

        /* ---------- 锟皆讹拷锟藉函锟斤拷锟斤�??---------- */
                L_YIELD: {
            /* task: cooperatively hand control back to the scheduler (non-task: no-op) */
            if (t->is_task && t->fiber_sched) SwitchToFiber(t->fiber_sched);
            continue;
        }
L_CALL_FUNC: {
            int fidx = ins.r1, res = ins.r2, argc = ins.r3;
            if (fidx >= 0 && fidx < root->func_count && root->func_names[fidx] && strncmp(root->func_names[fidx], "h", 1) == 0)
            if (fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL) {
                fprintf(stderr, "閿欒�? 鏃犳晥鍑芥暟绱㈠�?%d\n", fidx);
                t->running = false;
                vm->last_error = 1;
                continue;
            }
            if (t->sp + 1 < argc) {
                fprintf(stderr, "閿欒�? 鍙傛暟鏍堟孩鍑篭n");
                t->running = false;
                vm->last_error = 1;
                continue;
            }
            if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap) {
                fprintf(stderr, "閿欒�? 绾跨▼甯ф爤婧㈠嚭锛堣秴杩?%d 甯э級\n", VM_MAX_FRAMES);
                t->running = false;
                vm->last_error = 1;
                continue;
            }
            /* 锟斤拷锟斤拷锟斤拷梅锟斤拷锟??*/
            t->frame_code[t->frame_count] = t->code;
            t->frame_ip[t->frame_count] = t->ip;
            t->frame_base[t->frame_count] = t->base;
            t->frame_res[t->frame_count] = res;
            t->frame_count++;
            t->base += FRAME_REGS;
            Value *FR = t->reg + t->base;
            /* 锟斤拷锟斤拷锟斤拷锟斤拷权锟斤拷栈转锟狡ｏ拷锟姐拷锟斤拷锟斤拷锟斤拷栈锟斤拷锟斤拷锟斤拷锟揭伙拷锟斤拷锟??*/
            for (int i = argc - 1; i >= 0; i--) {
                FR[i + 1] = t->stack[t->sp];
                t->sp--;
            }
            /* frame_sp uses the same pre-increment frame slot as the other frame metadata. */
            t->frame_sp[t->frame_count - 1] = t->sp;   /* save sp AFTER popping args */
            
            t->code = root->funcs[fidx];
            R = FR;
            t->ip = 0;
            continue;
        }
        L_RETURN: {
            Value ret;
            if (ins.r1 > 0 && ins.r1 < FRAME_REGS) ret = R[ins.r1];
            else { ret.type = VAL_NIL; ret.ival = 0; ret.fval = 0; ret.sval = NULL; }
            if (t->frame_count > 0) {
                t->sp = t->frame_sp[t->frame_count];
                t->frame_count--;
                t->code = t->frame_code[t->frame_count];
                t->ip = t->frame_ip[t->frame_count];
                t->base = t->frame_base[t->frame_count];
                Value *OR = t->reg + t->base;
                R = OR;
                while (t->exc_depth > 0 && t->exc_stack[t->exc_depth - 1].frame_count >= t->frame_count) t->exc_depth--;
                int cres = t->frame_res[t->frame_count];
                if (cres >= 0 && cres < FRAME_REGS) {
                    OR[cres] = ret;  /* 浅锟斤拷锟斤拷锟斤拷锟街凤拷锟斤拷锟斤拷锟斤拷梅锟斤拷锟斤拷锟斤拷锟街★拷址锟斤拷锟斤拷锟斤拷头牛锟街革拷氡ｏ拷锟斤拷锟斤拷??*/
                }
            } else {
                t->running = false;
            }
            continue;
        }

        L_IS_NIL: {
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = (R[ins.r2].type == VAL_NIL) ? 1 : 0;
            continue;
        }
        L_EQK: {
            int sidx = ins.r3;
            const char *s = NULL;
            if (sidx >= 0 && sidx < t->code->string_count && t->code->str_interned) {
                s = t->code->str_interned[sidx];
                if (!s) { s = vm_intern(vm, t->code->string_pool[sidx]); t->code->str_interned[sidx] = (char*)s; }
            } else {
                s = vm_intern(vm, t->code->string_pool[sidx]);
            }
            Value sv;
            sv.type = VAL_STRING; sv.ival = (s != NULL) ? 1 : 0; sv.fval = 0;
            sv.sval = (char*)(s ? s : (t->code->string_pool[sidx] ? t->code->string_pool[sidx] : ""));
            Value opv = R[ins.r2];  /* 锟饺革拷锟狡诧拷锟斤拷锟斤拷锟斤拷r1 ??r2 锟斤拷锟斤拷同为 result 锟侥达拷??*/
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = val_eq(&opv, &sv) ? 1 : 0;
            continue;
        }
        L_NEQK: {
            int sidx = ins.r3;
            const char *s = NULL;
            if (sidx >= 0 && sidx < t->code->string_count && t->code->str_interned) {
                s = t->code->str_interned[sidx];
                if (!s) { s = vm_intern(vm, t->code->string_pool[sidx]); t->code->str_interned[sidx] = (char*)s; }
            } else {
                s = vm_intern(vm, t->code->string_pool[sidx]);
            }
            Value sv;
            sv.type = VAL_STRING; sv.ival = (s != NULL) ? 1 : 0; sv.fval = 0;
            sv.sval = (char*)(s ? s : (t->code->string_pool[sidx] ? t->code->string_pool[sidx] : ""));
            Value opv = R[ins.r2];  /* 锟饺革拷锟狡诧拷锟斤拷锟斤拷 */
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = val_eq(&opv, &sv) ? 0 : 1;
            continue;
        }

        L_DECLARE: {
            int kind = ins.r1;
            double v = (ins.r2 >= 0 && ins.r2 < t->code->float_count) ? t->code->float_pool[ins.r2] : 0.0;
            if (kind == 0) vm->limit_mem = (vm->limit_mem > 0.0 && vm->limit_mem < v) ? vm->limit_mem : v;
            else if (kind == 1) vm->limit_threads = (vm->limit_threads > 0 && vm->limit_threads < (int)v) ? vm->limit_threads : (int)v;
            else if (kind == 2) vm->limit_time = (vm->limit_time > 0.0 && vm->limit_time < v) ? vm->limit_time : v;
            else if (kind == 3) vm->limit_inst = (vm->limit_inst > 0.0 && vm->limit_inst < v) ? vm->limit_inst : v;
            else if (kind == 4) vm->limit_vram = (vm->limit_vram > 0.0 && vm->limit_vram < v) ? vm->limit_vram : v;
            continue;
        }

        L_RECORD: {
            int gidx = ins.r1;
            int packed = ins.r2;
            if (gidx == -1) { vm->record_default_store = packed &3; continue; }
            if (gidx >=0 && gidx <256) {
                int sidx = packed &0xFFFF;
                int meta = (packed >>16) &0xFF;
                const char *nm = (sidx >=0 && sidx < t->code->string_count && t->code->string_pool[sidx]) ? t->code->string_pool[sidx] : NULL;
                VM_LOCK(vm);
                if (gidx >= vm->record_meta_count) {
                    vm->record_meta = realloc(vm->record_meta, (gidx +1) * sizeof(*vm->record_meta));
                    for (int i = vm->record_meta_count; i <= gidx; i++) memset(&vm->record_meta[i],0, sizeof(*vm->record_meta));
                    vm->record_meta_count = gidx +1;
                }
                if (nm) {
                    if (gidx >= vm->record_names_cap) {
                        int nc = vm->record_names_cap ==0 ? 16 : vm->record_names_cap *2;
                        while (nc <= gidx) nc *=2;
                        vm->record_names = realloc(vm->record_names, nc * sizeof(char*));
                        for (int i = vm->record_names_cap; i < nc; i++) vm->record_names[i] = NULL;
                        vm->record_names_cap = nc;
                    }
                    free(vm->record_names[gidx]);
                    vm->record_names[gidx] = strdup(nm);
                }
                vm->record_meta[gidx].store = meta &3;
                vm->record_meta[gidx].scope = (meta >>2) &3;
                vm->record_meta[gidx].merge = (meta >>4) &1;
    if (vm->record_loaded_dict >0 && nm) {
                    Value found; found.type = VAL_NIL; found.ival =0; found.fval =0; found.sval = NULL;
                    ArrayObj *ld = vm_pool_slot(vm, vm->record_loaded_dict -1);
                    if (ld) {
                        for (int i =0; i +1 < ld->count; i +=2) {
                            Value *k = &ld->items[i];
                            if (k->type == VAL_STRING && k->sval && strcmp(k->sval, nm) ==0) { found = ld->items[i+1]; break; }
                        }
                    }
                    if (found.type != VAL_NIL) { VM_UNLOCK(vm); value_copy(&R[ins.r3], &found); }
                    else VM_UNLOCK(vm);
                } else { VM_UNLOCK(vm); }
            }
            continue;
        }



        L_MOD: {
            Value *a = &R[ins.r2], *b = &R[ins.r3];
            if (a->type == VAL_INT && b->type == VAL_INT) {
                if (b->ival == 0) {
                    vm_throw_msg(vm, "division by zero");
                    R = t->reg + t->base;
                    continue;
                }
                R[ins.r1].type = VAL_INT;
                R[ins.r1].ival = a->ival % b->ival;
                continue;
            }
            int bi = (int)val_as_double(b);
            if (bi == 0) {
                vm_throw_msg(vm, "division by zero");
                R = t->reg + t->base;
                continue;
            }
            double da = val_as_double(a);
            R[ins.r1].type = VAL_INT;
            R[ins.r1].ival = (int)da % bi;
            continue;
        }
        L_NEW_SET: {
            int n = ins.r3;
            int sidx = vm_set_new(vm);
            if (sidx < 0) {
                for (int i = 0; i < n && t->sp >= 0; i++) {
                    if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                        free(t->stack[t->sp].sval);
                    t->sp--;
                }
                R[ins.r1].type = VAL_NIL;
                continue;
            }
            if (n > 0 && t->sp >= n - 1) {
                int base = t->sp - (n - 1);
                for (int i = 0; i < n; i++) {
                    if (t->stack[base + i].type == VAL_SET) {
                        SetObj *src = vm_set_slot(vm, t->stack[base + i].ival);
                        vm_set_add_comp(vm, sidx, src);
                    } else {
                        vm_set_add(vm, sidx, &t->stack[base + i]);
                    }
                }
                for (int i = 0; i < n; i++) {
                    if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                        free(t->stack[t->sp].sval);
                    t->sp--;
                }
            }
            R[ins.r1].type = VAL_SET;
            R[ins.r1].ival = sidx;
            continue;
        }
        L_SET_INTERVAL: {
            if (t->sp < 1) { R[ins.r1].type = VAL_NIL; continue; }
            double hi = val_as_double(&t->stack[t->sp]); t->sp--;
            double lo = val_as_double(&t->stack[t->sp]); t->sp--;
            int nameFlags = ins.r2;
            int nameIdx = nameFlags & 0xFFFFFF;
            int flags = (nameFlags >> 24) & 0xFF;
            const char *sname = NULL;
            if (nameIdx >= 0 && nameIdx < t->code->string_count) sname = t->code->string_pool[nameIdx];
            int bi = builtin_set_index(sname);
            int sidx = vm_set_new(vm);
            if (sidx < 0) { R[ins.r1].type = VAL_NIL; continue; }
            SetObj *s = vm_set_slot(vm, sidx);
            s->kind = 2;
            s->nameIdx = bi;
            s->lo = lo;
            s->hi = hi;
            s->loInc = (flags & 1) ? 1 : 0;
            s->hiInc = (flags & 2) ? 1 : 0;
            R[ins.r1].type = VAL_SET;
            R[ins.r1].ival = sidx;
            continue;
        }
        L_IN: {
            Value la = R[ins.r2], rb = R[ins.r3];
            int res = set_contains_or_subset(vm, &la, &rb) ? 1 : 0;
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = res;
            continue;
        }
        L_MIN: {
            Value src = R[ins.r2];
            set_minmax(vm, &src, &R[ins.r1], 0);
            continue;
        }
        L_MAX: {
            Value src = R[ins.r2];
            set_minmax(vm, &src, &R[ins.r1], 1);
            continue;
        }
        L_BE: {
            int g = ins.r1;
            int setReg = ins.r2;
            int initReg = ins.r3;
            int sidx = (R[setReg].type == VAL_SET) ? R[setReg].ival : -1;
            im_mutex_lock((ImMutex*)VM_GSHARD(vm, g));
            if (g < 0) { im_mutex_unlock((ImMutex*)VM_GSHARD(vm, g)); continue; }
            im_mutex_unlock((ImMutex*)VM_GSHARD(vm, g));
            vm_global_grow(vm, g);   /* 鐙崰鍏ㄩ儴閿佹墿瀹癸紙姝ゆ椂鏈寔浠讳綍鍒嗙墖锛?*/
            im_mutex_lock((ImMutex*)VM_GSHARD(vm, g));
            if (g >= vm->globalCount) {
                for (int i = vm->globalCount; i <= g; i++) {
                    vm->globals[i].name = NULL;
                    vm->globals[i].val.type = VAL_NIL;
                }
                vm->globalCount = g + 1;
            }
            vm->be_bound[g] = sidx >= 0 ? sidx + 1 : 0;
            if (initReg >= 0) {
                Value init = R[initReg];
                if (sidx >= 0 && !set_contains(vm, sidx, &init)) {
                    Value err;
                    err.type = VAL_STRING;
                    err.sval = (char*)vm_intern(vm, "be: initial value out of range");
                    err.ival = 1;
                    err.fval = 0;
                    im_mutex_unlock((ImMutex*)VM_GSHARD(vm, g));
                    vm_throw(vm, t, &err);
                    R = t->reg + t->base;
                    continue;
                } else {
                    vm->globals[g].val = init;
                    if (init.type == VAL_STRING && init.sval && init.ival != 1) {
                        const char *np = vm_intern(vm, init.sval);
                        if (np) { vm->globals[g].val.sval = (char*)np; vm->globals[g].val.ival = 1; }
                    }
                }
            } else {
                vm->globals[g].val.type = VAL_NIL; /* uninitialized */
            }
            im_mutex_unlock((ImMutex*)VM_GSHARD(vm, g));
            continue;
        }



        /* ---------- 锟竭筹拷 ---------- */
        L_TRY_START: {
            TryEntry *te = NULL;
            if (t->code && t->code->try_count > 0) {
                int want = t->ip - 1;
                for (int i = t->code->try_count - 1; i >= 0; i--) {
                    if (t->code->try_entries[i].start_off == want) { te = &t->code->try_entries[i]; break; }
                }
            }
            if (!te) continue;
            if (t->exc_depth >= t->exc_cap) {
                t->exc_cap = t->exc_cap == 0 ? 8 : t->exc_cap * 2;
                t->exc_stack = realloc(t->exc_stack, (size_t)t->exc_cap * sizeof(ExcFrame));
            }
            ExcFrame *f = &t->exc_stack[t->exc_depth++];
            f->code = t->code;
            f->ip = t->ip;
            f->sp = t->sp;
            f->base = t->base;
            f->frame_count = t->frame_count;
            f->catch_ip = te->catch_off;
            f->var_idx = ins.r2;
            f->ignore = te->ignore;
            continue;
        }
        L_TRY_END: {
            if (t->exc_depth > 0) t->exc_depth--;
            continue;
        }
        L_THROW: {
            vm_throw(vm, t, &R[ins.r1]);
            R = t->reg + t->base;
            continue;
        }
        L_SET_ADD: {
            Value *sv = &R[ins.r1];
            if (sv->type == VAL_SET) vm_set_add(vm, sv->ival, &R[ins.r2]);
            continue;
        }

        L_THREAD_START: {
            int tidx = ins.r1, res = ins.r2, argc = ins.r3;
            if (tidx < 0 || tidx >= root->thread_count || root->threads[tidx] == NULL) {
                fprintf(stderr, "閿欒�? 鏃犳晥绾跨▼绱㈠�?%d\n", tidx);
                t->running = false;
                continue;
            }
            if (vm->threads[tidx] != NULL && !vm->threads[tidx]->finished) {
                if (!(root->thread_flags[tidx] & THREAD_FLAG_SINGLE))
                    fprintf(stderr, "thread %s already running\n", root->thread_names[tidx]);
                /* single: duplicate start silently ignored */
                while (argc-- > 0) {
                    if (t->sp >= 0) {
                        if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                            free(t->stack[t->sp].sval);
                        t->sp--;
                    }
                }
                continue;
            }
            /* 锟斤拷锟斤拷锟竭筹拷执锟斤拷锟斤拷锟斤拷??*/
            if (vm->limit_threads > 0 && vm->active_threads >= vm->limit_threads) {
                while (argc-- > 0) {
                    if (t->sp >= 0) {
                        if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1)
                            free(t->stack[t->sp].sval);
                        t->sp--;
                    }
                }
                limit_abort(vm, "threads", (double)vm->active_threads, (double)vm->limit_threads);
                continue;
            }
            if (root->thread_flags[tidx] & THREAD_FLAG_TASK) {
                /* task: virtual thread (Fiber-driven). single semantics: ignore duplicate start while running */
                int dup = 0;
                for (int _ti = 0; _ti < vm->task_count; _ti++)
                    if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx && !vm->tasks[_ti]->finished && vm->tasks[_ti]->running) { dup = 1; break; }
                if (dup) {
                    if (!(root->thread_flags[tidx] & THREAD_FLAG_SINGLE))
                        fprintf(stderr, "task %s already running\n", root->thread_names[tidx]);
                    while (argc-- > 0) { if (t->sp >= 0) { if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1) free(t->stack[t->sp].sval); t->sp--; } }
                    R[res].type = VAL_INT; R[res].ival = 0;
                    continue;
                }
                if (vm_task_create(vm, root, tidx, t, argc) == NULL) {
                    fprintf(stderr, "task limit %d exceeded (%s)\n", VM_MAX_TASKS, root->thread_names[tidx]);
                    while (argc-- > 0) { if (t->sp >= 0) { if (t->stack[t->sp].type == VAL_STRING && t->stack[t->sp].ival != 1) free(t->stack[t->sp].sval); t->sp--; } }
                    continue;
                }
                R[res].type = VAL_INT;
                R[res].ival = 0;
                continue;
            }
            VmThread *nt = calloc(1, sizeof(VmThread));
            nt->vm = vm;
            nt->code = root->threads[tidx];
            nt->ip = 0;
            nt->sp = -1;
            nt->tidx = tidx;
            nt->flags = root->thread_flags[tidx];
           nt->reg_cap = VM_THREAD_REG_COUNT;
            nt->reg = malloc(nt->reg_cap * sizeof(Value));
            memset(nt->reg, 0, nt->reg_cap * sizeof(Value));
            nt->R = nt->reg;
            nt->frame_cap = VM_MAX_FRAMES;
            nt->frame_code = calloc(nt->frame_cap, sizeof(Bytecode*));
            nt->frame_ip = calloc(nt->frame_cap, sizeof(int));
            nt->frame_base = calloc(nt->frame_cap, sizeof(int));
            nt->frame_res = calloc(nt->frame_cap, sizeof(int));
            nt->frame_sp = calloc(nt->frame_cap, sizeof(int));
            nt->exc_stack = NULL;
            nt->exc_depth = 0;
            nt->exc_cap = 0;
            nt->jump_req = -1;
            ImMutex *ml = im_mutex_new();
            nt->msg_lock = ml;
            /* 锟斤拷锟斤拷锟斤拷锟斤拷权锟斤拷栈转锟狡ｏ拷锟姐拷锟斤拷锟斤拷锟斤拷栈锟斤拷锟斤拷锟斤拷锟揭伙拷锟斤拷锟??*/
            for (int i = argc - 1; i >= 0; i--) {
                nt->R[i + 1] = t->stack[t->sp];
                t->sp--;
            }
            VM_LOCK(vm);
            vm->threads[tidx] = nt;
            VM_UNLOCK(vm);
            InterlockedIncrement(&vm->active_threads);
            nt->os_handle = im_thread_start((ImThreadProc)thread_entry, nt);
            R[res].type = VAL_INT;
            R[res].ival = 0;
            continue;
        }
        L_THREAD_CTRL: {
            int tidx = ins.r1, op = ins.r2;
            if (tidx == -1) {
                /* stop/pause/... all */
                for (int i = 0; i < VM_MAX_THREADS; i++) {
                    VmThread *tt = vm->threads[i];
                    if (tt && !tt->finished) {
                        if (op == THREAD_OP_PAUSE) tt->paused = true;
                        else if (op == THREAD_OP_RESUME) { tt->paused = false; tt->wake_at = 0; }
                        else { tt->stop_flag = true; if (op == THREAD_OP_KILL) tt->paused = false; }
                    }
                for (int _ti = 0; _ti < vm->task_count; _ti++) {
                    VmThread *tt2 = vm->tasks[_ti];
                    if (tt2 && !tt2->finished) {
                        if (op == THREAD_OP_PAUSE) tt2->paused = true;
                        else if (op == THREAD_OP_RESUME) { tt2->paused = false; tt2->wake_at = 0; tt2->blocked = 0; }
                        else { tt2->stop_flag = true; if (op == THREAD_OP_KILL) tt2->paused = false; }
                    }
                }
                }
            } else if (tidx >= 0) {
                VmThread *tt = (tidx < VM_MAX_THREADS) ? vm->threads[tidx] : NULL;
                if (!tt) { for (int _ti = 0; _ti < vm->task_count; _ti++) if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx) { tt = vm->tasks[_ti]; break; } }
                if (op == THREAD_OP_RESTART) {
                    /* restart: stop old (let it exit), start a fresh instance at ip 0 */
                    if (tt && !tt->finished) { tt->stop_flag = true; tt->paused = false; }
                    Bytecode *root2 = vm->code;
                    if (tidx < root2->thread_count && root2->threads[tidx]) {
                        if (root2->thread_flags[tidx] & THREAD_FLAG_TASK) {
                            if (vm_task_create(vm, root2, tidx, t, 0) == NULL)
                                fprintf(stderr, "task limit %d exceeded (%s)\n", VM_MAX_TASKS, root2->thread_names[tidx]);
                        } else {
                        VmThread *nt = calloc(1, sizeof(VmThread));
                        nt->vm = vm; nt->code = root2->threads[tidx]; nt->ip = 0; nt->sp = -1;
                        nt->tidx = tidx; nt->flags = root2->thread_flags[tidx];
                        nt->reg_cap = VM_THREAD_REG_COUNT;
                        nt->reg = malloc(nt->reg_cap * sizeof(Value));
                        memset(nt->reg, 0, nt->reg_cap * sizeof(Value));
                        nt->R = nt->reg;
                        nt->frame_cap = VM_MAX_FRAMES;
                        nt->frame_code = calloc(nt->frame_cap, sizeof(Bytecode*));
                        nt->frame_ip = calloc(nt->frame_cap, sizeof(int));
                        nt->frame_base = calloc(nt->frame_cap, sizeof(int));
                        nt->frame_res = calloc(nt->frame_cap, sizeof(int));
                        nt->frame_sp = calloc(nt->frame_cap, sizeof(int)); nt->exc_stack = NULL; nt->exc_depth = 0; nt->jump_req = -1;
                        ImMutex *ml2 = im_mutex_new(); nt->msg_lock = ml2;
                        VM_LOCK(vm); vm->threads[tidx] = nt; VM_UNLOCK(vm);
                        InterlockedIncrement(&vm->active_threads);
                        nt->os_handle = im_thread_start((ImThreadProc)thread_entry, nt);
                        }
                    }
                } else if (tt) {
                    if (op == THREAD_OP_PAUSE) tt->paused = true;
                    else if (op == THREAD_OP_RESUME) { tt->paused = false; tt->wake_at = 0; }
                    else { tt->stop_flag = true; if (op == THREAD_OP_KILL) tt->paused = false; }
                }
            } else if (tidx == -2) {
                /* this = current thread */
                if (op == THREAD_OP_PAUSE) t->paused = true;
                else if (op == THREAD_OP_RESUME) { t->paused = false; t->wake_at = 0; }
                else if (op == THREAD_OP_STOP) { t->running = false; }  /* stop this: natural end, restartable */
                else { t->stop_flag = true; t->running = false; }        /* kill this */
            }
            continue;
        }
        L_THREAD_GOTO: {
            int tidx = ins.r1, off = ins.r2;
            Bytecode *root2 = vm->code;
            if (tidx < 0 || tidx >= root2->thread_count || root2->threads[tidx] == NULL) {
                fprintf(stderr, "error: thread %d not defined\n", tidx);
                continue;
            }
            VmThread *target = vm->threads[tidx];
            if (target == NULL || target->finished) {
                /* not started: start fresh at label offset */
                VmThread *nt = calloc(1, sizeof(VmThread));
                nt->vm = vm; nt->code = root2->threads[tidx]; nt->ip = off; nt->sp = -1;
                nt->tidx = tidx; nt->flags = root2->thread_flags[tidx];
                nt->reg_cap = VM_THREAD_REG_COUNT;
                nt->reg = malloc(nt->reg_cap * sizeof(Value));
                memset(nt->reg, 0, nt->reg_cap * sizeof(Value));
                nt->R = nt->reg;
                nt->frame_cap = VM_MAX_FRAMES;
                nt->frame_code = calloc(nt->frame_cap, sizeof(Bytecode*));
                nt->frame_ip = calloc(nt->frame_cap, sizeof(int));
                nt->frame_base = calloc(nt->frame_cap, sizeof(int));
                nt->frame_res = calloc(nt->frame_cap, sizeof(int));
                nt->frame_sp = calloc(nt->frame_cap, sizeof(int)); nt->exc_stack = NULL; nt->exc_depth = 0; nt->jump_req = -1;
                ImMutex *ml2 = im_mutex_new(); nt->msg_lock = ml2;
                VM_LOCK(vm); vm->threads[tidx] = nt; VM_UNLOCK(vm);
                InterlockedIncrement(&vm->active_threads);
                nt->os_handle = im_thread_start((ImThreadProc)thread_entry, nt);
            } else {
                /* running: queue jump request (consumed at its next checkpoint) */
                target->jump_req = off;
            }
            continue;
        }

        L_THREAD_JOIN: {
            int tidx = ins.r1;
            VmThread *tt = (tidx >= 0 && tidx < VM_MAX_THREADS) ? vm->threads[tidx] : NULL;
            if (!tt) {
                for (int _ti = 0; _ti < vm->task_count; _ti++)
                    if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx) { tt = vm->tasks[_ti]; break; }
            }
            double timeout = -1.0;
            if (ins.r2 >= 0) timeout = val_as_double(&R[ins.r2]);
            if (!tt || tt->finished || tt == t) continue;
            unsigned long long deadline = 0;
            if (timeout >= 0) deadline = GetTickCount64() + (unsigned long long)(timeout * 1000);
            while (1) {
                /* task 鍙兘宸茶妲戒綅澶嶇敤/鍥炴敹锛氭瘡娆￠噸鏂拌幏鍙栧疄渚?*/
                VmThread *cur = NULL;
                if (tidx >= 0 && tidx < VM_MAX_THREADS) cur = vm->threads[tidx];
                if (!cur) { for (int _ti = 0; _ti < vm->task_count; _ti++) if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx) { cur = vm->tasks[_ti]; break; } }
                if (!cur || cur == t) break;
                if (cur->finished) break;
                if (t->stop_flag) break;
                if (deadline && GetTickCount64() >= deadline) break;
                if (t->is_task) { t->blocked = 1; SwitchToFiber(t->fiber_sched); continue; }
                Sleep(1);
            }
            continue;
        }
        L_THREAD_WAIT: {
            /* worker.wait N锟斤拷锟斤拷停目锟斤拷锟斤拷??N 锟诫（锟斤拷时锟皆讹拷锟街革�??*/
            int tidx = ins.r1;
            VmThread *wtt = (tidx >= 0 && tidx < VM_MAX_THREADS) ? vm->threads[tidx] : NULL;
            if (!wtt) { for (int _ti = 0; _ti < vm->task_count; _ti++) if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx) { wtt = vm->tasks[_ti]; break; } }
            if (wtt && !wtt->finished) {
                double sec = val_as_double(&R[ins.r2]);
                wtt->paused = true;
                wtt->wake_at = GetTickCount64() + (unsigned long long)(sec * 1000);
            }
            if (tidx >= 0 && tidx < VM_MAX_THREADS) {
                VmThread *tt = vm->threads[tidx];
                if (tt && !tt->finished) {
                    double sec = val_as_double(&R[ins.r2]);
                    tt->paused = true;
                    tt->wake_at = GetTickCount64() + (unsigned long long)(sec * 1000);
                }
            }
            continue;
        }
        L_THREAD_STATE: {
            int tidx = ins.r2, prop = ins.r3;
            bool val = false;
            VmThread *tt = (tidx >= 0 && tidx < VM_MAX_THREADS) ? vm->threads[tidx] : NULL;
            if (!tt) { for (int _ti = 0; _ti < vm->task_count; _ti++) if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx) { tt = vm->tasks[_ti]; break; } }
            if (tt) {
                switch (prop) {
                case 0: val = !tt->finished && !tt->stop_flag && !tt->paused; break;  /* running */
                case 1: val = tt->paused && !tt->finished; break;                     /* paused */
                case 2: val = tt->stop_flag; break;                                   /* stopped */
                case 3: val = tt->finished; break;                                    /* finished */
                }
            }
            R[ins.r1].type = VAL_BOOL;
            R[ins.r1].ival = val ? 1 : 0;
            continue;
        }
        L_LOCK: {
            int midx = ins.r1;
            ImMutex *cs;
            VM_LOCK(vm);
            cs = vm->mutexes[midx];
            if (!cs) {
                cs = im_mutex_new();
                vm->mutexes[midx] = cs;
                if (midx + 1 > vm->mutex_count) vm->mutex_count = midx + 1;
            }
            VM_UNLOCK(vm);
            if (ins.r2 == 0) im_mutex_lock(cs);
            else im_mutex_unlock(cs);
            continue;
        }
        L_SEND: {
            int tidx = ins.r1;
            VmThread *tt = (tidx >= 0 && tidx < VM_MAX_THREADS) ? vm->threads[tidx] : NULL;
            if (!tt) {
                for (int _ti = 0; _ti < vm->task_count; _ti++)
                    if (vm->tasks[_ti] && vm->tasks[_ti]->tidx == tidx) { tt = vm->tasks[_ti]; break; }
            }
            if (tt && tt->msg_lock) {
                ImMutex *ml = (ImMutex*)tt->msg_lock;
                im_mutex_lock(ml);
                if (tt->msg_cap == 0) {
                    tt->msg_cap = 8;
                    tt->msg_q = malloc(tt->msg_cap * sizeof(Value));
                    tt->msg_head = tt->msg_tail = 0;
                }
                if ((tt->msg_tail + 1) % tt->msg_cap == tt->msg_head) {
                    int newcap = tt->msg_cap * 2;
                    Value *nq = malloc(newcap * sizeof(Value));
                    int n = 0;
                    while (tt->msg_head != tt->msg_tail) {
                        nq[n++] = tt->msg_q[tt->msg_head];
                        tt->msg_head = (tt->msg_head + 1) % tt->msg_cap;
                    }
                    free(tt->msg_q);
                    tt->msg_q = nq;
                    tt->msg_cap = newcap;
                    tt->msg_head = 0;
                    tt->msg_tail = n;
                }
                Value v = R[ins.r2];
                if (v.type == VAL_STRING && v.sval) { v.sval = strdup(v.sval); v.ival = 0; }
                tt->msg_q[tt->msg_tail] = v;
                tt->msg_tail = (tt->msg_tail + 1) % tt->msg_cap;
                im_mutex_unlock(ml);
            }
                if (tt->is_task && tt->blocked && !tt->wake_at) { tt->blocked = 0; }
                continue;
        }
        L_RECV: {
            int res = ins.r1;
            double timeout = -1.0;
            if (ins.r2 >= 0) timeout = val_as_double(&R[ins.r2]);
            unsigned long long deadline = 0;
            if (timeout >= 0) deadline = GetTickCount64() + (unsigned long long)(timeout * 1000);
            Value out; out.type = VAL_NIL; out.ival = 0; out.fval = 0; out.sval = NULL;
            if (t->msg_lock) {
                ImMutex *ml = (ImMutex*)t->msg_lock;
                for (;;) {
                    int found = 0;
                    im_mutex_lock(ml);
                    if (t->msg_head != t->msg_tail) {
                        out = t->msg_q[t->msg_head];
                        t->msg_head = (t->msg_head + 1) % t->msg_cap;
                        found = 1;
                    }
                    im_mutex_unlock(ml);
                    if (found) break;
                    if (t->stop_flag) break;
                    if (deadline && GetTickCount64() >= deadline) break;
                    if (t->is_task) {
                        /* task: block until message or deadline; scheduler wakes us */
                        t->blocked = 1;
                        if (deadline) t->wake_at = deadline;
                        SwitchToFiber(t->fiber_sched);
                        continue;
                    }
                    Sleep(1);
                }
            }
            R[res] = out;
            continue;
        }
    }
}

/* ---------- 锟竭程癸拷锟斤拷 ---------- */
/* ================= task scheduler (virtual threads on Fibers) ================= */
static void __stdcall task_fiber_entry(LPVOID arg) {
    VmThread *t = (VmThread*)arg;
    for (;;) {
        vm_execute_thread(t);
        if (!(t->flags & THREAD_FLAG_RESTART)) break;
        if (t->stop_flag) break;
        t->ip = 0; t->sp = -1; t->base = 0; t->frame_count = 0;
        t->paused = false; t->stop_flag = false;
        t->wake_at = 0; t->blocked = 0; t->budget = VM_TASK_BUDGET;
    }
    t->finished = 1;
    t->running = 0;
    if (t->fiber_sched) SwitchToFiber(t->fiber_sched);
}

/* create a task instance (Fiber virtual thread); argc args popped from t stack into R[1..argc]. Returns NULL if task table full. */
VmThread *vm_os_thread_start(VM *vm, Bytecode *root, int tidx, VmThread *t, int argc) {
    if (tidx < 0 || tidx >= root->thread_count || root->threads[tidx] == NULL) return NULL;
    if (vm->threads[tidx] != NULL && !vm->threads[tidx]->finished) return NULL;
    if (vm->limit_threads > 0 && vm->active_threads >= vm->limit_threads) return NULL;
    VmThread *nt = calloc(1, sizeof(VmThread));
    nt->vm = vm;
    nt->code = root->threads[tidx];
    nt->ip = 0;
    nt->sp = -1;
    nt->tidx = tidx;
    nt->flags = root->thread_flags[tidx];
    nt->reg_cap = VM_THREAD_REG_COUNT;
    nt->reg = malloc(nt->reg_cap * sizeof(Value));
    memset(nt->reg, 0, nt->reg_cap * sizeof(Value));
    nt->R = nt->reg;
    nt->frame_cap = VM_MAX_FRAMES;
    nt->frame_code = calloc(nt->frame_cap, sizeof(Bytecode*));
    nt->frame_ip = calloc(nt->frame_cap, sizeof(int));
    nt->frame_base = calloc(nt->frame_cap, sizeof(int));
    nt->frame_res = calloc(nt->frame_cap, sizeof(int));
    nt->frame_sp = calloc(nt->frame_cap, sizeof(int));
    nt->exc_stack = NULL; nt->exc_depth = 0; nt->exc_cap = 0;
    nt->jump_req = -1;
    ImMutex *ml = im_mutex_new();
    nt->msg_lock = ml;
    /* is_task stays 0: wait/yield/recv use OS-thread (Sleep) paths, no Fiber dependency */
    for (int i = argc - 1; i >= 0; i--) { nt->R[i + 1] = t->stack[t->sp]; t->sp--; }
    VM_LOCK(vm);
    vm->threads[tidx] = nt;
    VM_UNLOCK(vm);
    InterlockedIncrement(&vm->active_threads);
    nt->os_handle = im_thread_start((ImThreadProc)thread_entry, nt);
    return nt;
}

VmThread *vm_task_create(VM *vm, Bytecode *root, int tidx, VmThread *t, int argc) {
    int slot = -1;
    for (int i = 0; i < VM_MAX_TASKS; i++) {
        if (!vm->tasks[i]) { slot = i; break; }
        /* finished tasks are reclaimed entirely by the scheduler; main thread only reuses fully-empty slots */
    }
    if (slot < 0) return NULL;
    VmThread *nt2 = calloc(1, sizeof(VmThread));
    nt2->vm = vm;
    nt2->code = root->threads[tidx];
    nt2->ip = 0;
    nt2->sp = -1;
    nt2->tidx = tidx;
    nt2->flags = root->thread_flags[tidx];
    nt2->reg_cap = VM_TASK_REG_COUNT;
    nt2->reg = calloc(nt2->reg_cap, sizeof(Value));
    nt2->R = nt2->reg;
    nt2->frame_cap = VM_TASK_MAX_FRAMES;
    nt2->frame_code = calloc(nt2->frame_cap, sizeof(Bytecode*));
    nt2->frame_ip = calloc(nt2->frame_cap, sizeof(int));
    nt2->frame_base = calloc(nt2->frame_cap, sizeof(int));
    nt2->frame_res = calloc(nt2->frame_cap, sizeof(int));
    nt2->frame_sp = calloc(nt2->frame_cap, sizeof(int));
    nt2->is_task = 1;
    nt2->budget = VM_TASK_BUDGET;
    nt2->blocked = 0;
    nt2->wake_at = 0;
    nt2->exc_stack = NULL; nt2->exc_depth = 0; nt2->exc_cap = 0;
    nt2->jump_req = -1;
    ImMutex *ml2 = im_mutex_new();
    nt2->msg_lock = ml2;
    for (int i = argc - 1; i >= 0; i--) { nt2->R[i + 1] = t->stack[t->sp]; t->sp--; }
    VM_LOCK(vm);
    vm->tasks[slot] = nt2;
    if (slot >= vm->task_count) vm->task_count = slot + 1;
    VM_UNLOCK(vm);
    nt2->running = true;
    if (!vm->sched_running) { vm->sched_running = 1; vm->sched_thread = im_thread_start((ImThreadProc)task_scheduler_entry, vm); }
        return nt2;
}
#ifdef _WIN32
unsigned __stdcall task_scheduler_entry(LPVOID arg) {
#else
void *task_scheduler_entry(void *arg) {
#endif
    VM *vm = (VM*)arg;
    void *sched_fiber = NULL;
    for (int att = 0; att < 10 && !sched_fiber; att++) {
        sched_fiber = ConvertThreadToFiber(NULL);
        if (!sched_fiber && att < 9) Sleep(10);
    }
    if (!sched_fiber) { vm->sched_running = 0; return 0; }
    for (int i = 0; i < VM_MAX_TASKS; i++)
        if (vm->tasks[i]) vm->tasks[i]->fiber_sched = sched_fiber;
    while (vm->sched_running) {
        ULONGLONG now = GetTickCount64();
        int progressed = 0;
        for (int i = 0; i < vm->task_count; i++) {
            VmThread *tk = vm->tasks[i];
            if (!tk || tk->finished || !tk->running) continue;
            if (tk->paused) { tk->blocked = 1; continue; }
            if (tk->blocked) {
                if (tk->wake_at && now >= tk->wake_at) { tk->blocked = 0; tk->wake_at = 0; }
                else continue;
            }
            if (!tk->fiber_self) {
                tk->fiber_self = CreateFiber(256 * 1024, task_fiber_entry, tk);
                tk->fiber_sched = sched_fiber;
            }
            if (!tk->fiber_self) { tk->running = 0; continue; }
                    SwitchToFiber(tk->fiber_self);
            progressed = 1;
            if (tk->finished) {
                /* scheduler owns finished-task reclamation: delete fiber, detach slot (VM_LOCK), free heap.
                   main thread only reuses fully-empty slots -> no use-after-free race in start/join loops */
                if (tk->fiber_self) { DeleteFiber(tk->fiber_self); tk->fiber_self = NULL; }
                VmThread *dead = tk;
                VM_LOCK(vm);
                vm->tasks[i] = NULL;
                VM_UNLOCK(vm);
                free(dead->exc_stack);
                free(dead->reg);
                free(dead->frame_code); free(dead->frame_ip); free(dead->frame_base); free(dead->frame_res); free(dead->frame_sp);
                for (int _mj = 0; _mj < dead->msg_cap; _mj++) { Value _mv = dead->msg_q[_mj]; if (_mv.type == VAL_STRING && _mv.ival != 1 && _mv.sval) free(_mv.sval); }
                free(dead->msg_q);
                if (dead->msg_lock) { im_mutex_free((ImMutex*)dead->msg_lock); }
                free(dead);
            }
        }
        if (vm->gc_pending || vm->gc_stop) {
            vm->gc_pending = 0;
            if (vm->gc_enabled) {
                vm->gc_stop = 1;
                int want = 0;
                for (int i = 0; i < VM_MAX_THREADS; i++) {
                    VmThread *tt = vm->threads[i];
                    if (tt && tt->running) want++;
                }
                int spins = 0;
                while (vm->gc_parked < want && spins < 20000) { Sleep(1); spins++; }
                gc_collect(vm);
                vm->gc_stop = 0;
            }
        }
        if (!progressed) Sleep(1);
    }
    DeleteFiber(sched_fiber);
    return 0;
}

#ifdef _WIN32
unsigned __stdcall thread_entry(LPVOID arg) {
#else
void *thread_entry(void *arg) {
#endif
    VmThread *t = (VmThread*)arg;
    for (;;) {
        vm_execute_thread(t);
        if (!(t->flags & THREAD_FLAG_RESTART)) break;   /* no restart label: exit */
        if (t->stop_flag) break;                        /* explicit kill/stop: no restart */
        /* restart: fully rewind thread state and run again */
        t->ip = 0;
        t->sp = -1;
        t->base = 0;
        t->frame_count = 0;
        t->paused = false;
        t->stop_flag = false;
        t->wake_at = 0;
        t->msg_head = 0;
        t->msg_tail = 0;
        memset(t->reg, 0, VM_THREAD_REG_COUNT * sizeof(Value));
        Sleep(100); /* debounce */
    }
    t->finished = true;
    t->running = false;
    if (!t->is_main && t->vm && t->vm->active_threads > 0)
        InterlockedDecrement(&t->vm->active_threads);
    return 0;
}

static void vm_stop_all_threads(VM *vm) {
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (tt) { tt->stop_flag = true; tt->paused = false; }
    }
}

static void vm_wait_all_threads(VM *vm) {
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (tt) {
            if (tt->flags & THREAD_FLAG_DAEMON) {
                /* daemon: main thread ended, do not wait - release handle, OS reclaims */
                if (tt->os_handle) { im_thread_close(tt->os_handle); tt->os_handle = NULL; }
                continue;
            }
            /* wait for thread to finish (max 3s) */
            unsigned long long deadline = GetTickCount64() + 3000;
            while (!tt->finished && GetTickCount64() < deadline) Sleep(1);
            if (tt->os_handle) {
                im_thread_join(tt->os_handle, 3000);
                im_thread_close(tt->os_handle);
                tt->os_handle = NULL;
            }
        }
    }
}

void vm_run(VM *vm) {
    vm->t_start = GetTickCount64();
    vm->exec_start_ms = GetTickCount64();
    record_load_from_file(vm, vm->record_save_path ? vm->record_save_path : "save.dat");
    /* 锟斤拷锟竭筹拷执锟斤拷锟斤拷锟斤拷锟斤拷 */
    VmThread main_t;
    memset(&main_t, 0, sizeof(main_t));
    main_t.vm = vm;
    main_t.code = vm->code;
    main_t.ip = 0;
    main_t.sp = -1;
    main_t.is_main = true;
    main_t.tidx = -1;
    main_t.flags = vm->code->main_flags;
    main_t.jump_req = -1;
    vm->main_thread = &main_t;
   main_t.reg_cap = VM_THREAD_REG_COUNT;
    main_t.reg = malloc(main_t.reg_cap * sizeof(Value));
    memset(main_t.reg, 0, main_t.reg_cap * sizeof(Value));
    main_t.R = main_t.reg;
    main_t.frame_cap = VM_MAX_FRAMES;
    main_t.frame_code = calloc(main_t.frame_cap, sizeof(Bytecode*));
    main_t.frame_ip = calloc(main_t.frame_cap, sizeof(int));
    main_t.frame_base = calloc(main_t.frame_cap, sizeof(int));
    main_t.frame_res = calloc(main_t.frame_cap, sizeof(int));
    main_t.frame_sp = calloc(main_t.frame_cap, sizeof(int));

    vm_execute_thread(&main_t);

    /* 锟斤拷锟竭程斤拷锟斤拷锟斤拷停止锟斤拷锟饺达拷锟斤拷锟斤拷锟斤拷锟竭程ｏ拷然锟斤拷锟斤拷??*/
    vm_stop_all_threads(vm);
    vm->main_thread = NULL;
    vm_wait_all_threads(vm);
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        if (vm->threads[i]) {
            VmThread *tt = vm->threads[i];
            vm->threads[i] = NULL;
            if (tt->flags & THREAD_FLAG_DAEMON) continue;  /* daemon: still running, OS reclaims */
            free(tt->exc_stack);
            free(tt->reg);
            free(tt->frame_code); free(tt->frame_ip); free(tt->frame_base); free(tt->frame_res); free(tt->frame_sp);
            for (int _mj=0; _mj<tt->msg_cap; _mj++) { Value _mv=tt->msg_q[_mj]; if (_mv.type==VAL_STRING && _mv.ival!=1 && _mv.sval) free(_mv.sval); } free(tt->msg_q);
            if (tt->msg_lock) {
                im_mutex_free((ImMutex*)tt->msg_lock);
                free(tt->msg_lock);
            }
            free(tt);
        }
    }
    free(main_t.exc_stack);
    free(main_t.reg);
    free(main_t.frame_code); free(main_t.frame_ip); free(main_t.frame_base); free(main_t.frame_res); free(main_t.frame_sp);
    record_save_to_file(vm, vm->record_save_path ? vm->record_save_path : "save.dat");
}

int vm_step(VM *vm) {
    if (vm->ip >= vm->code->count || !vm->running) return 1;
    vm->step_mode = true;
    vm_run(vm);
    return (vm->running && vm->ip < vm->code->count) ? 0 : 1;
}

/* ============ debug console API (exported for debug_mod.dll) ============ */

void vm_debug_exec(VM *vm, const char *code) {
    if (!vm || !code) return;
    if (strlen(code) > (1 << 18)) {  /* injection guard: 256KB cap */
        fprintf(stderr, "#run: code too large (max 256KB)\n");
        return;
    }
    Program *prog = parse_program(code);
    if (!prog) { fprintf(stderr, "#run: parse error\n"); return; }
    Compiler *comp = compiler_new();
    /* pre-register existing globals so indices match the loaded program */
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name) register_global(comp, vm->globals[i].name);
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    Bytecode *saved = vm->code;
    VmThread *saved_cur = vm_get_cur_thread();
    vm->code = bc;
    vm_run(vm);
    vm->code = saved;
    vm_set_cur_thread(saved_cur);
    /* persist new globals (indices beyond current globalCount) so later injections can reuse them */
    if (bc->global_names && bc->global_name_count > 0) {
        vm_global_grow(vm, bc->global_name_count - 1);
        for (int i = 0; i < bc->global_name_count; i++)
            if (!vm->globals[i].name) vm->globals[i].name = strdup(bc->global_names[i]);
        if (bc->global_name_count > vm->globalCount) vm->globalCount = bc->global_name_count;
    }
    compiler_free(comp);
}

int vm_params_load(VM *vm, const char *path) {
    if (!vm || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "params: cannot open '%s'\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (1 << 20)) { fclose(f); fprintf(stderr, "params: bad size for '%s'\n", path); return -1; }
    char *text = (char*)malloc((size_t)len + 1);
    size_t rd = fread(text, 1, (size_t)len, f);
    fclose(f);
    text[rd] = '\0';
    Program *prog = parse_program(text);
    if (!prog) { fprintf(stderr, "params: parse error in '%s'\n", path); free(text); return -1; }
    /* validation: only assignment statements allowed (no functions/loops/etc) */
    for (int i = 0; i < prog->count; i++) {
        Stmt *s = prog->stmts[i];
        if (s->type != STMT_ASSIGN) {
            fprintf(stderr, "params: '%s' only allows assignments (statement %d)\n", path, i + 1);
            free(text);
            return -1;
        }
        Expr *tg = s->assignStmt.target;
        int ok = (tg && tg->type == EXPR_IDENT);
        if (tg && tg->type == EXPR_MEMBER && tg->member.object && tg->member.object->type == EXPR_IDENT)
            ok = 1;
        if (!ok) { fprintf(stderr, "params: invalid assignment target in '%s'\n", path); free(text); return -1; }
    }
    Compiler *comp = compiler_new();
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name) register_global(comp, vm->globals[i].name);
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    Bytecode *saved = vm->code;
    VmThread *saved_main = vm->main_thread;
    VmThread *saved_cur = vm_get_cur_thread();
    vm->code = bc;
    vm_run(vm);
    vm->code = saved;
    vm->main_thread = saved_main;
    vm_set_cur_thread(saved_cur); /* nested vm_run overwrote the TLS cur thread: restore */

    /* persist any newly created globals (indices beyond current globalCount) */
    if (bc->global_names && bc->global_name_count > 0) {
        vm_global_grow(vm, bc->global_name_count - 1);
        for (int i = 0; i < bc->global_name_count; i++)
            if (!vm->globals[i].name) vm->globals[i].name = strdup(bc->global_names[i]);
        if (bc->global_name_count > vm->globalCount) vm->globalCount = bc->global_name_count;
    }
    compiler_free(comp);
    free(text);
    return 0;
}

void vm_debug_jump(VM *vm, const char *thread_name, const char *label) {
    if (!vm || !label) return;
    Bytecode *target = NULL;
    if (!thread_name || thread_name[0] == '\0' || strcmp(thread_name, "main") == 0) {
        target = vm->code;
    } else {
        for (int i = 0; i < vm->code->thread_count; i++)
            if (vm->code->thread_names[i] && strcmp(vm->code->thread_names[i], thread_name) == 0) { target = vm->code->threads[i]; break; }
    }
    if (!target) { fprintf(stderr, "#to: thread not found\n"); return; }
    int off = -1;
    for (int i = 0; i < target->label_count; i++)
        if (strcmp(target->labels[i].name, label) == 0) { off = target->labels[i].off; break; }
    if (off < 0) { fprintf(stderr, "#to: label '%s' not found\n", label); return; }
    if (!thread_name || thread_name[0] == '\0' || strcmp(thread_name, "main") == 0) {
        if (vm->main_thread) vm->main_thread->jump_req = off;
        else fprintf(stderr, "#to: main thread not running\n");
    } else {
        for (int i = 0; i < VM_MAX_THREADS; i++) {
            VmThread *tt = vm->threads[i];
            if (tt && !tt->finished && tt->tidx >= 0 && tt->tidx < vm->code->thread_count &&
                vm->code->thread_names[tt->tidx] && strcmp(vm->code->thread_names[tt->tidx], thread_name) == 0) {
                tt->jump_req = off;
                return;
            }
        }
        fprintf(stderr, "#to: thread '%s' not running\n", thread_name);
    }
}

void vm_debug_var(VM *vm, const char *mode) {
    if (!vm) return;
    for (int i = 0; i < vm->globalCount; i++) {
        char buf[128];
        const char *t = "nil";
        switch (vm->globals[i].val.type) {
            case VAL_INT: t = "int"; break;
            case VAL_FLOAT: t = "float"; break;
            case VAL_STRING: t = "string"; break;
            case VAL_BOOL: t = "bool"; break;
            case VAL_ARRAY: t = "array"; break;
            case VAL_DICT: t = "dict"; break;
            case VAL_SET: t = "set"; break;
            default: break;
        }
        value_to_string(vm, &vm->globals[i].val, buf, sizeof(buf), 0);
        if (!mode || !mode[0] || strcmp(mode, "all") == 0)
            printf("%s = %s (%s)%s\n", vm->globals[i].name ? vm->globals[i].name : "?", buf, t, vm->be_bound[i] > 0 ? " [be]" : "");
        else if (strcmp(mode, "value") == 0) printf("%s = %s\n", vm->globals[i].name ? vm->globals[i].name : "?", buf);
        else if (strcmp(mode, "type") == 0) printf("%s: %s\n", vm->globals[i].name ? vm->globals[i].name : "?", t);
        else if (strcmp(mode, "scope") == 0) printf("%s: global%s\n", vm->globals[i].name ? vm->globals[i].name : "?", vm->be_bound[i] > 0 ? " (be)" : "");
    }
    /* bare-try ignored-exception debug slot */
    if (vm->last_ignored_exc && (!mode || !mode[0] || strcmp(mode, "all") == 0 || strcmp(mode, "value") == 0))
        printf("@ignored = %s (x%lld)\n", vm->last_ignored_exc, vm->ignored_exc_count);
}

void vm_debug_threads(VM *vm) {
    if (!vm) return;
    if (vm->main_thread)
        printf("main: %s\n", vm->main_thread->finished ? "finished" : (vm->main_thread->paused ? "paused" : "running"));
    for (int i = 0; i < VM_MAX_THREADS; i++) {
        VmThread *tt = vm->threads[i];
        if (!tt) continue;
        const char *st = "running";
        if (tt->finished) st = "finished(zombie)";
        else if (tt->paused) st = "paused";
        else if (tt->stop_flag) st = "stopping";
        const char *nm = (tt->tidx >= 0 && tt->tidx < vm->code->thread_count && vm->code->thread_names[tt->tidx]) ? vm->code->thread_names[tt->tidx] : "?";
        printf("thread[%d] %s: %s\n", i, nm, st);
    }
}

/* ============ script-debugger / mod-script support (replaces debug_mod.dll) ============ */

/* free a Compiler but keep comp->mainBC alive (owned by vm->mod_bcs now) */
static void compiler_free_keep_bc(Compiler *comp) {
    if (!comp) return;
    for (int i = 0; i < comp->globalCount; i++) free(comp->globals[i].name);
    free(comp->globals);
    for (int i = 0; i < comp->localCount; i++) free(comp->locals[i].name);
    for (int i = 0; i < comp->builtinCount; i++) free(comp->builtins[i].name);
    for (int i = 0; i < comp->usingCount; i++) free(comp->usingMods[i]);
    free(comp->usingMods);
    free(comp);
}

/* parse+compile+run a .im file as a one-shot mod script; its bytecode stays alive
   in vm->mod_bcs for the lifetime of the VM. NOTE: vm_run() stops all threads at
   the end, so this is for initialization-style scripts, not long-lived debuggers. */
int vm_exec_script_file(VM *vm, const char *path) {
    if (!vm || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[mod] cannot read script '%s'\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (1 << 24)) { fclose(f); fprintf(stderr, "[mod] bad script size '%s'\n", path); return -1; }
    char *text = (char*)malloc((size_t)len + 1);
    size_t rd = fread(text, 1, (size_t)len, f);
    fclose(f);
    text[rd] = '\0';
    Program *prog = parse_program(text);
    free(text);
    if (!prog) { fprintf(stderr, "[mod] parse error in '%s'\n", path); return -1; }
    Compiler *comp = compiler_new();
    for (int i = 0; i < vm->globalCount; i++)
        if (vm->globals[i].name) register_global(comp, vm->globals[i].name);
    compiler_compile(comp, prog);
    Bytecode *bc = compiler_get_main_bytecode(comp);
    if (!bc) { compiler_free(comp); return -1; }
    if (vm->mod_bc_count >= 8) { compiler_free(comp); return -1; }
    vm->mod_bcs[vm->mod_bc_count++] = bc;
    Bytecode *saved = vm->code;
    VmThread *saved_cur = vm_get_cur_thread();
    vm->code = bc;
    int base_gc = vm->globalCount;  /* STORE_GLOBAL may advance globalCount during run */
    vm_run(vm);
    vm->code = saved;
    vm_set_cur_thread(saved_cur);
    if (bc->global_names && bc->global_name_count > 0) {
        vm_global_grow(vm, bc->global_name_count - 1);
        for (int i = 0; i < bc->global_name_count; i++)
            if (!vm->globals[i].name) vm->globals[i].name = strdup(bc->global_names[i]);
        if (bc->global_name_count > vm->globalCount) vm->globalCount = bc->global_name_count;
    }
    compiler_free_keep_bc(comp);
    return 0;
}

/* minimal disassembler (moved out of debug_mod.dll so .im debuggers can use it) */
static void vm_disasm_ins(Bytecode *code, int ip, char *out, size_t outsz) {
    RegInstruction *ins = &code->code[ip];
    const char *sn;
    switch (ins->op) {
    case OP_MOV:            snprintf(out, outsz, "MOV r%d, r%d", ins->r1, ins->r2); return;
    case OP_LOADK_INT:      snprintf(out, outsz, "LOADK_INT r%d, %d", ins->r1, ins->r2); return;
    case OP_LOADK_FLOAT:    snprintf(out, outsz, "LOADK_FLOAT r%d, #%d", ins->r1, ins->r2); return;
    case OP_LOADK_STRING:
        snprintf(out, outsz, "LOADK_STRING r%d, \"%s\"", ins->r1,
                 (ins->r2 >= 0 && ins->r2 < code->string_count && code->string_pool[ins->r2]) ? code->string_pool[ins->r2] : "?");
        return;
    case OP_LOADK_BOOL:     snprintf(out, outsz, "LOADK_BOOL r%d, %s", ins->r1, ins->r2 ? "true" : "false"); return;
    case OP_ADD:            snprintf(out, outsz, "ADD r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_CONCAT:         snprintf(out, outsz, "CONCAT r%d, r%d..r%d (n=%d)", ins->r1, ins->r2, ins->r2 + ins->r3 - 1, ins->r3); return;
    case OP_SUB:            snprintf(out, outsz, "SUB r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_MUL:            snprintf(out, outsz, "MUL r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_DIV:            snprintf(out, outsz, "DIV r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NEG:            snprintf(out, outsz, "NEG r%d, r%d", ins->r1, ins->r2); return;
    case OP_MOD:            snprintf(out, outsz, "MOD r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_EQ:             snprintf(out, outsz, "EQ r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NEQ:            snprintf(out, outsz, "NEQ r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LT:             snprintf(out, outsz, "LT r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_GT:             snprintf(out, outsz, "GT r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LE:             snprintf(out, outsz, "LE r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_GE:             snprintf(out, outsz, "GE r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_AND:            snprintf(out, outsz, "AND r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_OR:             snprintf(out, outsz, "OR r%d, r%d, r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_NOT:            snprintf(out, outsz, "NOT r%d, r%d", ins->r1, ins->r2); return;
    case OP_NEW_ARRAY:      snprintf(out, outsz, "NEW_ARRAY r%d, n=%d", ins->r1, ins->r3); return;
    case OP_INDEX_GET:      snprintf(out, outsz, "INDEX_GET r%d, r%d[r%d]", ins->r1, ins->r2, ins->r3); return;
    case OP_INDEX_SET:      snprintf(out, outsz, "INDEX_SET r%d[r%d] = r%d", ins->r1, ins->r2, ins->r3); return;
    case OP_LOAD_GLOBAL:    snprintf(out, outsz, "LOAD_GLOBAL r%d, [%d]", ins->r1, ins->r2); return;
    case OP_STORE_GLOBAL:   snprintf(out, outsz, "STORE_GLOBAL [%d] = r%d", ins->r1, ins->r2); return;
    case OP_JUMP:           snprintf(out, outsz, "JUMP %d", ins->r2); return;
    case OP_JUMP_IF_FALSE:  snprintf(out, outsz, "JUMP_IF_FALSE r%d, %d", ins->r1, ins->r2); return;
    case OP_JUMP_IF_TRUE:   snprintf(out, outsz, "JUMP_IF_TRUE r%d, %d", ins->r1, ins->r2); return;
    case OP_CALL_BUILTIN:
        sn = (ins->r2 >= 0 && ins->r2 < code->string_count && code->string_pool[ins->r2]) ? code->string_pool[ins->r2] : "?";
        snprintf(out, outsz, "CALL_BUILTIN r%d, %s (args:%d)", ins->r1, sn, ins->r3);
        return;
    case OP_CALL_FUNC:      snprintf(out, outsz, "CALL_FUNC f%d (args:%d, res:r%d)", ins->r1, ins->r3, ins->r2); return;
    case OP_RETURN:         snprintf(out, outsz, "RETURN r%d", ins->r1); return;
    case OP_PUSH_REG:       snprintf(out, outsz, "PUSH_REG r%d", ins->r1); return;
    case OP_POP_REG:        snprintf(out, outsz, "POP_REG r%d", ins->r1); return;
    case OP_SAY:            snprintf(out, outsz, "SAY r%d", ins->r1); return;
    case OP_WAIT:           snprintf(out, outsz, "WAIT r%d", ins->r1); return;
    case OP_STOP:           snprintf(out, outsz, "STOP"); return;
    case OP_HALT:           snprintf(out, outsz, "HALT"); return;
    case OP_IS_NIL:         snprintf(out, outsz, "IS_NIL r%d, r%d", ins->r1, ins->r2); return;
    case OP_THREAD_START:   snprintf(out, outsz, "THREAD_START t%d (args:%d)", ins->r1, ins->r3); return;
    case OP_THREAD_CTRL:    snprintf(out, outsz, "THREAD_CTRL t%d, op=%d", ins->r1, ins->r2); return;
    case OP_NEW_DICT:       snprintf(out, outsz, "NEW_DICT r%d, pairs=%d", ins->r1, ins->r3); return;
    case OP_EQK:            snprintf(out, outsz, "EQK r%d, \"%s\"", ins->r1, (ins->r3 >= 0 && ins->r3 < code->string_count && code->string_pool[ins->r3]) ? code->string_pool[ins->r3] : "?"); return;
    case OP_NEQK:           snprintf(out, outsz, "NEQK r%d, \"%s\"", ins->r1, (ins->r3 >= 0 && ins->r3 < code->string_count && code->string_pool[ins->r3]) ? code->string_pool[ins->r3] : "?"); return;
    case OP_TRY_START:      snprintf(out, outsz, "TRY_START r%d", ins->r1); return;
    case OP_TRY_END:        snprintf(out, outsz, "TRY_END"); return;
    case OP_THROW:          snprintf(out, outsz, "THROW r%d", ins->r1); return;
    default:                snprintf(out, outsz, "OP_%d", (int)ins->op); return;
    }
}

/* dbg_* builtins for .im debuggers (script-level bottom control) */
static int builtin_dbg_set_active(VM *vm) {
    int v = (vm_cur_sp(vm) >= 0) ? (vm_cur_stack(vm)[vm_cur_sp(vm)].type == VAL_INT ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0) : 0;
    pop(vm);
    vm->dbg_active = v ? 1 : 0;
    push_nil(vm);
    return 1;
}
static int builtin_dbg_active(VM *vm) {
    push_int(vm, vm->dbg_active);
    return 1;
}
static int builtin_dbg_wait_boundary(VM *vm) {
    /* If the main thread is already stopped at a boundary, return immediately
       (first stop). Otherwise wait for the next FRESH stop (count-based,
       immune to stale flags / outdated vm->ip). */
    if (vm->dbg_at_boundary && vm->dbg_pause) {
        push_nil(vm);
        return 1;
    }
    long long c = vm->dbg_boundary_count;
    while (vm->dbg_boundary_count == c) Sleep(2);
    push_nil(vm);
    return 1;
}
static int builtin_dbg_resume(VM *vm) {
    int step = (vm_cur_sp(vm) >= 0) ? (vm_cur_stack(vm)[vm_cur_sp(vm)].type == VAL_INT ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0) : 0;
    pop(vm);
    vm->dbg_pause = 0;
    if (step) vm->step_mode = true;
    push_nil(vm);
    return 1;
}
static int builtin_dbg_pause_now(VM *vm) {
    vm->step_mode = true;  /* main thread stops at the next instruction boundary */
    push_nil(vm);
    return 1;
}
static int builtin_dbg_break(VM *vm) {
    vm->step_mode = true;
    push_nil(vm);
    return 1;
}
static int builtin_dbg_ip(VM *vm) {
    push_int(vm, vm->ip);
    return 1;
}
static int builtin_dbg_disasm(VM *vm) {
    int n = (vm_cur_sp(vm) >= 0) ? (vm_cur_stack(vm)[vm_cur_sp(vm)].type == VAL_INT ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0) : 0;
    pop(vm);
    int ip = (vm_cur_sp(vm) >= 0) ? (vm_cur_stack(vm)[vm_cur_sp(vm)].type == VAL_INT ? vm_cur_stack(vm)[vm_cur_sp(vm)].ival : 0) : 0;
    pop(vm);
    Bytecode *code = vm->code ? vm->code : NULL;
    if (!code || ip < 0) { push_string(vm, ""); return 1; }
    if (n <= 0) n = 10;
    if (ip + n > code->count) n = code->count - ip;
    if (n < 0) n = 0;
    char out[4096], line[300];
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        vm_disasm_ins(code, ip + i, line, sizeof line);
        if (strlen(out) + strlen(line) + 16 >= sizeof out) break;
        snprintf(out + strlen(out), sizeof out - strlen(out), "%s%4d: %s\n", (ip + i == vm->ip) ? ">" : " ", ip + i, line);
    }
    push_string(vm, out);
    return 1;
}
static int builtin_dbg_var(VM *vm) {
    char mode[32] = "all";
    if (vm_cur_sp(vm) >= 0) {
        Value *v = &vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (v->type == VAL_STRING && v->sval) { strncpy(mode, v->sval, sizeof mode - 1); mode[sizeof mode - 1] = 0; }
    }
    pop(vm);
    vm_debug_var(vm, mode);
    push_nil(vm);
    return 1;
}
static int builtin_dbg_exec(VM *vm) {
    const char *code = (vm_cur_sp(vm) >= 0 && vm_cur_stack(vm)[vm_cur_sp(vm)].type == VAL_STRING)
                       ? vm_cur_stack(vm)[vm_cur_sp(vm)].sval : NULL;
    char *copy = code ? strdup(code) : NULL;
    pop(vm);
    if (copy) { vm_debug_exec(vm, copy); free(copy); }
    push_nil(vm);
    return 1;
}

void vm_debug_builtins_register(VM *vm) {
    vm_register_builtin(vm, "dbg_set_active", builtin_dbg_set_active);
    vm_register_builtin(vm, "dbg_active", builtin_dbg_active);
    vm_register_builtin(vm, "dbg_wait_boundary", builtin_dbg_wait_boundary);
    vm_register_builtin(vm, "dbg_resume", builtin_dbg_resume);
    vm_register_builtin(vm, "dbg_pause_now", builtin_dbg_pause_now);
    vm_register_builtin(vm, "dbg_break", builtin_dbg_break);
    vm_register_builtin(vm, "dbg_ip", builtin_dbg_ip);
    vm_register_builtin(vm, "dbg_disasm", builtin_dbg_disasm);
    vm_register_builtin(vm, "dbg_var", builtin_dbg_var);
    vm_register_builtin(vm, "dbg_exec", builtin_dbg_exec);
}
