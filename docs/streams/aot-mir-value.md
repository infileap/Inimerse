# 作业单：`aot-mir-value` —— 把 `aot_mir` 的值表示从 `NV` 换成 `Value`

**状态**：草案（等协调层过目，未写代码）
**流**：`stream/aot-mir-value`，base `main @ 37aa24e`
**母题**：`docs/DECFY_DESIGN.md` §3.5 裁定 **A′** 后果 1「不许造第三套值表示」、§3.7 裁定 **B″**（`var` 动态、其余静态）

---

## 1. 现场（全部在本树复现，命令与输出逐字）

```
python3 tools/ir_spec.py   -> ir spec: ok (67 opcodes, interp 67/67, disasm 67/67, aot 29/67)
./build/aot-mir subset     -> IR-MIR-SUBSET|emits=30
./build/aot-mir translate  -> IR-AOT-MIR-LOWERING|handles=30|of=67|unknown=37
                             IR-AOT-MIR-VERDICT|PASS|accepted=86|covered=29|missing=0|same-as-interpreter=86
```

三个三行程序，把「卡在哪」钉死：

| 程序 | 结果 |
|---|---|
| `a=[10,20,30]; say a[1]` | `illegal-operand: container=0 leaves the argument stack unbalanced: asp=3` |
| `d={"k":1}; say d["k"]` | `illegal-operand: container=0 leaves the argument stack unbalanced: asp=2` |
| `s="hi"; say s` | `unsupported-opcode` + `IR-MIR-NOT-YET|op=OP_LOADK_STRING|n=1` |

**根因不是我推的，是代码自己写的两句话**（`@ 37aa24e`）：

- `aot_mir.c` 文件头注释：「Registers become one flat `NV v[N]` array per container」
- `aot_mir.c` 的 `kPresetGlobals` 上方那句：「**NV carries int/float/bool only**」（解释为什么 23 个集合类预设全局要被按名拒绝）

⇒ `Value`（32 字节，`src/vm/vm.h`：`int type` + 匿名 union `{long long ival; double fval;}` + `char *sval` + `void *ptr`，`VAL_INT/FLOAT/STRING/BOOL/OBJECT/NIL/ARRAY/DICT/SET/FUNCTION` 十个 tag）**与 `NV`（三个 tag）不是宽窄之别，是缺项之别**：`NV` 没有字符串、没有 `nil`、没有容器指针。

## 2. 37 个 unknown 逐个归因（本作业单的核心判断）

按「卡在值表示」还是「卡在别处」分类。**只有第一类靠这一步通开**：

### A 类：卡在值表示，`Value` 一通就开（8 个）

`OP_LOADK_STRING`、`OP_CONCAT`、`OP_NEW_ARRAY`、`OP_NEW_DICT`、`OP_NEW_SET`、`OP_INDEX_GET`、`OP_INDEX_SET`、`OP_IS_NIL`

### B 类：卡在值表示**加**别的东西（12 个，需 A 类先通）

`OP_IN`、`OP_MIN`/`OP_MAX`（集合语义）、`OP_SET_ADD`、`OP_SET_INTERVAL`、`OP_EQK`/`OP_NEQK`（常量池里的字符串才需要）、`OP_CALL_BUILTIN`（多数内建吃字符串/容器）、`OP_RECORD`、`OP_BIND`、`OP_DECLARE`、`OP_LOAD_CAPTURE`、`OP_MAKE_FUNC`/`OP_CALL_VALUE`（闭包要 `Value` 持函数对象）

### C 类：**与值表示无关**，换了也卡着（14 个）

- 线程/锁（7）：`OP_THREAD_START/GOTO/JOIN/CTRL/STATE/WAIT`、`OP_LOCK` —— `build_nodes()` 里 `if (bc->thread_count > 0) return -2;` 逐字拒绝，是**模型缺失**不是值缺失
- 消息/调度（4）：`OP_SEND`/`OP_RECV`/`OP_WAIT`/`OP_YIELD` —— 需要调度器，AOT 产物是单线程 C
- 异常（3）：`OP_THROW`/`OP_TRY_START`/`OP_TRY_END` —— 需要 setjmp/状态机，与值无关
- 终止/栈（2）：`OP_STOP`、`OP_POP_REG`

⇒ **这一步的判据只能是 A 类，且第一步只做 `OP_LOADK_STRING`。** 把「37 个逐个 lowering」当成「37 个同类活」会把 C 类 14 个也算进值表示。

## 3. 这一步要做的（只做 `OP_LOADK_STRING`）

**判据**

1. `IR-AOT-MIR-LOWERING|handles=` **30 → 31**、`unknown=` **37 → 36**。
2. `ir_spec` 的 `aot N/67` 上涨并报值（它是「解释器实际执行过的 opcode 数」，与 lowering 数不是同一个量）。
3. `t_str.im`（`s="hi"; say s`）经 `aot-mir translate` **编出 C、能编译、能跑、输出 `hi`**，与 `./build/inimerse t_str.im` 逐字相同。
4. `IR-AOT-MIR-VERDICT` 的 `same-as-interpreter=` **不许掉**（今天 86）。
5. 全量门禁（禁 `--fast`、禁搬 `.verify`）。

**反向判据**：在 lowering 里把字符串值的构造故意写坏（`sval` 置 NULL，或 `type` 置成 `VAL_INT`），第 3 条必须红 —— **要贴真实读数**。

## 4. 必须由人类裁的**一个设计选择**

`NV` 与 `nv_*` 这份运行时**不是 `aot_mir` 私有的**：它来自 `aot_native.c` 的 `aot_native_preamble()`，注释逐字：

> `/* The preamble, as text, for the second consumer.  Returning kPreamble rather than a copy is the whole point: one value runtime, embedded twice. */`

`aot_mir.c` 的 `#include "aot_native.h"  /* one value runtime, not two */` 与生成时调它，就是这条。

### 甲：只给 `aot_mir` 造一份 `Value` 运行时（我不建议）

在 `aot_mir.c` 里另写一段 `Value` preamble，`aot_native.c` 一字不动。

- 代价：**这正是 A′ 后果 1 禁止的「第三套值表示」**（`NV` + 新 `Value` + VM 的 `Value`），且那句「one value runtime, not two」会变成假话。
- 好处：旧 AST 路线零风险。

### 乙：扩共享 preamble，让两条路线共用（代价最大）

把 `kPreamble` 的 `NV` 扩成 `Value` 形状（加 `char *sval` / `void *ptr` / 十个 tag），`nv_*` 保留名字只改内部。

- 好处：真正兑现「one value runtime, embedded twice」；两条路线同时获得字符串/`nil`。
- 代价与风险（**必须人类看过再点头**）：
  1. `aot_native` 的用例会立刻看到行为变化 —— 它的 `nv_say` 遇到字符串要会打印，否则原本「拒绝」的路径会变成「打印错东西」。**这是最大风险点**：`aot_native` 的历史交付里有「字符串仍是编译期显式拒绝」这条边界，扩 preamble 就**必须先确认它仍在拒绝**。
  2. `sizeof(Value)` 32 vs `NV` 24 ⇒ 寄存器数组变大 33%，对 AOT 的 34–36× 性能主张有可测影响，要给读数。
  3. `src/vm/vm.h` 的 `Value` **不能直接 `#include` 进生成代码**（带引擎内部枚举与指针语义）。所以「用 `Value`」实际是「写一份**结构兼容**的值类型」，**仍是一份拷贝**，靠注释与测试绑住两边。必须写成诚实边界。

### 丙（我实际最倾向，但需明确批准）：`aot_mir` 侧独立命名 `MirVal`

在 `aot_mir.c` 里放新 preamble，命名 `MirVal`（**不叫 `NV`、也不假装共用**），并在 `#include` 那句注释上就地补「preamble 共用已在此结束，理由与删除条件见作业单 §4」，把「one value runtime, not two」**改成一个有日期的欠账**，而不是让它变成假话。

- 好处：本步风险最小、可独立验收；`aot_native` 零改动。
- 代价：**暂时有两份，而且是有意的第二份**。删掉它的条件写死：等 `aot_native` 也改吃 IR 的那一步（§3.5 后果 2 要求「同一批」），两份合一。
- **但它需要一个明确批准**，因为 A′ 后果 1 的字面意思就是禁止这么干。我不自己给自己批。

## 5. 字符串常量的落法

- **甲（建议）**：字符串常量落成**静态 `const char[]` + `ival=1` 打驻留标记**，`sval` 指向静态存储。依据是 VM 已经在用这个约定：`src/vm/vm.c:227` 的注释逐字写着字符串驻留（`VAL_STRING` 且 `ival==1` 表示驻留、释放前先判 `ival`）[obs: 37aa24e]，`:1008`/`:1165` 的 `free(out.sval); out.sval=(char*)np; out.ival=1;` 就是驻留化的写法。
  - 代价：`ival` 被复用成「驻留标志」，与它当整数载荷的用法**同字段两义** —— 这是 VM 里已有的债，我沿用并在注释里写明。
  - 好处：**零运行时分配**，与 VM 的 `say`/`concat` 天然一致。
- **乙**：运行时 `strdup` 一次，`ival=0` 表示拥有。代价：每个常量一次分配、要管释放；且**与 VM 的驻留行为不同**，会给「逐字相同」判据埋长跑雷。
- **丙**：落成 `#define` 或内联字面量。代价：`say s` 走 `v[1]` 时拿不到，等于没做。

## 6. 诚实边界（落地时写进提交信息与 STATUS）

1. **字符串所有权语义是沿用不是新建**：`ival==1` = 驻留不释放，来自 `src/vm/vm.c:227` 的既有约定 [obs: 37aa24e]；本步不引入引用计数，也不解决「谁释放非驻留字符串」—— **AOT 产物里今天没有任何释放路径**，所以本步只做驻留常量，非驻留字符串要等 `OP_CONCAT`（那一步才产生新字符串，必须给释放策略）。
2. **`OP_LOADK_STRING` 通了不等于字符串语义通了**：`say` 打印、`==` 比较、`concat` 拼接是三件不同的事，本步只交付第一件。
3. **C 类 14 个 opcode 与值表示无关**（线程/调度/异常），本步不碰。
4. **生成代码里那份值类型是结构兼容的拷贝，不是 `src/vm/vm.h` 那一份**；两边靠测试绑住（`same-as-interpreter`），不靠 `#include`。
5. **Windows 半侧**：本步只动 lowering 与 preamble 文本，`aot_mir` 产物在 Windows 上未运行（沿用既有边界）。

## 7. 每题都要报的读数（后续每一步照此）

①`ir_spec` 的 `aot N/67`；②`IR-AOT-MIR-LOWERING|handles=/unknown=`；③`IR-AOT-MIR-VERDICT|same-as-interpreter=` 不许掉；④全量门禁（禁 `--fast`、禁搬 `.verify`）；⑤计数若动，`tools/gate.sh` / `CMakeLists.txt` / `docs/BOARD.md` 同笔。
