# stream/be-removal —— `be` 构造移除，声明形状唯一化为 `名字: 集合 [= 初值]`

- **基线**：`main` = `f6b3d87`（`git rev-parse main`，未动）
- **分支/工作树**：`stream/be-removal` / `.worktrees/be-removal`
- **机器**：`sakiko-WSL`（`nproc` 见下），2025-10-05
- **裁定**（人，2026-10）：`be` **立即移除，不留等价别名**；唯一声明形状是 `名字: 集合 [= 初值]`。取代此前「保留纯脱糖别名」的判断。

## 1. 为什么 `TOK_BE` 留着不删（这是刻意的）

移除的是**构造**，不是那个词在词法表里的位置。把 `be` 降级成普通标识符，旧写法会静默变形：

| 旧写法 | 降级后的结果 |
|---|---|
| `be = 5` | 变成「给变量 `be` 赋值 5」，**不报错** |
| `x be Byte: 42` | 裂成三条语句（表达式 `x`、表达式 `be`、声明 `Byte: 42`），**无声改写全局 `Byte`** |

保留 `TOK_BE` 之后，三条旧写法各报一条**带行号**的解析错误（`src/parser/parser.c:1376-1387`），exit 1。
对照实验（同一次构建）：`x zz Byte: 42`（`zz` 是普通标识符）→ **exit 0 且打印 `Byte=nil`**，即没有墓碑时的行为。

## 2. 实现面（12 个文件）

`src/parser/parser.c`（`starts_collection_expr` `:108`、标签闸门 `:1396`、`STMT_BIND` 分支 `:1415-1420`、墓碑 `:1376-1387`）、
`src/parser/ast.h`、`src/compiler/compiler.c`、`src/compiler/bytecode.h`、
`src/lexer/lexer.c:43`、`src/lexer/lexer.h:33`、`src/vm/vm.c`、`src/vm/vm.h`、
`src/runtime/runtime.c`、`src/runtime/runtime_posix.c`、`src/runtime/vm_exec_builtin.c`、
**`src/lint_mod.c:309`（11 文件清单漏掉的第 12 处）**。

`src/lint_mod.c:309` 的危险之处：它把源文件当**文本**重扫，用 `sscanf(dl, "%63s be %63s", dn, dt)` 找 `case <var>` 的有限类型 ——
不编译、不链接、不报错。**关键字的语义不只在源码里，还在扫描源码的代码里。** 现改为 `sscanf(dl, " %63[^: \t] : %63s", dn, dt)`。

`global_bound[]`（原 `be_bound[]`）的**三个消费者一行逻辑都没改**（它们只读数组，与语法无关）：
① `src/vm/vm.c:3681-3691` 的赋值重校验；② `builtin_range` / `posix_core_range`；③ `src/vm/vm.c:2770-2783` 的 GC 标记根。
登记 bound 的职责从已删除的 `be` 路径搬到 `:` 声明路径（对照 `src/compiler/compiler.c:2224-2232` 的 `STMT_TYPE` → `STORE_GLOBAL`）。

**硬顺序**（不能反）：先把三个消费者迁到 `:` 声明的全局上，再删 `be` 语法，最后迁 `.im`。反过来会有一个两边都不带重校验、约束静默失效的窗口。

## 3. 差分证据（旧写法 vs 新写法，逐条实跑）

差分方法：同一段程序写两份，`build/inimerse-before`（从主工作树复制的、改动前的二进制，先用吞掉探针验证过它给出 `x=nil`）跑旧写法，
`build/inimerse` 跑新写法，**stdout + exit code 逐字节比对**；两者都在同一目录下运行，保证相对 `include` 能解析。

| 文件 | 旧 → 新 | 结果 |
|---|---|---|
| `big_globals_test.im:168` | `g200 be N : 42` → `g200: N = 42` | SAME（`moduleA loaded` / `PASS: 300+ globals stored (incl idx>256)`） |
| `exc_test.im:11`、`projects/exc_test.im:11` | `gamemode be 0,1,2,3:0` → `gamemode: 0,1,2,3 = 0` | SAME（`in try` / `caught`） |
| `inf_set_test.im:115` | `gv be (0,10):5` → `gv: (0,10) = 5` | SAME（`true` / `true`） |
| `meta_test.im:45` | `g be 0,1,2,3:0` → `g: 0,1,2,3 = 0` | SAME（该文件在 `f6b3d87` 上就有**前置**解析错误 `expected 'expression', but got 'float' (type 30)`，改动前后一致；未注册 CTest） |
| `nsadv_mod.im:3` | `q be N : 5` → `q: N = 5` | SAME（`label_ok`） |
| `set_test.im:28` | `gamemode be 0,1,2,3:0` → `gamemode: 0,1,2,3 = 0` | SAME（`true` / `false`） |
| `vtest/type_collection_v04.im:3` | `x be Byte: 42` → `x: Byte = 42` | SAME（`typecoll x=42`，CTest `type_collection_runtime` ✓） |
| `vtest/lint_case_enum_v04.im:2`、`vtest/lint_case_membership_v04.im:2` | `dir be Direction = "N"` → `dir: Direction = "N"` | 改动前**根本不可解析**（`Error: expected 'expression', but got '=' (type 83)`），现在能跑（`north` / `north`、`all-directions` / `all-directions-bound`）；它们是只为 `--lint` 存在的 fixture，从来不是合法程序 |

另有 9 条 `c1`–`c9` 探针（`type Byte = [0~255]` / `x: Byte = 42` / `x = 300` / `x: Byte` / 无初值 / 越界初值 等）逐条 SAME。
迁移后 `git grep -nE "^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]+be[[:space:]]" -- '*.im'` → **零命中**。

### 3.1 标签冲突（不是理论问题，是实测撞上的）

`Label: stmt` 这条形式与新声明形状撞车。仓库里只有三处，全部 `<IDENT> : <IDENT> = <expr>`：

- `examples/regressions/label_test.im:49` `M: m = m + 1`；`:55` `SKIP: k = k + 1`；`examples/regressions/thread_test.im:25` `A: j = 0`。

**未改动的原文件在新解析器下实测**：`label_test.im` → exit 1 `error: unknown label 'SKIP'`（`SKIP: k = k + 1` 被读成声明，标签消失）；
`thread_test.im` → exit 0 但 `j` 从 `308177` 变成 `-1`（`jumpy to A` 不再到达 A）。

三处改为 `Label: { … }`，并**补上真实的跳转用例**（原文件里这三个标签没有任何 `goto`/`to` 指向，只被 fall-through 经过）：

- `M: { m = m + 1 }` + 向后跳 `if m < 3 { to M }`；
- `SKIP: { k = k + 1 }` + 向前跳 `to SKIP`（**逐字比对**：`say k` 必须仍是 `1`，它依赖「`role := body` 且同缩进 continuation」，花括号化正好切断这个依赖）；
- `A: { hits = hits + 1` / `j = 0 }` —— 用确定性的 `hits` 计数替换掉不确定的 `j` 自旋计数。

结果：`label_test.im` 新旧两种写法 **逐字节相同** `5 4 3 5 2 3 1 done`（向后跳使 m 到 3，向前跳后 k 仍是 1）；
`thread_test.im` 两种写法都是 `hits=1` 然后 `hits=2`。

## 4. 测试

新增 5 条（`CMakeLists.txt`）：`be_removed_decl_runtime` / `be_removed_assign_runtime` / `be_removed_bare_runtime` /
`bind_init_overflow_runtime` / `gc_bound_root`。前三条钉住「`be` 现在报带行号的语法错误」，第四条钉住 `initial value out of range` 这条消息。

**GC 根用例是真咬合的**（`vtest/gc_bound_root_v04.im`）：被约束的全局除 `global_bound[]` 外**没有任何 C 侧持有者**（全局里存的是值 2，不是集合）。
变异验证：注掉 `src/vm/vm.c:2770` 的标记根 ⇒ 该测试 **红**（`Required regular expression not found`）；恢复 ⇒ 绿。
第一版用例**没能**发现这个变异，原因是它 churn 的是**数组**（`[i, …]`）而集合池的槽位只会被下一次 **`vm_set_new` 拿走**（`src/vm/vm.c:1749-1760` 先弹 `set_free_list`）—— 改成 churn 集合（`churn = i, i + 1`，走 `src/parser/parser.c:1712` 的赋值路径）之后才咬合。

**两条 CTest 记账教训**（都是实测出来的，不是读文档读来的）：

1. 设了 `PASS_REGULAR_EXPRESSION` 之后 CTest **不看退出码**，只看正则；此时再加 `WILL_FAIL TRUE` 会把「匹配上」**反转成失败**。仓库里原本没有 `WILL_FAIL`，不要引入。
2. `PASS_REGULAR_EXPRESSION` 里的 **`;` 是 OR 不是 AND**（CTest 文档语义：至少一条匹配即通过）。
   实测：把标记之一漏掉、只留 `uncaught: type_mismatch` 也照样 Passed。⇒ 想钉住多条输出必须写成**一条**带 `.*` 的正则。
   推论：仓库里现有多标记 `;` 列表（例如 `gc_runtime` 的六条）**比看上去弱得多** —— 这是既有面，本条流未改动它们。

全量 ctest：**142 / 142 通过、0 跳过**（基线 137 + 本流 5）。计数是断言，已在三处同步：`tools/gate.sh` 的 `EXP_CTEST`、`docs/BOARD.md` §3、`docs/STATUS.md` §1 与 §2 的命令注释。

## 5. 登记为缺口（**不在这条流里实现**）

1. **`Byte` 不可枚举**：`size(Byte)` / `len(Byte)` / `list(Byte)` 给出 `nil` / `0` / `nil`，应当是 **256**。
   机制：`src/vm/vm.c:2182` 的 `if (c->nameIdx < 0 || c->nameIdx > 23) return -1;` —— R 是 **24**，而 `parse_bare_interval`（`src/parser/parser.c:497`）把每个裸区间都标成 base `"R"`。
   **不能只删闸门**：`src/vm/vm.c:2187` 的 `isInt = (c->nameIdx <= 3)` 是承重的（对 24 为假 ⇒ `frac = 11` ⇒ `scale = 10^11` ⇒ 一个假的 10⁻¹¹ 点阵）。要先裁定「R 的格点是什么」。
2. **`x.range` 的语义（T2）**：人已裁定 `x.range` 改为**纯值推断**（语法无关），声明另开 `x.declared`（无声明为 `nil`）。
   本流按**机械迁移、行为不变**处理，没有顺手改：今天 `x.range` 对受约束的 `x` 给 `set(R interval)`，而对 `arr[0].range` 给 `set(Z interval)` —— 同一个值两条路两个答案，T2 之后应合成一个答案。

## 6. 复现命令

```sh
cd .worktrees/be-removal
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j"$(nproc)"    # 期望 142/142、0 skipped
tools/gate.sh                                              # 全门禁
```

单点复现：

```sh
printf 'type Byte = [0~255]\nx: Byte = 42\nsay "x=" + str(x)\nx = 300\n' > /tmp/t.im && ./build/inimerse /tmp/t.im   # x=42 然后 uncaught: type_mismatch
./build/inimerse vtest/be_removed_decl_v04.im                                                                       # 带行号的 `be` 已移除错误
```
