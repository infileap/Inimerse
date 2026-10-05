# surgery-verify — independent verification report

Verifier: teammate `surgery-verify` (adversarial). **I wrote no product code.** Every number below is
either **reproduced by me** (command shown verbatim, output shown verbatim) or explicitly marked
**未独立验证** / **无法判定**. A claim backed only by reading source is never recorded as 通过.

Reported to lead. My branch: `stream/surgery-verify`.

---

## 0. 方法与身份（全部由我本人执行）

**工作树**：`/home/sakiko/inimerse/.worktrees/surgery-verify`（`stream/surgery-verify`）。
**为核验冻结 ref，我建了三棵 detached 工作树**（`git worktree add --detach`，未移动任何分支指针）：

```bash
git worktree add --detach .worktrees/sv-ref-prev  c89e077~1
git worktree add --detach .worktrees/sv-ref-be    6d9bb77
git worktree add --detach .worktrees/sv-ref-count 01da596
```
每棵都用 `cmake -S <dir> -B <dir>/build -DCMAKE_BUILD_TYPE=Release && cmake --build <dir>/build -j$(nproc)`
构建，四个二进制均为退出码 0：

| 我称它为 | ref | 二进制 sha256 前 16 位 |
| --- | --- | --- |
| baseline | `f6b3d87` | `25c02b4ec865d9a3` |
| prev(WIP) | `6287523` = `c89e077~1` | `cc0302fc47fad6f0` |
| be | `6d9bb77` = `stream/be-removal` | `94be3616294e7af8` |
| count | `01da596` = `stream/count-size` | `aec7dae3ac3fedf6` |

**证据探针**均在 `svprobe/`（含 lead 替我提交的 10 个，以及我为本次核验新写的 `ln_decl/ln_assign/ln_bare/ln_shift/base_swallow/c_probe/c_multi/gc2/im/old/*`）。

**我本人跑的 CTest（不是转述）**：

```
baseline f6b3d87 : 100% tests passed, 0 tests failed out of 137   exit=0
be       6d9bb77 : 100% tests passed, 0 tests failed out of 142   exit=0
count    01da596 : run1 → 99% tests passed, 1 tests failed out of 138   exit=8
                   run2 → 100% tests passed, 0 tests failed out of 138  exit=0
prev     6287523 : 99% tests passed, 1 tests failed out of 137
```

---

## ① `be` 移除（`stream/be-removal @ 6d9bb77`）

### A① 三处墓碑报错是否真报错且带行号 —— **部分通过（附一处我实测出的缺陷）**

**我复现了。** 在完全由我构造、无注释干扰的探针上，行号是对的：

```bash
# svprobe/ln_decl.im: 1=type Byte = [0~255]  2=say "l2"  3=x be Byte: 42  4=say "l3-reached"
$ .worktrees/sv-ref-be/build/inimerse svprobe/ln_decl.im
Error at line 3: `be` declarations were removed (docs/SYNTAX.md 6.3); write `name: set [= init]` in place of `name be set [: init]`   [exit=1]
# svprobe/ln_assign.im: 1=say "l1"  2=be = 5  →  "Error at line 2: …"   [exit=1]
# svprobe/ln_bare.im:   1=say "l1"  2=be      →  "Error at line 2: …"   [exit=1]
```
三条 shipped 墓碑 CTest 在本机全过：

```
$ ctest --test-dir .worktrees/sv-ref-be/build -R 'be_removed|gc_bound_root|bind_init_overflow|type_collection_runtime|lint_case_enum_runtime|lint_case_membership_runtime'
1/8 type_collection_runtime ....... Passed
2/8 be_removed_decl_runtime ....... Passed
3/8 be_removed_assign_runtime ..... Passed
4/8 be_removed_bare_runtime ....... Passed
5/8 bind_init_overflow_runtime .... Passed
6/8 gc_bound_root ................. Passed
7/8 lint_case_enum_runtime ........ Passed
8/8 lint_case_membership_runtime .. Passed
100% tests passed, 0 tests failed out of 8
```

**缺陷（我实测）——墓碑报出的行号在带 `#` 注释的文件里与磁盘不符。** 决定性实验：

```bash
$ cat -n svprobe/ln_shift.im
     1  # c1
     2  # c2
     3  # c3
     4  be
$ .worktrees/sv-ref-be/build/inimerse svprobe/ln_shift.im
Error at line 1: `be` declarations were removed …
```
`be` 在磁盘第 **4** 行，墓碑报第 **1** 行：**全行 `#` 注释被预处理剥掉，行号按剥离后的源码计**。
这正是三条 shipped 墓碑自己中招的原因（它们都以成块 `#` 注释开头）：

| 文件 | `be` 在磁盘第几行 | 墓碑报的行号 |
| --- | --- | --- |
| `vtest/be_removed_decl_v04.im` | 7 | **2** |
| `vtest/be_removed_assign_v04.im` | 5 | **1** |
| `vtest/be_removed_bare_v04.im` | 2 | **1** |

我把 8 个真实受版控文件的**旧内容**喂给新引擎，逐个复现行号偏移：

```
big_globals_test.im   exit=1  Error at line 167: `be` declaratio…   (be 在磁盘 168)
exc_test.im           exit=1  Error at line 10:  `be` declaratio…   (11)
inf_set_test.im       exit=1  Error at line 108: `be` declaratio…   (115)
nsadv_mod.im          exit=1  Error at line 2:   `be` declaratio…   (3)
set_test.im           exit=1  Error at line 27:  `be` declaratio…   (28)
vtest/lint_case_enum_v04.im         exit=1  Error at line 2: `be` declaratio…   (2, 正确)
vtest/lint_case_membership_v04.im   exit=1  Error at line 2: `be` declaratio…   (2, 正确)
vtest/type_collection_v04.im        exit=1  Error at line 2: `be` declaratio…   (3)
```
结论：**报错本身是真的、行号在无注释文件里是对的；但对所有以注释开头的真实文件，行号系统性偏小**，而墓碑的自我说明把这条错误称为「可定位的语法错误」。这是一个**可用性缺陷**，不是崩溃：CTest 的 `PASS_REGULAR_EXPRESSION` 并不断言行号，所以门禁绿着也看不出来。

### A② 反向验证 —— **未通过（我给的这条判据依据本身是错的，我实测纠正了它）**

lead 指示「在 `c89e077~1` 上那三条是否真的不报错」。**该前提不成立**：

```bash
$ git log --oneline f6b3d87..6d9bb77
6d9bb77 vtest: note the part of the tombstone message the PASS regex does not require
c89e077 be 构造移除：声明形状唯一化为 名字: 集合 [= 初值]
6287523 WIP be-removal: "name: set [= init]" declaration shape + internal renames

$ git merge-base --is-ancestor 6287523 f6b3d87   # → 非零
NO 6287523 is NOT ancestor of f6b3d87
```
`c89e077~1 = 6287523` 是**移除分支自己的 WIP 提交**，不是「移除前」的状态。在它上面三条墓碑探针**已经**报错，只是报得差：

```
$ .worktrees/sv-ref-prev/build/inimerse vtest/be_removed_decl_v04.im
Error: expected 'expression', but got 'be' (type 128)      [exit=1]   ← 无行号、无墓碑文案
$ .worktrees/sv-ref-prev/build/inimerse vtest/be_removed_assign_v04.im
Error: expected 'expression', but got 'be' (type 128)      [exit=1]
$ .worktrees/sv-ref-prev/build/inimerse vtest/be_removed_bare_v04.im
Error: expected 'expression', but got 'be' (type 128)      [exit=1]
```
**改用真正的移除前基线 `f6b3d87` 重跑**（文件不在该 ref 上，故把 be 树里的文件用绝对路径喂给 baseline 二进制）：

```
$ build/inimerse .worktrees/sv-ref-be/vtest/be_removed_decl_v04.im
must not be reached                                          [exit=0]   ← 静默跑通 ⇒ 反向验证成立
$ build/inimerse .worktrees/sv-ref-be/vtest/be_removed_assign_v04.im
Error: expected 'expression', but got 'be' (type 128)        [exit=1]   ← 基线本来就报错
$ build/inimerse .worktrees/sv-ref-be/vtest/be_removed_bare_v04.im
Error: expected 'expression', but got 'be' (type 128)        [exit=1]   ← 基线本来就报错
```
**判定**：三条墓碑里**只有 `be_removed_decl` 真的在反向钉住一个「静默失败」**（基线 exit 0 并打印 `must not be reached`，即 `x be Byte: 42` 被当成合法语句执行到底）。`be_removed_assign` 与 `be_removed_bare` 在基线**已经是错误**，它们钉住的**不是静默失败，而是错误信息的质量与行号**（基线是 `expected 'expression'` 无行号，现在是具名墓碑 + 行号）。这不是缺陷，但**分工与 self-report 不同**，报告不能笼统写「三条都钉住静默失败」。

**我还实测推翻了墓碑文件自带的两条机制断言。** `vtest/be_removed_decl_v04.im` 的注释声称「移除前 `x be Byte: 42` 被当成标签块，结果 **x 保持 nil**、而全局 **Byte 被静默改写成 42**」。我把这条判据直接写成探针：

```im
# svprobe/base_swallow.im
type Byte = [0~255]
x be Byte: 42
say "x=" + str(x)
say "Byte=" + str(Byte)
```
```
$ build/inimerse svprobe/base_swallow.im            # 基线 f6b3d87
x=42
Byte=set(R interval)                                 [exit=0]
$ .worktrees/sv-ref-be/build/inimerse svprobe/base_swallow.im
Error at line 2: `be` declarations were removed …    [exit=1]
```
**两条都错**：基线 `x=42`（不是 nil），`Byte` 仍是区间集合（没被改写）。基线的 `x be Byte: 42` 其实是一条**正常工作的 `be` 声明**（类型别名求值成集合，42 在集合内）。所以这个墓碑守卫的是「旧语法整体被拒」这件事——那是真的——但它给出的**机制解释是错的**。

### A③ `global_bound[]` 三个消费者是否都还在工作 —— **通过（三个我都用运行时复现，不只读代码）**

**源码面**：`grep -rn 'be_bound' src/` 在 `6d9bb77` 上 = **0 命中**（彻底更名，无残留）。`global_bound` 共 **29** 处，三个消费者都在：`src/vm/vm.c:2770-2771`（GC 标记根）、`src/vm/vm.c:3681-3682`（`L_STORE_GLOBAL` 重校验）、`src/runtime/runtime.c:1022-1023` + `src/runtime/runtime_posix.c:209-210`（`.range`）；写入点 `src/vm/vm.c:4279`，嵌套保存/恢复 `src/runtime/vm_exec_builtin.c:122-139`。

**运行时**（每条都是我亲手跑的）：

1）**赋值重校验 + `.range`**：
```
$ .worktrees/sv-ref-be/build/inimerse svprobe/p_colon_type_new.im   # type Byte=[0~255]; x: Byte = 42
init=42
in-range 255 ok -> 255
caught=type_mismatch                     ← x = 300 被拒
$ .worktrees/sv-ref-be/build/inimerse svprobe/p_colon_forms.im
A named-type  a=42
B bracket    b=2
C no-init    c=nil
$ .worktrees/sv-ref-be/build/inimerse svprobe/p_colon_new.im         # x: 0,1,2,3 = 2
init=2 / in-range 3 ok -> 3 / caught=type_mismatch
```
（对照基线：`x: Byte = 42` → `init=nil` 且 `x=300` 被接受；`x: 0,1,2,3 = 2` → `Error: expected 'expression', but got ',' (type 101)`。）

2）**GC 标记根——我用自己设计的、故意针对该根的压力用例复现**（`gc_auto(1)` + 6000 轮**集合**分配，而不是数组分配，因为集合槽位才走 `vm_set_new` 的 free-list）：
```im
# svprobe/gc2.im
gc_auto(1)
x: 0,1,2,3 = 2
say "pre=" + str(x) + " range=" + str(x.range)
gc_now()
i = 0
while i < 6000 { churn = i, i + 1; i = i + 1 }
gc_now()
say "post=" + str(x) + " range=" + str(x.range)
x = 2
say "rewrite-ok"
try { x = 9; say "DEFECT out-of-range 9 accepted x=" + str(x) }
catch (e) { say "out-of-range rejected=" + str(e) }
```
```
$ .worktrees/sv-ref-be/build/inimerse svprobe/gc2.im
pre=2 range=set(4)
post=2 range=set(4)
rewrite-ok
out-of-range rejected=type_mismatch        [exit=0]
```
生产者的 `vtest/gc_bound_root_v04.im` 我也单独跑过，输出 `bound-root-after-gc=2` / `bound-rewrite-ok`，随后 `x = 9` 抛未捕获 `type_mismatch`（`at ip=50`）——与它的 PASS 正则 `bound-root-after-gc=2.*bound-rewrite-ok` 及 FAIL 正则 `must not be reached` 一致。

### A④ 10 条迁移语句逐条实跑 —— **通过（8/9 可判定；meta_test.im 是仓库既有破损，与本分支无关）**

**旧内容必须失败**：我 `git show f6b3d87:<file>` 取出旧内容，喂给新引擎 → 8 个文件全部 exit=1 带墓碑（上方 A① 表格已列）。`meta_test.im` 报的是 `Error: expected 'expression', but got 'float' (type 30)`，**不是**墓碑——但我把同一份旧内容喂给 baseline 引擎，得到**完全相同**的错误：

```
$ build/inimerse svprobe/old/meta_test.im                              # baseline 引擎
Error: expected 'expression', but got 'float' (type 30)     [exit=1]
$ .worktrees/sv-ref-be/build/inimerse svprobe/old/meta_test.im         # 新引擎
Error: expected 'expression', but got 'float' (type 30)     [exit=1]
```
⇒ `meta_test.im` **在基线就已是坏文件**，本分支没有使它变坏。**不构成本分支缺陷。**

**新写法必须能跑**：新内容在新引擎上，10 个文件里 9 个 exit=0（`big_globals_test`/`exc_test`/`inf_set_test`/`nsadv_mod`/`projects/exc_test`/`set_test`/`type_collection`/`lint_case_enum`/`lint_case_membership`），`meta_test.im` 仍是上述既有破损。其中 3 个已被 CTest 覆盖且**在 142/142 的绿色运行里**（见 §0）。

### A⑤ `M`/`A`/`to SKIP` 花括号化前后逐字比对 —— **通过，但必须记下两个变化**

生产者的判据是「`say k` 必须仍是 `1`」。**我复现了 `k` 不变，但发现 `say m` 变了。**

```bash
$ .worktrees/sv-ref-be/build/inimerse examples/regressions/label_test.im      # 花括号化后
5 4 3 5 2 3 1 done
$ build/inimerse svprobe/label_old.im                                        # 基线旧内容（我 git show 出来的）
5 4 3 5 2 1 1 done
```
即：**`say m` = 3（新）vs 1（旧）**，**`say k` = 1（新旧一致）**。
脚本结构是 `to M` 向后跳、`to SKIP` 向前跳。旧写法 `M: m = m + 1` 在基线**根本没迭代**（m 只加了 1 次就落到 1），花括号化后 `M: { m = m + 1 }` 正常迭代到 3——所以这是一处**顺带修好的隐性 bug**，但它意味着**可观测输出变了**；`to SKIP` 向前跳的语义**逐字未变**（k 仍是 1，且 `k = 99` 被跳过）。生产者的 self-report 只声称 `k` 不变，这一点属实；它没有声称 `m` 不变，而 `m` 确实变了。

**未花括号化的旧文件在新解析器下会断**，我复现了这个中间态（WIP ref，文件尚未迁移）：
```
$ .worktrees/sv-ref-prev/build/inimerse examples/regressions/label_test.im
error: unknown label 'SKIP'     [exit=1]
```
`6d9bb77` 已把 `examples/regressions/label_test.im:52` 改成 `M: { m = m + 1 }`、`:60` 改成 `SKIP: { k = k + 1 }`，`examples/regressions/thread_test.im:30` 改成 `A: {`，改后 exit=0。

**两条「无人看守」的清单项（我独立查的）**：
- `examples/regressions/label_test.im` 与 `thread_test.im` **不在 `CMakeLists.txt` 的任何 `add_test(` 里**（`grep -nE 'label_test|thread_test|GLOB' CMakeLists.txt` 只给出与 bridge 有关的注释行 `:505`/`:620`），我也**没有找到**任何 `.sh`/`.cmake`/`Makefile`/`.ps1` 运行器引用它们。⇒ 标签形式收窄带来的破坏**没有门禁看守**，只能靠人工迁移。
- 10 条迁移语句中**只有 3 条**（`vtest/lint_case_enum_v04.im`、`vtest/lint_case_membership_v04.im`、`vtest/type_collection_v04.im`）在 CTest 里；其余 7 条（`big_globals_test.im`、`exc_test.im`、`inf_set_test.im`、`meta_test.im`、`nsadv_mod.im`、`projects/exc_test.im`、`set_test.im`）**没有任何 `add_test(` 引用它们**，我也没找到运行器。⇒ 这 7 条的迁移同样无门禁看守。

### A⑥ 生命周期面 —— **通过**

`grep -rn 'be_bound' src/` 在 `6d9bb77` 上 = **0**，因此不存在指向已释放对象的残留名。更名覆盖全部生命周期位点：`vm.c:1339-1342`（grow：realloc + 新槽清零）、`vm.c:1355-1365`（clone：换出-换入-回填）、`vm.c:1398-1399`（init）、`vm.c:1561`（`vm_free` 释放并置 NULL），以及嵌套 `vm_exec` 的 `src/runtime/vm_exec_builtin.c:122-139`（`saved_be`/`saved_be_cap` 保存、`free`、恢复）。
**诚实标注**：这一项我是**读 diff + grep**确认的，不是运行时复现的（没有构造 clone/嵌套 vm_exec 下的被约束全局用例）。**未独立验证**其运行时正确性。

---

## ② `count` / `size`（`stream/count-size @ 01da596`）

### B⑥ `count` 与旧 `size` 在含区间分量集合上是否一致 —— **通过**

```bash
$ .worktrees/sv-ref-count/build/inimerse svprobe/c_probe.im
s = 1, 2, Z[7~9]        →  set_size=5   set_count=5   set_len=5   list=[1, 2, 7, 8, 9]
t = Z[7~9]              →  onlyint_size=3   onlyint_count=3
u = 1, 2, Z[7~9], 5.5   →  withflt_size=6   withflt_count=6
e = 0, 1, 2, 3          →  plain_size=4     plain_count=4
str "hello"             →  str_size=5  str_count=5
arr [1,2,3]             →  arr_size=3  arr_count=3
dict {k,j}              →  dict_size=2 dict_count=2
```
含区间分量的枚举器路径（`vm_set_to_array` + `vm_array_len`）`count` 与 `size` **逐条一致**。
对照基线：同一个探针在 `f6b3d87` 上 exit=1，`[exception] uncaught: unknown builtin function 'count' (at ip=24)`，说明 `count` 是**真正新增**的，且此前名字确实空着（`grep '"count"'` 在 runtime.c/runtime_posix.c/vm.c = 空，我复现过）。

### B⑦ 非容器三条 nil 路径是否分开可辨 —— **部分通过（两条可辨，一条不可辨）**

```
int42_size=42   int42_count=nil      ← 可辨，且 count 没有继承强转兜底
flt37_size=3    flt37_count=nil      ← 可辨
neg5_size=nil   neg5_count=nil       ← 不可辨：两边都是 nil
```
`count(42)`/`count(3.7)` 返回 nil 而 `size` 返回 42/3，**证明 `count` 没有继承 `size` 的强转尾巴**——这是我在核验前最怀疑的一点，实测**它做对了**。但 `-5` 这一行**无法判别**：`size(-5)` 在基线就是 nil（`builtin_size` 尾部 `if (n<0) push_nil`），所以 `count(-5)=nil` 即便实现错误也会长得一样，这一行没有诊断力。

### B⑧ 多参陷阱 —— **通过（数值如下，无需判定）**

```
$ .worktrees/sv-ref-count/build/inimerse svprobe/c_multi.im
size555=5      ← size(5,5,5)：多余实参被静默忽略，读的是最后一个
len9=9         ← len(9)：整数强转兜底（基线行为）
count555=nil   ← count(5,5,5)：count 更严，拒绝多参
count_none=nil
```

### B⑨ 独立重算 29 处分类 —— **未通过（作为「划分」它算不平；作为「汇总」与我一致）**

只用受版控文件、且只用 `git ls-files '*.im'`（不数 worktree 副本）。`01da596` 树上有 **348** 个受版控 `.im`（基线 346 + `probe_count.im` + `vtest/count_builtin_v06.im`）；`size(` = **52** 次 / 14 文件，`count(` = **76** 次 / 17 文件。属**既有**文件的 `size(` 只剩 **13** 次（基线 29），差额 **16**。

我逐行取出全部含 `size(` 的删除行（`git diff -U0 f6b3d87 01da596 -- '*.im' | grep -E '^-' | grep 'size('`，共 19 行）并分类：

| 类别 | 条数 | 明细 |
| --- | --- | --- |
| 纯原地更名 `size(`→`count(` | **15** | `inf_set_test.im` 的 `say size(q)`；`projects/set_comp_test.im` 3；`projects/set_op_test.im` 3；`projects/tt4.im` 1；`set_comp_test.im` 3；`set_op_test.im` 3；`vtest/set_components_enumerable_v05.im` 1 |
| 改意图 | **1** | `inf_set_test.im` 的 `o = size(1,2,3)` → 改写为 `o = 1,2,3` 并另起 `say count(o)` |
| 代码里保留 | **10** | 基线 29 − 15 − 1 − 3 = 10 |
| 仅注释改写（不是代码迁移） | **3** | `vtest/set_str_component_count_v06.im` 两行注释 + `vtest/sum_components_int64_v06.im` 一行注释 |

**我的合计：15 + 1 + 10 + 3 = 29，正好等于基线总数。** 生产者写的「**16 迁** / 1 改意图 / 10 保留 / 3 注释」**作为划分相加 = 30 ≠ 29**。它与我的数字只有在一种读法下才自洽：「16」指**全部 16 个被移除的调用点**（其中 1 个改了意图），「1 改意图」是对其中一条的**子标签**，而不是第四个桶。`inf_set_test.im` 自己的注释解释了那条改意图：旧行打印 3 只是因为内建**只读最后一个实参**（即 `size(3)`，整数强转尾巴），并非 `{1,2,3}` 的基数。

**另一项（未在判据清单里，但属产物卫生）**：`probe_count.im` 是本次在**仓库根目录**新增的 27 行受版控临时探针文件，未注册为任何测试。

---

## ③ 横切

### C⑩ 两份平台副本是否仍一致 —— **通过（两个分支都查）**

**be-removal**：`builtin_range`（`src/runtime/runtime.c:1018-1023`）与 `posix_core_range`（`src/runtime/runtime_posix.c:206-210`）拿到**逐字对称**的改动——同一处 `be_bound`→`global_bound` / `be_bound_cap`→`global_bound_cap`，注释也对齐（宿主侧 `/* bound global: return its declared bound set */`，POSIX 侧 `so a bound global can expose its declared set instead of a broad inferred numeric range`）。**没有一边改一边没改。**

**count-size**：`builtin_count`（`runtime.c:142-168`，注册 `runtime.c:1819`）与 `posix_core_count`（`runtime_posix.c:54-84`，注册 `runtime_posix.c:1172`，对照 `posix_core_size` 注册 `:1140`）**结构等价**：同样的 `VAL_SET` 闭式（`kind==0 && compCount==0` → `iCount+count`），同样的枚举器兜底（`vm_set_to_array` + `vm_array_len`），同样的**带边界检查**的 `VAL_ARRAY`/`VAL_DICT`（`v->ival > 0 && v->ival - 1 < vm->arrayCount`，`a ? a->count : 0`，`a ? a->count / 2 : 0`），同样的 `VAL_STRING` strlen，非容器 `n` 保持 **-1** 并由尾部 `if (n < 0) push_nil(vm); else push_int(vm, n);` 变 nil。差异**仅在注释与排版**。另外 `runtime.c` 的新注释明确宣称它「mirrors posix_core_count() line for line, including the bounds-checked array/dictionary spellings」——**我逐行比对，该宣称属实**。

**注意一处与 `size` 的既有不对称（非本分支引入）**：`count` 的数组/字典分支带边界检查，而老的 `size` 那一侧写法不同。这属于既有差异，两个新副本彼此一致。

### C⑪ 计数同步 —— **每个分支内部自洽（通过），但合并后会错（未通过）**

| ref | `grep -c 'add_test('` | `tools/gate.sh` `EXP_CTEST` | `docs/BOARD.md` §3 行 |
| --- | --- | --- | --- |
| `main = f6b3d87` | 137 | `:-137` | `**137 / 137 通过` |
| `6287523` (WIP) | 137 | `:-137` | `**137 / 137 通过` |
| `stream/be-removal @ 6d9bb77` | **142** | `:-142` | `**142 / 142 通过` |
| `stream/count-size @ 01da596` | **138** | `:-138` | `**138 / 138 通过` |

三条（`add_test` 数 / `EXP_CTEST` / BOARD §3）在**每个分支上**都同步——这一点我复现了，**通过**。
**但**：be-removal 加 5 条、count-size 加 1 条，合并后的真实总数是 **137 + 5 + 1 = 143**，而这两个分支**谁都没有 143**。两者都改了 `tools/gate.sh` 的同一行，合并时会**冲突**，且**无论顺手取 142 还是 138 都是错的**——`stage_ctest` 会去找 `0 tests failed out of 143`，取错就红（或更糟：若有人为了让它绿而反过来改 `EXP_CTEST` 去迎合误报，就变成把断言写成事实）。**必须解析到 143。** 这正是 lead 点出的「两人分别同步到 142 与 138」的真实含义：每个分支单独看是对的，**合起来是错的**。
`docs/STATUS.md` 我另查了：它的 ctest 数字是一段**历史叙述**，同一文件里同时存在 100/101/102/108/111/112/113/114/115/116/117/118/119/120/121/122/127/128/132/134/137/138/142/144/146/164/190 等值，**不是单一权威数字**，无法用它做计数断言；我因此不把它记为通过或未通过。我会把这个观察作为**未独立验证**处理，因为我没有逐段读完全文确认每条数字的语境。

### C⑫ `PASS_REGULAR_EXPRESSION` 里 `;` 是 OR 不是 AND —— **通过（语义属实），并附一条「比看上去弱」的实测**

`CMakeLists.txt` 里共 **77** 处 `PASS_REGULAR_EXPRESSION`。在 CMake 中属性值是**列表**，`set_tests_properties(... PASS_REGULAR_EXPRESSION "a;b;c")` 展开为三个正则，**任一匹配即通过（OR）**。仓库里最长的 OR 串是 `gc_runtime`（`CMakeLists.txt:692`）：

```cmake
add_test(NAME gc_runtime COMMAND inimerse ${CMAKE_SOURCE_DIR}/vtest/gc_runtime_v04.im)
set_tests_properties(gc_runtime PROPERTIES … PASS_REGULAR_EXPRESSION "gc-ran;gc-stats-ok;thread-result-alive;closure-capture-alive;root-alive;gc-done")
```
我实测这六条**今天全都出现**，所以这条测试**当前并不空洞**：
```
$ .worktrees/sv-ref-be/build/inimerse vtest/gc_runtime_v04.im
gc-ran / {runs: 2, freed: 127, …} / gc-stats-ok / thread-result-alive / 3 / closure-capture-alive / 3 / root-alive / 3 / gc-done
  PRESENT gc-ran   PRESENT gc-stats-ok   PRESENT thread-result-alive
  PRESENT closure-capture-alive   PRESENT root-alive   PRESENT gc-done
```
**但写着六条、实际只要求一条**：一个只打印 `gc-ran` 就提前退出（或崩溃）的运行**仍然会 PASS**。这不是本分支引入的缺陷，而是**门禁强度**的既有事实：`gc_runtime` 名义上守着线程结果 / 闭包捕获 / 数组根三条不变量，实际只守着「任一被打印」。同类还有 `case_try_runtime`（`:755` `"deep-structural-hit;nested-result-hit"`，OR）与**故意**使用 OR 的 `posix_runtime_parity`（`:690` `"posix-runtime-parity-ok;${INIMERSE_PARITY_LOAD_MISSING}"`）。若要真正守住六条，需拆成六条 `add_test` 或改用 `FAIL_REGULAR_EXPRESSION` 反向钉住缺失。

---

## ④ 我试图推翻、而没能推翻的（lead 明确要求至少一条）

**目标：让 `global_bound[]` 这个 GC 标记根失效，用我自己的用例，而不是读代码。**

我的推翻尝试：写 `svprobe/gc2.im`，**刻意不用数组做 churn**——生产者的注释自己指出「只 churn 数组（`[i, ...]`）不会碰集合池，槽位不复用，于是关掉这个根也看不出差别」。我改成 6000 轮**集合**分配（`churn = i, i + 1`，走 `vm_set_new` 的 free-list），并在 churn 前后各做一次 `gc_now()`，最后**两侧都验**（合法值必须仍被接受，越界值必须仍被拒）。
**结果：没能推翻。** 输出 `pre=2 range=set(4)` / `post=2 range=set(4)` / `rewrite-ok` / `out-of-range rejected=type_mismatch`，exit=0。被约束集合在 `gc_auto(1)` 打开、6000 轮集合压力之后仍被正确标记，约束两侧都完好。⇒ GC 标记根**迁移正确**，报废形状（合法值被误拒 / 收集后槽位被复用导致约束读到别的集合）**没有出现**。生产者的 `vtest/gc_bound_root` 我也没有只信它——但它内部的 churn 规模与我不同，两条独立用例都绿，可信度更高。

**另外两条我尝试推翻、结果推翻的是「判据/叙述」而非产品**：
- 我试图证明墓碑的行号是错的（用的正是 shipped 墓碑），发现**根因是 `#` 注释剥离**而非墓碑逻辑；在无注释文件里行号正确 → 我**修正了自己的判据**，没有把它记成「墓碑有 bug」。
- 我试图推翻 `x: 0,1,2,3 = 2` 这条内联逗号集合（基线是硬解析错误 `expected 'expression', but got ',' (type 101)`），想证明它在新引擎下仍不可用 → **没能推翻**，它在 `6d9bb77` 上正常解析并正确约束。

---

## ⑤ 未独立验证 / 无法判定（明确列出）

1. **完整 `tools/gate.sh` 全门禁**：我只跑了 CTest（137/142/138 三档），**没有**跑那 12 个阶段（fuzz / economy / node / plugin / oauth_loop / ignored_credentials / links / doc_paths / text_integrity / orphan_fixtures 等）。任何「全门禁绿」的说法我**未独立验证**。
2. **生产者的变异验证**（「注掉 `src/vm/vm.c:2770` ⇒ 红，恢复 ⇒ 绿」）：我**未**做变异-重编译-重跑，因此该条目未独立验证；（我用自己独立的 churn 用例从正面支持了同一结论）。
3. **`vm_global_clone` / 嵌套 `vm_exec` 下被约束全局的运行时正确性**：仅 grep + 读 diff，**未**构造运行时用例。
4. **`docs/SYNTAX.md` / `docs/BOARD.md` / `docs/STATUS.md` 的散文更新**：我未逐条核对，除 §C⑪ 的计数表外全部未独立验证。
5. **`vtest/count_builtin_v06.im` 的 30 处 `size(` / 33 处 `count(`**：它在 142/138 的绿色运行里通过（我复现了测试通过），但我**未**逐条审读该文件的 68 行断言是否覆盖了它自称覆盖的全部情形。
6. **B⑤ GUI `size` 关键字未被改动**：我只做了源码侧比对（`TOK_SIZE` 仍在 `src/lexer/lexer.c:40`，GUI 分派 `src/parser/parser.c:216/231/909/1312-1313` 与 `src/mod/gui_mod.c:2790 builtin_gui_size` / `:3798` 注册均未在任一分支的 diff 中出现）。**我未能做出运行时判定**：全仓 346 个受版控 `.im` 里**没有任何一处使用 GUI 形式的 `size` 语句**（所有命中都是 `size(` 函数调用或字符串字面量），且该动词按 `docs/SYNTAX.md` §4.8 是「无 arity/类型检查、失败静默」，因此**没有可观测输出可用于判定**。⇒ **B⑤ 记为无法判定（缺可观测面），但源码比对未见改动。**

---

## ⑥ 复现清单（我跑过的每一条命令都能原样重放）

```bash
# 三棵 detached 冻结工作树 + 构建
git worktree add --detach .worktrees/sv-ref-prev c89e077~1
git worktree add --detach .worktrees/sv-ref-be 6d9bb77
git worktree add --detach .worktrees/sv-ref-count 01da596
for d in sv-ref-prev sv-ref-be sv-ref-count; do cmake -S .worktrees/$d -B .worktrees/$d/build -DCMAKE_BUILD_TYPE=Release && cmake --build .worktrees/$d/build -j$(nproc); done

# CTest（我本人跑的分档）
ctest --test-dir build -j$(nproc)                                  # baseline → 137/137
ctest --test-dir .worktrees/sv-ref-be/build -j$(nproc)             # be       → 142/142
ctest --test-dir .worktrees/sv-ref-count/build -j$(nproc)          # count    → 138/138（首次 137/138，socket_probe 抖动）
ctest --test-dir .worktrees/sv-ref-prev/build -j$(nproc)           # prev     → 136/137（type_collection_runtime）

# 计数同步（只用 git show，不 checkout）
for r in f6b3d87 6287523 6d9bb77 01da596; do
  echo "$r $(git show $r:CMakeLists.txt | grep -c 'add_test(') $(git show $r:tools/gate.sh | grep -oE 'EXP_CTEST:-[0-9]+')"
  git show $r:docs/BOARD.md | grep -oE '\*\*1[0-9][0-9] / 1[0-9][0-9] 通过' | head -1
done

# 行为探针（每条都见上文逐字输出）
.worktrees/sv-ref-be/build/inimerse svprobe/{ln_decl,ln_assign,ln_bare,ln_shift,base_swallow,gc2,p_colon_type_new,p_colon_new,p_colon_forms}.im
build/inimerse svprobe/{ln_decl,ln_assign,ln_bare,base_swallow,label_old}.im
.worktrees/sv-ref-count/build/inimerse svprobe/{c_probe,c_multi}.im

# 10 条迁移语句双向
git show f6b3d87:<file> > svprobe/old/<file> && .worktrees/sv-ref-be/build/inimerse svprobe/old/<file>

# 29 处分类
git ls-files '*.im' | wc -l ; git ls-files '*.im' | xargs grep -o 'size(' | wc -l
git diff -U0 f6b3d87 01da596 -- '*.im' | grep -E '^-' | grep -n 'size('
git diff --name-only f6b3d87 6d9bb77
```
