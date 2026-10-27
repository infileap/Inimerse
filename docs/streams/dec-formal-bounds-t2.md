# 有界证明 T2：字节码栈平衡的可抽性读数（执行器侧）

本页是 T1（`docs/streams/dec-formal-bounds.md`，VM 索引守卫的 SMT 有界证明）的姊妹页。T1 证的是索引算术，T2 要先回答一个更前置的问题：**栈平衡的规则到底写在哪里、能不能机械抽出来**。
本页只报抽取器的读数与已知盲区，不含栈平衡命题的证明——那一层要等次腿（emit 侧对账）落地后再写。

## 0. 边界与标签

沿用 T1 页的约定：`measured` = 本页命令可复现；`proved` = 求解器 unsat；`proved-conditional` = 模型内 sat 但见证要踩 `asserted` 公理或可达性假设；`asserted` = 写下来但本轮没重新读代码验证。本页没有 z3 参与，所以只有 `measured` 与 `asserted`。
定位符约定同 T1：`path:line` 只有在同一行给出该行逐字原文时才算 pin，否则标 `unverified-locator`。

## 1. 一条命令

    python3 tools/z3bounds/extract_stack_effects.py

退出码：0 = 自检通过；1 = 自检不通过（抽取器自己数出的计数与独立 grep 基线不一致）；2 = 这棵树不像抽取器描述的那棵（找不到 dispatch 行）。
末行读数必须形如 `SUMMARY|ops_total=67|with_effect=10|no_effect=57|unparsed_expr=1|selfcheck=PASS`（measured）。

## 2. 抽取法：两层跳转，不是一层

实测：执行器不是 `case OP_X: { ... }`，而是**跳转派发**——

    src/vm/vm.c:3161        case OP_MOV: goto L_MOV;
    src/vm/vm.c:3235        L_MOV:

所以抽取要走两步：`case OP_X: goto L_Y;` 建 op→label 映射（实测 67 条），再以 `L_Y:` 块为 op 体，收集块内 `t->sp` 写点与前置守卫。
块的终止边界取三者的最早者：下一个 label 行、下一个 `case OP_`、缩进小于 label 的 `}`。这条边界不是装饰——第一版没加它，最后一个 label 的块一直吃到文件尾，把 `src/vm/vm.c:4806` 与 `:4983` 的复位写进了某个 op 的效应里，读数因此虚高。

## 3. 四个数（本轮要求先印出来的）

    EXEC|ops_total=67|ops_with_sp_effect=10|ops_no_effect_but_declared=57|reads=42

- `ops_total=67`：执行器派发的 op 数。**注意这与 114 不同**：114 是全文件 `case OP_` 的行数，其中 47 行属于反汇编器那个 switch（`src/vm/vm.c:5312` 起，形如 `case OP_MOV:            snprintf(out, outsz, "MOV r%d, r%d", ins->r1, ins->r2); return;`）。我上一轮报 114 是抓全文件，属于口径错，这里更正为 67 + 47。
- `ops_with_sp_effect=10`：有 `t->sp` 写点的 op：

      OP_CALL_BUILTIN  dec x1  guard 3912   span 3857-3920
      OP_CALL_FUNC     dec x1  guard 4073   span 4060-4115
      OP_MAKE_FUNC     dec x1  guard 4043   span 4039-4055
      OP_NEW_ARRAY     dec x2  guard 3625,3635
      OP_NEW_DICT      dec x2  guard 3653,3663
      OP_NEW_SET       dec x2  guard none,4289
      OP_PUSH_REG      inc x5  guard 3818,3827,3833,3839,3845
      OP_RETURN        =expr x1 guard none
      OP_SET_INTERVAL  dec x4  guard 4317 x4
      OP_THREAD_START  dec x5  guard 4463,4475 x4

- `ops_no_effect_but_declared=57`：没有 `t->sp` 写点的 op，**逐个点名**：48 个 `register-only`（只写 `R[]`/`FR[]`，不碰栈）、1 个 `delegates-to=L_CALL_FUNC`（`OP_CALL_VALUE`，块内 `goto L_CALL_FUNC;`）、1 个 `read-only`（`OP_TRY_START`，只读 `t->sp`）、7 个**手工命名**（下表，附证据行）、0 个 `unclassified`。

      OP_DECLARE    limits-only                    src/vm/vm.c:4184
      OP_HALT       terminates-run                 src/vm/vm.c:4022
      OP_STOP       terminates-run                 src/vm/vm.c:4019
      OP_JUMP       control-flow-only              src/vm/vm.c:3797
      OP_LOCK       mutex-only                     src/vm/vm.c:4700
      OP_TRY_END    exception-depth-only           src/vm/vm.c:4436
      OP_YIELD      task-yield-only                src/vm/vm.c:4027

这七条的原文（各取一行，全部逐字）：

    src/vm/vm.c:4022        t->running = false;
    src/vm/vm.c:4019        t->running = false;
    src/vm/vm.c:3797        t->ip = ins.r2;
    src/vm/vm.c:4436        if (t->exc_depth > 0) t->exc_depth--;
    src/vm/vm.c:4027        if (t->is_task && t->fiber_sched) SwitchToFiber(t->fiber_sched);

只有这五条能在同一行找到逐字的语句；`OP_DECLARE` 与 `OP_LOCK` 的判定来自整块（4184-4195 只碰 `vm->limit_*`；4700-4715 只碰互斥量），**没有单行可引**，所以它们的理由是 `asserted`，不是 `measured`。这是本页最该被挑的一处，先自己挑出来。

## 4. 自检：我自己那三个基线有两个是错的

上一轮我报给量方的基线是 `19 / 5 / 7`。抽取器跑出来是 `20 / 5 / 3`，逐条查过，**基线错，抽取器对**：

- `dec`：19 → **20**。第 20 处是 `src/vm/vm.c:4044` 的 `Value *captured = &t->stack[t->sp--];`。我原来那条 grep 把 `t->sp[^;]*` 归一化后 `t->sp--` 与 `t->sp--]` 成了两种形状，去重时被算成 19 + 1，而我只看主形状。**归一化去重不是计数**。
- `assign_literal`：7 → **3**。7 里有 4 处是 `nt->sp = -1;`——`nt->sp` 含子串 `t->sp`，正则不加词边界就一起数了。加 `(?<![A-Za-z0-9_])` 后只剩 `src/vm/vm.c:3083`、`:4806`、`:4983` 三处 `t->sp = -1;`。
- `inc`：5，只有 `OP_PUSH_REG` 一处 op 内的五个分支（字符串/数组/字典/集合/函数各一个 `t->sp++`），前后一致。

现在的自检读数（measured，三条全 PASS）：

    SELFCHECK|dec=20|baseline=20|PASS
    SELFCHECK|inc=5|baseline=5|PASS
    SELFCHECK|assign_literal=3|baseline=3|PASS
    SELFCHECK|in_body:dec=18|inc=5|assign_literal=0

## 5. `unparsed` 基线：先报，不修到 0

    unparsed=0    unparsed-expr=1
    UNPARSED-EXPR|src/vm/vm.c:4121|t->sp = t->frame_sp[t->frame_count - 1];

这一条**必须原样留着**：它是「返回时把 `sp` 恢复到调用者保存的那一格」，右值是表达式不是常量。把它写成「变更 +N」就等于替 T2 证明了「`frame_sp` 里存的一定是合法栈顶」——那正是命题的一半。它进 `unparsed-expr` 等当面讨论，不进效应表。
另外，五处 `t->sp` 写点不在任何 op 体内，抽取器把它们单独印出来而不是丢掉：

    OUTSIDE|src/vm/vm.c:3083|assign_literal|t->sp = -1;
    OUTSIDE|src/vm/vm.c:4806|assign_literal|t->ip = 0; t->sp = -1; t->base = 0; t->frame_count = 0;
    OUTSIDE|src/vm/vm.c:4845|dec|for (int i = argc - 1; i >= 0; i--) { value_move(&nt->R[i + 1], &t->stack[t->sp]); t->sp--; }
    OUTSIDE|src/vm/vm.c:4885|dec|（同上，对象换成 nt2）
    OUTSIDE|src/vm/vm.c:4983|assign_literal|t->sp = -1;

⇒ 18 + 2 = 20，全部对上。这些是复位路径与线程启动时拷贝实参的循环，不归任何 op 的效应。

## 6. 「不存在栈效应表」这条本身是读数

    grep -rn 'STACK_EFFECT\|stack_effect\|op_sp' src/vm src/compiler   ⇒ 0 命中
    grep -rn 'stack_depth\|max_stack' src/compiler                        ⇒ 0 命中

⇒ 栈平衡的规则不在编译器声明里，只在执行器的命令式代码里（本页的 10 个 op + 5 处体外写点）。这也解释了为什么它一直没人证过：没有表可读，只能从源码抽。
同时 `emit()` 只声明 `(op, r1, r2, r3)`（`src/compiler/compiler.c:434  static void emit(Bytecode *bc, OpCode op, int r1, int r2, int r3)`），指令落盘唯一处在 `src/compiler/bytecode.c:62  bc->code[bc->count++] = ins;`——所以「从 emit 点抽栈效应」这条路在源码层面就不存在。

## 7. 次腿（emit 侧对账）与本页没做的事

- 次腿未做：每个 op 报「编译器发出的参数个数」与「执行器消费的个数」。样板已定：`src/vm/vm.c:4061  int fidx = (ins.op == OP_CALL_VALUE) ? call_fidx_override : ins.r1, res = ins.r2, argc = ins.r3;` ↔ 块内 `for (int i = argc - 1; i >= 0; i--) { value_move(&FR[i + 1], &t->stack[t->sp]); t->sp--; }`。
- 本页不证任何栈平衡命题：`OP_PUSH_REG` 的 `if (t->sp >= 1023) continue;` 与 `t->sp++` 是一对，但「所有路径都先测后写」属于 T2 的命题层，要等次腿给出参数计数之后才能建模。
- 接缝（已写进 T1 页，这里复述）：`ins.r3` 与 `func_argc` 都没有上界，栈平衡的公理表不能悄悄假设「实参个数合理」，必须显式写成公理。

## 8. 树号与改动清单

- 树号：1042aed（worktree /tmp/wt-z3c，已 `git worktree lock`）；本页与抽取器同批提交。
- 新增：`tools/z3bounds/extract_stack_effects.py`、本页。未改 `src/`、`selfhost/`、门禁脚本与 CTest 计数。
- 复现：`python3 tools/z3bounds/extract_stack_effects.py` 末行 `SUMMARY` 与 §3、§4 的读数逐字相同；退出码 0。
