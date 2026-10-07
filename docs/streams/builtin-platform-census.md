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

**七个名字、七次运行，七次都打。** 这是 `vm_register_builtin` 的重复注册守卫在说话 —— 即 `src/mod/gui_mod.c` 里 `gui_fullscreen` 那个重复注册被修掉之后，**`gui_say` 还留着一个**。**第二个注册点已测到**（`noble-zephyr`，观测点 `main`）：`src/mod/say_mod_windows.c:75` 逐字 `vm_register_builtin(vm, "say_target", say_target); vm_register_builtin(vm, "gui_say", say_console);`，而 `src/main.c` 里 `gui_mod_register(vm)` 在 `say_mod_register(vm)` 之前 ⇒ **活的是 `builtin_gui_say`**（定义在 `src/mod/gui_mod.c:2828`），第二个被 `vm_register_builtin` 的守卫拒绝。⇒ **本条归属已定**：代价是**每次 Windows 引擎启动印一行 stderr，包括每一次 CTest**。（与 §9 第 53 条同源。）

★ 而仓库早就为它写了一条**正确的断言**：门禁的 `stage_ctest` 抓整份 `ctest` 输出里的 `is already registered` 并据此非零退出。**但那条断言跑在看不见它的平台上** —— 那两个文件在 Linux 上不编，而 Windows 作业只跑 `ctest`、不跑 `gate.sh`。⇒ **一条正确的断言，被放在了结构上看不见这个缺陷的那一侧。**

## 9. 边界

- 「平台条件性」= **源码分支归属**，不是运行读数。它不随文档编辑改变，但**会随 `CMakeLists.txt` 或注册点改动而变，而且没有站岗者** —— 这份文件本身就是它唯一的记录。
- 本表所有行号**只在这份文件里**，且都写成 `<ref> @ <sha>` + 行号；**没有把 `CMakeLists.txt` / `tools/gate.sh` 的行号写进任何 `docs/**`**（那会移动 `tools/check_line_refs.py` 的 pin）。
- `docs/streams/` 被 `tools/check_doc_paths.py` **按构造跳过**（docstring：internal per-stream work orders, not delivered docs）⇒ 这份文件在 `doc-paths` 那一行上**是空绿**；它在 `links` 与 `text-integrity` 的扫描集里（分母会各 +1）。

## 10. 增补：第三类 —— 两侧都有名字，一侧是桩（`noble-zephyr`，观测点 `main`）

**§3 那张表按【注册名】分类，因此有一类它看不见：名字两侧都有，而一侧注册到一个什么都不做的函数。**

`src/runtime/runtime_posix.c:718` 逐字：

```c
static int posix_unsupported(VM *vm) {
    int n = vm_cur_sp(vm) + 1; if (n > 0) vm_cur_set_sp(vm, -1); push_int(vm, -1); return 1;
}
```

**它清空整个栈、返回 `-1`。** 而 POSIX 侧把它注册给了 **5 个名字**：

| 名字 | POSIX | Windows |
|---|---|---|
| `key_press` | `src/runtime/runtime_posix.c:1219` → `posix_unsupported` | `src/mod/io_mod.c:330` → `builtin_key_press` |
| `mouse_move` | `src/runtime/runtime_posix.c:1220` → `posix_unsupported` | `src/mod/io_mod.c:331` → `builtin_mouse_move` |
| `mouse_click` | `src/runtime/runtime_posix.c:1221` → `posix_unsupported` | `src/mod/io_mod.c:332` → `builtin_mouse_click` |
| `load_params` | `src/runtime/runtime_posix.c:1264` → `posix_unsupported` | `src/runtime/runtime.c:1907` → `builtin_load_params` |
| `save_params` | `src/runtime/runtime_posix.c:1265` → `posix_unsupported` | `src/runtime/runtime.c:1908` → `builtin_save_params` |

⇒ **这 5 个名字两侧都有**，而 **POSIX 侧什么都不做** —— 注册名的静态对会判成「两侧都有」，**这一类在注册名上不可见。**

**正确归属**：不是「仅 Windows 有」，是「**两侧都有名字，一侧是桩**」。§3 表把前 5 个中的 `load_params` 之外的四个当成了「仅 Windows」的近亲，而 `load_params` 甚至已经被登记为一条「语义分歧」（`CMakeLists.txt` 里写成「on a missing file answers…」）—— ★ **而真相是 POSIX 上它是个桩，无论文件在不在都返回 `-1`** ⇒ **「已登记的分歧」这个标签也可以盖住一个更粗的事实。**

★ **`-1` 是一个合法值**：`save_params` 在真实失败时也返回 `-1` ⇒ **调用方分不出「不支持」与「保存失败」** —— 一个不存在的能力，被一个在成功与失败的语言里都已经有主的值回答。

### 判据必须从「名字」降到「实现体」，而降下来之后**形状判不出来**

`noble-zephyr` 量了三种形状（本仓 236 对同名注册点）：

| 形状 | 命中 | 精度 | 假阳性是谁 |
|---|---|---|---|
| S1 同一侧 ≥2 个注册名共享同一个 C 函数（别名组） | WIN32 24 组 / POSIX 24 组 / **不对称 4 组** | **1/4** | 合法别名：`say.console say_console`、`gui_wheel wheel`、`gui_say say.console say_console` |
| S2 不读参数 **且** 清空栈 | WIN32 0 / POSIX 3 | **1/3** | `posix_entity_clear`、`posix_entity_count` —— **正确的零参内建** |
| S3 短函数体 | 158/236 | 67% 假阳性 | 含 `entity_at`/`float`/`exec`/`http_get`/`net_*`/`lower` 等真实现 |

⇒ ★ **S2 的两个假阳性是正确的代码** ⇒ **一条基于形状的门禁会把正确的代码判成缺陷** —— **这不是「还不够准」，是用错了对象。精度可以调，对象不能。**

### 因此这一类只能走【显式清单】

**正确的定义**（`noble-zephyr`）：不是「一侧是桩」（不可判），而是

> **同一侧把 N 个不同实现塌缩成一个函数，而另一侧保持 N 个。**

**这个可判**（它是注册表的性质，不需要读函数体），**本仓今天只有 1 处**，而那 5 个名字在 Windows 侧**确实是 5 个不同函数**。

**能红的只有清单形式**：断言「**注册到被声明为桩的那个函数的名字集合 == 声明的清单**」。两个方向都要能红：加第 6 个名字而不更新清单；清单里有一个名字而它今天不再注册到那个函数。

⇒ ★ **「要么被写成一份显式清单，要么它就不存在」—— 第三类只能走清单那一支，因为形状判不出来。**

**边界**：三种形状都是**启发式**，假阳性率是用本仓 236 对同名注册点量的，**不是定理**；S3 的假阳性率**部分由那份脚本自己的白名单边界造成**（★ **工具量的是它自己的扫描范围** —— 这是同一位在这一轮第三次撞上同一形状）；全部是**读码**，没有在 Windows 上跑过。
