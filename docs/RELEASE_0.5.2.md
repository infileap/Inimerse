# Inimerse 0.5.2

0.5.2 是一个**修栈**的版本：一处真实存在的、只在 Windows 上编译的运行时缺陷，在 Linux 上拿到的是**全绿的门禁**。它同时给门禁添了第十三个阶段，并落了一大轮文档诚实化。语言与运行时语义没有变化。

> **门禁报的 PASS，是「我看了」还是「我没看」？**

## 基线

| 项 | 值 |
|---|---|
| 版本 | 0.5.2 |
| 测试 | 发布点 `v0.5.2`（`git rev-parse --short 'v0.5.2^{commit}'`）上：Linux **148 / 148 真通过**（本地**十三**阶段门禁，0 跳过）。0.5.1 发布点 `v0.5.1` → `4e444dd` 上同样是 148 / 148、当时是**十二**阶段 |
| 相对 `v0.5.1` | `git rev-list --count 'v0.5.1^{commit}'..'v0.5.2^{commit}'` —— **本行不写这个字面量**：写下一个数就多一个提交，而那个数正是要数的提交数。在 `751b2a0` 上量是 **76**（初稿写 74，那是在 `88e9338` 上量的 —— 数与 ref 错位，见 `docs/AUDIT.md` §1.74） |

## 修了什么

1. **Windows 的 `re_seq` 用自递归吃掉栈，25 个字符就崩。** `match_long_pattern_runtime` 自报 `code=0xC00000FD`（`STATUS_STACK_OVERFLOW`）。最小复现不是 fixture 里的 2030，而是 **25**：

   ```
   s = "a"×25 ;  match(s, "^" + "a"×25 + "$zzz")
   ```

   同一长度、以 `"$"` 结尾的那一行**不崩**。机制是量出来的，不是读出来的：崩溃那一刻栈上有 **26 个活着的 `re_seq` 帧**，它们的返回地址彼此恰好相隔 **`0x8080` = 32896 B** —— 与序言反汇编逐位吻合（`push r15 / mov $0x8038,%eax / push r14,r13,r12,rbp,rdi,rsi,rbx / call ___chkstk_ms / sub %rax,%rsp` ⇒ 8×push(64) + 32824 + 8）。`SizeOfStackReserve 0x200000`（2 MiB）。**这里有三个数，它们量的是不同的东西，必须分开写**：**26 个 `re_seq` 帧自身**占 26 × 32896 = 855296 B（2 MiB 的 **40.8%**）；**崩溃瞬间整条栈已耗** 2069776 B = **98.70%**（`rsp=0x407af0` 对可读顶 `0x601000`）；**进下一帧还差 5520 B**（帧 32896 − `rsp` 到栈基 `0x401000` 的余量 27376）。三者不互相推导 —— 独立核对：2069776 + 32896 = 2102672，超出 2 MiB 预留 **5520 B**。**本节初稿把余量写成 `0x60f0`(24816)、差写成 8080，是 `0x407af0 − 0x401000` 这一步算错的连带值（实为 `0x6af0` = 27376）**，已更正。**2030 与 25 两个输入得到逐项相同的读数**（同一 `RSP=0x407af0`、同样 26 帧）⇒ **深度由栈预算封顶，与输入长度无关**。

   修法是**把三处纯尾调用改写成既有的 `for (;;)` 迭代**（`$` 分支、交替分支、无量化续接），新增一个 `quant` 标志分辨「无量化」与「有量化」两条路。有量化的那条**仍然递归**，深度由嵌套决定 —— 这一点写在这里，是因为它正是「修法只治了这一条路」的边界。`src/runtime/runtime.c` `+32/−6`。

   判据三条，缺一不可：**①** 该 CTest 由 `***Exception: SegFault` 变 `Passed`；**②** 反向验证在**同一棵树**上做（换回修复前字节、增量重建 ⇒ 重新 SEGFAULT）；**③** 同树 Windows 全量 ctest 里**变的是且只是目标那一条**（修复前 `98% tests passed, 3 tests failed out of 128`：`19 process_probe`、`136 count_builtin_runtime` SEGFAULT、`137 match_long_pattern_runtime` SEGFAULT ⇒ 修复后 `98% tests passed, 2 tests failed out of 128`）。语义判据另钉住「一律返回 false」的假修法过不去：第 10 行必须答 `impossible=false`、第 11 行必须答 `possible=true`。

## 门禁新增

2. **第十三个阶段 `orphan-targets`。** `tools/check_orphan_targets.py`（新，226 行）：**每一个 `add_executable` 出来的目标都必须被至少一条 CTest 跑到**。38 个目标里 **37 个被跑到、1 个有写明理由地豁免**（`websocket_probe` —— WebSocket 帧层未实现，探针没有可断言的东西，见 `docs/API.md` §「预留」两行）。它与 0.5.1 的 `orphan-fixtures` 是同族但方向相反：那个问「有没有输入没人跑」，这个问「有没有产物没人跑」。

   造它的过程里有一条教训值得留在这里：**按测试名匹配目标名是错的尺子** —— `literal_resolve_probe` / `resolve_timeout_probe` 的测试名是 `literal_resolve_runtime` / `resolve_timeout_runtime`，第一次用错尺子误报了三个。现在它读的是 `$<TARGET_FILE:…>` 与 `add_test` 的真实引用关系。

3. **`tools/gate.sh --required-for`：这一笔改动能影响哪些阶段。** 它从 `STAGE_SPECS` 的**真实注册表**派生，而不是按名字手写 glob —— 后者会漏掉 `tools/im_diff_fuzz.py`、`node_suites/`、`dsh-inimerse/`、`Infiverse_standard/oauth_loop` 这四个**名字不像 checker 的**阶段入口。用法：`--required-for <ref>` 回答「工作区相对 `<ref>` 变了什么」，`--required-for <base>..<head>` 回答「这个提交区间变了什么」。两种形式回答的是**两个不同的问题**，输出里各自自报。

## 一大轮文档诚实化

4. `docs/AUDIT.md` **+533/−32**（新增 §1.65–§1.74）、`docs/HANDOFF_INFIVERSE.md` **+196/−0**（新）、`docs/DECFY_DESIGN.md` **+70/−29**、`docs/BOARD.md` **+9/−7**、`docs/STATUS.md` **+5/−5**，另有 `docs/README.md`、`docs/HYGIENE.md`、`docs/RELEASE_0.5.1.md`、`docs/streams/json-min-nul-escape.md` 各 **+1/−1**。（**初稿写 `+565`/`99`/`16`/`10` 并把 HANDOFF 写成 `+196`：数都对，但那是「改动行数」＝ insertions + deletions，而同一个 `+` 号在 `+196` 那里读作 insertions 也成立 —— 一个记号两个意思。出处：`git diff --numstat v0.5.1 751b2a0 -- docs/`。**）

   这一轮治的是一族同形状的缺陷，它们都不改变任何行为，只改变**一个断言能不能变红**。值得单独点名的一条，因为它自己证明了自己：

   > **一个用字面量声明「该字面量全库零命中」的句子，写下那一刻就把自己证伪了。**

   `docs/AUDIT.md` §1.65 与 `docs/STATUS.md` 都曾写「全库 `grep -rn 'Total Tests: 131' docs/ tools/` 零命中」—— 而**写下这句话本身就往仓库里加了一处命中**。现在的写法是**直接给出处**，不写「几处」。

   同族的其余几条（都进了 §1.65–§1.73）：分母是「跑它的那一刻那个工作区的索引」而不是任何 ref（`check_text_integrity` 的 `822` vs `823`）；`git ls-files 'src/**/*.c'` 给 106 而 `':(glob)src/**/*.c'` 给 114（**不写 `:(glob)` 时 `**` 根本不是 `**`**）；文档里指向 `CMakeLists.txt` / `tools/gate.sh` 的行号**必须带 ref 或内容锚**（同一个目标在 `docs/AUDIT.md` 里已经有过三个号码，每次写下时都是对的）；**改动的单位是「段落」而不是「被点名的那一行」**（同一个段落里，改了一处而另一处留着，出现过三次）。

## 平台

- **Windows**：与 0.5.1 相同 —— hub 为减配版本，**9 个 POSIX hub 回归测试显式 `DISABLED`**（按维护者裁定，移植未做），两个 xlang 桥（Java / Python）按设计以退出码 77 跳过。本版的引擎修复**只作用于 Windows**（见下）。
- **Linux**：完整功能，十三阶段门禁全绿。

## 资产

| 平台 | 资产 |
|---|---|
| Linux | `inimerse-0.5.2-Linux-x86_64.tar.gz`、`inimerse-0.5.2-Linux-x86_64.zip`、`inimerse-0.5.2-Linux-x86_64.deb`、`SHA256SUMS` |
| Windows | `inimerse-0.5.2-Windows-x86_64.zip`（含 `bin/inimerse.exe`、`bin/aot-native.exe`、`bin/inim.py`）、`InfiverseSetup-0.5.2.exe`（Inno Setup 7 装机包）、`SHA256SUMS-Windows` |

`SHA256SUMS` 与 `SHA256SUMS-Windows` 里都是**裸文件名**，在下载目录里 `sha256sum -c` 可直接校验。

## 诚实边界

- **Linux 上的十三阶段全绿，对本版那处引擎修复不提供任何证据。** `CMakeLists.txt:429` 在 `if(WIN32)` 里选 `src/runtime/runtime.c`，**`:437` 的 `else()`** 选 `:438` 的 `src/runtime/runtime_posix.c`（后者没有 `re_seq`，用的是 libc `regcomp`）⇒ **Linux 门禁根本不编译被改的那个文件**（`nm build/inimerse | grep -c 're_seq'` = 0）。这条不是推测，是本版 §1.71 量出来的：`src/` 下 114 个受管 `.c` 里，Linux 构建只给 102 个产出 `.o`，缺的 12 个全部只出现在 `CMakeLists.txt:426-436` 的 `if(WIN32)` 块内。**「Windows 运行时只在 Windows 上编译」是合理的；不合理的是门禁把「没编译」报成 PASS。**
- 修复的证据来自一棵 `RelWithDebInfo` 树，**帧大小 `0x8080` 未在 `-O3 -DNDEBUG`（发布配置）上量过** —— 量不到就写「未量」，不写成量到了。
- **登记但未修**：`src/runtime/runtime.c` 里 `pos[4096]` 的**回溯读没有守卫**（写有 `if (count < 4096)`，读 `t = pos[count - 1]` 没有）—— 它被「25 个字符就崩」**证伪为本次崩溃的原因**，仍是独立缺陷；`re_seq` 的 32896 B 帧仍然存在，封顶嵌套量化 / 分组的深度；`count_builtin_runtime` 在 Windows 上同样 `0xC00000FD`，但 `[stack] #8` 是 `0x6ed8`（≠ `re_seq` 的 `0x8038`）、`re_seq` 断点在该 fixture 上计数 **0**、原始栈扫描定位到 **`compile_expr` 自递归**（32 帧）⇒ 那是**既有缺陷被新测试暴露**，不在本版修。
- `orphan-targets` 证明的是「每个目标都被某条 CTest 引用」，**不是**「那条测试断言了有意义的东西」；它的 `1 allowed` 是一条**有理由的豁免**，而豁免名单会退化成免责名单 —— 与 `orphan-fixtures` 的 `4 allowed` 是同一个风险。
- `--required-for` 的映射**只在 `tools/gate.sh` 的注册表里**，它不解析任何文件内容：一个阶段「受影响」是因为**改动落进了它读的那棵子树**，不是因为它真的会红。单参形式看的是**工作区**（干净树上答 `0 stage(s)` 且退 0），所以它自报回答的是哪个问题 —— 但「零个阶段」在一个敲错的调用里仍然是危险的答案。
- 文档里的行号引用**仍然可能过期**：本版把 `docs/AUDIT.md` 里指向 `tools/gate.sh` 与 `CMakeLists.txt` 的行号换成了锚或带 ref 的形式，但**同一个类的其余拷贝还在别的文档里**（`docs/BOARD.md` / `docs/STATUS.md` / `docs/DECFY_DESIGN.md` 合计十几行），本版**只登记、没有全部重取**。
- `docs/SYNTAX.md` §「核心高频」那一行**缺平台标注**：`file_exists` 与 `timer_ms` 只由 `src/mod/io_mod.c` 注册，而 `io_mod` 在 POSIX 上是空桩 ⇒ 那两个名字在 Linux 上是 `unknown builtin function`；`mkdir` 与 `list_dir` 则是**同一个名字两个生产点**（Windows 走 `io_mod`、POSIX 走 `runtime_posix`）。该行还自称「有 `vtest` 覆盖」，对这四个名字**全不成立**。本版**只登记、未裁定**。
- **这个仓库里版本号有四个生产点，没有任何东西让它们保持同步**：`CMakeLists.txt:8`（`set(INIMERSE_PACKAGE_VERSION …)`）、`installer.iss:7`（`AppVersion=`）、`installer.iss:13`（`OutputBaseFilename=`）、以及本文件与历史发版说明里的资产名。本版之前，前两个里只有 `CMakeLists.txt` 被升过 —— `installer.iss:7` 停在 `0.5.1`、`installer.iss:13` 是不带版本号的 `InfiverseSetup`，而 **0.5.0 / 0.5.1 / 0.5.2 三份发版说明都写 `InfiverseSetup-0.5.X.exe`，全库没有任何脚本产出那个名字**。本版把 `AppVersion` 与 `OutputBaseFilename` 都改到 `0.5.2`，但**装机包在本机无法构建、因而这条改动没有被任何东西验证过**（只在 Windows + Inno Setup 上才能验）。**下一次升版本时，这四个点要一起看。**
- **`v0.5.2` 这个 tag 被移动过一次。** 它先打在 `751b2a0`，推上去之后才发现基线表那一行的字面量（`74`）是在另一棵树（`88e9338`）上量的，而那个数**在被数的提交里写下就必然错位**；修正后 tag 移到 **`0ebd68d`**（`git push --force`）。**从此冻结**：此后发现的任何问题只进 `main`，不再移动 tag —— 想看这一版发布时仓库是什么样，用 `git show 'v0.5.2^{commit}':<path>`。
- **本文件此前是孤儿文档**：`git grep 'RELEASE_0\.5\.2'` 在全库 0 命中（`check_links` 只验「链能不能解析」，不验「文件有没有人引」—— 而本版新增的第十三个阶段问的正是「有没有产物没人跑」，同一个问题在文档侧没有对应的检查器）。现在 `docs/STATUS.md` 的「已发布点」那一格有一个入链。**没有为它开新阶段**：名录式检查会退化。
- `docs/RELEASE_0.5.1.md` 里 Windows `Total Tests: 131` / 运行 122 的读数**在本机无法复核**（本机是 Linux，测不到 Windows 的注册条件组合）⇒ 该条保持「未验证」，不因本版重述而变成已验证。
