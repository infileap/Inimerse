# P1 入口条件：三条可执行判据（tree `9a4db3b`，分支 `p1-entry-noble-quartz`）

ivory 在 m04411 §6 要的是：把 §10 的 P1 入口条件写成三条**可执行**判据，每条一个最小正例、一个最小反例，反例必须**今天就在树上变红**，且不动 main。

这份报告给三条判据的实现、17 条手写角色表的出处、六组实测，以及三条结构性结果。其中一条是**我自己的角色表先错了一次**，被同一条判据按 handler 抓回来；另一条说明 ③ 在检测能力上并不独立于 ①。

- 判据实现：`tools/p1_roles.py`（纯标准库，`--vm <path>`，红则 rc=1，不 import `ir_spec`，不与任何自检共用提取代码）
- 依据树：`9a4db3b`（ivory 修完三个打印器、把 ORDER 换成 REGS 之后）；历史树 `0d6b6b7` 用来证明这三条判据能把当年那三个真实缺陷重新抓出来
- 只读：没有改 `src/`，所有被改坏的 `vm.c` 都是 `.probe/` 下的拷贝，不进提交

## 1 判据的对象：一条打印行，不是一段 case 文本

被测物是反汇编器**打印出来的那一行**，所以判据只读 case 里的 `snprintf` 调用，不读整个 case 体：`case OP_CALL_BUILTIN` 在打印之前先用 `ins->r2` 算函数名（`vm.c:5355-5358`），那是计算不是行；把整段 `case` 扫一遍的半尺子，正是 `0d6b6b7` 那版 ORDER 行把 `CALL_BUILTIN` 判成「顺序不同」的原因。

打印行里的每个占位符按**字面前缀**分词，这是判据能成立的前提：

| 字形 | 命名空间 | 例 |
| --- | --- | --- |
| `r%d`，实参是 `ins->rN` | 寄存器 | `MOV r%d, r%d` |
| `t%d` / `f%d` / `g%d` | 带类型的索引（线程 / 函数 / 全局） | `THREAD_JOIN t%d` |
| 裸 `%d`（无字面前缀） | 立即数 / 表下标 | `RECORD g%d, sidx=%d` |

**一个立即数被写成 `r%d`，它在理解上、也在机器上，就已经是「寄存器 r2」了**——这正是判据 ③ 要挡的事，也是今天树上唯一还红的地方。

## 2 三条判据

设 `REG` = 该行打印出的寄存器集合，`dst/src/obj`、`imm` 取自下表声明：

1. **① 闭世界**：`REG ⊆ dst ∪ src ∪ obj`。违反了就是「这行印了一个角色表里没有的寄存器」。
2. **② 目的不外露**：`dst ⊆ REG`。违反了就是「这行把真正被写的寄存器藏了」。
3. **③ 命名纪律**：`imm ∩ REG = ∅`。违反了就是「立即数/下标被当成寄存器印出来了」。

三条都只测**打印行**，不测语义。

## 3 角色表：17 条，逐条指到 handler 行

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

`OP_THREAD_JOIN` 与 `OP_THREAD_WAIT` 在这一列上**看不出差别**：两者都打印 `timeout=r%d`，两者 `r2` 都真是寄存器。差别只在 handler 和发出处。这正是「打印行可读 ≠ 可判」的实例，也是下一节要记的错。

## 4 六组实测

全部在 `/tmp/wt-p1e`，命令 `python3 tools/p1_roles.py --vm <tree>`。

| # | 树 | 做了什么 | rc | 红在哪 |
| --- | --- | --- | --- | --- |
| A | `9a4db3b` 原样 | 无 | 1 | `C1:OP_TRY_START`、`C3:OP_TRY_START` |
| B | A + 一行 | `TRY_START catch var=r%d` → `var=%d` | 0 | 无（17 条全绿） |
| C | B + 一行 | `OP_MOV` 印 `ins->r3` 代替 `ins->r2` | 1 | `C1:OP_MOV` |
| D | B + 一行 | `OP_RECORD` 去掉 `res=r%d` 字段 | 1 | `C2:OP_RECORD` |
| E | B + 一行 | `OP_LOADK_INT` 把立即数印成 `r%d` | 1 | `C1:OP_LOADK_INT`、`C3:OP_LOADK_INT` |
| F | `0d6b6b7` 历史树 | 无（同一份判据、同一张表、未改一字节） | 1 | `C2:OP_RECORD`、`C1:OP_THREAD_WAIT`、`C1:OP_TRY_START`、`C3:OP_TRY_START` |

A 的红（今天树上真实存在，不是我改出来的）：

```
P1-C1|op=OP_TRY_START|printed=r2|declared=none|extra=r2|effect=a-register-the-roles-do-not-declare
P1-C3|op=OP_TRY_START|imm=r1,r2|printed-reg=r2|leak=r2|effect=an-immediate-printed-as-a-register
P1-ROLES|declared=17/67|unmeasured=50
P1-VERDICT|red|failed=C1:OP_TRY_START,C3:OP_TRY_START
```

F 的红（三个当年被发现的真实缺陷全部重现，每一条都带主语）：

```
P1-C2|op=OP_RECORD|dst=r3|printed=none|missing=r3|effect=the-line-hides-the-register-it-writes
P1-C1|op=OP_THREAD_WAIT|printed=r2,r3|declared=r2|extra=r3|effect=a-register-the-roles-do-not-declare
P1-C1|op=OP_TRY_START|printed=r1|declared=none|extra=r1|effect=a-register-the-roles-do-not-declare
P1-C3|op=OP_TRY_START|imm=r1,r2|printed-reg=r1|leak=r1|effect=an-immediate-printed-as-a-register
P1-VERDICT|red|failed=C2:OP_RECORD,C1:OP_THREAD_WAIT,C1:OP_TRY_START,C3:OP_TRY_START
```

`RECORD` 藏 dst（C2）、`THREAD_WAIT` 印 `r3`（`compiler.c:2130` 传的就是 0，C1）、`TRY_START` 印 `r1` 并把 frame 变量下标印成寄存器（C1+C3）。C 与 D 证明 ① 与 ② 各有独立的最小反例；E 见 §5.1。

## 5 三条结构性结果

### 5.1 ③ 在检测能力上不独立于 ①

角色表里每个 slot 只有一个角色，所以 `imm ∩ (dst ∪ src ∪ obj) = ∅` 恒成立，于是 `imm ∩ REG ≠ ∅ ⇒ REG ⊄ dst ∪ src ∪ obj`——**任何违反 ③ 的行必然违反 ①**。实测 E 就是构造：想造一个「只红 ③」的反例造不出来，最干净的一条也必然带 `C1`。

所以三条判据在**检测**上只有两条（① 与 ②），③ 的价值是**诊断命名**：它告诉修的人该把 `r%d` 改成 `%d`，而不是去改角色表。若要保留三条，③ 的措辞应写成命名纪律（「立即数与下标不得使用寄存器字形」），而不是并列的独立条件——否则它会看起来比实际多一条防线。

### 5.2 今天树上还剩一处真红：`TRY_START`

```
vm.c:5372  case OP_TRY_START:   snprintf(out, outsz, "TRY_START catch var=r%d", ins->r2); return;
```

`ins->r2` 是 frame 变量下标（`compiler.c:2292` 的 `varIdx`，`vm.c:4440` 存进 `f->var_idx`），不是寄存器；印成 `r2` 之后，读到这一行的人和机器都会认为它是寄存器 2。修复是一行：`var=%d`——`RECORD` 的 `sidx=%d, meta=%d` 已经是这个写法，所以它不是新风格，是把第三个打印器已经做对的事做完。B 组实测：改这一行，17 条全绿。

`THREAD_WAIT` 在 `9a4db3b` **不是**红的：它的 `r2` 真是寄存器（见 §6）。

### 5.3 我自己的角色表先错过一次

第一版表里我给 `OP_THREAD_WAIT` 写的是 `imm=r1,r2`，理由是「`r2` 是秒数」（`vm.c:4678` 里 `sec` 确实取自 `r2`）。于是工具在 `9a4db3b` 上报了四条红，其中两条主语是 `THREAD_WAIT`。回去读 handler 的出处才发现错的是表：

- `compiler.c:2129` `int sec_reg = compile_expr(comp, stmt->threadWaitStmt.arg);`，`compiler.c:2130` `emit(..., OP_THREAD_WAIT, tidx, sec_reg, 0)`——发出去的是**寄存器号**；
- `vm.c:4678` `double sec = val_as_double(vm, &R[ins.r2]);`——读的是 `R[ins.r2]`，即「r2 这个寄存器里的值」，不是「r2 这个数值」。

两张证据说的是同一件事：`r2` 是寄存器，打印 `timeout=r%d` 是对的。表改正之后这两条红消失，剩下的才是真红。

这条错的形状值得记住：打印行 `timeout=r%d` 在两种读法下**长得完全一样**——「超时值放在 r2」与「超时秒数是 r2 里的整数」。打印行本身不可判，只有 handler 可判。所以角色表的第三、四列（handler 行、读数出处）不是装饰：判据的每一行红，其主语都可能是表而不是打印机，而**把红归错主语**与「一个红没有主语」是同一种失效。

## 6 覆盖：17/67，没说到的 50 条不算通过

每次运行都打印 `P1-ROLES|declared=17/67|unmeasured=50` 与 `P1-UNMEASURED|ops=...`。一个没有被声明角色的 opcode **不是通过**，它只是没有被声明；这是这份判据与「67/67 全绿」之间唯一不能糊过去的差别。剩下 50 条要进入 P1，需要有人逐条从 handler 体读出 `dst/src/obj/imm`——这是语义动作，判据不能替代（能自动化的只有「把候选行摆到人面前」）。

## 7 这份判据不证明什么

- 它比较的是「打印行」与「手写角色表」。角色表是第二只手；§5.3 就是表错而不自知的实例，纠正它靠的是 handler，不是判据自己。
- 它不证明语义正确：`OP_MIN` 的 `reads_dst=no`（`set_minmax` 清 dst）是角色层面的陈述，不是求值正确性。
- 它不证明 disassembler 之外的事：编码、编译器的发出、VM 的执行都不在射程内。
- 它不比顺序。顺序在没有声明之前不是可判的（`0d6b6b7` 的 ORDER 行已经演示过一次），`ir_spec.py` 的 `IR-DISASM-REGS` 只比集合，在角色表落地之前这是对的。

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
