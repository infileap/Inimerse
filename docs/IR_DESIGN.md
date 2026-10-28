# IR 统一设计（IR_DESIGN）

状态：**设计稿，未实现任何一行**。本文档只写「应该长成什么形状、凭什么算成立、按什么顺序做」，
不改后端、不改字节码、不动解释器。

姊妹文档：`docs/DECFY_DESIGN.md`（去C化的整体方案，§3 是本文档的前身）。本文档不重复它的
层次表与裁定记录，只把 §3 从「方案」推进到「可施工的规格」：数据结构、单一权威表、四个消费者
（加第五个：`.im` 写侧）的契约、序列化与 ABI、验证装置、迁移分期与每一期的可证伪判据。

---

## 0. 结论摘要

1. **"IR 统一"不是"写一个中间表示"**。仓库里已经有三份从 `Expr*` 出发的 lowering（解释器、AOT、
   wasm）和两个只读消费者（反汇编器、`.im` 写侧）。今天缺的不是表示，是**同一份语义只写一次**
   的那个位置。
2. 设计成三层：**AST（前端契约，不动）→ MIR（唯一共享层，寄存器式、有类型提示、显式控制流）→
   目标选择（三个后端各自的私有层）**。语义规则只允许写在 MIR 的 `OpSemantics` 表里。
3. `OpSemantics` 表必须是**机器可读的单一源**（一个 Python 模块），由它生成：解释器 dispatch 的
   覆盖断言、AOT 的选择器表、wasm 的选择器表、反汇编器的名字与格式、`.im` 侧的 opcode 常量。
   一张表，五个消费者，一处修改。
4. 现有 `RegInstruction` 不废弃，**重新定义为 MIR 的扁平编码**（序列化形式），不是另一套 IR。
   操作码编号是 ABI：只许追加、不许复用（`OP_MOD` 是先例）。
5. 验证不是"跑一遍测试"，而是**表驱动的三端差分 + 覆盖率读数 + 突变 + 反证**，每一格都要有
   `tree=` 与 `spec-sha=`，都能被第三方复跑。
6. 迁移分 P0–P6，每期一个可证伪判据、一个回退点。**P0 不改变任何行为**（只建表与生成器），
   从第一行代码起就有读数可看。

---

## 1. 问题：同一语义今天写在几个地方

### 1.1 事实（可复现，不含行号引用）

| 位置 | 语言 | 输入 | 输出 |
| --- | --- | --- | --- |
| 编译器主体 | C | `Expr*` | `RegInstruction` 序列（`.inim` 内容） |
| `emit_expr`（AOT） | C | `Expr*` | 宿主 C 源码表达式（`nv_*` 帮助函数） |
| `cg_expr`（wasm） | C | `Expr*` | wasm 指令序列（槽位寻址） |
| `vm_disasm_ins` | C | `RegInstruction` | 一行文本 |
| `.im` 写侧编译器 | `.im` | 自己的语法树 | `INIMBC` 容器 |

五个位置里，**前三个各自把"运算 + 类型规则 + 错误行为"重写一遍**；后两个各自把"操作码叫什么、
怎么印/怎么编号"重写一遍。这不是五份代码，是**五份语义**。

### 1.2 漂移是结构性的，不是手艺问题

`%` 的统一过程提供了一个完整的样本（`tools/mod_spec.py`，`mod-rule-v1`）：

- 三个实现里有两处共享同一只截断函数的问题：`im_dbl_to_i64` 与 `nv_asi` 都只处理了"超出 int64
  范围"，NaN 落到 C 的未定义分支；wasm 走 `e_trunc_sat_i64`，于是同一表达式三端三个答案。
- 第四处是**测试自己**：`mod_semantics.test.py` 里有一张手写的 16 例期望表。它不参与实现，却在
  定义语义——改实现的人不知道要改它，改它的人不知道实现对不上。
- 统一之后，漂移消失的原因是**只剩一个可执行的定义**：`tools/mod_spec.py` 既是规则，也是用例
  生成器，测试从它取期望值。

**结论：IR 统一的核心动作不是"加一层"，而是"把规则搬到一个可执行的单一源里，然后让所有消费者
从那里取"。** 加层只是让这个源有一个自然的落点。

### 1.3 今天的一个具体不对称

`OpSemantics` 这张表在 `docs/DECFY_DESIGN.md` §3.1.7/§3.1.11 里已经写了 69 行（从 VM 的
`L_X:` 标签块逐块读出），但**代码里不存在这个符号**。表与代码之间没有任何机械联系：表说 A，
代码做 B，没有任何东西会红。这正是 `mod_spec` 之前那三处 `%` 的状态。

另有一处口径差值得先记下来：设计档的 69 行是**dispatch 标签**数，`bytecode.h` 里的 `OP_*` 是
**46 个**。两者不是同一个集合（有些标签不是 opcode，有些 opcode 没有独立标签）。统一时要先定
"一行"的判据，再谈覆盖率——否则覆盖率永远可以是 100%。

---

## 2. 目标与非目标

### 2.1 目标

1. 同一语义（算术、比较、布尔、容器、调用、异常、线程）**只在一处写规则**，其余位置从表里取。
2. 三个后端的行为差**可以被测出来**，而不是靠人读三份代码。
3. 新增一个 opcode 的代价从"改三处 + 改测试 + 改反汇编器 + 可能漏掉一处"降为"表里加一行，
   没实现的消费者报编译期错误"。
4. ABI 稳定：已入库的 `.inim` 继续可执行；新 opcode 只追加。
5. 每一步都能被第三方用一条命令复跑，读数带主语。

### 2.2 非目标

- **不删除 VM**、不把 C 当 IR。AOT 的产物是宿主 C 源码（必须经 `cc`），这是它的契约，不是缺陷。
- **不追求"三端字节相同"**。目标是一致的行为，不是一致的代码。差分测的是行为。
- **不改 parser 的 AST 形状**。AST 是前端契约；改它等于让所有前端工具一起动。
- **不做通用优化器**。MIR 保留可优化所需的形状（显式临时、显式块边界、纯运算标记），但本期
  不实现任何优化 pass。

---

## 3. 层次模型

```
源(.im) ──parser──> AST(Expr*/Stmt*)          ← 前端契约，不动
                       │
                       │ ① lowering（唯一共享）
                       ▼
                 MIR（IrModule/IrFunc/IrBlock/IrInsn）  ← 语义层：规则只在这里
                       │
        ┌──────────────┼──────────────┬───────────────┐
        │ ② 选择        │ ② 选择        │ ② 选择        │ ③ 只读
        ▼              ▼              ▼               ▼
   解释器 dispatch   AOT(→C 源)    wasm(→wasm)    反汇编器 / .im 写侧
```

四条规则：

1. **只允许②层的代码知道目标是 C 还是 wasm**；①层与语义无关。P1 里①层只做**重标号**（规范
   化留到 P2 之后，判据见第 10 节末尾），这样"逐字节等价"才是可达的判据。
2. **①层之外的任何地方不许出现"运算 + 类型 + 错误"的三元组**。要么查表，要么报错。
3. 反汇编器与 `.im` 写侧**只读表**，不读 MIR 结构——它们的对象是编码，不是语义。
4. 每个消费者在编译期声明它覆盖了哪些 opcode；**未覆盖必须在构建时红，不许运行期 fail**。

现有的 `emit_expr` / `cg_expr` 里那些 `fail(g, "… is outside the numeric subset")` 是**正确的
临时状态**（显式拒绝优于静默猜），但它们是运行期行为。P3/P4 之后，同样的信息要在构建期以
`IR-UNIMPLEMENTED|<op>|<backend>` 出现。

---

## 4. MIR 的数据结构

### 4.1 值的形状

三端共用的值形状已经存在：`Value` 32 字节（`_Static_assert` 已把这个宽度写进 ABI 契约），
`int type` + 匿名 union + `char *sval` + `void *ptr`；标签集是 `VAL_INT`/`VAL_FLOAT`/`VAL_STRING`/
`VAL_BOOL`/`VAL_OBJECT`/`VAL_NIL`/`VAL_ARRAY`/`VAL_DICT`/`VAL_SET`/`VAL_FUNCTION`。

MIR 不另造值表示，只加**类型提示**：

```c
typedef enum {
    IR_T_UNKNOWN = 0,   /* 动态：`var` 绑定、跨函数返回值、容器元素 */
    IR_T_INT, IR_T_FLOAT, IR_T_BOOL, IR_T_STRING,
    IR_T_NIL, IR_T_OBJECT, IR_T_FUNC, IR_T_ANY_CONTAINER
} IrType;
```

`IR_T_UNKNOWN` 与 `IR_T_ANY_CONTAINER` 是**两个不同的东西**：前者是"还没定/永远定不了"，后者是
"已知是容器但种类不定"。把它们合成一个，会让"静态模式下必须消掉 UNKNOWN"这条判据失效。

### 4.2 指令

```c
typedef struct {
    uint16_t op;        /* OpSemantics 表里的行号（== 编码编号） */
    uint8_t  flags;     /* IR_F_PURE | IR_F_MAY_THROW | IR_F_MAY_ALLOC | IR_F_TERMINATOR */
    int16_t  dst;       /* 目的寄存器，-1 = 无 */
    int16_t  a, b;      /* 两个源（寄存器号或常量池下标，由 op 的角色决定） */
    int32_t  imm;       /* 立即数/跳转目标/常量池下标 */
    IrType   type;      /* 结果类型提示 */
} IrInsn;
```

**不做 SSA**（见 §12 待裁定 1）。理由是现有 `RegInstruction` 已经是寄存器式，且 VM 的帧语义
（`sp`/`base`/`frame_count`）与 SSA 之间要额外一层 phi 处理，收益在本期无法证明。

### 4.3 块与函数

```c
typedef struct { int16_t start, end; int succ[2], nsucc; IrType stack_hint; } IrBlock;
typedef struct {
    int id, argc;
    IrInsn *insns; int ninsns, cap;
    IrBlock *blocks; int nblocks;
    int nregs;                 /* 帧内寄存器数 */
    IrType ret;
    uint32_t effects;          /* 函数级效果摘要：抛/分配/全局写 */
} IrFunc;

typedef struct {
    IrFunc *funcs; int nfuncs;
    Value *kint; double *kfloat; StringView *kstr;
    int nkint, nkfloat, nkstr;
    /* 源位置表：insn 下标 → 文件/行（供反汇编与错误信息，不参与语义） */
} IrModule;
```

`effects` 是给迁移判据用的：一个函数只要 `MAY_ALLOC`，就不能被 AOT 的子集直接内联；这类判据
今天靠读代码，P2 之后靠读一个位掩码。

### 4.4 与现有编码的关系

`RegInstruction`（`op, r1, r2, r3` 三个 int）**就是 MIR 的扁平编码**：`r1`=dst、`r2`=a、`r3`=b，
`imm` 落在需要它的 op 上。因此：

- MIR 不需要新的二进制格式；`INIMBC` 容器继续承载。
- `Bytecode` 结构里的 `string_pool`/`float_pool`/`funcs[]`/`try_entries` 就是 `IrModule` 的
  容器侧视图。**两者是同一个对象的两种访问方式**，不是两份数据。
- 这条关系必须写进代码注释，否则下一个读代码的人会以为有两套 IR。

---

## 5. 单一权威：`OpSemantics`

### 5.1 一行是什么

```python
# tools/ir_spec.py —— 唯一源（Python，因为要被生成器、测试、文档三边读）
OP_ADD = Op(
    name="ADD", code=0,                # code 是 ABI：只许追加
    operands=("num", "num"),           # 角色：num/str/any/reg/imm
    result="num",
    throws=(),                         # 可能抛出的异常名集合
    pure=True, may_alloc=False,
    interp="add",                      # 解释器侧实现标签
    aot="nv_add",                      # AOT 选择器 → 帮助函数名
    wasm=("W_I64_ADD", "W_F64_ADD"),   # wasm 侧按类型分派
    disasm="add {d}, {a}, {b}",        # 反汇编文本
    produced_by=("expr:binary:TOK_PLUS",),
)
```

### 5.2 生成什么

| 产物 | 用途 | 生成失败即构建失败 |
| --- | --- | --- |
| `src/ir/op_table.h/.c` | 编号、名字、效果位、操作数角色 | 编号重复/空洞/越界 |
| 解释器 dispatch 覆盖断言 | `switch` 里每个 op 都有 case | 缺 case ⇒ 编译期 `#error` |
| AOT 选择器表 | `aot=` 指向的帮助函数名 | 指向不存在的帮助函数 |
| wasm 选择器表 | `wasm=` 指令对 | 缺类型分支 |
| 反汇编格式表 | `disasm=` 模板 | 占位符数与操作数数不符 |
| `selfhost/op_codes.im` | `.im` 侧常量 | 与 C 侧编号不一致 |
| 测试用例骨架 | 每行至少一格 | 有行没格 ⇒ 红 |

### 5.3 读数的形状（每一期都要印）

```
IR-SPEC|rule=ir-rule-v1|sha=<12>|ops=46|produced=41|zero-production=5
IR-COVER|ops=46|interp=46|aot=38|wasm=41|disasm=29|selfhost=46|missing=aot:8,wasm:5,disasm:17
IR-PARITY|rule=ir-rule-v1|spec-sha=<12>|cases=214|disagree=0|backends=3
```

`missing=` 必须**逐项点名**，不许只印个数。理由与 `%` 那轮相同：一个数字不点名，读的人无法
判断它是"还没做"还是"不需要做"。

### 5.4 与 `mod_spec` 的继承关系

`tools/z3bounds` 与 `tools/mod_spec.py` 已经把方法论跑通：**规则做成可执行源 → 测试从它取期望
→ 突变它，测试必须红**。`ir_spec.py` 是同一方法在更大表上的应用；它应当复用 `mod_spec` 的读数
格式与自检习惯（`--self-check` 篡改一格必须红）。

---

## 6. 类型与宽度契约

### 6.1 B″ 的落点

已下的裁定是"`var` 动态、其余静态"（`docs/DECFY_DESIGN.md` §3.7）。在 MIR 里的落点：

- 函数参数与返回值、`let` 绑定、字面量：`IrType` 可定。
- `var` 绑定的槽位：`IR_T_UNKNOWN`，且**禁止**在①层做任何基于类型的特化。
- 容器元素：`IR_T_ANY_CONTAINER` 的成员一律 `IR_T_UNKNOWN`。

### 6.2 三端值映射（必须逐格写死，不许"按需"）

| MIR 类型 | 解释器 `Value` | AOT `NV` | wasm 槽 |
| --- | --- | --- | --- |
| INT | `VAL_INT` + `ival` | `NV_INT` + `i` | 槽 +0 标签、+8 i64 |
| FLOAT | `VAL_FLOAT` + `fval` | `NV_FLT` + `f` | 槽 +0 标签、+8 f64 位型 |
| BOOL | `VAL_BOOL` + `ival∈{0,1}` | `NV_BOO` + `i` | 槽 +0 标签、+8 0/1 |
| STRING | `VAL_STRING` + `sval` | 未覆盖（子集外） | `LOC_*` 指针槽 |
| NIL | `VAL_NIL` | `nv_nil()` | 0 标签 |

"未覆盖"必须写在表里（`aot=unsupported`），让它出现在 `IR-COVER` 的 `missing=` 里，而不是写
在散文里。

### 6.3 边界语义写在哪

NaN、±inf、超出 int64、除零、`%` 的符号——这些今天分散在三处实现里（`%` 那轮已统一到
`tools/mod_spec.py`）。它们属于**表的一个字段**（`boundary=`），指向 `mod_spec` 的规则名，而不是
在每个后端的注释里各写一遍。

具体做法：`OpSemantics` 行里加

```python
boundary="mod-rule-v1"   # 该 op 的边界行为由哪个 spec 定义
```

生成器检查：凡 `boundary=` 非空的行，其 spec 必须存在且 `spec_sha()` 能被印出来。

---

## 7. 五个消费者的契约

### 7.1 解释器（dispatch）

- 由表生成 `case`，或由表生成断言 `IR_ASSERT_DISPATCH_COVERED`。
- `switch (ins.op)` 的 `default` 分支**必须**是 `abort()` 而不是"忽略"——今天的 `default` 行为
  决定了一个未知 opcode 是崩溃还是静默，这条要写死。
- 覆盖读数：`IR-COVER|interp=46`。

### 7.2 AOT（`emit_expr` 的继任者）

- 输入从 `Expr*` 换成 `IrFunc`；选择器按 op 查表，命中 `aot=` 帮助函数名。
- 未覆盖 ⇒ **构建期**红（生成器检查表里 `aot=unsupported` 的行是否真的没有选择器）。
- C 语义泄漏必须显式处理：求值顺序（`&&`/`||` 的短路是语言语义，必须保）、未定义行为（越界
  移位、有符号溢出）、以及"帮助函数是否可能被调用两次"。这三条要在选择器层解决，不许散在
  `emit_expr` 的各个分支里。
- 目标（来自 §3.9 的裁定）：静态模式下 `OP_BE` 的生产点为 0。这条是判据，不是愿望。

### 7.3 wasm（`cg_expr` 的继任者）

- 槽位模型保留：`KIND_FRAME` / `KIND_CALLEE` / `KIND_GLOBAL`，临时槽从 `TEMP_BASE` 起、
  上限 `TEMP_MAX`。MIR 的 `dst/a/b` 要能被映射到（kind, slot）三元组。
- **临时槽压力是真实约束**：今天 `cg_expr` 用 `depth` 递增分配临时槽，深度超限直接失败。MIR 层
  不该知道这件事，所以选择器要印 `WASM-TEMPS|max_depth=|peak=|limit=`，让"表达式太深"从运行期
  失败变成可测的读数。
- 值布局按 §6.2 的槽表，**不许**在 wasm 侧另定一套偏移。

### 7.4 反汇编器

- 名字与格式来自表；覆盖率读数 `DISASM|named=N|total=M`。
- 今天只命名一部分 opcode（设计档 §3.1.6 的记录）。P5 之后必须 100%，且缺一格就红——反汇编器
  是"编码是否被理解"的唯一外部证据，它漏一个 opcode，意味着有一个编码没有被任何读取方解释过。

### 7.5 `.im` 写侧

- 写侧今天自己维护 opcode 编号。统一后它从生成的 `selfhost/op_codes.im` 取常量。
- 校验读数：`INIMBC-OPS|c-side=46|im-side=46|mismatch=0`。
- 这条是"去C"路线上最容易被忽略的一处：写侧已经能产出容器（`MIRROR|written=317|consumed=317|
  delta=0`），但它的**编号表是手写的**。手写的编号表与 C 侧漂移，产物仍然"看起来对"，直到某个
  opcode 错位。

---

## 8. 序列化与 ABI

1. 容器头不变：`INIMBC`，`INIM_BYTECODE_VERSION` 与 `INIM_ABI_VERSION` 各自递增的时机要写明
   （opcode 追加不改版本；opcode 语义变更必须改）。
2. **只许追加**。`OP_MOD` 追加在末尾是既有先例，写进文档作为规则而不是轶事。
3. 编号**不许复用**：删除一个 opcode 的编号永远空着。空号在表里以 `retired=True` 出现，让反汇编
   器能印 `op 29 (retired)` 而不是"未知"。
4. `.inim` 兼容矩阵：随仓库入库的预编译产物（如 `vtest` 下的 `.inim`）必须能在 HEAD 上执行；这条
   进 P1 的验收。
5. `--dump-ir`：给一个 `.inim` 或 `.im`，印 MIR 的可读形式（含 `spec-sha=`）。这是设计阶段的
   验收工具，也是将来差分测试的输入。

---

## 9. 验证装置

### 9.1 三端差分（主判据）

- 用例来源：`ir_spec` 的 `matrix()`（每行至少一格 + 边界格由 `boundary=` 生成），加上现有
  `vtest/` 语料。
- 形状：`IR-PARITY|rule=|spec-sha=|cases=|disagree=|per-case=` 失败时**逐例点名**。
- 判据：`disagree=0`；且用例数必须 ≥ 表行数（`cases >= ops`，否则有 op 没被测到）。

### 9.2 覆盖率

`IR-COVER` 五行各自对得上：`ops = interp = selfhost`，`aot/wasm/disasm` 允许缺，但 `missing=`
必须与表里 `unsupported` 标记**逐一对应**。不对应即红。

### 9.3 突变

- 改表一格（例如把 `ADD` 的 `aot=` 指错）⇒ 必须红。
- 改一个后端的选择器（让它读旧 AST 路径）⇒ 差分必须红。
- **反证**：把差分改成"只比 exit code"⇒ 必须变绿（证明它真的在比值，不是比信号）。

### 9.4 负控与反证的口径（沿用本仓库已确立的三条）

1. 一个从不失败的负控不是负控。
2. 反证（证明检查器不是装饰）必须有一条"故意关掉它，必须变绿"的格。
3. 每条读数带 `tree=`；档案与正文分离（`--record-readings` / `--check-readings` 的既有机制）。

---

## 10. 迁移分期

| 期 | 动作 | 可证伪判据 | 回退点 |
| --- | --- | --- | --- |
| P0 | 建 `ir_spec.py` 与原样表：67 个 `OP_*` 与解释器 dispatch 逐一对应，空号标 retired（**已完成**，`426ceef`） | `IR-SPEC|ops=67`+`IR-COVER|interp=67/67|disasm=67/67`；每个形状由处理器体证实（`IR-SHAPE|forgets=none|unconfirmed=none`） | 无行为改动，回退=删表 |
| P1 | 抽出 MIR 构建器，**只做重标号**：编译器主体改为 AST→MIR→编码，MIR 层不折叠、不重排、不改号 | 字节码**逐字节等价**：`ORIG-IDENTICAL|files=|bytes=|differ=`，且预编译 `.inim` 仍可执行 | 保留旧路径，开关 `INIM_IR_BUILD=0` |
| P2 | 解释器改表驱动 dispatch | `IR-COVER|interp=46|missing=`；差分绿；`default` 是 abort | 开关回旧 switch |
| P3 | AOT 改吃 MIR | `IR-COVER|aot=` 的 `missing` 与表一致；`fail(g, …)` 站点数从 17 降到 0；差分绿 | 开关回 `emit_expr` |
| P4 | wasm 改吃 MIR | `WASM-TEMPS` 读数存在且 `disagree=0`；槽表与 §6.2 一致 | 开关回 `cg_expr` |
| P5 | 反汇编器表驱动 | `IR-DISASM|cases=67/67|names-checked=67/67` | 表回退 |
| P6 | `.im` 写侧表驱动 | `INIMBC-OPS|mismatch=0`；`MIRROR` 仍 `delta=0` | 手写常量表回退 |

**形状今天只能证伪，不能证明完整（2026-10-28 记）。** 表的形状是散文，能与处理器体对照的只有
"提到了哪些 `ins.rN`"，所以有三件事它现在看不见，读数里如实印出而不假装：①**寄存器与立即数的
区别**——`DECLARE` 用 `ins.r1` 当 kind、`ins.r2` 当池下标，集合相等就绿；②**次数与角色**——处理器
读 r1 两次、形状写一次，集合相等；③**反汇编器印的操作数顺序**——名字对、印的寄存器顺序错的 case，
前三类检查全绿（`IR-DISASM-ORDER|enforced=no`，本日实测 `order-diff` 21 个 opcode、`invented` 3 个、
`hidden-dst` 1 个，这一批是"未测量"而不是"已通过"）。把这三件变成可判的，要求形状从"散文"改成
**角色三元组**（`dst=` / `src=` / `imm=`），那是 P1 的入口工作，不是 P0 的遗留缺陷。

**P1 的判据为什么是"重标号"（2026-10-28 定）。** 设计里第 3 节的层①写着"只做规范化"，而规范化
必然改字节（折叠常量、重排块、改号），于是"逐字节等价"与"层①做规范化"是互相否定的两条要求。
本仓选前者：**P1 的 MIR 层只允许重标号**——同一顺序、同一 opcode、同一编号，只把 `RegInstruction`
换成带类型与角色的 `IrInsn`，再从 `IrInsn` 编回去。这样"逐字节等价"是可达的最强判据，且一旦有人
在 MIR 层偷偷加了优化，它立刻红。真正的规范化是 P2 之后的事，那时判据换成差分（同语料
`(rc, 末行 stdout)` 相同），而不是字节。

**P1 比什么、比多少（同一定）。** ①**码流**：`bytecode` 子命令的 dump（`main:`/`func:`/`thread:`
三段）逐行比——它是纯数字、可逐字比对；②**容器**：已入库的 `.inim`（9 个）用 `cmp` 整文件比，
因为池、全局名表、`try_entries`、编号表都不在 dump 里；容器的比较只在同一棵树、同一 flags、
同一绝对路径下有效（`.inim` 尾部带主源与每个 import 的 SHA-256）。③**语料**：全量不抽样——399
个 `*.im` 全部过一遍，`files=` 必须等于语料数；没跑到要记为 `unmeasured` 并点名，不是 `same`。
④**负控**：交换两条独立指令必须 `differ>0` 且点名；把判据改成"只比退出码"必须变绿。

每期的纪律：

- 门禁 16 段全绿、174 个测试全过（数字随提交变，由合并者 ±1 修正）。
- 改动被 pin 住的文件时**行数中性**（`%` 那轮已付出代价并归档）：证据不该因为修复而搬家。
- 每一期单独一次提交，提交信息里带该期的读数行。

### 10.1 为什么 P0 先做表而不是先改代码

表是唯一可以在"零行为改动"的前提下把五个消费者同时接上的东西：它先制造**红**（覆盖缺口、
编号不一致），再让后面每一期去消灭自己那部分红。反过来先改代码，则每一期都在没有共同语言的
情况下重构，且没有一个判据能说"你还差多少"。

---

## 11. 度量与爆炸半径

### 11.1 每期要印的数

```
IR-SIZE|funcs=|insns=|ops_used=|ops_total=
IR-EFFECTS|may_throw=|may_alloc=|pure=
```

`ops_used` 与 `ops_total` 的差就是**爆炸半径的小量化**：先知道有多少 opcode 在真实语料里出现，
再决定 P3/P4 要先覆盖哪一批。今天是不知道的（设计档 §3.9 已指出这一点）。

### 11.2 与既有工具的关系

`tools/z3bounds/extract_stack_effects.py` 已经在读"每个 opcode 在真实语料里的出现频次与栈效果"，
它的读数形状（`SWITCHES|dispatch=…|disasm=…|ops_total=…|labels=…`）可以直接作为 `IR-SIZE` 的
输入，不必另造一个探针。

---

## 12. 待裁定问题（六条里第 6 条已定，其余五条本文档不替人决定）

1. **MIR 是否 SSA**。本文档按"非 SSA"写（§4.2）。改成 SSA 会让类型推断更容易、让优化更自然，
   但要在 VM 的帧语义上引入 phi，且 `RegInstruction` 不再能直接承载编码。
2. **字符串与容器的引用计数归谁管**。今天 `Value` 里 `sval`/`ptr` 的生命周期规则散在 VM 与 wasm
   的 `borrow` 逻辑里（wasm 侧有"临时槽只借用"的注释）。这属于值表示契约，必须在 P2 之前定。
3. **异常与线程 opcode 是否进 v1 表**。它们的效果集（可能阻塞、可能跨帧）与纯运算不同。本文档
   建议：进表，但 `flags` 里标 `IR_F_TERMINATOR`/`IR_F_MAY_THROW`，且 P1–P4 不覆盖它们。
4. **`.im` 写侧是否直接吃 MIR**（而不是只吃编号表）。直接吃 MIR 会把写侧绑到 MIR 的字节布局上，
   收益是写侧立刻获得语义；代价是 MIR 一旦演进，写侧要跟着改。
5. **是否允许"静态模式专用 opcode"**（如 `OP_BE`）进表并成为长期成员。§3.9 的目标是它在静态
   模式下生产点为 0；如果它长期存在，表里要有一个 `static_only=True` 的字段，而不是靠注释。
6. **`OpSemantics` 的源用 Python 还是 C**。**已定：Python**（本节标题的"不替人决定"指其余五条）。
   本文档选 Python（与 `mod_spec` 一致、生成器容易写），
   代价是构建期多一个 Python 依赖；如果 CI 环境不允许，退化为 C 源 + 一个 Python 校验器。

---

## 13. 附录：`%` 的一次完整穿越（今天已发生的，作为形状示例）

同一行表项在五个位置的落点：

1. 表：`OP_MOD`，`operands=("num","num")`，`result="num"`，`throws=("division_by_zero",)`，
   `boundary="mod-rule-v1"`，`aot="nv_mod"`，`wasm=("W_I64_REM_S",…)`，`disasm="mod {d}, {a}, {b}"`。
2. 解释器：`L_MOD` 分支（今天是手写 switch；P2 后由表生成断言覆盖）。
3. AOT：`nv_mod` 帮助函数（今天在 `kPreamble` 里手写；P3 后由选择器表引用）。
4. wasm：三处 `W_I64_REM_S`（今天按站点各写；P4 后由选择器表统一）。
5. 测试：`tools/mod_spec.py` 的 `matrix()` → `IR-PARITY` 的 28 例，`disagree=0`。

而 `boundary="mod-rule-v1"` 的实现 `tools/mod_spec.py` 里的 `trunc_sat()` 是**唯一**处理
NaN/越界的地方——三端各自那只截断函数（`im_dbl_to_i64`、`nv_asi`、`e_trunc_sat_i64`）在 P3/P4
之后都应当调用同一个规则，或直接消失。

这一格说明设计的目的：**不是让代码变少，而是让"同一件事写在几个地方"变成一个可测的数**。
`IR-COVER` 的 `missing=`、`IR-PARITY` 的 `disagree=`、`INIMBC-OPS` 的 `mismatch=` 三个读数，
分别是三个方向的同一个问题。

---

## 14. 证据诚实度

- 本文档的"今天"事实（46 个 `OP_*`、`Value` 32 字节、`TEMP_BASE`/`TEMP_MAX`、17 处 `fail(g, …)`、
  174 个注册测试、69 行设计档表、`mod_spec` 的 `disagree=0`）都是**本轮实测**，命令与读数在
  会话记录里；未在本轮复跑的历史结论（如 §3.9 的 `OP_BE` 目标）标注为引用设计档。
- 本文档**没有实现任何一行**。所有"P0–P6 会怎样"都是设计，不是结果。
- 本文档的路径引用**不带行号**：行号是会漂的证据，本仓库的指针账已无余量（`pointer delta +223`
  `ceiling 223`），新增引用一律用路径 + 符号名。
