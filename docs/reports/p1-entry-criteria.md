# P1 入口条件：三条可执行判据（tree `a670a70`，分支 `p1-entry-noble-quartz`）

ivory 在 m04411 §6 要的是：把 §10 的 P1 入口条件写成三条**可执行**判据，每条一个最小正例、一个最小反例，反例必须**今天就在树上变红**，且不动 main。

这份报告给三条判据的实现、39 条手写角色表（已归 5 族）的出处、七组实测，以及四条结构性结果。其中一条是**我自己的角色表先错了一次**，被同一条判据按 handler 抓回来；另一条说明 ③ 在检测能力上并不独立于 ①。

- 判据实现：`tools/p1_roles.py`（纯标准库，`--vm <path>`，红则 rc=1，不 import `ir_spec`，不与任何自检共用提取代码）
- 依据树：`a670a70`（ivory 修完三个打印器、把 ORDER 换成 REGS、并修了 `TRY_START` 之后；本分支已 rebase 到它）。旧树 `9a4db3b` 的读数不删，留在 §5.2 的对照表里；历史树 `0d6b6b7` 用来证明这三条判据能把当年那三个真实缺陷重新抓出来
- 只读：没有改 `src/`，所有被改坏的 `vm.c` 都是 `.probe/` 下的拷贝，不进提交

## 1 判据的对象：一条打印行，不是一段 case 文本

被测物是反汇编器**打印出来的那一行**，所以判据只读 case 里的 `snprintf` 调用，不读整个 case 体：`case OP_CALL_BUILTIN` 在打印之前先用 `ins->r2` 算函数名（`vm.c:5355-5358`），那是计算不是行；把整段 `case` 扫一遍的半尺子，正是 `0d6b6b7` 那版 ORDER 行把 `CALL_BUILTIN` 判成「顺序不同」的原因。

打印行里的每个占位符按**字面前缀**分词，这是判据能成立的前提：

| 字形 | 命名空间 | 例 |
| --- | --- | --- |
| `r%d`，实参是 `ins->rN` | 寄存器 | `MOV r%d, r%d` |
| `t%d` / `f%d` / `g%d` | 带类型的索引（线程 / 函数 / 全局） | `THREAD_JOIN t%d` |
| 裸 `%d`（无字面前缀） | 立即数 / 表下标 | `RECORD g%d, sidx=%d` |

**一个立即数被写成 `r%d`，它在理解上、也在机器上，就已经是「寄存器 r2」了**——这正是 ③ 要挡的事：`9a4db3b` 上的 `TRY_START` 就是这么红的（§5.2），`a670a70` 上它已经变绿，所以今天的红只剩 `THREAD_START` 把目的寄存器整个藏掉这一处（§3.2）。

## 2 三条判据

设 `REG` = 该行打印出的寄存器集合，`dst/src/obj`、`imm` 取自下表声明：

1. **① 闭世界**：`REG ⊆ dst ∪ src ∪ obj`。违反了就是「这行印了一个角色表里没有的寄存器」。
2. **② 目的不外露**：`dst ⊆ REG`。违反了就是「这行把真正被写的寄存器藏了」。
3. **③ 命名纪律**：`imm ∩ REG = ∅`。违反了就是「立即数/下标被当成寄存器印出来了」。

三条都只测**打印行**，不测语义。

## 3 角色表：39 条，逐条指到 handler 行

其中 31 条已归族（族 1 算术、族 2a 线程、族 2b 资源、族 3 store、族 4 output），8 条是早期逐条声明、还没归族的：`MOV LOADK_INT LOADK_I64 INDEX_SET TRY_START CALL_FUNC CALL_BUILTIN MAKE_FUNC`——它们各自的形状都不符合任何已写的族规则，硬归族会正好制造出「族内形状不一致」，所以先挂在这里，下一批按「调用族 / 载入族 / 容器族」的边界理由再动。

角色不按位置声明，因为位置在不同 opcode 上含义不同：`OP_CALL_FUNC` 的 dst 是 `r2`（`vm.c:4062`），`OP_RECORD` 的 dst 是 `r3`（`vm.c:4243`），`OP_THREAD_STATE` 的 `r2/r3` 是线程 id 与属性下标（`vm.c:4693`）。

| opcode | 角色 | dispatch | handler | 读数出处 |
| --- | --- | --- | --- | --- |
| `OP_MOV` | dst=r1 src=r2 | 3162 | 3236 | `vm.c:3237` `value_assign(&R[ins.r1], &R[ins.r2])` |
| `OP_LOADK_INT` | dst=r1 imm=r2 | 3163 | 3239 | `vm.c:3240` |
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
| `OP_LOCK` | imm=r1,r2 | 3209 | 4708-4722 | `vm.c:4709` `int midx = ins.r1;`；`vm.c:4719` `if (ins.r2 == 0) …`；发出处 `compiler.c:2170-2179` |

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

**`YIELD` 是零操作数形状的正例**（本批新增，角色全空）：`vm.c:4028-4031` 一个寄存器都不读，打印行 `vm.c:5400` `"YIELD"` 一个都不印。若哪天启发式在这里凭空印出一个寄存器，① 会立刻抓到——这是这一族里唯一「形状本身就是判据」的成员。

**边界上被排除的**（按 ivory 的裁定）：`RECORD` 单列 store 族（§3.4）、`SAY` 单列 output 族（§3.5）；`STOP/HALT/THROW/TRY_END/JUMP/RETURN/TRY_START` 属控制流与异常族（操作数是偏移与标志，没有线程槽）。

**本批读数**（`vm.c` @ `a670a70`，即 rebase 之后）：

```
P1-C2|op=OP_THREAD_START|dst=r2|printed=none|missing=r2|effect=the-line-hides-the-register-it-writes
P1-AMBIG|op=OP_RECV|slot=r2|kinds=register|sentinel(-1)
P1-AMBIG|op=OP_THREAD_JOIN|slot=r2|kinds=register|sentinel(-1)
P1-AMBIG-COUNT|n=2|effect=declared-but-not-judged
P1-ROLES|declared=39/67|unmeasured=28
P1-VERDICT|red|failed=C2:OP_THREAD_START
```

（同一份表在 `9a4db3b` 上还多两条 `C1:OP_TRY_START`、`C3:OP_TRY_START`；对照见 §5.2。）

三件新东西：

**（一）本批抓到第二个真红：`THREAD_START` 把目的寄存器整个藏掉了。** handler `vm.c:4460` `int tidx = ins.r1, res = ins.r2, argc = ins.r3;`，随后 `vm.c:4500,4508,4544` 三处 `value_set(&R[res], …)` 把新线程的 id 写进 `r2`；发出处 `compiler.c:1992` `emit(..., OP_THREAD_START, tidx, alloc_reg(), 0)` 与 `compiler.c:2095` `emit(..., OP_THREAD_START, tidx, result, stmt->startStmt.argCount)`。而打印行 `THREAD_START t%d (args:%d)` 只印 `r1` 与 `r3`——**一个寄存器都不印**。这比 `RECORD` 更彻底：`RECORD` 至少印了 `r1,r2`，只是把 `r3` 漏了；`THREAD_START` 让读者完全看不出这条指令写了一个寄存器。修复是一行：格式串补 `, res=r%d` 并传 `ins->r2`。

**（二）哨兵槽从一个变成两个：`RECV` 与 `THREAD_JOIN` 的 `r2` 都是「寄存器或 −1」。**

`RECV`：`compiler.c:2196-2197` `int timeout_reg = -1; if (stmt->recvStmt.timeout) timeout_reg = compile_expr(...);` → `emit(..., OP_RECV, result, timeout_reg, 0)`；handler `vm.c:4764` `if (ins.r2 >= 0) timeout = val_as_double(vm, &R[ins.r2]);`。

`THREAD_JOIN`：handler `vm.c:4653-4654` `double timeout = -1.0; if (ins.r2 >= 0) timeout = val_as_double(vm, &R[ins.r2]);`；enum 注释 `src/compiler/bytecode.h:34` 明写 `r2=timeout reg(-1=infinite)`。

两条打印行（`vm.c:5383` `RECV r%d, timeout=r%d`、`vm.c:5378` `THREAD_JOIN t%d, timeout=r%d`）在「没有超时」的场合都会印出 `timeout=r-1`——一个不存在的寄存器。工具对它们的处置是**只命名、不判红**：`P1-AMBIG` 两条 + `P1-AMBIG-COUNT|n=2`。理由是判据说不清「该不该用 `r%d` 印一个可能不存在的寄存器」——那要在 §10 定：要么打印器学 `none`（一行条件），要么编码上把「无超时」与「有超时」分开。

**（三）理论上的一条后果改写了**：§5.1 的 ③ ⊆ ① 只在「每个 slot 恰好一个角色」时成立。现在这条前提有两处失效（`RECV.r2`、`THREAD_JOIN.r2`），而它们不是孤例，是**同一条编码约定**（负数表示「不等待」）在两个 opcode 上的落地——所以这不是词汇缺口，是**编码与字形之间缺一层换算**。判据能给的是命名，不能给的是裁定；裁定在 §10。

### 3.3 第三批按族声明：资源族（2 条，本批新增 2 条）

**族的判据**：操作对象是互斥量（`l%d`）或一条资源上限；失败模式是「锁 / 资源索引指向哪里」，不是「哪个线程」。

成员：`LOCK`、`DECLARE`。

- `LOCK`：`vm.c:4709` `int midx = ins.r1;`，`vm.c:4719` `if (ins.r2 == 0) …`；打印行 `vm.c:5381` `LOCK l%d, %s`（0/1 印成 lock/unlock 两个字，不是 `r%d`）。角色 `imm=r1,r2`——`r2` 是标志位而不是寄存器，这正是把「带类型的索引」与「寄存器字形」分开的必要性。
- `DECLARE`：`vm.c:4193-4194` `int kind = ins.r1; double v = … float_pool[ins.r2] …`（`r2` 是 float_pool 下标），`vm.c:4195-4199` 五种上限（mem/threads/time/inst/vram）；打印行 `vm.c:5384` `DECLARE kind=%d, fp[%d]`——一行里没有任何寄存器字形，与 `ir_spec` 的 SHAPE 行 `[no register written]` 一致。

两条都绿。`DECLARE` 也是这一批里**文档、SHAPE、handler、打印行四者一致**的对照组——它绿得可信，正好反衬 §4.1 的两条不一致。

### 3.4 单成员族（一）：store 族 = `RECORD`

ivory 裁它单列，理由：`dst` 在 `r3`，而 `r1` 是 store 选择子（`-1=默认`），这已经是「dst 不在 r1」的第二类形状；并进任何一族都会破掉「族内 dst 位置一致」这条隐含前提。

我按 handler 建行：`vm.c:4204-4205` `int gidx = ins.r1; int packed = ins.r2;`；`vm.c:4206` `if (gidx == -1) { vm->record_default_store = packed &3; continue; }`（**这时没有 dst**）；`vm.c:4243` `value_move(&R[ins.r3], &found)`（找到了才有 dst）；`vm.c:4210` 的 `sidx` 取自 **string_pool**，不是 float_pool。角色 `dst=r3`、`imm=r1,r2`。

打印行 `vm.c:5385-5386` `RECORD g%d, sidx=%d, meta=%d, res=r%d` 与此一致，绿。

两点必须写下来：

1. **dst 是条件写**。② 只要求「声明的 dst 出现过」，不区分「总是写」与「有时写」——`gidx == -1` 那条路径根本不写 `r3`。判据在这里有一个已知盲点，单列一族正好把它摆出来。
2. **这一行的操作数在文档里是另一套说法**，见 §4.1（b）：我按 handler 建行，但不宣布文档错，等你裁。

### 3.5 单成员族（二）：output 族 = `SAY`

`vm.c:3923` `value_to_string(vm, &R[ins.r1], buf, sizeof(buf), 0)`，打印行 `vm.c:5362` `SAY r%d`。角色 `src=r1`、**无 dst**。

它值得单列不是因为只有一个成员，而是因为 ② 对它天然成立（空集是任何集合的子集）——**② 在一族里全绿，可能只是因为那族没有 dst**。这是「族」这个概念在判据里唯一自带盲点的地方，写进 §7。

## 4 七组实测

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

A 的红（今天树上真实存在，不是我改出来的）：

```
P1-C1|op=OP_TRY_START|printed=r2|declared=none|extra=r2|effect=a-register-the-roles-do-not-declare
P1-C3|op=OP_TRY_START|imm=r1,r2|printed-reg=r2|leak=r2|effect=an-immediate-printed-as-a-register
P1-ROLES|declared=17/67|unmeasured=50
P1-VERDICT|red|failed=C1:OP_TRY_START,C3:OP_TRY_START
```

（A 的 transcript 取自 17 条角色表的版本；§3.1 之后为 `declared=30/67|unmeasured=37`，§3.2 之后为 `39/67|28`。**红随树走**：`9a4db3b` 上是 `C1+C3:OP_TRY_START`，`a670a70` 上只剩 `C2:OP_THREAD_START`——B 组的绿是探针拷贝上的绿，G 组的绿才是树上的绿，两者不能互相顶替。）

F 的红（三个当年被发现的真实缺陷全部重现，每一条都带主语）：

```
P1-C2|op=OP_RECORD|dst=r3|printed=none|missing=r3|effect=the-line-hides-the-register-it-writes
P1-C1|op=OP_THREAD_WAIT|printed=r2,r3|declared=r2|extra=r3|effect=a-register-the-roles-do-not-declare
P1-C1|op=OP_TRY_START|printed=r1|declared=none|extra=r1|effect=a-register-the-roles-do-not-declare
P1-C3|op=OP_TRY_START|imm=r1,r2|printed-reg=r1|leak=r1|effect=an-immediate-printed-as-a-register
P1-VERDICT|red|failed=C2:OP_RECORD,C1:OP_THREAD_WAIT,C1:OP_TRY_START,C3:OP_TRY_START
```

`RECORD` 藏 dst（C2）、`THREAD_WAIT` 印 `r3`（`compiler.c:2130` 传的就是 0，C1）、`TRY_START` 印 `r1` 并把 frame 变量下标印成寄存器（C1+C3）。C 与 D 证明 ① 与 ② 各有独立的最小反例；E 见 §5.1。

### 4.1 停下来报的两条：文档与 handler 对同一件事给了两个定义

按 ivory m04594 §4 的新规则：**两个定义都在、且都能自洽**时，停下来报，不自己改族（THREAD_WAIT 那次是「一边缺一个事实」，不是这一类）。这一批读 handler 时撞到两处，都是这个形状。我按 handler 建表——判据的职责是查「打印行与代码是否一致」，所以代码是对象、文档是旁证——但**不宣布文档错**，等裁定。

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

两条的对照组是 `DECLARE` 与 `SAY`：文档、SHAPE、handler、打印行四者一致，绿得可信。**不一致时绿不算证据，停止才算。**

## 5 四条结构性结果

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

于是可以写下一个**可被证伪**的预期（tree `a670a70`）：

> 若下一族的成员在形状上是异质的（同一族内 dst 位置不固定、或寄存器与下标混用同一槽），该族应至少报出一处红；若一个明确异质的族跑出全绿，先怀疑族被切错了，再怀疑判据看不见那一类。

它可证伪的地方在于：**它给了你一个「本该红而绿」的失败形态**，而不是一句安慰。下一批的集合 / 区间族（`SET_ADD SET_INTERVAL NEW_SET NEW_ARRAY NEW_DICT`）正是天然的形状异质族——`SET_INTERVAL` 的 `r2` 是「string_pool 下标 | flags<<24」，`NEW_SET` 的 `r3` 是元素个数，`SET_ADD` 的 `r2` 是值寄存器——所以它被选作控制组：**如果它全绿，先别庆祝，那说明切族或判据有一处不对。**

（反过来，§3.5 的 `SAY` 是「本该绿而绿」的对照：一族没有 dst，② 天然成立，那种绿什么也不证明。两个方向都要有样本，这个预期才算被检验过。）

## 6 覆盖：39/67，没说到的 28 条不算通过

每次运行都打印 `P1-ROLES|declared=39/67|unmeasured=28` 与 `P1-UNMEASURED|ops=...`。一个没有被声明角色的 opcode **不是通过**，它只是没有被声明；这是这份判据与「67/67 全绿」之间唯一不能糊过去的差别。

覆盖率走过的路：17/67 → 30/67（族 1）→ 37/67（线程+资源的初版，未拆族）→ **39/67**（拆族后新增 `YIELD`、`DECLARE`）。剩下 28 条要进入 P1，仍需按族逐条从 handler 体读出 `dst/src/obj/imm`——这是语义动作，判据不能替代。当前未归族的是 `MOV LOADK_INT LOADK_I64 INDEX_SET TRY_START CALL_FUNC CALL_BUILTIN MAKE_FUNC`（§3 开头），以及集合/区间族、K 形式、控制流与异常族、字符串/IO 族的全部成员。

## 7 这份判据不证明什么

- 它比较的是「打印行」与「手写角色表」。角色表是第二只手；§5.3 就是表错而不自知的实例，纠正它靠的是 handler，不是判据自己。
- 它不证明语义正确：`OP_MIN` 的 `reads_dst=no`（`set_minmax` 清 dst）是角色层面的陈述，不是求值正确性。
- 它不证明 disassembler 之外的事：编码、编译器的发出、VM 的执行都不在射程内。
- 它不比顺序。顺序在没有声明之前不是可判的（`0d6b6b7` 的 ORDER 行已经演示过一次），`ir_spec.py` 的 `IR-DISASM-REGS` 只比集合，在角色表落地之前这是对的。
- **一个族在 ② 上全绿，可能只说明那族没有 dst。** `SAY`（§3.5）就是这样的族：空集是任何集合的子集，② 对它恒真。所以「某族全绿」这句话里的绿，要分清是「查过并通过」还是「没有可查的对象」——§5.4 把这两个方向都写成了预期。
- 它不判「条件写」。`RECORD` 的 `r3` 只在 `gidx != -1` 且命中时才被写（`vm.c:4206`、`vm.c:4243`），② 只问「声明的 dst 有没有出现在打印行里」，不问「这条路径一定写吗」。
- 它不判文档与 handler 谁对。§4.1 的两条不一致，判据如实报「我按 handler 建行」，裁定权在 §10。

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
