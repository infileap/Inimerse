# 分片身份尺子：从「持不持」到「持的是不是那一把」

观察点：`a22eb704`（语料）与 `6642fed`（验收集），两者判定逐字相同。
本文件只记录**测量**与**尺子的射程**，不记录修复。

## 0. 这一件问的是另一半

这一族此前所有 census 问的都是同一个问题：**「这个站点持不持分片？」**
答案一律是「持」的 18 处，被记进 `guarded=True`。

但一把分片保护的是**一个槽**，而**槽的身份是它的下标 mod 分片数**。
于是「持」只回答了前半个问题，后半个问题是：

> 你持的那一把，是不是保护**你正在写的那个槽**的那一把？

一个 held/not-held 的尺子**问不出**这个问题 —— 它的值域里没有「哪一把」。
这就是为什么下面那 6 处能在这个仓库的每一张表里都读成 `guarded`。

## 1. 十八处，逐处报出「持的是哪一把」

`tools/pathruler-shardtable.py --ref a22eb704 --sites tools/pathruler-sites18.txt`

| # | 站点 | 所在函数 | 持的是 | 该行写的槽 | 判定 |
|---|---|---|---|---|---|
| 1 | `vm->globals[idx].val.type = VAL_INT;` | `builtin_atomic_set` | `idx` | `idx` | 是 |
| 2 | `vm->globals[idx].val.sval = NULL;` | `builtin_atomic_set` | `idx` | `idx` | 是 |
| 3 | `vm->globals = realloc(vm->globals, ...)` | `vm_global_grow` | ALL | —（`realloc`） | 是（全分片） |
| 4 | 清填循环体第 1 行 `vm->globals[i].name = NULL;` | `vm_global_grow` | ALL | `i` | 是（全分片） |
| 5 | 清填循环体第 2 行 `vm->globals[i].val.type = VAL_NIL;` | `vm_global_grow` | ALL | `i` | 是（全分片） |
| 6 | 清填循环体第 3 行 `vm->globals[i].val.ival = 0;` | `vm_global_grow` | ALL | `i` | 是（全分片） |
| 7 | 清填循环体第 4 行 `vm->globals[i].val.sval = NULL;` | `vm_global_grow` | ALL | `i` | 是（全分片） |
| 8 | `vm->globalCap = nc;` | `vm_global_grow` | ALL | —（`globalCap`） | 是（全分片） |
| 9 | 展开清理循环体第 1 行 `vm->globals[i].name = NULL;` | `vm_throw` | `f->var_idx` | `i` | **否** |
| 10 | 展开清理循环体第 2 行 `vm->globals[i].val.type = VAL_NIL;` | `vm_throw` | `f->var_idx` | `i` | **否** |
| 11 | `vm->globalCount = f->var_idx + 1;` | `vm_throw` | `f->var_idx` | —（`globalCount`） | 同一语句的尾 |
| 12 | 建槽循环体第 1 行 `vm->globals[i].name = NULL;` | `vm_execute_thread` | `idx` | `i` | **否** |
| 13 | 建槽循环体第 2 行 `vm->globals[i].val.type = VAL_NIL;` | `vm_execute_thread` | `idx` | `i` | **否** |
| 14 | `vm->globalCount = idx + 1;` | `vm_execute_thread` | `idx` | —（`globalCount`） | 同一语句的尾 |
| 15 | `vm->globals[idx].val = newv;` | `vm_execute_thread` | `idx` | `idx` | 是 |
| 16 | 建槽循环体第 1 行 `vm->globals[i].name = NULL;` | `vm_execute_thread` | `g` | `i` | **否** |
| 17 | 建槽循环体第 2 行 `vm->globals[i].val.type = VAL_NIL;` | `vm_execute_thread` | `g` | `i` | **否** |
| 18 | `vm->globalCount = g + 1;` | `vm_execute_thread` | `g` | —（`globalCount`） | 同一语句的尾 |

每行进入时的状态集由 `tools/pathruler.py` 给出，例如
`src/vm/vm.c:3743  -> 持分片  持的是=idx  states=[frozenset(), frozenset({'idx'})]`
—— 「可能持、也可能不持」被原样报出，不被压成一个字。

## 2. 验收 ② ：「持有全部分片」仍是一个成员

- 上一件量过：「拿全部分片」这个类**只有 1 个成员**（`vm_global_grow`）。
- 这一件以**站点**为单位复测：**6 个站点、1 个函数**。
- 两个数不矛盾，因为分母不同：**1 是函数数，6 是站点数**。
- 三处调用点（`vm_throw` 展开时的扩容调用、`L_STORE_GLOBAL` 与 `L_BIND` 各自的建槽前扩容调用）在进入时**未持任何分片**
  （源码注释逐字：`独占全部锁扩容（此时未持任何分片）`），所以「全分片」从不与
  单把分片叠加。

## 3. 验收 ③ 的负向对照：零不是零

**找到了。** 6 个站点，3 条逻辑语句，形状完全相同：

```c
if (X >= vm->globalCount) {
    for (int i = vm->globalCount; i <= X; i++) {
        vm->globals[i].name = NULL;          /* 持的是 X 的分片 */
        vm->globals[i].val.type = VAL_NIL;   /* 写的槽是 i 的 */
    }
    vm->globalCount = X + 1;
}
```

其中 `X` 分别是 `f->var_idx`（`vm_throw`）、`idx`（`L_STORE_GLOBAL`）、
`g`（`L_BIND`）。

**分片保护的是 `i mod 16`，而这里持有的是一把**。该循环写的槽是**一段连续下标**，
一段长度 ≥ 2 的连续下标必然跨至少两个同余类，因此**区间内除 `X mod 16` 之外的所有槽，
都是在没有它们自己那把锁的情况下被写的**。

对照：**同一条路径的另一半 —— 读侧** —— 取的正是自己那把。
`L_LOAD_GLOBAL` 取 `VM_GSHARD(vm, idx)` 的那一行，其中
`int idx = ins.r2;` 那一行 —— **它读 `vm->globals[idx]` 就取 `idx` 的分片**。
所以读侧一对一，写侧一对多。

`vm_global_grow` 面对**同一个问题**（连续区间）时的答案是**取全部 16 把**
（源码注释逐字：`扩容移动 globals 数组指针，须独占全部 16 把分片锁`）。
建槽路径面对同一个问题时只取了一把。**同一个问题，两种答案，而 census 只看得见
「有没有取」，看不见「取了几把、取的是哪一把」。**

## 4. 判据

- ★ **一条纪律的射程按【对象】写，而实现它的代码把它当【区间】读 —— 于是它保护的是一列
  模 16 同余的槽，而它以为保护的是一段连续的槽。**
- ★ **「持了锁」与「持了保护你的那个东西的那把锁」，在 held/not-held 的表里是同一个格子。**
  一个族被量了一半，而另一半的缺席在报告里读成「已经安全」。
- ★ **同一个问题（连续区间的槽由谁保护）在这个仓库里有两个答案，而两个答案都活着：
  扩容路径取全部，建槽路径取一把** —— 而取一把的那个，正是为了避开注释里写明的
  「避免交叉等待死锁」。**它用一个正确性缺口换来了一个死锁免疫。**
- ★ **一把尺子的值域决定了它能回答哪些问题。** 值域是 {持, 不持} 的尺子，
  对「持的是哪一把」不是答错，是**没有这一问**。

## 5. 射程与未决（必须与读数同行）

1. **为什么 `?` 曾经出现。** 尺子在 `vm_global_grow` 里一度报出键 `'?'`。成因不是这条
   路径特殊，是**解析器把「没有花括号的循环头」吸收进了它自己的循环体**，于是循环体
   丢了循环上下文，`for (i = 0; i < VM_GLOBAL_SHARDS; i++)` 里的锁不再是「全部分片」。
   同一处缺陷的第二个症状更重：**无花括号的循环体吞掉了它后面整个块**（`vm_debug_exec`
   的 `:5059-5060` 正是这个形状），使若干站点变成「未解析」。
   两者同一族：**★ 一个没有花括号的循环头，是它的循环体的一部分，还是一行头？**
   按行扫把它读成一行头，而它的真身是「头 + 恰好一个语句」。
2. **P1（括号配平前提）。** 尺子选「包含该行的最内层定义」，而
   `task_scheduler_entry` 在 `#ifdef` 下两开一闭 —— 即**一个真实引擎缺陷成了这把尺子的
   输入前提**。已按裁定印进每一次运行的输出，不再只写在 README 里。
3. **P2（分片定义）。** `VM_LOCK(vm)`（全局锁）**故意不算分片**，所以「无分片」是
   「没有持有分片」，**不是「没有持锁」**。同样每跑必印。
4. **「持的不是那一把」与「这构成竞争」是两个结论。** 本文件只给前者，并且给出读侧
   配对作为路径存在的证据；**是否可复现、以什么时序复现，本文件不主张**。
5. `vm->globalCount = X + 1` 是**普通写**，且写在只持一把分片的临界区里 ——
   它是这段区间的**发布点**，而两个并发建槽者各自只持自己那把时不互斥。
   这一条**只登记，未量**。

## 6. 复跑

```
python3 tools/pathruler.py           --ref 6642fed --queries-file tools/pathruler-accept.txt
python3 tools/pathruler-shardtable.py --ref a22eb704 --sites tools/pathruler-sites18.txt
python3 tools/pathruler-redcontrol.py --ref 6642fed
```

两把尺子的关系是**并列**，不是替代：把行扫尺子换掉，会让「两把尺子不一致的那一处」
重新变成一个没有仪器的分歧。行扫尺子给出 17/64，路径尺子给出 18/64，
**差的那 1 处正是手核修正过的那一处**。
