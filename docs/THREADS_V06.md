# 线程：v0.6 的读数与设计

本档是 v0.6「全面补充线程」那一件的设计档。它先记今天量到了什么，再记要做什么。
裁定见 `docs/PLAN_V06.md` §6.4.1：**线程挂「去 C 化」线**，不新立主线。

观测点：本档第一版写于 `f07cc1e`。凡引用某一行，写 `<文件>:<行>`；凡引用一次读数，
写它读的是哪棵树（见 §7）。

---

## §1 为什么线程属于「去 C 化」

★ **不是「因为线程是用 C 写的」** —— 引擎的每一部分都是。

★ **是因为线程的规则有一条只活在 C 解析器里，而自举解析器不知道它。**
这不是一个比喻，是一个可复现的读数，见 §2.4。

★ 结论句（`vivid-anchor` 的，我逐字收）：**语义没有搬家，是规则没有跟着搬。**

---

## §2 今天量到了什么

### §2.1 语言面比预想的宽

| 面 | 位置 |
|---|---|
| 关键字 `thread` / `task` | `src/lexer/lexer.h:8`、`src/lexer/lexer.c:12` |
| 四个修饰 `endless` / `daemon` / `restart` / `single` | `src/lexer/lexer.c:13-14` |
| 定义形 `thread name: {}` 与 `thread name(params) {}` | `src/parser/parser.c:1594` |
| `start` / `join` | `src/parser/parser.c:247` 把 `TOK_JOIN` 映成 `"join"` |
| `thread_await` / `thread_result` / `thread_release` | `src/mod/result_mod.c:232-234` |
| 三个原子 `atomic_add` / `atomic_get` / `atomic_set` | `src/runtime/runtime.c:1920-1922`、`src/runtime/runtime_posix.c:1275-1277` |
| 真 OS 线程 | `src/platform/thread.c`（WIN32 `:20`/`:29`、POSIX `:58`/`:69`） |
| 语言层读数 | `usage()` 的 `"threads"` 与 `"threads_limit"` |

★ 实测：**五行 `.im` 就能把 `active_threads` 开到 2**（`thread` 定义 + 两个 `start`）。
★ 而 `active_threads` **只数 spawn 出来的线程，主线程不计入** ⇒ 主线程 + 一个 worker = 1。

★ **仓库自己的已注册 CTest 就开到了 2**：`vtest/thread_await_v04.im` 在 `thread_await_runtime`
里实测 `1 → 1 → 2 → 2`；而**同族的 `thread_result_runtime` 从头到尾没到过 2**（四个 worker 体太短）。
★ 两条同族的测试，一条开门一条不开，**而这件事只有把计数读出来才看得见**。

### §2.2 平台剖面：缺的是名字，不是机制

★ 第一版这一节写「POSIX 上一个锁 / 互斥量 / 条件变量 / 通道都没有」—— **那是错的**。
正确的形状是三层：

| 层 | POSIX | WIN32 |
|---|---|---|
| **引擎内部互斥量** `im_mutex_*`（`src/platform/platform.c:33-78`） | ✅ | ✅ |
| **脚本级 `lock` / `unlock` / `send` / `recv`**（在共享的 `src/vm/vm.c` 里） | ✅ | ✅ |
| **脚本级 `ai_lock` / `ai_unlock` / `ai_wait_task`**（`src/mod/io_mod.c:350-357`） | ❌ | ✅ |
| **脚本级 `gui_wait` / `gui_wait_broadcast`**（`src/mod/gui_mod.c:3707` / `:3827`） | ❌ | ✅ |
| **脚本级 `sleep_ms`**（`src/runtime/runtime_posix.c:1205`） | ✅ | ❌ |

★ `im_mutex_*` 全仓 **70 余处**（`src/vm/vm.c` 42、`src/runtime/runtime.c` 8、
`src/headless_server_posix.c` 11、`src/child_proc.c` 3、`src/platform/platform.c` 4）。
★ `L_LOCK` / `L_SEND` / `L_RECV` 与 `OP_LOCK` / `OP_SEND` / `OP_RECV` 都在共享的 `src/vm/vm.c` 里。

⇒ ★ **判据：一个「平台缺什么」的读数，只有在先问「缺的是机制还是名字」之后才是一个读数。**

★★ **两个平台真正共同缺的是条件变量** —— `pthread_cond|sem_init|sem_wait|sem_post|rwlock|barrier`
在 `src/` 与 `mods/` **合计零命中**。
⇒ ★ **`L_RECV` 因此是轮询**（取一次、没取到 `im_platform_sleep_ms(1)` 再取）：
**不是设计选择，是没有东西可以等。**

★ **`sleep_ms` 有两层身份**：`src/vm/vm.c:263` 逐字
`#define sleep_ms(ms) im_platform_sleep_ms((unsigned int)(ms))` —— **它同时是一个 C 宏名和一个脚本内建名**；
实现 `src/platform/platform.c:90-97` 两平台都只是**睡当前 OS 线程**。
⇒ ★ **在 `task`（Fiber）里睡的是整根 OS 线程 ⇒ 同一根线程上的所有 task 一起停。**
⇒ **「它是让出还是整进程停」的答案是「都不是」。**

### §2.3 `THREAD_FLAG_SINGLE` 改了什么都不改，只关掉一行 stderr

★ 全部执行点只有 2 处：`src/vm/vm.c:4355-4363`（OS 线程支）与 `src/vm/vm.c:4385-4391`（task 支）。
**控制流完全相同**（`while` 弹参数 + `continue` 无条件），**唯一的分支是那行 `fprintf` 印不印**。

★ 而那句注释 `/* single: duplicate start silently ignored */` **写在 `if` 之外** ——
**它描述的是所有线程的行为，不是 `single` 的。**

⇒ ★ **判据：一个修饰词的作用域，不是由它的名字决定的，是由它所在的那个 `if` 包住多少行决定的。**

★ 附带：**OS 线程支没写 `R[res]`、task 支写了** ⇒ 同一个 `start` 两个分支两种返回约定，
★ 重复 `start` 之后返回值寄存器保留上一次的值。

★ **`THREAD_FLAG_*` 定义两遍**：`src/parser/ast.h:7-11`（5 个，**含 `THREAD_FLAG_TASK`**）
vs `src/vm/vm.c:42-46`（5 个，**没有 TASK**，被 `#ifndef THREAD_FLAG_ENDLESS` 包住）。
⇒ **守卫只测 `ENDLESS`，而两处定义的差集是 `TASK`。**

### §2.4 「loop 里的 thread 定义」是真拒绝，而规则只活在 C 解析器里

★ 正控 / 拒绝臂实测：
- 顶层定义 ⇒ `Z-RAN`，rc=0；
- 循环里定义 ⇒ **解析期** `Error at line 3: task/thread definitions inside a loop are silently ineffective; define at top level`，rc=1，`AFTER` 一行不印。

⇒ ★ **是拒绝，不是描述。**

★★ **而 `p->loop_depth` 的全部出现都在 `src/parser/parser.c`**
（`:1479`/`:1481`、`:1517`/`:1519`、`:1525`/`:1527` 三对增/减 + `:1596` 唯一读点）；
`selfhost/parser.im` 里没有任何 `loop` / `top level` / `silently ineffective` 的命中，
而 `selfhost/parser.im:381` 照常解析 `thread`。

⇒ ★ **这条规则是「C 解析器的规则」，不是「语言的规则」。**
★ **自举之后它会静默消失，而今天没有任何东西会发现它消失了。**

★ 第二条独立证据线：`selfhost/compiler.im` 的 `compile_stmt`（`:424-739`）**25/25 全中**
（含并发七个 `join lock main recv send start tctrl thread twait`），
而 `selfhost/eval.im` 的 `eval_stmt`（`:221-299`）**13/25**（并发七个一个都没有，**连 `else` 都没有**）。
⇒ **同一门语言、两个 `.im` 消费者，一个全收、一个静默忽略。**

### §2.5 两条活缺陷（登记未修）

**① ABBA 锁序，两平台都有。**

- `vm_global_grow`（`src/vm/vm.c:1327` 升序取 16 把分片锁、`:1344` 降序放）；
- 反方向：`L_LOAD_GLOBAL`（`src/vm/vm.c:3648-3660`）持分片锁（`:3649`）调 `vm_intern`，
  而 `vm_intern`（`src/vm/vm.c:720`）`:724-725` 逐字
  `int need_lock = (vm->active_threads > 1); if (need_lock) VM_LOCK(vm);` ⇒ **分片 → `global_lock`**；
- 三条违反点（在持 `global_lock` 时调 grow）：`src/runtime/runtime.c:1687`（`VM_LOCK` 在 `:1683`）、
  `src/runtime/runtime.c:1781`、`src/runtime/runtime_posix.c:829`（`VM_LOCK` 在 `:824`）。

★★ **那条不变量（`/* 独佔全部锁扩容（此时未持任何分片） */`）在 `src/vm/vm.c:2493`、`:3669`、`:4270`
写了三遍，而没有写在 `vm_global_grow` 自己的抬头里** —— 抬头只提「自己的分片」。
⇒ ★ **规则写在读者旁边、没写在执行者旁边；三个违反点的作者大概率读过抬头、没读那三处注释。**

★ **判据：「没有规则」与「没有事故」今天长得一样。**

**② POSIX 三个原子内建没有分片锁。**

- `grep -c 'VM_GSHARD'` ⇒ `src/runtime/runtime.c` **8** / `src/runtime/runtime_posix.c` **0** / `src/vm/vm.c` **17**；
- `src/runtime/runtime_posix.c:925-926`：`__sync_lock_test_and_set(&vm->globals[idx].val.ival, val);`
  紧跟 `vm->globals[idx].val.type = VAL_INT;` ⇒
  **同一个 `Value` 的两个字段，一个原子写、一个普通写 ⇒ 并发读者能看到「新 `ival` + 旧 `type`」**；
- WIN32 对照（`src/runtime/runtime.c:1790` / `:1802`）在原子操作外**还拿分片互斥量**。

⇒ ★ **同一个保证，两侧用了两种机制**：WIN32 = 原语 + 互斥量（冗余，并制造上面那个 ABBA），
POSIX = 只有原语（于是 `.type` 裸写）。★ **两处都不是「少写一行」，是「同一件事被决定了两次」。**

### §2.6 `join` 是一个词两个语义（这是设计，已写进文档）

★ `docs/SYNTAX.md:393` 与 `:854`：`join(` 是内建函数，`join name` 是线程语句。
★ C 侧判据：`src/parser/parser.c:233` / `:247`，判别点在 `src/parser/parser.c:1652`
（`peek_next(p).type != TOK_LPAREN`）。
★ 决定点数：`join(arr, sep)` **2 个**（`src/runtime/runtime.c:1880` 与
`src/runtime/runtime_posix.c:1243`）；`thread_await`/`thread_result`/`thread_release` **1 个**
（`src/mod/result_mod.c:232-234`，而该文件同时在 `CMakeLists.txt` 的两个平台清单里）。

### §2.7 一个我自己量错、由别人更正的读数

★ 我第一版把 `start` 的返回值当成了唯一约定。实测：**OS 线程支不写 `R[res]`，task 支写。**
★ 这条留在档里的理由不是它重要，是**它证明这一族读数必须两边都跑**。

---

## §3 「去 C 化」对线程意味着什么

★ 不是「把 `src/vm/vm.c` 的线程部分重写成 `.im`」—— ★ 那是把 C 换成 C。

★ 是三件具体的事：

1. **把只活在 C 解析器里的规则搬到语言层。** 今天唯一被量到的实例是 §2.4 的 `loop` 规则。
   ★ 判据不是「自举解析器也能拒绝它」，是 **`selfhost/parser.im` 与 C 解析器对同一份输入给出同一个答案**，
   而这件事必须由一条在两边都跑的测试盯着。
2. **把「平台缺什么」从名字层抬到机制层。** §2.2 的条件变量是唯一的共同缺口；
   ★ 补它之前，`L_RECV` 的轮询不是可以改的实现细节，是**没有东西可以等**。
3. **把重复决定的语义收敛到一处。** §2.3 的 `THREAD_FLAG_SINGLE`、§2.5② 的原子内建、
   §2.1 的两条同族 CTest，都是**同一个语义被决定了两次而两次不一样**。

---

## §4 要做的（按顺序）

1. **给 §2.4 那条规则补一条守卫**：一条在 C 解析器与 `selfhost/parser.im` 上都跑的测试，
   ★ 断言两者对「循环里的 thread 定义」给出**同一个答案**（今天会红）。
2. **修 §2.5①**（ABBA）：把不变量写进 `vm_global_grow` 的抬头，并在三个违反点各留一条**指名它**的注释；
   ★ 然后是锁序本身。★ 这一件动 `src/**`，归 `exact-lumen`。
3. **修 §2.5②**（POSIX 原子的 `.type` 裸写）：★ 判据是「两侧对同一件事给出同一个保证」，
   而不是「两侧代码长得一样」。
4. **给 §2.3 的 `single` 一个真语义，或者把那个名字去掉。**
   ★ 两条路都行，★ **但今天这个状态是第三条：名字承诺一个行为、实现了一个日志开关。**
5. **`sleep_ms` 的语义写进文档**：它睡 OS 线程，task 里所有 task 一起停。
   ★ 这不需要改代码，需要**让读到 `sleep_ms` 的人知道它不是 `yield`**。

---

## §5 判据（这一档的可执行部分）

1. **一个修饰词的作用域，是由它所在的那个 `if` 包住多少行决定的。**
2. **一个「平台缺什么」的读数，只有在先问「缺的是机制还是名字」之后才是一个读数。**
3. **「没有规则」与「没有事故」今天长得一样。**
4. **同一个语义被决定了两次，那么它一定有一天会给出两个答案**（§2.3、§2.5②、§2.1 三条实例）。
5. **一条读数如果是「零」，先怀疑取数的那条命令**（本轮两条独立实例：命令没读到 / 程序没跑到）。

---

## §6 不做什么

- ★ **不在 v0.6 里重写线程运行时。** §2.2 的三层表说明今天缺的不是实现。
- ★ **不新增线程原语**（`chan` / `select` / `spawn` 之类）。★ 先让已有的那些**各自只有一个语义**。
- ★ **不把 `task` 与 OS 线程合并成一种。** 它们的差别（`sleep_ms` 的行为、返回值约定）是真的，
  ★ **今天的问题不是有两种，是两种的差别没有被写下来。**
- ★ **不动 `selfhost/` 的分发表**（那是「去 C 化」的另一件，不挂在线程上）。

---

## §7 诚实边界

1. **本档的平台剖面是读码 + 实测混合的**：`im_mutex_*` 的 70 余处是 `grep -c` 读数；
   ★ **WIN32 侧全部是读码，没有在 Windows 上实跑。**
2. **ABBA 没有被构造成死锁。** ★ 它是一条**读出来的**锁序，★ **不是一次事故。**
3. **§2.5② 的撕裂没有被构造成并发读者。** ★ 同样是读出来的。
4. **§2.4 的「静默消失」是一个推断**：`selfhost/parser.im` 今天不拒绝它，
   ★ **而我没有把自举解析器真的接到那条输入上跑过。**
5. **`sleep_ms` 在 task 里的行为没有实跑**（§2.2 那一句是从 `src/platform/platform.c:90-97`
   的实现读出来的：它睡当前 OS 线程）。
6. **§2.1 的「两条同族 CTest 一条开门一条不开」是实测**，★ 而**它只覆盖 `vtest/` 里已注册的那些**。
7. **本档引的行号是 `f07cc1e` 的。** ★ 行号会漂；★ **引本档的结论请引节号。**
