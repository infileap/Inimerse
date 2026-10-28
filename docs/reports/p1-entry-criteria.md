# P1 入口条件：三条可执行判据（tree `cff9197`，分支 `p1-entry-noble-quartz`）

ivory 在 m04411 §6 要的是：把 §10 的 P1 入口条件写成三条**可执行**判据，每条一个最小正例、一个最小反例，反例必须**今天就在树上变红**，且不动 main。

这份报告给三条判据的实现、49 条手写角色表（已归 7 族）的出处、十三组实测，以及七条结构性结果。其中一条是**我自己的角色表先错了一次**，被同一条判据按 handler 抓回来；另一条说明 ③ 在检测能力上并不独立于 ①；第五条是**同一个约定修了一处、漏了一处**；第六条是**控制组跑出全绿，把 §5.4 的预期证伪了**——证伪落在一句具体的措辞上，不在结论上；第七条是**判据只朝一个方向看**：它抓「多印了一个寄存器」，不抓「少印了一个操作数」，而今天树上就有两条这样的指令（`EQK`/`NEQK`）。

- 判据实现：`tools/p1_roles.py`（纯标准库，`--vm <path>`，红则 rc=1，不 import `ir_spec`，不与任何自检共用提取代码）
- 依据树：`cff9197`（在 `9dd3ade` 之后 main 又落了 opcode census，追加在 `vm.c` 末尾，`vm.c:5584` 以上一行未动，本族引用的行号因此未移；本分支已 rebase 到它，读数在 rebase 前后一致，见 §3.7 与 §4 M/N 组）。旧树的读数不删：`9a4db3b` 与 `a670a70` 的对照留在 §5.2，历史树 `0d6b6b7` 用来证明三条判据能把当年那三个真实缺陷重新抓出来
- 只读：没有改 `src/`，所有被改坏的 `vm.c` 都是 `.probe/` 下的拷贝，不进提交

## 1 判据的对象：一条打印行，不是一段 case 文本

被测物是反汇编器**打印出来的那一行**，所以判据只读 case 里的 `snprintf` 调用，不读整个 case 体：`case OP_CALL_BUILTIN` 在打印之前先用 `ins->r2` 算函数名（`vm.c:5355-5358`），那是计算不是行；把整段 `case` 扫一遍的半尺子，正是 `0d6b6b7` 那版 ORDER 行把 `CALL_BUILTIN` 判成「顺序不同」的原因。

打印行里的每个占位符按**字面前缀**分词，这是判据能成立的前提：

| 字形 | 命名空间 | 例 |
| --- | --- | --- |
| `r%d`，实参是 `ins->rN` | 寄存器 | `MOV r%d, r%d` |
| `t%d` / `f%d` / `g%d` | 带类型的索引（线程 / 函数 / 全局） | `THREAD_JOIN t%d` |
| 裸 `%d`（无字面前缀） | 立即数 / 表下标 | `RECORD g%d, sidx=%d` |

读数里 `printed-number=r3` 与 `printed-typed=f2` 是**非寄存器**命名空间：前者说「槽 r3 以裸数字字形印出」，后者说「函数 2」。它们与 `printed-reg` 是两个集合，混读会把立即数当成寄存器——`NEW_ARRAY` 的 `n=%d` 就是这一类。

**一个立即数被写成 `r%d`，它在理解上、也在机器上，就已经是「寄存器 r2」了**——这正是 ③ 要挡的事：`9a4db3b` 上的 `TRY_START` 就是这么红的（§5.2）。红随树走：`a670a70` 上 `TRY_START` 变绿、只剩 `THREAD_START` 藏 dst 那一处，`9dd3ade` 上两处都修好了，**今天树上的 verdict 是 green，而这个「无红」只覆盖已声明的 44 条**（§4 H、K 行与 §6）。

## 2 三条判据

设 `REG` = 该行打印出的寄存器集合，`dst/src/obj`、`imm` 取自下表声明：

1. **① 闭世界**：`REG ⊆ dst ∪ src ∪ obj`。违反了就是「这行印了一个角色表里没有的寄存器」。
2. **② 目的不外露**：`dst ⊆ REG`。违反了就是「这行把真正被写的寄存器藏了」。
3. **③ 命名纪律**：`imm ∩ REG = ∅`。违反了就是「立即数/下标被当成寄存器印出来了」。

三条都只测**打印行**，不测语义。

## 3 角色表：49 条，逐条指到 handler 行

其中 43 条已归族（族 1 算术 17、族 2a 线程 10、族 2b 资源 2、族 3 store 1、族 4 output 1、族 5 构造/区间 5、族 6 K 形式 7），6 条是早期逐条声明、还没归族的：`MOV INDEX_SET TRY_START CALL_FUNC CALL_BUILTIN MAKE_FUNC`。`LOADK_INT` 与 `LOADK_I64` 原来也挂在这一组里，本批被族 6 收进去（§3.7）——**先声明、后归族是允许的，但两处都要写清，否则「族数」与「批次数」对不上就会被读成漏算**。剩下 6 条的形状各自都不符合任何已写的族规则，硬归族会正好制造出「族内形状不一致」，所以先挂着，下一批按「调用族 / 容器族」的边界理由再动。

角色不按位置声明，因为位置在不同 opcode 上含义不同：`OP_CALL_FUNC` 的 dst 是 `r2`（`vm.c:4062`），`OP_RECORD` 的 dst 是 `r3`（`vm.c:4243`），`OP_THREAD_STATE` 的 `r2/r3` 是线程 id 与属性下标（`vm.c:4693`）。

| opcode | 角色 | dispatch | handler | 读数出处 |
| --- | --- | --- | --- | --- |
| `OP_MOV` | dst=r1 src=r2 | 3162 | 3236 | `vm.c:3237` `value_assign(&R[ins.r1], &R[ins.r2])` |
| `OP_LOADK_INT` | dst=r1 imm=r2 | 3163 | 3239 | `vm.c:3240` |
| `OP_LOADK_FLOAT` | dst=r1 imm=r2 | 3165 | 3252 | `vm.c:3253` `t->code->float_pool[ins.r2]`；同一行写 `R[ins.r1]`；打印 `vm.c:5325` |
| `OP_LOADK_STRING` | dst=r1 imm=r2 | 3166 | 3255 | `vm.c:3256` `int sidx = ins.r2;`；`vm.c:3268` 写 `R[ins.r1]`；打印 `vm.c:5326-5328` |
| `OP_LOADK_BOOL` | dst=r1 imm=r2 | 3167 | 3271 | `vm.c:3272` `ins.r2 ? 1 : 0`；打印 `vm.c:5330` |
| `OP_EQK` | dst=r1 src=r2 imm=r3 | 3213 | 4159 | `vm.c:4160` `int sidx = ins.r3;`；`vm.c:4171` `Value opv = R[ins.r2];`；`vm.c:4172` 写 `R[ins.r1]`；打印 `vm.c:5370` **只印 r1** |
| `OP_NEQK` | dst=r1 src=r2 imm=r3 | 3214 | 4175 | 同构：`vm.c:4187` 读 `R[ins.r2]`，`vm.c:4188` 写 `R[ins.r1]`；打印 `vm.c:5371` **只印 r1** |
| `OP_LOADK_I64` | dst=r1 imm=(r2,r3) | 3164 | 3242 | `vm.c:3247-3250` |
| `OP_ADD` | dst=r1 src=r2,r3 | 3168 | 3276 | `vm.c:3277` |
| `OP_MOD` | dst=r1 src=r2,r3 | 3217 | 4252 | `vm.c:4256-4267` |
| `OP_AND` | dst=r1 src=r2,r3 | 3180 | 3598 | `vm.c:3599-3602` |
| `OP_MIN` | dst=r1 src=r2（reads_dst=no） | 3221 | 4357 | `vm.c:4358-4359`；`set_minmax` `vm.c:2477` 清 dst |
| `OP_SUB` | dst=r1 src=r2,r3 | 3170 | 3444-3468 | `vm.c:3445` `a=&R[ins.r2], b=&R[ins.r3]`；`vm.c:3462` 写 `R[ins.r1]` |
| `OP_MUL` | dst=r1 src=r2,r3 | 3171 | 3469-3493 | `vm.c:3470`；`vm.c:3487` 写 `R[ins.r1]` |
| `OP_DIV` | dst=r1 src=r2,r3 | 3172 | 3494-3520 | `vm.c:3495`；`vm.c:3513` 写 `R[ins.r1]` |
| `OP_NEG` | dst=r1 src=r2 | 3173 | 3521-3538 | `vm.c:3522` `v=&R[ins.r2]`；`vm.c:3530` 写 `R[ins.r1]` |
| `OP_MAX` | dst=r1 src=r2 | 3222 | 4362-4366 | `vm.c:4363-4364` `set_minmax(vm, &src, &R[ins.r1], 1)` |
| `OP_EQ` | dst=r1 src=r2,r3 | 3174 | 3539-3547 | `vm.c:3542-3545` |
| `OP_NEQ` | dst=r1 src=r2,r3 | 3175 | 3548-3555 | `vm.c:3550-3553` |
| `OP_LT` | dst=r1 src=r2,r3 | 3176 | 3556-3565 | `vm.c:3557,3562-3563` |
| `OP_GT` | dst=r1 src=r2,r3 | 3177 | 3566-3575 | `vm.c:3567,3572-3573` |
| `OP_LE` | dst=r1 src=r2,r3 | 3178 | 3576-3585 | `vm.c:3577,3582-3583` |
| `OP_GE` | dst=r1 src=r2,r3 | 3179 | 3586-3597 | `vm.c:3587,3592-3593` |
| `OP_NOT` | dst=r1 src=r2 | 3182 | 3612-3619 | `vm.c:3613-3615` |
| `OP_IS_NIL` | dst=r1 src=r2 | 3203 | 4155-4158 | `vm.c:4156` |
| `OP_INDEX_SET` | obj=r1 mutate key=r2 val=r3，无 dst | 3185 | 3717 | `vm.c:3718-3720` |
| `OP_SAY` | src=r1 | 3193 | 3921 | `vm.c:3923` |
| `OP_THREAD_STATE` | dst=r1 imm=r2,r3 | 3208 | 4692 | `vm.c:4693` |
| `OP_THREAD_JOIN` | imm=r1 src=r2 | 3206 | 4646 | `vm.c:4654` `val_as_double(vm, &R[ins.r2])` |
| `OP_THREAD_WAIT` | imm=r1 src=r2 | 3207 | 4672 | `vm.c:4674` `int tidx = ins.r1;`，`vm.c:4678` `sec` 取自 `R[ins.r2]`；发出处 `compiler.c:2129-2130` `sec_reg = compile_expr(...)` → `emit(..., OP_THREAD_WAIT, tidx, sec_reg, 0)` |
| `OP_TRY_START` | imm=r1,r2（r1 从不被读） | 3224 | 4420 | `vm.c:4440` `f->var_idx = ins.r2;`；发出处 `compiler.c:2292` `emit(..., OP_TRY_START, 0, varIdx, 0)` |
| `OP_RECORD` | dst=r3 imm=r1,r2 | 3216 | 4203 | `vm.c:4243` `value_move(&R[ins.r3])`；发出处 `compiler.c:1515`、`compiler.c:2363` |
| `OP_CALL_FUNC` | dst=r2 imm=r1(fidx),r3(argc) | 3198 | — | `vm.c:4062` `res = ins.r2, argc = ins.r3` |
| `OP_CALL_BUILTIN` | dst=r1 imm=r2,r3 | 3191 | 3858 | `vm.c:5355-5358` 只用 r2 取函数名 |
| `OP_MAKE_FUNC` | dst=r1 imm=r2,r3 | 3199 | — | `vm.c:4043` 捕获循环先读 r3 再读 r2 |
| `OP_THREAD_START` | dst=r2 imm=r1,r3 | 3204 | 4459-4546 | `vm.c:4460` `int tidx = ins.r1, res = ins.r2, argc = ins.r3;`；`vm.c:4500,4508,4544` `value_set(&R[res], …)`；发出处 `compiler.c:1992`、`compiler.c:2095` |
| `OP_THREAD_CTRL` | imm=r1,r2 | 3205 | 4547-4611 | `vm.c:4548` `int tidx = ins.r1, op = ins.r2;` |
| `OP_THREAD_GOTO` | imm=r1,r2 | 3228 | 4612-4645 | `vm.c:4613` `int tidx = ins.r1, off = ins.r2;` |
| `OP_SEND` | imm=r1 src=r2 | 3210 | 4723-4760 | `vm.c:4724` `int tidx = ins.r1;`；`vm.c:4753` `value_copy(&v, &R[ins.r2])` |
| `OP_RECV` | dst=r1 src=r2（r2 可为 −1 哨兵） | 3211 | 4761-4792 | `vm.c:4762` `int res = ins.r1;`；`vm.c:4764` `if (ins.r2 >= 0) … &R[ins.r2]`；`vm.c:4792` `value_move(&R[res], &out)`；发出处 `compiler.c:2197`（无超时时 `timeout_reg = -1`） |
| `OP_WAIT` | src=r1 | 3194 | 3933-4027 | `vm.c:3934` `Value *v = &R[ins.r1];`；发出处 `compiler.c:2156` |
| `OP_YIELD` | 无操作数 | 3197 | 4028-4031 | `vm.c:4028` `L_YIELD: {` 一个寄存器都不读；打印 `vm.c:5401` `"YIELD"`——零操作数形状的正例 |
| `OP_LOCK` | imm=r1,r2 | 3209 | 4708-4722 | `vm.c:4709` `int midx = ins.r1;`；`vm.c:4719` `if (ins.r2 == 0) …`；发出处 `compiler.c:2170-2179` |
| `OP_DECLARE` | imm=r1,r2 | 3215 | 4192-4201 | `vm.c:4193` `int kind = ins.r1;`；`vm.c:4194` `float_pool[ins.r2]`（`r2` 是 float_pool 下标）；打印 `vm.c:5385` `DECLARE kind=%d, fp[%d]`，**一行里没有任何寄存器字形** |
| `OP_NEW_ARRAY` | dst=r1 imm=r3 | 3183 | 3620-3646 | `vm.c:3634` `int n = ins.r3;`；元素来自**值栈**（`vm.c:3638` `vm_array_push_n(vm, aidx, &t->stack[base], n)`）；`vm.c:3645` 写 `R[ins.r1]`；`ins.r2` 从不被读 |
| `OP_NEW_DICT` | dst=r1 imm=r3 | 3212 | 3648-3672 | `vm.c:3662` `int n = ins.r3;`；`2*n` 个值来自值栈（`vm.c:3666`）；`vm.c:3672` 写 `R[ins.r1]`；`ins.r2` 从不被读 |
| `OP_NEW_SET` | dst=r1 imm=r3 | 3218 | 4286-4313 | `vm.c:4287` `int n = ins.r3;`；`vm.c:4302` `vm_set_add_comp`；`vm.c:4312` `value_set(&R[ins.r1], VAL_SET, sidx, …)`；`ins.r2` 从不被读 |
| `OP_SET_INTERVAL` | dst=r1 imm=r2 | 3219 | 4315-4345 | `vm.c:4333-4335` `nameFlags = ins.r2; nameIdx = nameFlags & 0xFFFFFF; flags = (nameFlags >> 24) & 0xFF;`；界值来自值栈（`vm.c:4331-4332`）；`vm.c:4348` 写 `R[ins.r1]` |
| `OP_SET_ADD` | obj=r1 mutate src=r2，无 dst | 3227 | 4453-4457 | `vm.c:4454` `Value *sv = &R[ins.r1];`；`vm.c:4455` `vm_set_add(vm, sv->ival, &R[ins.r2])`——改的是 r1 指向的容器，**没有** `value_set(&R[ins.r1])` |

`OP_THREAD_JOIN` 与 `OP_THREAD_WAIT` 在这一列上**看不出差别**：两者都打印 `timeout=r%d`，两者 `r2` 都真是寄存器。差别只在 handler 和发出处。这正是「打印行可读 ≠ 可判」的实例，也是下一节要记的错。

### 3.1 第一批按族声明：纯寄存器算术 / 比较族（17 条，本批新增 13 条）

分工按 ivory 的要求改成按族做：每批填完先写「这一族为什么这样分」，再由 `ir_spec.py` 的 `IR-SHAPE` / `IR-DISASM-REGS` 对一遍，这样一次只暴露一类错误。

**族的判据（为什么是这 17 条）**：满足全部三条的 opcode 归此族——

1. `dst` 恒为 `r1`，且 handler 在写它之前不读它（输出专用）；
2. 操作数全是寄存器：没有立即数、没有 frame 变量下标、没有字符串池/表下标；
3. 语义是纯值运算（算术或比较），不带容器、字符串、线程副作用。

成员：`ADD MOD AND SUB MUL DIV NEG MIN MAX EQ NEQ LT GT LE GE NOT IS_NIL`。其中 `ADD/MOD/AND/MIN` 在第一批之前就已声明（当时作为正样本），所以本批实际新增 13 条——**族里有 17 条、本批新增 13 条**，这个差额留在表上，免得以后把「族」和「批次」当成同一个数。

**边界上被排除的，以及为什么**（排除理由比包含理由更该写下来）：

- `EQK` / `NEQK`：`r3` 是字符串池下标（`vm.c:4160` `int sidx = ins.r3;`），`r2` 是寄存器（`vm.c:4171`）。一个 opcode 里同时有寄存器与立即数，放进来会让「一族只暴露一类错误」失效；它们属于 K 族，且本来就在 `IR-DISASM-REGS` 的 `hidden=` 名单里。
- `CONCAT` / `IN` / `BIND` / `INDEX_GET` / `NEW_*`：失败模式是「对象/下标写错」，不是「操作数角色写错」，属于容器族。
- `MIN` / `MAX`：中间的 `set_minmax(vm, src, dst, isMax)` 会先把 `dst` 清成 nil（`vm.c:2477`），所以 `dst` 仍是输出专用，符合第 1 条；但它属于「需要理由才放进来的成员」，理由就是这一行。
- `LT/GT/LE/GE` 的 handler 里 `val_orderable` 失败会走异常路径（`vm.c:3557` 等），但那是控制流不是操作数，不影响角色。

**本批读数**（`vm.c` @ `9a4db3b`）：

```
P1-ROLES|declared=30/67|unmeasured=37
P1-VERDICT|red|failed=C1:OP_TRY_START,C3:OP_TRY_START
```

13 条新行全部绿——这一族里打印行与 handler 逐条一致，没有新增缺陷；唯一的红仍是 `TRY_START`（ivory 已认领修复）。这条结果本身是有信息的：反汇编器在「形状统一」的 opcode 上是对的，错都出在形状不统一的地方（`CALL_FUNC` 的 dst 在 r2、`RECORD` 的 dst 在 r3、`TRY_START` 的下标在 r2）。

**给交叉核对的一句话**：这一族的 `IR-SHAPE` 行若与上表不一致，先假定是我读错了族，而不是打印机错了——理由见 §5.3。

### 3.2 第二批按族声明：线程族（10 条，本批新增 8 条）

**先说分工的变动**：这一批最初被我写成「线程 / 资源族」一条（10 条），ivory 在 m04594 判为**必须拆成两批**，理由是失败模式不同——线程族的错法是「哪个线程号 / 等多久」，资源族的错法是「锁 / 资源下标指向哪里」，合起来会让「一族只暴露一类错误」失效。这正是我把 `EQK/NEQK` 排除出族 1 时用的同一条理由，它按同一条用回来。我接受，并按它的要求**先做线程族**。

**族的判据（为什么是这 10 条）**：

1. 操作对象是一个线程（`t%d`）或挂在线程上的邮箱；
2. 操作数是「线程 + 若干立即数字段」（控制码、偏移、标志、秒数寄存器），值寄存器最多一个；
3. 语义改变的是调度 / 通信状态，不是寄存器里的值。

成员：`THREAD_START THREAD_CTRL THREAD_GOTO THREAD_JOIN THREAD_WAIT THREAD_STATE SEND RECV WAIT YIELD`（本批新增 8 条；`THREAD_JOIN/THREAD_WAIT/THREAD_STATE` 早已声明）。

**`SEND`/`RECV` 我的读法**（ivory 要求：先读 handler、先给一句读法、再动表）：`SEND` 的 `r1` 是线程号、`r2` 是消息寄存器（`vm.c:4753` `value_copy(&v, &R[ins.r2])`），失败模式是「发给哪个线程」；`RECV` 的 `r1` 是结果寄存器、`r2` 是超时寄存器（`vm.c:4762-4764`），失败模式是「等多久」。两条都落在「哪个线程 / 等多久」，与 `JOIN`/`WAIT` 同形，所以我读**属于线程族**——与 m04594 的判定一致，理由也一致（不是语义亲缘，是失败模式）。

**`WAIT` 就是那个「SLEEP（若有）」**：树里没有 `OP_SLEEP`。`vm.c:3934` 读 `&R[ins.r1]`，`vm.c:3935-3937` 把 int/float 转成秒，然后让出执行（`vm.c:3941-3943` 设 `wake_at` 并 `SwitchToFiber`）。角色 `src=r1`，无水印。

**`YIELD` 是零操作数形状的正例**（本批新增，角色全空）：`vm.c:4028-4031` 一个寄存器都不读，打印行 `vm.c:5401` `"YIELD"` 一个都不印。若哪天启发式在这里凭空印出一个寄存器，① 会立刻抓到——这是这一族里唯一「形状本身就是判据」的成员。

**边界上被排除的**（按 ivory 的裁定）：`RECORD` 单列 store 族（§3.4）、`SAY` 单列 output 族（§3.5）；`STOP/HALT/THROW/TRY_END/JUMP/RETURN/TRY_START` 属控制流与异常族（操作数是偏移与标志，没有线程槽）。

**本批读数**（`vm.c` @ `9dd3ade`）：

```
P1-OPS|op=OP_RECV|printed-reg=r1,r2|printed-typed=-|printed-number=-|roles=dst:r1,src:r2,obj:-,imm:-
P1-BRANCH|op=OP_RECV|calls=2|effect=the-register-set-is-the-union-of-the-branches
P1-AMBIG|op=OP_RECV|slot=r2|kinds=register|sentinel(-1)|guarded=yes|effect=the-print-can-only-reach-the-register-glyph-when-the-slot-is-a-register
P1-AMBIG|op=OP_THREAD_JOIN|slot=r2|kinds=register|sentinel(-1)|guarded=no|effect=the-sentinel-can-be-rendered-with-the-register-glyph
P1-AMBIG-COUNT|n=2|effect=declared-outside-what-the-three-criteria-can-see-read-guarded-for-the-printer
P1-ROLES|declared=39/67|unmeasured=28
P1-VERDICT|green|declared=39|unmeasured=28|note=an-unmeasured-op-is-not-a-passing-op
```

同一份表、同一个工具，在三棵树上的读数（**红随树走，绿也随树走**）：

| 树 | 判据红 | 哨兵命名 |
| --- | --- | --- |
| `9a4db3b` | `C1+C3:OP_TRY_START`，`C2:OP_THREAD_START` | `RECV`、`THREAD_JOIN` 各一条 |
| `a670a70` | `C2:OP_THREAD_START` | 同上 |
| `9dd3ade` | 无（green，39/67） | 同上，但 `RECV` 变成 `guarded=yes` |

`9dd3ade` 是 ivory 的 `vm: THREAD_START shows the register it writes, RECV stops printing a sentinel as a register`——一条提交修了两处，正好给我两张不同的对照。

三件新东西：

**（一）本批抓到第二个真红：`THREAD_START` 把目的寄存器整个藏掉了。** handler `vm.c:4460` `int tidx = ins.r1, res = ins.r2, argc = ins.r3;`，随后 `vm.c:4500,4508,4544` 三处 `value_set(&R[res], …)` 把新线程的 id 写进 `r2`；发出处 `compiler.c:1992` `emit(..., OP_THREAD_START, tidx, alloc_reg(), 0)` 与 `compiler.c:2095` `emit(..., OP_THREAD_START, tidx, result, stmt->startStmt.argCount)`。而打印行 `THREAD_START t%d (args:%d)` 只印 `r1` 与 `r3`——**一个寄存器都不印**。这比 `RECORD` 更彻底：`RECORD` 至少印了 `r1,r2`，只是把 `r3` 漏了；`THREAD_START` 让读者完全看不出这条指令写了一个寄存器。修复是一行：格式串补 `, res=r%d` 并传 `ins->r2`——`9dd3ade` 就是这么修的（`vm.c:5367` 现在是 `THREAD_START t%d (args:%d), res=r%d`）。**这条红变绿是树变了，不是判据松了**：同一条判据在 `a670a70` 上仍然是红的（§4 G 行）。

**（二）哨兵槽不是一个数量问题，是一个「打印行有没有先判断」的问题。**

`RECV` 与 `THREAD_JOIN` 的 `r2` 都是「寄存器或 −1」：`RECV` 由 `compiler.c:2196-2197` `int timeout_reg = -1; if (stmt->recvStmt.timeout) timeout_reg = compile_expr(...);` 发出，handler `vm.c:4764` `if (ins.r2 >= 0) timeout = val_as_double(vm, &R[ins.r2]);`；`THREAD_JOIN` 的 handler `vm.c:4653-4654` 同形，enum 注释 `src/compiler/bytecode.h:34` 明写 `r2=timeout reg(-1=infinite)`。

在 `a670a70` 上两条打印行都无条件印 `timeout=r%d`（`vm.c:5378`、`vm.c:5383`），于是「没有超时」会印出 `timeout=r-1`——一个不存在的寄存器。我做了一件当时没写下来、后来证明必须写下来的事：工具原本只读 case 里**第一个** `snprintf`（`PRINT_RE.search`），而 `9dd3ade` 恰好把 `RECV` 的打印改成两支：

```c
vm.c:5383  case OP_RECV:  if (ins->r2 < 0) snprintf(out, outsz, "RECV r%d, timeout=none", ins->r1);
vm.c:5384              else snprintf(out, outsz, "RECV r%d, timeout=r%d", ins->r1, ins->r2); return;
```

只读第一支的读尺子于是在 `9dd3ade` 上把 `RECV` 判成绿——**这个绿来自「只读了那一支」**，不是来自「那一支没问题」。改成取 case 内所有 `snprintf` 的并集（并打出 `P1-BRANCH|calls=2`）之后，`r2` 回到寄存器集合里，问题才重新可见。

接着是第二层，也是本批真正的收获：**同一个约定，一条修了，一条没修。**

| opcode | 打印行 @ `9dd3ade` | 哨兵能不能印成寄存器 |
| --- | --- | --- |
| `RECV` | `vm.c:5383-5384` 先判 `ins->r2 < 0`，两支分开 | 不能（`guarded=yes`） |
| `THREAD_JOIN` | `vm.c:5378` `THREAD_JOIN t%d, timeout=r%d` 无条件 | 能（`guarded=no`），仍会印 `r-1` |

所以工具对哨兵的处置从「一条 AMBIG」改成读**打印调用前面有没有对这个槽的判断**（600 字符回看，判据之外的启发式，明确标注为读数不是判据）：`guarded=yes` 只说明「代码在印之前先决定了」，不说明决定是对的；`guarded=no` 则是可以直接指给人看的一句：**这条打印行能把 −1 印成寄存器。** 两条都不判红，仍计入 `P1-AMBIG-COUNT`。

**（三）一条约定在两个 opcode 上落地，就有一半修不到的风险。** ivory 的 `9dd3ade` 顺手修了 `RECV`，而 `THREAD_JOIN` 是同一条约定（负数表示「不等待」）的第二个落点，`vm.c:5378` 今天还是无条件的。这不是我发现的第三条缺陷，而是「修一处」与「修一类」的差别：**一次修复如果只覆盖了它读到的那个落点，它与没修的区别只在那一个落点上。** 判据能做的是把剩下的落点一直印在读数里（`guarded=no`），做不到的是替人去数「还有几个落点」——那要么靠读完表，要么靠枚举编码约定。

**（四）理论上的一条后果**：§5.1 的 ③ ⊆ ① 只在「每个 slot 恰好一个角色」时成立。这两个哨兵槽是前提失效处，但它们不是词汇缺口，是**编码与字形之间缺一层换算**：字形 `r%d` 只能表达「那里是个寄存器」，而编码用负数表达「那里没有寄存器」。判据能给的是命名，裁定在 §10。

**ivory 的裁定（`m05023`）**：这算**编码层缺陷，不算 ③ 的违规**——③ 说的是「立即数别印成寄存器」，而这里 `r2` 在一种情形下**确实是寄存器**，判据分辨不出两种情形，拿 ③ 去红它等于让判据替编码层做决定；但保持原样也不行，`timeout=r-1` 是一个不存在的寄存器，所以**打印器要学「这一个 slot 有两种形态」**（`ins->r2 < 0 ? "timeout=none" : "timeout=r%d"`），而编码若改（分成两个 opcode 或两个字段）是 P3 之后的事。因此 `P1-AMBIG` 保持不判红，但读法改了：不是「声明了但不判」，而是**声明了、三条判据看不见、打印器已按两种形态分别命名**。裁定里那句话（我已照抄进 §10）：**③ 在这类槽上重新独立，但正因此不能由判据替它判。**

### 3.3 第三批按族声明：资源族（2 条，本批新增 2 条）

**族的判据**：操作对象是互斥量（`l%d`）或一条资源上限；失败模式是「锁 / 资源索引指向哪里」，不是「哪个线程」。

成员：`LOCK`、`DECLARE`。

- `LOCK`：`vm.c:4709` `int midx = ins.r1;`，`vm.c:4719` `if (ins.r2 == 0) …`；打印行 `vm.c:5381` `LOCK l%d, %s`（0/1 印成 lock/unlock 两个字，不是 `r%d`）。角色 `imm=r1,r2`——`r2` 是标志位而不是寄存器，这正是把「带类型的索引」与「寄存器字形」分开的必要性。
- `DECLARE`：`vm.c:4193-4194` `int kind = ins.r1; double v = … float_pool[ins.r2] …`（`r2` 是 float_pool 下标），`vm.c:4195-4199` 五种上限（mem/threads/time/inst/vram）；打印行 `vm.c:5385` `DECLARE kind=%d, fp[%d]`——一行里没有任何寄存器字形，与 `ir_spec` 的 SHAPE 行 `[no register written]` 一致。

两条都绿。`DECLARE` 也是这一批里**文档、SHAPE、handler、打印行四者一致**的对照组——它绿得可信，正好反衬 §4.1 的两条不一致。

### 3.4 单成员族（一）：store 族 = `RECORD`

ivory 裁它单列，理由：`dst` 在 `r3`，而 `r1` 是 store 选择子（`-1=默认`），这已经是「dst 不在 r1」的第二类形状；并进任何一族都会破掉「族内 dst 位置一致」这条隐含前提。

我按 handler 建行：`vm.c:4204-4205` `int gidx = ins.r1; int packed = ins.r2;`；`vm.c:4206` `if (gidx == -1) { vm->record_default_store = packed &3; continue; }`（**这时没有 dst**）；`vm.c:4243` `value_move(&R[ins.r3], &found)`（找到了才有 dst）；`vm.c:4210` 的 `sidx` 取自 **string_pool**，不是 float_pool。角色 `dst=r3`、`imm=r1,r2`。

打印行 `vm.c:5386` `RECORD g%d, sidx=%d, meta=%d, res=r%d` 与此一致，绿。

两点必须写下来：

1. **dst 是条件写**。② 只要求「声明的 dst 出现过」，不区分「总是写」与「有时写」——`gidx == -1` 那条路径根本不写 `r3`。判据在这里有一个已知盲点，单列一族正好把它摆出来。
2. **这一行的操作数在文档里是另一套说法**，见 §4.1（b）：我按 handler 建行，但不宣布文档错，等你裁。

### 3.5 单成员族（二）：output 族 = `SAY`

`vm.c:3923` `value_to_string(vm, &R[ins.r1], buf, sizeof(buf), 0)`，打印行 `vm.c:5362` `SAY r%d`。角色 `src=r1`、**无 dst**。

它值得单列不是因为只有一个成员，而是因为 ② 对它天然成立（空集是任何集合的子集）——**② 在一族里全绿，可能只是因为那族没有 dst**。这是「族」这个概念在判据里唯一自带盲点的地方，写进 §7。

### 3.6 第六批按族声明：构造 / 区间族（5 条，本批新增 5 条）——控制组

§5.4 把这一族指定为控制组（预期异质族该报红），所以它的边界理由先写，成员后填。

**族的判据（为什么是这 5 条）**：

1. `dst` 恒为 `r1`，且是输出专用：`NEW_*` 三条与 `SET_INTERVAL` 全族只有一处写（`vm.c:3645`、`vm.c:3672`、`vm.c:4312`、`vm.c:4348`）；
2. 那个「不是寄存器」的操作数不是一个值，而是一个计数或一个打包下标：`NEW_ARRAY/NEW_DICT/NEW_SET` 的 `r3` 是元素数 / 对数，`SET_INTERVAL` 的 `r2` 是 `string_pool 下标 | flags<<24`；
3. 元素值与区间界值**不在寄存器里，在值栈上**（`vm.c:3638`、`vm.c:3666`、`vm.c:4331-4332`）。

成员：`NEW_ARRAY NEW_DICT NEW_SET SET_INTERVAL SET_ADD`。

`SET_ADD` 是族里唯一的例外，而且是**故意留的**：它没有 dst，`r1` 是容器（`obj` + `mutate`），`r2` 是值。留它是因为「容器写」正是这一族的第二类形状；若把它踢出去，这一族在角色上就变成「dst 恒为 r1」的统一族，控制组也就白设了。这与 §3.1 里 `MIN/MAX` 需要理由才放进来的做法相反：那里是「放进来要理由」，这里是「踢出去要理由」。

**本批读数**（`vm.c` @ `9dd3ade`）：

```
P1-ROLES|declared=44/67|unmeasured=23
P1-VERDICT|green|declared=44|unmeasured=23|note=an-unmeasured-op-is-not-a-passing-op
```

五条全绿，`P1-C1/C2/C3` 一条都没有。这是**预期被证伪**的那种绿，不是「这族没事」的那种绿：异质性确实存在，只是它长在判据看不见的那条轴上（操作数的存放位置），不在打印字形上。完整分析在 §5.6。

两条必须写下来的读数细节：

- `NEW_ARRAY` 的读数里 `printed-number=r3` 意味着「槽 r3 以裸数字字形印出」——`n=%d` 印的是元素个数，不是寄存器。`printed-reg` 与 `printed-number` 是两个集合，混读会把 `n` 当寄存器。
- `SET_INTERVAL` 的 `flags=%d` 实参是 `(unsigned int)ins->r2 >> 24`，不是 `ins->r2`，所以分词器归不到任何槽：这一行的 `printed-number` 是空的。**一个被算术拆开印的立即数，在 ③ 里等于不存在**（§7）。

### 3.7 第七批按族声明：K 形式族（7 条，本批新增 5 条）

先写边界理由：这一族的失败模式是「装错了哪一条常量」——非寄存器操作数指向常量池或直接是一个字面量，读错下标就把另一条常量装进寄存器，而且**不会报错**，只是值悄悄变了。

**族的判据（为什么是这 7 条）**：

1. `dst` 恒为 `r1`，且是唯一写点（`vm.c:3240`、`vm.c:3250`、`vm.c:3253`、`vm.c:3268`、`vm.c:3272`、`vm.c:4172`、`vm.c:4188`）；
2. 那个「不是寄存器」的操作数是**常量池下标或字面量**，不是值、不是计数：`LOADK_FLOAT` 的 `r2` 是 `float_pool` 下标（`vm.c:3253` `t->code->float_pool[ins.r2]`），`LOADK_STRING` 的 `r2` 是 `string_pool` 下标（`vm.c:3256` `int sidx = ins.r2;`），`LOADK_BOOL` 的 `r2` 是 0/1 字面量（`vm.c:3272` `ins.r2 ? 1 : 0`），`EQK`/`NEQK` 的 `r3` 是 `string_pool` 下标（`vm.c:4160` `int sidx = ins.r3;`）；
3. 该下标在打印行里必须落成裸数字或字面量，绝不落成 `r%d`：`"LOADK_FLOAT r%d, #%d"`（`vm.c:5325`）、`"LOADK_STRING r%d, \"%s\""`（`vm.c:5326-5328`）、`"LOADK_BOOL r%d, %s"`（`vm.c:5330`）、`"EQK r%d, \"%s\""`（`vm.c:5370`）、`"NEQK r%d, \"%s\""`（`vm.c:5371`）。

成员：`LOADK_INT LOADK_I64 LOADK_FLOAT LOADK_STRING LOADK_BOOL EQK NEQK`（前两条早期已声明，本批新增 5 条）。

**为什么 `MOV` / `LOAD_GLOBAL` / `PUSH_REG` / `INDEX_GET` 不在这一族**：`MOV` 没有非寄存器操作数（纯寄存器搬运）；`LOAD_GLOBAL`/`STORE_GLOBAL` 的下标指向一个**全局单元**，失败模式是「哪个命名空间 / 哪个全局」，属于容器族；`PUSH_REG` 把寄存器压到值栈，没有池；`INDEX_GET` 是容器取元素。**一个下标指向常量，和指向一个容器，不是同一种失败。**

**本批读数**（`vm.c` @ `cff9197`）：

```
P1-PRINTSHORT|op=OP_EQK|slot=r2|role=src|printed=r1|effect=declared-and-read-but-not-printed-the-line-hides-an-operand
P1-PRINTSHORT|op=OP_NEQK|slot=r2|role=src|printed=r1|effect=declared-and-read-but-not-printed-the-line-hides-an-operand
P1-ROLES|declared=49/67|unmeasured=18
P1-PRINTSHORT-COUNT|n=2|effect=declared-and-read-but-unprinted-named-not-judged
P1-VERDICT|green|declared=49|unmeasured=18|note=an-unmeasured-op-is-not-a-passing-op
```

五条新增全绿，`C1`/`C2`/`C3` 一条都没有——**但这一族没有干净地过去**：`EQK` 与 `NEQK` 都读 `R[ins.r2]`（`vm.c:4171`、`vm.c:4187`），而打印行只印出目标与那条字面量，被比较的寄存器一个字形都没有。读者看到 `EQK r1, "abc"` 无法知道它拿谁跟 `"abc"` 比。判据对这件事**完全没有反应**，因为 ① 只禁止「多印」，② 只对 `dst` 要求「少印不行」，源寄存器少印不在任何一条判据里（§5.7）。所以这一族加了一个**命名而不判**的读数：`P1-PRINTSHORT` 只说「声明的、handler 真的读了的槽，没有出现在打印行里」。

注：`LOADK_FLOAT` 印的是 `#%d`（池下标），不是值本身——这不是错，是这一族的正常形态：反汇编器指常量池，不解析浮点值；而 `LOADK_STRING` 解析出字符串本身并加引号。两种做法都在「非寄存器字形」一边，③ 对它们都无事可做。

## 4 十三组实测

全部在 `/tmp/wt-p1e`，命令 `python3 tools/p1_roles.py --vm <tree>`。

| # | 树 | 做了什么 | rc | 红在哪 |
| --- | --- | --- | --- | --- |
| A | `9a4db3b` 原样 | 无 | 1 | `C1:OP_TRY_START`、`C3:OP_TRY_START` |
| B | A + 一行 | `TRY_START catch var=r%d` → `var=%d` | 0 | 无（17 条全绿） |
| C | B + 一行 | `OP_MOV` 印 `ins->r3` 代替 `ins->r2` | 1 | `C1:OP_MOV` |
| D | B + 一行 | `OP_RECORD` 去掉 `res=r%d` 字段 | 1 | `C2:OP_RECORD` |
| E | B + 一行 | `OP_LOADK_INT` 把立即数印成 `r%d` | 1 | `C1:OP_LOADK_INT`、`C3:OP_LOADK_INT` |
| F | `0d6b6b7` 历史树 | 无（同一份判据、同一张表、未改一字节） | 1 | `C2:OP_RECORD`、`C1:OP_THREAD_WAIT`、`C1:OP_TRY_START`、`C3:OP_TRY_START` |
| G | `a670a70`（本分支 rebase 后的树） | 无（判据未改，表扩到 39 条） | 1 | `C2:OP_THREAD_START` |
| H | `9dd3ade`（再 rebase 一次） | 无（判据未改，表仍是 39 条；工具读打印行的方式改了：并集 + 分支读数） | 0 | 无——**但这不是「树上没事了」**，见下 |
| I | `9dd3ade` + 一行删除 | 在 `.probe/` 拷贝上删掉 `RECV` 的 `if (ins->r2 < 0)` 分支 | 0（**读数变、red 不变**） | `P1-AMBIG|op=OP_RECV|guarded=no` |
| J | `9dd3ade` + 一行反转 | 把那个判断反过来（`< 0` → `> 0`），打印逻辑因此是错的 | 0（**读数和 I 相反、仍然绿**） | `guarded=yes`——判据说「有判断」，不说「判断对」 |
| K | `9dd3ade` 原样 | 无（表扩到 44 条：族 5 构造/区间） | 0 | 无——**这一组是控制组**：预期它会红，它全绿了，见 §5.6 |
| L | `9dd3ade` + 一行「照文档修」 | 给 `NEW_ARRAY` 的打印行补 `base=r%d` 并传 `ins->r2`（`bytecode.h:14` 的说法） | 1 | `C1:OP_NEW_ARRAY`（`extra=r2`）——**照文档修会比照 handler 修先红** |
| M | `cff9197` + 一行 | 给 `EQK` 的打印行补上被比较的寄存器（`r%d` + `ins->r2`） | 0 | `P1-PRINTSHORT-COUNT|n=2` → `n=1`：只 `EQK` 消失，`NEQK` 还在——读数按落点移动，是 op 级的 |
| N | `cff9197` + 一行 | 把同一个槽印成 `@%d`（还是裸数字，换了个前缀） | 0 | `P1-PRINTSHORT` 仍点名 `OP_EQK:r2`——**它分不清「没印」与「印在别的命名空间」** |

A 的红（在 `9a4db3b` 上真实存在，不是我改出来的；那棵树已经不是今天的树，见 §5.2）：

```
P1-C1|op=OP_TRY_START|printed=r2|declared=none|extra=r2|effect=a-register-the-roles-do-not-declare
P1-C3|op=OP_TRY_START|imm=r1,r2|printed-reg=r2|leak=r2|effect=an-immediate-printed-as-a-register
P1-ROLES|declared=17/67|unmeasured=50
P1-VERDICT|red|failed=C1:OP_TRY_START,C3:OP_TRY_START
```

（A 的 transcript 取自 17 条角色表的版本；§3.1 之后为 `declared=30/67|unmeasured=37`，§3.2 之后为 `39/67|28`。**红随树走**：`9a4db3b` 上是 `C1+C3:OP_TRY_START` 与 `C2:OP_THREAD_START`，`a670a70` 上只剩后者，`9dd3ade` 上两处都没有了——B 组的绿是探针拷贝上的绿，H 组的绿才是树上的绿，两者不能互相顶替。）

I、J 两组是这一批唯一「绿着变读数」的实测：J 把判断反过来（`< 0` → `> 0`），打印语义已经错了，读数却回到 `guarded=yes`——**它只看得见「有没有判断」，看不见「判断对不对」**。这是引理级的一点，写进 §7。I 组则是：删掉那个判断之后 `P1-VERDICT` 仍然是 green（哨兵不属于三条判据），但 `RECV` 的 `guarded` 从 `yes` 翻成 `no`。**命名的东西不会因为不判红就没用**——它恰好是哨兵这类问题的唯一可观测面；反过来说，一处从 `guarded=yes` 退回 `guarded=no` 的改动，门禁不会响，只有这条读数会。

H 组那句「无红」要读得窄一点：它说的是**三条判据在这 39 条声明上都通过了**。`P1-VERDICT` 自己的尾巴还带着 `note=an-unmeasured-op-is-not-a-passing-op`，而 `P1-AMBIG` 仍是两条、其中 `THREAD_JOIN` 是 `guarded=no`（§3.2 二）。全绿不等于都测过，也不等于命名的两条已经不是缺陷。

F 的红（三个当年被发现的真实缺陷全部重现，每一条都带主语）：

```
P1-C2|op=OP_RECORD|dst=r3|printed=none|missing=r3|effect=the-line-hides-the-register-it-writes
P1-C1|op=OP_THREAD_WAIT|printed=r2,r3|declared=r2|extra=r3|effect=a-register-the-roles-do-not-declare
P1-C1|op=OP_TRY_START|printed=r1|declared=none|extra=r1|effect=a-register-the-roles-do-not-declare
P1-C3|op=OP_TRY_START|imm=r1,r2|printed-reg=r1|leak=r1|effect=an-immediate-printed-as-a-register
P1-VERDICT|red|failed=C2:OP_RECORD,C1:OP_THREAD_WAIT,C1:OP_TRY_START,C3:OP_TRY_START
```

`RECORD` 藏 dst（C2）、`THREAD_WAIT` 印 `r3`（`compiler.c:2130` 传的就是 0，C1）、`TRY_START` 印 `r1` 并把 frame 变量下标印成寄存器（C1+C3）。C 与 D 证明 ① 与 ② 各有独立的最小反例；E 见 §5.1。

### 4.1 停下来报的四条：文档与 handler 对同一件事给了两个定义，或两条边界对同一形状给了两个名字

按 ivory m04594 §4 的新规则：**两个定义都在、且都能自洽**时，停下来报，不自己改族（THREAD_WAIT 那次是「一边缺一个事实」，不是这一类）。这一批读 handler 时撞到三处，都是这个形状。我按 handler 建表——判据的职责是查「打印行与代码是否一致」，所以代码是对象、文档是旁证——但**不宣布文档错**，等裁定。

**（a）`THREAD_WAIT` 的 `r3`：四个权威，两种说法。**

| 权威 | 说什么 |
| --- | --- |
| enum 注释 `src/compiler/bytecode.h:35` | `r1=thread idx, r2=cond reg(-1=seconds), r3=timeout reg(-1=none)` |
| `ir_spec` 的 SHAPE 行 | `wait thread[r1] cond r2 timeout r3` |
| handler `vm.c:4672-4691` | 只读 `ins.r1` 与 `ins.r2`（`vm.c:4678`、`vm.c:4685` 两处 `val_as_double(vm, &R[ins.r2])`），**`r3` 一次都没读** |
| 打印行 `vm.c:5379` | `THREAD_WAIT t%d, timeout=r%d`，印 `r1`、`r2`，不印 `r3` |

两份文档说「有一个超时寄存器 r3」，代码说它不存在。我这一行只声明 `src=r2`，不声明 `r3`——这是按 handler 建行，不是裁决。若判文档为准，`THREAD_WAIT` 的 `r3` 就是**声明了、从未读取、也从未打印**的死槽；那时判据需要第三类命名（死槽），现在没有，所以我没加。

**（b）`RECORD` 的操作数：文档描述的是 `DECLARE`。**

| 权威 | 说什么 |
| --- | --- |
| enum 注释 `src/compiler/bytecode.h:44` | `/* resource declare: r1=kind(0=mem,1=threads,2=time,3=inst), r2=float_pool idx, r3=0 */` |
| `ir_spec` 的 SHAPE 行 | `record(kind=r1, float_pool[r2])` |
| `DECLARE` 的 handler `vm.c:4193-4199` | 恰好就是这段文字：`kind = ins.r1`、`float_pool[ins.r2]`、五个 kind |
| `RECORD` 的 handler `vm.c:4204-4243` | `gidx = ins.r1`、`packed = ins.r2`（`sidx` 取自 **string_pool**，`vm.c:4210`）、**写 `R[ins.r3]`** |

那两行文档描述的是 `DECLARE` 的操作数，却挂在 `RECORD` 名下——像一次改名把注释留在了旧名字上。后果：`ir_spec` 的 SHAPE 行现在说 `RECORD` 没有 dst、`r2` 是 float_pool 下标，而 handler 说 dst 是 `r3`、`r2` 是 string_pool 下标 + meta 打包。我按 handler 建行（`dst=r3`）；**若判文档为准，`RECORD` 就没有 dst，我这行的 ② 检查对象消失，得撤回——但那时要修的是文档，不是 handler，因为 `vm.c:4243` 是活的。**

**（c）`NEW_ARRAY` 的文档说有寄存器基址，handler 说元素不在寄存器里。**

| 权威 | 说什么 |
| --- | --- |
| enum 注释 `src/compiler/bytecode.h:14` | `new array: r1 = [reg[r2 .. r2+r3-1]]` |
| `ir_spec` 的 SHAPE 行 `tools/ir_spec.py:99` | `r1 = [R[r2 .. r2+r3-1]]` |
| handler `vm.c:3634`、`vm.c:3636-3638` | `int n = ins.r3;` / `int base = t->sp - (n - 1); vm_array_push_n(vm, aidx, &t->stack[base], n);`——元素来自**值栈** |
| 打印行 `vm.c:5347` | `NEW_ARRAY r%d, n=%d`（`ins->r1`、`ins->r3`），`r2` 从不出现 |
| `ins.r2` | 全文件从不被读（`NEW_DICT`、`NEW_SET` 同样：`vm.c:3662`、`vm.c:4287`） |

三处不一致里，这一处最危险，因为它的错法**在 P1 的方向上看着是对的**：一个照文档建的 MIR 会去读寄存器 `r2 .. r2+r3-1`，而 VM 读的是值栈——算术测试全绿，只有指针 / 数组测试才会暴露，而 P1 的等价性检验（原始字节 vs 重建字节）恰好不会区分二者。

**（d）两条族边界对同一种形状给了两个名字：打包立即数。**（这一条不是文档与 handler 冲突，是**族边界**冲突，按 ivory `m05023` §3 的要求停下来报。）

| opcode | 一个槽里塞了什么 | 拆法 | 今天所在族 |
| --- | --- | --- | --- |
| `SET_INTERVAL` | `r2 = (string idx) \| (flags << 24)`（`vm.c:4333-4335`） | 低 24 位下标 / 高 8 位标志 | 族 5 构造与区间 |
| `RECORD` | `r2 = sidx \| (meta << 16)`（`vm.c:4204`，`sidx` 取 `vm.c:4210`） | 低 16 位 string 下标 / 高 8 位 meta | 族 3 存储 |
| `DECLARE` | `r2` = `float_pool` 下标，**不打包** | — | 族 2b 资源 |

两条都是「一个操作数槽承载两三个字段」，而我今天把它们分在两个族里，依据是**失败的后果不同**（`RECORD` 的失败是「写错了哪个槽」，`SET_INTERVAL` 的失败是「名字或标志错位」），**不是形状不同**。这两个轴（失败的后果 / 操作数的存储形态）交叉一次，同一种形状就会落在两个族里——**这正是我自己在 §5.6 里说的「族切错会让判据看不见那一类」。** 我不自己切，等 §10 裁：若按 ivory 的提法再切一刀「打包立即数族」，族 5 与族 3 都要重切；在那之前这两行都按「一个立即数槽」声明（`imm=r2`），工具不拆包，也不该在判据里拆——判据只知道 `r2` 是不是立即数，不知道立即数里面还有几个字段。

`NEW_DICT` 的注释 `bytecode.h:40` 反过来写对了（`r2=0, r3=pair count (2*r3 values from stack)`），`ir_spec.py:100` 也写对了。所以这不是「注释全错」，是**注释只错了一条**——比全错更难发现，因为读者会从对的那两条推出剩下那一条也对。

L 组把这一条从散文变成读数：如果一个修的人**信了文档**，给 `NEW_ARRAY` 补上 `base=r%d` 并传 `ins->r2`，判据立刻红（`P1-C1|op=OP_NEW_ARRAY|printed=r1,r2|declared=r1|extra=r2`，rc=1）——因为角色表里 `r2` 不是任何角色。**照文档修比照 handler 修先红**，这就是判据在两条权威打架时唯一能说的话：它不裁定谁对，但它让「照文档修」这条路当场响。

三条的对照组是 `DECLARE` 与 `SAY`：文档、SHAPE、handler、打印行四者一致，绿得可信。**不一致时绿不算证据，停止才算。**

## 5 七条结构性结果

### 5.1 ③ 在检测能力上不独立于 ①

角色表里每个 slot 只有一个角色，所以 `imm ∩ (dst ∪ src ∪ obj) = ∅` 恒成立，于是 `imm ∩ REG ≠ ∅ ⇒ REG ⊄ dst ∪ src ∪ obj`——**任何违反 ③ 的行必然违反 ①**。实测 E 就是构造：想造一个「只红 ③」的反例造不出来，最干净的一条也必然带 `C1`。

（前提是每个 slot 恰好一个角色；`RECV` 的 `register|sentinel` 槽是前提失效处，见 §3.2 末段。）所以三条判据在**检测**上只有两条（① 与 ②），③ 的价值是**诊断命名**：它告诉修的人该把 `r%d` 改成 `%d`，而不是去改角色表。若要保留三条，③ 的措辞应写成命名纪律（「立即数与下标不得使用寄存器字形」），而不是并列的独立条件——否则它会看起来比实际多一条防线。

### 5.2 `TRY_START`：两棵树，两个读数（绿不预支）

```
vm.c:5372  case OP_TRY_START:   snprintf(out, outsz, "TRY_START catch var=%d", ins->r2); return;
```

`ins->r2` 是 frame 变量下标（`compiler.c:2292` 的 `varIdx`，`vm.c:4440` 存进 `f->var_idx`），不是寄存器；印成 `r2` 之后，读到这一行的人和机器都会认为它是寄存器 2。

| 树 | `P1-VERDICT|failed` | 谁改的 |
| --- | --- | --- |
| `9a4db3b` | `C1:OP_TRY_START, C3:OP_TRY_START` | 我报的（§4 A 组） |
| `a670a70` | 只剩 `C2:OP_THREAD_START` | ivory 的 `a670a70`「vm: TRY_START prints a frame variable index, not a register」 |
| `9dd3ade` | 无红（green，39/67） | ivory 的 `9dd3ade`「vm: THREAD_START shows the register it writes, RECV stops printing a sentinel as a register」 |

第二行是本分支 rebase 到 `a670a70` 之后**实跑**的，不是从第一行推出来的。B 组（探针拷贝上把 `var=r%d` 改成 `var=%d`）证明这条判据认得出这一类，它**不**证明树已经修好；树上的绿只能由树上跑出来的读数给。这条规矩也适用于后面所有族：**绿要带树号，红也要带树号。**

`THREAD_WAIT` 在 `9a4db3b` **不是**红的：它的 `r2` 真是寄存器（见 §5.3）。第二处真红（`THREAD_START` 藏 dst）是第二批按族声明时抓到的，见 §3.2。

### 5.3 我自己的角色表先错过一次

第一版表里我给 `OP_THREAD_WAIT` 写的是 `imm=r1,r2`，理由是「`r2` 是秒数」（`vm.c:4678` 里 `sec` 确实取自 `r2`）。于是工具在 `9a4db3b` 上报了四条红，其中两条主语是 `THREAD_WAIT`。回去读 handler 的出处才发现错的是表：

- `compiler.c:2129` `int sec_reg = compile_expr(comp, stmt->threadWaitStmt.arg);`，`compiler.c:2130` `emit(..., OP_THREAD_WAIT, tidx, sec_reg, 0)`——发出去的是**寄存器号**；
- `vm.c:4678` `double sec = val_as_double(vm, &R[ins.r2]);`——读的是 `R[ins.r2]`，即「r2 这个寄存器里的值」，不是「r2 这个数值」。

两张证据说的是同一件事：`r2` 是寄存器，打印 `timeout=r%d` 是对的。表改正之后这两条红消失，剩下的才是真红。

这条错的形状值得记住：打印行 `timeout=r%d` 在两种读法下**长得完全一样**——「超时值放在 r2」与「超时秒数是 r2 里的整数」。打印行本身不可判，只有 handler 可判。所以角色表的第三、四列（handler 行、读数出处）不是装饰：判据的每一行红，其主语都可能是表而不是打印机，而**把红归错主语**与「一个红没有主语」是同一种失效。

### 5.4 一个可证伪的预期：缺陷只长在形状不一致的地方

把这一批读到的都摆在一起，会看到一件不是刻意找、而是读出来才看见的事：反汇编器**恰好在形状一致的地方是对的**，已经找到的每一处缺陷都落在形状不一致处——

| 缺陷 | 形状为什么不一致 |
| --- | --- |
| `CALL_FUNC` 的 dst 是 `r2`（`vm.c:4062`） | 目的寄存器不在 `r1` |
| `RECORD` 的 dst 是 `r3`（`vm.c:4243`） | 目的寄存器在第三个槽，且是条件写 |
| `TRY_START` 的 `r2` 是 frame 变量下标（`vm.c:5372`） | 有 `r%d` 字形但不是寄存器 |
| `THREAD_START` 藏掉 `r2`（`vm.c:5367`） | 一行里唯一被写的寄存器恰好不被打印 |

于是可以写下一个**可被证伪**的预期（写在 tree `a670a70` 上，现读 `9dd3ade`）：

> 若下一族的成员在形状上是异质的（同一族内 dst 位置不固定、或寄存器与下标混用同一槽），该族应至少报出一处红；若一个明确异质的族跑出全绿，先怀疑族被切错了，再怀疑判据看不见那一类。

它可证伪的地方在于：**它给了你一个「本该红而绿」的失败形态**，而不是一句安慰。下一批的集合 / 区间族（`SET_ADD SET_INTERVAL NEW_SET NEW_ARRAY NEW_DICT`）正是天然的形状异质族——`SET_INTERVAL` 的 `r2` 是「string_pool 下标 | flags<<24」，`NEW_SET` 的 `r3` 是元素个数，`SET_ADD` 的 `r2` 是值寄存器——所以它被选作控制组：**如果它全绿，先别庆祝，那说明切族或判据有一处不对。**

（反过来，§3.5 的 `SAY` 是「本该绿而绿」的对照：一族没有 dst，② 天然成立，那种绿什么也不证明。两个方向都要有样本，这个预期才算被检验过。）

写完这句之后只过了一条提交：`9dd3ade` 把表里最后两处「形状不一致」补平了（`THREAD_START` 补印 `res=r%d`、`RECV` 分支打印），于是 `9dd3ade` 全绿。**这不是预期被证伪**——预期说的是「异质处该有红」，而那两处已经不异质了。它也不是预期被证实：证实的样本还没出现，下一个异质族（集合 / 区间）才是它的正式检验。这里只记一笔：预期在被检验之前就先被环境追平了一次，这种时候最容易误把「环境变了」当成「预期对了」。

（这一族后来跑过了，按字面被证伪，证伪点与修正后的措辞见 §5.6。§4 K 组就是那一跑。）

### 5.5 同一个约定修了一半：`RECV` 修了，`THREAD_JOIN` 没修

`9dd3ade` 一条提交修了两处（`THREAD_START` 补 `res=r%d`、`RECV` 分两支打印），但「负数表示不等待」这条约定有两个落点，第二个落点 `THREAD_JOIN`（`vm.c:5378`）今天还是 `timeout=r%d` 无条件的。

这不是「漏了一个 bug」那么简单。一次修复如果只改了它读到的那一处，**它和没修的区别只在那处**；而同一个约定通常同时在几处落地，读代码时只看得见你正读的那一处。判据给不出「还有几个落点」，它能给的是一条留在读数里的 `guarded=no`（§3.2 二），让人下次跑的时候还看得见。

要把它变成能数的东西，得先枚举「编码约定」，再对每条约定问一遍「哪些 opcode 用了它」——那是 §10 的工作，不是三个判据的工作。这条结论与 §5.4 的可证伪预期是一对：**缺陷长在形状不一致处，而修复长在形状一致处**。

### 5.6 控制组全绿：§5.4 的预期按字面被证伪

§5.4 的预期原文是：「若下一族的成员在形状上是异质的（同一族内 dst 位置不固定、或寄存器与下标混用同一槽），该族应至少报出一处红」。

构造 / 区间族 §3.6 按那句括号里的每一个条件都是异质的：dst 位置不固定（`SET_ADD` 没有 dst）、寄存器与下标混用同一槽（`SET_INTERVAL` 的 `r2` 是打包下标）、操作数来源不同（`NEW_*` 的元素在值栈上）。结果 5/5 全绿、rc=0（§4 K 组）。

**所以预期按字面被证伪。错在括号里那句判据，不在结论。** 它把「形状不一致」写成了两个轴——dst 的位置、槽的角色——而这两个轴都不是判据能看见的东西。判据能看见的只有**打印字形**：只要打印行把寄存器印成 `r%d`、把立即数印成裸数字，三条就都过，哪怕这条指令的输入根本不在寄存器里。

修正后的措辞（这是这一批真正的产出）：

> 一族在打印命名空间里同质（每条把 dst 印成 `r%d`、把不是值的东西印成裸数字），即使操作数的**存放位置**（寄存器 / 值栈 / 常量池）在族内不一致，三条判据也会全绿。所以「全绿」只说明打印行与角色表一致，不说明这一族被理解过。族的分界应按**打印命名空间**切；存放位置是另一条轴，写在族理由里，不进判据。

这也解释了 §3.6 为什么坚持留下 `SET_ADD`：它让这一族在打印命名空间上仍然同质（`r%d, r%d`），却在角色上异质（`obj` + `mutate`，没有 dst）。两个轴分开之后才看得清是哪个轴让预期失效——如果把 `SET_ADD` 踢出去，这一族在两个轴上都同质，「预期被证伪」这句话就无从说起。

第三个后果，也是这一批唯一新增的盲点：**③ 只看得见「原样印出的立即数」**。`SET_INTERVAL` 的 `flags=%d` 实参是 `(unsigned int)ins->r2 >> 24`，不是 `ins->r2`，分词器归不到槽，于是「一个打包下标被拆成两段印出来」在 ③ 里等于不存在。这不是判据的漏洞，是 ③ 的定义：它管「立即数用了寄存器字形」，不管「立即数怎么被拆开印」（§7）。

顺带把 §5.4 那句话的检验状态说清：它现在**有一次失败的检验**（这一族），而不是「尚未检验」。一个预期被证伪之后才有资格被修正；怕的是它一直停在「等下一族」的状态里，然后被当成已经成立的事实引用。

### 5.7 判据只朝一个方向看：多印的抓得到，少印的抓不到

`C1` 说的是「打印行里的寄存器 ⊆ 角色表声明的槽」——它抓**多印**。反方向（角色表声明的槽 ⊆ 打印行）只由 `C2` 对 `dst` 管一半：漏印目标会红。**源寄存器与容器槽漏印，三条判据里没有任何一条会响。**

今天树上的样本是 `EQK`/`NEQK`：`vm.c:4171`/`vm.c:4187` 读 `R[ins.r2]`，`vm.c:5370-5371` 只印 `r1` 和字面量。这一族因此是绿的——**绿得没有主语**：不是「这一族被查过且过」，而是「这一族有一类错不在射程内」。这与 §7 里 `SAY` 的第二种读法（没有可查的对象）不同：这里对象存在，判据没有这一问。

这是判据的**方向性缺陷**，不是实现 bug：① 的定义就是排除「凭空多出的寄存器」，而「隐藏的操作数」是另一句话。补法有两条，都要 §10 定：

- 修打印器：`EQK r%d, r%d, "%s"` 加上 `ins->r2`，让这一族也遵守「读什么印什么」；
- 加第四条判据：「声明的角色槽 ⊆ 打印行」，代价是今天 `EQK`/`NEQK` 立刻红两处，而且这是真要改打印器的红。

我倾向第二条，但它与 ② 重叠（② 是 `dst` 的特例），所以若 §10 采纳，应当写成 ② 的一般化——**「声明的角色槽 ⊆ 打印行」，`dst` 只是其中一类**——而不是第四条并列的判据。

M/N 组是它的边界：M 证明读数按落点移动（只修 `EQK`，就只有 `EQK` 那一行消失，`NEQK` 还在）；N 证明 `P1-PRINTSHORT` 分不清「没印」与「印在别的命名空间」——它报的是「这个槽没有以寄存器字形出现」，不是「这个槽没有被印」。

## 6 覆盖：49/67，没说到的 18 条不算通过

每次运行都打印 `P1-ROLES|declared=49/67|unmeasured=18` 与 `P1-UNMEASURED|ops=...`。一个没有被声明角色的 opcode **不是通过**，它只是没有被声明；这是这份判据与「67/67 全绿」之间唯一不能糊过去的差别。

覆盖率走过的路：17/67 → 30/67（族 1）→ 37/67（线程+资源的初版，未拆族）→ 39/67（拆族后新增 `YIELD`、`DECLARE`）→ 44/67（族 5 构造/区间新增 5 条）→ **49/67**（族 6 K 形式新增 5 条）。剩下 18 条要进入 P1，仍需按族逐条从 handler 体读出 `dst/src/obj/imm`——这是语义动作，判据不能替代。当前未归族的是 `MOV INDEX_SET TRY_START CALL_FUNC CALL_BUILTIN MAKE_FUNC`（§3 开头，6 条；`LOADK_INT` 与 `LOADK_I64` 本批已归入族 6），以及控制流与异常族（`JUMP JUMP_IF_FALSE JUMP_IF_TRUE RETURN STOP HALT THROW TRY_END CALL_VALUE`，9 条）、字符串/IO 与容器族（`CONCAT IN BIND INDEX_GET OR PUSH_REG LOAD_GLOBAL STORE_GLOBAL LOAD_CAPTURE`，9 条）。

## 7 这份判据不证明什么

- 它比较的是「打印行」与「手写角色表」。角色表是第二只手；§5.3 就是表错而不自知的实例，纠正它靠的是 handler，不是判据自己。
- 它不证明语义正确：`OP_MIN` 的 `reads_dst=no`（`set_minmax` 清 dst）是角色层面的陈述，不是求值正确性。
- 它不证明 disassembler 之外的事：编码、编译器的发出、VM 的执行都不在射程内。
- 它不比顺序。顺序在没有声明之前不是可判的（`0d6b6b7` 的 ORDER 行已经演示过一次），`ir_spec.py` 的 `IR-DISASM-REGS` 只比集合，在角色表落地之前这是对的。
- **一个族在 ② 上全绿，可能只说明那族没有 dst。** `SAY`（§3.5）就是这样的族：空集是任何集合的子集，② 对它恒真。所以「某族全绿」这句话里的绿，要分清是「查过并通过」还是「没有可查的对象」——§5.4 把这两个方向都写成了预期。
- 它不判「条件写」。`RECORD` 的 `r3` 只在 `gidx != -1` 且命中时才被写（`vm.c:4206`、`vm.c:4243`），② 只问「声明的 dst 有没有出现在打印行里」，不问「这条路径一定写吗」。
- 它不判文档与 handler 谁对。§4.1 的两条不一致，判据如实报「我按 handler 建行」，裁定权在 §10。
- **一个 case 里分支打印时，读一支等于没读。** 现在取 case 内所有 `snprintf` 的并集，并打出 `P1-BRANCH|calls=N`；`9dd3ade` 就是靠这条才没被读成假绿（§3.2 二）。但并集只覆盖同一个 case 里的分支，如果哪天打印行被抽到一个 helper 里、由调用方拼字符串，这层就失效了。
- **`guarded=yes` 只说明「印之前先判断了」，不说明判断对。** §4 J 组实测：把 `RECV` 的判断反过来（`ins->r2 < 0` → `> 0`），语义已经错了，读数仍回到 `guarded=yes`。它是「有没有一层换算」的观测面，不是换算正确性的判据。
- **它数不了约定有几个落点。** §5.5 的 `THREAD_JOIN` 就是同一约定没被修到的那一半；工具能一直把 `guarded=no` 印出来，不能告诉你还有哪几处。
- **它看不见值栈。** `NEW_ARRAY`/`NEW_DICT`/`NEW_SET` 的元素与 `SET_INTERVAL` 的界值都在值栈上（`vm.c:3638`、`vm.c:3666`、`vm.c:4302`、`vm.c:4331-4332`），判据只看打印字形，所以「这条指令的输入在哪」不在射程内。§5.6 就是这个盲点的样本：一族可以全绿，而它的操作数一个字都不在寄存器里。
- **③ 只看得见原样印出的立即数。** 实参一旦被算术改写（`ins->r2 >> 24`、`ins->r2 & 0xFFFFFF`），它就不再归属任何槽，那个立即数在 ③ 里等于不存在。`SET_INTERVAL` 的 `flags=%d` 和 `"%s"` 都是这样逃掉的。
- **「某族全绿」这句话有第三种读法**（前两种见上一条 `SAY`：查过并通过 / 没有可查的对象）。第三种是「那族的不一致不在打印字形上」——§5.6。
- **① 只朝一个方向看。** 它抓「打印了一个角色没声明的寄存器」，不抓「声明了、handler 读了、打印行没印」。② 只对 `dst` 管了反方向，对 `src`/`obj` 没有。`EQK`/`NEQK` 的 `r2` 就是今天树上这样的操作数（`vm.c:4171`、`vm.c:4187` 读了它，`vm.c:5370-5371` 一个字形都没有）。`P1-PRINTSHORT` 只命名，不判红——它进不了 `P1-VERDICT`（§5.7）。
- **`P1-PRINTSHORT` 分不清「没印」和「印在别的命名空间」。** N 组实测：把同一个槽印成 `@%d`，读数与完全不印一模一样。它报的是「这个槽没有以寄存器字形出现」。
- **多出来的名字数组不会骗到它，但也没被验证过。** `cff9197` 在 `vm.c` 末尾追加了 68 个名字的 census 表，里面每个 opcode 名都是一条字符串字面量；读数在 rebase 前后完全一致（§3.7），因为提取只认 `case OP_X:` 分支——但这是「恰好不撞」，不是一条被写下来的防线。

## 8 怎么跑

```
python3 tools/p1_roles.py                       # 默认 src/vm/vm.c，红则 rc=1
python3 tools/p1_roles.py --vm /path/to/vm.c    # 任意树 / 拷贝
```

输出是分行读数：`P1-OPS`（每行印了什么、声明了什么）、`P1-C1/C2/C3`（每条红带 `effect=`）、`P1-ROLES`、`P1-UNMEASURED`、`P1-VERDICT`。不接 `tools/gate.sh`：一个检查要不要进门禁、以什么期望值进门禁，是门禁所有者的决定，不是这份报告的。

## 9 §10 建议的三句措辞

1. **闭世界**：反汇编器一行里出现的每个寄存器，都必须在该 opcode 声明的 `dst/src/obj` 里；没声明的角色等于没测的寄存器。
2. **目的不外露**：声明的 `dst` 必须出现在打印集合里——少印一个寄存器比多印一个更难发现，因为没有任何一条读数要求它出现。
3. **命名纪律**：立即数、frame 变量下标、表下标一律不得使用 `r%d` 字形；这条不是第四条防线（它被 ① 蕴含），它是给修复者的指路牌。

三条合起来仍然只是「打印行 vs 声明」。把 P1 的入口判据说成「结构等价」是不成立的——那要等 MIR 有自己的角色声明、并且两份声明能互相证伪。
