# 内建名的平台归属普查

**观测点**：`main` = `fb18bd7`（普查本身在 `fa8247e` 上做，两棵树的结果相同，**行号不同** —— 见末节）。全部读数用 `git show <sha>:<path>` 与 `git grep <sha> -- src/` 取，**不读工作树**：普查开始时根工作树正在合并（`.git/MERGE_HEAD` 存在、`UU docs/BOARD.md`），同一个命令在两个 ref 上给出的行号差 11 —— **那不是文件变了，是树在动。**

## 1. 为什么有这份文件

`docs/AUDIT.md` §1.76 只处理了 `docs/SYNTAX.md` 那张「核心高频内建」名单上的**四个**名字（`file_exists`、`timer_ms`、`mkdir`、`list_dir`）。把同一份名单**逐个名字**跑一遍注册点与构建分支归属之后，**平台条件性注册的名字一共 10 个** —— 除那四个之外还有 **7 个**，而其中 **3 个就住在 §1.76 自己改过的那一行上**（`env`、`time_ms`、`sleep_ms`）。

⇒ **一份文档可以在它「已经修过」的那一行上，仍然留着同一个病。**

## 2. 判定口径

一个名字在某平台「有注册」= `git grep -n '"<name>"' <ref> -- src/` 里存在 `vm_register_builtin` 命中，**且该命中所在文件属于 `CMakeLists.txt` 的 `if(WIN32)` 源码分支或 `else()` 源码分支中的哪一支**（这两支的边界用内容定位，不写行号）。

- **平台条件性** = 注册点只落在两支中的**一支**里。
- **不是**平台条件性的对照：`mkdir` 有**两个**生产点（一支一个），文档标的是「两平台」，正确；另有 **56 个名字**是「同名、两个生产点、一个平台一个」（`runtime.c` ↔ `runtime_posix.c`），那是**两平台各一份实现**，不是条件性。

## 3. 表

**左侧两列（生产点 / 分支归属）的 ref 是 `fa8247e`；右侧两列（实跑读数）的 ref 见每格。** 每一格都写着它是在哪一侧、哪棵树上量的 —— 没有一格用另一侧的结果替它说话。

| 名字 | 生产点 @ `fa8247e` | 文件属哪一支 | POSIX 实跑（rc / 逐字） | Windows 实跑（rc / 逐字） |
|---|---|---|---|---|
| `file_exists` | `src/mod/io_mod.c:318` | `if(WIN32)` | **1** `unknown builtin function 'file_exists'`（§1.76） | 未跑（只有分支归属） |
| `timer_ms` | `src/mod/io_mod.c:337` | `if(WIN32)` | **1** `unknown builtin function 'timer_ms'`（§1.76） | 未跑（只有分支归属） |
| `mkdir` | `src/mod/io_mod.c:319` + `src/runtime/runtime_posix.c:1183` | 两支各一个 | **0** `true`（§1.76） | 未跑（只有分支归属） |
| `list_dir` | `src/runtime/runtime_posix.c:1211` | `else()` | **0**（§1.76） | 未跑（只有分支归属） |
| **`env`** | `src/runtime/runtime_posix.c:1209` | `else()` | **0** `/home/sakiko` | **1** `unknown builtin function 'env'` |
| **`sleep_ms`** | `src/runtime/runtime_posix.c:1205` | `else()` | **0** `1` | **1** `unknown builtin function 'sleep_ms'` |
| **`time_ms`** | `src/runtime/runtime_posix.c:1204` | `else()` | **0** `20726978` | **1** `unknown builtin function 'time_ms'` |
| **`io_list_dir`** | `src/mod/io_mod.c:323` | `if(WIN32)` | **1** `unknown builtin function 'io_list_dir'` | **0** 空字符串（`/tmp` 在 Windows 上不存在） |
| **`key`** | `src/mod/gui_mod.c:3836` | `if(WIN32)` | **1** `unknown builtin function 'key'` | **0** `0` |
| **`rand`** | `src/mod/gui_mod.c:3848` | `if(WIN32)` | **1** `unknown builtin function 'rand'` | **0** `1` |
| **`window`** | `src/mod/gui_mod.c:3689` | `if(WIN32)` | **1** `unknown builtin function 'window'`（见 §5） | **0** 无输出 |

**实跑读数的两棵树（不是同一棵，明写）**：POSIX 侧取自 `439df94` 的导出树现建的引擎，并用仓库自带的 `build/inimerse` 复核，**两个二进制逐条一致**（含 `ip=3`/`ip=5`/`ip=7` 三个偏移量）；Windows 侧取自 `/mnt/d/inim-rel/d27413a-src/build-n/inimerse.exe`（`--version` → `inimerse 0.5.2`），即 **`d27413a`**。⇒ **两侧不是同一棵树**，表里每一格各自绑各自的。

**文档有没有标注**（`docs/SYNTAX.md` @ `fb18bd7`）：§1.76 已标四个；**`env`/`sleep_ms`/`time_ms` 三个未标**（它们就在 §1.76 改过的那一行上）；`io_list_dir` 只作为 `list_dir` 括注里的名字出现；`key` 作为内建**从未被列出**；`rand` 见 §4；`window` 见 §5。

## 4. `rand`：文档自己给的反例

`docs/SYNTAX.md` §5 更正块逐字写着「**⚠ 这张名单曾经是错的（2026-10 更正）**：`rand` 从未被任何文件注册（`grep -rn '"rand"' src/` 零命中）」。而**它自己引的那条命令**在 `fa8247e` 上恰好给 **1 条命中**：

```
$ git grep -n '"rand"' fa8247e -- src/
fa8247e:src/mod/gui_mod.c:3848:    vm_register_builtin(vm, "rand", builtin_gui_rand);
```

那一行来自 `8248e08 Release Infiverse 0.2.0`（2026-08-27 05:56 +0800），**早于** 2026-10 那次更正 ⇒ **「零命中」这句话写下的当时就已经不成立。**

**它真剩下的那半句是对的**：`src/mod/gui_mod.c` 只在 `if(WIN32)` 分支里 ⇒ POSIX 上 `rand` 确实不存在（上表 rc=1 实测）。⇒ **真正的病是「把『这个平台上没有』写成了『任何文件里都没有』」** —— 与 §1.76 同形，而**证伪者就住在被告席旁边、没人去问**。

## 5. `window` 必须分两层写

**作为关键字**，两个平台都有通路，而且那三个文件在 `CMakeLists.txt` 的**公共源码清单**里（不在 `if(WIN32)` / `else()` 任何一支）：`src/parser/parser.c:241`（`TOK_WINDOW` → `fname = "window"`）、`src/compiler/compiler.c:2205`（@ `344a37b`：`lookup_builtin(comp, "window")`）、`src/compiler/compiler.c:2927`（@ `344a37b`：把 `"window"` 写进编译器自己的内建名表）。

**作为 VM 侧内建注册点**，只有 `src/mod/gui_mod.c:3689`，即**只有 Windows**。

⇒ POSIX 上 `window(...)` **能解析、能编译**，运行时得到 `unknown builtin function 'window'`。**这一条是预测在先、实测在后的**（跑之前就写下「应该是 VM 运行期异常，不是语法错、不是编译错」），实测逐字：

```
$ build/inimerse --no-mods window.im        # window(800, 600, "t")
rc=1
[exception] uncaught: unknown builtin function 'window'
  at ip=7 frames=0
```

`ip=7` 是一个**有效的字节码指令指针** ⇒ 解析过了、编译过了、跑到了调用点才没有这个内建。Windows 侧同一条脚本 **rc=0、无输出**。

⇒ **§4.8/§6.2 列的是关键字，那里「没标平台」是对的；要标的是「这个关键字在 POSIX 上没有 VM 实现」。** 把两层合成一句，会把一条构建清单的事实写成文件的性质。

## 6. 一条能重跑整个普查的命令

普查脚本在 `/tmp/syntax-census/census.sh <ref>`（未进仓库）。它对 `docs/SYNTAX.md` 里**每个反引号标识符**跑三步，只输出条件性的：

1. 名字的注册点：`git grep -n '"<name>"' <ref> -- src/` 里含 `vm_register_builtin` 的那些行；
2. 该行文件属于哪一支：`<ref>:CMakeLists.txt` 的 `if(WIN32)` / `else()` 两张清单（按内容定位，不写行号）；
3. 文档位置与紧邻括注：`<ref>:docs/SYNTAX.md` 里 `` `name` `` 后紧跟的 `（…）`。

它在 `fa8247e` 与 `158a891` 上给**同样的 10 条**，但**行号全不一样** —— 这正是第 1 节那句的实测形态。

## 7. 未测（写「没测到」，不是「不存在」）

- **10 个名字各有几侧被实跑过，不是整齐的**：7 个（`env`、`sleep_ms`、`time_ms`、`io_list_dir`、`key`、`rand`、`window`）**两侧都跑了**；`file_exists`、`timer_ms`、`mkdir`、`list_dir` 这 4 个**只有 POSIX 一侧是实跑读数**，它们的 Windows 侧**仍然只是分支归属**。⇒ **「每个名字都至少有一侧被实跑过」成立；「两侧都实跑过」不成立。**
- **两侧不是同一棵树**（POSIX `439df94` / Windows `d27413a`），上表已逐格标注；**没有在同一个 ref 上跑过两侧**。
- 仓库自带的 `build/inimerse` **是从哪个 ref 配置出来的，没测到**（它 `find src -name '*.c' -newer build/inimerse` 为空、`git diff HEAD -- src/` 为空 ⇒ 工作区 src == HEAD src，但这不等于该二进制 == HEAD 的构建）。为消掉这条边界，另从 `git archive 439df94` 现建了一个引擎复核，两者一致。
- `docs/SYNTAX.md` 里 **269 个反引号标识符在 `src/` 里没有任何 `vm_register_builtin` 生产点**（多为 token 名、类型名、测试名、C 函数名），**没有逐个判它们是不是内建** —— 没测到。**这个数是一条记录，不是一条可重跑的断言**：产出它的脚本是 `vivid-anchor` 的 `/tmp/syntax-census/nonreg.py`（**不在仓库里**），判据是「`docs/SYNTAX.md` 里被反引号括起的标识符（不含点）中，在 `src/` 里找不到以 `vm_register_builtin` 起头、第二实参为该名字的语句行」。**要重跑请重写这条判据**；两轮实测（`fa8247e` 与 `344a37b`）都得 269，但**两轮的规则不同**（第一版正则允许带点，得 287）。
- `vtest/` 覆盖情况这一轮没查（§1.76 的那一半沿用它的结论，**没有重量**）。

## 8. 一条顺手测出来、归属未定的旁证

Windows 侧**每一条运行**都在 stderr 第一行打印：

```
[vm] builtin 'gui_say' is already registered; the first one stays
```

**七个名字、七次运行，七次都打。** 这是 `vm_register_builtin` 的重复注册守卫在说话 —— 即 `src/mod/gui_mod.c` 里 `gui_fullscreen` 那个重复注册被修掉之后，**`gui_say` 还留着一个**。**第二个注册点在哪，没测到** ⇒ 本节只登记读数，不判归属。（与 §9 第 53 条「两个 body 哪个该注册由人决定」同族，但**没有验证过它们同源**。）

## 9. 边界

- 「平台条件性」= **源码分支归属**，不是运行读数。它不随文档编辑改变，但**会随 `CMakeLists.txt` 或注册点改动而变，而且没有站岗者** —— 这份文件本身就是它唯一的记录。
- 本表所有行号**只在这份文件里**，且都写成 `<ref> @ <sha>` + 行号；**没有把 `CMakeLists.txt` / `tools/gate.sh` 的行号写进任何 `docs/**`**（那会移动 `tools/check_line_refs.py` 的 pin）。
- `docs/streams/` 被 `tools/check_doc_paths.py` **按构造跳过**（docstring：internal per-stream work orders, not delivered docs）⇒ 这份文件在 `doc-paths` 那一行上**是空绿**；它在 `links` 与 `text-integrity` 的扫描集里（分母会各 +1）。
