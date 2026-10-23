# 验收：`win-source-attribution` 的十二个「只在 WIN32 分支里」的源文件

- **验收者**：`honest-island`（`session-7546de99-9b6e-4d4b-9a4b-4009464e76cb`）。角色是**独立第二读者**：只读、只判，不履约。
- **基线**：`main` = `a16dae224c5cfb2b0d13f97d510376dcaab0b7eb`（开工时 `git -C /home/sakiko/inimerse log --oneline -1` 逐字确认，工作树当时干净）。
- **分支 / 工作树**：`stream/win-source-attribution-verify` / `.worktrees/win-source-attribution-verify`（从 `a16dae2` 起）。
- **被验收对象**：`docs/BOARD.md` 里状态列是 `待验收` 的那条 `win-source-attribution` 行（**按内容找的，不是按行号**），以及它指向的交付 [streams/win-source-attribution.md](win-source-attribution.md)。
- **写域**：本文件是唯一的产出。`docs/BOARD.md`、`docs/AUDIT.md`、`src/**`、`CMakeLists.txt` **一行未动**。
- **机器 / 目录 / 时刻**：`LAPTOP-ALLQECIL`（WSL）/ `/home/sakiko/inimerse` 与上面那个工作树 / 2026-10-22 傍晚（+08:00）。

## 1. 先把两个口径分开：occ 18 ／ rows 12

「十二个」在这份语料上有两个都正确的读数，差 6，必须并排写：

| 口径 | 数 | 定义 |
|---|---|---|
| **rows** | **12** | 逐条枚举出的**不同文件**条数 |
| **occ** | **18** | 这 12 个路径字面量在 `CMakeLists.txt` 里**出现的次数** |

那 6 次差额是**源文件清单之外的注释提及**，逐条分摊如下：

| 文件 | occ | 清单外的提及 |
|---|---|---|
| `src/runtime/runtime.c` | 6 | **5** 处（注释里引用它的行号与行为） |
| `src/mod/io_mod.c` | 2 | **1** 处（注释里引用 `runtime_register_builtins` 与它的关系） |
| 其余 10 个 | 各 1 | 0 |

⇒ **「只在 `if(WIN32)` 里出现」这个短语必须读成「作为源文件条目，只在 WIN32 分支里出现」**。读成「全文只在那一段出现一次」是**错的**：另有 6 处是散文注释在提这些路径。这不是措辞洁癖——`occ` 口径下 18 与 `rows` 口径下 12 都「对」，只报一个数就会让下一个人以为另一个数是错的。本仓库在别处踩过同一个坑（`docs/DECFY_DESIGN.md` 的 `[实测]`：occ 18 ／ 行首字面 16）。

## 2. 两条独立枚举路径，给出同一组 12 个

### 方法 A（CMakeLists 的集合差：WIN32 源码列表 − POSIX 源码列表）

先按内容锚定那一块，而不是按行号：找**后面紧跟 `list(APPEND INIMERSE_ENGINE_SOURCES` 的那个 `if(WIN32)`**。可复现命令（逐字，在仓库根跑）：

```sh
python3 - <<'PY'
import re
L=open('CMakeLists.txt',encoding='utf-8').read().splitlines()
i=next(k for k,l in enumerate(L) if l.strip()=='if(WIN32)' and 'list(APPEND INIMERSE_ENGINE_SOURCES' in L[k+1])
j=next(k for k,l in enumerate(L[i:],i) if l.strip()=='else()')
k=next(m for m,l in enumerate(L[j:],j) if l.strip()=='endif()')
W=set(re.findall(r'src/\S+?\.c',' '.join(L[i:j]))); P=set(re.findall(r'src/\S+?\.c',' '.join(L[j:k])))
only=sorted(W-P)
print("rows =",len(only)); print('\n'.join(only))
PY
```

输出逐字：`rows = 12`，随后 12 行（`child_proc.c`、`headless_server.c`、`runtime.c`、`mod.c`、`gui_mod.c`、`io_mod.c`、`identity_mod.c`、`social_mod.c`、`ai_mod.c`、`net_mod.c`、`server_mod.c`、`say_mod_windows.c`，均带 `src/` 前缀）。**它命中的块是唯一的**：`awk` 实测该 `if(WIN32)` 与配对 `endif()` 之间只有 426–444 这一段（`CMakeLists.txt` 全文另有 11 处 `if(WIN32)`，都不带 `list(APPEND INIMERSE_ENGINE_SOURCES`）。

### 方法 B（被 git 跟踪的 `src/**/*.c` 与 Linux 构建产出的 `.o` 求差）

```
被跟踪的 src 下的 .c        = 115   （src/*.c 8 个 ∪ src/**/*.c 107 个）
build/ 下 .o 的去后缀名字    = 104
两者之差（没有 .o 的）      = 12
```

**两法给出的集合逐条相等**（我做过双向差集，两个方向都为空）。方法 B 与 §1.71 记的「114 / 102 / 12」只差一个分母：`c76273f` 时被跟踪的是 **114**，今天 **115** —— 差额是 `src/types/error_cli.c`（`c76273f..a16dae2` 之间唯一新增的 `.c`，其余 11 个是修改）。**分母移动了，那 12 个的成员一个没动。**

**自捉一条（尺子，不是语料）**：方法 B 第一遍我写错了——CMake 把对象命名成 `<name>.c.o`，我用 `splitext` 去掉一层 `.o` 后名字停在 `<name>.c`，于是 **115 个全被判成「没有 `.o`」**。修法是把 `.c.o` 与 `.c` 都剥掉。这个 115 与「真的 12」长得一样像读数，区别只在于**去后缀去了几层**。

## 3. 逐条裁定（三分类：成立 ／ 不成立 ／ 证据不足）

三问：① 是否列在 `CMakeLists.txt` 的 WIN32 源码分支 ② 有无 POSIX 对应物 ③ 有没有任何注册测试可达它。

| # | 文件 | ① | ② | ③ | 判词 |
|---|---|---|---|---|---|
| 1 | `src/runtime/runtime.c` | 成立 | 成立：`src/runtime/runtime_posix.c`（互斥的 else 分支里） | 成立：`runtime_register_builtins` 73/74 | **成立**（两平台各一份实现） |
| 2 | `src/child_proc.c` | 成立 | 成立：**无对等文件、也无空桩** | 成立：`child_proc_spawn` / `child_proc_list` **0/74** | **成立**（仅 Windows，**构建表所致**） |
| 3 | `src/headless_server.c` | 成立 | 成立：`src/headless_server_posix.c` | 成立：`headless_init` / `headless_loop` **0/74** | **成立**（两平台各一份实现） |
| 4 | `src/mod/mod.c` | 成立 | 成立：`src/mod/mod_posix.c` | 成立：`mod_load_all` 57/74 | **成立** |
| 5 | `src/mod/gui_mod.c` | 成立 | 成立：空桩 `STUB_REG(gui_mod_register)` | 成立：73/74 | **成立**（仅 Windows，**空桩**） |
| 6 | `src/mod/io_mod.c` | 成立 | 成立：空桩 | 成立：73/74 | **成立**（空桩） |
| 7 | `src/mod/identity_mod.c` | 成立 | 成立：空桩 | 成立：73/74 | **成立**（空桩） |
| 8 | `src/mod/social_mod.c` | 成立 | 成立：空桩 | 成立：73/74 | **成立**（空桩） |
| 9 | `src/mod/ai_mod.c` | 成立 | 成立：空桩 | 成立：73/74 | **成立**（空桩） |
| 10 | `src/mod/net_mod.c` | 成立 | 成立：`src/mod/net_mod_posix.c` | 成立：73/74 | **成立** |
| 11 | `src/mod/server_mod.c` | 成立 | 成立：`src/mod/server_mod_posix.c` | 成立：73/74 | **成立** |
| 12 | `src/mod/say_mod_windows.c` | 成立 | 成立：`src/mod/say_mod_posix.c` | 成立：73/74 | **成立** |

**统计：成立 12 ／ 不成立 0 ／ 证据不足 0**（这个 0 的边界在 §6：③ 的所有读数都是 `d27413a` 上的，今天树没有重取）。

### 每一条我实际看到的原文

①（12/12）我读到的是那一块本身，不是表里的行号。逐条文件名与其所在行（**行号只作定位、不作判据**）：

```
  if(WIN32)
    list(APPEND INIMERSE_ENGINE_SOURCES
      src/platform/process.c src/platform/socket.c
      src/runtime/runtime.c src/child_proc.c src/headless_server.c        ← 1,2,3
      src/isolate_mod.c src/desugar_mod.c src/lint_mod.c src/mod/mod.c    ← 4
      src/mod/gui_mod.c src/mod/io_mod.c src/mod/net_mod.c src/mod/json_mod.c   ← 5,6,10
      src/mod/record_mod.c src/mod/replay_mod.c src/mod/infiverse_mod.c src/mod/verse_dist_mod.c
      src/mod/server_mod.c src/mod/identity_mod.c src/mod/social_mod.c    ← 11,7,8
      src/mod/ai_mod.c src/mod/result_mod.c mods/build/build_mod.c        ← 9
      src/verse/json_min.c)
    list(APPEND INIMERSE_ENGINE_SOURCES src/mod/say_mod_windows.c src/mod/say_stream.c  ← 12
  else()
```

② 的六个对等实现我逐个 `test -f` 过（`runtime_posix.c`、`headless_server_posix.c`、`mod_posix.c`、`net_mod_posix.c`、`server_mod_posix.c`、`say_mod_posix.c` 全部存在且被跟踪）；五个空桩我读到的是 `src/platform/posix_stubs.c` 的头部，逐字：

```
static void unsupported(const char *name) { fprintf(stderr, "inimerse: capability '%s' is not available on this POSIX build yet\n", name); }
#define STUB_REG(name) void name(VM *vm) { (void)vm; }
STUB_REG(gui_mod_register) STUB_REG(build_mod_register) STUB_REG(io_mod_register)
STUB_REG(identity_mod_register)
STUB_REG(social_mod_register) STUB_REG(ai_mod_register)
```

⇒ 这条**独立证实了 §1.71 补记的那句「登记得不全」**：桩有六个（`gui`、`build`、`io`、`identity`、`social`、`ai`），而其他文档只登记了三个。我这一遍没有看到第七个桩。

③ 的机制我读了源码、不是只抄表：`src/main.c` 里 `register_core_modules(VM *vm)`（函数体在 571 行）**无条件调用**八个 `*_mod_register`，两个调用点（1067、1097）都在 `if (load_mods) register_world_modules(...)` **之前**；`--no-mods` 只是把 `load_mods` 置 0（1000 行）。⇒ 八个注册函数在**任何一次启动 VM 的测试**里都会命中，这与「73/74、唯一例外是 `version_runtime`」是同一件事，不是两条巧合。

`child_proc.c` 的「构建表所致」我也逐条核了：文件里 `grep -c '_WIN32'` = **0**；注释首行逐字 `/* Cross-platform child process registry. */`；头文件给了非 Windows 的替身（`#ifdef _WIN32` → `<windows.h>`，`#else` → `typedef uint64_t DWORD;`）；`grep -n child_proc CMakeLists.txt` **只有一行**命中（就是上面那块里的 429 行）。⇒ 文件本身跨平台，是**清单**没列它。

## 4. 我另外核了的四条（不在三问里，但属于这条行）

1. **十二个都真的编进了 Windows 引擎**：`d27413a` 导出树的 `inimerse_engine.dir` 下十二个 `.obj` 逐个在，字节数 53717 / 4460 / 6616 / 3028 / 204681 / 33413 / 11885 / 6592 / 17654 / 13818 / 13391 / 11051。
2. **④ 复现**：`git diff --name-only 9969e5b 0077c90` 只输出 `docs/streams/win-source-attribution.md` 一个文件。
3. **③ 的「没测到」口径**：`grep -c '没测到'` = **2**，与判据栏一致。
4. **静态旁证（不是重取）**：`git grep` 在 `vtest/` 里对 `child_proc` / `--headless` 的命中是 **0**；对 `file_exists`、`io_list_dir`、`ai_list`、`clipboard_set`、`http_get` 的命中也各是 **0**。⇒ 这十个名字在测试语料里根本没有被调用，与「0/74」互相印证；它解释得通，但**它本身不是那次测量**。

## 5. 与原文的差异（三处，都在尺子层，不动任何一个结论）

| # | 差在哪 | 是否需要改原文 |
|---|---|---|
| 1 | **occ 18 ／ rows 12 的分叉**（6 处清单外注释提及） | 「只在 `if(WIN32)` 里出现」应限定为**源文件条目**口径；否则会与全文口径冲突 |
| 2 | **§1.71 的分母 114 → 今天的 115**（`src/types/error_cli.c` 新增） | 不需要：12 个成员一个没变；但「114」是 `c76273f` 的读数，今天照抄就过期了 |
| 3 | 我自己的方法 B 第一遍**去了几层后缀**算错（115 missing） | 不需要：已修，记在这里是因为它与「12」长得一样像读数 |

## 6. 边界（这一节是判词的一部分）

- **③ 的全部读数都是 `d27413a` 上的。** 今天树没有重取：Windows 侧导出树仍钉在 `d27413a`（`build-n/CTestTestfile.cmake` 的 `grep -c 'add_test('` = 148 钉身份）。我在本会话里对**同一棵树、同一个 ref** 做过一次 74/74 的重取，与记录逐条零差异——但那是**复核**，不是另一次测量。
- **「同机同树同 ref 的重取」不是另一台机器的独立复现。** 它证明的是「原始件可复核且可重跑」，不是「这个结论在任何机器上都成立」。
- **方法 B 依赖增量 `build/`**，不是 CMake 目标图的解析：某个文件本该被编译却因增量状态缺 `.o`，会同样落进这 12 个里。这次两条路径交叉相同，降低了这个风险，但没有消除它。
- **② 的「空桩」只证明 POSIX 侧注册了一个空实现**，不证明调用者会看到「这个平台不支持」——§1.71 已登记 `unsupported()` 只被一个函数调用，六个 `STUB_REG` 一个字都不打。
- **本文件落在 `docs/streams/` 里**，所以 `tools/check_doc_paths.py` 对它**不作检查**（模块 docstring 逐字 `skipped  docs/streams/`）。`--only doc-paths` 绿，对本文件是**空绿**，不得当作它的依据；真正覆盖本文件的是 `--only links` 与 `--only line-refs`。

## 7. 建议协调者落

1. **状态**：本件我判为**内容成立**——十二个的集合、三问的归类、③ 的可达性、④ 的文件边界，我逐条独立复现后没有一条对不上。是否把状态列改掉，由你落；我不动 Board。
2. **建议加一句口径限定**：在判据栏或补记里写上「十二个」的 **occ 18 ／ rows 12**，并注明 6 处差额是清单外注释。这不是数字更正，是**读数口径的备案**——只报一个数，下一个人会以为另一个是错的。
3. 没有其它改动建议：② 的空桩少登记、③ 的「0/74 ∉ 不存在」、`child_proc.c` 的「意愿不是现状」，原文都已经写了，且与我的读数一致。

## 8. 判据栏 ①–⑥ 的逐条复核（同一次独立读数）

| 判据 | 我读到的 | 判 |
|---|---|---|
| ① 四项回报齐 | 四项的**编号**都在（`pwd` / `rev-parse HEAD` / 读法 / 原始输出），第四项现在指向那一行文件名与三个路径；但**逐字内容不在本文档里**（全文 71 行、围栏块 0） | **名存实缺**：位置 ≠ 内容。逐字补全在本会话另一支 `stream/win-source-attribution-evidence` 的**暂存区**里，未合入本树 |
| ② ③那一列实测 | 原始件在、可复现、可重跑（§3、§6） | 成立 |
| ③ 未测写成「没测到」 | `grep -c '没测到'` = 2 | 成立 |
| ④ 只动了那份文档 | `git diff --name-only 9969e5b 0077c90` 一个文件 | 成立 |
| ⑤ 三阶段分母 | 栏里那三个数是**写下那一刻**的；今天已在 `a16dae2` 上重数（见回执） | **已过期**（原文自己也这么写） |
| ⑥ `doc-paths` 空绿 | `skipped  docs/streams/` | 仍是空绿，**不是通过**；本文件也在同一盲区里 |

⇒ 判据 ① 是本条唯一「不成立」的一格，而它的修法不在这一支分支上：要么把 evidence 支的逐字内联合进来，要么在本文档里另写一次。**这一格我按「不成立」记，不按「通过」记**——因为该栏要求的是「原样输出」，而今天树上的文档只有文件名。
