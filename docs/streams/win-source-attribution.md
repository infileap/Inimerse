# 作业单：`win-source-attribution`

- **认领者**：`vivid-anchor`
- **分支**：`stream/win-source-attribution`（base `main` = `9969e5b`）
- **工作树**：`.worktrees/win-source-attribution`
- **裁定依据**：`docs/AUDIT.md` §1.71（Linux 门禁对 12 个 `src/**/*.c` 结构性失明）。本文是该节的**测量附录**，不是它的替代：§1.71 说「这些文件只在 `if(WIN32)` 里」，本文回答**每一个到底是哪一类 Windows-only**。
- **测量 ref**：**`d27413a`**（下文行号一律 `<ref> @ d27413a`）

## 1. 四项回报

1. `pwd` = `/home/sakiko/inimerse`
2. `git rev-parse HEAD` = `9969e5b02a0627d369c613bcd32d3546c30af483`（= `main`）。**全部测量跑在 `d27413a`**：`git merge-base --is-ancestor d27413a 9969e5b` → rc=0，且 `git diff --stat d27413a 9969e5b -- src/ CMakeLists.txt` **输出为空** ⇒ 期间 `main` 只动了 docs，测量可迁移。
3. 读法：①② 用 `git show d27413a:<path>` 读**提交字节**；③ 用 Windows 侧导出树 `D:/inim-rel/d27413a-src`（`git archive d27413a | tar -x` 解出，`grep -c 'add_test('` = 148 钉身份）与它自己的 `build-n`。
4. 原始输出：`/mnt/d/inim-rel/probe/hits-all.txt`（74 条测试 × 14 个断点的逐条计数）、`/mnt/d/inim-rel/probe/runhits.log`（驱动日志）、脚本 `/mnt/d/inim-rel/probe/mods.gdb` 与 `/mnt/d/inim-rel/probe/runhits.sh`。

## 2. 十二个文件的归属

| 文件 | Windows 侧生产点 | POSIX 对应物 | 归属 | ③ 注册测试可达性 | 依据 |
|---|---|---|---|---|---|
| `src/runtime/runtime.c` | `CMakeLists.txt:429` | `src/runtime/runtime_posix.c`（`:438`） | **两平台**（各一份实现） | `runtime_register_builtins` **73/74**；`re_seq` 仅 3 条 | 互斥分支；`grep -c re_seq src/runtime/runtime_posix.c` = 0 |
| `src/child_proc.c` | `:429` | **无**（无对等文件、无空桩） | **仅 Windows（构建表所致）** | `child_proc_spawn` / `child_proc_list` **0/74** | `grep -n child_proc CMakeLists.txt` 只有 `:429` 一行；文件内 `grep -c _WIN32` = **0**，头文件 `src/child_proc.h:7` `#ifdef _WIN32` / `:10` `typedef uint64_t DWORD;` 明确写了非 Windows 的替身；注释首行 `/* Cross-platform child process registry. */` |
| `src/headless_server.c` | `:429` | `src/headless_server_posix.c`（`:442`） | **两平台**（各一份实现） | `headless_init` / `headless_loop` **0/74** | 互斥分支；Windows 侧唯一入口 `src/main.c:895`（`--headless`）→ `:1118 headless_init` |
| `src/mod/mod.c` | `:430` | `src/mod/mod_posix.c`（`:439`） | 两平台 | `mod_load_all` **57/74** | 互斥分支 |
| `src/mod/gui_mod.c` | `:431` | 空桩 `STUB_REG(gui_mod_register)`（`src/platform/posix_stubs.c:6`，列入 `:441`） | **仅 Windows（空桩）** | `gui_mod_register` **73/74** | POSIX 无实现 |
| `src/mod/io_mod.c` | `:431` | 空桩（`posix_stubs.c:6`） | **仅 Windows（空桩）** | `io_mod_register` **73/74** | 同上 |
| `src/mod/identity_mod.c` | `:433` | 空桩（`posix_stubs.c:7`） | **仅 Windows（空桩）** | `identity_mod_register` **73/74** | 同上 |
| `src/mod/social_mod.c` | `:433` | 空桩（`posix_stubs.c:8`） | **仅 Windows（空桩）** | `social_mod_register` **73/74** | 同上 |
| `src/mod/ai_mod.c` | `:434` | 空桩（`posix_stubs.c:8`） | **仅 Windows（空桩）** | `ai_mod_register` **73/74** | 同上 |
| `src/mod/net_mod.c` | `:431` | `src/mod/net_mod_posix.c`（`:439`） | 两平台 | `net_mod_register` **73/74** | 互斥分支 |
| `src/mod/server_mod.c` | `:433` | `src/mod/server_mod_posix.c`（`:439`） | 两平台 | `server_mod_register` **73/74** | 互斥分支 |
| `src/mod/say_mod_windows.c` | `:436` | `src/mod/say_mod_posix.c`（`:439`） | 两平台 | `say_mod_register` **73/74** | 互斥分支 |

分支结构（`CMakeLists.txt @ d27413a`）：`:426 if(WIN32)` → `:427-435` 的 `list(APPEND …)`（12 个文件分别落在 `:429`／`:430`／`:431`／`:433`／`:434`）→ `:436` 单独一行列 `say_mod_windows.c` → `:437 else()` → `:438-443` 的 POSIX 列表（对等实现与 `posix_stubs.c` 都在这里）→ `:444 endif()`。

十二个文件**全部编进了 Windows 引擎**：导出树的 `build-n/CMakeFiles/inimerse_engine.dir/<path>.obj` 十二个都在（`runtime.c` 53717 B、`gui_mod.c` 204681 B、`io_mod.c` 33413 B、`ai_mod.c` 17654 B、`net_mod.c` 13818 B、`server_mod.c` 13391 B、`identity_mod.c` 11885 B、`say_mod_windows.c` 11051 B、`headless_server.c` 6616 B、`social_mod.c` 6592 B、`child_proc.c` 4460 B、`mod.c` 3028 B），且符号在 `inimerse.exe` 里（该 exe 共 5987 个已定义符号，含 `0000000140069210 T child_proc_spawn`、`00000001400697c0 T headless_accept`、`00000001400633a0 t re_seq`）。

## 3. ③ 那一列是怎么量的

**不是读码。** 步骤：

1. 注册表：`main @ d27413a` 的 Windows 注册 = **137** 条，其中 **9 条 `(Disabled)`**；**74 条**的 COMMAND 直接是 `build-n/inimerse.exe`（其余是探针可执行文件、python 脚本与 node 脚本）。名单取自生成的 `build-n/CTestTestfile.cmake`，与 `ctest -N` 的 `Total Tests: 137` 一致。
2. 对每条跑一次 gdb，断点打在 14 个入口：`gui_mod_register`、`io_mod_register`、`identity_mod_register`、`social_mod_register`、`ai_mod_register`、`net_mod_register`、`server_mod_register`、`say_mod_register`、`runtime_register_builtins`、`mod_load_all`、`child_proc_spawn`、`child_proc_list`、`headless_init`、`headless_loop`。每个断点用 `commands` / `silent` / 计数器自增 / `continue`，程序结束（正常退出或信号）后一次性 `printf` 十四个计数。
3. 结果：**73/74** 命中那九个启动期注册函数；**57/74** 命中 `mod_load_all`；**0/74** 命中 `child_proc_*` 与 `headless_*`。唯一未命中启动的是 `version_runtime`（`inimerse --version`，不起 VM）。未命中 `mod_load_all` 的 17 条 = `version_runtime` + **16 条 `--no-mods` 测试**。

**这一列把 §1.71 说的「结构性失明」从形容词变成了可复现的机制**：八个 `*_mod_register` 由 `src/main.c:1067` 与 `:1097` 的 `register_core_modules(&vm)` **无条件调用**，`--no-mods` 挡不住它们 —— `src/main.c:1000` 的 `--no-mods` 只把 `load_mods` 置 0，而 `load_mods` 只挡 `src/main.c:1068`／`:1084`／`:1098`／`:1099` 的 `register_world_modules` 与 `mod_load_all`。所以「引擎起得来」与「这八个模块被调用」是同一件事。

## 4. 两个工具陷阱

**陷阱一：`nm.exe` / `objdump.exe` 是 Windows 程序，必须给 `D:/…` 形式的路径。** 给 `/mnt/d/…` 会得到 `nm.exe: '/mnt/d/…': No such file`，而 `nm --defined-only` 因此**只输出 1 行** —— 看上去与「符号不存在」一模一样。我第一遍就中了这个，据此差点写下「`child_proc_*` / `headless_*` 不在 exe 里」；换成 `D:/inim-rel/d27413a-src/build-n/inimerse.exe` 后同一命令输出 5987 个已定义符号，两者都在。⇒ **「工具说没有」与「确实没有」之间，隔着一次「我给它的路径形式它读不读得懂」的检查。**

**陷阱二：该导出树的对象文件后缀是 `.obj` 不是 `.o`。** 用 `find build-n -name '*.o'` 去找，十二个文件**全部报缺失**，而它们十二个都在 `CMakeFiles/inimerse_engine.dir/<path>.obj`。⇒ 同一个入口的第二次：**一次筛选取不到东西，先怀疑筛选条件，再怀疑世界。**

## 5. 未测项（写「没测到」，不写「不存在」）

- `child_proc.c` 与 `headless_server.c` 在 **Windows 侧注册表里 0 条测试可达**。但 `headless_probe` 在 Windows **根本没注册**（137 条里没有这个名字，它在 Windows 的 11 条未注册名单里），`src/headless_server_probe.c:10` 的 `headless_init(18130)` 因此在 Windows 上跑不到。这是**没测到**，不是**不存在**。
- 只覆盖了 **74 条直接以 `inimerse.exe` 为 COMMAND 的测试**。python／node 类测试若 fork 引擎会走同一条启动路径，但 gdb 不跟子进程，**未覆盖**。
- `child_proc.c` **是否能编进 POSIX 分支 —— 未测**。文件里没有一行 `_WIN32`、头文件为非 Windows 备了 `DWORD` typedef、它调用的 `im_process_*` 来自 `src/platform/process.c`（两个分支都列），这些都只是**读码候选**，不构成「可以」。
- `child_proc_init` 没有外部调用者：它由 `src/child_proc.c:12` 的 `child_proc_spawn` 惰性调用（`if(!g_inited)child_proc_init();`）。所以「有没有测试碰 `child_proc.c`」等价于「有没有测试碰 `child_proc_spawn` / `child_proc_list`」，本表量的是后者。

## 6. 「仅 Windows」有两义，不要混写

- **仅 Windows（空桩）**：`gui_mod.c`、`io_mod.c`、`identity_mod.c`、`social_mod.c`、`ai_mod.c`。POSIX 侧由 `src/platform/posix_stubs.c:6-8` 的 `STUB_REG(...)` 顶成空函数（宏在 `:5`），功能确实只在 Windows。这些模块在 POSIX 上是**静默缺席**：`:4` 的 `unsupported()`（会打印 `inimerse: capability '%s' is not available on this POSIX build yet`）只被 `:9` 的 `build_project_impl` 调用，六个 `STUB_REG` 展开成 `void name(VM *vm) { (void)vm; }`，什么都不打印。
- **仅 Windows（构建表所致）**：`child_proc.c`。文件本身是跨平台的（注释这么写、代码这么写、头文件还专门给了非 Windows 的 `DWORD`），**是 `CMakeLists.txt` 只在 `if(WIN32)` 里列它**。

**这两义混在一列里，表就把一个「有人决定不编」说成了一个「文件只能这样」。** 本表因此把它们分开写。

## 7. 这份文档的边界

- 它是**记录**，不是断言：表中每一格都指到 `d27413a` 的某个文件某一行，或指到一次可重跑的 gdb 计数。换 ref 请重量，不要搬运。
- 它不修改 `docs/AUDIT.md` §1.71；§1.71 末尾的一行指针由协调者加。
- 它不裁定 `child_proc.c` 该不该编进 POSIX 分支 —— 那是人类/协调者的决定，本表只把它现在的状态量清楚。
