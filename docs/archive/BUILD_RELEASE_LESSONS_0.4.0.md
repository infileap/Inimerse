# 0.4.0 构建与发布经验

本文记录 Inimerse 0.4.0 从源码构建到 Linux 发布候选验证过程中确认的工程约束。
它服务于后续版本维护，不把研究性设计或未完成的平台能力写成已交付功能。

## 版本信息必须单一化

版本号会同时出现在 CMake、公共头文件、CLI 输出、安装器、WASM probe、发行包
文件名和 CI 工作流中。任何一处单独维护都会产生“包名正确但运行时版本错误”或
“源码版本正确但发布门禁仍接受旧版本”的漂移。

当前约定：

- CMake 通过 `INIMERSE_VERSION_STRING` 注入构建版本；
- 非 CMake 构建在 `src/common/common.h` 保留与当前发布线一致的回退值；
- CLI、CPack、安装器和 WASM probe 都由同一发布版本检查；
- 修改版本号时，必须同时检查 `CMakeLists.txt`、`common.h`、`installer.iss`、
  工作流和发布验证脚本。

## CMake 与 Makefile 必须覆盖同一源集

项目同时支持 CMake 和 Makefile。新增运行时模块时，如果只更新其中一个构建入口，
会出现本地测试通过但另一条构建链缺符号、漏注册或漏测的情况。新增源文件后应检查：

```bash
rg -n "src/|closure|registry|typeset|enum|error_types|ed25519" CMakeLists.txt Makefile
```

构建入口应覆盖相同的核心模块，并至少各自完成一次干净构建和测试。

## 并行构建不能依赖伪顺序

`make -j2 clean all` 会把 `clean` 和 `all` 当作可并行目标，可能在编译过程中删除
刚生成的对象文件。需要顺序清理和构建时，应使用递归 Make：

```make
linux:
	$(MAKE) clean
	$(MAKE) all
```

或者分成两个显式命令执行。不要把 `clean all` 放在同一个可并行目标列表中。

## 发行包检查内容，不只检查文件名

文件存在和 SHA-256 正确并不能证明包可用。发布验证还应检查：

- `tar.gz`、`zip`、`deb` 的版本名；
- tar/zip 中的 `bin/inimerse` 和 `bin/inim`；
- Unix 可执行权限；
- WASM probe 的安装位置；
- DEB 的 `/usr/bin` 入口和运行时依赖；
- `SHA256SUMS` 对当前文件重新计算后匹配；
- Winget manifest 不含占位摘要。

特别是 ZIP：压缩包内的权限位和 Python 解压后的文件模式不一定表现相同，因此
验证器应直接读取 ZIP 条目的 Unix mode，而不是只检查解压目录中的权限。

## DEB 运行时依赖必须显式声明

`inim` 的本地/HTTP registry 辅助路径依赖 Python 运行时。构建机存在 Python
不能替代用户安装时的依赖声明，CPack DEB 必须生成：

```text
Depends: python3
```

发布门禁使用 `--require-deb-python3` 强制检查这一点。

## WASM probe 的版本策略

WASM probe 同时承担“资产确实被安装”和“构建版本正确”的轻量检查。0.4.0 使用：

- probe 标记：`0x0400`；
- ABI revision：`1`。

发布版本标记可以随版本递增；稳定 ABI revision 只有在 ABI 合约发生不兼容变化时
才递增。两者不能混为一个数字，也不能只验证源码中的常量而不验证发行包中的文件。

## 发布门禁要验证可复现证据

发布验证脚本应拒绝损坏归档、缺失入口、错误权限、错误依赖、摘要不匹配和占位值。
测试文件同时覆盖正例和负例，避免验证器因为“文件存在”而放过不可安装或不可运行的包。

0.4.0 的最小重复验证命令：

```bash
cmake --build build-local
ctest --test-dir build-local --output-on-failure
make -j2 check
python3 tools/inim.test.py
python3 tools/eidos_desugar.test.py
python3 tools/eidos_runtime.test.py build-local/inimerse
python3 tools/release_verify.test.py
python3 tools/release_verify.py build-local --version 0.4.0 \
  --require-wasm --require-deb-python3
git diff --check
```

重新运行 CPack 后必须重新生成摘要文件：

```bash
sha256sum build-local/inimerse-0.4.0-Linux-x86_64.tar.gz \
  build-local/inimerse-0.4.0-Linux-x86_64.zip \
  build-local/inimerse-0.4.0-Linux-x86_64.deb \
  > build-local/SHA256SUMS
```

## 发行平台边界要写清楚

本轮交付的是 Linux `tar.gz`、`zip` 和 `deb`。Windows 构建工作流可以继续作为
编译和测试验证，但在没有真实 Windows 安装器及真实 SHA-256 之前，不应把它放入
正式 Release 资产，也不应让 Winget manifest 通过发布门禁。

同理，`future/` 下的 Infiverse、完整 Eidos 和 Inim OS 设计文档属于规划材料。
发布说明只能引用已经实现并经过测试的子集。

## 0.4.0 验证记录

截至 2026-09-13：

- CTest：66/66 通过；
- `make -j2 check` 通过；
- `inim`、Eidos desugar/runtime 和发布验证测试通过；
- Linux 三种发行包结构、摘要、WASM probe 和 DEB `python3` 依赖通过；
- 集合审计三种模式结果一致，规模 10000，每模式 3 次，峰值 RSS 最大约
  70324 KiB。

## 版本间自举产物对比与编译器效果验证

每个发布版本都将自举产物与 C++ 编译产物作为对比范本，以量化评估编译器的正确性和优化效果。

### 自举验证方法

1. **编译命令对比**
   - 自举编译：使用上一版本生成的 `inim` 编译当前版本源码
   - C++ 编译：使用当前版本的 `inimerse` 编译当前版本源码
   - 对比两种方式生成的可执行文件大小、构建时间

2. **运行时行为一致性**
   - 用 `inim` 编译所有 0.5.0 的测试用例
   - 用 `inimerse` 编译相同测试用例
   - 运行两种产物并比较测试结果和执行时间

3. **产物验证清单**
   - 检查二进制大小是否满足预期范围
   - 验证所有测试套件通过（包括平台探针、协议回归、WASM probe）
   - 对比性能基准（执行时间、内存占用）
   - 确认运行时输出与 C++ 编译版本一致

### 0.5.0 自举对比记录（2026-10-02 实测）

**前置更正（必须先读）：本仓库从未有过 `inim` 目标。**

`git log --all -S 'add_executable(inim ' -- CMakeLists.txt` 输出为空——全分支历史里 `inim` 从未是 CMake 目标；当前 `CMakeLists.txt` 的可执行目标列表中也没有它，`ls build/inim` 报 `No such file or directory`。

本仓库的「自举编译器」是**源码**而非二进制：`selfhost/compiler.im`（22,357 B）+ `selfhost/lexer.im`（3,421 B）+ `selfhost/parser.im`（13,248 B），文件头自述用法为 `inimerse selfhost/compiler.im <target.im>`（"用 inimerse 写的编译器:AST -> 宿主字节码数据 -> vm_exec 执行"）。

因此下文第 1、3 项按**实际存在的对象**重述；原措辞中 `inim` / `inimerse` 两个二进制对比的那一半**不适用**，不编造比值。

执行命令（当前树可用；`build/` 是已配置目录，`build-local` 已不存在）：

```bash
# 0. 配置与构建（build/ 尚未配置时）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)"

# 1. 二进制 / 产物大小
ls -l build/inimerse
wc -c selfhost/compiler.im selfhost/lexer.im selfhost/parser.im
./build/inimerse buildc tools/bench/bench_collections.im /tmp/bc.inim && ls -l /tmp/bc.inim

# 2. 测试通过率
ctest --test-dir build -j4 --output-on-failure
python3 tools/release_verify.test.py

# 3. 版本一致性
./build/inimerse --version

# 4. 性能（刻意不带 --write-docs，原因见文末「已知缺陷」）
python3 tools/selfhost_bench.py --runs 5
/usr/bin/time -v ./build/inimerse run /tmp/bc.inim
```

**关键验证点（0.5.0，2026-10-02，主机 `Linux x86_64`，Python 3.14.4）：**

- `[x]` **二进制大小** —— `build/inimerse` = **872,584 字节**（构建于 2026-10-02 15:39）；自举编译器以源码分发 = 22,357 + 3,421 + 13,248 = **39,026 字节**；`buildc` 产物 `.inim` = **2,542 字节**（`bench_collections.im`）。**未给出 `inim`/`inimerse` 大小比**：该对象不存在。
- `[x]` **测试通过率** —— `ctest` **97/97 通过，0 失败，0 跳过**（real 12.81 s）；`python3 tools/release_verify.test.py` → `release verifier: ok`（exit 0）。0.4.0 基线为 66/66（见本文上方「截至 2026-09-13」）。
- `[x]` **运行时版本一致性** —— `./build/inimerse --version` → `inimerse 0.5.0`。版本号单一来源：`src/common/common.h:61-64`（`INIMERSE_VERSION_STRING "0.5.0"` → `INFIVERSE_VERSION`），由 `CMakeLists.txt:8`（`set(INIMERSE_PACKAGE_VERSION "0.5.0")`）在 `CMakeLists.txt:342/345` 注入编译定义，`src/main.c:778` 打印、`src/main.c:619` 写入构建记录；CTest `version_runtime`（#53）守着它。本仓库只有一个运行时，故「两版 `--version` 输出相同」退化为「运行时与其自身构建元数据一致」——**已核实一致**。
- `[x]` **性能对比** —— 本次采样（2026-10-02 08:01 UTC，`--runs 5`）对比上一份报告（[SELFHOST_BENCHMARK.md](SELFHOST_BENCHMARK.md)，2026-09-29 13:49 UTC）的运行中位数：

  | 套例 | 本次运行中位 | 上次运行中位 | 变化 |
  |---|---|---|---|
  | collections | 26.2 ms | 32.3 ms | **−18.9%**（更快） |
  | case-try | 32.8 ms | 29.2 ms | **+12.3%**（慢，未超阈） |
  | vfs | 99.5 ms | 132.9 ms | **−25.1%**（更快） |
  | selfhost | 27.5 ms | 28.7 ms | **−4.2%**（更快） |

  编译中位数同向改善（1.6 / 1.8 / 1.8 / 1.7 ms，上次 2.0 / 2.3 / 2.2 / 3.2 ms）。**无任一套例劣化超过 20%**，[STATUS.md](../STATUS.md) §5 的发布门禁条款成立。四套例的规范化字节码哈希与上次报告**逐字相同**（`1edb937201a3a50f…` / `ca2a5a3dfbf0057d…` / `85c00d0b84beacde…` / `39167d8f6b3428d9…`），可复现构建性质跨时间成立。

  RSS：`/usr/bin/time -v ./build/inimerse run /tmp/bc.inim` → **Maximum resident set size 70,032 KiB**，wall 0.06 s（0.4.0 记录的同量级峰值为 70,324 KiB，见上方「集合审计」）。

**已知缺陷（本次不修，记账）：** 上面第 4 项刻意**不带 `--write-docs`**。`tools/selfhost_bench.py:121` 与 `tools/perf_compare.py:131` 把报告写到 `docs/SELFHOST_BENCHMARK.md` —— 少了 `archive/` 一级，真实文件在 `docs/archive/SELFHOST_BENCHMARK.md`；带该开关会凭空新建一个错位文件。该缺陷已记录在 [STATUS.md](../STATUS.md) §1063，且修复写域归属未认领行 `aot-native-integration`（[BOARD.md](../BOARD.md) §5 已把 `tools/selfhost_bench.py`、`tools/perf_compare.py`、`docs/archive/SELFHOST_BENCHMARK.md` 列入该行 writeScopes），故**不在本次写域内**。

**口径（§67.2 三轴 / §64.2 E 级）：** 本节四项的 `coverage_status = covered`（对象、命令、数字齐备）、`implementation_status = implemented`（当前树可复现）、`evidence_level = E4`（自动化验证：CTest + 基准脚本 + `release_verify`）。但第 1、3 项的**原始 `inim` 前提** `coverage_status = conflicted`——文档假设的对象在本仓库全历史中不存在，已按实际对象重述；**跨版本自举（用 0.4.0 的 `inim` 编译 0.5.0 源码）未发生，不构成 E4，不宣称**。

#### 更正：自举工具链曾**不可解析**（2026-10-02 发现，同日修复；本文第 1 项仍不可读作「自举可用」）

上面前置更正说「自举编译器以源码分发」，这只说明**源码在**，不等于**能跑**。实测：

```bash
./build/inimerse selfhost/compiler.im selfhost/d1.im          # exit 1
./build/inimerse selfhost/compiler.im --dump selfhost/lexer.im # exit 1（文档记载的用法）
# 两者都是：Error at line 564: expected '':'', but got '}' (type 98)
```

失败发生在**载入/解析期**，早于 `compiler.im` 自身的任何逻辑；报错来自宿主解析器（`src/parser/parser.c:84`），`selfhost/*.im` 里根本没有 `expected` 这个字面串。

**根因（已定位到行）：** `src/parser/parser.c:1650-1664`（表达式语句后）与 `src/parser/parser.c:1689-1700`（`say` 后）处理后缀 `if`/`unless` 时**不检查换行**，于是把下一行 `if cond {` 的 `if` 当成后缀条件吃掉；剩下的 `{ ... }` 落到语句位置，被 `src/parser/parser.c:305-338` 当**表达式**解析成字典字面量，在 `src/parser/parser.c:327` 的 `consume(p, TOK_COLON, "':'")` 上炸掉。后缀条件本身是**合法且有测试**的语言特性（`vtest/postfix_condition_v04.im` 就是 `mark_true() if true`），所以这是**真歧义**，不是笔误。

最小复现（4 行，逐字验证 exit 1）：

```im
        emit(ctx, OP_JUMP, pos, 0, 0)
        if len(g_breaks) > 0 {
            push(g_breaks[len(g_breaks) - 1], pos)
        }
```

即：**`<调用语句>` 换行后紧跟 `if cond {` 一律解析失败**。`say "hello"` + `if true { say "yes" }` 这种最普通的写法同样 exit 1（报 `Error: expected 'expression', but got 'say' (type 27)`）。

**这是回归，不是历史如此：** 后缀 `if` 由 `a21915b`（2026-09-06 06:09，"Add postfix if and unless syntax"）引入，`say` 上的后缀由 `fe65f6d`（2026-09-06 06:20）引入；两者都**不是** 0.2.0 发布点 `8248e08`（2026-08-27）的祖先。而该模式在 0.2.0 时已存在于 `selfhost/compiler.im` 与 `scripts/array_test.im` 中——即自举编译器是在 2026-09-06 那天被悄悄打坏的。

**影响面（主树，已排除 `.worktrees/`）：** 该模式共 **19 处**，其中 `selfhost/` **15 处**（`compiler.im`、`eval.im`、`lexer.im`、`parser.im` 全中），另有 `scripts/array_test.im:30`、`examples/scripts/block_edit.im:232`、`projects/demo/main.im:330` 及根目录 1 处。三个非 `selfhost/` 文件实测全部 exit 1，且 `grep -rn "array_test\.im\|block_edit\.im\|projects/demo/main\.im" CMakeLists.txt tools/` **零命中**。

**为什么门禁是绿的：** 没有任何测试碰 `selfhost/*.im`（`grep -rn "compiler\.im" tools/ CMakeLists.txt` 零命中；CTest `inim_regression` #28 跑 `tools/inim.test.py`，`selfhost_benchmark` #30 跑 `tools/selfhost_bench.py`）。`selfhost/` 与 0.2.0 发布版**逐字节相同**（`git diff --stat 8248e08 HEAD -- selfhost/` 为空），所以它一路腐坏而七阶段全绿。[STATUS.md](../STATUS.md) §574 仍把 `compiler.im --dump lexer.im` 记作自举入口，已失效。

**测量方法上的教训（值得单独记）：** 最初用 `grep 'Error at line'` 判定失败，结论全错——`src/parser/parser.c:71` 的 `parse_error_expected()` 打印的是**不带行号**的 `Error: expected 'X', but got 'Y' (type Z)`，于是所有走这条路径的失败都被误判为「通过」。**判定编译器行为必须用退出码，不能 grep 报错文本。**

**口径（§67.2 / §64.2）：** 本缺陷的证据级别 **E4**（`./build/inimerse` 的真实退出码 + 最小复现 + 全仓扫描计数 + `git log -S` 定位引入点）。**发现时**自举工具链的 `implementation_status` 记作**不可运行**：源码齐备但解析不过。本节四项测量**不受影响**（它们测的是宿主 `inimerse` 与基准脚本，不依赖 `selfhost/*.im` 能跑），但**跨版本自举（用旧版编译器编译新版源码）在这条链路修好之前不可能发生**。

修复已开 [BOARD.md](../BOARD.md) §5 行 `selfhost-parser-postfix-ambiguity`（写域 `src/parser/parser.c`、`selfhost/`、`docs/`）。

#### 后续：解析回归已修复，但自举产物仍与宿主路径不一致（2026-10-02 同日）

消歧规则定为**后缀 `if`/`unless` 必须与宿主语句同行**：`src/parser/parser.h` 的 `Parser` 新增 `int prevLine;`，`src/parser/parser.c` 的 `advance()` 改为先记 `p->prevLine = p->lex.line;` 再取下一枚 token，并新增 `postfix_cond_here(p)`（`return p->lex.line == p->prevLine;`），两处后缀调用点（原 `:1650`/`:1689`）改用它。之所以选同行规则而非「`if` 后跟条件再跟 `{` 时优先当块语句」：`lexer_next()` 先 `skip_whitespace()` 再取词（`src/lexer/lexer.c:100-101`），所以这两个行号是**已有信息**、零额外前瞻；块语句候选则要保存/恢复 `Lexer` 再解析一遍条件，既重复解析又会让首次尝试的 `parse_error_expected()` 打到 stderr。

修复后（均为实测）：

```bash
./build/inimerse selfhost/compiler.im tests/_mini.im            # exit 0（修复前 exit 1）
./build/inimerse selfhost/compiler.im --dump lexer.im           # exit 0（文档记载的用法）
```

`selfhost/` 下 15 个 `.im` 与 `scripts/array_test.im`、`examples/scripts/block_edit.im`、`projects/demo/main.im` 共 **19 处全部不再报解析错**。进树回归两条：`vtest/postfix_condition_multiline_v05.im`（CTest `postfix_condition_multiline_runtime`）与 `selfhost_toolchain_parses`（跑 `compiler.im --dump tests/_mini.im` 并断言输出含 `main:`）；`EXP_CTEST` **97 → 99**。两条回归都做过**负控**：`git checkout HEAD -- src/parser/parser.c` 后重编，二者均 `Failed`。

**但「能解析」不等于「能用」——宿主路径与自举路径的产物差别极大：**

```bash
./build/inimerse selfhost/test1.im                 # 宿主路径：13 行程序输出
./build/inimerse selfhost/compiler.im test1.im     # 自举路径：0 行程序输出（exit 仍是 0）
./build/inimerse selfhost/compiler.im --dump test1.im   # 完整输出只有 main: 和 33,0,0,0
```

`33` 就是 `OP_HALT`（按 `src/compiler/bytecode.h:7-21` 逐项数得，`OP_MOV`=0 … `OP_HALT`=33），即**自举编译器对任何程序都只发出一条「停机」指令**；对 3,421 字节的 `selfhost/lexer.im` 同样如此。`compile_program`（`selfhost/compiler.im:723-746`）在 `:743-745` 的 `for s in stmts { compile_stmt(mctx, s) }` 之后于 `:746` 发 `OP_HALT`，dump 里只剩后者，说明循环体一条都没发——是 `parse_file(tgt)`（`:806`）返回了空列表，还是 `compile_stmt` 全是 no-op，尚未区分。

**注意退出码在这里完全掩盖了分歧**（两条路径都 exit 0），所以判据必须是程序输出或字节码哈希。该缺陷已开 [BOARD.md](../BOARD.md) §5 行 `selfhost-codegen-empty`，并把「C 路径 vs 自举路径的规范化字节码哈希 + 运行输出哈希成对记录」列为它的核心验收物（这也是 ROADMAP「编译器自举与双构建对比」中 stage1 的地基）。口径：自举工具链当前 `coverage_status = partial`、`implementation_status = 前端可用 / 后端空转`、`evidence_level = E4`（有实测对照），**跨版本自举仍不主张**。

此章节将在每个版本发布后更新验证记录，形成版本间的编译器效果演变曲线。
