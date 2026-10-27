# 有界证明 T2：字节码栈平衡——可抽性读数、判据形状与次腿对账

本页是 T1（`docs/streams/dec-formal-bounds.md`，VM 索引守卫的 SMT 有界证明）的姊妹页。
T1 证的是索引算术；T2 要先回答两个更前置的问题：**栈平衡的规则写在哪里、能不能机械抽出来**，
以及**编译器发出的 op 是否恰好都有执行者、实参是否被读到**。
本页报读数与判据形状，**不证栈平衡命题**——那一层要等下面第 7 节的公理被接受之后才成立。

## 0. 边界与标签

沿用 T1 页约定：`measured` = 本页命令可复现；`proved` = 求解器 unsat；
`proved-conditional` = 模型内 sat 但见证要踩 `asserted` 公理或可达性假设；`asserted` = 写下来但本轮没重新读代码验证。
本页只有 `measured` 与 `asserted`（没有 z3 参与），但第 7 节给出 T2 将来的结论形态是 `proved-conditional`。

定位符约定同 T1：`path:line` 只有在同一行给出该行逐字原文时才算 pin，否则标 `unverified-locator`。

## 1. 两条命令

    python3 tools/z3bounds/extract_stack_effects.py
    python3 tools/z3bounds/reconcile_emit_dispatch.py

退出码（两个工具同义）：0 = 自检/对照通过；1 = 判据自己先不一致（读数照印，不吞）；2 = 这棵树不像工具描述的那棵。
末行读数必须形如 `SUMMARY|ops_total=67|with_effect=10|no_effect=57|unparsed_expr=1|selfcheck=PASS`（measured）。

## 2. 抽取法：两层跳转，不是一层

实测：执行器不是 `case OP_X: { ... }`，而是**跳转派发**：

    src/vm/vm.c:3160        switch (ins.op) {
    src/vm/vm.c:3161        case OP_MOV: goto L_MOV;
    src/vm/vm.c:3235        L_MOV:

而同一文件里还有**另一个** `case OP_` 世界——反汇编器：

    src/vm/vm.c:5311        switch (ins->op) {
    src/vm/vm.c:5312        case OP_MOV:            snprintf(out, outsz, "MOV r%d, r%d", ins->r1, ins->r2); return;

所以 `grep -c 'case OP_'` = **114** 是**两种用途的并集**，而执行器只派发 **67** 个 op。
这条读数现在由工具自己印出来，重推 114 的人会被它挡住：

    SWITCHES|dispatch=1@3160..3232|dispatch_cases=67|head=switch (ins.op) {
    SWITCHES|disasm=1@5311..5368|disasm_cases=47|head=switch (ins->op) {
    SWITCHES|ops_total=67|labels=67|case_OP_lines=114|note=two_worlds:grep_case_OP_is_dispatch_plus_disasm

块的终止边界取三者的最早者：下一个 label 行、下一个 `case OP_`、缩进小于 label 的 `}`。
这条边界不是装饰——第一版没加它，最后一个 label 的块一直吃到文件尾，
把 `src/vm/vm.c:4806` 与 `:4983` 的复位写进了某个 op 的效应里，读数因此虚高。

## 3. 判据的形状：先印规则，再印读数

读数的可信度不在数字，在**判据的形状**。同一类错误我已经犯过两次，两个方向：

- 方向一（去重吃掉真形态）：`t->sp--` 与 `t->sp--]` 被旧归一化当成两种形状去重 ⇒ 少数（19 而非 20）。
- 方向二（子串吃进假形态）：`nt->sp = -1;` 不加词边界就被 `t->sp` 匹配吃进 ⇒ 多数（7 而非 3）。

同一个病在次腿又出现一次：`OP_POP_REG` 只出现在 `src/compiler/bytecode.h:20` 的注释里
（`/* 29 is a hole: OP_POP_REG used to sit here.  The numbering after it is`），
不做注释剥离就会报「编译得出、执行不了」——最危险的那一类结论。**形状没印出来的读数，读者无法复核。**

所以工具现在先印判据：

    NORMALIZE|dedupe=line-identity(each matching line counted once,no shape folding)|word-boundary=yes|patterns=dec:t->sp--,inc:t->sp++,assign:t->sp=<rhs>,read:t->sp

并配两条**负对照**（比自检更硬：自检只证明「和我手数的一致」，负对照证明「判据不因无关改名而漂」）：

    NEGCTL|unrelated-rename|nt->sp=>nnt->sp|dec=20|inc=5|assign_literal=3|unchanged=YES
    NEGCTL|token-rename|t->sp--=>t->spX--|dec=0|expected_drop=20|drop=20|name_exact=YES
    NEGCTL|decoy-sites|nt->sp_assign=4|counted_as_t->sp=0|matcher=word-bounded

第二条要说明一句：把**真形态**改名成 `spX` 之后，正确判据的读数**必须减少**且减幅恰好等于改名处数（20）；
若读数不变，说明匹配器在按 `->sp` 前缀匹配、把 `spX` 也算成了 `sp`——那才是词边界没修对。
所以负对照分两类：无关改名「必须不变」，真形态改名「必须按点数减少」。

## 4. 四个数

    EXEC|ops_total=67|ops_with_sp_effect=10|ops_no_effect_but_declared=57|reads=42
    REASONS|register-only=48|delegates=1|read-only=1|named=7|unclassified=0

- `ops_with_sp_effect=10`：CALL_BUILTIN dec1(guard 3912)、CALL_FUNC dec1(guard 4073)、MAKE_FUNC dec1(guard 4043)、
  NEW_ARRAY dec2(3625,3635)、NEW_DICT dec2(3653,3663)、NEW_SET dec2(none,4289)、
  PUSH_REG inc5(3818,3827,3833,3839,3845)、RETURN =expr1、SET_INTERVAL dec4(4317)、THREAD_START dec5(4463,4475)。
- `ops_no_effect_but_declared=57` 逐个点名，没有一条留成「不知道」：register-only 48、委托 1、只读 1、具名 7、**unclassified 0**。
  具名七条：DECLARE limits-only(4184)、HALT terminates-run(4022)、STOP terminates-run(4019)、
  JUMP control-flow-only(3797)、LOCK mutex-only(4700)、TRY_END exception-depth-only(4436)、YIELD task-yield-only(4027)。

## 5. 两条 asserted 升成 measured：穷举，而不是引用

`OP_DECLARE` 与 `OP_LOCK` 没有单行可引，但要证的是**「这一段不碰 `t->sp`」**，
那是**穷举**命题，证据是**写点全集**，不是一行原文：

    NO-SP|src/vm/vm.c:4184..4195|writes=vm->limit_inst,vm->limit_mem,vm->limit_threads,vm->limit_time,vm->limit_vram|locals=kind,v|sp_writes=0|source=extracted
    NO-SP|src/vm/vm.c:4700..4715|writes=vm->mutex_count|locals=cs,midx|sp_writes=0|source=extracted

`sp_writes=0` 加写点全集，就是「不改栈」的证据形态。⇒ 七条里 5 条有逐字语句可引，
2 条由写点全集支撑，`asserted` 只剩第 7 节那条真正的假设。

## 6. 自检：我自己那三个基线有两个是错的

上轮报的 `19 / 5 / 7`，工具跑出 `20 / 5 / 3`，逐条查过：**基线错，工具对**（原因见第 3 节）。
现在三条全 PASS：

    SELFCHECK|dec=20|baseline=20|PASS
    SELFCHECK|inc=5|baseline=5|PASS
    SELFCHECK|assign_literal=3|baseline=3|PASS
    SELFCHECK|in_body:dec=18|inc=5|assign_literal=0

## 7. unparsed 基线、A-FRAME 公理，与 T2 将来的结论强度

本节与下一节所有缩进块都是树 `d01f8db` 上的工具输出；每块紧上方那一句散文认领它的树号，块内不加 marker。（早先在 `ba075d0` 上取过同样的读数，`git diff --stat d01f8db HEAD -- src selfhost` 为空，所以两棵树上这些输出逐字相同；参考树写 `d01f8db` 不欠账。）

本节（`## 7.` 到 `## 8.` 之间）共 **12 行缩进、6 个块**——这个数可核，但核对命令必须写成**行内**，写进缩进块就会被它自己数进去：`awk '/^## 7\./{f=1;next}/^## 8\./{f=0}f&&/^    [^ ]/{n++}END{print n}' docs/streams/dec-formal-bounds-t2.md` ⇒ 12。跨树复用读数的唯一证据是 `git diff --stat d01f8db HEAD -- src selfhost` **输出为空**（不是「我记得没改」）——所以上一条「两棵树共用同一个树号」是测出来的，不是省事的写法。

    unparsed=0    unparsed-expr=1
    UNPARSED-EXPR|src/vm/vm.c:4121|t->sp = t->frame_sp[t->frame_count - 1];

这一条**必须原样留着**：它是「返回时把 `sp` 恢复到调用者保存的那一格」，右值是表达式。
折算成常量就等于替 T2 证明了「`frame_sp` 里存的一定是合法栈顶」——那正是命题的一半。

因此它不以 `unparsed` 收尾，而以**显式公理**进入模型：

    A-FRAME:  ∀i. -1 <= frame_sp[i] <= 1023      [写点 measured；下界 -1 由写点量出，上界 1023 仍只靠 A4（asserted）]

**上界那一半仍然靠 A4**：写点枚举能证明「存进帧槽的就是 `t->sp`」，给不出 `sp <= 1023` 这一步；所以这条公理是半 measured、半 asserted，T2 只在 A4 成立时成立——别因为「下界现在对了」就把结论标签升成 `proved`。

枚举用同一个工具、同一条命令（词边界 `frame_sp`；读数于树 `d01f8db`）：

    FRAME-SP-BASELINE|lines=16|occurrences=16|classified=16|covered=YES|matcher=word-bounded
    FRAME-SP-SUMMARY|element_writes=2|allocs=6|frees=5|decls=1|reads=1|comments=1|other=0
    FRAME-SP-WRITE|src/vm/vm.c:2689|stores=t->sp
    FRAME-SP-WRITE|src/vm/vm.c:4104|stores=t->sp

元素写点总共**两处**，都存 `t->sp`：
`src/vm/vm.c:2689  t->frame_sp[t->frame_count - 1] = t->sp;`（宿主侧 `vm_call_func_at`：先 `frame_count++`，再写同一格）[obs: d01f8db]；
`src/vm/vm.c:4104  t->frame_sp[t->frame_count - 1] = t->sp;   /* save sp AFTER popping args */`（执行器 `L_CALL_FUNC`：弹完 argc 个实参之后）[obs: d01f8db]。
其余 14 处是 6 次 `calloc`、5 次 `free`、1 条声明 `src/vm/vm.h:107  int *frame_sp;   /* save sp at call for correct return */`、1 次读 `src/vm/vm.c:4121  t->sp = t->frame_sp[t->frame_count - 1];`、1 条注释 `src/vm/vm.c:4103  /* frame_sp uses the same pre-increment frame slot as the other frame metadata. */`。[obs: d01f8db]

**这个枚举否掉了我上一版公理的 0 下界**：两处写点存的都是 `t->sp`，所以帧槽的值域就是 `sp` 的值域，而 `sp` 可达 -1（下两行产生于 `d01f8db`）：

    FRAME-SP-RANGE|element_writes=2|every_write_stores_t->sp=YES|sp_range=[-1,1023]|source=axiom A4|axiom_as_stated=0 <= frame_sp[i] < reg_cap|verdict=lower_bound_too_strong
    FRAME-SP-WITNESS|path=src/vm/vm.c:4104|argc=1|sp_before=0|guard_at_4073=if (t->sp + 1 < argc) return 0|guard_refuses=NO(1<1 false)|after_one_pop=sp -1|frame_slot=-1|status=reachable_on_handcrafted_bytecode

证人：`sp=0`、`argc=1`；4104 之前的守卫 `src/vm/vm.c:4073  if (t->sp + 1 < argc) {` 判 `sp + 1 < argc` 即 `1 < 1`，假，于是放行；4100–4101（`value_move(&FR[i + 1], &stack[t->sp]);` 与 `t->sp--;`）弹掉一个实参后 `sp--` 到 -1，4104 把 -1 存进帧槽；返回时 `src/vm/vm.c:4121` 恢复 `sp = -1`（空栈，合法）。[obs: d01f8db]
**太强的公理不是保守，是让结论变空**：`0 <= frame_sp[i]` 会把「空栈返回」这条正常路径排除在模型之外，T2 若靠它证成，证的是一个到不了的状态。

负对照两条，方向各自写清（「不变」和「变了」都可能说明匹配器坏了；下两行产生于 `d01f8db`）：

    FRAME-SP-NEGCTL|rename|frame_sp=>frameSp|element_writes=0|expected_drop=2|drop=2|name_exact=YES
    FRAME-SP-NEGCTL|decoy|frameSp[]=|decoy_sites=1|element_writes=2|unchanged=YES|counted_as=0|matcher=word-bounded

真形态改名 ⇒ 写点数**必须掉到 0**（掉到 0 才说明匹配是词边界而非前缀）；插入 `frameSp[] =` 这种错误形状 ⇒ 读数**必须不变**。两条都过。
诱饵那条的 `counted_as=0` 是给读者看的判据：它说明那行**根本没被当作 `frame_sp` 读过**，而不是「读了但算作 0」——缺这个字段，两种情形在读数上长得一模一样。

于是 T2 将来的结论形状不变，但依赖链换成了 A4：

    proved-conditional：若 A4（-1 <= sp <= 1023）成立，则每次返回后 sp 落回合法栈顶。

这与 T1 的 P4 是同一个形状（P4 依赖 A7 `0<=param_slots<=1024` 与 A8 `base 对齐`）：两个靶子的结论强度一致，不存在一处很硬一处很软。
另五处写点在体外，单独印不丢：`3083`、`4806`（复位）、`4845`、`4885`（线程启动拷实参）、`4983`。
18 体内 + 2 体外 dec = 20，对得上。

## 8. 次腿：emit 侧对账（集合差在前，参数在后）

    EMIT|ops=67|dispatched=67|emit_sites=114|files=src/compiler/*.{c,h}
    EMIT-DIFF|emitted_not_dispatched=0|dispatched_not_emitted=0
    EXCLUDED|OP_POP_REG|prov=src/compiler/bytecode.h:20|why=only_in_comment|text=/* 29 is a hole: OP_POP_REG used to sit here.  The numbering after it is
    MIRROR|enum_ops=47|not_in_dispatch=-|absent_from_mirror=20
    ARGC-SUMMARY|checked=67|suspicious=0|note=r3=third_emit_arg_vs_ins.r3_reads

- **集合差双向都是 0**：编译器能发出的 67 个 op 与执行器派发的 67 个 op 逐一对应，
  没有「编出来没人执行」，也没有「派发了没人编」的死 op。这正是 `ops_total=67` 的邻域问题，
  而它比栈平衡更接近真事故——编出来没人执行 = 静默丢弃。
- 第 68 个命中是注释里的 `OP_POP_REG`（`bytecode.h:20`），剥离注释后集合才是 67。
- `MIRROR`：`selfhost/` 镜像编译器的 op 枚举有 **47** 个，全部在派发集合里；
  反过来有 **20** 个派发 op 不在镜像枚举中——这是**读数**，不是缺陷判定（镜像走内建/合成路径与否由镜像车道判断）。
- 参数对账（第三段）：`r3_reads`（体内 `ins.r3` 读点，**沿 `goto` 委托传递**）对比 emit 的第三个实参。
  `suspicious=0` 的判定分两型：执行器读 `r3` 但编译端永远喂字面 0；编译端喂了非零 `r3` 而执行器从不读。

**这一段我自己先错三次，全部记在此处留证**：

1. 注释剥离函数把 `//` 换成**一个**空格（不是两个），长度不再保持，切片索引整体漂移 ⇒ 凭空造出 10 个「可疑」op。
2. 参数下标差一位（`a[3]` 是 `r2` 不是 `r3`）⇒ 又造出一批。
3. 没有沿委托走：`OP_CALL_VALUE` 的块体只是 `goto L_CALL_FUNC;`，argc 在目标块里读，
   只数首块就会报「执行器忽略 r3」——恰好冤枉了唯一会转发 argc 的那个 op。

修正后 `suspicious=0`。**先接受工具的错，才有资格报它的读数**；这也是判据形状同一课。

## 9. 「不存在栈效应表」这条本身是读数

    grep -rn 'STACK_EFFECT|stack_effect|op_sp' src/vm src/compiler  ⇒ 0 命中
    grep -rn 'stack_depth|max_stack' src/compiler                   ⇒ 0 命中

⇒ 栈平衡的规则不在编译器声明里，只在执行器的命令式代码里（本页 10 个 op + 5 处体外写点）。
这也解释了为什么它一直没人证过：没有表可读，只能从源码抽。
且 `src/compiler/compiler.c:434  static void emit(Bytecode *bc, OpCode op, int r1, int r2, int r3)` 与
唯一落盘点 `src/compiler/bytecode.c:62  bc->code[bc->count++] = ins;` 说明：
从 emit 点造效应表这条路在源码层不存在。

## 10. 树号、改动清单、没做的事

- 树号：1042aed 为基线；本轮读数在分支 `z3bounds-t1` 取得（该分支先后 rebase 到 `4b41f59`、`dfc5957`、`9a3e53a`）（worktree `/tmp/wt-z3c`，已 `git worktree lock`），第 7 节 `frame_sp` 那批读数产生于树 `d01f8db`。本页所在提交号不写死，用 `git log -1 -- docs/streams/dec-formal-bounds-t2.md` 取——页内自指会随提交漂移。
- 页内 `[obs: <sha>]` 标记命名的是**这批读数被产生的那棵树**，不是合并时踩在哪棵树上；否则标记会随合并动作漂移，等于没标。
- 新增：`tools/z3bounds/extract_stack_effects.py`、`tools/z3bounds/reconcile_emit_dispatch.py`、本页。
  未改 `src/`、`selfhost/`、门禁脚本与 CTest 计数；本页不引门禁 TARGET 文件名，避免把本页纳入比对输入集。
- 姊妹页：T3 的探针表（三前端读数从源码重生成）见 `docs/streams/dec-formal-bounds-t3.md`。
- 没做：栈平衡命题的建模与求解（等 A-FRAME 的上界那一半被接受）；`src/vm/vm.c:3025` 起的执行器外框（线程/任务切换路径）的枚举；
  A4（`sp` 自身值域）的写入点全集——A-FRAME 的写点已枚举（2 处、都存 `t->sp`），仍标 asserted 的是 A4 本身。
- 自查命令（改被引文件时先跑）：`git grep -nE 'extract_stack_effects[.]py:' -- docs/ | wc -l` ⇒ 期望 0；非 0 就要改指并搬引文。  模式里拿 `[.]` 写那个点是刻意的：直接写点号，这一行自己就会成为命中，命令永远报 1——判据不能把自己算进去。

  同族还有两格，一并记在这里，免得下一个人只记住模式、忘了原因：

  1. **命令自己出现在被扫文件里**：脚注这一行写在 `docs/streams/dec-formal-bounds-t2.md`，而它扫的就是 `docs/`；
  2. **判据的输入包含判据自身**：模式与「期望 0」住在同一个文件里。

  通用修法只有两条：把模式写成字符类（`[.]`、`[a-z]`），或把期望写成「≤ 已知自命中数」并把那个自命中数与读数**一起印出来**。**不要靠人记得**——靠记忆的判据，换一个人就失效。
- 复现：两条命令的末行 `SUMMARY` 与第 2、3、4、5、8 节读数逐字相同，退出码 0。
