# 有界证明 · T5：把"修好了"变成一次读数（P4/P5 在修复后的树上复量）

T1 页里 P4、P5 两条缺陷的读数是 **sat**（有反例）。main 上两个修复进来之后（`c4d3db4`
"vm: the call guard reserves a whole frame, not the first 8 registers of it" 与
`55d9526` "vm: a frame fills at most VM_FRAME_REGS parameter slots, whatever the artifact asks"），
"这两条现在应该是 unsat" 是一句**推论**，不是一次读数。本页把推论换成读数：同一条命令、
同一套公理表、同一组命题，只把被 pin 的源文件换成修复后的树，然后看变体状态。

结论先写：P4 与 P5 的 asis 都从 sat 翻成 **unsat**，而修复前的守卫重新作为**红色变体**留在模型里，
读数仍是 sat。也就是说这一页能同时说两件事——"修后的守卫够用"和"修前的守卫不够用"——
而且两者出自同一个模型、同一次运行，差别只在那一行 `if`。

## 0. 边界与标签

证明的是**守卫算术**：在公理表（见 T1 页 §3）与修复后的源码内，"守卫通过却仍越界"不可满足。
不证明 C 语义、分配器行为、`func_argc` 在镜像里的合法性、以及并发下的 `reg`/`base` 竞争。
更明确地说：这不是"代码到底有没有 bug"的证明，而是"这一版守卫在这一套公理下够不够宽"的读数。

四个状态严格分开，不许合并：

    proved                : asis=unsat，且 pin 校验与 GUARD-PIN 校验都通过
    proved-conditional    : asis=unsat，但这依赖某条 asserted 公理（本页依赖 A8；A7 已降为 measured，见 §5）
    sat                   : 给出反例，反例由工具打印（MODEL 行），不是手算
    mutant-red            : 故意改坏/改回旧守卫之后仍是 sat——它证明"这一格会被抓到"，不是缺陷

标签只说**这一次运行**的事。任何一行读数都属于产生它的树；本页所有读数产生于 `d064f8d`，
另有 T1 页那批读数产生于 `f393dda`，两页的树号不能互相顶替。

## 1. 一条命令，以及它的构造

    cd /tmp/wt-t5 && ./tools/z3bounds/run_bounds_proof.sh

读数的**构造**必须一起写，否则同一个命令在不同环境下会给出不同的 rc（这一条来自具体事故：
`tools/check_line_refs.py` 在"被扫描文件缺失"与"PATH 里没有 git"两种情况下都给 rc=4，
在仓库外给 rc=2；另一位读者在另一种构造下拿到 rc=1 加 `FAILED (6 assertion(s))`，两边都"跑过"）：

    pwd                 = /tmp/wt-t5（git worktree，.git 是一个文件而不是目录）
    git rev-parse HEAD  = d064f8d21f515550a98db476bb965da6355cc69c（detached）
    git status          = 只有 M tools/z3bounds/gen_bounds_proof.py（本轮工具从 lane 树覆盖进来）
    src/ selfhost/      = 干净，未改（所以 §3 的 blob 是这棵树自己的内容）
    python（WSL 侧）    = 无 z3，只用来做 pin 校验与生成脚本
    python（Windows 侧）= /mnt/c/Python314/python.exe，z3 5.3.2，CPython 3.14.7
    生成脚本            = /mnt/c/Users/Lenovo/AppData/Local/Temp/z3probe/bounds_proof.py
                          bytes=37591  sha256=434b93d1d0508758
    git 在 PATH 上      = 是（GUARD-PIN 与 GIT-BLOB 两节都要用它）

Windows 侧解释器必须收到**Windows 路径**的脚本：把 `/mnt/c/...` 交给 `python.exe` 会得到
`can't open file '\\wsl.localhost\...': [Errno 13] Permission denied`。这一条 T1 页已记过，这里重申是因为
换了 worktree 之后路径又变了一次。

## 2. 第一个读数是 pin 检查拦下来的

在修复后的树上第一次跑，命令在写脚本之前就退出了（rc=2）：

    PIN FAILED: src/vm/vm.c:3025 pin drift

    pinned:     Value *R = t->R;
    tree  :     VM *vm = t->vm;

紧接着 4033、4067、4073、4079、4093、4095、4096、4097、4099 一路漂移——因为两个修复在
`L_CALL_FUNC` 里插入了 9 行注释与夹取。**这不是工具坏了，这是工具在工作**：pin 表记的是
"这个行号上必须是这句原文"，行号一挪，模型描述的就是另一行代码了。若 pin 只在导入时比一次、
或者按"附近有没有长得像的句子"去匹配，这次漂移会被静默吃掉，随后所有 unsat 都指向错误的位置。

重钉用的是**精确原文搜索**，不是算术加偏移：旧行号 -> 新行号由 grep 该行原文得到。

    3025->3026  4033->4034  4067->4068  4073->4074  4093->4094
    4095->4096  4096->4104  4097->4105  4099->4107  4100->4108
    4101->4109  4104->4112  4511->4519  4513->4521  4515->4523

旧 4079 那行（`+ 8` 守卫）**在新树里不存在了**，被 4080 的 `+ VM_FRAME_REGS + VM_FRAME_REGS`
取代；另外新增三个 pin：4103（夹取）、4133 与 3085（A8 的两个对齐点）。
重复原文的行必须按行号钉，不能按文本钉：

    for (int i = argc - 1; i >= 0; i--) {     在 4107 与 4535 各有一处
    t->sp--;                                  在 3915 / 4109 / 4537 各有一处

## 3. 两道防"假绿"的读数

### 3.1 GUARD-PIN：模型里的守卫到底是不是那一行

每一次运行都打印七个命题的守卫锚点与源码的对照，分四种判定：

    GUARD-PIN|P1|src/vm/vm.c:4068|model_vs_source=verbatim|line=if (fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL) {
    GUARD-PIN|P2|src/vm/vm.c:4034|model_vs_source=verbatim|line=if (!t->closure_env || ins.r2 < 0 || (size_t)ins.r2 >= im_closure_env_size(t->closure_env)) {
    GUARD-PIN|P3|src/vm/vm.c:4080|model_vs_source=conjunct|line=if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap) {
    GUARD-PIN|P4|src/vm/vm.c:4080|model_vs_source=verbatim|line=if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap) {
    GUARD-PIN|P5|src/vm/vm.c:4080|model_vs_source=verbatim|line=if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap) {
    GUARD-PIN|P6|src/vm/vm.c:3026|model_vs_source=not-a-guard-anchor|line=    VM *vm = t->vm;
    GUARD-PIN|P7|src/vm/vm.c:4074|model_vs_source=verbatim|line=if (t->sp + 1 < argc) {

三种判定各有理由：verbatim 是"pin 那行恰好就是 `if (<模型守卫>) {`"；conjunct 是"模型只取了
这一行的一个合取项"，P3 故意如此（它只谈 `frame_count`）；not-a-guard-anchor 是"这个命题的前提是一处
**缺席**"——P6 说的是执行体里没有第二处 `reg_cap` 检查，所以它锚在一行普通语句上，判定就不该是
verbatim。三者混成一个"对"字，conjunct 与 not-a-guard-anchor 这两格就会被当成 verbatim 的弱化版。
判定不通过直接 exit 2，不生成脚本。

### 3.2 GIT-BLOB：unsat 是因为守卫变强，还是因为 pin 指到了别处

每一个被 pin 的源文件还要打印它在 HEAD 里的 blob，并与 `git rev-parse HEAD:<path>` 比对：

    GIT-BLOB|path=src/vm/vm.c|blob=988892141b73715a19903f599eefa17a8d5a6b77|head=d064f8d
    GIT-BLOB|path=src/vm/vm.h|blob=8003a18d0450293d58cbc2c6b0e483336d8a5038|head=d064f8d
    GIT-BLOB|path=src/compiler/bytecode.c|blob=9edd351ac491aaa9a10095f07cf024259f3d6a08|head=d064f8d
    GIT-BLOB|path=src/compiler/bytecode.h|blob=f77f7af4e365bbcb16364c8a6493305904b46eb5|head=d064f8d
    GIT-BLOB|path=src/runtime/vm_exec_builtin.c|blob=0e49f0093d6a9f82e0d551f4f207bd799e46ccb2|head=d064f8d

这是一条针对本轮最危险假绿的读数：**sat 变 unsat** 既可能来自守卫变宽，也可能来自 pin 指向了
另一个（更严的）位置。blob 把"我看到的是这份文件"变成可核对的事实，GUARD-PIN 再把"模型描述的守卫是
这一行"钉住。两道读数都过，unsat 才能归因到守卫。

## 4. 读数

同一次运行的完整结果（17 个变体，全部逐字取自运行输出）：

    RESULT|P4|asis|unsat|0.002
    RESULT|P4|prefix_guard_plus_8|sat|0.002
    RESULT|P5|asis|unsat|0.002
    RESULT|P5|no_clamp|sat|0.003
    RESULT|P5|clamp_only_prefix_guard|sat|0.001
    RESULT|P5|declared_2000_prefix_guard|sat|0.008
    RESULT|P6|asis|sat|0.004
    RESULT|P6|candidate_guard|unsat|0.004
    SUMMARY|variants=17|pass=17|fail=0

反例由工具自己打印（`MODEL` 行，键是模型变量的名字），所以它们是读数不是手算：

    MODEL|P4|prefix_guard_plus_8|VM_FRAME_REGS=1024, param_slots=1024, root_func_count=0, t_base=2095104, t_frame_cap=2048, t_frame_count=2047, t_reg_cap=2097152, t_sp=0
    MODEL|P5|no_clamp|argc=2048, param_slots=2048, t_base=2094080, t_frame_count=2047, t_reg_cap=2097152
    MODEL|P5|clamp_only_prefix_guard|argc=1024, param_slots=1024, t_base=2095104
    MODEL|P5|declared_2000_prefix_guard|argc=2000, param_slots=2000, t_base=2095104

P6 是对照组，本轮没动它：它仍然 sat（执行体里寄存器操作数确实没有守卫），候选守卫下翻 unsat。
对照组不跟着变，说明这一轮 sat->unsat 的移动不是全局参数被拧了。

### 4.1 第二次运行：测量树前移到 `029bea6`

第一次测量之后 main 又前进了一格（`d064f8d` -> `029bea6`，增量只有 `docs/AUDIT.md` 33 行）。
按"测量树里进了别人的改动就要复量"的规矩，同一棵测量树前移到 `029bea6` 再跑一次：

    git diff --stat d064f8d 029bea6        -> docs/AUDIT.md | 33 ++++++
    被 pin 的 5 个文件 blob 逐个比对        -> src/vm/vm.c 98889214 / vm.h 8003a18d /
                                             bytecode.c 9edd351a / bytecode.h f77f7af4 /
                                             vm_exec_builtin.c 0e49f009，与 d064f8d 相同
    生成脚本                               -> bytes=37591  sha256=434b93d1d0508758（同）
    读数                                   -> P4 asis=unsat / prefix_guard_plus_8=sat,
                                             P5 asis=unsat / no_clamp=sat /
                                             clamp_only_prefix_guard=sat / declared_2000_prefix_guard=sat,
                                             SUMMARY|variants=17|pass=17|fail=0

两次运行里只有求解耗时不同（0.002 与 0.003 之类），**耗时不进结论**。这也说明一件事：
"读数属于产生它的树"不等于"每换一个树号都要重跑"——当被 pin 的文件逐字节相同，第二次运行
是**核对**，不是新事实；页里写两个树号，是为了让后来的读者能判断哪一次是哪一棵树。

## 5. 机制：为什么 unsat，而不是"因为它修了"

设 `R = t->reg_cap = 2097152`（`VM_THREAD_REG_COUNT = 2048 * 1024`），`F = VM_FRAME_REGS = 1024`。

修前的守卫：`base + F + 8 >= R` 即拒绝。所以**通过**守卫的 base 满足 `base + 1032 < R`；
在帧对齐（A8：`base % 1024 == 0`）下，最深的可达 base 是 `2046 * 1024 = 2095104`。
命题要的是 `base + F + param_slots < R`。取 A7 的上界 `param_slots = F = 1024`，
就要求 `base < R - 2048 = 2095104`——而最深的可达 base 恰好等于 2095104，
于是子帧最后一个槽落在 `reg[2095104 + 1024] = reg[2097152] = reg[reg_cap]`，**越界一个 Value**。
这就是 `MODEL|P4|prefix_guard_plus_8` 打印的那个点：`t_base=2095104, param_slots=1024`。
差值 1016 个槽（`F - 8`）就是旧守卫的松量缺口。

修后的守卫：`base + F + F >= R` 即拒绝。最深的被准入 base 变成 `2045 * 1024 = 2094080`，
留给子帧的空间是 `2097152 - (2094080 + 1024 + 1024) = 1024` 个槽，正好是 A7 的上界，
所以 `base + F + param_slots < R` 在 A7 下不可满足——**asis=unsat**。
换句话说，修复不是"把数字调大"，是把预留量从"子帧外的 8 个寄存器"改成"整帧宽 1024"，
于是准入条件与命题的写域假设第一次对齐。

第二半（夹取）是另一件事：即使守卫已经整帧预留，`param_slots = max(func_argc[fidx], argc)`
仍可能大于 1024（`func_argc` 从镜像里读回，未经校验，`src/runtime/vm_exec_builtin.c:83`）。[obs: 029bea6]
4103 行的 `if (param_slots > VM_FRAME_REGS) param_slots = VM_FRAME_REGS;` 把这个上界变成代码事实，
A7 因此从 asserted 降为 **measured**。`MODEL|P5|no_clamp` 就是拿掉这条夹取之后的情形：
同一守卫（`t_base=2094080`）下 `argc=2048` 直接给出 `param_slots=2048`，越界 1024 个槽。

## 6. 红色的那一半留在模型里

只报"现在是 unsat"的模型，下一次有人把守卫改回 `+ 8` 时会安静地通过。所以 P4 的
`prefix_guard_plus_8` 变体把旧守卫原样放回模型，其余一切不变，读数是 **sat**：

    RESULT|P4|asis|unsat|0.002
    RESULT|P4|prefix_guard_plus_8|sat|0.002

两行的差别只有那一行 `if`。这是本页最强的一句归因：sat/unsat 的翻转由守卫本身造成，
不是由公理、不是由变量域、不是由求解器版本。同理 P5 的 `clamp_only_prefix_guard`
（旧守卫 + 夹取）仍是 sat，`declared_2000_prefix_guard`（旧守卫 + `func_argc=2000`）也是 sat。

## 7. 两个修复互不替代（在修复后的树上依然成立）

T1 页的 `guard_fix_only=sat` 说明"只修守卫不够"。在修复后的树上这条关系换了形状，但没消失：

    P4/asis                     = unsat   （整帧预留 + 夹取）
    P5/no_clamp                 = sat     （整帧预留，但没有夹取）
    P5/clamp_only_prefix_guard  = sat     （有夹取，但守卫还是旧松量）

也就是说两个修复各自覆盖一半：守卫管"子帧有没有地方放"，夹取管"要放多少"。任取一半，
另一个变体就红。这一格是这一轮最该留给后来人的：**"两个缺陷其实是同一个"这类方便说法，
要用两个各自缺一半的变体去打，而不是用一句解释去打。**

## 8. 与 T1 页的关系（读数不跨树顶替）

T1 页 §4 的 P4、P5 读数是**修复前**的读数，产生于 `f393dda`（`/tmp/wt-z3`），
那时 `src/vm/vm.c` 的 blob 是 `91a495cbb7d524dd3a9c200384167d820fcbf7aa`，
守卫是 `src/vm/vm.c:4079  if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap) {`（这一行读于修复前的树）[obs: 029bea6]，
`param_slots` 那行在 4095 且没有夹取。本页的读数产生于 `d064f8d`，blob 是
`988892141b73715a19903f599eefa17a8d5a6b77`。同一命题、同一公理表、不同树，所以：

    旧树 asis = sat（缺陷）          新树 asis = unsat（修复够用）
    旧树 fix_reserve_frame = unsat   新树 prefix_guard_plus_8 = sat（旧守卫作为红色变体）

T1 页不改写：它记录的是它那棵树上的事实。要判断"现在怎么样"，看这一页。
两页的生成脚本 sha256 也不同（T1：`428494cc4c0c7829`；本页：`434b93d1d0508758`），
因为 pin 表与命题文本都变了——脚本哈希相同而读数不同，本身就是该被怀疑的情况。

## 9. 这一页没有证明什么

- 不是**代码级证明**：模型是 `vm.c` 的 C 子集转录（守卫、写入点、寄存器文件规模），
  不是把 `vm.c` 交给某个 C 前端。转录与源码的一致性靠 pin + GUARD-PIN + GIT-BLOB 三道读数，
  靠的是"人盯着这几行"，不是形式化。
- A8（`base` 非负且 1024 对齐）仍是 **asserted**。P4/P5 的 unsat 都是 proved-conditional 于它。
  如果 `base` 可以不对齐，最深可达 base 会比 2094080 更深，结论就要重算。
- 没进 CTest：门禁跑的地方没有 z3（WSL 侧 `python3` 无 z3，只有 Windows 解释器有），
  把这条注册进去会得到 SKIP。**skip 不是 pass**，所以这条命令暂时是 lane 内的工具，不是契约。
- 没有覆盖 host 侧的同名守卫（`src/vm/vm.c:2677` 与 2685-2686 也拿到了同样两个修复）[obs: 029bea6]，
  本页只复量执行体的那一处；host 侧要另开一轮。

## 10. 树号与改动清单

- 测量树：`d064f8d`（detached worktree `/tmp/wt-t5`，locked）。
- 写入树：`/tmp/wt-z3c`，分支 `z3bounds-t1`。
- 改：`tools/z3bounds/gen_bounds_proof.py`（pin 表重钉到修复后的行、A7 由 asserted 降为 measured、
  P4/P5 重写并保留旧守卫作为红色变体、新增 GUARD-PIN 与 GIT-BLOB 两条读数）、本页。
- 未改：`src/`、`selfhost/`、门禁脚本与其 TARGETS、测试数量上限。
- 复量方式：把本页所在树的生成器覆盖进测量树，跑同一条命令，然后把测量树的那个文件
  `git checkout --` 还原（`git status` 在测量前后各看一次，§1 的构造里已写）。
