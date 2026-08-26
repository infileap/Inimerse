/* infiverse_mod.c - Infiverse protocol bridge module (C core)
 * Optimizes the verse_kernel.im script prototype:
 *   - world storage: script dict -> open-addressing hash (O(1) get/set)
 *   - portal dialing: script loop -> builtin bit ops (18-bit + extendable)
 *   - block adjacency: custom links (non-Euclidean topology) in C
 *   - entity proximity: builtin scan (grid index later)
 *   - snapshot: builtin world traversal -> array for json_serialize
 *   - per-Verse law KV + world slots (multiple verses, switch by id)
 *
 * API (arg order: first arg = r_arg(argc-1), last arg = r_arg(0)):
 *   verse_use(id)                      select active world slot (0..15)
 *   verse_block_set(x,y,ly,i,t)        place block (t=0 removes)
 *   verse_block_get(x,y,ly,i)          -> block type (0 = air)
 *   verse_block_count()                -> placed blocks
 *   verse_link(ax,ay,bx,by)            custom adjacency (non-Euclidean)
 *   verse_neighbors(x,y)               -> [block keys...]
 *   verse_dial(candles)                -> {x,y,ly} (candle binary address)
 *   verse_portal_set(pwd,x,y,ly)       register portal password
 *   verse_portal_get(pwd)              -> {x,y,ly} or nil
 *   verse_entity_put(id,x,y)           register entity position
 *   verse_entity_remove(id)
 *   verse_nearby(x,y,r)                -> [ids...] within radius
 *   verse_law_set(name,value) / verse_law_get(name) -> law constants
 *   verse_snapshot()                   -> [{x,y,t},...] world dump
 */
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---------- world storage ---------- */
#define VERSE_MAX_WORLDS 16
#define VERSE_HASH_INIT 1024

#define VERSE_BIOME_PALETTE 16
struct Biome {
    char name[64];
    int level;    /* 0=root, deeper = more specific */
    int parent;   /* -1 = none */
    int palette[VERSE_BIOME_PALETTE];
    int palette_count;
};
typedef struct Biome Biome;

/* r_* stack helpers are defined below; forward decls for early helper blocks */
static void r_push(VM *vm, Value v);
static void r_push_nil(VM *vm);
static void r_push_int(VM *vm, int n);
static Value r_arg(VM *vm, int i);
static double r_num(VM *vm, int i);
static const char *r_str(VM *vm, int i);
static void r_popn(VM *vm, int n);

typedef struct {
    uint64_t *keys;
    int16_t *vals;
    int cap;
    int count;
} BlockMap;

typedef struct {
    uint64_t key;
    int *nb;
    int n;
    int cap;
} AdjEntry;

typedef struct {
    char id[64];
    int x, y;
} EntityPos;

typedef struct {
    char pwd[64];
    int x, y, ly;
} Portal;

typedef struct {
    char name[64];
    double val;
} Law;

typedef struct {
    BlockMap blocks;
    AdjEntry *adj;
    int adj_count, adj_cap;
    EntityPos *ents;
    int ent_count, ent_cap;
    Portal *portals;
    int portal_count, portal_cap;
    Law *laws;
    int law_count, law_cap;
    /* spatial grid (local-distance queries; MC far-lands prevention) */
    struct { struct GridCell *cells; int cap, count; } grid;
    int grid_dirty;
    /* biome hierarchy */
    struct Biome biomes[256];
    int biome_count;
    BlockMap biome_map;
} VerseWorld;

static VerseWorld worlds[VERSE_MAX_WORLDS];
static int cur_world = 0;
static int worlds_inited[VERSE_MAX_WORLDS];

/* block key: x,y in [-255,255], ly 0..7, i 0..1 -> unique uint64 */
static uint64_t bk(int x, int y, int ly, int i) {
    return (uint64_t)(x + 256) | ((uint64_t)(y + 256) << 9) |
           ((uint64_t)(ly & 7) << 18) | ((uint64_t)(i & 1) << 21);
}
static int bkx(uint64_t k) { return (int)(k & 0x1FF) - 256; }
static int bky(uint64_t k) { return (int)((k >> 9) & 0x1FF) - 256; }
static int bkly(uint64_t k) { return (int)((k >> 18) & 7); }
static int bki(uint64_t k) { return (int)((k >> 21) & 1); }

static void block_map_init(BlockMap *m);
static VerseWorld *vw(void) {
    /* lazy init: worlds are zeroed and their maps allocated on first use
       (verse_use normally does this; this guard covers scripts that don't) */
    if (!worlds_inited[cur_world]) {
        memset(&worlds[cur_world], 0, sizeof(VerseWorld));
        block_map_init(&worlds[cur_world].blocks);
        block_map_init(&worlds[cur_world].biome_map);
        worlds_inited[cur_world] = 1;
    }
    return &worlds[cur_world];
}

/* ---------- biome helpers ---------- */
static int biome_find(VerseWorld *w, const char *name) {
    for (int i = 0; i < w->biome_count; i++)
        if (strcmp(w->biomes[i].name, name) == 0) return i;
    return -1;
}
static void biome_push_dict(VM *vm, VerseWorld *w, int id) {
    Biome *b = &w->biomes[id];
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return; }
    Value k, v;
    k.type = VAL_STRING; k.ival = 1; k.fval = 0; k.sval = (char*)"name";
    v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = b->name;
    vm_array_push(vm, aidx, &k); vm_array_push(vm, aidx, &v);
    k.sval = (char*)"level";
    v.type = VAL_INT; v.ival = b->level; v.fval = 0; v.sval = NULL;
    vm_array_push(vm, aidx, &k); vm_array_push(vm, aidx, &v);
    k.sval = (char*)"parent";
    v.ival = b->parent;
    vm_array_push(vm, aidx, &k); vm_array_push(vm, aidx, &v);
    /* palette array */
    int paidx = vm_array_new(vm);
    if (paidx >= 0) {
        for (int i = 0; i < b->palette_count; i++) {
            Value pv; pv.type = VAL_INT; pv.ival = b->palette[i]; pv.fval = 0; pv.sval = NULL;
            vm_array_push(vm, paidx, &pv);
        }
    }
    k.sval = (char*)"palette";
    v.type = VAL_ARRAY; v.ival = paidx + 1; v.fval = 0; v.sval = NULL;
    vm_array_push(vm, aidx, &k); vm_array_push(vm, aidx, &v);
    Value d; d.type = VAL_DICT; d.ival = aidx + 1; d.fval = 0; d.sval = NULL;
    r_push(vm, d);
}

/* ---------- entity spatial grid (cell = 64) ---------- */
#define VERSE_GRID_CELL 64
#define VERSE_COORD_LIMIT_DEFAULT (1 << 25)   /* +-33.5M world border */
struct GridCell { int64_t key; int *idxs; int count, cap; };
static int64_t grid_cell_key64(int x, int y) {
    int cx = x >> 6, cy = y >> 6;
    return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cy;
}
static void grid_clear(VerseWorld *w) {
    for (int i = 0; i < w->grid.cap; i++) {
        free(w->grid.cells[i].idxs);
        w->grid.cells[i].idxs = NULL;
        w->grid.cells[i].count = 0;
    }
    w->grid.count = 0;
}
static void grid_rebuild(VerseWorld *w) {
    grid_clear(w);
    if (w->ent_count == 0) return;
    int need = 64;
    while (need < w->ent_count * 2) need *= 2;
    if (need > w->grid.cap) {
        free(w->grid.cells);
        w->grid.cells = calloc((size_t)need, sizeof(struct GridCell));
        w->grid.cap = need;
    }
    w->grid.count = 0;
    for (int i = 0; i < w->ent_count; i++) {
        int64_t key = grid_cell_key64(w->ents[i].x, w->ents[i].y);
        int slot = (int)((key ^ (key >> 32)) & (w->grid.cap - 1));
        struct GridCell *c = NULL;
        for (;;) {
            c = &w->grid.cells[slot];
            if (!c->idxs) {
                c->key = key;
                c->idxs = malloc(4 * sizeof(int));
                c->cap = 4;
                c->count = 0;
                w->grid.count++;
                break;
            }
            if (c->key == key) break;
            slot = (slot + 1) & (w->grid.cap - 1);
        }
        if (c->count >= c->cap) { c->cap *= 2; c->idxs = realloc(c->idxs, (size_t)c->cap * sizeof(int)); }
        c->idxs[c->count++] = i;
    }
}
/* collect entity indices within Chebyshev radius r; returns count */
static int grid_query(VerseWorld *w, int x, int y, int r, int *out, int out_cap) {
    int n = 0;
    if (!w->grid.cells || w->grid.count == 0) return 0;
    int x0 = (x - r) >> 6, x1 = (x + r) >> 6;
    int y0 = (y - r) >> 6, y1 = (y + r) >> 6;
    if (x1 - x0 > 2048 || y1 - y0 > 2048) {   /* huge query: fall back to linear scan */
        for (int i = 0; i < w->ent_count && n < out_cap; i++) {
            int64_t dx = (int64_t)w->ents[i].x - x; if (dx < 0) dx = -dx;
            int64_t dy = (int64_t)w->ents[i].y - y; if (dy < 0) dy = -dy;
            if (dx <= r && dy <= r) out[n++] = i;
        }
        return n;
    }
    for (int cx = x0; cx <= x1; cx++) {
        for (int cy = y0; cy <= y1; cy++) {
            int64_t key = ((int64_t)(uint32_t)cx << 32) | (uint32_t)cy;
            int slot = (int)((key ^ (key >> 32)) & (w->grid.cap - 1));
            for (;;) {
                struct GridCell *c = &w->grid.cells[slot];
                if (!c->idxs) break;
                if (c->key == key) {
                    for (int k = 0; k < c->count && n < out_cap; k++) {
                        int i = c->idxs[k];
                        int64_t dx = (int64_t)w->ents[i].x - x; if (dx < 0) dx = -dx;
                        int64_t dy = (int64_t)w->ents[i].y - y; if (dy < 0) dy = -dy;
                        if (dx <= r && dy <= r) out[n++] = i;
                    }
                    break;
                }
                slot = (slot + 1) & (w->grid.cap - 1);
            }
        }
    }
    return n;
}

static void block_map_init(BlockMap *m) {
    m->cap = VERSE_HASH_INIT;
    m->keys = calloc((size_t)m->cap, sizeof(uint64_t));
    m->vals = calloc((size_t)m->cap, sizeof(int16_t));
    m->count = 0;
}
static void block_map_free(BlockMap *m) {
    free(m->keys); free(m->vals);
    m->keys = NULL; m->vals = NULL; m->cap = 0; m->count = 0;
}
static uint64_t hash64(uint64_t v) {
    v ^= v >> 33; v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ULL;
    v ^= v >> 33;
    return v;
}
static void block_map_grow(BlockMap *m) {
    int nc = m->cap * 2;
    uint64_t *nk = calloc((size_t)nc, sizeof(uint64_t));
    int16_t *nv = calloc((size_t)nc, sizeof(int16_t));
    for (int i = 0; i < m->cap; i++) {
        if (m->keys[i]) {
            int j = (int)(hash64(m->keys[i]) & (nc - 1));
            while (nk[j]) j = (j + 1) & (nc - 1);
            nk[j] = m->keys[i]; nv[j] = m->vals[i];
        }
    }
    free(m->keys); free(m->vals);
    m->keys = nk; m->vals = nv; m->cap = nc;
}
static void block_set(BlockMap *m, uint64_t k, int16_t t) {
    if ((m->count + 1) * 10 >= m->cap * 7) block_map_grow(m);
    int j = (int)(hash64(k) & (m->cap - 1));
    while (m->keys[j]) {
        if (m->keys[j] == k) {
            if (t == 0) { m->keys[j] = 0; m->count--; } else m->vals[j] = t;
            return;
        }
        j = (j + 1) & (m->cap - 1);
    }
    if (t != 0) { m->keys[j] = k; m->vals[j] = t; m->count++; }
}
static int16_t block_get(BlockMap *m, uint64_t k) {
    int j = (int)(hash64(k) & (m->cap - 1));
    while (m->keys[j]) {
        if (m->keys[j] == k) return m->vals[j];
        j = (j + 1) & (m->cap - 1);
    }
    return 0;
}

/* ---------- arg helpers (io-style; r_arg(0) = LAST arg) ---------- */
static Value r_arg(VM *vm, int i) { return vm_cur_stack(vm)[vm_cur_sp(vm) - i]; }
static double r_num(VM *vm, int i) {
    Value v = r_arg(vm, i);
    return (v.type == VAL_INT) ? (double)v.ival : (v.type == VAL_FLOAT) ? v.fval : 0.0;
}
static const char *r_str(VM *vm, int i) {
    Value v = r_arg(vm, i);
    return (v.type == VAL_STRING && v.sval) ? v.sval : "";
}
static void r_popn(VM *vm, int n) {
    while (n-- > 0 && vm_cur_sp(vm) >= 0) {
        Value v = vm_cur_stack(vm)[vm_cur_sp(vm)];
        if (v.type == VAL_STRING && v.ival != 1 && v.sval) free(v.sval);
        vm_cur_set_sp(vm, vm_cur_sp(vm) - 1);
    }
}
static void r_push(VM *vm, Value v) {
    if (vm_cur_sp(vm) < 1023) {
        vm_cur_set_sp(vm, vm_cur_sp(vm) + 1);
        vm_cur_stack(vm)[vm_cur_sp(vm)] = v;
    }
}
static void r_push_int(VM *vm, int n) {
    Value v; v.type = VAL_INT; v.ival = n; v.fval = 0; v.sval = NULL;
    r_push(vm, v);
}
static void r_push_nil(VM *vm) {
    Value v; v.type = VAL_NIL; v.ival = 0; v.fval = 0; v.sval = NULL;
    r_push(vm, v);
}
/* push {x,y,ly} dict */
static void r_push_cell(VM *vm, int x, int y, int ly) {
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return; }
    Value kx; kx.type = VAL_STRING; kx.ival = 1; kx.fval = 0; kx.sval = (char*)"x";
    Value kx2; kx2.type = VAL_STRING; kx2.ival = 1; kx2.fval = 0; kx2.sval = (char*)"y";
    Value kx3; kx3.type = VAL_STRING; kx3.ival = 1; kx3.fval = 0; kx3.sval = (char*)"ly";
    Value vx; vx.type = VAL_INT; vx.ival = x; vx.fval = 0; vx.sval = NULL;
    Value vy; vy.type = VAL_INT; vy.ival = y; vy.fval = 0; vy.sval = NULL;
    Value vl; vl.type = VAL_INT; vl.ival = ly; vl.fval = 0; vl.sval = NULL;
    vm_dict_set(vm, aidx, &kx, &vx);
    vm_dict_set(vm, aidx, &kx2, &vy);
    vm_dict_set(vm, aidx, &kx3, &vl);
    Value d; d.type = VAL_DICT; d.ival = aidx + 1; d.fval = 0; d.sval = NULL;
    r_push(vm, d);
}

/* ---------- builtins ---------- */
static int b_verse_use(VM *vm) {
    int id = (int)r_num(vm, vm->cur_argc - 1);
    r_popn(vm, vm->cur_argc);
    if (id < 0 || id >= VERSE_MAX_WORLDS) { r_push_int(vm, 0); return 1; }
    cur_world = id;
    if (!worlds_inited[id]) {
        memset(&worlds[id], 0, sizeof(VerseWorld));
        block_map_init(&worlds[id].blocks);
        block_map_init(&worlds[id].biome_map);
        worlds_inited[id] = 1;
    }
    r_push_int(vm, 1);
    return 1;
}

static int b_verse_block_set(VM *vm) {
    int argc = vm->cur_argc;
    int t = (int)r_num(vm, argc - 5);
    int i = (int)r_num(vm, argc - 4);
    int ly = (int)r_num(vm, argc - 3);
    int y = (int)r_num(vm, argc - 2);
    int x = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    block_set(&w->blocks, bk(x, y, ly, i), (int16_t)t);
    r_push_int(vm, 1);
    return 1;
}

static int b_verse_block_get(VM *vm) {
    int argc = vm->cur_argc;
    int i = (int)r_num(vm, argc - 4);
    int ly = (int)r_num(vm, argc - 3);
    int y = (int)r_num(vm, argc - 2);
    int x = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    r_push_int(vm, block_get(&vw()->blocks, bk(x, y, ly, i)));
    return 1;
}

static int b_verse_block_count(VM *vm) {
    r_popn(vm, vm->cur_argc);
    r_push_int(vm, vw()->blocks.count);
    return 1;
}

static int b_verse_link(VM *vm) {
    int argc = vm->cur_argc;
    int by = (int)r_num(vm, argc - 4);
    int bx = (int)r_num(vm, argc - 3);
    int ay = (int)r_num(vm, argc - 2);
    int ax = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    uint64_t ka = bk(ax, ay, 0, 0);
    uint64_t kb = bk(bx, by, 0, 0);
    /* find or create adj entry for ka, append kb */
    int idx = -1;
    for (int i = 0; i < w->adj_count; i++) if (w->adj[i].key == ka) { idx = i; break; }
    if (idx < 0) {
        if (w->adj_count >= w->adj_cap) {
            w->adj_cap = w->adj_cap == 0 ? 64 : w->adj_cap * 2;
            w->adj = realloc(w->adj, (size_t)w->adj_cap * sizeof(AdjEntry));
        }
        idx = w->adj_count++;
        w->adj[idx].key = ka; w->adj[idx].nb = NULL; w->adj[idx].n = 0; w->adj[idx].cap = 0;
    }
    AdjEntry *e = &w->adj[idx];
    for (int i = 0; i < e->n; i++) if (e->nb[i] == (int64_t)kb) { r_push_int(vm, 1); return 1; }
    if (e->n >= e->cap) {
        e->cap = e->cap == 0 ? 4 : e->cap * 2;
        e->nb = realloc(e->nb, (size_t)e->cap * sizeof(int));
    }
    e->nb[e->n++] = (int)kb;
    r_push_int(vm, 1);
    return 1;
}

static int b_verse_neighbors(VM *vm) {
    int argc = vm->cur_argc;
    int y = (int)r_num(vm, argc - 2);
    int x = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    uint64_t k = bk(x, y, 0, 0);
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return 1; }
    for (int i = 0; i < w->adj_count; i++) {
        if (w->adj[i].key == k) {
            for (int j = 0; j < w->adj[i].n; j++) {
                uint64_t nb = (uint64_t)w->adj[i].nb[j];
                char buf[64];
                snprintf(buf, sizeof buf, "%d,%d", bkx(nb), bky(nb));
                const char *s = vm_intern(vm, buf);
                Value v; v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = (char*)(s ? s : buf);
                vm_array_push(vm, aidx, &v);
            }
            break;
        }
    }
    if (w->adj_count == 0) {
        /* default flat four-neighbors as strings */
        char buf[64];
        snprintf(buf, sizeof buf, "%d,%d", x + 16, y); { const char *s = vm_intern(vm, buf); Value v1; v1.type = VAL_STRING; v1.ival = 1; v1.fval = 0; v1.sval = (char*)(s ? s : buf); vm_array_push(vm, aidx, &v1); }
        snprintf(buf, sizeof buf, "%d,%d", x - 16, y); { const char *s = vm_intern(vm, buf); Value v2; v2.type = VAL_STRING; v2.ival = 1; v2.fval = 0; v2.sval = (char*)(s ? s : buf); vm_array_push(vm, aidx, &v2); }
        snprintf(buf, sizeof buf, "%d,%d", x, y + 16); { const char *s = vm_intern(vm, buf); Value v3; v3.type = VAL_STRING; v3.ival = 1; v3.fval = 0; v3.sval = (char*)(s ? s : buf); vm_array_push(vm, aidx, &v3); }
        snprintf(buf, sizeof buf, "%d,%d", x, y - 16); { const char *s = vm_intern(vm, buf); Value v4; v4.type = VAL_STRING; v4.ival = 1; v4.fval = 0; v4.sval = (char*)(s ? s : buf); vm_array_push(vm, aidx, &v4); }
    }
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1; a.fval = 0; a.sval = NULL;
    r_push(vm, a);
    return 1;
}

static int b_verse_dial(VM *vm) {
    int argc = vm->cur_argc;
    Value arr = r_arg(vm, argc - 1);   /* candles array (first arg) */
    r_popn(vm, argc);
    uint32_t code = 0;
    if (arr.type == VAL_ARRAY) {
        ArrayObj *a = vm_pool_slot(vm, arr.ival - 1);
        if (a) {
            for (int i = 0; i < a->count; i++) {
                Value *it = &a->items[i];
                int bit = (it->type == VAL_INT) ? (it->ival != 0 ? 1 : 0) : 0;
                code = (code << 1) | (uint32_t)bit;
            }
        }
    }
    /* 18-bit: low 9 bits = x+255, high 9 bits = y+255 (range 0..511) */
    int x = (int)(code & 0x1FF) - 255;
    int y = (int)((code >> 9) & 0x1FF) - 255;
    r_push_cell(vm, x, y, 0);
    return 1;
}

static int b_verse_portal_set(VM *vm) {
    int argc = vm->cur_argc;
    int ly = (int)r_num(vm, argc - 4);
    int y = (int)r_num(vm, argc - 3);
    int x = (int)r_num(vm, argc - 2);
    const char *pwd = r_str(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    for (int i = 0; i < w->portal_count; i++) {
        if (strcmp(w->portals[i].pwd, pwd) == 0) {
            w->portals[i].x = x; w->portals[i].y = y; w->portals[i].ly = ly;
            r_push_int(vm, 1); return 1;
        }
    }
    if (w->portal_count >= w->portal_cap) {
        w->portal_cap = w->portal_cap == 0 ? 16 : w->portal_cap * 2;
        w->portals = realloc(w->portals, (size_t)w->portal_cap * sizeof(Portal));
    }
    Portal *p = &w->portals[w->portal_count++];
    snprintf(p->pwd, sizeof p->pwd, "%s", pwd);
    p->x = x; p->y = y; p->ly = ly;
    r_push_int(vm, 1);
    return 1;
}

static int b_verse_portal_get(VM *vm) {
    int argc = vm->cur_argc;
    const char *pwd = r_str(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    for (int i = 0; i < w->portal_count; i++) {
        if (strcmp(w->portals[i].pwd, pwd) == 0) {
            r_push_cell(vm, w->portals[i].x, w->portals[i].y, w->portals[i].ly);
            return 1;
        }
    }
    r_push_nil(vm);
    return 1;
}

static int b_verse_entity_put(VM *vm) {
    int argc = vm->cur_argc;
    int y = (int)r_num(vm, argc - 3);
    int x = (int)r_num(vm, argc - 2);
    const char *id = r_str(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    /* world border (law coord_limit, default +-2^25): reject beyond it
       so entity math can never approach the int32 overflow / float32 jitter zones */
    double lim = VERSE_COORD_LIMIT_DEFAULT;
    for (int i = 0; i < w->law_count; i++)
        if (strcmp(w->laws[i].name, "coord_limit") == 0) lim = w->laws[i].val;
    if (lim > 0 && ((double)x > lim || (double)x < -lim || (double)y > lim || (double)y < -lim)) {
        fprintf(stderr, "[verse] entity '%s' at (%d,%d) beyond world border +-%g - rejected\n", id, x, y, lim);
        r_push_int(vm, 0); return 1;
    }
    w->grid_dirty = 1;
    for (int i = 0; i < w->ent_count; i++) {
        if (strcmp(w->ents[i].id, id) == 0) {
            w->ents[i].x = x; w->ents[i].y = y;
            r_push_int(vm, 1); return 1;
        }
    }
    if (w->ent_count >= w->ent_cap) {
        w->ent_cap = w->ent_cap == 0 ? 32 : w->ent_cap * 2;
        w->ents = realloc(w->ents, (size_t)w->ent_cap * sizeof(EntityPos));
    }
    EntityPos *e = &w->ents[w->ent_count++];
    snprintf(e->id, sizeof e->id, "%s", id);
    e->x = x; e->y = y;
    r_push_int(vm, 1);
    return 1;
}

static int b_verse_entity_remove(VM *vm) {
    int argc = vm->cur_argc;
    const char *id = r_str(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    for (int i = 0; i < w->ent_count; i++) {
        if (strcmp(w->ents[i].id, id) == 0) {
            w->ents[i] = w->ents[w->ent_count - 1];
            w->ent_count--;
            r_push_int(vm, 1); return 1;
        }
    }
    r_push_int(vm, 0);
    return 1;
}

static int b_verse_nearby(VM *vm) {
    int argc = vm->cur_argc;
    int r = (int)r_num(vm, argc - 3);
    int y = (int)r_num(vm, argc - 2);
    int x = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return 1; }
    if (w->grid_dirty) { grid_rebuild(w); w->grid_dirty = 0; }
    int *hits = NULL;
    int hn = 0;
    if (w->ent_count > 0) {
        hits = malloc((size_t)w->ent_count * sizeof(int));
        if (hits) hn = grid_query(w, x, y, r, hits, w->ent_count);
    }
    for (int i = 0; i < hn; i++) {
        Value v; v.type = VAL_STRING; v.ival = 1; v.fval = 0; v.sval = w->ents[hits[i]].id;
        vm_array_push(vm, aidx, &v);
    }
    free(hits);
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1; a.fval = 0; a.sval = NULL;
    r_push(vm, a);
    return 1;
}

static int b_verse_law_set(VM *vm) {
    int argc = vm->cur_argc;
    double val = r_num(vm, argc - 2);
    const char *name = r_str(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    for (int i = 0; i < w->law_count; i++) {
        if (strcmp(w->laws[i].name, name) == 0) {
            w->laws[i].val = val;
            r_push_int(vm, 1); return 1;
        }
    }
    if (w->law_count >= w->law_cap) {
        w->law_cap = w->law_cap == 0 ? 16 : w->law_cap * 2;
        w->laws = realloc(w->laws, (size_t)w->law_cap * sizeof(Law));
    }
    Law *l = &w->laws[w->law_count++];
    snprintf(l->name, sizeof l->name, "%s", name);
    l->val = val;
    r_push_int(vm, 1);
    return 1;
}

static int b_verse_law_get(VM *vm) {
    int argc = vm->cur_argc;
    const char *name = r_str(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    for (int i = 0; i < w->law_count; i++) {
        if (strcmp(w->laws[i].name, name) == 0) {
            Value v; v.type = VAL_FLOAT; v.fval = w->laws[i].val; v.ival = 0; v.sval = NULL;
            r_push(vm, v);
            return 1;
        }
    }
    r_push_int(vm, 0);
    return 1;
}

static int b_verse_snapshot(VM *vm) {
    r_popn(vm, vm->cur_argc);
    VerseWorld *w = vw();
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return 1; }
    for (int i = 0; i < w->blocks.cap; i++) {
        if (w->blocks.keys[i]) {
            uint64_t k = w->blocks.keys[i];
            Value cell; cell.type = VAL_DICT; cell.ival = 0; cell.fval = 0; cell.sval = NULL;
            int caidx = vm_array_new(vm);
            if (caidx < 0) break;
            Value kx; kx.type = VAL_STRING; kx.ival = 1; kx.fval = 0; kx.sval = (char*)"x";
            Value k2; k2.type = VAL_STRING; k2.ival = 1; k2.fval = 0; k2.sval = (char*)"y";
            Value k3; k3.type = VAL_STRING; k3.ival = 1; k3.fval = 0; k3.sval = (char*)"t";
            Value vx; vx.type = VAL_INT; vx.ival = bkx(k); vx.fval = 0; vx.sval = NULL;
            Value vy; vy.type = VAL_INT; vy.ival = bky(k); vy.fval = 0; vy.sval = NULL;
            Value vt; vt.type = VAL_INT; vt.ival = w->blocks.vals[i]; vt.fval = 0; vt.sval = NULL;
            vm_dict_set(vm, caidx, &kx, &vx);
            vm_dict_set(vm, caidx, &k2, &vy);
            vm_dict_set(vm, caidx, &k3, &vt);
            cell.ival = caidx + 1;
            vm_array_push(vm, aidx, &cell);
        }
    }
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1; a.fval = 0; a.sval = NULL;
    r_push(vm, a);
    return 1;
}

/* ---------- register ---------- */
/* ---------- biome builtins ---------- */
static int b_verse_biome_add(VM *vm) {   /* verse_biome_add(name, level, parent) -> id or -1 */
    int argc = vm->cur_argc;
    const char *name = r_str(vm, argc - 1);
    int level = (int)r_num(vm, argc - 2);
    int parent = (int)r_num(vm, argc - 3);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    if (w->biome_count >= 256 || biome_find(w, name) >= 0) { r_push_int(vm, -1); return 1; }
    if (level < 0 || level > 8) level = 0;
    if (parent < -1 || parent >= w->biome_count) parent = -1;
    Biome *b = &w->biomes[w->biome_count];
    snprintf(b->name, sizeof b->name, "%s", name);
    b->level = level; b->parent = parent; b->palette_count = 0;
    r_push_int(vm, w->biome_count++);
    return 1;
}
static int b_verse_biome_id(VM *vm) {   /* verse_biome_id(name) -> id or -1 */
    int argc = vm->cur_argc;
    const char *name = r_str(vm, argc - 1);
    r_popn(vm, argc);
    r_push_int(vm, biome_find(vw(), name));
    return 1;
}
static int b_verse_biome_name(VM *vm) {  /* verse_biome_name(id) -> name or nil */
    int argc = vm->cur_argc;
    int id = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    if (id < 0 || id >= w->biome_count) { r_push_nil(vm); return 1; }
    Value sv; sv.type = VAL_STRING; sv.ival = 1; sv.fval = 0; sv.sval = w->biomes[id].name;
    r_push(vm, sv);
    return 1;
}
static int b_verse_biome_info(VM *vm) {  /* verse_biome_info(id) -> {name,level,parent,palette} or nil */
    int argc = vm->cur_argc;
    int id = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    if (id < 0 || id >= w->biome_count) { r_push_nil(vm); return 1; }
    biome_push_dict(vm, w, id);
    return 1;
}
static int b_verse_biome_children(VM *vm) {  /* verse_biome_children(id) -> [ids] */
    int argc = vm->cur_argc;
    int id = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return 1; }
    if (id >= 0 && id < w->biome_count) {
        for (int i = 0; i < w->biome_count; i++) {
            if (w->biomes[i].parent == id) {
                Value v; v.type = VAL_INT; v.ival = i; v.fval = 0; v.sval = NULL;
                vm_array_push(vm, aidx, &v);
            }
        }
    }
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1; a.fval = 0; a.sval = NULL;
    r_push(vm, a);
    return 1;
}
static int b_verse_biome_ancestors(VM *vm) {  /* verse_biome_ancestors(id) -> [root..self] */
    int argc = vm->cur_argc;
    int id = (int)r_num(vm, argc - 1);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    int aidx = vm_array_new(vm);
    if (aidx < 0) { r_push_nil(vm); return 1; }
    if (id >= 0 && id < w->biome_count) {
        int chain[256], cn = 0;
        int cur = id;
        while (cur >= 0 && cn < 256) { chain[cn++] = cur; cur = w->biomes[cur].parent; }
        for (int i = cn - 1; i >= 0; i--) {
            Value v; v.type = VAL_INT; v.ival = chain[i]; v.fval = 0; v.sval = NULL;
            vm_array_push(vm, aidx, &v);
        }
    }
    Value a; a.type = VAL_ARRAY; a.ival = aidx + 1; a.fval = 0; a.sval = NULL;
    r_push(vm, a);
    return 1;
}
static int b_verse_biome_set(VM *vm) {  /* verse_biome_set(x,y,ly,i,bid) */
    int argc = vm->cur_argc;
    int x = (int)r_num(vm, argc - 1);
    int y = (int)r_num(vm, argc - 2);
    int ly = (int)r_num(vm, argc - 3);
    int i = (int)r_num(vm, argc - 4);
    int bid = (int)r_num(vm, argc - 5);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    if (bid < 0 || bid >= w->biome_count) { r_push_int(vm, 0); return 1; }
    block_set(&w->biome_map, bk(x, y, ly, i), (int16_t)(bid + 1));
    r_push_int(vm, 1);
    return 1;
}
static int b_verse_biome_get(VM *vm) {  /* verse_biome_get(x,y,ly,i) -> bid or -1 */
    int argc = vm->cur_argc;
    int x = (int)r_num(vm, argc - 1);
    int y = (int)r_num(vm, argc - 2);
    int ly = (int)r_num(vm, argc - 3);
    int i = (int)r_num(vm, argc - 4);
    r_popn(vm, argc);
    int16_t v = block_get(&vw()->biome_map, bk(x, y, ly, i));
    r_push_int(vm, v > 0 ? (int)v - 1 : -1);
    return 1;
}
static int b_verse_biome_palette(VM *vm) {  /* verse_biome_palette(id, [t1,t2,...]) */
    int argc = vm->cur_argc;
    int id = (int)r_num(vm, argc - 1);
    Value arr = r_arg(vm, argc - 2);
    r_popn(vm, argc);
    VerseWorld *w = vw();
    if (id < 0 || id >= w->biome_count) { r_push_int(vm, 0); return 1; }
    Biome *b = &w->biomes[id];
    b->palette_count = 0;
    if (arr.type == VAL_ARRAY) {
        ArrayObj *a = vm_pool_slot(vm, arr.ival - 1);
        if (a) {
            for (int j = 0; j < a->count && b->palette_count < VERSE_BIOME_PALETTE; j++) {
                if (a->items[j].type == VAL_INT) b->palette[b->palette_count++] = a->items[j].ival;
            }
        }
    }
    r_push_int(vm, b->palette_count);
    return 1;
}

void infiverse_mod_register(VM *vm) {
    vm_register_builtin(vm, "verse_use", b_verse_use);
    vm_register_builtin(vm, "verse_block_set", b_verse_block_set);
    vm_register_builtin(vm, "verse_block_get", b_verse_block_get);
    vm_register_builtin(vm, "verse_block_count", b_verse_block_count);
    vm_register_builtin(vm, "verse_link", b_verse_link);
    vm_register_builtin(vm, "verse_neighbors", b_verse_neighbors);
    vm_register_builtin(vm, "verse_dial", b_verse_dial);
    vm_register_builtin(vm, "verse_portal_set", b_verse_portal_set);
    vm_register_builtin(vm, "verse_portal_get", b_verse_portal_get);
    vm_register_builtin(vm, "verse_entity_put", b_verse_entity_put);
    vm_register_builtin(vm, "verse_entity_remove", b_verse_entity_remove);
    vm_register_builtin(vm, "verse_nearby", b_verse_nearby);
    vm_register_builtin(vm, "verse_law_set", b_verse_law_set);
    vm_register_builtin(vm, "verse_law_get", b_verse_law_get);
    vm_register_builtin(vm, "verse_snapshot", b_verse_snapshot);
    vm_register_builtin(vm, "verse_biome_add", b_verse_biome_add);
    vm_register_builtin(vm, "verse_biome_id", b_verse_biome_id);
    vm_register_builtin(vm, "verse_biome_name", b_verse_biome_name);
    vm_register_builtin(vm, "verse_biome_info", b_verse_biome_info);
    vm_register_builtin(vm, "verse_biome_children", b_verse_biome_children);
    vm_register_builtin(vm, "verse_biome_ancestors", b_verse_biome_ancestors);
    vm_register_builtin(vm, "verse_biome_set", b_verse_biome_set);
    vm_register_builtin(vm, "verse_biome_get", b_verse_biome_get);
    vm_register_builtin(vm, "verse_biome_palette", b_verse_biome_palette);
    printf("[infiverse mod] loaded (%d worlds, world %d active)\n", VERSE_MAX_WORLDS, cur_world);
}
