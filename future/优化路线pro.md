# Inimerse 极致优化计划书（完整版）

## 目录

1. 总纲与目标
2. 算法级底层优化
3. 编译器优化
4. JIT 编译器
5. 底层接口与汇编
6. 性能基准标准
7. 安全与性能平衡
8. 实施路线图
9. 与 Infiverse 特性的整合

---

## 一、总纲与目标

### 1.1 核心目标

| 目标 | 指标 | 衡量方式 |
|---|---|---|
| **性能** | 计算密集代码达到 C -O2 的 1.0~1.5x | 基准测试套件 |
| **灵活性** | 热修改、宇宙变换、动态类型全保留 | 功能测试 |
| **安全** | 性能代价 < 6% | 对比测试 |
| **兼容** | 所有优化不破坏已有脚本 | 回归测试 |

### 1.2 分层执行架构

```
┌──────────────────────────────────────────────────────────────┐
│                     Inimerse 执行引擎                          │
│                                                              │
│  层 0：解释执行                                               │
│  ├─ 适用：冷代码、初始化、一次性脚本                         │
│  ├─ 性能：≤25x O2                                           │
│  ├─ 动态性：完全动态，热修改即时生效                          │
│  └─ 特点：零编译开销，启动即运行                              │
│                                                              │
│  层 1：模板 JIT                                              │
│  ├─ 适用：热点循环、频繁调用函数                             │
│  ├─ 性能：≤3x O2                                            │
│  ├─ 动态性：版本号检查，热修改后失效重编译                   │
│  └─ 特点：编译快（<1ms），质量中等                           │
│                                                              │
│  层 2：优化 JIT                                              │
│  ├─ 适用：极热点代码、数值计算密集                           │
│  ├─ 性能：≤1.5x O2                                          │
│  ├─ 动态性：类型特化 + 假设失效回退                          │
│  └─ 特点：编译慢（<100ms），质量高                           │
│                                                              │
│  层 3：底层接口                                              │
│  ├─ 适用：性能关键模块、加密、物理引擎                       │
│  ├─ 性能：= O2（标量）/ = O3（SIMD）                        │
│  ├─ 动态性：无（原生代码）                                   │
│  └─ 特点：手写汇编或加载原生库                               │
│                                                              │
│  层 4：ML 辅助编译                                           │
│  ├─ 适用：优化决策、自动调优                                 │
│  ├─ 性能：提升整体 10-30%                                    │
│  └─ 特点：离线训练，在线推理                                  │
└──────────────────────────────────────────────────────────────┘
```

---

## 二、算法级底层优化

### 2.1 值表示

#### 2.1.1 NaN Boxing 完整设计

```c
// 64 位值布局
// ┌────────────────────────────────────────────────────┐
// │ bit 63-51: 必须为 0xFF8（NaN 标记）               │
// │ bit 50-48: 类型标记                                │
// │ bit 47-0:  载荷（int/float/pointer）               │
// └────────────────────────────────────────────────────┘

typedef uint64_t Value;

// 类型编码
#define VAL_INT      0x1    // 48 位有符号整数
#define VAL_FLOAT    0x2    // 48 位浮点载荷
#define VAL_STRING   0x3    // 指向字符串对象
#define VAL_ARRAY    0x4    // 指向数组对象
#define VAL_DICT     0x5    // 指向字典对象
#define VAL_EIDOS    0x6    // 指向 Eidos 对象
#define VAL_BOOL     0x7    // true=1, false=0
#define VAL_NIL      0x0    // 全零

// 快速类型检查
static inline int val_is_int(Value v) {
    return ((v >> 48) & 0x7) == VAL_INT;
}

static inline int val_is_float(Value v) {
    return ((v >> 48) & 0x7) == VAL_FLOAT;
}

static inline int val_is_ptr(Value v) {
    int tag = (v >> 48) & 0x7;
    return tag >= VAL_STRING && tag <= VAL_EIDOS;
}

// 打包/解包
static inline Value pack_int(int64_t raw) {
    return (0x7FF8ULL << 48) | (VAL_INT << 48) | (raw & 0xFFFFFFFFFFFFULL);
}

static inline int64_t unpack_int(Value v) {
    // 符号扩展 48 位
    int64_t raw = v & 0xFFFFFFFFFFFFULL;
    return (raw << 16) >> 16;
}

static inline Value pack_float(double d) {
    // 取浮点数的低 48 位作为载荷
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return (0x7FF8ULL << 48) | (VAL_FLOAT << 48) | (bits & 0xFFFFFFFFFFFFULL);
}

static inline double unpack_float(Value v) {
    uint64_t bits = (v & 0xFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;
    // 需要恢复完整的 64 位双精度
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

// 特殊优化：bool 直接编码
#define VAL_TRUE   (0x7FF8ULL << 48) | (VAL_BOOL << 48) | 1
#define VAL_FALSE  (0x7FF8ULL << 48) | (VAL_BOOL << 48) | 0
#define VAL_NIL_VALUE 0
```

#### 2.1.2 指针压缩

```c
// 堆基址固定，指针使用 32 位偏移
typedef struct {
    uint8_t* heap_base;
    uint8_t* heap_limit;
    uint32_t next_offset;
} CompressedHeap;

static inline Value pack_ptr(int tag, void* ptr, CompressedHeap* heap) {
    uint32_t offset = (uint8_t*)ptr - heap->heap_base;
    return (0x7FF8ULL << 48) | (tag << 48) | offset;
}

static inline void* unpack_ptr(Value v, CompressedHeap* heap) {
    uint32_t offset = v & 0xFFFFFFFF;
    return heap->heap_base + offset;
}
```

#### 2.1.3 立即字符串

```c
// ≤7 字节字符串直接编码
static inline Value pack_immediate_string(const char* s, int len) {
    Value v = (0x7FF8ULL << 48) | (VAL_STRING << 48);
    memcpy((char*)&v, s, len);
    return v;
}
```

### 2.2 哈希表

#### 2.2.1 Robin Hood 哈希

```c
typedef struct {
    uint32_t hash;
    uint32_t probe_distance;
    Value key;
    Value value;
} RobinHoodEntry;

// 插入时保持探测距离有序
// 新元素探测距离 > 现有元素 → 交换（Robin Hood 策略）
// 使最坏情况查找从 O(n) 降到 O(ln n)
```

#### 2.2.2 Swiss Table

```c
// 控制字节数组 + 键值数组分离
typedef struct {
    uint8_t* ctrl;       // 控制字节（0x80=空, 0x7F=墓碑, 其他=哈希高7位）
    Value* keys;
    Value* values;
    int capacity;
    int size;
} SwissTable;

// SIMD 并行查找 16 个槽
static inline int swiss_find(SwissTable* t, Value key) {
    uint64_t hash = hash_value(key);
    uint8_t h2 = hash >> 57;    // 高 7 位
    __m128i target = _mm_set1_epi8(h2);
    
    for (int group = 0; group < t->capacity / 16; group++) {
        __m128i ctrl = _mm_loadu_si128((__m128i*)(t->ctrl + group * 16));
        __m128i match = _mm_cmpeq_epi8(ctrl, target);
        int mask = _mm_movemask_epi8(match);
        
        while (mask) {
            int idx = __builtin_ctz(mask);
            int slot = group * 16 + idx;
            if (values_equal(t->keys[slot], key)) return slot;
            mask &= mask - 1;
        }
    }
    return -1;
}
```

#### 2.2.3 小字典

```c
typedef struct {
    int count;          // 0-4
    Value keys[4];
    Value values[4];
} SmallDict;

// 对于 ≤4 键的字典，线性查找比哈希快
// 大多数 Eidos 对象只有 3-5 个属性
```

### 2.3 字符串

#### 2.3.1 Rope 实现

```c
typedef struct {
    enum { STR_FLAT, STR_ROPE, STR_SLICE } kind;
    union {
        struct { char* data; int len; int cap; };
        struct { String* left; String* right; };
        struct { String* base; int offset; int len; };
    };
} String;

// 连接：O(1) 创建 Rope 节点
String* str_concat(String* a, String* b) {
    if (a->kind == STR_FLAT && a->len == 0) return b;
    if (b->kind == STR_FLAT && b->len == 0) return a;
    String* r = alloc_string();
    r->kind = STR_ROPE;
    r->left = a;
    r->right = b;
    return r;
}

// 扁平化：仅在需要时展开
// 使用迭代器避免递归栈溢出
```

#### 2.3.2 紧凑表示

```c
typedef enum {
    ENC_ASCII,      // 1 字节/字符
    ENC_LATIN1,     // 1 字节/字符
    ENC_UTF8,       // 变长
    ENC_UTF16       // 2 字节/字符
} StringEncoding;
```

### 2.4 内存分配

#### 2.4.1 Bump Allocator

```c
typedef struct {
    char* start;
    char* current;
    char* end;
} BumpAllocator;

void* bump_alloc(BumpAllocator* ba, size_t size) {
    size = (size + 15) & ~15;    // 16 字节对齐
    if (ba->current + size > ba->end) return NULL;
    void* ptr = ba->current;
    ba->current += size;
    return ptr;
}

void bump_reset(BumpAllocator* ba) {
    ba->current = ba->start;
}
```

#### 2.4.2 TLAB

```c
typedef struct {
    BumpAllocator local;
    BumpAllocator* global_pool;
} TLAB;

// 每线程 64KB 缓冲
#define TLAB_SIZE 65536

void* tlab_alloc(TLAB* tlab, size_t size) {
    void* ptr = bump_alloc(&tlab->local, size);
    if (ptr) return ptr;
    // 从全局池获取新块
    refresh_tlab(tlab);
    return bump_alloc(&tlab->local, size);
}
```

#### 2.4.3 尺寸类分配器

```c
// 小对象（≤ 256 字节）按 16 字节间隔分桶
// 每桶一个自由链表
typedef struct {
    void* free_list[16];    // 16, 32, 48, ..., 256
} SizeClassAllocator;
```

### 2.5 GC

#### 2.5.1 分代 GC

```c
typedef struct {
    // 年轻代：两个半区（复制式）
    char* young_from;
    char* young_to;
    int young_size;
    
    // 老年代：标记-清除
    Object** old_objects;
    int old_count;
    
    // 卡表
    uint8_t* card_table;
    int card_count;
} GenerationalGC;
```

#### 2.5.2 写屏障

```c
// 写入对象字段时调用
static inline void write_barrier(GC* gc, Object* parent, Value* field, Value new_val) {
    *field = new_val;
    if (gc->in_young_gc && is_old_object(parent)) {
        // 标记卡
        int card_idx = ((char*)field - (char*)parent) / 256;
        gc->card_table[card_idx] = 1;
    }
}
```

#### 2.5.3 增量标记

```c
// 每次执行 N 条指令后标记 M 个对象
void gc_incremental_step(GC* gc) {
    if (gc->worklist_head == NULL) return;
    for (int i = 0; i < gc->step_size; i++) {
        Object* obj = pop_worklist(gc);
        if (obj == NULL) return;
        mark_children(gc, obj);
    }
}
```

---

## 三、编译器优化

### 3.1 AST 优化 Pass 详细

#### Pass 1：常量折叠

```c
// 遍历 AST，折叠常量表达式
ASTNode* fold_constants(ASTNode* node) {
    if (node->type == AST_BINARY_OP) {
        node->left = fold_constants(node->left);
        node->right = fold_constants(node->right);
        
        if (is_const(node->left) && is_const(node->right)) {
            Value result = compute(node->op, node->left->value, node->right->value);
            return make_const_node(result);
        }
    }
    return node;
}
```

#### Pass 2：死代码消除

```c
// 构建控制流图，标记可达代码
// 移除不可达基本块
// 移除无副作用的未使用表达式
```

#### Pass 3：循环不变量外提

```c
// 分析循环体，找出不随循环变化的表达式
// 移出循环外，避免重复计算
for (int i = 0; i < n; i++) {
    x = expensive_computation();   // 不依赖 i → 外提
    arr[i] = x;
}
// 变为：
x = expensive_computation();
for (int i = 0; i < n; i++) {
    arr[i] = x;
}
```

#### Pass 4：循环强度削减

```c
// i * 4 → 累加器
// 初始：x = 0
// 每轮：x += 4
```

#### Pass 5：尾调用消除

```c
// 尾递归 → 循环
// func f(n) { if n <= 1 return 1; return f(n-1) * n; }
// → while 循环
```

### 3.2 字节码优化 Pass

#### 窥孔优化规则表

```c
// 规则：模式 → 替换
{ OP_LOAD_CONST, OP_ADD } → { OP_ADD_CONST }
{ OP_LOAD_CONST_0, OP_ADD } → {}   // 加 0 无操作
{ OP_JUMP, OP_JUMP } → { OP_JUMP }  // 跳转链接
{ OP_LOAD, OP_STORE_SAME } → {}     // 无操作
{ OP_CMP, OP_JUMP_IF_FALSE } → { OP_JUMP_IF }  // 合并
```

#### 寄存器分配

```
生命周期分析 → 干扰图构建 → 图着色（或线性扫描）
目标：最小化寄存器使用，减少内存溢出
```

### 3.3 Eidos 优化

#### 属性偏移量表

```c
typedef struct {
    int prop_count;
    int* prop_offsets;      // 属性名 → 偏移量
    EidosVTable* vtable;
} EidosLayout;

// 属性访问：
// obj.hp → *(Value*)((char*)obj + header_size + prop_offsets["hp"] * 8)
```

#### 方法内联缓存

```c
typedef struct {
    EidosType* cached_type;
    MethodFunction cached_method;
} InlineCache;

MethodFunction ic_lookup(InlineCache* ic, Object* obj, int method_idx) {
    if (obj->type == ic->cached_type) {
        return ic->cached_method;    // 命中
    }
    // 未命中：查找 vtable 并更新缓存
    ic->cached_type = obj->type;
    ic->cached_method = obj->type->vtable->methods[method_idx];
    return ic->cached_method;
}
```

---

## 四、JIT 编译器

### 4.1 模板 JIT

```c
// 预定义汇编模板
static const char* template_add_int = 
    "mov rax, [rsp+8]      \n"   // 加载操作数 1
    "add rax, [rsp+16]     \n"   // 加操作数 2
    "ret                   \n";

// 生成函数
void* jit_compile_template(Bytecode* code) {
    void* buf = native_alloc_exec(4096);
    char* p = buf;
    
    for (int i = 0; i < code->length; i++) {
        int opcode = code->ops[i];
        // 拼接对应模板
        memcpy(p, templates[opcode], template_sizes[opcode]);
        p += template_sizes[opcode];
    }
    
    native_make_executable(buf);
    return buf;
}
```

### 4.2 类型特化

```c
// 生成带类型检查的快速路径
void* jit_compile_specialized(Bytecode* code, TypeInfo* assumed_types) {
    // 快速路径
    emit_type_check(assumed_types);    // 检查假设
    emit_fast_code(code, assumed_types);  // 特化代码
    emit_jump_to_slow_path();          // 假设失败 → 回退
}
```

### 4.3 逃逸分析

```
分析对象的创建和使用位置
  ↓
对象不逃逸出方法？
  ↓ 是
栈分配（无需 GC）
  ↓
或标量替换（分解为寄存器变量）
```

### 4.4 自动向量化

```c
// 检测循环中的 SIMD 机会
// for i in range(n): c[i] = a[i] + b[i]
// → 使用 AVX2 一次处理 8 个元素

// 向量化检查：
// - 数组访问连续
// - 操作可向量化（加减乘等）
// - 无循环间依赖
// - 边界检查可合并
```

---

## 五、底层接口与汇编

### 5.1 完整 ABI 规范

```c
// 系统调用接口
typedef struct {
    // x86-64 寄存器约定
    int64_t (*call_0)(void* func);
    int64_t (*call_1)(void* func, int64_t a1);
    int64_t (*call_2)(void* func, int64_t a1, int64_t a2);
    int64_t (*call_3)(void* func, int64_t a1, int64_t a2, int64_t a3);
    int64_t (*call_4)(void* func, int64_t a1, int64_t a2, int64_t a3, int64_t a4);
} NativeCallInterface;
```

### 5.2 自动桥接代码生成

```c
// 脚本类型 → 原生类型
Value bridge_to_native(Value v, TypeInfo* expected) {
    if (expected->type == NATIVE_INT) {
        return unpack_int(v);
    }
    if (expected->type == NATIVE_DOUBLE) {
        return unpack_float(v);
    }
    if (expected->type == NATIVE_PTR) {
        return unpack_ptr(v);
    }
    // ...
}
```

### 5.3 SIMD 内置函数表

```
simd_add_ps(xmm0, xmm1)    → addps xmm0, xmm1
simd_mul_ps(xmm0, xmm1)    → mulps xmm0, xmm1
simd_dp_ps(xmm0, xmm1, i8) → dpps xmm0, xmm1, imm8
simd_sqrt_ps(xmm0)         → sqrtps xmm0
simd_rsqrt_ps(xmm0)        → rsqrtps xmm0
simd_cmp_gt_ps(xmm0, xmm1) → cmpltps xmm1, xmm0
simd_max_ps(xmm0, xmm1)    → maxps xmm0, xmm1
simd_min_ps(xmm0, xmm1)    → minps xmm0, xmm1
simd_and_ps(xmm0, xmm1)    → andps xmm0, xmm1
simd_or_ps(xmm0, xmm1)     → orps xmm0, xmm1
simd_xor_ps(xmm0, xmm1)    → xorps xmm0, xmm1
simd_andnot_ps(xmm0, xmm1) → andnps xmm0, xmm1
```

### 5.4 性能基准表（更新版）

| 操作 | C -O2 | C -O3+SIMD | 解释器 | 模板JIT | 优化JIT | 底层接口 | 底层+SIMD |
|---|---|---|---|---|---|---|---|
| int add 10^9 | 100ms | 25ms | 2500ms | 200ms | 120ms | 100ms | 25ms |
| float mul 10^9 | 110ms | 30ms | 2800ms | 220ms | 130ms | 110ms | 30ms |
| mat4 mul 10^7 | 300ms | 80ms | 8000ms | 600ms | 350ms | 300ms | 80ms |
| vec4 dot 10^8 | 200ms | 50ms | 5000ms | 400ms | 250ms | 200ms | 50ms |
| AES 10^6 | 120ms | 30ms | 3000ms | 250ms | 150ms | 120ms | 30ms |
| array sum 10^9 | 120ms | 25ms | 3000ms | 250ms | 150ms | 120ms | 25ms |

---

## 六、性能基准标准

### 6.1 达标线

| 执行模式 | 最低要求 | 努力目标 |
|---|---|---|
| 解释执行 | ≤25x O2 | ≤20x O2 |
| 模板 JIT | ≤3x O2 | ≤2x O2 |
| 优化 JIT | ≤1.5x O2 | ≤1.2x O2 |
| 底层接口（标量） | =O2 | 0.8x O2 |
| 底层接口（SIMD） | =O3 | 0.5x O3 |

### 6.2 基准测试套件

```
整数运算    20%
浮点运算    20%
内存访问    25%
对象操作    20%
控制流      15%
```

### 6.3 CI 集成

- 每 commit 运行基准
- 性能回退 >5% 阻止合并
- 生成性能报告

---

## 七、安全与性能平衡

| 安全机制 | 性能代价 | 防护对象 |
|---|---|---|
| 进程隔离 | <1% | 恶意代码 |
| 加密存储 | <0.2ms/帧 | 内存读取 |
| 密钥轮换 | 忽略 | DMA/ML |
| 服务器权威 | <0.1ms/帧 | 篡改 |
| 沙盒权限 | <1% | 越权 |
| 热修改保护 | <0.1% | 运行时安全 |
| **总计** | **<6%** | **全面防护** |

---

## 八、实施路线图（详细）

### P0：算法基础（第 1-4 周）

| 工作 | 预期提升 | 前置条件 |
|---|---|---|
| NaN Boxing 值表示 | 内存减半，提速 20-30% | 无 |
| Robin Hood 哈希 | 字典提速 30% | 无 |
| Bump Allocator + TLAB | 分配提速 5-10 倍 | 无 |
| 类型特化数组 | 数值数组提速 10 倍 | 无 |
| 小字典优化 | 对象属性提速 20% | 无 |

### P1：AST 与字节码优化（第 5-12 周）

| 工作 | 预期提升 | 前置条件 |
|---|---|---|
| 常量折叠/传播 | 10-30% | P0 |
| 窥孔优化 | 5-15% | P0 |
| 循环不变量外提 | 20-200% | P0 |
| vtable 化 | 方法调用 10 倍 | P0 |
| 内联缓存 | 方法调用 25 倍 | vtable |
| 属性偏移量 | 属性访问 10 倍 | P0 |

### P2：模板 JIT（第 3-6 月）

| 工作 | 预期提升 | 前置条件 |
|---|---|---|
| 热点检测 | — | P1 |
| 模板汇编库 | 5-20 倍 | P1 |
| 类型特化 | +30% | 模板 JIT |
| GC 栈映射 | 安全性 | P1 |
| 内联缓存失效 | 热修改支持 | P1 |

### P3：优化 JIT（第 6-12 月）

| 工作 | 预期提升 | 前置条件 |
|---|---|---|
| 数据流分析 | +20% | P2 |
| 逃逸分析 | +15% | P2 |
| 方法内联 | +50% | P2 |
| 寄存器分配 | +10% | P2 |
| 分代 GC | 暂停 100→1ms | P2 |
| 自动向量化 | 数值 4-8 倍 | P2 |

### P4：极限性能（第 12-24 月）

| 工作 | 预期提升 | 前置条件 |
|---|---|---|
| 底层接口 + SIMD | =O3 | P3 |
| ML 辅助编译 | +10-30% | P3 |
| AOT 编译 | 启动提速 | P3 |
| LLVM IR 接入 | 生态扩展 | P3 |

---

## 九、与 Infiverse 特性的整合

### 9.1 热修改与 JIT 共存

```
Eidos 热修改 → 版本号递增 → JIT 代码检查版本号 → 失效重编译
延迟：<1ms
```

### 9.2 宇宙变换与类型特化

```
对象穿越宇宙 → 类型改变 → 特化假设失效 → 回退通用路径
新宇宙中的类型模式稳定后 → 重新特化
```

### 9.3 反 DMA 与底层接口

```
底层接口代码使用 AES-NI 指令加密
密钥在 XMM 寄存器中，不出现在内存
DMA 无法读取寄存器
```

### 9.4 虚拟课堂与性能

```
40 个学生共享同一宇宙
每个学生的设备不同
自动选择：低端设备→解释，高端→JIT
同一宇宙，不同设备，无缝适配
```

---

## 十、总结

**Inimerse 的极致优化是一场从“解释器”到“自适应优化编译器”的演化。**

| 维度 | 起点 | 终点 |
|---|---|---|
| 值表示 | 装箱对象 | NaN Boxing + 指针压缩 |
| 属性访问 | 哈希查找 | 偏移量直接寻址 |
| 方法调用 | 字典查找 | vtable + 内联缓存 |
| 内存分配 | malloc | Bump + TLAB + 尺寸类 |
| GC | 标记-清除 | 分代 + 增量 + 并发 |
| 执行 | 解释执行 | 多层 JIT + 底层接口 |
| 优化决策 | 静态启发式 | ML 自适应 |
| 性能 | 25x O2 | 1.0x O2（底层） |
| 灵活性 | 动态 | 动态 + 投机静态 |
| 安全 | 无 | 多层沙盒 <6% 代价 |

**这是 Inimerse 从一个“脚本引擎”蜕变为“Infiverse 物理常数”的完整技术路线。** 每一步优化都经过精心设计，既追求极致性能，又不牺牲动态语言的灵活性和多宇宙安全性。

**最终，Inimerse 将成为这样一个存在：它让脚本像 C 一样快，让动态像静态一样稳，让安全像物理定律一样不可动摇。**