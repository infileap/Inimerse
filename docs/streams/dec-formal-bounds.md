# 有界证明：VM 索引守卫（z3）

本页把 `src/vm/vm.c` 里三处"只能用点测看"的索引守卫，换成**可重跑、带变异**的 SMT 有界证明。
每一个"证明"都配一个把被证对象打断的变异，读数必须是红（sat）而不是转述；每一个"缺陷"都配一个候选补丁，读数必须是绿（unsat）。
没有变异读数的结论，本页一律不写。

## 0. 边界：这一页证明什么、不证明什么

证明的是**守卫算术**：在给定的公理表（§3）内，"守卫拦不住的那条路径"不可满足，或者可满足并给出具体反例。
不证明 C 语义本身、分配器行为、并发、未定义行为、编译器前端与字节码落地的一致性（那属于另外两条靶子 T3/T4，本轮没做，理由见 §6）。

三个状态严格分开：

| 读数 | 含义 | 本页怎么用 |
|---|---|---|
| unsat | 在本模型内被证明（命题之否不可满足） | 记为"证明" |
| sat | 反例：模型给出了具体取值 | 记为"缺陷候选"，不是证明 |
| unknown | 超时或未判定 | 记为"没证"，本页没有出现 |

数字标签：measured = 本页命令可复现的读数；proved = 上面的 unsat，且见证不额外踩任何 asserted 公理或可达性假设；proved-conditional = 模型内 sat，但见证要踩 §3 的 asserted 公理（A7）或一条可达性假设（A8），依赖链见 §5；asserted = 写下来但本轮没有重新读代码验证的前提（§3 里只有一条，A7）。

proved-conditional 是这次补的第四个标签：P4/P5/P6 三条 sat 用它，既不被抬成"证明"，也不被贬成"猜测"——它说的是"条件成立时这里会越界"。

## 1. 一条命令

    ./tools/z3bounds/run_bounds_proof.sh

它做三件事：逐行校验 pin（对不上就中止，退出码 2）、生成自包含的 z3 脚本、交给**带 z3 的 Windows 解释器**跑。
末行读数必须是 `SUMMARY|variants=17|pass=17|fail=0`，退出码 0。

- measured：本机 WSL 侧没有 z3；`/mnt/c/Python314/python.exe` 是 CPython 3.14.7 + z3 5.3.2。
- measured：Windows 解释器只接受 **Windows 路径**参数（传 `/mnt/c/...` 会报 can not open file 加 UNC 路径），所以脚本用 `wslpath -w` 转换。
- measured：17 个变体单次全部判定，耗时 0.001–0.006 s（timeout 20 s 未触发）。
- measured：这条命令的可执行位在本克隆里是**索引事实，不是文件系统事实**——`core.fileMode=false`，所以 `chmod +x` 在文件系统上生效、在 git 里什么都不记录（`ls -l` 显示可执行，`ls-files -s` 仍是 100644），只有 `git add --chmod=+x` 才写库。本轮之前它正是 100644，即本页这句「一条命令」当时**跑不起来**（`Permission denied`），是我用 `bash` 代跑才没暴露。判据：带执行位的命令除按字面跑一遍，还要 `git ls-tree <tree> <path>` 核模式位。

生成物是普通 z3py，可单独审阅：pin 表、公理表（每条公理旁边就是原文）、每个变体一段。
本次生成的脚本 sha256 前 16 位 = `428494cc4c0c7829`，36876 字节（measured）。

## 2. 模型怎么建（决定读数的边界）

| 对象 | 建模 | 理由 |
|---|---|---|
| C int / size_t 值域 | 数学整数 Int（不回绕） | 只谈索引算术，不把回绕当成"能兜住" |
| (size_t)x | 显式二补回绕 If(x < 0, x + 2**64, x) | 强制把"负值转大数"写进公式，不许隐式帮忙 |
| 指针判空（funcs[fidx] == NULL、!closure_env） | 不受约束的 Bool 原子 | 它们不约束索引；写死成 true/false 就等于偷偷假设索引合法 |
| im_closure_env_size(...) | 不透明整数，公理 0 <= size < 2**63 | 大小未知，但必须非负有限 |
| 公理 | 与守卫同一套 C 子集文法，同一个翻译器生成 | 不手抄进 z3py，避免"证明的是另一个公式" |

## 3. 公理表（每条带行号与当行原文）

原文按行引用，不缩写。指针判空与闭包大小是不可约束原子，见 §2。

| id | 标签 | 锚点 | 当行原文 |
|---|---|---|---|
| A1 | measured | src/vm/vm.c:4511 | `            nt->reg = malloc(nt->reg_cap * sizeof(Value));` [obs: 1042aed] |
| A1 | measured | src/vm/vm.h:73 | `#define VM_THREAD_REG_COUNT (2048 * 1024)` [obs: 1042aed] |
| A2 | measured | src/vm/vm.c:4515 | `            nt->frame_code = calloc(nt->frame_cap, sizeof(Bytecode*));` [obs: 1042aed] |
| A2 | measured | src/vm/vm.h:74 | `#define VM_MAX_FRAMES 2048` [obs: 1042aed] |
| A3 | measured | src/vm/vm.h:82 | `#define VM_FRAME_REGS 1024` [obs: 1042aed] |
| A3 | measured | src/vm/vm.c:4093 | `            t->base += FRAME_REGS;` [obs: 1042aed] |
| A4 | measured | src/vm/vm.h:101 | `    Value stack[1024]; int sp;` [obs: 1042aed] |
| A4 | measured | src/vm/vm.c:479 | `    if (g_cur_thread->sp >= 1023) return;` [obs: 1042aed] |
| A5 | measured | src/compiler/bytecode.h:104 | `    struct Bytecode *funcs[1024];` [obs: 1042aed] |
| A5 | measured | src/compiler/bytecode.c:274 | `    for (int i = 0; i < func_count && i < 1024; i++) {` [obs: 1042aed] |
| A6 | measured | src/vm/vm.c:4095 | `            int param_slots = root->func_argc[fidx] > argc ? root->func_argc[fidx] : argc;` [obs: 1042aed] |
| A7 | asserted | src/vm/vm.c:4097 | `                value_set(&FR[i + 1], VAL_NIL, 0, 0, NULL, NULL);` [obs: 1042aed] |
| A8 | measured | src/vm/vm.c:4125 | `                t->base = t->frame_base[t->frame_count];` [obs: 1042aed] |
| A8 | measured | src/vm/vm.c:3084 | `                t->base = 0;` [obs: 1042aed] |

读出的公理：

- A1：reg_cap = 2097152（寄存器文件按 reg_cap 个 Value 分配）
- A2：frame_cap = 2048（frame_code 恰按 frame_cap 分配）
- A3：帧宽 = 1024，一次调用把 base 前进一帧
- A4：-1 <= sp <= 1023（定长 1024 槽，压栈助手在 sp >= 1023 时拒绝）
- A5：0 <= func_count <= 1024，且 func_argc[1024] 同宽
- A6：param_slots = max(func_argc[fidx], argc)
- A7：0 <= param_slots <= 1024 —— **asserted**："一帧装得下全部实参"的 regime，不是代码保证；P5 就是把这条拆掉
- A8：base >= 0 且 base % 1024 == 0 —— 可达的 base 一定是整帧（起点 0、每调用 +1024、返回时从 frame_base 恢复）

A8 是本轮补上的关键公理：没有它，求解器会给出 base = 2096119 这种**不可达**的见证，反例虽真却讲不出"多深的地方会踩"。加上 A8 后，可达的最大 base = 2046 * 1024 = 2095104（measured：两个合取同时卡住，见 P4）。

## 4. 七个命题与读数

每个命题五行：靶子 / 命题 / 读数 / 命令 / 树号。asis 是代码原样，其余是变异或候选补丁。

**这一节的读数是修复前的读数**（树 `f393dda`，`src/vm/vm.c` blob `91a495cb`）：P4 的 asis=sat、P5 的 guard_fix_only=sat 都属于那棵树上的事实，本页不改写。main 上的两个修复（`c4d3db4` 整帧预留、`55d9526` 夹取 `param_slots`）之后，同一套命题在同一棵新树（`d064f8d`，blob `98889214`）上的复量见 `docs/streams/dec-formal-bounds-t5.md`：asis 翻 unsat，旧守卫变成红色变体 `prefix_guard_plus_8`=sat。**读数不跨树顶替**，要判断"现在怎么样"看 T5 页。

### P1 调用函数索引：唯一把 fidx 挡在 funcs[] 外的守卫

    靶子: src/vm/vm.c:4067  if (fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL) {
    命题: 守卫拦下时（守卫取反）=> 0 <= fidx < root->func_count；再加 A5 得 fidx < 1024
    读数: asis=unsat(proved) | extent=unsat(proved) | 变异 drop_count_check=sat，反例 fidx=0, func_count=0
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P1/asis, P1/extent, P1/drop_count_check
    树号: f393dda（worktree /tmp/wt-z3；脚本 sha256 前16位 428494cc4c0c7829）

变异 drop_count_check 就是把 "或 fidx >= root->func_count" 这一项剪掉：读数翻成 sat，反例正是 AUDIT.md §1.3 那一类（函数表为空时 fidx=0 已经越界）。它证明的是**这个析取项不可省**，不是"代码现在是坏的"。

### P2 闭包槽位：(size_t) 转换安全，是因为前面先测了负值

    靶子: src/vm/vm.c:4033  if (!t->closure_env || ins.r2 < 0 || (size_t)ins.r2 >= im_closure_env_size(t->closure_env)) {
    命题: 守卫拦下时 => 0 <= ins.r2 < im_closure_env_size(env)，(size_t) 按二补回绕建模
    读数: asis=unsat(proved) | 变异 strict_gt=sat，反例 ins.r2=0, size=0（读第 0 号槽，而大小是 0）
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P2/asis, P2/strict_gt
    树号: f393dda

这条专盯"把 >= 写成 >"一个字符：哪天有人为了"允许 r2 == size"去改比较符，读数立刻从 unsat 翻成 sat。

### P3 帧元数据写入：frame_code[frame_count] 落在 calloc(frame_cap) 内

    靶子: src/vm/vm.c:4079  if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap) {
    命题: 第一个合取被拦下时 => t->frame_count < t->frame_cap（A2 给 frame_cap=2048）
    读数: asis=unsat(proved) | 变异 strict_gt=sat，反例 frame_count=2048, frame_cap=2048
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P3/asis, P3/strict_gt
    树号: f393dda

### P4 缺陷：守卫只给子帧留了 8 个寄存器，而子帧要 1024 个

    靶子: src/vm/vm.c:4079  if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap) {
    命题: 守卫通过 => base + 1024 + param_slots < 2097152，即子帧 [base+1024 .. base+1024+param_slots] 全在寄存器文件内
    读数: asis=sat（proved-conditional：条件 A7、A8，见 §5），反例 base=2095104(=2046*1024，A8 下最深可达帧), param_slots=1024(A7 的上界), frame_count=2047
          候选补丁 fix_reserve_frame=unsat（把 + 8 换成 + VM_FRAME_REGS）
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P4/asis, P4/fix_reserve_frame
    树号: f393dda

最小反例：第 2046 层帧里发起一次 1024 实参的调用，`src/vm/vm.c:4097`（`                value_set(&FR[i + 1], VAL_NIL, 0, 0, NULL, NULL);`）的初始化循环写 FR[1024]，即 reg[2095104 + 1024] = reg[2097152] = reg[reg_cap]——**恰好越界一个 Value**。
算术上不需要 1024 个实参：循环写 FR[1..param_slots]，落点 base + 1024 + param_slots；守卫只保证 base + 1032 < reg_cap。两者的差就是 1016 个槽（1024 - 8）。
候选补丁把松量从 8 改成整帧宽（base + VM_FRAME_REGS + VM_FRAME_REGS >= t->reg_cap）后，读数转 unsat。

### P5 缺陷：param_slots 没有上界，func_argc 直接从字节码里来

    靶子: src/vm/vm.c:4095  int param_slots = root->func_argc[fidx] > argc ? root->func_argc[fidx] : argc;
    命题: 守卫通过 => base + 1024 + param_slots < 2097152（去掉 A7，即不再假设"一帧装得下实参"）
    读数: asis=sat（proved-conditional：条件 A8，见 §5）（base=2095104, argc=1024 => param_slots=1024）
          declared_2000=sat（func_argc=2000 => param_slots=2000，同深度越界 976 个槽）
          clamp_param_slots=unsat（param_slots 夹到 1023 后转绿）
          guard_fix_only=sat（只修 P4 的松量**不够**：param_slots=2048 时仍越界，反例 base=2094080, argc=2048）
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P5/asis, P5/declared_2000, P5/clamp_param_slots, P5/guard_fix_only
    树号: f393dda

guard_fix_only=sat 是这条命题目最值钱的一格：它把"P4 和 P5 是同一个缺陷"这个方便说法打掉。两个修法各自单独够用（P4 的守卫修法配 A7，或 P5 的夹取），但**只修守卫不够**，因为实参个数本身没有上界。
func_argc 的来源已 pin：

    src/runtime/vm_exec_builtin.c:83  bc->func_argc[bc->func_count] = (av.type == VAL_INT) ? av.ival : 0;

没有 band 检查，而 vm_exec 让 .im 能把字节码直接喂进来；同文件 :61 处 r1/r2/r3 也是同样直接取值。
夹取法能闭合索引窗口，但它**静默丢实参**——那是正确性缺陷，不是内存缺陷，所以本页只给读数，不下结论。

### P6 缺陷：寄存器操作数 ins.r1 完全没有守卫

    靶子: src/vm/vm.c:3025  Value *R = t->R;（执行器序言；R 就是 t->R，即 t->reg）
    命题: R[ins.r1] 的索引落在 [0, reg_cap) 内
    读数: asis=sat（proved-conditional：条件＝操作数字段可由字节码写入），反例 ins.r1=-1（寄存器文件前一个槽；.im 量级的 -5000000 是 160 MB 之前）
          候选补丁 candidate_guard=unsat（加 ins.r1 < 0 || ins.r1 >= t->reg_cap 的拒绝条件）
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P6/asis, P6/candidate_guard
    树号: f393dda

"没有守卫"这件事用**负 pin**钉住，不靠人眼：在执行器主体（3022–4400 行）里，提到 reg_cap 的行只有 4079 一行，而且是调用守卫、不是操作数守卫。哪天有人加了操作数检查，这个集合就会变大，生成器直接中止并要求重读前提——它不会带着过时的"没有守卫"继续出读数。

### P7 证明：实参出栈不会跌破栈底

    靶子: src/vm/vm.c:4073  if (t->sp + 1 < argc) {
    命题: 守卫拦下时 => sp + 1 - argc >= 0，即最低读取下标 sp - argc + 1 >= 0
    读数: asis=unsat(proved，只用 A4 的 -1 <= sp <= 1023) | 变异 drop_guard=sat，反例 sp=-1, argc=1（读 stack[-1]）
    命令: ./tools/z3bounds/run_bounds_proof.sh    变体 P7/asis, P7/drop_guard
    树号: f393dda

出栈循环本身（4099–4101）**没有**逐步的栈底检查，所以 4073 这一行独自扛着整个不变式。变异必须选"拆掉守卫"（改成恒不拦），而不是"加强守卫"——加强守卫只会让读数更容易是 unsat，测不出东西。

## 5. 依赖链与标签：P4 是条件证明，不是已证事实

sat 只说明"模型里有见证"。见证能不能站住，取决于它踩了哪几条假设。P4 的依赖链如下（每一条都可追到本页的公理表）：

1. P4 的见证要 `param_slots = 1024`；
2. 这个上界来自 **A7**（`0 <= param_slots <= 1024`），而 A7 是本页唯一一条 `asserted`，**未证**；
3. A7 的上界恰好是 P5 证明代码里**没有**的东西：`func_argc` 经 `src/runtime/vm_exec_builtin.c:83`（`            bc->func_argc[bc->func_count] = (av.type == VAL_INT) ? av.ival : 0;`）不夹取就进了 VM。也就是说 A7 不描述代码，描述的是"若要越界最少，实参得被夹在 1024 以内"的最佳情形；实参越不受控，P4 的越界只会更多（P5/declared_2000 的 976 槽就是这个方向）；
4. P4 的见证还要 `base = 2095104`，这依赖 **A8**（base 是 1024 的倍数、且从 0 起每次 +1024、返回时从 frame_back 恢复）。没有 A8，求解器给的是 `base = 2096119` 这种不可达见证——反例为真，但讲不出"多深踩"；
5. 因此 P4 的正确读法是：**若 A7、A8 成立，则 P4 的状态在模型内可达**。这与"P4 是缺陷"不是同一个命题。本页**没有给出可执行的指令序列**（2046 层递归在 .im 里是否可达本轮未实测），所以 P4 标 `proved-conditional`，不标 `proved`。

同理：P5 标 `proved-conditional`（条件 A8 + `func_argc` 域可由字节码写入，后者有源码路径但未在 HEAD 构建上实测）；P6 标 `proved-conditional`（条件：操作数字段可由字节码写入）。P1、P2、P3、P7 是 `proved`：它们的见证不依赖任何 `asserted` 公理（P7 只用 A4，A4 带 pin）。

P5 比 P4 硬在入口：一行 .im 源码 `vm_exec(字典)` 就能把 `func_argc` 填成任意整数（`src/runtime/vm_exec_builtin.c:83` 一处代码可见），而 P4 的 2046 层深调用是"若可达"。读者该信哪一半，由这条入口差决定。

另外把"三处操作数都直接取值"也钉成引文（不是转述）：

    src/runtime/vm_exec_builtin.c:60  if (m > 0) { x = vm_array_get(vm, ins.ival - 1, 0); op = (OpCode)((x.type == VAL_INT) ? x.ival : 0); }
    src/runtime/vm_exec_builtin.c:61  if (m > 1) { x = vm_array_get(vm, ins.ival - 1, 1); r1 = (x.type == VAL_INT) ? x.ival : 0; }
    src/runtime/vm_exec_builtin.c:62  if (m > 2) { x = vm_array_get(vm, ins.ival - 1, 2); r2 = (x.type == VAL_INT) ? x.ival : 0; }

连 opcode 本身（:60）都不在枚举范围内做检查，这一条对 T2 也相关。

可达性：vm_exec 把外部字典直接装配成字节码（src/runtime/vm_exec_builtin.c:61 与 :83 两处都不做 band 检查），所以 func_argc、r1、r2 这些域在语言层面就可得。P5 的越界因此不需要任何深层调用：一帧、一次调用、一个字典值即可。

注意（measured，必须写下来）：主树里的 inimerse 二进制是 0.5.0（10 月 1 日），**不是 HEAD**。用它回放 AUDIT.md §1.3 的五行脚本，得到的是 -1、退出码 0、一段 stderr 字典转储，而不是修好之后的退出码 1 加"无效函数索引 999"。所以：

- 本页的 P4/P5/P6 是**模型读数**（sat 明确给出见证），不是运行时崩溃复现；
- 要用真实 VM 复现，得先按 HEAD 构建；本轮不构建、不改 src/，也不把"没崩溃"当成反驳。

## 6. 本轮故意没证明的（以及为什么）

- T3（三个前端的有界等价）：要把两侧语义都建模，等于再造一个解释器模型，读数可信度低于直接差分测试。
- T4（INIMBC 往返）：v1 只有写端，读端不存在，"往返"无从证明；能证的只有写端不变式，价值低于 P4/P5。
- 浮点、异常、GC 根扫描、并发：不在本轮索引算术范围内。

## 7. 红线：两处会变红的地方

1. **pin 漂移**（实测）：把 `src/vm/vm.c:4079`（`            if (t->frame_count >= t->frame_cap || t->base + VM_FRAME_REGS + 8 >= t->reg_cap) {`）里的 `+ 8` 换成 `+ VM_FRAME_REGS`（即候选补丁本身）后，在**副本**上跑生成器，得到 `PIN FAILED: src/vm/vm.c:4079 pin drift`，退出码 2，且**不生成**脚本。本页的读数因此不可能悄悄变成"针对另一份代码"。
2. **变异不红就是测量失败**：每个 proved 命题的变异读数必须是 sat，每个 finding 的候选补丁必须是 unsat；17 个变体任一不符，脚本以退出码 1 结束（本次 0）。

## 8. 树号与改动清单

- 树号：f393dda（首次证明，/tmp/wt-z3）；e280fdcb（换基线复跑，/tmp/wt-z3b）；1042aed（第三次复跑，/tmp/wt-z3c；前两个 worktree 的管理目录被外部清掉，文件仍在，于是重建了一棵）。
- 新增：tools/z3bounds/gen_bounds_proof.py（pin 校验 + 公理表 + 生成器）、tools/z3bounds/run_bounds_proof.sh（唯一命令）、本页。
- 未改：src/、selfhost/、门禁脚本及其 TARGETS、测试数量上限。缺陷只以最小反例记录，补丁只以"候选"出现在读数的另一列。
- 换基线复跑（measured）：f393dda..e280fdcb 之间，被 pin 的 5 个文件逐字节相同（逐文件 blob 比对），所以 pin 表一个字没改、生成脚本 sha256 仍是 428494cc4c0c7829、17/17 同读数。
- 可复现性读数（measured，每棵树各测两次：本页在位 / 本页移走，两次逐字相同）：
      f393dda  digest=8ba5c95d  dirty(2) / dirty(1)
      check_line_refs: pins -- explicit delta +12 (expect +12), unanchored delta +9 (ceiling +12)
      check_line_refs: readings -- explicit 185 (base 173), unanchored 571 (base 562)
      e280fdcb  digest=7c56e529  dirty(2) / dirty(1)
      check_line_refs: pins -- explicit delta +12 (expect +12), unanchored delta +9 (ceiling +12)
      check_line_refs: readings -- explicit 185 (base 173), unanchored 571 (base 562)
      1042aed   digest=0c8c505e  dirty(2) / dirty(1)
      check_line_refs: pins -- explicit delta +12 (expect +12), unanchored delta +9 (ceiling +12)
      check_line_refs: readings -- explicit 185 (base 173), unanchored 571 (base 562)
  本页只引 src/ 路径，不进该检查的输入集，所以这两个 delta 在三棵树上都不动；pin 的 5 个源文件在三点间逐字节相同，17/17 与 sha256=428494cc4c0c7829 每棵树都重跑过。
