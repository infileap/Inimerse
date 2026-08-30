#ifndef VM_H
#define VM_H

#include "bytecode.h"
#include "../platform/sync.h"
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <stdint.h>
typedef pthread_mutex_t CRITICAL_SECTION;
static inline void InitializeCriticalSection(CRITICAL_SECTION *m) { pthread_mutex_init(m, NULL); }
static inline void DeleteCriticalSection(CRITICAL_SECTION *m) { pthread_mutex_destroy(m); }
static inline void EnterCriticalSection(CRITICAL_SECTION *m) { pthread_mutex_lock(m); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *m) { pthread_mutex_unlock(m); }
#endif

typedef struct VM VM;
typedef struct VmThread VmThread;

enum ValueType { VAL_INT, VAL_FLOAT, VAL_STRING, VAL_BOOL, VAL_OBJECT, VAL_NIL, VAL_ARRAY, VAL_DICT, VAL_SET, VAL_FUNCTION };

typedef struct {
    int type; int ival; double fval; char *sval;
} Value;

/* set interval component: builtin set nameIdx intersected with [lo,hi] (inc flags); +/-1e308 = unbounded */
typedef struct {
    int nameIdx;
    int loInc, hiInc;
    double lo, hi;
} SetComp;

/* set object: kind 0=finite(+components) 1=named builtin 2=interval of a builtin set */
typedef struct {
    int kind;
    int nameIdx;      /* builtin set table index (N/Z/Z+/Z-/FloatN/floatN) */
    int loInc, hiInc; /* interval endpoint inclusivity */
    double lo, hi;    /* interval bounds; +/-1e308 = unbounded */
    long long *i64;   /* compact integer elements (finite sets) */
    int iCount, iCap;
    Value *items;     /* non-integer elements (float/string/bool) */
    int count, cap;
    SetComp *comps;   /* components (finite intervals / named sets) in a literal */
    int compCount, compCap;
} SetObj;

/* 锟竭筹拷执锟斤拷锟斤拷锟斤拷锟侥ｏ拷每锟斤拷 OS 锟竭程讹拷锟斤拷一锟捷ｏ拷 */
#define VM_MAX_THREADS 32
#define VM_THREAD_REG_COUNT (2048 * 1024) /* 2048 ??锟斤�?1024 锟侥达拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟??锟节诧拷莨锟斤拷锟饺★拷~45,512帧锟斤拷锟斤拷锟斤拷 */
#define VM_MAX_FRAMES 2048
#define VM_MAX_TASKS 2048          /* virtual threads (Fiber-driven, no OS thread) */
#define VM_TASK_REG_COUNT 8192     /* task register file (192KB, ~48MB for OS threads) */
#define VM_TASK_MAX_FRAMES 128     /* task frame stack depth */
#define VM_TASK_BUDGET 10000       /* instructions per task before forced yield */
#define ENTITY_GRID_W 64
#define ENTITY_GRID_H 48
#define ENTITY_CELL 16.0
#define VM_FRAME_REGS 1024

typedef struct {
    Bytecode *code;
    int ip;
    int sp;
    int base;
    int frame_count;
    int catch_ip;
    int var_idx;
    int ignore;   /* 1 = bare try (no catch): exception swallowed + recorded into VM debug slot */
} ExcFrame;

struct VmThread {
    VM *vm;
    Bytecode *code; int ip;
    Value *reg;             /* 执锟斤拷锟斤拷锟斤拷锟侥ｏ拷锟侥达拷锟斤拷锟斤拷malloc??*/
    int reg_cap;            /* reg capacity (thread=VM_THREAD_REG_COUNT, task=VM_TASK_REG_COUNT) */
    Value *R;
    Value stack[1024]; int sp;
    Bytecode **frame_code;   /* frame stack (dynamic: thread=VM_MAX_FRAMES, task=VM_TASK_MAX_FRAMES) */
    int frame_cap;
    int *frame_ip;
    int *frame_base;
    int *frame_res;
    int *frame_sp;   /* save sp at call for correct return */
    int frame_count;
    /* task (virtual thread, Fiber-driven) fields */
    bool is_task;
    void *fiber_self;       /* Windows Fiber */
    void *fiber_sched;      /* scheduler fiber (yield target) */
    int budget;             /* instr budget, yield when <=0 */
    bool blocked;           /* blocked on wait/recv: scheduler skips until wake */
    int base;
    volatile bool running;
    volatile bool paused;
    volatile bool stop_flag;
    volatile bool finished;
    volatile long gc_parked;  /* parked at a GC safe point (stop-the-world) */
    bool is_main;
    int tidx;
               /* 锟竭程讹拷锟斤拷锟斤拷锟斤拷??1=锟斤拷锟竭程ｏ拷 */
    void *os_handle;        /* Windows 锟竭程撅拷锟?*/
    unsigned long long wake_at;  /* 锟斤拷时锟街革拷时锟戒（GetTickCount64??*/
    /* 锟斤拷息锟斤拷锟叫ｏ拷锟斤拷锟轿ｏ拷锟街凤拷锟斤拷锟筋拷锟斤�??*/
    Value *msg_q; int msg_head; int msg_tail; int msg_cap;
    void *msg_lock;         /* CRITICAL_SECTION* */
    int flags;              /* THREAD_FLAG_* labels (endless/daemon/restart/single, appended) */
    ExcFrame *exc_stack;    /* exception frames (appended last, ABI-safe) */
    int exc_depth;
    int exc_cap;
    volatile int jump_req;   /* thread jump request (label offset) -1=none (appended) */
    volatile unsigned long long wait_end; /* L_WAIT re-entrant deadline (0 = none, appended) */
};

#define ARRAY_INLINE_CAP 8  /* L1: small arrays live inline in ArrayObj (no heap) */
typedef struct {
    Value *items;
    int count;
    int cap;
    Value inline_buf[ARRAY_INLINE_CAP];
} ArrayObj;

typedef int (*BuiltinFunc)(VM *vm);
typedef void (*VMHook)(VM *vm);

/* L1 modular SPI: builtin capability domains (minimal-permission model).
   bit0 (0x01) stays the legacy "dangerous" flag (safe_mode gate);
   CAP_MASK bits declare which domains a builtin needs.
   Scripts declare their needs via spi_meta(id, version, "io,net"); vm->mod_caps
   is -1 (unrestricted) for the platform/C mods. */
#define CAP_IO    0x0100
#define CAP_NET   0x0200
#define CAP_AI    0x0400
#define CAP_VERSE 0x0800
#define CAP_DBG   0x1000
#define CAP_PROC  0x2000
#define CAP_MASK  0xFF00

#define VM_STR_POOL_LIMIT (1 << 20) /* 1M distinct strings (dedup bounds growth) */ /* 锟街凤拷锟斤拷锟斤拷锟斤拷锟斤拷:锟斤拷锟睫猴拷锟斤拷锟街凤拷锟斤拷锟斤拷??intern(锟斤拷锟斤拷 strdup),锟斤拷止锟斤拷锟斤拷锟斤拷锟斤拷 */
struct StrPool;

typedef struct { char *name; Value val; } GlobalSlot;

/* 绌洪棿缃戞牸妗讹細姣忔《涓€涓姩�?id 鏁扮粍锛堣閬块摼琛ㄦ《鎮�?鎴愮幆锛?*/
typedef struct { int *ids; int count; int cap; } EntBucket;
struct VM {
    Bytecode *code; volatile int ip; Value stack[1024]; int sp;
    GlobalSlot *globals; int globalCount; int globalCap;  /* dynamically grown; grow under VM_LOCK */
    struct { char *name; BuiltinFunc func; int flags; int since; } builtins[512]; int builtinCount;  /* flags: bit0=1 dangerous (blocked in safe_mode) */
    struct { char *name; VMHook func; } hooks[16]; int hookCount;
    /* spi event bus: spi_on(event, funcname) / spi_emit(event, data) */
    struct SpiSub *spi_subs; int spi_sub_count, spi_sub_cap;
    void *user_data;
    bool running; volatile bool step_mode;  /* step_mode is written by the debugger thread */
    void (*debug_script)(VM *vm, const char *script_path);
    void (*build_script)(VM *vm, const char *input, const char *output);
    /* GUI 支锟斤拷 */
    void (*print_hook)(const char *text);
    void (*gui_run)(VM *vm, const char *script_path);
    /* 锟斤拷锟?exe 锟斤拷锟斤拷时锟斤拷锟斤拷锟斤拷嵌锟斤拷锟侥ｏ拷�??main 锟斤拷锟斤拷??*/
    void (*load_embedded_mods)(VM *vm);
    /* 锟斤拷锟斤拷兀锟阶凤拷锟斤拷诮峁癸拷锟侥┪诧拷锟斤拷锟斤拷锟斤拷锟斤拷�??DLL 锟斤拷锟街讹拷偏锟狡硷拷锟捷ｏ拷 */
    /* 锟斤拷锟斤拷/锟街碉拷兀锟絛ict 锟斤拷锟斤拷�?key/value锟斤拷锟斤拷锟斤拷锟斤拷锟姐够锟斤拷锟皆筹拷锟斤�?AST 锟饺革拷锟接结构锟斤�?*/
    ArrayObj arrays[4096];
    int arrayCount;
    /* 锟竭程ｏ拷同锟斤拷追锟斤拷锟斤拷末尾??*/
    struct VmThread *threads[VM_MAX_THREADS];   /* 锟筋动锟竭筹拷实锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤�??*/
    struct VmThread *tasks[VM_MAX_TASKS]; /* Z�?Fiber	
` OS ??*/
    /* entity system (SoA + spatial grid) */
    float *ent_x, *ent_y, *ent_vx, *ent_vy;
    int *ent_hp, *ent_kind;
    int ent_cap, ent_count;
/* 绌洪棿缃戞牸妗讹細姣忔《涓€涓姩�?id 鏁扮粍锛堣閬块摼琛ㄦ《鎮�?鎴愮幆锛?*/
EntBucket *ent_buckets;
    int *ent_free;
    int ent_free_head;
    int ent_grid_dirty;
    int task_count;
    volatile bool sched_running;
    void *sched_thread;               /* �?OS ?�?*/
    void *global_lock;
    void *global_locks[16];          /* 鍒嗙墖閿侊紙鍏ㄥ眬妲借闂儹鐐癸級锛岃VM_GSHARD */      /* CRITICAL_SECTION*锟斤拷锟斤拷??globals/arrays */
    void *mutexes[256];      /* CRITICAL_SECTION* 锟斤拷锟斤拷??*/
    int mutex_count;
    /* 锟斤拷锟斤拷锟叫诧拷锟斤拷锟斤拷args() 锟斤拷锟矫猴拷锟斤拷使锟矫ｏ拷argv[0] 锟角脚憋拷路锟斤拷之锟斤拷牡锟揭伙拷锟斤拷锟斤拷锟斤拷锟?*/
    int argc;
    char **argv;
    /* 锟斤拷锟斤拷囟锟教拷锟斤拷荩锟阶凤拷锟斤拷锟侥┪诧拷锟斤拷锟斤拷志锟斤�??DLL 锟街讹拷偏锟狡硷拷锟斤拷??*/
    ArrayObj *arrays_big;   /* 锟斤拷锟斤拷??>= 4096 锟侥讹拷??*/
    int bigCap;             /* arrays_big 锟斤拷锟斤拷 */
    /* 锟街凤拷锟斤拷锟斤拷(interning):VAL_STRING ??ival==1 锟斤拷示锟斤拷锟街凤拷锟斤拷(锟斤拷锟斤拷锟斤拷锟斤拷锟酵放★拷锟斤拷指锟斤拷冉锟? */
    struct StrPool *str_pool;
    int cur_argc;  /* 锟斤拷前锟斤拷锟矫碉拷锟矫的诧拷锟斤拷锟斤拷??锟斤拷锟矫碉拷锟斤拷??锟斤拷锟解弹锟斤拷锟斤拷�?? */
    /* 执锟斤拷时锟斤拷锟斤拷锟斤拷(ms),0=锟斤拷锟斤拷????main 锟斤拷锟斤拷 --time-limit 锟斤拷锟斤拷 */
    unsigned long exec_timeout_ms;
    /* debug hook: instruction-boundary callback (debug mod) */
    void (*debug_hook)(VM *vm);
    struct VmThread *main_thread;   /* main thread ptr (set by vm_run, for debug jump) (appended) */

    /* declare resource limits (appended, ABI-safe) */
    double limit_mem;
    int limit_threads;
    double limit_time;
    double limit_inst;
    double used_mem;
    volatile long active_threads;
    unsigned long long t_start;
    /* record system (appended last: keeps old DLL ABI offsets intact) */
    int record_default_store;
    struct RecordMetaTag { int store; int scope; int merge; int dirty; int version; } *record_meta;
    int record_meta_count;
    char **record_names;
    int record_names_cap;
    int record_loaded_dict; /* loaded save dict (aidx+1), 0 = none */
    char *record_save_path;
    unsigned long long record_autosave_interval; /* ms,0 = off */
    unsigned long long record_last_autosave;
    /* GUI 锟斤拷息锟矫癸拷锟斤�? GUI 模式锟斤�?wait 锟节硷拷也锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷�?(gui_mod 锟节达拷锟斤拷锟斤拷台时锟斤拷锟斤拷) */
    int (*gui_pump)(void);
    /* 执锟斤拷锟斤拷始时锟斤拷(ms): 锟斤拷锟斤拷锟矫猴拷锟斤拷锟斤拷�?--time-limit / declare time */
    unsigned long long exec_start_ms;
    /* set support (appended last, ABI-safe) */
    SetObj *sets;
    int setCount;
    int setCap;
    int *be_bound; int be_bound_cap; /* per-global bound set (set pool idx+1, 0=none), for OP_BE; grown with globals */

    /* Inimerse2D engine state (appended last, ABI-safe) */
    int im2d_interval_ms;        /* frame interval in ms, 0 = off */
    unsigned long long im2d_next_frame;
    int im2d_ready;              /* on_load callback executed */
    double im2d_dt;              /* last frame dt (seconds) */
    char im2d_scene[64];         /* pending scene switch ("" = none) */
    char im2d_last_scene[64];
    int im2d_cb_load, im2d_cb_update, im2d_cb_render; /* cached callback func idx (-1=unresolved, -2=absent) */
    int builtin_hash[512]; /* open-addressing fast builtin lookup: builtin idx+1, 0=empty */

    /* script-level debugger (appended last, ABI-safe): .im debugger replaces debug_mod.dll */
    volatile int dbg_active;      /* 1 = script debugger attached (main thread stops at boundaries) */
    volatile int dbg_pause;       /* 1 = main thread waits at boundary until debugger releases it */
    volatile int dbg_at_boundary; /* 1 = main thread currently stopped at an instruction boundary */
    volatile long long dbg_boundary_count; /* incremented at every boundary stop (fresh-stop detection) */
    struct Bytecode *mod_bcs[8];  /* mod-script bytecodes kept alive (debugger threads reference them) */
    int mod_bc_count;
    int safe_mode;                /* 1 = dangerous builtins (exec/fs/net/... ) are blocked (code-injection guard) */

    /* bare-try ignored-exception debug slot (appended last, ABI-safe) */
    char *last_ignored_exc;       /* string form of the last exception swallowed by a bare try (NULL = none) */
    long long ignored_exc_count;  /* how many exceptions have been swallowed by bare try since VM start */

    /* L1 dict: parallel open-addressing hash index per array-pool slot (appended last, ABI-safe) */
    struct DictHash *dict_hashes; /* [dict_hashes_cap]; a DictHash with slots==NULL = index not built yet */
    int dict_hashes_cap;
    int last_error;               /* 1 = last run ended with an uncaught exception (AI-loop exit code) */
    double limit_vram;
    /* ---------- mark-sweep GC for pool slots (arrays/dicts/sets): opt-in via gc_auto/--gc ---------- */
    int gc_enabled;          /* 1 = auto GC at instruction safe points */
    int gc_pending;          /* allocator crossed threshold: collect at next safe point */
    double gc_threshold;     /* used_mem trigger (bytes); 0 = auto (2MB initial) */
    volatile int gc_stop;    /* stop-the-world request (cooperative park) */
    volatile int gc_parked;  /* threads currently parked at a safe point */
    int *array_free_list;    /* recycled array slots (free list) */
    int array_free_n, array_free_cap;
    int *set_free_list;      /* recycled set slots (free list) */
    int set_free_n, set_free_cap;
    unsigned char *gc_amark; int gc_amark_cap;   /* per-array-slot mark bits */
    unsigned char *gc_smark; int gc_smark_cap;   /* per-set-slot mark bits */
    int *gc_work; int gc_work_count, gc_work_cap; /* mark work stack */
    int gc_runs; int gc_freed;            /* bitmap/VRAM byte cap, 0 = unlimited (forced via --limit-vram / declare vram) */

    /* L1 modular SPI (appended last, ABI-safe) */
    int mod_caps; /* -1 = unrestricted (platform/C mods); else declared capability bitmask */
    int modCount;
    struct { char id[48]; int version; int api_min; int caps; } mods[32];
};

typedef struct DictSlot { int pair_idx; unsigned hash; } DictSlot;
typedef struct DictHash {
    DictSlot *slots; /* open-addressing table, pair_idx = key slot index in the pair array (-1 = empty) */
    int cap;         /* power of two */
    int mask;
    int count;       /* live entries */
} DictHash;

/* 锟斤拷锟斤拷夭锟斤拷锟斤拷锟??runtime 锟斤拷锟矫猴拷锟斤拷??VM 指锟斤拷使锟斤拷??*/
ArrayObj *vm_pool_slot(VM *vm, int idx);
const char *vm_intern(VM *vm, const char *s);
int  vm_array_new(VM *vm);
void vm_array_push(VM *vm, int idx, const Value *v);
void vm_array_set(VM *vm, int idx, int i, const Value *v);
Value vm_array_get(VM *vm, int idx, int i);
Value vm_array_pop(VM *vm, int idx);
int  vm_array_len(VM *vm, int idx);
int vm_set_to_array(VM *vm, int sidx);
Value vm_dict_get(VM *vm, int aidx, const Value *key);
void vm_dict_set(VM *vm, int aidx, const Value *key, const Value *val);
bool vm_dict_remove(VM *vm, int aidx, const Value *key);
VmThread *vm_get_cur_thread(void);
typedef struct SpiSub { char *event; int tidx; } SpiSub;
VmThread *vm_task_create(VM *vm, Bytecode *root, int tidx, VmThread *t, int argc);
VmThread *vm_os_thread_start(VM *vm, Bytecode *root, int tidx, VmThread *t, int argc);
void value_free(Value *v);
void vm_set_cur_thread(VmThread *t);
double val_as_double(const Value *v);
bool val_eq(const Value *a, const Value *b);

#ifndef VM_LOCK
#define VM_LOCK(vm) im_mutex_lock((ImMutex*)((vm)->global_lock))
#define VM_UNLOCK(vm) im_mutex_unlock((ImMutex*)((vm)->global_lock))


#endif

/* 鍒嗙墖閿侊細鍏ㄥ眬妲借闂儹鐐癸紙L_LOAD_GLOBAL / L_STORE_GLOBAL / L_BE / vm_throw锛夈�?
   鎸夊叏灞€绱㈠紩鍝堝笇鍒?16 鎶婇攣鈥斺€斾笉鍚屽彉閲忓苟琛岋紝鍚屽彉閲忎覆琛屻€?
   global_lock锛堝垎鐗?涔嬪鐨勫崟閿侊級浠嶇敤浜庢暟缁?瀛楀吀/闆嗗�?瀛楃涓叉睜绛夐€氱敤涓寸晫鍖恒�?*/
#define VM_GLOBAL_SHARDS 16
#define VM_GSHARD(vm, idx) ((ImMutex*)((vm)->global_locks[((idx) & (VM_GLOBAL_SHARDS - 1))]))

/* 锟斤拷值转为锟街凤拷锟斤拷锟斤拷say/join 锟斤拷锟斤拷锟斤拷锟??*/
void vm_value_to_string(VM *vm, const Value *v, char *buf, int bufsz);

/* 锟斤拷前锟竭筹拷栈锟斤拷锟绞ｏ拷锟斤拷锟矫猴拷锟斤拷锟矫ｏ拷锟斤拷锟竭筹拷锟斤拷每锟竭程讹拷锟斤拷栈??*/
int builtin_lookup(VM *vm, const char *name);
int vm_cur_sp(VM *vm);
Value *vm_cur_stack(VM *vm);
void vm_cur_set_sp(VM *vm, int sp);

void push_int(VM *vm, int v);
void push_float(VM *vm, double v);
void push_string(VM *vm, const char *s);
void push_double(VM *vm, double d);
void push_bool(VM *vm, bool b);
void push_nil(VM *vm);
void pop(VM *vm);
Value *top(VM *vm);

void vm_init(VM *vm);
void vm_load_bytecode(VM *vm, Bytecode *bc);
void vm_run(VM *vm);
void vm_free(VM *vm);
void vm_register_builtin(VM *vm, const char *name, BuiltinFunc func);
void vm_register_builtin_safe(VM *vm, const char *name, BuiltinFunc func);  /* dangerous: blocked in safe_mode */
void vm_register_hook(VM *vm, const char *hook_name, VMHook func);
void vm_trigger_hook(VM *vm, const char *hook_name);
void vm_set_user_data(VM *vm, void *data);
void *vm_get_user_data(VM *vm);
void vm_throw_msg(VM *vm, const char *msg);
/* Throw a canonical member of one of the preset error sets. */
void vm_throw_kind(VM *vm, const char *kind);
void vm_global_grow(VM *vm, int need);
void vm_global_clone(VM *vm); /* swap globals for a deep-name/shallow-value copy (nested vm_exec) */
int vm_set_new(VM *vm);
void gc_collect(VM *vm); /* mark-sweep pool GC (opt-in, gc_auto/--gc) */
void vm_register_builtin_full(VM *vm, const char *name, BuiltinFunc func, int flags, int since);
void vm_register_mod(VM *vm, const char *id, int version, int api_min, int caps);
void limit_abort(VM *vm, const char *what, double used, double limit);
int vm_step(VM *vm);

#ifdef _WIN32
unsigned __stdcall thread_entry(LPVOID arg);
#else
void *thread_entry(void *arg);
#endif

/* debug console API (exported for debug_mod.dll) */
void vm_debug_exec(VM *vm, const char *code);
int vm_params_load(VM *vm, const char *path);
void vm_debug_jump(VM *vm, const char *thread_name, const char *label);
void vm_debug_var(VM *vm, const char *mode);
void vm_debug_threads(VM *vm);

/* script-debugger / mod-script support (replaces debug_mod.dll) */
int  vm_exec_script_file(VM *vm, const char *path);  /* parse+compile+run a .im file, keep its bytecode alive */
void vm_debug_builtins_register(VM *vm);              /* dbg_* builtins for .im debuggers */

#endif
