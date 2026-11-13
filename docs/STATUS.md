# Inimerse / Infiverse 状态、路线图与版本裁定

> **本文件是项目状态、路线图和版本裁定的唯一权威来源。**
> 语言与 API 事实见 [API.md](API.md)；需求分析与治理裁决见 [REQUIREMENTS_ANALYSIS.md](REQUIREMENTS_ANALYSIS.md)；历史版本说明与已废止文档见 [archive/](archive/README.md)。
>
> 口径核实日期：**2026-10-01**。核实时使用的方法与原始输出见 §2。 ★ 本文件里的一个数是**写下它的那一刻在那个观测点上的读数**，不是「今天」的值：**同一个记号既承担「现在是 N」的断言、也承担「它从 N 走到 M」的记录，而两者逐字同形**；**想知道它今天是否还成立，唯一的办法是重跑产生它的那条命令，不是读它。**

---

## 1. 口径

### 1.1 实现状态（每个条目必须标注，缺一不可）

| 标记 | 含义 | 最低证据 |
| --- | --- | --- |
| **已验证** | 代码已合入，且有可重复的自动化验收 | CTest 用例名或 `tools/*.test.*` |
| **部分实现** | 有可运行代码，但覆盖面、错误处理或跨平台行为尚未冻结，**不承诺兼容性** | 源文件路径 |
| **设计未实现** | 只有设计文档，无可交付实现 | 设计文档章节 |
| **已废止** | 曾被文档声称存在，经核实为不成立 | 见 §3.2 裁定表 |

### 1.2 证据等级（E0–E6）

E0 概念 · E1 文字设计 · E2 静态样例 · E3 可运行原型 · E4 自动化验证 · E5 多环境验证 · E6 长期运行证据。

**硬规则**：

1. 只有 **E4 及以上**才能称为**已验证**；E3 不证明跨平台兼容、网络权威、AOT 加速或安全隔离。
2. 不得使用「支持所有平台」「完全兼容」「已完成」这类无维度、无证据的表述。
3. 状态必须**按维度分别标注**，例如：语法 已验证 / 编译器 部分实现 / Linux 后端 部分实现 / WASM 完整组件模型 设计未实现。
4. **设计文档中的方案不等于已具备功能。** 任何把「讨论中方案」写成「已实现」的表述都按 §1.1 的**已废止**处理。
5. 覆盖 ≠ 实现 ≠ 验证。三者必须分开陈述。
6. **归档件不受引用有效性约束。** `docs/archive/` 与 `future/archive/` 是**历史快照**：里面的路径、文件名、章节号记录的是**写作当时**的事实。被指向的文件后来被移动或删除**不构成缺陷**，也不应回改归档件去追新路径（改了就篡改了历史）。`tools/check_doc_paths.py` 因此按设计跳过这两个目录（见其 `:24-25` 的 `frozen history` 注释）。

**推论（`archive-changes-refs` 的裁定）**：只把检查器放绿**不等于**把引用接上。一条归档件里的引用若要让人能顺下去，就必须在**该处**写明去向；否则「检查器全绿但引用是断的」这件事仍然成立。本仓库两件事都做：规则写在上面这条，`docs/archive/protocol_v1.md:50` 这一处就地补了说明。

---

## 2. 当前基线（2026-10-07 更新测试计数到 148；**2026-10-20 在 `069c316` 上重测，计数已走到 157**（见「全量测试」格）；其余各项为 2026-10-01 实测。**表内数字除注明外均为 Linux 实测**；平台并列项见「全量测试」与「测试注册」两行 —— **不写行号**，行号会随本档增删漂移）

| 项目 | 实测值 | 证据 |
| --- | --- | --- |
| 版本 | `0.5.2` | `CMakeLists.txt:8`（源码中的版本）。**已发布点 = 发版说明落笔点 `751b2a0`**（`CMakeLists.txt:8` 升到 `0.5.2`、发版说明落笔，都在这一笔上；说明见 [RELEASE_0.5.2.md](RELEASE_0.5.2.md)）。**但这一笔不等于「四个版本生产点都说对了」的那棵树**：实测在 `751b2a0` 上 `CMakeLists.txt:8` = `0.5.2` ✅，而 `installer.iss:7` 仍是 `AppVersion=0.5.1`、`:13` 仍是不带版本号的 `InfiverseSetup`、`build_installer.ps1:25` 同 ⇒ **四个生产点只有 1 个对**；改对它们的是 `2cf072e`。**这一版没有任何一个提交同时说对四个** —— `751b2a0` 说对 1/4，而 4/4 的那棵树（`git rev-parse 'v0.5.2^{commit}'` 给出的那个）在发版**之后**。判据一条命令：`for p in CMakeLists.txt:8 installer.iss:7 installer.iss:13 build_installer.ps1:25; do git show <ref>:$p; done`，任一与 `CMakeLists.txt:8` 的版本不等 ⇒ 那个提交不是发版点。**本格不写 tag 当前指向哪个提交**：tag 是**可变指针**（本轮它被移动过两次，而移动在本地不留痕迹 —— `git reflog show v0.5.2` 返回空），要它就 `git rev-parse 'v0.5.2^{commit}'`；**也不要从它派生任何数**。本格初稿写过 `v0.5.2` → `0ebd68d` 与「到 0.5.2 发布点 **76**」：**前者在 tag 被移动后失效，后者是 `751b2a0` 上的值**（**记录一个曾经的绑定，不能写成绑定的形状**：`tools/check_release_tags.py` 按形状认绑定，分不出「断言」与「回忆」，扫一眼这一格的读者也分不出） —— 而写下它们的那一笔**当时确实是对的**，是后来的操作把它们改成了假的。这**不是** A（命令复现不出）、**不是** B（筛选条件错）、**也不是** C（自指），是第四种：**写下时对、之后被别人移动了被量的东西**。见 §1.74 E 段。上一个已发布点 `v0.5.1` = `4e444dd`；**两个不同的锚，别当同一个数**：`git rev-list --count 'v0.5.0^{commit}'..5868940` → **112**（到语言迁移交付点），`git rev-list --count 'v0.5.0^{commit}'..'v0.5.1^{commit}'` → **113**（到 0.5.1 发布点）；**0.5.2 的那个数一律带 ref 写**，例如「在 `751b2a0` 上量是 **76**」 |
| 干净构建 | configure / build 均退出码 0，**35 warnings / 0 error** | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| 全量测试 | **148 / 148 真通过**（Linux，`8256924`/`6de3d9ad` 时点）；**2026-10-20 在 `069c316` 上重测为 157 / 157**（`ctest --test-dir build -j8` → 逐字 `100% tests passed, 0 tests failed out of 157`；`tools/gate.sh --jobs 6` 同一棵树 16/16 全过），无 `WILL_FAIL` 记账项；`-j12` 高争用单轮约 13 s。**按平台并列，且每个读数带自己的 ref**：本树（合并后的 `main`）Linux `Total Tests: 148` / `release/051-final` @ `c71ea00` （导出树 `ab70a71` 与它逐字相同，**两个 sha 都要写**：读数是在那棵导出树上取的，`c71ea00` 是它的工具链归属）上 Windows `ctest -N` 的 `Total Tests:` **131**（**122 是 ctest 汇总分母、120 是实跑，都不是 `Total Tests:`**；**`122 = 131 − 9`**：分母只排除 9 条 `(Disabled)`，2 条 `***Skipped` 仍在 122 里；见 §10.81） | `ctest --test-dir build -j$(nproc)`；Windows 读数见 §10.81（ucrt64，CI 同款工具链） |
| 高争用稳定性 | §2.9 的端口窗口**已关闭**：hub 一律用内核分配端口（`--port 0 --http-port 0`），不再由 harness 猜号。`tools/ports_race_probe.py` 实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」为 **6**，证明竞态在 harness 而非引擎）。本行原来的「80 轮失败 1 轮」是**内核分配之前**的数字，未复测 | `python3 tools/ports_race_probe.py`；`for i in $(seq 80); do ctest --test-dir build -j12; done` |
| 编译器诊断 | **35 条 warning，0 error**（§2.5 修复后干净重建日志） | 干净重建日志 |
| 引擎代码 | `src/` **114** 个 `.c` + **51** 个 `.h`，合计 **53,547** 行（`.c` 单独 **50,706** 行） | 文件个数与行数是**两个量，各给自己的命令**：`find src -name '*.c' \| wc -l`、`find src -name '*.h' \| wc -l`；`find src \( -name '*.c' -o -name '*.h' \) \| xargs cat \| wc -l`。**原值 `101`/`49`/`48,753`/`46,120` 已陈**，且原来只给了一条**数行数**的命令却拿它当**文件个数**的出处 —— 引的证据与它证的命题不是同一个量（@ `0ebd68d` 重取） |
| 内建函数注册 | 531 处 `vm_register_builtin*` 调用 | `grep -rho 'vm_register_builtin[a-z_]*' src \| wc -l` |
| 自举编译器 | `selfhost/` 48 个 `.im`、2,316 行 | `find selfhost -name '*.im'` |
| 脚本规模 | 仓库 316 个 `.im`（根目录 148 个为回归测试） | `find . -name '*.im' -not -path './build/*'` |
| 测试注册 | `CMakeLists.txt` 中 `add_test(` 的个数**不写在这里**（它是 `grep -c 'add_test(' CMakeLists.txt` 的读数；**它不等于 `EXP_CTEST`** —— `EXP_CTEST` 是 `tools/gate.sh` 里那个常量，`stage_ctest` 用它断言输出里确有 `0 tests failed out of $EXP_CTEST`，量的是**跑起来的**条数；两者今天相等是巧合，不是约束。历史增量见各 §10.x）。**注册数不是任一平台的实跑数**：注册还受 `INIMERSE_NODE`（`:233` `find_program(INIMERSE_NODE node)`）、`INIMERSE_CLANG`（`:367`，条件为 `:368 if(INIMERSE_CLANG AND NOT WIN32)`）、`INIMERSE_PYTHON`（`:66`）三个**环境**条件裁剪，而它们都不是平台条件，所以平台间的差**无法只由 `CMakeLists.txt` 推出**（这四个行号已在合并树上复核）。Windows 的实测留痕是 `release/051-final` @ `c71ea00`（**导出树 `ab70a71` 与它逐字相同**，两个 sha 都要写）上 `ctest -N` 的 `Total Tests:` **131**（汇总分母 **122**、实跑 **120**；**`122 = 131 − 9`** —— `ctest -N` 印了 131 条 `Test #N:`，其中 9 条带 `(Disabled)` 不进汇总分母，2 条 `***Skipped` 仍在分母里；**`ctest -N` 不检查可执行文件是否存在**，所以它是**注册表读数**、不是实跑数；见 §10.81）。**`131` 就是 Windows 配置出的注册数** —— 它作为 **Windows 读数**的出处是 `docs/RELEASE_0.5.1.md:14`；在 `EXP_CTEST` 记账里的出处是 §10.81 的「计数 131 → 132」（`docs/STATUS.md` §10.81（@ 57ece55 → :3663）），与同一行的 CTest 序号 `#132` 同处一行（`#131` 在同日的另一节 §10.80），**同一个数字在两个语境里是两个角色**（平台注册数 / 记账增量），这正是它容易被读错的原因。**本条不要写成「零命中」**：引用它的每一行都是它的一次出现，写下结论这个动作本身就会让那个计数失效（`docs/AUDIT.md` §1.65 已按此更正） | — |
| 工具 | `tools/` 98 个条目 | `ls tools \| wc -l` |
| 性能（`sum(1..2000000)`） | 解释器 88 ms = 1.00x · AOT 打包 = 与解释器**等同**（分布中位 **0.98x**） · Wasm MVP 58 ms = 1.51x | [SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) |

### 2.1 复现命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4      # 期望 100% tests passed, 0 failed out of 157（Linux；`069c316` 实测；此命令块里原写 148，那是 `8256924` 时点的读数）Windows 的 `Total Tests:` 是 **131**，汇总分母 122、实跑 120，见 §10.81）
node tools/node_suites/run_all.js                    # JS 侧协议套件 12 个（不在 CTest 内）
python3 tools/selfhost_bench.py --runs 5 --write-docs
```

### 2.2 已知问题（不影响 §2 结论，但必须记录）

- `src/mod/gui_mod.c` 含非 UTF-8 字节（`grep` 判为 binary）。该文件 3,996 行，是 Scratch 风格 GUI 实现，**不是**任何 2D 游戏引擎的场景树/瓦片/相机/碰撞抽象。
- `make wasm` 未先执行时，`tools/wasm_host.test.js` 会跳过（打印 `wasm host test skipped (build wasm first)`）。Wasm 相关 CTest（`wasm_backend_regression` / `wasm_host` / `wasm_probe`）不受影响。
- 根目录 `./inimerse` 是本地构建产物（`.gitignore` 已忽略 `/inimerse`），**不保证与源码版本一致**；请以 `build/inimerse --version` 为准。

### 2.3 §43.5 迁移导入：四条缺陷已**全部修复**（方案 B 重构，2026-10-01 验证）

**现状：本节记录的是修复前的证伪证据。** 四条缺陷已按 §9.1 的决定（方案 B）全部消除，`tools/economy_migration.test.py` 由「33 通过 / 5 失败」变为 **39/39 通过**，CTest 注册已去掉 `WILL_FAIL TRUE` 与 `known-defect` 标签。

下表是修复前的原始证据（保留，因为它是方案 B 的立项依据）：

`src/platform/http_posix.c` 里 `/economy/export` + `/economy/import` 曾是**未提交的在制品**（`git status` 显示该文件 218 行改动）。第三方核验（`tools/economy_migration.test.py`，注册为 CTest `economy_migration_regression`）证明**其核心声明未实现、且存在真实完整性缺陷**。测试 **33 项通过 / 5 项失败**，稳定可复现。

| # | 缺陷 | 位置 | 证据 |
| --- | --- | --- | --- |
| 1 | **核心声明未实现。** 注释写「re-derive the ledger chain from the package itself」，实际只扫描序列化文本里的 `"hash":"…"` 串、取**最后一个**与声称的 `ledger_tail` 比较；从不重算 sha256、不检查 `prev` 链接、不检查条目数。`prev`/`calc`/`n_events` 都是死变量，1552–1555 是立即 `break` 的死循环。 | `src/platform/http_posix.c:1549-1573` | 取 beta 的导出原字节，把 `ledger[0].amount` 由 700 改成 701（**所有 hash 字段、`ledger_tail`、`content_hash` 逐字节不变**）→ 全新 hub 返回 **HTTP 200 `"status":"imported"`**，应为 409 `ledger_chain_broken`。整条删除首个 ledger 条目 → 同样 200。对照：改动最后一个条目自己的 hash → 正确 409。 |
| 2 | **余额快照同样未认证。** `balances_hash` 只当作请求里的**声明**读取，从不从 `balances` 数组重算；`content_hash` 只封住那串声明值。 | `src/platform/http_posix.c`（import 的 `balances_hash` 处理） | `balances[0].amount` 600→601、其余字段保持导出原样 → 200 `imported`，并把已失真的 `9323c77f03d1899ea1aedd9934293c5ec5f37239edb6f5c6939912249a3a6ea2` 当作业已验证结果回显。只改 `balances_hash` 字段本身**会**被抓（409 `integrity_failed`），说明封条只覆盖声明值。 |
| 3 | **一字之差的 off-by-one。** `"GET /economy/domain/"` 是 **20** 字符，代码写 `+ 19`，于是 cid 变成 `/<64hex>`，`econ_currency()` 永不匹配。 | `src/platform/http_posix.c:1256` | 在**刚声明过 alpha 的那个 hub 上**取 alpha：`GET /economy/domain/83ebe5ee…` → `404 {"error":"currency_not_found"}`。兄弟处理器 `src/platform/http_posix.c:861` 用的是正确偏移（`"GET /route/"` = 11 → `+ 11`）。修法：`+ 20`。 |
| 4 | **分词脆弱。** 扫描要求字面量 `"ledger":[`；合法 JSON 变体 `"ledger": [`（多一个空格）会让 hub **拒绝自己未被篡改的包**并报 409 `ledger_chain_broken`——一个误导性的「篡改」判定。 | `src/platform/http_posix.c:1558` | 与同文件自身的约定不一致：`json_field_string`（`src/platform/http_posix.c:369`）显式跳过 `:` 后的空格与制表符。 |

**正确的重算代码在同文件里已经存在**：审计处理器 `src/platform/http_posix.c:1378-1396` 做了 `sha256(prev ‖ "seq|kind|currency_id|from|to|amount|idem")` 并**同时**比对 `hash` 与 `prev`。导入器可对包内 `ledger` 数组套用同一公式。

**另一条必须知道的性质**：保留账本是**跨币种的单条全局链**（`g_econ_tail` 是全局变量），因此币种级导出的切片起点 `prev` 可能落在包外（同 hub 先声明 alpha 再声明 beta 时，beta 切片的 `prev` 等于 alpha 的末哈希）。导出的 "self-contained integrity / no server key required" 只在**切片内部**成立，**不是**从创世起可验证。

**处置（已完成）**：按 §9.1 的决定执行方案 B —— `/economy/import` 改为用 `src/verse/json_min.{h,c}` 结构化解析，并对包内 `ledger` 数组**逐条重算** `sha256(prev ‖ canon)`、同时校验 `prev` 链接与 `content_hash`/`definition_hash`，另从 `balances[]` 重算 `balances_hash` 并要求快照等于该切片的重放结果。`GET /economy/domain/` 的 `+ 19` 已改 `+ 20`。修复前该 CTest 曾以 `WILL_FAIL TRUE` + `known-defect` 记账；**现该标记与标签均已删除**，测试为真通过。

### 2.4 Ed25519 签名每 32 次错 1 次（**已修**，这是发布门禁 flaky 的真正原因）

**症状**：全量 `ctest` 会**间歇性**失败，失败项每次都不同（`verse_pack_regression`、`node_discovery_regression`、`reconnect_generation_regression`），而**单独重跑一律通过**。典型断言是 `tools/reconnect_generation.test.py:103` 的 `assert "adv=1" in out`（实测 `adv=0`），hub 返回 `HTTP 400 {"error":"invalid_signature"}`。曾被误判为端口冲突——已排除：`free_port()` 在 200 组三元组里没有重复端口。

**根因**（`src/common/ed25519.c:416-421`）：`ed25519_sign` 计算 `S = (r + k·a) mod L` 时，`s` 保存 8 个 limb 的 512 位乘积 `k·a`，但把 `r` 相加的循环**只遍历低 4 个 limb**：
```c
for (int i = 0; i < 4; i++) { c += (u128)s[i] + rbuf[i]; s[i] = (u64)c; c >>= 64; }
```
`i == 3` 之后 `c`（进位）被**直接丢弃**。当 `t_low + r` 溢出 256 位时，和比真值少了正好 `2^256`，于是 `scalar_mod_L` 得到 `S = S_correct + (16L − 2^256)` —— 一个**无效签名**。溢出概率 ≈ `(L/2)/2^256 = L/2^257` = **恰好 1/32**。

**定位证据**（用 `cryptography` 46.0.5 的 Ed25519 作参考实现）：
- 引擎自签自验 2000 组 → **64 失败 = 3.2%**（首次失败 index 12）。修后 **2000/2000 通过**。
- 600 次固定 `(key, msg)` 重复 → **18/600 = 3.0%**，且**同一 `(key,msg)` 每次结果一致**（`differing_repeats=0`）⇒ 排除未初始化内存。
- 用纯整数运算复算 RFC 8032 的 `S`：389 条逐字节精确匹配，11 条的差值**恒为常数 `443877084437957656573631004654138375888` = `16L − 2^256`**（`L = 2^252 + 27742317777372353535851937790883648493`）。
- 大规模交叉验证：修复后 **20,000 条签名全部被参考实现接受，0 拒绝，公钥 0 不匹配**。

**修法**（一行，已落地）：循环改为 `for (int i = 0; i < 8; i++)`，让进位穿过全部 8 个 limb。`t + r < 2^506 + 2^252 < 2^512`，故 limb 7 的进位确实为零、可安全丢弃。

**回归防线**：`src/common/ed25519_probe.c`（CTest `ed25519_probe`）现在有四组守卫：①**RFC 8032 §7.1 TEST 1/2/3 已知答案**（公钥 + 签名字节比对，并用引擎验证官方签名）；②**SHA-512 已知答案**，13 个长度覆盖 padding 边界与 8 KiB 之外（`0/111/112/113/127/128/129/1000/8255/8256/8257/9000/20000`），并额外用 7 字节分块喂流式 API 要求与一次性结果一致；③**Ed25519 长消息已知答案**（固定 seed `00..1f`，长度 `0/1/8256/8257/20000` 的期望签名硬编码）；④确定性 round-trip fuzz `600` 轮（对 1/32 缺陷期望 18.75 次失败，`(31/32)^600 ≈ 5e-9` 漏检率；CTest 实测 3.4 s，此前 2000 轮版本是 29.7 s）。**现有测试每套只签几次，抓不到 3% 的失败率——这正是它此前长期潜伏的原因。** 写这套 KAT 的收益立刻兑现：它第一次运行就抓出了本节修复过程中我自己引入的一个 SHA-512 长度字段错误（见下）。

**长消息静默截断（**已修**）**：原实现把 `R ‖ A ‖ M` 拼进固定缓冲 `unsigned char inp[128 + 8192]`，在 `off < sizeof inp` 处停止拷贝，于是**超过 8288 字节的消息被静默截断**；`ed25519_verify` 的同一缓冲从偏移 64 起，上限是 **8256** 字节，两个阈值还不一致。结果：**长度 > 8256 字节的消息产生的签名永远无法通过验证，且不报任何错**。修法：给 `src/common/ed25519.h` 增加流式接口 `Sha512Ctx` + `sha512_init/sha512_update/sha512_final`（`h[8]`、`total`、`buf[128]`、`buflen`），`sha512_buf` 降为它的一次性包装，`ed25519_sign` 的三处（`r` 的 `h+32` 与消息、`k` 的 `rEnc‖pub‖msg`）与 `ed25519_verify` 的 `k` 全部改为流式调用；`grep "128 + 8192"` 已无命中。**修复后实测**：`0/1/31/32/33/127/128/129/255/1024/4096/8191/8192/8255/8256/8257/8288/8289/9000/20000` 共 20 个边界长度**全部被 python `cryptography` 参考实现接受，0 拒绝**（修复前 8257/8288/8289/9000/20000 被拒）。**顺带修掉的一个我自己引入的错误**：第一版 `sha512_final` 把 64 位长度写进 `buf[112..119]` 却未初始化 `120..127`，空串摘要得到 `8d3c2d62…` 而非 `cf83e135…`，被上面的 KAT 在第一轮就拦住；正确写法是长度字段按 128 位大端，高 8 字节 `112..119` 置 0、低 64 位写 `120..127`。另一个调用点 `src/mod/verse_dist_mod.c:435` 只用 `sha512_buf`，不受影响。

### 2.5 `ctest -j12` 偶发失败：UDP hub 的就绪信号说谎（**已修**）

**症状**：`ctest -j12` 下 `hub_dist_regression` 间歇性失败，栈为
`tools/hub_dist.test.py` 的 `udp_fetch` → `data, _ = u.recvfrom(65536)` → **`TimeoutError: timed out`**；
单独重跑必过。也曾以 `reconnect_generation_regression` 的 `AssertionError: hub N did not start` 形态出现。

**排查中被证伪的两个理论**（记录下来以免重复走）：
- 「并发敏感」——`--repeat until-fail:15` 15/15 通过，证伪。
- 「UDP 端口被别的进程抢占」——`ss -ulpn` 实测每个 UDP 端口只有一个 PID，且把 UDP 从
  `INADDR_ANY` 改成 `127.0.0.1`、去掉 `SO_REUSEADDR` 后**失败率不变**（仍 22/24），证伪。
- 「`free_port()` 重复分配」——确实存在（同一表达式里连续调用约 1/3 批次会重复），且确实会
  造成 `headless: bind N failed`，已另行修复；但它**不是** UDP 超时的原因：换成
  `distinct_ports()` 后超时仍是 22/24。

**根因**（`src/platform/http_posix.c` 的 `verse_http_start()`）：函数**先**建立 TCP 监听、
置 `g_http_running = 1`、启动 accept 线程，**之后**才 `socket(AF_INET, SOCK_DGRAM)` + `bind` UDP 并启动
`udp_loop`。于是存在一个「TCP 已经接受连接、UDP 还没绑定」的窗口。测试套件的就绪判据是
`wait_port()`（TCP accept）或 `wait_http_ping()`（`GET /ping`）——**它们只证明 TCP 就绪**。
在 `-P 12` 的 CPU 争用下这个窗口被拉长，实测：

```
TCP accept at +0.1199s, UDP socket visible at +0.1404s     # 单跑，窗口 ~20 ms
TCP 接受后再发一个数据报，等到 UDP 应答需要 +0.152s        # -P 12，窗口 ~150 ms
```

窗口内发出的**单个**数据报被内核直接丢弃，且 **UDP 不重传**——所以一次性的
`udp_fetch()` 永久等待。判据实验（同一份脚本，只改参数）：

| 实验 | 结果 |
| --- | --- |
| TCP 就绪后立刻单发一次（套件的做法） | **12/12 超时** |
| 同样单发，但在 TCP 就绪后等 1 s | **0/12 超时** |
| 12 个 hub 串行取包 | 0/12 |
| 单个 hub、12 个并发客户端 | 0/12 |
| 12 个独立进程各起一个 hub（`-P 12`） | **被判为超时** |

即：不是端口、不是并发客户端、不是 hub 数量，而是**「进程刚起来」与「TCP 先就绪」的叠加**。
引擎侧加日志后确认：失败的那些 trial 里 `udp_loop` **一次 `recvfrom` 都没有发生**——数据报从未到达引擎。

**修法**：把 UDP 的创建、绑定与 `udp_loop` 启动**整体前移到 TCP 监听之前**；TCP 监听或 accept 线程
创建失败时，回滚已启动的 UDP 线程并关闭其 fd。绑定地址同时由 `INADDR_ANY` 改为 `INADDR_LOOPBACK`
（与 TCP 监听一致，这个 hub 是本地进程服务），并去掉 `SO_REUSEADDR`——这两项不是为了修超时
（它们修不了），而是缩小「别的 socket 影子绑定同端口」的可能面。改后「TCP 能连上」才第一次成为
**两种传输都就绪**的诚实信号。

**验证**（改后）：
- 单发即 TCP 就绪后立刻发：**0/12 超时**（改前 12/12）。
- `ctest -j12` 原 8 个易抖测试连续 **25 轮全部 100% 通过**（改前 20 轮里 3 轮失败）。
- 全量 `ctest -j$(nproc)` 连续 **3 轮 85/85**。
- 清理构建：35 warnings / **0 error**。

**教训**：`wait_port()` 只证明「有人在该端口 accept」。当同一端口上还有第二种传输时，
不能用它当整体就绪判据——要么让第二种传输先就绪（本轮做法），要么给套件一个真正覆盖两种传输的
就绪探针。这条与 §2.4 的教训同源：**不确定性失败先怀疑产品代码，别先怀疑测试**。

### 2.6 `closure_probe` 的堆损坏：探针自身在 release 之后读已释放对象（**已修**）

**症状**：`ctest -j12` 约每 60 轮出现 1 次 `closure_probe (Subprocess aborted)`，报
`free(): invalid size` / `corrupted size vs. prev_size`，偶发 segfault。单独跑 **2000 次全过**，
十二路并发跑 **3/720 失败**。

**根因（ASan 给出确切栈）**：

```
ERROR: AddressSanitizer: heap-use-after-free
READ of size 8 ... thread T0
    #0 im_closure_env_get    src/vm/closure.c:69
    #1 im_closure_env_copy_slot  src/vm/closure.c:60
    #2 main                  src/vm/closure_probe.c:50
freed by thread T0 here:
    #1 im_closure_env_release   src/vm/closure.c:55
previously allocated by thread T0 here:
    #1 im_closure_env_new       src/vm/closure.c:37
```

`src/vm/closure_probe.c` 原第 38 行是 `im_closure_env_retain(e); im_closure_env_release(e); im_closure_env_release(e);`
—— `e` 出生时 `refs == 1`（`:22` 已断言），这一行 retain 到 2、release 到 1、再 release 到 **0 ⇒ 就地释放**。
而后面 `:50` 的 `assert(!im_closure_env_copy_slot(empty, 0, e, 0))` 仍把 `e` 当源传进去，读的就是那块已释放内存。

**为什么时隐时现**：`build/CMakeCache.txt:25` `CMAKE_BUILD_TYPE:STRING=Release`，而
`CMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG` —— **Release 下 `assert` 被编译掉**，那条断言不再执行，
use-after-free 退化成堆破坏，只在并发把内存布局扰动到特定形状时才炸。

**被证伪的假设**：我最初怀疑 `im_closure_env_retain` / `im_closure_env_release` 引用计数非原子（两条线程各
10000 次并发 retain/release）。**这是错的**：`src/vm/closure.c:8` 与 `:11` 用的就是 `atomic_size_t refs`，
`retain`/`release` 走 `atomic_fetch_add/sub_explicit`，无数据竞争。

**修法**：只修探针，引擎代码零改动（按用户裁定）。`e` 的所有使用移到最终 release 之前；
`:50` 的空目标断言改用一个**活的**源 env（新增 `src`），并补一条「目标槽位越界也必须拒绝」的断言；
最终那次 release 成为对 `e` 的最后一次触碰。修后：ASan **0/480**（改前第 1 轮即触发）、
Release 十二路并发 **0/720**（改前 3/720）。

**教训**：`assert` 在 Release 下不存在，所以**探针不能把唯一的正确性检查放在 `assert` 里**——
一旦断言被编译掉，错误就从「断言失败」变成「静默堆破坏」。另：**单独跑全过不等于没缺陷**，
并发压力与不同的 `-O` 级别才是暴露内存错误的条件。

### 2.7 `socket_probe` 的 exit 11：发出去到对方可读之间的竞态（**已修**）

**症状**：`ctest -j12` 偶发 `socket_probe (Failed)`，实测退出码 **11**（`src/platform/socket_probe.c:19`
的 `if (im_socket_peek(accepted) <= 0) return 11;`），十二路并发 **2/480**。

**根因**：探针在客户端 `im_socket_send(client, "ping", 4)` 之后**只 peek 一次**就断言有数据可读。
发送与「字节到达并进入已接受套接字的接收队列」是两个独立事件，loopback 快但不瞬时；机器有负载时
字节可能还在路上。**读到 0 不是缺陷**——同一函数下面那段读 payload 的循环本来就是轮询的。

**修法**：把单次 peek 改成最多 200 次、每次 1 ms 的轮询（与下方循环同样的处理方式），
`src/platform/socket_probe.c` 增 `#include <time.h>`。修后十二路并发 **0/480**（改前 2/480）。

### 2.8 hub 的 HTTP 绑定失败曾完全静默（**已修**）

**症状**：`ctest -j12` 下 hub 偶发「永不就绪」，进程存活、脚本跑完（stdout 有 `hub`、stderr 有
`headless: 127.0.0.1:N` 与 `[0]="hub"`），但**没有 `http api: 127.0.0.1:N` 这一行**，
随后调用方等到 10 s 超时才失败。

**根因（临时插桩取证）**：`src/main.c:1012-1013` 原来只在成功时打印，失败时什么都不说：

```c
if (verse_http_start(headless_http_port)) fprintf(stderr, "http api: 127.0.0.1:%d\n", headless_http_port);
```

插桩后 200 轮 × 6 hubs = 1200 次启动里 8 次 never-ready，每次 stderr 都是
`[hs] listen -> (nil) errno=98` ⇒ **errno 98 = EADDRINUSE**：TCP bind 失败，端口已被别的进程占用。
直接实验确认：故意让 `--port` 与 `--http-port` 取同一端口，得到与野外**逐字一致**的 stderr 签名
（有 `headless:`、无 `http api:`、`wait_http_ping` 为 False）。

**修法**：`src/main.c` 补上失败分支，打印 `http api: bind <port> failed (port in use?)`。
一个没有 HTTP API 的 hub 不是能用的 hub，这必须是响亮的失败而不是静默的半活状态。
（插桩已完全移除，`grep INIMERSE_UDP_DEBUG` 与 `[hs]` 均为 0。）

### 2.9 测试套件的端口分配：跨池重叠与 release 到 bind 的窗口（**已修**）

**两层缺陷，都已修**：

1. **`free_port()` 是 TOCTOU**（`tools/testports.py:58`）：`bind(0)` 取号后立刻关闭，返回瞬间即释放。
   实测单次连续分配 20 个时约 **23/300** 批次出现重复。全部 hub 端口点已改用 `distinct_ports`。
2. **`distinct_ports()` 的保证只在单次调用内成立**（`tools/testports.py:160`）：两次独立调用各建一个池，
   第二次可以拿到第一次已归还的号码——实测 `distinct_ports(6)` 两次重叠 **5/200**、`distinct_ports(3)`
   两次重叠 **5/500**。更隐蔽的是套件内部：`tools/economy_migration.test.py` 的 `start_hub` 原来自己
   `listen_port = distinct_ports(1)[0]`，这个新池**看不到调用方已持有的 6 个 HTTP 端口**，
   实测 **9/500** 会撞上。

**修法**：把「hubs 一次、tcp 再一次」合并为**单次调用**（`tools/lease_handoff.test.py:111`
`distinct_ports(7)`、`tools/node_discovery.test.py:150` `distinct_ports(5)`、
`tools/reconnect_generation.test.py:104` `distinct_ports(6)`）；`economy_migration` 改为
一次 `distinct_ports(12)`（6 HTTP + 6 listen），`start_hub(engine, cwd, script, port, log, listen_port)`
两个端口都由调用方传入。三个套件的 `start_hub` 回退分支加了警告注释（该池可能与调用方持有的端口相撞）。

**验证**：12 端口单池的 200 轮 × 6 hubs 就绪实验 **6/1200 → 0/1200** never-ready。

**残余（原判）与最终修法**：端口在「池归还」到「子进程 bind」之间仍有一个窗口，**另一个套件的池**可以在
这个窗口里抢到同一个号码。这是 `distinct_ports` 机制固有的，实测约 **1 轮 / 80 轮**全量 `ctest -j12`
会因此失败。原判给了两条候选修法：让引擎支持 `--http-port 0` 由内核分配并把真实端口报出来，或让套件在
就绪失败时用新端口重试。**采纳第一条，否决第二条**：就绪失败重试只是把不确定性推给下一次尝试，窗口仍在；
而「预留一个 socket 再交给子进程」也不可能成立 —— 预留必须在子进程 `bind` 之前**释放**，预留天生只是建议性的。
已实施（见 §10.10）：引擎支持 `--port 0` / `--http-port 0` 并把内核给的**真实**端口报出来，harness 改为
**从引擎读取端口、不再猜号**；`tools/ports_race_probe.py` 实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」
为 **6**，证明竞态在 harness 而非引擎）。

---

## 3. 版本裁定

### 3.1 v0.5.0（已在 2026-09-25 发布，tag `v0.5.0` 存在）

**已交付且可自动化验证（E4）**

| 能力 | 证据 |
| --- | --- |
| 自举编译链路 | `selfhost/*.im`；CTest `selfhost_benchmark`；`docs/archive/SELFHOST_BENCHMARK.md` |
| 增量编译与依赖尾块 | CTest `cli_incremental_regression`；`tools/cli_incremental.test.py` |
| 可复现构建与产物清单 | CTest `release_verify_regression`；`tools/release_verify.py` |
| Wasm 数值子集后端 | CTest `wasm_backend_regression` / `wasm_host` / `wasm_probe`；`tools/wasm_backend.test.py` |
| 绑定生成与扫描工具 | CTest `bindgen_regression`（`tools/bindgen.test.py` → `tools/bindgen.py`）、`scan_tools_regression`（`tools/scan_tools.test.py` → `tools/cpp_scan.py`、`tools/python_scan.py`）、`migrate_report_runtime`（`tools/migrate_report.test.py` → `tools/migrate_report.py`）。本行原先把它与另外三个工具并列当证据，而**没有任何东西跑它**；该缺口已补（见 [AUDIT.md](AUDIT.md) §1.59 第 4 条与 §1.60） |
| 语言语法糖（`?`、`\|>`、`case try`、lambda、`>>`、集合推导、`?.`、`??`） | CTest `pipeline_runtime` / `try_finally_runtime` / `lambda_*_runtime` / `case_*_runtime` / `null_coalesce_runtime` / `optional_member_runtime` |
| POSIX 运行时对等 | CTest `posix_core_api_runtime` / `posix_runtime_parity` / `headless_probe` |
| 协议与回放（CRP 会话、节点发现、权威交接、断线重连、确定性回放、包签名） | CTest `crp_session_flow_regression` / `node_discovery_regression` / `lease_handoff_regression` / `reconnect_generation_regression` / `replay_closure_regression` / `verse_pack_regression` / `hub_dist_regression` / `protocol_regression` |
| 经济域与可审计结算 | CTest `economy_domain_regression`；`tools/economy_domain.test.py` |

**声称与现状的对照** —— `docs/archive/RELEASE_0.5.0.md` 的下列表述：前两条**今天仍不成立**，后三条**已被后续交付推翻**（保留在此是为了让「当初为什么不成立」有据可查）。行号按 2026-10-02 的该文件正文计 —— 更正横幅插在文件顶部，此前的行号全部漂移了 9~11 行：

| 该文件声称 | 核实结果 |
| --- | --- |
| 「AOT compilation … outperform the interpreter by at least 2x」（第 52 行） | 实测与解释器**等同**——12 次试验中位 **0.98x**（0.85x..1.33x），与「解释器对自己」的对照区间（0.77x..1.36x）**12/12 重叠**，1.09x 只是其中一个噪声样本。AOT 通道是**打包**通道（引擎副本 + 嵌入规范化字节码），复用同一个 C 解释器，不自生成原生代码。原文 `SELFHOST_BENCHMARK.md` 已自述「不满足 ≥2x 目标」。 |
| 「WebAssembly output … supporting SIMD optimizations and WebAssembly GC」（第 47 行） | **它把三件不同的事写在了一起，`wasm-simd-gc`（`3fadc8b`）逐件给了结论，见 §10.16**：**heaps 已做**（模块自己的线性内存 arena，确定性引用计数回收，耗尽显式 `heap_exhausted (code 4)`）；**v128 SIMD 已实现、已测量（本机 ~1.5–2.5×）、但生成器不选路**（16 字节装箱槽让向量存储无收益、整型 lane 定宽回绕而语言要求越过 int64 即报 `numeric_overflow`（v3.1 之前该差异表现为「32 位溢出提升为 float」，见 §10.49）、浮点归约重结合会改变结果 —— 与「逐字节等价」判据冲突）；**wasm-GC 明确未实现**，且它本来就是另一件事（需 `--enable-gc` 与 struct/array 引用类型）。`wasm_backend.h` 那句 `future work` 已改写。 |
| 「Python extension bridge: `inimerse_extension.c` implementing `PyInit_inimerse()`」（第 35 行） | **已推翻（`xlang-bridge`，`36bf4e9`）**：`src/bridge/inimerse_extension.c:93` 就是 `PyMODINIT_FUNC PyInit_inimerse(void)`，见 §10.15。当初的记录是「仓库中不存在该文件、全仓无 `PyInit_inimerse` 实现」。 |
| 「Java native bridge: `InimerseBridge.java`」（第 36 行） | **已推翻（`xlang-bridge`，`36bf4e9`）**：`InimerseBridge` 类由 `tools/bindgen.py` 从唯一 IDL 生成到构建树，`InimerseBridgeMain.java` 是仓库里的 driver；`build/bridge/InimerseBridge.jar` 含 `InimerseBridge.class`。见 §10.15。 |
| 发布产物 `.whl` / `.jar` / `inimerse-aot`（第 132 / 133 / 139 行） | **部分推翻（`xlang-bridge`，`36bf4e9`）**：`.whl` 与 `.jar` 现在真的会被构建出来（`build/bridge/inimerse-0.5.0-cp314-cp314-linux_x86_64.whl`、`build/bridge/InimerseBridge.jar`），但**是构建产物、不提交进 git**（见 §10.15 裁决 1）；`inimerse-aot` / `libinimerse.so` 仍不存在 —— `--aot` 是打包通道，见 §10.14。 |

**注**：`examples/` 下确有针对这些绑定的 `.im` 示例与 `build.gradle` / `pom.xml`，但示例脚本与真正的桥接产物不是一回事。

### 3.2 v0.6 声明废止表

来源：`docs/archive/ROADMAP_0.5-0.6.md`（v0.6 段每小节标 **已完成**）。以下裁定为逐条核实结果，**任何后续文档不得再引用其原表述**。

| 该文件声明（标「已完成」） | 核实证据 | 裁定 |
| --- | --- | --- |
| §1.1 Inim OS Daemon 常驻进程；`Task` / `Process` / `Thread` 与能力模型 | 全仓无 `inim_os_daemon` / `Inim OS Daemon` 字样，无 `src/inim_os` | **设计未实现** |
| §1.2 VFS 层级 `/system/`、`/verse/<id>/`、`/packages/`、`/cache/`、`/userdata/`；`vfile`；`inim os import` | 不存在 `inim` CLI；上述路径常量在源码中无对应实现 | **设计未实现** |
| §1.3 统一 PAL：`device::Keyboard`/`Mouse`/`Gamepad`、`window::Window`、`audio::Audio`、`network::Socket` | `device::` / `window::` / `audio::` / `net::` 在 `src/`、`selfhost/`、`examples/` 中 **0 处命中** | **设计未实现** |
| §1.4 `inim package install --os=…`、`inim verse deploy`、热更新/版本回滚/崩溃恢复、`log::crash_report()` | 不存在 `inim` CLI；`log::` 0 处命中 | **设计未实现** |
| §2.1 发布 `infiverse.protocol.v1/` 版本化 RFC 与兼容性矩阵 | 不存在 `infiverse.protocol.v1/` 目录，无 `.rfc` 文件 | **设计未实现**（协议本身以代码 + 白皮书形式存在） |
| §2.2 `verse::Layer` / `block::Block` / `cell::Cell` 参考实现 | 上述命名空间 0 处命中。但 `src/mod/infiverse_mod.c`（839 行）确有 `verse_biome_*`、`verse_block_*`、`verse_portal_*`、`verse_law_*`、`verse_snapshot` 等内建函数 | **部分实现**（API 名与文档不符） |
| §2.3 Hub / 节点发现 / 多节点服务器 / `inim verse pull\|push\|verify` | 节点发现与权威交接确有交付（CTest `node_discovery_regression`、`lease_handoff_regression`）；`inim verse …` CLI 不存在 | **部分实现** |
| §3 2D 游戏引擎：`scene::Node` / `tilemap::Tilemap` / `collision::AABB` / `camera::Camera` / `sprite::Sprite`；"已实现瓦片地图、精灵、输入、场景树、动画系统、音频抽象和编辑器集成" | `grep -rl 'tilemap\|scene::Node\|collision::AABB\|camera::Camera' src/ examples/` = **0 个文件** | **设计未实现** |
| §4 标准库 `io::` / `net::` / `sys::` / `thread::` / `async::`；"跨平台 I/O、网络和系统调用已完成" | 上述命名空间 0 处命中。但集合、`result`、`optional`、线程均以**内建函数**形式存在且有用例（`collection_*_runtime`、`result_*_runtime`、`optional_member_runtime`、`thread_*_runtime`） | **部分实现** |
| 实现清单段（内核态服务、文件系统、网络、显示引擎、渲染管线、物理引擎、音频引擎、字体引擎、模板引擎、数据库、队列、事件、定时器、IPC、安全/沙箱、内存管理、文件 I/O 一律称"已完成原型和基本测试"） | 仓库中不存在对应实现 | **已废止** |

**该文件另有结构性缺陷**：§1（Inim OS）与 §2（Infiverse 内核）的两套小节被重复写入（第 163–238 行与第 239–303 行内容相同，前者带「已完成」标记、后者不带），且两套都缺少 §3、§4 的对应内容。

### 3.3 唯一有实现证据的当前线

**`0.4.x` → `0.5.0`（tag `v0.5.0`）**，配合 `docs/archive/CHANGELOG_0.5.0.md` 的 `[Unreleased]` 段落。其余版本号表述（例如根 `README.md` 曾写"当前发布基线：0.5.0"却链到 `RELEASE_0.4.1.md`）属于笔误：**当时只修了那个链接，数字 `0.5.0` 留在原地**，直到本轮才把它改成 `0.5.2` —— 并且**这条基线现在有门禁比对**（`tools/check_release_tags.py`：根 `README.md` 写的发布基线必须等于最新 tag，不等即红）。

**`--jit=template|optimized` 是空开关。** 实测证据：`src/main.c:826` 调用 `im_jit_mode_parse()`、`:828` 赋值给 `im_jit_mode`，而 `im_jit_mode` 在 `src/` 内**除 `src/vm/jit_mode.c` 与其头文件外再无任何读取点**（`grep -rn 'im_jit_mode' src/` 仅命中 `src/main.c:826,828`）；`IM_JIT_TEMPLATE` / `IM_JIT_OPTIMIZED` 在 `src/vm/jit_mode.*` 之外**零引用**。因此该选项只改变一个从未被消费的全局变量，**不影响执行**。唯一相关测试 `jit_mode_probe` 验证的是参数解析，不是加速。任何文档或表述都**不得**称其为 JIT 加速。

---

## 4. 路线图

状态口径见 §1。`[x]` = 已验证；`[~]` = 部分实现；`[ ]` = 设计未实现。

### 阶段一：本底宇宙与单机参考实现

**已完成**

- [x] 词法、语法、字节码编译器、寄存器 VM、GC 与模组加载器。
- [x] 动态全局表、命名空间、分片锁、原子操作、task/Fiber 调度。
- [x] 实体系统、空间索引、精灵索引、SPI 事件总线。
- [x] `isolate_run` 未签名模组隔离与 per-mod 资源记账。
- [x] 字符串所有权审计、函数帧恢复、selfhost 大文件回归。
- [x] `--lint`、AI 代码检查和基础 Ollama 接口。
- [x] `verse_dist`、`record`、`sync` 等协议基础模块。

**稳定性治理（未完成项）**

- [ ] 发布稳定 ABI v1，并为模组声明接口版本。
- [ ] 建立弃用周期、迁移工具和 LTS 分支策略。
- [x] 将所有固定盘符、调试路径和个人数据从发布构建中移除。
- [ ] 完成 Windows、Linux、WebAssembly 构建矩阵。
- [x] 将 VM 时钟、休眠、目录和可执行文件定位迁移到 `src/platform/` 抽象层。
- [ ] 将 Fiber、线程、锁和进程能力拆分为 POSIX/Windows 后端，并为不可用能力提供明确错误。
- [ ] 将 `net_mod` / `server_mod` 的 TCP、HTTP 和 WebSocket 后端迁移到 `ImSocket`。
- [ ] 将 `net_mod` / `server_mod` 的平台专用注册逻辑改为能力检测并提供 POSIX 实现。
- [x] 在 `desugar_mod` 增加 `unless` 条件语法糖的规范化转换。
- [ ] 将 `isolate_mod` 的输出捕获、超时和资源限制迁移到 `ImProcess` 管道后端。
- [ ] 统一使用 `src/platform/features.h` 的能力声明，禁止模块直接散落平台宏判断。

> 进展：平台时钟/休眠已接入 VM 兼容层；跨平台互斥锁接口已建立，VM 现有 `CRITICAL_SECTION` 调用仍待逐步替换；原生 Verse 线程和调度器线程的启动、join/close 已接入 `src/platform/thread.h`，取消与超时强制终止语义仍待迁移。Fiber 调用已接入 `src/platform/fiber.h`（Windows Fiber / POSIX `ucontext`）。主程序自身路径解析已迁移到 `im_platform_executable_path`。VM 全局/分片/消息/脚本锁已迁移到 `ImMutex` 平台接口，Windows 构建回归通过。`child_proc` 注册表锁和计时已迁移；进程创建/终止后端仍待 POSIX 实现。`src/platform/im_process.h` 已提供跨平台进程 API，并有 `process_probe` 验证程序。`child_proc` 已迁移到 `ImProcess`，下一步处理 `isolate_mod` 的输出捕获与超时。`server_mod` 已移除固定盘符，路径默认随可执行文件目录推导并支持环境变量覆盖；房间目录创建/删除已脱离 Win32 文件调用，目录枚举仍待平台目录迭代器。

**阶段出口条件**：ABI v1 有版本协商和兼容性测试；至少一个旧版模组可在新运行时加载；三平台构建在干净环境可复现。

### 阶段二：多人在线与跨服务器互联（当前阶段）

**UPP：宇宙进程协议**

- [x] 定义宿主、Verse 子进程、客户端之间的握手和能力协商（`tools/upp_reference.js`）。
- [x] 实现心跳、启动、停止、日志、崩溃和版本不兼容消息（`tools/upp_reference.js`）。
- [x] 为每个 Verse 生成 manifest、入口脚本、接口需求和文件 SHA-256 摘要（`node tools/upp_reference.js --generate <dir>`）。

**CRP：宇宙中继协议**

- [x] 阶段 2A：`FIND`、`PORTAL`、`SIGNAL` 消息的本地参考协议（`tools/crp_reference.js`）。
- [x] 阶段 2B：HTTP 中继参考节点、Verse 注册/发现/连接/信号和 SHA-256 内容寻址（`tools/crp_relay.js`）。
- [x] 阶段 2C：客户端指数退避、取消和断线重连基础（`tools/crp_client.js`）；好友图谱和 NAT 穿透待接入。
- [x] 阶段 2D：客户端多源下载、包 SHA-256 校验和 HMAC 能力令牌（`CrpClient.fetchContent`、`crp_relay`）。

**客户端验收**

- [ ] 单机、热联机、冷联机三种模式均可从桌面 UI 启动。
- [ ] `verse://hub/<id>` 可发现、下载、校验并启动 Verse。
- [ ] 节点状态、延迟、版本和错误原因在客户端可见。

**阶段出口条件**：UPP/CRP 本地回环通过；签名篡改、版本不兼容和断线重连均有自动化测试；桌面端可从 UI 完成一次端到端 Verse 启动。

### 阶段三：Verse Forge 与宇宙分发

- [ ] Verse 配置模型：拓扑、坐标、时间流速、物理常数和传送协议。
- [ ] Avatar、生态、社会权限、视觉叙事和 NPC 配置面板。
- [ ] 蓝图导入/导出：地形、结构、资源、生态和传送门种子。
- [ ] 生成 `.vverse` 包：`manifest.json`、`laws/`、`blueprint.json`、`assets/`、`mods/`、`signatures/`。
- [ ] 打包签名、哈希、版本兼容检查和只读预览。
- [ ] Hub 清单、上传、下载、分叉与本地缓存。
- [ ] 工作台支持创建、打开、运行、分享和下载 Verse。

**阶段出口条件**：同一 `.vverse` 包在 Windows 与 Linux 解包结果一致；只读预览无需执行脚本；签名、哈希和依赖缺失均能给出可操作错误。

### 阶段四：AI 居民与训练沙盒

- [ ] AI 居民 API：人格、记忆、行为树、日程、对话和声誉。
- [ ] 强制 AI 标识与权限边界，避免将 AI 伪装成人类玩家。
- [ ] AI 资源配额：时间、指令、网络、记忆和模型调用预算。
- [ ] 隔离训练 Verse：快进、回放、评估、重置和迁移。
- [ ] AI 镜像玩家的行为审核、可解释日志和一键停止。
- [ ] 将 `ai_code`、`ai_code_check` 与 Verse Forge 配置流程打通。

**阶段出口条件**：AI 居民始终带有可见标识；资源配额可强制执行；训练 Verse 可暂停、回放、重置并安全迁移，且审计日志可导出。

### 阶段五：经济、资产与数字主权

- [ ] 本我之核：跨 Verse 成就、特异点、声誉和可迁移身份声明。
- [ ] 资产溯源：发行、转移、兑换、版本和冲突记录。
- [ ] `store:server` / `store:both` 的 CAS 冲突解决和审计接口。
- [ ] 研究 `store:chain` 的可插拔账本接口，不绑定单一链或货币。
- [ ] 公开信用证明查询，不提供中心化价值评级。

**阶段出口条件**：身份和资产声明可导出、校验和迁移；冲突解决过程可审计；实现不绑定单一链、货币或中心化评分机构。

### 桌面应用交付线（`Infiverse_standard/`）

> 该子系统最后一次提交是 `f90d355 Release v0.3.0`；`src-tauri/` 是 Tauri（Rust + WebView2）壳。是否继续维护见 §6 决策 2。

- [x] Tauri 八模块壳、现代化 UI、相对路径和发布安装包。
- [x] 主页资料编辑、二维码、成就概览和本地身份。
- [x] 工作台编辑、保存、运行、参数面板、新建项目和 AI 建议。
- [x] 引擎发现、多版本选择、更新通道、组件状态、包管理和 Repair 自检基础。
- [x] 插件搜索、安装、启停和移除。
- [ ] GitHub/Bilibili `code → token → profile → oauth_bind` 完整链路。
- [~] 内置 IDE：查找替换与错误行定位已完成；语法高亮、运行停止未完成。
- [ ] Verse Forge 桌面入口和 `.vverse` 管理器。

### 多样化 `say` 输出与流路由（全部未实现）

目标：让脚本根据场景选择文本的接收对象，而不是把所有文本都写入标准输出。语法保持 `say` 兼容，输出目标通过可选通道参数或上下文默认值决定。

**输出目标**

- [ ] `say.console(text)`：终端/REPL，保留当前默认行为。
- [ ] `say.log(text, level)`：结构化日志流（debug/info/warn/error），支持时间、模块和 Verse ID。
- [ ] `say.chat(text, channel)`：聊天/群组/AI 对话频道，支持权限和消息回执。
- [ ] `say.ui(text, region)`：桌面 UI 或 Web UI 指定区域，支持富文本和生命周期。
- [ ] `say.world(text, scope)`：Verse 内广播、区域广播、玩家私聊和事件流。
- [ ] `say.character(text, character, audience)`：游戏角色/NPC 对白，携带角色身份、情绪、动作和受众范围。
- [ ] `say.dialogue(character, text, options)`：可分支对白，支持选项、意图、上下文和回执。
- [ ] `say.system(text, severity)`：系统提示、任务提示和不可伪装的安全告警。
- [ ] `say.network(text, peer)`：UPP/CRP 节点消息，默认受能力令牌和速率限制保护。
- [ ] `say.file(text, path)`：显式文件流，使用沙箱路径和轮转策略。
- [ ] `say.json(value, stream)`：机器可读 JSONL，供 IDE、AI 和自动化工具消费。

**AI 分析输出**

- [ ] `say.ai(event, context)`：向 AI 分析流发送结构化事件，不默认显示给玩家。
- [ ] `say.ai_observe(scene, actors, state)`：提交场景快照，供 AI 分析角色关系、风险和下一步行动。
- [ ] `say.ai_trace(action, result, cost)`：记录 AI 行为、结果、资源消耗和因果链。
- [ ] `say.ai_feedback(label, reward, reason)`：向训练沙盒提交反馈，支持强化学习评估。
- [ ] AI 输出必须标记 `source=ai`，并携带模型、会话、Verse、时间戳和置信度元数据。
- [ ] AI 分析流与玩家可见流分离；只有经过权限过滤的摘要才能路由到 `say.chat` 或 `say.ui`。
- [ ] 禁止将隐藏思维链直接写入玩家流；仅保留可审计的结论、证据引用和安全事件。

**统一流模型**

- [ ] 定义 `OutputStream` 抽象：目标、格式、优先级、缓冲、背压、取消和错误策略。
- [ ] 支持同步输出与异步事件：`say_start` / `say_chunk` / `say_done` / `say_error`。
- [ ] 流路由表可由宿主、Verse 或客户端覆盖，但安全模式禁止越权目标。
- [ ] 断线时可配置丢弃、缓存或回放；网络流不得阻塞 VM 主线程。
- [ ] 所有输出记录来源（模块、线程、Verse、身份）并可按权限脱敏。

**语法糖与去糖**

- [ ] `say "hello" -> say.console("hello")`（保持旧代码兼容）。
- [ ] `say@chat "hello" -> say.chat("hello", "current")`。
- [ ] `say@character("guard", "请止步", "nearby") -> say.character("请止步", "guard", "nearby")`。
- [ ] `say@ai(event) -> say.ai(event, "current")`。
- [ ] `say@ui("status", text) -> say.ui(text, "status")`。
- [ ] `say@log.warn text -> say.log(text, "warn")`。
- [ ] 所有新语法只在 `desugar_mod` 实现，规范 AST/字节码不增加平台耦合。

**验收标准**

- [ ] 同一脚本可在 CLI、桌面 UI、WebSocket 客户端和 headless 服务端选择不同输出目标。
- [ ] 慢网络客户端不会阻塞 VM；超过配额时产生可观察的 `say_error`。
- [ ] `--safe` 下文件、网络和跨 Verse 输出必须显式声明能力。
- [ ] 输出流测试覆盖编码、顺序、取消、断线重连、背压和敏感信息脱敏。
- [ ] 角色对白测试覆盖身份伪造、受众范围、对白分支、字幕/语音同步和 AI 标识。
- [ ] AI 分析测试覆盖结构化事件、权限隔离、脱敏、可解释摘要和训练反馈回放。

### v0.6 目标：集合化类型系统（2026-10 由 v3.1 提前）

集合化类型系统原为 §5 P3 的**明确延期项**（→ v3.1，见 [archive/ROADMAP_3.1.md](archive/ROADMAP_3.1.md)）。2026-10 收到人类指令改为 **v0.6 目标**——指令原文、i.json 设计原文的逐字引语、以及「`type` 用集合规范化定义」的规范条文（R1–R6）见 **[TYPESET_V06.md](TYPESET_V06.md)**。

本节只登记**这件事的状态**，交付清单与验收标准不在这里重复：

- [x] 已有雏形（取证见 [TYPESET_V06.md](TYPESET_V06.md) §4.1）：`be` 约束绑定、内置集合／字面量／区间／推导式、`+`/`*`/`-`/`in` 均已实现并被 CTest 覆盖。
- [~] `type NAME = <集合>` 能解析能跑，但 `src/compiler/compiler.c:2224-2231` 把它编译成 `OP_STORE_GLOBAL`（**运行时全局值**），不是编译期类型。
- [ ] R2：`Z` 改为数学整数集（任意精度），`int`/`int32`/`int64` 降为「`Z` 的子集别名」。**这一条落地前，「`int` 是 32 位还是 64 位」在语言层无法回答**——今天 `Z` 的上界是 `src/vm/vm.h:42` 的 `union { long long ival; double fval; };`。
- [ ] R3：位宽由集合基数／范围派生；`@bits(n)`/`@bytes(n)` 显式钉住并编译期校验「位宽是否足够存储该集合的所有值」。
- [ ] 消去「同一语义多决定点」：`in`／子集／`be` 今天各有两份实现（C 内核 `src/types/` 与 VM `SetObj`），须与 [DECFY_DESIGN.md](DECFY_DESIGN.md) 用同一份决定点清单收敛。

**已裁定（2026-10，人类指令）**：

- **声明形状采纳 ① 与 ③**：`b be a : 0`（`i.json#267`，即仓库已实现的 `OP_BE` 形式）与 `b: a = 0`（`i.json#721` 的「`[@标签...] 变量名 : 类型 [= 初值]`」）。上一轮的 `b = 0 : a` 是笔误、已撤回。两者是不同的事（前者约束绑定、后者声明标注），同一个 `:` 的两个用途仍待处理。
- **`FloatN`／`floatN` 移出核心内置集合，改由拓展库提供**。影响清单（`file:line`）见 [TYPESET_V06.md](TYPESET_V06.md) §8；`docs/archive/ROADMAP_3.1.md` §非目标「v3.1 之前不删除」据此失效，但**该文件原文不改写**（§1 硬规则 6）。
- **集合运算用四则运算符**（`+` 并、`*` 交、`-` 差），不采用 `∪`／`∩`／`\`；**谓词可判定边界 = 简单计算 + 正则表达式**，SMT 求解器与形式化验证工具作拓展库接入、核心不依赖（内置已证结论表留在核心）。规范条文见 [TYPESET_V06.md](TYPESET_V06.md) §3.2–§3.3。
- **基数记法 = `count(A)`**；`#A` 不可行（`#` 已是注释引导符，`src/lexer/lexer.c:82`／`:83-92`）。但 `count` 今天不存在、`size` 存在，两者处置**未定**——见 [TYPESET_V06.md](TYPESET_V06.md) §3.2／§7.8。
- **声明形状唯一化为 `名字: 集合 [= 初值]`**（即 `b: a = 0`），取代同一天更早的「应为①③」；`be` 的退役代价（**19 个 `vtest/*.im`、42 行 `.im`、六处实现、`docs/SYNTAX.md` §6.3**）与退役节奏见 [TYPESET_V06.md](TYPESET_V06.md) §3.1／§7.8。
- **`@bits(n)` 后置**（§3.4）；**谓词集合复用 `|`**（§3.5）；**`Z` 的 BigInt = 立即数 + 溢出切堆对象**（§3.6）。

---

## 5. 优先级

### P0：诚实化收口（已完成）

- [x] 修正 `ROADMAP_0.5-0.6.md` 的 v0.6 虚假「已完成」标记 → §3.2 废止表。
- [x] 修正 `RELEASE_0.5.0.md` 的「至少 2x」及 SIMD/GC、Python/Java 桥接声称 → §3.1。
- [x] 裁定 `?.` / `??` 状态冲突：**已实现**（证据 `vtest/optional_member_v04.im`、`vtest/null_coalesce_v04.im`，CTest `optional_member_runtime` / `null_coalesce_runtime`）。
- [x] 清除指向 `/home/sakiko/inimerse_stable` 的陈旧 `build/` 树（该树中全部 CTest 报 `Failed to change working directory`）。
- [x] 清除 `Infiverse_standard/src-tauri/target/`（4.0 GB 构建产物，`.gitignore` 已忽略）。
- [x] 统一版本号来源，修正根 `README.md` 的失效链接。
- [x] 归档历史版本说明至 `docs/archive/`。

### P1：最小 Layer 闭环（已完成）

按白皮书 §77 的要求——**不再增加概念数量**——只做一个最小 Layer，验证八个动作：

- [x] ① 创建：`inim-server` 建立 `verse_id` + Layer 目录与 manifest。`src/verse/server.c` 的 `inim-server <root> [verse_id]` 幂等建层（已存在即 `VL_ERR_CONFLICT`→视为正常，**绝不覆盖 manifest**）。
- [x] ② 进入：`inim-client` 握手、能力协商、拒绝降级必须显式可见。`hello` 校验版本与能力：不支持的能力返回 `code:"capability_refused"` 并**点名该能力**，版本不符返回 `code:"protocol_mismatch"` 并给出双方版本；未握手前任何 op 返回 `code:"no_session"`。`src/verse/client.c` 是真正独立的进程（fork + 双管道，仅靠协议字节流通信）。
- [x] ③ 同步：客户端提交意图，服务端权威裁决；客户端**结构性**不能提交最终位置、资产余额或事件序号。两条独立防线：协议层 `VL_AUTHORITY_FIELDS = {"seq","rev","head","balance","state_hash","committed"}` 任一出现即 `code:"client_authority"` 拒绝（不是静默忽略）；内核层 `seq`/`rev` 由 `vl_layer_put`/`vl_layer_undo` 分配，客户端自报不符即 `VL_ERR_REJECTED`（**写入任何字节之前**）。
- [x] ④ 排空：断开前把队列排空或明确丢弃，不得留下半提交状态。`{"op":"drain"}` 是持久化屏障：flush 日志 → 写快照 → 复验锚点，返回 `drained`/`pending`/`seq`/`head`；不一致时 `drained:false` 并带 `code`。`bye` 隐式排空并把结果回给客户端；服务端进程退出前再查一次锚点，不通过则退出码 2。
- [x] ⑤ 恢复：崩溃后从事件日志 + 快照恢复，恢复失败必须进 `RECOVERY_REQUIRED` 而不是返回成功。`vl_layer_replay`、`vl_layer_snapshot_save/load`、`vl_layer_anchor_check`；闭环测试用 `#crash` 指令 `SIGKILL` 服务端进程，随后新进程重开同一 root 并复现同一 `head`。
- [x] ⑥ 撤销：幂等键 + 逆操作，重复请求不得二次生效。`vl_layer_undo` 在写入前校验目标存在（否则 `VL_ERR_NOT_FOUND`，不写日志，避免一条无法应用的记录永久污染后续重放）；同一幂等键重复提交不追加记录。
- [x] ⑦ 回放：确定性重放并校验哈希一致。两个全新进程对同一 root 的 `status` 必须给出逐字节相同的 `head`；重放前先查锚点，不通过直接 `VL_ERR_RECOVERY_REQUIRED`。
- [x] ⑧ 单一闭环测试：一个脚本覆盖以上七步，进 CTest。`tools/verse_closed_loop.test.py` = `verse_closed_loop`，67 项检查，全部经**真实子进程**（不用进程内链接），覆盖 create/enter/sync/drain/undo/replay/recover/tamper。

**已完成的三个增量**（2026-10-01，CTest 79 → 83 项全过；此后 `ed25519_probe` 与 `economy_migration_regression` 各加 1 项 → **85 项，全部真通过**，无记账项）：

| 增量 | 文件 | 验证用例 |
| --- | --- | --- |
| 1 事件日志与提交契约 | `src/verse/eventlog.h`、`src/verse/eventlog.c` | `verse_eventlog_probe`（canonical JSON 键序无关、哈希稳定、第 6 步失败不得推进序号且步骤 7–10 不执行、第 7 步失败进 `RECOVERY_REQUIRED` 且不返回 committed、陈旧 head 报 conflict、篡改后 `recover` 报 `RECOVERY_REQUIRED`、幂等键不二次追加） |
| 2 权威 Layer | `src/verse/layer.h`、`src/verse/layer.c`、`src/verse/json_min.h`、`src/verse/json_min.c` | `verse_layer_probe`（创建/重复创建冲突、服务端分配 seq/rev、幂等重试不追加、撤销可重复且目标不存在时**写入前**拒绝、重放复现同一哈希、快照恢复状态不回退日志、篡改由 `commit.head` 锚点暴露） |
| 3 会话协议与进程边界 | `src/verse/protocol.h`、`src/verse/protocol.c`、`src/verse/server.c`、`src/verse/client.c` | `verse_protocol_probe`（49 项：未握手拒绝、未知能力点名拒绝、版本不符拒绝、6 个越权字段逐一拒绝且不推进序号、幂等重试、撤销幂等、排空屏障与 bye 隐式排空）；`verse_closed_loop`（67 项：跨真实进程的八步闭环 + 篡改检测） |

**双进程边界**：`inim-server <root> [verse_id]` 从 stdin 读 canonical-JSON 请求行、向 stdout 写恰好一行响应（每行 flush），诊断走 stderr，stdout 保持纯协议流；退出码 0=干净结束、2=收尾锚点不一致、3=无法打开/创建 Layer。`inim-client [--server <path>] <root> <verse_id> [scenario]` 用 `fork` + 双管道拉起服务端子进程（`#crash` 指令 `SIGKILL` 它）。`inim-client` 的子进程派生目前是 POSIX-only，非 POSIX 平台会**明说未实现**（退出码 4）而不是假装可用。

**规格之外的补充发现（必须记录，否则重放不可信）**：日志的哈希链是**自洽**的——就地改写一条记录后重新计算的链仍然自洽，因此**可被检测的前提是存在外部锚点**。为此 Layer 增加了一个持久提交指针 `verse/<verse_id>/commit.head`，在每次成功提交后写入；打开时用它与从磁盘重算的链比对，不一致即 `RECOVERY_REQUIRED`。`vl_layer_repair()` 可显式重锚，**绝不自动调用**。闭环测试里 `tamper` 场景专门验证这条：改写 `events.log` 后 `drain` 返回 `drained:false` + `code:"recovery_required"`。

**另一条实现期发现**：`canonical JSON` 的容器逗号状态必须把「刚写完键」与「容器为空」拆成两个标志（`vl_cjson_sep` 消费 `after_key[]` 再判 `first`）。用一个 `first` 会让两者互相破坏，输出 `{"a":,1}`；这个 bug 在第一轮探针里就被抓到。

**第三轮发现并已修的真实缺口**：增量 3 第一版只在 `drain` 里查锚点，`status` 没有任何锚点检查。实测把 `events.log` 里的 `"value":2` 改成 `"value":9` 后，`status` 会把这个被篡改的值**当作已提交状态返回给客户端**，只有 `drain` 报 `recovery_required`。已收紧为：锚点不一致时 `hello` 直接 `recovery_required` 且**不授予会话**（在没人能担保的状态之上发 `seq` 会让客户端在坏状态上继续提交），`status` 同样拒绝。同时明确了会话规则——`put`/`undo` 必须先握手，`drain`/`status` **不需要会话**（Layer 因锚点不一致而拒绝 `hello` 时，运维恰恰需要它们）。

**其中必须先固化的契约**（来自白皮书 §79/§80/§93/§94）：

- 14 类事件与 `state_hash = SHA-256(canonical_json(...))`。
- 提交顺序 10 步：第 6 步 durable flush 失败时**不得**执行第 7–10 步；第 7 步失败必须进 `RECOVERY_REQUIRED`，**不得**返回 `committed`。
- 客户端提交的能力/角色/actor 一律不可信（`max_depth = 0`）。
- Tick T8 未完成持久化前不得标记 `committed`。

### P2：对齐白皮书 MVP（§78–§101）

CLI 退出码（9 个，`unknown` 不得退出 0）· 互操作剖面 T0–T10 · `EventLog` 的 `conditional_append` / `durable_flush` · `canonical_json/1` 的 13 类编码错误 · CI 门与 L0–L10 测试层。

### P3：明确延期，不要投入

- ~~集合化类型系统（⚠ 集合化：`Z`/`Z+` 集合关系与 `be` 语法）→ v3.1，见 [archive/ROADMAP_3.1.md](archive/ROADMAP_3.1.md)。~~ **2026-10 由人裁定提前为 v0.6 目标**，见 §4「v0.6 目标：集合化类型系统」与 [TYPESET_V06.md](TYPESET_V06.md)。本行保留为历史裁定（它记录的是**当时为什么决定不做**），**不再是「不要投入」的依据**。
- 概率编程、证明携带代码、量子启发叠加 → 见 [archive/ROADMAP_FRONTIER.md](archive/ROADMAP_FRONTIER.md)。
- 微内核 / 裸机 Inim OS、去中心化权威 → 见 `future/`。

---

## 6. 决策记录

| # | 问题 | 决策 | 理由 |
| --- | --- | --- | --- |
| 1 | 本次交付边界 | **只做 P1 最小 Layer 闭环**，不扩散到资产经济、去中心化节点或裸机路线 | 白皮书 §77 明确要求；P0 已收口 |
| 2 | `Infiverse_standard` 桌面端去留 | **冻结在 v0.3.0**，不新建功能；仅在 P1 闭环需要端到端演示时才触碰 | 它最后一次提交是 v0.3.0，且当前无引擎对接；同时维护会分散 P1 |
| 3 | 白皮书 §28–§101 的归宿 | **保持 `future/` 研究态**，不拆成 `docs/infiverse.protocol.v1/` RFC | 协议尚未实现；先有 P1 的可运行证据再上升为标准（§1.2 规则 4） |

---

## 7. 跨版本原则

1. **协议和 ABI 优先于实现。** 新增字段必须向后兼容，每个版本提供迁移指南和可重复测试夹具。
2. **实验特性通过显式开关启用。** 不破坏默认解释器路径，失败要有明确降级和错误信息。
3. **性能目标以基准数据为依据。** 不以未经验证的理论倍数作承诺；`docs/archive/SELFHOST_BENCHMARK.md` 是唯一性能事实来源。
4. **跨平台运行时 API 对等。** 核心内建与 Windows/POSIX 后端分层，通用函数两端注册、返回值与错误类型一致。
5. **规范先于实现。** 规范文档与测试向量同步发布，外部实现可通过互操作测试。

---

## 8. 验收基线

- 引擎：`contract_test.im`、`ai_syntax_test.im`、`syntax_simple_test.im`、`node_task_state_test.im`、`string_ownership_test.im`。
- selfhost：`compiler.im --dump lexer.im`、`eval.im`、`parser.im`。**更正（2026-10-02）**：`compiler.im` 自 `2026-09-06` 起一直无法解析（后缀 `if`/`unless` 跨行贪婪匹配，由 `a21915b`/`fe65f6d` 引入；见 BOARD 行 `selfhost-parser-postfix-ambiguity`，已修复并进 CTest）。修复后它能解析、能运行，但**其代码生成对任何程序只发出一条 `OP_HALT`（空程序）**，尚无任何目标能与宿主路径产出相同的运行结果 —— 见 BOARD 行 `selfhost-codegen-empty`。故本行列举的三个入口目前只证明「能载入」，**不证明自举可用**。
- 协议：UPP/CRP 本地回环、签名篡改、断线重连和版本不兼容测试。
- 桌面：`node --check src/ui/app.js`、`cargo check --offline`、`cargo build --release --offline`。
- **发布门禁**：干净环境下 `ctest` 必须 100% 通过；`python3 tools/selfhost_bench.py` 相对上一份报告中位数劣化超过 20% 时不得宣称发布。
- **门禁必须可重现（2026-10-01 教训）**：一次通过不算通过。历史上「间歇性失败、失败项每次不同、单独重跑必过」被误当成测试基础设施抖动，实际是 `ed25519_sign` 有 1/32 概率产出无效签名（§2.4）。此后凡遇到不确定性失败，**先怀疑产品代码**；对一个疑似 flaky 的测试，连续重跑（本轮取 12 轮）是判定它的标准动作，不得以「重跑就过」结案。

---

## 9. 下一步

### 9.1 §43.5 迁移导入的决定：**重构（复用已有的 `json_min`），不就地修补、不删除** —— 已执行完毕

用户要求先做「重构 vs 修复」的成本比较再决定（m01286 第 2 项），随后选定 `partial_slice` 语义并要求落地（m01677）。**本节的决定已实施，实施记录见文末「执行结果」。**

**诊断：缺陷不在那四条，而在检证方式。** 现有导入校验用 `strstr` 在序列化文本里找 `"hash":"` 串（`src/platform/http_posix.c:1549-1573`）。§2.3 的四条缺陷全都是**同一个根因的实例**：用文本搜索代替结构化解析，于是「找到最后一个 hash 字符串」被当成了「重算整条链」。就地修补意味着**再写一个手搓的 JSON 数组/对象扫描器**——那正是产生这四条缺陷的做法。

| 方案 | 改动量 | 结果 | 风险 |
| --- | --- | --- | --- |
| **A. 就地修补**（继续 `strstr`） | ≈60 行，且必须手写数组/对象/转义扫描 | 只是把第 1 条缺陷换成一个更长的版本；空白、键序、转义、嵌套任一变化都会重新引入同类缺陷（第 4 条就是这么来的） | **高** |
| **B. 重构到真正的解析器**（推荐） | 替换 1549–1573 的校验块 ≈50 行 + 从 `balances[]` 重算 `balances_hash` ≈20 行 + 4 处 CMake 目标各加 1 个源文件 + `+19`→`+20` | **消除整类缺陷**；第 3、4 条顺带消失（解析器自带空白容忍与纯整数金额） | 低 |
| **C. 删除在制品** | 删 218 行 + 删测试 | 保住空白，但不推进功能 | 无（但放弃已写好的导出侧与签名/内容哈希闸门） |

**决定：B。** 理由：
1. **解析器已经写好且已被单测证明** —— `src/verse/json_min.{h,c}`（272 + 39 行）是 P1 增量中为 Layer 写的，`vj_parse` / `vj_get` 已由 `verse_layer_probe`、`verse_eventlog_probe` 覆盖；本次不需要从零造。
2. **兼容性实测通过**：`json_min` 只接受整数（遇到 `.`/`e`/`E` 报 `non-integer number unsupported`），而经济域的全部数值都是整数——`amount` 是 `long long`（`src/platform/http_posix.c:132/142/174`）、余额 `%lld`、`seq` 为 `%llu`；导出侧本就只产整数。深度上限 32 对包结构足够，数组/对象用 `realloc` 动态增长无元素数上限，导入请求缓冲 `req[65536]`（`src/platform/http_posix.c:755`）远大于包体。
3. **正确的公式同文件已有**，不需重新推导：审计处理器 `src/platform/http_posix.c:1378-1391` 就是 `sha256(prev ‖ "seq|kind|currency_id|from|to|amount|idem")` 并同时比对 `hash` 与 `prev`；重构只是把这段从「遍历内存里的 `g_econ_events`」改成「遍历解析出来的 `ledger` 数组」。
4. **验收门禁已经存在且已编码正确契约**：`tools/economy_migration.test.py`（586 行 / 39 项检查，自带独立的 `balances_digest()` 与 `links_ok()` 参考实现）。重构后它应先去掉 `WILL_FAIL TRUE`，以「33 通过 / 5 失败 → 39 全过」作为完成判据。**这正是 B 比 A 便宜的地方：测试已经替我们写好了验收标准，A 与 B 的工作量相当，但只有 B 能把它跑绿。**

**关于切片起点**：保留账本是跨币种单条全局链（`g_econ_tail` 全局），币种级导出的 `prev` 可能落在包外。这是**语义问题**，不是解析问题——重构不会自动解决它。必须显式决定：要么导入时把「包内首条 `prev` ≠ `0`」记为 `partial_slice` 并如实回显，要么要求导出携带该币种自创世起的完整切片。**不要把它当成解析器的副产物。**

**执行结果（2026-10-01，已完成）**

用户选定 `partial_slice` 语义（包内首条 `prev != "0"` 即置位，并**由包自身字节推导、不采信声明值**），重构已落地：

| 改动 | 位置 |
| --- | --- |
| 引入结构化解析器（`#include "../verse/json_min.h"`） | `src/platform/http_posix.c` 顶部 |
| 抽出共享摘要核心 `econ_digest_lines(EconBalLine*, int, char[65])` —— 本地快照与导入包用**同一算法** | `src/platform/http_posix.c`（`econ_balances_digest` 就地重构） |
| 新增 `econ_entry_hash` / `EconReplay` + `econ_replay_slot` / `econ_pkg_balances_digest` / `econ_snapshot_is_replay` / `econ_verify_package` / `econ_record_snapshot` | `src/platform/http_posix.c`（`econ_ledger_digest` 之后） |
| `ImImportedLedger` 增加 `int partial_slice;` | 结构体定义 |
| `if (econ_import)` 整块重写：body 提取 → `vj_parse` → ①`content_hash` 复算 ②`definition_hash == sha256_hex(def)`（**新增检查**）③`http_node_verify(issuer, def, sig)` ④`econ_verify_package` → 登记（`conflict` / `already_present` / `imported` / 507 满）；`imported` 与 `already_present` 均回显 `"partial_slice"` | `src/platform/http_posix.c`（原 1740–1842 行） |
| `+19` → `+20` | `src/platform/http_posix.c`（`GET /economy/domain/`） |
| `src/verse/json_min.c` 加入 4 处目标 | `CMakeLists.txt:127`(`http_probe`) / `:130`(`hub_probe`) / `:133`(`websocket_probe`) / `:240`（主 `inimerse` POSIX 分支）★ **本行是【记录】**：实测今天这三处在 `CMakeLists.txt` 第 214 / 218 / 222 行 —— 这个过期不是本次改动造成的，那三行早就上移了；按内容找、不按号。 |
| 去掉 `WILL_FAIL TRUE` 与 `known-defect` 标签 | `CMakeLists.txt:179` |

**验收**：`python3 tools/economy_migration.test.py` → `economy migration: ok`（**39/39**）；干净重建 0 error；`ctest --test-dir build -j4` = **`100% tests passed, 0 tests failed out of 85`**。

**两条实施教训（都已修，值得记住）**：

1. **`req` 含 HTTP 请求行与头部**，`vj_parse(req, …)` 从偏移 0 解析必然失败（症状：全部导入返回 400 `malformed`，hub stderr 报 `malformed package (unexpected character at offset 0)`）。必须先 `strstr(req, "\r\n\r\n") + 4` 取 body。
2. **相邻性检查不能过严**。保留账本是单一全局链，币种切片**合法地跳过别的币种的事件**——实测 alpha 的 seq 为 1,2,**5**（beta 的 `mint-b`/`pay-b1` 占了 3,4），`ledger[2].prev` 指向 beta 的 tail `eca42172…` 而非 `ledger[1].hash` `2cc18fae…`。正确规则：`seq <= prev_seq` 报 `"seq does not increase"`，**仅当 `seq == prev_seq + 1` 才要求 `eprev == prev`**。这样既保留可证伪的强校验，又不误伤交错切片。

**诊断陷阱（本次踩坑）**：`tools/economy_migration.test.py` 的 `start_hub` 用新 `open(log,"w")`，而每个篡改用例都重启 hub ⇒ 日志互相覆盖，只留最后几个，容易误判成败。要看全部诊断需 `stdbuf -e0` 或让 stderr 继承父进程（`stderr=None`）。

### 9.2 已结项

1. **`ed25519_sign` 长消息静默截断（原第 2 项待决）→ 已修**。见 §2.4：新增流式 `sha512_init/update/final`，20 个边界长度全部通过参考实现验证；`ed25519_probe` 同时升级为四组守卫（RFC 8032 KAT + SHA-512 KAT + 长消息 KAT + 600 轮 fuzz），耗时由 29.7 s 降到 3.4 s。
2. **仓库卫生**：`codex-reconnect-fix/`（1008K，嵌套的无关克隆）已按第 4 项删除。
3. **P0 文档诚实化收口**、**P1 最小 Layer 闭环八步**、**3 项开放问题裁决**：均已完成（见 §5）。
4. **DSH harness 桥（`tools/dsh-inimerse/`）**：把引擎接进 agent 会话的 Cordis 插件，五个工具（`inim_status` / `inim_build` / `inim_test` / `inim_run` / `inim_verse`）全部通过真实二进制工作，不复制任何引擎逻辑。验证：离线 43/43、`--live` 55/55（连跑两次幂等）、`--live --build` 58/58；已以 `application: applied` 装入 web profile，并用插件自身的工具复核：`inim_test` = 85/85 通过、`inim_verse` 往返（put seq 1 → undo seq 2 → drain 锚点一致）、`inim_run` 内联脚本执行。说明见 `tools/dsh-inimerse/README.md`，索引见 `tools/README.md` §3。
5. **§43.5 迁移导入重构（方案 B + `partial_slice` 语义）→ 已完成**。见 §2.3 与 §9.1：`/economy/import` 改用 `src/verse/json_min.{h,c}` 结构化解析并逐条重算链哈希，`+19`→`+20`，`WILL_FAIL TRUE` / `known-defect` 标记已删除；`tools/economy_migration.test.py` **39/39 通过**，CTest **85/85 真通过**。
6. **dsh-m 上架：已发布 npm 包，PR 待开**。见 `tools/dsh-inimerse/marketplace/README.md`：`package.json` 已按上架要求补全（`repository.directory`、`publishConfig`、`license` 由 `BSD-3-Clause` 更正为 **MIT** 以匹配仓库 `LICENSE`）；`npm pack` 产出 8 文件 / 14.9 kB tarball，`shasum 9038702626b5501c2908f743f31ccd6fef047aa5`。**`dsh-inimerse@0.1.0` 已于 npmjs 发布成功**（`npm view dsh-inimerse version` → `0.1.0`，`license` → `MIT`，`dist.shasum` 与本地 pack 一致）。dsh-m 的 v1 schema 只接受 `source: npm|github`，GitHub 源会把**仓库根**当成包（本仓库根是引擎不是插件），故只有 npm 可行。PR 需改上游**两个**文件：`registry.json` 追加条目 + `tests/registry.test.mjs` 的 `plugins.length` `23`→`24`；分支 `add-dsh-inimerse`（commit `67a16c9`，`git diff f18fc81` 纯追加 19 行）已在 `/home/sakiko/inimerse/.dshm-pr` 备好，本地复现上游 CI 全绿（**`registry 校验通过`**，24 条条目全部通过，含 `dsh-inimerse` 的 npm/GitHub/homepage 三项），已推送到 fork `infileap/dsh-m`，**PR [iasiv5/dsh-m#1](https://github.com/iasiv5/dsh-m/pull/1) 已开**。其 `Registry check` 运行停在 `action_required` 且**零 job 执行**——这是 GitHub 对首次贡献者的审批闸门，需维护者点 *Approve and run*，不是失败。发布产物已从 registry 重新下载比对：shasum 与本地 pack 一致，8 个文件全部与 `tools/dsh-inimerse/` 逐字节相同。

   发布侧的坑（已解决，留作前车之鉴）：Classic token 已废弃，Granular token **必须勾 Bypass 2FA** 且权限选 `Read and write (publish and stage)`（`stage only` 只能 `npm stage publish`）；本机 `npm login` 必崩（npm 12.0.2 web 流程轮询 `/-/v1/done` 30 秒后 404 并掉进 couch 通道，非交互 TTY 下报 `Exit handler never called!`）。

**路线图上的下一步**：

1. ~~UPP 本地参考协议与 Verse manifest。~~ **引擎侧已实现**（§10.1）；剩余边界是 `timestamp` 只收整数、以及尚未接入 `inim-server`/`inim-client` 传输层。
2. CRP `FIND` / `PORTAL` 回环和签名校验。~~**仍未实现**~~ **今天不成立：§10.6 那句成立**。四项分开核过（§10.100）：`FIND`、`PORTAL`、`SIGNAL`、签名校验都在**跨进程**的真实交换里跑通（闭回环 31 次交换 0 失败、`crp_probe` 185 checks / 0 failures、`crp_engine_crosscheck` 115 records text-identical —— 本流把解析边角语料并进来后是 128 条，见 §10.102），且四个负控件逐个 403（篡改签名得 `invalid capability token`、错密钥铸的 token、别人的 token、替另一对签的 proof）。原句是 §10.6 落地**之前**的陈述，没有被一起改掉 —— 两句话同时挂在本文档里是这次裁决的对象。**这条边界已在本流收掉**：`PORTAL` 的签发现在先过一张 per-`(verse, peer)` 准入表，未登记的 pair 与证明错的 pair 拿到的响应逐字节相同（§10.102）。`/friends`、`/content`、`/package` 三个端点**今天都在**：`src/platform/http_posix.c` 的路由里有 GET/POST `/friends`、`GET /content/`、`GET /package/`、`POST /package/fork` 与 `GET /packages`（原先这里记的是「三端点仍未实现」，已按代码更新），见 [BOARD.md](BOARD.md) 的 `crp-in-engine` 行。
3. ~~`.vverse` 打包、预览、下载与启动。~~ **引擎侧打包器已实现**（§10.2），`.im` 侧入口也已通：`verse_pack` builtin 在 `src/mod/verse_dist_mod.c`，由 `src/lobby_online_src.im` 调用，参考 CLI `node tools/vverse_pack.js preview|unpack` 能把同一份包读回来；原先记的「剩余只是 `.im`/CLI 入口（`vverse-cli`）」已按代码更新。~~与 hub 对 >64 KiB 包的静默截断（`hub-large-package`）~~ —— 截断已在 `578eb95` 修掉，今天复验（§10.100）：66560 字节的包在 `/v`、`/package`、`/content`、`POST /package/fork` 四条出口上**要么完整送达、要么显式 413**，而恰好 65536 字节仍 `200` 全量（边界是 `size > cap`，不是 `>=`）。
4. GitHub/Bilibili OAuth token 交换和资料绑定。**分两半，只有一半成立**（§10.100 末节只做评估、不写代码）：GitHub 侧**已是端到端**并经用户实机验证（`linked_accounts.json` 重写、`access_token` 40 位、`GET api.github.com/user` → `200 login=infileap`，见 §10.34/§10.36）；Bilibili 侧**只有代码级端点**（authorize 与 token endpoint 都写了，`device_code_endpoint()` 对 bilibili 返回 `None`），**没有注册过的应用、没有一次真机往返**，`linked_accounts.json` 的读取侧也未端到端验 —— 前置依赖在仓库之外，不能自己拍板。
5. Verse Forge 第一批时空/物理/蓝图面板。

### 9.3 多智能体协调桥接层（`设计未实现`）

设计文档 [`future/multi-agent-coordination-bridge.md`](../future/multi-agent-coordination-bridge.md)（624 行，2026-10-02 复核更新，作业单 [streams/agent-bridge.md](streams/agent-bridge.md)）：在既有的 CRP（能力路由 / 授权平面）、Verse Layer（历史与提交）、UPP（打包与校验）之上，定义智能体之间如何**声明意图、达成承诺、留下可审计的证据**。它**不新增第五套机制**，只做三层映射：G²CP 的 performative 叠在 CRP 的 `SIGNAL` 上、承诺状态落成 Verse Layer 的一个 `commitment:<id>` cell、身份与生命周期沿用 UPP 的监督。

- 状态：**`设计未实现` / E1** —— 只有设计，没有实现。判定依据：`grep -rn "commitment" src/ --include=*.c --include=*.h -i` 零命中（rc=1），`grep -rniE "\bagent" src/` 同样零命中。
- 代码改动：**零**。本项不需要 `CMakeLists.txt`，也不需要动 `src/**`。
- 挂靠点：§4 阶段四「AI 居民与训练沙盒」的 `强制 AI 标识与权限边界，避免将 AI 伪装成人类玩家。`（阶段出口条件见该节末）。
- 已知前提（**本流之后不再是缺口**）：CRP `PORTAL` 的签发侧过去只有**调用方认证**、没有 per-`(verse, peer)` **授权**（§7.7，`crp-portal-auth` `40f6094` 之后）。本流（`portal-per-pair`）把证明输入换成 hub 持有的一张表：每个 `(verse, peer)` 一行、一行一把 secret，未登记的 pair 连「这个 verse 存不存在」都问不出来（§10.102）。`UPDATE = PORTAL + SIGNAL` 的映射因此第一次有了可查的准入，而不是形式上的。
- 主要未决项：智能体的身份没有已验证的载体（文档 §7.9）；`REJECT` 落账的 cell 语义；§7.1 的 64 事件环不落盘。
- 可重复的验收命令（验证的是**底座**；桥接层自身没有可运行的验收，这正是 `设计未实现` 的含义）：

  ```bash
  ctest --test-dir build -R "crp|upp|json_min|verse_" --output-on-failure   # 0 tests failed out of 16
  ./build/verse_crp_probe                                                   # crp_probe: 185 checks, 0 failures
  node tools/crp_engine_crosscheck.js ./build/verse_crp_probe                # 115 records, text-identical
  grep -rn "commitment" src/ --include=*.c --include=*.h -i                  # 无输出，rc=1
  ```

---

## 10. 四条并行流的交付与边界（2026-08 集成）

> `.worktrees/` 下四条并行流的产出，由协调者在 `integration/streams-2026-08` 上合并、
> 串行跑 `tools/gate.sh` 后进 `main`。**合并没有冲突** —— `upp-in-engine` 与
> `vverse-produce` 都改了 `CMakeLists.txt`，但落在不同区段。
> 分工：[BOARD.md](BOARD.md) §5 记「谁在做」，本节记「做完了什么、边界在哪」。
>
> 一个教训：两条流各自只看到**自己分支上**的 ctest 数 87，合并后是 **89**（85 + 2 + 2）。
> 这正是 [BOARD.md](BOARD.md) §3 那句「门禁数字变了就必须同时改这张表和 STATUS.md §1」
> 要防的事 —— 单人视角的验收数字天然偏低。

### 10.1 UPP 引擎侧（`upp-in-engine`）

**交付**：`src/verse/upp.{h,c}`、`src/verse/upp_probe.c`、`tools/upp_engine_crosscheck.js`，`CMakeLists.txt` +15。

线上格式逐条照抄 `tools/upp_reference.js`：JSONL 一行一帧、`{upp:1,type,id?,payload}`、`id` 为空时**整键删除**、1 MiB 上限按 UTF-8 **字节**计、manifest 五字符串校验（`id` 正则 / `version` semver / `abi` 正整数 / `abiRange` 为 `N` 或 `N..M`）、hello/welcome ABI 协商（同角色拒绝、range 不交报 `incompatible ABI ranges: A vs B`、远端无 role 时 welcome **省略 `peerRole` 键**）。状态机：start 在 running **幂等**、从 crashed/incompatible 起报 `cannot start from %s`、stop 无条件、crash 的 error 缺失回落 `unknown crash`、recover 只许 crashed/stopped、心跳 `seq`/`timestamp` 非递减且**相等放行**、`check_heartbeat` 只在 running 判定、`timeoutMs=15000` 且严格 `>`。

**证据**：`verse_upp_probe` **213 条断言**；`verse_upp_crosscheck` —— 111 个 op 的语料（用参考实现**自己的构造器**造帧）同进程喂 JS 参考、同时写成 JSONL 交给引擎，两边逐行**文本**比对，`upp crosscheck: ok (111 ops, engine and reference agree)`；ASan+UBSan 下断言模式与全语料模式均干净。

**没做什么**：
- `timestamp` **只支持整数**。它依赖 `src/verse/json_min.c`，而该解析器遇 `.`/`e`/`E` 直接报 `non-integer number unsupported`，于是对端带小数的 `Date.now()` 会让**整行帧被拒**。没改 `json_min.c`：它同时被 layer/eventlog/protocol 的 canonical JSON 与 `state_hash` 复用，改数字语法有破坏事件日志契约的风险，应作为独立增量连同回归一起做。
- **UPP 尚未接入任何传输层**。交付的是库 + 探针；`inim-server`/`inim-client` 没有 `--upp` 开关。判据「引擎跑完全序列」是在**进程内**由探针跑完的，跨进程集成属 `crp-in-engine` 范畴。
- 两处错误文本与参考/V8 不一致（非字符串的 truthy `payload.error`；malformed JSON 的**语法**错误措辞），**已在对照语料里避开** —— 即这两类没有被对照覆盖。

### 10.2 `.vverse` 引擎侧打包器（`vverse-produce`）

**交付**：`src/common/gzip.{h,c}`、`src/common/vverse_pack.{h,c}`、`src/common/vverse_pack_probe.c`、`tools/vverse_cross.test.py`，`CMakeLists.txt` +19。

写侧做**确定性 gzip**（10 字节头 MTIME=0 / XFL=0 / OS=3，stored DEFLATE 块 ≤65535/块 + CRC32/ISIZE），读侧必须能解 zlib level-6 的 dynamic Huffman。打包是树内容的**纯函数** —— 不往源目录写任何东西（参考实现的 `pack()` 会写源树，交叉测试里显式断言了这个差异）。

**证据**：`vverse_pack_probe` **37 项**（同目录连续两次打包 sha256 相同 → `4e798ef7376a6ed342fbd7fa5e5775908e6181ec906a01e7e213306f8e3c62e3`；篡改四类全按预期报错；`../escaped.txt` 恶意包被拒且外部无文件落地；200000 / 9000 字节逐字节往返；ed25519 载荷 19195 字节 > 8192 证明走流式 `Sha512Ctx`）。`tools/vverse_cross.test.py` **47 项双向**交叉验证：引擎包 → `vverse_pack.js unpack/preview` ＋ `vverse_validate.js --strict --require-signature --require-complete-signature --require-public-signature` 全绿（即 node `crypto.verify` 认可引擎产出的 DER SPKI）；解压后的 JSON 体与参考实现**逐字节相同**；node 用自生成密钥 + zlib(dynamic) 打的包 → 引擎 unpack/validate 通过；放进 `INIMERSE_HUB_DIR` 后 `GET /v/<id>` 逐字节取回。

**没做什么**：
- **没有 `.im` / CLI 入口** —— 现在只是 `src/common/` 的库 + 探针驱动器（`build/vverse_pack_probe --pack <dir> <out>`）。所以「引擎能自己产出 `.vverse`」严格说只做到**库**一级。已立 `vverse-cli` 行。
- **写侧不做真压缩**：产出是 stored DEFLATE，包体约等于源内容的 1.33×，比参考实现的 zlib level-6 大。这是为「逐字节确定性」付的价，不是漏做。
- 未做 inflate fuzz、几十 MB 超大包、Windows 路径分支。

### 10.3 仓库根清理（`repo-hygiene`）

**交付**：删 24 个 `CHANGES_*.txt`（740 行，`git rm`）＋主工作区的 `CMakeLists.txt.bak`（24881 B）。新增 `docs/HYGIENE.md`（357 行）＝第 2 批分类方案。

`CMakeLists.txt.bak` 被 `.gitignore:32` 的 `*.bak` 忽略且未跟踪，**worktree 检出里根本不存在**，只能在主工作区删 —— 这**违反了 [BOARD.md](BOARD.md) §1「一个工作区一个会话」的字面要求**，已记录在 HYGIENE.md §9（回滚副本在 `/tmp/CMakeLists.txt.bak.stash`，已核对它是现行 `CMakeLists.txt` 的旧子集，缺 P1 的全部段落）。

**口径更正（重要）**：板上原写「135 个零引用」是**少报**，真实候选集 **146**。两处漏报机制：① **自匹配** —— `grep -rqF "$f" .` 把文件自己内容里出现自己的名字算成引用，藏了 16 个（`block_edit.im`、`cpu8m.im`、`endless_busy.im`、`entity_stress.im`、`limit_test.im`、`ports.bat`、`smoke2.im`、`stress_cpu.im`、`stress_mem.im`、`test_room.im`、`textbox.im`、`timeout_test.im`、`ai_build.ps1`、`imai.ps1`、`endless_test.im`、`entity_stress2.im`）；② 作业单自己点名 8 个候选，被扫描器当成「引用者」。分桶 **A 删 118 / B 迁出根 22 / C 留根 6**（互不交叠、并集 == 146，生成时程序校验），另有 30 个二阶孤儿。

**两处作业单前提已失效**（已写进 HYGIENE.md）：① `hl_bridge.c` **并不被 `CMakeLists.txt` 引用**（零命中、也无 GLOB 兜底），真实关系是**反向**的 —— 它 `#include` 那 6 个 `*_embed.h`，自己作为源码被 `src/mod/server_mod.c:61` 拼进运行期 exe 路径；它照样要留，但**理由不同**。② `CMakeLists.txt` 里的 41 个 `.im` 全在 `vtest/` 下，根目录 167 个 `.im` **没有一个**出现在 `CMakeLists.txt` 中 ⇒ 「根目录 `.im`」那条判据选出**空集**。另确认仓库无 GLOB / `*.im` 通配消费风险。

**没做什么**：第 1 批**没跑构建/ctest**（只删 `.txt`，不进构建 —— 但「未验证」是事实）；桶 A「已被 `vtest/` 覆盖」是按特性名与文件头**自述**推断的，没把 28 个 `*_test.im` 的断言与 41 个入册用例逐条对照 —— 这是整个方案里**最大的判断风险**。

**第 2 批已执行：只做桶 B，只搬不删（`6b56af1`，2026-08）**。口径定为「迁移」而非「清理」：**唯一验收点是迁移前后逐文件 blob 哈希相同**，用来证明这是搬家而不是删除。结果 **30/30 逐字节相同**，模式全为 `100644`，`git diff --diff-filter=D` 为空，`git` 报 30 个 `R100` —— 没有出现「删一个、加一个」的假重命名。核对表固化在 [HYGIENE.md](HYGIENE.md) §10.2（不是只写在交接报告里）。仓库根文件数 **228 → 198**。

去处：`examples/{scripts 5, regressions 7, legacy-ui 8, bench 2+7, assets 1}`；新增 6 个 `examples/**/README.md` 说明每批文件的来历，`README.md` 的 Layout 补一行 `examples/`。**桶 A 的 118 个一个都没删，桶 C 6 个仍在根**。

**唯一一处搬运之外的源码改写**（已报备，超出该流写入范围）：`entity_stress2.im:8` 的 `gui_sprite(..., "monster8.bmp")` 改成 `"examples/assets/monster8.bmp"`。理由：该字符串是**相对进程 cwd 的文件系统路径**（`src/mod/gui_mod.c` 原样交给 `LoadImageA`/WIC，不做资源解析），纹理搬走而脚本留在根，不改就会让一个仍在根的文件失效。代价：它属于桶 A，被改了内容（没被删），且**不在** §10.2 的 30 行哈希表内。

**第 2 批没解决的**（交接原文，未粉饰）：① `es_200/400/800/1600.im` 仍写裸名 `"monster8.bmp"`，**故意没改** —— 它们是本批被迁移的文件，改一个字节就破坏「内容逐字节相同」这个验收点；后果是这 4 个脚本只在进程 cwd 能看见该文件名时才找得到纹理，已写进 `examples/bench/README.md` 但**未修复**。② 复扫得的零引用数是 **128**，与 §2 的 138 / 146 对不上，**差额没有解释完**（已定位部分来源：`docs/STATUS.md` 叙述性提到 `ports.bat` 与 `smoke2.im`，`HYGIENE.md` 自身点名全部候选）。③ `examples/bench/*.ps1` 硬编码 Windows 路径，**没能真跑**，只有静态路径核对。④ 顺带测出 **`home_embed.h` 是过期快照**：`chat/desktop/netplay/wb.html` 与各自 header 除末尾换行外相同，`verse_forge.html` 只差生成器插入的空格，但 **`home.html`（5 537 B）比 `home_embed.h`（解码 3 900 B）新**，多出的标记只存在于 HTML；仓库里**没有任何生成器脚本**能重建这些 header。

### 10.4 文档口径复查（`docs-audit`）

**交付**：`docs/REQUIREMENTS_ANALYSIS.md` 失效路径改指实际位置；新增 `tools/check_doc_paths.py` 并接成门禁第 7 阶段 `doc-paths`；`README.md` 基线数字复现（实跑 `100% tests passed, 0 tests failed out of 85` 那时为真，见 §10.5 的新数）。

**关键更正**：失效路径实为 **23 条 / 46 处**，不是板上写的 22 条 —— ASCII 反引号 grep 匹配不到 CJK 文件名 `docs/archive/工作台使用教程.md`（它当时在 `docs/` 根，收敛时被归档）。所有引用改指 `docs/archive/…` 或仓库根 `AI_LAYOUT.md`，逐条 `test -e` 核对；只改**指向**，未动归档内容与结论。

**为什么 `check_links.py` 看不见这类问题**：它必须先剥离行内代码（不剥离的话 `` `object["name"](...)` `` 会产生 15 个假阳性），而这些路径恰好写在反引号里 ⇒ **检查器全绿不等于路径都对**。这就是本次新增第 7 阶段的原因。新检查器扫根 `README.md` + `docs/*.md` + `future/*.md` 共 10 份，**故意不扫 `docs/archive/` 与 `future/archive/`**（归档件引用旧路径是历史事实）；日后在别处新增 `.md` 需手动加进文件表。

**一条被驳回的怀疑（协调者裁定）**：`docs/STATUS.md` §4 的 `**已完成**` 标题曾被怀疑违反 §1.2 规则 2（无维度表述），因为同阶段下面还列着 8 个 `[ ]`。**裁定：不是违规。** 那个粗体标签只辖它**紧邻的下一个列表组**（那一组的条目全部是 `[x]`），未完成项在自己**显式命名**的 `**稳定性治理（未完成项）**` 标题下。文档没有把未做的事说成已完成。

### 10.5 合并后的门禁（协调者串行执行）

```bash
tools/gate.sh          # 九个阶段，串行；不要并发跑，§2.9 的端口窗口会假失败
```

见 [BOARD.md](BOARD.md) §3 的阶段表。合并后的基线数字：**ctest 93 / 93**、economy **39 / 39**、plugin **55 / 55**、node **11 / 11**、links 与 doc-paths 均 **0 broken**。

### 10.6 CRP 的引擎侧线层（`crp-in-engine`，`7b20505`）

**先更正板子上原先那句话**：`docs/BOARD.md` 曾写「CRP … 引擎侧无实现」，**那是错的**。引擎**早已**有会话层 —— `src/platform/crp_session.{h,c}`（220 行）＋ `crp_session_probe.c`：11 个状态（含 §55.6 的 `DEGRADED`/`DISCONNECTED_GRACE`/`REATTACHING`/`RESUMED`/`READ_ONLY`/`EXPIRED`）、版本与能力协商、30 秒租约、`accept()` 去重与缺口判定、`resume_plan()` 重放 vs 快照。缺的是它**外面**那一层，而那一层当时只活在 `tools/crp_relay.js` 里。派活前先更正了那一行，并写成 [streams/crp-in-engine.md](streams/crp-in-engine.md)。

**交付**：`src/verse/crp.h`(215) / `crp.c`(1257)、`crp_hub.c`(387)、`crp_peer.c`(696)、`crp_probe.c`(1181)、`tools/crp_closed_loop.test.py`(123)、`tools/crp_engine_crosscheck.js`(394)，`CMakeLists.txt` +36。8 文件 4289 行，全部落在该流写入范围内；**没有重写** `crp_session.*`。

**证据**：
- `tools/crp_relay.test.js` 的 23 条断言中 **12 条覆盖**（register / find / portal.peer / expires 落在 `(now, now+ttl]` / resume seq4→lastSeq4 / resume 回退→409 / signal 202 / 借他人 token→403 / revoke / revoke 后 403 / 吊销集封顶 / 未知 verse→404）。**剩下 11 条按边界不做**，全部落在本流明令不做的三个端点（`/friends` 2 条、`/content` 3 条、`/package` 6 条）—— **这不是「我的测试过了」，是那三个端点根本没实现**。
- **真实两进程闭回环**：`build/crp-hub` ↔ `build/crp-peer`，**27 次交换 0 失败**，driver 17 项检查 0 失败，状态码直方图 200×13 / 400×3 / 403×4 / 404×3 / 409×1 / 202×3，27 条 `>>` 对 27 条 `<<`，hub tail `bye:true`。hub 把临时端口作为 stdout 第一行 announce 出来、driver **从不猜端口** —— 这是对 §2.9 端口竞争的主动规避。
- **crosscheck 逐行文本比对**：**101 条语料 text-identical**，同一份语料既走引擎、也走 `crp_reference.js` 与真实 `crp_relay.js`。关键机制：在 `require` 参考实现之前**冻结 `Date.now`**，否则令牌 `exp` / `expires` / TTL 剪枝会被墙钟漂移污染、根本不可比。首跑出现 8 条不一致、归为 4 类真问题，全部修掉。
- **ASan + UBSan**（`ASAN_OPTIONS=detect_leaks=1`）：探针 **173 项检查 0 失败**，转写回放路径也单独过了一遍，无 sanitizer 输出。
- `ctest` **89 → 92**（新增 `verse_crp_probe` / `verse_crp_closed_loop` / `verse_crp_crosscheck`）。

**四条与参考实现的真实分歧**（都保留在引擎里，**没有为了对齐而放宽任何校验**）：
1. **`/status` 是引擎独有的**，参考 relay 没有。后果具体：`sessions` 计数引擎是 3 而参考只会是 2 —— 引擎在 PORTAL 签发时就绑了一个会话。因此这条**不可跨实现比对**，被排除在 crosscheck 语料之外。
2. **base64url 解码比 Node 严**：引擎拒绝 `n%4==1`、非 alphabet 字节、`=` 填充、尾部非零 bit，而 Node 的 `Buffer.from(x,'base64url')` 会宽容接受其中一部分。分歧方向是「更严」，不会把坏输入当好输入。
3. **JSON 语法错误文本无法一致**：引擎给 `bad literal at offset 0`，V8 给 `Unexpected token …`。引擎不嵌 V8。**契约级错误文本全部逐字一致**（`message must be an object`、`invalid CRP frame`、`unsupported CRP type: %s`、`CRP frame exceeds 1 MiB`、`FIND limit must be 1..1000` 等均已核对）；语料全是合法 JSON，所以这条分歧**未被触发**。
4. **会话层语义是引擎独有的**：`im_crp_session_accept()` 的缺口判定、租约、11 个状态，参考实现都没有对应物。
5. **两个 `\u` 解码分歧，由 `json-min-nul-escape` 流引入并有意保留**（精确 input/output 对见 §10.7）：
   - **`\u0000` 被引擎拒绝**：`{"x":"x\u0000y"}` ⇒ 引擎 `ok:false`、`\u0000 is not representable at offset 13`、**不写任何字节**；`JSON.parse` 则给出内嵌 NUL（`payload.x` 字节 `78 00 79`）。**这不是能力不足，是刻意的**：`VjVal` 的字符串是无长度字段的 `char *s`，收下 NUL 就等于静默截断，而**引擎没有任何写出器能产出 `\u0000`**（七个 `\u%04x` 写出点循环条件全是 `for (p = …; *p; p++)`，裸 NUL 会先终止循环，最小可产出转义是 `\u0001`）⇒ 拒绝它**不构成对任何引擎可生成输入的收窄**。
   - **孤立代理映射为 U+FFFD**：`{"x":"\uD83D"}` ⇒ 引擎输出 `EF BF BD`；`JSON.parse` 保留未配对代理，`JSON.stringify` 把它重新转义成**六个 ASCII 字符** `\ud83d`。**字节不同、解码后的值也不同**（U+FFFD vs 未配对代理码元），不是同一个值的两种拼法。**被迫而非可修**：以 UTF-8 字节为字符串模型就装不下未配对代理。
   - **这两条都无法进 crosscheck**：`tools/crp_reference.js` 走 `JSON.parse`，它**两者都接受** ⇒ 语料里放进去只会得到「不一致」或需要归一化（那等于把 crosscheck 唯一要断言的「逐字节一致」放宽掉）。因此**精确字节钉在 `src/verse/json_min_probe.c`，不放宽比较器**。`tools/crp_engine_crosscheck.js` 里只有**字面** `\\u0000`（六个字符、无 NUL 字节）的条目 —— 那不是转义，是普通文本。

**没做什么 / 已知边界**：
- **一个显式取舍，代价已写明**：`session_store_seq()`（`src/verse/crp.c:912-916`）会调 `im_crp_session_accept()` 取 §55.6 判决，但只把它记进 `/status` 的 `acceptVerdict`，**最终写进 `last_applied` 的是参考实现更宽松的规则**（无条件 `last_applied = seq`）。**代价是引擎自带的缺口判定不影响线上行为，只作诊断暴露。** 选它的理由：判据要求对齐参考实现的线上行为。若日后要求「缺口即拒」，必须先明确「对齐参考实现」与「执行 §55.6」哪个优先 —— 在本流范围内二者不可兼得。
- 参考实现的测试配置 `{ttlMs:1000, tokenTtlMs:1000, maxRevokedTokens:1}` **没有等价复现**：`crp-hub` 只走默认值（token TTL 5 min / registry TTL 30 min / 吊销集封顶 10000）。等价语义用**不同数值**单独验过（registry 剪枝、lease 过期、`max_revoked=2` 时封顶生效），但**那组具体数值没跑过**。
- **base64url 与 HMAC-SHA256 直接放在 `src/verse/crp.{h,c}`**，没有进 `src/common/`（当时只有 CRP 一个消费者）。日后若 `.vverse` 或别的模块也要 base64url，需要再抽公共实现 —— 那时必须同时保住 `src/common/vverse_pack.c:169-215` 的**严格** padding 语义。
- 顺带发现 `src/verse/json_min.c` 一条**继承的既有缺陷**，本流**没修**（避免动到现有语义）：`json_min.c:95` 的 `if (cp < 0x80) out[len++] = (char)cp;` 在 `\u0000` 时会往字符串里塞一个**裸 NUL**，而 `VjVal` 的字符串是**没有长度字段的 `char *s`** ⇒ 所有基于 `strlen` 的下游消费者都会看到被截断的字符串。已立为 BOARD §5 的 `json-min-nul-escape`，**并已由该流修复** —— 见 §10.7。

### 10.7 `json_min` 的 `\u` 解码（`json-min-nul-escape`，`cef77f5`）

**立项**：§10.6 顺带发现的那条缺陷。派活前的侦察把它从「一条 BOARD 行」扩成了**跨四个模块**的真实缺陷面：`grep` 得 `src/` 下 **63 处 `vj_str(` 调用点**，消费者包括 `src/verse/crp.c`、`crp_peer.c`、`upp.c`、`layer.c`、`src/common/vverse_pack.c`，以及 **`src/platform/http_posix.c:1760`（解析来自网络的经济域导入包）**。`struct VjVal`（`src/verse/json_min.h:15-25`）的字符串字段是 `char *s;`，**无长度字段**。

**修前实测**（Node `JSON.parse` 与引擎 `vj_parse` 逐字节对照）：

| 输入 | Node | 引擎（修前） |
| --- | --- | --- |
| `"\uD83D\uDE00"` | `F0 9F 98 80`(4) | `ED A0 BD ED B8 80`(6) —— **CESU-8，非法 UTF-8** |
| `"\uD83D"` | `EF BF BD` | `ED A0 BD` |
| `"\uDE00"` | `EF BF BD` | `ED B8 80` |
| `"x\u0000y"` | `78 00 79`(3) | `78`(1) —— **静默截断** |
| `"alice\u0000A"` / `"alice\u0000B"` | 两个不同串 | 都变 `alice` —— **两个不同 JSON 串塌缩成同一个 C 串** |

线上形式是六个 ASCII 字节 `\`,`u`,`0`,`0`,`0`,`0`（**不含裸 NUL**），能穿过 HTTP 头部剥离与所有 `strstr`/`strlen` 检查到达解析器。根因两处：`json_min.c:95` 写裸 NUL；`json_min.c:96-97` 把任何 ≥0x80 的码点编成至多 3 字节，于是代理对两半各自成 3 字节。

**下游后果比 BOARD 原话更重**（`src/platform/http_posix.c`）：`econ_replay_slot(EconReplay *r, const char *account)` 用 `if (!strcmp(r->account[i], account)) return i;`，`econ_pkg_balances_digest(const VjVal *pkg, char out[65])` 用 `snprintf(items[n].account, …, "%s", acct)` ⇒ **只差 NUL 之后内容的两个账户会被合并进同一个余额槽位**，不只是摘要相同。

**修法**（不采用给 `VjVal` 加长度字段：63 个调用点连锁，且它不解决 CESU-8）：①代理对合成 4 字节 UTF-8；②孤立代理输出 U+FFFD；③`\u0000` **显式拒绝**；④对象**键**走同一个 `vj_parse_string_raw`，一并修。

**交付**：`src/verse/json_min.c`(103 行改动)、新 `src/verse/json_min_probe.c`(+262，76 checks，**链进 `src/verse/upp.c`** 使出厂的 `upp_json_write_string` 参与往返测试)、`CMakeLists.txt`(+10，`add_test(NAME verse_json_min_probe … LABELS "protocol;json")`)、`tools/crp_engine_crosscheck.js`、`tools/gate.sh`（`EXP_CTEST` 92→93）。

**证据**：
- **fails-before-fix**：`git stash push -- src/verse/json_min.c` 后探针 `74 checks, 20 failures`、exit 1；`stash pop` 后 `76 checks, 0 failures`。
- **ASan + UBSan**（`ASAN_OPTIONS=detect_leaks=1`）：干净；贴边用例的堆缓冲恰为 `strlen+1`，任何越读都会被 ASan 抓到。
- **crosscheck 语料 101 → 107 条 text-identical**（新增代理对 / 键位代理对 / 连续两对 / `\u0001` / `\u00e9\u4e2d` / 字面 `\\u0000`；**去掉**两条孤立代理行）。
- **`P->p[0..3]` 前读（`json_min.c:88`）**：队友在 ASan+UBSan 下、每个输入装在恰为 `strlen+1` 的堆缓冲里，证得 `"\"\\u004"` / `"\"\\u0041"` / `"\"\\uD83D"` **无报告** —— 循环里第一个非法读就是 NUL 本身且在界内（`p[3]` 是终止符，`p[4]` 永不触及）⇒ **旧代码只对零余量的 NUL 结尾调用方安全，且纯属侥幸**。新增 `vj_read_hex4()` 开头的 `if (h == '\0') { vj_fail(P, "bad \\u escape"); return -1; }` 让读**可证有界**，且**不改变任何既有错误文本**。
- 最终门禁（我在合并前于该 worktree 亲跑 `rm -rf build && tools/gate.sh --jobs 4`）：**七阶段全 PASS、ctest 93/93、`gate: OK`、exit 0**。

**一次被驳回的交付（值得记的教训）**：队友首版为让孤立代理语料「通过」，在 `tools/crp_engine_crosscheck.js` 里加了 `canonicalRecord(line)`，把未配对代理归一成 U+FFFD 后再比。**这等于把 crosscheck 唯一要断言的「逐字节一致」放宽掉**，且注释里「the same bytes on the wire」**事实错误**（`\ud83d` 是六个 ASCII 字符、`EF BF BD` 是三个字节）。驳回并要求：删掉整个归一化函数、删掉孤立代理语料条目、把字节断言留在探针里，然后**证明去掉归一化后仍 text-identical 且门禁仍绿**。**我复核时补做了一次反向验证**：把被删掉的那条孤立代理行重新塞回一份临时副本，crosscheck 立刻报 `MISMATCH line 41` / `1 of 108 records differ` / exit 1 ⇒ **比较器是真的能看见这个分歧，而不是「不再喂它」了**。教训：**当测试开始需要归一化才能通过时，被放宽的往往正是它存在的理由。**

### 10.8 门禁 `links` 阶段的封闭性（`gate-hermeticity`）

**缺陷（实测，修复前）**：`tools/check_links.py:57-74` 的 `iter_markdown_files()` **当时**用 `os.walk(REPO_ROOT)` 遍历**工作区**，只靠 `SKIP_DIRS`/`SKIP_PREFIXES` 黑名单挡杂物。另一个会话在仓库根留下的暂存目录 `.verify/`（未跟踪）里有一份引用 GitHub 社区健康文件（`SECURITY.md`/`GOVERNANCE.md`/`MAINTAINERS.md`）的 README，于是本仓库的门禁被判红：

| 树 | 检查器 | `git ls-files '*.md' '*.markdown'` |
| --- | --- | --- |
| 主工作区（含 `.verify/`） | **86 files / 319 links / 12 broken**，exit 1 | 78 |
| 同一棵树的 worktree 去掉 `.verify/` | 78 files / 284 links / 0 broken | 78 |

**为什么这不是「多漏了一个目录」**：门禁的职责是回答「**这个仓库**能不能合」。工作区里别人丢下的文件与这个答案无关，却能让答案变成「不能」。更贵的是它会**训练人不信任门禁**——红了先问「这次是不是又是杂物」，那一刻门禁就废了。**任何黑名单都永远漏一个目录**（这次是 `.verify/`，下次是别的名字），所以修法不是加名单。

**修法**：`iter_markdown_files()` 改为 `git ls-files -z -- '*.md' '*.markdown'`（`cwd=REPO_ROOT`）。被检查的集合**就是仓库的内容**，按定义不漏不多。`SKIP_DIRS`/`SKIP_PREFIXES`/`SKIP_FILES` 三个常量随之删除（已无用）。git 不可用时 `sys.exit` 并打印清楚错误，**不提供 `os.walk` 回退**——回退会把本条流刚修掉的缺陷原样带回来，且只在 git 不在时复现。

**顺带修掉的同族缺陷**：作业单判 `tools/check_doc_paths.py` 为「封闭」，实测**不成立**（已被写域条款授权修改）。它的 `scan_files()` 走 `os.listdir`，因此在 `docs/` 直接丢一个**未跟踪** `.md` 同样能让它变红：`12 markdown files, 1 broken`，exit 1。修法是**过滤**而非换路径（关键：git 的 `docs/*.md` 路径式**会**匹配 `docs/archive/*.md`，实测 51 vs 非递归 6；直接换会把本检查器故意不判的归档件拉进来），即保留 `os.listdir` 决定「本检查器拥有哪些文档」的原语义，再用 git 跟踪集过滤。对已跟踪文件判定**逐字不变**（11 files / 152 refs / 0 broken）。

**反向验证（最重要的判据，两个检查器都做了）**：在一个**已跟踪**文件里插入指向不存在文件的相对引用，检查器必须**仍然能红**——否则 `0 broken` 只意味着「什么都没扫」。

```
BROKEN  docs/streams/vverse-produce.md  ->  ./__REVERSE_VERIFICATION_PROBE__.md  (does not exist)

check_links: 78 markdown files, 285 links (7 external, 0 anchors, 278 local), 1 broken
exit=1
```
同一次运行里还放着未跟踪的 `scratch-xyz/README.md`（内含指向 `NOPE.md` 的断链）：**只点名了已跟踪的那条，未跟踪的那条看不见**；`files` 仍是 78。`check_doc_paths.py` 的同类验证（探针插在 `docs/API.md` 内，同时另放一个未跟踪的 `docs/` 断链文件）输出：

```
BROKEN  docs/API.md  ->  docs/__DOC_PATHS_REVERSE_PROBE__.md  (no such file)

check_doc_paths: 11 markdown files, 153 backtick refs, 1 broken
exit=1
```

两处探针都已还原，`sha256` 与改前逐字节相同、`git diff --exit-code` 干净。

**交付**：`tools/check_links.py`（`iter_markdown_files` 重写 + `subprocess` 导入，净 +62/−23）、`tools/check_doc_paths.py`（新增 `tracked_paths()` 并在 `scan_files()` 里过滤）、`.gitignore`（`universe/_cache/` 与 `.verify/` 两行——**这是卫生措施，不是本缺陷的修复**）。

**判据**：①文件数守恒 `--json` 的 `files=78` == `git ls-files '*.md' '*.markdown' | wc -l` = 78；②`.verify/` **仍物理存在**的前提下检查器报 `78 files / 0 broken`、`grep .verify` 命中 0 行；③反向验证如上；④`scratch-xyz/README.md`（未跟踪）不被看见；⑤`bash tools/gate.sh --fast --only links` 与 `--only doc-paths` 均 PASS。

**没有做的事**：没碰 `tools/gate.sh`/`CMakeLists.txt`/`tools/node_suites/run_all.js`（`httpfix` 持有）、`tools/crp_ws_client*`/`tools/upp_session*`（`wscoverage` 持有）、`src/**`、`docs/BOARD.md` 与本文件既有行；没有 `push`/`merge`；**没有改任何已跟踪文件的既有引用**——修前修后 `0 broken` 一致，说明本次没有靠「把红的说成绿的」来收敛。

**事后补记（协调者）：我把本节的反向验证输出抄进 `docs/BOARD.md` 时，自己在 `main` 上埋了一个红门禁。** 合入 `fa30036` 后，我把上面那段逐字输出抄进了 `docs/BOARD.md` 的 `gate-hermeticity` 行——**但抄成了行内 code span，不是围栏块**。`check_doc_paths.py` 只清空围栏代码块、**不清空行内 span**，于是两个并不存在的探针名被当成真引用：

```
BROKEN  docs/BOARD.md  ->  docs/__DOCPATHS_REVERSE_PROBE__.md  (no such file)
BROKEN  docs/BOARD.md  ->  docs/zzz-probe.md  (no such file)

check_doc_paths: 12 markdown files, 167 backtick refs, 2 broken
exit=1
```

即 `bash tools/gate.sh --fast --only doc-paths` → `✘ docs backtick paths (expect 0 broken) (exit 1)`、`gate: FAILED — do not merge.`（`7f9d7f4`）。

**教训（比缺陷本身重要）**：本节把探针输出放进围栏块是**对的**，错在我**把它抄到别处时丢掉了这个前提**。「检查器跳过围栏块」**不等于**「检查器跳过示例路径」——要贴检查器输出就贴进围栏块，或者干脆不要写成以 `.md` 结尾的 `docs/…` 形状。**也不允许**反过来通过删掉行内 span 通道来「修」：那个通道正是用来发现**正文里真断链**的，删了它，本节这条反向验证也就不成立了。修复方式是改 `docs/BOARD.md` 那一格的写法，**没有动任何检查器代码**。

**另一个必须记住的门禁后果（另一个会话实测）**：**新增文档必须先 `git add` 再跑门禁**。两个检查器都只枚举**已跟踪**文件，所以一份新写的 `.md` 在被 `git add` 之前对它们**不存在**——检查器会照样打印 `0 broken`，而这个「绿」是假的。实测：`git add` 前引用计数 161、之后 167。

### 10.9 WebSocket 客户端的连接 / 重连 / 排队（`ws-client-coverage`）

**立项**：`docs/BOARD.md` §5 原写「`tools/crp_ws_client.js` 只有 **1 条**断言，且没有任何文档或脚本引用它（`upp_session` 同样 0 引用）」。派活前实测，这句话**一半是错的**：`tools/regression.js:4` 的数组里**列了** `crp_ws_client.test.js`，`tools/upp_session` 也被 `tools/upp_engine_crosscheck.js:30` 直接 `require`。⇒ 真正的缺口是**行为没测**（连接、重连、排队三个机制一条断言都没有），外加 `crp_ws_client` 在 `docs/` 里没有任何出处。

**它是什么、在哪**：CRP（跨节点会话）的 WebSocket 客户端。23 行，`tools/crp_ws_client.js`，导出 `CrpWebSocketClient`；构造 `new CrpWebSocketClient(url, { retries = 3, backoffMs = 100, onMessage = () => {} })`，方法 `connect()` / `send(value)` / `close()`。三个机制：① **连接** —— `open` 兑现 `connect()`，`error` 按 `retries` 次、以 `backoffMs * 2**(n-1)` **指数退避**重试，预算耗尽则 reject；② **重连** —— 对端主动关闭时自动重连，但 `close()` 之后不再重连，且**旧 generation 的 `close` 事件不能顶掉新连接**；③ **排队** —— 未 OPEN 时 `send()` 入队且不碰底层 socket，`open` 后按入队顺序 flush，`close()` 清空队列。它**直接用全局 `WebSocket`**（`crp_ws_client.js:10`、`:12`），而仓库既没有 WebSocket 服务端实现、也没有 `ws` 依赖。

**怎么跑**（三条入口等价，都不需要网络）：

```bash
node tools/crp_ws_client.test.js        # 单跑本套件，31 条断言
node tools/node_suites/run_all.js       # JS 协议套件总入口（门禁 node 阶段跑的就是它）
bash tools/gate.sh --fast --only node   # 门禁第 6 阶段
```

测试**离线且确定性**：在 `require('./crp_ws_client')` **之前**把假 `WebSocket` 装进 `globalThis.WebSocket`（带 `OPEN` 常量与可手动触发的 `open`/`message`/`error`/`close`，并实现 `{once:true}`），跑完还原。**不引第三方 `ws`、不占端口、不依赖真实对端** —— 否则红了也分不清是「客户端回归」还是「对端慢」。

**证据**：`tools/crp_ws_client.test.js` 由 2 条断言（入队 / `close()` 清空）扩到 **31 条**（连接 7、重试 8、重连 8、排队 8），跑完打印 `CRP WebSocket client tests: ok (31 assertions)`。**非空证据 = 变异测试**，脚本已入库、可复跑：

```bash
python3 tools/crp_ws_client.mutation.py    # 默认 --old-ref d1bef61:tools/crp_ws_client.test.js
```

对 `crp_ws_client.js` 做 **29 个单行变异**（其中 4 个是为击穿「断言被前一条挡住」而写的定向变异，标注 `[targeted]`）。每个变异都在一个**唯一临时目录**（`tempfile.mkdtemp(prefix="crp-ws-mutation-")`，成功失败都删除）里跑**两套**套件 —— 旧套件（`d1bef61` 的 2 条断言，**只从显式 ref 读，绝不从 `HEAD` 读**）与新套件（31 条标签断言）—— 并逐条记录**第一个变红的断言**。实测两列（`old`/`new` 为退出码，1 = 该变异下该套件变红）：

```
mutation                                                   old  new
connect: drop the in-flight dedup                          0    1
connect: dial twice per connect()                          0    1
connect: dial a url the constructor was not given          0    1
connect: forget to record the dialing socket               0    1
connect: resolve with the socket, not the client           0    1
connect: the de-duplicated caller resolves to the socket   0    1
message: hand onMessage the event, not event.data          0    1
retry: swallow the error at exhaustion instead of rejecting 0    1
retry: retries + 1 attempts                                0    1
retry: no backoff before the first retry                   0    1
retry: fixed backoff instead of exponential                0    1
retry: retries 0 resolves instead of rejecting [targeted]  0    1
retry: clamp retries to at least 1                         0    1
retry: closed loop exits with the socket instead of the client 0    1
retry: dial once more after the loop decides to stop [targeted] 0    1
reconnect: never dial a replacement                        0    1
reconnect: clear the socket after scheduling the replacement [targeted] 0    1
reconnect: close() forgets to forget the socket            0    1
reconnect: drop the generation guard                       0    1
reconnect: drop the closed and generation guards           0    1
reconnect: a close event always drops the socket           0    1
queue: always queue, never write through                   0    1
queue: send() through a socket that is not OPEN            0    1
queue: pretty-print the JSON instead of compacting it      0    1
queue: flush LIFO (pop instead of shift)                   0    1
queue: flush without draining the queue                    0    1
queue: send AND queue on an OPEN socket                    0    1
queue: only queue when a socket already exists [targeted]  1    1
queue: close() leaves the queue behind                     1    1
```

⇒ **旧文件只看得见 29 个变异里的 2 个**（`queue: only queue when a socket already exists [targeted]`、`queue: close() leaves the queue behind`），其余 **27 个变异下它 exit 0 全绿**；**新套件 29/31 条断言各有至少一个能把它打红的变异**。剩 2 条是**结构性屏蔽**，不是空断言 —— #20「`close()` 之后重连拨的是新 socket」被 #18「`close()` 忘掉当前 socket」挡住（任何复用旧 socket 的写法都先违反 #18），#28「OPEN 时 `send()` 直接上线」被 #23「陈旧 `close` 之后活 socket 仍能发数据」挡住（同一条规则在更早的场景里已经断言过）。这正是「旧文件测不到连接 / 重连 / 排队」的机器证据。

**可复现性（脚本自己会守）**：第一版脚本读的是 `HEAD:tools/crp_ws_client.test.js` —— 在提交**之前** HEAD 恰好还是 `d1bef61`，所以表是对的；提交之后 HEAD 变成 269 行的新套件，两列会**一起变成新套件**、「旧文件看不见 27/29」当场不可复现（复核者在已提交的 worktree 里实测到了这个假象）。现在脚本把「旧文件必须是 6 行 / 2 条 `assert.` / 无 `  ok  ` 标签」写成**前置条件**，不满足就打印实际行数与标签数、**exit 2 拒绝运行**；另外还校验 `tools/crp_ws_client.js` 与 `d1bef61` 版本**逐字节相同**（本流承诺「未改模块」，可用 `--no-module-check` 跳过）、以及新套件自报的断言数与实际标签数一致。跑完打印 `RESULT: the documented kill map reproduces`、exit 0；与本文记录的数字不符则 exit 1 并指名是哪一条。

**没做什么 / 遗留**：

- **没有改 `tools/crp_ws_client.js`**。31 条断言在**未改动**的模块上全绿，没有发现真缺陷。
- 一条**观察（不是缺陷，未改）**：`this.closed` 与 `_generation` 冗余 —— `close()` 同时做 `closed = true` 与 `++_generation`，所以任何 `closed === true` 的时刻，活回调捕获的 generation 必然已经对不上。代价为零，留着当第二道闸也无害。
- **`closed` 守卫与 `_generation` 守卫无法各自单独隔离**：`close()` 一次动两个字段，所以「`close()` 之后不再重连」只能断言**可观察行为**；「旧 generation 的 `close` 不能顶掉新连接」可以单独归因给 generation 守卫（变异里去掉它、只留 `!this.closed`，该断言立刻变红）。
- 没测：`send()` 在 `close()` 之后仍会入队（且永不 flush）这一姿势；默认值 `retries: 3` / `backoffMs: 100` 只由构造签名与显式配置的 `retries: 0` / `retries: 2` 间接覆盖，没跑默认值下的 4 次重试（那条要等约 700ms，收益不值）。

### 10.10 超大包的静默截断与端口分配（`http-truncation-and-ports`，`578eb95`）

**立项（实测）**：`src/platform/http_posix.c:312 hub_body()` 用 `fread(body, 1, cap, vf)` 读文件，超过 `cap` 即截断，而**调用方仍然回 200**。四条路径各自的真实后果不同：

| 端点 | 原行为（实测） | 现行为 |
| --- | --- | --- |
| `GET /content/<hash>` | 回算 sha256 对不上 ⇒ **500 `content_corrupt`**（不是「截断」，是**把完好文件误报成损坏**） | 413 `content_too_large` |
| `GET /v/<id>` | 200 + 只回 65536 / 66560 字节 | 413 `package_too_large` |
| `GET /package/<id>` | 同上 | 413 `package_too_large` |
| `POST /package/fork` | **201 `{"size":65536}`，且盘上留下截断副本**（写路径丢数据，四条里最严重） | 413，**在 `write_atomic` 之前拒绝，不落盘** |

UDP 发送侧（`src/platform/http_posix.c:59`）另加了 `status == 200 && blen > 0 && blen < 60000` 闸 —— 因为 `src/mod/verse_dist_mod.c:2060 verse_udp_fetch` 会把**任何**数据报原样当包返回。

**修法**：新增 `static int read_file_capped(FILE *f, void *buf, size_t cap, size_t *out_len, unsigned long long *out_size)`（`src/platform/http_posix.c:328`），四处调用点改用它；超限即按端点回 413，不再静默。

**端口**：`verse_http_start` 原来拒绝 `port < 1`，所以 harness 只能**猜号**。现在 `--port 0` / `--http-port 0` 交给内核分配，并把**真实**端口报出来：`verse_http_bound_port()`（`src/platform/http_posix.c:2001`）、`headless_bound_port()`（`src/headless_server_posix.c:51`）、`src/main.c:1018-1039` 打印真实端口（`:1025` 的闸改为 `headless_http_port >= 0`）；`hub_udp_bind()` 在 `:2005`，`port == 0` 的循环在 `:2053-2062`（≤16 次尝试），`:2068-2069` 记录内核给的号，`:2095` stop 时清零。harness 侧：`tools/testports.py:160 distinct_ports`（重复定义已删）、`:261 start_hub_bound_ports(...)`、`:302` 只认正数端口；八处 hub 启动改用 `--port 0 --http-port 0`。

**证据（修复前必须失败）**：用**原始引擎**跑新套件 `tools/http_large_package.test.py`（30 checks）→ **exit 1，9 项失败**（上表四条路径 + `--port 0` → `headless: bind 0 failed`）；修后 → exit 0，30/30。

**端口竞态的前后对照**（`tools/ports_race_probe.py`，每格 1224 次启动）：

```
                        修前   对照（已修引擎但仍猜端口）   修后
失败 / 1224              5              6                0
```

**对照格是关键证据**：它证明引擎侧的修复本身**对竞态毫无作用** —— 竞态在 harness 的「猜号」里，不在引擎里。这也正是否决「就绪失败时用新端口重试」那条候选修法的实测依据（见 §2.9）。

**一个被固化的旧期望（值得单独记一笔）**：`tools/vverse_cross.test.py:415-428` 原先竟把截断缺陷**写成了期望**（注释 "truncated … measured, not required"）。现已断言 413 + 真实大小（48 checks）。这是「测试**记录**缺陷而不是**约束**缺陷」的典型：测试变绿并不说明行为正确，只说明行为没变。

**进树**：`CMakeLists.txt:230-231` 注册 `http_large_package_regression`（TIMEOUT 180，LABELS "protocol;regression"）；`tools/gate.sh:43` 的 `EXP_CTEST` 93→**94**（`add_test(` 共 94）；`tools/ports_race_probe.py` **故意不进 CTest** —— 它是测量工具，不是断言。

**没做的（边界）**：Windows 孪生 —— `src/headless_server.c` 没有 bound-port getter，故 `--port 0` / `--http-port 0` 目前仅 POSIX；`src/mod/verse_dist_mod.c:1972-1990` 的裸 `blen < 60000`；`b_verse_listen(0)`（`src/mod/verse_dist_mod.c:2096`）—— 脚本仍无法得知内核给的 listen 端口，这是**最后一个引擎还能吸收的猜端口**。

**环境事实（会影响别人写测试）**：本沙箱的 loopback 会**丢弃 >~1400 字节的 UDP 数据报**（用 python echo server 验证过），所以新测试的 UDP 探针用 900 字节包。

### 10.11 `.im` 打包内建写出的必须是真 `.vverse`（`vverse-cli`，`82a2789`）

**板上原话错在哪。** §5 原写「`vverse-cli`：无 `.im`/CLI 入口」。**这句是错的**：`src/mod/verse_dist_mod.c:2693` 早就注册了内建 `vm_register_builtin_full(vm, "verse_pack", b_verse_pack, 1|CAP_VERSE|CAP_NET, 0)`，`src/lobby_online_src.im:40` 也早就在用 `r1 = verse_pack(".", "universe/room.vverse")`；实测一条 3 行 `.im` 跑通并产出文件（exit 0）。**缺的不是入口，是格式。**

**真正的缺陷。** `b_verse_pack`（原 `:1109-1271`，163 行）**自己手工拼 JSON**：拼 `"files":{…}`、用 sha256 **hex**，写出的是**裸 JSON**（实测头 32 字节 `{"id":"src","version":"1.0.0","publisher":"e4610a1d…`）。真正的 `.vverse` 是 `gzip({"format":"vverse-1","files":{…base64…}})`（mtime 固定 0）。把内建产物喂给参考实现：`node tools/vverse_pack.js preview <pkg>` → `vverse pack: incorrect header check`，`unpack` 退出码 1 ⇒ **内建是遗留的第二种格式，与整条工具链不兼容**：能打包，但打出来的东西谁也读不了。

**修法（不写第二份打包器）。** `b_verse_pack` 改成 `vverse_pack(dir, out, NULL, err, sizeof err)` 的**薄适配层**（`src/mod/verse_dist_mod.c:1108-1146`），容器仍归 `src/common/vverse_pack.c` 所有 —— 该文件早已被 `tools/vverse_cross.test.py` 与 JS 参考实现**双向**交叉验证。为此把它与 `src/common/gzip.c` 加进引擎目标（此前只链进 `vverse_pack_probe`，见 `CMakeLists.txt:131-136`），并补 `src/platform`/`src/verse` 两个 include 目录（已核对无头文件重名）。

**顺带修掉的一个更深的断裂（队友发现，我确认）。** 换掉写侧之后，引擎**读不懂自己刚写的东西**：`verse_open("verse://local/…")` 走的是遗留 `verse_unpack`（`src/mod/verse_dist_mod.c:685`），不认 gzip ⇒ 打印 `[VDP] bad package json`、`open=0`。**当时没有任何测试覆盖它**（`src/lobby_online_src.im` 无人注册）。修法是**嗅探首字节**（`do_open` 里 `pkg_len >= 2 && 0x1f 0x8b` ⇒ 交给新的 `vverse_unpack_mem()`），遗留分支**逐字节未动**：两种容器不可能混淆（gzip 魔数 vs JSON 的 `{`）。同时把 `vverse_unpack()` 重构成 `file_read_all` + `vverse_unpack_mem()` 的薄包装 —— 引擎从**线上**取包与从磁盘取包一样常见，不该为此写临时文件。

**被改写的那份测试（值得单独记一笔）。** `tools/verse_pack.test.py` 原本把**遗留容器**钉死：`json.loads` 一个裸 JSON、断言 `publisher` 长 64 / `signature` 长 128、并对 `vtest_signed.vverse` 做**逐字节重生成**。写侧一换它就崩（`FileNotFoundError`）。处置：**步骤 5、6 逐字保留**（它们驱动 `verse_open` 读那个**已提交的**遗留向量，遗留**读侧**的回归因此不丢）；步骤 1–3 的篡改断言改为针对**真容器**（digest 表即签名，参考实现是裁判）；**步骤 7 退役**。`vtest_signed.vverse` 由此**改变角色而非失去覆盖** —— 它现在是**没有生产者的冻结遗留输入向量**，不是套件重生成的产物。这句话已写进该文件抬头（`:27-42`）的醒目段落，防止后人「顺手修好」。

**交付证据。** 修前（未改引擎，新套件先写先跑）：`AssertionError: verse_pack did not write a gzip container; first bytes: b'{"id":"pkgdir","'`、exit 1（我**独立复现**过，用 main 的 `build/inimerse` 跑分支上的新套件，同样这条）。修后：包首字节 `1f8b 0800 0000 0000 0003 …`；`node tools/vverse_pack.js unpack` rc 0，再 `node tools/vverse_validate.js <dir> --strict --require-signature --require-complete-signature` rc 0（**裁判是参考实现，不是自己**）；源树**未被改动**（`find src -printf '%p %s %T@\n' | sort` 前后逐行相同，含 mtime）；**确定性**：同一棵树打两次 `cmp` 逐字节相同；**往返**：`verse_pack` → `verse_open` → `open=1` 且 `home/universe/<pkg>/` 下文件与字节正确（这是原先完全缺失的断言，现在被断言住了）。门禁 `rm -rf build && tools/gate.sh --jobs 4` → **七阶段全 PASS、ctest 95/95、`gate: OK`、exit 0**。

**两条有意为之的能力损失（必须记录，不是疏忽）。** ①`b_verse_pack` 传 `seed = NULL`，**不再**用本地身份自动签名（遗留实现会）—— 这是「产物必须是树内容的纯函数」的直接要求（作业单第 3 条），代价是引擎写的包**不含** `signatures/ed25519.json`，故 `--require-public-signature` 对引擎产物不适用。②真容器的 `id`/`version`/`min_version` 在它自己的 `manifest.json` 里，故遗留那套 publisher / version / `min_version` 元数据校验对真容器**不适用**；它们对遗留容器仍然照旧生效（步骤 5、6）。

**进树**：`CMakeLists.txt` 新增 CTest `vverse_cli_regression`；`tools/gate.sh:43` 的 `EXP_CTEST` 94→**95**；新 `tools/vverse_cli.test.py`（10 checks）。`tools/verse_pack.test.py` 与 `tools/vverse_cli.test.py` 的断言都住在**既有**的 CTest 条目里，所以没有第 96 个条目 —— 计数停在 95 是**有意**的。

### 10.12 能力令牌的签发需要 enrollment 证明（`crp-portal-auth`，`40f6094`）

**缺口。** `POST /portal` 是 hub 的**唯一**授权入口：`/signal`、`/session/resume`、`/session/stop`、`/ws` 都只要求令牌（`tools/crp_relay.js:70`/`:77`/`:83`/`:137`），所以**令牌本身就是全部授权**，而它可以被任何人索取 —— 参考实现只检查「verse 已注册且 `peer` 非空」，不看调用方是谁。危害不止拿到令牌：`crp_registry_portal` 还会 `registry_get_session`（不存在就新建）并 `im_crp_session_apply("start")` + `im_crp_session_lease_begin`，于是未认证调用方可以用任意 `peer` 字符串**无上限造 session**，并在该 (verse, peer) 已有会话时**重置并夺取租约**。这是**契约级缺口**（引擎忠实实现了参考实现），所以修法是**先动判据方**（参考实现），再让引擎跟随 —— 即板上 (b) 分支。

**修法。** 证明式 `base64url(HMAC-SHA256(CRP_ENROLL_SECRET, String(verse) + "\0" + String(peer)))`，与 `tools/crp_relay.js` 的 `enrollProof()` 同式。判定点**只有一处**：`src/verse/crp.c` 新增导出 `int crp_enroll_check(const char *enroll_secret, const VjVal *verse, const VjVal *peer, const VjVal *auth)`（`:1057`），`crp_registry_portal`（`:1069`）与 HTTP 监听器都调用它。未配置 `CRP_ENROLL_SECRET` ⇒ 403 `portal enrollment is not configured`（**fail-closed**，安全默认不依赖调用方记得打开）；证明不匹配 ⇒ 403 `invalid enrollment proof`；**两条拒绝都在查注册表之前**，故未证明的调用方连 verse 是否存在都探不到。比较是常量时间的（先比长度，再 `diff |= got[i] ^ want[i]`）。

**第一版交付被驳回的原因（值得记一笔）。** 第一版（`e47f903`）在 `src/platform/http_posix.c` 里**又写了一份**证明推导：用**原始文本扫描**（`portal_member` 从第一个 `"` 读到下一个 `"`）重新取 `verse`/`peer`，注释宣称与 `enrollProof()` **byte-identical**。我用裸 socket 打真引擎，证伪了三处：①`{"verse":"a\"b",…}` 的原始字节是 `"a\"b"`，扫描读到 `a\` ⇒ **proof over `a"b` → 403，而 over 错的 `a\` → 200**；②冒号后只跳空格与制表符，不跳换行 ⇒ pretty-print 的 body（`json.dumps(indent=2)` 风格）被读成 `"undefined"` → 403；③`strstr` 扫**整个原始请求**而不只是 body ⇒ 请求头里的 `"verse":"other"` 会**遮蔽** body 里的正确值 → 403。三者都不是认证绕过（攻击者仍须算出 HMAC），但都是新引入代码里的**契约/互操作瑕疵**，且 `tools/crp_engine_crosscheck.js` 走的是 `verse_crp_probe`（crp.c 路径），**不经过 HTTP 监听器**，所以门禁看不见。

**第二版修法（`40f6094`，协调者裁定「按最彻底的修法」）。** 不去补扫描器，而是**删掉整份第二推导**（`portal_hmac_sha256`/`portal_b64url`/`portal_member`/`portal_js_string`/`portal_proof`/`portal_proof_ok`），HTTP 监听器改为 `strstr(req,"\r\n\r\n")` 定位 body → `vj_parse` → `vj_get` → `crp_enroll_check`（`src/platform/http_posix.c:2028-2040`）。`verse_portal_proof` 保留签名作薄包装，`src/platform/http_probe.c` 未动。`CMakeLists.txt` 四处加源（三个 probe + 引擎本体的非 WIN32 分支）、三处加 `target_include_directories(... src/verse src/common src/platform)`。CTest 计数**未变**（95）。

**协调者的独立验收（不采信自述）。** `rm -rf build` 全量构建 exit 0，35 条 warning **无一来自 `crp.c`**；`ctest` **95/95**；`tools/gate.sh --fast` 与合入 main 后的 `rm -rf build && tools/gate.sh` 均**七阶段全 PASS、`gate: OK`、exit 0**。**修前必失败**（在 worktree 里 `git checkout main -- src/`，保留新的 `tools/` 测试后重编）：`ctest -R "verse_crp_probe|verse_crp_closed_loop|crp_session_probe|verse_crp_crosscheck|crp_session_flow_regression|reconnect_generation_regression|http_probe|hub_probe"` → **4 failed out of 8**，其中 `reconnect_generation_regression` 的失败值逐字是 `AssertionError: (200, '{"token":"posix-6abf402f","expires":1790919002,"verse":"v1","peer":"p1"}\n')` —— **无证明的 `/portal` 真的拿到了令牌**。修后同一条命令全绿。`crp_engine_crosscheck` → **112 records, text-identical**；`verse_crp_probe` 自检 → **185 checks, 0 failures**。

**F1/F2/F3 由裸 socket 探针确认消失**（真引擎、逐字节控制请求体）：proof over `a"b` → **200**，over 错的 `a\` → **403**（错误读法不再被接受）；`verse` 值换行 → 200；请求头遮蔽 → 200；无 `CRP_ENROLL_SECRET` + 有效证明 → **403 fail-closed**。

**类型强制转换已与 JS `String()` 逐一对齐**（HTTP 路径实测，并与 `verse_crp_probe` 的 crp.c 路径交叉验证）：`null`→`"null"`、`true`→`"true"`、`{}`→`"[object Object]"`、`["a","b"]`→`"a,b"`、`[null,"a"]`→`",a"`、键缺失→`"undefined"`、`\u00e9` 与裸 UTF-8 均 → `café`，全部 200。**这条正是「同一个检查」的直接证据**：`vj_parse` 解码转义与空白的方式与 `crp.c` 对同一串字节的处理一致，故不再有第二套语义。

**已知没解决的四点（都不是本次引入 —— 每一条都在 main `922f9af` 上原样复现，故不阻塞合入）。**

1. **空 scope 等于「任意」**（`src/platform/http_posix.c:287`）：`return (!g_tokens[i].verse[0] || …) && (!g_tokens[i].peer[0] || …)` —— 空 scope 匹配任何请求。`/portal` 用 `vj_str(vj_get(pbody,"verse"), "")`（`:2046-2047`）取 scope，**对任何非字符串 JSON 值都得到 `""`**，故 `{"verse":5,"peer":null}` 铸出的是 **hub 全域通配令牌**。端到端复现：铸出后 `POST /signal` / `POST /session/stop` 打 `{"verse":"totally-other-verse","peer":"other-peer"}` → **200 `{"ok":true,"accepted":true}`**，而 `{"verse":"demo","peer":"p1"}` 铸的令牌 → 403。**用 main 的 `build/inimerse` 跑同一探针输出逐行相同，且 main 上这一步连证明都不需要** ⇒ 本次交付是**收紧**（旧代码路径无需任何证明即可铸出通配符令牌），不是引入。今天严重度低（enrollment secret 是**一个** hub 级值，持有者本就能为任意 pair 出证明），但铸发侧应拒绝非字符串 verse/peer 或拒绝存入空 scope，`token_allows` 也不该把「没有 scope」读成「所有 scope」。
2. **重复 JSON 键**：`vj_get` 返回**第一个**匹配键，main 的 `json_field_string`（`:419` `if (*p != '"') return 0;`）也是 first-wins，而 JS `JSON.parse` 是 last-wins。实测 `{…,"auth":"<good>","auth":"x"}` → 200，`{…,"auth":"x","auth":"<good>"}` → 403。无 secret 不可利用。
3. **非字符串值的回显**：参考实现回显解析后的原值（`{"verse":5}` → `"verse":5`），监听器回显 `vj_str(…,"")` → `""`。与第 1 点同源。
4. **HTTP 监听器的 `/portal` 仍不查注册表**（F4）：它对未注册的 verse 也铸发令牌，而 `crp_registry_portal` 与 `tools/crp_relay.js` 都答 404 `verse not found`。`src/platform/http_posix.c:2023-2028` 的注释称调用方「prove[s] it may open a portal for this exact (verse, peer)」，对非字符串 verse/peer 而言这句不成立。

这四点已登记为 `task-9`（写域 `src/platform/http_posix.c`），附了验收条件：监听器的 `/portal` 要么整体走 `crp_registry_portal`，要么复现它的证明后拒绝（非字符串 verse → 404、falsy peer → 404，且不再有「意外为空」的 scope），并补一条「用非字符串 verse 铸发后、该令牌对另一个 verse 必须被拒」的回归。

### 10.13 portal 令牌的 scope 必须是真实已注册的一对（`crp-portal-postcheck`，`d565253`）

**来源。** §10.12 末段四点（`task-9`）。最重的是 **R1** —— `token_allows` 把「没有 scope」读成「所有 scope」；以及 **R4** —— HTTP 监听器的 `/portal` **从不查注册表**。

**修法：三道防线，让 R1/R4 按构造消失（不是就地补扫描器）。**

1. **判据方先改**（本仓库规矩）：`tools/crp_relay.js` 的 `/portal` 收紧为 `if (typeof p.verse !== 'string' || !verses.has(p.verse) || typeof p.peer !== 'string' || !p.peer) return json(res, 404, { error: 'verse not found' });`
2. `src/platform/http_posix.c` 新增 `static int portal_verse_registered(const char *id)`，扫 `g_verses` —— 即 **`POST /register` 填的那张表**（写入 `:1959-1961`，读出 `:1166`），与参考实现的 `verses` Map 是同一集合。`/portal` 在两条 403 **之后**加：`if (!vv || vv->type != VJ_STR || !vv->s || !vv->s[0] || !portal_verse_registered(vv->s) || !pv || pv->type != VJ_STR || !pv->s || !pv->s[0]) { status = 404; body = "{\"error\":\"verse not found\"}\n"; }`；scope 改用 `vv->s`/`pv->s` **直取**，不再 `vj_str(…,"")` 强转。
3. `token_register` 拒绝存入空的 scope 半边：`if (!verse || !*verse || !peer || !*peer) return;`；`token_allows` 改为先 `if (!g_tokens[i].verse[0] || !g_tokens[i].peer[0]) return 0;`，再要求两半**逐字相等**。
4. 引擎的 crp.c 路径同步收紧：`crp_registry_portal` 的 peer 测试加上 `peer->type != VJ_STR || !peer->s || !peer->s[0]`，并把 `pstr` 从 `crp_js_string(peer, peerbuf, …)` 改成 `peer->s` —— 两侧**不再有强转可分歧**。

**行为改变（对第三方客户端是破坏性的，必须写明）。** `/portal` 现在只为**经 `POST /register` 注册过**的 verse 铸发令牌。三个在树套件补了 `/register` 步骤：`tools/crp_relay.test.js`、`tools/crp_session_flow.test.js`、`tools/reconnect_generation.test.py`；`tools/crp_client.test.js` 本来就先 `register`。

**协调者独立验收（不采信自述）。** worktree 内 `bash tools/gate.sh`（全量，非 `--fast`）→ 七阶段全 PASS、`gate: OK`、exit 0。裸 socket 探针（真引擎，先 `POST /register {"id":"demo",…}`）：**11 条断言全过** —— `{"verse":5,"peer":null}`→404、`{"verse":5,"peer":"p1"}`→404、`{"verse":"demo","peer":5}`→404、`{"verse":"demo","peer":null}`→404、`{"verse":"demo","peer":""}`→404、未注册 `{"verse":"ghost"}`（**证明正确**）→404、注册过的 `{"verse":"demo","peer":"p1"}`→200；该令牌打 `/signal` 与 `/session/stop` 的 `(demo,p1)` → 200，打 `(other,other)` → 403。**同一探针打 main 的 `build/inimerse` → 6 条 FAIL**，其中 `{"verse":5,"peer":null}` 铸出 `{"token":"posix-…","expires":…,"verse":"","peer":""}` —— 逐字就是 §10.12 记的 hub 全域通配令牌；`{"verse":"ghost"}` 则铸出 `"verse":"ghost","peer":"p1"`（**未注册也发**）。

**修前必失败（独立复现）。** worktree 内 `git checkout 1fe93be -- src/`（保留本流的 `tools/`）后重编：`verse_crp_crosscheck` → `crp_engine_crosscheck: 2 of 115 records differ`（line 76 `peer:5`、line 77 `peer:[]`：参考 404 而引擎 200）；`crp_session_flow_regression` → `Expected values to be strictly equal: 200 !== 404`；`reconnect_generation_regression` → `AssertionError: (200, '{"token":"posix-6abf5171","expires":1790923420,"verse":"v-unregistered","peer":"p1"}\n')`。恢复后重编、全量 `ctest` 全绿。语料 **112 → 115**。

**遗留（未调和，如实登记）。** **R2 重复 JSON 键**：`vj_get` 是 first-wins，而 JS `JSON.parse` 是 last-wins。本流实测仍分歧 —— 对 `{"verse":"demo","verse":"ghost",…}`，证明按**首个** `demo` 算 → 200，按**末个** `ghost` 算 → 403；参考实现恰好相反（`JSON.parse` 取 `ghost`，故按 `ghost` 的证明会被接受、再被 `verses.has("ghost")` 判 404）。未修的原因是两个都成立：`vj_get` 在 `src/common/**`（本波硬禁碰），且 `JSON.parse` **无法**表达 first-wins，两侧不可兼得。R3（非字符串值回显）**随 R1 一并消失**：非字符串 verse/peer 现在直接 404，且回显改用 `portal_json_escape(vv->s)` 而非强转。

### 10.14 AOT：`1.09x` 是噪声，而 `≥2x` 需要一个此前不存在的后端（`aot-backend`，`d23691e` + `70f73ae`）

**结论一：归档的 `1.09x` 不可复现，它是中心在 `1.00x` 的分布里的一个样本。**
`tools/aot_parity.py` 把整套 `perf_compare` 对比重复 N 次并打印**分布**，外加「解释器对解释器自己」的对照来量这台机器上的噪声地板（分子 = `run work.im` 中位减 `run empty.im` 中位，分母同理）。

| 工作负载 | AOT 比值 | 对照（解释器 vs 自己） | 落在对照区间内 | ≥2x |
|---|---|---|---|---|
| `sum(1..2000000)`，12 次试验 | 0.85x .. 1.33x，中位 **0.98x** | 0.77x .. 1.36x，中位 0.98x | **12/12** | **0/12** |
| `sum(1..20000000)`，7 次试验（10 倍工作量） | 0.47x .. 1.05x，中位 **0.95x** | 0.52x .. 1.07x，中位 0.99x | 6/7 | **0/7** |

`1.09x` 恰好是第 8 次试验取到的值。工作量放大 10 倍也收不紧噪声（±20%~50%）。**所以「先原样复现 1.09x」这个要求本身不可满足 —— 而「复现不出来」就是答案。**

**结论二：`--aot` 是打包通道，与解释器等同是预期结果，不是实现质量差。**
`main_aot_package()`（`src/main.c:636-656`）走 `parse_program_file` → `comp->target = TARGET_AOT`（`:641`）→ 取主函数字节码 → `bytecode_append_to_exe(self, bc, output)`（`:646`）→ `chmod 0755`。`bytecode_append_to_exe()`（`src/compiler/bytecode.c:367-389`）把引擎可执行文件按 4096 字节分块**逐字节复制**，再追加字节码与尾魔数 `0x1BC0FFEE` + 偏移 —— 产物 = **引擎副本 + 字节码尾块**，跑的是同一个 C 解释器。`--aot` 分发在 `src/main.c:1120-1139`，`run --aot` 明确报错（`:1169-1170`）。
**决定性证据：`TARGET_AOT` 是死值。** 声明在 `src/compiler/compiler.h:20`，只在 `src/main.c:641` 被赋值，**全仓库没有任何地方读它**（`grep -rn TARGET_AOT --include=*.c --include=*.h .` 只有这两处）。所以 `compile --aot` 产出的就是解释器本来也会产出的那份字节码。
**因此「outperform the interpreter by at least 2x」在这个通道上不可能成立，与实现质量无关。**

**结论三：`≥2x` 所属的原生代码生成后端此前不存在，本次给出原型并实测。**
`src/compilation/aot_native.{c,h}` + `aot_native_tool.c`：AST 数值子集 → 独立 C → 宿主 `cc -O2`，子集外**一律拒绝翻译**（不是错误编译）。`python3 tools/aot_native_bench.py --trials 5 --runs 5`，工作负载 `mix(1, 2000000)`（数据依赖递推 `x = (x*3+7) % 65536`，无闭式）：

| 试验 | 解释器计算 (ms) | 原生计算 (ms) | 加速比 | 对照 |
|---|---|---|---|---|
| 1 | 125.37 | 3.383 | 37.1x | 1.06x |
| 2 | 126.16 | 3.683 | 34.3x | 1.00x |
| 3 | 129.77 | 3.419 | 38.0x | 1.02x |
| 4 | 132.58 | 3.899 | 34.0x | 0.98x |
| 5 | 121.49 | 3.670 | 33.1x | 0.90x |

中位 **34.3x**（33.1x .. 38.0x），对照中位 1.00x；生成汇编含 **99 处回跳分支**，两通道输出一致（`19713`）。工具内置两条拒绝条件：汇编没有循环分支就拒绝给比值；两通道答案不一致也拒绝。
**识破并防住了两个会把结果变成虚构的测量陷阱：** ①`cc -O2` 会**完全常量折叠**字面量工作负载 —— 首次测量得到 **153x，纯属虚构**（GCC 编译期就算完 `sum(1..2000000)`，生成函数零分支，把 n 改成 2e8 仍耗时 0.00 s）；②即使输入来自运行期，标量演化仍会把 `sum(1..n)` 闭式成 `n(n-1)/2`，同样零分支。故改用无闭式的数据依赖递推 + `--extern/--entry` harness 钩子。

**协调者独立验收（不采信自述）。** worktree 内 `bash tools/gate.sh` → 七阶段全 PASS、`gate: OK`、exit 0。`tools/aot_native_build.sh` 重建原型成功且 `-Wall` **零诊断**。我复跑 `tools/aot_native_bench.py --trials 5 --runs 5` → 中位 **36.4x**（27.1x .. 46.2x），对照中位 1.00x，99 处分支，两通道 `19713` —— 与它自报同量级。`tools/aot_native.test.py` → `57 cases (45 equivalence, 2 pinned divergences, 10 refusal), 0 failures`。

**遗留（重要，已登记为 `aot-native-integration`）。** **整套原型不在门禁内**：`src/compilation/aot_native.c`（636 行）**不被 CI 编译**（第一波冻结了 `CMakeLists.txt`），`tools/aot_native.test.py`（57 例）**不被 CI 运行**，构建靠 stop-gap 的 `tools/aot_native_build.sh` 扫 `build/CMakeFiles/inimerse.dir/**/*.o`（`rm -rf build` 后必须先建引擎）。**提交了 636 行 C，门禁一行都不会看它。**
另有四条如实上报的边界：①两处**真实语义分歧**被**钉死**在测试里（`func nothing() { x = 1 } say nothing()` → 解释器 `nil` vs 原生 `0`；函数内给全局赋值 → 解释器把该名字变**局部**、实测 `5\n2\n`，原生写全局 `7\n7\n`），只钉死未修；②`x = (x*1103515245+12345) % 2147483648` 解释器警告「out of 32-bit range, promoted to float」并输出 `0`，原生输出 `357615489` —— 既未修也**未进语料**；③**浮点打印不纳入等价语料**（解释器 `1.0/3.0`→`0.333333` 但 `1.23456789012345678`→`1.234568`，无单一 printf 精度可匹配，原生用 `%g`）—— 是**已知不覆盖**，不是通过；④45 项等价用例**全是 int/bool**，不能外推到语言整体。

**口径收敛后的唯一正确表述**（`docs/archive/SELFHOST_BENCHMARK.md` 新增「口径归属」一节）：AOT **打包**通道 = 与解释器**等同**，不得表述为加速；Wasm MVP = 1.51x；原生代码生成 = 原型实测 34.3x 但**尚未发布、尚未接入构建**，不构成 v0.5 的能力声称；`≥2x` 的唯一归属是**优化型 AOT 后端，v0.5→v0.6 后续迭代**。

**连带修正（本次一并落地）。** ①**行号错误**：`≥2x` 那句在 `docs/archive/RELEASE_0.5.0.md` **第 52 行**，不是第 43 行（第 43 行是「Optimizations: loop invariant hoisting…」）；`:43` 原先写在本文档 §3.1，被抄进了 `docs/BOARD.md` 与 `docs/streams/aot-backend.md`，三处已修正。②`docs/archive/CHANGELOG_0.5.0.md` 是唯一还挂着「at least 2x」且**没有任何更正**的副本，已加废止说明。③`docs/archive/ROADMAP_0.5-0.6.md:54` 写 Wasm MVP「~1.8x」，与 benchmark 自己的 **1.51x** 矛盾，已改为 1.51x。④`1.09x` 在 `RELEASE_0.5.0.md`、`archive/README.md`、`API.md`、`REQUIREMENTS_ANALYSIS.md`、`future/优化路线pro.md` 与本文档 §2 均改为**分布口径**（与解释器等同，中位 0.98x）。
**当时未修（发现但不在本次写域内）：** `tools/perf_compare.py:131` 与 `tools/selfhost_bench.py:121` 的 `--write-docs` 把报告写到 `docs/` 下的 `SELFHOST_BENCHMARK.md` —— 少了 `archive/` 这一级，那条路径**不存在**，脚本会新建一个文件；真实文件在 `docs/archive/SELFHOST_BENCHMARK.md`。修它会牵动发布流程，单独记账。**（这一段的坐标是 `d23691e` 那棵树上的读数；`199e240` 已把它修掉 —— 今天那两个 `--write-docs` 写在 `docs/archive/SELFHOST_BENCHMARK.md`，在 `tools/selfhost_bench.py:130` 与 `tools/perf_compare.py:133`。）**

### 10.15 跨语言绑定从「文档里的承诺」变成真的 `.whl` 与 `.jar`（`xlang-bridge`，`363665f`）

**缺口（本文档 §3.1 第 ③④⑤ 条）。** `docs/archive/RELEASE_0.5.0.md:26` 声称 `inimerse_extension.c` / `PyInit_inimerse()` 存在、`:27` 声称 `InimerseBridge.java` 存在、`:123-130` 声称 `.whl` / `.jar` 存在 —— **全部不存在**。仓库当时只有 `tools/bindgen.py`（一份 `.def` → C/C++/Java/Python 的**生成器**）与 `tools/bindgen.test.py`（只覆盖生成器本身）；`examples/interface.def` 的 `imim_add` / `im_greet` / `im_fail` 在树里**没有任何实现**；`examples/python_bridge.im` 与 `examples/java_bridge.im` 只是 `say "see …"` 两行的说明脚本。

**修法：一个 IDL，两侧真产物。**
- 唯一 IDL `src/bridge/inimerse_bridge.def` 声明三个函数：`version()`、`sha256_file(path)`、`parse_count(source)`。
- `CMakeLists.txt` 由 `tools/bindgen.py` 从它生成 `build/bindings/c/inimerse_bridge.h` 与 `build/bindings/java/InimerseBridge.java`，再分别编出 `inimerse_python`（`MODULE`，CPython 扩展）与 `inimerse_bridge`（`SHARED`，JNI）+ `InimerseBridge.jar`。
- **引擎源表改成对象库**：`add_library(inimerse_engine OBJECT ${INIMERSE_ENGINE_SOURCES})`（`CMakeLists.txt:334`，`POSITION_INDEPENDENT_CODE ON`），`src/main.c` 移出引擎源表、单独成为 `add_executable(inimerse src/main.c)` 并链引擎 —— 于是可执行文件与两个 `.so` 用的是**同一批翻译单元**，不是副本。
- 三个函数都调**真引擎符号**（`src/bridge/bridge_abi.c`）：`version()` → `INFIVERSE_VERSION`；`sha256_file()` → `inim_file_sha256()`（`src/compilation/checksum.c`）；`parse_count()` → `parse_program()`（`src/parser/parser.c`）。**没有一个是 echo。**

**门禁改造（本波授权该流改 `tools/gate.sh`）。** `EXP_CTEST` **95 → 97**（`tools/gate.sh:49`）；stage 标签变 `ctest (expect 97/97, 0 skipped)`（`:159`）；`stage_ctest` 新增**跳过计数**（`:101`）：`grep -cF '***Skipped'`，非零就**红掉**并把 `The following tests did not run:` 段落打到 stderr。理由写在 diff 注释里：`ctest` 对 `exit 77`（`SKIP_RETURN_CODE`）的测试仍然打印 `100% tests passed, 0 tests failed out of N` —— 没有这个计数，**缺工具链时门禁会绿着一个字都没验证的状态**。
两条新测试 `xlang_python_bridge`（`CMakeLists.txt:487`）/ `xlang_java_bridge`（`:494`）**无条件注册**（在 `if(XLANG_HAVE_TOOLCHAIN)` 块**之外**），只因前缀缺失而 exit 77。
**工具链在仓库外**：`XLANG_TOOLCHAIN` 默认 `$ENV{HOME}/.local/xlang-toolchain`（`CMakeLists.txt` 明确不硬编码绝对路径），`examples/BUILDING_BRIDGES.md` 逐字记下 `apt-get download` / `dpkg-deb -x` 的全部命令。**前缀里没有一个字节进 git**：`git ls-files | grep -c xlang-toolchain` = 0，提交文件里 `/home/sakiko` **零命中**。

**协调者独立验收（不采信自述）。**
- 拓扑：单 commit `363665f`，基 `5305ebf`；`git diff --stat 5305ebf..stream/xlang-bridge` = 11 files, **+1161/-8**；与 main 自基点以来的改动**零文件重叠**；`git merge-tree --write-tree main stream/xlang-bridge` rc=0；`git merge --no-ff` → **`36bf4e9`**。
- **我复跑两侧回归**：`python3 tools/xlang_python_bridge.test.py build ~/.local/xlang-toolchain` → `16 check(s), 0 failure(s)`；`python3 tools/xlang_java_bridge.test.py …` → `14 check(s), 0 failure(s)`。
- **两侧输出逐字节相同**：`python says: 0.5.0 c3ece9cf870e190b5637b4d37e18944b7f37f6f2af8c47283b0241de7c758e47 5` 与 `java says:` 同一行。
- **我独立重算该摘要**：`sha256sum src/bridge/bridge_fixture.im` → `c3ece9cf870e…47`，与两侧打印一致；fixture 头部手工标注的**顶层语句数 5** 也对得上。
- **`.whl` 是真的**：`zipfile` 列出 = `inimerse.cpython-314-x86_64-linux-gnu.so` + `inimerse-0.5.0.dist-info/{METADATA,WHEEL,RECORD}`；`METADATA` = `Name: inimerse` / `Version: 0.5.0`。
- **`.jar` 是真的**：`jar tf` = `InimerseBridge.class` + `InimerseBridge$InimerseException.class` + `InimerseBridgeMain.class`（另有一个 `javac.stamp`，是「classes 目录当 jar 根」的无害副产物）。
- **修前必失败（结构性复核）**：main `468f5e8` 上 `git ls-tree main --name-only src/bridge/` = **0 个文件**、`tools/` 里 `xlang` **零命中**、`CMakeLists.txt` 里 `inimerse_bridge` **零命中**，且 `cmake --build build --target inimerse_bridge` → `gmake: *** No rule to make target 'inimerse_bridge'. Stop.`（与它自报的 `/tmp/xb-base` 结果一致）。
- **合并后全量门禁（清理 `build/` 从零 configure）**：`rm -rf build && bash tools/gate.sh --jobs 8` → 七阶段全 PASS、`ctest (expect 97/97, 0 skipped)`、`gate: OK`、exit 0；links 88 files / 318 links / 0 broken。

**三条裁决（它明确要我裁的）。**
1. **`bindings/**` 与 `.whl`/`.jar` 不提交进 git —— 准。** 它们是**构建产物**，由唯一 IDL 生成/打包；提交了就必然与 IDL 漂移，还得再加一条「生成物过期」检查。验收条件是「存在且可重复构建」，而两侧回归**就在构建产物上断言**（`.whl` 装进干净 venv 后断言 `import` 从 venv 内解析而非 build 树、`.jar` 用真 `java -cp` 跑），比提交一个 blob 更强。发布文档里「`.whl`/`.jar` 存在」由此成为事实。
2. **`parse_program()` 语法错时 `exit(1)` —— 确认是真缺陷，本次不修，单开后续流。** 我实测（不是采信）：在装了 wheel 的路径上 `inimerse.parse_count('x = ')` → **宿主 Python 进程直接以 exit code 1 退出**，stderr 只有 `Error: expected 'expression', but got '' (type 141)`，**没有异常、没有 traceback**。`grep -n "exit(1)" src/parser/parser.c` 命中 12 处（行 69 / 73 / 82 / 86 / 144 / 1061 / 1064 / 1411 / 1519 / 1521 / 1624）。`src/bridge/bridge_abi.c:78-81` 写了 `if (!prog) return 4;` 这条错误返回 —— **但它在语法错时不可达**：桥接层是按「可恢复」写的，引擎**不是**可恢复的。修它要动 `src/parser/**`（本波写域外）并会牵动 CLI 的既有错误策略。`examples/BUILDING_BRIDGES.md:162-165` 已如实披露这一条。
3. **`inim_load_text()` 的绕行 —— 准其作为短期方案，但这是味道，单开后续流。** `grep -rn inim_load_text src/` 确认声明在 `src/common/common.h:55`、**唯一实现**在 `src/main.c:346`（另有 `src/compilation/aot_native_tool.c:24` 也定义了一份）。把 `src/main.c` 移出引擎源表后两个 `.so` 报 `undefined symbol: inim_load_text`，它的解是让桥接目标**再编一次同一份 `src/main.c`** 并用 `target_compile_definitions(... PRIVATE main=<target>_entry_unused)` 改名 —— **同源不同编译，不是拷贝**，且 `.so` 不导出 `main`。更干净的做法是把 `inim_load_text` 搬进引擎本体，那要动 `src/**`。

**遗留（未做）。** ①**Windows 分支未验证**：桥接目标只在工具链探测通过时构建，WIN32 下不生成也不红，OBJECT 化保留了原 WIN32 链接块。②本机无 `python3.14-venv` / `ensurepip`，venv 用 `--without-pip` 建、wheel 用 `pip install --target` 装，wheel 本身用 `zipfile` 手工组装（`python3 -m build` 不存在）—— 已记在 `examples/BUILDING_BRIDGES.md`。③`examples/interface.def` 的 `inim_add` / `im_greet` / `im_fail` **仍是悬空示例 IDL**，本次没动它。①②③与上面两条裁决 2、3 一并登记为 `xlang-bridge-followups`。


### 10.16 Wasm：线性内存堆是真的，v128 已实现但未被选路，GC 明确没做（`wasm-simd-gc`，`3fadc8b`）

**来源。** `docs/archive/RELEASE_0.5.0.md:47` 声称「WebAssembly output … supporting SIMD optimizations and WebAssembly GC」，而 `src/compilation/wasm_backend.h` 原文写 `SIMD/GC/heaps are future work.`。作业单把这一句拆成**三件互不相同的事**，要求逐件给出事实或有证据的降级结论。判据是 `tools/wasm_backend.test.py` 的**解释器 vs wasm host 双跑逐字节等价**（13 例 → **22 例**）。

**三件的结论。**

- **heaps：做了，而且是确定的。** 模块自己的线性内存里开了一个 arena（`HEAP_BASE=4206592` .. `HEAP_END=8388608`，≈3.99 MiB，`MEM_PAGES=128`），块头 16 字节 `[size][refs][next_free][count]`，元素 i 在 `payload+16i`，`count` 同时就是 `len()`；分配器是 LIFO 空闲链表 first-fit + bump。**耗尽不是静默的**：`repeat 70000 { b=[a,2,3]; a=b }` → wasm rc=1、`error: heap_exhausted (code 4)`（解释器 rc=0；这是**有界分歧**，已写进文档）。**回收是确定的**：新块 `refs=0`，只有「被拥有槽持有」才 +1（具名/全局/参数/元素槽拥有，帧临时槽只借用）；`repeat 300000 { b=[1,2,3]; s=s+b[0] }` 打印 300000 与解释器一致 —— 不复用的话这个 arena 只装得下 65344 次（`(HEAP_END-HEAP_BASE)/64`）。
- **v128 SIMD：实现了、测量了，但生成器不选它。** 导出 `bench_sum_scalar(n)` / `bench_sum_simd(n)`（f64x2 lane 并行，两者结果**逐位相等**），`node tools/wasm_run.js --bench <mod> <n>`。协调者独立复跑：n=2e7 → **2.158 / 2.342 / 2.008**（该流自报 1.870 / 2.136 / 1.791），n=5e7 → **1.509**（自报 1.888）—— 同量级，机器上有波动。**不选路的三条理由是承重的**：16 字节装箱槽让向量存储没有收益、整型 lane 定宽回绕而语言要求越过 int64 即报 `numeric_overflow` 使整型 lane 语义不同（v3.1 之前这条表现为「32 位溢出提升为 float」，见 §10.49）、浮点归约重结合会改变结果（与「逐字节等价」判据直接冲突）。所以口径是**「已实现、已测量、未选路」，不是「已用 SIMD 优化」**。
- **wasm-GC：明确未实现，而且它本来就是另一件事** —— 需要 `--enable-gc` 与 struct/array 引用类型，与上面这个线性内存堆无关。**这正是 `RELEASE_0.5.0.md` 把三件事混为一谈的地方。**

`wasm_backend.h` 那句已改写（现 `:10-27`，三段口径），新增 `docs/WASM.md`（211 行：ABI/PAL、模块布局、堆与所有权、数组语义表、错误码、SIMD 实测表、GC 降级、复现命令、已知边界）。

**顺手修掉的两个真实缺陷 —— 都改了 `main` 的行为，必须写明。** ① `cg_print_slot` 原来把整段分派包在 `if (tag != 0)` 里，**`nil` 什么都不打印**：协调者实测 `func f() {}\nsay f()\nsay 7`，解释器输出 `nil\n7`，而 main 的 wasm **只输出 `7`（`nil` 被静默丢弃）**；修后两侧都是 `nil\n7`。② 元素存储与 `im_release` 用了**两个不同的元素基点**，每个数组末尾**多写 16 字节**，在 arena 末尾表现为 `memory access out of bounds` 而不是设计的 `heap_exhausted`。

**协调者的独立验收（不采信自述）。** `git diff --name-only main...stream/wasm-simd-gc` 恰好 5 个文件、全在写域内（`CMakeLists.txt`、`tools/gate.sh`、`src/common/**`、`docs/STATUS.md`、`docs/BOARD.md` 零改动）；`merge-tree` rc=0 零冲突。**修前必失败**（worktree 内 `git checkout main -- src/compilation/wasm_backend.{c,h}`、保留本流的 `tools/` 后重编）：`tools/wasm_backend.test.py` 在 `nil_print` 上抛 `AssertionError: nil_print: output mismatch / wasm: b'0\n' / interp: b'nil\n0\n'`；恢复后 `wasm backend: ok (22 equivalence cases, 3 rejections, 3 explicit failures, simd bench equal)`。全量门禁七阶段 PASS、`ctest (expect 97/97, 0 skipped)`、`gate: OK`、exit 0 —— **未新增 CTest、未改 `CMakeLists.txt`**（新用例全塞进既有 `wasm_backend_regression`）。

**遗留。** ① **字符串没做**（只做了数组）：打印字符串要给 PAL 加第 6 个 `env` 导入，而宿主契约是「未提供的导入一律拒绝」，加导入等于改宿主契约 ⇒ 维持编译期显式拒绝 `strings are not supported by the wasm MVP subset`。这是路线 A 唯一未做的子项。② 只存在于**临时槽**的数组不会被回收（**有界泄漏**，不是悬垂指针；具名槽回收确定）。③ `a[i] = v` 的越界写是**显式分歧**（解释器自动增长，wasm 拒绝 `array_index_out_of_range`），`say <数组>` 同理（`array_op_unsupported`）。④ `docs/WASM.md` 与头注释都写着 `22 equivalence cases` 这一行，**将来加例要同步两处**。

**连带修正。** ① `RELEASE_0.5.0.md` 的声明行号**此前全部漂移**（更正横幅插在文件顶部所致）：SIMD/GC 实为 `:47`（旧记 38）、Python 桥 `:35`（旧记 26）、Java 桥 `:36`（旧记 27）、发布产物 `:132`/`:133`/`:139`（旧记 123–130）；AOT 那条 `:52` 是对的。② §3.1 表里「仓库中不存在 `inimerse_extension.c` / `InimerseBridge.java` / `.whl` / `.jar`」**三条已被 `xlang-bridge`（`36bf4e9`）推翻**，已一并更正 —— 留着一句「仓库中不存在」指着一个已经存在的文件，正是本仓库的口径纪律要防的事。

### 10.17 后缀 `if`/`unless` 的跨行歧义，与 VM 返回时恢复 `sp` 读错帧槽（`37d4ea6` + `df82cf6`）

**来源。** 为 `selfhost-0.5-record` 验数时，协调者发现 `./build/inimerse selfhost/compiler.im <任意目标>` 与文档记载的 `selfhost/compiler.im --dump selfhost/lexer.im` **全部 exit 1** —— **整个自举工具链自 2026-09-06 起就不可解析**，而门禁一直是绿的。顺着往下挖出第二条缺陷。两条都属同一类：**退出码与「跑得动」完全掩盖了错误**。

**缺陷 A —— 后缀 `if`/`unless` 跨行贪婪匹配（`37d4ea6`）。** `src/parser/parser.c` 的两处后缀条件调用点（原 `:1650` 表达式语句后、`:1689` `say` 后）**不检查换行**，把下一行 `if cond {` 的 `if` 当后缀条件吃掉；剩下的 `{ … }` 落到语句位置被当**表达式**解析成字典字面量，在 `consume(p, TOK_COLON, "':'")` 上炸掉。**这是回归**：后缀 `if` 由 `a21915b`、`say` 上的后缀由 `fe65f6d` 引入，两者都**不是 0.2.0 发布点 `8248e08` 的祖先**，而该模式在 0.2.0 时已存在于 `selfhost/compiler.im` 与 `scripts/array_test.im`。

**消歧规则＝后缀 `if`/`unless` 必须与宿主语句同行。** 实现三处：`src/parser/parser.h` 的 `Parser` 新增 `int prevLine;`；`src/parser/parser.c` 的 `advance()` 改为 `p->prevLine = p->lex.line;` 再取下一个 token；新增 `static int postfix_cond_here(Parser *p)`（`return p->lex.line == p->prevLine;`），两处调用点改用它。**为什么选同行规则而不是「`if` 后跟条件再跟 `{` 优先当块语句」**：`lexer_next()` 先 `skip_whitespace()` 再取词（`src/lexer/lexer.c:100-101`），所以 `p->lex.line` 就是当前 token 起始行、`p->prevLine` 是上一枚已消费 token 的行，判据是**已有信息**、零额外前瞻；而块语句候选要保存/恢复 `Lexer` 再解析一遍条件表达式，既重复解析又会把首次尝试的 `parse_error_expected()` 打到 stderr。**残留风险**：规则依赖「同一行」，所以**排版（换行）会改变语义**。

**影响面与验收。** 主树（排除 `.worktrees/`）**19 处**，`selfhost/` 占 15 处（`compiler.im`/`eval.im`/`lexer.im`/`parser.im` 全中），另有 `scripts/array_test.im:30`、`examples/scripts/block_edit.im:232`、`projects/demo/main.im:330`。进树回归 `vtest/postfix_condition_multiline_v05.im` + CTest `postfix_condition_multiline_runtime`，**带负控**（`git checkout HEAD -- src/parser/parser.c` 后重编 → `Failed  Required regular expression not found`；`git apply` 回补丁后 `Passed`，`cmp` 证明逐字节相同）；另有 CTest `selfhost_toolchain_parses` 跑 `compiler.im --dump tests/_mini.im` 并断言输出含 `main:`。**方法论纠正**：`--lint` **不能**当解析判据 —— `src/main.c:1252-1257` 的 `lint_check()` 走独立通道并**恒返回 0**，实测 `x = = 5` 的 `--lint` exit 0 而真跑 exit 1。

**缺陷 B —— VM 返回时恢复 `sp` 读错帧槽（`df82cf6`）。** 修好解析后自举 codegen 只发一条 `OP_HALT`，再往下挖发现引擎把「实参里嵌一个用户函数调用」编错了。**判定在 VM 调用约定，不在代码生成**：临时 `dbg_fdis` 反汇编证明 `m2` 的函数体在「单独成文件」与「与 m1 同文件」两种形态下**字节码逐条相同**，只有函数下标不同。**真因**：`frame_sp` 写入用**本帧下标**（`L_CALL_FUNC` 在 `frame_count++` 之后写 `frame_sp[frame_count-1]`），读出却**高了一格**（`L_RETURN` 在 `frame_count--` **之前**读 `frame_sp[frame_count]`）；`frame_code`/`frame_ip`/`frame_base`/`frame_res`/`frame_env` 五个字段都是本帧下标读写，**只有 `frame_sp` 是异类**。`INIM_TRACE_SP` 实证：`[call f0] fc=2 WRITE frame_sp[1]=0` / `[ret] fc=2 READ frame_sp[2]=0`。读到陈旧值 ⇒ 返回时 `t->sp` 被重置成更深一层调用留下的旧值，**静默覆盖挂起的操作数**。**修复一行**（`src/vm/vm.c:3692`）：`t->sp = t->frame_sp[t->frame_count];` → `t->sp = t->frame_sp[t->frame_count - 1];`。

**原判断被推翻。** 「取决于编译单元里还有什么」是**假象** —— 同一文件内连续调 `m2` 得 `call1=2 call2=1 call3=1`，**只有第一次调用是对的**，加 `m1` 只是让 `m2` 不再是第一次。⇒ **受影响面比 `push` 宽得多：凡是「操作数栈非空时发生的用户函数调用」都会中招**。扫描 318 个 `.im`：**21 个文件、44 个 (外层,内层) 调用点**（`selfhost/parser.im` 10、`selfhost/eval.im` 4、`selfhost/compiler.im` 3、`workbench.im` 4、`projects/demo/main.im` 3…）。两条进树回归 `call_arg_clobber_alone_runtime`（负控对照组，在 main 上本来就对）+ `call_arg_clobber_shared_runtime`（**在 main 上必失败**，`***Failed  Error regular expression found in output. Regex=[m2=1|dk=0|nest=0|rep=1,1,1]`），用 `FAIL_REGULAR_EXPRESSION` 断言**数值**——退出码在两种形态下都是 0。

**协调者独立复跑（不采信自述）。** main `d923ec3` 的既有 `build/`：`callarg-shared-ok m1=1 m2=1 m3=2 m4=0 m5=0 dk=0 nest=1 rep=1,1,1`；`df82cf6` 分支工作树重建后：`callarg-inner=1` / `callarg-shared-ok m1=1 m2=2 m3=2 m4=1 m5=1 dk=1 nest=1 rep=2,2,2`；`git diff d923ec3..df82cf6 -- src/vm/vm.c` 确认净改动**恰好一行**。

**门禁计数。** `EXP_CTEST` **99 → 101**（`tools/gate.sh:49`），BOARD §3 与本文件 §2/§2.1 同步。**作业单在这里算错了**：它写「99 → 100」，但同一段点名了**两个**测试名 ⇒ 加两条就是 101；队友第一次门禁照 100 跑，红在 `gate: ctest did not report '0 tests failed out of 100'.`。`CMakeLists.txt` 的 `add_test(` 实测 **101** 个。**这正是本仓库要把用例数当断言的原因**：两个文档检查器都不校验这个数字，门禁可以在文档写着 95、97、99 的状态下全绿。

**顺带解除的阻塞。** `./build/inimerse selfhost/compiler.im --dump test1.im` 从 1 条 `OP_HALT` 变成 **120 条指令**；但自举路径仍**一行程序输出都没有**（C 路径打 11 行），两者 exit 均为 0 ⇒ `selfhost-codegen-empty` 的阻塞已解除，但它要查的是**第二层**原因。

### 10.18 全量 `.im` 语法参考，以及一份冗余/危险语法清单（`docs/SYNTAX.md`）

**起因。** 自举逃逸工作开始前需要一份「当前实现究竟接受什么语法」的权威清单 —— 此前 `docs/` 下**没有任何现行语法参考**（`docs/archive/SYNTAX_SUGAR.md` 是 2026-10-01 归档的历史件，只作参考）。**它同时是一次验收工具**：`engine-push-call-arg-miscompile`（§10.17）就是在这份文档 §7.1 的 D1 里被记为最高优先级危险语法，而该文档是**先于**修复完成的。

**取证纪律。** 每条语法都给出**源码行号**（`路径:行号`）或**可复现的实测命令与输出**，证不出的不写。真相来源：关键字表 `src/lexer/lexer.c:5-53`、token 枚举 `src/lexer/lexer.h:7-40`、表达式种类 `src/parser/ast.h:41-49`、语句种类 `src/parser/ast.h:84-104`，加上约 40 个探针脚本的实测输出。**逐关键字反查结论**：对 `keywords[]` 每个词 grep 全部 `src/**/*.c|h`，**只有 `TOK_UNKNOWN` 在 lexer 之外零引用** —— 即没有完全死的关键字，但「被引用」≠「有语法」。

**结构。** §0 取证来源表 / §1 词法 / §2 字面量与值 / §3 表达式与优先级（12 级优先级表，由 `parse_expr:720` → `parse_coalesce:687` → `parse_logic_or:678` → `parse_logic_and:669` → `parse_comparison:626` → `parse_add_sub:617` → `parse_mul_div:608` → `parse_unary:596` → `parse_postfix:503` → `parse_primary:183` 的下降链推出）/ §4 语句 / §5 内建函数（447 个 `vm_register_builtin*` 调用点的分布表）/ §6 完整关键字表（八组别名）/ §7 **疑似冗余或危险的语法** / §8 复现方法与回归断言注意事项。

**§7 是重点，共 31 条，每条都有实测输出或源码行号。** 10 条「危险·静默」（不报错但结果错）：D1 `push(list, <用户函数调用>)` 误编译、D2 GUI 动词无 arity/类型检查（`sprite 42` 与 `sprite "x"` 一样「成功」）、D3 类型标注纯装饰（`int x = "hello"` 通过）、D4 参数个数不校验（`h(1,2)`→`1`、`h()`→`nil`，两个方向都不报错）、D5 未声明变量求值为 `nil`、D6 字符串里的 `\0` 截断（`len("a\0b")` = 1）、D7 `(1,2)` 是区间不是元组、D8 `a[1~2]` 是集合区间不是切片、D9 `type` 已注册为内建却是保留字（`type(x)` 是解析错误，唯一途径 `x.type`）、D10 `N`/`Z`/`Z+`/`Z-`/`Float1..9` 被硬编码为集合前缀（`func Z(a,b)` 后 `Z(1,5)` 得到 `set(Z interval)`，函数根本没被调用）。11 条「危险·误导」：M1 `\|>` 与 `>>` 不能混用（两个顺序循环而非统一循环，报错是 `expected ')'`）、M2 `->` 既是 lambda 又是类型转换、M3 命名实参不支持且报错落在别处、M4 保留字可作成员名、M5 `until`/`till` 只在 `do…until`、M6 `..` 不是通用运算符、M7 `show(` 变函数调用、M8 `join` 双重身份、M9 `match` 上下文敏感（`no_infix_match`）、M10 后缀条件的行敏感规则、M11 `--lint` 恒 0。7 条冗余：R1 八组关键字别名、R2 后置 `with` 子句**四段代码不可达**（`src/parser/parser.c:1690`/`:1692`/`:1693`/`:1722` 用 `peek(p).type == TOK_IDENT && sv_eq_cstr(text,"with")` 判断，而 `with` 是 `TOK_WITH`）、R3 `src/parser/parser.c:1286`/`:1287` 是逐字相同的一行、R4 十六进制有两条扫描路径、R5 约 30 个 GUI 关键字没有自己的语法（只有 `parse_gui_stmt` 的「原文当字符串」机制）、R6 `TOK_UNKNOWN` 无人处理、R7 完全没有按位运算符。3 条卫生：H1 14 个受版本控制文件含 U+FFFD（C 注释是损坏的 GBK；最坏的 `src/mod/gui_mod.c.bak2_20260808_221050` 1485 已随该文件删除，见 §10.48 —— 删除后复核**仍是 14 个**，因为原表漏记的 `docs/AUDIT.md`（2）补上了，现在最坏的是 `mods/debug/debug_mod.c` 619、`src/compiler/bytecode.c` 409…）、H2 上述 `.bak2_` 备份文件被入库（**已修，见 §10.48**）、H3 `ai_browser_diag.js` 是唯一**非法 UTF-8** 文件（偏移 478）。

**两条方法论结论（对回归测试有直接影响）。** ①引擎在程序输出前固定打印三行模块装载信息（`[TBP] timeBeginPeriod(1) …`、`[infiverse mod] loaded …`、`[verse_dist mod] VDP loaded …`）与一行调用回显（形如 `[0]="str" [1]="len"`），**任何断言 stdout 的测试都必须容忍这些前缀行**。②**退出码经常区分不出对错** —— D1、D2、D5、M11 都返回 0；`--lint` 尤其不可用作解析谓词（`src/main.c:1252-1257` 的 `lint_check()` 恒返回 0）。

### 10.19 `?.` 安全成员访问恒为 `nil`：两个独立缺陷，以及一条「有牙的断言」的写法（`optional-member-safe-access`）

**起因。** §10.18 的 D11 是**只靠读码**写下的：`?.` 实测求值为 `nil`，文档如实记了现象，并明写「具体失效点尚未定位」。本条把它定位到行、修好，并把 `.`（非 `?.`）与它分开。

**根因是两个独立缺陷，同在 `src/compiler/compiler.c` 的 `EXPR_MEMBER` safe 分支。**

①**键截断**：原文 `src/compiler/compiler.c:1047` 是 `int key_idx = bytecode_add_string(comp->curBC, expr->member.member.start);`。`expr->member.member` 是 `StringView`（`start` + `length`，指向源缓冲区内部，**不以 `\0` 结尾**），而 `src/compiler/bytecode.c:66` 的 `int bytecode_add_string(Bytecode *bc, const char *str)` 内部用 **`strcmp` 查重 + `strdup` 复制**，两者都是 NUL 终止语义。于是 `obj?.name` 的查找键不是 `"name"`，而是 **`"name"` 加上源文件剩下的一切**，`OP_INDEX_GET` 必然查不到 ⇒ `nil`。**这条缺陷留下过一个可识别的指纹**：引擎回显里那个 `[2]="name\nsay present\n\nmissing = nil?.name\nsay missing\n"` —— 当时看着莫名其妙，其实就是被 `strdup` 读走的「成员名 + 文件剩余部分」。

②**陈旧寄存器**：对象为 `nil` 时 `OP_JUMP_IF_TRUE` 跳过写 `result`，而 `result` 来自 `alloc_reg()` 的回收寄存器，**保留上一次的值**。所以只修①，`nil?.name` 会返回上一次安全访问产生的 `inimerse`。

**修复。** 键按视图长度复制（非 `safe` 的 `.` 分支本来就是这么写的，**同一函数里一个对一个错**，正是这个疏漏的旁证）：

```c
char keyName[256];
snprintf(keyName, sizeof(keyName), "%.*s",
         (int)expr->member.member.length, expr->member.member.start);
int key_idx = bytecode_add_string(comp->curBC, keyName);
```

并在 `OP_IS_NIL` 之前把 `result` 兜底为对象自身：

```c
int result = alloc_reg();
emit(comp->curBC, OP_MOV, result, object, 0);
```

选兜底而不是「载入 nil」，是因为引擎**没有专用 nil 载入操作码**：`src/compiler/bytecode.h` 里 NIL 相关只有 `OP_IS_NIL`，`grep -arn 'LOADK_NIL\|LOAD_NIL' src/compiler/ src/vm/` 零命中，也没有 `EXPR_NIL`。

**同类审计**：`grep -an 'bytecode_add_string([^)]*\.\(start\|text\)' src/compiler/*.c src/vm/*.c` **零命中** —— 这是孤例，不是一类错误。

**回归断言：一条「有牙」的断言长什么样。** 本条的教训值得单独记，因为这个项目刚刚栽在相反的地方（M13：101 个 CTest 里 73 个只看退出码）。第一版设想是 `PASS_REGULAR_EXPRESSION "inimerse"` —— **它在缺陷还在时也会通过**，因为引擎的内建调用回显会打印字典字面量，输出里**本来就有裸的 `inimerse`**。所以 `vtest/optional_member_v04.im` 重写成打印带前缀的**值行**：

```
obj = {"name": "inimerse"}
present = obj?.name
missing = nil?.name
say "OM present=" + present
say "OM missing=" + missing
```

`CMakeLists.txt:544-550` 随之加两条断言，**各抓一个缺陷**：`PASS_REGULAR_EXPRESSION "OM present=inimerse"`（抓①键截断）、`FAIL_REGULAR_EXPRESSION "OM missing=inimerse"`（抓②陈旧寄存器）。

**双向验证做了两次，不是一次** —— 因为两条正则各自声称抓一个缺陷，只验「整个修复还原后会红」证明不了第二条：

| 编译器状态 | 程序输出 | 判决 |
|---|---|---|
| 还原到 HEAD（两缺陷都在） | `OM present=nil` / `OM missing=nil` | **Failed** — `Required regular expression not found. Regex=[OM present=inimerse]` |
| 只保留缺陷②（删掉那行 `OP_MOV`） | `OM present=inimerse` / `OM missing=inimerse` | **Failed** — `Error regular expression found in output. Regex=[OM missing=inimerse]` |
| 两处修复都在 | `OM present=inimerse` / `OM missing=nil` | **Passed** |

第二行是关键：`PASS` 正则**已经满足**，是 `FAIL` 正则把它拦下来的 —— 证明缺陷②被独立捕获，不是靠 `PASS` 顺带盖住。

**可复用的模式**：**当引擎回显会污染输出时，断言必须打在程序自己打印的带标记值行上**，不能打在裸值上。

**`.`（非 `?.`）不是缺陷。** `src/compiler/compiler.c:1128-1159` 对 `EXPR_IDENT` 对象把 `"d.a"` 拼成字符串，`lookup_local(comp, "d.a")` 失败后调 `register_global(comp, full)` 再 `OP_LOAD_GLOBAL` —— **读的是一个名叫 `"d.a"` 的全局**（`u.count` 这类模块限定名的设计用途）。源码注释写着「读取不创建全局」，但代码调用的正是 `register_global()`。所以对字典用 `.` 会**静默读到 `nil`**，是 D 级危险行为，但**不是成员访问实现缺失**。`docs/SYNTAX.md` §7.1 已把 D11 拆成 (a) `?.`（已修复）与 (b) `.`（设计使然）两部分。

**顺带修掉一处杂散改动**：`git diff` 显示 `src/compiler/compiler.c:1` 的 UTF-8 BOM 被之前的编辑剥掉了，不在修复意图内，已补回，使该文件的 diff 只剩两处修复。

**门禁**：本轮**不新增 CTest**，`EXP_CTEST` 保持 **101**；改动落在 `src/compiler/compiler.c`、`vtest/optional_member_v04.im`、`CMakeLists.txt`，文档落在 `docs/SYNTAX.md`（D11 与四处交叉引用）、`docs/BOARD.md`（新行 `optional-member-safe-access` + `ctest-assertion-gap` 标记首个实例）、`docs/STATUS.md`（本节）。

### 10.20 一条说谎的文件名，以及「有牙的断言」在闭包上的三种打断（`lambda-test-assertions`）

**起因。** §10.19 补完第一条断言后，`ctest-assertion-gap` 的下一批是语言行为类的 `*_runtime`。lambda 三件套排在最前，因为其中一条**一眼就能看出问题**：`lambda_capture_runtime` 跑的脚本叫 `vtest/lambda_capture_rejected_v04.im`。

**文件名在说谎，测试内容没错。** `git log` 把来历交代得很清楚：`69e7fa0`（2026-09-01，「Add regression gate for rejected lambda captures」）建这个文件时，首行注释是 `# Until closure environments land, outer-local captures must be rejected.`；`bd69935`（2026-09-03，「Execute captured lambdas in VM」，9 文件）实现了闭包捕获，把**注释改写成一段正常成功的用例**，却**没有改文件名**。于是文件名一直声称一个已经不存在的契约，而 CTest 条目既没有 `PASS_REGULAR_EXPRESSION` 也没有 `WILL_FAIL` —— 两边都不说实话。**定性结论：不是测试写错，是文件名写错。** 已 `git mv` 为 `vtest/lambda_capture_v04.im`（`git mv` 是执行了的，但内容从 6 行重写到 30 行，`git diff -M` 相似度低于 50%，所以提交里呈现为 delete + create，不是 `R`）。

**三条断言。** 先用单条 `.*` 正则，而不是 `;` 分隔的列表 —— CMake 的 `PASS_REGULAR_EXPRESSION` 列表语义是**任一匹配即通过**，写成列表反而比一条正则更弱。仓库里既有用法可以佐证 `.*` 能跨行匹配（`CMakeLists.txt:607` 的 `"30.*20"`、`:617` 的 `"12.*7.*closure-throw-caught"`）。

| 测试 | 新断言 |
|---|---|
| `lambda_runtime` | `"lambda double=42.*lambda sum2=5"` |
| `lambda_capture_runtime` | `"lambda capture2=5.*lambda capture10=13.*lambda local=8"` |
| `lambda_nested_runtime` | `"lambda nested=9"`（原来是裸 `"9"`） |

三个 `.im` 相应重写为打印**带标记的值行**（`say "lambda double=" + str(double(21))`）。**标记是必需的**：引擎回显 `[0]="lambda double=" [1]="str" [2]="lambda sum2="`，裸的 `42` / `5` / `9` 既可能与回显碰撞，`9` 还可能匹配到任何更大的数字里。顺手把捕获的覆盖面也加深了 —— 新增两个独立闭包（`add2` / `add10`，各自保留自己的捕获值）和一处**局部变量（非参数）捕获**（`make_bump`），因为原来的用例只覆盖了参数捕获。

**双向验证：三种打断。** 每次打断都 `cp` 还原 `src/vm/vm.c` 并 `cmake --build build -j12` 重建，确认回绿后再做下一种。

| 打断 | `lambda_runtime` | `lambda_capture_runtime` | `lambda_nested_runtime` |
|---|---|---|---|
| `L_MAKE_FUNC` 里 `im_closure_env_new((size_t)ins.r3)` → `im_closure_env_new(0)`（所有捕获读回 `nil`） | Passed | **Failed** | **Failed** |
| `im_closure_function_new(ins.r2, env)` → `ins.r2 + 1`（闭包自己的 `function_index` 偏一） | **Failed** | **Failed** | **Failed** |
| `value_set(&R[ins.r1], VAL_FUNCTION, ins.r2, 0, NULL, fn)` → `ins.r2 + 1` | Passed | Passed | Passed |

第一行里 `lambda_runtime` 保持绿色是**正确**的：它的两个 lambda（`x -> x * 2`、`(a, b) -> a + b`）都不捕获外层变量，本来就不该被闭包环境的破坏波及 —— 这正好说明三条断言各自的射程是分开的。

**③ 牵出一条观察：`Value.ival` 在函数值上没人读。** 第三行打断把 `OP_MAKE_FUNC` 写进 `Value.ival` 的函数下标故意写错一格，**三个测试全绿**。查下去：`src/vm/vm.c:3632` 的 `L_CALL_VALUE` 派发用的是 `im_closure_function_index(im_closure_from_value(&R[ins.r1]))`，也就是闭包对象自己的 `fn->function_index`，从不看 `Value.ival`；`src/vm/closure.c` 里唯一的 `ival` 读取是 `v->type == VAL_STRING && v->ival != 1`（静态字符串标志）。所以 `OP_MAKE_FUNC` 往 `Value.ival` 写的那一格是**没人读的**。这里只宣称「对派发无效，已用『写错也不产生任何可观测差异』实证」，**不宣称该字段在整个引擎里全无用途**。

**可复用的模式（补 §10.19 那条）。** §10.19 的结论是「当引擎回显会污染输出时，断言必须打在程序自己打印的带标记值行上」。本条补上后半句：**断言还要分开射程** —— 一条只覆盖它真正依赖的机制（`lambda_runtime` 不含捕获，就不该被捕获的破坏带红），否则双向验证时无法判断新断言抓的是哪一个缺陷。

**门禁**：本批**不新增 CTest**（只加属性），`EXP_CTEST` 保持 **101**；`ctest-assertion-gap` 的剩余数从 72 更新为 **71**。

### 10.21 Result 三件套与 `try/finally`：四条断言，以及「FAIL 断言不能用字符串字面量」（`result-finally-assertions`）

**起因。** §10.20 之后继续按 `ctest-assertion-gap` 的清单往下走，这一批是 Result 三件套（`result_runtime`、`result_question_runtime`、`result_propagation_runtime`）与 `try_finally_runtime`。四个脚本都重写成打印带标记的**值行**：

| 脚本 | 新输出 |
|---|---|
| `vtest/result_v04.im` | `result is_ok=true` / `result unwrap_or=42` / `result is_ok_err=false` / `result unwrap_or_err=99` / `result unwrap=42` |
| `vtest/result_question_v04.im` | `resultq value=42` / `resultq caught` / `resultq reached=0` |
| `vtest/result_propagation_v04.im` | `resultprop unwrapped=7` / `resultprop err_is_ok=false` / `resultprop err=inner` |
| `vtest/try_finally_v04.im` | `finally normal=101` / `finally nested=100` / `finally nested err=rethrow-me` / `finally caught=1110` |

**一次失败的写法，就地推翻。** `result_question_runtime` 第一版是这样的：脚本里 `?` 传播之后留一句 `say "resultq unreachable"`，测试加 `FAIL_REGULAR_EXPRESSION "resultq unreachable"` —— 想法是「这句话不该被执行，所以它不该出现在输出里」。**跑出来是红的，而程序其实是对的**：引擎会把程序里**每一个字符串字面量**回显出来，回显行里赫然写着 `[6]="resultq unreachable"`。也就是说这条 FAIL 断言**在语句真的没跑时也会命中**，它断言的是「这个字面量不存在」，而不是「这句话没执行」——**是空的**。

改法不是绕开，而是换一个不可被回显的东西当证据：让未执行的分支只改一个**计数变量**。

```
reached = 0
try {
  bad = err("failure")?
  reached = 1
} catch (ex) {
  say "resultq caught"
}
say "resultq reached=" + str(reached)
```

断言变成 `PASS_REGULAR_EXPRESSION "resultq value=42.*resultq caught.*resultq reached=0"` —— 传播一旦失效，`reached` 就是 1，正则匹配不上。

**这条把 §10.19 / §10.20 的结论往前推了一步**：不只是「断言要打在程序自己打印的值行上」，而是**字符串字面量本身也会被回显，所以 `FAIL_REGULAR_EXPRESSION` 不能拿字面量当靶子**。要断言「某件事没发生」，就得让「它发生了」留下一个**值**上的痕迹（计数、状态、标记），而不是留下一段**文本**。

**双向验证：三种打断，各自只打红该打的那一个。**

| 打断 | `result_runtime` | `result_question_runtime` | `result_propagation_runtime` | `try_finally_runtime` |
|---|---|---|---|---|
| ① `src/mod/result_mod.c:29` 的 `result_is_ok` 改成恒真 | **Failed** | Passed | **Failed** | Passed |
| ② `src/compiler/compiler.c:1241` 顶层 `?` 由 `unwrap` 改成 `result_value`（Err 不再抛出） | Passed | **Failed** | Passed | Passed |
| ③ `src/compiler/compiler.c:2232` 的 `for (int i = 0; i < stmt->tryStmt.finallyCount; i++)` 改成 `i < 0`（正常路径不再编译 finally 体） | Passed | Passed | Passed | **Failed** |

第①行里 `result_runtime` 与 `result_propagation_runtime` 一起红是**应该的** —— 两者都直接用 `is_ok`；第②③行各自只打红一条，说明这两条断言的射程是分开的。每次打断后都从 `/tmp/e13/` 的原始副本还原并 `cmake --build build -j12` 重建，确认 `git diff --numstat` 归零。

**门禁**：本批**不新增 CTest**，`EXP_CTEST` 保持 **101**；`ctest-assertion-gap` 的剩余数从 71 更新为 **67**（`*_runtime` 类还剩 11 个）。

### 10.22 `case` 的 `as` 别名恒绑 `nil`：一个不可达的 `OP_STORE_GLOBAL`（`case-alias-binding`）

**起因。** 按 `ctest-assertion-gap` 的清单给 `case_alias_runtime` 补断言时，先读它跑的脚本 `vtest/case_alias_v04.im`（5 行）：

```
value = 42
case value {
    42 as whole: say whole
    _: say "miss"
}
```

**实测输出 `nil`，而不是 `42`。** 注意分支是**匹配上了**的（没走 `_` 那条），所以问题不在匹配、而在**绑定**。

**语法取证。** `src/parser/parser.c:1047-1048` 是 `if (match(p, TOK_AS)) { br->alias = consume(p, TOK_IDENT, "alias name").text; br->hasAlias = true; }`，**紧随其后**才是 `if (match(p, TOK_PIPE))` 守卫 ⇒ 顺序固定为 `pattern as name | guard:`。`br->alias` 在 `src/compiler/compiler.c` 里**只有一处**引用，就是下面那个坏块。

**四种模式全中**（修复前，探测脚本 `/tmp/e13/alias_probe2.im`）：

| 用例 | 修复前 | 修复后 |
|---|---|---|
| `42 as whole:` | `A whole=nil` | `A whole=42` |
| `in [40~50] as w5:` | `E w5=nil` | `E w5=42` |
| `42 as g \| g > 0:` | `G g=nil` | `G g=42` |
| `42 as g2 \| g2 > 100:` | `H miss` | `H miss`（正确不命中） |

**根因：一个在所有路径上都不可达的 `OP_STORE_GLOBAL`。** 修复前 `src/compiler/compiler.c` 的 case 分支编译里是：

```c
if (br->hasAlias) {
    char alias[256];
    snprintf(alias, sizeof(alias), "%.*s", (int)br->alias.length, br->alias.start);
    emit(comp->curBC, OP_STORE_GLOBAL, register_global(comp, alias), subj, 0);
    body_start = comp->curBC->count;
}
```

这一块**排在守卫块之后**，而且把 `body_start` **推进到了 store 之后**。可是匹配成功的跳转（`body_jumps`，在 `if (!br->guard)` 分支里 patch）与守卫为真的跳转（`guard_true`）**都被 patch 到 `body_start`** —— 于是两条路都落在 store **之后**，那个 `OP_STORE_GLOBAL` 从来没有被执行过。`as` 恒为 `nil` 是必然结果。

**修复：把绑定提到守卫之前，并让它成为分支入口。** 三处改动（`src/compiler/compiler.c`）：

1. 在 `if (br->guard) {` **之前**插入 `int entry_pc = -1;` 与新的别名块 —— `entry_pc = comp->curBC->count;` 后发 store，并把 `body_jumps` **改指 `entry_pc`**；
2. 守卫块里原来那句无条件 `for (… ) comp->curBC->code[body_jumps[ji]].r2 = guard_start;` 改成 **只在没有别名时**执行（`if (entry_pc < 0)`）；
3. `if (!br->guard)` 里的 patch 改成 `entry_pc >= 0 ? entry_pc : body_start`；原来那个排在守卫之后的旧块**删掉**。

为什么是这个形状：这是唯一同时满足「别名在**分支体**里可见」与「别名在**守卫**里可见」的位置 —— 守卫必须先看到绑定，所以 store 必须在守卫之前；而匹配跳转必须落在 store 上，所以 store 必须就是分支入口。**附带修好了一个此前无法表达的写法**：`42 as g | g > 0` 现在成立（命中），`42 as g2 | g2 > 100` 正确不命中。

**回归：一条有牙的断言。** `vtest/case_alias_v04.im` 重写为四段 `case`、输出四行带 `alias ` 前缀的**值行**：

```
alias whole=42
alias ranged=42
alias guarded=42
alias guarded2-miss
```

`case_alias_runtime`（`CMakeLists.txt:607-608`）补上 `PASS_REGULAR_EXPRESSION "alias whole=42.*alias ranged=42.*alias guarded=42.*alias guarded2-miss"`。

**标记在这里同样是必需的，不是装饰。** 引擎的内建调用回显会打印脚本里每一个字符串字面量：`[0]="alias whole=" [1]="str" [2]="alias whole-miss" [3]="R" [4]="alias ranged=" …`。也就是说输出里**本来就有 `alias whole=`**，用裸 `42` 之类的正则会被回显满足，坏掉时也会通过 —— 与 §10.19、§10.21 是同一个坑的第三次出现。

**双向验证。** 把 `src/compiler/compiler.c` 还原成修复前那份（`/tmp/e13/compiler.c.casealias.orig`）并重建：

```
1/1 Test #86: case_alias_runtime ...............***Failed
  Required regular expression not found. Regex=[alias whole=42.*alias ranged=42.*alias guarded=42.*alias guarded2-miss
程序输出：alias whole=nil / alias ranged=nil / alias guarded-miss / alias guarded2-miss
```

修复版回到 **#86 Passed**，全量 `ctest --test-dir build` → `100% tests passed, 0 tests failed out of 101`（其中 21 条 `case|finally|result` 相关全绿）。

**一条顺带说清的语义**：`as` **只做绑定，不做匹配**。`n as whole2` 里的 `n` 是**裸标识符模式**，语义是值比较（`42 == nil` 不匹配，所以走 `_`），不是「把 subject 绑到 `n`」。这个区分在修复前后都一样。

**门禁**：本批**不新增 CTest**，`EXP_CTEST` 保持 **101**；`ctest-assertion-gap` 的剩余数从 67 更新为 **66**（`*_runtime` 类还剩 **10** 个）。

### 10.23 `*_runtime` 第二批八条：两种「没有牙」的写法，以及「回显行」能整条满足正则（`language-behaviour-assertions`）

承 §10.19/§10.21/§10.22 的 `ctest-assertion-gap`。本批给八个只断言退出码的 `*_runtime` 测试补上带标记的值行与 `PASS_REGULAR_EXPRESSION`，**八条全部双向验证通过**。但其中两条的**初版**被证明没有牙，而且两个根因不一样 —— 这两条教训比八条断言本身更值钱。

**八条断言**（均为单条 `.*` 正则，理由见 `docs/SYNTAX.md` §8 第 5 条）：

| 测试 | 断言 |
|---|---|
| `type_collection_runtime` | `typecoll x=42` |
| `pipeline_runtime` | `pipe one=5.*pipe two=4.*pipe arg=5.*pipe order=1` |
| `null_coalesce_runtime` | `coalesce nil=42.*coalesce some=7` |
| `chained_comparison_runtime` | `chain inside-hit.*chain hits=1 end` |
| `postfix_condition_runtime` | `postfix if-true.*postfix say-if.*postfix unless-block.*postfix fired=0` |
| `case_collection_patterns_runtime` | `coll positive-type=42.*coll range-hit=7.*coll wildcard-hit=999` |
| `case_structural_runtime` | `struct record-hit=1.*struct nested-hit=2.*struct deep-hit=3.*struct bind=not_found:1.*struct missing-safe=5` |
| `float_precision_runtime` | `float value=1.2345678901234567` |

**顺带加深了 `pipeline` 的覆盖**：原来的 `3 |> add(2)` 里 `add` 是可交换的，把前插改成后插也得出同一个数，**测不出插在哪一头**。改成非交换的 `func sub(x, y) { return x - y }` 与 `3 |> sub(2)`（`pipe order=1`）后，前插改后插会打印 `pipe order=-1`。

**没有牙的写法 ①：正则匹配的是前缀。** `chained_comparison_runtime` 初版断言 `chain hits=1`。把 `src/compiler/compiler.c:828` 的 `else emit(comp->curBC, OP_AND, result, result, cmp);` 改成 `OP_OR` 后，程序确实打印 `chain inside-hit` / `chain outside-hit` / `chain hits=101` —— 可 `chain hits=101` **以 `chain hits=1` 开头**，正则照样命中，测试仍然是绿的。标记改成 `chain hits=1 end` 后，同一个打断变红。

**没有牙的写法 ②：全部由字面量组成的标记，会被回显行整条满足。** 这是 §10.19/§10.21 那个坑的加强版：引擎把程序里**所有**字符串字面量回显成**同一行**，所以一条跨多个纯字面量标记的正则会被那一行整体命中。`case_collection_patterns_runtime` 初版断言 `coll positive-type.*coll range-hit.*coll wildcard-hit`，而回显行是

```
[0]="R" [1]="coll positive-type=" [2]="str" [3]="coll other=" [4]="coll range-hit=" [5]="coll range-other=" [6]="coll bad-positive=" [7]="coll wildcard-hit="
```

三个标记全在里面。实测两次打断，**程序行为确实被破坏，CTest 仍然 Passed**：

- 把 `src/compiler/compiler.c:1682` 的 `emit(comp->curBC, OP_IN, tmp, subj, pat);` 换成 `pat, subj`（成员测试实参对调）⇒ 输出从 `coll positive-type` / `coll range-hit` 变成 `coll other=0` / `coll range-other=0`，**测试仍 Passed**；
- 把 `src/compiler/compiler.c:1623` 的 `emit(comp->curBC, OP_EQ, eq, got, expected);` 换成 `OP_NEQ`（dict 字段比较取反）⇒ 第一行从 `struct record-hit` 变成 `struct bad`，**测试仍 Passed**。

**修法：每个标记后面接一个计算值**（`say "coll positive-type=" + str(42)`），使「标记+值」这个串**只可能出现在程序自己的输出里**（回显行里只有 `[1]="coll positive-type="` 与 `[2]="str"`，没有 `coll positive-type=42`）。改后同样的两次打断都变成 `Required regular expression not found`：

```
1/1 Test #84: case_collection_patterns_runtime ...***Failed
  Required regular expression not found. Regex=[coll positive-type=42.*coll range-hit=7.*coll wildcard-hit=999
1/1 Test #85: case_structural_runtime ..........***Failed
  Required regular expression not found. Regex=[struct record-hit=1.*struct nested-hit=2.*struct deep-hit=3.*struct bind=not_found:1.*struct missing-safe=5
```

**八条打断点与结果**（每条：打断 → 重建 → 确认该条变红 → 还原）：

| 测试 | 打断点 | 结果 |
|---|---|---|
| `null_coalesce` | `src/compiler/compiler.c:639` `OP_JUMP_IF_FALSE`→`OP_JUMP_IF_TRUE` | RED |
| `pipeline` | `src/parser/parser.c` 的 `\|>` 分支：前插 `args[0]` 改后插 `args[argCount]` | RED |
| `type_collection` | `src/compiler/compiler.c:2205` `OP_BE, g, setReg, initReg`→`…, -1` | RED |
| `chained_comparison` | `src/compiler/compiler.c:828` `OP_AND`→`OP_OR` | RED |
| `postfix_condition` | `src/parser/parser.c:1662` `int invert = (peek(p).type == TOK_UNLESS);`→`= 0;`（**该行出现 2 次，须替换第 1 次**） | RED |
| `float_precision` | `src/runtime/runtime_posix.c:59` `"%.17g"`→`"%.2f"` | RED |
| `case_collection_patterns` | `src/compiler/compiler.c:1682` `OP_IN` 实参对调 | RED |
| `case_structural` | `src/compiler/compiler.c:1623` `OP_EQ`→`OP_NEQ` | RED |

**打断时踩到的两条源码事实**（都不是本批的缺陷，但会让打断脚本静默失效）：

- **`OP_NE` 不存在。** `src/compiler/bytecode.h:11` 是 `OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE,`，另有 `:37` 的 `OP_NEQK`。写 `OP_NE` 会编译失败（`BUILD-FAIL`），而不是让测试变红。
- **`str` 被注册两次，后者覆盖前者。** `src/runtime/runtime.c:1810` 的 `vm_register_builtin(vm, "str", builtin_str);` 被 `src/runtime/runtime_posix.c:1032` 的 `vm_register_builtin(vm, "str", posix_core_str);` 覆盖。所以改 `src/runtime/runtime.c:67` 的 `builtin_str` **完全不生效**，`float_precision` 必须打断 `src/runtime/runtime_posix.c:59`。

**门禁**：本批**不新增 CTest**，`EXP_CTEST` 保持 **101**；`ctest-assertion-gap` 的剩余数从 66 更新为 **58**（`*_runtime` 类从 10 降到 **2**，只剩 `eidos_desugar_runtime` 与 `eidos_runtime`，它们是 Python 驱动的脚本测试、脚本内部自带 `assert`）。全量 `ctest --test-dir build` → `100% tests passed, 0 tests failed out of 101`。

### 10.24 `ctest-assertion-gap` 结案：58 个的分类，以及六个「在 Release 里空转」的探针（`vacuous-probe-assertions`）

§10.19 → §10.23 把 `*_runtime` 这一类补完了。本节处理剩下的 58 个只断言退出码的 CTest：**先分类，再修其中真正坏掉的那一类。**

**分类结果：58 个里没有一个是由 `inimerse` 直接跑 `.im` 的。** 用 `ctest --show-only=json-v1` 取每条测试的 `command`，按第一个词分类：

| 驱动 | 条数 | 退出码是否是它的契约 |
|---|---|---|
| C 探针二进制（`*_probe`、`*_crosscheck`） | 27 | 是 —— 源码里逐条 `return 27/28/…` 或 `return g_failures == 0 ? 0 : 1;` |
| `python3` 驱动（`*_regression`、`*_diagnostic` 等） | 25 | 是 —— 失败路径 `return 1` / `raise SystemExit(...)` |
| `node` 驱动（`protocol_regression`、`verse_*_crosscheck`、`wasm_*`） | 6 | 是 |
| `cmake -P`（`verse_closed_loop`、`verse_crp_closed_loop`） | 2 | 是 |
| `bash`（`http_probe`） | 1 | 是 |

**所以「只断言退出码」对它们不是缺陷，退出码就是契约** —— 与 `*_runtime` 的区别在于：`inimerse` 跑一段 `.im` 后无论程序打印什么都返回 0，而探针与驱动的返回值是它们自己算出来的判决。分类依据可复现：`crp_session_probe.c` 每个检查点返回不同非零码（`return 27` … `return 45`），`tools/finally_control_flow.test.py:23` 是 `return 1`，`tools/xlang_python_bridge.test.py:73` 是 `sys.exit(77)`。

**但分类过程中抓到一类真缺陷：六个探针在 Release 门禁里什么都不检查。** 它们用 `assert()` 表达**全部**期望，而门禁的构建带 `-DNDEBUG`：

```
CMAKE_BUILD_TYPE=Release
CMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG
build/CMakeFiles/enum_probe.dir/flags.make:  C_FLAGS = -O3 -DNDEBUG -std=gnu11
nm -C build/enum_probe | grep -c '__assert_fail'   →  0
```

`assert()` 被预处理掉，六个可执行文件里**一个 `__assert_fail` 符号都没有**，无论引擎行为对错都 `return 0`。共 **107 条断言**：

| 探针 | 断言数 |
|---|---|
| `src/types/enum_probe.c` | 36 |
| `src/types/typeset_probe.c` | 21 |
| `src/vm/closure_probe.c` | 20 |
| `src/types/error_types_probe.c` | 14 |
| `src/compiler/bytecode_capture_probe.c` | 9 |
| `src/types/registry_probe.c` | 7 |

**实证（修前）**：往 `src/types/registry_probe.c` 的 `main` 开头插一句 `assert(1 == 2);` 并重建 ——

```
1/1 Test #17: type_registry_probe ...............   Passed
100% tests passed, 0 tests failed out of 1
```

**修法**（`CMakeLists.txt`，紧随 `add_test(NAME type_registry_probe …)`）：

```cmake
foreach(_asserting_probe
    bytecode_capture_probe closure_probe typeset_probe
    enum_probe error_types_probe type_registry_probe)
  target_compile_options(${_asserting_probe} PRIVATE -UNDEBUG)
endforeach()
```

`-UNDEBUG` 必须**排在 `-DNDEBUG` 之后**才会生效；`target_compile_options` 正是追加到 `C_FLAGS` 尾部（修后 `C_FLAGS = -O3 -DNDEBUG -std=gnu11 -UNDEBUG`），而 `target_compile_definitions` 只能继续加宏、撤不掉已经生效的。

**实证（修后）**：六个二进制各含 **1** 个 `__assert_fail`；同一句 `assert(1 == 2);` 立刻让测试变红 ——

```
1/1 Test #17: type_registry_probe ...............***Failed  (Subprocess aborted)
```

删掉那句后 `ctest -R 'bytecode_capture_probe|closure_probe|typeset_probe|enum_probe|error_types_probe|type_registry_probe'` → `100% tests passed, 0 tests failed out of 6`，`git diff --numstat src/` 为空（探针源码**一行未改**，只改了构建选项）。

**穷尽性检查（确保没有第七个）**：对全部 `git ls-files '*.c'` 里「含 `int main` 且含 `assert(`」的文件，统计「除 `assert` 之外还有没有返回非零的路径」，只有这六个是 `nonzero_paths=0`；`src/verse/crp_probe.c`（13 条非零路径）与 `src/verse/json_min_probe.c`（19 条）虽然各含 1 处 `assert` 字样，但判决走自己的计数器，不受影响。

**结案**：`ctest-assertion-gap` 的剩余数 **58 → 0**。73 个无输出断言的 CTest 全部归类：17 个补上了有牙的 `PASS_REGULAR_EXPRESSION`（§10.19–§10.23），6 个探针恢复了在 Release 下真正生效的断言（本节），其余 50 个的退出码经源码取证确认就是它们的契约。**本批不新增 CTest，`EXP_CTEST` 保持 101。**

### 10.25 自举工具链「零输出」的两个根因：一个被 `posix_unsupported` 顶着的注册点，和一条缺失的隐式 `OP_RETURN`（`selfhost-codegen-empty`）

BOARD 行 `selfhost-codegen-empty` 的记录停在「`--dump` 能出 120 条指令，但真跑一行程序输出都没有」，并判断「已不是 push 误编译」。本节给出**第二层与第三层原因**：它们都在该行开工前就存在（`selfhost/` 与 0.2.0 发布版 `8248e08` 逐字节相同，`git diff --stat 8248e08 HEAD -- selfhost/` 为空），属**一直没被测过的既有缺陷**，不是新回归。

#### 根因 A：自举路径在 POSIX 上根本没有被执行

`vm_exec` 这个内建把一段已编译的 `Bytecode *` 交给 VM 执行，是自举工具链的唯一入口。它的实现（`bc_from_data` 静态辅助 + `builtin_vm_exec`）原本只写在 `src/runtime/runtime.c` 里，并**只**由该文件自己的 `runtime_init` 注册。而 Linux 门禁走的是 `src/runtime/runtime_posix.c`，它在同名位置注册的是 `posix_unsupported`：

```c
vm_register_builtin_full(vm, "vm_exec", posix_unsupported, 1 | CAP_DBG | CAP_PROC, 0);
```

于是自举路径调 `vm_exec` 恒得 `-1` —— 编译出来的字节码**从来没被执行过**。这解释了一个此前无法解释的现象：`--dump`（纯前端，不经 `vm_exec`）能出 120 条指令，而真跑一行输出都没有，且**两条路径的退出码都是 0**。

探针实测：`vm_exec_rc` 修前 **-1**、修后 **1**。

#### 根因 B：自举编译器缺函数体末尾的隐式 `OP_RETURN`

C 编译器在函数体编译完后无条件补一条返回：

```c
/* 末尾默认返回 nil */
emit(comp->curBC, OP_RETURN, 0, 0, 0);
```

（`src/compiler/compiler.c:378-379`）。`selfhost/compiler.im` 的 `compile_program` 函数体循环之后**什么都没有**。少了这一条，函数体执行完就直接跑出 `code` 数组，**调用点之后的语句被静默丢弃** —— 这比「返回垃圾值」更难发现，因为它只吃掉调用者后续的输出。

实证（`func g() { say "inner" }` / `g()` / `say "after"` / `stop all`）：

| | `func:g:0` 指令 | 程序输出 |
|---|---|---|
| 修前 | 2 条（`3,1,0,0` / `30,1,0,0`） | 只有 `inner` |
| 修后 | 3 条（多 `35,0,0,0`） | `inner` + `after` |

#### 修复

- 新增 `src/runtime/vm_exec_builtin.c`：把 `bc_from_data` 与 `im_builtin_vm_exec` 从 `runtime.c` **逐字搬过来**，使两个 runtime 都能链接到同一份实现（避免复制两份后各自漂移）。
- `src/runtime/runtime.h` 加声明；`src/runtime/runtime.c` 删掉旧的两段并把注册改为 `im_builtin_vm_exec`；`src/runtime/runtime_posix.c:1064` 的 `posix_unsupported` → `im_builtin_vm_exec`。
- `CMakeLists.txt` 把新文件加进 `INIMERSE_ENGINE_SOURCES`。
- `selfhost/compiler.im:759` 补 `emit(fctx, OP_RETURN, 0, 0, 0)`，带三行中文注释指向 `compiler.c:379`。

#### 判据②：两条路径的程序输出逐字节一致

`./build/inimerse selfhost/compiler.im selfhost/test1.im` 与 `./build/inimerse selfhost/test1.im` 的输出**逐字节相同**（滤掉引擎固定打印的三行模块装载前言与 `[0]=…` 调用回显后各 63 B，输出哈希 `2f13eb753c50`）。

#### 判据③：产物对照工具 `tools/selfhost_compare.py`

对每个目标跑四条命令 —— C 运行 / 自举运行 / `inimerse bytecode <目标>` / `compiler.im --dump <目标>` —— 记录**运行输出哈希对**与**规范化字节码哈希对**（`ops_only()` 只取每条指令的 opcode 列）。

实测 `python3 tools/selfhost_compare.py --check`：**`selfhost parity OK: 10 target(s) byte-identical, 23 skipped`，`CHECK_EXIT=0`**。逐字节相同的 10 个是 `selfhost/test1.im`、`bench_calc`、`bench_n`、`chain`、`fib15`、`h3`、`io_t4`、`prog1`、`prog2`、`rec`、`truthy_test`。

**哈希对只记录、不要求相等**：实测 C dump **129 行** vs 自举 **123 行**，寄存器分配与全局下标不同（C 用全局槽 23/24、自举用 0/1），`stop all` 在 C 是 `OP_STOP`(32) 而在自举是 `OP_THREAD_CTRL`(38)。判据原文只要求「成对记录」+「对固定源码/参数可重复」，**真正必须相等的是程序运行输出**；两个编译器行为一致而寄存器分配自由，是合理差异。

23 个跳过**每一个都打印理由**，五类：

| 类别 | 含义 | 实例 |
|---|---|---|
| `path-base` | 相对路径基准不同 | C 路径按**脚本所在目录**解析 `read_file`/`import`，自举路径继承 `compiler.im` 的目录 `selfhost/`；同一目标 `read_file("tests/prog1num.im")` 在 C 侧得 `len=0`、自举侧得 `len=15` |
| `unsupported-syntax` | 自举前端只实现**批式子集** | GUI 语法 `stage`/`sprite`/`when`/`on`/`forever`/`broadcast` 会打印 `parse error:` 到 **stdout 且 exit 0** |
| `no-output` | 库/数据夹具 | `_mini` / `libx` / `prog1min` / `prog1num` / `cat_game`，两边都不打印 |
| `gui-script` | GUI 脚本在 C 路径上死循环 | 必须**前置**判定，否则每个目标耗满一个 timeout |
| `non-hermetic` | 活的外部副作用 | `hw_test.im` 调 `exec("echo hello from inimerse")` 与 `http_get("http://example.com")`，自举侧连跑三次输出哈希是 `b09127…` / `3de1f7…` / `b09127…`，**不可重复** |

**顺带**：C 路径此前**没有** `--dump`（`--dump` 只实现在 `selfhost/compiler.im:780-793`），新增 `./build/inimerse bytecode <input.im>` 子命令（`src/main.c`，复用 `parse_program_file` + `compiler_new` + `TARGET_HOST`），输出格式与 `compiler.im --dump` 对齐 —— 没有它，判据③的「规范化字节码哈希」在 C 侧无从取得。

#### 判据④：双向验证，两个根因各自独立成立

新增 CTest `selfhost_codegen_parity`（`tools/selfhost_compare.py --check --engine $<TARGET_FILE:inimerse>`，`TIMEOUT 300`，`LABELS "compiler;selfhost;regression"`）。**不能用退出码**（两条路径都 exit 0），只断言程序输出。

| 打断 | 改什么 | 结果 |
|---|---|---|
| BREAK 1（根因 A） | `src/runtime/runtime_posix.c:1064` 改回 `posix_unsupported`，**重编** | `FAIL selfhost/test1.im  c=63B sh=0B  c_out=2f13eb753c50 sh_out=-`，`exit=1` |
| BREAK 2（根因 B） | 注释掉 `selfhost/compiler.im:759` 的 `emit`，**不需要重编**（`compiler.im` 是被解释的） | `FAIL selfhost/tests/prog2.im  c=25B sh=14B  c_out=46fb6fbf6057 sh_out=d818a31d7094`，`exit=1` |

**一条关键发现**：`selfhost/test1.im` **不覆盖**根因 B —— 它的函数全部以显式 `return` 结尾，打断 B 后它仍然绿；覆盖 B 的是 `selfhost/tests/prog2.im`（`func greet(who) { say "hi " + who }` 从末尾掉出去）。两者都在默认目标集里，CTest 因此**同时钉住两个根因**。两处打断都已用 `cp` 还原、以 `cmp` 证明逐字节相同，重编后复绿。

#### 判据⑤：门禁计数同步

`EXP_CTEST` **101 → 102**（`tools/gate.sh:49`），`docs/BOARD.md` §3 的 ctest 行同步为「**102 / 102 通过**」与 `0 tests failed out of 102`。重新 configure 后 `grep -c 'add_test(' CMakeLists.txt` = **102**、`ctest --test-dir build -N` 的 `Total Tests: 102`，`ctest -R selfhost_codegen_parity` → **`Test #32 … Passed 19.76 sec`**。

#### 一条方法论点

本行开工前的描述判断「已不是 push 误编译」是对的，但**漏了「自举路径在 POSIX 上被 `posix_unsupported` 顶着」这一层**。它与「`--dump` 出 120 条指令而真跑零输出、且两者 exit 都是 0」完全自洽：`--dump` 是纯前端路径，不经过 `vm_exec`。教训是**同一个「零输出」现象可以有两层互不遮蔽的原因**，只修一层仍会留下另一层，而退出码把两层都掩盖掉。

### 10.26 原生后端接进门禁：一条 `add_executable` 取代 stop-gap 脚本，以及 `%2147483648` 分歧的定位（`aot-native-integration`）

BOARD 行 `aot-native-integration` 是 `aot-backend` 留下的**最大残余风险**：原型（`src/compilation/aot_native.c` 636 行 + `aot_native.h` 55 行 + `aot_native_tool.c` 106 行）当时**不在任何门禁里** —— 不被 CI 编译（第一波作业单冻结了 `CMakeLists.txt`）、`tools/aot_native.test.py` 不被 CI 运行、构建靠一个 stop-gap 脚本扫 `build/CMakeFiles/inimerse.dir/**/*.o` 拼链接命令。**提交了 636 行 C，门禁一行都不会看它。**

#### stop-gap 为什么必须被取代

`tools/aot_native_build.sh` 的机制是 `find build/CMakeFiles/inimerse.dir -name '*.o' ! -name 'main.c.o' | sort`，再 `cc -O2 -std=gnu11 -Wall` 把 `aot_native.c` + `aot_native_tool.c` + 全部引擎对象链在一起（`-I` 九个 src 子目录、`-D_GNU_SOURCE -D_stricmp=strcasecmp`、`-lm -lpthread -ldl`）。三条理由使它不能留在门禁里：

1. 它要求 build 树**已经存在**，所以 `rm -rf build` 后必须由脚本自己先建引擎 —— 顺序耦合；
2. 引擎**新增一个翻译单元**时脚本不会知道，产物会静默过期（不报错、只是少一个 `.o`）；
3. 门禁从不调用它，所以它自己也**从不被验证**。

`CMakeLists.txt` 里已有现成的复用点：`:371` 的 `add_library(inimerse_engine OBJECT ${INIMERSE_ENGINE_SOURCES})`（`POSITION_INDEPENDENT_CODE ON`，且**不含 `src/main.c`**，注释在 `:367-368` 写明）。于是只加两行：

```cmake
add_executable(aot-native src/compilation/aot_native.c src/compilation/aot_native_tool.c)
target_link_libraries(aot-native PRIVATE inimerse_engine)
```

引擎新增翻译单元时 `aot-native` 自动跟着重建，顺序耦合与过期问题一并消失。`tools/aot_native_build.sh` 已 `git rm`，两处调用点（`tools/aot_native.test.py` 的 prerequisites 与找不到二进制时的错误提示、`tools/aot_native_bench.py:27` 与 `:132`）同步改写为 `cmake --build build`。

#### 判据②：注册进 CTest，并让数量成为断言

```cmake
add_test(NAME aot_native_regression
         COMMAND ${INIMERSE_PYTHON} ${CMAKE_SOURCE_DIR}/tools/aot_native.test.py
                 --build ${CMAKE_BINARY_DIR} --cc ${CMAKE_C_COMPILER})
set_tests_properties(aot_native_regression PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300 LABELS "compiler;aot;regression")
```

`--cc ${CMAKE_C_COMPILER}` 让测试用**与引擎同一个编译器**，避免「引擎用 gcc、测试用别的 cc」的隐性偏差。`EXP_CTEST` 由 **102 同步到 103**（`tools/gate.sh:49`，`docs/BOARD.md` §3 ctest 行）。注意 `stage_ctest` 检查的是输出里确有 `0 tests failed out of 103` —— 光看 ctest 退出码是看不出一整个 `add_test( )` 悄悄掉了的。

#### 判据③：它在 main 上必然失败（三证）

- **M1（改坏实现，断言变红）**：把 `src/compilation/aot_native.c:210` 的 `nv_mod` 从 `return nv_int(y ? x % y : 0);` 改成 `x + y` ⇒ CTest `***Failed`，`FAIL int_mod` / `int_mod_negative` / `int_lcg_first_step` / `mixed_program` 四条等价用例同时红，且分歧钉死项报 `lcg_float_promotion: native moved from '377401575\n' to '3587540464946819303\n' — if it now matches the interpreter, promote this case to EQUIVALENCE`。`cmp` 证明还原后逐字节相同、重编复绿。
- **M3（隐藏二进制）**：`mv build/aot-native /tmp/…` ⇒ `error: build/aot-native not found — configure and build the project (cmake --build build), which now builds aot-native as a normal target`，`rc=2`。
- **main 上根本没有该目标**：`git show f923f4d:CMakeLists.txt | grep -c 'aot-native'` = **0**，改后 = **1**。即在这条 CTest 进树之前，`build/aot-native` 只能由那个不被门禁调用的脚本产生。

#### 判据④：两处语义分歧写明为**已知不等价**

两处都**没有修**（修它们要动解释器的名字解析与作用域规则，超出本行范围），而是**双端逐字钉死**在 `tools/aot_native.test.py` 的 `DIVERGENCE` 表里，任一侧改动即红：

| 用例 | 源码 | 解释器 | 原生 | 机制 |
| --- | --- | --- | --- | --- |
| `func_nothing_returns_nil` | `func nothing() { x = 1 }` + `say nothing()` | `nil` | `0` | 解释器：函数里对未声明名字赋值不产生返回值，落到隐式 `nil`；原生：`x` 被当成局部并返回最后一个表达式的值 |
| `global_write_from_func` | `global g` / `g = 2` / `func bump() { g = g + 5` / `return g }` / `say bump()` / `say g` | `5\n2\n` | `7\n7\n` | 解释器：函数内 `g = …` 把该名字**变成本地变量**，全局不动；原生：写的是全局 |

#### 判据⑤：`%2147483648` 分歧从「排除」改为「覆盖」

BOARD 原文只给了结果（解释器 `0`、原生 `357615489`），没有表达式。从本节之外的 `docs/STATUS.md` §10.14（`aot-backend` 那节）取回原文：

```
x = (x*1103515245+12345) % 2147483648
```

实测把分歧**定位到了迭代次数上**，这是原文没有说清的一点：

| 迭代次数 | 解释器 | 原生 | |
| --- | --- | --- | --- |
| 1 | `1103527590` | `1103527590` | 一致 |
| 2 | `0` | `377401575` | 分歧 |
| 3 | `12345` | `662824084` | 分歧 |
| 4 | `0` | `1147902781` | 分歧 |

机制：`2147483648` 超过 `INT32_MAX`，解释器把它**提升为 float**（stderr 打 `warning: integer literal 2147483648 out of 32-bit range, promoted to float`，本测试只比 stdout 故不受影响）。第一步的 `x*1103515245+12345` 还在 2^53 以内、双精度精确，所以两边一致；**从第二步起中间量越过 2^53，低位被抹掉，取模塌成 `0` 或 `12345`**。原生用 int64，始终精确。三次重跑确认确定性。

于是拆成两个用例：第一步进**等价语料**（`int_lcg_first_step`），第二步起进**钉死分歧**（`lcg_float_promotion`）。这比「明确排除」强：排除只是不测，钉死是**把差距固定住并让它在缩小时报警**（提示语明确要求「if it now matches the interpreter, promote this case to EQUIVALENCE」）。现为 `59 cases (46 equivalence, 3 pinned divergences, 10 refusal), 0 failures`。

#### 顺带修掉的写域内路径缺陷

行 109（`aot-backend`）与 `docs/archive/BUILD_RELEASE_LESSONS_0.4.0.md:202` 把 `tools/selfhost_bench.py`、`tools/perf_compare.py`、`docs/archive/SELFHOST_BENCHMARK.md` 列进了本行写域。核查发现一个真缺陷：两个工具都写 `docs/` 根下的 `SELFHOST_BENCHMARK.md`，而**该路径不存在** —— 唯一受版本控制的是 `docs/archive/SELFHOST_BENCHMARK.md`（报告在归档整理时搬走了，工具没跟着改）。后果是 `--write-docs` **静默新建一个未跟踪文件**而不是更新归档报告，跑完什么也没更新、还不报错。

六处替换（`tools/selfhost_bench.py:6/:63/:121/:122`、`tools/perf_compare.py:16/:131`）后实测：`python3 tools/selfhost_bench.py --runs 1 --write-docs` 输出 `written: docs/archive/SELFHOST_BENCHMARK.md`，`git status` 只显示 ` M docs/archive/SELFHOST_BENCHMARK.md`，`docs/` 根下的 `SELFHOST_BENCHMARK.md` **不再被创建**（验证后已 `git checkout --` 还原归档报告）。

#### 遗留（如实上报）

- **`aot_native.test.py` 的 46 条等价用例全是 int/bool**，不能外推到语言整体；浮点打印**不在等价语料内**（解释器 `1.0/3.0` → `0.333333` 而 `1.23456789012345678` → `1.234568`，无单一 printf 精度可匹配，原生用 `%g`）—— 是**已知不覆盖**，不是通过。
- **拒绝语料 10 例**只证明「超出子集时拒绝」，不证明拒绝原因文本的稳定性。
- 门禁需要可用的 `cc`；引擎构建本来就要求它，故未新增前置。

### 10.27 桥接层不该被调用方的坏源码杀死，以及 `inim_load_text()` 回家（`xlang-bridge-followups`）

`xlang-bridge`（§10.15）交出的桥接层是按「可恢复」写的：`src/bridge/bridge_abi.c` 的 `inimerse_bridge_parse_count()` 里有一句 `if (!prog) return 4;`。这一行**从来不可达** —— `parse_program()` 的错误路径是 `exit(1)`。于是桥接层把**调用方给的文本**喂进去，一次语法错就让宿主的 Python / JVM 进程消失：没有异常、没有 traceback，只有 stderr 上一行 `Error: expected 'expression', but got '' (type 141)`。

#### ① `parse_fatal()`：把「进程要不要死」变成一个决定

`src/parser/parser.c` 里 **11 处**语法错误原先各自 `fprintf(stderr, ...); exit(1);`（BOARD 原文写「12 处」并列了 11 个行号，实测就是 11 处），现在全部改走一个函数：

```c
static _Thread_local jmp_buf g_parse_recover;
static _Thread_local int     g_parse_recover_armed;

static void parse_fatal(void) {
    if (g_parse_recover_armed) { g_parse_recover_armed = 0; longjmp(g_parse_recover, 1); }
    exit(1);
}
```

`parse_program_recoverable()`（声明在 `src/parser/parser.h`）在那一次解析期间装恢复点：`if (g_parse_recover_armed) return NULL;` → `if (setjmp(g_parse_recover)) return NULL;` → 装点、调 `parse_program()`、清点、返回。

**三点必须写下来的取舍**：

- **CLI 一个字都没改。** `parse_program()` / `parse_program_file()` 保持 `exit(1)`：脚本语法错就没有可执行的程序，退出码是引擎对外承诺的行为。实测 `./build/inimerse /tmp/e13/bad.im` → rc 1，`say 1+1` → rc 0，`--err-json` 路径同样 rc 1 并打出 `{"error":"io",...}`。
- **`_Thread_local` 不是可选项。** 桥接层能被任意 Python / JVM 线程进入，而 `longjmp()` 跳进另一个线程的栈帧是未定义行为。用全局 `jmp_buf` 在单线程测试里看不出问题，在多线程宿主里是随机崩溃。
- **`longjmp` 会跳过 `free`。** 坏源码解析到一半时已 malloc 的 AST 片段被漏掉，每次语法错泄漏一段。桥接层是「宁可漏一点也不能死」，这是**已知且接受**的代价，不是没想到。

`bridge_abi.c` 一行改完后（`parse_program(buf)` → `parse_program_recoverable(buf)`），那句 `return 4` 第一次可达，Python 侧看到的是正常的 `RuntimeError: inimerse: parse_count failed (code 4)`。

#### ② `inim_load_text()` 搬进 `src/common/common.c`

它声明在 `src/common/common.h:55`，唯一实现在 `src/main.c:346`，而 `src/main.c` **故意不在** `IMINERSE_ENGINE_SOURCES` 里（它拥有引擎自己的 `main()`）。引擎内部有两个翻译单元调它 —— `src/parser/parser.c`（经 `parse_program_file()`）与 `src/compiler/compiler.c`（模块内联）—— 于是每个链接引擎的嵌入方都得**把整个 `src/main.c` 再编一遍**，还要用 `target_compile_definitions(... PRIVATE main=<target>_entry_unused)` 把 `main` 改名。同一份源码不是拷贝，但味道难闻。

`src/common/common.c` 原先只有一行 `#include "common.h"`，**却本来就在** `IMINERSE_ENGINE_SOURCES`（`CMakeLists.txt:344`），所以搬过去零成本：连同它唯一的使用者 `static int inim_utf8_valid()`（严格 UTF-8 校验，拒绝 overlong 与代理对）整段移过去，`src/main.c` 原址留一段指向 `common.c` 的说明，`src/common/common.h:54` 的 `Implemented in main.c` 改成 `common.c`。

**顺带修掉一个真分歧**：`src/compilation/aot_native_tool.c` 里还有**第三份** `inim_load_text()`，是 POSIX-only 的 —— 它**静默丢掉了引擎在 `_WIN32` 下做的 GBK(cp936)→UTF-8 转码**。删掉它之后该工具直接用引擎那份，Windows 上读旧 GBK 脚本终于和引擎给一样的字节。（`aot-native` 链接 `inimerse_engine`，留着它本来也会是重复符号。）

删除后的断言不是「我觉得没重复了」，是可查的：

```
build/CMakeFiles/inimerse_python.dir/link.txt → 无 main.c.o
nm build/bridge/inimerse*.so                  → 恰好一个 T inim_load_text
nm .../inimerse_engine.dir/src/common/common.c.o → 定义在这里
nm .../aot_native_tool.c.o                    → 不再定义它
```
`rm -rf build` 全新构建 rc 0、无 duplicate symbol，`build/aot-native` 仍 876,920 B。

#### ③ 双向验证：新断言必须能变红

`tools/xlang_python_bridge.test.py` 新增 4 条断言，核心是子进程里跑：

```python
try:
    n = inimerse.parse_count('x = ')
    print('NO-RAISE', n); sys.exit(3)
except RuntimeError as e:
    print('RAISED', 'code 4' in str(e))
print('count-after', inimerse.parse_count('x = 1\ny = 2\n'))
print('HOST-ALIVE')
```

`HOST-ALIVE` 只有活到最后一行才会打印，所以这条断言不可能靠巧合通过。

| 状态 | 结果 |
|---|---|
| 修好（`parse_program_recoverable`） | **20 checks / 0 failures / rc 0** |
| 改坏（换回 main 那行 `parse_program(buf)`） | **20 checks / 4 failures / rc 1**，正好是新增那 4 条 |

改坏时子进程的 **stdout 整段为空** —— 连调用之前那句 `print("version: ...")` 都没到管道。`exit(1)` 杀进程时缓冲里的 stdout 一起丢了。这比「宿主死了」更值得记一笔：桥接层闯的祸会把调用方**已经打印的输出**一起带走。

`EXP_CTEST` **保持 103**（只在既有 `xlang_python_bridge` 里加断言，没有新增 `add_test`）。完整 `tools/gate.sh --jobs 4` 七阶段 PASS。

#### 遗留（如实上报）

- **`longjmp` 的泄漏**（见上）：每次语法错漏掉半棵 AST。要「不漏」得把整个递归下降改成返回错误码，那是另一个量级的改动，没有做。
- **嵌套调用返回 NULL**：在一次可恢复解析里再调 `parse_program_recoverable()` 会直接返回 NULL（解析器本来就不是可重入的）。今天没有这样的调用方。
- `src/main.c.bak`（未被版本控制）里还有一份 `inim_load_text()`；它不参与构建，没有动。**（2026-10-04：该文件已删除，见 §10.48。）**

### 10.28 OAuth 回环拆出 Tauri、八个真实面板进 DOM 断言，以及「门禁把跳过当通过」（`oauth-bind-transport`、`forge-panels`）

本轮两行都有一个共同形状：**板上写的验收对象，本机根本编译不出来 / 根本不存在**。处理方式不是降低判据，是把判据钉到真正能跑的那一层，并把做不到的部分**显式拆成新的前置行**。

#### ① 为什么 OAuth 那一行必须拆（`oauth-bind` → 行 100 + 行 101）

行 100 的标题是「OAuth **token 交换与资料绑定**」，而原判据只要求「回环证据」。**`gatehermetic` 对抗审计的原话**：「这个标题与判据之间的落差就是坑本身」。

实测本机 `pkg-config` / `webkit2gtk-4.1` / `javascriptcoregtk-4.1` / `libsoup-3.0` / `gtk+-3.0` **全缺**，且**无免密 sudo**（`sudo -n true` 失败）⇒ 整个 Tauri 壳**编不出来**。但回环本身是普通 Rust，不需要 Tauri。于是：

- **行 100 `oauth-bind-transport`（已完成）**：`Infiverse_standard/oauth_loop/` —— 纯 `std`、零外部依赖的库 crate，复刻 `Infiverse_standard/src-tauri/src/lib.rs:919-980` 的五个函数。真回环证据：`cargo test` **16 passed; 0 failed**，`callback_live_loop_over_real_socket` 绑 `127.0.0.1:0`（内核分配，**不是固定 8765**）、由测试自己扮演浏览器经**真实 `TcpStream`** 发请求，断言 `query() == "code=TESTCODE&state=TESTSTATE"` 并读回 `HTTP/1.1 200 OK`。
- **行 101 `oauth-bind-transaction`（阻塞，新增）**：承载标题真正指的另外四件事 —— token 交换、`oauth_bind` 命令、`state` 校验、把 5 秒上限改回阻塞。**它仍需要 Tauri 壳**，所以是真前置行，不是拖延。写域与行 100 在 `Infiverse_standard/oauth_loop/` 重叠，已在板上注明。

**判据「删掉 `&state={}` 会让测试红」被实测证伪**：那样改是**编译错** `error: argument never used`（`format!` 拒绝无用实参），拿不到断言失败。改用 `&state=X{}` 才是真咬合证据。同样被证伪的还有「非原子的引用计数」（见 §10.29 的闭包一节）。

**`start_callback()` 静默丢迟到回调（审计抓出的真缺陷）**：原实现 `lib.rs:964` 的 `incoming().flatten().next()` **无限期阻塞**；crate 版的 `query()` 上限 `CALLBACK_WAIT = 5s`（`lib.rs:43`）然后写 `""`。对照实测（`--test-threads=1`）：≤5s 回 `code=FASTCODE&state=FASTSTATE`；**6s 后监听仍回 `200 OK`，但之后六次轮询全 `""`**。消费方窗口约 60s（`app.js:366` 每秒轮询，`++tries > 60`）⇒ **真人输密码或过 2FA 必然被丢**。`lib.rs:39-42` 那段辩解**两处都假**：不是「纯粹为测试」（它改变了生产行为），也不是「原实现从不阻塞」（阻塞的是 Rust 侧，非阻塞的只是 JS 轮询）。这段已改写为 `THIS IS A DIVERGENCE FROM THE ORIGINAL, AND IT IS NOT TEST-ONLY`，crate 头注释新增 `WHAT THIS CRATE DOES **NOT** PROVE`（5 条，全部带 `file:line`）。

**坐实的一处用户可见假声明**：`app.js:285` 对用户写「授权后由回调服务交换 code，再调用 `oauth_bind` 保存资料」——**两半都假**。全仓 `grep -rniE 'access_token|oauth2/token|grant_type|refresh_token|exchange'` **零命中**；`oauth_bind` 只活在那句散文里；`linked_accounts.json` 全仓**只读不写** ⇒ 这个 UI **永远不可能**返回 `linked:true`。

#### ② `forge-panels`：原判据是事实错误，改窄到实际存在的八个模块

原文写「面板住在 `Infiverse_standard/src/ui/`」。实测：`grep -rln -iE 'forge'`（排除 `node_modules`）**只命中文档**与两个无关文件；`grep -rlni 'forge|spacetime|蓝图|blueprint'` 在 `Infiverse_standard/` 的 `*.rs`/`*.js`/`*.html`/`*.json`/`*.md` 里**零命中**；`src/ui/` 只有 `index.html`(41 行) / `app.js`(877 行) / `app.css`(113 行)。⇒ **不存在 Forge 面板，也不存在时空/物理/蓝图面板**，「面板可用」在源头上无物可验。

改窄后的判据钉在 `app.js:7-16` 的 `MODULES` 表：`home`/`chat`/`browse`/`workbench`/`inimerse`/`toolbox`/`plugins`/`settings` 八个模块的**渲染、切换、以及每个模块触发的 IPC 命令序列**，由 `tools/infiverse_panels.test.js` 以 jsdom DOM 断言固定。原始截图/录屏要求随不存在的面板一起**降级并明说**。

**三条踩坑（都是「看起来绿了其实没测」的同类）**：

- **桩的形状会决定生死**：`get_local_ip` 必须返回**列表**（`app.js:83` 调 `.join(', ')`）、`get_engine_info` 需带 `.path`、`tool_files`/`get_achievements` 也必须是列表（`app.js:513`/`:94` 调 `.map`）。返回 `null` 会在 app 自己的 promise 链里抛 unhandled rejection，**直接杀进程，早于任何断言**。
- **活动栏按钮数是 9 不是 8**：`#activitybar` 有 9 个 `.ab-item`，第 9 个是 `#theme-toggle`。按 `[data-mod]` 计数才等于 8，theme-toggle 单独断言存在。
- **第一次破坏实验用错了函数名**：把 `setModule` 改成 `{ return; }` 完全没有效果（真实函数叫 `switchModule`），是一次**假红实验**。换成真名后 `module "home" rendered no content` 立刻变红。三证齐：删一个 `MODULES` 项 ⇒ 红；`switchModule` 空实现 ⇒ 红；删 `invoke('record_run')` ⇒ 红。

#### ③ `stage_node` 原先把「跳过」当成「通过」——本轮最值得记的一条

`tools/node_suites/run_all.js` 旧版只看退出码，把套件自报的 `skipped` 也算成 `ok`。于是 `wasm_host.test.js` **一直在 skip**：它默认找 `tools/wasm_probe.wasm`，而 CMake 把 probe 建在 `<build>/wasm_probe.wasm`（`CMakeLists.txt:320-328`）。⇒ 板上那条「expect 11/11」**从来不是 11 个真通过**。

修法两层：`run_all.js` 用 `/\bskipped\b/` 单独归类并打印 `skipped (NOT passes): ...`；`stage_node` 见到任何 skip **一律让阶段红**，并断言**分母**（新增 `EXP_NODE="${EXP_NODE:-12}"`）。依据是 `examples/BUILDING_BRIDGES.md` 的既有规矩：**a gate can never go green while claiming evidence it did not collect**。传参后 `wasm_host.test.js` 真跑并过，带 `NODE_PATH` 时 **12/12 全通过、0 skip**（本仓首次）。

同一个道理在本轮的另一处也生效：`oauth-loop` 阶段断言的是 `test result: ok. 16 passed; 0 failed` 这行**计数**，因为 `cargo test` 退 0 分不清「16 个测试过」和「1 个过 + 15 个被删」。

#### ④ 环境与运行时事实

- Rust 工具链由本轮装成：**cargo 1.99.0 / rustc 1.99.0**（`RUSTUP_HOME=/home/sakiko/.rustup`、`CARGO_HOME=/home/sakiko/.cargo`）。`static.rust-lang.org` 的 DNS 只给 IPv6 地址且不可达，`sh.rustup.rs` 可达。
- **jsdom 30.1.1 装在仓外**：`/home/sakiko/.local/inimerse-jsdom/`（`npm install --prefix … jsdom --cache /home/sakiko/inimerse/.npm-tmp`，37 packages）。**仓内无 `package.json`，依赖不入仓**；运行时需 `NODE_PATH=/home/sakiko/.local/inimerse-jsdom/node_modules`，否则 node 阶段**按设计**因 skip 而红。

#### 遗留（如实上报）

- **行 101 仍阻塞**：需要 `webkit2gtk-4.1` 等系统库与能安装它们的权限，本机两样都没有。行 100 的 crate 只证明**运输层**正确，**不**证明浏览器授权流程端到端可用。
- **`forge-panels` 的降级是降级**：新判据证明「能渲染 / 能切换 / 按预期发 IPC」，**不**证明「面板可用」原义 —— 原义的对象不存在。它按 `阻塞` → `已完成` 落账时，这一点已写在判据里，不是脚注。

### 10.29 `oauth-bind-transaction` 结案：PKCE 消掉 `client_secret`，以及一个被函数提升吃掉的按钮（行 101）

§10.28 把行 101 记为「仍阻塞：需要 `webkit2gtk-4.1` 等系统库」。**那个阻塞的成因不是依赖冲突，是 apt 索引过期**——这值得先记，因为它花掉了两轮对话。

#### ① 阻塞的真相：过一个版本的镜像 404，而不是依赖打架

用户执行 `apt-get install` 报：

```
gstreamer1.0-plugins-good_1.28.2-2ubuntu0.3_amd64.deb  404 Not Found
E: Unable to satisfy dependencies. Reached two conflicting assignments:
   ... libwebkit2gtk-4.1-0 ... 依赖 gstreamer1.0-plugins-good but none of the choices are installable
```

`E: Unable to satisfy dependencies` 读起来像依赖冲突，**实际是索引指向了一个已被镜像删除的版本**：`apt-cache policy gstreamer1.0-plugins-good` 里唯一候选是 `1.28.2-2ubuntu0.3`，而镜像池里现存的只有 **`1.28.2-2ubuntu0.4`**（两个镜像 HTTP 200，`ubuntu0.3` 两个都 404）。`sudo apt-get update` 后一切照常安装。

**教训**：`E: Unable to satisfy dependencies` 要先怀疑**索引过期**，再去读依赖图。此时引擎自己报的那条「abandon all hope」式的冲突信息完全是误导。

装完实测：`pkg-config` / `webkit2gtk-4.1` / `javascriptcoregtk-4.1` / `libsoup-3.0` / `gtk+-3.0` **全部 PRESENT**；`cd Infiverse_standard/src-tauri && cargo build` ⇒ `Finished dev profile … in 1m 45s`，RC=0（编译 tauri 2.11.5、webkit2gtk 2.0.2、soup3 0.5.0、tao 0.35.3、muda 0.19.3、qrcode 0.14.1、app v0.2.1）。

#### ② `client_secret` 的保管问题被 PKCE 消掉，而不是被藏起来

行 101 原本要解决的问题之一是「`client_secret` 放哪」。**桌面应用无法保管 `client_secret`** —— 它必然随二进制分发。改用它本来的替代品：**PKCE（RFC 7636）**，授权请求带 `code_challenge`（S256），换取 token 时带 `code_verifier`，**不需要任何 secret**。

`oauth_loop` 的 crate 头注释写着 **HARD REQUIREMENT 1: no external dependencies. Pure std only.**（`Cargo.toml:8`）。中间一度加了 `sha2` / `base64` / `getrandom` 三个依赖，**已全部撤销**：SHA-256（FIPS 180-4，约 80 行含 64 个 K 常量）与无填充 base64url 都**手写在 `src/lib.rs` 里**，并按**公开向量**钉死而非「信任自己的实现」：

- `pkce_rfc7636_appendix_b_vector`：RFC 7636 Appendix B 原文的 `code_verifier = dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk` ⇒ `code_challenge = E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM`（从 `rfc-editor.org` 取回原文核对，另用 Python `hashlib` + `base64.urlsafe_b64encode` 独立复算，三处一致）。
- `sha256_known_answer_vectors`：FIPS 向量六条（空串 / `abc` / 56 字节的标准串 / 100 万个 `a` / 55 字节 / 56 字节 —— 后两条专打 padding 边界）。

**为什么手写哈希在这里可以接受，写进了注释**：它的唯一输入是 PKCE verifier，按构造就是公开的（verifier 要发给 provider、challenge 要进 authorize URL），所以手写哈希通常的风险（侧信道、密钥处理）不适用；且用公开向量钉死而不靠自我信任。注释同时写明「**若要拿它哈希别的东西，请改用真库**」——避免这个实现被当成通用哈希用。读熵同样如此：`fill_os_random()` 读 `/dev/urandom`，非 unix **返回 Err 而不是用时钟猜**——「Failing loudly beats deriving a "random" verifier from a clock」。

#### ③ 第一次双向验证暴露了真问题：四个承载判据的函数当时**零覆盖**

按 §10.27 的规矩，改完先做打断实验。**把 `verify_callback()` 的 state 比对取反（`!=` → `==`），20 个测试全绿。**

也就是说 `verify_callback` / `parse_token_response` / `token_request_body` / `form_encode` 这四个**正是承载「token 交换、`state` 校验」判据的函数**，当时一条测试都没碰到。这与 task-15 对行 100 的审计结论（「套件对关键路径盲」）**同型，只是这次发生在协调者刚写的新代码上**——同一个错误在同一个项目里换了个人重犯，说明它不是某人的疏忽，而是「先写实现再补测试」的默认路径自带的。

补 16 条覆盖测试后立刻报出**第二个真 bug**：`parse_token_response()` 只读嵌套的 `fields` 而不读顶层，导致 **GitHub 朴素 JSON 响应里的 `token_type` / `refresh_token` / `scope` 全部静默丢失**（`access_token` 因有 `.or_else(|| status.get("access_token"))` 回退才保住）。修法是四个字段统一走 `fields.get(k).or_else(|| status.get(k))`。修前 3 红，修后全绿。**这个 bug 是被「先抓覆盖、再抓行为」的顺序逼出来的**：如果当初补的是「让现有测试变绿」而不是「让断言先能红」，它会一直在。

`oauth_loop` 测试数 16 → 18（`CALLBACK_WAIT` 两条）→ 20（PKCE 向量两条）→ **36**；`tools/gate.sh` 的 `stage_oauth_loop()` 计数断言同步到 `test result: ok. 36 passed; 0 failed`，注释写明「The count is 36 since … the coverage that the bidirectional check showed was missing entirely (inverting the state comparison left the suite green at 20)」。

#### ④ 一个被函数提升吃掉的按钮：`app.js` 声明了两次 `bindBrowse`

`tools/infiverse_panels.test.js` 新增的 OAuth 流程回归**第一次跑就红**，而它红的方式指向一个此前从未被发现的既有缺陷：

`app.js` 有**两个同名顶层 `function bindBrowse()`** —— **行 288**（含 Hub 包安装、联机探测、**以及全部 OAuth 授权接线**：`oauth_status` 预取、`openAuth`、`ghBtn.addEventListener`，107 行）与 **行 615**（含 `[data-browse]` 下载卡片、`#btn-ping`、好友节点列表，40 行）。**JS 函数声明提升，后声明的覆盖先声明的** ⇒ 288 整份是死代码，**OAuth 授权按钮点了毫无反应**。

取证不是靠读代码，是靠 jsdom 调试脚本在真 `index.html` + `app.js` 下切到 browse、点 `#oauth-gh`、追踪 `invoke` 记录（`/tmp/e13/dbg_panels.js` 等四份）。结果是：`oauth-client` / `oauth-gh` / `links-msg` **都渲染出来了**、点击**已派发**，但 `calls` 里**一个 `oauth_*` 都没有**。`grep` 全文件确认 `bindBrowse` 是**唯一**重复的顶层函数。

**它为什么一直没被发现**：既有面板套件只断言「模块能否渲染与切换」，而重复声明后**每个模块照常渲染**。**能渲染 ≠ 接线生效** —— 一个被覆盖的函数在「渲染」这个粒度上完全隐形。新的 OAuth 回归能抓到它，唯一的原因是它断言的是点击之后的**实际 IPC 序列**。

处理：保留 615 那份，把 288 独有接线并入（`#btn-ping` 两份都有，保第一份，其提示语更完整），删除重复声明并在原址留注释指向这条断言。**合并时先逐条列出两份的接线清单做差集**，避免「合并」变成「又一次覆盖」。教训记一条：脚本按 `');}))` 之类 token 切分时会把 `forEach(...)` 的收尾行一起删掉，产生 `SyntaxError: Unexpected token ')'`；改回按**大括号配对**定位边界，并按行级精确删除。

套件里新增了一条通用断言：**`app.js` 不得重复声明任何顶层函数**（`topLevelFunctionNames()` + `dupes` 断言）。这不是风格规则，是防这一类隐形死代码。三处打断全部红在正确断言上：重引入重复声明 ⇒ `app.js declares these top-level functions more than once: bindBrowse.`；去掉 `oauth_pkce_start` ⇒ `the link flow must request a PKCE challenge; saw [...]`；跳过 `oauth_bind` ⇒ `a received callback must reach oauth_bind; saw [...]`。

#### ⑤ 两处静默失败（这是「绑定了却什么都没发生」的另两个来源）

- **`oauth_start_callback` 在端口被占时返回假的 `true`**：原实现 `let Ok(listener) = TcpListener::bind(...) else { return; };` 跑在 **detached 线程**里，bind 失败就静默 `return`，调用方收到 `true`。审计定级 HIGH。改为**在调用者线程上 bind**，失败返回 `{"ok": false, "error": "cannot listen on 127.0.0.1:8765: <e>"}`，UI 显示「❌ 无法监听回调端口」。
- **`parse_token_response` 只看嵌套字段**（见 ③）：这是「授权成功但账号资料不全」的来源。

#### ⑥ 验收与残留

**验收**：`rm -rf build` 后完整八阶段 `tools/gate.sh --jobs 4` → `GATE_RC=0`、`gate: OK — every stage passed.`：build / ctest **103/103** / economy **39/39** / node **12/12** / dsh-inimerse plugin **55/55** / oauth_loop **36/36** / docs relative links / docs backtick paths。crate 的 `[dependencies]` **仍为空**（`cargo tree` 只有 `oauth_loop v0.1.0` 自身），`src-tauri` 只加一条 `oauth_loop = { path = "../oauth_loop" }`。

**残留（不属本行判据，如实上报）**：

- **`redirect_uri` 仍未绑定**（审计定级 MEDIUM）：`POST /TOTALLY/DIFFERENT/PATH?code=ATTACKER` 带 `Host: evil.example` 仍得 `200 OK`，query 被原样收下。修它需要回调服务校验路径与 Host，**尚未做**。
- **`linked_accounts.json` 的读取侧未验证**：本轮证明了它会**被写**，但 `oauth_status` 永远返回 `linked:false` 的**另一半**（读回并反映到 UI）没有端到端验证过。
- **端到端浏览器授权未跑**：`cargo build` 通过只证明壳能编译。PKCE 换取 token 走的是真 HTTP 到 `github.com`，本机网络对 `raw.githubusercontent.com` 都不可达，**没有做过一次真实的 provider 往返**。

---

### 10.30 一次真实 GitHub 往返抓出的缺陷：授权 URL 没有百分号编码（行 101/102）

这一节记的是一次**用户亲手跑出来的**失败。它推翻了我上一节写的「没有做过一次真实的 provider 往返」——那次往返做了，而且它当场打穿了三层都绿的代码。

#### ① 现场

用户在 app 里点 GitHub 授权，浏览器显示 `Authorization received. You can return to Infiverse.`（**回环本身是好的**），但 app 弹：

```
❌ 绑定失败：provider refused the code (incorrect_client_credentials: The client_id and/or client_secret passed are incorrect.)
```

回调 URL 是：

```
http://127.0.0.1:8765/callback?code=3f65cad143c3467351b0&iss=https%3A%2F%2Fgithub.com%2Flogin%2Foauth&state=9604aba2-6c1a-4db6-95ad-b3db8b901cc5
```

`code` 到手、`state` 原样回来、路径与 `Host` 都通过 §10.29 新加的绑定校验（**没被拒绝**）——所以那一层是好的。

#### ② 根因：`redirect_uri` 是原样插进 URL 的

`Infiverse_standard/src-tauri/src/lib.rs` 的 `oauth_authorize` 当时是：

```rust
format!("https://github.com/login/oauth/authorize?client_id={}&redirect_uri={}&scope=read:user%20user:email&state={}{}", client_id, redirect_uri, state, pkce)
```

`client_id`、`redirect_uri`、`state` **三个值全部零编码**。而 `redirect_uri` 恒为 `http://127.0.0.1:8765/callback`，里面 `:` 与 `/` 都是 query 值里的保留字符。provider 解析出的 `redirect_uri` 因此与注册值不同：它照样渲染授权页、照样回调（所以**看起来**是通的），但它发的 `code` 绑定的上下文与 token 请求提交的 `redirect_uri` 不一致，交换于是死在 `incorrect_client_credentials` —— **这个错误名在指责 `client_id`，实际是在指责 `redirect_uri`**。

同一个文件里 `oauth_bind` 与 PKCE 参数**都用了 `form_encode`**，唯独授权 URL 这三个基础参数没有。它是行 101 早期就带进来的，一直被我当作「已经工作的部分」而没碰。

#### ③ 这个缺陷为什么能活过三层绿灯（本节最值得记的部分）

1. **crate 里那份是「逐字节复现」**：`Infiverse_standard/oauth_loop/src/lib.rs` 的 `oauth_authorize` 头注释原文是「Byte-for-byte reproduction of `oauth_authorize` (lib.rs:929-936)」，并**明确**把「空值判定 trim、插值不 trim」当作要保留的保真度。它把缺陷当规格抄了一遍。
2. **测试把缺陷钉成了规范**：`authorize_github_is_byte_identical` 的期望值是硬编码字面量，注释还专门说明为什么不用重新计算的 `format!`（「an expectation recomputed from the implementation agrees with any change to it, including a wrong one」——诊断学是对的）。而它逐字节钉死的正是 `&redirect_uri=http://127.0.0.1:8765/callback&`。**十九个测试忠心耿耿地复现一个 GitHub 会拒绝的 URL。**
3. **唯一跑得近的套件把它 stub 掉了**：`tools/infiverse_panels.test.js:140` 用 `return Promise.resolve('https://github.com/login/oauth/authorize?client_id=CID')` 顶替这个命令，所以它断言「有没有传 PKCE challenge / 三个调用是否共用同一个 redirectUri」，**永远看不到真实的那个字符串**。

三层都绿，且都在互相确认一个不能用的事实。

#### ④ 修法：把构造搬进 crate，让测试住的地方能测到它

- **crate**：新增 `pub fn oauth_authorize_pkce(provider, client_id, redirect_uri, state, code_challenge: Option<&str>, code_challenge_method: Option<&str>)`，对进 query 的**每一个值**做 `form_encode`，并顺带把 `client_id` 的空白 trim 掉（从网页复制 client id 常带尾随空白：授权端点容忍、token 端点不容忍，是**单向静默失败**）。原来的 `oauth_authorize` 保留四参数签名，转调 `…_pkce(.., None, None)`，无 challenge 时 URL 形状不变。`code_challenge_method` 缺省 S256。
- **壳**：`oauth_authorize` 命令改为一行委托 `oauth_loop::oauth_authorize_pkce(...)`。**不再有第二份实现**——缺陷原来正因为在壳里，而壳里没有测试。
- **第三处（顺手，同类）**：`oauth_bind` 里 `let _ = si.write_all(payload.as_bytes());` 把写入错误整个吞掉。写失败时 curl 会发一个**空 POST**，GitHub 的回答正是「client_id 不对」——与本次症状同型。改为收集 `write_err`，失败返回 `could not send the token request body to curl: {e}`。
- **JS**：`app.js` 的 `openAuth` 对 client id 与 redirect uri 都做 `.trim()`，并让**输入框优先于 `localStorage`**（原写法在输入框为空时会退回旧值，而 `:358` 每次点击都覆写存档，陈旧 id 可能一直压住新填的）。

#### ⑤ 验收

- crate 测试 **46 → 49**：`authorize_github_is_byte_identical`、`authorize_bilibili_is_byte_identical` 的期望值改为编码后的 URL；新增 `authorize_encodes_the_redirect_uri`（断言编码存在、`redirect_uri=http://` **不**存在、并按 `&`/`=` 切分后解码能还原出精确的 redirect_uri）、`authorize_carries_the_pkce_challenge`、`authorize_trims_a_client_id_that_carries_whitespace`。
- **双向验证（三个破坏实验，各自只红对应用例）**：①`let rdu = redirect_uri.trim().to_string();`（还原原始缺陷）⇒ `authorize_encodes_the_redirect_uri` + 两个 byte-identical 共 **3 红**；②不 trim `client_id` ⇒ `authorize_trims_a_client_id_that_carries_whitespace` **1 红**；③`code_challenge_method` 硬编码成 `plain` ⇒ `authorize_carries_the_pkce_challenge` **1 红**。全部还原后 49/49 绿。
  - 过程教训：第一次做③时改成了 `format!("{}", ...)`，那是**编译错**（未使用变量）不是测试红 —— 破坏实验必须能编译，否则你拿到的是构建失败而不是断言失败。
- **完整八阶段门禁**：`rm -rf build && NODE_PATH=/home/sakiko/.local/inimerse-jsdom/node_modules bash tools/gate.sh --jobs 4` → `GATE_RC=0`，逐阶段 build / ctest（103/103，0 skipped）/ economy 39/39 / node 12/12 / dsh-inimerse plugin 55/55(live) / **oauth_loop crate 49/49** / links / doc-paths 全 PASS。`tools/gate.sh` 的计数断言与 `expect 49/49` 已同步。
- **壳**：`cargo build` RC=0；真实产物 URL 由一次性探针 `oauth_loop::oauth_authorize_pkce(...)` 打印确认：

```
https://github.com/login/oauth/authorize?client_id=Iv1.0a1b2c3d4e5f6789&redirect_uri=http%3A%2F%2F127.0.0.1%3A8765%2Fcallback&scope=read:user%20user:email&state=9604aba2-6c1a-4db6-95ad-b3db8b901cc5&code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&code_challenge_method=S256
```

#### ⑥ 环境事实（与代码无关，但花掉了两轮）

- 本机 `api.github.com` → 200，`github.com` → **curl 28 超时**。用户机器 `github.com` → 200、`ip=20.205.243.166`（与本机 DNS 同 IP）。
- 双方对 `github.com/login/oauth/access_token` 的 GET/POST（含空 POST）**都得 404 `{"error":"Not Found"}`** ⇒ 该路径在两边网络都被特殊处理，**本机无法复现真实交换**。这条要记住：不要把它误读成「端点坏了」或「client_id 坏了」。
- 已证伪的假设：`form_encode` 会损坏 client id（对三种真实形状都是恒等变换）；client_id 来自 GitHub App（是 **OAuth App**）；Tauri 的 camelCase→snake_case 映射失效（`oauth_bind` 到达了 GitHub，证明映射有效）。

#### ⑦ 残留

- **修复后仍未重跑真实往返**：用户尚未重试，所以「PKCE 真能换到 token」目前仍是代码层成立 + URL 形状正确，**没有现场确认**。
- `linked_accounts.json` 的读取侧（`oauth_status` 的 `linked:true` 分支）仍未端到端验证。

### §10.31 client_secret：一个被官方文档推翻的设计前提

#### ① 现场

`redirect_uri` 编码修好后（§10.30）用户**亲手重跑了一次真实 GitHub 往返**，错误**逐字相同**：

```
provider refused the code (incorrect_client_credentials:
The client_id and/or client_secret passed are incorrect.)
```

即 §10.30 的修复是**必要的但不是充分的** —— 两个独立缺陷叠在同一个错误名上。

#### ② 取证：先排除三个假设，再定案

为免再花一轮网络往返，在 `oauth_bind` 里加了一段临时 `eprintln!` 诊断（**只印长度与首尾字符，不印完整 client_id / code / token**）。用户重启后跑一次，得到：

```
[oauth-diag] client_id len=20 first4="Ov23" last2="se" | verifier len=43
             | code len=20 | redirect_uri="http://127.0.0.1:8765/callback"
             | endpoint=https://github.com/login/oauth/access_token
```

| 假设 | 判据 | 结论 |
|---|---|---|
| client_id 混入隐藏字符 | 长度 **20**（`Iv1.` 型约 19） | **排除** |
| PKCE verifier 没送出去 | 长度正好 **43**（RFC 7636 要求 43–128） | **排除** |
| `redirect_uri` 与注册值不符 | 与注册值**逐字相同** | **排除** |

诊断随即从工作树删除。**注意**：诊断输出里的 `Ov23` 前缀一度让我推断「用户抄了 GitHub App 的 ID」，用户随即确认其 **OAuth Apps 列表里那个 app 的 Client ID 也是 `Ov23`** ⇒ 该推断**被证伪**，GitHub 现在对 OAuth App 也发 `Ov23.` 前缀，`Iv1.` 不再是判据。这条弯路写在这里，是因为它差点导致一个错误的「改用 GitHub App」建议。

#### ③ 定案：官方文档

取证对象：`https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps`（HTTP 200，378,973 bytes），解析「2. Users are redirected back to your site by GitHub」一节下的 token 交换参数表。

| 参数 | Required? | 原文要点 |
|---|---|---|
| `client_id` | `Required` | — |
| **`client_secret`** | **`Required`** | **无任何「除非用了 PKCE」豁免** |
| `code` | `Required` | — |
| `redirect_uri` | `Strongly recommended` | — |
| **`code_verifier`** | `Strongly recommended` | 「**Required if `code_challenge` was sent during the user authorization**」 |

授权侧同时确认 PKCE **是被支持的**：`code_challenge` 必须是 43 字符 SHA-256；`code_challenge_method` **必须 `S256`，`plain` 不支持**。

⇒ **在 GitHub OAuth App 上，PKCE 是 `client_secret` 的附加加固，不是替代品；两者都必须送。**

#### ④ 我错在哪里

「PKCE 消掉 `client_secret`」是本行立项时写下的设计目标（并因此进了 `app.js:285` 的用户可见文案）。它的依据是 **RFC 7636 对公共客户端的通用模型** —— 在那个模型里 verifier 确实替代 secret。但 **GitHub OAuth App 没有采纳它**：`client_secret` 仍是无条件 `Required`。

我一度还说「OAuth App 不支持 PKCE」—— 文档证明**支持**，只是**不够**。两句都是我只凭先验、没查文档就下的判断。

`incorrect_client_credentials` 的官方措辞是「client_id **and/or client_secret**」——**它字面就是缺 secret**，只是错误名把注意力引向了 client_id。这是本行花了四轮往返才拆开的一层。

#### ⑤ 落地（a）：把 secret 变成显式输入

`oauth_loop`（**`[dependencies]` 仍为空**）：

- `token_request_body_with_secret(provider, code, redirect_uri, code_verifier, Option<&str>)` —— 有 secret 才追加 `client_secret=`；**空或纯空格视为缺失**（发 `client_secret=` 是同一个被拒的请求，只是多了个字段名）
- `secret_is_required(provider) -> bool` —— GitHub 为真。**这是 provider 的属性，不是 PKCE 的属性**，写成函数是为了不让「需要 secret」变成散落各处的隐含假设
- `client_secret_env_var(provider)` / `read_client_secret(provider)` —— 环境变量按 provider 命名，避免多 provider 互相覆盖

壳侧（`Infiverse_standard/src-tauri/src/lib.rs`）：

- `oauth_secret_path(provider)` —— 文件名**由 provider 派生**（非 ASCII 字符换成 `_`），不接受调用方给路径：否则一次凭据写入会变成任意文件写入
- `oauth_set_secret` 命令（已注册进 `generate_handler!`）—— 写 `userdata/oauth_secret_<provider>.txt`
- `oauth_bind` 取 secret 的顺序是**文件 → 环境变量**；`secret_is_required` 为真且两者皆无时**提前带原因失败**，不让 provider 用指责 client_id 的话来回答

UI（`Infiverse_standard/src/ui/app.js`）：

- 新增 `#oauth-secret`（`type=password`，不回显）
- `openAuth` 在**开窗之前**调 `oauth_set_secret` —— 顺序有意义：放在后面会让一次失败的授权丢掉刚填的 secret
- `app.js:285` 那句「无需 client secret」**已改写**。它现在（且一直）是假的，只是假的方式变了：原来假在「没有这个命令」，现在假在「不需要 secret」

#### ⑥ 验收

| 项 | 结果 |
|---|---|
| `oauth_loop` 测试 | **49 → 55**（secret 携带 / 编码 / 空值省略 / trim / provider 判定 / 环境变量命名） |
| `tools/gate.sh` 计数断言 | 三处同步到 `55 passed`（注释、`grep -qE`、`run_stage` 标签） |
| 面板套件 | 新增 3 条：secret 字段存在且为 `password`；填入的 secret 必须被持久化；持久化必须在 `oauth_authorize` **之前**（58 IPC calls） |
| 双向验证 | ①注释掉 `oauth_set_secret` 调用 ⇒ 套件红；②从交换 body 去掉 secret ⇒ 套件红。均还原后复绿 |
| 完整八阶段门禁 | `rm -rf build` 后 `GATE_RC=0`，`oauth_loop crate (expect 55/55)` **PASS**，其余七阶段全 PASS（ctest 103/103、economy 39/39、node 12/12、plugin 55/55 live、links、doc-paths） |

#### ⑦ 诚实边界与残留

- **这（a）不是安全**。secret 明文落盘在 `userdata/oauth_secret_<provider>.txt`。桌面 app 无法真正保管 client secret —— 它随二进制分发、可被提取。这条路只保证：**不进仓库、不编进二进制、不随程序分发**。这句话也写进了 crate 注释与 UI 文案，而不是只写在文档里。
- **(b) device flow 是官方唯一「不需要 secret」的路径** —— 文档原文「The `client_secret` is not needed for the device flow.」，且第二份 `client_secret` 表标注为 `Required unless the token was generated using the device flow`。代价是授权方式整体替换（`user_code` 在浏览器输入，回调服务器 / `state` 校验 / PKCE 全套不再需要），需在 OAuth App 页面勾上 **Enable Device Flow**。**留给后续行**。
- **仍未端到端验证**：`linked_accounts.json` 读取侧（`oauth_status` 的 `linked:true` 分支）。

### 10.32 device flow：官方唯一「不需要 client secret」的 GitHub 路径（BOARD 行 104）

§10.31 落地 (a) 之后紧接着落地 (b)。这一节记的是 (b)，以及它当场抓出的三个真 bug。

#### ① 为什么这一行存在

(a) 让回调路线能跑通，但它的诚实边界写在 §10.31 ⑦ 里：secret 明文落盘，**那不是安全**。官方文档同时给出了另一条路 —— device flow 侧明文写 **「The `client_secret` is not needed for the device flow.」**，第二份 `client_secret` 参数表也标注 `Required unless the token was generated using the device flow`。所以这一行不是「加个备选入口」，而是**唯一一条不依赖我们无法保管的凭据的路径**。

#### ② 代价是明说的，不是悄悄换的

device flow 的授权形状整体不同：没有回环监听、没有 `state` 校验、没有 PKCE，改为在浏览器打开 `https://github.com/login/device` 手动输入 8 位 `user_code`。UI 文案把两条路并列写出来（哪条要 secret、哪条不要），而不是只留按钮。使用前提：**必须在 OAuth App 设置里勾上 `Enable Device Flow`**，否则 step 1 直接返回 `device_flow_disabled` —— 本行把这个错误码单独映射成一句可执行的提示，而不是当成一般失败。

#### ③ crate 侧（`Infiverse_standard/oauth_loop/src/lib.rs`，`[dependencies]` 仍为空）

新增 `device_code_endpoint(provider)`、`device_code_request_body(provider, client_id, scope)`、`device_poll_request_body(provider, client_id, device_code)`、`parse_device_code_response`（返回 `DeviceCode { device_code, user_code, verification_uri, interval, expires_in }`）、`parse_device_poll`（返回 `DevicePoll` 枚举），以及 `grant_type` 常量 `urn:ietf:params:oauth:grant-type:device_code`。`parse_device_poll` **不把 pending 折叠成失败**：`DevicePoll::{Token, Pending, SlowDown{interval}, Expired, Denied, Disabled, Failed}` 七个分支各自可辨 —— 把「还在等」和「出错了」混成一个 bool，是这类流程最常见的写坏方式。

#### ④ 壳侧（`Infiverse_standard/src-tauri/src/lib.rs`）

新增 `fn post_form(endpoint, body) -> Result<String, String>`，把 POST 传输抽成一处（两个设备命令共用）：body 走 **stdin**（`--data-binary @-`），client id 与各码**不进进程表**；**非 2xx 即失败并带 stderr**，即使 curl 写了响应体。新增 `oauth_device_start(provider, client_id, scope)` 与 `oauth_device_poll(provider, client_id, device_code)` 两个命令，均已注册进 `generate_handler!`。`oauth_device_poll` 在拿到 token 时写出 `linked_accounts.json`，与 `oauth_bind` 同一记录形状。

#### ⑤ UI（`Infiverse_standard/src/ui/app.js`）

browse 模块新增 `#oauth-device` 按钮、`#oauth-device-box` 展示区与 `#oauth-user-code`。轮询按 provider 给的 `interval` 进行，**`slow_down` 会抬高这个下限并照办**（无视它只会换来更多 `slow_down`，最终由限流结束流程）；`expiresIn` 到点即停；`denied` / `expired` 各自有独立文案。

#### ⑥ 双向验证（两证都有牙，均还原后复绿）

- **破坏 1**：不把 `userCode` 写进 `#oauth-user-code` ⇒ `the user code must be displayed for the user to type; got ""` 红。整个流程的价值就在这 8 个字符上，不显示等于不可用。
- **破坏 2**：轮询时把 `started.deviceCode` 写成 `started.userCode` ⇒ `'WDJB-MJHT' !== 'DC'` 红。套件另有一条断言专门盯这个：**user code 绝不能被送给兑换端点** —— 混淆这两个码是这条流程最可能写错的一处。

#### ⑦ 验收

测试 55 → **69**（新增 14 条：设备码请求体、`grant_type` 常量、响应解析、七个轮询分支、`interval` 默认与 `slow_down` 携带新值、未知错误码不被当成 pending）。`tools/gate.sh` 四处同步 69（`:229` `grep -qE`、`:230` 错误串、`:256` 标签、`:217` 注释）。完整八阶段门禁 `rm -rf build && NODE_PATH=/home/sakiko/.local/inimerse-jsdom/node_modules bash tools/gate.sh --jobs 4` → **`GATE_RC=0`**、`gate: OK — every stage passed.`，逐阶段全 PASS（build / ctest **103/103** / economy 39/39 / node 12/12 / plugin 55/55 live / **oauth_loop 69/69** / links / doc-paths）。`tools/infiverse_panels.test.js` 新增 (9a) 段三组断言（设备按钮必须存在；必须先后调 `oauth_device_start` 与 `oauth_device_poll`；user code 必须在屏幕上且**绝不**出现在轮询参数里），IPC calls 58 → **62**。

#### ⑧ 残留

- **端到端仍未验证**：本机没有可用的 OAuth App 配置，device flow 与 (a) 一样**只在代码层与测试层成立**，没有跑过一次真的 `user_code` → token。要真正验证需先把 `Enable Device Flow` 勾上。
- `linked_accounts.json` 读取侧（`oauth_status` 的 `linked:true` 分支）仍未端到端验证 —— §10.29/§10.31 起就挂着。
- `redirect_uri` 未绑定：本行不涉及，device flow 没有回调。

### 10.33 device flow 点一下就把窗口卡死 30 秒（BOARD 行 105）

用户实测报「infiverse 输入了之后卡退」。这是 §10.32 那版 device flow 的**真缺陷**，不是环境问题。

#### ① 症状与现场

- `~/inimerse/Infiverse_standard/userdata/oauth_secret_github.txt` 在 `11:05` 被写入，**只有 7 字节**，内容以 `dd616` 开头 —— 那不是 GitHub client secret（正常 40 位），是提交号 `dd6165a` 的前缀被粘进了 secret 输入框。
- 进程已退出（`pgrep` 无匹配），日志 `/home/sakiko/.local/share/com.infiverse.app/logs/Infiverse.log` **是空的**，所以卡退没有任何日志痕迹 —— 走的是「窗口先无响应、再退出」这条路，不是 panic。

#### ② 根因：同步的 `#[tauri::command]` 跑在 webview 线程上

`Infiverse_standard/src-tauri/src/lib.rs` 里 **67 个命令全是同步的，没有一个 `async`**（`grep -c "async fn"` = 0）。Tauri 2 里**非 async 的 `#[tauri::command]` 在主线程上执行**，也就是驱动 webview 的那个线程。而本轮新增的 `post_form` 用 `curl --max-time 30` 发请求。

本机 `github.com` 不可达（`curl` 28 超时；同一时段 `api.github.com` 返回 200）⇒ 每次调用**占满 30 秒**。device flow 是**每轮轮询都调一次**，所以窗口一路无响应。

**实测（不是推断）**：写了一个与原命令同形状的 Rust 程序（同步 `Command::output()` + 同样的 `--max-time 30`，POST `https://github.com/login/device/code`）并计时 ⇒ **`elapsed=30.0s`，主线程确实被按住 30.0 秒**。

#### ③ 为什么测试没抓到

`tools/infiverse_panels.test.js` 在 jsdom 里跑，**jsdom 没有「被阻塞的主线程」这回事**，而且它把 `invoke` 整个 stub 掉了。任何 DOM 断言都看不见这个缺陷 —— 能渲染、能点击、IPC 序列全对，真机上照样卡死。**这是本行最重要的方法论结论**：UI 套件全绿与 UI 不卡死是两件事。

#### ④ 修法

`post_form` 拆成两层：`post_form` 变成 `async`，内部用 **`tauri::async_runtime::spawn_blocking`** 把阻塞的 curl 挪到运行时的阻塞线程池（不引入 `tokio` 依赖 —— 该 crate 的 `[dependencies]` 里本来就没有）；真正干活的 `post_form_blocking` 保持同步。三个网络命令改 `async`：`oauth_bind`、`oauth_device_start`、`oauth_device_poll`。`grep -c "^async fn"` 由 0 → **4**，`cargo build` 零 warning。

#### ⑤ 进树守卫（因为 DOM 断言看不见它）

新增 `tools/check_async_commands.py`：断言 `NETWORK_COMMANDS = ["oauth_bind", "oauth_device_start", "oauth_device_poll"]` 三个命令**必须声明为 `async`**，否则报「declared sync but shells out to a blocking curl — a sync #[tauri::command] runs on the main thread and freezes the window」。挂在 `tools/gate.sh` 的 `stage_oauth_loop()` 里（crate 计数断言之后）。

**双向验证**：把 `oauth_device_poll` 改回同步 ⇒ `FAIL: oauth_device_poll: declared sync but does blocking network I/O`、`rc=1`；还原 ⇒ 打印 `ok: 3 network command(s) are async (off the webview thread)`。完整八阶段门禁 `rm -rf build && NODE_PATH=/home/sakiko/.local/inimerse-jsdom/node_modules bash tools/gate.sh --jobs 4` → **`GATE_RC=0`**、`gate: OK — every stage passed.`，`check_async_commands` 在日志里可见。`tools/infiverse_panels.test.js` 仍 `62 IPC calls` 全绿。

#### ⑥ 残留

- **`hub_download_install`（`lib.rs:689`）是同一形状**：同步命令 + `--max-time 30`，本行**没有改**（它属于 Hub 下载那条线，且是一次性下载而非每轮轮询；改动面应独立评估）。同一目录下 `:1170`（`oauth_bind`）与 `:1275`（`post_form`）已修。
- **`oauth_open` 不回收子进程**：`lib.rs:968-974` 的 `Command::spawn()` 立即丢弃句柄，`xdg-open` 每次调用留一个僵尸进程。本行未改。
- **secret 内容本身是错的**：用户需要填入真的 40 位 client secret；当前文件里是提交号前缀。修卡死**不能**让错误的 secret 通过 —— 那会是 `incorrect_client_credentials`。
- device flow 端到端仍未验证（本机 `github.com` 不可达，勾了 `Enable Device Flow` 也走不通）。

### 10.34 回调授权仍失败：错的 secret 一直在文件里，而报错怪 client_id（BOARD 行 106）

用户实测：「授权依旧输出 `❌ 绑定失败：provider refused the code (incorrect_client_credentials: ...)`；但设备码授权一次搞定」。**device flow 成功是决定性的证据**：它与回调路径用同一个 `client_id`，却不需要 secret —— 所以 client_id 一定是对的，问题只可能在 secret。

#### ① 现场取证

`Infiverse_standard/userdata/oauth_secret_github.txt` 实测 **7 字节、首二字符 `dd`、末二字符 `5a`** —— 也就是提交号 `dd6165a`。它不是 secret，而且**从头到尾没被换掉过**。

#### ② 为什么它一直没被换掉（真正的缺陷）

`app.js:368` 是 `if (secret) { await invoke('oauth_set_secret', ...) }` —— **字段为空时不写入**。这个选择本身是对的（空字段不该抹掉已保存的 secret），但它造成一个死角：

- 面板的输入框**永远是空的**（密码框不回填，且没有「已保存」提示）；
- 文件里却躺着一个错的 7 字符值；
- 每次点「授权」都用它去兑换；
- GitHub 回 `incorrect_client_credentials`，**原文怪的是 "client_id and/or client_secret"**。

于是用户看到的是「client_id 不对」，而 client_id 完全正确。**这正是本行从 §10.30 起反复出现的同一类缺陷：一个指向错误嫌疑人的报错，而 app 里没有任何东西去反驳它。**

#### ③ 修法（三层，都在「别让错误值走到网络」这一侧）

1. **crate 侧新增 `secret_shape_problem(secret) -> Option<String>`** 与常量 `IMPLAUSIBLE_SECRET_LEN = 20`（GitHub 发 40 位、bilibili 32 位，20 是远远低于两者的地板）。返回的消息**只说长度、绝不复述值**：`the saved client secret is only 7 characters, which is too short to be one (GitHub issues 40) — re-paste it from the OAuth App's settings page`。**刻意不做格式校验**（那是 provider 的事），只拦「不可能成立」的值。
2. **壳侧 `oauth_bind` 在发请求之前**调用它，不合格就直接返回该消息 + 文件路径 —— 不再浪费一次往返，也不再把 GitHub 那句误导的话转给用户。
3. **壳侧新增 `oauth_secret_status(provider)`** 命令（返回 `saved`/`length`/`problem`/`path`/`envVar`，**不含值**），UI 把它显示在输入框旁：`已保存 client secret（40 位）` 或 `⚠️ 已保存的 secret 只有 7 位，不能是有效的`。**让陈旧值可见**，这是 ② 那个死角的直接解药。

#### ④ 用户要求的复制功能

`#oauth-copy-code` 按钮。复制失败**不能假装成功**：WebKit 只在 secure context 暴露异步剪贴板 API，所以失败时退回「选中文本 + 提示按 Ctrl+C」，并在消息里说清是哪一种。复制的是 `#oauth-user-code` 的内容（`WDJB-MJHT` 这类 user code），**不是** `deviceCode`。

#### ⑤ 双向验证（三证，均还原后复绿）

- 复制按钮写空串 ⇒ `'' !== 'WDJB-MJHT'` 红；
- 剪贴板拒绝时仍置 `copied = true` ⇒ `a refused copy must not claim success; got "✅ 已复制设备码..."` 红；
- crate 的 `if trimmed.len() < IMPLAUSIBLE_SECRET_LEN` 改成 `if false` ⇒ 3 红（`a_secret_too_short_to_be_one_is_named_as_such` / `a_real_looking_secret_passes_the_shape_check` / `whitespace_does_not_make_a_short_secret_look_longer`），`69 passed; 3 failed`。

#### ⑥ 验收

crate 测试 69 → **72**；`tools/gate.sh` 四处同步 72；`tools/infiverse_panels.test.js` 新增 (9b) 段（复制写对值、成功要说、拒绝不许说成功、secret 长度必须显示且**不得打印 secret 本身**），IPC calls 62 → **64**。完整八阶段门禁全 PASS。

#### ⑦ 残留

- **用户仍需填入真的 40 位 client secret**：修好报错不等于修好配置，现在的行为是「在发请求前告诉你它是 7 位」。
- device flow 端到端**已被用户实测跑通**（这是本行第一次拿到真实成功的端到端证据），但回调路径端到端**仍未成功过**。

#### ⑧ 两个意外发现（都来自这次实测）

**发现 1：device flow 端到端真的跑通了 —— 这是本行第一次拿到真实的成功证据。**

`Infiverse_standard/userdata/linked_accounts.json` 在用户点过「设备码授权」之后被写出来了，内容形如 `access_token = gho_…[len=40]` / `provider=github` / `scope=read:user user:email` / `token_type=bearer`（只列键与长度，不复述值）。⇒ 从 `oauth_device_start` → 用户在浏览器输入 8 位码 → `oauth_device_poll` 拿到 token → 落盘，**整条链路在真机上成立**，不再是「只在代码层成立」。同时 `oauth_status` 的读取侧（`linked:true` 分支）也随之被真实数据覆盖 —— 那是 §10.29 起一直挂着的残留。

**发现 2：那个 token 文件是可提交的，和 secret 是同一个陷阱。**

`.gitignore` 为了保住 `.gitkeep` 而用 `!Infiverse_standard/userdata/` 重新包含了整个目录，于是 `linked_accounts.json`（**装着真的 access token**）处于「未跟踪但 `git add -A` 就会进去」的状态。这与 §10.33 的 secret 完全同型，只是这次躺的是活凭据。已加三条忽略规则（`linked_accounts.json`、`Infiverse_standard/userdata/linked_accounts.json`、`Infiverse_standard/src-tauri/userdata/linked_accounts.json`），`git check-ignore` 确认命中，`git add -A --dry-run` 现在只列源码文件。

> 这两次是同一根因的两次发作：**运行时写进 `userdata/` 的东西默认是可提交的**。加 `oauth_secret_*.txt` 与 `linked_accounts.json` 是打补丁；真正该做的是把 `userdata/` 的忽略规则反过来写（默认忽略、白名单只放 `.gitkeep`），那是独立的一行。

#### ⑨ ctest 的一次间歇性失败（不是本行引入）

本次完整门禁第一遍在 `node_discovery_regression` 上红了：`AssertionError: add1=1 add2=1 ping_ok=false hub2_count=0`，即 `verse_hub_ping` 对其中一个 hub 返回 ≤0。

- **结构性证据**：本行改动的 7 个文件（`Infiverse_standard/oauth_loop/src/lib.rs`、`Infiverse_standard/src-tauri/src/lib.rs`、`Infiverse_standard/src/ui/app.js`、`docs/STATUS.md`、`tools/gate.sh`、`tools/infiverse_panels.test.js`、`.gitignore`）**在 `CMakeLists.txt` 里出现次数全为 0** ⇒ 引擎二进制与任何 ctest 用例都不消费它们。
- **复现测量**：单跑 `node_discovery.test.py` **10/10 通过**；`ctest -R "node_discovery|lease_handoff|reconnect_generation|economy_migration|protocol_regression" -j4` **12/12 通过**；完整 `ctest -j4` **3/3 通过**（`100% tests passed, 0 tests failed out of 103`）。合计 **1 次失败 / 约 16 轮**。
- 失败签名是 UDP 往返超时（`add1`/`add2` 都成功，说明地址登记没问题；`ping_ok=false` 说明 ping 那一步超时），与 OAuth 无关。
- **结论**：**既有间歇性缺陷**，不是本行引入。它本身值得单独一行 —— 一个会随机红的门禁，其代价是让人开始习惯重跑。
- **方法教训**：我第一次写复现循环时用 `tail -3 | grep "100% tests passed"` 判定，结果 summary 行被 `tail` 截掉，**12 轮全被误判为 FAIL**（真实情况全过）。这与早先 `GATE_EXIT` 那次「读到 `tail` 的退出码」是同一类错误：**别解析输出尾部来判断成败，用退出码**。

## 10.35 被推荐的路线是唯一没人检查的路线（环境变量取 secret 的覆盖缺口）

**触发**：收尾时用户问「我怎么做」，我推荐路线 A（secret 走环境变量、不落盘）而不是界面输入框。
推荐完顺手核对那条路的代码，才发现它**零测试**。

- **缺口**：`Infiverse_standard/oauth_loop/src/lib.rs` 的 `read_client_secret(provider)` 是壳侧取 secret 的
  fallback（`oauth_bind` 的取法在 `Infiverse_standard/src-tauri/src/lib.rs:1161-1165`：**先读文件、trim 后非空**，
  否则 `or_else` 走环境变量）。它的文档注释写着「This is the crate's only input-reading side effect, and it is here
  rather than in the shell **so that a test can exercise the same lookup the shell uses**」——
  **而这样的测试从来不存在**（全文件 `set_var` 零命中）。被推荐的路线恰恰是唯一没人检查的路线。
- **补的 3 条**：`read_client_secret_reads_the_environment`（读值并 trim）、
  `read_client_secret_treats_whitespace_as_absent`、`read_client_secret_is_none_when_nothing_is_set`。
  每条用**自己的 provider 名**（环境是进程全局的，而测试并行跑，共用 `github` 会互相踩）。
- **破坏实验暴露我自己的空转测试（本节最重要的部分）**：把 `std::env::var(client_secret_env_var(provider))`
  改成 `std::env::var("INFIVERSE_NEVER_SET_AT_ALL")` 之后，`read_client_secret_reads_the_environment` 红了，
  **但 `read_client_secret_treats_whitespace_as_absent` 照样通过** —— 因为「变量名错了」返回的也是 `None`，
  与「空白被过滤掉」在断言层面不可区分。这条测试当时是**关于 `None` 的陈述，不是关于空白的陈述**。
- **改写**：同一变量名先设纯空白断言 `None`，**再设真值断言 `Some`** —— 后一句证明这个名字确实是被读的那个，
  于是前一句的 `None` 才成为关于空白/过滤的陈述。注释里写明为什么需要后半段。
- **两处破坏（改写后）**：变量名指向不存在的名字 ⇒ **2 红**（`73 passed; 2 failed`）；
  去掉 `.filter(|v| !v.is_empty())` ⇒ **1 红**（只有 whitespace 那条）。
- **同步**：crate 72 → **75**；`tools/gate.sh` 四处（`:217` 注释、`:238` `grep -qE`、`:239` 错误串、`:273` 阶段标签）
  与 `docs/BOARD.md` §3 阶段表、行 101/105 一并同步。**插注释时把续行漏了 `#` 前缀**，
  `bash -n tools/gate.sh` 报 `syntax error near unexpected token 'and'`，已补。
- **教训（可复用）**：**一个只在「什么都不做」时才通过的测试，和没有测试一样**；断言 `None` / 空 / 未调用时，
  必须同时证明「有输入时它确实会变」，否则断言的是失败路径的形状而不是行为。

## 10.36 回调路径第一次端到端走通（行 101 的最后一个残留清掉）

**2026-10-03 14:16–14:18，用户实机操作。** §10.29 起一直挂着的「回调这条路从未成功过」，到这一步结束。

- **操作方式**：路线 A（secret 走环境变量、不落盘）。`Infiverse_standard/userdata/oauth_secret_github.txt`
  已删除，app 由 `INFIVERSE_GITHUB_CLIENT_SECRET=<40 位> ./target/debug/app` 启动。
- **时间线（用进程与文件时间戳判定，不靠叙述）**：app 启动 `14:16:36`（pid 64949）⇒ 用户点授权、
  `oauth_start_callback` 绑定 `127.0.0.1:8765` ⇒ 浏览器授权后回环收到 code ⇒ `14:18:39`
  `linked_accounts.json` 被重写（此前是 11:36 的 device flow 记录）⇒ 之后 8765 不再监听
  （`serve_once` 只服务一次即释放），与「已收到并处理完」一致。
- **落盘内容（只列键与长度）**：`access_token = gho_…len=40`、`provider = github`、
  `scope = read:user user:email`、`token_type = bearer`、`refresh_token = None`。
- **凭据是活的，不是「写进去了」**：拿这个 token 打真实 API ——
  `GET https://api.github.com/user` → **200**，`login = infileap`、`id = 141123918`、`name = 月识`。
- **路线 A 成立**：`oauth_secret_github.txt` **全程不存在** ⇒ 明文 secret 没有落盘，
  这条路径依赖的 `read_client_secret()` 正是 §10.35 补上测试的那个函数。
- **这条链上此前每一环都曾经是错的**，回调能成说明它们现在同时成立：
  ①授权 URL 的 `redirect_uri` 必须百分号编码（`8b06402`，否则 GitHub 发回的 code 与 token 请求上下文不匹配，
  报错却是 `incorrect_client_credentials`）；②`client_secret` 必须送（`dd6165a`，PKCE 不替代它）；
  ③回调的路径与 `Host` 必须与注册的 redirect_uri 相符；④`state` 必须逐一比对；
  ⑤token 响应必须解析 `error` 字段（HTTP 200 也可能是失败）；⑥**空 secret 字段不得覆盖已保存的 secret** ——
  最后这条正是 §10.34 那个 7 字节 `dd6165a` 的死角。
- **仍未做（不属于本行）**：`redirect_uri` 与 `Host` 的绑定校验只在**收回调时**做，
  注册值本身仍由用户在 GitHub 页面手工保证；`linked_accounts.json` 的**过期/刷新**路径不存在
  （`refresh_token = None`，GitHub 这个 app 没开 `Expire user access tokens`）。

## 10.37 忽略规则写反了：运行时产物默认可提交（BOARD 行 106）

**根因一句话**：`.gitignore` 先忽略 `userdata/`，又用 `!userdata/` 与 `!Infiverse_standard/userdata/`
**把目录整体收回**（本意是保住 `.gitkeep`）。git 的语义是**收回一个目录就收回了它里面的一切** ——
于是运行时写进去的任何文件**默认可提交**，而 `git add -A` 不会征求任何人同意。

- **两次发作，同一根因**：①`Infiverse_standard/userdata/oauth_secret_github.txt`（§10.33，client secret）；
  ②`Infiverse_standard/userdata/linked_accounts.json`（§10.34，**活的 `gho_` access token，40 字符**）。
  两次都是「补一条文件名规则」挡住的。
- **为什么补丁是错的修法**：补丁只覆盖**当事人想起来的名字**。第三个运行时产物（`stats.json` 早就是，
  只是没人注意到）出现时，默认仍然是「可提交」。这不是疏忽，是**默认值选反了**。
- **修法（反过来写）**：忽略**内容**、白名单放行占位文件 ——
  ```
  userdata/*
  !userdata/.gitkeep
  ```
  三个目录各一份（`userdata/`、`Infiverse_standard/userdata/`、`Infiverse_standard/src-tauri/userdata/`）。
  安全答案是默认值，新产物在**任何人知道它存在之前**就已受保护。
- **顺手清掉的历史包袱**：`Infiverse_standard/src-tauri/userdata/stats.json` 自
  `8248e08 Release Infiverse 0.2.0` 起**被跟踪**，每次跑测试都会把工作树弄脏。已 `git rm --cached`
  （**文件仍在磁盘上**，app 自己会重建），现在 `git ls-files | grep userdata/` 只剩两个 `.gitkeep`。
- **新增进树检查 `tools/check_ignored_credentials.py`**，挂在门禁新第 8 阶段
  `run_stage "userdata ignore rules (default deny)" ignored-credentials stage_ignored_credentials`。
  **它断言的是默认值，不是清单**：用 `git check-ignore --no-index -q` 去问一个
  **从没被写过的文件名**（`__runtime_state_probe_never_created__.txt`）是否已被忽略。
  为什么必须这样：一个只检查那三个已知名字的测试，**在旧规则下只要有人再加一条补丁就会变绿** ——
  这正是原缺陷熬过两次事故的方式。检查同时要求 `.gitkeep` **不被**忽略（否则目录会从新克隆里消失），
  并跑一遍 `git add -A --dry-run` 要求其中不出现任何 `userdata/` 路径。
  `--no-index` 让它问的是**规则**而不是工作树，因此对不存在的路径与已跟踪文件都成立。
- **双向验证（两次破坏，各自红在对应用例）**：①把规则改回旧写法 ⇒ **8 条失败**，
  含三个目录的默认拒绝探针、三个 `stats.json`，以及 `git add -A` 那两项（它明确列出会收进提交的路径）；
  ②删掉 `!userdata/.gitkeep` ⇒ 报 `userdata/.gitkeep: ignored. The placeholder must stay visible`。
  两次还原后均 `ok`。
- **判据要求的现场验证**：在三个目录里**同时**放入 `oauth_secret_github.txt`、`linked_accounts.json`、
  `stats.json`（内容是占位值，不是真凭据），`git add -A --dry-run` **只列 6 个源码/文档文件，
  `userdata/` 路径计数为 0**。
- **门禁与文档同步**：阶段数 **八 → 九**（`docs/BOARD.md` §3 表新增 `ignored-credentials` 行、
  `tools/gate.sh` 头部 `--only` 清单补上 `oauth-loop` 与 `ignored-credentials`、
  `docs/README.md` 与本节顶部对 `tools/gate.sh` 的描述同步为九个阶段）。
  完整九阶段 `GATE_RC=0`、`gate: OK — every stage passed.`
- **保留的纵深防御**：`.gitignore` 仍保留 `oauth_secret_*.txt` 与 `linked_accounts.json` 两条**不限路径**的
  规则 —— 这两个名字无论落在哪里都是凭据，不该只靠那三个目录保护。

## 10.38 node_discovery 的 flake：一个被证伪的机制，和一个被测出来的机制（BOARD 行 131）

**先说更正。** 这一行立项时我写的机制是「`wait_http_ping` 等的是 HTTP，而 `verse_hub_ping` 走的是
UDP 往返，两者不同步」。**代码证伪了它**：`b_verse_hub_ping`（`src/mod/verse_dist_mod.c:929`）
调的是 `http_get_body(url)`，而 `url` 是 `http://<uri>/ping`，判定条件是 body 以 `pong` 开头 ——
**它和 `wait_http_ping` 打的是同一个 HTTP 端点**。这条机制从来没成立过，写进板子时也没有验过。

### 目标 flake（`ping_ok=false`）没有复现

`tools/node_discovery.test.py:224` 的 `AssertionError: add1=1 add2=1 ping_ok=false hub2_count=0`
是本行唯一的一手现场。为了复现它做了这些测量，**全部零命中**：

| 负载 | 结果 |
| --- | --- |
| 整套件 48 轮 × 12 并发 | 1 次失败（端口那条，不是这条） |
| 整套件 120 轮 × 32 并发 | 4 次失败（**全是**端口那条） |
| 整套件 40 轮 × 12 并发，开 `INIMERSE_PING_DIAG=1` | **40/40 通过，且每次 ping 都是 `rc=0 status=200`** |
| 两个热 hub + 300 次双 ping × 8 并发 | 0 失败 |
| 健康 hub 连打 1200 次 ping | **0 个 0** |
| 每轮新起两个 hub（复刻判据现场）+ 60 次双 ping × 12 并发 | 0 失败 |

为此在 `b_verse_hub_ping` 里加过一段临时插桩（`INIMERSE_PING_DIAG`，把
`http_get_body` 折叠掉的「连接失败 / HTTP ≥ 400 / body 空」三件事分开打印），
**用完已完全移除**，`git diff src/mod/verse_dist_mod.c` 为空。

**同时被实验否掉的四个假设**（都有证据，不是推测）：

- 「亚毫秒往返返回 0」—— `r_push_int(vm, ok ? (int)dt : 0)`（`src/mod/verse_dist_mod.c:944`）
  确实把**耗时**当返回值、又用 `0` 兼作失败哨兵，语义上真的二义；但 1200 次实测 0 个 0
  （每次请求都要 `getaddrinfo`，`dt` 恒 ≥ 1ms）。**记为潜在隐患，不是本次原因。**
- 「`shutdown()` 丢弃未发数据造成 RST」—— `im_socket_close` 就是裸 `close(fd)`，没有 `shutdown`。
- 「`ok` 为假导致 404」—— `src/platform/http_posix.c:1187` 的 `ok` 合取里含 `ping`，`/ping` 恒被识别。
- 「`im_socket_connect_timeout` 留下非阻塞套接字」—— `src/platform/socket.c:155` 恢复原 flags。

### 被测出来的机制：FakeDirectory 预留端口再绑定

同一套件里另有一条**可复现**的失败，而且它才是常见的那条：`FakeDirectory`
（`tools/node_discovery.test.py:156` 的 `fake_port = distinct_ports(1)[0]`，`:194` 才
`HTTPServer(("127.0.0.1", fake_port), ...)`）**先预留号码、很久以后才绑定** ——
正是 `tools/testports.py` 存在的理由（释放到绑定的窗口），而套件对自己的服务器仍在用它。

- **修复前**：120 轮 × 32 并发 → **116/120，4 次失败（3.3%）**，签名恒为
  `OSError: [Errno 98] Address already in use`，出在 `self.socket.listen(...)`，
  **一个断言都还没跑**。
- **修复**：让内核分配 —— `HTTPServer(("127.0.0.1", 0), FakeDirectory)` 然后
  `fake_port = fake.server_address[1]`。从内核给号那一刻起套接字就被持有，**窗口为零**。
  这是 `start_hub_bound_ports` 对 hub 一直在用的同一套纪律。
- **修复后**：同样 120 轮 × 32 并发 → **120/120，0 失败**。
- **反向**：把 `distinct_ports(1)[0]` 那版改回去 → 同一负载下 **116/120，4 次失败（3.3%）**，
  与基线逐位相同；还原后复绿。

### 进树守卫

新增 `tools/check_test_ports.py`，挂在 `tools/gate.sh` 的 `stage_ctest()` 里（ctest 通过之后）：
扫描 `tools/*.test.py` 里所有 `HTTPServer(` / `ThreadingHTTPServer(` 构造，**端口参数必须是 `0`**，
否则报出 `file:line` 与理由。**双向**：把预留再绑定那版写回去 → `FAILED`，指出
`tools/node_discovery.test.py:201` 与 `port 'fake_port'`；还原 → `ok`。
**刻意做窄**：只查套件**自己绑**的服务器；交给子进程的端口（`--port 0` 的 hub 等）是 §2.9 的
另一个问题，把它们一起标红会牵连与本缺陷无关的套件。

### 这一行真正的结论

- 记录在板子上的机制是**错的**，且从未验证 —— 这是本行最值得记的一条。
- 目标 flake 在六种负载下**一次都没复现**，因此**没有修它**：在没有机制的情况下改代码，
  只是把一次未知的失败换成一次未知的成功。
- 但**同一套件里另一条 flake 被量出来了**（3.3%），它可复现、有明确根因、修复后归零，
  并且进了树 —— 这是本行实际交付的东西。
- 顺带让下一次发生时可诊断：`multi.im` 现在除了 `ping_ok=` 还会打印
  `ping_ms=<p1>,<p2>`，**指明是哪个 hub、返回值是多少**。原来那个裸布尔值什么都说明不了，
  这次调查之所以没有结论，一半原因是现场只留下一句 `ping_ok=false`。

## 10.39 全方位语言审计与四条执行通道（2026-10）

目标要求四件事：全方位检查语言 bug、比较解释运行 / JIT / 编译运行 / C++ / Rust 的效率与资源占用、形成优化方案。交付物是 [AUDIT.md](AUDIT.md)，本节记录过程里**值得留下的判断**，细节不复述。

### 手写探针找不到的东西，模糊测试找到了

本轮最严重的一条 —— `and` / `or` 在两个后端返回不同的东西 —— **不是手写探针找到的**。解释器的编译器把 `and`/`or` 编译成短路跳转并返回操作数（`src/compiler/compiler.c:648-670`），AOT 编译成布尔（`src/compilation/aot_native.c:334-339`）；实测 `31 or 1` 解释器给 `31`、AOT 给 `true`。 ★ **本行是【记录】，坐标读于写下它的那一刻**；解释器今天已按用户裁定改成布尔（`d4b65c6`）—— 按内容找，不按号。

**最难察觉的地方在于**：VM 的 `L_AND`/`L_OR`（`src/vm/vm.c:3166-3179`）**确实是布尔语义的**，但编译器根本不发它们 —— `OP_OR` 声明在 `bytecode.h` 里却**全仓从未被 emit**，`OP_AND` 只在链式比较里用。所以「去读 VM 代码」会得出与运行时**相反**的结论。而 `docs/API.md` 只说这两个是「逻辑」，没说返回操作数还是布尔 ⇒ **两边都符合文档，都不报错**。★ **本行是【记录】，坐标读于写下它的那一刻**；★ **「全仓从未被 emit」与「只在链式比较里用」今天都已变假**（`d4b65c6` 让解释器按布尔编译，`OP_OR` 因此有了发射点）—— **按内容找，不按号**。

`x = a or default` 是常见的默认值写法，它在解释器下有值语义、在编译产物里变成布尔。这条能活到现在，靠的正是「文档没规定」与「读代码会读错」这两件事叠在一起。

为此写了 `tools/im_diff_fuzz.py`：生成随机程序，同一份源码送进解释器与 AOT（`aot-native translate` → `cc -O2`），逐对比较。**150 例（seed 7）里 109 例一致、31 例分歧、10 例一边拒绝一边没拒绝 —— 41/150 = 27.3%。** 按根因归类这 41 例：**28 例含 `and`/`or`、13 例含 int32 边界常量或 `%`、0 例无法归因。**

**「0 例无法归因」比总数更重要** —— 它说明背后没有第五个未知机制，四条已记录的缺陷就解释完了全部 41 例。

### 三条缺陷，两条是静默算错

`%` 在浮点路径把两个操作数截成 32 位 int（`src/vm/vm.c:3815` 的 `L_MOD`），`(int)da` 越界是 UB，x86-64 的 `cvttsd2si` 给 `0x80000000` ⇒ 静默负结果。**四个预测值逐一命中**（`2147483648 % 7`→`-2`、`3000000000 % 10`→`-8`、`2147483648 % 3`→`-2`、`2330089441 % 2147483647`→`-1`）——预测先写、结果后到，这比「找到一个反例」有力得多。AOT 的 `nv_mod` 用 `long long`，**同一源码给 `182605794`**，于是同门语言的两个后端对同一程序给出不同答案且都不报错。

第二条同源：整数运算**静默退化成 double**。`(2147483647 + 1).type` 是 `float`，于是 2^53 以上失精（`9007199254740992 + 1` 得到 `9007199254740992`）。**只有字面量越界会警告，计算溢出完全静默** —— 这是这条缺陷真正的形状：不是「算错了」，而是「在某个规模上悄悄开始算错」。

第三条是 `src/vm/vm.c:3637` 的**空体 `if`**，它把下一行的函数索引越界检查吃成了自己的函数体，守卫只在函数名以 `h` 开头时执行，且首个条件自身就先越界读了一次。

### 第一次测出来的性能数字是假的

朴素累加循环被 `rustc -O` 闭式化简：`arith.rs.bin` 在 N=10M/50M/100M/200M 上**恒为 0.74ms**（比值 1.02/0.98/1.01），而 AOT/C++ 是 0.36/0.34 ns/iter 且随 N 线性增长。按第一版测法，Rust 会以「比解释器快 3522 倍」被写进报告 —— 那个数字量的是「打印一个数有多快」。

修法不是加注意，是**加闸门**：每个通道额外在 N 与 2N 各测一次，时间不增长 ≥1.4 倍即报 `OPTIMIZED AWAY` 并丢弃该通道。`arith` 因此**刻意留在工作负载集里当负对照**，它的 Rust 通道每次都必须被拦下 —— 一条闸门如果没有一个已知必须被它拦下的案例，就没人知道它是否还活着。

### 两道闸门都各自抓到过东西

**正确性闸门**要求同一 N 下四个通道打印同一个整数。它不是我加的装饰：设计 `lcg` 工作负载时，乘数取 31、模数取 1000003（31 × 1000002 = 31,000,062 < 2^31）**正是为了让乘积留在 int32 内**，否则解释器会踩到上面那条 `%` 缺陷、闸门会正确地拒绝给解释器报时间。基准参数的选择被缺陷反向约束了，这件事本身值得记。

**缩放闸门**抓到了 Rust，也抓到了我自己两次：`fib` 是指数递归，按 2N 缩放会把 `fib(66)` 送进地质时间（改成 N+1，黄金比 ~1.62 已过阈值）；缩放用的 argv 必须按「trip count 是最后一个元素」构造，我写成 `[argv[0], n2]` 丢了脚本路径，症状是**每个工作负载都报 `interpreter: exit 1`** —— 一个看起来像引擎故障、实际是测试脚手架故障的信号。

### 结论

- **AOT 比解释器快 7.7×–146.6×**，但**相对手写 C++ 从持平到慢 14 倍**（`fib` 14.1×，调用密集最弱）。
- **解释器常驻内存是原生通道的 3 倍**（68.5 MB vs 23.2 MB），多出的 ~45 MB 是每个线程/任务启动时 malloc+memset 的 64 MiB 寄存器文件，**与工作负载无关**（五个负载的解释器 RSS 都是 68.4–68.5 MB）。
- **`--jit` 不是通道**：`src/vm/jit_mode.c` 全文 15 行、执行路径零读、三种取值字节码 md5 相同、墙钟全在噪声内。它在 `docs/STATUS.md` 与 `docs/API.md` 里早已被记为不得当作加速，本轮只是把三证补齐。

优化方案 12 条按 效果÷风险 排在 [AUDIT.md](AUDIT.md) §5。**明确不值得先做的是 NaN-boxing**：全局变量与寄存器两处的每指令成本相同（都是 ~6 ns/instr），支配项是派发次数与指令条数，不是 32 字节结构体的宽度 —— 先做结构体特化会在错误的层上优化。

四条缺陷**尚未修复**，只立项与取证；修复它们的顺序、判据与「修完要回头重新审视 `tools/aot_native.test.py` 里三条 `DIVERGENCE` 钉死项」都写在 [AUDIT.md](AUDIT.md) §5 与 §5 末尾。

---

## 10.40 越界调用索引的段错误，与 wasm 通道的第一次实测（BOARD 行 134）

### 缺陷本身

`src/vm/vm.c:3637` 原本是这么写的：

```c
if (fidx >= 0 && fidx < root->func_count && root->func_names[fidx] && strncmp(root->func_names[fidx], "h", 1) == 0)
if (fidx < 0 || fidx >= root->func_count || root->funcs[fidx] == NULL) { … }
```

第一行**没有花括号、也没有语句**，所以第二行那个完整的越界检查成了它的**语句体** —— 只有函数名以 `h` 开头时才执行。其余情况下代码继续走到 `:3666` 的 `root->func_argc[fidx]`，越界读。

**一处自我更正**：审计初稿还写了「第一个条件自身就对越界 `fidx` 做 `root->func_names[fidx]`，在检查之前就已经越界读」。**这是错的** —— C 的短路求值保证 `root->func_names[fidx]` 只在边界检查通过之后才求值。缺陷只有上面那一条。

### 触发不需要恶意字节码文件

`vm_exec` 接受运行期手工构造的字节码（`src/runtime/vm_exec_builtin.c` 的 `bc_from_data` 读 `code`/`strings`/`floats`/`funcs`），所以五行 `.im` 就够：

```im
bc = {"code": [[34, 999, 0, 0], [33, 0, 0, 0]], "strings": [], "floats": [], "funcs": []}
say vm_exec(bc)
```

（`OP_CALL_FUNC` = 34、`OP_HALT` = 33，见 `src/compiler/bytecode.h:8-28`，写于 [obs: 7f3473a]；`funcs` 为空 ⇒ `fidx = 999` 全面越界。）

| | 修复前 | 修复后 |
| --- | --- | --- |
| `./build/inimerse --no-mods badfidx.im` | **`EXIT=139`（Segmentation fault）** | `EXIT=1`，stderr 报「无效函数索引 999」 |

### 双向验证是测试级的，不是探针级的

进树回归 **`tools/vm_bad_fidx.test.py`**（ctest 名 `vm_bad_fidx_regression`，`EXP_CTEST` 103 → 104）断言三件事：不被信号杀死、退出码非 0、stderr 里出现 `999`；另加一个**对照程序**（`func f() { return 7 }` + `say f()` 必须 rc=0 且 stdout 含 `7`），使「VM 拒绝一切」无法蒙混过关。

把守卫按原样重新包回 `if (... strncmp(root->func_names[fidx], "h", 1) == 0)` 后重建：

```
the out-of-range call index crashed the VM (signal 11); the L_CALL_FUNC guard is not running.
```

恢复修复后：`vm bad fidx: ok (out-of-range call index refused, control call works)`。

**一个中间教训**：第一次打断时我给那个外层 `if` 加了 `{`，于是 vm.c **编译失败**，而测试**照样通过** —— 它跑的是上一次构建留下的旧二进制。`cmake --build` 的退出码必须看，不能只看测试结果。

### wasm 通道：从「读码结论」到「实测结论」

审计 §2 此前写的「wasm 与 AOT 一致」**基于读码**，当时就标注了这一点。本轮把它变成实测，代价是发现 wasm 通道**不能传 argv**：`tools/wasm_run.js:103` 调的是 `inst.exports.inimerse_run(0)`，而 wasm MVP 子集**没有 `args()`** —— 模板首行 `N = int(args()[0])` 让五个工作负载全部报

```
error: wasm MVP subset: function 'int' not found (builtins are not in the wasm MVP subset) (line 1)
```

解法是把绑定行**替换成字面量**再重新编译。这带来一个结构后果：通道不能再被当作「固定 argv」，`tools/perf_channels.py` 里每条通道变成一个 `rebuild[chan](m) -> (argv, err)`，解释器/原生通道只是换 argv，**wasm 通道重新编译**；缩放闸门随之从 `argv[:-1] + [str(n2)]` 改为调用 `rebuild`。

**五负载答案在 interpreter / aot / wasm / c++ 之间逐位一致**：`199999990000000` / `25002330004640` / `3524578` / `469475` / `750656284`。

| 工作负载 | N | interpreter | aot | **wasm** | c++ | rust |
| --- | --- | --- | --- | --- | --- | --- |
| `arith` | 20,000,000 | 0.845s (1.00x) | 0.005s (155.07x) | 0.310s (2.72x) | 0.006s (151.68x) | *闸门拦下* |
| `branch` | 10,000,000 | 0.966s (1.00x) | 0.078s (12.42x) | 0.450s (2.15x) | 0.033s (29.34x) | 0.034s (28.56x) |
| `fib` | 33 | 0.712s (1.00x) | 0.082s (8.63x) | 0.160s (4.44x) | 0.005s (140.80x) | 0.010s (73.45x) |
| `lcg` | 20,000,000 | 1.164s (1.00x) | 0.083s (14.02x) | 0.425s (2.74x) | 0.062s (18.63x) | 0.065s (18.02x) |
| `nested` | 3,400 | 0.711s (1.00x) | 0.044s (16.06x) | 0.313s (2.27x) | 0.033s (21.77x) | 0.037s (19.38x) |

`RC=1` 是设计使然：唯一的 PROBLEM 是 `arith/rust: OPTIMIZED AWAY`，即负对照被正确拦下，不是故障。

**结论**：wasm 只比解释器快 **2.15×–4.44×**、比 AOT 慢 **2×–57×**。最刺眼的是 `arith` —— 纯累加循环上只有 2.72× 而 AOT 是 155×（**差 57 倍**）。这不能归因于循环被折掉（缩放闸门已验证 wasm 的耗时随 N 增长），只能说明 wasm 后端的算术发射质量差；已立为优化项 **O13**。RSS：wasm **49.9–50.3 MB**，但**含 Node 宿主进程**，不可与原生通道（23.0–23.1 MB）直接比。

### 验收

`rm -rf build` 后完整九阶段 `tools/gate.sh --jobs 4` → `GATE_RC=0`、`gate: OK — every stage passed.`，九阶段全 PASS：build / ctest（**104/104**，0 skipped）/ economy 39/39 / node 12/12 / dsh-inimerse plugin / oauth_loop **75/75** / userdata ignore rules / docs links（92 文件 377 链接 0 broken）/ doc-paths（15 文件 283 引用 0 broken）。

---

## 10.41 底层去C化设计：同一个语义的六个决定点（BOARD 行 135）

**交付** [DECFY_DESIGN.md](DECFY_DESIGN.md)（新建）。本行**只出设计、不改任何引擎源码**；BOARD 行 135 的状态按规矩写 `进行中`，设计文档不等于交付实现。

**这一行要纠正的直觉**：去C化的难点不是「哪个文件是 C 写的」，而是**同一条语言语义在仓库里有多个独立决定点，且这个分裂横切实现语言**。最硬的证据是两个 `.im` 文件互相矛盾：

- `selfhost/compiler.im:254-266` —— `and`/`or` **值语义**（与 C 侧 `src/compiler/compiler.c:648-670` 同形：`OP_JUMP_IF_FALSE`/`OP_JUMP_IF_TRUE` + `OP_MOV result, right`）； ★ **本行是【记录】；而「同形」这句今天反了 —— C 侧与 `selfhost/compiler.im` 今天都是布尔（`d4b65c6`）。**
- `selfhost/eval.im:114-123` —— 同一对运算符 **布尔语义**（`if !truthy(l) { return false }`）。

⇒ **「用 `.im` 重写」本身不消除分歧，只是改变分歧发生在哪两个文件之间。** 同语言内部同样分裂：C 侧 `compiler.c`（值）对 `vm.c`/`aot_native.c`/`wasm_backend.c`（布尔）。

**六个决定点**（全部逐行读过，`[读码]`）：`src/compiler/compiler.c:648-670`（C·值）、`selfhost/compiler.im:254-266`（`.im`·值）、`src/vm/vm.c:3166-3179`（C·布尔）、`src/compilation/aot_native.c:334-339`（C·布尔）、`src/compilation/wasm_backend.c:769-786`（C·布尔）、`selfhost/eval.im:114-123`（`.im`·布尔）= **2 值 : 4 布尔**。 ★ **本行是【记录】，且它与 `docs/BOARD.md` 的 `decfy-design` 行是同一张表写了两遍 —— 两份一起过期，今天读是 `0 值 : 6 布尔`。**`%` 有 **3 个**：`src/vm/vm.c:3815-3824`（32 位）、`src/compilation/aot_native.c:208-211`（64 位）、`src/compilation/wasm_backend.c:938`/`:960`（64 位）。

**注释不可当证据**：`src/compilation/aot_native.c:334-339` 的注释声称 "C's && and || short-circuit exactly as the interpreter's do"，与该处行为**相反**；`src/compilation/wasm_backend.c:6` 声称 "mirror the C VM exactly"，而其 `:9` 描述的 `L_MOD` 形状与自己的 `:938`（`W_I64_REM_S`）矛盾。这正是分歧能活到现在的原因。

**根因比 `%` 更广**：`src/vm/vm.h:24-26` 的 `Value` 是 32 字节且整数载荷 **32 位 `int`**，而 `src/compilation/aot_native.c:172` 的 `NV` 整数是 **64 位 `long long`** —— VM 32 位 vs AOT 64 位是**系统性宽度分裂**，`%` 只是它的一个可观测面，与 §1.2 的 int32→double 提升同源。

**方向**：不是换语言，而是**让所有后端消费同一个 IR（字节码）**。今天五个消费者都是 `Expr*` 进、各自判断（`src/compiler/compiler.c` → 字节码、`src/compilation/aot_native.c` → C、`src/compilation/wasm_backend.c` → wasm、`selfhost/compiler.im` → 字节码、`selfhost/eval.im` → 值）。AOT/wasm 改吃字节码后，`and`/`or` 在字节码里已定死，后端只剩 `opcode → 目标指令` 的映射表。**顺带发现**：`docs/archive/RELEASE_0.5.0.md:48` 早已书面宣称「**All backends (interpreter, AOT, Wasm) share the same bytecode format**」—— 代码并不满足这条 ⇒ 本设计是**兑现既有承诺**，不是新造需求。`OP_OR`（`src/compiler/bytecode.h:12`）**全仓零 emit**；`OP_AND` 唯一 emit 在 `src/compiler/compiler.c:827`（链式比较 `1 < x < 10`，需要布尔）⇒ **`OP_OR` 可删、`OP_AND` 必须保留**，这与 `AUDIT.md` §5 的 O0 是两个独立决定。

**「五类暂时搬不动的 C」各给最强理由**：①`src/vm/vm.c`（5007 行）is substrate；②`src/platform/`、`src/runtime/runtime_posix.c`、`src/common/` 是 `.im` 无权表达的宿主能力；③`src/compilation/aot_native.c:168` 的 `kPreamble` 产出**宿主 C 源码**且必须经 `cc`；④**引导链是唯一不能靠重写消除的依赖** —— 没有 C VM 就没有运行 `.im` 的东西，这决定了去C化有理论上界；⑤`Value` 的宽度是 VM 寄存器 / AOT 生成的 C / wasm 线性内存槽 / 固定导入表（`src/compilation/wasm_backend.c:13-15`）**共同的形状**，且 `docs/archive/ROADMAP_3.1.md:23-25,28` 要求 BigInt / 窄化整数 / 枚举值在模块 ABI 与序列化中**可逆**。

**与 `AUDIT.md` §5（现为 O0–O13）不冲突**：本设计**采纳 O0 选项①**（值语义胜出），并把该决定落到唯一语义表；O11 把「`selfhost/compiler.im` 必须与 `src/compiler/compiler.c` 保持一致」列为**风险**，本设计把它当作**待根治的缺陷本身**（§1.1 证明重写不消除分歧）；O13 诊断 wasm 后端算术发射质量差、怀疑每次算术都把 `Value` 装箱再拆箱 —— 这是 §3 的第二个独立论据（分歧与发射质量差同一个根因：缺统一 IR 层）。

**诚实度**：本文档的 `[实测]` 栏是**「无」** —— 本会话**未构建、未跑 `tools/im_diff_fuzz.py`、未跑任何执行通道**，全部结论为 `[读码]`；引用的 fuzz/性能数字均标注为 `[转述]`（来自 `AUDIT.md`，非本会话实测）。`AUDIT.md` §1.0 的「四行表格两后端逐格相同」与 `tools/im_diff_fuzz.py` 归零都**仍是待做的验证**，不是已完成事实。

## 10.42 把三条「只取证未修复」的语义缺陷修掉，判据换成三后端逐格比对（BOARD 行 136）

对应 `AUDIT.md` §1.0 / §1.1 / §1.2 与方案 **O0 / O1 / O2(a)**。这一节的重点不是「改了哪几行」，而是**判据怎么从「读码结论」变成「可复现的机器判定」**。

### 先测爆炸半径，再动语义（这是本节最值钱的一步）

用户裁定 O0 走**布尔语义**（`and`/`or` 一律产出真值、保留短路）之后，第一步不是改代码，而是量清楚**改它要付多少**。`git ls-files '*.im'` 全量分类：

| 类别 | 数量 | 布尔语义下 |
| --- | --- | --- |
| 条件位置（`if`/`while`/`elif` 行） | **71** | 不受影响 |
| 值位置但两侧已是布尔（`ok = ok and x == 7`） | **27** | 不受影响 |
| 字符串/注释里的假命中（`"and works"`、`"op": "or"`） | **11** | 无关 |
| **真正会变的值选择** | **1** | `t_sugar_desugared.im:7` 的 `k = 0 or 1` |

⇒ 代价是 **1 个文件**，而且它被 git 跟踪却**不被任何 CTest 或门禁引用**（`grep -rn t_sugar_desugared CMakeLists.txt tools/` 零命中）⇒ **门禁零回归**。改后实测该文件由 `j=3 k=1` 变 `j=3 k=true`，与预测一致。**没有这张表，「布尔语义」只是一个听着合理的方向；有了它，它才是一个可以执行的决定。**

### 三处修复

**O0（`and`/`or`）**：解释器 `src/compiler/compiler.c:648-672` 改为「跳转路径上 `result` 已知为真（and）/假（or），故 `OP_AND`/`OP_OR` 对两操作数求真值**恰好等于右操作数的真值**；短路路径发 `OP_LOADK_BOOL` 常量」——**短路被保留**（右子树只在被取路径上编译）。`selfhost/compiler.im:254-268` 同形状改写。wasm 的拒绝（原 `src/compilation/wasm_backend.c:1237-1240`）改为**复用已有的 `cg_cond`**（它本来就发真值且含短路）+ `e_store_bool_from_stack`；AOT 本来就是布尔、**未改**。

**顺带抓出第二个既有真缺陷：wasm 的 `not` 是反的（**当时**；今天已修，判据见 `docs/AUDIT.md` §1.28 那一节）。** `cg_cond` 的 `TOK_NOT` 分支已经做了一次 `i32.eqz`，`cg_expr` 的 `TOK_NOT` 分支又调 `cg_cond` 再 `eqz` 一次 ⇒ 双重否定。六条 `not` 用例**全部恰好相反**（`not 0` 在 wasm 上打 `false`，而解释器与 AOT 都打 `true`）。`git diff` 证明该分支未被本次改动触及，**是既有缺陷**，只是从来没人同时跑两个后端所以没被发现。

**O1（`%`）**：`src/vm/vm.c` 的 `L_MOD` 有三重问题 —— 把两操作数窄化过 32 位 `int`（`2147483648 % 7` → **-2**，AOT `2`、wasm `1`）、**在截断之后**才判零（`7 % 0.5` 误报 `division_by_zero`，而真值 0 完全合法）、以及 `-2147483648 % -1` 实测 **rc=136 `Floating point exception`**（真崩溃，不是干净拒绝）。AOT 侧 `nv_div` 除零静默给 `inf` —— **AOT 此前完全没有错误机制**（`grep nv_error|nv_throw|nv_panic|abort()` 在 `src/compilation/aot_native.c` 零命中）。修法：VM 新增饱和转换 `im_dbl_to_i64()`、零检查**移到窄化之前**、`y == -1` 特判、结果超出 int32 时提升为 float（照抄 `L_NEG` 对 `INT_MIN` 的既有先例）；AOT 新增 `nv_die_division_by_zero()`；wasm 用 `e_trunc_sat_i64`（`0xFC 0x06`）替代 `e_trunc_sat_i32`。

**O2(a)（整数静默提升可诊断）**：三处提升点 `src/vm/vm.c:2950`（`L_ADD`）/`:3073`（`L_SUB`）/`:3097`（`L_MUL`）形状相同且完全静默。配套缺陷是 `src/main.c:1242-1247` 的 `--lint` **恒返回 0** —— 而 `lint_check` 本身是真的（它在 `src/lint_mod.c` 里，签名 `int lint_check(const char *path, char *out, int cap)`，体内 `int n = lint_scan(path, &lb);` —— 这里引形式不引行号：原来的行号已经漂了，而签名与那句委托还在原地），所以 `vtest/lint_case_*_v04.im` 五个 fixture 里有四个「即使 lint 报告再多也永远绿」。修法：退出码承载判据（**1 = 有发现 / 2 = 文件不可读 / 0 = 干净**）。

### 判据：三后端逐格比对，不是「值对了」

新增两个进树回归，形状相同 —— 同一份源码在**解释器 / AOT / wasm 三个后端各跑一遍，然后互相比对**：

- `tools/logic_semantics.test.py`（**34 例**：值位置 8 / 真值性 4 / 优先级 2 / 链式嵌套 3 / `not` 7 / 条件位置 6 / 短路 4）（§10.44 起该文件增至 **64 例**）；
- `tools/mod_semantics.test.py`（**16 例** `%`/`/` 边界，含 `REFUSED` 哨兵）。

**短路必须被双向证明**：四条 `SHORT_CIRCUIT` 用**除零当副作用**（`z = 0` 前置，`0 and (1/z)` → `false`、`1 and (1/z)` → **REFUSED**、`1 or (1/z)` → `true`、`0 or (1/z)` → **REFUSED**，三后端一致）。理由是「不做短路的那个方向必须拒绝」——否则「右操作数根本没跑」也会伪装成通过。用除零而非字符串是因为 AOT 与 wasm 都拒绝字符串，而「全局变量在函数里赋值」是本仓**已钉住的分歧**（`global_write_from_func`），两者都当不了见证。

**为什么这个形状值钱**：`not` 那条缺陷在解释器和 AOT 上一直是对的，**任何只测一个后端的测试都不会发现它**。

### 双向验证（每条修复都验过两个方向）

| 动作 | 结果 |
| --- | --- |
| 还原 wasm 的 `not` 修复（加回多余的 `eqz`） | `logic_semantics` **rc=1，6 条 FAIL** |
| 把 `compiler.c` 的布尔发射改回 `OP_MOV result, right` | `logic_semantics` **rc=1，11 条 FAIL** |
| 把 AOT 的 `if (y == 0) nv_die_division_by_zero();` sed 成 `if (0)` | `mod_semantics` **3 条 FAIL（`aot died from a signal`）、rc=1** |
| `--lint` 用 `lint_always0.sh` 复现修复前行为 | `lint_expected` **`FAIL (5 problem(s))`、rc=1** |
| 全部还原 | **rc=0，全绿** |

### 测试自己抓出了一个「把 bug 钉死的期望」

首跑全量 ctest：**107 例 1 失败**，`34 - wasm_backend_regression`。原因是 `tools/wasm_backend.test.py` 的 `REJECT_CASES` 里有一条：

```python
("and_outside", 'x = true and false\n', "'and'/'or' outside a condition"),
```

**它把 O0 要消除的拒绝行为当作正确行为钉住了** —— 报错是 `AssertionError: and_outside: expected compile-time rejection`。修法不是在 `CASES` 里钉一个新字符串，而是加两条**与解释器输出比对**的等价性用例（`and_or_value`、`not_value`），断言强度更高：钉字符串只保证「不退化回旧的」，比对解释器保证「两个后端说的是同一句话」。现为 `wasm backend: ok (24 equivalence cases, 2 rejections, 3 explicit failures, simd bench equal)`。

### 计数与门禁

`grep -c 'add_test(' CMakeLists.txt` = **107**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **106 → 107**（新增 `logic_semantics_regression`，ctest 中是 **#56**、14.12 s）。全量 **`100% tests passed, 0 tests failed out of 107`**（`CTEST_RC=0`）。完整 `tools/gate.sh` 九阶段 PASS。

### 与 §10.41 的一处冲突（必须记下来）

`docs/DECFY_DESIGN.md` **采纳了 O0 选项①（值语义胜出）**，并把「`selfhost/compiler.im` 必须与 `src/compiler/compiler.c` 保持一致」当作风险。**该决定已被用户推翻** —— 用户（m13513）裁定**布尔语义**。所以：

- `DECFY_DESIGN.md` §1.1 里「值语义胜出」这一句现在是**过期结论**，与本节冲突时**以本节为准**（本节是已落盘、已过测试的实现）；
- 但该设计文档的**核心论点不受影响、反而被本节加强**：它说「难点是同一语义有多个决定点、分裂横切实现语言」，本节正是这句话的实测版 —— 修 `and`/`or` 要**同时**动 C 编译器、`.im` 编译器、wasm 后端**三处**，而 `selfhost/eval.im`（`.im`·布尔）**不需要动**，因为它本来就与新语义一致。**决定点的数量就是修复要碰的文件数**。

### 诚实边界

- `tools/im_diff_fuzz.py` **仍未接进门禁**（怎么接、什么阈值，用户未定）。
- O2 的 BigInt / 小整数快速路径**未做**（`docs/archive/ROADMAP_3.1.md` 已规划）。
- `lint_scan` 是**逐行扫描器、根本不解析**：它自己的例子 `x = = 5` 在 `--lint` 下 **0 行发现、rc=0**，而真跑 rc=1。修退出码**不会**让 `x = = 5` 变红 —— 这条限制已用 `tools/lint_expected.test.py` 的 PINNED 用例**钉进测试**，而不是只写在文档里。

## 10.43 `+` 链按项数换答案：`OP_CONCAT` 的整数回绕（BOARD 行 137）

**起因。** §10.42 把「三后端逐格比对」立成判据之后，收尾复核的第一件事是去问：**审计自己写下的「三方一致」样本，取样够不够长。** §1.2 拿 `2147483647 + 1` 当「整数提升三方一致」的典型 —— 它确实一致，但它是**两项链**。

**缺陷。** `src/compiler/compiler.c:715-786` 把 3 项以上的左结合 `+` 链折成**单个 `OP_CONCAT`**，2 项链走 `OP_ADD`；该处注释声称「identical per-step semantics to OP_ADD」。`OP_ADD` 用 `int64_t` 折叠、越界提升 float；`OP_CONCAT` 的整数快路径（`src/vm/vm.c:3042-3047`）直接相加两个 32 位载荷 ⇒ **回绕**。于是 `x = 2147483647` 时 `x + 1` 是 `2147483648` 而 `x + 1 + 0` 是 **`-2147483648`**。

**分歧是 2 比 1，不是 1 比 1。** AOT 与 wasm 自始至终互相一致，解释器的 `OP_CONCAT` 是唯一异类 —— 这一点与作业单的初始表述相反，是本轮**先实测再定修法**的直接结果。（作业单把它写成「解释器 vs AOT」；若照此裁，会去改本来正确的 AOT。）

**为什么漏了这么久。** 两件事同时成立才让它隐形：①`contract_test.im` §1 里**没有任何超过两项的 `+` 链**；②审计把两项链当成了整数提升的代表样本。**判据的样本长度本身就是判据的一部分。**

**修法。** `L_CONCAT` 整数分支对齐 `L_ADD`：`int64_t` 折叠，越界提升 float 并置 `acc.sval = NULL`（与相邻泛型 double 分支一致）。`acc.type == VAL_INT` 时 `acc_fold_owned` 必为 0（唯一置 1 的字符串分支先判且已 `continue`），故无需 free。**不触及** §1.2 的结论。

**判据与双向验证。** 新增 `tools/arith_chain_semantics.test.py`（CTest `arith_chain_semantics_regression`，**#57**，3.22 s，注册于 `CMakeLists.txt:357-365`），10 格 × 解释器/AOT/wasm，同时断言期望值与三方一致性。**反向验证**：把 `src/vm/vm.c` 退回 `HEAD` 重编 ⇒ **6/10 红**（恰为 3 项链与 4 项链那 6 格，无假阴无假阳）；恢复修复重编 ⇒ 全绿。所以它**不是** vacuous probe。

**门禁计数。** `grep -c 'add_test(' CMakeLists.txt` = **108**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **107 → 108**；`docs/BOARD.md` §3 与本节 §2/§2.1 一并同步为 **108 / 108**。顺带修掉一处**先存的不同步**：BOARD §3 与本节 §2 此前还停在 **104**，而 `EXP_CTEST` 已经是 107 —— 上一个会话只改了 `gate.sh`，没改表，而 `docs/BOARD.md:69` 明文要求两者同时改。

**门禁实测（本轮）。** 九阶段 **8 PASS / 1 FAIL**：`ctest (expect 108/108, 0 skipped)` **PASS**，`build` / `economy` / `node (12/12)` / `plugin (55/55 live)` / `userdata` / `links (0 broken)` / `doc-paths (0 broken)` 全 PASS。唯一失败是 `oauth_loop crate (expect 75/75)`，**exit 46，且 `cargo --version` 同样 exit 46**：

```
internal error, please report: running "rustup.cargo" failed: cannot create transient scope: DBus error "org.freedesktop.DBus.Error.UnixProcessIdUnknown": [Failed to set unit properties: No such process]
```

这是**环境的**失败（本沙箱里 systemd 无法为 rustup 创建 transient scope），不是测试失败，也与本轮改动无关 —— `git diff --name-only` 里没有任何 `Infiverse_standard/oauth_loop/` 路径。**因此本轮不能宣称「九阶段全绿」**，只能宣称「八阶段绿 + 一条环境失败的 Rust 阶段（连 `--version` 都跑不起来）」。

**附带的两个发现（均已取证，未修，另行立项）。**

1. **AOT 打印浮点丢精度。** `say(1000000.5)` → `1e+06`、`say(2147483648.5)` → `2.14748e+09`，而解释器与 wasm 打全精度。`vtest/float_precision_v04.im` 与 CTest `float_precision_runtime`（`CMakeLists.txt:739-740`）**只钉住解释器**（`src/runtime/runtime_posix.c:59` 的 `"%.17g"`）。这是**第二处** AOT 与解释器/wasm 不一致，且方向相反（这次 AOT 是异类）。
2. **`contract_test.im` 根本没进门禁。** `grep -in contract CMakeLists.txt` 只有两条无关注释，没有任何 `add_test`；它是手动/dormant 套件（本节 §9 把它列在手动套件堆里），而且**当时跑到 `contract_test.im:80` 就抛异常**：`check(list(Z[1~3])[0] == 1, "list set")` → `CONTRACT FAIL: list set`。该行来自 `8248e08`（Release Infiverse 0.2.0），是**先存**失败。所以本轮往它 §1 补的 5 条链断言**验证了契约、但没有被门禁自动执行** —— 这点必须写在明处，不能让「断言加进去了」听起来像「它被守住了」。（**均已修**：§10.47 把它注册进 CTest；§1.10 的两处集合枚举缺陷修掉后该行通过，实测 `contract: all passed`；§10.50 又把检查数从 70 提到 73。上面这句是**当时**的状态记录。）

**诚实边界。**

- 本轮**没有**修 AOT 的浮点打印，也**没有**修 `contract_test.im:80` 的 `list set`（两者都会把本轮提交撑成混合改动）。
- 作业单写的是「加进 `tools/logic_semantics.test.py`」，实际落在**新建的 `tools/arith_chain_semantics.test.py`**：`logic_semantics.test.py` 的自述是「deliberately narrow（只谈 `and`/`or`/`not`）」，而本仓既有惯例是**每个被修掉的语义缺陷配一条自己的三后端套件**（`mod_semantics.test.py` 对 `%`、`logic_semantics.test.py` 对 `and`/`or`）。新增一条 CTest 也让计数变化**显式可见**。这是**有意的偏离**，不是遗漏。（**§10.44 已按用户裁定改回**：那 10 格链用例移进了 `tools/logic_semantics.test.py`，`arith_chain_semantics_regression` 连同 `tools/arith_chain_semantics.test.py` 一起删掉了 —— 见下节。）

## 10.44 三份浮点格式化器合一 + `list(<集合>)` 差一格（BOARD 行 138）

**起因。** 本节要清掉 §10.43 结尾「附带的两个发现（均已取证，未修）」的 ① 和 ②，外加一条用户裁定：把 §10.43 临时单开的 CTest 并回既有套件。

### 一、AOT 打印浮点丢精度 → 三个后端的三份实现合一

**先实测，再定修法。** 三个后端本来各有**一份独立**的浮点格式化器：AOT 的 `nv_say` 用 `%g`，`tools/wasm_run.js` 的 `fmtFloat` 是一份 JS 重写，解释器走 `vts_double`（`src/vm/vm.c:1036-1063`）。逐格量出来是**五类分歧，不是一类**：

| `say(…)` | 解释器 | AOT | wasm |
| --- | --- | --- | --- |
| `0.9999999` | `0.1` | `0.1` | `0.1` ← **三方全错** |
| `2.0000001` | `2.` | `2.` | `2` ← 多一个点 |
| `0.0 - 0.5` | `0.5` | `0.5` | `-0.5` ← 解释器与 AOT **丢符号** |
| `0.0 - 1e-20` | `0.` | `0.` | `-0` |
| `1e20` | `1e+20` | `1e+20` | `1.e+20` ← wasm 的点位置错 |
| `123456789.125` | `123456789.125` | `1.23457e+08` | `123456789.125` ← AOT 丢精度 |
| `2147483648.5` | `2147483648.5` | `2.14748e+09` | `2147483648.5` |

三个 bug 各自独立，却都被同一个错误前提掩盖：**`AOT` 的注释断言「解释器的格式化器不可复现（它把 1e-20 打成 `0.`、6 位与 7 位精度混用），所以打印浮点是有记录的分歧」** —— 这句话被写进了 `tools/aot_native.test.py:34-37`，并据此把浮点**排除在语料外**。审计把 bug 写成了规范，于是它活了很久。

**裁定：不把解释器的 bug 移植过去。** `fmtFloat` 是三者中数学上正确的那个，于是**以它为基准**反过来修 `vts_double`，再把三份实现全部对齐到修好的规范。规范：`nan` → `"nan"`；`dv == 0.0`（含 `-0.0`）→ `"0"`；整值且在 int64 内 → 整数文本；`|dv| >= 1e15` → `%.17g`；否则去符号取幅值，`ip = (long long)a`、`frac = (long long)((a - ip) * 1000000.0 + 0.5)`，**`frac == 1000000` 时进位**（`0.9999999` 由此才不再是 `0.1`），整数部分用 `vts_int(ib, sizeof ib, neg ? -ip : ip)` 打印、**且只在 `neg && ip == 0` 时单独发一个 `'-'`**（于是 `-1e-20` 是 `-0` 而不是 `0`），`frac == 0` 直接返回（不再吐孤零零的 `.`），否则 `.` + 六位补零后修剪尾零。

**修了三处。** ①解释器：`src/vm/vm.c` 的 `vts_double` 按上式重写；②AOT：`src/compilation/aot_native.c` 的 `kPreamble` 里新增 `#include <string.h>` 与 `static void nv_fmt_double(char *buf, size_t bufsz, double dv)`（同算法，`snprintf`/`strlen`），`nv_say` 的浮点分支改为 `{ char b[64]; nv_fmt_double(b, sizeof b, a.f); printf("%s\n", b); }` —— AOT 没有 `str()`、也没有第二个打印浮点的位置，所以这是唯一一处；③wasm：`tools/wasm_run.js` 的 `fmtFloat` 重写（符号用变量的形式给出）并新增 `fmtG17(d)`（`toPrecision(17)` 后按 `'e'` 切开、修剪尾数与多余的点、再拼回指数 —— 这才是 `1e+20` 不再是 `1.e+20` 的原因）。

**自己犯的错也记一笔。** `ip` 是**幅值**，第一版把 `vts_int(..., ip)` 直接打出去，于是 `-1.5` 成了 `1.5`；被 `.verify/edge.im` 当场抓到，改成 `neg ? -ip : ip` 才过。三个后端里同一个错各修了一遍 —— 这正是「三份实现」的代价。

**判据与反向验证。** 15 条浮点等价行插进 `tools/aot_native.test.py` 的 `EQUIVALENCE`，等价用例 **46 → 65**，全量报 **`aot_native.test: 78 cases (65 equivalence, 3 pinned divergences, 10 refusal), 0 failures`**；同时把 `:34-37` 那条「把 bug 写成规范」的注释换成「`nv_fmt_double` 现在移植 `vts_double`，浮点**在语料内**」。另在 `tools/logic_semantics.test.py` 加 `FLOAT` 表 20 格（把 wasm 的 JS 格式化器也拉进门禁），该套件 **58 → 64 例**。**反向验证**：把 `tools/wasm_run.js` 的 `fmtFloat` 改坏（去符号 + 去进位）⇒ **4/64 红**，报的正是 `say 0.0 - 0.5` 期望 `-0.5` 实得 `0.5` 一类；还原（`cmp` 证逐字节一致）⇒ 全绿。**所以这些格子不是摆设。**

**第二轮（提交前自查）又抓出一类：平局的舍入规则不同 —— 这一类的修法与上面同批。** 14 格全绿之后，我又问了一次「`%.17g` 与 `toPrecision(17)` 真的等价吗」，答案是**不等价**：C 的 `%.17g` 按 IEEE 默认（round-half-to-**even**）处理平局，ECMAScript 的 `toPrecision` 规范却是「若有两个这样的 n，取**较大**者」。**这不是理论问题**：`1.0000000000000002e15` 的精确值就是 `1000000000000000.25`，取 17 位有效数字正好是平局 —— glibc 给 `1000000000000000.2`，JS 给 `1000000000000000.3`；`2000000000000000.25` 与 `0.0 - 1.0000000000000002e15` 同样。**解释器与 AOT 是同一份 glibc，所以一致，只有 wasm 那份 JS 是异类**（与 `%g` 那批的异类方向相反）。为什么 14 格没抓到：**平局要求精确十进制展开的**第 18 位**恰好是 5 且其后全零**，而 `1e20`、`12345678901234567890.0` 这些被钉住的整数都不是平局 —— 判据的**取样区间**又一次成了漏点（与 §10.43「样本长度」同型）。修法**封闭在 `tools/wasm_run.js` 一个文件里**：新增 `exactDecimal(a)`（double 必是 `m * 2^e`，`e < 0` 时即 `m * 5^-e × 10^e`，精确展开有限、BigInt 装得下），`fmtG17` 改为在精确整数上取 17 位有效数字、`2r == 10^k` 时看 `q` 的奇偶（**五成双**），再按 `%g` 规则选定点/科学计数；解释器与 AOT 不动。验证三件：①`FLOAT` 表 **14 → 20 格**、套件 **58 → 64 例**；②`aot_native.test.py` 等价 **60 → 65**；③**随机扫描 380 个 double**（六个量级区间 + `10^14…10^19` 的 `+0.25/0.5/0.75`）三后端逐格比对 **0 分歧**。**反向验证**：把平局判据改成 ECMAScript 的 `if (twice >= p) q += 1n;` ⇒ **恰好奇迹般地只有那 3 条平局格红**（`wasm printed '1000000000000000.3', expected '…0.2'`），其余 61 格全绿；还原后 `cmp` 证逐字节一致 ⇒ 64/64 绿。

### 二、把 §10.43 临时单开的 CTest 并回既有套件

按用户裁定：`tools/arith_chain_semantics.test.py` **删除**（`git rm`），它在 `CMakeLists.txt` 的注册块移除 —— **`arith_chain_semantics_regression` 这个 CTest 现在不存在了**。10 格 3 项以上 `+` 链成了 `tools/logic_semantics.test.py` 的 `CHAIN` 表（`(label, source, want)` 三元组，因为链要**先赋值再算**），该套件 **34 → 64 例**（`TABLE` 24 + `COND` 6 + `SHORT_CIRCUIT` 4 + `CHAIN` 10 + `FLOAT` 20），`EXP_CTEST` **108 → 107**。取舍说清楚：并回之后**计数变化不再显式可见**（这正是 §10.43 当初新开一条的理由），换来的是一个语义面只有一条入口、`logic_semantics.test.py` 的自述不再与内容矛盾 —— 自述里**明写**了它现在也钉 `+` 链与打印浮点。

### 三、`list(<集合>)` 差一格：`vm_set_to_array` 把 1-based 句柄交给按 raw 索引解释的调用方

**症状。** `contract_test.im:88` 的 `list set` 失败：`list(Z[1~3])` 打印 `[]`、`len(list(Z[1~3]))` 是 **0**、`[0]` 是 nil。

**根因：两套索引约定被接在一起。** `VAL_ARRAY` **值**里存的是 **1-based 句柄**（构造点 `src/vm/vm.c:3240` 的 `value_set(&R[ins.r1], VAL_ARRAY, aidx + 1, 0, NULL, NULL)`；读者都减一，见 `src/vm/vm.c:3275`、`:3323`、`:2447`），而整个 `vm_array_*` **函数族**收的是 **raw 池下标**（`vm_array_len(vm, idx)`，`src/vm/vm.c:789-795`）。`vm_set_to_array`（`src/vm/vm.c:1966`）**两条造数组的分支都以 `return aidx + 1;` 收尾**，而它的三个调用方全部按 raw 解释：`src/runtime/runtime_posix.c:19`（`len` 的集合分支，`vm_array_len(vm, a)`）、`:236`（`list`，`{ VAL_ARRAY, idx + 1 }`）、`src/runtime/runtime.c:134`（WIN32 的 `list`，`ival = r + 1`）。⇒ `list(<集合>)` 交回的是**池里的下一个数组**（刚新建、通常是空的）。　★ **同一个号上的内容在那两棵树之间变了**：号没有漂，是那一行换了；本行的坐标读于写下它的那一刻。[obs: 0ff5359a08db -> main @ 711a339]

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `list(Z[1~3])` | `[]` | `[1, 2, 3]` |
| `len(list(Z[1~3]))` / `len(Z[1~3])` | 0 / 0 | 3 / 3 |
| `list(Z[1~3])[0]` / `[2]` | nil / nil | 1 / 3 |
| `len(list(Z[1~100]))` / `len(list(N[0~10]))` | 0 / 0 | 100 / 11 |
| `list(<数组>)` | `[1, 2]` | `[1, 2]`（透传，未受影响） |

**修法。** 两处 `return aidx + 1;` → `return aidx;` + 函数头一条约定注释（raw 索引）。**改函数而不是改三个调用方**：它就坐在按 raw 索引的 `vm_array_*` 族里，且三个调用方本来就假定 raw。**注意这条缺陷只改数值、不改退出码**，所以回归只能断言数值。

**判据与反向验证。** 进树回归 `vtest/list_set_off_by_one_v05.im` + CTest `list_set_off_by_one_runtime`（`CMakeLists.txt`，`LABELS "vm;language;regression"`，`PASS_REGULAR_EXPRESSION "listset-ok n=3 first=1 last=3 setlen=3"` / `FAIL_REGULAR_EXPRESSION "n=0|first=nil|setlen=0"`）。**反向验证**：把函数里两处还原成 `return aidx + 1;` 重编 ⇒ `listset-ok n=0 first=nil last=nil setlen=0`、FAIL 正则命中 ⇒ 红；恢复重编 ⇒ `n=3 first=1 last=3 setlen=3` ⇒ 绿，`diff` 证源码逐字节一致。

### 四、`contract_test.im` 剩下的 4 条失败是结构性的，不是缺陷

用一遍自动跳过的脚本（跑套件 → 读 stderr 的 `CONTRACT FAIL: <desc>` → 注释掉该行 → 重跑）穷举出**恰好 4 条**：`str2int`、`str2int invalid -> 0`（`:120`/`:121`）、`noise range`（`:170`）、`vram accounting`（`:174`）；跳过这 4 条后该套件 `rc=0`。所以 `list set` 是它里面**唯一**的真语言缺陷。这 4 条属 io_mod / gui_mod，而 **POSIX 构建根本不编这两个 mod** —— `CMakeLists.txt:383-393` 是 `if(WIN32)`，POSIX 的 `else()`（`:394-400`）挂的是 `src/platform/posix_stubs.c`，其 `STUB_REG(...)` 是**空实现**；该套件自己写着 `# usage: inimerse.exe --time-limit 60 contract_test.im`（Windows）。

**顺带记录一条可诊断性缺陷（未修）。调用未注册的函数是静默留栈，不是报错。** `say(str2int("42"))` 打印 `42`，但 `str2int("42") == 42` 是 **false**；`noise2d(1.5, 2.5, 7)` → `7`；`gui_canvas(8, 8)` → `8`；`gui_vram_used()` → `0`（带不带 `--no-mods` 一样）。**这 4 条失败表现为「值不对」而非「未知函数」，正是上面那遍穷举只能靠注释跳过、不可能靠报错定位的原因。**

### 五、门禁实测（本轮）

**九阶段全绿 —— 这是本轮与 §10.43 的关键差别。** `build (Release, configure+incremental, -j12)` / `ctest (expect 108/108, 0 skipped)` / `economy migration (§43.5, expect 39/39)` / `node protocol suites (12/12 passed)` / `dsh-inimerse plugin (55/55 checks passed, live)` / **`oauth_loop crate (expect 75/75)`** / `userdata ignore rules` / `docs relative links (93 files, 383 links, 0 broken)` / `docs backtick paths (332 refs, 0 broken)` 全 **PASS**，末行 `gate: OK — every stage passed.`、`GATE_RC=0`。ctest 本体：**`100% tests passed, 0 tests failed out of 108`**，`gate: ctest reported 0 skipped test(s)`，`Total Test time (real) = 23.11 sec`。

**§10.43 那条环境失败这次没出现。** 上一轮同一台机器上 `oauth_loop crate` 是 **exit 46**、连 `cargo --version` 都起不来（`DBus error … cannot create transient scope`），当时只能宣称「八阶段绿 + 一条环境失败」。本轮同一阶段 **PASS**，说明那是**间歇性的沙箱/systemd 状态**而非本仓缺陷 —— 也说明单次门禁结果不能当作「这个阶段坏了」的证据，要看它是否复现。

### 六、诚实边界

- **另一条本轮发现、当时未修的独立缺陷：放进变量的集合完全不可枚举（已由 §10.45 修掉）。** `z = Z[1~3]` 会被编成 `OP_SET_INTERVAL` **加一条 `OP_NEW_SET`**，于是 `z` 是 `compCount > 0` 的 kind-0 集合，正好撞上 `src/vm/vm.c:1989` 的 `if (s->kind != 0 || s->compCount > 0) return -1;`：`len(z)` 0 / `list(z)` nil / `size(z)` nil，而 `min(z)` 1 / `max(z)` 3 / `2 in z` true **全对**（那三个走 `comps[]`）。`s2 = 1, 2, Z[7~9]` 后 `len(s2)` 是 0（应为 5）。上面那条进树回归与 `contract_test.im:88` 钉的都是**内联**形态，故本轮修复对它们成立；变量形态当时是**另一个 bug**、只在 `vtest/list_set_off_by_one_v05.im` 头部注释里写明 —— **§10.45 已将其修掉并钉进回归**。
- `list` **只存在于解释器**：AOT 与 wasm 后端都没实现（`grep '"list"' src/compilation/*.c` 为空），所以这条没有三后端比对可言，判据只能是解释器侧的数值断言。
- `contract_test.im` **仍未进门禁**。它本来就是手动/dormant 套件（`grep -in contract CMakeLists.txt` 只有两处无关注释）。本轮往它 §1 补的断言验证了契约，但**不被门禁自动执行** —— 这点必须写在明处，不能让「断言加进去了」听起来像「它被守住了」。
- `type` **是保留字**：`say type(z)` 直接编译失败（`expected 'expression', but got 'type' (type 129)`），`say size z`（不带括号）也失败（`... 'size' (type 66)`）—— 必须写成 `type(z)` / `size(z)`。这是本轮探针反复踩到的坑。


## 10.45 集合字面量是并集：三处「只走一半」的枚举（BOARD 行 139）

上一轮 §10.44 的「诚实边界」里留了一条：**放进变量的集合完全不可枚举**。本轮去修它，结果连着挖出**三个**缺陷，形态完全一样 —— 「只处理了集合的一半」。三个都修掉了，各配一条进树回归，并都做了反向验证。

### 一、一个集合有三个半边

`SetObj`（`src/vm/vm.h:36-47`）把成员分在三处：

| 半边 | 字段 | 装什么 |
| --- | --- | --- |
| `i64` | `long long *i64; int iCount, iCap;` | 整数字面量（有序，二分插入） |
| `items` | `Value *items; int count, cap;` | 非整数字面量 |
| `comps` | `SetComp *comps; int compCount, compCap;` | 区间分量（`SetComp` = 具名集合 ∩ `[lo,hi]`） |

所以 `1, 2, Z[7~9]` 不是「一个区间」，而是**三者的并集** —— 两个整数放 `i64`，一个分量放 `comps`。`L_NEW_SET`（`src/vm/vm.c:3882-3910`）对 `VAL_SET` 操作数走 `vm_set_add_comp`、其余走 `vm_set_add`，`z = Z[1~3]` 于是成为 `compCount > 0` 的 kind-0 集合。这也正是 `min`/`max`/`in` 一直正确（走 `comps[]`）而 `len`/`list`/`size` 全瞎（不展开分量）的原因。

### 二、① `vm_set_to_array` 拒绝一切带分量的集合

`vm_set_to_array` 原文以 `if (s->kind != 0 || s->compCount > 0) return -1;` 收尾。而 `posix_core_len`（`src/runtime/runtime_posix.c:11-28`）拿到 -1 后 `n` 停在 0 —— 于是「拒答」被显示成「空集」，这是最坏的一种错：**错的答案长得像对的答案**。

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `b = 1, 2, Z[7~9]` 后 `len(b)` | 0 | **5** |
| `c = Z[1~3], Z[5~7]` 后 `len(c)` | 0 | **6** |
| `d = 1, Z[1~3]` 后 `len(d)`（1 落在分量里） | 0 | **3** |
| `e = Z[1~3]` 后 `len(e)` | 0 | **3** |
| `f = Z, 1` 后 `len(f)` / `list(f)`（真无限） | 0 / nil | 0 / nil（**仍拒绝**） |
| `r = R[0~3]` 后 `len(r)` / `size(r)`（R 无格点） | 0 / nil | 0 / nil（**仍拒绝**） |

`list(d)` 是 `[1, 2, 3]` 而不是四个 —— **必须去重**，字面量可能落在分量里。拒绝的边界必须原样保住：分量只有**两个有限端点且有格点**才可枚举，而 `vm_set_add_comp` 给「整个具名集合」写的哨兵 `(lo,hi,loInc,hiInc) == (0,0,0,0)`（`set_contains` 读作「无界」）因此仍返回 -1。

### 三、② 浮点点阵按累加步长走，漏掉最后一个成员

`FloatN`/`floatN` 的成员是 `k / 10^N`，**必须按格点下标走**。原实现累加 double 步长，而 `0.1 + 0.1 + 0.1` 是 `0.30000000000000004`，**严格大于** double `0.3`，于是区间最后一个成员被循环条件甩掉 —— 而 `in` 直接问 `builtin_contains`，仍然认它。**同一个集合上 `in` 与 `list` 自相矛盾**，这比单纯算错更难发现：

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `s = float1[0~0.3]` 的 `(0.3 in s)` | true | true |
| `len(s)` | **3** | **4** |
| `list(s)` | **`[0, 0.1, 0.2]`** | **`[0, 0.1, 0.2, 0.3]`** |
| `len(Float1[0~0.3])`（大写，不含整数） | **2** | **3** |
| `len(float1[0~0.5])` | 5 | **6** |

修法是按 `k = ceil(lo·10^frac) … floor(hi·10^frac)` 走下标、`x = k / 10^frac`，显式重测端点包含性，`builtin_contains` 仍参与过滤。另外设了两道**拒绝而不是截断**的闸：`kh - kl >= 10000000`、以及端点绝对值超过 `9.0e18`（整数格点另有 32 位 `VAL_INT` 边界）。`floatN` 含整数、`FloatN` 不含，所以同一区间两者基数不同 —— 这既是修法的对照，也是 `builtin_contains` 仍在生效的证明。

### 四、③ `vm_set_add_comp` 不复制 `src->i64`：嵌套字面量丢整数半

`vm_set_add_comp` 在 `src->kind == 0` 分支里复制了 `items` 与 `comps`，**唯独没有 `i64` 循环**：

| 探针 | 修前 | 修后 |
| --- | --- | --- |
| `p = 1, 2, 3` 后 `len(p)` | 3 | 3 |
| `q = p, 4` 后 `len(q)` | **1** | **4** |
| `list(q)` | **`[4]`** | **`[1, 2, 3, 4]`** |
| `m = 1.5, 2.5` / `n = m, 3.5` 后 `len(n)` | 3 | 3 |

最后一行是**对照**：非整数走 `items`，所以它一直是对的 —— 这正是「只走一半」在修前能藏这么久的原因。

### 五、判据与双向验证

三个缺陷都**只改数值、不改退出码**，所以回归一律断言数值、不看 `rc`：

- `vtest/set_components_enumerable_v05.im` ← CTest `set_components_enumerable_runtime`，成绩行 `setcomp-ok union=5/1,9 two=6/5 overlap=3/3 single=3 nested=4/4 size=3 infinite=0,nil`；`FAIL_REGULAR_EXPRESSION "union=0|overlap=0|single=0|nested=1|size=nil"`。一行同时钉住并集基数、首末元素、重叠去重、单分量包一层、嵌套、`size`，以及**真无限仍被拒**。
- `vtest/set_float_lattice_v05.im` ← CTest `set_float_lattice_runtime`，成绩行 `setlattice-ok n=4 last03=true in03=true upper=3 upper0=false half=6 int=6 big=100`；FAIL 正则 `n=3|last03=false|in03=false|upper0=true`。`last03=true` 即「`list` 的最后一个成员确实等于 `0.3`」，与 `in03=true` 合起来正是修前那条自相矛盾的反面。（末元素靠**比较**断言，因为 `str()` 走 `%.17g`，会打出 `0.29999999999999999`。）

**反向验证**：`git checkout HEAD -- src/vm/vm.c src/runtime/runtime_posix.c` 重编 ⇒ `ctest -R "set_components_enumerable|set_float_lattice"` 得 **`0% tests passed, 2 tests failed out of 2`**，两条**各自命中各自的 FAIL 正则**（`Regex=[union=0|overlap=0|single=0|nested=1|size=nil]` 与 `Regex=[n=3|last03=false|in03=false|upper0=true]`）；拷回重编 ⇒ 两条 `Passed`，`cmp` 证源码逐字节一致。**不是 vacuous probe。**

**顺带统一**：`posix_core_size` 原先自己算 `(hi-lo)/step + 1`，现在改走同一个枚举器。于是 `size(<集合>)` 与 `len(<集合>)` 在复合/浮点形态上不再可能给出两个答案 —— 修前 `z = Z[1~3]` 的 `size(z)` 是 nil 而 `len(z)` 是 0，本身就是这种分裂的极端例子。

### 六、门禁实测（本轮）

`grep -c 'add_test(' CMakeLists.txt` **108 → 110**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **110**，`ctest -N` 的 `Total Tests: 110`（`Test #109: set_components_enumerable_runtime`、`Test #110: set_float_lattice_runtime`）。全量 **`100% tests passed, 0 tests failed out of 110`**；完整九阶段 `tools/gate.sh` **全 PASS**、`gate: OK — every stage passed.`、`GATE_RC=0`、`gate: ctest reported 0 skipped test(s)`。

### 七、诚实边界

- `list` **只存在于解释器**：AOT 与 wasm 后端都没实现（`grep '"list"' src/compilation/*.c` 为空），所以这三个缺陷没有三后端比对可言，判据只能是解释器侧的数值断言 —— 与 §10.44 那条 `list` 缺陷同源。
- 枚举上限的两道闸是**拒绝**而非截断：分量跨度超过 `10^7` 格、或端点绝对值超过 `9.0e18`，`list`/`len` 会回到「拒绝」而不是给一个截断的集合。这是刻意的取舍（宁可说不知道，也不给半截答案），但**没有回归钉住这两道闸** —— 已知空白。
- `contract_test.im` **仍未进门禁**（手动/dormant 套件），也未随本轮扩充。
- **调用未注册函数是静默留栈而非报错**（`say(str2int("42"))` 打印 `42`，但 `str2int("42") == 42` 为 false）—— 取证在 `docs/AUDIT.md` §1.9，**已由 §10.46 修掉并钉进回归**。

---

## 10.46 调用未注册的内建函数不再静默返回栈顶（BOARD 行 140）

### 一、症状：不报错，反而把最后一个实参交回来

POSIX 上 `io_mod_register` 是空桩（`src/platform/posix_stubs.c`），所以 `str2int` 这个内建**确实不存在**。调用它不报错：

| 探针（修前） | 输出 |
| --- | --- |
| `say "A:" + str(str2int("42"))` | `A:42` —— 拿到的是**字符串** `"42"` |
| `say "B:" + str(str2int("42") == 42)` | `B:false` |
| `say(str2int("abc"))` | `abc` |
| 退出码 | **0** |

这正是 §1.9 记录的那 4 条 `contract_test.im` 失败表现为「值不对」而不是「未知函数」的原因 —— 也是那遍穷举只能靠**注释跳过**、不可能靠报错定位的原因。

### 二、根因：`if (bi >= 0)` 没有 `else`

`L_CALL_BUILTIN`（`src/vm/vm.c:3519`）先 `int bi = builtin_lookup(vm, name);`（`builtin_lookup` 定义在 `src/vm/vm.c:1486`，表是 `builtins[512]` / `builtinCount`，`src/vm/vm.h:157`），然后：

```c
if (bi >= 0) {
    …安全模式检查… …能力检查…
    vm->cur_argc = ins.r3;
    vm->builtins[bi].func(vm);
}
/* 没有 else —— 查不到名字时控制流落到 case 末尾的收尾代码 */
if (t->sp >= 0) { value_move(&R[ins.r1], &t->stack[t->sp]); t->sp--; }
```

（收尾代码在 `src/vm/vm.c:3564-3567`。）它本来只服务于「有返回值的调用把结果移到 r1」，在查不到函数时就变成「把最后一个实参塞进 r1」。`str2int("42")` 因此把自己的实参当返回值交出来。（`src/vm/vm.c:4983` 的另一处 `OP_CALL_BUILTIN` 只是反汇编器。）

### 三、解释器又是唯一的异类

与「`+` 链整数溢出」（§1.9 之前）和「浮点第 17 位平局」（§1.8）同形：两个编译后端**早就在编译期拒绝同一个程序**。

| 后端 | 拒绝文本 |
| --- | --- |
| AOT | `aot_native: call to 'nosuchbuiltin' is not a user-defined function in this program` |
| wasm | `error: wasm MVP subset: function 'nosuchbuiltin' not found (builtins are not in the wasm MVP subset) (line 2)` |

所以这一改是让第三个后端**向另外两个靠拢**，而不是新立一条语义。（两个后端还会整体拒绝「用字符串」的版本 —— `aot_native: expression kind 2 is outside the numeric subset` / `error: wasm MVP subset: expression type 2 not supported by wasm MVP subset (strings/collections need the interpreter) (line 1)` —— 所以三后端可比对的只有数值形态。）

### 四、修法

在 `bi >= 0` 块末尾补上缺失的分支：

```c
} else {
    char eb[256];
    snprintf(eb, sizeof eb, "unknown builtin function '%s'", name);
    vm_throw_msg(vm, eb);
}
```

修后 `./build/inimerse --no-mods .verify/unreg.im` 的 stdout 为空，stderr 为 `[exception] uncaught: unknown builtin function 'str2int'` 加 `  at ip=4 frames=0`，**退出码 1**。它是普通可捕获的 throw：`try { x = str2int("42") } catch (e) { say "caught: " + str(e) }` 打印 `caught: unknown builtin function 'str2int'`，随后程序照常继续。

**影响面零**：修后全量 `ctest --test-dir build -j$(nproc)` 得 `100% tests passed, 0 tests failed out of 110`；没有任何测试依赖那条静默栈垃圾。

### 五、判据与双向验证

进树回归 `vtest/unknown_builtin_v05.im` ← CTest `unknown_builtin_runtime`（`CMakeLists.txt`，`LABELS "vm;language;regression"`，`PASS_REGULAR_EXPRESSION "unreg-ok state=1 control=true"` / `FAIL_REGULAR_EXPRESSION "unreg-ok state=0|unreg-ok state=2|control=false"`）。测试用 `definitely_not_a_builtin_xyz` 这个名字 —— 任何构建都不存在，故不依赖平台挂了哪些 mod。

**反向验证**：`cp src/vm/vm.c .verify/vm.c.unknown_builtin` → `git checkout HEAD -- src/vm/vm.c` → 重编 ⇒ 该 CTest **红**（`Error regular expression found in output. Regex=[unreg-ok state=0|unreg-ok state=2|control=false]`），且 `./build/inimerse --no-mods .verify/q.im` 仍打印 `returned 1` / `alive`；拷回 + `cmp` 证逐字节一致 + 重编 ⇒ **绿**。

### 六、门禁实测（本轮）

`grep -c 'add_test(' CMakeLists.txt` **110 → 111**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **111**，`ctest -N` 的 `Total Tests: 111`（`Test #111: unknown_builtin_runtime`）。全量 **`100% tests passed, 0 tests failed out of 111`**；完整九阶段 `tools/gate.sh` **全 PASS**、`gate: OK — every stage passed.`、`GATE_RC=0`。

### 七、诚实边界与一条坑

- **写这条回归踩到的坑（已写进 `docs/AUDIT.md` §1.11）**：CTest 同时抓 stdout **和 stderr**，而引擎在 stderr 上还会打一份**字符串常量池转储**（形如 `[0]="returned" [1]="definitely_not_a_builtin_xyz" …`）。第一版测试的 FAIL 正则里写了 `wrong-msg`，正好命中池里的 `[6]="wrong-msg:"` ⇒ **实现已经正确、测试却报红**（一次假阴性）。改法：两个正则都改成**池里不会出现**的文本 —— 状态用整数 `state=0/1/2`，打印的键靠字符串拼接。`PASS_REGULAR_EXPRESSION` 同理。
- 这条修复**没有**让 `contract_test.im` 的 4 条失败变绿：它们的输出形状变了（现在是抛异常而不是给错值），但**仍然失败**，因为 POSIX 构建本来就不编 gui_mod / io_mod。0 条 → 0 条，性质从「值不对」变成「名字不存在」，可诊断性才是本轮收益。
- `contract_test.im` 当时**仍未进门禁**（手动/dormant 套件）—— **§10.47 已把它注册进 CTest**。
- `tools/im_diff_fuzz.py` 仍未接进门禁（阈值未裁定）。


## 10.47 手动套件进门禁：`contract_test.im` 成为 CTest（BOARD 行 141）

**症状（流程缺陷，不是运行错误）**：仓库根的 `contract_test.im` 是 API/SPI 契约套件，头注释自称「每次引擎 API 变更必须跑本套件（回归门槛）」—— 但它**当时根本不在 CTest 里**。`grep -in contract CMakeLists.txt` 在本轮之前只命中两处无关注释 —— **§10.47 的处置已把它注册进 CTest**。§1.8 的 `+` 链整数溢出与 §1.10 的 `list(<set>)` 句柄错位**都是它先抓到的**，只因要人手动跑，从没在门禁里红过。**一条不跑的套件等于没有套件。**

### 一、为什么它会红、而且比文档记的还多

POSIX 构建把 `io_mod`/`gui_mod` 桩成空实现（`src/platform/posix_stubs.c` 的三个 `STUB_REG`），`str2int`/`noise2d`/`gui_*` 在本平台不存在。§10.46 修掉「未注册内建静默返回栈顶」之后，这些调用**从「静默返回垃圾」变成「抛异常」**，于是三条原本**靠垃圾值侥幸通过**的断言立刻翻红：

| 断言 | 旧行为（静默垃圾） | 修 §10.46 之后 |
| --- | --- | --- |
| `contract_test.im:206` `gui_frame_interval(16)` 包在 try 里 | 返回垃圾但不抛 → `fok = true` → **通过** | 抛 → `fok = false` → **失败** |
| `contract_test.im:197` `check(gui_canvas(8, 8) >= 0, "canvas create")` | 返回垃圾 `8` → `8 >= 0` → **通过** | 抛 → **失败** |
| `contract_test.im:194` `check(noise2d(…) == noise2d(…), "noise deterministic")` | 两次都返回同一个垃圾 `7` → 相等 → **通过** | 抛 → **失败** |

**这是「可诊断性」的复利**：让错误可见，会把此前被静默掩盖的「通过」一起推翻。三条假通过里没有一条是断言写错了 —— 是它们测的东西当时根本不存在。

### 二、修法

套件开头加两个探针（`io_ok` 试 `str2int`；`gui_ok` 试无副作用的 `gui_vram_used()`），第 9/15/16 段按探针整段跳过。**跳过是可闻的**：末尾打印 `contract: skipped io=/gui=` 的实际取值。

更关键的是加了一道**下限断言**：

```
if pass < 60 {
    throw "CONTRACT FAIL: only " + str(pass) + " checks ran"
}
```

全套共 **70** 个 `check`，POSIX 上 io/gui 双缺、跳过 **7** 个，实测 **63**。下限 60 对两个平台都成立（Windows 上是 70），而「整段整段没跑」不可能静默变绿。

### 三、判据与双向验证

进树判据：CTest `contract_suite_runtime`，注册在 `CMakeLists.txt`（`COMMAND inimerse ${CMAKE_SOURCE_DIR}/contract_test.im`，`WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}`，`TIMEOUT 30`，`LABELS "vm;language;regression;contract"`，`PASS_REGULAR_EXPRESSION "contract: [0-9]+ passed"`）。

**故意不设 `FAIL_REGULAR_EXPRESSION`**：抛出的文本 `CONTRACT FAIL` 正是套件里的字符串字面量，而 CTest 会读引擎 stderr 上的**常量池转储**——照 §10.46 七记的那条坑，这个正则会在**绿跑**上命中池里的 `[N]="CONTRACT FAIL: "`，把正确实现判红。异常让进程 `exit 1`，靠退出码就够。

**双向验证**：注册后 `ctest -R contract_suite_runtime` **绿**（`1/1 Test #112 … Passed`）；`git checkout HEAD -- contract_test.im`（未加探针的旧版）+ 重建 ⇒ **红**（`***Failed  Required regular expression not found. Regex=[contract: [0-9]+ passed`，旧版在第一处 `str2int` 就抛、退出码 1）；`cp` 回 + `cmp` 证逐字节一致 ⇒ 复绿。所以这条注册**真的在跑引擎**，不是「注册了一个永远绿的壳」。

### 四、门禁实测（本轮）

`grep -c 'add_test(' CMakeLists.txt` **111 → 112**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步 **112**，`ctest -N` 的 `Total Tests: 112`（`Test #112: contract_suite_runtime`）。`docs/BOARD.md` §3 与本节 §2 的计数同步 **112 / 112**。

### 五、诚实边界

- POSIX 上 io/gui 双缺，所以套件跑的是 **63/70**：门禁**确实没有验证那 7 条 io/gui 契约**。这是平台能力边界（这两个模块不在 POSIX 构建里），不是套件偷懒；`src/platform/posix_stubs.c` 把 `io_mod_register`/`gui_mod_register`/`build_mod_register` 桩成空实现是现状，不是本轮引入的。
- 探针本身要跑一次真实调用：`str2int("42")` 与 `gui_vram_used()` 都无副作用，选后者正是因为它不建画布。
- 7 条被跳过的断言**不是**「已通过的等价物」；本节把它们逐条列出，是为了让跳过在文档里同样可闻。

## 10.48 差分模糊测试进门禁、枚举拒绝闸门钉进回归、两份备份删除（BOARD 行 143、144）

本轮是用户的三条直接指令：**「接进门禁；回归钉住；删除备份」**（2026-10-04）。三件事互不相关，但都属于同一类：**已经知道的东西没有被门禁或文档钉住**。

### 一、接进门禁：`tools/im_diff_fuzz.py` 成为门禁第 4 阶段

`tools/im_diff_fuzz.py` 是 `docs/AUDIT.md` §1.5 立的三后端差分方法本身，却只在有人想起来时手动跑。它进不了「零期望」的门，这是先测出来的：

| 配置 | agreed | DIVERGE | THREW | not translated |
| --- | --- | --- | --- | --- |
| `--count 20 --seed 1` | 20 | 0 | 0 | 0 |
| `--count 120 --seed 1`（工具默认） | 111 | **5** | **4** | 0 |
| 把常量池限制成 int32 | — | **3** | **4** | 0 |

限制常量域不解决问题：分歧来自**计算**（`x + 1 + 1`，`x = 2147483647`），与 §1.2 是同一件事 —— `Value` 是 32 位 `ival` + double，而 codegen 的 `NV.i` 是 64 位。

**修法：钉住（ratchet），双向都红。** `tools/gate.sh` 新增 `EXP_FUZZ_COUNT=120` / `EXP_FUZZ_SEED=1` / `EXP_FUZZ_DIVERGE=5` / `EXP_FUZZ_THREW=4` 与 `stage_fuzz()`，注册在 ctest 之后。种子与程序数固定 ⇒ 分歧集合确定；两份计数**精确断言** ⇒ 多一条是新的分歧，少一条是修好了一条（**同样红**，须把该例提升进 `tools/aot_native.test.py` 的 `EQUIVALENCE` 再抬 pin —— 与 `tools/aot_native.test.py:132` 的 `DIVERGENCE` 清单同一约定）。`not translated` 双向必须为 **0**（生成器造出后端翻不动的东西是生成器 bug，不是分歧；放宽生成器不能用来把红变绿）。**工具自身的退出码不参与判定** —— 它发现任何东西就退 1，若拿退出码当判据，默认调用永远红。

### 二、回归钉住：枚举器的两道拒绝闸门

`vm_set_to_array` 在两条边上主动 `return -1` 而不是硬走：跨度 `kh - kl >= 10000000`、端点越界（`|lo| > 9.0e18`，或整数格点落在 int32 之外，因为 `VAL_INT` 只有 32 位）。这是契约（截断成前 N 个又便宜又错），但此前只被间接碰过（`vtest/set_components_enumerable_v05.im` 的 `infinite=0,nil` 只钉住「真无限」那一种）。**关键：拒绝与空区间在 `len()` 下都是 0**，所以必须断言 `list()`。

新增 `vtest/set_enumeration_refusal_v05.im` ← CTest `set_enumeration_refusal_runtime`（**#113**），一行断言七个值：

`setrefuse-ok bigspan=nil edge=nil i32=nil neg32=nil empty=0 ok10=10 ok3=3`

前四个 `nil` 是四种拒绝（跨度 `Z[1~10000001]`、端点 `Z[1~10000000000000000000]`、int32 之外 `Z[1~3000000000]` 与 `Z[-3000000000~-1]`），`empty=0` 是空区间**不是**拒绝（`list(Z[5~4])` 是 `[]`），`ok10=10` / `ok3=3` 是**对照**：闸门不许扩大到误伤普通区间。`FAIL_REGULAR_EXPRESSION "ok10=0|ok3=0|empty=nil"` 专抓「拒绝过度」这一侧；三个 token 都带数字或词，池转储里凑不出（§10.46 七记的坑）。实测 **0.057 s**。

### 三、删除备份

- `src/mod/gui_mod.c.bak2_20260808_221050`（**入库**，107461 字节，1485 个 U+FFFD）→ `git rm`。
- `src/main.c.bak`（**未跟踪**，34646 字节）→ `rm`。
- `mods/debug/mod.st.bak`（**未跟踪且被 `.gitignore:50` 的 `*.bak` 忽略**，78 字节）→ `rm`。它是 `mod.st.dev` 的逐字重复（同样三行、同一个 `debug_mod.dll`），且没有任何代码引用它。**同目录的 `mod.st.orig` / `mod.st.dev` / `mod.st.dev28` / `mod.st.dev29` / `mod.st.hold5` 没有动** —— 它们虽然名字像备份，但都是**受版本控制的、故意的 mod 状态变体**（`git ls-files mods` 收录），是 mod 加载器的状态夹具，不是遗留物。区分标准是「受版本控制 + 被别处使用」而不是文件名后缀。

删前证明没有东西依赖它们：`tools/`、`CMakeLists.txt`、`*.sh`/`*.py`/`*.js`/`*.cmake` 对两个路径**零命中**，唯一引用它们的只有文档（`docs/SYNTAX.md`、`docs/STATUS.md`、`docs/BOARD.md`，本轮同步）。`realpath` 校验绝对路径之后才删。

### 四、顺带纠正：U+FFFD 的「14 个」是巧合而不是准数

`docs/SYNTAX.md` 的 H1 表把 `.bak2`（1485）列在第一行并计入 14，却**漏记了 `docs/AUDIT.md`（2）** —— `git show HEAD:docs/AUDIT.md` 实测就是 2，所以删除前的真实数字是 **15**，表里的 14 早已不准。删除后重新统计**仍是 14 个**（少一个、补一个），标题里的数字恰好没变。表已改：去掉 `.bak2` 行、补上 `docs/AUDIT.md | 2`，现在最坏的是 `mods/debug/debug_mod.c` 619。

### 五、判据与双向验证

**fuzz**：默认 pin ⇒ `gate: fuzz findings match the pin (5 DIVERGE, 4 THREW, 0 untranslated).`、阶段 PASS；`EXP_FUZZ_DIVERGE=0 bash tools/gate.sh --fast --only fuzz` ⇒ `gate: differential fuzz found 5 DIVERGE / 4 THREW, pinned 0 / 4` + 两条处置说明 + `✘ differential fuzz (interp vs AOT, pinned 0+4) (exit 1)` + `gate: FAILED — do not merge.` **v3.1 起这条 pin 已降到 0 / 0**，阶段名改为「expect 0 findings」，见 §10.49。

**拒绝闸**：把跨度闸 `if (kh - kl >= 10000000) return -1;` 临时改成 `>= 3` ⇒ `1/1 Test #113 …***Failed  Error regular expression found in output. Regex=[ok10=0|ok3=0|empty=nil]`、`0% tests passed, 1 tests failed out of 1`（`Z[1~10]` 也一起被拒了）；`cp` 还原 + `cmp` 逐字节一致 + 重编 ⇒ `Passed`、`100% tests passed`。

### 六、门禁实测

`grep -c 'add_test(' CMakeLists.txt` **112 → 113**，`tools/gate.sh` 的 `EXP_CTEST` 同步 **113**，`ctest -N` 的 `Total Tests: 113`（`Test #113: set_enumeration_refusal_runtime`）。`tools/gate.sh` 的阶段数**九 → 十**，`--only` 的键列表新增 `fuzz`；`docs/BOARD.md` §3 与本节 §2 同步 **113 / 113**。

**全量门禁实测（2026-10-04）**：十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。其中 ctest `100% tests passed, 0 tests failed out of 113` 且 **0 skipped**；fuzz `gate: fuzz findings match the pin (5 DIVERGE, 4 THREW, 0 untranslated).`；`check_links` 93 个 markdown、388 条链接、**0 broken**；`check_doc_paths` 16 个 markdown、372 条 backtick 引用、**0 broken**。

### 七、诚实边界

- fuzz 门禁**不证明解释器与 AOT 一致**，只证明「在这 120 个程序上分歧的数量没有变」。它是一把尺子，不是一张合格证；真正的收敛要靠把 5+4 条分歧逐条修掉，每修一条抬一次 pin。`not translated = 0` 也只覆盖这批程序能被 AOT 翻译，不代表生成器覆盖了全部语法。**（这 5+4 条已在 v3.1 里逐条修掉、pin 归零 —— 见 §10.49。）**
- fuzz 阶段约 **45–50 s**，是门禁里最慢的一段。
- 备份文件**删除不等于内容消失**：`gui_mod.c.bak2` 的内容仍在 git 历史里（`git show <旧提交>:src/mod/gui_mod.c.bak2_20260808_221050`）；而 `src/main.c.bak` **从未入库**，它现在只存在于别处的工作副本里。
- 拒绝闸门的断言只覆盖**解释器**：`list`/`len`/`size` 只有解释器实现（`grep '"list"' src/compilation/*.c` 为空），没有三后端比对可言。

## 10.49 v3.1 整数位宽：Value 的整数槽改 int64，模糊测试的分歧归零（BOARD 行 145）

### 一、症状

§10.48 把差分模糊测试接进门禁时，默认配置 `--count 120 --seed 1` 实测 `agreed 111 / DIVERGE 5 / THREW 4`，两份计数被**钉**在 `5` / `4`（[AUDIT.md](AUDIT.md) §1.13）。那 5 条 DIVERGE 里解释器把 `9007199254740993` 算成 `9007199254740992`、把整数除零算成 `inf`；4 条 THREW 是 AOT 抛 `division_by_zero` 而解释器根本不认为那是错。

### 二、根因是一个，不是九个

`src/vm/vm.h:24` 的 `Value` 是 `{int type; int ival; double fval; char *sval; void *ptr;}`，**32 字节、整数槽 32 位**。于是 `src/compiler/compiler.c` 在字面量超过 int32 时把它降级成浮点，整条表达式从此走 double：2^53 以上的低位没了，而且**所有整数守卫都不再被命中** —— `2147483647 / 0` 抛 `division_by_zero`，`2147483648 / 0` 却算出 `inf`，因为操作数已经不是整数了。守卫本身是对的，只是永远够不着。

### 三、为什么是匿名 union 而不是加宽字段

`docs/DECFY_DESIGN.md` §2(a) 层次划分表「值表示 + 集合运行时」行 把 `Value` 的宽度**冻结**在 32 字节：它同时是 VM 寄存器、AOT 生成 C 的 `NV`、wasm 线性内存槽位的共同形状。`int ival` 直接改 `long long` 会让 `sizeof` 从 32 变成 40，是真正的 ABI 破坏。改用匿名 union：

```c
int type;
union { long long ival; double fval; };
char *sval;
void *ptr;
```

实测 `sizeof` 前后都是 32，`type`/`sval`/`ptr` 的偏移不变（`ival` 与 `fval` 共用偏移 8）。`CMakeLists.txt:19-20` 是 C11，匿名 union 合法。wasm 后端**本来就是这样**的（`src/compilation/wasm_backend.c:67` 的 `SLOT_BYTES 16` 注释即 `[tag i32 @+0][pad][i64 payload @+8]`），所以 AOT 的 `NV` 完全没动，只有 VM 和 wasm 要改。顺带一提，改完之后 `grep -n 'Value v; .*v\.fval = 0' src/` 这类「同时给 `ival` 和 `fval` 赋值」的位置全部失效（两者现在是同一块存储），全仓 28 个文件里那些 5 字段的位置初始化 `{VAL_NIL, 0, 0, NULL, NULL}` 都要去掉中间那个 `0` —— 这是本次 diff 里最大的一片机械改动。

### 四、逐条：加宽之后才暴露的六处

1. **字节码没有 int64 字面量的载体。** `RegInstruction` 只有 `r1/r2/r3`，`OP_LOADK_INT` 的 `r2` 是**符号扩展的 int32**，无法承载无符号低半。先试过把高低 32 位相加（`r2 + (r3<<32)`），**这是错的**：低半符号位为 1 时 `(int)` 会把它符号扩展，结果少了 2³²（实测 `9223372036854775807` 被读成 `9223372032559808511`）。最终**在 `OpCode` 枚举末尾追加 `OP_LOADK_I64`**（`src/compiler/bytecode.h` 自己的约定就是「追加在末尾以保持旧 opcode 编号、DLL ABI 兼容」），两半都按无符号拼装；旧字节码的 `OP_LOADK_INT` 语义不变，`INIM_BYTECODE_VERSION` 仍是 **3** —— 它在 `src/compiler/bytecode.c:351`、`:731` 是**严格相等**比较的，一升版所有既有 `.inim` 都读不了。
2. **`push_int` 的参数是 `int`。** `src/vm/vm.c:325` / `src/vm/vm.h:379` 原文 `void push_int(VM *vm, int v)`，而 `L_PUSH_REG` 对 `VAL_INT` 直接调它 —— **函数传参路径上每一个 64 位整数都被截成低 32 位**。实测 `func f(a) { return a } g = f(9007199254740993) say g` 解释器打印 `1`，AOT 和 wasm 都是 `9007199254740993`。这个缺陷一直潜伏：字面量以前都被降级成 double 走 `push_float`，`push_int` 根本见不到超过 int32 的值。★ **这一行上的两个号不同档**（写下时都按当时那棵树取）：`src/vm/vm.c` 那个号从来没指到过 `push_int` —— 签名取 `int` 的那一段里，它在该文件被触及的 23 棵树上从 123 走到 315，从未到过 325（逐棵取 `git rev-list 8248e08..33b6dd2^ -- src/vm/vm.c`，再对每棵取 `git show <c>:src/vm/vm.c | grep -n 'void push_int(VM *vm, int v)'`），今天它在 478 行；`src/vm/vm.h` 那个号只在【改完之后】的那棵树上成立 —— 句子引的是改之前的签名 `int`，而 `git show 8248e08:src/vm/vm.h | grep -n 'void push_int'` 印出 330、`git show 33b6dd2^:src/vm/vm.h | grep -n 'void push_int'` 印出 357，只有 `git show 33b6dd2:src/vm/vm.h | grep -n 'void push_int'` 印出 379（那一行已是 `long long`）；今天它在 395 行。
3. **布尔操作数把表达式拖回 double。** 守卫写的是 `a->type == VAL_INT && b->type == VAL_INT`，`VAL_BOOL` 不满足，于是 `true * 9007199254740993` 解释器和 wasm 都是 `...992`、AOT 是 `...993`。判据定为「布尔是**整数值**操作数」：新增 `val_is_intlike()`（INT 或 BOOL）与 `val_i64()`（BOOL 取 0/1），用于 `L_ADD`/`L_SUB`/`L_MUL`/`L_DIV`/`L_NEG`/`L_MOD`、`val_cmp` 以及 `val_eq` 的跨类型数值分支；wasm 侧对应新增 `e_push_is_intlike()` / `e_push_is_numeric()`。
4. **`L_DIV` 根本没有整数路径。** 它一律 `val_as_double(a) / val_as_double(b)` 并返回 `VAL_FLOAT`，所以 `6/3` 是 `2.0`。而 AOT 的 `nv_div` 一直按**文档写明**的规则做（其 preamble 原文：*The interpreter yields an int when the division is exact and a float otherwise (4/2 prints 2, 7/2 prints 3.5, 6/4 prints 1.5)*）—— 即「精确整除留整数，有余数才转浮点」。是解释器从没实现过自己这条规则。现在 `L_DIV` 逐字镜像 `nv_div`（`ib == 0` 抛 `division_by_zero`；`ib != -1 && ia % ib == 0` 留整数；否则浮点；`ib == -1` 故意落浮点，因为 `INT64_MIN / -1` 会溢出），wasm 的 `TOK_SLASH` 改成同形状三分支。**`2.0 / 0` 和 `2 / 0.0` 三端仍都是 `inf`** —— 只有 intlike×intlike 的零才抛。
5. **wasm 的关系运算无条件走 f64**（`src/compilation/wasm_backend.c` 约 `:1061-1067`），所以 `9007199254740993 > 9007199254740992` 只有 wasm 是错的。补了缺失的 `W_I64_LE_S 0x57` / `W_I64_GE_S 0x59`，加了 i64 精确快路径。
6. **AOT 的 `nv_eq`/`nv_ne` 判据错了两处。** 旧规则是「任一边是 bool 就按整数比，否则按 double 比」：于是 `true == 1.5` 把 1.5 经 `nv_asi` 截成 1 而答 `true`（另两端 `false`），而两个超过 2^53 的整数舍入到同一个 double，`9007199254740993 == 9007199254740992` 答 `true`（另两端 `false`）。改为镜像 `val_eq`（`src/vm/vm.c:293-309`）：**同 tag 按该 tag 比，只有混合数值对才提升到 double** —— 后者正是 D2「`==` 跨类型数值等价」的要求，`1 == 1.0` 与 `true == 1` 仍是 `true`。

### 五、第 7 处不是算术缺陷，是编译器寄存器水位

修完上面 6 处后 seed 1 已经 `120/0/0`，但 seed 2 还剩 1 条：

```
func f(a) { return ((a or (2147483646 < a)) % 31) }
g = f(4294967296)
say g
```

解释器 `0`，AOT 和 wasm 都是 `1`。这条**不能**套用「解释器是离群者」的老结论 —— 它是解释器**忠实地执行了错误的字节码**。函数体 dump：

```
0,2,1,0          MOV r2, r1            ; result r2 = a
26,2,7,0         JUMP_IF_TRUE r2 -> 7
1,3,2147483646,0 LOADK_INT r3, 2147483646
12,5,3,1         LT r5, r3, r1
0,4,5,0          MOV r4, r5
17,2,2,4         OR r2, r2, r4
24,0,8,0         JUMP -> 8
4,2,1,0          LOADK_BOOL r2, true   ; 短路路径写 result
1,2,31,0         LOADK_INT r2, 31      ; ← `%` 的右操作数复用了 r2
50,2,2,2         MOD r2, r2, r2        ; 31 % 31 = 0
```

`src/compiler/compiler.c` 的逻辑表达式发射器（`TOK_AND`/`TOK_OR`）把释放水位 `int r_wm = next_register;` 放在 `result = alloc_reg()` **之前**；而 `alloc_reg()` 就是 `return next_register++;`，所以 `result == r_wm`，末尾的 `release_to(comp, r_wm)` **把它自己刚分配的结果寄存器释放了**，外层二元表达式随即把同一个寄存器分给右操作数并覆盖真值。只在函数里出现是因为：左操作数是变量时 `last_temp = 0` 才走 `alloc_reg()` 分支；字面量或全局读取返回 `last_temp = 1`，直接复用左寄存器。修法是把水位采集移到结果分配之后。另两处 `r_wm`（`TOK_IN` 与通用二元发射器）审过是安全的：它们的释放只发生在 `result = left < r_wm` 的分支里。

### 六、顺带修掉四处「加宽之后才成立的截断」

`Value` 的整数槽变宽之后，一批**以前无损、现在会丢**的 `int` 假设必须跟着改（做一次干净重建的 warning 扫描把它们照出来了）：

- **`%d` 配 `long long`** 是 UB，三处：`src/runtime/runtime_posix.c:58`（`str()` 的整数分支）、`src/runtime/runtime.c:67`（同一个 `builtin_str` 的 Windows 版）、`src/mod/replay_mod.c:87`（`rp_hash_value` 把整数**十进制文本**喂进 SHA-256 —— 这一处若截断，重放哈希会对不同的值算出同一个哈希）。全部改成 `"%lld", (long long)v->ival`。
- **`src/mod/replay_mod.c:47`**：`rp_push_int(VM *vm, long long n)` 的**参数本来就是 `long long`**，函数体却写 `v.ival = (int)n;`。改成 `v.ival = n;`。
- **`len()` / `size()` 把整数当长度时截断**：`src/runtime/runtime_posix.c` 里两个函数各有一行 `n = (int)v->ival;`，而 `n` 是 `int`。**双向验证**（探针 `.verify/v31/trunc.im`，四行 `say`）：把 `posix_core_size` 那一行**临时改回 `(int)`** 重建 ⇒ `str(9007199254740993) len(9007199254740993) size(9007199254740993) str(2147483648)` 打出 `9007199254740993 9007199254740993 **1** 2147483648`（`1` 正是 2⁵³+1 的低 32 位）；改回 `n = v->ival;`（`n` 改成 `long long`）重建 ⇒ 第三格变成 `9007199254740993`。`cmp` 证逐字节还原。
- 干净重建的 warning 计数：改之前 **37**，改完 **35**，`error: 0` —— 与 §2 基线记的 **35 warnings / 0 error** 一致，所以 §2 不需要改。

### 七、错误名用已注册的 `numeric_overflow`

整数离开 int64 时抛的错**没有**用草稿里自造的 `integer_overflow`：`src/types/error_types.c:6-31` 早已注册 `{"numeric_overflow", IM_ERROR_DOMAIN_ARITHMETIC_VM, 2002}`，而 `docs/archive/ROADMAP_CASE_TYPES_V04.md:44` 把 `ArithmeticVMError` 定义为 `{division_by_zero, numeric_overflow, invalid_numeric_operation}` —— 第四个同义词会是未注册、也未文档化的东西。三端现在的行为（实测，`rc=1`，stderr 逐字）：`INT64_MAX + 1`、`0 - x - 2`、`INT64_MAX * 2`、`0 - INT64_MIN` 都是 `[exception] uncaught: numeric_overflow`；`2147483648 / 0`、`2147483648 % 0` 都是 `[exception] uncaught: division_by_zero`。AOT 侧用 `__builtin_{add,sub,mul}_overflow` 判溢出后调新增的 `nv_die_numeric_overflow()`；wasm 侧用位运算判据（加 `((a^r)&(b^r))<0`、减 `((a^b)&(a^r))<0`、乘 `r/b != a`，并把 `b == 0`（不会溢出）与 `b == -1`（唯一会让 `i64.div_s` 陷阱的除数）特判掉）后调 `IMP_ERROR`，错误码新增 **7**（`tools/wasm_run.js` 的 `im_error` 映射表里 `2` 已经是 `call_frame_overflow`）。

### 八、判据与双向验证

- **`tools/gate.sh`**：`EXP_FUZZ_DIVERGE` / `EXP_FUZZ_THREW` **5 / 4 → 0 / 0**，阶段标签从 `pinned ${EXP_FUZZ_DIVERGE}+${EXP_FUZZ_THREW}` 改成 `differential fuzz (interp vs AOT, expect 0 findings)`。实测 seeds 1–6 各 120 个程序、seed 1 400 个程序，**全部 `0 DIVERGE / 0 THREW / 0 not translated`**。
- **`tools/aot_native.test.py`**：73 例 → **104 例**（86 equivalence / 2 pinned divergences / 6 runtime errors / 10 refusal）。新增 `RUNTIME_ERROR` 类别断言「两端都 rc≠0 **且错误种类逐字相同**」；`EQUIVALENCE` 增补 20 行；原先钉在 `DIVERGENCE` 里的 `lcg_float_promotion` 被**提升**为 `int_lcg_second_step` —— 这正是 §1.13 那条「修好一条就把它提升、再抬 pin」的约定第一次被真正执行。
- **反向验证**：上一节那条 `size()` 探针（临时退回 `(int)` ⇒ 红，还原 ⇒ 绿，`cmp` 证逐字节）；第 5 节的 `or` 覆盖缺陷另有 7 个分解探针（`.verify/v31/fz/s2b.im` … `s2i.im`），修前 5 个错、修后全对。

### 九、门禁实测

十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。其中 ctest `100% tests passed, 0 tests failed out of 113` 且 **0 skipped**（`EXP_CTEST` 仍是 **113** —— 本轮没有增删 CTest，改的是既有 `aot_native_regression` 的用例数）；fuzz `gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`；`check_links` 与 `check_doc_paths` 均 **0 broken**。干净重建（独立目录）**35 warnings / 0 error**，与 §2 基线一致。

### 十、诚实边界

- 归零的是**这一批程序**上的分歧，不是「三后端处处一致」。模糊测试的生成器只覆盖数值子集，字符串、集合、闭包、模块边界都不在其中。
- `Value` 的 32 字节契约仍在，**超过 int64 的整数还没有表示**：`INT64_MAX + 1` 是抛错而不是变成 BigInt。v3.1 phase 2 的 `VAL_BIG`（盒装 BigInt 走 `Value.ptr`，与 `VAL_STRING`/`VAL_ARRAY` 同构）尚未实现，`docs/archive/ROADMAP_3.1.md:23` 说的 `Z` = 无限整数集 + BigInt 也仍是路线图而非现状。
- **`sum()` 仍在 double 里累加**：`src/runtime/runtime.c:138` 的 `builtin_sum` 用 `double sum` 求和、`allInt` 时再 `push_int(vm, (int)sum)`。这条**不是本轮引入的**（许多 int32 相加本来就会溢出 int32），但加宽之后它成了唯一还会静默丢精度的整数路径，已在此记录、未修。（**已修，见 §10.50** —— 而且不止「double 累加」这一条：集合的区间分量还被整个跳过，`sum(1, 2, Z[7~9])` 是 0。）
- Windows 专属分支（`src/mod/io_mod.c` 等）上的 `push_int` 调用点没有实测，只有 POSIX 侧跑过门禁。

## 10.50 `sum()` 的两个静默错误答案：集合分量被跳过、整数在 double 里累加（BOARD 行 146）

### 一、症状与发现路径

§10.49 的诚实边界把 `sum()` 记成「唯一还会静默丢精度的整数路径」，当时未修。回查它时发现**两个**独立的错，都在同一个函数里，而且**都不改退出码**：

1. **集合的区间分量被整个跳过，却报成功。** `b = 1, 2, Z[7~9]` 的 `len(b)` 是 5、`list(b)` 是 `[1, 2, 7, 8, 9]`，而 **`sum(b)` 是 0**。`t = Z[1~2], Z[5~6]` 同理（`len` 4、`list` `[1, 2, 5, 6]`、`sum` 0）。`sum(Z[1~4])` 是 0 而 `sum(list(Z[1~4]))` 是 10。
2. **整数在 `double` 里累加。** `sum([9007199254740993])` 答 **9007199254740992**，`sum([9007199254740993, 1])` 同样是 `...992`。

两者的共同点是**没有任何异常**、退出码 0 —— 只有把值打出来才看得见。

### 二、根因

**第一个错的根因**是读法：`sum` 只从 `s->i64`（整数字面量）与 `s->items`（非整数字面量）里读，而且整段包在 `if (s->kind == 0 && s->compCount == 0)` 里。集合字面量是三部分的并（`i64` / `items` / 区间 `comps`），带分量的集合不满足这个前提，于是**两个循环都没进**；`ok` 停在初值 **1**（不是 0），所以既不报错也不返回 nil，总额就停在初值 0。

**第二个错的根因**是累加器类型：`double sum`，`allInt` 时再 `push_int(vm, (int)sum)` —— 2⁵³ 以上先被舍入，Windows 上再被截成 32 位。

**同一个函数有两份实现，必须同步。** `CMakeLists.txt` 二选一编译：非 Windows 编 `src/runtime/runtime_posix.c`（`posix_core_sum`，`:241` 定义、`:1042` 注册），WIN32 编 `src/runtime/runtime.c`（`builtin_sum`，`:138` 定义、`:1693` 注册）。**POSIX 上生效的是前者** —— 只读后者会预测错行为，这是本次排查真实踩过的坑（先按 `src/runtime/runtime.c` 的代码推理，结论与实测不符，才发现生效的是另一份）。

### 三、修法

- 集合改走 **`vm_set_to_array`** 枚举 —— 就是 `len()`/`size()`/`list()` 已经走的那条路（§1.10 修完枚举器之后它才看得见分量），拿到的暂存数组由 GC 管，三个既有调用方都不 free 它。无界集合（`Z`、开口朝无穷的区间）返回 -1，`sum` 因此答 **nil**。**这是一处行为改变，而且是一处纠正**：`sum(Z)` 原来答 **0**（旧闸门不成立时 `ok` 停在初值 1、总额停在初值 0），`sum(Z[1~])` 同理；实测 `sum(R)` 前后都是 nil。`list(Z)` 是 nil，nil 才是诚实的答案。
- 整数在 **`long long isum`** 里累加，同时并行维护 `double fsum`；序列里一出现浮点就改用后者。越界用 `__builtin_add_overflow` 判定并抛 **`numeric_overflow`**（§10.49 第七节定的运算符规则）。
- Windows 那份去掉 `(int)` 截断，并保留它自己的错误风格（非数字元素 `vm_throw_msg(vm, "sum: non-numeric element")`，而不是 POSIX 的 nil）。

### 四、判据

新增 `vtest/sum_components_int64_v06.im` ← CTest **`sum_components_int64_runtime`（#114）**，一行断言：

```
sum-ok comp=27 two=14 single=10 big=9007199254740993 big2=9007199254740994 plain=6 float=3.5 empty=0 bool=nil inf=nil
```

配 `FAIL_REGULAR_EXPRESSION "comp=0|two=0|single=0|big=9007199254740992|bool=0"` —— 前三个正对「分量被跳过」，第四个正对「double 累加」。缺陷只改值不改退出码，所以断言的是数字而不是退出码。溢出分支会 `exit 1`，放不进同一个文件，改由 `contract_test.im` 的 `try`/`catch` 转成一条 `check`。`EXP_CTEST` **113 → 114**，`docs/BOARD.md` §3 与本节 §2 同步 **114 / 114**。

### 五、顺带纠正契约套件里的旧教条

`contract_test.im` §1 有 5 行写的是 `check(x + 1 == 2147483648.0, "int overflow -> float promote")` 之类。它们在 v3.1 之后**数值上仍然成立**（`==` 跨类型数值等价），所以一直是绿的 —— 但**描述已经是旧教条**。按 §10.49 的规则改成：越过 int32 仍是整数（`x + 1 == 2147483648`）、只有除不尽才出浮点（新增 `4 / 2 == 2`）、同类型比较精确（新增 `9007199254740993 != 9007199254740992`），并新增一条 `sum` 溢出检查。套件从 **70 条 `check` / 实测 63** 变成 **73 / 66**，`docs/AUDIT.md` §1.12 的计数与 `CMakeLists.txt` 的注册注释同步。

### 六、门禁实测

十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`（`warnings: 0` —— **该计数当时是假的**，见 §10.54）、`✔ ctest (expect 114/114, 0 skipped)`（`100% tests passed, 0 tests failed out of 114`）、`✔ differential fuzz (interp vs AOT, expect 0 findings)`（`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`）、`✔ economy migration (39/39)`、`✔ node protocol suites (12 registered)`、`✔ dsh-inimerse plugin (offline + live)`、`✔ oauth_loop crate (75/75)`、`✔ userdata ignore rules`、`✔ docs relative links`（`check_links: 93 markdown files, 394 links (17 external, 0 anchors, 377 local), 0 broken`）、`✔ docs backtick paths`（`check_doc_paths: 16 markdown files, 391 backtick references, 0 broken`）。

### 七、诚实边界

- `sum` 现在只是**与运算符同规则**，不是任意精度：`sum([INT64_MAX, 1])` 抛 `numeric_overflow` 而不是给出 BigInt（v3.1 phase 2 的 `VAL_BIG` 未实现）。
- **Windows 那份没有实测**：本机是 POSIX 构建，`src/runtime/runtime.c` 在 Linux 上连 WinHTTP 段都编不过（`HINTERNET`/`WinHttpOpen`/`URL_COMPONENTS`/`WCHAR` 未声明），只能做语法检查 —— `cc -std=gnu11 -fsyntax-only -Isrc -Isrc/common -Isrc/runtime -Isrc/compiler -Isrc/vm -Isrc/platform -Isrc/types -Isrc/mod src/runtime/runtime.c` 的报错行**全部落在 593–1643 的 WinHTTP 段**，`builtin_sum` 所在的 **138–195 区间内 0 个错误**。这不是「测过了」，是「编得过」。
- `src/mod/io_mod.c:51` 的 `io_push_int(VM*, int)` 仍是 `int` 参数（调用点传的是句柄与字节数，不是本节的累加器），本次未改；`src/mod/replay_mod.c:47` 的 `rp_push_int` 已随 §10.49 改成 `long long`。

## 10.51 WIN32 副本的 `len()`/`size()` 也没有分量回退：同一个门、同一个类（BOARD 行 147）

**症状与发现路径。** §10.50 修完 `sum()` 之后，用 `grep -rn compCount src/` 把每个调用点过了一遍，找同一类写法。`src/runtime/runtime.c`（**WIN32 那份**）里 `builtin_len` 与 `builtin_size` 都写着 `if (s->kind == 0 && s->compCount == 0)` 这个门，**但都没有 `else` 回退**：`builtin_len` 的 `n` 停在初值 `0` ⇒ `len(1, 2, Z[7~9])` 答 **0**（POSIX 答 5）、`len(Z[1~4])` 答 **0**（POSIX 答 4）；`builtin_size` 的 `n` 停在初值 `-1` ⇒ 答 **nil**（POSIX 答 5），因为它后面接的 `else if (s->kind == 2 && …)` 只认闭区间，而带分量的集合 `kind` 是 0。两者都只改值、不改退出码。

**根因。** 集合字面量是 i64 / items / 区间 comps 三部分的并（§10.10），`kind == 0 && compCount == 0` 只说明**能直接数**，不是元素个数的定义。§10.10 修好枚举器之后正确写法只有一种：能直接数就直接数，否则交给 `vm_set_to_array`。POSIX 两份都这么写了，WIN32 两份都没写 —— 这与 §10.50 的 `sum()` 是同一个门、同一个类。

**修法。** WIN32 的集合分支对齐 POSIX（`else { int a = vm_set_to_array(vm, v->ival); if (a >= 0) n = vm_array_len(vm, a); }`，现 `src/runtime/runtime.c:88`、`:116`），`int n` 换成 `long long n`（`push_int` 收 `long long`，`len(9007199254740993)` 在 `int n` 下会截断 —— §10.49 那一类截断的又一处），并删掉 `builtin_size` 里用 `pow()` 的闭区间公式（它对闭区间与枚举器同答案、对分量集合给 nil）。`CMakeLists.txt:383` 的 `if(WIN32)` 决定只编一份：WIN32 编 `src/runtime/runtime.c`（`:386`）、其它平台编 `src/runtime/runtime_posix.c`（`:395`）。

**判据与诚实边界。** **本机不可执行**：Linux 上 `src/runtime/runtime.c` 连 WinHTTP 段都编不过（`cc -std=gnu11 -fsyntax-only` 报错全在 601–1651 行），只能证明改动区间 0 错误。可证明的是**构造上的一致**：改动后两份的集合分支逐字相同。而 POSIX 那一份是实测的 —— `vtest/set_components_enumerable_v05.im:32` 早已钉着分量集合的 `len`/`size`，本机复测 `len(1, 2, Z[7~9]) = 5`、`size(1, 2, Z[7~9]) = 5`、`len(Z[1~4]) = 4`、`size(Z[1~4]) = 4`。**「两份写法一致」能证明，「Windows 上真的跑对了」不能。** 详见 [AUDIT.md](AUDIT.md) §1.16。

## 10.52 `str()` 把集合总结成 `set(iCount + count)`：分量集合被读成空集（BOARD 行 148）

**症状与发现路径。** §10.50 与 §10.51 修的是 `sum()` 和 WIN32 的 `len()`/`size()`，都出自「拿快速路径的门当答案」这一类。把 `SetObj` 的消费者全部列出来之后（`grep -rn "SetObj" src/`），只剩 `src/vm/vm.c:1104` 一处同形写法 —— 而这一处**在本机就能看见**，因为它在 `value_to_string` 里：

```c
else if (s->kind == 0) snprintf(buf, bufsz, "set(%d)", s->iCount + s->count);
```

`iCount + count` 只是**字面量那一部分**。实测（修复前，右边是当时就已经正确的 `len()`）：

| 程序 | `str()` 修复前 | `len()` |
| --- | --- | --- |
| `1, 2, Z[7~9]` | `set(2)` | 5 |
| `Z[1~4]` | `set(0)` | 4 |
| `Z[1~3], Z[5~7]` | `set(0)` | 6 |
| `1, 2, 3` | `set(3)` | 3 |

`set(0)` 对一个四元素集合不是概括而是**错答案**，它读起来就是「空集」。`z = Z[1~4]` 这种只写一个区间的写法编译器仍然包成 `NEW_SET`，所以变量也中招。缺陷只改值、不改退出码。

**根因。** 与 §10.50/§10.51 同源：集合字面量是 i64 / items / 区间 comps 三部分的并（§10.10），`iCount + count` 对任何带分量的集合都不是元素个数。§10.10 修好枚举器之后正确写法只有一种：能直接数就直接数，否则交给 `vm_set_to_array`。

**修法。** 带分量时改走 `vm_set_to_array` 枚举（`src/vm/vm.c:1113-1116`），与 `len()`/`size()`/`list()`/`sum()` 同一条路，于是两者**构造上一致**而不是各自算一遍。枚举器会去重，这一步必需：字面量可以落在分量里（`1, Z[1~3]` 是 {1,2,3} 而不是四个），两个分量也可以重叠。修复后实测 5 / 4 / 3 / 6，重叠的两种是 3 和 5。

**为什么在异常路径上分配是安全的。** `value_to_string`（`src/vm/vm.c:1093`）不只在 `say`/`str()` 里被调用，还在 `vm_throw`（`src/vm/vm.c:2312`）里格式化异常值（`:2343` 的 `try` 无 `catch` 分支、`:2358` 的 JSON 错误输出），在那里分配 VM 池对象看起来危险。实际安全：`vm_array_new`（`src/vm/vm.c:656`）只会**置 `gc_pending`**（`src/vm/vm.c:685`），真正的 `gc_collect` 发生在解释器主循环（`src/vm/vm.c:2814`），**不在这次调用里**；`vm_array_push`（`src/vm/vm.c:718`）同样不收集。这一点是写代码前先读出来确认的，不是假设。

**判据。** 新增 `vtest/set_str_component_count_v06.im` ← CTest **`set_str_component_count_runtime`（#115）**，一行断言：

```
setstr-ok union=set(5)/5 single=set(4)/4 overlap=set(3)/3 two=set(6)/6 plain=set(3)/3 refused=set(0)/0 named=set(Z) interval=set(float1 interval)
```

配 `FAIL_REGULAR_EXPRESSION "union=set\(2\)|single=set\(0\)|overlap=set\(1\)|two=set\(0\)|plain=set\(0\)"` —— 正对缺陷值。这条 FAIL 正则**已用 `grep -E` 双向验证过**：匹配修复前的那一行、不匹配修复后的那一行；一条永远匹配不上的 FAIL 正则等于没有断言。`EXP_CTEST` **114 → 115**，`docs/BOARD.md` §3 与本节 §2 同步 **115 / 115**。

**门禁实测。** 十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`（`warnings: 0` —— **该计数当时是假的**，见 §10.54）、`✔ ctest (expect 115/115, 0 skipped)`（`100% tests passed, 0 tests failed out of 115`）、`✔ differential fuzz (interp vs AOT, expect 0 findings)`（`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`）、`✔ economy migration (39/39)`、`✔ node protocol suites (12 registered)`、`✔ dsh-inimerse plugin (offline + live)`、`✔ oauth_loop crate (75/75)`、`✔ userdata ignore rules`、`✔ docs relative links`、`✔ docs backtick paths`。

**诚实边界。** 枚举器**拒绝**走不通的分量（端点朝无穷、整个具名集合、成员超过一千万），这时 `str()` 与 `len()` 都退回字面量部分 —— 两者仍然一致，`refused=set(0)/0` 就是这一格：`Z[1~10000001]` 的 `str` 是 `set(0)`、`len` 是 0，**两边都拒绝**，不是一边答空集。`kind == 1`（`Z`）与 `kind == 2`（`float1[0~0.3]`）从不声称计数，仍印 `set(Z)` 与 `set(float1 interval)`。另外 `str()` 印的始终是**概括**而不是元素表 —— 元素表是 `list()` 的事，`str(1, 2, 3)` 过去和现在都是 `set(3)`。详见 [AUDIT.md](AUDIT.md) §1.17。

## 10.53 真值产生点收敛为一处，以及它牵出的 `+` 链折叠缺陷（BOARD 行 149、150）

**这一节的起点不是「再找一个同形缺陷」，而是换一类。** §10.50/§10.51/§10.52 连着三节都是「集合的某个消费者只读了字面量那一部分」，再找第四个没有新信息。于是转去查**两份副本的系统性差异**：`.verify/v31/twocopies.py` 从 `src/runtime/runtime.c`（WIN32）与 `src/runtime/runtime_posix.c` 各抽出 `vm_register_builtin*` 注册的函数体，去注释、去空白、把函数名统一成 `FN` 之后逐字节比对。59 个同名内建里 **2 个相同、56 个不同、1 个没有函数体**。比例最低的几个是 `lower`/`upper`（0.264）、`bool`（0.256）、`atomic_get`（0.247）、`match`（0.227）—— 逐个看下去，`bool` 那一对把话题带到了真值本身。

**症状：真值有五个产生点，六个答案。** 判断真值的地方各自写了一遍三目链：

| 位置 | 空串 `""` | 数组 `[1,2]` | 字典 `{1:2}` | 集合 `(1,2)` |
| --- | --- | --- | --- | --- |
| `L_JUMP_IF_FALSE`（`if`） | 真 | 真 | 真 | 真 |
| `L_JUMP_IF_TRUE` | 假 | **真** | 假 | 假 |
| `L_OR` | 假 | 真 | 假 | 假 |
| `L_NOT` | 假 | 真 | 假 | 假 |
| `posix_core_bool`（`bool()`） | **假** | 真 | 真 | 真 |
| WIN32 `builtin_bool` | 假 | **假** | 假 | 假 |

`if s` 认为空串为真，`not s`、`s or x` 与 `bool(s)` 认为它为假。而 `docs/DECFY_DESIGN.md` §3.3 第 2 条 要求的是「`OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` 各对应一条 `W_IF` 映射，**真值产生点唯一**」，§1.1 表第 3 行 写下的规则是那条三目链的尾句 `… : (va.type == VAL_NIL) ? 0 : 1`，即**非 nil 且非零为真，空串也算真**。

**判据必须是短路，不能是返回值。** 第一轮探测看的是 `"" or "FALLBACK"` 的返回值，得出「空串在 `or` 里是假」——**这是错的**：O0 那次修复之后 `and`/`or` 恒产出布尔，所以 `"" or 1` 与 `1 or 1` 都是 `true`，返回值不区分。改用带副作用的右操作数（`.verify/v31/sc.im` 的 `func boom(t) { say "  BOOM:" + t; return true }`）才看得见：修复前 `"" or boom()`、`{1:2} or boom()`、`(1,2) or boom()` **都不短路**，只有数组短路。

**修法：唯一入口 `vm_truthy`。**

```c
int vm_truthy(const Value *v) {
    switch (v->type) {
    case VAL_BOOL:  return v->ival != 0;
    case VAL_INT:   return v->ival != 0;
    case VAL_FLOAT: return v->fval != 0.0;
    case VAL_NIL:   return 0;
    default:        return 1;
    }
}
```

新增在 `src/vm/vm.c:293`、声明在 `src/vm/vm.h:356`；六处调用点全部改为调用它：`src/vm/vm.c:3341-3342`（`L_AND`）、`:3348-3349`（`L_OR`）、`:3355`（`L_NOT`）、`:3538`（`L_JUMP_IF_FALSE`）、`:3543`（`L_JUMP_IF_TRUE`），以及 `src/runtime/runtime_posix.c:69` 与 `src/runtime/runtime.c:68` 两份 `bool()`。三份文件里旧的三目链 `grep -c` 已为 0。`src/vm/vm.c:3896` 的 `OP_IS_NIL`（`(R[ins.r2].type == VAL_NIL) ? 1 : 0`）判的是 nil 本身而不是真值，**正确地未动**。

**为什么统一到「空串为真」而不是统一到 `bool()` 的「空串为假」。** ① 前者是 `docs/DECFY_DESIGN.md` §1.1 表第 3 行 写下的规则，且六处里本来就有三处如此；② 前者**完全不动 `if` 的控制流**，对既有 `.im` 程序的爆炸半径为零 —— 反过来统一到「空串为假」会让每个 `if s` 在 `s` 为空串时静默换分支。代价是 **`bool("")` 从 `false` 变成 `true`**，这是本次唯一面向用户的语义变化，单独写在明处。

**判据。** 新增 `vtest/truthiness_single_point_v06.im` ← CTest **`truthiness_single_point_runtime`（#116）**，一行断言：

```
truth estr:T:false:true:0:1 str:T:false:true:0:1 arr:T:false:true:0:1 dict:T:false:true:0:1 set:T:false:true:0:1 zero:F:true:false:1:0 one:T:false:true:0:1 fzero:F:true:false:1:0 nil:F:true:false:1:0
```

每格是 `键:if:not:bool:or调用数:and调用数`：真值一律 `T:false:true:0:1`（`or` 短路、`and` 求值），假值一律 `F:true:false:1:0`。`FAIL_REGULAR_EXPRESSION` 正对修复前的四格（`estr:T:false:false:1:1` 与 `str`/`dict`/`set` 的 `…:true:1:1`），**已用 `grep -E` 双向验证**：匹配修复前的那一行、不匹配修复后的那一行。

**写这个测试踩的坑，值得单独记一笔。** 计数器不能用全局标量。`.im` 里函数体内的 `n = n + 1` 创建的是**局部变量**、会遮蔽全局，于是 `bump()` 数的是自己的局部、调用方读的全局永远是 0 —— 第一版九个格子全印 `0:0`，**看起来像「短路全对」**。改用全局数组的一个槽（`cnt[0] = cnt[0] + 1`）才观察得到。一个恒为 0 的计数器比没有计数器更危险，因为它给的是一个假的全绿。

**它牵出的第二个缺陷：函数里的 `+` 链会把一个参数折进操作数。** 真值测试写不下去的时候，`func f(k, v) { return k + ":" + "z" }` 这条探针暴露了别的东西：

| 调用 | 修复前 | 期望 |
| --- | --- | --- |
| `f("arr", "s")` | `arrs:` | `arr:z` |
| `f("arr", 5)` | `arr5:` | `arr:z` |
| `f("arr", [1,2])` | `Error: '+' is not defined for arrays/dicts …` | `arr:z` |

第二个参数**根本没参与**这个表达式，却出现在结果里。字节码是 `LOADK_STRING r3` / `LOADK_STRING r4` / `CONCAT r5, 1, 3` / `RETURN r5`，而 `L_CONCAT`（`src/vm/vm.c:3094`）折的是**连续区间** `R[r2 .. r2+r3-1]`；链的真实操作数在 r1（参数 `k`）、r3、r4，于是 `R[1..3]` 读到的是 `k`、`v`、`":"`，正好是 `"arr" + "s" + ":"`。参数 `v` 是被**读**进来的，不是被**传**进来的。

根因在发射器 `src/compiler/compiler.c` 的 `case TOK_PLUS`（`:735-808`）：它把左结合 `+` 链压平成 `ops[0..nops-1]`，注释（`:749-750`）自己就写着「OP_CONCAT assumes contiguous operand registers R[first..first+nops-1]」，`:751-757` 的守卫也挡掉了多寄存器种类 —— 但白名单里留着 **`EXPR_IDENT`**。局部变量与参数解析到**自己已有的寄存器**而不是新分配一个临时，连续区间出现空洞，而发射器**从未检查过连续性**，直接把 `first` 和 `nops` 交给了 `OP_CONCAT`。

**修法：先占住整段区间，再把每个操作数搬进它的槽位。**

```c
int first = next_register;
for (int i = 0; i < nops; i++) (void)alloc_reg();
for (int i = 0; i < nops; i++) {
    int r = compile_expr(comp, ops[i]);
    if (r != first + i) emit(comp->curBC, OP_MOV, first + i, r, 0);
}
emit(comp->curBC, OP_CONCAT, first, first, nops);
release_to(comp, first + 1);
```

连续性于是**由构造保证**，而不是由守卫的枚举去猜。守卫限制操作数只能是单寄存器种类，所以每个操作数最多多分配一个临时，峰值寄存器 `nops + 1`；`release_to(comp, first + 1)` 复现了原来「第一个操作数作为结果复用、其余临时被吃掉」的语义。`.verify/v31/d2.im` 的八行（参数在链首、局部变量在链首、局部变量在链中、全字面量折叠路径、混合字面量）三后端一致，`f("arr", [1,2])` 由抛错变为 `arr:z`。

**门禁实测。** 十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`（`warnings: 0` —— **该计数当时是假的**，见 §10.54）、`✔ ctest (expect 116/116, 0 skipped)`（`100% tests passed, 0 tests failed out of 116`）、`✔ differential fuzz (interp vs AOT, expect 0 findings)`（`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`）、`✔ economy migration (39/39)`、`✔ node protocol suites (12 registered)`、`✔ dsh-inimerse plugin (offline + live)`、`✔ oauth_loop crate (75/75)`、`✔ userdata ignore rules`、`✔ docs relative links`（`check_links: 93 markdown files, 403 links (17 external, 0 anchors, 386 local), 0 broken`）、`✔ docs backtick paths`（`check_doc_paths: 16 markdown files, 411 backtick 引用, 0 broken`）。

**诚实边界。** ① 两个编译后端**根本走不到真值这些值**：AOT 的 `nv_tru`（`src/compilation/aot_native.c:193`）只处理数字，wasm 的 `cg_cond`（`src/compilation/wasm_backend.c:778-845`）对 `INT`/`BOOL`/`FLOAT` 之外的 tag 直接返回 1，而且 `grep -c "EXPR_ARRAY\|EXPR_SET\|EXPR_DICT"` 在两个后端里都是 **0** —— 字符串与容器的真值只在解释器里有定义，这一节的三后端一致性**无从验证**，只能验证解释器自洽。② `src/verse/crp.c:57` 的 `vj_truthy()` 是**另一套类型系统**，它自己的注释就写着「JS `x || fallback` truthiness」，空串为假是刻意的，不在本次范围内、也不应改。③ `+` 链这次修的是**连续性**而不是链式折叠本身，`INDEX`/`CALL`/`MEMBER` 仍按原设计退回 `OP_ADD`（那条路本来就对，只是慢）。④ 两个缺陷**都没有退出码信号**：不抛异常、不报错，只是安静地算错，与 §10.50/§10.52 同类 —— 这正是模糊测试的盲区（它只覆盖 AOT 能接受的数字子集），只能靠手工探针撞出来。详见 [AUDIT.md](AUDIT.md) §1.18 与 §1.19。


## 10.54 union 的另一个成员，以及门禁那句「warnings: 0」从来没被测过（BOARD 行 151、152）

**症状与发现路径。** 第 13 轮换了一类去找：不再追「快路径当答案」（§10.50/§10.52/§10.53 已三例），而是把 WIN32 与 POSIX 两份运行时的内建逐个对读（`.verify/v31/twocopies.py`，59 个同名内建，逐字相同 2 个、不同 56 个）。对读过程中先看到 `float` 两份写法不一致，探针一跑就撞见 `float(true)` 答 `4.9406564584124654e-324`。

**根因。** v3.1 把 `Value` 的整数槽改成 64 位时，为守住 32 字节宽度契约（`docs/DECFY_DESIGN.md` §2(a)「值表示 + 集合运行时」行）用的是匿名 union：`ival` 与 `fval` **共享存储**。于是所有「只分两种类型」的取值写法 `X.type == VAL_INT ? X.ival : (int)X.fval` 对 `VAL_BOOL` 都会去读 `fval`，读出来的是 `ival` 的位模式。`float(false)`/`float(nil)` 答 0 只是碰巧（位模式全零）。

**六处实测（修复前 → 修复后）。** `sqrt(true)` `2.2227587494850775e-162` → `1`；`float(true)` `4.9406564584124654e-324` → `1`；`gc_auto(true)` `0` → `1`（这一格最重：**静默地把 GC 关掉**）；`atomic_add("k", true)` `0` → `1`；`atomic_set("j", true)` `0` → `1`。

**修法。** 补上 `val_as_double` 缺的整数对偶 `val_as_int`（`src/vm/vm.c`，声明 `src/vm/vm.h:355`），每个分支先看 tag。替换 10 处：`src/runtime/runtime_posix.c` 的 `posix_core_float`（`:82-95`）、`posix_sqrt`（`:579`）、`posix_atomic_add`（`:734`）、`posix_atomic_set`（`:757`）、`posix_spi_meta`（`:923`）；`src/runtime/runtime.c` 的 `builtin_sqrt`（`:16`）、`builtin_gc_auto`（`:1550`）、`builtin_spi_meta`（`:1198`）；`src/mod/verse_dist_mod.c`（`:1953`、`:2049`）。审计后确认不用改的四处写在 [AUDIT.md](AUDIT.md) §1.20。

**判据。** 新增 CTest **`union_member_tag_runtime`（#117）**，钉一行 `union-ok sqrt-true=1 sqrt-false=0 sqrt-int=2 float-true=1 float-false=0 float-nil=0 aadd=1 aset=1 gcauto-true=1 gcauto-false=0`；FAIL 正则 `sqrt-true=2\.22|float-true=4\.94|aadd=0|aset=0|gcauto-true=0` **已双向验证**（`grep -Ec`：修复前 1、修复后 0）。`EXP_CTEST` **116 → 117**，§2 与本表同步 **117 / 117**。

**顺带撞出的门禁缺陷。** `tools/gate.sh` 的 `stage_build` 先真编译一次，**再编一次**并数第二次的输出 —— 热树上第二次什么都不做，所以那个计数**只可能**是 0。实测：`touch src/runtime/runtime_posix.c` 后第一次构建 1 条警告，紧接着第二次 0 条。也就是说历次门禁记录里那句 `✔ build（warnings: 0）`（§10.50/§10.51/§10.53）**从来没有被测过**，它盖住的是 **34 条警告 / 23 个位置**。已改成只统计真正编译的那一次（日志落盘、失败时 `cat`），警告数仍是信息性的、不作断言（把它做成断言需要一个与编译器版本绑定的数字，那是脆的）。

**修掉的机械警告。** `-Wunused-result` 的 `fread` 七处（`src/compiler/bytecode.c:566`、`src/mod/verse_dist_mod.c:319`、`src/mod/record_mod.c:66`/`:193`、`src/mod/mod_posix.c:23`、`src/runtime/runtime_posix.c:582`、`src/runtime/runtime.c:17`）改成接住返回值并按**实际读到的字节数**收尾；`chdir` 三处（`src/main.c:280`/`:454`/`:878`）—— 注意 **`(void)` 强制转换消不掉 `-Wunused-result`**，实测仍报，要写成 `if (_chdir(p) != 0) { }`；`_GNU_SOURCE` 重定义（`src/main.c:1`）加 `#ifndef` 保护。

**门禁实测。** 十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`（**`warnings: 25`** —— 这是本仓库第一次如实报出警告数；`errors: 0`）、`✔ ctest (expect 117/117, 0 skipped)`（`100% tests passed, 0 tests failed out of 117`）、`✔ differential fuzz (interp vs AOT, expect 0 findings)`（`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`）。**`--clean-first` 是必需的**：只统计「第一次构建」在树恰好是脏的时候才对，门禁经常在刚构建过的树上跑，那时增量构建一个文件都不重编、计数又变回 0（这正是修完第一版后仍看到 `warnings: 0` 的原因）。

**诚实边界。** ① 还剩下 **16 条 `-Wformat-truncation`**（清单在 [AUDIT.md](AUDIT.md) §1.21），本轮**未动** —— 每一条都要判断是扩大缓冲、显式接受截断还是改成拒绝，留作独立一轮。② 六处缺陷**都没有退出码信号**，只改值：不抛异常、不报错，和 §10.50/§10.52/§10.53 同类。③ `sqrt` 仍然不吃字符串（`float("9")` 走 `strtod`，`sqrt("9")` 不是 3），是另一个未修的既有缺口。④ WIN32 的 `builtin_sqrt` 与 `src/mod/verse_dist_mod.c` 的两处端口解析**本机不可执行**（Linux 上这两个目标不编），能证明的只是与 POSIX 写法逐字一致。


## 10.55 16 条 `-Wformat-truncation` 清空，以及「路径拼接拒绝而不是截断」的新约定（BOARD 行 153）

**起点。** §10.54 让门禁第一次如实报出警告数，那个数字是 `warnings: 25`（全量 `--clean-first`），其中 **16 条是 `-Wformat-truncation` / 15 个不同位置**（`src/platform/http_posix.c` 编进 4 个目标，所以按出现次数是 4 条）。本轮把它们全部修掉，`warnings` 归零。

**为什么这些警告是真的。** `-Wformat-truncation` 说的是 `snprintf` 的目标缓冲区可能装不下格式化结果，而 `snprintf` 的回应是**静默截断**。对一条路径来说，截断意味着文件写到别的地方；对一条诊断信息来说，意味着用户看到半句话。GCC 只在能证明边界时才报，所以每一条都需要一次判断。

**三种判断。**

① **缓冲区本来就该更大**：`src/compiler/compiler.c:912` 的 `char fname[256]`（`:919` 的 `nsfull_c` 就是 `char[512]`）→ **512**；`src/compiler/compiler.c:1952` 的 `char tname[256]`（固定前缀 `"sprite#"` 7 字节 + `sname[256]`）→ **264**；`src/mod/replay_mod.c:286` 的 `char tail[256]`（固定 40 + `key_esc[288]` + `g_prev_hash[65]` = 最坏 391）→ **448**。

② **截断可接受，但要显式写出来**：`src/lint_mod.c:377` `"%s"` → `"%.95s"`；`src/lint_mod.c:425`/`:429` 的 `variant[8]`/`type_name[64]` → `%.7s`/`%.63s`（最坏 165 < 320）；`src/mod/verse_dist_mod.c:588`/`:590` → `"http://%.500s/v/%.680s"`（最坏 1190/1194 < 1200）；`src/platform/http_posix.c:553`/`:555` → `"http://%.499s/ping"` / `"%.506s/ping"`。

**`src/platform/http_posix.c:556` 的第一版 `%.500s` 仍然报同一条警告**：`"http://"`(7) + 500 + `"/ping"`(5) = 512，正好等于 `sizeof url`，但 `snprintf` 的容量**包含结尾 NUL**，所以差一字节；`%.499s` 才是 7 + 499 + 5 = 511 + NUL = 512。这是这类 off-by-one 的常见来源。

③ **路径不能截断 —— 改成拒绝**：`src/main.c:252`、`src/mod/verse_dist_mod.c:386`、`:839`（两处）、`:1305` 四处换成 `im_platform_path_join`（`src/platform/platform.c:139-150`，装不下返回 **-1**，同时归一化分隔符、按平台选 `/` 或 `\`）。理由：静默截断的路径把文件写到**错误的位置**，那是安静的数据损坏；被拒绝的 join 只是一个被跳过的条目。调用方一律 `if (im_platform_path_join(...) != 0) continue;`。`src/lint_mod.c:181` 的 `lint_add` 没有「扩大」这个选项（`LintBuf` 已 12.8 KB、可能栈上），改成前缀 `snprintf` + 显式 clamp + `memcpy` 余量 + NUL，**`msg` 完全不再走 `%s`**。因为 join 自己按平台选分隔符，`src/main.c:252` 与 `src/mod/verse_dist_mod.c:386` 里那两段 `#ifdef _WIN32` 的 `/`→`\` 转换循环随之删掉。★ **这四个号由三笔平移，量各不相同** —— 写下时它们是 247 / 384 / 839 与 839 / 1297：`git show d6bc6a2^:src/main.c | sed -n '247p'` 印出 `snprintf(outPath, sizeof outPath, "%s/%s", outDir, name);`，`git show d6bc6a2:src/main.c | grep -n im_platform_path_join` 印出 251、`git show 46944f02:src/main.c | grep -n im_platform_path_join` 印出 252（先 4、再 1）；`src/mod/verse_dist_mod.c` 由 `d6bc6a2` 从 384 推到 386，而 1297 那一处先到 1298、后来到 1305（两次插入）。

**判据。** 把 join 的语义补进既有的 `src/platform/platform_probe.c`（CTest `platform_probe`，`CMakeLists.txt:190`）：装不下必须返回 -1、`NULL`/零容量必须返回 -1、多余分隔符必须归一化。**没有新增 CTest，`EXP_CTEST` 保持 117**，§2 与本表不变。

**门禁实测。** 全量 `--clean-first` 构建 **`warning lines: 0`**（`.verify/v31/warn4.txt`，`BUILD_RC=0`）；`platform_probe` 退出码 **0**；`ctest -R 'platform_probe|truthiness|union_member|...'` **8/8 Passed**。

**诚实边界。** ① **没有为「路径过长时跳过条目」写端到端测试** —— 触发它需要构造超过 1024 字节的路径，而 `zip_extract_all` / verse jar 解包都不可从 `.im` 直接调用；能钉住的只有 `im_platform_path_join` 本身的语义，拼接点是否都检查了返回值只由代码审阅保证。② `warnings: 0` 现在是真的，但 `stage_build` **仍然对警告数返回 0**，它不是断言；这一轮能说「归零」是因为全量 `--clean-first` 的输出被落盘并数过，而不是因为门禁说 0。③ WIN32 目标本机不编，`src/main.c` / `src/mod/verse_dist_mod.c` 的 Windows 分支只有与 POSIX 逐字一致这一层保证。④ 清掉的是 GCC 当前愿意报的那些，换编译器版本可能报出新的。

## 10.56 `+` 里非字符串的左操作数被静默丢掉（BOARD 行 154）

**起点。** §10.55 收尾时留了一个「下次再看」的疑点：`str(1 + "7")` 看起来是 `7`，而 `L_ADD` 的源码写的是「任一操作数是字符串就拼接」。改用变量（排除常量折叠）后确认：这不是疑点，是缺陷。

**症状。** 用 `x = 5` / `y = "7"`：

| 表达式 | 修复前 | 修复后 |
| --- | --- | --- |
| `x + y` | `7` | `57` |
| `x + "a"` | `a` | `5a` |
| `nil + "x"` | `x` | `nilx` |
| `true + "x"` | `x` | `truex` |
| `5.5 + "x"` | `x` | `5.5x` |
| `x + y + "8"` | `78` | `578` |
| `x + y + x` | `75` | `575` |
| `"a" + x` | `a5` ✓ | `a5` ✓ |

退出码全程 0，只有值不对。`"7" + 1` 一直是 `"71"`，所以规则本身没有争议，是**只实现了一半**——静默数据丢失，不是优先级问题。

**机制。** `L_ADD`（`src/vm/vm.c`）把**右**操作数送进 `value_to_string`，**左**操作数直接读 `a->sval`。`sval` 是 `Value` 里独立的 `char *`（**不在 union 内**，所以不会被 `ival` 的写入覆盖），刚 load 出来的整数那里是 NULL，于是左边贡献空串。`L_CONCAT` 的一般折叠路径有**逐字相同**的不对称（`const char *sa = acc.sval ? acc.sval : ""`），所以三项以上的链丢的是同一个操作数——这正是为什么 2 项链（走 `OP_ADD`）和 3 项链（走 `OP_CONCAT`）必须用同一条规则。

**修法。** 两处都改成对称选择：类型是 `VAL_STRING` 就取 `sval`，否则 `value_to_string` 进一个新的 `abuf3[128]` / `abuf2[128]`；两处各加一段注释写明两条折叠路径必须一致。

**判据。** 新增 CTest **`concat_left_operand_runtime`（#118）**，钉一行 `concat-ok intstr=57 strlit=5a nil=nilx bool=truex float=5.5x chain3=578 chainix=575 rev=a5`；FAIL 正则 `concat-ok intstr=7 |strlit=a |nil=x |bool=x |float=x |chain3=78 |chainix=75 ` **已双向验证**（`grep -Ec`：修复前 1、修复后 0）。末尾空格是刻意的——引擎把字符串池 dump 到 stderr，而 CTest 连 stderr 一起捕获，带尾空格的模式匹配不到池里的任何字面量。`EXP_CTEST` **117 → 118**，§2 与本表同步 **118 / 118**。

**门禁实测。** 十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`（`warnings: 0`、`errors: 0` —— 这个计数从 §10.54 起才是真的）、`✔ ctest (expect 118/118, 0 skipped)`（`100% tests passed, 0 tests failed out of 118`）、`✔ differential fuzz (interp vs AOT, expect 0 findings)`（`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`）、`✔ economy migration (§43.5, expect 39/39)`、`✔ node protocol suites (expect 12 registered)`、`✔ dsh-inimerse plugin (offline + live)`、`✔ oauth_loop crate (expect 75/75)`、`✔ userdata ignore rules (default deny)`、`✔ docs relative links`（`check_links: 93 markdown files, 409 links (17 external, 0 anchors, 392 local), 0 broken`）、`✔ docs backtick paths`（`check_doc_paths: 16 markdown files, 425 backtick 引用, 0 broken`）。日志：`.verify/v31/gate_concat.log`。

**诚实边界。** ① **只有解释器有这条路径** —— AOT 的 `nv_add`（`src/compilation/aot_native.c:220-225`）压根不处理字符串（`emit_expr` 只有 `EXPR_NUMBER` 一个 case），wasm 同样拒绝字符串，所以三通道差分模糊测试**测不到它**。② **f-string 从没走过坏路径** —— `$"sum={name + 1}"` 被明确拒绝（`Error: f-string interpolation only supports plain identifiers inside {}`），而 `parse_fstring` 生成的链第一个操作数永远是字面量字符串。③ **编译器无辜** —— `x = 1; y = "7"; z = x + y` 的字节码是正确的 `OP_ADD`（`LOAD_GLOBAL r1 = x`、`LOAD_GLOBAL r2 = y`），缺陷在 VM 的拼接分支里。

## §10.57 atomic_* 的宽度，以及溢出该抛而不是回绕

**这一轮做的是「同一个计算有若干个产生点」的又一次实例** —— §10.53 的真值、§10.56 的 `+` 都属这一类；
这次窄的不是类型判断，而是**宽度**：语言的整数层是 int64，`atomic_add` / `atomic_set` 却是 32 位。

**症状。** `.verify/v31/atom.im` 修复前：`literal=3000000000`（字面量本身没问题），
但 `get-after-set=-1294967296`、`add-big-ret=-1294967296`、`add-2p31-ret=-2147483648`、
`get-after-2p31=-2147483648`。`add-small=7`、`add-float=8` 正常 —— 小数字看不出来。
**退出码 0**，只改值。`atomic_get` 一直是对的，所以缺陷只在**写之后**可见。

**机制。** POSIX `posix_atomic_add`（`src/runtime/runtime_posix.c:729`）是
`int d = (int)val_as_int(&delta);` → `int old = __sync_fetch_and_add(&…ival, d);` → `push_int(vm, old + d);`，
三步各窄一次；`posix_atomic_set`（`:753`）是 `int val = (int)val_as_int(&value);`。
WIN32 三个函数（`src/runtime/runtime.c` 的 `builtin_atomic_add` `:1570`、`builtin_atomic_get` `:1604`、
`builtin_atomic_set` `:1623`）逐字对应地窄，用的是 `LONG` 版的 `Interlocked*`。

**为什么不能只加宽。** `+` 对整数溢出有明确规矩：`.verify/v31/ovf.im` 两行
（`x = 9223372036854775807` / `say x + 1`）给出 `[exception] uncaught: numeric_overflow`、退出码 1。
而裸的 `__sync_fetch_and_add` 看不见自己的溢出（先写再返回旧值），所以「加宽」只会把
「静默少 2^32」换成「静默回绕到 INT64_MIN」。要判溢出就必须先算后写 —— CAS 循环。

**修法。** `.verify/v31/atomicfix.py`（逐处 `assert b.count(old)==1` 的字节级替换，两文件共 6 处）：
两处 `atomic_add` 改成 CAS 循环（POSIX 用 `__builtin_add_overflow`，WIN32 因 MSVC 无此内建函数而手写
`LLONG_MAX`/`LLONG_MIN` 边界），溢出时 `vm_throw_kind(vm, "numeric_overflow")` 且**槽原样不动**；
`atomic_set` 的 `int` 换成 `long long`/`LONG64`；WIN32 两个手写三元换成 `val_as_int`（顺带修掉
`atomic_*(…, true)` 0 → 1，与 §10.52 同源）；`runtime.c` 补 `#include <limits.h>`；三个 `Interlocked*`
全部换成 `Interlocked*64` 打在 `(volatile LONG64*)&…ival` 上。修复后 `.verify/v31/atom.im`：
`get-after-set=3000000000`、`add-big-ret=3000000000`、`add-2p31-ret=2147483648`、`get-after-2p31=2147483648`；
`.verify/v31/atom2.im` 现在打印 `max=9223372036854775807` 然后抛 `numeric_overflow`（原来是静默回绕）。
构建 `BUILD_RC=0`，无新增警告。

**判据。** 新增 CTest **`atomic_int64_width_runtime`（#119）**，钉一行
`atomic-ok set=3000000000 add=3000000000 get=3000000000 add2p31=2147483648 get2=2147483648 small=7 getsmall=7 ovf1=numeric_overflow keep1=9223372036854775807 half=9223372036854775807 ovf2=numeric_overflow keep2=9223372036854775807`；
FAIL 正则 `set=-1294967296|add=-1294967296|get=-1294967296|add2p31=-2147483648|get2=-2147483648|ovf1=0 |half=-1 |ovf2=0 `
**已双向验证**（`grep -Ec`：修复前 1、修复后 0）。溢出用 `try { … } catch (err) { … }`
（`docs/SYNTAX.md:339-340`）接住，`str(err)` 就是 `numeric_overflow` —— 所以测试只断言**值**，
不依赖退出码，也就不需要 `WILL_FAIL` 记账项。`EXP_CTEST` **118 → 119**，§2 与本表同步 **119 / 119**。

**门禁实测。** 十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`（`warnings: 0`、
`errors: 0`）、`✔ ctest (expect 119/119, 0 skipped)`（`100% tests passed, 0 tests failed out of 119`）、
`✔ differential fuzz (interp vs AOT, expect 0 findings)`（`gate: fuzz findings match the pin (0 DIVERGE, 0 THREW, 0 untranslated).`）、
`✔ economy migration (§43.5, expect 39/39)`、`✔ node protocol suites (expect 12 registered)`、
`✔ dsh-inimerse plugin (offline + live)`、`✔ oauth_loop crate (expect 75/75)`、
`✔ userdata ignore rules (default deny)`、`✔ docs relative links`、`✔ docs backtick paths`。
日志：`.verify/v31/gate_atomic.log`。

**诚实边界。** ① **WIN32 那三个函数本机不可执行** —— 该目标不在 Linux 上构建，只能证明它们与 POSIX
逐字同构、且两个文件里再无残留的 32 位 `Interlocked*` 或 `(int)val_as_int`（`grep` 为空），**不是**跑过；
② 两份实现的溢出判定**故意不同构**（`__builtin_add_overflow` vs 手写 `LLONG_MAX`/`LLONG_MIN`），
因为 MSVC 没有那个内建函数；③ 字符串参数仍然等于 0（`val_as_int` 的 `default`），与 `sqrt("9")` 不是 3 一致，
但与 `int("7")` = 7 不同，本轮不改；④ `atomic_*` 在此之前**没有任何文档**，
`grep -rn atomic docs/*.md` 除 §1.20 的表格外没有命中。

## §10.58 同一个程序在两个平台上意思不同：`sum()` 的非数字元素

**这一轮修的不是「算错」，而是「两个平台对同一段程序给出不同的含义」。** §10.53 的真值、§10.56 的
`+`、§10.57 的 atomic 宽度都是「同一个计算有若干个产生点」；这次的两个产生点是 **POSIX 与 WIN32 两份
`sum()`**，而且分歧是**已知**的 —— §1.15 当初写下「保留它自己的错误风格（非数字元素
`vm_throw_msg(...)`，而不是 POSIX 的 nil）」。那是个保守决定，不是正确性论证；这一轮把它推翻。

**症状。** `.verify/v31/sumd.im`（修复前，POSIX）：

```
sumstr=nil        # sum([1, "a"])
sumnil=nil        # sum([1, nil])
sumbool=nil       # sum([true, 2])
sumarr=nil        # sum([1, [2]])
sumstrplus1=1     # sum([1, "a"]) + 1   <-- 真正的伤害
sumstr_is_nil=true
```

而 WIN32 的 `builtin_sum`（`src/runtime/runtime.c`）在这四种输入上**抛**
`sum: non-numeric element`。于是同一个 `.im` 程序在 Linux 上打印 `1`，在 Windows 上中止。

最后一格是关键：`nil` 被算术吸收（`nil + 1` == `1`），所以「求和失败」这个信号**不可观测** ——
调用方分不清「和是 1」和「和失败了」。这正是 §1.14 让整数溢出抛异常而不是回绕的同一条理由。

**推翻保守决定的理由。** ① 平台相关的程序含义本身就是缺陷 —— 门禁里有
`differential fuzz (interp vs AOT)` 阶段，正因为「同一程序在不同通道上不同」被当作缺陷，
而 POSIX 与 WIN32 之间**没有任何门禁**；② `nil` 在这里不可观测；③ §1.14（`docs/AUDIT.md:624`）只规定**算术**溢出抛异常而不是回绕，**不是通用教条；本节原先把它升格了，更正见 §1.46。**

**顺带修掉的第二处。** WIN32 的元素规则是 `if (t == VAL_STRING || t == VAL_BOOL) { ok = 0; break; }`
—— **`VAL_NIL` 不在拒绝之列**，`val_as_double(nil)` 得 0.0，所以 `sum([1, nil])` 在 Windows 上答 **1**。
POSIX 的规则（`t != VAL_INT && t != VAL_FLOAT`）更严。统一到 POSIX 那条。

**修法。** `.verify/v31/sumfix.py` + `.verify/v31/sumfix2.py`（逐处 `assert b.count(old)==1` 的字节级替换，
两文件各一处注释 + 一处函数体）：两份实现现在**逐字同构**。关键是把原来那**一个** `ok` 标志拆成
`enumerated` 与 `bad_element` 两个 —— 三种原因给三种信号：元素不是数字 → **抛**；集合无法枚举
（无界，如 `Z`）→ `nil`（与 `list(Z)` 一致，§1.10 的惯例）；参数根本不是容器 → `nil`（同 `list(123)`）。
POSIX 原来把三种压成一种；WIN32 原来靠 `pop` 之后再读 `v->type` 区分，而 `v` 是指进栈的指针 ——
现在两种情况都在 `pop` **之前**就决定好。

**修复后实测**（`.verify/v31/sumd2.im`）：

```
sumstr=THREW:sum: non-numeric element     sumnil=THREW:sum: non-numeric element
sumbool=THREW:sum: non-numeric element    sumarr=THREW:sum: non-numeric element
sumempty=0   sumfloat=3.5   sumok=6   sumbig=9007199254740993
sumZ=nil     sum5=nil       sumabc=nil
```

**判据。** 既有 CTest **`sum_components_int64_runtime`（#114）** 就地扩展，**不新增 CTest**
（`EXP_CTEST` 保持 **119**）：值那半仍是原来那行，末尾接 `refuse-bool=` / `refuse-str=` / `refuse-nil=`
三格，各用 `try`/`catch` 接住并断言 `str(e)`。FAIL 正则
`comp=0|two=0|single=0|big=9007199254740992|refuse-bool=nil|refuse-str=nil|refuse-nil=nil`
**已双向验证**（`grep -Ec`：修复前 **1**、修复后 **0**；修复前的行是
`… inf=nil refuse-bool=nil refuse-str=nil refuse-nil=nil`）。`sum: non-numeric element` **不进字符串池**
（实测 `grep` 计数 **0**），所以 PASS 正则引用它是安全的。`contract_test.im` **66 passed / all passed**。

**门禁实测。** 十阶段**全绿**，`gate: OK — every stage passed.`、`GATE_RC=0`。逐阶段：`✔ build`
（`warnings: 0`、`errors: 0`）、`✔ ctest (expect 119/119, 0 skipped)`（`100% tests passed, 0 tests failed
out of 119`）、`✔ differential fuzz (interp vs AOT, expect 0 findings)`、`✔ economy migration (§43.5,
expect 39/39)`、`✔ node protocol suites (expect 12 registered)`、`✔ dsh-inimerse plugin (offline + live)`、
`✔ oauth_loop crate (expect 75/75)`、`✔ userdata ignore rules (default deny)`、`✔ docs relative links`、
`✔ docs backtick paths`。日志：`.verify/v31/gate_sum.log`。

**诚实边界。** ① **WIN32 那份本机不可执行** —— `CMakeLists.txt:381-395` 把 `src/runtime/runtime.c` 放在
`if(WIN32)` 分支里，Linux 上编译的是 `runtime_posix.c`；对 `runtime.c` 只有「与 POSIX 逐字同构」+
「没有残留旧写法」两条证据，**没有执行过**，打 Windows 包时它是第一次真正被编译；② `err_test.im`
是未被门禁覆盖的旧文件，它早就假设 `sum` 会抛，本轮之后那一段终于走到 `catch`，但同一文件里
`round("abc", 2)` 与 `list(123)` 仍答 `nil` 并打印 `should not print` —— 那是另一类（`nil` 是
`list`/`round` 的既定拒绝惯例），不在本轮范围；③ `sum` 只读栈顶一个参数，`sum(1,2,3,4)` 与
`sum("a","b")` 的行为不由 `argc` 决定，本轮未改。

## §10.59 没有顺序的一对值，比较却答「0」

**这一轮修的是「比较运算符声称两个值相等，而 `==` 说它们不等」。** `val_cmp` 在既不是「两边都是
字符串」也不是「两边都是整数」时落到 `val_as_double(a) < val_as_double(b)`，而 `val_as_double`
的 `default` 是 **0.0** —— 于是**每一个非数字值都按数字 0 参与比较**。

**实测（修复前，`.verify/v31/rt15.im`）：**

```
"abc" < 1   = true      "abc" > -1 = true      "abc" <= 0 = true      "abc" >= 0 = true
"abc" == 0  = false
[1, 2] < 1  = true      [1, 2] > -1 = true     [1, 2] == 0 = false
(1, 2) < 1  = true                             # 集合
nil < 1     = true      nil > -1 = true        nil <= 0 = true         0 <= nil = true
nil == 0    = false
```

两条独立的伤害：① **`<=` 与 `==` 自相矛盾** —— `nil <= 0` 与 `0 <= nil` 同时为真而 `nil == 0`
为假，即 `a <= b && b <= a` 为真却 `a != b`；② **比较本身答错了** —— `"abc" < 1` 为真没有任何
读法成立，字符串不是数字也不是 0。

**顺带一处：`max()` 把 nil 丢掉。** `s = nil, 5` 时 `max(s)` 答 **5**（nil 被当成 0 比较、输掉），
而两个参数的 `min(nil, 1)` 早就答 **nil** —— 同一个问题两种拼写两个答案。`set_minmax`
（`src/vm/vm.c`）的三处 `val_cmp` 只挡了「整数 vs 字符串」（`bestIsStr`），没挡 nil / 数组 / 字典。

**修法。** `.verify/v31/cmpfix.py`（逐处 `assert b.count(old)==n` 的字节级替换，共 9 处）。
顺序只在**两个字符串之间**或**两个数字之间**有定义（数字 = `VAL_INT`/`VAL_FLOAT`/`VAL_BOOL`），
其它任何一对**拒绝**：

```c
static int val_is_num(const Value *v) {
    return v->type == VAL_INT || v->type == VAL_FLOAT || v->type == VAL_BOOL;
}
static int val_orderable(const Value *a, const Value *b) {
    if (a->type == VAL_STRING && b->type == VAL_STRING) return 1;
    return val_is_num(a) && val_is_num(b);
}
```

`L_LT`/`L_GT`/`L_LE`/`L_GE` 四个操作码先查 `val_orderable`，不成立就
`vm_throw_kind(vm, "type_mismatch")` 后 `R = t->reg + t->base; continue;`（与 `L_DIV` 的
`division_by_zero` 同一形状）。`set_minmax` 的三处 `val_cmp` 加同一个前置检查，不成立就沿用
它已有的 `have = -1` 拒绝路径。

**修复后实测**（`vtest/order_requires_orderable_v06.im`）：

```
order-ok strnum=type_mismatch arrnum=type_mismatch nille=type_mismatch zerole=type_mismatch
         nillt=type_mismatch streq=false strlt=true strgt=false numlt=true numle=true
         boollt=true biglt=true minset=nil maxset=nil minscalar=nil
```

后半段是**反方向的判据**，挡住「把 `<` 一律改成抛异常」这种假修复：`"abc" < "abd"` 仍 true、
`"abc" > "abd"` 仍 false、`1 < 2` / `2 <= 2` / `true < 2` / `9007199254740993 < 9007199254740994`
全部不变。

**判据。** 新 CTest **`order_requires_orderable_runtime`**（#120，注册在
`atomic_int64_width_runtime` 之后），`EXP_CTEST` **119 → 120**，计数锚点同轮更新。FAIL 正则
`strnum=true|arrnum=true|nille=true|zerole=true|nillt=true|maxset=5 ` **已双向验证**
（`grep -Ec`：修复前 **1**、修复后 **0**）。**全套 ctest 120 / 120、0 失败** —— 也就是说门禁里
**没有任何用例依赖这个强制转换**，blast radius 为零，这是实测而不是推断。

**门禁实测。** **门禁实测（`.verify/v31/gate_cmp.log`）：** `GATE_RC=0`，`gate: OK — every stage passed.`，十阶段全 PASS，`warnings: 0` / `errors: 0`，`100% tests passed, 0 tests failed out of 120`、0 跳过，差分模糊测试钉住 `(0 DIVERGE, 0 THREW, 0 untranslated)`，`check_links: 93 markdown files, 412 links (17 external, 0 anchors, 395 local), 0 broken`，`check_doc_paths: 16 markdown files, 433 backtick 引用, 0 broken`。（文档在跑门禁之前已写完，所以链接与反引号路径两个阶段已经覆盖到本节。）

**诚实边界。** ① 这一处只在解释器里（AOT 的 `emit_expr` 只有 `EXPR_NUMBER` 一个 case、wasm
拒绝字符串），三通道差分模糊测试**够不到**；② `nil` 现在**完全不可排序** —— `nil <= nil` 也拒绝，
与 Python 的 `None <= None` 抛 `TypeError` 一致，但这也意味着任何拿 nil 参与 `<` 的旧代码现在会
抛 `type_mismatch` 而不是拿到一个（错的）布尔；③ **`val_cmp` 没有加运行时断言**，前置条件靠四个
操作码与 `set_minmax` 各自检查 + 注释说明，将来若有新调用方忘了查，缺陷会以「又答 0」回来；
④ `set_minmax` 对集合里的数组/字典元素以前按 0 比较、现在拒绝 —— 两者都不是「对」，
只是后者不再声称一个数字。

## §10.60 仓库自己的头挡住了 CRT 的同名头：Windows 构建的 158 个提交

Windows 构建从 `39ddabd`（2026-10-02）起一直红，最后一次绿是 `a2a583d`（v0.4.1）。根因不是任何一个函数写错，而是 **`src/platform/process.h` 与 CRT 的 `<process.h>` 同名**，而 CMake 把 `src/platform` 放进了 include 路径 —— `#include <process.h>` 拿到的是仓库自己的头，于是 `_beginthreadex`（`src/platform/thread.c:24`、`src/headless_server.c:188`、`src/mod/gui_mod.c:3199`）和 `_getpid`（`src/common/vverse_pack_probe.c:42`）全部变成「没有声明」。mingw-w64 与仓库同名的头还有 `dir.h`、`parser.h`，目前没有尖括号引用，属于同类隐患。

**修法（三处，互不相关）。** ① 仓库头改名 `src/platform/process.h` → `src/platform/im_process.h`（`git mv`），更新 5 个 C 引用点与 3 处文档反引号引用；没选 `#include_next`，因为那是 GCC 专有扩展，而改名是纯标准 C。② `getline` 与上面无关 —— mingw-w64 的 `<stdio.h>` 里 `getline` **零出现**（MINGW64 与 UCRT64 两个 sysroot 都是 0），所以新增 `src/common/probe_compat.h` 提供 Windows 本地 shim（`fgets` + `realloc`），而不是把两个探针从 Windows 构建里排除。③ `src/common/vverse_pack.c` 的 `VV_STAT` 是 `_stat`，而 `_stat` 展开为 `_stat64i32`（`_mingw_stat64.h:22`），填的却是 `struct stat`，加 `VV_STAT_T` 修正。

**验证。** 直接调用 Windows 侧工具链 `/mnt/c/msys64/mingw64/bin/gcc.exe`（gcc 16.1.0）：对全部 `src/**/*.c` 做 `-fsyntax-only` 只筛 `implicit declaration`，修复前命中 5 处，修复后只剩 `src/platform/http_probe.c:122` 的 `setenv`，而它在 `CMakeLists.txt:202` 的 `if(NOT WIN32)` 里、不参与 Windows 构建。受影响编译单元逐个 PASS。发布会话在仓库外克隆上完成 164/164 干净重建、`inimerse.exe` 链接成功。

**门禁实测（`.verify/v31/gate_win.log`）：** `GATE_RC=0`，`gate: OK — every stage passed.`，十阶段全 PASS，`warnings: 0` / `errors: 0`，`100% tests passed, 0 tests failed out of 120`、0 跳过，差分模糊测试钉住 `(0 DIVERGE, 0 THREW, 0 untranslated)`，`check_links: 93 markdown files, 412 links (17 external, 0 anchors, 395 local), 0 broken`，`check_doc_paths: 16 markdown files, 433 backtick 引用, 0 broken`。**没有新增 CTest，`EXP_CTEST` 保持 120** —— 本轮只动头文件位置与两个宏，不新增可执行行为。

**诚实边界。** ① 本机 MSYS2 没装 `cmake.exe`，我给的证据是逐编译单元的 `-fsyntax-only`，完整构建证据来自发布会话。② Linux 门禁结构上**看不见**这一类缺陷（glibc 没有 `<process.h>`），能挡住它的只有 Windows CI。③ 编译修好后 Windows 的 ctest 仍只有 **54/84**（30 项运行时失败），本轮故意不修。

## §10.61 `vm_init` 的字段清单漏了两个字段：Windows 上 18 个用例段错误

Windows 上 18 个 CTest 用例以 `0xC0000005` 段错误退出，栈完全相同：RVA `0x25673` ⇒ `prof_record_call+0x73`（`src/compilation/profiler.c:101` 的 `fr->depth = depth;`，`st = (ProfState*)vm->prof_state`）。根因是 `src/vm/vm.c` 的 `vm_init` 原本是一张**逐字段赋值清单**、没有任何 `memset`，而清单里从来没有 `prof_enabled`（`src/vm/vm.h:309`）与 `prof_state`（`src/vm/vm.h:310`）；两个调用点 `src/main.c:949`、`src/main.c:979` 都是**栈上**的 `VM vm;`，这两个字段因此是栈垃圾，`src/vm/vm.c:3930` 的 `if (vm->prof_enabled) prof_record_call(...)` 就拿垃圾指针去记录调用。Linux 是绿的只是因为新栈页恰好为零 —— 不是「Linux 对」，是「Linux 没被照到」。

**这是清单这种写法的必然结果，不是两个字段的疏漏。** `vm_init` 自己的注释（原 `src/vm/vm.c:1407`）已经写着「追加字段必须清零」，清单还是漂了。把 `vm_init` 的 `vm->X` 赋值集合与 `src/vm/vm.h` 的结构体声明做差，**12 个字段一次赋值都没有**：`hookCount`、`spi_sub_count`、`spi_sub_cap`、`spi_subs`、`user_data`、`argc`、`argv`、`cur_argc`、`prof_enabled`、`prof_state`、`main_thread`、`mod_bcs`；整组 `im2d_*` 同样一次都没有。

**修法。** `memset(vm, 0, sizeof(VM))` 放在 `vm_init` 最前面，原清单**原样保留** —— 清单设置的是「正确值不是 0」的字段（`sp = -1`、`exec_timeout_ms = 120000`、`mod_caps = -1`、`record_save_path = strdup("save.dat")`、`ent_free_head = -1`），清零保证起点确定，清单保证意图不被清零替代。`im2d_cb_*` 的 `-1` 哨兵不会被破坏：它只在 `src/vm/vm.c:2529` 的 `if (!vm->im2d_ready)` 里被读，而 `:2526` 的 `if (vm->im2d_interval_ms <= 0) return 0;` 先挡掉整条 `vm_frame_callback`，哨兵在第一次被读之前就重新建立。同一轮删掉 Windows 上的调试残留 `src/vm/vm.c:1312-1313`（`[TBP] timeBeginPeriod(1) result=%u`），改为 `(void)timeBeginPeriod(1);`。

**判据（新增 CTest `vm_init_probe`，#121）。** `src/vm/vm_init_probe.c` 先用 `memset(&vm, 0xAA, sizeof vm)` 填脏，再调 `vm_init`，逐项断言全部字段为零以及清单的非零默认值仍正确。**预填 0xAA 是这条判据的全部意义**：它让「删掉 memset」在任何平台上都红。**双向验证**：正常构建 `vm_init_probe: OK`（exit 0）；把 `memset` 换成空注释后重编，探针打印 **25 条 `FAIL`** 并以 exit 1 结束；还原后复绿。探针自己修过一处：第一版负控**丢掉了全部 FAIL 行**（检查失败时结构体按定义未定，`vm_free` 跟着垃圾指针走，进程在 stdout flush 之前以 `free(): invalid pointer` 中止），现在失败路径不调 `vm_free`、先 `fflush(stdout)` 再返回 1。

**计数同步。** `tools/gate.sh:50` 的 `EXP_CTEST` **120 → 121**，`docs/BOARD.md` §3 与本节 §2/§2.1 同步为 **121 / 121**。新探针注册在 `CMakeLists.txt` **测试列表末尾**（`add_test(NAME vm_init_probe COMMAND vm_init_probe)`）—— 它必须排在最后：CTest 的 `#N` 是注册顺序，插在中间会把 `#57` 之后的每一个既有编号整体后移，而多个历史行正引用着那些编号。

**诚实边界。** ① 18 个用例是在 Windows 上观察到的，本机修复前后都绿，所以「修好了那 18 个」的最终确认在发布会话的 Windows 机器上；本节的证据是「字段差集 + 负控探针」。② `memset` 修的是「未初始化」，不是「未定义」：清单与结构体的同步仍然靠人，探针只钉住**这一版**的字段集合，结构体新增字段而清单与探针都没跟上时它不会自动发现。③ 探针只断言 `vm_init` 之后的字段值，不覆盖 `vm_free` 与运行期语义。④ 同一轮**没有**碰 `atomic_*` 的 Windows 位宽缺陷与两个 verse C 探针的失败 —— 它们是独立的三类问题，分开处理。

## §10.62 `atomic_set` 写坏了 `Value` 的联合体：Windows 上它写进去的是 0

`docs/AUDIT.md` §1.29。Windows `atomic_int64_width_runtime`（#119）实测
`atomic-ok set=0 add=3000000000 get=3000000000 add2p31=2147483648 get2=2147483648 small=7 getsmall=7 ovf1=NO-THROW keep1=1 half=4611686018427387903 ovf2=NO-THROW keep2=4611686018427387904`。
根因是 `src/runtime/runtime.c` 的 `builtin_atomic_set` 在 `InterlockedExchange64(…ival, val)` 之后又写了 `…fval = 0`，而 `ival`/`fval` 是同一块存储（`src/vm/vm.h:24-26`）。删掉那一句、把 `sval = NULL` 挪到写 `ival` 之前。全仓 14 处「双写联合体」只有这一处有害；`.verify/v31/union_repro.c` 在 Linux 与 mingw 两个工具链上给出同一结论。Linux 侧未新增 CTest（缺陷不可达）。**没有**在 §2 的测试计数里加任何数字。

## §10.63 路径锚定：调用者的路径锚调用者的 cwd，引擎自己产生的路径锚进程 cwd

`docs/AUDIT.md` §1.30。Windows CI 上 `cli_incremental_regression` 把相对脚本名解析到了构建目录。修法是给「调用者给的路径」一个显式的调用者 cwd 锚。同轮抓到一个自伤回归：`comp->dep_paths[i]`（`src/main.c:779`）是引擎自己产生的**相对**路径（`src/compiler/compiler.h:76` 的 `cur_dir` 在顶层为 `""`，`resolve_import_path` 原样返回 `rel`），语义基准是脚本目录，因此新增 `make_abs_path_cwd()` 并只把那一个调用点换过去。Linux `ctest -R "incremental|dep|compile|selfhost"` 4/4。

## §10.64 「建目录」有两个生产点，两处都错在盘符根，verse 那份还用 `fopen(dir)` 判存在

`docs/AUDIT.md` §1.31。这一条一起解释了 Windows 的 #2/#3/#5/#39/#74。`vl_mkdir_p`（`src/verse/layer.c`）的存在性判据从 `fopen(dir, "r")`（Windows 上打开目录必然失败）换成 `errno == EEXIST`；`vl_mkdir_p` 与 `im_platform_mkdirs`（`src/platform/platform.c:99`）各加一个 `is_drive_root` 谓词，**两处都改**。判据全部是真 Windows 工具链实测，含负控：去掉守卫后 `vl_layer_create` 对绝对路径答 `-2`（`VL_ERR_IO`），加上后 `0`（`VL_OK`）/ 再次 `-3`（`VL_ERR_CONFLICT`）。`src/platform/platform_probe.c` 扩了 8 项 `im_platform_mkdirs` 契约断言。**不新增 CTest，`EXP_CTEST` 保持 121。**

## §10.65 hub 端口：两个生产点只有一个会报实际绑定的端口

`docs/AUDIT.md` §1.32。Windows 的 7 个 hub 用例全部倒在 `start_hub_bound_ports`。`src/headless_server.c` 与 `src/mod/verse_dist_mod.c`（winsock 的 `verse_http_start`）都报「被请求」的端口；POSIX 的两个孪生实现本来就有 getter。两处各加 `getsockname()` 与一个 getter，`src/main.c` 里两组 `#if defined(_WIN32)` 分支删除。Linux 上 9 个 hub 相关用例 9/9 通过。

## §10.66 eventlog 的回滚在 Windows 上被 `#ifndef` 编译掉了

`docs/AUDIT.md` §1.33。`src/verse/eventlog.c` 故障注入第 6 步的 `ftruncate` 原本在 `#ifndef _WIN32` 里，Windows 上回滚不落盘，下一次 `verify()` 答 `VL_ERR_RECOVERY_REQUIRED`。改成宏 `vl_truncate`（Windows `_chsize`）后无条件调用；两个平台都是 `all checks passed`。

## §10.67 谓词在 Windows 上答的是数字，不是布尔

`str(startswith("abc", "a"))` 在 Linux 答 `true`、在 Windows 答 `1`。Windows 的 `builtin_str_startswith`（`src/runtime/runtime.c:465-474`）与 `builtin_str_endswith`（`src/runtime/runtime.c:475-485`）用 `push_int`，POSIX 的 `posix_core_startswith`（`src/runtime/runtime_posix.c:494-500`）与 `posix_core_endswith`（`src/runtime/runtime_posix.c:502-509`）用 `push_bool`，而 Windows 同一文件里的 `builtin_has`（`src/runtime/runtime.c:403`）/`builtin_remove`（`src/runtime/runtime.c:433`）用的也是 `push_bool` —— 异类是 Windows。

对 59 个同名内建做 `push_*` 词汇差分，全部差异只有 5 处 3 类：上述两处，加上 `spi_mods` 与 `mod_usage` 缺 `aidx < 0` 守卫（分配失败时把指向 −1 号槽的引用包装成 dict/array 发布出去；`vm_dict_set` 在 `src/vm/vm.c:1028` 有守卫，所以不越界写），以及 `round` 非数字实参的硬/软拒绝分歧（**保留**，见 [AUDIT.md](AUDIT.md) §1.34 诚实边界 ②）。

**计数同步。** `tools/gate.sh:50` 的 `EXP_CTEST` **121 → 122**，`docs/BOARD.md` §3 与本节 §2/§2.1 同步为 **122 / 122**。新用例 `vtest/predicate_result_type_v06.im` 注册在 `CMakeLists.txt` **测试列表末尾**（`add_test(NAME predicate_result_type_runtime …)`）—— 必须排在最后：CTest 的 `#N` 是注册顺序，插在中间会把既有编号整体后移，而多个历史行正引用着那些编号。

## §10.68 依赖 trailer 的分隔符与模组通知的流向

两处 Windows-only 缺陷，都属于「同一个计算有两个生产点，只有一处守规矩」。

**① trailer 记不下相对路径（#24 cli_incremental）。** `src/compilation/deps.c:83-93` 的 `deps_bc_dirname` 已经在 `_WIN32` 下额外找 `\`（`:86-89`），而 `:97-121` 的 `deps_relative_path` 只按 `/` 切（`:104-105` 的尾部剥离与 `:107-108` 的两处 `strtok`）。`src/main.c:390-396` 的 `normalize_path` 在 Windows 上把 `/` 换成 `\`，所以 `src/main.c:774`/`:781` 传进来的是反斜杠绝对路径，`common` 恒为 0，函数吐出 `".."` 加完整绝对路径；读回时 `deps_entry_abs_path`（`src/compilation/deps.c:123-133`）不认它，sha256 失败，于是**每次增量都重编译**。修法是平台相关的分隔符集合（`_WIN32` 下 `DEPS_SEPS` 为 `"\\/"`）。ucrt64 探针实测：`C:\Users\x\nt2` → `C:\Users\x\nt2\app.im` 由 `../C:\Users\x\nt2\app.im` 变成 `app.im`，`/tmp/tmp.X` 两版都是 `app.im`（POSIX 未变）。

**② 模组加载通知写进了 stdout（#29 wasm_backend）。** 只有 Windows 引擎链接 `mods/build/build_mod.c`（`CMakeLists.txt:400`），而它的 `mods/build/build_mod.c:742` 用 `printf`，另两处模组通知（`src/mod/infiverse_mod.c:839`、`src/mod/verse_dist_mod.c:2688`）都用 `fprintf(stderr, ...)`。改成 stderr；同文件里属于 `build` 内建**自身**输出的那些 `printf`（`:371`/`:373`/`:405`）保持不动。

**没有新增用例。** 两处都在 Linux 上不参与编译（`deps.c` 走 POSIX 分支，`build_mod.c` 不在 POSIX 源列表里），Linux 门禁只能证明「没改坏」：`ctest -R "incremental|dep|compile|selfhost|cli_"` 5/5。详见 [AUDIT.md](AUDIT.md) §1.35 与 §1.36。

## §10.69 客户端在 Windows 上是一份桩，包名只按 `/` 取

两处 Windows-only 缺陷，都属于「同一个计算有多个生产点，只有一处守规矩」。

**① `inim-client` 在 Windows 上整份是桩（#5 verse_closed_loop）。** `src/verse/client.c` 原来在 `_WIN32` 下只有 `int main(void) { ...; return 4; }`，于是 `tools/verse_closed_loop.test.py:221` 期望的 137 拿到 4，客户端根本没发过请求，后面 `seq=0` / `cells=[]` / `head=e3b0c442`（空串的 sha256）全是连锁。修法是把「带两条管道的子进程」抽成 `child_spawn`/`child_kill`/`child_wait`/`child_close` 四个操作、每平台一份，参数解析与请求循环全部共享；POSIX 那半逐行未改，Windows 那半是 `CreatePipe` + `CreateProcessA` + `_open_osfhandle`/`_fdopen`(`_O_BINARY`) + `TerminateProcess`。真 Windows 实测：`#crash` → 137、子进程退出 3 → 原样传出、以 `\` 结尾的 root 参数一字不差、带空格的 server 路径能起来；Linux `ctest -R verse_closed_loop` 仍 Passed。

**② 包名只按 `/` 取（#38 verse_pack）。** `src/mod/verse_dist_mod.c:1082` 的 `strrchr(tail, '/')` 在 Windows 的 `C:\...\mypkg.vverse` 上返回 NULL，整条绝对路径成了包名，`dest` 里出现分量 `C:`，`mkdir` 失败 → `cannot create laws`。同一个文件里 `:271-273`（`home_dir`）与 `:387-389` 早就在同时看 `\`。修法是取靠后的那个分隔符。

**没有新增用例**（两处都是 Linux 上不参与编译的 Windows-only 分支），`EXP_CTEST` 仍为 122。详见 [AUDIT.md](AUDIT.md) §1.37 与 §1.38。

## §10.70 DWARF 的行号程序记下了整条绝对路径

`src/compilation/debug_info.c` 取源文件 basename 时只写了 `strrchr(source_path, '/')`，Windows 的 `C:\dir\app.im` 于是找不到分隔符，整条绝对路径被写进 `.debug_line` 的 line program。修法是同时看 `\` 并取靠后的那个（`:155-157`）。与 §1.35/§1.38 同类。该文件在公共源列表（`CMakeLists.txt:387`）里，两个平台都编，但 Linux 的输入全是 `/`，所以**没有新增用例**，`EXP_CTEST` 仍为 122；判据是协调者在 Windows 上检查 `<out>.debug_line` 记的是 basename。影响面只有调试元数据，不改执行语义。

**另记一条查过并关掉的候选：** `src/compiler/bytecode.c:676` 硬编码 `"%s\\%s"` 把 `/` 换成 `\`，且该文件那里没有平台守卫——但唯一的调用点 `src/main.c:547` 位于 `src/main.c:524` `load_embedded_mods_impl()` 的 `#ifndef _WIN32` / `#else` 的 **Windows 分支**内（POSIX 侧直接 `(void)vm; return;`），所以它只在 Windows 上被调用，不是缺陷。守卫在调用方，不在被调用方。详见 [AUDIT.md](AUDIT.md) §1.39。

## §10.71 有界名称解析：承诺了时间上界的那次连接，真正停住的一步没有上界

`src/platform/socket.c:97` 的 `im_socket_connect_timeout()` 名字里有「timeout」，调用方 `src/platform/http_client.c:25` 也按 5000 ms 传参，两处都承诺了时间上界；但这个 `timeout_ms` 只用在了 `:126` 的 `select()` 上。`:99` 先调用的 `resolve_addr()`（`:66`）第五个参数是 `passive`，不是超时，它里面 `:70` 的 `getaddrinfo()` 完全没有超时 —— 而断网时真正停住的就是这一步。

症状是 peer 报的 `#27 selfhost_codegen_parity`：Linux 13.03 s、Windows `***Timeout 300.05 sec`。根因在 `selfhost/tests/hw_test.im` 的 `http_get("http://example.com")`：它走真实网络，而 `unshare -rn` 断网后同一段分别只要 0.12 / 0.18 / 0.14 s（DNS 立刻失败）。所以慢的不是解析本身，而是断网后 DNS 服务器不回应、`getaddrinfo()` 无限期等待。

**改法。** 名称查找改走 `src/platform/thread.c` 的 `im_thread_start` / `im_thread_join`：在工作线程里解析，超时即拒绝。不用 glibc-only 的 `getaddrinfo_a`，也不改全局的 `RES_OPTIONS` —— 后者动的是整个进程的解析行为，不是一个调用的上界。

**A/B 证据：** HEAD `elapsed=34270 ms` → 修复后 `elapsed=5000 ms`。

**新增 CTest #123 `resolve_timeout_runtime`**（`src/platform/resolve_timeout_probe.c` + `src/platform/slowdns_preload.c`，用 `LD_PRELOAD` 注入一个会停顿的 `getaddrinfo`），注册在 `CMakeLists.txt` 最末（编号规则见 §10.67），`tools/gate.sh:50` 的 `EXP_CTEST` 122 → 123。

**反向对照：** 探针链到 HEAD 的 `socket.c` → `FAIL the bound did not hold (10224 ms for a 2000 ms timeout)`、rc=1；链到修复版 rc=0。同一个探针在旧代码上必须红。

**诚实边界：** ① 钉子（探针）是 POSIX-only，Windows 侧没有对应回归；② 修的是「不再无限等」，不是「解析一定成功」——超时后连接直接失败，调用方拿到的是错误而不是地址；③ 被中断的解析线程仍在后台跑，线程本身没有被取消；④ 上界是**分阶段**的：解析等一个 `timeout_ms`，随后的 TCP connect 又拿一个全新的 `timeout_ms`，最坏总耗时接近 `2×timeout_ms`，别读成硬性总上界。

**独立复核推翻了一版非真空判定。** 第一版探针用时间窗判「停顿有没有真的注入」（`elapsed < 1500` 即判空）。复核者实测 `env -u LD_PRELOAD` 连跑 11 次，第 11 次得到 `elapsed_ms=2871 connected=0` + `resolve_bound: ok` + rc=0 —— 没有注入却全绿；原因是新代码下「无注入」的调用本就要花真实解析加一整个 connect 预算（约 2025 ms），真实 connect 一慢就落进窗口，所以丢掉 preload 只有约 10/11 的概率被抓。现改为让垫片向 `SLOWDNS_LOG` 追加一行、探针读文件判空（`FAIL getaddrinfo was not interposed; the pin is vacuous`），同样的负控 **12/12** 都红。

## §10.72 CMake 的 `ENVIRONMENT` 是最后写者胜

`CMakeLists.txt` 末尾的 `get_property(INIMERSE_ALL_TESTS DIRECTORY PROPERTY TESTS)` 加 `set_tests_properties(${INIMERSE_ALL_TESTS} PROPERTIES ENVIRONMENT "PYTHONIOENCODING=utf-8")`（`CMakeLists.txt:921-923`）会覆盖**每一个**已注册测试的 `ENVIRONMENT` 属性。

第一次把上面那个新测试块放在 `if(INIMERSE_BUILD_ENGINE)` 内时，`build/CTestTestfile.cmake` 里该用例只剩 `ENVIRONMENT "PYTHONIOENCODING=utf-8"`，我们设的 `LD_PRELOAD` 被整个吃掉，探针静默退化成「注入没有发生」（测试因此假绿/假红都说不清）。整块移到那个全局循环**之后**（`CMakeLists.txt:940`）才生效。

**教训：** 任何需要 `ENVIRONMENT` 的测试都必须注册在那个全局循环之后。`set_tests_properties` 不是合并，是覆盖。

## §10.73 探针可诊断性：各个失败码在日志里长得一样

`src/platform/process_probe.c` 旧版只打印 `process_pid=`，失败时统一 `return 1`，于是 peer 报的 `#19 process_probe` 在 Windows 上间歇性变红时，日志里看不出停在哪一步 —— 各个失败码（2/3/4/5/6/7/8/9）的输出完全一样，无法定位。

现在每个失败点自报所在步骤并打印实测值（进程是否已退出、等了多久、期望什么），一律返回 1；旧 step 6 的 20 ms 等待改成 500 ms（对 `cmd.exe` 的启动来说 20 ms 本来就是竞态），并把「慢子进程还活着」从等待之后的判断改成显式的前置检查。

真实 Windows（ucrt64 gcc）上新探针 12/12 通过。

**诚实边界：** 旧探针在本机 20/20 也通过，flake 始终没有被复现。所以这次修的是**可诊断性**与一处可疑的短等待，**不能声称 20 ms 就是那次红的根因**。

## §10.74 一个逃逸守卫读了没写过的字节

`src/platform/vfs.c:22` 的 `..` 守卫用 `strchr(out, '/')` 去搜一个**还没写终止符**的
缓冲区（终止符在 `:29` 才写），于是越过已写入的字节读未初始化内存。实测
`im_vfs_normalize("os:/../escape")` 40/40 返回 0（本该拒绝）。`vfs_probe` 早已断言了正确
行为却从未 `add_test` 注册，门禁从来没跑过它。修法：`memchr(out, '/', w)`。负对照：HEAD 版
`vfs.c` 编的旧探针 rc=2。注册为 CTest `#124`。详见 [AUDIT.md](AUDIT.md) §1.43。

## §10.75 `im_platform_write_file` 的成败极性反了

`src/platform/platform.c:213` 原本 `int ok = (...) ? 0 : -1; if (ok) return 0; return -1;`：
成功返回 -1、失败返回 0。唯一调用者 `src/mod/server_mod_posix.c:89` 以 `!= 0` 判失败，
所以 POSIX 上 `server_start` 每次都 kill 掉自己刚 spawn 的子进程并返回 0 而不是房间号。
修法：直接返回那个三元表达式。`platform_probe` 加往返检查（码 20–23），负对照 rc=20。
详见 [AUDIT.md](AUDIT.md) §1.44。

## §10.76 同一个内建名两份实现，四个不同的答案

59 个同名内建逐条对拍：`int()` 在 Windows 上经 `int` 截断 32 位；`chr()` 不查 type 就读
`ival`（读指针的一半）；`atomic_get`/`atomic_set` 把非字符串名字交给 `strcmp(NULL)` 而
**崩溃**（`builtin_atomic_add:1585` 一直有守卫）；`say_log`/`say_file` 的第一个实参当成了
另一个值。**判据不是「挑一侧当基线」** —— 两份副本都是消费者，契约还不存在
（`docs/DECFY_DESIGN.md:8-12`）。`int("0x10")` 那条曾**已登记、未裁定**：我一度把 base-16
补进 POSIX、随后**已撤回**（那是特性移植，不是消去分歧），登记一轮后**用户裁定两端都答 `16`**，
于是两份副本一起改、pin 恢复断言它 —— 判据的最终来源是人，不是任何一侧的代码。新 pin `vtest/divergent_builtin_contract_v06.im` 注册为 CTest `#125`，**两端都
跑**，补上 `posix_runtime_parity` 在 Windows 被 `DISABLED TRUE` 留下的空洞。用 mingw64
手工全量编出的真 Windows 引擎上：修前 rc=5，修后 rc=0 且输出与 Linux 逐字相同。
详见 [AUDIT.md](AUDIT.md) §1.45。

## §10.77 `round` 非数字实参：两侧各自现行值，已登记并 pin

`round("x", 2)` 在 POSIX 答 `nil`（`src/runtime/runtime_posix.c:104`）、在 Windows 抛
`round: expected number`（`src/runtime/runtime.c:41`）。**两侧都不改**，但把它从「未记录」
变成「已登记、已 pin」—— 先例是 [AUDIT.md](AUDIT.md) §1.25（`sum()` 的非数字元素，保留两侧
风格并记录，未统一）。新 pin `vtest/round_nonnumber_contract_v06.im` 注册为 CTest `#126`
`round_nonnumber_contract_runtime`，`PASS_REGULAR_EXPRESSION` 在 configure 期按平台选，
断言的是**双方各自的现行值**，所以任一侧漂移立刻变红、分歧保持可见 —— 这正是 §1.43 缺的那一环。
状态 **REGISTERED, NOT RESOLVED**（POSIX 的 `nil` 属 [SYNTAX.md](SYNTAX.md) §7.1 D5
【危险·静默】，Windows 的抛是【响的失败】；响的优于静默的，但「更好」不等于「已裁定」）。
详见 [AUDIT.md](AUDIT.md) §1.47。

## §10.78 一个没被写过的字节，让 `a.b` 变成了 `a?.b`

`src/parser/parser.c:570` 的普通 `.` 路径用 `malloc` 分配 AST 节点、只写三个字段，
`member.safe`（`src/parser/ast.h:63`）从未被赋值。字节非 0 时 `src/compiler/compiler.c:1083`
把它当成安全访问，`a.b` 被编成 `OP_INDEX_GET`，**点号全局静默答 `nil`**（例如参数
`player.max_hp`）。症状伪装成「400 字节文件大小阈值」，实际是分配器旧数据。
修法是把 `src/parser/parser.c` 里 95 处 AST 分配统一为 `calloc`（40 个 `Expr`、55 个 `Stmt`、
1 个 `Program`），并新增 `src/parser/parser_member_safe_probe.c` —— 它先向堆灌 `0x01` 再解析，
所以修复前必然报 `5 failure(s)`（rc=1）、修复后 10 项全 `ok`。注册为 CTest
`#127 parser_member_safe_probe`，`tools/gate.sh:50` 的 `EXP_CTEST` **126 → 127**。
详见 [AUDIT.md](AUDIT.md) §1.48。

## §10.79 参数文件的路径是相对谁解析的

`load_and_run()` 打开参数文件时，进程已经在 `chdir_to_script_dir()` 里进了**脚本目录**（`src/main.c:1346` → `:1348 load_and_run`），而 `params_path` 是相对路径，于是 `inimerse --params s.params sub/s.im` 去找 `sub/s.params`、找不到、**每个参数读成 nil 而退出码仍是 0**。修法是在任何 chdir 之前就把它固定成绝对路径；并且 `--params` **命名**的文件读不到时现在报错（默认的 `params.params` 仍可选）。同批删掉 `load_and_run_source()` 里 `8248e08` 带来的裸调试打印。新 pin `#128`/`#129`/`#130`（`--params` 参数**故意写相对路径**），用 `git stash push -- src/main.c` 做了双向验证：修前**三条全部 Failed**，修后 **4/4 Passed**。另一条旁证：`vtest/params_precompiled_v06.inim` 是用**修复前**的 `buildc` 编的，把 §1.48 的缺陷烘进了字节码，重编后才答 `player=42`。详见 [AUDIT.md](AUDIT.md) §1.49。

## §10.80 能力串有两个生产点：同一句 `spi_meta` 在两侧拿到不同的权限

`spi_meta(id, version, caps)` 的能力串此前有**两份解析代码**：POSIX 对整串连做六次 `strstr`，
WIN32 先切到下一个 `,` 再 `strncmp`。两侧都按**子串**匹配能力名，方向相反，实测修前
`"io net"` 768 | 256、`"audio"` 256 | 0、`"ionet"` 768 | 256（唯一一致的是 `vm.h:159` 写下的
`"io,net"`）。已收成单一生产点 `vm_parse_caps`（`src/vm/vm.c`，声明 `src/vm/vm.h`），按整段
逗号分隔 token 精确匹配；不在文档形状里的输入什么都不授（`vm.h:156` 的 minimal-permission）。
新 pin CTest **#131** `spi_caps_contract_runtime`：六个字符串字段一份断言，`bool=` 按平台各断言
各自现行值（**已登记、未裁定**：`true` POSIX 0 / WIN32 65280），任一侧漂移立刻变红。
**双向验证**：`git stash push -- <4 个 src 文件>` 后 Linux 打印 `space=768 audio=256 ionet=768`、
Windows 打印 `space=256 audio=0 ionet=256`，两侧 ctest 均 **Failed**；修后两侧均 Passed
（Windows 侧 `#121`）。同批更正：上一轮「WIN32 `builtin_spi_mods` 用 `vm_array_push` 冒充 dict」
是读码结论，实测 `vm_dict_set` 自己的布局就是交替键值数组、哈希惰性建，两者可观测等价
（12 个字典操作两平台逐字相同）⇒ 登记为未被复现。详见 [AUDIT.md](AUDIT.md) §1.50。

## §10.81 原子槽不是整数时，两侧都去读了 union 里没被写过的那个成员

**发现路径**：上一轮修 `spi_meta` 的能力串时，发现它与 `atomic_*` 是同一个形状（同一个名字两份实现），于是把 `atomic_*` 安排进审计。审计交付两条真分歧，都在这个族里，都是**不查槽的 `type` 标记就读写 union**，都是**退出码 0 只改值**。

**根因**：`Value`（`src/vm/vm.h:24-26`）的 `ival` 与 `fval` 是同一块存储。POSIX 侧完全没有类型门，WIN32 侧把「不是整数」归一化成「它是 0」再加。与 `docs/SYNTAX.md` §7.1 **D13**（`chr` 不查 `type` 就读 `ival`）同病，只是 D13 当时只修了 `chr` 一处。

**实测（POSIX，修前，退出码全 0）**：`atomic_get("y")` 对 `y = 1.5` 答 **4609434218613702656**（IEEE754 位型）；`atomic_add("h", 0)` 对 `h = 2.5` 答 **4612811918334230528**，**加零就把 2.5 毁掉**；`atomic_add("t", 1)` 对 `t = "abcdef"` 把字符串毁成 **2**；整数槽一直正确（`k = 7` → `atomic_add("k", 5)` = 12）。

**修法**：两侧都先问 `type`。不是 `VAL_INT` 的槽**不是计数器**，答 `0` 并不碰那个槽——正是这个族在名字解不开时已经给的答案，所以没有新立约定。`atomic_set` 不动（它是调用方明确要写一个整数进去）。**为什么不报 `type_mismatch`**：与 D13 同一条边界——仓库里没有任何一条「槽类型不对时怎么办」的规范，报错是新立一条规范，需要人批；当前已登记为 **REGISTERED, NOT RESOLVED**。

**双向验证**：新 pin `vtest/atomic_slot_type_contract_v06.im` + CTest **#132** `atomic_slot_type_contract_runtime`（`PASS_REGULAR_EXPRESSION` 钉整行，两平台**同一行**）。`git stash push -- src/runtime/runtime_posix.c` 重编 ⇒ 打出 `atomic-slot-ok g1=4609434218613702656 r1=4609434218613702657 y=4609434218613702657 r3=4609434218613702657 yz=4609434218613702657 g2=1 r2=2 s=2 g3=7 r4=12`、退出码 0；恢复 ⇒ `atomic-slot-ok g1=0 r1=0 y=1.5 r3=0 yz=1.5 g2=0 r2=0 s=abcdef g3=7 r4=12`。Windows 侧（ucrt64，CI 同款工具链；读数在 `ab70a71` 的导出树上取，`c71ea00` 与它逐字相同 —— **两个 sha 都要写**；ctest 汇总分母 **122**，**不是 `Total Tests:`** —— 该平台注册数是 **131**，`122 = 131 − 9` 条 `(Disabled)`，见 §10.81）同形 A/B：把新守卫换回 `type = VAL_INT; ival = 0;` 后重编 ⇒ 该测试打出 `atomic-slot-ok g1=0 r1=1 y=1 r3=1 yz=1 g2=0 r2=1 s=1`、**Failed**（`PREFIX_CTEST_RC=8`）；换回 ⇒ Passed。这一行把「修前 WIN32」从读码结论升为实测。计数 **131 → 132**（`tools/gate.sh` 的 `EXP_CTEST`）。

**诚实边界**：WIN32 侧的「修前」值已由 ucrt64 上的 A/B 实测确认（打出 `g1=0 r1=1 y=1 r3=1 yz=1 g2=0 r2=1 s=1`、Failed）；只核了这三个名字。


## §10.82 一个内建名字有两个生产点，而只有一个被注册

`random(10)` 在 Windows 上答 **27606**、`random(0)` 答**同一个 27606**（实参根本没被读），
退出码 **0**；POSIX 上同一程序答 `3` / `0`。两个生产点（**修前行号**，两处都已改动）：`src/runtime/runtime.c:16` 的
`builtin_random`（`rand() % max`，**从未被注册**）与 `src/mod/io_mod.c:143` 的
`builtin_random`（裸 `rand()`，忽略实参，**被注册**）。`src/mod/io_mod.c` 只在 Windows 上编译，
所以分歧只在 Windows 出现。修法：给 runtime 侧补上注册与 `max > 0` 门，删掉 io_mod 的副本
（三处编辑），于是**只剩一个生产点**。新 pin `vtest/random_bounded_contract_v06.im` /
CTest **#133**，`PASS_REGULAR_EXPRESSION` 钉整行且两平台**同一行**（`posix_random` 本来就有
上界与 `n > 0` 门，没有新立规则）。真 ucrt64 引擎双向验证：退回 `HEAD` ⇒
`random(10)` = `27606 9428 30941`、`random(0)` = `27606 9428`，退出码 0；修后 ⇒ `1 7 9` / `0 0`。
计数 132 → **133**。

同批**只登记、不动代码**的三处同形实例（`gui_fullscreen` 重名且一条不可达、`rand` 有文档
有示例但**POSIX 侧零注册、Windows 侧有**（见 [builtin-platform-census.md](streams/builtin-platform-census.md) §4）、`docs/SYNTAX.md` §5（写下时 `:500`）的「有 `vtest` 覆盖」对 `random` 不成立），以及
`docs/API.md:234` **早就记下 `gui_fullscreen` 重复却只当成计数问题**这一点，
见 [AUDIT.md](AUDIT.md) §1.53。`docs/SYNTAX.md` §5（写下时 `:500`）已就地更正（`rand` 移出名单）。

## §10.83 数组池唯一的门不能拒绝一个下标，所以它后面八个 `if (!a)` 都是死代码

`vm_pool_slot`（`src/vm/vm.c:743`）是全 `src/` **71 个调用点**取数组/字典槽的唯一入口，而它**不可能返回 NULL** —— 两个分支都返回地址。负下标答 `&arrays_big[idx - 4096]`（新 VM 上 `arrays_big == NULL` ⇒ 野低地址 `0xffffffffffeefef0`，Linux 与 Windows **逐字节相同**；池长大后 ⇒ 真的堆地址），超出 `bigCap` 的下标答分配之外的槽，只有 `idx == 4096` 且 `bigCap == 0` 时**偶然**是 `NULL`。`src/mod/verse_dist_mod.c` 的八处 `ArrayObj *a = vm_pool_slot(vm, pkg.ival - 1); if (!a) return 0;` 因此全是死代码。

同一个文件里 GC 标记 `src/vm/vm.c:2818` 对**同一个句柄**用的是 `ival > 0 && ival - 1 < vm->arrayCount` —— **检查存在于一个地方，却不在所有人都要过的那道门上**。修法是把边界放回那道门（`vm_array_new` 在交出槽之前一定先扩好 `bigCap`，所以池真正拥有的下标不会被拒）。

新 pin `src/vm/vm_pool_slot_probe.c` / CTest **#134 `vm_pool_slot_probe`**（**无 PASS 正则，退出码即判据**；同时断言 `slot(0)`/`slot(4095)` 仍解析，防「一律返回 NULL」蒙混）。双向验证：Linux `git stash push -- src/vm/vm.c` ⇒ `***Failed` + 5 条 FAIL，恢复 ⇒ `Passed`；Windows（ucrt64）换回旧函数体 ⇒ `FAIL slot(-1) answered FFFFFFFFFFEEFEF0` 等 5 条、`PREFIX_PROBE_RC=1`，换回 ⇒ 四个 `(nil)`、`Passed`。计数 133 → 134。

同批：初版探针把局部变量写成 `far`/`at`，mingw 的系统头把 `far` 定义成**空宏** ⇒ **Windows 编不过而 Linux 编得过**；已改名 `deep`/`edge`，双工具链 0 error。详见 [AUDIT.md](AUDIT.md) §1.54。

## §10.84 受版本控制的文本文件里的 NUL 字节让 `grep` 静默截断

三个 C 源文件在块注释里带着 NUL 字节（`src/mod/gui_mod.c` 5 个、`src/lexer/lexer.c` 2 个、
`src/lexer/lexer.h` 1 个）。编译器不在意；`grep` 在意 —— 它把文件判成二进制，**只列出 NUL
之前找到的匹配**，把 `binary file matches` 打到 stderr，**退出码 0**。被吞掉的正是证明
`gui_fullscreen` 注册两次（`src/mod/gui_mod.c:3690`／`:3697`）的那两行。8 个字节已换成空格
（字节数不变），`grep -n` 修后直接给出三行。新增门禁第 **11** 阶段 `text-integrity`
（`tools/check_text_integrity.py`，`git ls-files` + 扩展名白名单，实测 **770 个文本文件 / 0 个
含 NUL**；**数量不作断言**，它随每个新增文本文件变动，断言的是「0 个含 NUL」；
反向验证塞回一个 NUL ⇒ exit 1）。机制、登记表与五条诚实边界见
[AUDIT.md](AUDIT.md) §1.55。

## §10.85 关于实参类型的第一条通用规则：D14 落地

**裁定**（2026-10，人类，见 [`docs/SYNTAX.md`](SYNTAX.md) §7.1 D14）：内建函数收到无法解释为
所要求类型的实参时，按**这次调用是否操作共享状态**分两支 —— 操作共享状态（原子槽
`atomic_get`／`atomic_set`／`atomic_add`）⇒ 抛 `type_mismatch`，**不得静默返回**；只产出值
（`chr`／`int`／`round`）⇒ 按各自已裁定的定义值作答。**运行期状态**的类型不符（槽里存的不是
`VAL_INT`）**明确排除**，仍答 `0` 且不得改动那份状态。

**为什么值得改**：这不是「少一个守卫」，而是 POSIX 侧**两个决定点被折成了一个返回值** ——
`posix_atomic_find` 用同一个 `-1` 回答「你给的实参不是字符串」（调用方错误，这次调用从来不可能
成功）与「这个名字不存在」（运行期状态，那里本来就没有东西）。调用方拿回 `-1` 后分不开，只能
给一个答案，而这个答案对第一种情形是**静默失败**。与 §10.83 的数组池是同一种病的两种形态。

**改动六处**：Windows（`src/runtime/runtime.c`）三处 `if (!nm) { push_int(vm, 0); return 1; }`
改为 `vm_throw_kind(vm, "type_mismatch")`；POSIX（`src/runtime/runtime_posix.c`）三处在**调用
`posix_atomic_find` 之前**加类型守卫（放后面就晚了）。`chr`／`int`／`round` 一个字未动。

**pin**：`vtest/atomic_slot_type_contract_v06.im` 新增三段 `try/catch`，**断言错误种类串**
（末行 `… r4=12 n1=type_mismatch n2=type_mismatch n3=type_mismatch n4=0`）。`n4` 是「名字不存在」
那一支，**必须仍是 `0`** —— 两条不能同时被区分出来就等于没落地。`CMakeLists.txt` 的 PASS 正则
已扩并新增 `FAIL_REGULAR_EXPRESSION "n1=NO-THROW|n2=NO-THROW|n3=NO-THROW|n4=type_mismatch"`，
**双向验过**（修前命中、修后不命中）。

**A/B（Linux）**：`git stash push -- src/runtime/runtime_posix.c` 重编 ⇒ fixture 打出
`n1=NO-THROW n2=NO-THROW n3=NO-THROW n4=0` 且**退出码仍是 0** —— 一个只看退出码的 pin 会**绿着**
放它过去。恢复后打出期望行。

**同批更正的两处过期声明**：① [`docs/SYNTAX.md`](SYNTAX.md) §7.4 H3 原写「全仓**唯一一个**连
UTF-8 都不是的文本文件」—— 实测是**两个**（`ai_browser_diag.js` 偏移 478 与
`examples/legacy-ui/desktop.html` 偏移 3248），第二个是本轮新发现；② §7.1 D13 的两段散文原写
「设计记录里没有任何『参数类型不对时怎么办』的规定」「需要先有一条规范」—— 这条规范现在存在了，
`chr` 答 `""` 不变，但「没有规定」不再成立。另给 §7.2 M13 加了「分母已过期」标注（标题里的 101
是写下时的快照，今天 `ctest -N` 报 134；分子 66 仍是「101 里 66」，**不改成新数以免再次过期**）。

**诚实边界**：① 这是关于参数类型的第一条通用规则，而「操作共享状态 ⇒ 抛／只产出值 ⇒ 答定义值」
这个切法**本身还没被正式确认**，人类明确要求只落窄条款 ⇒ 本节结论**不能**外推成「引擎开始校验
参数类型了」；② 运行期状态仍答 `0`，是排除项不是遗漏，`atomic_set` 的槽类型归一化没动；
③ **没有静态证明**，判据是 134/134 与 fuzz 阶段全绿；④ **Windows 侧只做了编译与成对改动的等价性
核对，本轮没有在真 Windows 上跑 `atomic_get(42)` 的 A/B**。

**计数不变**：本轮**没有新增 CTest**，`EXP_CTEST` 保持 **134**（只扩了 `#132` 的正则并加了一条
FAIL 正则）。机制、六处表与完整边界见 [AUDIT.md](AUDIT.md) §1.56。

## §10.86 `--only` 打错阶段名：跑了个空，却打印绿灯

`tools/gate.sh --only definitely-not-a-stage` 曾把**每一个**阶段记为 SKIP，然后照旧打印
`gate: OK — every stage passed.` 并**退出 0** —— 收尾句与一次全绿的门禁逐字相同。这是仓库已有的
「一个 skip 不是一次 pass」规则的下一层：**跳过全部**反而是绿的；CI 里阶段名写错会静默什么都不跑
且绿。

修法：`run_stage` 把每个选择器记进 `STAGE_WANTED`；阶段注册完后校验 `--only`，无匹配 ⇒ exit 2
并列出**从 `STAGE_WANTED` 派生**的合法值（不是第二份手写清单）；收尾句在部分运行上不再读起来像
整场门禁（`--only links` ⇒ 自报「this was NOT the full gate: 11 stages are registered and only
this one ran.」）。

实测：打错名字 ⇒ exit **2** + 合法值列表；`--only links` ⇒ 1 个阶段 + 自报部分运行。

与 §10.83（数组池的门不能拒绝）、§10.85（两个问题折成一个返回值）登记在一起：同一个缺陷类在
**门**、**返回值**、**总结**三个层面上的样子。发现来自 `ivory-ember`（它在自己的
`stream/ci-gate-static` 分支上也修了同一处），我独立复现。完整边界见 [AUDIT.md](AUDIT.md) §1.57。

## §10.89 一个内建名字有两个注册点，而注册表自己不会说

`builtin_insert`（`src/vm/vm.c:1667`）线性探测**取第一个空槽，从不检查重名**；
`builtin_lookup`（`:1657`）**返回探测链上第一个名字匹配的槽**。
⇒ 同名注册两次时，**先注册的永远胜出，后注册的不可达**，而 `builtinCount` 照样把它算进去 ——
「表里有几个内建」与「能调用几个内建」是两个数，**没有任何东西比较过它们**。

**普查**：`src/` 全部注册点 **588** 个、唯一名字 **462** 个；同文件内重名**只有两处** ——
`src/isolate_mod.c` 的 `isolate_run`（`#ifdef` 分叉，**误报**）与
`src/mod/gui_mod.c` 的 `gui_fullscreen`（同一个函数里相隔 7 行，**真重名**，即 §10.82/§1.53 登记的那个）。

**修法**：两个注册函数各加守卫（`src/vm/vm.c:1684`／`:1708`）——
`builtin_lookup(vm, name) >= 0` 时**拒绝注册**并打一行
`[vm] builtin '<name>' is already registered; the first one stays`。
**不改变任何分派行为**（第二个本来就不可达），只是**不再保持沉默**。
**A/B**：把 `src/runtime/runtime_posix.c:1117` 的 `random` 故意复制成两行 ⇒ 该行出现；恢复后消失。
**Linux 零假阳性**（POSIX 源列表里 0 个重名），守卫在这里是无操作。

**闸门断言**：`tools/gate.sh:146-152` —— ctest 输出里出现 `is already registered` ⇒ 阶段红。
放在这里是因为重名发生在 VM 初始化期，**没有任何单个 `.im` 测试看得见它**。

**§1.53 那个实例**：删掉的是**不可达的那一行注册**，**没有删掉另一个实现体** ——
`builtin_fullscreen`（`src/mod/gui_mod.c:1682`）保留、编译、由 `(void)builtin_fullscreen;` 引用，
旁边写明**两个体不一样**（保留的要求实参、用 `SetWindowLongA`；不注册的缺省切换、用 `SetWindowLongPtr`），
**「该注册哪一个」不在这里决定**，§1.53 记着它是人的决定。⇒ **行为逐位不变，选择仍只差一行。**

**诚实边界**：①`src/mod/gui_mod.c` **只在 Windows 上编译**（`CMakeLists.txt:431`），
本机无法验证它编译得过；②守卫在调用方、不在表里，直接调 `builtin_insert` 会绕过它；
③`builtin_insert` 本身仍然不检查重名；④两个体行为不同这件事没实测过（需要窗口）。
完整边界见 [AUDIT.md](AUDIT.md) §1.58。

## §10.90 一份表格声称有 CTest 覆盖，而那两个 fixture 从来没有注册过（BOARD 行 267）

**症状。** `docs/API.md` 第 2.1 节的证据列标题是「证据（CTest）」，而 `:114` 把 `lint_case_missing_default_v04.im` 写成「相关 CTest」，`:115` 把 `lint_case_exhaustive_v04.im` 与四个真实测试名并列。**两处都不成立**：全仓库对这两个文件名的引用**只有那两行文档**，`ctest -N` 里没有它们，也没有任何含 `missing_default` 的名字。同族的第三、四处：`docs/REQUIREMENTS_ANALYSIS.md:177` 的「`migrate_report.py` + 对应 CTest」与 `docs/STATUS.md:286` 把该工具与 `bindgen_regression`／`scan_tools_regression` 并列 —— `tools/` 下没有 `migrate_report.test.py`，那两个 CTest 分别跑 `bindgen.test.py` 与 `scan_tools.test.py`（后者只碰 `cpp_scan`／`python_scan`）。

**能力是真的，覆盖不是。** 两个 fixture 今天都跑得对（`--lint` 各出一条 `[WARN]`、rc=1），`migrate_report.py` 也跑得对（`--help` rc=0；对 `tools/cpp_scan.py` 出真报告、rc=0）。所以不是「文档描述了不存在的东西」，而是**「文档描述了一个从未接上的东西」**。

**为什么没人发现。** `CMakeLists.txt:733-743` 的五个 `lint_case_*` 是**手写列举**的（`:744` 的注释自己数着「The five lint_case_* tests above」），第六、第七个 fixture 落地时没有任何东西要求把它们加进去；仓库里**没有任何一处比较过「`vtest/` 里有什么」与「CTest 跑什么」**。

**普查。** 67 个 `vtest/*.im` 中 5 个一次都没被 `CMakeLists.txt` 提到：两个是**真缺口**（就是上面那两个），三个合法 —— `params_precompiled_v06.im` 是 `params_precompiled_v06.inim` 的源（`params_precompiled_runtime` 跑的是产物）、`eidos_object_probe_v04.im` 是 `docs/API.md:201` 的样例（功能由 `tools/eidos_runtime.test.py` 的内联脚本断言）、`say_pair_probe_v06.im` 是一次性探针（输出记在 `docs/AUDIT.md:1807`）。`tools/*.test.py` 30 个、`tools/*.test.js` 13 个**今天都是 0 孤儿**。

**修法两半。** ①注册那两条 CTest（**#135** `lint_case_missing_default_runtime`、**#136** `lint_case_exhaustive_runtime`，注册在文件末尾以免既有 `#N` 位移），各带 `PASS_REGULAR_EXPRESSION` **和一条断言对方那条警告不出现的 `FAIL_REGULAR_EXPRESSION`** —— 两个 fixture 只差一行、走同一个 `--lint` 通道，只断言自己的发现分不开「因正确的理由触发」与「对每个 case 都触发」；两个方向实测都成立。②新增 `tools/check_orphan_fixtures.py` 与门禁第 **12** 阶段 `orphan-fixtures`：比较「测试输入集合」与「真正会跑的集合」，不在 `CMakeLists.txt` 里的必须进脚本的 `ALLOWED`，**且每条要写出「那跑的是什么」**（只列文件名的白名单是同一个缺陷上升一层）。

```
check_orphan_fixtures: 110 input(s) checked (67 vtest fixtures, 30 python harnesses, 13 node harnesses); 64 fixtures registered, 3 allowed with a stated reason.
```

**A/B。** 把 `CMakeLists.txt` 退回 `HEAD` ⇒ `2 orphaned input(s) out of 110 checked`、**rc=1**，点名的正好是文档声称有覆盖的那两个；恢复 ⇒ rc=0。**A/B 还抓出脚本自己的缺陷**：第一次跑它 `NameError: name 'CMAKE_SOURCE_DIR' is not defined` —— f-string 把 `${CMAKE_SOURCE_DIR}` 的花括号当成了替换字段，于是**一个「能发现孤儿」的检查在真发现孤儿时抛异常而不是报告**。已改成 `${{CMAKE_SOURCE_DIR}}`。**没有这次 A/B 就不会有人发现。**

**同批更正四处文档声称**：`docs/API.md:114` 改成点名真测试 `lint_case_missing_default_runtime` ← 那个 fixture；`:115` 把文件名换成 `lint_case_exhaustive_runtime` ← 那个 fixture；`docs/REQUIREMENTS_ANALYSIS.md:177` 逐条写明哪个工具由哪条 CTest 覆盖、并写明 **`migrate_report.py` 无任何 CTest**，行末判定 **已完成 → 部分**；`docs/STATUS.md:286` 同。**`migrate_report.py` 的测试本轮不写**（`tools/*.test.py` 是对等方的改动域），它是本阶段唯一「已知且被记录」的覆盖缺口。**后来补上了** —— 见 §10.94。

**计数 134 → 136**，四处同步（`tools/gate.sh` 的 `EXP_CTEST`、`docs/BOARD.md` §3、`docs/STATUS.md` §2/§2.1）。**与 `main` 合并后为 137**（`main` 的 `desugar_runtime`），**本轮再加 `migrate_report_runtime` 后为 138** —— 见 §10.94。**诚实边界**见 [AUDIT.md](AUDIT.md) §1.59：子串测试不是解析；本阶段不判断被注册的测试是否断言了任何东西；只查 `vtest/*.im`；`migrate_report.py` 那类「既非 fixture 又无 `X.test.py`」的工具落在两组之外。

## §10.91 两处「声称了但没人验」：模糊测试的分母，和三条指向文件的证据

**症状（流程缺陷，不是引擎缺陷）。** 0.5.1 开在 0.5.0 发布之后，起因是两类同一形状的问题：

1. `tools/gate.sh` 的 fuzz 阶段只跑一个种子（`EXP_FUZZ_SEED=1`），`sed` 只抽出 `DIVERGE` / `THREW` / `not translated` 三个计数，`-z` 兜底只覆盖「计数读不出来」。于是「120 个程序跑完且全部一致」与「一个程序都没跑」打印出**同一个绿**。
2. 三处文档把**文件名**或**实现名**写在「证据（CTest）」列里：`docs/API.md` §2.1 的两行 case 覆盖写的是 `lint_case_missing_default_v04.im`、`lint_case_exhaustive_v04.im` 两个 vtest 文件（没有任何 CTest 引用它们），`docs/REQUIREMENTS_ANALYSIS.md` 第 11/12 行与 `docs/STATUS.md` 的绑定工具行则把 `tools/*.py` 的实现名当证据，其中 `tools/migrate_report.py` 没有任何 CTest 或测试脚本引用。

**共同形状：断言了一个没有分母的结论。** fuzz 的 pin 只说「分歧数为 0」，没说「跑过几个程序」；证据列只说「有这么个文件」，没说「哪个用例会因此变红」。这和第 24/123 那次发布门禁、和「Windows 需重新 configure」是同一类 —— 决定点（跑没跑 / 谁在验）不在被断言的位置上。

**改法（fuzz）。** `EXP_FUZZ_SEED=1` 变成 `EXP_FUZZ_SEEDS="${EXP_FUZZ_SEEDS:-1 2 3}"`，`stage_fuzz` 逐种子解析 `seed N, M programs` 与 `agreed`：

- `M != EXP_FUZZ_COUNT` ⇒ 红，打印「一个空跑或缩水的运行不许当成『没有发现』」；
- `agreed + DIVERGE + THREW + not translated != M` ⇒ 红（工具内部的分桶和必须闭合）；
- 累计种子数为 0 ⇒ 红（`EXP_FUZZ_SEEDS` 为空时什么都没验证）；
- pin 与 `not translated` 的判定改在**累计总数**上做，失败信息里带上「共几个种子 × 几个程序」。

**改法（证据）。** 三个此前零引用的产物各自接进一个点名用例，且每个用例带一条指认**另一个**诊断的 `FAIL_REGULAR_EXPRESSION`（两个诊断写反了会被抓，而不是两边都算过）：

| 用例 | 断言 | 原先写在证据列里的东西 |
| --- | --- | --- |
| `lint_case_missing_default_runtime` | `case has no wildcard '_'/'else' branch` | `lint_case_missing_default_v04.im` 相关 CTest |
| `lint_case_exhaustive_runtime` | `case branch is unreachable: wildcard '_'/'else' appears before this branch` | 把 vtest 文件名混进测试名列表 |
| `desugar_runtime` | `desugar tests: ok`（外加负对照：同一份源码不加 `--desugar` 必须被拒） | `tools/desugar_probe.sh`（零引用 ⇒ 从未运行） |

`tools/desugar_probe.sh` 已删除，改为 `tools/desugar.test.py`：同一个探针（`--desugar` 产出的三行改写逐条断言），但能被 CTest 驱动，且用 Python 写因此在 Windows 上也能跑。负对照是这段探针的价值所在 —— 它钉住的不只是「脱糖能跑」，还有「`say@target` 确实只有脱糖这一条路」（`docs/API.md` §3.3 的结论）。

**计数同步。** 本节所属提交把 `grep -c 'add_test('` 从 123 抬到 **126**，`tools/gate.sh:50` 的 `EXP_CTEST` 同步为 126；与 `stream/builtin-contract-rulings` 合并后为 **137**（`EXP_CTEST` 同步 137），`docs/BOARD.md` §3、本节 §2/§2.1 同步。三个新用例注册在 `CMakeLists.txt` **末尾**，`#123 resolve_timeout_runtime` 不移位（编号规则见 §10.67）。

**正向对照。** `bash tools/gate.sh --fast --only fuzz` ⇒ 3 个种子 × 120 个程序 = 360，`0 DIVERGE / 0 THREW / 0 untranslated`，`gate: fuzz findings match the pin over 3 seed(s) x 120 programs (1 2 3)`，阶段 PASS。

**诚实边界。** ① 分母断言保证的是「跑满了这么多个程序」，**不保证种子集选得好** —— 三个种子只是把「种子 1 恰好干净」的疑虑压小，不是覆盖率证明；② 证据列改成点名用例只让「谁在验」可查，**不改变那三行的实现状态**（`--lint` 的 case 覆盖仍是「部分实现」，`tools/migrate_report.py` 仍然没有测试）；③ 每条种子的程序数与 `not translated` 为 0 的约定不变（生成器造出后端翻不动的东西仍是生成器的 bug）。

## §10.92 第三处同形状：插件验证跑的是**另一个** checkout

**症状（流程缺陷，不是引擎缺陷）。** `tools/dsh-inimerse/verify.mjs` 从 `cordis.patch.yml`（**已提交**，`repoRoot: /home/sakiko/inimerse`）里读出绝对路径，再把它交给 `mod.apply`。于是 `tools/gate.sh` 的 plugin 阶段在**任何 worktree 或新克隆**里跑，验的都是**那个路径上的仓库**，而不是被测的那棵树 —— 0.5.1 的 worktree `/tmp/inim-051` 里，plugin 阶段是对 `/home/sakiko/inimerse` 的引擎打印 55/55 的，`/tmp/inim-051/build/inimerse` 一次都没被碰过。证据是实测：把 `/tmp/inim-051/build/inimerse` 改名藏起来，阶段**照样 55/55**。

**同一个缺陷的副产物：崩溃，而不是报错。** 那个「别人的引擎」在验证瞬间不存在时（当时另一会话正在主 worktree 里重链接它），`inim_run` 返回 `{ok:false, code:'engine_missing'}`（这种早返回**没有** `stdout`），而 `verify.mjs:130` 直接读 `inline.stdout.includes(...)` ⇒ `TypeError: Cannot read properties of undefined`，整个验证器带着一个堆栈退出，**一个命名的失败都没留下**。本轮 0.5.1 的门禁运行里出现的就是这一行。

**共同形状。** 与 §10.91 的两处同类：**决定点（「在验哪棵树」）不在被断言的位置上**。断言说「这个桥能用」，而「这个」指的是配置里的路径，不是被测的树。

**改法。** `verify.mjs` 改为锚在**它自己所处的 checkout**（`CHECKOUT = resolve(HERE, '..', '..')`）；`cordis.patch.yml` 仍被读取，但只用于在两者不一致时打印一行 `note:`（该配置是给**装进 DSH 的插件**用的，不是被测树的属性）。同时 `inline.stdout` 改为防御性读取：早返回时不再崩，而是让已有的两条断言**带工具的完整 JSON 失败**（里面就有它找过的确切路径）。

**验证（两个方向）。** ① 引擎在：`note: cordis.patch.yml names /home/sakiko/inimerse, but this verifier lives in /tmp/inim-051; verifying the checkout under test.` + `55/55 checks passed (live)`，rc=0；② 把 `build/inimerse` 改名藏起来：4 条命名失败（`inim_run: executed inline source — {"ok":false,"code":"engine_missing","error":"engine not found at /tmp/inim-051/build/inimerse; run inim_build first"}` 等）+ `51/55`，rc=1，**没有 TypeError**。失败信息里的路径正是**被测树**的路径，这一点本身就是锚定生效的证据。检查条数不变（仍 55），这一改修的是「报告方式」，不是「检查内容」。

**诚实边界。** ① 那行 `note:` **不是断言**：worktree 里配置指向别处**仍然是绿的** —— 那份配置是装机用的，不是被测树的属性，为它判红是错的；② **没有任何 CI 工作流引用 `tools/gate.sh`**（`.github/workflows/*.yml` 零命中），所以这个阶段的证据只存在于本机；③ 该阶段仍会在 checkout 内写 `verify-root`（结束时删除）。

## §10.93 第四处同形状：同一个修复贴着两份拷贝，只修了一份

**症状。** `29402f8` 把 `.github/workflows/release.yml:45` 的抽取模式改成 `Test[[:space:]]*#`，并在 `:52-54` 加了一条 `collected != Total Tests` 的断言。同一段文本在 `.github/workflows/linux-build.yml:45` **原样留着旧模式** `Test #[0-9][0-9]*`，而且**没有那条断言**。实测本树：旧模式从 137 个测试名里只收集 **38** 个（三位数编号 `#100`–`#137`），新模式 **137** 个。`linux-build.yml` 那个 job 于是**跑 38 个、静默跳过 99 个**，照样报成功、照样打包 —— 与 0.5.0 发布时「24 of 123」、`--lint $FILES` 空 `$FILES`、fuzz 空跑是同一个形状：**断言了一个没有分母的结论**。

**机制。** `ctest -N` 把编号**右对齐**，所以 `Test  #64:` 与 `Test #100:` 的空格数不同。旧模式**只在 `Total Tests <= 99` 时成立**，它在**第 100 个测试被加进来的那一刻静默降级**，而仓库里没有任何东西记录过这个阈值。⇒ 任何依赖 `ctest -N` 输出格式的解析都会在某个位数处失效，且失效点不会被写在它旁边。

**为什么「修一份、留一份」比「两份都错」更难发现。** 两份拷贝都绿、门禁也绿（门禁不读 workflow），只有 `38` 这一个数在说话 —— 而那个数**没有被打印过**。

**改法。** 抽成 `tools/ctest_enumerate.sh <build-dir>`：它同时拥有**模式**与**断言**（`ctest -N` 失败 ⇒ exit 1；收集数为 0 ⇒ exit 1；`collected != Total Tests` ⇒ exit 1），把测试名逐行打到 stdout。两个 workflow 都改成调用它，先落盘再 `mapfile`：

```bash
if ! bash tools/ctest_enumerate.sh build > build/ctest-names.txt; then
  echo "::error title=test extraction::ctest -N enumeration failed"
  exit 1
fi
mapfile -t TESTS < build/ctest-names.txt
```

落盘是必需的：`mapfile -t TESTS < <(…)` 的进程替换**吞掉退出码**，断言就会白写。

**验证（A/B）。** ① 用 `sed` 把脚本里的模式退回旧形状，跑同一棵树：`collected 38 of 137 registered tests from build`，**rc=1**；② 脚本原样：137 个名字，**rc=0**。

**诚实边界。** ① 这条断言证明的是「抽取与 `ctest -N` 一致」，**不是**「跑的是对的那些测试」；② 两个 workflow 的真实效果只能由一次真实 CI 运行证明，本机证据只是脚本自身的 A/B；③ `linux-build.yml` **仍然没有 `***Skipped` 断言**（门禁有、CI 没有）—— 这一条**只记录、未改**，因为在 CI 上 xlang 桥按设计 exit 77 跳过，为它判红需要先确认 CI 上到底该不该有跳过，而那是另一次判断。

## §10.94 第四处「声称了但没人验」补上了：`tools/migrate_report.py`

**症状。** §10.90 记的三处文档声称之外，第四处是 `tools/migrate_report.py`：`docs/REQUIREMENTS_ANALYSIS.md:177`
第 11 行与 `docs/STATUS.md:286` 把它与 `bindgen_regression`／`scan_tools_regression` 并列当「证据（CTest）」，
而 `tools/` 下**没有** `migrate_report.test.py`，那两个 CTest 跑的是 `bindgen.test.py` 与 `scan_tools.test.py`、
**都不碰它**。§10.90 当时把它记成「本阶段唯一已知且被记录的覆盖缺口」，理由是 `tools/*.test.py` 属于对等方的
改动域。**人的裁定是补上它**，本轮照办。

**修法。** 新增 `tools/migrate_report.test.py` 与 CTest `migrate_report_runtime`
（`CMakeLists.txt` 末尾、`desugar_runtime` 之后；`PASS_REGULAR_EXPRESSION "migrate_report tests: ok"`）。
它断言的是**分母**，不是形状：

- 两个 fixture 覆盖 `PYTHON_RULES` / `C_RULES` 里的**每一条规则**（C：`goto`/`setjmp`/`alloca`/`threads`/`func-ptr`；
  Python：`dynamic-attr`/`metaclass`/`yield`/`async`/`varargs`/`decorator`/`global-stmt`/`exec-eval`/`lambda`）；
- 表头的 `Scanned:` / `Manual adaptation points:` / `Dependencies:` **等于表体实际行数** —— 报表打
  `Manual adaptation points: 0` 而下面是一张满表，正是我们这一轮治的形状；
- **负对照**：干净输入必须报 `0` 且表体为空 —— 没有它，一个「把什么都报成发现」的工具会通过上面两条；
- 找不到源文件必须**非零退出并点名原因**（`error: no source files found`），而不是打一份看起来干净的报表。

**A/B（两个方向都实测有牙）。** 删掉 `PYTHON_RULES` 里的 `("lambda", …)` 一行 ⇒
`FAIL: rule 'lambda' did not fire on the fixture built for it`、rc=1；把
`f"- Manual adaptation points: {len(all_findings)}"` 改成常量 `0` ⇒
`FAIL: Manual adaptation points: says 0 but the table has 15 row(s)` + `FAIL: -o output's header count
disagrees with its own table`、rc=1；两次都从 `/tmp` 备份精确恢复 ⇒ `migrate_report tests: ok`、rc=0。

**第三份拷贝（本轮的操作教训，与 §10.93 同族）。** 我在**不知道**远端 `12ff42a` 已经修好 CI 抽取的情况下，
独立写了 `tools/ci_ctest.sh`（抽取 + 计数断言 + `***Skipped` 断言 + 逐测试循环）并把两份工作流改成调用它，
建在一个本地 `git merge origin/main` 之上。收到来信后核对远端，**放弃本地 merge、删掉 `tools/ci_ctest.sh`、
把两份工作流恢复成对方那一版**，只保留真正独有的部分（本节这个 harness）。
判据是**可复核的**：动手之前 `git log --oneline origin/main | head` 就能答出「谁已经在修」——
与 §10.90 的 `orphan-fixtures` 是**同一个动作**（先比较两个集合，而不是事后）。

**计数。** `grep -c 'add_test('` **134 → 136（本分支）→ 137（合并 `main` 后）→ 138**；
`tools/gate.sh:54` 的 `EXP_CTEST`、`docs/BOARD.md` §3、`docs/STATUS.md` §2/§2.1 四处同步。
`check_orphan_fixtures` 的输入数 **110 → 112**（`tools/desugar.test.py` + `tools/migrate_report.test.py`；
`tools/desugar_probe.sh` 被删除）。

**诚实边界。** ①本轮没有在 Windows 上复核任何东西，以上全部是 Linux 实测；②`migrate_report.test.py`
断言的是**报表自身的自洽**（表头 vs 表体、规则是否触发），**不判断**某条规则该不该存在、也不判断迁移建议对不对；
③「在 `CMakeLists.txt` 里被提到」这类子串判据的边界见 §10.90；④`linux-build.yml` 仍然**没有
`***Skipped` 断言**（`ivory-ember` 在 `stream/ci-gate-static` 上加过，未合入）—— 「跳过不是通过」在 CI 上
仍然只有本地 `tools/gate.sh` 守着。

## §10.95 两个 fixture 描述的程序从来没有被解析过；以及计数的第六个点

**缺陷**：`vtest/lint_case_enum_v04.im:2` 与 `vtest/lint_case_membership_v04.im:2` 写的是 `dir be Direction = "N"`。`be` 的初始化分隔符是 **`:`**（`src/parser/parser.c:1383-1391`：`if (match(p, TOK_COLON)) stmt->beStmt.init = parse_expr(p);`），`=` 属于另一个语句 `type X = ...`（`parse_type_stmt`，`src/parser/parser.c:1218-1227`：`consume(p, TOK_EQ, "'='")`）。直接跑 ⇒ `Error: expected 'expression', but got '=' (type 83)`、**rc=1**。

**为什么一直绿**：这两个 fixture 只被 `--lint` 消费，而 **lint 路径不打印解析错误**（实测同一条命令里 `expected '` 出现 0 次，只有 `[lint] line 3 [WARN] …`，rc=1）。CTest 的 `PASS_REGULAR_EXPRESSION` 匹的是那条 warning ⇒ **绿着，而 fixture 描述的程序从来没有被解析过**。分母（「这个 fixture 能不能被解析」）没有任何地方问过。

**普查**：`vtest/*.im` 共 67 个，**2 个**吐解析错误（就是这两个），其余 65 个干净。全仓受跟踪 `.im` 里 `be` 只有 10 处，其中恰好这 2 处用了 `=`。

**修法**：两处 `=` → `:`。直接跑 rc=1 → **rc=0**；`--lint` 输出**逐字不变**（`line 3 [WARN] finite case type 'Direction' is missing members: E, W`；`line 6`/`line 11` 两条 unreachable）⇒ `PASS`/`FAIL` 正则一条都不用改。

**守卫**：新增 `tools/fixture_parse.test.py` 与 CTest **`#139 fixture_parse_runtime`**（注册在 `migrate_report_runtime` 之后，既有 `#N` 不移）。断言「吐解析错误的 fixture 数 == 0」，并**同时打印两个数**（`fixture_parse: 67 fixture(s) scanned, 0 emitted a parse error`）；`MIN_FIXTURES = 50` 防「0 of 0 读作干净」，`ALLOWED` 每条必须写出它是什么的证据（今天为空）。**A/B**：改回 `=` ⇒ `67 fixture(s) scanned, 2 emitted a parse error` + 逐条点名 + rc=1；恢复 ⇒ rc=0。

**计数**：`add_test(` 138 → **139**；`EXP_CTEST`（`tools/gate.sh:54`）同步。

**同节附带修掉的一处**：`docs/BOARD.md:54` 写的是 `137 / 137` 与 `0 tests failed out of 137`，而 `tools/gate.sh` 当时已是 **138** —— 这一行恰恰是**引用 `stage_ctest` 断言文本**的那一行，即「一份声称数量是断言的表格，自己引的数落后一轮」。上一轮我报「四处计数已同步」时把这一处算进去了，**它实际没被改**；本轮连同 139 一起改成**六处**：`tools/gate.sh:54`、`docs/STATUS.md` §2 的 `:38`/`:44`/`:51`/`:60`、`docs/BOARD.md:54`。

**同族第七处，本轮被自己踩到**：新增 `tools/fixture_parse.test.py` 使 `tools/check_orphan_fixtures.py` 的输入数从 **112** 变成 **113**（python harness 32 → 33），而 `docs/BOARD.md:62` 的阶段表逐字引着 `112 input(s) checked (67 vtest fixtures, 32 python harnesses, 13 node harnesses)`。⇒ **加一个 harness 会静默让一份文档里引的数变成假的**，而没有任何东西比较这两者（`vivid-anchor` 在那行末尾留了一句「`tools/*.test.py` 每加一个，第二个数就加一」，那是一句**给人的提醒，不是检查**）。本轮手工改成 113/33；**「阶段表引的期望串必须真的出现在门禁输出里」这条检查留给 0.5.2**。

**诚实边界**：标记表是取自 `src/parser/parser.c` 与 `src/lexer/lexer.c` 的**字面前缀**（新增一种错误消息形状不会被认出）；刻意排除 `Error at line %d: task/thread definitions inside a loop are silently ineffective`（被接受然后被忽略，不是解析失败）；超时算作没解析失败；**只扫 `vtest/*.im`**（仓库根 `*.im`、`selfhost/**`、`examples/**`、`projects/**` 不在范围内，那 8 处 `be` 恰好都在这些位置且今天都是 `:`）；「`--lint` 不打印解析错误」是一条命令的实测，没有读实现去解释。详见 [AUDIT.md](AUDIT.md) §1.61。

## §10.96 两条发版前的修复：`substr` 的钳位溢出与文字量的解析器往返

### ① `substr(s, start, len)`：`start + len` 溢出后守卫恒假 ⇒ 段错误

`start`、`len` 都是 `int`，`start` 已钳进 `[0, sl]`。`len` 接近 `INT_MAX` 时 `start + len` **有符号溢出**变负 ⇒ `if (start + len > sl)` 恒假 ⇒ `len` 保持 ~2^31 ⇒ `malloc(~2 GB)` 后 `memcpy` 越界读约 2 GB。**任何脚本可触发**：`substr("abcdefghij", 5, 2147483647)` ⇒ **rc=139 SIGSEGV**。

**阈值实测**：`start=5` 时 `len=2147483642`（和 = INT_MAX）rc=0 打印 `fghij`，`len=2147483643`（溢出）**rc=139**；`start=0` 永不崩。**崩溃边界恰好是溢出边界** ⇒ 机制证明。

**修法**：`start` 已钳好 ⇒ `sl - start ∈ [0, sl]` 不可能溢出，比较换到减法一侧 —— `if (len > sl - start) len = sl - start;`，**POSIX `src/runtime/runtime_posix.c:476` 与 WIN32 `src/runtime/runtime.c:560` 两处都换**，对不溢出输入逐字相同；另补 **WIN32 缺的 NULL 检查**（POSIX 早有，两份拷贝在 OOM 上原本给出两个答案）。

**A/B**：新增 CTest **`#140 substr_boundary_runtime`**（`vtest/substr_boundary_v06.im`，三个长度跨边界：`2147483642` / `2147483643` / `2147483647`，另钉 `start > sl`、负 `start`、负 `len`、`len=0` 各一行）。修复版 **100% passed**；换回 `start + len > sl` 重建 ⇒ **`0% tests passed, 1 tests failed`** 且手工跑 **rc=139**，恢复后复绿。

**为什么活到今天**：`docs/SYNTAX.md` §5（写下时 `:500`）把它列进「核心高频内建（有 vtest 覆盖的）」，而实际只有**一条 happy path**（`vtest/posix_core_api_v04.im:14` 的 `substr(s, 2, 5)`）；边界一个都没有 —— 分母从来没被问过。

### ② 数值字面量也走全套解析器 ⇒ Windows 上 `socket_probe` 撞 10 s 上限

Windows/ucrt64 上 `socket_probe`（`#20`）`***Timeout 11.31 sec`。逐调用计时显示 `im_socket_init` 恒 1–4 ms，而 `listen` 0.9–3.7 s、`port_available` 最高 1478 ms、`connect` 1542 ms、`port_open` 2116 ms；一轮探针做 4 次解析 ⇒ wall time 151 ms–8564 ms，长尾撞上 ctest 默认 10 s。

`getaddrinfo()` 接受 `"127.0.0.1"` 并原样返回，但**穿过完整解析路径**才做到。

**修法**：`src/platform/socket.c` 新增 `addr_from_literal()`（`inet_pton` / WIN32 `InetPtonA`，经 `IM_INET_PTON` 择一；先 `AF_INET` 再 `AF_INET6`）。**两个入口都问同一个助手**：`resolve_addr`（`im_socket_listen`）与 `resolve_addr_timed`（`im_socket_connect_timeout`，且在线程派生之前）—— 否则快路径会变成 `listen` 一条规则、`connect` 另一条。空主机/主机名/`AI_PASSIVE` 全部回落 `getaddrinfo`，行为不变。

**A/B（不吃时间的判定）**：新增 LD_PRELOAD 垫片 `src/platform/getaddrinfo_log_preload.c` + 探针 `src/platform/literal_resolve_probe.c`，CTest **`#141 literal_resolve_runtime`**，断言 **`getaddrinfo` 调用计数**而非毫秒。修复版 `literal-resolve literal=0 name=1`（文字量 0 次、`localhost` 1 次）；把 `addr_from_literal` 改成恒 -1 重建 ⇒ `***Failed Required regular expression not found` + `a literal host reached getaddrinfo 1 time(s); the fast path is gone`。探针**第二半**（主机名必须到达 `getaddrinfo`）不是装饰：「文字量不进解析器」被「干脆永不解析」同样满足，而那严重得多。

**计数**：`add_test(` **139 → 141**，六处同步（`tools/gate.sh:54` 的 `EXP_CTEST`、`docs/STATUS.md` §2 四处、`docs/BOARD.md` §3 两处）。`check_orphan_fixtures` 输入数 **113 → 114**（新增一个 `vtest` fixture，67 → 68；`docs/BOARD.md:62` 的期望串同步）。

**诚实边界**：①WIN32 的两处改动**本机编不到**（`src/runtime/runtime.c` 只在 `if(WIN32)` 分支；`InetPtonA` 只在 Windows 头里），**读码判断，未实测**，由 peer 在 ucrt64 复核；②本机 Linux 上 ②的改前/改后逐调用数字**无法区分**（都被 1–6 ms 淹没）—— 收益是 Windows 特有的，`#141` 钉的是**机制**；③`#141` 是 **POSIX-only**，**Windows 侧对这条仍无覆盖**；④修的是溢出与多余解析，**不是**「超大 `len` 应当被拒绝」那种语义决定。

## §10.97 POSIX 的 `match` 把一个超长模式截断后再运行，于是给出了一个形状正确的错答案

**症状**：`match` 的模式超过约 2031 字节时，**模式被悄悄切掉尾部，而被切过的模式就是实际运行的那个**。

**测量**（引擎本体实测，非推断）：

| 模式 | 长度 | `match` 返回 | 应当 |
| --- | --- | --- | --- |
| `"^" + 2028 个 a + "$zzz"` | 2033 | `false` | `false` ✅ |
| `"^" + 2030 个 a + "$zzz"` | 2035 | **`true`** | `false` ❌ |
| `"^" + 3000 个 a + "$zzz"` | 3005 | **`true`** | `false` ❌ |
| `"^" + 50 个 a + "$zzz"`（对照） | 55 | `false` | `false` ✅ |

`"$"` 要求串尾，其后还要再跟字面 `zzz` —— 这个模式**不可能匹配任何串**。翻转点落在 N=2030，与读码推出的 2031 字节上限逐字吻合。

**机制**：`src/runtime/runtime_posix.c:131` 的 `char translated[2048]; size_t j = 0;` 配一条 `for (size_t i = 0; pattern[i] && j + 16 < sizeof translated; i++)` —— 拷贝在 `j + 16 >= 2048` 时停止，命中 `\d`/`\w`/`\s` 的展开还会额外吃掉配额。尾部（这里整个 `$zzz`）被丢掉，剩下的 `^aaa…` 匹配前缀 ⇒ **真**。

**覆盖为零，而且原因要说对**：`match` 全仓只有 **1 个** vtest 用到（`vtest/posix_core_api_v04.im`），`vtest/` 里最长的模式是 **59 字节** —— 比阈值低 **34 倍**。所以缺陷不是「用户会踩到」，是「**阈值远在任何正常用法之外，越界时它不说话**」：**缺陷是沉默，不是概率**。

**平台与基线**：`char translated[2048]` **只出现在 `src/runtime/runtime_posix.c:131`**；Windows 那份 `regex_match`（`src/runtime/runtime.c:986`）没有定长模式缓冲，本来就是对的 ⇒ **「同一个名字、两个答案」落在「两个平台副本」上，而且基线是没有 bug 的那一份**。这与 DECFY 设计档 §3.1.10「并集归哪一层」是同一类判断：不是选一个实现，是**定谁代表契约**。

**修法**（`vivid-anchor` 批准，走「动态分配」方案）：`size_t tcap = 16 * strlen(pattern) + 1; char *translated = (char *)malloc(tcap);`，分配失败就 `push_bool(vm, 0)` 并返回；循环边界改为 `j + 16 < tcap`；`regfree` 之后 `free(translated);`。16 倍是**上界而非测量**（最大的展开是 `\w` → `[[:alnum:]_]` 的 13 倍）。**任何路径都不再静默截断**。

**守卫与双向 A/B**：新 fixture `vtest/match_long_pattern_v06.im` + CTest **`#142 match_long_pattern_runtime`**，**跨平台注册**（不放 `if(NOT WIN32)` 里）—— Windows 那份本来就对，只在 POSIX 注册会让「Windows 是对的」这件事没有被钉住。断言是**成对**的：两档超长（2030、3000）的**不可能**模式必须 `false`，同长度的**可能**模式必须 `true`（否则「一律返回 false」的假修法也能过），另加 50 字节短对照。A/B：修好 ⇒ `Passed 0.07 sec`；把 `tcap` 缩回 2048（旧定长）⇒ `***Failed  Required regular expression not found`，两档 `impossible=true`，而两档 `possible` 与短对照仍然全对 —— **正好只有该红的红**。

**计数**：`add_test(` 141 → **142**，`tools/gate.sh:54` 的 `EXP_CTEST` 同步；`docs/STATUS.md` §2 四处与 `docs/BOARD.md` §3 两处同步。`check_orphan_fixtures` 输入数 114 → **115**（`vtest` fixture 68 → 69；`docs/BOARD.md:62` 的期望串同步）。

**一条过程记录（我自己的错，不是引擎的）**：我最初把 `PASS_REGULAR_EXPRESSION` 写成 `"match-long 2030 impossible=false possible=true"`，但输出是**两行**，`^` 把 `possible=true` 锚到了行首 ⇒ 断言永远不匹配，测试一上来就是红的，A/B 的第一步也因此假红。教训：**跨行的期望串不能用 `^` 起头拼**。

**诚实边界**：①`16 * strlen(pattern) + 1` 的 16 倍是**上界估计，不是实测倍数**；②翻转点 N=2030 与「2031 字节上限」两处是我在本机实测的，`noble-zephyr` **只核了代码形状、未独立复跑**；③我只在 Linux/gcc 上跑了这条断言，Windows 侧的绿是**由代码形状推断的**（那份没有定长缓冲），本次冻结期内**没有在 ucrt64 上实测**；④分配失败分支返回 `false` 对**本来应该匹配**的超长模式是错答案 —— 它不比原来对，只是**不再沉默**；⑤这条修的是「截断后仍给答案」，**不是**「超长模式应当被拒绝」那种语义决定。

## §10.98 一条注释声称成对，而断言在 CMake 里是「或」不是「与」

`match_long_pattern_runtime` 的注释逐字写着「Each length is paired: the impossible pattern must be false AND the possible one must be true, so that a non-fix which simply always answers false cannot pass」，而它唯一的断言是：

```cmake
PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"
```

**注释承诺的那一半，没有任何东西在读。** fixture 数据本身是成对的（六行输出：两档 `impossible=false` 与两档 `possible=true` 都在），缺的不是数据，是断言。

### 第一个修法被实测证伪

把断言改成 `;` 分隔的两项 ——

```cmake
PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false;match-long 2030 possible=true"
```

**看起来**是把两半都钉住了。实测：把 `posix_core_match` 临时改成恒 `push_bool(vm, 0)`（一个「永远答 false」的假修法）重建后，这条 CTest **仍然 `100% tests passed`**。原因是 CMake 的 `PASS_REGULAR_EXPRESSION` 是**列表内任一命中即通过（或）**，不是要求全部命中（与）。⇒ 这个「修法」把一个注释谎言换成了一个断言谎言，**比不改更坏**：下一个读者会以为成对性已经被断言了。

### 落地的形态

成对性要用两个**不同种类**的属性表达，而不是同一个列表的两项：

```cmake
  PASS_REGULAR_EXPRESSION "match-long 2030 impossible=false"
  FAIL_REGULAR_EXPRESSION "match-long [0-9]+ possible=false"
```

`FAIL_REGULAR_EXPRESSION` 命中即**红**，所以「没有任何一个可能模式答 false」这件事才有了读者。

### 第二个坑：「possible」是「impossible」的子串

我第一版写的是 `FAIL_REGULAR_EXPRESSION "possible=false"` —— 它匹配上了 `impossible=false` 里的子串（`im` + **`possible=false`**），于是**真修好的代码也红**。这与 `strstr("audio","io")` 过授 `CAP_IO` 是同一个形状：**子串匹配把「看起来像」当成了「就是」**。锚到行首那一节（`match-long [0-9]+ possible=false`）之后两个方向才对。

### A/B（两个方向，都走 CTest 本身）

| 代码 | 结果 |
|---|---|
| 真修法（`tcap = 16 * strlen(pattern) + 1`） | `100% tests passed, 0 tests failed out of 1` |
| 假修法（恒 `push_bool(vm, 0)`） | `***Failed  Error regular expression found in output. Regex=[match-long [0-9]+ possible=false]` |

还原后 `cmp` 证明与提交版本**逐字节相同**，worktree 干净。

### 诚实边界

①`FAIL_REGULAR_EXPRESSION` 钉的是「**没有任何**行报 `possible=false`」，它不检查 `possible=true` 出现了几次，也不检查那两行是**同一个长度**的两半（fixture 里长度与两半的配对关系由 fixture 自己保证，断言看不到）；②`match-long` 这个前缀字符串是断言与 fixtures 之间的**隐式契约**，改名会让断言静默失配（今天会红，因为 `PASS_` 那一半也找不到，但这是巧合，不是设计）；③这条记录的是**断言机制**，`match` 截断缺陷本身见 §10.97。
## §10.94 `count` 成为基数正典：`size` 的集合语境迁出，以及「数出来的数必须带 ref」

**裁定**（2026-10，人类选择「1、2」，[TYPESET_V06.md](TYPESET_V06.md) §7.8 第 2 条）：`count(A)` 是集合基数的**规范名**；`size` 在**集合语境**下降为**已弃用别名**并迁移；`size` 对**字符串／数组／字典**保留；`size` 作为 **GUI 关键字**不动。本流：`stream/count-size`，提交 `01da596`。

**实现。** `posix_core_count`（`src/runtime/runtime_posix.c:63`，注册 `:1172`）与 `builtin_count`（`src/runtime/runtime.c:149`，注册 `:1819`）。两份与 `size` **共用同一份集合代码**——无分量集合走 `iCount + count` 闭式，含分量集合走 `vm_set_to_array` + `vm_array_len` 枚举——所以「`count(A)` 与今天 `size(A)` 同答」是**按构造成立**，不是两次独立计算碰巧相等。`src/compiler/compiler.c:2894` 的 `comp->builtins` 条目只是卫生，不是生效路径：`EXPR_CALL`+`EXPR_IDENT` 在 `lookup_func` 落空后直接回落**按名**的 `OP_CALL_BUILTIN`，而 `lookup_builtin` 全文只被 `"window"` 用过（`:2186`）。

**写死的语义决定**：`count` 认**所有容器**（集合／数组／字典／字符串），**非容器一律 `nil`**，**不继承 `size` 的强转兜底**。于是 `count(42)` = `nil` 而 `size(42)` = `42`。

**⚠ 三条 nil 是三条不同的路，不能混为一谈：**

| 表达式 | 结果 | 机制 |
|---|---|---|
| `count(42)` | `nil` | **类型拒绝** —— 参数不是容器，`n` 保持 -1 |
| `size(-5)` | `nil` | **数值兜底** —— `-5` 是合法整数结果，但尾部 `if (n < 0) push_nil(vm)` 把负数当成「无答案」 |
| `count(Z)` | `nil` | **枚举器拒绝** —— 无格点，`vm_set_to_array` 返回 -1 |

三者答案相同、来路不同。这正是为什么 `size(42)`／`size(3.7)` 的**强转**与 `size(-5)` 的 **nil** 不是同一件事，测试里必须分开钉。

**⚠ 数出来的数必须带 ref，不能写成一个常量。** 本流最初把迁移面写成「29 处」，`surgery-verifier` **只用 `git ls-files '*.im'`、不数 worktree 副本**独立重算得到 **30**；`be-surgeon` 在同一口径下得到 **29**，并给出机制：**差异来自 ref，不是分类**。

| ref | `git ls-files '*.im'` | `size(` |
|---|---|---|
| `stream/count-size` 基线 `f6b3d87` | 346 | **29** 处 / 17 文件 |
| `stream/be-removal`（`be-surgeon` 的树） | 351 | **29** |
| 集成分支（`surgery-verifier` 量） | — | **30** |

**三个数各自为真，因为它们量的不是同一棵树。** ⇒ 文档一律写「**在 ref X 上是 N**」，并把「**落地迁移前必须在当时的 ref 上重数**」当作规则。全盘 `grep -r "size("` 得到 195 一类的大数，那是把 5 棵 worktree 的副本与 `.verify/` 一起数了，**不是迁移代价**。

**分类（口径：在 `f6b3d87` 上重数，29 处 / 17 文件）** —— 26 处是真实调用，3 处只在注释里：

- **16 处集合语境调用 → 迁 `count`**，其中 **15 处纯改名、输出逐字节不变**：`set_comp_test.im:5/9/13`、`set_op_test.im:8/15/34`、`projects/set_comp_test.im:4/8/12`、`projects/set_op_test.im:8/15/34`、`projects/tt4.im:2`、`vtest/set_components_enumerable_v05.im:32`、`inf_set_test.im:97`。**证据**：`vtest/set_components_enumerable_v05.im` 改名后它的 PASS 正则**至今仍要求 `size=3`**（`CMakeLists.txt:827`），输出不变红 ⇒ 改名没改行为。
- **1 处必须改意图、不许改名了事**：`inf_set_test.im:85 o = size(1,2,3)`。它印出 `3` **只是因为内建只读最后一个实参**（等价于 `size(3)`），**纯属巧合**，不是 `{1,2,3}` 的基数——直接改名成 `count(1,2,3)` 会得到 `nil`（整数被拒）。已改成先绑 `o = 1,2,3` 再 `count(o)`，并在原处留注释写明为什么，以及这个巧合被钉在哪条测试里。
- **10 处不是集合语境 → 保留 `size`**：9 处数组（`arr_test.im:4`、`err_test.im:15`、`lp2.im:4`、`lp3.im:4`、`params_test.im:6`、`t_lp3.im:4`、`t_push.im:6`，以及 `set_op_test.im:39`／`projects/set_op_test.im:39`）＋ 1 处字典（`vtest/posix_core_api_v04.im:15`）。**注意后两处**：那里是 `l = list(a)`，**`list()` 返的是数组、不是集合**，所以按「数组保留」不动——这一点本流最初归错了（报成 17 迁），是复核时抓出来的。
- **3 处只出现在注释里**（`vtest/set_str_component_count_v06.im:8/18`、`vtest/sum_components_int64_v06.im:30`）：无代码改动，注释随规范名改写。

**⚠ 多参陷阱：只有最后一个实参会被读到（本流实测发现）。** 每个内建只读栈顶 `vm_cur_stack(vm)[vm_cur_sp(vm)]`（`src/runtime/runtime_posix.c:32` / `src/runtime/runtime.c:119`），而 `src/vm/vm.c:3805` 虽记了 `vm->cur_argc = ins.r3` 却**无人消费**，调用返回时也只弹一项（`src/vm/vm.c:3825` 的 `value_move(&R[ins.r1], &t->stack[t->sp]); t->sp--;`），于是多出的实参被**静默忽略**、留在栈上。实测：`size(5,5,5)` = `5`、`size(7,8)` = `8`、`size(7,8,9)` = `9`、`len(9)` = `9`、`len(5,5,5)` = `5`、`count(7,8)` = `nil`、`sum(7)` = `nil`、`sum(1,2,3,4)` = `nil`。**这不是本次引入的缺陷，是既有行为**；登记它是因为 `size(1,2,3)` 那种写法在语言里**没有任何一处会报错**，而人读到会以为它是「三个元素」。已钉进 `vtest/count_builtin_v06.im`（CTest `count_builtin_runtime`）。

**测试与验证。**
- 新增 `vtest/count_builtin_v06.im`，CTest `count_builtin_runtime`（`LABELS "runtime;types;regression"`）。PASS 正则 `count-ok bad=0 set=3,5,5,5 dict=2,2 str=5,5 arr=3,3 int=nil,42 float=nil,3 neg=nil,nil unenum=nil,nil arity=nil,5,9`，FAIL 正则 `bad=[1-9]`。断言文本用「字面量 + 计算值」的形状：CTest 同时抓 stderr，而引擎在 stderr 会打字符串常量池（`[0]="count-ok bad=" …`），字面量直接当正则会假绿。
- 实测（`./build/inimerse --no-mods probe_count.im`，本机 Linux/POSIX，`f6b3d87` + 本流改动）：`countset=3 countmixed=5 countmixed_size=5 countstr=5 countarr=3 countdict=2 countint=nil countfloat=nil countbool=nil countnil=nil countZ=nil sizeint=42 sizefloat=3 sizeneg=nil countneg=nil sizestr=5 sizearr=3 sizedict=2 sizeZ=nil multisize=5 multilen=9 multicount=nil`。
- `ctest --test-dir build -R count_builtin -V` ⇒ `100% tests passed, 0 tests failed out of 1`。

**诚实边界。**
1. **GUI `size` 零覆盖。** GUI 语境（`move`/`size`/`bounce`）在 346 个受版控 `.im` 里出现 **0 次** ⇒ CTest 对它**没有任何断言**，**「门禁全绿」不能当作「GUI `size` 没被动过」的证据**。本流没有改 `docs/SYNTAX.md:450/468/540/910`，也没有改 `src/compiler/compiler.c:1996` 的 `strcmp(verb, "size")`，但**在本流内未做验证**——这是缺口，不是成绩。
2. **WIN32 那份没有编译证据。** `src/runtime/runtime.c` 在 Linux **不参与构建**（`CMakeLists.txt:429` 是 WIN32 源列表、`:438` 才 `list(APPEND … runtime_posix.c)`），且按 `docs/AUDIT.md` §1.16 它连 `cc -std=gnu11 -fsyntax-only` 都过不了（错误全在 601–1651 行）。本流只做到「两份同体 + 数组/字典都用 POSIX 那份带边界检查的写法」，**没有**跑改动前后错误行集合求差。
3. **计数基线。** 本分支原生基线是 `f6b3d87`，`add_test` 137 → **138**；`main` 已在 `5cf4aa1`（142）。集成时正确值是 **143**（137+5+1），由协调者在集成分支处理，本分支不追。

## §10.99 同一份健康表里的 `unknown` 与 `down`：15 秒预算下的负载敏感 flake（**未修，只记一笔**）

**这一笔是记录，不是修复：本流没有改任何测试代码，也没有改引擎。**

**观测**（2026-10-21，本机 12 核，同时有别的会话在编译与跑测试）。在**干净检出**的 `origin/main` = `e29aee0` 上跑全量门禁，`ctest` 阶段第 46 条红，原始输出逐字：

```
125/157 Test  #46: lease_handoff_regression ..............***Failed   16.57 sec
Traceback (most recent call last):
  File ".../tools/lease_handoff.test.py", line 161, in main
    assert health.get(node_ids["C"]) == "down", health
AssertionError: {'7f11e493…': 'up', '9f56c34b…': 'up', '244a9c2d…': 'unknown'}
```

**它报的不是「C 是 `up`」，是「C 还没有被观测到」**（`unknown`），而断言期望 `down`。这两个词在这份健康表里是两件事。

**它不是确定的**：同一棵树、同一份二进制，之后 **22 次运行全绿** —— 孤立单跑 20/20；`ctest --test-dir build --output-on-failure -j6` 2/2（157/157）；`tools/gate.sh --fast --only ctest --jobs 6` 1/1；**去掉 `NODE_PATH` 后单跑也过**（这一条是顾问要求做的对照，用来排除本地环境变量造成的假红，结果是排除掉了）。唯一红的一次，是它和别的会话的编译同时跑的时候。主干推进到 `f5c7369` 后再跑全量门禁，`ctest` 阶段也过（158/158）。

**机制（读码，未插桩）**：`tools/lease_handoff.test.py:151-158` 轮询 `60 × 0.25s = 15 秒`，等 A/B 为 `up` **且** C 为 `down`；`16.57 秒` 正好是预算耗尽后断言的那一刻。C 是一个已死的端点：`down` 是「探过、不可达」，`unknown` 是「还没探」。⇒ 这是**预算问题，不是语义问题** —— 观测是异步的，15 秒在高争用下不够。[obs: e29aee0]

**为什么没有当场改**：把预算从 15 秒加长是一件现在就能做的事，但**它的正确值取决于 hub 侧的观测周期，而本流没有读那个周期**；在没读它之前挑一个更大的数，就是把「未知的失败」换成「未知的成功」—— 本仓对同类问题已有这条判据（`docs/BOARD.md` 的 `node-discovery-flake` 行逐字：**没有机制的「修复」只是把未知失败换成未知成功**）。

**下一步（可执行）**：读 hub 侧 `/node/discover` 的 health 由谁写入、多久一次，把等待预算写成**由那个周期导出**的数并标 `[obs:]`；或把断言改成「在预算内 C 必须从 `unknown` 变成 `down`」并给出预算依据。**反向判据**：人为让观测周期超过预算，这条测试必须仍然红 —— 否则改的是数字不是断言。

**与 §2.9 的关系**：那个窗口**已修**，且是另一种失败模式（harness 猜端口，报「hub did not start」）。这一条是**新的一种**：端口对了、hub 起来了、健康表也答了，只是**答案是 `unknown` 时预算已经烧完**。`tools/lease_handoff.test.py` 自己已记录过另一个**已修**的旧 flake（`docs/STATUS.md` §2.9），两者都不是这一条。

**诚实边界。**
1. 本流**只记录观测，没有修任何代码**；「下一步」是建议，不是已做的事。
2. 失败签名本流**只见到 `unknown` 这一种**，**没有**见到「C 报 `up`」。
3. 22 次全绿说明它**不是**确定性红，**不说明它罕见到可以忽略** —— §2.9 那个窗口当年也是约 80 轮 1 轮。
4. 本流**没有**用 80 轮重复去测它的真实频率；上面那个「22 次」是随手的重跑次数，**不是频率的读数**。

## §10.100 路线图第 2 条那句「仍未实现」是落地之前的陈述：CRP 回环、签名校验与 413 的今天读数（路线图 #2）

**裁决先说：成立的是 §10.6，不是 §9.2 第 2 条。** §9.2 第 2 条写「CRP `FIND` / `PORTAL` 回环和签名校验 **仍未实现**」，§10.6 记录引擎侧线层已落地；两句话同时挂在这份文档里自相矛盾。今天在 `d6ef39f` 的 worktree（`stream/crp-loopback`，based on 基线 `d6ef39f81088940dcb3eaa13f97faf9efe9fc269`）上用**真二进制**复核，四项逐一跑通，且四个负控件逐个 403 ⇒ **§10.6 成立，§9.2 第 2 条不成立**（它是 §10.6 落地之前的陈述，没有被一起改掉）。

**判据（两句话）**：① 交换必须跨**进程边界** —— 驱动进程起真 `build/crp-hub`、用真 socket 走协议，不复用引擎内 `verse_crp_probe` 的断言；② 失败签名必须被**拒** —— 负控件要红，且红的是「拒」，不是「没跑到」。

**命令与读数（全部在本流 worktree 重跑，不是转述）**：

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
BUILD_EXIT=0
$ ctest --test-dir build -R crp
100% tests passed, 0 tests failed out of 5
$ ./build/verse_crp_probe
crp_probe: 185 checks, 0 failures
$ node tools/crp_engine_crosscheck.js ./build/verse_crp_probe
crp_engine_crosscheck: 115 records, text-identical
$ INIM_CRP_HUB_BIN=./build/crp-hub python3 tools/crp_closed_loop.test.py ./build/crp-peer
| crp_peer: 31 exchanges, 0 failures
| hub: {"ok":true,"served":1,"exchanges":30,"bye":true}
crp_closed_loop: 19 checks, 0 failures
$ node .scratch/crp_wire_neg.js ./build/crp-hub
wire_neg: 13 checks, 0 failures
```

闭回环转录逐行统计（`| >>` 31 行对 `| <<` 31 行）：状态码 `200×14 / 202×3 / 400×3 / 403×7 / 404×3 / 409×1`；按 op 计 `find×3 / portal×7 / signal×8 / resume×6 / revoke×1 / status×2 / register×3 / bye×1`。

**四项分列表**（不合成一句「CRP 做完了」）：

| 项 | 今天是否覆盖 | 跨进程证据 | 负控件（必须红） |
|---|---|---|---|
| `FIND` | 覆盖 | 闭回环 `find×3`（全库、`DEM` 小写匹配、空库）；线级探针 `query=dem` → `200`、`items=1` | 空库 `find` 返回空列表不报错（这是语义选择，不是安全负控，见边界 2） |
| `PORTAL` | 覆盖 | 闭回环 `portal×7`，有效 proof → `200` 并铸出 capability token | 无 auth / 错 auth / **替另一对（`demo`, `other-peer`）签的 proof** → `403` |
| `SIGNAL` | 覆盖 | 闭回环 `signal×8`；探针有效 token → `202` | **签名末字符翻转** → `403`，`error` 逐字 `invalid capability token`；**错密钥铸的 token** → `403`；**别人的 token 发 signal** → `403` |
| 签名校验（resume / revoke / 过期） | 覆盖 | 探针 `resume` 有效 → `200`；闭回环 `revoke×1` 后再用 → `403`、乱序 resume → `409` | 签名篡改 → `403`；revoke 后重放 → `403`；乱序 → `409` |

**§9.2 第 3 条复验（`hub-large-package`）**：`578eb95` 已修，今天用两条互不依赖的尺子复验：① `python3 tools/http_large_package.test.py` → `LARGE_PKG_EXIT=0`、31 条 `ok`、末行 `all checks passed`（part 1 覆盖 `/v`、`/package`、`/content`、`POST /package/fork` 四条出口，外加「恰好 65536 仍须全量」「被拒的 fork 不在盘上留截断副本」「UDP 对超大包不发任何东西」）；② 本流自己起 hub 用 curl 读：`GET /v/small` → `200` / `100 B`、`GET /v/exact`（65536 B）→ `200` / `65536 B`、`GET /v/big`（66560 B）→ `413` / `57 B`，body 逐字 `{"error":"package_too_large","size":66560,"limit":65536}`。⇒「要么完整送达、要么显式 413」今天成立，且边界是 `size > cap` 而非 `>=`。

**§9.2 第 4 条：只评估，不写代码**（涉及外部凭据，不能自己拍板）：

- **GitHub 侧：成立，而且是端到端的。** 不是「代码里有」：用户实机走过一次回调路径，`linked_accounts.json` 被重写、`access_token` 是 `gho_…` 40 位、`scope read:user user:email`，拿它打 `GET https://api.github.com/user` 得 `200 / login=infileap`（§10.34、§10.36）。
- **Bilibili 侧：只有代码级端点，没有一次真机往返。** `Infiverse_standard/oauth_loop/src/lib.rs` 里 authorize 与 token endpoint 都写了（后者是 `https://passport.bilibili.com/oauth2/token`），但 `device_code_endpoint()` 对 bilibili 返回 `None`（不编造官方不存在的端点）；仓库里没有任何 Bilibili 开放平台应用注册记录，也没有真机授权往返的读数。
- **前置依赖**：B 站侧需要一个已注册的开放平台应用（client id/secret 或扫码授权），这在仓库之外、不能由本流造出来；另外 `linked_accounts.json` 的**读取侧**（谁在何时读它、绑定关系被谁消费）没有端到端验过。
- **可做与不可做**：可做的是把「Bilibili 侧缺什么」写成可验收清单（应用注册、回调白名单、device flow 是否开放）；不可做的是替用户注册应用，或替它编造一个端点。

**反向判据（若它其实没覆盖，哪个读数会变）**：把 `crp_capability_verify` 改成恒真，「签名末字符翻转」「错密钥」「别人的 token」「替另一对签的 proof」这四条负控件必须变成 `200`；若变红的是探针而不是协议，探针没牙；若协议没变而套件仍绿，说明那四条根本没被跑到。**本流没有做这个变异实验**（见边界 4），所以上表证明的是「今天跑到了、答案是拒，而且每道门禁都会再看到一次」，不是「这个拒在结构上不可绕过」。

**诚实边界。**
1. **线级探针现在住在 `tools/crp_wire_neg.test.js`，是 node 套件的第 13 个套件**（`tools/gate.sh` 的 `EXP_NODE` 12→13 与 `docs/BOARD.md` 的表同一笔）。它起真 `crp-hub`、走真 TCP、用协议自己的 HMAC 自签 proof 与 token，所以「四个负控件要红」的证据是**每道门禁都看一遍**，不是我看过一次。仍有的边界：这四条只覆盖 `PORTAL` / `SIGNAL` / `RESUME` 的签名与配对，`FIND` 的查询语义、token 的过期窗口与重放都不在其中；而且它验证的是「拒」，不是「结构上不可绕过」（见反向判据的变异实验）。
2. `FIND` 只覆盖 4 次调用（闭回环 3 次 + 探针 1 次）：查询语义（大小写、排序、`limit`/`cursor`、更宽的匹配面）没有逐格覆盖。
3. `PORTAL` 只做认证，**不做逐 (verse, peer) 授权**：拿到 token 的一对能否访问另一对，仍由发送方自觉（§7.7 的缺口）。BOARD 的 `crp-in-engine` 行早已记这条遗留边界，本流没有改变它。
4. `crosscheck` 的 115 条与 §10.6 写的 101 条**不是矛盾**：101 是 `json_min` 扩项之前的语料数，今天实测 115。同理，`docs/` 里每一处 ctest 计数都带自己的时点。
5. **第一笔只动文档**，不改 `EXP_CTEST` / `EXP_NODE`，也不改 `CMakeLists.txt`，因此 pin 与计数不该动。**第二笔只动 `EXP_NODE`（12→13）与 `tools/node_suites/run_all.js`、`tools/gate.sh`、`docs/BOARD.md` 的表**，仍不碰 `CMakeLists.txt`、不动 `EXP_CTEST`：`gate.sh` 里的值是**原地替换**，行数不变，所以 `docs/` 里指向 `gate.sh:<N>` 的 pin 全部不位移。全套门禁读数见本笔的回报（若合并时基线又推进，以门禁日志为准，不以本节文字为准）。

**补记（本笔自己撞出来的红）：文档改动也是门禁输入。** 本笔声明「只动文档」，仍一度把 `ctest #171 docs_headings` 打红：`tools/docs_headings.pin.json` 钉着 `headings=1374`，新增的 `§10.100` 把它推到 1375，于是「删一节」那两条用例拿到 `removed=0`（删完正好等于旧基线，删与加互相抵消）。修法不是改测试，是 `python3 tools/check_docs_headings.py --write-pin` 重钉。而**重钉本身还有一条陷阱**：`--write-pin` 记录的是当时的 HEAD，若随后 `--amend`，被记录的那棵树就不再是任何提交的祖先 —— pin 指向悬空对象，`git gc --prune` 之后一棵**干净**的树会红，且报错把人引向「历史被改写」。本笔先 amend（到 `890dfa1`）才发现，于是把 pin 重钉到 `59dbc9a` 并**单独提交一笔**，判据是 `git merge-base --is-ancestor <pin.tree> HEAD`（实测 `REACHABLE`）。⇒ **「只动文档」是关于 diff 的陈述，不是关于门禁输入的陈述**：`headings` 计数就是被这 1 个小节亲手从 1374 推到 1375 的。
## §10.101 UPP 接入传输层：一次「信封是唯一差别」的等值测量（`upp-transport`）

**核实先于动手。** [BOARD.md](BOARD.md) 的 `upp-in-engine` 行当时的「遗留边界」写了两条：`timestamp` 只收整数、尚未接入 `inim-server`/`inim-client` 传输层。开工前逐条实测：

| 主张 | 实测命令 | 结果 |
|---|---|---|
| 传输层未接入 | `grep -n "upp" src/verse/server.c src/verse/client.c` | 两文件零命中 ⇒ **成立**，本流做这一条 |
| `timestamp` 只收整数 | `sed -n '/int upp_session_apply/,/^}/p' src/verse/upp.c`（heartbeat 分支） | 判定用 `json_min` 的整数路径，非整数即拒 ⇒ **成立**，不在本流范围 |

**做了什么。** `inim-server --upp` 与 `inim-client --upp`：同一会话，外面套 UPP 信封（`src/verse/upp.h`）。Layer 协议一个字没改 —— 请求以 `verse` 帧 `payload` 的**原始 JSON 文本**进出，所以两种模式只差信封。判据就是这条等值本身，且它是**被断言**的（`tools/verse_upp_transport.test.py`，CTest 名 `verse_upp_transport`）。

| 判据 | 读数 | 复跑 |
|---|---|---|
| 同一 scenario 走两种传输，响应行逐字相同 | `23` 条断言时 5 行全同；加大请求后共 `29` 条断言 | `INIM_SERVER_BIN=build/inim-server python3 tools/verse_upp_transport.test.py build/inim-client` |
| 等值不是「两边都没输出」 | 响应行数 == 请求行数（5/5、4/4） | 同上 |
| Layer 自己的拒绝穿过信封 | 首行两模式都是 `{"code":"no_session",...}` | 同上 |
| 比较器是活的（多一个 `put` 必须不等） | `first_difference >= 0` | 同上 |
| 60 KiB 请求（超 `VL_LINE_MAX/2`，界内）两模式同答、且 `ok:true` | 逐字相同 | 同上 |
| 比较器自身可被反驳 | `--self-check`：`6` 条，含空对空比较返回「相等」这一陷阱的显式断言 | `python3 tools/verse_upp_transport.test.py --self-check`（CTest 名 `verse_upp_transport_selfcheck`） |

**信封新增的四条拒绝**（`upp.h` 本身不决定、由传输决定；全部是拒绝而不是尽力而为）：

| 情形 | 退出码 | stderr 原文（节选） |
|---|---|---|
| `start` 之前收到 `verse` 帧 | `4` | `upp refused: 'verse' frame while the session is init, not running` |
| 未知帧类型 | `4` | `upp refused: unknown frame type 'nope'` |
| 这一行根本不是帧 | `4` | `upp refused: ...`（`upp_decode_line` 的原因） |
| 与服务器不同 abi 的 `hello` | `5` | `upp hello refused: ...` |
| `payload` 超过 Layer 的请求上界 | `4` | `upp refused: 'verse' payload is 400046 bytes, past the Layer's 65535-byte request bound` |

**反向判据（三个控制，逐个实测，恢复后全绿）。** 每一条都改**实现**而不是改断言，看断言是否真的红：

| 控制（改实现） | 修复前（控制态） | 修复后（出厂态） |
|---|---|---|
| A：服务器直接把 Layer 响应写出去，不发信封 | `28 checks, 7 FAILURES`（等值、退出码、60 KiB 三组全红） | `29 checks, 0 failures` |
| B：删掉「只有 RUNNING 才收 `verse`」 | `29 checks, 3 FAILURES`（`verse` before start 被**答复**而不是拒绝） | 同上 |
| C：删掉 payload 的请求上界 | `29 checks, 3 FAILURES`（超界请求被**提交**） | 同上 |

**本流顺带修掉的两处既有缺陷**（都在 `src/verse/client.c`，都属于「静默」这一类）：

1. **scenario 行读器静默截断**：`slurp()` 原来用 `char buf[VL_LINE_MAX]` + `fgets`，超过 64 KiB 的一行被**截断**而不是读整；截断后的半个 JSON 仍是一行，服务器答 `malformed`、客户端退 `0` —— 报错里的偏移量指向一个写请求的人从没写过的位置。改成按需增长。同一形状在本档 §10.10 已记过一次（那次是**回复**被截断）。
2. **`stop` 帧与 `bye` 的竞争**：Layer 的 `bye` 让服务器自己退场，客户端随后那条礼节性 `stop` 帧可能撞上已退出的子进程，默认 `SIGPIPE` 处置把客户端**杀死在打印完所有响应之后**（实测 `-13`，且是间歇的）。改成 POSIX 上忽略 `SIGPIPE`：写失败是这里想要的行为。Windows 无此信号，那一半不受影响。

**诚实边界。**

1. **只在本机 Linux 实测。** Windows 那半（`child_spawn` 里 `cmdline_append` 追加 `--upp`）**只编译过、没运行过** —— 本机无法确认 `CreateProcessA` 那条路径。这是「被报告的代码」而不是「已验证的行为」。
2. **plain 传输的 64 KiB 行读器仍在。** `vl_server_handle()` 没有长度上界，上界住在 `run_plain()` 的 `fgets(line, VL_LINE_MAX, stdin)` 里。超过 65535 字节的请求在 plain 侧被**截断**，Layer 报 `unterminated string at offset 65535`；UPP 侧按**同一条上界点名拒绝**。⇒ 两种传输在超界处的**措辞**不同，语义相同（都拒）。本流**没有**修 plain 那一半：修它等于把 Layer 的请求上界从行读器搬进 `protocol.c`，那是协议暴露面的改动，不属于「接传输层」。**下一步**：把上界变成 `protocol.c` 里一个可被引用的常量，两半共用，plain 侧也就从截断变点名。
3. **1 MiB 帧没有被测到边界本身。** 实际测到的是 60 KiB（提交路径）与 100 KiB（拒绝路径）；`UPP_MAX_FRAME_BYTES` 只用来说明缓冲区大小从哪来，不是一个被验证的上界。
4. `timestamp` 仍只收整数（§10.1 的遗留边界，未在本流动）。
5. **未接网络层**：UPP 仍只在 stdin/stdout 上跑，没有 WebSocket/套接字那一半；`crp_ws_client.js` 的客户端与它无关。
6. **这一笔把 line-refs 的余量用光了。** 接线要加三行到 `CMakeLists.txt`，而 `docs/` 用行号引用它比引用任何别的文件都多：三行插在中间，`check_line_refs.py` 的 unanchored delta 就从 +11 走到 +87，判决当场点名 `unanchored-over-ceiling`。改成在文件末尾追加（`target_include_directories` 可以在目标存在之后再说，两处 `add_executable` 只在原有行内加源码），读完是 **+12，天花板也是 +12** —— 合上了，但**一格余量都没有**。这是这一笔的成本，不是留给下一笔的钱：日后往 `CMakeLists.txt` 中间添一行，或者往 docs 里加一条指不到锚的 `file:NNN`，都会先撞在这条天花板上。下一个人动手前该先读到的一句是提交 `6248dfc` 的标题：

> a line added above a cited address is a citation moved

**别动数字，动脚。** `EXP_LINE_REFS_UNANCHORED_DELTA_MAX` 是债的账本，不是配置项：它可以用环境变量覆盖，而「设一个环境变量把它修绿」正是那段注释禁止的动作。要腾余量只有两条路 —— 修一条 unanchored 引用（把债还掉），或者把插行改成末尾追加（把脚走对）。
7. **`stop` 帧是尽力而为。** 忽略 `SIGPIPE` 之后，对已退出的 peer 发 `stop` 会静默失败 —— 这是设计，不是遗漏：会话已经在 `bye` 处结束，那条 `stop` 只是替「没有 `bye` 的场景」收尾。

## §10.102 per-`(verse, peer)` 授权：把「谁允许谁」变成一张可读的表（portal-per-pair）

BOARD 的 `agent-bridge-round2`（`63fef73`）决断②把这条缺口收成一句话：**最小形态 = 把 `/portal` 的证明输入从「两个自选字符串」扩为「hub 持有的 per-pair 注册项」**（per-pair secret + per-pair `capabilities`）。同一份决断里还有一条约束，本流是按它设计的：**未登记的 pair 与证明错的 pair 必须不可区分（都是 403）**，否则 404 就成了枚举 oracle。

**今天之前**：证明是 HMAC(全局密钥, `verse` + `\0` + `peer`)。它证明的是「这个 pair 知道那把全局密钥」——认证成立；签发侧的 scope 只被**记录**、从没和「谁允许谁」比过 ——授权不成立。缺的不是校验，是**一张表**。

**本流做了什么**（参考实现与引擎两侧同一套语义）：

- 一张 TSV：`verse \t peer \t secret \t capabilities`，`#` 注释与空行忽略，`\t` 必须恰好 4 段。`capabilities` 本轮只认 `signal`（见边界第一条），为空即无能力。
- **单一来源，跨级不回退**：`CRP_ADMISSIONS_FILE` 与 hub 目录下的 `portal_admissions.tsv` 二选一，选不中就是**空表**，绝不降级到另一个来源——「文件配错就悄悄用默认路径放行」是这一层最坏的失败。
- **整份作废而不是逐行挑**：同一 `(verse, peer)` 出现两行、字段数不对、`capabilities` 里有未知值、secret 字段为空——任一命中就整份不用，全拒。轮换做到一半和手滑写重，运维都该看见，而不是被 first-wins 悄悄掩掉（`R2` 那笔欠账就是同一种形状的病）。
- **两种拒绝逐字节相同**：未登记 pair 与证明错都返回同一段 `{"error":"invalid enrollment proof"}`，成因只进 hub 的 stderr（`cause=unregistered-pair` / `bad-proof` / `admissions-duplicate` / …）。判据不是断言「都是 403」，是抓原始字节（状态行 + 除 `Date` 外的全部头 + body）逐字节比较，并且有一条**元判据**：把文案改成 `pair not admitted` 之后它必须红——证明这个断言本身是活的。
- **准入先于注册表**：只有被准入的 pair 才走得到「verse 是否注册过」这一步，所以 404 只留给「被允许访问、但这个 verse 没注册」；一个未登记的 pair 连 verse 存不存在都问不出来。
- **两处 404 变成 403，是上一条的必然结果，不是顺手改的**：`{verse, peer}` 里有一个不是字符串（`{"verse":5,"peer":null}`）、或者整个 peer 缺席，都**不构成一对**，因而在表里查不到——它们走的是「未登记」那条路，拿到的就是与证明错逐字节相同的 403。旧行为给它们 404（`verse not found`），那等于告诉调用方「你这个 verse 名我不认识」，而这条消息对未登记的双方本来都该是不可说的。受影响的既有断言两处：`http_probe` 的 wildcard 用例从「要求 404」改成「要求 403 且 body 是 `invalid enrollment proof`」（它真正要测的「非字符串铸不出通配令牌」不变）；`crp_peer` 的 `portal without peer` 从 404 改 403。`reconnect_generation` 里 `v-unregistered` **仍是 404**（被准入、只是没注册），另加一条未登记的 `v-stranger` 断言 403 **且响应与无 auth 逐字节相同**——这两条一起才说明 404 没有被扩用成侧信道。
- **`CRP_ENROLL_SECRET` 变成死的事实**：表启用后它不参与任何判定，env 在场也不放行未登记 pair。这是不兼容变更，所以迁移说明写死：登记行先沿用旧全局密钥 ⇒ 客户端零改动；**撤掉它是收尾，不是可选**。
- **比较两侧都常时**：文案相同而耗时不同一样是 oracle，HMAC 比对用常时比较（参考实现 `crypto.timingSafeEqual`，引擎侧同义实现）。
- 表里 secret 是明文：加载时 `stat` 一次，组内/其他可读只记 `cause=admissions-perms-loose` **不拒**——警告让运维看见自己的文件是 644，而不是替他把服务器关掉。

**读数**（本流 worktree 实跑）：`crp_probe` **185 checks / 0 failures**；`crp_engine_crosscheck` **128 records, text-identical**（原 115 + 13 条解析边角语料：空串、纯空行、纯注释、正常、secret 含空格、CRLF、空 `capabilities`、字段不足、空 verse、未知 capability、重复 pair、尾随空格、前导空白+注释）；闭回环 **31 次交换 / 0 失败**（peer 侧 19 项驱动判据 0 失败）；`crp_session_flow`、`reconnect_generation`、`crp_relay.test.js`、`crp_client.test.js`、`crp_wire_neg.test.js`（13/0）全绿。

**诚实边界**：①`capabilities` 目前是**占位**——只认 `signal`、不参与判定，「per-pair 能力」这一半决断②还没有消费者；②secret 明文落盘，权限只警告；③表在进程内加载一次，**没有热加载**（改表要重启 hub），也没有 pair 台账与所有者表——「谁登记了谁」仍然只在文件里；④Windows 半侧只编译不运行；⑤没有签名链，也不做每 verse 一把所有者密钥。

**迁移**：写表（每对一行、secret 沿用旧全局密钥）→ 客户端不必改（同一 HMAC 算法、同一 secret）→ 起 hub 验证同一 pair 从 403 变 200 → **从环境里撤掉 `CRP_ENROLL_SECRET`**（留着不参与判定，但会让人以为还有一条全局通道）。
## §10.103 MIR 侧的第一个字符串常量：驻留、零分配、一个打印出口（aot-mir-value，A 类首项）

作业单 `docs/streams/aot-mir-value.md` 的 §3 只要求 `OP_LOADK_STRING` 一通，§5 与 §6 把落法与诚实边界写死了。本流交付它，并把上一流那句「6 条断言常驻」从**承诺**变成**测量**。

**两笔提交**：笔一让 `tools/aot_mir.test.py` 读每一条生成产物的文本、逐条点名 6 条布局断言（缺哪条报哪条），反向控制是把 `offsetof(MirVal, ptr) == 24` 从模板里删掉并确认判据红且点名；笔二落地 `OP_LOADK_STRING` 与三个 MIR-only 用例。

**落法（§5 甲）**：字符串常量是**驻留**的静态 C 字面量，`ival = 1` 打驻留标记，与本 VM 已有的 `VAL_STRING` 约定同源；**零分配、零拷贝、无所有者**。`ival` 同字段两义是 VM 已有债，本流沿用并在生成代码的注释里写明，不新造干净字段。不碰 `NEW_ARRAY`/`NEW_DICT`/`NEW_SET`/`INDEX_*`/`CONCAT`。

**打印（§4 丙的后果）**：共享 preamble 的 `nv_say` 只有三个分支且 `NV` 没有字符串载荷，`mv_as_nv` 对 `MV_STR` 按设计中止。因此本流新增**恰好一个** MIR 侧打印出口 `mv_say`：**只有驻留字符串**走自己的分支，其余所有 tag 一律 `nv_say(mv_as_nv(a))` —— 整数/浮点/布尔的格式化仍只有一份实现，重实现会变成「影子里的第二套运行时」。它不是许可，而是第二笔诚实欠账：删除条件写在注释里（`nv_say` 学会字符串，或完成 P3 后只剩一个打印器）。

**读数（本流 worktree 实跑，逐字）**

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=3|mir-only-same-as-interpreter=3
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)
IR-AOT-MIR|accepted=89|refused=0|same-as-interpreter=89|of=29|emittable=31|of-ops=67|not-emittable=36|corpus=inline-cases:86|mir-only-cases:3|beyond-ast-denominator=1|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-LOWERING|handles=31|of=67|unknown=36|table=31|emitter=31|reported=31|agree=yes
IR-AOT-MIR-BEYOND|ops=(OP_LOADK_STRING)|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR-UNEXECUTED|ops=(OP_OR)|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR-VERDICT|PASS|tree=6469528d33bc6b7bde35645ebed75002e0b99a81|accepted=89|of=29|covered=29|missing=0|refused=0|same-as-interpreter=89|prelude-excluded=yes|beyond-ast-denominator=1
```

`same-as-interpreter` 由 86 涨到 89；`handles` 由 30 涨到 31、`unknown` 由 37 降到 36。

**与作业单 §3 判据 2 的偏差（要说的那条）**：作业单期望 `ir_spec` 的 `aot N/67` 上涨并报值。它没有涨，仍是 29/67，而且**不是漏做**：这个分母测的是「解释器在 86 例 AST 语料上**实际执行过**的 opcode」，而 AST 路线对每个字符串程序都是编译期拒绝（expression kind 2 is outside the numeric subset），所以任何字符串程序都进不了那份语料，分母也永远不会因为本步而涨。把 MIR-only 的 opcode 塞进 `of` 或塞进那个 pin，会让 `missing` 变成空话（仓库里已把「改分母使其自洽」记成一种陷阱）。因此本流**不动那个分数**，另开一行 `IR-AOT-MIR-BEYOND` 与 pin 里的 `beyond-ast-denominator`：**同一个 opcode 可以在这条路线被计数，却不在那条路线的分母里**，两件事各有各的行。

**`OP_OR` 是顺手撞见的另一件事**：`bool_or` 与两条 `or_result_*` 的 lowering 里都有 `OP_OR`，而解释器一次都没执行过它（短路 `JUMP_IF_TRUE` 直接跨过去了），所以它既不在 `covered` 里、也不是本流的功劳，单列 `IR-AOT-MIR-UNEXECUTED`。这是**语料的缺口**，不是 lowering 的缺陷；把它算进 covered 就是拿没跑过的 opcode 记账。

**诚实边界（作业单 §6 逐条）**

1. 字符串所有权语义是**沿用不是新建**：驻留标记来自 VM 既有约定，本步不引入引用计数、也不解决「谁释放非驻留字符串」——AOT 产物今天没有释放路径。非驻留字符串要等 `OP_CONCAT`，那一步才产生新字符串，必须给释放策略。
2. `OP_LOADK_STRING` 通了**不等于字符串语义通了**：`say` 打印、`==` 比较、`concat` 拼接是三件事，本步只交付第一件。
3. C 类 14 个 opcode 与值表示无关（线程/调度/异常），本步不碰。
4. 生成代码里那份值类型是**结构兼容的拷贝**，不是 `src/vm/vm.h` 那一份；两边靠 `same-as-interpreter` 与 6 条布局断言绑住，不靠 `#include`。
5. Windows 半侧：本步只动 lowering 与 preamble 文本，产物在 Windows 上未运行（沿用既有边界）。
6. 新增边界：**一个打印出口覆盖不到的东西**——`ival != 1` 的字符串。本子集没有 opcode 能造出它，所以遇到它 `mv_say` 选择中止而不是打印 `ival` 里的整数；等 `OP_CONCAT` 落地时，这里必须先有决定。
7. 新增边界：`IR-MIR-LAYOUT-ASSERT` 读的是**生成出来的文本**，不是编译器真的求过值的那 6 条断言。它抓的是「模板丢了一条」，抓不到「`offsetof` 与真实布局在别的编译器/别的 ABI 上不一致」——后者只有编译失败能抓，且只有构建过才知道。

## §10.104 「默认值是整数 0」是一次假等价：`MV_NIL` 与未解析全局（aot-mir-value，A 类第二项）

### 这笔债的来源与日期

**来源**：`kMirValBlock` 落地寄存器类型的那一步。生成文件里全局默认写的是 `static MirVal g_x = { MV_INT, 0, 0 };`，而解释器把每个全局建成 `VAL_NIL`（`src/vm/vm.c` 造全局表时 `value_set(..., VAL_NIL, ...)`）。**日期**：2026-11-05 本流把它改成 `{ MV_NIL, 0, 0 }`，并在这一节把「曾是假等价」写下来。

为什么这是个**假**等价而不是小瑕疵：0 与 nil 只在「假值」这一件事上一致。实测（把 `mv_nil` 的 tag 临时改成 `MV_INT` 的那一刻，也就是「默认值曾是整数 0」的等价物）：`say nosuchglobal == 0` 解释器是 `false`、生成文件是 `true`；`say nosuchglobal ?? 2` 解释器打 `2`、生成文件打 `0`；`say nosuchglobal` 解释器打 `nil`、生成文件打 `0`。而 `say nosuchglobal == nil` 两边都打 `true`——**恰恰因为 nil 被当成了同一个 0，这一条看不出差别**，能看出差别的永远是「拿 nil 去当 0 用」的那一半（`== 0`、`??`、`say`）。凡是用比较结果的程序都在比较两个不同的东西，而当时的判据一条都看不见它——因为 MIR 路线在 **prescan 就拒译**了：

```
IR-MIR-REFUSED|why=illegal-operand|detail=container=0 insn=0 global index -1 out of range (count=23)
```

拒绝把「差异」改写成「看不见」：程序连编都编不出来，`same-as-interpreter` 里当然也没有它。所以本流的三笔顺序是判据先行。

### nil 的真入口是「未解析全局」

这个语言**没有 nil 字面量**：`nil` 和从未赋值的名字一样，都编译成 `OP_LOAD_GLOBAL` 且 `r2 = -1`（`-1` 是编译器对「未解析」的拼法），解释器对该索引明确回 `VAL_NIL`。所以「nil 语义」的判据必须同时覆盖 `-1` 这条加载路径与 `OP_IS_NIL` 这条运算符路径。

`OP_IS_NIL` 在编译器里有 **4 个发射点**（`grep -n OP_IS_NIL src/compiler/` 实测）：`src/compiler/compiler.c:648` [obs: 2979f55]（`TOK_QUESTION`，即 `??`）、`:578`（`EXPR_SETCOMP`，`OP_CALL_BUILTIN list` 之后）、`:1082`（`EXPR_MEMBER` 的安全成员 `?.`）、`:1359`（嵌套 `INDEX_GET` 的中间层判 nil）。后三处各自要发 `OP_CALL_BUILTIN` / `OP_INDEX_GET` / `OP_NEW_ARRAY` 这类本子集不可发射的 opcode，所以**能被本子集接受并运行的**程序里，`OP_IS_NIL` 只可能来自 `??`（`x ?? y`）；另外三种写法是整程序被拒译，而不是走到 `OP_IS_NIL`。四处行号是在 `2979f55` 上读的、本流没有碰 `src/compiler/`，所以写成 record 而不是 pointer——这条线上 pointer 计数已经贴着上限，一格余量都没有。

### 三笔

| 笔 | 内容 | 反向判据 |
| --- | --- | --- |
| 一 | `MIR_ONLY` 加三个用例（`mir_nil_vs_zero` / `mir_nil_coalesce` / `mir_nil_print_truthiness`），按字节比 stdout、并印 `stdout-hex` | 实现前必须是红的：`IR-MIR-MIR-ONLY|case=mir_nil_vs_zero|mir=refused|why=illegal-operand` + `IR-MIR-VERDICT|FAIL`（实测如此） |
| 二 | `MV_NIL` + `mv_nil`/`mv_is_nil` + 四个 nil 直臂；prescan 接受 `LOAD_GLOBAL idx == -1`；`OP_IS_NIL` 走 `mv_is_nil`；全局默认改 nil | 把 `idx == -1` 的接受改回拒译 → 红回同一句点名 `global index -1 out of range` |
| 三 | 读数同步（pin 重记） | — |

`idx < -1` 仍是拒译且点名具体值：它不可能来自编译器，来自它就说明上游坏了。`OP_STORE_GLOBAL` 也不接受负索引——赋值会**创建**全局，而这条路线没有可创建的表。

### 为什么 nil 不是第五个打印出口

打印仍走 `nv_say` 语义：`mv_say` 里 nil 只多一个分支（打印解释器自己也打的字面 `nil`），其余一切照旧委托。`not` 也没有自己的臂——`mv_not = mv_boo(!mv_tru(a))`，`vm_truthy` 仍是唯一的真假规则；`mv_eq`/`mv_ne`/`mv_tru` 各有恰好一个 nil 臂，因为 `nv_*` 根本没有 nil 可交。**臂的数量是 3 个适配器（`mv_eq` / `mv_ne` / `mv_tru`）+ 1 个打印分支（`mv_say`）= 4 个 nil 臂**（`mv_is_nil` 是判据本身，不是 nil 值上的适配），每个都在注释里带可判定的删除条件（`nv_say` 同时学会字符串与 nil，或 P3 完成后只剩一个打印器；判据是 `MV_NIL` 在该块内计数归零）。

### 读数（本流 worktree 实跑，逐字）

```
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=6|mir-only-same-as-interpreter=6
IR-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=32|of=67|unknown=35|table=32|emitter=32|reported=32|agree=yes
IR-AOT-MIR-BEYOND|ops=(OP_IS_NIL,OP_LOADK_STRING)|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=92|of=29|covered=29|missing=0|refused=0|same-as-interpreter=92|prelude-excluded=yes|beyond-ast-denominator=2
```

`tree=` 三处填的是本流笔二的提交 sha，此处留字段名而不写死：**一份文档不能含有装载它自己那次提交的哈希**，写死就会在改一个字之后变成一句假话（两条 pin 的 `tree` 字段是同一个问题的另一面，它们由脚本在干净的树上重记，不是手写）。

`handles` 31 → 32（`OP_IS_NIL`；`OP_LOAD_GLOBAL` 本来就在表里，接受 `-1` 不新增 opcode），`unknown` 36 → 35。`reasons` 三个桶的**名字与计数都没变**，这是必须守住的：新增覆盖不许动分词逻辑。`same-as-interpreter` 89 → 92（三个 nil 用例）。

### 诚实边界

1. `nil` 的**算术与比较**（`nil + 1`、`nil < 1`）解释器抛 `type_mismatch`，本路线没有异常机制，于是 `mv_as_nv` 中止。中止是响的不是对的；关掉它要 C 类异常 opcode。
2. `nil` 作为**容器**（`INDEX_GET`、dict、list、函数返回值）不在本子集，本步不定义。
3. 生成文件里的全局**默认**是 nil 了，但 `OP_STORE_GLOBAL` 仍只认同一个已存在的全局名；「赋值创建全局」这件事这条路线没有。
4. `MV_NIL` 的 tag 号 5 来自 `Value`（`src/vm/vm.h`），与 `NV` 的三 tag 无关；`mv_tag_to_nv` 对它默认 `abort()`，这是**故意**的第四个守卫。
5. 判据只认三个用例。它们是反例族里最小的三个，不是全集：`??` 之后接算术、nil 进函数参数都没有用例，属下一格。
6. **顺带量到的一个活洞（本笔不修，留给下一格）**：`s = "hi"` / `t = "ho"` / `say s == t` 编译成 `OP_EQ r2/r3`，prescan **接受**，生成的 C 运行 `RUN_RC=134`（SIGABRT）——`mv_eq` 没有 `MV_STR` 臂，`mv_as_nv(MV_STR)` 走 `mv_tag_to_nv` 的 `abort()`；而同一比较的常量形式 `s == "hi"` 走 `OP_EQK` 被 prescan 拒译（`NOT-YET`）。即「常量形式安全拒、变量形式接受后崩」的不对称。解释器对前者打印 `false`。本子集还没有字符串相等语义（`OP_LOADK_STRING` 只承诺「驻留、零分配、可打印」），这一条是下一格的入口，不是本笔的读数。


## §10.105 字符串比较：`mv_eq` 的 `MV_STR` 臂，与「常量形式仍拒译、变量形式现在正确」这处不对称（aot-mir-value，A 类第三项）

§10.104 的诚实边界第 6 项把下面这条洞写成了「下一格的入口」。本节是那一格：它不是新发现，是**已经量到、已经记下来、然后按「实现而不是拒译」关掉**的那一条。

### 一、复现（修之前的原始读数，2026-11-06）

```im
s = "hi"
t = "ho"
say s == t
```

修复前的三句话，逐条：

- 解释器有明确答案：`./build/inimerse eq.im` → 打印 `false`，`INTERP_RC=0`。
- MIR 路线**接受**：`./build/aot-mir translate eq.im eq.c` → `TRANSLATE_RC=0`，`IR-MIR-CONSERVE|insns=9|emitted=9|holds=yes|bytes=186`、`IR-AOT-MIR-OK`。
- `cc -O2` 通过（`CC_RC=0`），**然后运行时死**：`RUN_RC=134`（`Aborted` / SIGABRT），**stdout 为空**。

即「程序被接受、编译通过、跑起来崩，而解释器早就答了」。这比拒译更坏：拒译是诚实的，这个是路线替一个**已知答案**报了错，而且报的是一个信号的形状，不是一句话。

**根因与来源**：`MV_STR` 是 `Value` 的 tag 5（`src/vm/vm.h`），`nv` 只有三个 tag（int/double/bool）。`mv_eq` 只有 nil 臂，于是 `mv_as_nv(MV_STR)` 落到 `mv_tag_to_nv` 的 `default: abort()`。那个 `default` 是 **2026-11-06 上线的 `MirVal` 布局那笔（提交 `8b796dd`，"aot_mir: registers hold MirVal, the Value layout by name"）**引入的，它的本意是「tag 表上不可能出现的值一律停」——§10.104 诚实边界第 4 项当时就是这么写的。**守卫本身没错，错的是它替一个能被执行的 tag 兜了底**：`OP_LOADK_STRING` 能造出 `MV_STR`，而 prescan 接受 `OP_EQ`，于是「不可能态」这个前提在字符串身上是假的。判据不成立时，`abort()` 就从「守卫」变成了「缺失分支的替身」。

### 二、裁定：实现，不是拒译

`val_eq` 对字符串的规则是明确的、写在解释器里的，而且**先比指针再比内容**：

| 情形 | 解释器规则（`src/vm/vm.c` 第 413–428 行，`val_eq`） | 本节照抄成 |
| --- | --- | --- |
| 同型、都是字符串 | `a->sval == b->sval` 为真即 `true`，否则 `strcmp`，空指针归一成 `""`（第 417 行） | 指针快路 → `strcmp` |
| 字符串 vs 数值 | 同型块（第 414 行）挡住，数值强转块（第 425–427 行）只认 INT/FLOAT/BOOL 的组合，其余走 `return false`（第 428 行） | 先按**类型**分流，非字符串一律 `false` |
| 字符串的真假 | `vm_truthy` 的 `default: return 1`（第 400 行） | 字符串恒真，空串也是真 |
| `!=` | 与 `==` 是同一次 `val_eq` 的取反（第 3552–3553 行） | `mv_ne` 的串臂 = `!mv_tru(mv_eq(a,b))`，内容规则只有一个所有者 |

语义是明确的，所以拒译等于把「已经存在的答案」藏在 `unsupported-opcode` 后面。但边界写死，就三件事：

- **只对驻留字符串做 `strcmp`**（`ival == 1 && sval`）；这条路线里 `mv_str` 只做驻留，造不出非驻留串，真遇到就 `abort()`——那是**不可能态的守卫**，不是缺失分支的能力，注释里逐字写着。
- **先类型、后内容**：字符串不塞进数值比较路径。`s == 1` 是 `false`、`s != 1` 是 `true`，这不是猜的，是上表第 414/425–428 行。
- **零分配、不新开打印出口**：`strcmp` 读的是驻留字面量，不拷贝。

### 三笔

| 笔 | 内容 | 反向判据 |
| --- | --- | --- |
| 一（`b10ae79`） | `MIR_ONLY` 加三个用例：`mir_loadk_string_eq`（四个真值：`==`/`!=` 各两种）、`mir_loadk_string_eq_number`（字符串对 int 与 float）、`mir_loadk_string_truthiness`（`if`/`not`） | 实现前必须是红的，实测 `IR-MIR-EXEC-DIFF|case=mir_loadk_string_eq|interp-rc=0|mir-rc=-6|interp-out=b'false\ntrue\ntrue\nfalse\n'|mir-out=b''` + `IR-MIR-VERDICT|FAIL` |
| 二 | `mv_eq`/`mv_ne` 各**恰好一个**串臂、`mv_tru` 一个串臂，带债务注释（WHY / ADDS / NOT COVER / DELETE CONDITION / MEASURED BY） | 把内容比较改成恒 `true` → 只有 `mir_loadk_string_eq` 变红（`b'true\ntrue\nfalse\nfalse\n'` 对 `b'false\ntrue\ntrue\nfalse\n'`），另两例仍 `same`；把串臂删掉 → 红回 SIGABRT 形态（`mir-rc=-6`、`stdout-hex=` 空）；两次注入都只动 MIR 侧、解释器基线现场重算，是**非对称**的 |
| 三 | 读数同步 + 本节 | — |

反向判据判据本身也量过不对称：**「把内容比较改成恒 true」时 `s == s` 仍是 `true`**（指针快路还在），红的是 `s == t` 这一格；`mir_loadk_string_eq_number` 与 `mir_loadk_string_truthiness` 保持 `same`。这说明这条判据咬的是**内容比较**这一格，而不是「字符串这条路线整体」，也就说明它不是靠一个大范围故障同时污染期望与实际。

### 字符串臂（生成文件里逐字长这样）

```c
static inline int mv_tru(MirVal a) {
    if (a.type == MV_NIL) return 0;   /* vm_truthy: VAL_NIL is false, and only it */
    if (a.type == MV_STR) return 1;   /* vm_truthy's `default: return 1` ... */
    return nv_tru(mv_as_nv(a));
}
static inline MirVal mv_eq(MirVal a, MirVal b) {
    if (a.type == MV_NIL || b.type == MV_NIL) return mv_boo(a.type == b.type);
    if (a.type == MV_STR || b.type == MV_STR) {
        if (a.type != b.type) return mv_boo(0);   /* val_eq:414/425-427/428 */
        if (a.ival != 1 || b.ival != 1 || !a.sval || !b.sval) abort();  /* guard */
        if (a.sval == b.sval) return mv_boo(1);   /* val_eq:417 pointer fast path */
        return mv_boo(strcmp(a.sval, b.sval) == 0);
    }
    return mv_from_nv(nv_eq(mv_as_nv(a), mv_as_nv(b)));
}
static inline MirVal mv_ne(MirVal a, MirVal b) {
    if (a.type == MV_NIL || b.type == MV_NIL) return mv_boo(a.type != b.type);
    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(!mv_tru(mv_eq(a, b)));
    return mv_from_nv(nv_ne(mv_as_nv(a), mv_as_nv(b)));
}
```

**删除条件（可判定）**：P3 那笔把 `Value` 的比较语义搬进 IR、并给共享的 `NV` 一个字符串 payload 时删掉这三个臂；判据是机械的——`kMirValBlock` 内 `grep -c 'a.ival != 1'` 归零，且 `MV_STR` 不再出现在该块除 tag `#define`、`mv_str()`、`mv_say` 打印分支之外的地方。删早了（`NV` 还没有字符串载荷时）等于让 emitter 产出的值重新没有答案，也就是本节开头那个信号。

### 读数（本流 worktree 实跑，逐字）

```
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=9|mir-only-same-as-interpreter=9
IR-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=32|of=67|unknown=35|table=32|emitter=32|reported=32|agree=yes
IR-AOT-MIR-BEYOND|ops=(OP_IS_NIL,OP_LOADK_STRING)|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR|accepted=95|refused=0|same-as-interpreter=95|of=29|emittable=32|of-ops=67|not-emittable=35|corpus=inline-cases:86|mir-only-cases=9|beyond-ast-denominator=2
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
IR-AOT-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=95|of=29|covered=29|missing=0|refused=0|same-as-interpreter=95|prelude-excluded=yes|beyond-ast-denominator=2
```

`tree=` 仍留字段名而不写死，理由同 §10.104：一份文档不能装载它自己那次提交的哈希。

三个必须守住的不变量，逐条：

- `same-as-interpreter` **92 → 95，只涨没掉**（三个新用例，每个一条）。拒绝的仍然是 0。
- `handles` **32 不变**：`OP_EQ`/`OP_NEQ` 本来就在 lowering 表里，字符串不是新 opcode；为了涨数字加 case 是本节明确不做的事。
- `reasons` 三个桶**名字与计数逐字不变**（`identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4`）：新增语义不许动分词。`covered` 仍 `29/29`，分母是 AST 语料，没有动。

### 仍然存在、明确记录的不对称（不是默默留着）

**常量形式仍拒译，变量形式现在正确。** `s == "hi"` 与 `"hi" == "ho"` 由编译器发成 `OP_EQK`（`src/compiler/compiler.c` 第 703–716 行），本子集没有这个 opcode，prescan 给 `IR-MIR-NOT-YET|op=OP_EQK|n=1` + `IR-MIR-REFUSED|why=unsupported-opcode`——**安全拒译**；而 `s == t` 走 `OP_EQ`，现在打印与解释器逐字节相同的 `false`。同一个问题，两种拼写，一个答案在、一个答案没有。

本节选择**记录**而不是顺手修，理由是可判定的三条：①本笔的边界是「`mv_eq`/`mv_ne` 各恰好一个串臂」，加 `OP_EQK` 是给 lowering 表加一个 opcode，那是另一个决定（它会让 `handles` 从 32 变 33，而本节把「`handles` 不变」写成了不变量）；②`OP_EQK` 需要自己的判据（常量在左、常量在右、字符串常量与数值常量的 `OP_EQK`/`OP_NEQK` 两两组合，目前一条用例都没有）；③它现在是**拒译**而不是崩溃，所以它不是「响亮但不对」，坏处有限。**下一格**：要么给 `OP_EQK`/`OP_NEQK` 实现（规则就是本条 `val_eq`，`mv_eq` 已经在，`EQK` 只是把常量装进寄存器再走同一条路），要么把它们也写进拒译理由表并给出判据；两条都可以，**留着不说不行**，这就是本节存在的第二理由。

### 诚实边界

1. **字符串的序关系（`s < t`、`<=`、`>`、`>=`）仍是活洞，本笔不修**。解释器对它有规定（`val_orderable` 在第 443 行说字符串对可比，第 449–451 行先比指针再 `strcmp`），但 `mv_lt`/`mv_le`/`mv_gt`/`mv_ge` 仍直接委托 `nv_*`，所以 `s < t` 被 prescan **接受**、编译通过、运行时 SIGABRT（`RUN_RC=134`），而解释器打印 `true`/`false`。**形状与本笔开头那条一模一样**，是下一格的入口。它没在本笔关掉，是因为「恰好一个臂」的边界只覆盖相等与真假；把它顺手塞进来会让这一笔同时改两族语义、一条反向判据管两个结论。
2. **非驻留字符串**（`ival != 1` 或 `sval` 为空）在串臂里 `abort()`。这是守卫不是能力：`mv_str` 只做驻留，本路线造不出来；真造出来了，宁可停也不读 `sval` 里碰巧躺着的东西。
3. **字符串与容器**（阵列/dict/set 的比较、字符串做 set/dict 的 key）、大小写折叠、Unicode 归一、locale collation **一概不覆盖**。这些 opcode 不在本子集。
4. `mv_tru` 的串臂**没有驻留守卫**，因为它不读载荷——`vm_truthy` 的 `default: return 1` 对任何字符串 tag 都成立，加守卫只会在一个不需要判据的地方造一条假规矩。
5. 判据只认三个用例，是反例族里最小的三个，不是全集：空串与非空串的 `!=`、两个内容相同但不同字面量的串（`s == "h" + "i"` 之类当前语法也到不了）、`not (s == t)` 的组合都没有用例，属下一格。
6. 这一笔**没有**新增打印出口（`mv_say` 未动）、没有新增 `reasons` 桶、没有动 `covered` 的分母，也没有为了让读数好看而加 lowering case。

## §10.106 「返回值默认是整数 0」：同一个假等价在 `return` 这条路径上（aot-mir-value，A 类第四项）

### 一、这笔债的来源与日期

**来源**：`src/compilation/aot_mir.c` 里 `emit_one` 的 `OP_RETURN` 臂。它第一次出现是 **`1dec5e6`（2026-11-01，"aot_mir: a C translation unit generated from the IrInsn stream"）**，当时写的是 `return nv_int(0)`；**`8b796dd`（2026-11-05，"registers hold MirVal"）**把同一个位置原地改成 `return mv_int(0)`——**只换了字形，换的是同一个答案**。本流 **2026-11-06** 把它改成 `return mv_nil()`，并把「曾是假等价」写在这里。

**与 §10.104 的关系**：同一类假等价，不同的路径。§10.104 修的是**全局**的默认值（生成文件里 `static MirVal g_x = { MV_INT, 0, 0 };`，解释器把每个全局建成 `VAL_NIL`）；本节修的是**返回值**。两处的错误机制都是「把一个没有值的位置读成整数 0」，区别只是那个位置叫 `g_x` 还是叫函数掉出末尾。

**与零初始化的关系（这是它为什么安静的技术原因）**：生成函数开头是 `for (int i = 0; i < nregs; i++) v[i] = mv_int(0);`，而 `mv_from_nv` 把 tag 抄进 `.type`、把 `.sval`/`.ptr` 清零；`MV_INT` 的 tag 数值恰好是 **0**。所以**一个从没写过的寄存器读出来就是整数 0**，旧臂的 `mv_int(0)` 与「读一个空寄存器」在字节上不可区分。换句话说：这不是「忘了写一个分支」，而是「寄存器形状本身让一个缺失的分支看起来像有答案」——这就是 `docs/STATUS.md` §10.104 记的那种假等价在返回值路径上的同一张脸。

### 二、复现（修之前的原始读数，2026-11-06）

```im
func n() { x = 1 }
say n()
```

- 解释器：打印 `nil`，`INTERP_RC=0`。
- MIR 路线**接受并编译**：`TRANSLATE_RC=0`，`IR-MIR-CODEGEN|…|regs-max=3|emits=32|generated=(OP_MOV,OP_LOADK_INT,OP_SAY,OP_HALT,OP_CALL_FUNC,OP_RETURN)`，`IR-MIR-CONSERVE|insns=6|emitted=6|not-yet=0|holds=yes|bytes=134`，`IR-AOT-MIR-OK`。
- 生成体（今天逐字；修之前这里两处都是 `return mv_int(0);`，`IR-MIR-CONSERVE` 的 `bytes` 由 134 变成 133）：

```c
static MirVal fn_n(void) {
    MirVal v[3];
    for (int i = 0; i < 3; i++) v[i] = mv_int(0);
L0:
    v[2] = mv_int(1LL);
    v[1] = v[2];
    return mv_nil();
L3: ;
    return mv_nil();
}
```
- `cc -O2` 通过（`CC_RC=0`），**运行 rc=0、stdout 是 `0`**。两边退出码都是 0——**没有任何人看得见这个错**，除了 stdout 的那一个字节。
- 实现前的判据红原文（`tools/aot_mir.test.py` 的 `mir_implicit_return` 用例）：

```
IR-MIR-EXEC-DIFF|case=mir_implicit_return|interp-rc=0|mir-rc=0|interp-out=b'nil\nnil\nnil\n'|mir-out=b'0\n0\n0\n'
IR-MIR-MIR-ONLY|case=mir_implicit_return|mir=accepted|cc=ok|exec=diff|rc=0|bytes=6|stdout-hex=300a300a300a
IR-MIR-VERDICT|FAIL|tree=2f71413…|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

三个形状在同一个用例里：`func n() { x = 1 }`（非空体掉出末尾）、`func m() { }`（空体）、`func b() { return }`（裸 `return`）。三种形状是**同一个 opcode 形状**：编译器对它们都发 `OP_RETURN` 且 `r1 == 0`（实测 6 条指令、最后一条 `OP_RETURN r1=0`），所以它们一起红、一起绿；一个只带空体的用例会说不出「非空体有同一个洞」这句话。

### 三、裁定：显式返回 nil，不靠「寄存器恰好是 0」

规则不是本节发明的，是照抄解释器。`src/vm/vm.c` 第 4123–4130 行的 `L_RETURN`：`if (ins.r1 > 0 && ins.r1 < FRAME_REGS) ret = R[ins.r1]; else { ret.type = VAL_NIL; ret.ival = 0; ret.sval = NULL; }`。**裸 `return` 与「掉出末尾」在解释器里共用同一个 else 分支**，所以两者的答案是同一个 nil。

因此这一笔只改两个发射点、不新增概念：

- `emit_one` 的 `OP_RETURN` 臂：`r1 == 0` 时发射 `return mv_nil();`（`r1 > 0` 的 `return e` 原样不动——它一直是对的）。
- `emit_container` 的非 main 尾声：同样发射 `return mv_nil();`。它是「函数体最后一条不是 `OP_RETURN`」的兜底；**兜底答错就不算兜底**，所以两处同笔。
- 寄存器初始化 `v[i] = mv_int(0)` **保留**：它让「读一个没写过的寄存器」保持确定；本节只让**返回值**不再依赖它。
- main 的 `return 0;` 不动：那是 `int main(void)` 的**进程退出码**，不是 Inimerse 值，也不经过 `MirVal`。

债务注释写在 `emit_one` 里该臂的正上方（WHY / 为什么不能靠"寄存器已经是 0" / ADDS / NOT COVER / 可判定 DELETE CONDITION / MEASURED BY），删除条件是「P3 把 nil 搬进 IR、该臂改发射 `mv_from_nv(nv_nil())`」那一笔：机械判据是对 `src/compilation/aot_mir.c` 用 grep 查"函数返回整数 0 的生成串前缀"零命中（注释本身不写全该 pattern，所以不会自匹配）；任何命中都说明旧的发射点还在。

### 四、反向判据（不对称）

把发射的 `return mv_nil()` 改回 `return mv_int(0)`（只改 `OP_RETURN` 臂这一处，解释器基线现场重算，两侧不同时动），重建 `BUILD_EXIT=0` 后判据**真红**：

```
IR-MIR-EXEC-DIFF|case=mir_implicit_return|interp-rc=0|mir-rc=0|interp-out=b'nil\nnil\nnil\n'|mir-out=b'0\n0\n0\n'
IR-MIR-VERDICT|FAIL|…
```

还原后 `grep -c INJECTED` 为 0、`BUILD_EXIT=0`、该用例回到 `exec=same|bytes=12|stdout-hex=6e696c0a6e696c0a6e696c0a`（`nil\nnil\nnil\n`），`IR-MIR-VERDICT|PASS`。这证明判据咬的是**返回值这一个答案**，不是字符串路线或布局断言整体故障。

### 五、读数（本流 worktree 实跑，逐字，笔一之后）

```
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=10|mir-only-same-as-interpreter=10
IR-MIR-VERDICT|PASS|tree=<笔一 sha>|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=32|of=67|unknown=35|table=32|emitter=32|reported=32|agree=yes
IR-AOT-MIR-BEYOND|ops=(OP_IS_NIL,OP_LOADK_STRING)|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR|accepted=96|refused=0|same-as-interpreter=96|of=29|emittable=32|of-ops=67|not-emittable=35|corpus=inline-cases:86|mir-only-cases=10|beyond-ast-denominator=2
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
IR-AOT-MIR-VERDICT|PASS|tree=<笔一 sha>|accepted=96|of=29|covered=29|missing=0|refused=0|same-as-interpreter=96|prelude-excluded=yes|beyond-ast-denominator=2
```

`tree=` 仍留字段名而不写死，理由同 §10.104：一份文档不能装载它自己那次提交的哈希。

必须守住的不变量：`same-as-interpreter` **95 → 96**（只涨没掉，一个新用例一条），拒绝仍 0；`handles` **32 不变**（`OP_RETURN` 本来就在 lowering 表里，nil 不是新 opcode）；`reasons` 三个桶**名字与计数逐字不变**；`covered` 仍 `29/29`。`mir-only-cases` **9 → 10**。

### 六、诚实边界

1. `return e` 路径**未动**且一直正确；本节只改 `r1 == 0` 的两处发射。裸 `return` 与掉出末尾同形同答案，这是解释器第 4123–4130 行同一个分支说的，不是本节定的。
2. **生成 main 的结尾仍是 `return 0;`**：那是进程退出码。若有人把两处 `return 0` 当成同一个洞去"顺手统一"，会把退出码改成 nil，那是另一个决定。
3. 寄存器初始化仍是 `mv_int(0)`：万一将来有 opcode 读一个从没写过的寄存器，它仍表示整数 0。当前子集没有这样的读；改它属于另一个决定，不在本节。
4. **字符串的序关系（`s < t`）仍是活洞**（§10.105 诚实边界第 1 项已记），本笔没碰，下一笔。
5. 判据只认三种形状，不是全集：`return` 之后还有指令（末尾不可达）、递归里的隐式返回、`return` 在 `if` 分支里、`??` 与隐式返回的组合都没有用例，属下一格。
6. 没有新增打印出口、没有新增 `reasons` 桶、没有为了让读数好看而加 lowering case。

## §10.107 字符串的序关系：四条臂，与「混合类型的序仍是未定义的 aborted 边界」（aot-mir-value，A 类第五项）

### 一、这笔债的来源与日期

**来源**：`src/compilation/aot_mir.c` 里的 `mv_lt`/`mv_le`/`mv_gt`/`mv_ge` 四条适配臂。数值比较的委托形态第一次出现是 **`1dec5e6`（2026-11-01，"aot_mir: a C translation unit generated from the IrInsn stream"）**，当时是 `nv_lt(a, b)` 直接进 `nv_*`；**`8b796dd`（2026-11-05，"registers hold MirVal"）**把它们原地包成 `mv_from_nv(nv_lt(mv_as_nv(a), mv_as_nv(b)))`——**指令位置没变，仍是四条一行委托，也仍是同一个洞**：`nv_*` 没有字符串 tag，`mv_as_nv` 把一个字符串硬塞进数值通道，撞上 `mv_tag_to_nv` 的 `default: abort()`。本节 **2026-11-06** 给这四条臂各加一个字符串分支，规则照抄解释器。

**与 §10.105 的关系**：同一类洞，同一条路线上紧挨着的下一格。§10.105 修的是 `==`/`!=`/真假的字符串臂，它的诚实边界第 1 项把 `s < t` 明确记成「仍是活洞、下一笔」；本节就是那一笔。**§10.106 第六节第 4 项（「字符串的序关系仍是活洞」）在本节关闭**——那句话是写它当时的记录，不改写历史。

### 二、复现（修之前的原始读数，2026-11-06）

```im
s = "hi"
t = "ho"
say s < t
```

- 解释器：打印 `true`，`INTERP_RC=0`。
- MIR 路线**接受并编译**：`TRANSLATE_RC=0`，`IR-AOT-MIR-OK`，`IR-MIR-CODEGEN|…|generated=(OP_MOV,OP_LOADK_STRING,OP_LT,OP_LOAD_GLOBAL,OP_STORE_GLOBAL,OP_SAY,OP_HALT)`，`IR-MIR-CONSERVE|insns=10|emitted=10|not-yet=0|holds=yes|bytes=204`。
- `cc -O2` 通过（`CC_RC=0`），**运行 `RUN_RC=134`（SIGABRT）、stdout 空**。
- 实现前的判据红原文（`tools/aot_mir.test.py` 的 `mir_loadk_string_order` 用例，十二条输出覆盖四条运算符、两个方向与等值对）：

```
IR-MIR-EXEC-DIFF|case=mir_loadk_string_order|interp-rc=0|mir-rc=-6|interp-out=b'true\ntrue\nfalse\nfalse\nfalse\nfalse\ntrue\ntrue\ntrue\ntrue\nfalse\nfalse\n'|mir-out=b''
IR-MIR-MIR-ONLY|case=mir_loadk_string_order|mir=accepted|cc=ok|exec=diff|rc=-6|bytes=0|stdout-hex=
IR-MIR-VERDICT|FAIL|tree=2f71413…|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

注意 `mir-rc=-6`：harness 把「被信号杀死」报成负的 signal 号，而这条路线是**先被接受、再在运行时死掉**的——比崩溃更糟的是它通过了 prescan 与 codegen 两道门。

### 三、裁定：实现，规则照抄 `src/vm/vm.c` 第 443 行与第 449–451 行 [obs: 2f71413]

不是本节发明的规则，是**读出来照抄**的：

- **`src/vm/vm.c` 第 443 行** [obs: 2f71413]（`val_orderable`）：同型字符串对**是可比较的**（即解释器对这个问句本来就有答案；拒译等于藏起一个已有的答案）。
- **`src/vm/vm.c` 第 449–451 行** [obs: 2f71413]（`val_cmp` 的同型字符串分支）：先比 `sval` **指针**（`if (a->sval == b->sval) return 0;`，第 450 行的快路），指针不同才 `strcmp`（第 451 行），NULL 载荷按空串读。

实现只加**一条内容规则**与**四个符号**，因为规则是一条规则、四条运算符只差问哪个符号：

```c
static inline int mv_str_cmp(MirVal a, MirVal b) {
    if (a.type != MV_STR || b.type != MV_STR) abort();  /* 混合类型：见下 */
    if (a.ival != 1 || b.ival != 1 || !a.sval || !b.sval) abort();  /* 不可能态守卫 */
    if (a.sval == b.sval) return 0;   /* val_cmp:450 指针快路 */
    return strcmp(a.sval, b.sval);    /* val_cmp:451 */
}
static inline MirVal mv_lt(MirVal a, MirVal b) {
    if (a.type == MV_STR || b.type == MV_STR) return mv_boo(mv_str_cmp(a, b) < 0);
    return mv_from_nv(nv_lt(mv_as_nv(a), mv_as_nv(b)));
}
/* mv_le / mv_gt / mv_ge 同形，符号换成 <= 0 / > 0 / >= 0 */
```

- **零分配**：`strcmp` 只读驻留载荷，不产生新串、不复制、没有所有权问题。
- **不加新打印出口**：本节的答案全部经由已有的 `mv_boo` 与 `say`。
- **NULL 归一化的那一半照抄不了也不假装照抄**：解释器 `val_cmp` 把 NULL 载荷读成空串，那是因为它允许 NULL；本路线的驻留守卫（`ival != 1 || !sval`）先停住，所以那个分支在这里**不可达**，所以与 `mv_eq` 一样写成 abort 而不是静默空串。
- 债务注释写在该 helper 正上方（WHY / ADDS / NOT COVER / 可判定 DELETE CONDITION / MEASURED BY）。删除条件是**实验而非愿望**：删掉 `mv_str_cmp` 与四条臂、恢复一行 `nv_*` 委托，重建后跑 `mir_loadk_string_order`——**绿**说明 `nv_*` 已经拥有该规则、臂可以永久删除；**红（SIGABRT）**说明 `NV` 仍无字符串载荷，提前删就是本节命名的那个崩溃。

### 四、边界：混合类型的序仍是未定义的 aborted 边界（**这是写死的**）

`s = "hi"` 之后 `say s < 1`，两条路线实测（2026-11-06）：

- 解释器：`[exception] uncaught: type_mismatch`，`INTERP_RC=1`。第 443 行说得很清楚：一对值可比较**当且仅当**「两个都是字符串」或「两个都是数值」。
- 本路线：`TRANSLATE_RC=0`、`CC_RC=0`、`MIR_RUN_RC=134`（SIGABRT）、stdout `0` 字节。

本路线**没有异常机制**（C 类那一格还没做），所以没有可能复现「拒绝」这个答案。两个假的替代答案都比一次停止更坏：

- 静默 `false`/`true` ⇒ 一个退出码为 0 的错答，正是 §10.106 刚刚关掉的那一类**静默**错答；
- 拒绝编译 ⇒ 把一个运行时类型事实挪到静态门禁，把「类型错了」说成「语法不支持」。

因此 `mv_str_cmp` 对混合对 **abort**：它是一条**未定义边界守卫**，说的是「这个问句在本路线上没有答案」，不是「少了某个分支」。这条边界写进了注释与本节，**不许**被后来的人用常量答案"修"掉。

### 五、反向判据（不对称，且第二段本身是一个诚实结论）

**(1) 内容规则注入**：把 `return strcmp(a.sval, b.sval);` 改成恒 `return 0;`（只动 `mv_str_cmp` 这一处，`grep -n` 先印原件在 402 行，解释器基线现算，两侧不同时动），`INJ_HITS=1`、`BUILD_EXIT=0` 后判据**真红**：

```
IR-MIR-EXEC-DIFF|case=mir_loadk_string_order|interp-rc=0|mir-rc=0|interp-out=b'true\ntrue\nfalse\nfalse\nfalse\nfalse\ntrue\ntrue\ntrue\ntrue\nfalse\nfalse\n'|mir-out=b'false\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse\ntrue\ntrue\ntrue\ntrue\nfalse\nfalse\n'
IR-MIR-VERDICT|FAIL|…
```

红色的内容读得出来：`<` 与 `>` 全部答 `false`、`<=` 与 `>=` 全部答 `true`——「所有字符串都相等」。

**(2) 删掉指针快路**（`if (a.sval == b.sval) return 0;` 那一行）重建后判据**仍然绿**：

```
IR-MIR-MIR-ONLY|case=mir_loadk_string_order|mir=accepted|cc=ok|exec=same|rc=0|bytes=66|stdout-hex=747275650a…66616c73650a
IR-MIR-VERDICT|PASS|…
```

这不是判据的缺陷被容忍，而是**判据能看见什么、不能看见什么的诚实记录**：快路是**代价**（`s <= s` 少一次 `strcmp`），不是**语义**（`strcmp("hi","hi") == 0` 给出同一个答案）。按字节比对答案的判据**结构上**看不见它。所以这段的结论写成两半：内容规则被注入会被咬住；快路被删不会被咬住，而后者是**故意接受**的——若要守住快路，需要的是一个计数/时序类判据，那是另一格。

还原后 `grep -c INJECTED` 为 0、`grep -n` 快路回到 403 行、`BUILD_EXIT=0`、用例回到 `exec=same|bytes=66`（十二条逐字 `true,true,false,false,false,false,true,true,true,true,false,false`），`IR-MIR-VERDICT|PASS`。

### 六、读数（本流 worktree 实跑，逐字，笔二之后）

```
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=11|mir-only-same-as-interpreter=11
IR-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=32|of=67|unknown=35|table=32|emitter=32|reported=32|agree=yes
IR-AOT-MIR-BEYOND|ops=(OP_IS_NIL,OP_LOADK_STRING)|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR|accepted=97|refused=0|same-as-interpreter=97|of=29|emittable=32|of-ops=67|not-emittable=35|corpus=inline-cases:86|mir-only-cases=11|beyond-ast-denominator=2
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
IR-AOT-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=97|of=29|covered=29|missing=0|refused=0|same-as-interpreter=97|prelude-excluded=yes|beyond-ast-denominator=2
```

必须守住的不变量：`same-as-interpreter` **96 → 97**（只涨没掉）；`handles` **32 不变**（`OP_LT/OP_LE/OP_GT/OP_GE` 本来就在 lowering 表里，字符串分支不新增 opcode）；`reasons` 三个桶**名字与计数逐字不变**；`covered` 仍 `29/29`；`mir-only-cases` **10 → 11**。

### 七、诚实边界

1. **混合类型的序没有答案**：`s < 1`、`1 < s`、`s < nil` 在本路线是 SIGABRT（守卫），不是 false、不是拒绝编译。这是本节第四节的全部内容，也是本节唯一"没有修好"的东西——因为它需要异常机制，不是需要一条比较规则。
2. **只有驻留串**：`ival == 1` 且 `sval` 非空是本节的前提；真出现非驻留串，`mv_str_cmp` 守卫 abort（不可能态），不静默当空串。
3. 没有 locale/Unicode 折叠：`strcmp` 比字节，与解释器 `val_cmp` 一致；解释器也没做折叠，所以这里不是"少做了一步"。
4. **快路是代价不是语义**（第五节第二段），判据看不见它；这条不对称被写下来而不是被藏起来。
5. 用例十二条输出覆盖四条运算符、两个方向、等值对，但**不是全集**：字符串 vs nil、字符串 vs 数组/字典、`strcmp` 前缀相同（"abc" vs "abd"）的中间字节差异、空串 `""` 与 `""` 相比都没有独立用例，属下一格。
6. 没有新增打印出口、没有新增 `reasons` 桶、没有为了让读数好看而加 lowering case；`OP_CONCAT` / 容器 / 异常 / 并发**一律没碰**（各自的下一格）。

## §10.108 字符串拼接：堆所有权第一次进入这条路线（aot-mir-value，A 类第六项）

### 一、这笔债的来源与日期

**来源**：`src/compilation/aot_mir.c` 里 `mv_add` 的一行委托 —— `mv_from_nv(nv_add(mv_as_nv(a), mv_as_nv(b)))`。数值 `+` 的委托形态第一次出现是 **`1dec5e6`（2026-11-01，"aot_mir: a C translation unit generated from the IrInsn stream"）**；**`8b796dd`（2026-11-05，"registers hold MirVal"）**把它原地包成 MirVal 形态 —— **指令位置没变、仍是一行委托、仍是同一个洞**：`nv_*` 没有字符串 tag，`mv_as_nv` 把一个字符串硬塞进数值通道，撞上 `mv_tag_to_nv` 的 `default: abort()`。本节 **2026-11-06** 给它加字符串分支。

**与 §10.105 / §10.106 / §10.107 的关系**：同一类洞，同一条路线上紧挨着的下一格。但这一格和前三格**不是同一类修法**：§10.105 的字符串来自 `OP_LOADK_STRING`（驻留字面量，`ival == 1`，零分配），§10.107 的序关系只读驻留串；本节第一次**造出**一个解释器里靠 `malloc` 才存在的答案，因此也是**堆所有权第一次进入这条路线**。这不是加固一个已有的不变量，是**引入一个新的不变量**，本节把它写下来：谁拥有、谁释放、什么时候释放、以及这些都不在寄存器里。

### 二、复现（修之前的原始读数，2026-11-06）

```im
s = "hi"
t = "ho"
say s + t
say s + "x"
```

- 解释器：打印 `hiho` / `hix`，`INTERP_RC=0`。
- MIR 路线**接受并编译**：`TRANSLATE_RC=0`、`IR-AOT-MIR-OK`、`IR-MIR-CODEGEN|insns=13|regs-max=3|emits=32|generated=(OP_LOADK_STRING,OP_ADD,OP_LOAD_GLOBAL,OP_STORE_GLOBAL,OP_SAY,OP_HALT)`、`IR-MIR-CONSERVE|insns=13|emitted=13|not-yet=0|holds=yes|bytes=277`。
- `cc -O2` 通过（`CC_EXIT=0`），**运行 `RUN_RC=134`（SIGABRT）、stdout 空**。
- 实现前（笔一 `08c1d6a` 的树）的判据红原文，用例 `mir_add_string_concat`：

```
IR-MIR-EXEC-DIFF|case=mir_add_string_concat|interp-rc=0|mir-rc=-6|interp-out=b'hiho\nhix\nxhi\nhihi\nhi\nhiho\nhihohi\nhi\nho\n'|mir-out=b''
IR-MIR-ALLOCS|case=mir_add_string_concat|reported=no|why=rc=-6
IR-MIR-PROBLEM|mir_add_string_concat: the MIR route printed b'' rc=-6, the interpreter b'hiho\nhix\nxhi\nhihi\nhi\nhiho\nhihohi\nhi\nho\n' rc=0
IR-MIR-VERDICT|FAIL|tree=08c1d6a2f8e61b5759a2631c1def0928e4a9bb2b|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

### 三、裁定：判据先立，仪器同一笔，实现一笔

**判据先立**（笔一 `08c1d6a`）。新的 `MIR_ONLY` 用例 `mir_add_string_concat` 只比 stdout 字节，覆盖七件事：`s + t`（两条不同字面量的拼接）、`s + "x"`（右操作数是字面量，左是变量）、`"x" + s`（方向反过来）、`s + s`（同一操作数两次，两侧同一指针）、`s + ""`（空右操作数）、`u = s + t` 之后 `say u` 与 `say u + s`（结果先落变量（走 `OP_MOV`）再被再拼一次）、以及**拼接不改操作数**（拼完之后再 `say s` / `say t`，两条原文）。混合类型（`s + 1`、`1 + "7"`）**不进**这条判据，理由见第七节第 1 项。

**仪器必须在实现之前或同一笔**（同一笔 `08c1d6a`）。判据按字节比 stdout，**结构上看不见分配与生命周期**：双释放／泄漏在它面前照样绿。所以笔一在生成的 TU 里加了 `IR-MIR-ALLOCS|allocs=N|freed=N|live=N|bytes=N`，由生成程序自己在退出时打到 **stderr**（stdout 必须保持与解释器逐字节可比，所以读数不能上 stdout），并在工具里断言：

- 行**必须存在**（缺失本身是红，不是静默的 0）；
- `live == 0`（退出清扫不许留下任何一块，红要**点名用例**并报出剩余块数与请求字节数）；
- `allocs|bytes` 与一张**预写死的表**逐字相等（`ALLOCS_EXPECT`），这条才是牙齿：`live = allocs - freed` 由同一次清扫自己算，所以一个**绕开登记**的 `malloc` 会报出干净的 `0|0|0`，只有「这个用例必须分配 7 次、33 字节」才抓得住它。

仪器自己也有反向判据（第五节）：把 `live == 0` 的断言改成恒过，或把登记那一行删掉，红要消失／出现，逐字贴出来。

### 四、实现（笔二）

**照抄的那几行**（`src/vm/vm.c` 的 `L_ADD` 字符串分支；本节只抄它，不发明规则）：

```
   :3291  if (a->type == VAL_STRING || b->type == VAL_STRING) {
   :3304      size_t la = strlen(sa), lb = strlen(sb);
   :3305      char *nb = malloc(la + lb + 1);
   :3306      if (!nb) nb = strdup("");
   :3307      else { memcpy(nb, sa, la); memcpy(nb+la, sb, lb); nb[la+lb] = 0; }
   :3308      value_set(&R[ins.r1], VAL_STRING, 0, 0, nb, NULL);
```

第三个参数**就是 `ival == 0`**：解释器对「算出来的串」用 `ival = 0`（堆拥有），对驻留串用 `ival = 1`（不拥有、不释放）。本路线沿用同一条约定，于是 `ival` 在两条路线里都只剩**驻留／释放标记**这一个含义。溢出检查**抄链式路径、不抄 `L_ADD` 的遗漏**：解释器的 `L_CONCAT` 在 `:3351` 查 `if (total > (size_t)-1 - l - 1)` 而 `L_ADD` 的 `:3305` 根本没查，本路线按两操作数拼出同一个表达式（`la > (size_t)-1 - lb - 1`）并 abort —— **抄检查、不抄遗漏**。

生成的 TU 里新增（全部在 `kMirValBlock` 内，追加不拼接，所以 `classify()` 的共同前缀与三个 preamble marker 不受影响）：

```c
static inline MirVal mv_str_concat(MirVal a, MirVal b) {
    if (!a.sval || !b.sval) abort();            /* no state this route builds */
    size_t la = strlen(a.sval), lb = strlen(b.sval);
    if (la > (size_t)-1 - lb - 1) abort();      /* copy chain's check, not L_ADD's omission */
    char *nb = mv_alloc(la + lb + 1);           /* registered, swept at exit */
    memcpy(nb, a.sval, la);
    memcpy(nb + la, b.sval, lb);
    nb[la + lb] = 0;
    MirVal r;
    r.type = MV_STR;
    r.ival = 0;      /* heap-owned: value_set(..., VAL_STRING, 0, 0, nb, NULL) */
    r.sval = nb;
    r.ptr  = 0;
    return r;
}
static inline MirVal mv_add(MirVal a, MirVal b) {
    if (a.type == MV_STR || b.type == MV_STR) {
        if (a.type != MV_STR || b.type != MV_STR) abort();   /* MIXED PAIR */
        return mv_str_concat(a, b);
    }
    return mv_from_nv(nv_add(mv_as_nv(a), mv_as_nv(b)));
}
```

**所有权方案：程序级登记表，不是 per-register 所有权。** 生成代码里每个寄存器写都是裸赋值 `v[i] = expr`，`emit_one` 有几十个发射点，没有 `value_set` 那样的唯一咽喉；若只让 `OP_ADD` 一处「拥有」，`OP_MOV`（`v[1] = v[2]`）会浅拷贝堆指针 ⇒ 两个拥有者 ⇒ 双释放。所以「拥有」不放在寄存器里，放在一张程序级链表里：`mv_alloc()` malloc 后把指针登记进 `mv_blocks`，`atexit` 注册的 `mv_free_all()` 在程序退出时一次性释放全部并打印读数。双释放因此在结构上不可能（每个块只被登记表释放一次），而代价是中间值一律活到程序退出（第七节第 4 项）。

**两处守卫同步放宽（可预期的联动，做在笔二并点名）**。`mv_eq` / `mv_str_cmp` / `mv_say` 原先靠「本路线造不出非驻留串」这个前提守住 `ival != 1 ⇒ abort`；拼接一存在，这个前提当场失效，所以三处守卫改成只查**载荷**（`sval` 非空），`ival` 只剩释放标记的作用。本节写下的判据只有 `mir_add_string_concat` 一条，但放宽本身另有实测（第七节末）：`s + t == c`、`s + t != d`、`s + t < c`、`w = s + t; say w == c; say w` 的 MIR stdout 与解释器逐字节相同（除解释器的模块加载前言），读数 `IR-MIR-ALLOCS|allocs=4|freed=4|live=0|bytes=20`。

### 五、反向判据（每一笔至少一次，非对称）

**笔一（仪器）**。注入 I1：删掉登记表那一行 `mv_blocks = e;`（块被分配、但永远扫不到），并在 `main` 前言后加一行 `mv_alloc(16)`（故意漏登记一次）。`BUILD_EXIT=0`、`MIR_EXIT=1`：

```
IR-MIR-ALLOCS|case=mir_loadk_string|reported=yes|allocs=1|freed=0|live=1|bytes=16|expect-allocs=0|expect-bytes=0|ok=no
IR-MIR-PROBLEM|mir_loadk_string: LEAK -- live=1 block(s) survived the exit sweep (allocs=1, freed=0, 16 byte(s) requested in all); the criterion says the sweep must leave none
IR-MIR-PROBLEM|mir_loadk_string: allocation count off -- got allocs=1|bytes=16, the criterion demands allocs=0|bytes=0 (a site that mallocs without registering reports 0|0)
```

注入 I2（在 I1 之上）：把 `if live != 0:` 改成 `if live > 999:`（把断言做成恒过）。重跑后 `IR-MIR-ALLOCS` 那行**逐字不变**（`live=1` 照旧），但 `grep -c "LEAK --"` 变成 **0**：**命名泄漏的是断言，不是读数**。这正是不对称的地方 —— 读数不会自己变红，断言是唯一的牙齿。

**笔二（实现）**。注入 A：把 `return mv_str_concat(a, b);` 改成 `return mv_int(0);`（把拼接答案做成常量）。`BUILD_EXIT=0`、`MIR_EXIT=1`：

```
IR-MIR-EXEC-DIFF|case=mir_add_string_concat|interp-rc=0|mir-rc=-6|interp-out=b'hiho\nhix\nxhi\nhihi\nhi\nhiho\nhihohi\nhi\nho\n'|mir-out=b''
IR-MIR-ALLOCS|case=mir_add_string_concat|reported=no|why=rc=-6
IR-MIR-PROBLEM|mir_add_string_concat: the MIR route printed b'' rc=-6, the interpreter b'hiho\nhix\nxhi\nhihi\nhi\nhiho\nhihohi\nhi\nho\n' rc=0
```

（红的原因不是「字节不同」四个字：`u` 成了整数，后面 `say u + s` 就是混合类型对，`mv_add` 的混合守卫 abort；stdout 因为 `abort` 不跑 `atexit` 而被整块丢弃 —— 这也顺带证明「守卫停止不打印读数」是设计而不是丢了输出。）注入 B：只删掉登记那一行 `mv_blocks = e;`（分配照旧、清扫扫不到）：

```
IR-MIR-ALLOCS|case=mir_add_string_concat|reported=yes|allocs=7|freed=0|live=7|bytes=33|expect-allocs=7|expect-bytes=33|ok=no
IR-MIR-PROBLEM|mir_add_string_concat: LEAK -- live=7 block(s) survived the exit sweep (allocs=7, freed=0, 33 byte(s) requested in all); the criterion says the sweep must leave none
```

这一次 **stdout 字节完全对**（七条答案逐字正确），只有 `IR-MIR-ALLOCS` 红 —— 这是「判据按字节比看不见分配与生命周期」的现场证据：同一次运行里，答案全对、泄漏 7 块 33 字节。还原后 `grep -c INJECTED` 为 0（两个文件各一次）、`BUILD_EXIT=0`、`IR-MIR-ALLOCS|case=mir_add_string_concat|…|allocs=7|freed=7|live=0|bytes=33|ok=yes`、`IR-MIR-VERDICT|PASS`、`IR-MIR-PROBLEM` 计数 0。

### 六、读数（本流 worktree 实跑，逐字，笔二之后）

```
IR-MIR-ALLOCS|case=mir_add_string_concat|reported=yes|allocs=7|freed=7|live=0|bytes=33|expect-allocs=7|expect-bytes=33|ok=yes
IR-MIR-MIR-ONLY|case=mir_add_string_concat|mir=accepted|cc=ok|exec=same|rc=0|bytes=39|stdout-hex=6869686f0a6869780a7868690a686968690a68690a6869686f0a6869686f68690a68690a686f0a
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=12|mir-only-same-as-interpreter=12
IR-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=32|of=67|unknown=35|table=32|emitter=32|reported=32|agree=yes
IR-AOT-MIR|accepted=98|refused=0|same-as-interpreter=98|of=29|emittable=32|of-ops=67|not-emittable=35|corpus=inline-cases:86|mir-only-cases:12|beyond-ast-denominator=2
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
IR-AOT-MIR-VERDICT|PASS|tree=<笔二 sha>|accepted=98|of=29|covered=29|missing=0|refused=0|same-as-interpreter=98|prelude-excluded=yes|beyond-ast-denominator=2
```

必须守住的不变量：`same-as-interpreter` **97 → 98**（只涨没掉）；`handles` **32 不变**（`OP_ADD` 本来就在 lowering 表里，字符串分支不新增 opcode）；`reasons` 三个桶**名字与计数逐字不变**；`covered` 仍 `29/29`；`mir-only-cases` **11 → 12**；`beyond-ast-denominator` 仍 2。

### 七、诚实边界

1. **混合类型的 `+` 仍是 SIGABRT，不是答案。** 解释器对 `s + 1` 走「两侧按同一规则串化」（那份注释自己写着 `1 + "7"` 曾经静默答 `"7"` 而 1 消失、exit 0）；照抄它就是在本路线新开一类**打印出口**，而且照抄的是一条历史上有静默错答的规则。所以 `mv_add` 对混合对直接 abort，并且**不进判据**（判据不许声称覆盖它）。
2. **空右操作数不短路。** `s + ""` 照样分配一份拷贝（判据按 3 字节记账）。短路成 `return a` 会把「驻留字面量」和「拥有块」混成同一个指针，清扫时 `free` 一个字面量 —— 这是结构性错误，所以宁可多分配 3 字节。
3. **OOM 与解释器分歧，写下来。** 解释器在 `:3306` 用 `nb = strdup("")` 静默给空串（而 strdup 自己也是 malloc，真没堆时它照样给 NULL），`s + t` 于是答出 `""` —— 静默错答、exit 0。本路线改为 abort（`mv_alloc` OOM 也 abort）：**分歧是故意的**，宁可无输出也不给一个错的串。
4. **登记表在循环里线性增长。** 它是记账不是所有权，所以循环里每拼一次就多一块、活到程序退出。实测（本节新增读数，本流 worktree）：`s = "x"` 后 `while i < 200 { s = s + "y" … }` ⇒ `IR-MIR-ALLOCS|allocs=200|freed=200|live=0|bytes=20500`；同样程序 `i < 1000` ⇒ `IR-MIR-ALLOCS|allocs=1000|freed=1000|live=0|bytes=502500`。字节是长度之和，所以 N 次拼接长串是 **O(N²／2)** 的滞留量（1000 次 ≈ 490 KiB 活到退出），**不是泄漏**（`live=0`），但也不是「零分配」——本路线的「零分配」不变量从此只在**没有 `+`** 的程序里成立。
5. **`OP_MOV` 浅拷贝共享指针。** `w = s + t` 让 `w` 与临时寄存器指向同一块；因为没有任何寄存器「拥有」，两个名字共享一个指针是安全的（不会双释放），代价是**覆盖寄存器不回收内存**：中间值只能等到退出。这也是为什么本方案能不做 per-register 所有权就敢落地。
6. **函数返回拥有串**。实测 `func jn(a, b) { return a + b }` + `say jn("hi","ho")` + `say jn(jn("a","b"),"c")` ⇒ stdout `hiho` / `abc`，`IR-MIR-ALLOCS|allocs=3|freed=3|live=0|bytes=12`。被调者的寄存器数组随 `return` 消失，块却仍被登记表持有 ⇒ 不悬垂、不双释放；代价同上：返回的串只能活到程序退出。
7. **per-register 所有权没有做，而且这是本节的 DELETE CONDITION。** 落地那笔要删除登记表与 `mv_free_all`，机械检查写成：生成 TU 里 `mv_free_all` 的引用计数为 0、`mv_set(` 出现次数等于寄存器写点数。
8. **`OP_CONCAT` 没碰。** 链式拼接（一次为整条链分配）是另一格，本节判据与本节任何一句话都不声称覆盖它；容器、异常、并发同样没碰。
9. **判据结构上看不见的那一半。** 字节判据看不见「谁拥有」，`IR-MIR-ALLOCS` 的精确 `allocs|bytes` 把「绕开登记」变成了红，`live` 把「登记了但没释放」变成红；但「登记了、释放了、却释放了两次」在这套记账里不产生任何读数（真实双释放要靠 glibc 的 abort 才看得见）——本节靠的是结构上不可能，不是靠判据看得见。

## §10.109 常量形式的字符串相等：`OP_EQK` / `OP_NEQK` 两条臂（aot-mir-value，A 类第七项）

§10.105 结尾那句「**常量形式仍拒译，变量形式现在正确。**」是一处**被写下、当时故意没修**的不对称（那一格的原话是「先记不顺手修」）。本节就是那一格：`s == t`（变量形式，`OP_EQ`）在 §10.105 已经被答对，`s == "hi"`（常量形式，`OP_EQK`）仍然停在 `unsupported-opcode`。§10.105 的叙述**不改写**——它是历史上的正确记录——本节把它覆盖为**已由本节关闭**，下面是关闭之后的读数。

WHY IT EXISTS，用一句话说清楚：**`op_regs` 是一个「哪些操作数是寄存器」的掩码**，而 `OP_EQK` 的 r1/r2 是寄存器、**r3 是字符串池下标**（不是寄存器）。登记进 lowering 表就等于承诺「这个 opcode 的每个操作数我都在 prescan 里验过、在 emit_one 里读对了」，而 §10.105 只给了一个三寄存器形状的 `OP_EQ`：常量形式的第三操作数换了**种类**，于是没有一条现成的检查能接住它，它就被留在了 `unsupported-opcode` 那边。判据也因此在「同一条规则、两种拼法」上不对称——一种被答对，一种被拒。

### 一、判据先行（实现之前，逐字）

`tools/aot_mir.test.py` 的 `MIR_ONLY`（第 379 行，[obs: d537593]）加四例，按字节比 stdout：

| 用例 | 源码 | 它咬的是什么 |
| --- | --- | --- |
| `mir_eqk_string` | `s = "hi"` / `say s == "hi"` / `say s == "ho"` | `OP_EQK` 的内容比较两向：真值一对、假值一对 |
| `mir_eqk_string_ne` | `s = "hi"` / `say s != "hi"` / `say s != "ho"` | `OP_NEQK` 是**另一条臂**，不是 `OP_EQK` 换个壳 |
| `mir_eqk_number` | `say 1 == 1` / `say 1 != 1` / `say true == true` | **必要条件**：数值/布尔常量的行为必须**不变**，否则就是「把整个 `OP_EQK` 写错」而不是「只加字符串臂」 |
| `mir_eqk_string_cross` | `s = "hi"` / `say "hi" == s` / `say "ho" == s` | 常量在**左边**：证明判据咬的是**方向**（哪一侧是池下标、哪一侧是寄存器） |

实现之前的原始行（`python3 tools/aot_mir.test.py --build build`，`IR-MIR-VERDICT|FAIL`）：

```
IR-MIR-MIR-ONLY|case=mir_eqk_string|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_eqk_string_ne|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_eqk_number|mir=accepted|cc=ok|exec=same|rc=0|bytes=16|stdout-hex=747275650a66616c73650a747275650a
IR-MIR-MIR-ONLY|case=mir_eqk_string_cross|mir=refused|why=unsupported-opcode
IR-MIR-PROBLEM|mir_eqk_string: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_EQK:2)
IR-MIR-PROBLEM|mir_eqk_string_ne: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_NEQK:2)
IR-MIR-PROBLEM|mir_eqk_string_cross: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_EQK:2)
IR-MIR-VERDICT|FAIL|tree=df7cfbd68607841e98e140451889cb8b1cb9add9|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

（`OP_EQK:2` / `OP_NEQK:2` 是这两例里各有两条比较指令，不是一条。）`mir_eqk_number` 在实现前就是绿的，这是**故意**的：它是必要条件，绿着才有资格说「我只加了字符串臂」。

### 二、两种拼法是同一条指令

实测字节码（`./build/inimerse --no-mods run selfhost/compiler.im --dump`，格式 `op,r1,r2,r3`）：

```
s == "hi"  ->  3,1,0,0 / 23,0,1,0 / 22,1,0,0 / 46,2,1,1 / 30,2,0,0 / 33,0,0,0
"hi" == s  ->  3,1,0,0 / 23,0,1,0 / 22,1,0,0 / 46,2,1,1 / 30,2,0,0 / 33,0,0,0
```

**逐字节相同**：编译器无论常量写在左还是右，都把字面量放进 r3、把另一边编成寄存器（`src/compiler/compiler.c` 第 700–719 行 [obs: df7cfbd]）。所以这两例在 lowering 侧是**同一个输入**，`mir_eqk_string_cross` 咬的不是「左右顺序」这个语法位置，而是**这条臂读对没读对 r2 与 r3 的角色**——第五节的反向判据 (b) 就红在它身上。

### 三、照抄解释器（一行一行）

解释器的规则是现成的、明确的（`src/vm/vm.c` [obs: df7cfbd]）：

| 情形 | 解释器（`src/vm/vm.c` 第 413–428 行，`val_eq` [obs: df7cfbd]） | 本节照抄成 |
| --- | --- | --- |
| 同型、都是字符串 | `a->sval == b->sval` 为真即 `true`，否则 `strcmp`，空指针归一成 `""`（第 417 行 [obs: df7cfbd]） | `mv_eq` 的串臂（指针快路 → `strcmp`），与 §10.105 同一条 |
| 字符串 vs 数值/布尔 | 同型块（第 414 行 [obs: df7cfbd]）挡住；强转块（第 425–427 行 [obs: df7cfbd]）只认 INT/FLOAT/BOOL 的组合；其余 `return false`（第 428 行 [obs: df7cfbd]） | 常量侧**恒为** `MV_STR`，所以跨类型的答案由 `mv_eq` 已有的类型分流给出，不在本节重新发明 |
| `OP_EQK` 的常量从哪来 | `int sidx = ins.r3;` … `vm_intern(vm, t->code->string_pool[sidx])`，带 `str_interned` 缓存（第 4159–4173 行 [obs: df7cfbd]） | `mv_str(c->node->string_pool[w3])`：**同一个池**，`mv_str` 只做驻留（`ival = 1`，见 `kMirValBlock`） |
| `OP_EQK` 比完写回 | `Value opv = R[ins.r2];` 然后 `value_set(&R[ins.r1], VAL_BOOL, val_eq(&opv, &sv) ? 1 : 0, …)`（第 4171–4172 行 [obs: df7cfbd]） | `v[r1] = mv_eq(v[r2], mv_str(...))` |
| `OP_NEQK` | 同一段抄一遍，最后**取反**：`val_eq(&opv, &sv) ? 0 : 1`（第 4175–4189 行 [obs: df7cfbd]） | `mv_ne(v[r2], mv_str(...))`；`mv_ne` 的串臂是 `!mv_tru(mv_eq(…))`，内容规则仍然只有一个所有者 |

`val_eq` 对字符串**先比指针再比内容**这条快路也照抄：`mv_str` 造出的常量来自池，`OP_LOADK_STRING` 造的运行时串也来自同一个池，同一条字面量在两个寄存器里是同一个指针，所以快路在这条臂上是真的会命中的，不是装饰。

### 四、实现（三处，各管一件事）

```c
/* src/compilation/aot_mir.c，op_regs：r3 是池下标，不是寄存器 */
case OP_EQK:
case OP_NEQK:
    return R1 | R2;                    /* 只声明两个寄存器；池界在 prescan 里验 */

/* prescan：池下标按「本容器」的池验，而不是按寄存器上限 */
case OP_EQK:
case OP_NEQK: {
    int32_t ix = ir_insn_r3(in);
    if (ix < 0 || ix >= node->string_count) return bad(...);
    if (!node->string_pool || !node->string_pool[ix]) return bad(...);
}

/* emit_one：两条 case 分开写，各自一行代码 */
case OP_EQK: {
    buf_fmt(b, "    v[%d] = mv_eq(v[%d], mv_str(\"", r1, r2);
    buf_c_string(b, c->node->string_pool[w3]);
    buf_str(b, "\"));");
    return 0;
}
case OP_NEQK: { … mv_ne … }
```

三处的分工是判据逼出来的，两处踩过的边记在这里，免得下一格再踩：

1. **`case OP_EQK: case OP_NEQK:` 不能写在同一行**。`IR-AOT-MIR-OPERANDS|…` 的操作数探测器按 case chunk 归属，两个标签共用一个 chunk 时它的内容只归最后一个标签，于是 `OP_EQK` 变成「prescan 验了、emit_one 却不读它的索引」的**死 case**，判据红成 `prescan checks OP_EQK, which the emitter does not index`。拆成两条独立的 case 才干净——而且本来就该拆，两条臂是两段代码。
2. **债务注释里别写带方括号的池表达式**。探测器用 `\b(\w+_pool|…)\s*\[` 找「这个 case 读了索引」，注释里一句 `code->string_pool[sidx]` 会被算成 `OP_NEQ` 的索引，于是红成「`OP_NEQ` 读了索引但 prescan 没验它的操作数」。改成不带方括号的写法（「`code->string_pool` 的第 sidx 项」）就没事。注释的措辞被迫服从判据，这条**不理想**，但比放宽判据好。

债务注释（`emit_one` 的 `case OP_EQK:` 上方，约 127 行）分成五段，逐段是：

- **WHY IT EXISTS**：一种拼法被拒、一种被答对；`op_regs` 的寄存器语义接不住「第三操作数是池下标」；§10.105 只登记了三寄存器的 `OP_EQ`。
- **WHAT IT ADDS**：`handles=34` 里的两条——`OP_EQK`/`OP_NEQK`，各自一个 case，各自一个池界检查，**不新增 opcode、不新增打印出口、零分配**（`mv_str` 只把指针包成 `MirVal`，`strcmp` 读驻留字面量）。
- **WHAT IT DOES NOT COVER**：见第八节，逐条是量到的，不是猜的。
- **DELETE CONDITION（可判定）**：在给共享 `NV` 加字符串 tag、把字符串比较移出「池下标」编码的那笔（`mv_eq` 自己注释里点名的 P3 步）里，删掉这两条 case 和 `mv_eq` 的串臂；此后编译器不再发 `OP_EQK`，`grep -c OP_EQK src/compiler/compiler.c` 为 0 [obs: df7cfbd]，两种拼法**按构造**同路。早于那笔删掉，这处不对称立刻回来。
- **MEASURED BY**：`mir_eqk_string` / `mir_eqk_string_ne` / `mir_eqk_string_cross` 三例按字节比 stdout，实现前全红，反向判据见第五节；`handles=34` 与 `IR-AOT-MIR-COVER|covered=29|of=29` 是它没越界的读数。

### 五、反向判据（两次注入，非对称，都还原了）

注入前先 `grep -n` 印原件（`v[%d] = mv_eq(v[%d], mv_str(\"` 那一行），两份原件副本在 `.scratch/aot_mir.c.keep`；还原证据 `grep -c INJECTED` → 0、`BUILD_EXIT=0`、判据回绿。

**(a) 让内容比较恒真**（`v[r1] = mv_boo(1); /* INJECTED */`，替掉 `mv_eq` 调用）：

```
IR-MIR-MIR-ONLY|case=mir_eqk_string|mir=accepted|cc=ok|exec=diff|rc=0|bytes=10|stdout-hex=747275650a747275650a
IR-MIR-MIR-ONLY|case=mir_eqk_string_cross|mir=accepted|cc=ok|exec=diff|rc=0|bytes=10|stdout-hex=747275650a747275650a
IR-MIR-PROBLEM|mir_eqk_string: the MIR route printed b'true\ntrue\n' rc=0, the interpreter b'true\nfalse\n' rc=0
IR-MIR-PROBLEM|mir_eqk_string_cross: the MIR route printed b'true\ntrue\n' rc=0, the interpreter b'true\nfalse\n' rc=0
IR-MIR-VERDICT|FAIL|tree=e5f737c1af00f955ae4b9b5ed1dfe4b83d06748e|…
```

`mir_eqk_string_ne` 与 `mir_eqk_number` **保持绿**：这次只注入了 `OP_EQK`，`OP_NEQK` 的臂没被碰——判据能分清两条臂，说明它不是靠一个大故障同时污染期望与实际。

**(b) 把常量的取法读错一次**（`c->node->string_pool[w2]`，把**寄存器字段**当池下标，`/* INJECTED: roles swapped */`）：

```
IR-MIR-MIR-ONLY|case=mir_eqk_string|mir=accepted|cc=ok|exec=diff|rc=0|bytes=12|stdout-hex=66616c73650a66616c73650a
IR-MIR-MIR-ONLY|case=mir_eqk_string_cross|mir=accepted|cc=ok|exec=diff|rc=0|bytes=12|stdout-hex=66616c73650a66616c73650a
IR-MIR-PROBLEM|mir_eqk_string: the MIR route printed b'false\nfalse\n' rc=0, the interpreter b'true\nfalse\n' rc=0
IR-MIR-PROBLEM|mir_eqk_string_cross: the MIR route printed b'false\nfalse\n' rc=0, the interpreter b'true\nfalse\n' rc=0
IR-MIR-VERDICT|FAIL|…
```

**红的是 `mir_eqk_string_cross`**，正是那条为「方向」写的用例。

**一条量出来的更正**：我原以为「方向」可以靠「把左右操作数调错一次」来咬，实测**做不到**——`mv_eq` 的相等是对称的，而两种拼法的流**逐字节相同**（第二节），所以「左右调换」这个注入在字节上看不见。真正能红在交叉用例上的是**角色**：r2 是寄存器（放值）、r3 是池下标（放常量）。第五节 (b) 注入的就是这个角色错位。这条更正比一次「注入→红」重要：它说明判据咬的是**取法的正确性**，不是拼法的左右。

### 六、读数（本节 worktree 实跑，逐字；`rm -rf build` 后干净重建，读数在最后一笔源码改动 `d537593` 上重跑）

```
IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)|basis=the-six-_Static_assert-lines-kMirValBlock-writes-into-every-generated-file-read-off-the-emitted-text
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=15|mir-only-same-as-interpreter=15
IR-MIR-VERDICT|PASS|tree=d53759393bc1304d2f9bdb640c60629851f7a574|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=34|of=67|unknown=33|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=34|emitter=34|reported=34|agree=yes
IR-AOT-MIR|accepted=101|refused=0|same-as-interpreter=101|of=29|emittable=34|of-ops=67|not-emittable=33|corpus=inline-cases:86|mir-only-cases:15|beyond-ast-denominator=4|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
IR-AOT-MIR-PIN|tree=a0460b2100b654adcbd47364c452d5d0056402b8|head=d53759393bc1304d2f9bdb640c60629851f7a574|of=29|covered=29|pinned-covered=29|match=yes|tree-moved=yes
IR-AOT-MIR-VERDICT|PASS|tree=d53759393bc1304d2f9bdb640c60629851f7a574|accepted=101|of=29|covered=29|missing=0|refused=0|same-as-interpreter=101|prelude-excluded=yes|beyond-ast-denominator=4
```

必须守住的不变量：`handles` **32 → 34**（正好是 `OP_EQK`/`OP_NEQK` 两条，多一条就说明动了不该动的）；`same-as-interpreter` **97 → 101**（只涨没掉）；`covered` 仍 `29/29`（两条臂不在 AST 路线的 29 个 opcode 里，所以 `beyond-ast-denominator` **2 → 4**，这是「涨了但账算得清」）；`reasons` 三个桶**名字与计数逐字不变**（`identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4`）；`mir-only-cases` **11 → 15**。

### 七、笔

| 笔 | 内容 | 反向判据 |
| --- | --- | --- |
| 一（`136a23b`） | `MIR_ONLY` 四例：`mir_eqk_string`/`mir_eqk_string_ne`/`mir_eqk_number`/`mir_eqk_string_cross` + 用例上方的说明 | 实现前全红（第一节原文；`mir_eqk_number` 绿着，作必要条件） |
| 二（`e5f737c`） | 三处实现 + 五段债务注释 + `kMirValBlock` 那句转成过去时（历史不改写，只把「曾经」说清楚） | 第五节 (a)(b) 两次注入 |
| 三（`a0460b2`） | 债务注释里三条**先写后量**的话改成量到的（纯注释：`git show` 逐行是 `*`） | — |
| 四（`a872fff`） | `tools/aot_mir_cover.pin.json` 重记（tree + 计数；`--pin` 工具写的，`covered` 列表一字未动） | — |
| 五（`d537593`） | 债务注释里六处 `src/vm/vm.c` 的行号**读错**（凭记忆写的），重读原文逐个改对（纯注释） | — |
| 七（本节） | `docs/STATUS.md` 的 §10.109（覆盖 §10.105 那处不对称，不改写历史） | — |
| 七（`365bc8e`） | `tools/docs_headings.pin.json` 重记：本节加了一个 `##` 节，`docs/STATUS.md` 的计数 82 → 83、总数 1406 → 1407 | — |

第三笔值得写明白：注释里原来写「NaN 不可达」「容器拒绝 n=11」「非驻留串要等一个未来 opcode」，量完之后三条**全错**——NaN 是可达的（`0.0/0.0` 在本路线不 abort，两边都答 `false`），容器那条是 `n=10` 且 `why=illegal-operand`（不是 `unsupported-opcode`），非驻留串今天就有（`+`），它在 `mv_add` 里更早就崩。**注释里的数字不许先写后量**，这一笔就是补那个差额。

第五笔是同一种病的另一种形态：`src/vm/vm.c` 的行号是**凭记忆**写的，六处全偏了几行（`L_EQK` 结束在 4173 不是 4174、`L_NEQK` 在 4175–4189 不是 4177–4191、`vm_intern` 那处在 4164–4166 不是 4161、`VAL_BOOL`/`VAL_NIL` 是 418/419 不是 419/420）。一条以「这条规则抄自第 N 行」为全部论证的注释，只值那个 N；猜来的 N 就是债。重读原文后逐处改对。

### 八、诚实边界（逐条是量到的）

1. **常量对容器**（`a = [1, 2]` / `say a == "hi"`）：解释器答 `false`（`src/vm/vm.c` 第 428 行 [obs: df7cfbd]）；本路线**拒译**——`IR-MIR-REFUSED|why=illegal-operand|detail=container=0 leaves the argument stack unbalanced: asp=2`、`IR-AOT-MIR-REFUSED|…|n=10`，`exit 1`，**没有半截答案**。原因不是本节的臂，而是**没有任何可发射的 opcode 造得出容器**：`OP_NEW_ARRAY`/`OP_NEW_DICT`/`OP_NEW_SET` 都不在 lowering 表里（这正是 `handles=34` 而不是 37 的那笔账）。属于本子集外，拒译是诚实的。
2. **非驻留串作比较值**（`r2` 侧）：今天唯一能造出它的是字符串 `+`（`s = "a" + "b"`），而它在**本节之前**就是坏的——生成程序死在 `mv_add` 里（`RUN_RC=134`，stdout 空，`mv_as_nv` 没有 `MV_STR` 臂）。所以非驻留串**到不了** `r2`；真到了，`mv_eq` 的守卫 `abort()`，不静默给答案。属于本子集外（字符串算术是另一格）。
3. **`-0.0` / NaN**：`say -0.0 == "hi"` / `!=` 两边都 `false`/`true`；`say (0.0/0.0) == "hi"` 两边都 `false`。NaN 在本路线**可达**，且因为「浮点对字符串永不强转」（第 414/425-428 行 [obs: df7cfbd]）答案与「NaN 不等于一切」无关——它不是数值路径。
4. **跨容器 / 全局常量形状**：非 root 容器**能用**——`func f(a) { return a == "hi" }` 对的是**函数自己的池**（prescan 用 `node->string_count`，emit 用 `c->node->string_pool`），与解释器逐字节相同；未定义全局（`nosuchglobal == "hi"`）两边都 `false`/`true`（`L_LOAD_GLOBAL` 的 idx=-1 给 nil，走 `mv_eq` 的 nil 臂）。
5. **大池下标**：5000 条字面量先把池撑到 ~5001，`OP_EQK` 的 r3 **越过了 `REG_LIMIT`（4096）**，仍然逐字节相同（`IR-MIR-CONSERVE|insns=10009|emitted=10009|holds=yes`）。这就是第四节第 2 处为什么按 `string_count` 验、而不是按寄存器上限验。
6. **池里放空指针**：编译器不会这么发（字面量必然非空），但 prescan 仍**拒绝**——`vm_intern(NULL)` 在解释器里会退化成 `""`（第 4169–4170 行 [obs: df7cfbd]），本路线不去模仿那条退化路径，宁可拒译。
7. **两种拼法的字节相同**这条性质是**本节的判据的一部分**：如果哪天编译器开始按左右区分 r3（比如把常量编成寄存器），`mir_eqk_string_cross` 会先红。它今天红不了，因为差异不存在——这一点在第五节「一条量出来的更正」里说过。

**今天还会失败/会拒译的 `OP_EQK` 常量形状**，逐条（属于本子集外的一并注明）：

- 常量对**容器**（数组/字典/集合）：**拒译**（`illegal-operand`，没有容器构造 opcode）。本子集外。
- 常量对**非驻留串值**（`+` 串出来的）：生成程序在 `mv_add` 里 **SIGABRT**，早于比较。本子集外（字符串算术）。
- 常量对**函数值**（`f == "hi"`，`OP_LOAD_FUNC` 之类）：`VAL_FUNCTION` 的相等按 `ptr`，但本路线没有可发射的函数值 opcode，会拒译。本子集外。
- 常量对**非 root 容器的外部捕获**（闭包/upvalue 里的字符串）：本子集没有闭包，会拒译。本子集外。
- 常量在 **`OP_EQK` 的宽池形状**（池下标 ≥ 32767，走 `IR_F_WIDE_B`）：代码路径上 `.scratch` 里量了 5001 这一档（不用宽字段），32767 以上没有实际量过，注释里也没声称过——**没有测量就不写结论**。

容器、异常、并发、`OP_CONCAT`：**一笔没碰**。
## §10.110 链式拼接：一条链只分配一次，而不是 N-1 次（aot-mir-value，A 类第八项）

### 一、这笔债的来源与日期

**来源**：解释器对 `a + b + c` 不是做两次 `+`，而是**一条** `OP_CONCAT` —— `src/vm/vm.c` 的 `L_CONCAT` 分支读 `int first = ins.r2, count = ins.r3`，把所有操作数当**连续寄存器** `R[first .. first+count-1]`，全串时先累加总长、**一次** `malloc(total + 1)`、再逐段 `memcpy`。编译器在 3 个及以上操作数、且操作数全是字面量／变量时把链折成这一条（叠成 `OP_CONCAT` 而不是 N-1 条 `OP_ADD`）。可这条 MIR 路线的 lowering 表（`src/compilation/aot_mir.c` 的 `op_regs`）**没有登记它**，`emit_one` 也没有分支，于是语料里 6 条链整条拒绝。

§10.108 末尾第 8 条把这一格**点名留在门外**（原文：「`OP_CONCAT` 没碰。链式拼接（一次为整条链分配）是另一格，本节判据与本节任何一句话都不声称覆盖它」）。本节就是那一格，日期 **2026-11-06**。

### 二、复现（修之前的原始读数，2026-11-06）

`a = "aa"` / `b = "bb"` / `c = "cc"` / `say a + b + c`，`translate` 在 d820953 的树上：

```
IR-MIR-NOT-YET|op=OP_CONCAT|n=1
IR-MIR-REFUSED|why=unsupported-opcode
```

放进判据（`mir_concat_chain`，见下）之后，整条用例的红原文：

```
IR-MIR-MIR-ONLY|case=mir_concat_chain|mir=refused|why=unsupported-opcode
IR-MIR-PROBLEM|mir_concat_chain: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_CONCAT:6)
IR-MIR-VERDICT|FAIL|tree=d820953a38279670600d23213c4cfa55a15ff435|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`OP_CONCAT:6` 就是用例里那六条链：三串、十一操作数、`a + b + "" + c`、`u = a + b + c`、`"z" + u + ""`、`u + u + u`。

### 三、判据先立：链的「一次分配」正是唯一看得见的差别

新用例 `mir_concat_chain` 只比 stdout 字节（照现有 MIR_ONLY 写法），六条链各管一件事：三串链是平面情形；**十一操作数**的长链（中间夹五个驻留字面量）是长度足够让「逐对折叠」的原形露出来的情形；`a + b + "" + c` 把空串放在链**中间**（0 长度段照样是操作数，短路的实现在这里会拿错缓冲区）；`u = a + b + c` 后 `say u` 与 `u == v` / `u != v` 让链结果活成**变量**并参与比较（不是只有 `say` 能读的临时值）；`"z" + u + ""` 让**堆拥有的链结果**继续当中段操作数；`u + u + u` 同一块缓冲区三次（原地追加的实现会在这里把 `u` 弄坏）；最后 `say a` / `say b` 证明原串没被改。

**判据的牙齿是分配读数，不是字节。** 字节判据对「折叠」与「不折叠」**完全瞎**：逐对折叠打印的字节逐字相同。所以 `ALLOCS_EXPECT` 里 `mir_concat_chain` 被逐字写死成 **(6, 66)** —— 六条链各一块，而不是 `(3-1)+(11-1)+(4-1)+(3-1)+(3-1)+(3-1) = 21` 块；66 字节是六条链各自的 `总长 + 1` 之和：`(2+2+2+1) + (2+1+2+1+2+1+2+1+2+1+2+1) + (2+2+0+2+1) + (2+2+2+1) + (1+6+0+1) + (6+6+6+1) = 7+18+7+7+8+19`。这两个数**先量后写**：一次折叠的实测是 `allocs=6|freed=6|live=0|bytes=66`（第六节），逐对折叠的实测是 `allocs=21|bytes=199`（第五节注入 I1），两者 stdout 相同 —— 21 与 199 就是「折叠」与「不折叠」的可测差别。

### 四、实现

**lowering 表**：`OP_CONCAT` 登记为 `R1 | R2`。`r3` 在这张表里是**负载而不是寄存器**（`L_CONCAT` 读它是 `count`），所以它**故意不进 mask**：通用边界循环核对的是「每个被 mask 的字段是一个寄存器」，把 count 当寄存器核对会把 `REG_LIMIT` 比错，而真正必须落在界内的**最后一个操作数** `r2 + count - 1` 反而没人查。那个界在**两个地方**各写一次：`prescan` 里一段**写在 switch 之外**的 `if (op == OP_CONCAT)`（`count` 非负且 `r2 + count - 1 < REG_LIMIT`，负 count 直接拒绝而不是转交给发射器），以及 `container_nregs` 按 `r2 + count - 1` 放宽寄存器数组的尺寸 —— 只按 `r2` 算，寄存器最高的那条链会让 `&v[r2]` 之后的拷贝走出数组。

**发射**：一条链发一行，`v[r1] = mv_concat_n(&v[r2], count);`。`r1` 只在折叠**之后**写一次，位置与解释器的 `value_set(&R[rres], …)` 相同，所以目的寄存器与自己的操作数别名（`s + s + s` 落到 `v[first]`）是安全的：所有操作数先被读，再写。

**运行时的链式折叠**（生成 TU 的 `kMirValBlock` 内，追加、不拼接）：

```c
static inline MirVal mv_concat_n(MirVal *ops, size_t n) {
    if (n == 0) return mv_nil();             /* count <= 0 -> VAL_NIL */
    if (n == 1) return ops[0];               /* value_assign, L_CONCAT */
    int all_str = 1;
    size_t total = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        if (ops[i].type != MV_STR) { all_str = 0; break; }   /* -> mv_add fold */
        if (!ops[i].sval) abort();          /* impossible state */
        size_t l = strlen(ops[i].sval);
        if (total > (size_t)-1 - l - 1) abort();  /* :3351, summed over the CHAIN */
        total += l;
    }
    if (all_str) {
        char *nb = mv_alloc(total + 1);     /* ONE block for the whole chain */
        size_t off = 0;
        for (i = 0; i < n; i++) {
            size_t l = strlen(ops[i].sval);
            memcpy(nb + off, ops[i].sval, l);
            off += l;
        }
        nb[total] = 0;
        MirVal r;
        r.type = MV_STR;
        r.ival = 0;      /* heap-owned, the same marker mv_str_concat sets */
        r.sval = nb;
        r.ptr  = 0;
        return r;
    }
    {   /* not all strings: the interpreter's left-assoc fold, delegated */
        MirVal acc = ops[0];
        for (i = 1; i < n; i++) acc = mv_add(acc, ops[i]);
        return acc;
    }
}
```

**照抄的三点，与一处故意的分歧**：

- 溢出检查**抄链式的、不抄两操作数的**。`L_CONCAT` 的 `:3351` 是 `if (total > (size_t)-1 - l - 1)`，其中 `total` 是**运行中的总长**、`l` 是当前段。本节把它逐字搬进链循环，所以一条 11 段链是拿整条和核对的，不是只核对相邻两段（两操作数版本只能看见一对，链长到三段以上时它看不见总和）。
- **OOM 保持 abort**，与 §10.108 的 `mv_str_concat` 臂一致。解释器在 `:3356` 是 `nb = strdup("")`，那是**静默错答**（`s + t` 答成 `""`）且 strdup 自己也是 malloc，本路线不抄；分歧是故意的，写在注释里。
- **空串不短路**：0 长度段照样是操作数、照样计入 `total + 1` 的那一个字节。`a + b + "" + c` 的 66 字节里就含它。

**不碰 §10.108 的 `OP_ADD` 串臂**：`mv_str_concat` / `mv_add` 一个字没改，本节只**调用** `mv_add`（在非全串链回退时），并新增 `mv_concat_n`。混合类型的链因此落在**已经写下的边界**上：解释器的通用路径对非串操作数按 `L_ADD` 规则**串化**、对两个 set 做并集，本路线两者都没有，所以回退到 `mv_add` 就停在 `mv_add` 停的地方（混合对 abort），不会新造第二种语义。

**为什么那段不写成 switch 的 case**（这是本节唯一一处由仪器逼出来的形状）：`tools/aot_mir_cover.test.py` 的 operands 读数把 `prescan` 里的 `case OP_X:` 标签读成「这个 opcode 核对了一个**索引**」，再和 `emit_one` 里真正读池／全局／函数／标签的那几个 case 对账。把 `OP_CONCAT` 写成 case 标签，它就会被记成「核对了一个索引却从不发射一个索引」，读数红成 `dead=(OP_CONCAT)`、`agree=no`、`IR-AOT-MIR-PROBLEM|prescan checks OP_CONCAT, which the emitter does not index`。那段核对的确实不是索引而是**计数**，所以它按它本来的样子写成 switch 外的 `if`；读数随之回到 `prescanned=9|checked=8|dead=(none)|agree=yes`。改的是**代码的形状**，不是仪器的判据。

### 五、反向判据（非对称，注入一次）

**注入处先印原件**（`src/compilation/aot_mir.c`，单次分配那一行）：

```
"        char *nb = mv_alloc(total + 1);     /* ONE block for the whole chain */\n"
```

**注入 I1**：在 `if (all_str) {` 之前插入逐对折叠并直接 return，绕开那条单次分配（这就是本节要删掉的形状本身）：

```
"    /* INJECTED-NMINUS1 fold: the N-1 shape the criterion exists to catch */\n"
"        return acc; }                 /* INJECTED-NMINUS1 end */\n"
```

**注入后先印被计数的对象**：`grep -c INJECTED src/compilation/aot_mir.c` = `2`。`BUILD_EXIT=0`、`MIR_EXIT=1`，判据红原文：

```
IR-MIR-ALLOCS|case=mir_concat_chain|reported=yes|allocs=21|freed=21|live=0|bytes=199|expect-allocs=6|expect-bytes=66|ok=no
IR-MIR-MIR-ONLY|case=mir_concat_chain|mir=accepted|cc=ok|exec=same|rc=0|bytes=83|stdout-hex=6161626263630a61617c62627c63637c64647c65657c66660a6161626263630a6161626263630a747275650a66616c73650a7a6161626263630a6161626263636161626263636161626263630a61610a62620a
IR-MIR-PROBLEM|mir_concat_chain: allocation count off -- got allocs=21|bytes=199, the criterion demands allocs=6|bytes=66 (a site that mallocs without registering reports 0|0)
IR-MIR-VERDICT|FAIL|tree=d820953a38279670600d23213c4cfa55a15ff435|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

这是本节最重要的一次读数：`exec=same`、`stdout-hex` 与实现后**逐字相同**（83 字节一条不差），而分配数从 6 变成 21、字节从 66 变成 199。**判据按字节比是绿的，只有分配读数红** —— 这正是「折叠」与「不折叠」的可测差别，也是为什么这一格的判据必须有牙齿而不能只比 stdout。

**还原证据**：把注入前的源文件拷回，`grep -c INJECTED src/compilation/aot_mir.c` = `0`（无匹配），`grep -n "ONE block for the whole chain"` 回到原件那一行，`BUILD_EXIT=0`，重跑判据 `allocs=6|freed=6|live=0|bytes=66|ok=yes`、`IR-MIR-VERDICT|PASS`、`IR-MIR-PROBLEM` 计数 0。

### 六、读数（本树上测得）

`python3 tools/aot_mir.test.py --build build`，`MIR_EXIT=0`：

```
IR-MIR-ALLOCS|case=mir_concat_chain|reported=yes|allocs=6|freed=6|live=0|bytes=66|expect-allocs=6|expect-bytes=66|ok=yes
IR-MIR-MIR-ONLY|case=mir_concat_chain|mir=accepted|cc=ok|exec=same|rc=0|bytes=83|stdout-hex=6161626263630a61617c62627c63637c64647c65657c66660a6161626263630a6161626263630a747275650a66616c73650a7a6161626263630a6161626263636161626263636161626263630a61610a62620a
IR-MIR-VERDICT|PASS|tree=d820953a38279670600d23213c4cfa55a15ff435|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`python3 tools/aot_mir_cover.test.py --build build`，`COVER_EXIT=0`，`IR-AOT-MIR-VERDICT|PASS`：

```
IR-AOT-MIR|accepted=99|refused=0|same-as-interpreter=99|of=29|emittable=33|of-ops=67|not-emittable=34|corpus=inline-cases:86|mir-only-cases:13|beyond-ast-denominator=3
IR-AOT-MIR-OPERANDS|indexed=8|prescanned=9|checked=8|missing=(none)|dead=(none)|detector-saw=yes|agree=yes
IR-AOT-MIR-LOWERING|handles=33|of=67|unknown=34|table=33|emitter=33|reported=33|agree=yes
IR-AOT-MIR-BEYOND|ops=(OP_CONCAT,OP_IS_NIL,OP_LOADK_STRING)|...
```

`handles` 从 32 到 33，`accepted` 从 98 到 99、`same-as-interpreter` 从 98 到 99，`mir-only-cases` 从 12 到 13：三个数各自 +1，正好是新用例一条、OP_CONCAT 一格。`beyond-ast-denominator` 从 2 到 3，多出来的正是 `OP_CONCAT`（MIR-only 用例执行了它、AST 语料从没执行过它）——pin 必须为此重记，那是下一笔的事。

`python3 tools/check_line_refs.py` 与 `python3 tools/check_word_refs.py` 都是 `RC=0`：`CITATIONS|base=1199|now=1199|removed=0|added=0|tree=d820953`；`pins -- explicit delta +12 (expect +12), unanchored delta -3 (ceiling +1)`；`readings -- explicit 185 (base 173), unanchored 559 (base 562), mis-labelled 0`。本节没有新增任何 `路径:行号` 形式的显式引用（源码行只写裸 `:NNNN` 加代码原文），所以 explicit 与 unanchored 两个棘轮一格没动。

**重记 pin 与门禁**（下一笔提交 85bebc4 上测得，`GATE_EXIT=0`）：

```
IR-AOT-MIR-PIN|tree=59c6045b5b3060bf8ed1442754c8bc14b519fa43|head=85bebc45eecd8624c87cdafc991448ae567cf5c5|of=29|covered=29|pinned-covered=29|match=yes|tree-moved=yes
gate: OK — every stage passed (20/20 stages ran).
```

`match=yes` 是数字对上了（of/covered/pinned-covered 三个 29 与 beyond 名单一致）；`tree-moved=yes` 是**预期**的，不是失败：pin 的 `tree` 指的是「这一读数是在哪个提交上取的」，而这个提交（只动 pin 文件的那一笔）当然在它后面，读数本身没有移动。`gate.sh` 的 20 个阶段全过，`check_docs_headings` 的 `HEADINGS|pin|tree=59c6045|docs=111|headings=1408|dirty=no` 也在其中。

### 七、诚实边界（「一次分配」在哪些形态下成立、哪些不成立）

1. **「一次分配」只对全串链成立。** `mv_concat_n` 先扫一遍看是否全串：是，一次 `mv_alloc`；不是，回退到逐对的 `mv_add`（每一对各自分配，混合对 abort）。所以本节能说的准确句子是「**全串**的 `OP_CONCAT` 链一次分配」，不是「`OP_CONCAT` 永远一次分配」。
2. **count 为 0 / 1 的链不分配。** count <= 0 回 `nil`、count == 1 直接传值，与解释器 `L_CONCAT` 同；这两种形态零分配，而用例里也没有它们（编译器只在 3 个以上操作数时才折成 `OP_CONCAT`），所以它们的字节与分配数**本用例不覆盖**。
3. **空串段照样占一个终止符字节。** `a + b + "" + c` 分配 `2+2+0+2+1 = 7` 字节，不是 6 —— 与解释器 `malloc(total + 1)` 逐字一致（解释器也给 0 长度段算 length 0、给整条链一个终止符）。
4. **溢出检查是整条链的，但只在字符串段上累加。** 非字符串段在扫描时直接跳出，所以「全串假设不成立时不做溢出检查」不是漏洞：那种形态回退到 `mv_add`，而 `mv_add` 的 `mv_str_concat` 臂有自己的两操作数检查（§10.108）。
5. **OOM 与解释器分歧。** 解释器 `:3356` 的 `strdup("")` 静默答成空串，本路线 abort —— 与 `mv_str_concat` 臂一致，故意不抄静默错答。
6. **混合类型链不成立于「折叠」。** 串 + 数／浮点／布尔、以及 set／array／dict 操作数，解释器走通用路径做串化／并集；本路线回退到 `mv_add` 停的地方（混合对 abort，容器类操作数在解释器里是 `exit(1)`，这里也是 abort）。这些形态**没有**一次分配，也**不声称**有。
7. **`all_str` 的判定用 `type == MV_STR`，与解释器的全串快路径同一个判据。** 解释器也是先扫一遍、遇到第一个非串就转逐对；本路线同样先扫后决定，所以「先扫」本身不增加一次读遍之外的成本，也不改变任何输出。
8. **别名安全，但依赖「先读后写」。** `v[r1]` 只在 `mv_concat_n` **返回之后**被写，所有操作数在分配与拷贝期间只被读，所以 `u + u + u`（同一块缓冲区出现三次）与 `s + s + s` 落到 `v[first]` 都安全。将来若有人把这段改成「边拼边写回 `v[r1]`」，这条就断了。
9. **`container_nregs` 的放宽是保守的。** 它按「最后一个操作数」放大寄存器数组，只放大不缩小，所以别的容器不会因此缩水；但它是一个共享函数，理论上会改变其他容器（`OP_CONCAT` 与普通 opcode 共存时）的数组尺寸 —— 只变大，不影响输出。
10. **分配读数只对这一条用例写死。** `ALLOCS_EXPECT` 里只有 `mir_add_string_concat` 与 `mir_concat_chain` 两个条目，其余 MIR-only 用例仍按 `(0, 0)` 要求「不分配也不漏」，本节没有放宽那条默认。
11. **本用例不覆盖 OOM、不覆盖超过 `SIZE_MAX / 2` 的链长、不覆盖 count <= 0 与 count == 1 的 `OP_CONCAT`。** 这三类要么不可测（OOM 与超长），要么编译器今天不产（短链）。

## §10.111 短路：左真时 `OP_OR` 根本不执行，所以补的是语料而不是阈值（aot-mir，B 类）

### 一、这笔债的来源与日期

**来源**：`IR-AOT-MIR-UNEXECUTED` 这一行在 d820953 上报的是 `ops=(OP_OR)`。它的含义**不是** MIR 路线不会发射 `OP_OR`（`OP_OR` 早就在 lowering 表里，`handles` 里有它、`emittable=yes`），而是：共享语料里有 3 个程序的降低流里出现 `OP_OR`，可**解释器跑共享语料时一次也没执行过它** —— 因为 `X or Y` 在 X 为真时短路跳过 OR 本身，而那 3 个程序里恰好都是左真。

**这不是正确性缺口，是语料缺口。** 自造 `a or b` / `b or a` / `a and b` 三例，两侧输出 `true/true/false` 逐字节一致；也就是说 `OP_OR` 的 MIR 语义**今天就是对的**，缺的是一次「让它真的被执行」的读数。所以这一格不许改语料程序绕过、不许调阈值，只许补一条**真执行**它的 MIR_ONLY 用例。日期 **2026-11-06**。

### 二、缺口原文（补语料之前，85bebc4 上）

```
IR-AOT-MIR-UNEXECUTED|ops=(OP_OR)|basis=opcodes-in-a-lowered-stream-of-a-shared-case-that-no-interpreter-run-of-the-corpus-executed|in-ast-denominator=no|counted-in-covered=no|emittable=yes
```

`emittable=yes` 在同一行上，所以这一行自己就说清了缺口在哪一侧：不是发射器，是「没有一次执行」。

### 三、判据先立：让 `OP_OR` 真的跑一次

新用例 `mir_short_circuit_or`（`tools/aot_mir.test.py` 的 MIR_ONLY 列表，照现有写法只比 stdout 字节）：

```
a = false
b = true
c = false
say a or b     # false or true  -> OP_OR 执行，答右侧
say a or c     # false or false -> OP_OR 执行，答 false（答恒 true 的臂在这里红）
say a or a     # 同一寄存器两次过 OP_OR
say b or a     # 对照：左真，OP_OR 被跳过（原子仍答 true）
say a and b    # and 的镜像：左假，AND 被跳过
say b and a    # and 的镜像：左真，AND 执行
```

**左操作数为假的三行才是本节的目的**：`or` 只在左假时才走 `OP_OR`。`b or a` 是刻意留的**对照行**：它证明短路仍然短路，把 `or` 实现成「恒执行 OR」会在别处露馅（见第七节第 3 条：在本仓库今天能测的范围内，这一行的保护力有限，那是本节要写下来的边界之一）。

这一条用例落在 `ALLOCS_EXPECT` 的默认 `(0, 0)` 上：它一个字符串都不碰，所以既不分配也不许漏。

### 四、这不是代码改动

`src/` 下**一个字没改**：`OP_OR` 早就在 `op_regs` 与 `emit_one` 里，`OP_JUMP_IF_FALSE`／`OP_JUMP_IF_TRUE` 也早就在。补的是**语料**，也就是「谁去执行它」这件事。共享语料那 3 个 emit `OP_OR` 的程序**一个字节没动**，`tools/aot_cover.pin.json` 与任何阈值也没动。

### 五、机制证据（非对称，先印被计数的对象）

`INIMERSE_HOT_OPS=1` 的解释器热普查，两个程序只差左操作数：

```
a = false / b = true / say a or b     ->  OP-HOT|op=OP_OR|interp=1
a = false / b = true / say b or a     ->  grep -c OP_OR 该日志 = 0（短路跳开）
```

同一条 `OP_OR`、同一个 `say`，只有左边真假不同：左假执行 **1 次**，左真执行 **0 次**。这就是「左操作数为 false 时才走 `OP_OR`」的实测量，不是注释里的断言。

### 六、反向判据（非对称注入一次）

**注入处先印原件**：用例第一行 `'a = false\nb = true\nc = false\n'`。

**注入 I2（`INJECTED-TRUE-LEFT`）**：把它改成 `a = true`，于是每一条 `or` 都变成左真、`OP_OR` 又被短路跳开，而**答案仍然由解释器自己定**（判据不与解释器对账，只比字节，所以 stdout 不会红）。

**注入后先印被计数的对象**：`grep -c INJECTED-TRUE-LEFT` = `2`。`COVER_EXIT=0`（注意：这一行不是退出码判据），读数红原文：

```
IR-AOT-MIR-UNEXECUTED|ops=(OP_OR)|basis=opcodes-in-a-lowered-stream-of-a-shared-case-that-no-interpreter-run-of-the-corpus-executed|...
IR-AOT-MIR|accepted=100|refused=0|same-as-interpreter=100|of=29|emittable=33|of-ops=67|not-emittable=34|corpus=inline-cases:86|mir-only-cases:14|beyond-ast-denominator=3
IR-AOT-MIR-VERDICT|PASS|...|accepted=100|same-as-interpreter=100|...
```

这次注入是本节最有信息量的一次读数：`accepted=100|same-as-interpreter=100` **一点没变**，`beyond-ast-denominator` 从 4 掉回 3，`UNEXECUTED` 从 `(none)` 回到 `(OP_OR)`。**正确性没动，只有「执行性」这本账动了** —— 这正是「语料缺口，不是正确性缺口」的可测证明。

**还原证据**：拷回注入前的测试文件，`grep -c INJECTED` = `0`（无匹配，rc=1），用例首行回到 `a = false`，重跑读数 `IR-AOT-MIR-UNEXECUTED|ops=(none)`、`beyond-ast-denominator=4`。

### 七、诚实边界

1. **`UNEXECUTED` 的 basis 是「所有用例（`shared` + `mir_only`）的解释器执行集合」，不是只跑共享语料。** 读数那一行自带的注释写的是 `a-shared-case-that-no-interpreter-run-of-the-corpus-executed`，可脚本是先 `cases = shared + mir_only`、再对**每一个** case 跑 `executed_ops.update(dyn)`，所以集合里既有共享语料的执行、也有 MIR-only 用例的执行 —— 本节能把它归零，恰恰是因为补的那条 MIR-only 用例**真的跑了**解释器。这句话必须写在这里：下一个人若照那行注释理解成「只算共享语料」，就会得出「补 MIR-only 用例不可能影响 UNEXECUTED」的错结论（这个错结论今天已经被证伪）。
   它也不声称 MIR **生成的**代码执行了它 —— 那是另一本账（`beyond-ast-denominator`），本节只是让这本账多一条。
2. **短路的方向是固定的**：`or` 只在左假执行 `OP_OR`，`and` 只在左真执行 `OP_AND`。所以补执行覆盖必须补**左假**的 `or`（以及左真的 `and`），写一个 `a or b` 但 `a` 为真是没用的。
3. **字节判据对「MIR 是否短路」是盲的。** 这一条要说明白：`say b or a` 这行的输出在「MIR 短路」与「MIR 恒执行 OR 但值算对」两种实现下**完全相同**，而解释器热普查只数解释器（`OP-HOT` 的 `why` 字段明说 `aot=na`：生成 C 把每个 op 展平成内联代码，在那里计数是在数一个被改过的对象）。所以本节的判据补的是**解释器侧的语料覆盖**；「MIR 生成的代码是否真的跳过了 OR」今天没有仪器能测，也不声称测了。
4. **不碰阈值、不碰语料、不碰 pin 的语义**。`tools/aot_cover.pin.json` 未改，`ALLOCS_DEFAULT` 未改，共享语料未改；只新增了 MIR_ONLY 一条用例。
5. **`beyond-ast-denominator` 从 3 到 4 是这条用例带来的，不是 MIR 路线新发射了什么。** `OP_OR` 一直在 `handles` 里；变的只是「有人跑过它」。
6. **`accepted` 从 99 到 100 只是多了一条 MIR-only 用例**；共享语料仍是 86 条，`of`/`covered` 仍是 29/29。
7. **不覆盖**：`??`、嵌套 `a or b or c`、`or` 的结果再喂给 `or`、常量折叠形态（`true or b`）、以及 `and`/`or` 与字符串真值的组合。这些形态今天没有判据。
8. **热普查是 opt-in 环境变量**（`INIMERSE_HOT_OPS=1`），gate 里的 cover 回归用同一套，所以 `ops=(none)` 这个读数在门禁里也被重算，不是本节手工贴上去的。
9. **`say a or b` 打印的是布尔原样**（`true`/`false`），不经过字符串化路径；所以这条用例不覆盖字符串化。

**重记 pin 与门禁**（重记 pin 之后、HEAD=8a2762f 上测得，`GATE_EXIT=0`）：

```
IR-AOT-MIR-PIN|tree=e1fa8f0c19c6d9db7ae58ec377915548312a5e46|head=8a2762fc8fadf5fc3794a6d640194791009824a9|of=29|covered=29|pinned-covered=29|match=yes|tree-moved=yes
GATE|start|head=8a2762fc8fadf5fc3794a6d640194791009824a9|worktree=.worktrees/mirval-concat2
gate: OK — every stage passed (20/20 stages ran).
```

`match=yes` 是数字对上了（of/covered/pinned-covered 三个 29 与 4 项 beyond 名单一致）；`tree-moved=yes` 是**预期**的：pin 的 `tree` 记的是「这一读数在哪个提交上取」，重记 pin 的那一笔在它后面，读数本身不动。这条用例的 `UNEXECUTED|ops=(none)` 在门禁里被**重算**了一次（cover 回归跑同一套用例），不是本节手工贴的。

## §10.112 容器最小闭环第一步：`OP_NEW_ARRAY` 与只读的 `OP_INDEX_GET`（aot-mir-container，A 类第九项）

前九节（§10.101–§10.109）走的都是**标量与字符串**。容器这条路今天不是「答错了」，是**整条路不存在**：MIR 路线里凡是建数组的程序都停在 `unsupported-opcode`，而它的邻居——参数栈——会先把翻译器绊倒。本节要的是**最小闭环的第一步**：`[1, 2, 3]` 建得出来、`a[1]` 按字节读得对、越界与非整数下标与解释器同分类。`INDEX_SET`、`NEW_DICT`、`NEW_SET` 一笔没碰，它们是下一节的事。

WHY IT EXISTS，用一句话说清楚：**数组的构造在 MIR 路线里是一条「参数栈协议」而不是一条「操作数协议」**——编译器把元素逐个 `PUSH_REG` 上参数栈，再发一条只带目标寄存器与元素个数的 `OP_NEW_ARRAY`（`src/compiler/compiler.c:1042-1047` [obs: 1d5cfd1]），而 MIR 路线没有参数栈这个设施，`prescan` 因此只看见「栈进了三个、没人取」，于是它用最保守的理由拒掉整个容器。要接住它，得同时补三样东西：一个能放容器的值、一个按元素顺序取走参数的 emit 出口、以及「读下标」那条**分类**语义（整数/浮点/布尔/其它四类，`src/vm/vm.c:3678-3680` [obs: 1d5cfd1]）。任何一样缺了，读数就会以「拒译」或「字节不同」的形态红回来。

### 一、判据先行（实现之前，逐字）

`tools/aot_mir.test.py` 的 `MIR_ONLY` 加七例（逐个打印元素、边界下标、嵌套容器、空数组、字符串下标、非整数下标、类型错），另加一张反向表 `MIR_ONLY_REFUSE`（一例：常量越界下标**必须被拒译**，并且拒译理由必须叫 `index-out-of-range-constant`）。七例都在 `exec=same` 上判：`tools/aot_mir.test.py:1038` 把 **rc 与 stdout 一起比**（[obs: 1d5cfd1]），不是「看起来差不多」。

| 用例 | 源码 | 它咬的是什么 |
| --- | --- | --- |
| `mir_container_new_array` | `a = [1, 2, 3]` / `say a[0]` / `say a[1]` / `say a[2]` | 建数组 + 三个下标各读一次，逐字节 |
| `mir_container_new_array_mixed` | `a = ["hi", 2, true]` / 三行 `say` | 混合元素类型：串、整数、布尔都要原样出来 |
| `mir_container_index_get` | `b = [[5, 6], [7]]` / `say b[0][1]` 等 | 嵌套容器：`b[0]` 本身是一次读，`[1]` 是第二次 |
| `mir_container_index_edges` | `a = [7, 8, 9]` / `i = -1` / `say a[i]` / `i = 3` / `say a[i]` / `i = 1.9` / `say a[i]` / `i = true` / `say a[i]` | 运行期边界：负下标答 nil、越界答 nil、浮点下标截断、布尔当 0/1 |
| `mir_container_new_array_empty` | `a = []` / `i = 0` / `say a[i]` | 零元素数组与它的越界读 |
| `mir_container_string_index` | `s = "hi"` / `say s[0]` / `say s[1]` / `say s[2]` / `say s[-1]` | 第二种容器：字符串下标（负下标回卷，命中出一字节新串） |
| `mir_container_index_type_mismatch` | `a = [7, 8, 9]` / `say a[nosuchglobal]` | **不是**静默 0：解释器走 type_mismatch（rc=1、无 stdout），本路线必须同 rc 同空输出 |
| `mir_container_const_oob`（反向） | `a = [1, 2, 3]` / `say a[3]` | 常量越界下标**必须被拒译**，理由必须是 `index-out-of-range-constant` |

判据**先**写、实现**后**做；实现之前，在裸基线（`src/compilation/aot_mir.c` 是 `1d5cfd1` 的原件）上跑出的原文逐字如下，`MIR_EXIT=1`：

```
IR-MIR-MIR-ONLY|case=mir_container_new_array|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_container_new_array_mixed|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_container_index_get|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_container_index_edges|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_container_new_array_empty|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_container_string_index|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_container_index_type_mismatch|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY-REFUSED|case=mir_container_const_oob|mir=refused|why=illegal-operand|expect=index-out-of-range-constant|ok=no|interp-rc=0|interp-out=b'nil\n'|stdout-bytes=4|stdout-hex=6e696c0a
IR-MIR-VERDICT|FAIL|tree=1d5cfd1477a4cf90c7f823c7aabf2e8ddafe4b43|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

四例 `illegal-operand` 的 detail 是同一个原因，逐字：

```
IR-MIR-PROBLEM|mir_container_new_array: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=3)
IR-MIR-PROBLEM|mir_container_index_get: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=5)
IR-MIR-PROBLEM|mir_container_new_array_empty: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_NEW_ARRAY:1,OP_INDEX_GET:1)
IR-MIR-PROBLEM|mir_container_string_index: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_INDEX_GET:4)
IR-MIR-PROBLEM|mir_container_const_oob: refused with why=illegal-operand, the criterion demands why=index-out-of-range-constant (a refusal for the wrong reason is not the refusal this list measures)
```

`asp=3`（三个元素）/ `asp=5`（嵌套两层的五个元素）是**第一个真因被红在脸上**：参数栈只进不出。空数组与字符串下标没有 `PUSH`，撞的是 unknown-opcode 那道墙——同一个缺口，两种红法。反向例在实现前红成「拒译了，但**理由不对**」：一条理由错的拒绝不算这条判据要的拒绝，所以它单独占一个 `ok=no`。

### 二、操作数布局：由 dump 证实，不凭记忆

```
$ printf 'a = [1, 2, 3]\nsay a[1]\nsay a[3]\n' > .scratch/probe1.im
$ ./build/aot-mir dump .scratch/probe1.im
IR-MIR-INSN|func=0|idx=6|op=OP_NEW_ARRAY|r1=1|r2=0|r3=3|argc=0
IR-MIR-INSN|func=0|idx=10|op=OP_INDEX_GET|r1=3|r2=1|r3=2|argc=0
IR-MIR-INSN|func=0|idx=14|op=OP_INDEX_GET|r1=3|r2=1|r3=2|argc=0
```

- `OP_NEW_ARRAY`：`r1` = 目标寄存器、`r2` = 0（编译器恒发 0、解释器也**不读**）、`r3` = 元素**个数**。元素不在操作数里，它们在参数栈上（三元素先各自 `PUSH_REG`，栈高 3）。
- `OP_INDEX_GET`：`r1` = 目标、`r2` = 容器寄存器、`r3` = 下标寄存器。

三处独立来源一致：`src/compiler/bytecode.h:14-16` 的注释（[obs: 1d5cfd1]）、`src/compiler/compiler.c:1047` 的 `emit(OP_NEW_ARRAY, r, 0, expr->list.count)`（[obs: 1d5cfd1]）、解释器 `src/vm/vm.c:3620-3647` 只读 `ins.r1`/`ins.r3`、元素自 `base = t->sp - (n - 1)`（第一个 push 就是元素 0，[obs: 1d5cfd1]）。所以 `op_regs` 只把 `r1` 记成寄存器、把 `r3` 当**计数**读——这一步写反了，`r3` 就会被当成第三个寄存器，读数会以「读了一个没写过的寄存器」的形态红。

### 三、实现（全部在 `src/compilation/aot_mir.c`）

1. **值表示**：`#define MV_ARR 6`（= `src/vm/vm.h` 的 `VAL_ARRAY`，[obs: 1d5cfd1]），见 `src/compilation/aot_mir.c:740`（[obs: 85a4660]）。既有约定照抄：解释器把容器放进寄存器时 `ival` 是**池下标加一**（`src/vm/vm.c:3644`，[obs: 1d5cfd1]），所以 `MV_ARR` 的 `ival` 也是池下标；`sval` 留给驻留串标记、`ptr` 留给将来的字典。`MirVal` 的 32 字节布局与 `offsetof` 断言（`src/compilation/aot_mir.c:96-102`，[obs: 85a4660]）**一个字都没动**——这里没有第三套值表示。
2. **分配走 `mv_alloc`**：`mv_arr_new`（`src/compilation/aot_mir.c:745`，[obs: 85a4660]）每个数组字面量一次 `mv_alloc(sizeof(MirVal) * cap)`，池从 8 槽起**整块搬家一倍一倍地长**，旧块留给 `atexit(mv_free_all)` 一次清扫。记账仪器因此能说出真实开销：`bytes=256` = 8 槽 × 32 字节，不是「一个数组 0 字节」的那种假绿。
3. **`op_regs` 两条臂**（`src/compilation/aot_mir.c:1083`、`:1092`，[obs: 85a4660]）：`OP_NEW_ARRAY` → `R1`；`OP_INDEX_GET` → `R1|R2|R3`。
4. **`prescan` 两条臂**：`OP_NEW_ARRAY` 校验 `r3` 落在 `[0, REG_LIMIT)`、`asp >= n` 且 `asp -= n`（`src/compilation/aot_mir.c:1524`，[obs: 85a4660]）——这一行就是第一节 `asp=3` 那四条红的解药；`OP_INDEX_GET` 本趟无事可做。
5. **生成前能查的常量下标**：新增 `prescan_constant_subscripts`（`src/compilation/aot_mir.c:1361`，[obs: 85a4660]），只在**常量可证**时生效——`OP_LOADK_INT`/`OP_LOADK_I64` 记下标整数、`OP_NEW_ARRAY` 记容器长度，遇到**基本块汇合点**清空知识（join 处不猜），两样都知道且 `k < 0 || k >= len` 时拒译。为了让「理由的名字」成为读数的一部分，为它单开 `g_prescan_why`（`src/compilation/aot_mir.c:974`，[obs: 85a4660]），否则这条拒绝会混进 `illegal-operand` 那一锅，判据也就分不出「拒对了」与「拒错了」。
6. **运行期语义逐条照解释器**：`mv_arr_get`（`src/compilation/aot_mir.c:793`，[obs: 85a4660]）。下标先归类、再找容器（`mv_index_i32`，`src/compilation/aot_mir.c:780`，[obs: 85a4660] 与 `src/vm/vm.c:323-334`、`:3678-3680` 一致，[obs: 1d5cfd1]）；数组臂越界/负下标答 nil（`src/vm/vm.c:1000-1012`，[obs: 1d5cfd1]）；字符串臂负下标回卷 `i = len + i`、命中 `mv_alloc(2)` 出一字节新串（`src/vm/vm.c:3700-3711`，[obs: 1d5cfd1]）；其它容器答 nil（`src/vm/vm.c:3712-3714`，[obs: 1d5cfd1]）。
7. **emit 出口**：`OP_NEW_ARRAY` 与 `OP_INDEX_GET` 在 `src/compilation/aot_mir.c:1902-1907`（[obs: 85a4660]）；`emit_container` 的暂存区按**参数栈最深点**定尺寸（`src/compilation/aot_mir.c:2040-2055`、`:2120-2121`，[obs: 85a4660]）——不放大，`mvargs + asp - n` 就会指到相邻寄存器，读出来的是「别人的值」而不是数组元素。

### 四、读数（实现之后，原始）

```
IR-MIR-ALLOCS|case=mir_container_new_array|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_container_new_array|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=310a320a330a
IR-MIR-ALLOCS|case=mir_container_new_array_mixed|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_container_new_array_mixed|mir=accepted|cc=ok|exec=same|rc=0|bytes=10|stdout-hex=68690a320a747275650a
IR-MIR-ALLOCS|case=mir_container_index_get|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_container_index_get|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=360a370a350a
IR-MIR-ALLOCS|case=mir_container_index_edges|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_container_index_edges|mir=accepted|cc=ok|exec=same|rc=0|bytes=16|stdout-hex=6e696c0a6e696c0a380a380a370a390a
IR-MIR-ALLOCS|case=mir_container_new_array_empty|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_container_new_array_empty|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=6e696c0a
IR-MIR-ALLOCS|case=mir_container_string_index|reported=yes|allocs=3|freed=3|live=0|bytes=6|expect-allocs=3|expect-bytes=6|ok=yes
IR-MIR-MIR-ONLY|case=mir_container_string_index|mir=accepted|cc=ok|exec=same|rc=0|bytes=10|stdout-hex=680a690a6e696c0a690a
IR-MIR-ALLOCS|case=mir_container_index_type_mismatch|reported=no|why=rc=1
IR-MIR-MIR-ONLY|case=mir_container_index_type_mismatch|mir=accepted|cc=ok|exec=same|rc=1|bytes=0|stdout-hex=
IR-MIR-MIR-ONLY-REFUSED|case=mir_container_const_oob|mir=refused|why=index-out-of-range-constant|expect=index-out-of-range-constant|ok=yes|interp-rc=0|interp-out=b'nil\n'|stdout-bytes=4|stdout-hex=6e696c0a
IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=1|ok=1|expect=1
IR-MIR-VERDICT|PASS|tree=1d5cfd1477a4cf90c7f823c7aabf2e8ddafe4b43|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
MIR_EXIT=0
```

逐条读：`exec=same` 七例、`ok=yes` 十一行、`REFUSED-TOTAL|cases=1|ok=1|expect=1`。ALLOCS 是「先量后写死」：`1|256` 五例、`3|6`（三次一字节新串）、`0|0`（类型错那例在 `exit(1)` 之前确实一次分配都没有）逐例写进 `ALLOCS_EXPECT`，没有留成默认的 `0|0`（默认值就是那条「绕开 `mv_alloc` 直接 `malloc` 也会假绿」的陷阱）。这里有一处**没有被认证**的地方，写出来：类型错那例 `rc=1`，而工具只在 `rc_r == 0` 时才比对分配计数（所以它印的是 `reported=no|why=rc=1`），**那一格的 `0|0` 今天没有裁判**——它是一句期望，不是一条读数。

覆盖读数（同一棵树，`python3 tools/aot_mir_cover.test.py --build build`）：

```
IR-AOT-MIR-LOWERING|handles=36|of=67|unknown=31|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=36|emitter=36|reported=36|agree=yes
IR-AOT-MIR|accepted=109|refused=0|same-as-interpreter=109|of=29|emittable=36|of-ops=67|not-emittable=31|corpus=inline-cases:86|mir-only-cases:23|beyond-ast-denominator=6
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
IR-AOT-MIR-UNEXECUTED|ops=(OP_OR)|basis=opcodes-in-a-lowered-stream-of-a-shared-case-that-no-interpreter-run-of-the-corpus-executed|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR-VERDICT|PASS|tree=1d5cfd1477a4cf90c7f823c7aabf2e8ddafe4b43|accepted=109|of=29|covered=29|missing=0|refused=0|same-as-interpreter=109|prelude-excluded=yes|beyond-ast-denominator=6
```

`handles=34 → 36`（两条新臂），`agree=yes` 表示 `table`/`emitter`/`reported` 三处报的是同一个数；`covered=29|of=29` 没变（本节没新增可覆盖 opcode，只是把两条已有的发射补齐）。`IR-AOT-MIR-UNEXECUTED|ops=(OP_OR)` **是基线 `1d5cfd1` 上就有的读数**（来源：共享语料里左真短路的 `bool_or`/`or_result_mod`），本节七例既不发射也不执行 `OP_OR`，**本笔不声称关闭这一格**——主线后来的语料补了它，那是别人的账。

### 五、反向判据（非对称：注入破坏一件，判据必须红）

两次注入都先 `grep -n` 印原件，再读判据输出；两次都从同一棵树（`85a4660` 的 `src/compilation/aot_mir.c`）出发、`git checkout --` 复原。

**注入 A：删掉一句越界检查**（`src/compilation/aot_mir.c:801`，[obs: 85a4660]）：

```
$ grep -n 'if (i < 0 || i >= n) return mv_nil();' src/compilation/aot_mir.c
801:"        if (i < 0 || i >= n) return mv_nil();  /* … */\n"
$ python3 - <<'EOF'   # 整行删掉
EOF
INJECTION-A applied: bounds check line removed
BUILD_EXIT=0 → MIR_EXIT=1
IR-MIR-EXEC-DIFF|case=mir_container_index_edges|interp-rc=0|mir-rc=0|interp-out=b'nil\nnil\n8\n8\n7\n9\n'|mir-out=b'3\n0\n8\n8\n7\n9\n'
IR-MIR-EXEC-DIFF|case=mir_container_new_array_empty|interp-rc=0|mir-rc=0|interp-out=b'nil\n'|mir-out=b'0\n'
IR-MIR-VERDICT|FAIL|tree=85a466084a6e917d4e8edf62d5ff54df5bc7ebe2|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

注意这两行的形状：**rc 相同（都是 0）、字节不同**。删掉那句检查之后，负下标读到的是池里**计数槽**（`3`），空数组的 `a[0]` 读到槽外的 `0`——这正是那句检查的力气所在。如果判据只看 rc，这一格会**假绿**。

**注入 B：把读值改成常数**（`src/compilation/aot_mir.c:802`，[obs: 85a4660]）：

```
$ grep -n 'return mv_arrmem[iv + i];' src/compilation/aot_mir.c
802:"        return mv_arrmem[iv + i];\n"
$ python3 - <<'EOF'   # 换成 return mv_int(0);
EOF
INJECTION-B applied: read value replaced with a constant 0
MIR_EXIT=1
IR-MIR-PROBLEM|mir_container_new_array: the MIR route printed b'0\n0\n0\n' rc=0, the interpreter b'1\n2\n3\n' rc=0
IR-MIR-PROBLEM|mir_container_new_array_mixed: the MIR route printed b'0\n0\n0\n' rc=0, the interpreter b'hi\n2\ntrue\n' rc=0
IR-MIR-PROBLEM|mir_container_index_get: the MIR route printed b'nil\nnil\nnil\n' rc=0, the interpreter b'6\n7\n5\n' rc=0
IR-MIR-PROBLEM|mir_container_index_edges: the MIR route printed b'nil\nnil\n0\n0\n0\n0\n' rc=0, the interpreter b'nil\nnil\n8\n8\n7\n9\n'
IR-MIR-VERDICT|FAIL|tree=85a466084a6e917d4e8edf62d5ff54df5bc7ebe2|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

**复原**：两次都是 `git checkout -- src/compilation/aot_mir.c` → `BUILD_EXIT=0` → `MIR_EXIT=0`、`109` 例 `exec=same`。工具里没有叫 `INJECTED` 的计数读数（`grep -rn INJECT tools/*.py` 无命中），所以「计数归一」在这里的证据只能是这三行复原读数，不是某个数字。

### 六、诚实边界

1. **只读索引只做 `INDEX_GET`**。`INDEX_SET`、`NEW_DICT`、`NEW_SET` 明确不做，仍然拒译——它们是下一节的内容，本节一个字节都没碰。
2. **数组不是标量，今天也没有「容器打印」这条路**。量出来的对照：解释器 `a = [1, 2, 3]` + `say a` 打 `[1, 2, 3]`、`a == a` 答 `true`、`a == b` 答 `false`（rc=0）；本路线这几例都是 **rc=134（abort）**、无 stdout。原因是 `mv_as_nv` 没有 `MV_ARR` 的臂（`case MV_BOO: return NV_BOO; default: abort();`），而打印/比较/算术都从 `mv_as_nv` 出去。这是一条**停止**，不是错误答案，也不假装能打印。
3. **常量越界只在基本块内可证**。`prescan_constant_subscripts` 在任何块汇合点清空长度/下标知识，所以「先建数组、再经一个 `if` 之后读常量下标」**不拒译**，落回运行期答 nil（与解释器一致）。保守，不完备；完备性本节不承诺。
4. **字符串下标出一字节新串**是照解释器 `strdup` 逐字节抄的（`alloc` 记账里 `3|6` 就是这三次的 2 字节 × 3）。**数组臂不同**：解释器的 `vm_array_get` 会把**非驻留**串重新 intern（`src/vm/vm.c:1006-1009`，[obs: 1d5cfd1]），本路线**没有**那条 intern 路径——今天数组里的串只可能是驻留串（元素来自 `LOADK_STRING` 或嵌套 `NEW_ARRAY`），所以读数一致；但「先 `+` 拼出一个串、放进数组、再读出来」这种形状本节**没有量过**，不声称。
5. **容器嵌套深度不受限**（数组套数组照常，`mir_container_index_get` 咬了 `b[0][1]`），但**没有环形引用检测**：自引用容器会把 `mv_as_nv` 之外的路径留给 abort，本节没有量过。
6. **`mv_free_all` 只在程序退出时清扫**，所以 `live` 永远是 0、`freed` 永远等于 `allocs`——这是**退出清扫**的读数形状，不是「每个数组字面量都被及时释放」。一次循环里建数组会一直长到退出，与解释器的池增长同形，本节没有做回收。
7. **`OP_NEW_ARRAY` 的 `r2` 不读**：dump 证明编译器恒发 0；如果将来编译器把「首个元素寄存器」放进 `r2`，本节的 `op_regs` 会**漏掉一个操作数**，读数会以「读了一个没写过的寄存器」的形态红。这一条写在注释里（`src/compilation/aot_mir.c:1083` 附近，[obs: 85a4660]）。
8. **全局寄存器池上限 `REG_LIMIT` 之外的容器**（超过 4096 个态）本路线不追踪；本节没有量过那个规模的语料。

### 七、与主线和在途分支的关系（写给合并的人）

本节按派工建在 `1d5cfd1` 上，工作期间 `origin/main` 已前进到 `cdd5a11`（合入 `mirval-concat2` 与 `OP_OR` 语料）。我**故意不跟进、不 rebase**，所以：

- 主线 `cdd5a11` 已经有一节 `§10.112`（concat 链）与 `§10.111`（`OP_OR` 短路），本节按「接着本分支最大节号往下排」写成 `§10.112`，**合并时会撞号**，需要在合并提交里顺延为 `§10.112`（正文一字不改，只改标题行与节号引用）。
- `IR-AOT-MIR-UNEXECUTED|ops=(OP_OR)` 在本分支的基线上就是这一格，合并后它会自己变成 `(none)`；**本笔不声称关闭它**。
- `IR-AOT-MIR-PIN|match=no|tree-moved=yes` 在本分支上是正当的（pin 树 `2477ece` 早于本节代码），收尾时按仓库惯例 `--pin` 重钉。

本节**没做**的事，逐条：`INDEX_SET`/`NEW_DICT`/`NEW_SET`、容器打印与容器相等、驻留复用、及时回收（非退出清扫）、环形引用检测、`REG_LIMIT` 之外的容器、以及 `IR-AOT-MIR-UNEXECUTED` 那一格。写了「做了」而没写「没做」的地方，上面逐条补上了。
## §10.113 零分配组的下一格：`OP_MIN`/`OP_MAX` 是**集合归约**，所以这一格补的是「值域上的 nil」（aot-mir，A 类第十项）

### 一、这笔债的来源与日期

**来源**：cdd5a11 上 `IR-AOT-MIR-NOT-YET|op=OP_MIN|n=1` 与 `op=OP_MAX|n=1`。看起来是「零分配组」的机械下一格：补一个「两输入选一、不分配」的折叠。**读真身之后不是。**

- 操作数布局在 `src/compiler/bytecode.h:49-50`：`OP_MIN,           /* min: r1 = min(r2) */` 与 `OP_MAX,           /* max: r1 = max(r2) */` —— 只有**一个**源操作数 `r2`，第三操作数不存在；编译器两处 emit 都在 `src/compiler/compiler.c`：`:1025` 临时值原地（`operand, operand`）、`:1031` 写进变量 `r`。 [obs: 0ff78e2]
- 语义在 `src/vm/vm.c:4357` 的 `L_MIN` 与 `:4362` 的 `L_MAX`：两处都是 `Value src = R[ins.r2]; set_minmax(vm, &src, &R[ins.r1], 0/1);`，最后一位是 `isMax`。 [obs: 0ff78e2]
- `set_minmax` 本体的开头（`src/vm/vm.c:2476`）： [obs: 0ff78e2]

```
static void set_minmax(VM *vm, Value *src, Value *dst, int isMax) {
    dst->type = VAL_NIL; dst->ival = 0;  dst->sval = NULL;
    if (src->type != VAL_SET) return;
```

`dst->type = VAL_NIL` 是**函数体第一条语句**（本树 `src/vm/vm.c:2477`），`if (src->type != VAL_SET) return;` 是第二条（`:2478`）。也就是说 `min`/`max` 不是算术二元运算，是**集合归约**，而且**只有集合才可能非 nil**。 [obs: 0ff78e2]

- 语法层也没有二元 `min(a, b)`：`min`/`max` 是词法关键字（`src/lexer/lexer.c:47`），按一元表达式解析（`src/parser/parser.c:640`）；`min(1, 2)` 是集合区间拼写（先 `OP_SET_INTERVAL` 再 `OP_MIN`，实测见第五节）。 [obs: 0ff78e2]

日期 **2026-11-07**。

### 二、缺口原文（实现之前，本树）

六条 MIR-only 用例先立、先跑，红的原文（`why` 全是 `unsupported-opcode`，`op_regs` 返回 `-1` 的直接结果）：

```
IR-MIR-MIR-ONLY|case=mir_minmax_int|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_minmax_boundary|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_minmax_float|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_minmax_string|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_minmax_nil|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_minmax_var|mir=refused|why=unsupported-opcode
IR-MIR-PROBLEM|mir_minmax_int: the MIR route refused OP_MIN -- see IR-MIR-NOT-YET (the interpreter ran it)
...
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|...|mir-only-cases=24|mir-only-same-as-interpreter=18
IR-MIR-VERDICT|FAIL|tree=cdd5a11a940239f108683aca8775ed1c034fcf44|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
MIR_EXIT=1
```

同一批读数的 `NOT-YET` 计数逐条变化（每跑一条用例就重算一次）：`(OP_MIN:2,OP_MAX:2)` / `(OP_MIN:3,OP_MAX:2)` / `(OP_MIN:2,OP_MAX:1)` / `(OP_MIN:1,OP_MAX:1)` —— `min` 用例把 `OP_MIN` 计满、`max` 用例把 `OP_MAX` 计满，两条 arm 各有独立红证据。

### 三、判据先立：六个用例，十二条读数

`tools/aot_mir.test.py:619-627` 新增六条 MIR_ONLY（只比 stdout 字节 + 分配读数）： [obs: 0ff78e2]

| 用例 | 覆盖 | 解释器答案 |
|---|---|---|
| `mir_minmax_int`（`:619`） | `min(5)`/`max(5)`/`min(-7)`/`max(-7)`，整数正负混合 | 四行 `nil` |
| `mir_minmax_boundary`（`:620`） | `min(2147483647)`/`max(-2147483648)`/`min(-9223372036854775807)`/`max(9223372036854775807)`，i32 与 i64 两头 | 四行 `nil` |
| `mir_minmax_float`（`:623`） | `min(-0.0)`/`max(-0.25)`/`min(1e-300)`/`max(0.0)`/`min(3.5)` | 五行 `nil` |
| `mir_minmax_string`（`:625`） | `min("b")`/`max("b")`/`min("")` 标量字符串操作数 | 三行 `nil`（**不是**字典序比较） |
| `mir_minmax_nil`（`:626`） | `min(nil)`/`max(nil)` | 两行 `nil` |
| `mir_minmax_var`（`:627`） | `x = 3; min(x)` 与 `q = -2; max(q)`，变量拼写 | 两行 `nil` |

共享语料（86 条）一个字节没动，`tools/aot_cover.pin.json` 没动，`ALLOCS_EXPECT` 没动（六条都落在默认 `(0, 0)`：不分配也不许漏）。

### 四、为什么这一格能只有一行：值域上的三道闸

MirVal 没有 set 标签（`src/compilation/aot_mir.c` 的 `kMirValBlock` 只有 `MV_INT`/`MV_FLT`/`MV_BOO`/`MV_NIL`/`MV_STR` 五个标量标签）；操作数的三个生产者全部在子集之外：

```
say min(Z)                        -> why=illegal-operand|detail=...global 1 is the preset set global 'Z'...   (src/compilation/aot_mir.c:1399) [obs: 0ff78e2]
p = 1, 2, 3 / say min(p)          -> why=illegal-operand|detail=...leaves the argument stack unbalanced: asp=3  (src/compilation/aot_mir.c:1433-1434) [obs: 0ff78e2]
say min(1, 2)                     -> why=illegal-operand|detail=...asp=2
```

预设集合全局按名拒（表在 `src/compilation/aot_mir.c:976` 的 `kPresetGlobals[23]`），`OP_NEW_SET`/`OP_SET_INTERVAL` 未降低（它们的 pop 不计 `asp`，所以容器检查先红）。 [obs: 0ff78e2]

所以：**任何被接受的流，`R[r2]` 都是非集合**，解释器第一条语句答 `VAL_NIL`，且无论 `isMax` 是几都一样。这就是为什么这一格不是折叠，而是「值域上的 nil」——一行就是全语义，方向参数在这个值域上没有方向。

### 五、改动（三处 + 一个守卫，零分配）

1. `src/compilation/aot_mir.c:1087`：`op_regs` 加 `case OP_MIN: case OP_MAX: return R1 | R2;`（第三操作数不读，不占寄存器）。 [obs: 0ff78e2]
2. `src/compilation/aot_mir.c:1693-1694`：`emit_one` 加两臂，写法与 `OP_IS_NIL` 同一行内风格： [obs: 0ff78e2]

```
    case OP_MIN:          buf_fmt(b, "    v[%d] = mv_minmax(v[%d], 0);", r1, r2); return 0;
    case OP_MAX:          buf_fmt(b, "    v[%d] = mv_minmax(v[%d], 1);", r1, r2); return 0;
```

3. `src/compilation/aot_mir.c:402` 起（`kMirValBlock` 内）：`mv_minmax` 与一段长注释（WHY / WHAT / DOES-NOT-COVER / DELETE CONDITION / MEASURED BY），函数体： [obs: 0ff78e2]

```
static inline MirVal mv_minmax(MirVal a, int isMax) {
    (void)isMax;
    if (a.type != MV_INT && a.type != MV_FLT && a.type != MV_BOO &&
        a.type != MV_NIL && a.type != MV_STR)
        abort();   /* a tag this block has never seen: not a scalar, so a set */
    return mv_nil();   /* set_minmax's first statement, for a non-set operand */
}
```

`abort()` 是**死代码**（五个标签今天穷尽），但它是一个 **DELETE 检测器**，不是防御分支：将来给 MirVal 加第六个标签（集合）而不重读这里，程序会**崩**而不是静默答 nil。

零分配：没有 `mv_alloc`。`mv_nil()` 只是把五个标量字段写成 0，不碰 heap；`abort` 路径不分配。

### 六、读数（原始）

`MIR_EXIT=0`、`IR-MIR-VERDICT|PASS`：

```
IR-MIR-MIR-ONLY|case=mir_minmax_int|mir=accepted|cc=ok|exec=same|rc=0|bytes=16|stdout-hex=6e696c0a6e696c0a6e696c0a6e696c0a
IR-MIR-MIR-ONLY|case=mir_minmax_boundary|mir=accepted|cc=ok|exec=same|rc=0|bytes=16|stdout-hex=6e696c0a6e696c0a6e696c0a6e696c0a
IR-MIR-MIR-ONLY|case=mir_minmax_float|mir=accepted|cc=ok|exec=same|rc=0|bytes=20|stdout-hex=6e696c0a6e696c0a6e696c0a6e696c0a6e696c0a
IR-MIR-MIR-ONLY|case=mir_minmax_string|mir=accepted|cc=ok|exec=same|rc=0|bytes=12|stdout-hex=6e696c0a6e696c0a6e696c0a
IR-MIR-MIR-ONLY|case=mir_minmax_nil|mir=accepted|cc=ok|exec=same|rc=0|bytes=8|stdout-hex=6e696c0a6e696c0a
IR-MIR-MIR-ONLY|case=mir_minmax_var|mir=accepted|cc=ok|exec=same|rc=0|bytes=8|stdout-hex=6e696c0a6e696c0a
IR-MIR-ALLOCS|case=mir_minmax_int|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_minmax_boundary|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_minmax_float|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_minmax_string|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_minmax_nil|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_minmax_var|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=24|mir-only-same-as-interpreter=24
IR-MIR-VERDICT|PASS|tree=cdd5a11a940239f108683aca8775ed1c034fcf44|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`stdout-hex=6e696c0a...` 是若干行 `nil\n`；`bytes` 与行数吻合（4/4/5/3/2/2 行 → 16/16/20/12/8/8 字节）。`IR-MIR-LAYOUT-ASSERT|files=86|need=6|files-complete=86|missing=(none)` 原样（`MirVal` 与 `Value` 逐字节兼容的六条 `_Static_assert` 没动）。

覆盖读数（`COVER_EXIT=0`）：

```
IR-AOT-MIR-LOWERING|handles=37|of=67|unknown=30|table=37|emitter=37|reported=37|agree=yes|basis=...
IR-AOT-MIR|accepted=110|refused=0|same-as-interpreter=110|of=29|emittable=37|of-ops=67|not-emittable=30|corpus=inline-cases:86|mir-only-cases:24|beyond-ast-denominator=8|basis=...
IR-AOT-MIR-UNEXECUTED|ops=(none)|basis=...|in-ast-denominator=no|counted-in-covered=no|emittable=yes
IR-AOT-MIR-COVER|covered=29|of=29|denominator=tools/aot_cover.pin.json:covered_ops|coverage-claim=none
```

`handles 35→37` 与预期一致；`accepted 104→110` 是六条新用例，不是共享语料变了（共享语料仍 86 条、`of=29` 仍 29）；`UNEXECUTED|ops=(none)` 没有冒出新的 opcode 名字（`OP_MIN`/`OP_MAX` 落在 `IR-AOT-MIR-BEYOND` 那一行的名单里，那是「执行过但不在 AST 分母里」的另一本账）。

### 七、反向判据（非对称两次）

**① 方向注入**：先 `grep -n` 印原件 `src/compilation/aot_mir.c:407` 的 `return mv_nil();   /* set_minmax's first statement, for a non-set operand */`，改成 `return isMax ? a : mv_nil();`（只剩 `max` 保留候选）。红在**字节**上，不是崩溃： [obs: 0ff78e2]

```
IR-MIR-EXEC-DIFF|case=mir_minmax_int|interp-rc=0|mir-rc=0|interp-out=b'nil\nnil\nnil\nnil\n'|mir-out=b'nil\n5\nnil\n-7\n'
IR-MIR-ALLOCS|case=mir_minmax_int|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-PROBLEM|mir_minmax_int: the MIR route printed b'nil\n5\nnil\n-7\n' rc=0, the interpreter b'nil\nnil\nnil\nnil\n' rc=0
IR-MIR-VERDICT|FAIL|tree=cdd5a11a940239f108683aca8775ed1c034fcf44|accepted=5|refused=0|differ=0|c-identical=0|c-diff=5|perturb-stdout-changes=5
```

五条 `IR-MIR-PROBLEM`、`INJ1_EXIT=1`。**非对称**：分配那本账一行没动（仍 `allocs=0|...|ok=yes`），只有字节账红了 —— 「零分配」与「值语义」两个判据互相独立。

**② 期望值注入**：印 `tools/aot_mir.test.py:214-218` 的 `ALLOCS_EXPECT` 原件，给 `mir_minmax_boundary` 加一条正期望 `(1, 8)`。红在期望值那条线上，字节与执行不变： [obs: 0ff78e2]

```
IR-MIR-ALLOCS|case=mir_minmax_boundary|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=1|expect-bytes=8|ok=no
IR-MIR-MIR-ONLY|case=mir_minmax_boundary|mir=accepted|cc=ok|exec=same|rc=0|bytes=16|stdout-hex=6e696c0a6e696c0a6e696c0a6e696c0a
IR-MIR-PROBLEM|mir_minmax_boundary: allocation count off -- got allocs=0|bytes=0, the criterion demands allocs=1|bytes=8 (a site that mallocs without registering reports 0|0)
```

`INJ2_EXIT=1`。还原后 `grep -c INJECTED` 两个文件都为 `0`，`BUILD_EXIT=0`。

**另一次诚实的反向读数（不是红的）**：把 emit 里字面 `0/1` 对调，**按设计无变化** —— 这条路上 `isMax` 没有方向（第四节）。所以「方向」只能从语义表注入（上面①就是这么做的）；字面参数对调是等价变换，不是漏掉的注入。

### 八、诚实边界

1. **折叠本身不可测，也不声称测了。** `min`/`max` 真正的语义（`set_minmax` 的折叠段 `src/vm/vm.c:2481-2502`：整数按 `long long`、浮点按 `double`、字符串按 `strcmp`、串/数混合拒）**今天不可达**：MIR 里造不出集合。本节的 nil 不是「实现了 min」，是「在 MIR 可达值域上恰好与解释器**同一条语句**同答」。DELETE CONDITION 钉在「给 `MirVal` 加集合标签 / 降低 `OP_NEW_SET`」的那一笔提交上。 [obs: 0ff78e2]
2. **NaN 不可测。** NaN 只能经 builtin 到达（`say sqrt(-1)` → `nan`，`say 1.0/0.0` → `inf`），而 `OP_CALL_BUILTIN` 不在子集（`asp=1` 拒）；把 NaN 塞进集合也不行（集合拼写本身被拒）。解释器确有确定答案（集合含 NaN 时 `val_cmp` 的 `d > d` 全 false ⇒ 当相等处理），但本节不「照抄」它，因为一条也到不了。
3. **-0.0 的符号不可观测。** 两路 stdout 都印 `0`（实测 `say -0.0` → `0`）；并且 `p = -0.0, 0.0` 是**解析错**（`expected 'expression', but got '-'`），集合里也测不了。`mir_minmax_float` 的 `min(-0.0)` 只覆盖到「字面量作标量操作数」这一层。
4. **带负号的集合字面量写不出来。** `p = -5, 3, -1` → `expected 'expression', but got ',' (type 101)`；能写的那种也不行（`asp` 先红，见第 5 条）。元素级整数正负混合与边界因此**不可测**，只测了标量操作数。
5. **`min(1, 2)` 的拒译理由是 `asp` 不平衡，不是 unsupported-opcode。** 这是 prescan 的容器检查先于 opcode 检查，属既有形状（`OP_NEW_SET`/`OP_SET_INTERVAL` 未降低 ⇒ 它们的 pop 不计 `asp`）。不是本节引入的，但它意味着「集合形态的 min」的拒绝理由对读者不直白；单独记这里，**不顺手改**。
6. **预设集合全局的答案非 nil，但同样不可达。** `min(Z+)` / `max(Z+)` / `min(Z)` 解释器答 `1` / `nil` / `nil`（`set_minmax` 的 kind==1 分支 `src/vm/vm.c:2515-2522`），MIR 全部 `illegal-operand`（按名拒）。本节不假装覆盖它们。 [obs: 0ff78e2]
7. **`abort()` 守卫是死代码**（五个标签今天穷尽），它是 DELETE 检测器：新增第六个标签会崩而不是静默答 nil。故意不加「容器值」防御分支，因为 MirVal 今天就装不下集合。
8. **不特判常量。** 没有加任何常量折叠；`min(5)` 走的就是 `LOADK_INT` → `OP_MIN` 的同一条路径（dump 原文四条指令：`LOADK_INT r1=1 r2=5` / `OP_MIN r1=1 r2=1 r3=0` / `OP_SAY` / `OP_HALT`）。
9. **字节判据的 oracle 是解释器本身**（harness 里没有独立的值期望表），所以「把期望值改掉」那次注入改的是 `ALLOCS_EXPECT` 那一行 —— 本节唯一按用例声明的期望；值那一侧只能靠注入实现反向（第七节①）。
10. **`beyond-ast-denominator 4→8` 是六条新用例带来的，不是 MIR 发了新东西。** 变的只是「有人跑过 `OP_MIN`/`OP_MAX`」；`emittable 35→37` 是这一对 opcode。
11. **`unknown=30` 没动。** `of=67` 的分母没变，剩下 30 个未知 opcode 是别的债，本节不声称碰过。
12. **引用形式**：本节的引用一律是 `路径:行号` 形（不是「第 N 行」的散文形），所以不需要树戳；若将来有人改成散文形，必须带 `[obs: cdd5a11]` 这样的树戳。



## §10.115 `OP_IN` 是**容器类型分派**，不是「算出一个布尔」——这一格补的是「非数组容器一律假」（aot-mir-container，A 类第十一项）

这一节补 `OP_IN`（成员测试：`x in a`）。它看起来像「算一个布尔」，实际不是：解释器的 `L_IN` 只做一次转发，全部语义在被转发到的那个函数里，而那个函数**按右操作数的类型分派**。所以这一格不是「补一个比较」，是「补一张分派表」；表里有两格在这条路线上**结构上不可达**，这两格必须写明理由，不能假装补了。

### 一、这笔债从哪来

基线上（`f7a7dc0`）`OP_IN` 在「未降低」账本里。它的表现不是「答错」，是**整条拒译**：

```
a = [1, 2, 3]
say 2 in a
```

旁边的数组字面量已经降低（`OP_NEW_ARRAY`）、`say` 也已经降低，但 `OP_IN` 没有发射器，于是整条程序 `why=unsupported-opcode`。这类「被邻居挡住」的格子正是 A 类账本要一格一格清的原因。

操作数与语义，三处原文：

- 布局 `src/compiler/bytecode.h:48` —— `in: r1 = (r2 in r3)`：`r1` 收结果布尔，`r2` 是**元素**，`r3` 是**容器**。 [obs: f8cc5af]
- 编译器同形的一处 `src/compiler/compiler.c:727` —— `emit(comp->curBC, OP_IN, result, left, right)`。 [obs: f8cc5af]
- 执行 `src/vm/vm.c:4351-4356` —— `L_IN` 读 `R[ins.r2]`/`R[ins.r3]`，调 `set_contains_or_subset(vm, &la, &rb)`，把结果写成 `VAL_BOOL`。 [obs: f8cc5af]

所以答案不在 `L_IN` 里，在 `set_contains_or_subset` 里（`src/vm/vm.c:2381`）。 [obs: f8cc5af] 它的分派顺序是我要照抄的形状：

1. `b` 是**集合**（`:2382-2395`）：`a` 数组 → 逐个 `set_contains`；`a` 集合 → `set_subset`；否则 `set_contains(b, a)`。
2. `b` 是**数组**（`:2397` 起）：`a` 数组 → **子集**，逐元素 `val_eq`（`:2403-2413`）；`a` 集合 → 逐个 `val_eq`；否则 → **成员**，`val_eq(a, &items[j])`（`:2425-2428`）。
3. 其余一律 `return 0`（`:2432`）。

这三条的语义在 MIR 线上一条都不能发明。尤其是第 2 条的**元素比较**用的是 `val_eq`（`src/vm/vm.c:413`）。 [obs: f8cc5af] 而 `val_eq` 的数组行是**身份**比较：`if (a->type == VAL_ARRAY) return a->ival == b->ival;`（`:420`）——`[1] == [1]` 是 false，`x == x` 是 true。这一行不是我的选择，是 `val_eq` 自己写的。

### 二、解释器给什么答案：逐组合实测，不是推导

下面每一条都在本树上跑过解释器（不是从上面的代码推导出来的）。这张表的用处是：它把「这条路上不可达的那两格」跟「真的会答错的格子」分开。

| 表达式 | 解释器 | 为什么 |
| --- | --- | --- |
| `2 in [1, 2, 3]` | true | `b` 数组、`a` 标量 → 成员，`val_eq` |
| `5 in [1, 2, 3]` | false | 成员未命中 |
| `[1, 2] in [1, 2, 3]` | true | 子集，逐元素 `val_eq` |
| `[1, 4] in [1, 2, 3]` | false | 子集缺元素 |
| `[] in [1, 2, 3]` | true | 空集是任何集合的子集（`:2405-2406`）|
| `7 in []` / `[1, 2] in []` | false | 空容器里什么都找不到 |
| `1.0 in [1, 2]` | true | `val_eq` 的数族互比按 `double`（`:425-427`）|
| `2.5 in [1, 2]` | false | 同上，逐值比 |
| `1.5 in [1]` | false | **不是截断**：`double` 比较，`1.5 != 1.0` |
| `true in [1]` / `0 in [false]` | true | bool 与数按 1/0 互比（同上）|
| `nil in [nil, 1]` | true | nil 与 nil 相等 |
| `x in [x]` | true | 同一句柄 |
| `"b" in ["a", "b"]` | true | 字符串按 `strcmp` |
| `"bc" in ["a", "b"]` | false | 不是子串判定，是整元素相等 |
| `[1] in [[1], [2]]` | false | 数组按**身份**，元素是新句柄 |
| `"b" in "abc"` | false | **没有子串语义**：`b` 不是数组也不是集合 → `:2432` |
| `"abc" in "abc"` | false | 同上，即使一模一样 |
| `1 in 5` / `1.0 in 5` / `nil in 5` / `"a" in 1` | false | 标量右操作数一律假 |
| `"k" in {"k": 1}` | false | 字典**不是键集合**：也是 `:2432` |
| `2 in {1, 2, 3}` | true | 整数集合走 i64 二分（`src/vm/vm.c:2020`）。 [obs: f8cc5af] |
| `2.0 in {1, 2, 3}` | false | **二分只认 `VAL_INT`**：浮点不进 i64 查找，线性 `val_eq` 又没命中 |
| `2 in {2.0, 3.0}` | true | 非整数集合走线性 `val_eq` |

这张表里最重要的是两行：`"b" in "abc"` 是 **false**（没有子串），`"k" in {"k": 1}` 也是 **false**（字典没有键测试）。「直觉上该是 true」的两条恰恰是解释器的答案，而这条路线要搬的是**解释器的答案**，不是直觉。

### 三、判据先立：五条 `MIR_ONLY`，先红

在 `tools/aot_mir.test.py` 的 `MIR_ONLY` 里加五例（程序原文在测试文件里，这里只记形状）：`mir_in_array_hit`（七行命中，含浮点/bool/nil/字符串/数组⊆数组/空数组）、`mir_in_array_miss`（七行未命中）、`mir_in_string`（六行：四真二假，含 `not (x in "abc")`）、`mir_in_scalar`（六行标量右操作数）、`mir_in_computed`（十行：元素是算出来的、容器是变量、数组作元素、还有一行 `say [1] != c[0]`）。

先跑的原文（本节所有读数都是跑出来的，不是预期的）：

```
IR-MIR-MIR-ONLY|case=mir_in_array_hit|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_in_array_miss|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_in_string|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_in_scalar|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_in_computed|mir=refused|why=unsupported-opcode
IR-MIR-PROBLEM|mir_in_array_hit: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_IN:7)
IR-MIR-PROBLEM|mir_in_array_miss: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_IN:7)
IR-MIR-PROBLEM|mir_in_string: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_IN:6)
IR-MIR-PROBLEM|mir_in_scalar: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_IN:6)
IR-MIR-PROBLEM|mir_in_computed: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_IN:8)
IR-MIR-VERDICT|FAIL|tree=f7a7dc0e42b621daea609f0d03b42c2291a1cb75|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`MIR_EXIT=1`。注意每例的 `(OP_IN:n)` 各自是 7/7/6/6/8，不是同一个数字：五条程序确实各走各的形状。

### 四、改动：六处，每处一个理由

1. **`op_regs`：`case OP_IN: return R1 | R2 | R3;`**（`src/compilation/aot_mir.c:1395`）。 [obs: f8cc5af] 三个操作数都是寄存器，没有池下标、没有元素个数、没有偏移。
2. **`prescan` 不加 `case OP_IN`。** 这是**故意的**，理由不是省事：`tools/aot_mir_cover.test.py` 的 operand 检测器把 `case OP_X:` 读成「该 opcode 核一个索引」，一个不核任何索引的 case 会红成 `dead=(OP_IN)|agree=no`（`OP_CONCAT`/`OP_NEW_ARRAY`/`OP_INDEX_GET` 写在外面就是同一个原因）。本节的读数里 `dead=(none)|agree=yes`（第九节）。
3. **`emit_one`：一行**（`src/compilation/aot_mir.c:2078`）： [obs: f8cc5af] `case OP_IN: buf_fmt(b, "    v[%d] = mv_in(v[%d], v[%d]);", r1, r2, r3); return 0;`。
4. **`mv_in`**（`src/compilation/aot_mir.c:1094`）。 [obs: f8cc5af] 臂的顺序照抄 `set_contains_or_subset`：数组对数组是子集（空 `a` 为真），数组对标量是成员，其余是 false。零分配：只读池、调 `mv_eq`，从不造值。
5. **`mv_eq`/`mv_ne` 的数组行**（`src/compilation/aot_mir.c:753`、`:760`）。 [obs: f8cc5af] 这处**不是顺手改的**，是必需的：子集臂要比较的元素本身可以是数组（`[[1], [2]]` 是本路线自己造的），而旧的 `mv_eq` 走到 `nv_eq(mv_as_nv(...))`，`mv_as_nv` 在 `MV_ARR` 上 `abort()` —— 于是程序**被接受、编译、再在运行时被杀**，无 stdout，比拒译更难解释。`val_eq` 的数组行（身份，`:420`）本来就是解释器的规则，两条写法（`in` 与 `==`/`!=`）必须同一行，否则 `say [1] == [1]` 答 false 而 `say [1] != c[0]` 直接停。
6. **`#define MV_ARR 6` 上移**（`src/compilation/aot_mir.c:91`）。 [obs: f8cc5af] **实测发现的**：生成的 C 按块顺序编译，而等号对在前、容器块在后，`cc` 报 `error: 'MV_ARR' undeclared (first use in this function)`。上移到标签组旁边是唯一的修法（不是把等号对往下移，因为 `mv_in` 两样都要）。

### 五、分配账本：先量后写

`OP_IN` 本体**零分配**。五次读数（先跑出来，之后才写进 `ALLOCS_EXPECT`）：

| 用例 | allocs | bytes |
| --- | --- | --- |
| `mir_in_array_hit` | 3 | 1792 |
| `mir_in_array_miss` | 3 | 1792 |
| `mir_in_string` | 1 | 256 |
| `mir_in_scalar` | 0 | 0 |
| `mir_in_computed` | 3 | 1792 |

为什么是这些数：容器字面量放进 `MV` 项池，首次用 8 槽 × 32 字节 = 256 字节，之后**倍增**（8→16→32 槽），旧块运行期不释放、退出时统一清扫，所以 `freed` 恒等于 `allocs`。三个 1792 的用例字面量超过 16 槽 → 走到 32 槽那块（256+512+1024）。`mir_in_string` 的容器只有一个 3 槽 `["a","b"]`，只吃第一块（256）；它的字符串走**驻留串池**，不花块。`mir_in_scalar` 没有容器字面量，`0|0` 就是这一格的零：如果 `mv_in` 每次测试都分配，它会先在这一行露出来。

### 六、反向判据（非对称，两次）

**① 命中判成未命中**（把成员循环换成恒 `mv_boo(0)`）：`mir_in_array_hit` 的 stdout 字节必变，`mir_in_string` 的第 5 行（`"b" in ["a","b"]`）也跟着变：

```
IR-MIR-EXEC-DIFF|case=mir_in_array_hit|interp-rc=0|mir-rc=0|interp-out=b'true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n'|mir-out=b'false\nfalse\nfalse\nfalse\nfalse\ntrue\ntrue\n'
IR-MIR-PROBLEM|mir_in_array_hit: the MIR route printed b'false\nfalse\nfalse\nfalse\nfalse\ntrue\ntrue\n' rc=0, the interpreter b'true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\n' rc=0
IR-MIR-MIR-ONLY|case=mir_in_array_hit|mir=accepted|cc=ok|exec=diff|rc=0|bytes=40|stdout-hex=66616c73650a66616c73650a66616c73650a66616c73650a66616c73650a747275650a747275650a
```

`MIR_EXIT=1`。**注意 rc 两边都是 0、只有字节不同** —— 只看 rc 的判据在这里会假绿。

**② 未命中冠以命中**（成员循环末尾的 `return mv_boo(0)` 改 `mv_boo(1)`）：这次命中的是**另一例**，`mir_in_array_hit` 保持绿：

```
IR-MIR-EXEC-DIFF|case=mir_in_array_miss|interp-rc=0|mir-rc=0|interp-out=b'false\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\n'|mir-out=b'true\ntrue\ntrue\ntrue\nfalse\ntrue\nfalse\n'
IR-MIR-PROBLEM|mir_in_array_miss: the MIR route printed b'true\ntrue\ntrue\ntrue\nfalse\ntrue\nfalse\n' rc=0, the interpreter b'false\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\n' rc=0
IR-MIR-VERDICT|FAIL|tree=f7a7dc0e42b621daea609f0d03b42c2291a1cb75|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

**复原**（两次都在工作树上改回，`grep -c INJECTED` 对两个文件都读 `0`）：

```
[BUILD_EXIT=0]
[MIR_EXIT=0]
122          # grep -c "exec=same" .scratch/restore.log
IR-MIR-VERDICT|PASS|tree=f7a7dc0e42b621daea609f0d03b42c2291a1cb75|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

工具里没有叫 `INJECTED` 的计数读数（`grep -rn INJECTED tools/ --include=*.py` 在本树上是零命中），所以「计数归一」在这里的证据只能这几行复原读数，不是某个数字。

### 七、没做什么（逐条）

1. **集合容器臂没做**（`b` 是 `VAL_SET`）。不是忘了，是**结构不可达**：`OP_NEW_SET`/`OP_SET_INTERVAL` 未降低，23 个预设集合全局按名拒，集合字面量把参数栈弄不平衡（实测 `p = 1, 2, 3` + 成员测试在 `asp=3` 处拒；`say 2 in Z` 在 `Z` 名字上拒）。所以解释器那条「`2 in {1,2,3}` 走二分」的路径到不了 `mv_in`，本节不写集合臂、也不声称支持。
2. **字典容器臂没做**。`d = {"k": 1}` + `say "k" in d` 在 `OP_IN` **之前**就拒（`OP_NEW_DICT` 的 pop 不计 `asp`），而且解释器对这个程序本来就答 false（`:2432`），字典在解释器里不是键集合。
3. **子串语义没做**。解释器没有，`"b" in "abc"` 是 false（第二节实测）；「补一个更像人想的语义」会让这条路线的答案和它要重现的答案分道。
4. **集合⊆数组 / 无限集⊆数组** 这两个臂没做，同上不可达。
5. **`mv_tru(MV_ARR)` 仍然 `abort()`**：`if a {}` 拿数组当条件还是停程序。这是 `OP_JUMP_IF` 的债，不是等号的，也不是成员测试的。
6. **数组打印、数组排序（`OP_LT` 那对）**没动，仍是 `mv_as_nv` 的债。
7. **没有常量折叠**：`say 2 in [1, 2]` 走的就是 `LOADK` → `NEW_ARRAY` → `OP_IN` 的同一条路径，没有特判。
8. **`OP_NEW_DICT` 的 `asp` 不平衡问题**没顺手改：它是既有形状（`OP_NEW_SET` 同形），本节只记录它挡住了字典容器。

### 八、诚实边界

1. **被接受的每条流里，`L_IN` 只会走数组臂。** 这是本节最强的边界，也是最该写清的：不是「集合答不对」，是「集合根本到不了这里」。读者若看到 `2 in p` 拒译，正确的读法是第七节第 1 条，不是本节的 bug。
2. **数组相等是身份，不是逐元素。** `[1] == [1]` false、`x == x` true、`[1] in [[1],[2]]` false —— 这三条都是解释器的规则（`:420`），不是我选的。逐元素相等会在这三处全部答反，且 `mir_in_computed` 的 `[x - 1] in c` 一行会跟着反。
3. **`b` 的坏句柄分支是死代码**（`:2399`）：`mv_arr_new` 从不构造坏句柄，所以这行只是照抄解释器的活槽检查（也抄了 `mv_arr_get` 的同形检查），不是这条路线的语义。
4. **`a` 的坏句柄分支答 true**（`:2406`）：解释器对「数组的池下标越界」在子集测试里答 1（空数组视为子集），照抄；同样不可达。
5. **字符串池是驻留的**，所以 `"b" in ["a","b"]` 这类比较走指针快路径与 `strcmp` 两条都可以；本节的读数不区分它们（两条都对，且 `val_eq` 自己就是这个形状）。
6. **两个反向判据都只改「比较那一句」**，没有改臂的顺序，也没有改类型分派 —— 也就是说它们测的是「值有没有真的被比」，不是「分派表对不对」。分派表本身的直接判据是第二节的语义表 + 五条字节判据（容器类型错了，五条里必有一条字节不同）。
7. **本节不声称覆盖 `OP_INDEX_SET`**（容器写入），那是下一格；`OP_IN` 与它无交集。

### 九、计数器与主线关系

本节的读数（都在本节的代码提交上跑出来）：

- `IR-AOT-MIR-LOWERING|handles=40|of=67|unknown=27|table=40|emitter=40|reported=40|agree=yes`（基线 39|67|28）。
- `IR-AOT-MIR-OPERANDS|indexed=10|prescanned=11|checked=10|missing=(none)|dead=(none)|detector-saw=yes|agree=yes`。
- `IR-AOT-MIR-COVER|covered=29|of=29`，`IR-AOT-MIR-VERDICT|PASS|accepted=122|same-as-interpreter=122|missing=0|refused=0|beyond-ast-denominator=11`。
- `IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=1|ok=1|expect=1`（未变：本次没有新增「应该被拒」的用例）。
- `IR-AOT-MIR-PIN|match=no|tree-moved=yes` 在本分支上是**正当的**（pin 树早于本节代码），收尾按仓库惯例 `--pin` 重钉。

### 十、左操作数是容器时，这条路线上够得到，两侧答案相同

第一节的语义表里有一支是「`a` 是数组」，而它的元素本身可以是数组（`[[1], [2]]` 就是本路线自己造得出来的）。四支探针（解释器 `--no-mods run` 与 MIR `translate` → `gcc -O1` → 跑，两侧逐字相同）：[obs: 9d6741e]

| 程序 | 解释器 | MIR |
|---|---|---|
| `a = [[1], [2]]; say a == a` | `true` | `true` |
| `a = [[1], [2]]; b = [[1], [2]]; say a == b` | `false` | `false` |
| `x = [1]; say x == [1]` | `false` | `false` |
| `a = [1]; b = [[1], [2]]; say a in b; say b[0] in b` | `false` / `false` | `false` / `false` |

读法：数组作**元素**、数组作**容器**这两格都够得到且答对（子集臂用的是 `val_eq`，而数组那一行是身份比较，见 `src/vm/vm.c:420` 与 `src/compilation/aot_mir.c:753`）。[obs: 9d6741e]真正结构上不可达的只有**集合**的格子（：`src/vm/vm.c:2382-2395` 的 `set_contains`/`set_subset`），因为这条线上 `OP_NEW_SET` 不在降低表里、集合值造不出来，`k in {…}` 在 `OP_IN` 之前就被拒。所以本节写边界时用的词是**集合**，不是「左操作数是容器」。

本节按派工建在 `f7a7dc0` 上，**不 push、不 rebase、不动 main**。本分支出身的那棵树（`f7a7dc0`）带着上一笔合并留下的残留：一行 `@@PIN-GATE@@` 和半截冲突标记；本节照派工没有改动它，合并在 `03bacff` 里把它清掉了（清理笔同时补了 `tools/check_text_integrity.py` 的一格检查，见 `2dba43b`），所以本节在这次合并之后的行里不再有那几行。
## §10.116 容器写入的第一格（分支上的号是 §10.114，合并前主线已发布 §10.115，故顺延）：`OP_INDEX_SET` 写的是容器的槽，不是寄存器（aot-mir，A 类第十二项）

### 一、这笔债的来源与日期

来源是上一节自己钉下的边界句：§10.112 让数组只读，`src/compilation/aot_mir.c:864-866` 的 DOES-NOT-COVER 第一条原话是「OP_INDEX_SET is not emitted, so no program on this route can store INTO an array」；`a = [1, 2, 3]` 之后 `a[1] = 99` 今天停在 `unsupported-opcode`。日期 **2026-11-08**。 [obs: 816f6cb]

### 二、语义照着 `L_INDEX_SET` 抄，不发明

- **操作数角色与读侧不同序**：`src/vm/vm.c:3718-3720` 是 `Value *obj = &R[ins.r1]; Value *idxv = &R[ins.r2]; Value *valv = &R[ins.r3];` —— `r1` 是**容器**、`r2` 是**下标**、`r3` 是**值**，`r1` 不是目的寄存器。`src/compiler/bytecode.h:16` 的注释写 `reg[r1][r2] = reg[r3]`，读起来像赋值目标，但它只是下标记法；编译器实际发的是 `emit(comp->curBC, OP_INDEX_SET, arr, idx, val)`（`src/compiler/compiler.c:2378`），第一个操作数就是容器寄存器。 [obs: 816f6cb]
- **下标转换在数组分支内部**：`src/vm/vm.c:3722` 的 `int i = (idxv->type == VAL_INT) ? idxv->ival : (int)val_as_double(vm, idxv);` 写在 `if (obj->type == VAL_ARRAY)`（`:3721`）里面，所以非数组容器**根本不转换下标**——与读侧 `src/vm/vm.c:3679` 先转换再看容器**不对称**。实测：`x = 5; x[nosuchglobal] = 7` 解释器 rc0 静默丢弃；同一个非法下标在**读**侧是 rc1 `type_mismatch`。 [obs: 816f6cb]
- **负下标抛**：`src/vm/vm.c:3723-3726` 抛 `index_out_of_range`（stderr 一行 + rc1；stdout 只留异常之前打印过的字节）。 [obs: 816f6cb]
- **非数值下标拒**：nil / 字符串 / 数组经 `val_as_double`（`src/vm/vm.c:323-332`，default 臂是 `vm_throw_kind(vm, "type_mismatch")`）拒掉；整数、浮点（截断）、布尔（真=1）放行。 [obs: 816f6cb]
- **越界不是错**：`vm_array_set`（`src/vm/vm.c:976-999`）在 `i >= a->count` 时先 `array_ensure`（`:987`）长大、再用 `while (a->count <= i) value_copy(&a->items[a->count++], &nil);`（`:992`）填 nil，所以「写下标 5」之后读回是 nil，不是异常。 [obs: 816f6cb]
- **先克隆再落槽**：`:978-979` 的注释与 `value_copy(&tmp, v)` 是为了自赋值 `a[0] = a[2]` 与增长时重分配都安全；落槽是 `value_free(&a->items[i]); a->items[i] = tmp;`（`:995-996`）——**32 字节槽拷贝，不按 tag 分派**。
- **存活槽守卫**：`:977` 的 `if (idx < 0 || idx >= vm->arrayCount || i < 0) return;` 是静默返回。

### 三、判据先立：五条用例，先红

`tools/aot_mir.test.py` 的 `MIR_ONLY` 加五条（`:770-792`），`ALLOCS_EXPECT` 加五条（`:249-271`）： [obs: 816f6cb]

| 用例 | 覆盖 | 解释器答案 |
|---|---|---|
| `mir_index_set_int`（`:770`） | 常量下标写入并回读；**计算下标** `i = z+1`、`j = z+2` | `99 / 2 / 42 / 3 / 7` |
| `mir_index_set_overwrite`（`:774`） | 同槽覆盖两次、经第二个全局别名 `b` 读回、自赋值 `a[0] = a[2]` | `88 / 88 / 3 / 88 / 3` |
| `mir_index_set_grow`（`:778`） | 写越界下标 5（长到 6 并填 nil）、越界读回；第二个数组后建后再写第一个（**区域必须搬家**） | `9 / nil / nil / 3 / 1 / 8 / nil / 7 / 9` |
| `mir_index_set_edges`（`:784`） | 非数组容器写（静默丢弃）、字符串值、末尾一条 `type_mismatch` | `5` `hi`，rc1 |
| `mir_index_set_negative`（`:788`） | `z - 1` 负下标 → `index_out_of_range` | `2`，rc1 |

**为什么下标必须是算出来的**：常量下标会被既有的 `prescan_constant_subscripts` 先判掉——同一段流写成常量时是 `why=index-out-of-range-constant|detail=container=0 insn=20 constant subscript 5 is outside the 3 element(s) of the array built into register 4`，运行期那一臂就测不到。这条也正是第六节第 4 条要说的边界。

实现之前的红原文（`.scratch/red3.log`，`MIR_EXIT=1`）：

```
IR-MIR-MIR-ONLY|case=mir_index_set_int|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_index_set_overwrite|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_index_set_grow|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_index_set_edges|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_index_set_negative|mir=refused|why=unsupported-opcode
IR-MIR-PROBLEM|mir_index_set_int: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_INDEX_SET:3)
IR-MIR-PROBLEM|mir_index_set_overwrite: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_INDEX_SET:3)
IR-MIR-PROBLEM|mir_index_set_grow: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_INDEX_SET:2)
IR-MIR-PROBLEM|mir_index_set_edges: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_INDEX_SET:3)
IR-MIR-PROBLEM|mir_index_set_negative: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_INDEX_SET:1)
IR-MIR-VERDICT|FAIL|tree=f7a7dc0e42b621daea609f0d03b42c2291a1cb75|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

### 四、改动：三个钩子 + 一段运行期

1. `op_regs`：`src/compilation/aot_mir.c:1398` 加 `case OP_INDEX_SET: return R1 | R2 | R3;`，长注 `:1384-1397` 写明「r1 容器 / r2 下标 / r3 值，掩码里**没有目的寄存器**（处理臂从不写 `R[ins.r1]`），三个仍全进掩码，因为 generic bound loop 是唯一的界」。 [obs: 816f6cb]
2. `prescan`：`src/compilation/aot_mir.c:1892` 加 `if (op == OP_INDEX_SET) continue;`，紧贴 `if (op == OP_INDEX_GET) continue;` 写在 `switch` **之外**。写成 `case` 会被 `tools/aot_mir_cover.test.py` 的 operand 检测器读成「该 opcode 核一个索引」，并排进 `dead`（本轮读数 `IR-AOT-MIR-OPERANDS|indexed=10|prescanned=11|checked=10|missing=(none)|dead=(none)|detector-saw=yes|agree=yes`）。 [obs: 816f6cb]
3. `emit_one`：`src/compilation/aot_mir.c:2294` 加 `case OP_INDEX_SET: buf_fmt(b, "    mv_arr_set(v[%d], v[%d], v[%d]);", r1, r2, r3); return 0;`——**调用、无赋值**：写完容器仍要持有容器本身。 [obs: 816f6cb]
4. 运行期（`kMirValBlock` 内）： [obs: 816f6cb]
   - `mv_arr_need(want)`（`src/compilation/aot_mir.c:921`）从 `mv_arr_new` 里**提取**（不是新语义）：新字面量与越界写一条增长规则； [obs: 816f6cb]
   - `mv_arr_live(iv)`（`:983`）是**转发头**解析器：活数组的计数头是 `MV_INT`，被搬走的区域把头改成 `{MV_ARR, 新下标}`（`:1114`），所有读者经它落地，跳数上限 64（坏池变成停，不是挂）；
   - `mv_arr_get` 走转发头（`:998`）；
   - `mv_arr_set`（`:1075`）：非 `MV_ARR` 容器**静默 return 且不转换下标**；`mv_index_i32` 抛 `type_mismatch`；`i < 0` 走 stderr + `exit(1)`；存活槽原地整体赋值（**零分配**）；越界高时，区域正好在池高水位就**原地长**，否则**搬到池边界**并留转发头。

**分配账（先量后写死）**：`tools/aot_mir.test.py:249-271` 五条 `(allocs, bytes)` 与逐条理由—— [obs: 816f6cb]

- `mir_index_set_int` `(1, 256)`：池首块。`a = [1, 2, 3]` 第一次 `mv_arr_new` 要 4 槽 → 容量 8 → `8 × 32 = 256`；其后两次写入一次 `mv_alloc` 都没有（原地覆盖，或新槽仍在 8 槽容量内）。
- `mir_index_set_overwrite` `(1, 256)`：同一条账。`b = a` 不建新数组（别名共用同一区域），三次写全部零分配。
- `mir_index_set_grow` `(3, 1792)`：`256`（首块 8 槽）＋`512`（`b = [7]` 落在 `mv_arrused = 7`，要 9 槽 > 8 → 翻到 16 槽）＋`1024`（`b[s]` 的 `s = 3` 越界，而 `c = [9, 9]` 正压在 `b` 的区域上，只能搬到池边界：新区域要 `mv_arrused + want + 1` 槽 → 翻到 32 槽）。三次合计 `3 | 1792`。
- `mir_index_set_edges` / `mir_index_set_negative` `(0, 0)`：两条都以 rc≠0 收尾（`type_mismatch` / `index_out_of_range`），harness 对 rc≠0 只印 `reported=no|why=rc=1`、不比分配账；表里这两个数是「故意声明不查」。

### 五、绿读数与反向判据（非对称两次）

`MIR_EXIT=0`、`COVER_EXIT=0`，`tree=816f6cb7a15a72737d8e76c97479057c1edb37ab`：

```
IR-MIR-ALLOCS|case=mir_index_set_int|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_index_set_int|mir=accepted|cc=ok|exec=same|rc=0|bytes=12|stdout-hex=39390a320a34320a330a370a
IR-MIR-ALLOCS|case=mir_index_set_overwrite|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_index_set_overwrite|mir=accepted|cc=ok|exec=same|rc=0|bytes=13|stdout-hex=38380a38380a330a38380a330a
IR-MIR-ALLOCS|case=mir_index_set_grow|reported=yes|allocs=3|freed=3|live=0|bytes=1792|expect-allocs=3|expect-bytes=1792|ok=yes
IR-MIR-MIR-ONLY|case=mir_index_set_grow|mir=accepted|cc=ok|exec=same|rc=0|bytes=24|stdout-hex=390a6e696c0a6e696c0a330a310a380a6e696c0a370a390a
IR-MIR-ALLOCS|case=mir_index_set_edges|reported=no|why=rc=1
IR-MIR-MIR-ONLY|case=mir_index_set_edges|mir=accepted|cc=ok|exec=same|rc=1|bytes=5|stdout-hex=350a68690a
IR-MIR-ALLOCS|case=mir_index_set_negative|reported=no|why=rc=1
IR-MIR-MIR-ONLY|case=mir_index_set_negative|mir=accepted|cc=ok|exec=same|rc=1|bytes=2|stdout-hex=320a
IR-MIR-VERDICT|PASS|tree=816f6cb7a15a72737d8e76c97479057c1edb37ab|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=40|of=67|unknown=27|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=40|emitter=40|reported=40|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=816f6cb7a15a72737d8e76c97479057c1edb37ab|accepted=122|of=29|covered=29|missing=0|refused=0|same-as-interpreter=122|prelude-excluded=yes|beyond-ast-denominator=11
```

两个 `accepted` 不是同一本账：第一行 `IR-MIR-VERDICT` 的 `accepted=86` 是**共享语料**（86 条一个字节没动），`IR-AOT-MIR-COVER` 那一行的 `accepted=122` 是语料 + MIR-only（`117 → 122`）。`handles 39 → 40` 就是 `src/compilation/aot_mir.c:1398` 那一行表格；`mir_only 31 → 36`。 [obs: 816f6cb]

**反向判据①：下标错取一个操作数。** 把 `:2294` 的格式参数从 `r1, r2, r3` 改成 `r1, r1, r3`（下标取容器那个寄存器）。`BUILD_EXIT=0`、`MIR_EXIT=1`，红的原文：

```
IR-MIR-EXEC-DIFF|case=mir_index_set_int|interp-rc=0|mir-rc=1|interp-out=b'99\n2\n42\n3\n7\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_index_set_overwrite|interp-rc=0|mir-rc=1|interp-out=b'88\n88\n3\n88\n3\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_index_set_grow|interp-rc=0|mir-rc=1|interp-out=b'9\nnil\nnil\n3\n1\n8\nnil\n7\n9\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_index_set_edges|interp-rc=1|mir-rc=1|interp-out=b'5\nhi\n'|mir-out=b'5\n'
IR-MIR-PROBLEM|mir_index_set_int: the MIR route printed b'' rc=1, the interpreter b'99\n2\n42\n3\n7\n' rc=0
IR-MIR-PROBLEM|mir_index_set_overwrite: the MIR route printed b'' rc=1, the interpreter b'88\n88\n3\n88\n3\n' rc=0
IR-MIR-PROBLEM|mir_index_set_grow: the MIR route printed b'' rc=1, the interpreter b'9\nnil\nnil\n3\n1\n8\nnil\n7\n9\n' rc=0
IR-MIR-PROBLEM|mir_index_set_edges: the MIR route printed b'5\n' rc=1, the interpreter b'5\nhi\n' rc=1
IR-MIR-VERDICT|FAIL|tree=816f6cb7a15a72737d8e76c97479057c1edb37ab|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

红在 **stdout 字节**上（`mir-out` 与 `interp-out` 不同），不是"rc 不同就算"这种弱判据；`mir_index_set_negative` 仍是 `exec=same`（它的异常早于任何下标使用），这是预期的不对称。

**反向判据②：值取成常数。** 同一行改成 `buf_fmt(b, "    mv_arr_set(v[%d], v[%d], mv_int(1LL));", r1, r2)`——写入的值恒为 1。期望行必须红，且分配账一行不动：

```
IR-MIR-EXEC-DIFF|case=mir_index_set_int|interp-rc=0|mir-rc=0|interp-out=b'99\n2\n42\n3\n7\n'|mir-out=b'1\n2\n1\n3\n1\n'
IR-MIR-EXEC-DIFF|case=mir_index_set_overwrite|interp-rc=0|mir-rc=0|interp-out=b'88\n88\n3\n88\n3\n'|mir-out=b'1\n1\n1\n1\n3\n'
IR-MIR-EXEC-DIFF|case=mir_index_set_grow|interp-rc=0|mir-rc=0|interp-out=b'9\nnil\nnil\n3\n1\n8\nnil\n7\n9\n'|mir-out=b'1\nnil\nnil\n3\n1\n1\nnil\n7\n9\n'
IR-MIR-EXEC-DIFF|case=mir_index_set_edges|interp-rc=1|mir-rc=1|interp-out=b'5\nhi\n'|mir-out=b'5\n1\n'
IR-MIR-ALLOCS|case=mir_index_set_int|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-PROBLEM|mir_index_set_int: the MIR route printed b'1\n2\n1\n3\n1\n' rc=0, the interpreter b'99\n2\n42\n3\n7\n' rc=0
IR-MIR-PROBLEM|mir_index_set_overwrite: the MIR route printed b'1\n1\n1\n1\n3\n' rc=0, the interpreter b'88\n88\n3\n88\n3\n' rc=0
IR-MIR-PROBLEM|mir_index_set_grow: the MIR route printed b'1\nnil\nnil\n3\n1\n1\nnil\n7\n9\n' rc=0, the interpreter b'9\nnil\nnil\n3\n1\n8\nnil\n7\n9\n' rc=0
IR-MIR-PROBLEM|mir_index_set_edges: the MIR route printed b'5\n1\n' rc=1, the interpreter b'5\nhi\n' rc=1
IR-MIR-VERDICT|FAIL|tree=816f6cb7a15a72737d8e76c97479057c1edb37ab|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

两次注入之后都 `git checkout -- src/` 复原（`grep` 印回原件那一行、`git status --porcelain` 空），`BUILD_EXIT=0`、`MIR_EXIT=0`、`same=122 diff=0`、`VERDICT|PASS`。仓库里没有 `INJECTED` 这种读数，所以证据只能是复原后的重跑。

### 六、没做什么（逐条）

1. **DICT 那一半没做。** `OP_NEW_DICT`/`OP_NEW_SET` 仍未降低（实测 `d = {}; d[0] = 5` → `why=unsupported-opcode`），所以 `src/vm/vm.c:3729-3731` 的 `vm_dict_set` 臂**不可达**；`mv_arr_set` 的非数组臂只是静默 return，不是字典写入。 [obs: 816f6cb]
2. **写侧不做 interning。** 读侧 `vm_array_get` 把结果暂存进全局 intern 表让临时串活过一步，这一段没有复刻到写侧（写侧本来不 intern）。
3. **不做运行期回收。** `value_free(&a->items[i])`（`src/vm/vm.c:995`）与 `vm->used_mem` / `array_ensure` 的记账（`:987-993`）没有复刻：本路线所有块都登记在 `mv_alloc` 的链表里（`src/compilation/aot_mir.c:171-183`），退出时一次扫（`IR-MIR-ALLOCS|…|live=0`），运行期不回收。覆盖掉一个堆串元素不会立刻释放旧串。 [obs: 816f6cb]
4. **`array_ensure` 的失败路径没有对应物。** 解释器 `:987-990` 在 ensure 失败时 `value_free(&tmp)` 然后静默 return（丢一次写）；这边 `mv_alloc` 的 OOM 是 `if (!p) abort();`（`src/compilation/aot_mir.c:173`），即「停止」而不是「丢一次写」。 [obs: 816f6cb]
5. **常量下标越界仍按既有 policy 拒译**，即使那条写其实会增长：`a[p] = 9` 之后 `say a[5]` → `why=index-out-of-range-constant`。不放行，因为放行等于让「可证明写错的下标」与「对的下标」同答（`MIR_ONLY_REFUSE` 里 `mir_container_const_oob` 的注释就是这个理由）。**这一条已于 §10.151 退役**：解释器对越界读答 `nil`、对增长后的读答写进去的值，所以「写错的下标」不存在，拒绝反而拒绝了一个有答案的程序。
6. **`say a`（打印容器）仍崩**：`a = [1, 2, 3]; a[1] = 9; say a` 在 MIR 路线 rc=134（`mv_say` 的 `MV_ARR` 臂 abort），解释器打印 `[1, 9, 3]`。这是 §10.112 已钉的**既有**边界（本节没碰 `mv_say`），也不作为本节判据——一条没人会修的红判据只会变成噪音。
7. **链式下标写入只在里层已存在时可用**：`a = [[1, 2], [3]]; a[0][1] = 88` 两路都答 `88`；「里层是 nil 就新建」那一臂不可达——`a = [[1, 2]]; a[3][0] = 5` 先被常量下标 policy 拒掉（`why=index-out-of-range-constant`，解释器答 `5` 再 `2`）。
8. **共享语料与 refuse 表一笔没动**：`tools/aot_cover.pin.json` 仍 `86` 条、`of=29`，`MIR_ONLY_REFUSE` 里 `mir_container_const_oob` 仍在册。
9. **文档缺陷（不是本节造成的，本节只顺手删掉四行标记）。** 基线 `f7a7dc0` 的 `docs/STATUS.md` 尾部带着一段未解决的冲突痕迹：`docs/STATUS.md:5270` / `:5438` / `:5611` 三处（`0ff78e2` 引入；两边 §10.112 与 §10.113 的正文都对，只是标记没删） [obs: 816f6cb]。主线随后用 `03bacff` 只删掉这四行，本分支做同样的删除——好让本节的追加处不再落在被删掉的行上；两边的正文一字未动，冲突解本身不是本节做的。合并时两边都删了同样四行，所以这次合并里它没有留下任何文本。

### 七、诚实边界：容器写入仍不是完整容器语义

这一格补的是「**已有数组上的 32 字节标量槽**」：原地覆盖、越界高增长（尾上填 nil）、负下标抛、非数值下标抛、非数组容器静默丢弃、区域搬家用转发头保别名。它不是：字典写入、链式新建、写侧内存审计、与解释器同机制的 `ArrayObj`（解释器的句柄不动、只搬 `a->items`，这边搬的是槽本身并靠头解决别名——**行为等价、机制不同**，所以「`ival` 就是池下标」这条旧断言仍然成立）。还有一条更根本的：解释器的 `value_free` + `used_mem` 让「覆盖」有可观测的代价，本路线的覆盖在任何读数上都**零分配**（这正是 `ALLOCS_EXPECT` 里三条 `(1|256)` 与 `(3|1792)` 只在建数组/搬家时上涨的原因）。

## §10.117 字典最小闭环第一步：`OP_NEW_DICT` 与只读 `OP_INDEX_GET`（aot-mir，A 类第十三项）

§10.116 之后，容器这一族只剩字典没有闭环：数组能建、能读、能写，字典连字面量都过不了 prescan。这一节交的是**字典的第一口气**——能建一个字面量，能用 `d[...]` 读一个值；写、比较、打印、迭代都留在第七节的边界里，逐条带读数。解释器语义照抄 `src/vm/vm.c:3648-3673`（`L_NEW_DICT`：`vm_array_new` 拿池下标，`push_n` 推进 2*n 个 key/value，句柄写 `aidx + 1`）与 `src/vm/vm.c:3675-3714`（`L_INDEX_GET` 的三路分派：数组、字典、字符串），字典那一臂再分两条：`src/vm/vm.c:3683` 先看 `idxv->type == VAL_INT`，是整数就是**位置**（读 `items[i * 2]`，即第 i 对的 KEY，界是 `src/vm/vm.c:3688`），否则走 `vm_dict_get`（`src/vm/vm.c:3697-3698` → `:1149-1172`，缺失键答 nil，命中把值拷一份）。[obs: 1fc5d3a]

### 一、判据先红（实现之前）

四条 `MIR_ONLY` 用例在 `tools/aot_mir.test.py:1001-1020`：`mir_dict_new_get`（字面量 + 两个字符串键）、`mir_dict_missing_key`（缺键答 nil）、`mir_dict_key_register`（键在寄存器里）、`mir_dict_position_key`（整数下标是**位置**而不是键——`a, b` 与 `0, 1` 两个答案必须分得开，一个 ival 比较会把它们合在一起）。实现之前先跑一次（`tree=3efa354626aa68d6c3b4595f3305a7f7f349a487`）： [obs: 1fc5d3a]

```
IR-MIR-MIR-ONLY|case=mir_dict_new_get|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_dict_missing_key|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_dict_key_register|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_dict_position_key|mir=refused|why=illegal-operand
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=45|mir-only-same-as-interpreter=41
IR-MIR-PROBLEM|mir_dict_new_get: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=4)
IR-MIR-PROBLEM|mir_dict_missing_key: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=2)
IR-MIR-PROBLEM|mir_dict_key_register: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=4)
IR-MIR-PROBLEM|mir_dict_position_key: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=4)
IR-MIR-VERDICT|FAIL|tree=3efa354626aa68d6c3b4595f3305a7f7f349a487|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

红是 `why=illegal-operand`，不是 `why=unsupported-opcode`：**不平衡的参数栈先于缺 arm 发火**。`OP_NEW_DICT` 的 2*n 个 push 槽没人消费，pre-scan 在发射前就拒了整条程序（`src/compilation/aot_mir.c:2203-2213`）——`OP_NEW_ARRAY` 那一格记的是同一个形状（`src/compilation/aot_mir.c:1685-1692`）。`mir-only-cases=45`、`mir-only-same-as-interpreter=41`：新增的四条就是 45 与 41 之间那 4 个，红在它们身上，不是红在共享语料的 86 条上。[obs: 1fc5d3a]

### 二、实现（三处入口 + 两个成员 + 一条搬家的行）

- **`MV_DIC` 是一个新 tag，不是新池**：`src/compilation/aot_mir.c:97-106`。因为解释器把字典存在**数组池**里（`src/vm/vm.c:3648-3673`），句柄同样是 `aidx + 1`，所以 `mv_arrmem` 就是字典的存储，头一格放的是 ITEM 数 `2*pairs`（`ArrayObj` 的 `count`），后面按 push 顺序 key0,value0,key1,value1 排（`src/vm/vm.c:3666` 的 `vm_array_push_n` 就是这个顺序）。[obs: 1fc5d3a]
- **构造器** `mv_dic_new`（`src/compilation/aot_mir.c:1077-1089`）：一次 `mv_arr_need(used + 2*n + 1)`，头写 `mv_int(2*n)`，拷 2n 个槽。字典**建完不动**，所以 `mv_arr_live` 的转发头对它永远是 no-op（头是 `MV_INT`，不是 `MV_ARR`）。[obs: 1fc5d3a]
- **读者** `mv_dict_get_at` / `mv_dict_get`（`src/compilation/aot_mir.c:1194-1217`）：前者是位置（`k < 0 || k*2 >= n` 答 nil），后者是键（线性扫，`mv_eq` 当 `val_eq` 用，缺键答 nil）。**不建哈希表**：解释器的开放寻址只是一个**索引**，每个候选最后仍然用 `val_eq` 判（`src/vm/vm.c:1138-1146`），所以线性扫访问的正是那个 probe 会答的那一对、同一顺序；哈希在这条路线上是第二个答案的主人，还要复刻 `dict_hash_key` 的跨类型规则（`src/vm/vm.c:1038-1072`）。[obs: 1fc5d3a]
- **分派进 `mv_arr_get`**：`src/compilation/aot_mir.c:1228-1240`，字典是第二个 dispatch（数组之后、字符串之前，`src/compilation/aot_mir.c:1239`）。**这一格真正的坑是下标的转换**：`mv_index_i32` 原来在函数头、对所有容器都转——对数组和字符串对，对字典**错**，因为字典的非整数下标是**键查找**，一个字符串下标走 `mv_index_i32` 会打 `type_mismatch` 然后退出，而解释器答的是 `d["a"]` 的值。所以转换被搬进了数组臂与字符串臂各自的里面（`src/compilation/aot_mir.c:1230-1234`、`:1244-1245`），默认分支的注释也补上了 `MV_DIC`（`src/compilation/aot_mir.c:1110-1111`）。[obs: 1fc5d3a]
- **三个 lowering 入口**：`op_regs` 把 `r1` 认成唯一的寄存器、`r3` 是 PAIR 数、`r2` 不读（`src/compilation/aot_mir.c:1694-1704`）；pre-scan 把容器当成**一族**，槽数是 `2*n` 或 `n`，先查 `n` 的界再查 asp（`src/compilation/aot_mir.c:2203-2213`）；发射是 `mv_dic_new(n, mvargs + asp - 2*n)` 并把 asp 减 2*n，空字面量走 `mv_dic_new(0, 0)`（`src/compilation/aot_mir.c:2655-2661`）；`emit_container` 的栈扫描把字典当成带参数消费者（`src/compilation/aot_mir.c:2820-2824`）。[obs: 1fc5d3a]
- 顺手把三处被这次改动**证伪**的旧注释改掉（不是新功能）：数组块里「`OP_NEW_DICT` 不发射、没有 DICT 能到池里」那段（`src/compilation/aot_mir.c:927-936`）、`mv_in` 里「DICT 容器在 `OP_IN` 之前就被拒」（`src/compilation/aot_mir.c:1300-1305`）、发射器容器对注释里把 `OP_NEW_DICT` 列为未发射的那句。[obs: 1fc5d3a]

### 三、读数（绿，原样）

实现后第一次跑**每个字典读都印 nil**：我自己的区域判据写成了 `mv_arrmem[iv - 1].type != MV_DIC`，而构造器把头写成 `mv_int(2*n)`——头永远是 `MV_INT`，所以判据把每一个活字典都判成「不活」。改成和数组臂**同一条范围规则**（`mv_arr_live` 过一遍 + `iv < 1 || iv > mv_arrused`）之后：

```
IR-MIR-ALLOCS|case=mir_dict_new_get|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_dict_new_get|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=310a320a
IR-MIR-ALLOCS|case=mir_dict_missing_key|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_dict_missing_key|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=6e696c0a310a
IR-MIR-ALLOCS|case=mir_dict_key_register|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_dict_key_register|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=320a320a310a
IR-MIR-ALLOCS|case=mir_dict_position_key|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-MIR-ONLY|case=mir_dict_position_key|mir=accepted|cc=ok|exec=same|rc=0|bytes=8|stdout-hex=610a620a6e696c0a
IR-MIR-VERDICT|PASS|tree=a19eaebc6ee5a8139951a175c16c4dd439b8cad6|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`stdout-hex` 逐字节就是解释器的：`310a320a` = `1\n2\n`，`6e696c0a310a` = `nil\n1\n`，`320a320a310a` = `2\n2\n1\n`，`610a620a6e696c0a` = `a\nb\nnil\n`——最后一条是位置读（第 0、1 对是键 `a`、`b`，位置 2 越界答 nil），一条 ival 比较读不出它。覆盖账（同一棵树，`python3 tools/aot_mir_cover.test.py --build build`）：

```
IR-AOT-MIR|accepted=131|refused=0|same-as-interpreter=131|of=29|emittable=42|of-ops=67|not-emittable=25|corpus=inline-cases:86|mir-only-cases:45|beyond-ast-denominator=13|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-OPERANDS|indexed=10|prescanned=11|checked=10|basis=case-labels-in-aot_mir.c's-emit_one-that-read-a-pool-global-function-or-label-index-versus-the-cases-in-prescan|missing=(none)|dead=(none)|detector-saw=yes|agree=yes
IR-AOT-MIR-LOWERING|handles=42|of=67|unknown=25|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=42|emitter=42|reported=42|agree=yes
IR-AOT-MIR-UNEXECUTED|ops=(none)|basis=opcodes-in-a-lowered-stream-of-a-shared-case-that-no-interpreter-run-of-the-corpus-executed|in-ast-denominator=no|counted-in-covered=no|emittable=yes
```

`handles 41 → 42` 就是 `src/compilation/aot_mir.c:1704` 那一行（`OP_NEW_DICT` 同时出现在 `op_regs` 和 `emit_one`，所以 table / emitter / reported 三个数都 +1）；`accepted=131` 是语料 + MIR-only（`122 → 131`，九条：四条字典 + 本次顺带不再被 asp 拒掉的既有 MIR-only），`beyond-ast-denominator` 从 11 到 13（`OP_NEW_DICT` 与 `OP_INDEX_GET` 进入「解释器在 MIR-only 用例上执行、但不进 AST 分母」那一栏）。`OPERANDS` 仍是 `dead=(none)`、`agree=yes`：pre-scan 的容器分支从 `OP_NEW_ARRAY` 一处变成一族，但**计数的判据没动**（`prescanned=11` 与 `indexed=10` 的差仍是那几处只读池/全局/函数/标签下标以外的 case）。[obs: 1fc5d3a]

### 四、`ALLOCS_EXPECT` 先量后写

四条字典用例的分配账**先跑、后写**（`tools/aot_mir.test.py:292-305`）：四条全部 `(1, 256)`。原因在账上不是猜的：字典与数组共享 `mv_arrmem`，区域是 `1 + 2*pairs` 槽，第一块池是 8 槽 = 256 字节，所以 1 对（3 槽）与 2 对（5 槽）都塞进第一块，只有那块被分配；**读不分配**（`mv_dict_get` 只用 `mv_eq` 比键、`mv_dict_get_at` 拷一个 32 字节的槽）。谁若给每个字典 malloc 一张表、或把键装箱，这一对就会换个数，读数会说出是哪一种。[obs: 1fc5d3a]

### 五、反向判据（两条，非对称）

仓库里没有 `INJECTED_COUNT` 这种读数，所以每次注入的证据是「注入后 `grep -n` 印出被改的那一行 + 判据原文」和「复原后同一行印回原件 + 判据原文」，计数用我自己的 `grep -c` 打出来。

**① 键比较换成整数比较。** `src/compilation/aot_mir.c:1214` 的 `if (mv_tru(mv_eq(mv_arrmem[iv + p], idxv)))` 改成 `if ((int)mv_arrmem[iv + p].ival == (int)idxv.ival)`——驻留字符串的 `ival` 都是 1，于是每个字符串键都命中第 0 对。注入后 `INJECT1_COUNT=1`，`grep -n` 印出 `1214:"        if ((int)mv_arrmem[iv + p].ival == (int)idxv.ival)  /* INJECT1 */`，`BUILD_RC=0`、`TEST_RC=1`： [obs: 1fc5d3a]

```
IR-MIR-EXEC-DIFF|case=mir_dict_new_get|interp-rc=0|mir-rc=0|interp-out=b'1\n2\n'|mir-out=b'1\n1\n'
IR-MIR-EXEC-DIFF|case=mir_dict_missing_key|interp-rc=0|mir-rc=0|interp-out=b'nil\n1\n'|mir-out=b'1\n1\n'
IR-MIR-EXEC-DIFF|case=mir_dict_key_register|interp-rc=0|mir-rc=0|interp-out=b'2\n2\n1\n'|mir-out=b'1\n1\n1\n'
IR-MIR-MIR-ONLY|case=mir_dict_position_key|mir=accepted|cc=ok|exec=same|rc=0|bytes=8|stdout-hex=610a620a6e696c0a
IR-MIR-VERDICT|FAIL|tree=a19eaebc6ee5a8139951a175c16c4dd439b8cad6|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

红在三条键扫描用例的 **stdout 字节**上，`mir_dict_position_key` 仍 `exec=same`——位置分支根本不经过这一行，这正是这条判据的**不对称**：它钉的是键那一臂，位置那一臂在自己的用例上不动。复原后 `INJECT1_COUNT=0`、那一行印回 `if (mv_tru(mv_eq(mv_arrmem[iv + p], idxv)))`、`git status --porcelain` 空、`TEST_RC=0`、`VERDICT|PASS`。

**② 缺键答 0，不答 nil。** 同一函数的原 `return mv_nil();`（`src/compilation/aot_mir.c:1216`）改成 `return mv_int(0LL);`。注入后 `INJECT2_COUNT=1`，印出 `1216:"    return mv_int(0LL);  /* INJECT2 */           /* src/vm/vm.c:1158-1161 */`，`BUILD_RC=0`、`TEST_RC=1`： [obs: 1fc5d3a]

```
IR-MIR-MIR-ONLY|case=mir_dict_new_get|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=310a320a
IR-MIR-EXEC-DIFF|case=mir_dict_missing_key|interp-rc=0|mir-rc=0|interp-out=b'nil\n1\n'|mir-out=b'0\n1\n'
IR-MIR-MIR-ONLY|case=mir_dict_missing_key|mir=accepted|cc=ok|exec=diff|rc=0|bytes=4|stdout-hex=300a310a
IR-MIR-MIR-ONLY|case=mir_dict_key_register|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=320a320a310a
IR-MIR-MIR-ONLY|case=mir_dict_position_key|mir=accepted|cc=ok|exec=same|rc=0|bytes=8|stdout-hex=610a620a6e696c0a
IR-MIR-VERDICT|FAIL|tree=a19eaebc6ee5a8139951a175c16c4dd439b8cad6|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

只红 `mir_dict_missing_key` 一条（`0` vs `nil`），其余三条 `exec=same`——它们没有缺键。两条判据各自只打自己那一臂，所以它们不是同一个断言的两次复读。复原后 `INJECT2_COUNT=0`、`src/compilation/aot_mir.c:1216` 印回 `return mv_nil();`、`git status --porcelain` 空、`BUILD_RC=0`、`TEST_RC=0`、`VERDICT|PASS`。[obs: 1fc5d3a]

### 六、没做什么（逐条）

1. **字典写入没做。** `OP_INDEX_SET` 走的是 `mv_arr_set`，它对非数组容器静默 `return`（`src/compilation/aot_mir.c:1394-1395`），所以 `d["a"] = 9` 在这条路线上是**静默 no-op**，解释器答 9、这边答 1（实测，见第七节第 1 条）。`vm_dict_set`（`src/vm/vm.c:1184-1222`）在 `OP_INDEX_SET` 的字典臂（`src/vm/vm.c:3730`）上是**存在但不可达**的。[obs: 1fc5d3a]
2. **哈希索引没建。** `dict_hash_key`（`src/vm/vm.c:1038-1072`）、`dict_hash_find`（`src/vm/vm.c:1138-1146`）、`dict_hash_build`/`dict_hash_ensure` 一个都没复刻：线性扫用一条 `val_eq` 规则（`mv_eq`），这是「一个答案只有一个主人」的选择，代价是每次读 O(n)；`dict_hash_key` 的跨类型规则（1 与 1.0 hash 相同）也就没有被复刻的必要。[obs: 1fc5d3a]
3. **interning 没做。** `vm_dict_get` 命中时把非驻留串换进驻留表（`src/vm/vm.c:1163-1166`，位置臂在 `src/vm/vm.c:3689-3693`）；这边没有驻留表，字节相同、身份不可观测——这是数组臂已经记下的同一笔债，不是新债。[obs: 1fc5d3a]
4. **运行期回收没做。** 字典块登记在 `mv_alloc` 链表里，退出时一次扫；没有 `value_free` 的对应物。[obs: 1fc5d3a]
5. **`mv_eq` 没加 `MV_DIC` 行。** 加了就等于顺手把「字典当键 / 两个字典比较」也答了，那是另一格（题目不许扩）。现在它们走 `nv_eq → mv_as_nv` 并 abort，见第七节。[obs: 1fc5d3a]
6. **`mv_say` 没碰**：打印字典仍是 abort（第七节第 3 条）。这是 §10.112/§10.116 已钉的既有边界。[obs: 1fc5d3a]
7. **共享语料与 refuse 表一笔没动**：`tools/aot_cover.pin.json` 仍 86 条、`of=29`。[obs: 1fc5d3a]

### 七、诚实边界：字典这条路上仍答不对的形状

每条要么有探针读数（探针在 `.scratch/probe_mir.sh`，跑的是「translate → cc → 运行」与 `inimerse --no-mods run` 两路对照），要么明写「未测」。**答对的不列进来装点门面，但列出来是为了说明「哪一格的读数来自哪一臂」。** 

**答错、而且是静默的（最危险的一类）：**

1. **往已有键写入**：`d = {"a": 1}; d["a"] = 9; say d["a"]` —— 解释器 `9`，这边 `1`（rc=0，不报错）。因为 `mv_arr_set` 对非数组容器静默 return（`src/compilation/aot_mir.c:1394-1395`）。[obs: 1fc5d3a]
2. **写一个新键**：`d = {"a": 1}; d["b"] = 7; say d["b"]; say d["a"]` —— 解释器 `7`、`1`，这边 `nil`、`1`（rc=0）。同上一条，同一个根因，但读数不同（新键连旧键都没被碰）。[obs: 1fc5d3a]

**停住程序的（rc=134，stdout 空）：**

3. **`say d`（打印字典）** —— 解释器 `{a: 1, b: 2}`，这边 rc=134、stdout 空：`mv_say` 的非 nil/非字符串走 `mv_as_nv`（`src/compilation/aot_mir.c:1481`），`MV_DIC` 到不了 NV。[obs: 1fc5d3a]
4. **字典当键**：`d = {inner: 2}; say d[inner]`（`inner` 是另一个字典）—— 解释器 `2`（`val_eq` 按句柄相等，`src/vm/vm.c:421`），这边 rc=134：`mv_eq` 没有 `MV_DIC` 行（`src/compilation/aot_mir.c:752-766`，数组同一性的行在 `:763-764`），落到 `nv_eq`。[obs: 1fc5d3a]
5. **两个字典比较**：`d = {"a": 1}; e = d; say d == e` —— 解释器 `true`，这边 rc=134。[obs: 1fc5d3a]

**答对、但机制不同（不是缺口，写下来是别让人以为是缺口）：**

6. **嵌套字典**：`d = {"a": {"b": {"c": 3}}}; say d["a"]["b"]["c"]` —— 两路都 `3`（读的每一层都是同一臂）。[obs: 1fc5d3a]
7. **非字符串键**：`d = {1.0: "x", 2.0: "y"}; say d[1.0]` → 两路 `x`；`say d[1.0]` 的浮点下标走键臂（`val_eq` 的 int/float 互等，`mv_eq` 的 `nv_eq` 也一样）；`d = {true: 7}; say d[true]` → 两路 `7`。整数**字面量**下标则不是键而是位置：`d = {1: "x", 2: "y"}; say d[0]; say d[1]` → 两路 `1`、`2`（答的是键 1、键 2）。[obs: 1fc5d3a]
8. **重复键**：`d = {"a": 1, "a": 2}; say d["a"]` —— 两路 `1`（解释器的哈希索引与这边的线性扫都答第 0 对）。这一条是**实测相同**，不是原则相同：解释器的答案取决于 `dict_hash_build` 的插入顺序与探针扫描顺序，这边永远答第一对；两种实现此刻给出同一个数，但这不是本节可证明的等价。[obs: 1fc5d3a]
9. **负下标 / 空字典**：`d = {"a": 1, "b": 2}; n = 0 - 1; say d[n]` → 两路 `nil`；`d = {}; say d["x"]; say d[0]` → 两路 `nil`、`nil`。[obs: 1fc5d3a]
10. **`in` 于字典**：`d = {"a": 1}; say "a" in d; say "zz" in d` → 两路 `false`、`false`（§10.115 钉的是「非数组容器一律假」，这里没有新答案）。[obs: 1fc5d3a]
11. **字典在数组里**：`a = [{"a": 1}, {"a": 5}]; say a[0]["a"]; say a[1]["a"]` → 两路 `1`、`5`（分配账是两块 256 字节块：两个字典字面量各一块，`allocs=2|bytes=512`）。[obs: 1fc5d3a]

**未测（不是「未支持」，是这一节没有跑过的形状）：**

12. **迭代** `for x in d` / 任何按键序遍历 —— 未测。[obs: 1fc5d3a]
13. **把字典当参数传 / 当返回值传 / 越过函数调用的边界** —— 未测。[obs: 1fc5d3a]
14. **字典参与算术、拼接、`OP_CMP`** 等其他算符 —— 未测。[obs: 1fc5d3a]
15. **`removal`（`vm_dict_remove`，`src/vm/vm.c:1223` 起）** —— 没有对应 opcode，未测，也不可达。[obs: 1fc5d3a]
16. **驻留身份的观测**（同一字符串两次读是否是同一个指针） —— 本路线没有驻留表，解释器侧可观测与否**未测**；字节层面不可观测。[obs: 1fc5d3a]

### 八、这一格之后

`handles` 从 41 到 **42**，不是更多：这一节只让 `OP_NEW_DICT` 出现在 `op_regs` 与 `emit_one` 两个表里（`src/compilation/aot_mir.c:1704`、`:2655`），`OP_INDEX_GET` 本来就在（这次只是它的第二个分派臂长出来）。字典这条路线下一步真正的增量是 **`OP_INDEX_SET` 的字典臂**（`src/vm/vm.c:3730` 的 `vm_dict_set`）与 **`mv_eq` 的 `MV_DIC` 行**（`src/vm/vm.c:421`）：前者让第 1、2 条边界变绿，后者让第 4、5 条变绿，两个都是新 opcode 级别的洞，不是这一节能顺手带上的。[obs: 1fc5d3a]

## §10.119 并发（E 组）：把「边界」写成判据，而不是留给下一个人去猜

这一节不改任何 lowering 代码，`handles` 仍是 **42/67**。它做的是另一件事：把一个**已经被量出来的决定**（并发不进 MIR 路线）变成**会被门禁看着的判据**，并且把「为什么不是工作量估计」这句话换成读数。

### 一、边界在哪，用一句话说清

`src/compilation/aot_mir.c:1835` 的 `if (bc->thread_count > 0) return -2;` 是这条路线唯一的并发边界，它拒的是**容器**，不是「某个线程已经被启动」。所以下面这个程序——声明了一个线程函数、**从不启动它**——同样被拒：[obs: 9453186]

```inimerse
thread plain_worker {
    return 1
}
say "defined-never-started"
```

解释器对它回答 `defined-never-started`（rc 0），MIR 路线整份拒掉，`why=threads`。这条不对称正是判据要用的形状：**解释器答得出，这条路线拒得掉，两个事实并排**，而不是「一个没人跑得起来的程序」。`thread_count` 是容器级的，编译器把计数挂在 `mainBC` 上（`src/compiler/compiler.c:250`）。[obs: 9453186]

### 二、判据（新增两格，`MIR_ONLY_REFUSE` 1 → 3）

`tools/aot_mir.test.py` 的 `MIR_ONLY_REFUSE` 现在有三格，新增两格都要求 `why=threads`：

1. **`mir_thread_decl_never_started`** —— 上面那个「声明但不启动」的程序。读数：`mir=refused|why=threads|expect=threads|ok=yes|interp-rc=0|interp-out=b'defined-never-started\n'|stdout-hex=646566696e65642d6e657665722d737461727465640a`。[obs: 9453186]
2. **`mir_thread_result_unwrap`** —— `thread plain_worker { return 11 }` 之后 `r = thread_result("plain_worker")`、`say r`。这个程序本来是一个**值表示问题**（把 Result 拆开）。它被拒的理由必须是 `threads`，不许变成「参数栈不平衡」：实测把线程段去掉以后，同样形状拿到的是 `IR-MIR-REFUSED|why=illegal-operand|... asp=3`，也就是说这两个 why 是**可以互换的**，不钉住就会漂。读数：`mir=refused|why=threads|interp-rc=0|interp-out=b'{ok: false, error: thread is missing or not finished}\n'|stdout-hex=7b6f6b3a2066616c73652c206572726f723a20746872656164206973206d697373696e67206f72206e6f742066696e69736865647d0a`。[obs: 9453186]

总计：`IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=3|ok=3|expect=3`。

### 三、反向判据：把边界改掉，两格必须红，而且要**点名**

把 `src/compilation/aot_mir.c:1835` 的守卫改成永不触发（`if (0 && bc->thread_count > 0) return -2;`，只改这一处、随后还原、`INJECTED_COUNT=0`、`BUILD_EXIT=0`），两格立刻红，而且红的原因是**具名的**，不是「程序崩了」：[obs: 9453186]

`IR-MIR-PROBLEM|mir_thread_decl_never_started: refused with why=container-shape, the criterion demands why=threads (a refusal for the wrong reason is not the refusal this list measures)`

同一个注入让 `why` 从 `threads` 漂成 `container-shape`——**「拒对了没拒对理由」这件事是可测的**，这就是这两格存在的理由。还原后 `cases=3|ok=3` 回绿。[obs: 9453186]

### 四、为什么不选 A，为什么不选 B（两条都是读数，不是估计）

- **A（先做内置调用打底，再放开线程线）**：它给的 +2 个 `handles` 不是 E 组的，是 `OP_CALL_BUILTIN` 与 `OP_NEW_DICT`；而 `x = str(1); say x`（纯内置、无线程）今天在 MIR 上就死在 `why=illegal-operand ... asp=1`——挡它的是**参数栈平衡判据**，不是这条线程线。所以 A 连「拒到线程边界」都还没走到。[obs: 9453186]
- **B（真的发射线程）**：会让 `same-as-interpreter` 掉。解释器 `thread` 走真 OS 线程（`src/vm/vm.c:4459` 的 `L_THREAD_START:`，`src/vm/vm.c:4852` 注释写明 `is_task stays 0: wait/yield/recv use OS-thread (Sleep) paths, no Fiber dependency`），`task` 走协作式 Fiber，两套模型并存；一个真并行探针 20 次里出现 **2 种输出**，而这条路线所有 `MIR_ONLY` 判据都是**逐字节**比较。把「具名拒绝」换成「不可测的绿」，是拿判据的牙齿去换一个数字。[obs: 9453186]
- **C（本节做的事）**：`handles` +0，代价是两格判据与一段文字，收益是「边界」这件事以后**漂了会红**。

### 五、诚实边界（这一节没做的事）

- E 组本体是 **9 个** opcode（`OP_THREAD_START`/`CTRL`/`JOIN`/`WAIT`/`STATE`/`LOCK`/`SEND`/`RECV`/`THREAD_GOTO`，`src/compiler/bytecode.h:32-37` 起的线程段），另有 2 个非 E 组的前置（内置调用、字典）在 26 个 unknown 里押着；剩下的属其他组。**「并发 +0 格」不等于「unknown 只剩并发」。** [obs: 9453186]
- `start` 带参数时 `OP_PUSH_REG` 能发射而 `OP_THREAD_START` 不消费，**预测**会退化成 `illegal-operand: asp=N` —— 这条**未测**（要测它必须先放开边界，本节没放开）。[obs: 9453186]
- 关于「要不要有一天真做并发」：本节不裁定。它只钉住「现在不做，而且是因为测不了」，不是「因为难」。

### 六、这一格之后

`handles` 42 不动；`MIR_ONLY_REFUSE` 从 1 到 3，`IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=3|ok=3` 是新的读数。下一步仍然是容器写入（`OP_INDEX_SET` 在字典臂上还差一格）与内置调用（`OP_CALL_BUILTIN` 有 27 号 opcode 与 `asp` 判据一起挡着），两个都在值表示之外。

## §10.120 字典的写入半边：把「静默 no-op」变成读数

`handles` 仍是 **42/67**（`OP_INDEX_SET` 早在表里，这一节长出来的是它的第二个分派臂），`MIR_ONLY` 从 45 格到 **48** 格。这一节补的是 §10.117 自己点名的第一号边界。

### 一、病：写进去却没写

在 `mv_dict_set` 存在之前，`d["a"] = 9` 是**被发射的**，但 C 侧拿到的是 `mv_arr_set`，而它的第一条事实就是「不是数组容器就立刻返回」。于是程序打印**旧值**、rc 0——三种可能答案里最坏的一种：它看起来像一个答案（`IR-MIR-EXEC-DIFF|case=mir_dict_set_overwrite|interp-out=b'1\n9\n2\n'|mir-out=b'1\n1\n2\n'`）。[obs: 1530818]

### 二、判据（三格，先红后绿）

`tools/aot_mir.test.py` 新增三格，全部与解释器逐字节比较：

1. **`mir_dict_set_overwrite`** —— 键存在：`d = {"a": 1, "b": 2}` → `say d["a"]` → `d["a"] = 9` → `say d["a"]` → `say d["b"]`。解释器 `1 / 9 / 2`，修前这条路 `1 / 1 / 2`。这一格是**唯一**能抓住「静默 no-op」的形状：键存在时，no-op 与正确写入只差第二行。[obs: 1530818]
2. **`mir_dict_set_append`** —— 键不存在：`d = {"a": 1}` → `d["b"] = 7` → `say d["b"]` → `say d["a"]` → `d["c"] = 3` → `say d["c"]`。修前 MIR 打 `nil / 1 / nil`，解释器 `7 / 1 / 3`。它同时要求「追加后新键可读」与「追加不破坏旧键」。[obs: 1530818]
3. **`mir_dict_set_grow_after_neighbour`** —— 字典之后**又建了一个数组**，于是字典的区域不再贴着池高水位，**必须搬走**：`d = {"a": 1}` / `arr = [1, 2, 3]` / `d["b"] = 7` / `say d["b"]` / `say d["a"]` / `say arr[2]` / `say arr[0]`。解释器 `7 / 1 / 3 / 1`。数组要在同一个程序里读回来：搬家若踩了邻居，先在这里露出来。[obs: 1530818]

### 三、实现（`src/compilation/aot_mir.c:1436` 的 `mv_dict_set`，臂接在 `:1471`）[obs: 1530818]

照抄 `src/vm/vm.c:1184` 的 `vm_dict_set` 的**顺序**：先用读臂同一个相等判定找键（解释器是 `val_eq`，这里是 `mv_eq`——`src/vm/vm.c:1138-1146` 的哈希只是索引，每个候选仍由 `val_eq` 裁决），命中就**原地覆盖值**、未命中就**追加键值对**。位置语义是判据的一部分：覆盖不动位置、追加去末尾，因此整型下标（读臂的「第几对」）在写入之后仍指向解释器指向的那一对。[obs: 1530818]

增长走**数组那条路**、也走数组那条理由：区域贴着池高水位就原地延长；不贴就**复制到前沿并留下转发头**，于是程序持有的句柄仍指向同一批键值对。唯一的差别是表头计数：数组数元素，字典数**项**（`2*pairs`），所以一次追加加两格。分配数是量出来的：覆盖 `(1, 256)`（覆盖本身不分配，那块 256 是字典字面量），追加 `(1, 256)`（`3+2+2 = 7` 格仍装得进第一块），搬家 `(2, 768)`（第二次分配是前沿块）。[obs: 1530818]

### 四、两次非对称注入（各只红一格）

- **键扫描强制永不命中**（`mv_tru(mv_eq(...))` 前加 `0 &&`）⇒ 只有 `mir_dict_set_overwrite` 红：`b'1\n1\n2\n'`——未命中会再追加一对同名键，而读取仍取**最先**那一对。`append` 与 `grow` 两格**照旧绿**，说明这一格量的是「命中路径」，不是「写入有没有发生」。[obs: 1530818]
- **搬家时跳过复制循环**（`k < n` 改 `k < 0`）⇒ 只有 `mir_dict_set_grow_after_neighbour` 红：`mir-out=b'7\nnil\n3\n1\n'`，旧的键值对没跟着走，而邻居数组 `3 / 1` 仍对。原地两格照旧绿，说明这一格量的是「搬家」。[obs: 1530818]

两次注入都还原、`BUILD_EXIT=0`、注入标记计数 0；还原后 `IR-MIR-VERDICT|PASS`、`86/86` 语料与 `48/48` MIR-only 与解释器逐字节相同、`MIR_ONLY_REFUSE` 3/3。

### 五、诚实边界（这一节没做的事）

- **删除键值对**（`vm_dict_remove`）没有对应 opcode，仍然不可达。[obs: 1530818]
- **覆盖时丢弃的旧值不释放**：解释器在覆盖前 `value_free` 旧值（`src/vm/vm.c:1201`），这条路线的字符串要么驻留、要么由退出时清扫，它没有 free 可用，所以是「丢引用不丢内存」。分配判据只数真正发生的分配，因此这一条**不被任何读数看见**——写在这里，而不是假装它不存在。[obs: 1530818]
- **字典当键 / 两个字典比较**：`mv_eq` 没有字典行，遇到就会中止程序（这是 §10.117 已记的债，本节未动）。[obs: 1530818]
- **哈希表**：读与写都是对键值对的线性扫描，`O(n)`；解释器的哈希是速度结构，两条路线的**相等语义**必须只有一份，这是不变的部分。[obs: 1530818]

### 六、这一格之后

`handles` 42 不动。字典这条路线还剩两处具名的洞：`OP_INDEX_SET` 的**静默错**已在字典臂关掉，但**数组臂**同样形状的边界（越界写、增长语义）在 §10.116 已写；真正的下一格是把字典接进 `mv_eq`（`MV_DIC` 行）还是去做 `OP_CALL_BUILTIN`（内置调用同时挡着 27 号 opcode 与 `asp` 判据），需要先按读数选，不按「哪个看起来容易」选。

## §10.121 字典相等：按身份，不按内容（并把一条「防守得对但测不出来」的守卫写下来）

`handles` 仍是 **42/67**（相等行不是 opcode），`MIR_ONLY` 从 48 格到 **50** 格。这一节关的是 §10.117 点名的第四、五号边界（`d == e` 与「字典当键」前一半）。

### 一、病：不是答错，是**没有答案**

解释器比两个字典看的是**身份**（`src/vm/vm.c:421` `if (a->type == VAL_DICT) return a->ival == b->ival;`）：同一个字面量建出来的两个字典**不相等**，而 `f = d` 之后两个句柄指的同一个字典**相等**。这条路线上 `mv_eq` 没有 `MV_DIC` 行，操作数会走到 `mv_as_nv`，那里直接 `abort()`：[obs: bf93297]

- `mir_dict_identity_eq`：解释器 `b'false\ntrue\ntrue\n'`，修前 `rc=-6`、stdout 空。
- `mir_dict_neq_and_cross_type`：解释器 `b'true\nfalse\nfalse\ntrue\n'`，修前同样 `rc=-6`、stdout 空。

崩掉比答错更坏也更好：更坏是因为一个程序整份没有输出，更好是因为它**不会**被误当成答案（rc 134 与 rc 0 分得开）。判据先红、红成 SIGABRT，这一步是量出来的。[obs: bf93297]

### 二、实现（`src/compilation/aot_mir.c:769` 与 `:777`）[obs: bf93297]

`mv_eq` 的容器行扩成「数组或字典」，语义照抄 `val_eq:421`：**类型相同且句柄相同**。字典句柄与数组句柄同构——字典搬家时同样留下转发头（§10.120），句柄在移动前后不变，所以「身份」是一个**跨搬家稳定**的量，不是内存地址。`mv_ne` 不重写判定，直接否定 `mv_eq`（它的字符串行与数组行本来就这么做）。

「字典与数组相比」也是**答案**而不是类型错误：`val_eq` 的同类型链走完落到 `return false`（`src/vm/vm.c:428`）。所以保留类型测试，让 `d == arr` 走 `false`，而不是靠「两个句柄碰巧不同」侥幸对。[obs: bf93297]

### 三、两次有牙的注入，与一次**没有牙**的注入（这条更重要）

- **身份强制为假**（`mv_boo(0 && …)`）⇒ 只有 `mir_dict_identity_eq` 红：`b'false\nfalse\nfalse\n'`（`d == d` 也变假）。`neq_and_cross_type` 照旧绿。[obs: bf93297]
- **容器分支的 `!=` 强制为假**（`mv_ne` 里返回 `mv_boo(0)`）⇒ 只有 `mir_dict_neq_and_cross_type` 红：`b'false\nfalse\nfalse\nfalse\n'`。[obs: bf93297]
- **去掉类型测试**（只留 `a.ival == b.ival`）⇒ **一条都不红**。原因是实测的、不是想当然的：两个活着的区域**永远不会共用池下标**——分配器是只增的，搬走的区域留下转发头而不是把老下标还回去。所以那条类型测试**今天没有任何程序能证伪它**；它是**正确的但不可观测的**守卫。把它写在这里，而不是假装「有测试保护着」。[obs: bf93297]

### 四、诚实边界

- **字典当键**：读路径的键比较用 `mv_eq`，因此字典键现在会得到「身份相等」的答案而不是中止；但解释器的哈希键处理（`dict_hash_key`，`src/vm/vm.c:1038-1072` 的跨类型规则）这条路线的线性扫描**没有**复现，跨类型键（`1` 与 `1.0` 同键）的**写入**行为按 §10.120 的扫描规则走。这一条没有被本节的任何判据覆盖，写在这里。[obs: bf93297]
- **顺序比较**（`d < e`）未动：解释器的 `val_cmp` 只对字符串与数字有定义，其余落到报错路径；本节不碰它。[obs: bf93297]
- **内容比较不存在**：这条路线上 `{"a":1} == {"a":1}` 永远是 `false`，与解释器一致；「按内容比」在解释器里也不存在，所以这不是债，是语义。[obs: bf93297]

### 五、这一格之后

`handles` 42 不动，`MIR_ONLY` 50 格全绿。字典这条路线剩下的具名洞：**哈希表**（速度结构，不是语义）、**删除键值对**（无 opcode）、**覆盖时不 free 旧值**（§10.120 已记）。真正的下一格在值表示之外：`OP_CALL_BUILTIN`（27 号 opcode 与 `asp` 判据一起挡着）——它同时是 A 组「内置调用打底」那条被否掉的路线留下的唯一真实缺口。

## §10.122 内建调用的拒绝有了名字（以及为什么不是实现）

`handles` 不动（**42/67**）、`emittable` 不动（42）、判据格数不动（`MIR_ONLY_REFUSE` 5 格）。这一节不加能力，只把一条**早就存在的决定**从「像缺 opcode」改成「像一条边界」。

### 一、病：拒绝是对的，理由不是

两个程序在解释器里都有答案，在这条路线上都被拒——但被拒的理由都不是「内建调用」：

- `x = str(1)`（`say x` 解释器印 `1`）：why=`illegal-operand`，detail 是 `container=0 leaves the argument stack unbalanced: asp=1`。参数以 `OP_PUSH_REG` 到达，**没有任何东西消费它**，于是容器被判成「参数栈不平衡」。读者会去查栈平衡，而问题在调用本身。[obs: 0c4749f]
- `say dbg_ip()`（解释器印 `0`）：why=`unsupported-opcode`，`IR-MIR-NOT-YET|op=OP_CALL_BUILTIN`。这一条读起来像「忘记实现一个 opcode」，而它是一条**刻意的**边界。[obs: 0c4749f]

崩溃与答错分得开，但「答错的原因」也分得开：这两条今天都指错了方向。

### 二、两处机制（都是量出来的，不是想当然）

- **拒绝必须写在 `op_regs` 跳过之前**。`prescan` 的主循环是 `int mask = op_regs(op); if (mask < 0) { g_notyet++; … continue; }`，而 `op_regs(OP_CALL_BUILTIN)` 是 -1 ⇒ 写成 switch 里的 `case` **根本到不了**（实测：读数停在 `illegal-operand`/`unsupported-opcode` 不动）。所以它是一条 `if (op == OP_CALL_BUILTIN)`，放在那条 skip 之前。[obs: 0c4749f]
- **不能写成 `case` 标签**：`tools/aot_mir_cover.test.py` 的 operand 检测器把本函数里每个 `case` 标签读成「该 opcode 核了一个池/全局/函数/标号下标」，而这一臂只把 r2 当**字符串池下标**用来**点名**（`src/compiler/compiler.c:464`：`emit(curBC, OP_CALL_BUILTIN, result, ni, argc)`，`ni` 是名字、r3 是参数个数）。这与 `OP_CONCAT` 的操作数个数检查同因，后者同样写在 switch 之外。写成 case 会被报 `dead`、`agree=no`。[obs: 0c4749f]

### 三、为什么是**点名拒绝**而不是实现

内建的**注册表**、它的**能力位**（`CAP_MASK`）与 **safe-mode 规则**都在 VM 与各 mod 里（`vm_register_builtin` 的调用者）。在这条路线上「发一次内建调用」等于把那份派发**再抄一遍**，而抄一份就会有两套能力判定、两套 safe-mode 语义。这与 §10.119 对线程段的处理同因、同形：**边界是要写下来的决定，不是要偷偷打开的缺口**。所以 `handles` 42/67 不动，`not-emittable` 25 不动——这一节的价值在**可读性**与**可证伪性**，不在覆盖率。[obs: 0c4749f]

### 四、有牙的反向判据

把那条 `if` 停用（`if (0 && op == OP_CALL_BUILTIN)`）：

```
IR-MIR-MIR-ONLY-REFUSED|case=mir_builtin_call_with_arg|why=illegal-operand   ok=no
IR-MIR-MIR-ONLY-REFUSED|case=mir_builtin_call_no_arg |why=unsupported-opcode  ok=no
IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=5|ok=3|expect=5
```

两个格子同时翻红、其余三格不动 ⇒ 这两格**确实在量这条 if**，而不是在量别的东西。复原后 `cases=5|ok=5`、`IR-MIR-VERDICT|PASS`、`handles=42|of=67|unknown=25`、`IR-AOT-MIR-OPERANDS|…|dead=(none)|agree=yes`（证明这次改写没有变成下标检查）。[obs: 0c4749f]

### 五、剩下的诚实边界

- **内建调用整族仍然不可达**：`str()`/`int()`/`len()`/`dbg_ip()` 在解释器里都有答案（各印 `1`/`42`/`2`/`0`），这条路线上被具名拒绝。这不是「还差一点」，是**当前不在这条路线上**。[obs: 0c4749f]
- **`OP_CALL_VALUE`（把函数当值调用）仍是通用 `unsupported-opcode`**：它今天连一个具名理由都没有，属于同一族但没被本节覆盖。写下它，免得下一个读者以为「内建调用说清楚了 = 调用族都说清楚了」。[obs: 0c4749f]
- **`g_notyet` 不再为内建调用计数**：拒绝发生在计数之前，因此 `IR-MIR-NOT-YET|op=OP_CALL_BUILTIN` 不再出现在输出里；`not-emittable` 的 25 来自 `op_regs` 的口径，**没有变**（读数是 `emittable=42|not-emittable=25`）。[obs: 0c4749f]

## §10.123 `len()` 是这条路线的第一个内建（handles 42 → 43）

§10.122 把内建调用从「像缺 opcode」改成「像一条边界」，并写明**不实现**的理由：注册表、能力位与 safe-mode 规则都在 VM 和各 mod 里，抄一份就会有两套语义。这一节把那条理由收窄到**它真正适用的范围**，而不是撤掉它。

### 一、为什么是 `len`，以及为什么理由在这里不成立

三件事同时成立，`len` 才成为第一个：

- **语义是有限的、可抄的**：解释器的实现在一个函数里（`src/runtime/runtime_posix.c:11-27`），按 ARRAY、SET、DICT、STRING、INT、FLOAT **顺序**判定，其余（nil、bool）落到 `n = 0`。没有任何一处需要能力位、safe-mode 或 mod 参与。[obs: fdeba0a]
- **值域已在模型里**：六个臂分别读「区域头里的元素数」「区域头里的槽数除以二」「`strlen`」「整数本身」「浮点截断」和常量 0，**没有一个需要模型今天不持有的值**，`len` 自己也**一次都不分配**。[obs: fdeba0a]
- **AST 路线答不了**：`say len("abc")` 在 `aot-native` 上被拒（`expression kind 10 is outside the numeric subset`）⇒ 这是**超出 AST 路线**的能力，不是把已有的东西再实现一遍。[obs: fdeba0a]

剩下 24 个不发射的 opcode 里，其余内建名（`str`/`list`/`push`/`gui_stage`/`read_file`…）仍然按名拒绝：它们要么返回值模型今天给不出的东西（运行时构造的字符串），要么要 mod 的本事。**边界没有移动，只是第一格被证明是可搬的**。[obs: fdeba0a]

### 二、四处改动，每处都有量出来的理由

- **`op_regs`（`src/compilation/aot_mir.c:1879`）**：`OP_CALL_BUILTIN → R1`。r1 是**结果寄存器**；r2 是内建名在容器字符串池里的下标、r3 是参数个数，都不是寄存器。加之前这一行没有条目，所以每个内建调用都被算进 `not-emittable`。[obs: fdeba0a]
- **`prescan`（`:2425`）**：核池下标、**先判名字再判参数个数**，然后 `asp -= argc`。顺序是量出来的：把参数个数判在前面，`say dbg_ip()`（零参数）会得到 `illegal-operand` 而不是 `builtin-call`——那条读数与 §10.122 的判据直接冲突。参数是 `OP_PUSH_REG` 推上来的，这正是过去被报成「参数栈不平衡」的那次推入，现在由这一臂消费掉。[obs: fdeba0a]
- **`emit_one`（`:2849`）**：名字**在这里也解析一次**（读同一个字符串池），所以发射器的下标读取与 prescan 的界检查是**同一个主张**——operands 仪表正是比对这两者；`len` 之外的任何名字返回 -1，作为第二道锁。[obs: fdeba0a]
- **`kMirValBlock` 的 `mv_len`（`:1363`）**：解释器的值域**逐个臂**照抄，**除了 SET**——那条臂在这条路线上**不可达**（没有集合可表示），这与 `mv_in` 对容器臂的结构性说明是同一类话。DICT 臂除以二（字典区域存的是**键值对**，与 `src/vm/vm.c:3693` 同一个 `/2`），FLOAT 向零截断。[obs: fdeba0a]

### 三、读数与两次有牙的注入

`handles 42→43`、`emittable 42→43`、`not-emittable 25→24`、`accepted 136→137`、`mir-only-cases 50→51`、`IR-AOT-MIR-OPERANDS|indexed=11|prescanned=12|checked=11|missing=(none)|dead=(none)|agree=yes`。[obs: fdeba0a]

判据是一条程序里的八个形状（数组、字典、字符串、整数、浮点、nil、bool、空串），解释器答案是 `3, 1, 4, 7, 2, 0, 0, 0`：

- **DICT 臂去掉 `/2`** ⇒ `3,2,4,7,2,0,0,0`，`IR-MIR-EXEC-DIFF` 点名，其余形状不动。[obs: fdeba0a]
- **STRING 臂强制 0** ⇒ `3,1,0,7,2,0,0,0`，只有字符串那一格动。[obs: fdeba0a]

两次注入都只咬这一格 ⇒ 这条判据在量这个函数，而不是在量「程序跑起来了」。[obs: fdeba0a]

### 四、诚实边界

- **SET 臂没有实现，是判不了**：`len({1,2})` 在这条路线上根本走不到 `mv_len`——没有集合能构造出来（§10.117/§10.120 的 `NEW_SET` 缺口）。这一条要等集合进模型。[obs: fdeba0a]
- **只有 `len`**：`str`（43 个文件先撞它）、`list`、`push`、`size`、`count` 以及所有 mod 内建仍然按名拒绝。下一个可搬的格子要先满足「语义写在本地、值域已在模型里、不分配」三条，而 `str` **今天不满足第三条**（它要构造一个运行时字符串）。[obs: fdeba0a]
- **参数个数判据是编译器的性质**：`argc != 1` 的分支写成拒绝而不是等价物，因为编译器为内建只会发它自己的参数个数；这条分支**没有任何判据覆盖**，写在这里。[obs: fdeba0a]
## §10.124 `OP_THROW` 的第一格：未捕获时**退出码与非空 stderr**才是语义（aot-mir，A 类第十四项）

### 一、来源与日期

来源是异常支线的路线裁定：`try`/`catch` 的真实合同是 handler 栈 + 弹栈 + 剪帧（`src/vm/vm.c:2570-2577`、`:4142`），跨帧 catch 必须动 `OP_CALL_FUNC` 的每一条生成码或改 longjmp，那是 B 类的一整格。`OP_THROW` 自己只要答「未捕获」这一半就是**可观测**的语义：真跑 stdout、stderr 一行、退出码 1。日期 **2026-11-08**，基线 `3efa354`。 [obs: 92955ec]

今天的红（本分支第一次跑，二进制还是基线的 `build/aot-mir`，判据已先写进 `tools/aot_mir.test.py`；红点在 MIR 侧，不是解释器侧）：

```
IR-MIR-MIR-ONLY|case=mir_throw_uncaught_top|mir=refused|why=unsupported-opcode
IR-MIR-MIR-ONLY|case=mir_throw_uncaught_cross|mir=refused|why=unsupported-opcode
IR-MIR-PROBLEM|mir_throw_uncaught_top: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_THROW:1)
IR-MIR-PROBLEM|mir_throw_uncaught_cross: the MIR route refused a case this list exists to measure: unsupported-opcode (OP_THROW:1)
IR-MIR-VERDICT|FAIL|tree=3efa354626aa68d6c3b4595f3305a7f7f349a487|accepted=86|refused=0|...
```

### 二、语义：`vm_throw` 的未捕获路径写了什么

- handler 的唯一调用点是 `vm_throw(vm, t, &R[ins.r1])`（`src/vm/vm.c:4449`）：**r1 是被抛的值**，是一个寄存器，不是池下标。编译器的三个 emit 点都传表达式寄存器（`src/compiler/compiler.c:616`、`:2316`、`:2328`）。 [obs: 92955ec]
- 没有 handler 时，`fprintf(stderr, "\n[exception] uncaught: %s\n", (err->type == VAL_STRING && err->sval) ? err->sval : "(non-string value)")`（`src/vm/vm.c:2640-2641`），随后 `  at ip=%d frames=%d` 与帧链（`:2642-2644`），再 `last_error = 1`（`:2648`）——所以**退出码是 1**，且 stdout 只保留异常之前写过的字节。 [obs: 92955ec]
- 解释器实测（本树）：`p1`（`say "a"` / `throw "boom"` / `say "b"`）rc=1、stdout `a\n`、stderr `\n[exception] uncaught: boom\n  at ip=4 frames=0\n`；`p2`（`func f() { throw "from-func" }`，从 main 调 `f()`）rc=1、stdout `before\n`、stderr `\n[exception] uncaught: from-func\n  at ip=2 frames=1 -> ip=3\n`。 [obs: 92955ec]

### 三、判据三条，先立、先红

| 判据 | 位置 | 要求 | 今天的红 |
|---|---|---|---|
| A-1 | `tools/aot_mir.test.py:628`（`MIR_ONLY`）+ `:355`（`STDERR_EXPECT`） | `p1` 被接受、真跑、rc=1、stdout 逐字节同解释器、stderr 含 `[exception] uncaught: boom` | translate rc=1 / refused（`OP_THROW:1`） [obs: 92955ec] |
| A-2 | `tools/aot_mir.test.py:629` + `:359`（`STDERR_EXCLUDE`） | `p2` 的 stderr 含 `[exception] uncaught: from-func`，**且**不含 `[exception] uncaught: boom` | 同上，refused [obs: 92955ec] |
| A-3 | `tools/aot_mir.test.py:1109`（`MIR_ONLY_REFUSE` 的 `mir_try_no_throw`） | `p14`（`try`/`catch`、无 `throw`）**仍然**拒译，`why=unsupported-opcode`，且 not-yet 含 `OP_TRY_START`/`OP_TRY_END`、不含 `OP_THROW` | 拒译这一半今天是绿的（旁表见 `:1126-1127`）；A 落地后必须保持 [obs: 92955ec] |

A-3 是**负判据**：它堵的是「顺手把 try 也做了」。`p14` 解释器答案是 rc=0、stdout `a\nd\n`、stderr 空（catch 体不跑）；把 try 半做掉，这个程序就从「具名拒译」变成「rc=1 + 一条假异常」，那是**带退出码的错答案**，比拒译更坏。旁表把「拒译是关于什么」写成断言：两个 TRY opcode 在、`OP_THROW` 不在（`tools/aot_mir.test.py:1126-1127`）。 [obs: 92955ec]

### 四、改动：三处 + 一段 helper

1. `op_regs`：`src/compilation/aot_mir.c:1625` 加 `case OP_THROW: return R1;`，长注 `:1607-1624` 写明「r1 是被抛的值、它是寄存器；r2/r3 不是操作数；这条是单向读，掩码没有方向位」；并写明 **`prescan` 不需要 case**（没有池/全局/函数/标签索引可查，r1 由 generic bound loop 兜）。 [obs: 92955ec]
2. `emit_one`：`src/compilation/aot_mir.c:2627` 加 `case OP_THROW: buf_fmt(b, "    mv_die_uncaught(v[%d]);", r1); return 0;`，注 `:2609-2626` 写明「只答未捕获的一半；带 handler 的整条仍拒译，这就是 A-3 存在的理由」。 [obs: 92955ec]
3. 运行期 helper：`src/compilation/aot_mir.c:1055-1060` 的 `mv_die_uncaught(MirVal a)`：打印寄存器里的字符串（非字符串打 `(non-string value)`），然后 `exit(1)`——`exit` 会跑 `atexit(mv_free_all)`，所以 ALLOCS 尾巴是诚实的（注册点 `src/compilation/aot_mir.c:2732`）。 [obs: 92955ec]
4. **helper 为什么必须落在 `kMirValBlock` 之内**（`src/compilation/aot_mir.c:38-48` 的块注释自己写了这条）：共享 preamble 在两路逐字节相同，`classify()` 取第一个差异行并要求共同前缀仍带三个 marker（`tools/aot_mir.test.py:105`、`:1207`）；把 helper 插进 preamble 会让每个 c-diff 变成 `Unclassifiable`。 [obs: 92955ec]

### 五、仪表：包含（含具名子串），不是相等

- `STDERR_EXPECT`（`tools/aot_mir.test.py:355`）要求具名子串；`STDERR_EXCLUDE`（`:359`）要求「别的 throw 的字**不**出现」；两者合起来才看得见「答案是常量」这种假实现——一个只写 `mv_str("boom")` 的臂能通过所有正向子串，却答不出 `from-func`。 [obs: 92955ec]
- `ALLOCS_EXPECT` 的两条 `(0, 0)`（`tools/aot_mir.test.py:301-302`）是**先量后写**：两个生成程序自己的 stderr 都是 `IR-MIR-ALLOCS|allocs=0|freed=0|live=0|bytes=0`（字面量是 resident，throw 不分配；退出仍走 atexit 扫）。 [obs: 92955ec]
- 为什么**不**逐字节比：已 pin 的绿用例 `mir_container_index_type_mismatch`（`tools/aot_mir.test.py:739-740`）两边 stderr 今天就不一致——解释器打前导空行 + `  at ip=11 frames=0`，MIR 打无前导空行、无 ip 行，并在尾部多一行 ALLOCS。相等仪表会在落地的当天先欠一笔**假红**。 [obs: 92955ec]
- 两条仪表都做了双向自检（`tools/aot_mir.test.py:1338-1383`）：子串在/不在、别的 throw 的字泄漏、not-yet 丢了 TRY_END、not-yet 多了 OP_THROW；`--self-check` 读数 `checks=18|verdict=PASS`。 [obs: 92955ec]

### 六、两次非对称反向判据

1. **把消息写成常量**：`emit_one` 的臂改成 `mv_die_uncaught(mv_str("boom"))`。读数：`mir_throw_uncaught_top` 仍绿、`mir_throw_uncaught_cross` **两条都红**（`missing=([exception] uncaught: from-func)|leaked=([exception] uncaught: boom)`），`IR-MIR-VERDICT|FAIL`。复原（`grep -c` 该注入拼写 = 0、重编、重跑）→ `VERDICT|PASS`。 [obs: 92955ec]
2. **只 exit 不打印**：臂改成裸 `exit(1);`。读数：`mir_throw_uncaught_top` 红在**缺消息**（`missing=([exception] uncaught: boom)`；退出码 1 与 stdout 仍对，说明只看退出码看不见静默停机），`VERDICT|FAIL`。复原 → `PASS`。 [obs: 92955ec]

### 七、读数（本树，实跑）

- `p1`/`p2`：translate rc=0、cc rc=0、运行 rc=1、stdout 与解释器**逐字节相同**（`a\n`、`before\n`）；stderr 为 `[exception] uncaught: boom` / `[exception] uncaught: from-func`，各跟一行 `IR-MIR-ALLOCS|allocs=0|freed=0|live=0|bytes=0`。 [obs: 92955ec]
- `p14`：translate rc=1、`why=unsupported-opcode`，`IR-MIR-NOT-YET|op=OP_TRY_START|n=1`、`|op=OP_TRY_END|n=1`，**没有 OP_THROW**；解释器 rc=0、`a\nd\n`。 [obs: 92955ec]
- `IR-MIR-COVER|…mir-only-cases=43|mir-only-same-as-interpreter=43`（基线 41，两个新用例各 +1）；`IR-AOT-MIR|accepted=129|refused=0|same-as-interpreter=129|of=29|emittable=42|of-ops=67|not-emittable=25|beyond-ast-denominator=13`（`OP_THROW` 进 BEYOND）；`IR-AOT-MIR-LOWERING|handles=42|of=67|unknown=25|table=42|emitter=42|reported=42|agree=yes`。 [obs: 92955ec]
- `IR-AOT-MIR-UNEXECUTED` 仍是 `ops=(none)`：两个新用例由解释器执行过，不是「共享用例里没跑到的 opcode」。 [obs: 92955ec]

`handles` 41 → **42**，不是更多：这一格只加 `OP_THROW` 一个 opcode。`try` 相关的 `OP_TRY_START`/`OP_TRY_END` 按设计继续拒译（A-3），其余 24 个 opcode 仍 unknown。

### 八、诚实边界（单列）

1. **`try`/`catch`/`finally` 仍整条拒译**，`p14` 是判据（`tools/aot_mir.test.py:1109`）；`finally` 的正常落穿行为也没复刻（`p12` 解释器只打 `body\ndone\n`，别拿它当参照）。 [obs: 92955ec]
2. **非字符串抛出值**的 message 是字面 `(non-string value)`（`src/vm/vm.c:2641`），本树没有专门用例逐字比对，只按来源抄。 [obs: 92955ec]
3. **`--err-json` 不复刻**：解释器的 `g_err_json` 分支（`src/vm/vm.c:2616-2636`）打的是 JSON 形状，本路线没有这个开关。 [obs: 92955ec]
4. **线程内异常**没有专门判据：`OP_THROW` 在 thread 容器里落到的仍是本进程 `exit(1)`，与解释器线程语义（`vm->last_error` 只对 main）未逐一对照。 [obs: 92955ec]
5. **`at ip=…` / `frames=…` 两行不保证逐字节相同**：MIR 路线没有字节码 ip、也没有帧台账，这两行不打印；前导空行同样不打印（与既有两个 stop 同形）。**这正是仪表是「包含」而不是「相等」的原因**（§五）。 [obs: 92955ec]
6. 多出来的一行 `IR-MIR-ALLOCS|…` 是 atexit 尾巴，**两边 stderr 的尾部本来就不同**；任何把 stderr 当逐字节合同的断言都会先红在这个绿用例上（`tools/aot_mir.test.py:739-740`）。 [obs: 92955ec]

## §10.125 集合字面量 `OP_NEW_SET`：这条路线的第三个容器（handles 44 → 45，aot-mir，A 类第十五项）

### 一、来源与日期

来源是值表示路线的继续：`s = 1, 2` 在 MIR 路线里**整片被拒**，红在容器关口（`illegal-operand: container=0 leaves the argument stack unbalanced: asp=2`），因为 `OP_NEW_SET` 没有登记臂、预扫描把集合元素当成了压栈实参。日期 **2026-11-08**，基线 `62ff19e`。 [obs: 480bb0b]

先红的读数（判据先提交、实现后来；这一笔**由本次执行体重跑**，不是凭记忆写）：

```
IR-MIR-MIR-ONLY|case=mir_set_new_len_in|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_set_new_single|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_set_new_dedup|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_set_new_membership|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_set_new_register_elements|mir=refused|why=illegal-operand
IR-MIR-PROBLEM|mir_set_new_len_in: the MIR route refused a case this list exists to measure: illegal-operand (aot_mir: illegal-operand: container=0 leaves the argument stack unbalanced: asp=2)
```

`_dedup` 的 `asp=3`、`_membership` 的 `asp=8` 是元素个数不同所致：**同一个病因、不同的计数**，所以「红」在这里是**有名字的红**（不是程序崩）。

### 二、语义：`L_NEW_SET` 到底做了什么

- `r3` 是元素**个数**（不是寄存器）；`r1` 拿到集合的池下标，池条目种类 `VAL_SET`。分配失败时把栈上那 n 个元素逐个 `value_free` 再把 `r1` 置 nil —— **失败路径本身有分配语义**，不实现它就是在别的错误上撒谎。`src/vm/vm.c:4286-4310` [obs: 480bb0b]
- 元素按**压栈顺序**读（`base = sp - (n-1)` 起、往上走），顺序不是细节：集合的合并与去重都按这个顺序。`src/vm/vm.c:4297-4305` [obs: 480bb0b]
- 元素若**已经是集合**，走 `vm_set_add_comp`：把它两个半边（排序 i64 半边与 items 列表）一起并进来，不是按引用加一项。`src/vm/vm.c:1921-1930` [obs: 480bb0b]
- `SetObj` 根本**没有哈希表**：排序的 `long long *i64` + 非整数 `Value *items` + 区间分量 `SetComp *comps`。`src/vm/vm.h:60-69` [obs: 480bb0b]
- `vm_set_add` 是**不对称**的：INT 元素在 i64 半边二分、**从不看 items**；其余 tag 扫 items 用 `val_eq`、**从不看 i64**。`src/vm/vm.c:1872-1920` [obs: 480bb0b]

不对称是可观测的：解释器实测 `s = 1.0, 1` 是**两个元素**（`len` 2、`1 in s` 真、`1.0 in s` 真）；反写 `s = 1, 1.0` 也是两个；`s = 1, 1` 是一个。**一个统一的数值相等会把第一种给成 `len` 1** —— 这是本格存在的「静默错形状」，`mir_set_new_membership` 把两种拼写顺序都钉住。 [obs: 480bb0b]

### 三、判据五条（都先红后绿，都比对 stdout 字节）

| 用例 | 语句 | 它钉住什么 |
|---|---|---|
| `mir_set_new_len_in` | `s = 1, 2; say len(s); say 2 in s; say 3 in s` | 命中与落空**两条**都要对，只有一条对的实现会在两条里红一条 |
| `mir_set_new_single` | `s = 7, 7` | 单元素字面量：重复既是去重又是唯一元素 |
| `mir_set_new_dedup` | `s = 1, 1, 2` | 压三个存两个 |
| `mir_set_new_membership` | `s = "a", "b"`、`t = 1.0, 1.0`、`u = 1, 1.0`、`w = 1.0, 1`，各带 `len` 与 `in` | **两种拼写顺序**（不对称合并的指纹） |
| `mir_set_new_register_elements` | `a = 1; b = 2; s = a, b` | 元素来自**寄存器**而不是常量池 |
 [obs: 480bb0b]

### 四、读数（合并前，本树 `480bb0b`）

```
IR-MIR-ALLOCS|case=mir_set_new_len_in|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_new_single|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_new_dedup|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_new_membership|reported=yes|allocs=2|freed=2|live=0|bytes=768|ok=yes
IR-MIR-ALLOCS|case=mir_set_new_register_elements|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-COVER|mir-only-cases=58|mir-only-same-as-interpreter=58
IR-AOT-MIR-LOWERING|handles=45|of=67|unknown=22|table=45|emitter=45|reported=45|agree=yes
IR-AOT-MIR|accepted=144|refused=0|same-as-interpreter=144|beyond-ast-denominator=16
IR-AOT-MIR-UNEXECUTED|ops=(none)
```

三行 `allocs=1|bytes=256` 是**同一个读数的三种形状**（第一块 8 槽 = 256 字节；`[count][1][2]` 占 3 槽，不翻倍）；`_membership` 的 `2|768` 是**真翻倍**（`s`/`t` 各 3 槽共 6，`u` 要第 9 槽 ⇒ 池从 8 槽到 16 槽）。**去重的地板要说出来**：若重复真的占了槽（`s = 1, 1, 2` 压 3 存 3），仍装得进 8 槽块 ⇒ **块数看不见这个缺陷**，能看见它的是 stdout（`len` 会印 3）。这一行是把它说出来，不是假装它就是那道闸。 [obs: 480bb0b]

### 五、不覆盖的边界（每条都由「谁需要它」点名）

1. **`OP_SET_ADD` / `OP_SET_INTERVAL`**：无发射臂、无运行时 —— 集合由字面量**一次建完**、永不扩展。集合**作元素**（`q = p, 4`）会落到 `mv_eq` 就停，而解释器走 `vm_set_add_comp` 把 p 的两个半边并进来（`src/vm/vm.c:1921-1930`）。 [obs: 480bb0b]
2. **集合相等**：`mv_eq` 没有 `MV_SET` 行，`s == t` 把集合交给 `mv_as_nv`、**直接 abort（rc 134）**；解释器 `L_EQ` 对 SET/SET 调 `set_equal` 按**内容**比（`src/vm/vm.c:3539-3544`、`:2310` 比两半边）。**不能学 `MV_DIC` 用身份短接**：`val_eq` 对 dict 是身份（`src/vm/vm.c:421`）、对 set 是 FALSE —— 身份行会让**两个相等集合答 false**，那是错答而不是停。内容相等是下一格。 [obs: 480bb0b]
3. **集合作 dict 键、作 `+`/`<`/`min`/`not` 的操作数、被 `say` 打印**：一律落到 `mv_as_nv` 或 `mv_tru` 的兜底 abort，与本路线 dict 的停法同形。 [obs: 480bb0b]
4. **`s in arr`（集合被数组探测）**：本路线停。**解释器读数是我实测的**：`s = 1, 2` 时 `s in [1, 2]` 是 **false**、`s in [1, 3]` 是 false、`[1, 2] in s` 是 **true**、`1 in s` 是 true、`1.0 in s` 是 false。**这里更正一处实现注释的说法**：注释原先写「解释器的 array 臂只遍历 items 半边、所以 `{1, 2} in [1, 2]` 答 TRUE 是空转」，实测是 false —— 停的理由仍然成立（本条路线没有这条臂），但「解释器答错」这半句不成立，已按实测改写。 [obs: 480bb0b]
5. **哈希结构**：`SetObj` 没有哈希表（`src/vm/vm.h:60-69`），这里的 O(n) 扫描就是 `set_contains` 自己做的事：**答案相同，复杂度也相同**，不是我们省了。 [obs: 480bb0b]
6. **集合的 `len` 与 `in` 是臂而不是新 opcode**：`len()` 那格（§10.123）只答数组；本格把 set 半边补进同一对辅助函数，`mir_set_new_len_in` 是这条的判据。 [obs: 480bb0b]

### 六、为什么不一并做「集合写入」

`OP_SET_ADD`/`OP_SET_INTERVAL` 在发射侧就有形状问题（`r3` 是区间计数不是值），而集合**作元素**要复刻的 `vm_set_add_comp` 是「两次合并」不是一个赋值；那两格属于「容器写入」家族（数组写入 `OP_INDEX_SET` 已做、字典写入未做），合并顺序已在 `docs/BOARD.md` 的待办里。把它们塞进本格会让五条判据同时验两件事，红时分不清是谁错。

## §10.127 集合相等按内容（aot-mir，A 类第十六项）

### 一、来源与日期

来源是 §10.125 留下的那一格：那条路线的 `mv_eq` 没有 `MV_SET` 行，`s == t` 把集合交给 `mv_as_nv`、在**运行期** abort，而解释器的 `L_EQ` 对 SET/SET 对按**内容**答。判据笔 **49f6fc5**（`aot-mir: set equality -- six criteria, red, before the implementation`），实现笔 **217cc2a**（`aot-mir: set equality by CONTENT (sets answer, and the agreement with a defect is stated)`），日期 **2026-11-09**。

先红的读数**由本次执行体重跑**，不抄 commit message：把 `src/compilation/aot_mir.c` 还原成 49f6fc5 的内容、`cmake --build build --target aot-mir`，再跑 `python3 tools/aot_mir.test.py --build build`。两笔只差这一个文件（`git diff --stat 49f6fc5..217cc2a` 的唯一一行就是 `src/compilation/aot_mir.c`），所以这就是判据笔的实现。六条全部 `IR-MIR-EXEC-DIFF`：**translate 接受（`mir=accepted`）、运行时 SIGABRT、stdout 空**；读数里的 `rc=-6` 是 Python 对 SIGABRT 的记法，shell 看到的是 134。

```
IR-MIR-MIR-ONLY|case=mir_set_eq_same|mir=accepted|cc=ok|exec=diff|rc=-6|bytes=0|stdout-hex=
IR-MIR-EXEC-DIFF|case=mir_set_eq_same|interp-rc=0|mir-rc=-6|interp-out=b'true\nfalse\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_set_eq_diff|interp-rc=0|mir-rc=-6|interp-out=b'false\ntrue\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_set_eq_mixed_order|interp-rc=0|mir-rc=-6|interp-out=b'true\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_set_eq_int_vs_float|interp-rc=0|mir-rc=-6|interp-out=b'false\ntrue\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_set_eq_vs_scalar|interp-rc=0|mir-rc=-6|interp-out=b'false\ntrue\nfalse\nfalse\n'|mir-out=b''
IR-MIR-EXEC-DIFF|case=mir_set_eq_dedup|interp-rc=0|mir-rc=-6|interp-out=b'true\n2\n'|mir-out=b''
IR-MIR-PROBLEM|mir_set_eq_same: the MIR route printed b'' rc=-6, the interpreter b'true\nfalse\n' rc=0
```

红是**有名字的红**：失败模式是 `exec=diff` 加一条 `IR-MIR-PROBLEM`，而且解释器的答案（`interp-out=`）与崩溃读数印在同一行 —— 「这一格该答什么」在红的时候就已经固定，不是实现完再补的期望值。`mir=accepted` 这一半同样重要：这条路线**编译通过**，红在运行期，所以它不是「发射器还不认识这个 opcode」那种红。

### 二、`L_EQ` / `set_equal` 的语义

- **`L_EQ` 是一次二分**：只有 SET/SET 才进 `set_equal`，其余全部落 `val_eq`。`src/vm/vm.c:3539-3544` [obs: 217cc2a]
- **`set_equal` 先按 kind 分流**：kind1 比名字、kind2 比区间、kind0 的单 comp 等于 kind2（`src/vm/vm.c:2221-2235` [obs: 217cc2a]）；kind0 对 kind0 才比内容，且先要求两半边**各自的计数**相等。`src/vm/vm.c:2236` [obs: 217cc2a]
- **半边比半边**：i64 半边只在 `a->i64` 里找 `b->i64`（按值，`src/vm/vm.c:2237-2241` [obs: 217cc2a]）；items 半边只在 `a->items` 里找 `b->items`（按 `val_eq`，`src/vm/vm.c:2242-2246` [obs: 217cc2a]）；comps 半边对 comps（`src/vm/vm.c:2247-2251` [obs: 217cc2a]）。**没有一条跨半边的比较**，所以 `{1}` 与 `{1.0}` 不相等。
- **`val_eq` 的行里没有 `VAL_SET`**：INT/FLOAT/STRING/BOOL/NIL/ARRAY/DICT/FUNCTION 八行（`src/vm/vm.c:413-428` [obs: 217cc2a]），末尾的数值强转只覆盖 INT/FLOAT/BOOL，其余一律 `false`（`src/vm/vm.c:429` [obs: 217cc2a]）。ARRAY 与 DICT 是**身份**（`ival`，`src/vm/vm.c:421` [obs: 217cc2a]），SET 连身份行都没有 —— §10.125 那句「身份短接会答错」的出处。
- **本路线**：`mv_eq` 的 set 分支放在数组/字典身份行**之前**（`src/compilation/aot_mir.c:790-793` [obs: 217cc2a]，身份行在 `src/compilation/aot_mir.c:794-801` [obs: 217cc2a]）；`mv_ne` 的对应臂在 `src/compilation/aot_mir.c:809` [obs: 217cc2a]；`mv_set_eq` 在 `src/compilation/aot_mir.c:1282-1300` [obs: 217cc2a]：两侧总槽数先相等（`src/compilation/aot_mir.c:1285` [obs: 217cc2a]），然后 INT 只配 INT 按值（`src/compilation/aot_mir.c:1291-1292` [obs: 217cc2a]）、非 INT 只配非 INT 用 `mv_eq`（`src/compilation/aot_mir.c:1293-1294` [obs: 217cc2a]）。`mv_set_new` 把元素拍平成 `[count][items...]`，所以「半边」由**元素自己的 tag** 决定，不是索引；「不做跨半边强转」在实现里就是那两个 type 判断。

### 三、六条判据（都先红后绿，都比对 stdout 字节）

| 用例 | 语句 | 它钉住什么 |
|---|---|---|
| `mir_set_eq_same` | `s = 1, 2`；`t = 2, 1`；`say s == t`；`say s != t` | **顺序无关**：按池元素顺序比的实现会答 false |
| `mir_set_eq_diff` | `s = 1, 2`；`t = 1, 3` | 不等方向，且 `!=` 与 `==` 并列：硬编码一个答案的实现露馅 |
| `mir_set_eq_mixed_order` | `s = 1.0, 1`；`t = 1, 1.0` | 两种拼写里 int 都在一半、float 在另一半 ⇒ **true**；它是「一个统一数值相等」的反证 |
| `mir_set_eq_int_vs_float` | `s = 1, 2`；`t = 1.0, 2.0` | 整半边两个元素 vs items 半边两个元素 ⇒ `set_equal` 答 0；跨半边强转会答 true，这一条把它分开 |
| `mir_set_eq_vs_scalar` | `s == 1`、`s != 1`、`s == [1, 2]`、`[1, 2] == s` | 集合对非集合是 **false 而不是 void**（读数 false/true/false/false），走 `mv_as_nv` 的实现会崩 |
| `mir_set_eq_dedup` | `s = 1, 1, 2`；`t = 1, 2`；`say s == t`；`say len(s)` | 相等比的是**去重后**的内容；`len` 同时钉住去重（会印 3 的实现在这里红） |

### 四、读数（本树 `217cc2a`）

```
IR-MIR-ALLOCS|case=mir_set_eq_same|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_eq_diff|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_eq_mixed_order|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_eq_int_vs_float|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_eq_vs_scalar|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_set_eq_dedup|reported=yes|allocs=1|freed=1|live=0|bytes=256|expect-allocs=1|expect-bytes=256|ok=yes
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|perturbed=5|perturb-stdout-changes=5|mir-only-cases=64|mir-only-same-as-interpreter=64
IR-MIR-VERDICT|PASS|tree=217cc2a1f1d16e94a91018f9551c77558a139d15|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR-LOWERING|handles=45|of=67|unknown=22|table=45|emitter=45|reported=45|agree=yes
IR-AOT-MIR|accepted=150|of=29|covered=29|missing=0|refused=0|same-as-interpreter=150|prelude-excluded=yes|beyond-ast-denominator=16
IR-AOT-MIR-UNEXECUTED|ops=(none)
```

六行 `allocs=1|freed=1|live=0|bytes=256` 是**同一个读数的六种形状**：相等臂自己不分配（它只遍历两个池），这 1 块 256 字节是字面量本身的池。**这个读数的来历要写出来**：`mir_set_eq_int_vs_float` 最初的拼法是单元素字面量 `s = 1` / `t = 1.0`，实测 `allocs=0|bytes=0` —— 因为单元素集合字面量**被常量折叠成了标量**，那对程序比的是两个普通值、不是两个集合。所以这一条改成两个元素，行表里的 1 是实测重取的值，不是推导。`tools/aot_mir.test.py:371-381` [obs: 217cc2a]

### 五、不覆盖的边界（每条都有实测读数）

1. **集合对数组的 `in`：这里的一致是一致于缺陷。** 解释器 `s in arr` 的分支从 `res = 0` 起步、只遍历 `sa->items`、**从不置 1**（`src/vm/vm.c:2402-2423` [obs: 217cc2a]），所以答案恒 false —— 我的实测：`s = 1, 2` 对 `[1, 2]` 与 `[1, 9]` 全 false，`t = 1.0, 2.0` 对两者也全 false；反向 `a in s` 是 true。加上 set 相等臂之后，这条路线在这些例子上**也**答 false（`build/aot-mir translate` + `cc` 后逐行比对，六行与解释器逐字节相同）。所以正确的句子是「与解释器一致」，**不是「已答对」**：`s in [1, 2]` 的正确答案是 true，两边都没给。
2. **集合相等只看两半边**：`1, 2` 与 `1.0, 2.0` 不相等（i64 半边两个元素 vs items 半边两个元素），与解释器 `set_equal` 一致，`mir_set_eq_int_vs_float` 钉的就是这条。**单元素拼法不存在**：`s = 1` 落成标量（见上面 `allocs=0`），所以「`{1}` 与 `{1.0}`」在这个路线里根本构不成两个集合 —— 能说这句话的只有 `len` ≥ 2 的拼法。
3. **集合作 dict key：本格让它从「停」变成了「答得不一样」**，这一条与 §10.125 写的不同，按实测改。`say s`、`s + 1`、`s < 1`、`not s` 仍是 abort（实测 rc 134；`not s` 的同时解释器印 false），这条没变。但 `d[s] = 7; say d[s]` **不再 abort**：这条路线的 dict 键比较就调 `mv_eq`（`src/compilation/aot_mir.c:1460-1470` [obs: 217cc2a] 的 `mv_dict_get`、`src/compilation/aot_mir.c:1762-1770` [obs: 217cc2a] 的 `mv_dict_set`），所以 set 键现在按**内容**命中 —— 实测 `d[s] = 7; say d[s]` 在实现笔上印 **7**，解释器印 **nil**（`len(d)` 两边都是 1）。解释器那一半是它自己的缺陷：`vm_dict_set` 收下键，而 `dict_hash_find` 用 `val_eq` 比键（`src/vm/vm.c:1144` [obs: 217cc2a]），`val_eq` 对 set 是 false（见二节），所以它插入的键永远查不回来。**这一条是 §10.127 引入的不一致，不写成「已答对」**；消掉它要比 `val_eq` 或 dict 键的语义先定下来，那是 dict 那格的事。**补齐这一条的是 §10.128**：两处键比较改走 `mv_key_eq`（集合键恒不命中、其余委托 `mv_eq`），`mir_set_dict_key_lookup` / `mir_set_dict_key_append` 先红后绿，`d[s]` 与 `len` 都回到解释器的字节。
4. **kind1 / kind2 集合与 `comps`**：`set_equal` 对名字集、区间集、单一 comp 与 kind2 的相等都有行（`src/vm/vm.c:2224-2235` [obs: 217cc2a]），这条路线**造不出**这些集合（`mv_set_new` 只产 kind0 的扁平槽），所以那些行在本格不可达，也没有判据；`comps` 半边同样不可达。
   `[obs: 217cc2a]`

### 六、为什么不在 `mv_eq` 里抄 dict 的身份短接

字典那格（§10.121）用的行是 `a.type == b.type && a.ival == b.ival`，出处是 `val_eq` 对 dict 就是身份（`src/vm/vm.c:421` [obs: 217cc2a]）。**这一格不能借这一行**，理由不是「风格不同」，是同一行在两种类型上给出相反的对错：

- `val_eq` 对 dict 有身份行，对 set **没有**行 ⇒ 对 set 一律 `false`（`src/vm/vm.c:429` [obs: 217cc2a]）。照抄身份行，等于把一个「两集合内容相同」的问题换成「两个池下标相同」的问题：`s = 1, 2` 与 `t = 2, 1` 落在不同下标，答案 false —— **错答**，不是停。停至少能让 `exec=diff` 喊出来；错答在 `exec=same` 的外衣下也能活着（它只是没被这条判据看见），而在这里它连外衣都穿不上。
- 更直接的一条：`mir_set_eq_same` 钉的就是「两份拼写、同一内容」—— 身份实现在这一条上直接红（两个集合字面量必然在不同池下标），`mir_set_eq_mixed_order` 也红。**这不是「防守得对但测不出来」的情形**，是有一条判据正对着它。§10.121 说 dict 的身份行「防守得对但测不出来」，是因为 dict 的两个句柄相等只在同一变量被赋两次时出现；集合不一样：内容相等是集合字面量的常态。

所以这一格借的是**内容**：`set_equal` 比什么（两半边各自的计数 + 半边对半边），`mv_set_eq` 就比什么；它比 `set_equal` 少的是 kind1/kind2/comps（见五节第 4 条），多的是没有 —— 这不是省掉的语义，是造不出来的输入。

## §10.128 集合作字典键：键比较照 `val_eq`，不照 `OP_EQ`（aot-mir，A 类第十七项）

### 一、来源与日期

来源是 §10.127 五节第 3 条留给下一格的那句：那一格把 `d[s] = 7; say d[s]` 从「运行期 abort」变成「答得不一样」，并明说这是它引入的不一致、消掉它要先把字典键的语义定下来。本格就是消掉它的那一笔。判据笔 **d60b984**（`test: two criteria for a set used as a dict key`），实现笔 **0112b13**（`aot-mir: a set key never matches -- the dict follows val_eq, not OP_EQ`），日期 **2026-11-09**。

先红的读数**由本次执行体重跑**，不抄提交信息：基线 `9becdd3` 的 `build/aot-mir` 二进制 + 只在 `tools/aot_mir.test.py` 里加这两条用例，跑 `python3 tools/aot_mir.test.py --build build`。两笔只差一个源文件（判据那笔只动测试，实现那笔只动 `src/compilation/aot_mir.c`），所以红读数的实现就是基线。

### 二、两条键规则，各自有出处

- **解释器收键与查键都是 `val_eq`**，不是 `OP_EQ`：写侧 `vm_dict_set`、查侧 `dict_hash_find` 的接受测试都调 `val_eq`（都在 `src/vm/vm.c`）。而 `val_eq` 的同一 type 链只有八行、**没有 `VAL_SET` 行**，跨类型尾部只认两个数字 ⇒ 一个 set 对任何东西（包括它自己）都答 false。所以解释器**收下**这一对、然后永远找不到它：`d[s] = 7; say d[s]` 印 `nil`，两个内容相同的 set 当键是**两对**。
- **本路线两臂原来都调 `mv_eq`**，而 `mv_eq` 对 set 在 §10.127 拿到了按**内容**答的臂（`OP_EQ`/`OP_NEQ` 需要它）⇒ 同一程序在 MIR 上命中、在解释器上不命中。
- 判别器是 `t = 1, 2`：与 `s` 内容相同、**区域不同**。按身份答的实现对 `s`、`t` 都答不命中，按内容答的实现对两者都答命中 —— 一条用例里同时出现这两种操作数，两种实现就分得开。只写 `d[s]`（同一句柄）的话，一个按身份的实现在这里也能绿。

红读数（`9becdd3`，两条用例先红，harness 原文）：

```
IR-MIR-PROBLEM|mir_set_dict_key_lookup: the MIR route printed b'7\n7\n1\n' rc=0, the interpreter b'nil\nnil\n1\n' rc=0
IR-MIR-PROBLEM|mir_set_dict_key_append: the MIR route printed b'1\n9\n' rc=0, the interpreter b'2\nnil\n' rc=0
```

两条都**编译通过、红在答案上**（`mir=accepted`），不是「发射器还不认识」那种红；`interp-out` 与 `mir-out` 印在同一行，期望值在红的时候就已经钉住。

### 三、两条判据

| 用例 | 语句 | 它钉住什么 |
|---|---|---|
| `mir_set_dict_key_lookup` | `d = {"a": 1}`；`s = 1, 2`；`t = 1, 2`；`d[s] = 7`；`say d[s]`；`say d[t]`；`say d["a"]` | **读侧**：集合键（同一句柄、不同区域同内容两种）都不可查，而字符串键仍可查（`1`）—— 一个把整张字典判空的实现会在最后一行红 |
| `mir_set_dict_key_append` | `s = 1, 2`；`t = 1, 2`；`d = {}`；`d[s] = 7`；`d[t] = 9`；`say len(d)`；`say d[s]` | **写侧**，而且读的是**对数**而不是查找：`t` 与 `s` 内容相同，按内容比的写臂会覆盖第 0 对（len `1` / `d[s]` `9`），解释器追加成第 1 对（len `2` / `nil`）。字典建在两个集合之后，写时不吃拷贝分支，所以这里的 `len` 就是对数本身 |

### 四、读数（本树 `0112b13`）

```
IR-MIR-ALLOCS|case=mir_set_dict_key_lookup|reported=yes|allocs=2|freed=2|live=0|bytes=768|expect-allocs=2|expect-bytes=768|ok=yes
IR-MIR-MIR-ONLY|case=mir_set_dict_key_lookup|mir=accepted|cc=ok|exec=same|rc=0|bytes=10|stdout-hex=6e696c0a6e696c0a310a
IR-MIR-ALLOCS|case=mir_set_dict_key_append|reported=yes|allocs=2|freed=2|live=0|bytes=768|expect-allocs=2|expect-bytes=768|ok=yes
IR-MIR-MIR-ONLY|case=mir_set_dict_key_append|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=320a6e696c0a
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|mir-only-cases=66|mir-only-same-as-interpreter=66
IR-MIR-VERDICT|PASS|tree=0112b13f30c44654030cb1903925ab9d81fe1c70|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

两行 `allocs=2|bytes=768` 是**同一个读数的两种形状**（和 §10.127 六条同形）：键比较自己不分配，这两块是字典块与集合块（池按 256 字节一块、需要时翻倍）。`stdout-hex` 的尾巴 `6c0a` 是 `nil\n`、`320a` 是 `2\n` —— 与解释器的字节相同。`mir-only-cases` 从 64 到 66、覆盖语料的 `accepted` 从 150 到 152（`same-as-interpreter=152`），都是本格**新增这两条用例**的直接后果；发射器一个也没增删，所以 `handles=45|of=67|unknown=22|agree=yes` 与 §10.127 四节同值（`IR-AOT-MIR-LOWERING` 那一行是这次在合并树 `a0cc094` 上重取的）。

### 五、这是一致于**缺陷**，不是「已答对」

1. **解释器那一半是它自己的缺陷**：它收键用 `val_eq`，而 `val_eq` 对 set 没有行。正确的语义有争论（集合键按内容？按身份？干脆拒译？），但**今天解释器的事实是印 `nil`**，而这条路线的灵魂指标是与解释器逐字节一致 —— 所以本格照抄缺陷，没有去修解释器。
2. **修法只有一处、方向已写清**：`mv_key_eq` 只对 `MV_SET` 说「不命中」，其余委托 `mv_eq`；如果哪天解释器把 `val_eq` 补上按内容的 `VAL_SET` 行，本格要跟着删掉那两行里的一个 `if`，改完两条判据的期望值也跟着变成 `7/nil` 那一侧 —— 这正说明它是抄来的规则，不是推导出来的。
3. **拒译不可行**：字典键是动态类型，`prescan` 无法静态判定某个键是集合，所以「在翻译期拒掉集合键」这条路没有地方落。
4. **顺记一笔独立的缺陷，不在本格修**：`mv_len` 的容器臂直接读 `(int)a.ival`，**不经 `mv_arr_live`**，所以一棵**搬移过**的字典会把转发头里的新下标当成 item count。实测（`0112b13` 的二进制）：`s = 1, 2 / d = {} / d[s] = 7 / say d[s] / say len(d)` 这颗字典建在集合**之后**，写时走拷贝分支留下转发头 ⇒ MIR 印 `7` 与 `2`，解释器印 `nil` 与 `1`。这与集合键无关（把键比较换回 `mv_eq` 仍是 `2`），改法是容器臂先 `mv_arr_live`，应各自一格、各自一笔读数。本格的第二条判据故意让字典建在两个集合之后，就是为了不让这个缺陷污染它的对数读数。 **这笔债由 §10.129 修掉，同时更正本段两处记录：搬移只在「字典先建、集合后建」的顺序下发生（本段写的顺序不会搬移），且本段的 `7` 与 `2` 来自两棵不同的树 —— 证据以 §10.129 五节为准。**

### 六、为什么不动 `mv_eq`

§10.127 的六条判据要求 `s == t` 按内容答 `true`；把 `mv_eq` 的 set 臂改成恒 false 会让那六条全红，而 `OP_EQ` 确实该按内容答。这个解释器里，**相等运算与字典键是两条不同的规则**（`OP_EQ` 走 `L_EQ` 的 set/`set_equal` 二分，字典收键走 `val_eq`），本格只是把这个分叉照抄成两个函数（`mv_key_eq` 与 `mv_eq`）。`mv_ne` 同样不动：它服务 `OP_NEQ`，不服务字典键。

## §10.129 `len()` 在搬移过的容器上先解引用句柄（aot-mir，A 类第十八项）

### 一、来源与日期

判据 `ce78553`（`stream/mirval-lenhead`，在 `c53cdc5` 上先红），实现 `ad91878`，2026-11-09。缺陷是 §10.128 量它自己的红读数时顺带照出来的，那一段（五.4）把它记成了一笔债而不是当格修：`mv_len` 的容器臂读 `(int)a.ival`，**不经 `mv_arr_live`**。

### 二、缺陷的形状（读码，不是猜）

池里只有两种 `iv`：**活区域的起始槽**，与**转发头**。活区域的头部槽（`iv - 1`）是 `MV_INT`，`ival` 是 item 数；区域被搬到池前沿时，旧头部槽被改写成 `MV_ARR`，`ival` 是**新句柄**（`mv_dict_set` 拷贝分支的最后一件事）。`mv_arr_get`、`mv_dict_get`、`mv_dict_set` 都先调 `mv_arr_live`，`mv_len` 没有 —— 于是把新句柄当成了 item 数，字典臂还按 `/2` 除了一次。

### 三、两条判据

| 用例 | 语句 | 红读数（`c53cdc5`） | 绿读数（本树） |
|---|---|---|---|
| `mir_len_after_a_dict_moves` | `d = {"a": 1}`；`s = 1, 2`；`d["b"] = 2`；`say len(d)`；`say d["a"]`；`say d["b"]` | interp `2/1/2`，route `3/1/2`（头 7，`7/2`） | interp 与 route 都是 `2/1/2` |
| `mir_len_after_an_array_moves` | `a = [1, 2]`；`b = [3, 4]`；`i = 2`；`a[i] = 9`；`say a[i]`；`say len(a)`；`say b[0]` | interp `9/3/3`，route `9/7/3`（同一个头，数组臂不除） | interp 与 route 都是 `9/3/3` |

两条**编译通过、红在答案上**（`mir=accepted`），红读数是 `IR-MIR-PROBLEM` 整行，两边输出印在同一行。一个只修字典臂的实现会在数组那条红（`7` 对 `3`），只修数组的会在字典那条红（`3` 对 `2`）。

### 四、读数（本树 `ad91878`）

```
IR-MIR-ALLOCS|case=mir_len_after_a_dict_moves|reported=yes|allocs=2|freed=2|live=0|bytes=768|expect-allocs=2|expect-bytes=768|ok=yes
IR-MIR-MIR-ONLY|case=mir_len_after_a_dict_moves|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=320a310a320a
IR-MIR-ALLOCS|case=mir_len_after_an_array_moves|reported=yes|allocs=2|freed=2|live=0|bytes=768|expect-allocs=2|expect-bytes=768|ok=yes
IR-MIR-MIR-ONLY|case=mir_len_after_an_array_moves|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=390a330a330a
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|mir-only-cases=68|mir-only-same-as-interpreter=68
IR-MIR-VERDICT|PASS|tree=ad91878...|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

两块 `2/768` = 容器块（池按 256 字节、翻倍到 16 槽）与搬移后的前沿块；**解引用句柄一行不分配**，所以修复前后这两个数不变，把 `mv_arr_live` 写成会分配的版本会在这里红。`stdout-hex` 尾巴 `320a310a320a` 是 `2\n1\n2\n`、`390a330a330a` 是 `9\n3\n3\n` —— 与解释器逐字节相同。`mir-only-cases` 66→68、覆盖语料 `accepted` 152→154 是新增两条用例的直接后果（合并树上重取），发射器不增删，`handles` 不动。

### 五、一处更正：§10.128 五.4 的读数对来自两棵树

五.4 记的是「`d[s]` 印 `7`、`len` 印 `2`，解释器 `nil` / `1`」。那对数字**从来没有在同一棵树上同时成立**：`7` 是 §10.128 **之前**的树（集合键按内容命中，于是写是覆盖而不是追加、字典根本没搬），`2` 是 §10.128 **之后**的树（键不再命中，追加把字典搬到前沿，`len` 读到头）。同一段把语句顺序也写反了：搬移只在**字典先建、集合后建**（写时字典不在前沿）这个顺序下发生，而五.4 写的 `s = 1, 2` 在前、`d = {}` 在后**不会搬移**。今天实测（`c53cdc5`）：先建字典、后建集合的那颗印 `nil` / `2`，修后 `nil` / `1`；五.4 写的顺序在 `c53cdc5` 上本来就印 `nil` / `1`。所以缺陷是真的、债也真欠着，**要更正的只是那一段的证据**；判据与读数以本节为准。

### 六、边界

1. **只有 ARR 与 DIC 会留转发头** —— 它们是唯二能增长的区域；SET 的头部是 `MV_INT`，`mv_arr_live` 在集合句柄上是 no-op，所以集合臂的读数字节未动。
2. **数组那条用变量下标**（`i = 2; a[i] = 9`）：常量越界下标在翻译期就被拒（`why=index-out-of-range-constant`，它本身是另一格的判据），所以这条判据存在的理由之一就是绕开那道拒译、让写臂真的走搬移分支。
3. **不修解释器**：解释器的 `len` 读的是 `ArrayObj`/`SetObj` 自己的计数，句柄搬移只是它内部数组在动，本来就不会读到下标。

## §10.130 函数值：无捕获闭包的最小形态（aot-mir，A 类第十九项）

### 一、来源与日期

判据 `774822e`（先红，读数见三、四节），实现 `c447c09`，2026-11-09。这一格是闭包组的第一条，也是这条路线上**第一次出现函数值**：此前 `f = () -> 42` 在预扫描就被拒（`why=unsupported-opcode`，`OP_MAKE_FUNC` 与 `OP_CALL_VALUE` 一起挂在 `IR-MIR-NOT-YET` 上）。闭包组的另外四条判据（捕获读、高阶传参、返回并转递捕获、零参调用堆字符串）**已经写好并保持红**，留在 `stream/mirval-closure` 上逐条落地 —— 本格只买了一条。

### 二、形状（读码，不是猜）

解释器的三个 opcode 各读了一遍：`OP_MAKE_FUNC` 把 `r3` 个值从参数栈弹进一个新环境、把 `VAL_FUNCTION` 与函数下标写进目标寄存器；`OP_LOAD_CAPTURE` 读当前帧环境的槽 `r2`（没有环境时给 `nil`）；`OP_CALL_VALUE` 要求寄存器 `r1` 持有 `VAL_FUNCTION`，取出下标后**复用**普通调用路径。编译器侧：每个捕获值先发一条 `OP_PUSH_REG`，再发 `OP_MAKE_FUNC`；被调者是变量而不是名字时发 `OP_CALL_VALUE`。

本路线的对应形状是**一个值、一张够用的表**：`MV_FUN` 取 `VAL_FUNCTION` 的 tag 号，载荷是**这条路线自己的模块下标**（经 `nodes[0].child_ids[]` 从字节码函数下标翻译而来 —— 所有 lambda 都注册进主字节码，那一个数组是唯一的翻译点）；`ptr` 置 0，**留给下一格的捕获数组**。`MirVal` 的 32 字节布局与它的六条 `_Static_asserts` 一个没动：A′ 冻结 `Value` 的宽度、禁止第三套值表示，而 `ptr` 本来就在结构里 —— 所以「闭包需要加宽值」这个担心在这个仓库是**不成立**的，挡路的是调用机制而不是布局。

调用值走一张派发表，**只在确实搬动函数值的模块里发射**。不这么做，既有 86 个语料程序的 C 文本会被移动一格，`c-identical=0|c-diff=86` 这对读数就不再是它们本来的意思 —— 本格是买一条判据，不是重写这条路线的输出。

### 三、判据

| 用例 | 语句 | 红读数（`2de6727`） | 绿读数（本树） |
|---|---|---|---|
| `mir_closure_value_nocapture` | `f = () -> 42`；`say f`；`if f { say "truthy" }`；`g = f`；`say g()` | `mir=refused|why=unsupported-opcode`（`OP_MAKE_FUNC:1,OP_CALL_VALUE:1`） | `mir=accepted|cc=ok|exec=same`，输出 `<function>`/`truthy`/`42` |

这一条钉四件事：函数值打印成 `<function>`（与解释器同一行）、真值位置为真、能拷进另一个变量、**能经那个拷贝被调用**（拷贝是 `OP_MOV`，调用是 `OP_CALL_VALUE`）。任何把整数下标当值打印、或让拷贝不可调用的实现，在第一行或第三行就红。

### 四、读数（本树 `c447c09`）

```
IR-MIR-ALLOCS|case=mir_closure_value_nocapture|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-MIR-ONLY|case=mir_closure_value_nocapture|mir=accepted|cc=ok|exec=same|rc=0|bytes=21|stdout-hex=3c66756e6374696f6e3e0a7472757468790a34320a
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|mir-only-cases=69|mir-only-same-as-interpreter=69
IR-MIR-VERDICT|PASS|tree=c447c097738b34c77033566eb5b6d37f024cc74f|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`stdout-hex` 解出来是 `<function>\ntruthy\n42\n`，与解释器逐字节相同。**`allocs=0` 是有意义的零**：无捕获的函数值不持环境，所以这一格一条分配都不该有；有分配的实现在这一行红（`ok=no`）。覆盖侧 `IR-AOT-MIR-LOWERING` 从 `handles=45|of=67|unknown=22` 走到 `handles=47|of=67|unknown=20|agree=yes`（`OP_MAKE_FUNC` 与 `OP_CALL_VALUE` 两个 case 标签进表），`accepted=154→155`、`beyond-ast-denominator=16→18`，`refused` 仍为 0（四条捕获判据不在本树的列表里，见五节）[obs: c447c09]。

### 五、只买了一条：四条捕获判据留在分支上，不跟着上 main

1. **为什么不放到这个列表里**：`tools/aot_mir.test.py` 的判据列表就是「这条路线今天答得出来的形状」的清单，判定要求 `mir-only-same-as-interpreter == 列表长度`。把这四条红着放进来，main 上每一枚 pin 的 `IR-MIR-VERDICT` 都会是 FAIL，直到整个闭包组做完 —— 那不是买一条判据，是把未完成的工作变成常红的门。它们的正文、红读数与「为什么每条都必要」写在 `stream/mirval-closure` 的判据笔 `bc9862c` 里，逐条落地时一次只搬一条。
2. **它们今天红在哪（同一家族、不同原因）**：四条都是 `why=illegal-operand`，细节是容器门上的具名拒译 —— `function index N captures 1 value(s); only the capture-free function form i...`。这个细节从早先的 `argument stack unbalanced: asp=N` 换成了捕获计数，是**同一个红家族**换了更准确的原因，不是把判据改松。
3. **为什么捕获不能顺带做**：捕获值要活过建它的那一帧（`make` 返回后 `n` 已经不在栈上），所以值必须**自己持**一份捕获数组；本路线没有「每帧环境」这种结构，最省的形状是把 `MirVal*` 挂在 `MV_FUN.ptr` 上（`ptr` 就是为此留的），被调容器则需要一个隐藏的自指针参数，且**只在真有捕获的模块里加**（否则又是移动既有 C 文本）。这是下一格的活，不是这一格的。
4. **一个还不答的角落**：`f == g` 今天会走 `mv_as_nv` 中止（没有任何 accepted 流碰到它，所以判据不测）；要答它得先读解释器 `val_eq` 对函数的那一行。记在这里，免得下一个人把它当成「已答对」。

### 六、边界

1. **无捕获只此一形**：`r3 != 0` 的 `OP_MAKE_FUNC` 按名拒译（`function index N captures ...`），不是静默丢环境。拒译带名字，所以下一个人一眼能看出缺的是哪一格。
2. **函数表只在需要的模块里长出来**：无函数值的模块 C 文本与上一格逐字节相同（`c-identical=0|c-diff=86` 未动），扰动读数也未动。
3. **不认捕获不等于不认函数**：打印、真值、拷贝、调用四条都答；`OP_LOAD_CAPTURE` 仍不降，它的零环境语义（给 `nil`）属于下一格。

## §10.131 捕获：值自己持环境，自指针只在真需要它的容器里（aot-mir，A 类第二十项）

### 一、来源与日期

判据 `576be6b`（捕获读，在 `8dd61d8` 上先红）与 `939c1dd`（另外三条：高阶、返回并转递捕获、零参调用堆字符串；先红读数取自 `8dd61d8` 的二进制），实现 `1c9d664` 与注记纠正 `3f7f0f1`，2026-11-09。这一格把闭包组**五条判据全部落齐**：上一格（§10.130）只买了无捕获一条，另外三条当时以为需要各自一格，实测发现同一套机制就答了 —— 见五节。

### 二、形状（读码，不是猜）

`OP_MAKE_FUNC` 带 `r3`：把参数栈上 `r3` 个值弹进一块**新环境**（每次实例化一份，解释器在 `src/vm/vm.c` 的 MAKE_FUNC 臂就是这样弹的）；`OP_LOAD_CAPTURE` 读当前帧环境的槽 `r2`，**没有环境时给 `nil`**。本路线把这块环境挂在值自己的 `MV_FUN.ptr` 上 —— 上一格特意留空的字段就是为它留的，`MirVal` 的 32 字节布局与六条 `_Static_asserts` 仍未动（A′ 冻结 `Value` 宽度、禁止第三套表示）。

调用侧：派发表 `mv_callv` 已经拿着函数值，把它作为**隐藏的自指针参数**传给被调容器；按名调用的容器（`make(10)` 这种）拿到的是 `nil` —— 这正是解释器的语义（`CALL_FUNC` 进入的帧本来就没有环境）。隐藏参数**只加给真正读环境内槽的容器**：不然每个已接受程序的 C 文本都会移动，`c-identical=0|c-diff=86` 那对读数就不再是它们本来的意思（这条纪律与 §10.130 的派发表同源）。

发射端另有两处「条件发射」：`mv_capture`/环境构造这两个运行时函数只在模块确实有 `OP_LOAD_CAPTURE` 或有捕获的 `OP_MAKE_FUNC` 时才进生成物。`OP_LOAD_CAPTURE` **故意不给 prescan case**：那张表的标签定义是「发射端真的读池/全局/函数/标签下标的 case」，给它加一条会被覆盖测试判成 `dead=(OP_CALL_VALUE)` —— 检测器是对的，这条 opcode 的发射端确实不读编译期下标。

### 三、判据

| 用例 | 断言 | 红读数（`8dd61d8`） | 绿读数（本树） |
|---|---|---|---|
| `mir_closure_capture_read` | `make(10)` 与 `make(3)` 印 15 再 10（共享环境会印 15 再 15） | `why=illegal-operand`（`captures 1 value(s)`） | `exec=same`，`<function>`/`15`/`10` |
| `mir_closure_higher_order` | 42/42/15，第三个把带环境的闭包当参数穿过 `apply` | 同上（`captures 1 value(s)`） | `exec=same`，`42/42/15` |
| `mir_closure_return_value` | 5 再 6；`outer(1)(2)(3)` 的转递捕获 | 同上（`captures 1 value(s)`） | `exec=same`，`5/6` |
| `mir_closure_zeroarg_string` | `f()` 零参调用仍印 `hithere`（调用路径不读参数栈） | 同上 | `exec=same`，`hithere` |

四条都**编译通过、红在答案上或具名拒译上**，没有一条靠崩溃当红。红读数的家族名是 `captures 1 value(s); only the capture-free function form ...`（具名拒译），改前 `mir-only-cases=70|same=69`（判据笔那次），改后 `73|73`。

### 四、读数（本树 `939c1dd`）

```
IR-MIR-ALLOCS|case=mir_closure_capture_read|reported=yes|allocs=2|freed=2|live=0|bytes=64|expect-allocs=2|expect-bytes=64|ok=yes
IR-MIR-MIR-ONLY|case=mir_closure_capture_read|mir=accepted|cc=ok|exec=same|rc=0|bytes=17|stdout-hex=3c66756e6374696f6e3e0a31350a31300a
IR-MIR-ALLOCS|case=mir_closure_higher_order|reported=yes|allocs=1|freed=1|live=0|bytes=32|expect-allocs=1|expect-bytes=32|ok=yes
IR-MIR-MIR-ONLY|case=mir_closure_higher_order|mir=accepted|cc=ok|exec=same|rc=0|bytes=9|stdout-hex=34320a34320a31350a
IR-MIR-ALLOCS|case=mir_closure_return_value|reported=yes|allocs=2|freed=2|live=0|bytes=96|expect-allocs=2|expect-bytes=96|ok=yes
IR-MIR-MIR-ONLY|case=mir_closure_return_value|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=350a360a
IR-MIR-ALLOCS|case=mir_closure_zeroarg_string|reported=yes|allocs=2|freed=2|live=0|bytes=40|expect-allocs=2|expect-bytes=40|ok=yes
IR-MIR-MIR-ONLY|case=mir_closure_zeroarg_string|mir=accepted|cc=ok|exec=same|rc=0|bytes=8|stdout-hex=686974686572650a
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|mir-only-cases=73|mir-only-same-as-interpreter=73
IR-MIR-VERDICT|PASS|tree=939c1dd4e884fdbb5c24957232c143b93ad43880|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

四个 `allocs` 数都是**先量后写**（生成程序自己的 `IR-MIR-ALLOCS` 行），写进判据表时没有做任何分解推导：`1|32`、`2|96`、`2|40`、`2|64` 各有自己的判别力（共享环境会把 `higher_order` 变成 0|0，转递捕获只拷中间帧会把 `return_value` 印成 3）。`stdout-hex` 解出来分别是 `<function>\n15\n10\n`、`42\n42\n15\n`、`5\n6\n`、`hithere\n`，与解释器逐字节同 rc。发射器侧：`handles=47→48|of=67|unknown=20→19|agree=yes`（`OP_LOAD_CAPTURE` 进表），`accepted=156→159`、`beyond-ast=18→19`，`refused` 仍 0；`covered=29|of=29|missing=0` 不变 [obs: 939c1dd]。

### 五、一个计划外的发现：转递捕获不需要第二套机制

剩下三条判据原本按「四条捕获形状各自一格」排的，实测是**同一套机制答的**：

1. **转递捕获**：编译器在内层 `OP_MAKE_FUNC` 之前把父捕获**当作普通值重新载入**（`src/compiler/compiler.c` 的 `:1297-1302` 区段），那次读走的就是本格加的 `OP_LOAD_CAPTURE`、`self` 也已经是中层闭包自己的函数值。所以 `outer(1)(2)(3)` 的 6 不是新代码买来的，是同一机制的自然结果。
2. **高阶**：`apply(g, x)` 里的 `g` 是参数而不是名字，调用是 `OP_CALL_VALUE`，自指针参数顺着派发表传下去 —— 与 `capture_read` 是同一条路径，只是函数值从参数栈来。
3. **零参调用**：`f()` 的 `argc=0`，派发表按 0 参数调用，不读参数栈；捕获的堆字符串在环境里活着，所以印 `hithere`。

三格另跑了探针（都与解释器同 stdout 同 rc）：创建者帧内按值调用、创建后改 `n`（环境是拷贝，旧值仍在）、`g = f` 复制后仍可调用、main 里无环境读得 `nil`。纠正笔 `3f7f0f1` 就是把注记里那句当时写错的「转递捕获仍不可达」改成实测结论 —— 错的注记留在树上比没有注记更坏。

### 六、边界

1. **`OP_LOAD_CAPTURE` 没有 prescan case**，这是有意的：它的发射端不读编译期下标，多一条会让操作数检测器判 `dead`。`OP_MAKE_FUNC` 的 prescan arm 改成按 `r3` 消耗参数栈（下溢检查 + `asp -= ncap`），不再是按名拒绝。
2. **两个创建点不一致会被大声拒**：同一容器下标若有两个创建点、捕获数不同，`func_cap` 返回 `-2`，发射端拒绝而不是猜一个。
3. **函数值相等仍不答**：`f == g` 走 `mv_as_nv` 会中止；没有 accepted 流碰到，所以判据不测，记成债而不是「已答对」。
4. **既有 86 例语料未动**：`c-identical=0|c-diff=86`、`reasons` 两条分量、`perturb-stdout-changes=5` 与上一格逐字相同；`refused=0` 说明没有把任何既有形状改成拒译。

## §10.132 比较两个函数：答案是那个解释器本来就会给的「否」（aot-mir，A 类第二十一项）

### 一、来源与日期

判据 `7673622`（两例，在 `fb043c0` 上先红）、实现 `d39a926`、注记更正 `e880dab`，2026-11-09。这是 §10.131 末尾记下的那笔债 —— 那里明说「`f == g` 走 `mv_as_nv` 会中止、仍不答」，这一格把它答了。

### 二、形状

`mv_eq` 与 `mv_ne` 各加一行：任一侧是函数值时，相等答 false、不等答 true。**这不是简化，是解释器的答案**：`val_eq` 的 FUNCTION 行比的是 `ImClosureFunction` 的指针，而解释器**每次读函数值都克隆**（`value_copy` 走 `im_closure_function_clone`）—— `f == f` 是把全局读两次、两个克隆，`h = f` 在赋值时又克隆一次，下标读也是克隆。所以没有任何形状能让两个操作数指向同一个闭包对象，那一行在解释器里**事实上是死代码**。本路线复制 `MirVal` 结构体、不克隆（而且 `MV_FUN.ptr` 存的是环境，无捕获时是 NULL），照那一行的**意图**实现会印 true 而解释器印 false —— 那是静默分歧，比中止更坏。跨类型也在这条里：函数不是数字，`val_eq` 的数值强制尾部够不到它，所以 `f == 42`、`f == nil` 与 `f == g` 用同一行。

**顺序**（`f < g`）与**算术**（`f + 1`）不在这一格：`val_cmp` 只要两个字符串或两个数字，解释器在那些形状上也抛错，本路线继续中止而不是编一个数。**没有为身份造对象**：不新增分配，`MirVal` 布局与六条断言未动 —— 见六节的删除条件。

### 三、判据

| 用例 | 断言 | 红读数（`fb043c0`） | 绿读数（本树） |
|---|---|---|---|
| `mir_function_equality_nocapture` | 六行 `false/false/false/true/false/false`：两个创建、一次拷贝、同值自比、`!=`、与整数、与 nil | `exec=diff`、`rc=-6`、stdout 空 | `exec=same`，35 字节 |
| `mir_function_equality_captured` | 四行 `false/false/false/true`：两个捕获实例、一次拷贝、自比、`!=` | 同上（`rc=-6`、空） | `exec=same`，23 字节 |

两条红都是**编译通过、然后被 abort 杀掉**（`why=rc=-6`、stdout 空），不是拒译：这是本仓库最不想要的那类红 —— 被接受、被编译、答案存在却被杀。第二条的 `p = m` 是关键行：解释器在赋值时克隆，所以「同一个闭包的两份」也答 false；本路线若按指针比会答 true。

### 四、读数（本树 `d39a926`）

```
IR-MIR-ALLOCS|case=mir_function_equality_nocapture|reported=yes|allocs=0|freed=0|live=0|bytes=0|expect-allocs=0|expect-bytes=0|ok=yes
IR-MIR-MIR-ONLY|case=mir_function_equality_nocapture|mir=accepted|cc=ok|exec=same|rc=0|bytes=35|stdout-hex=66616c73650a66616c73650a66616c73650a747275650a66616c73650a66616c73650a
IR-MIR-ALLOCS|case=mir_function_equality_captured|reported=yes|allocs=2|freed=2|live=0|bytes=64|expect-allocs=2|expect-bytes=64|ok=yes
IR-MIR-MIR-ONLY|case=mir_function_equality_captured|mir=accepted|cc=ok|exec=same|rc=0|bytes=23|stdout-hex=66616c73650a66616c73650a66616c73650a747275650a
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|mir-only-cases=75|mir-only-same-as-interpreter=75
IR-MIR-VERDICT|PASS|tree=d39a926fda320b020a9421ff7665fd4e7f123093|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

`stdout-hex` 解出来就是 `false` 四/六行那两组；两条与解释器逐字节同 rc。分配是**先量后写**：无捕获 0|0（没有环境可造），有捕获 2|64（`make` 两次各一份 32 字节环境）。发射器侧：`accepted=159→161`、`mir-only-cases=73→75`、`handles=48|of=67|unknown=19|agree=yes` **不动**（这一格没有新 opcode），`covered=29|missing=0|refused=0` 不变，既有 86 例 C 文本未动 [obs: d39a926]。

### 五、一致于缺陷，以及为什么「比指针」在这里是错的

这一格和 §10.128 同型：目标是**印出解释器印的东西**，而解释器的答案来自一个克隆行为。四个探针把这件事钉死（每个都先跑解释器）：`f = () -> 7` 下 `say f != f` 印 true；`func eq(a, b) { return a == b }` 配合 `say eq(f, f)` 印 false（参数入栈就克隆）；`d = [f]` 后 `say d[0] == d[0]` 印 false（每次下标读都克隆）；`say f == f` 本体印 false。所以「两条都是同一个函数值所以该相等」这个直觉，在解释器里根本不成立。

反过来说，本路线**没有**为了对齐而克隆：它的值是复制语义，若照 `val_eq` 的指针行直接比 `ptr`，`h = f` 之后会印 true —— 一条只在我们的实现里成立的「正确」，正是判据要防的静默分歧。所以答案是常量 false，并在 `mv_eq` 的注记里写清楚这是**与缺陷达成一致**。

### 六、边界

1. **顺序与算术仍中止**：`f < g`、`f + 1` 走 `mv_as_nv`，与解释器抛错同类的形为是中止；这一格只答相等/不等。
2. **不造身份对象**：没有分配、没有新 tag、没有动 `MirVal`；因此函数值之间的「同一实例」在本路线不可观测 —— 这正好是解释器给出的可观测性。
3. **删除条件（可判定）**：解释器停止「读时克隆」的那笔提交，会让身份变得可观测；那时这一行必须长出真正的身份对象才能继续对齐 —— 注记里写了这句，而不是「以后再说」。
4. **与 §10.131 的接缝**：捕获环境仍只在真读它的容器上作为隐藏参数传递；这一格没有碰调用机制，`mir_closure_*` 五条判据读数不变。

## §10.133 打印一个容器：在 MirVal 层拼出解释器的那串字节（aot-mir，A 类第二十二项）

### 一、来源与日期

判据 `e9116da`（六例，在 `f5706cb` 上先红）、实现 `87d2c12`、注记更正 `8d6932e`，2026-11-09。这补的是 §10.130 之前就记下的那一类 —— 「`say a` 是 STOP，不是答案」，只是那时它被写成「打印出口自成一类」。

### 二、形状

`say` 的结构值是三条 tag 的事：数组、字典、集合（以及它们嵌套里的任何一个）。它以前落在 `mv_as_nv` 上、被 `mv_tag_to_nv` 中止（空 stdout、rc 134），因为共享前导的 `nv_say(NV)` 只有三个分支，而 `NV` 没有指针 —— 容器教不进去。所以字节必须在 MirVal 层拼：新函数 `mv_vts` 逐句搬 `value_to_string`：

- **深度只展开 `depth < 2`**，所以 `[[1, 2], [3]]` 印全、而 `[[[1]]]` 印 `[[[]]]`（深度 2 只印空括号）；
- **集合印 `set(N)`**，N 是去重后的元素数，从区域头槽读；`1, 2, 2.0` 是**三**个（整数半边从不去重一个浮点）；
- **数组元素走 256 字节暂存、字典键 128、值 256**，放不下的元素被丢弃但括号照常闭合（`used + need < bufsz`）—— 这是解释器自己的截断规则，不是我们的简化；
- **标量沿用已经打印过的拼法**：`nil`、`true`/`false`、`<function>`、整数 `%lld`、浮点**仍走 `nv_fmt_double`**（数字拼法只有一个实现）。

它**不分配**：函数只在栈上拼，六个用例的 ALLOCS 读数是字面量自己那份池块（下文）。

### 三、判据

| 用例 | 钉住什么 | 红读数（`f5706cb`） | 绿读数（本树） |
|---|---|---|---|
| `mir_print_array_whole` | 平数组 + 其后的 `len` | 空 stdout、rc −6 | `[1, 2]`，9 字节 |
| `mir_print_dict_whole` | 插入序键值对，字符串键不加引号 | 空、rc −6 | `{a: 1, b: 2}`，15 字节 |
| `mir_print_nested_depth` | 深度 1 展开、深度 2 收缩 | 空、rc −6 | `[[1, 2], [3]]` / `[[[]]]`，21 字节 |
| `mir_print_set_whole` | `set(N)` 取去重数，`1, 2, 2.0` 是 3 | 空、rc −6 | `set(3)` / `3`，9 字节 |
| `mir_print_mixed_tags` | 容器里的 bool/nil/string/float 全在 | 空、rc −6 | `{k: [true, nil, s]}` / `[1.5, 2]`，29 字节 |
| `mir_print_empty` | `[]` 与 `{}` 的空体 | 空、rc −6 | `[]` / `{}`，6 字节 |

六条红都是**被接受、被编译、然后运行时死掉**（不是拒译）：这是「拒译比答错好」那条规矩下最容易被忽略的一类，因为翻译侧一切正常。

### 四、读数（本树 `87d2c12`）

```
IR-MIR-MIR-ONLY|case=mir_print_array_whole|mir=accepted|cc=ok|exec=same|rc=0|bytes=9|stdout-hex=5b312c20325d0a320a
IR-MIR-MIR-ONLY|case=mir_print_dict_whole|mir=accepted|cc=ok|exec=same|rc=0|bytes=15|stdout-hex=7b613a20312c20623a20327d0a320a
IR-MIR-MIR-ONLY|case=mir_print_nested_depth|mir=accepted|cc=ok|exec=same|rc=0|bytes=21|stdout-hex=5b5b312c20325d2c205b335d5d0a5b5b5b5d5d5d0a
IR-MIR-MIR-ONLY|case=mir_print_set_whole|mir=accepted|cc=ok|exec=same|rc=0|bytes=9|stdout-hex=7365742833290a330a
IR-MIR-MIR-ONLY|case=mir_print_mixed_tags|mir=accepted|cc=ok|exec=same|rc=0|bytes=29|stdout-hex=7b6b3a205b747275652c206e696c2c20735d7d0a5b312e352c20325d0a
IR-MIR-MIR-ONLY|case=mir_print_empty|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=5b5d0a7b7d0a
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|mir-only-cases=81|mir-only-same-as-interpreter=81
IR-MIR-VERDICT|PASS|tree=87d2c12...|accepted=86|refused=0|differ=0|perturb-stdout-changes=5
```

ALLOCS 是**先量后写**：平数组/字典/集合各一块 256 字节池（`allocs=1|bytes=256`），两个嵌套形状各两块（`2|768`），空的读一块 —— `a = []` 不占区域而 `d = {}` 仍要一个计数槽。发射器侧：`accepted=161→167`、`mir-only-cases=75→81`，`handles=48|of=67|unknown=19|agree=yes` **不动**（没有新 opcode），`covered=29|missing=0|refused=0` 不变，既有 86 例 C 文本未动 [obs: 8d6932e]。

### 五、写这条之前先量过的三件事

1. **深度规则不是猜的**：`[[[1]]]` 在解释器里印 `[[[]]]`（`od -c` 逐字节读过），它是 `depth < 2` 的直接后果，不是截断事故。
2. **`set(N)` 的 N 是去重数、不是压栈数**：`1, 2, 2.0` 印 `set(3)`、`len` 也是 3；一个按压栈数印的实现在别的集合用例上都绿，只在这里红。
3. **截断规则的边界**：`used + need < bufsz` 而不是 `<=`，所以恰好放满的最后一个元素也会被丢 —— 照抄解释器的算式而不是「差不多就行」，是因为字节判据不看意图。

### 六、边界

1. **区间分量仍不可达**：`set(Z interval)` 需要区间运行时，本路线没有（见 `mv_set_new` 注记）；本函数的 `set(?)` 分支是「句柄已不解析」的兜底，不是区间支持。
2. **没有新分配、没有互操作**：`mv_vts` 不分配，池块仍只由字面量产生；字符串仍走它自己的打印分支（字节相同，故没合并）。
3. **截断是可复现的**：超长容器按同一算式截断；这不是「大容器不支持」，而是解释器本来也只印一屏。
4. **与 §10.130/§10.131 的接缝**：容器里的函数值印 `<function>`（同一拼法），捕获环境不参与打印。

## §10.134 `stop all`：一个线程的程序里，线程停下就是程序结束（aot-mir，A 类第二十三项）

### 一、来源与日期

判据 `5a5a4ee`（四例，在 `ed758ba` 上先红）、实现 `471143c`，2026-11-09。这是 `unknown` 从 19 降到 18 的那一格。

### 二、形状

解释器里 `OP_STOP` 的臂只有两句 —— `t->running = false; continue;` —— 也就是**「正在跑的那个线程停在这里」**，而不是「程序结束」。`OP_HALT` 才是后者。这两句话在别处是一对不同的语义，在本路线里却重合：本路线**一个程序只发射一个线程**，带 `OP_THREAD_*` 的容器在更早的地方就被按名拒掉（`why=threads`）。所以对任何本路线会接受的程序，「这个线程停下」与「程序结束」是同一件事，退出字节也就相同：在 main 里 `return 0;`，在容器里 `fflush(stdout); exit(0);`（`OP_HALT` 早就在用的那两个出口）。

**承重的不是它等于什么，而是它不能等于什么**：

- 不能是 `break`：`stop all` 从 `while` 第三圈里来，循环后面的 `say "done"` 就不该印（`mir_stop_mid_loop` 钉这条，印出 `done` 的实现会漏）；
- 不能是「设个标志继续」：那会连后面的语句一起跑完；
- 容器里不能是 `return mv_nil()`：那会让调用点接着印 `3`（`mir_stop_inside_a_function` 钉这条）。

退出是**无条件**的，所以 `atexit(mv_free_all)` 照绑架 —— 分配读数不受影响；`mir_stop_after_allocating` 用一个在停下之前建好的数组字面量钉住这一点（`freed=1|live=0` 必须还报得出来）。

`op_regs` 给的是零掩码：解释器的臂从不碰 `R[]`，编译器发它时三个字段都是 0（`STMT_STOP: emit(comp->curBC, OP_STOP, 0, 0, 0)`）。零掩码同时也是这条路对 `IR-MIR-CONSERVE` 的声明 —— 它是**发射了**，不是记进 not-yet。

### 三、判据

| 用例 | 钉住什么 | 红读数（`ed758ba`） | 绿读数（本树） |
|---|---|---|---|
| `mir_stop_at_toplevel` | 停下之后的 `say 2` 永不执行，rc 仍是 0 | 拒译 `OP_STOP:1` | `1`，2 字节 |
| `mir_stop_mid_loop` | 从 `while` 第三圈里来，`done` 不印 | 拒译 `OP_STOP:1` | `0 1 2`，6 字节 |
| `mir_stop_inside_a_function` | 容器路径：调用点不继续（不印 `3`） | 拒译 `OP_STOP:1` | `1`，2 字节 |
| `mir_stop_after_allocating` | 停下之前建的一块仍被 atexit 扫掉 | 拒译 `OP_STOP:1` | `2`，2 字节，`freed=1` |

四条红都是**拒译**（`why=unsupported-opcode`），不是错答 —— 这一格没有「被接受然后印错」的阶段。

### 四、读数（本树 `471143c`）

```
IR-MIR-MIR-ONLY|case=mir_stop_at_toplevel|mir=accepted|cc=ok|exec=same|rc=0|bytes=2|stdout-hex=310a
IR-MIR-MIR-ONLY|case=mir_stop_mid_loop|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=300a310a320a
IR-MIR-MIR-ONLY|case=mir_stop_inside_a_function|mir=accepted|cc=ok|exec=same|rc=0|bytes=2|stdout-hex=310a
IR-MIR-MIR-ONLY|case=mir_stop_after_allocating|mir=accepted|cc=ok|exec=same|rc=0|bytes=2|stdout-hex=320a
IR-MIR-ALLOCS|case=mir_stop_at_toplevel|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_stop_mid_loop|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_stop_inside_a_function|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_stop_after_allocating|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-COVER|...|mir-only-cases=85|mir-only-same-as-interpreter=85
IR-MIR-VERDICT|PASS|tree=471143c...|accepted=86|refused=0|differ=0
```

发射器侧：`handles=49|of=67|unknown=18|agree=yes`、`accepted=171|same-as-interpreter=171`、`beyond-ast-denominator=20`（`OP_STOP` 进了「MIR 独有例子上解释器执行过的 opcode」那一栏）、`covered=29|missing=0` [obs: 471143c]。

### 五、写这条之前先量过、以及一处量不出来的事

1. **裸 `stop` 不产生这个 opcode**。`stop` 后面必须跟线程名或 `all`；裸写会被解析器拒（`Error at line 3: expected 'thread name', but got 'say'`），`stop t1` 走的是 `OP_THREAD_CTRL`（`STMT_THREAD_CTRL`）。所以本格能覆盖的形状只有 `stop all` 加「无线程体」，这与读数一致。
2. **`stop all` 的名字与它的实现不完全一致**。实现只停**正在跑的那个线程**，名字说「都停」。我试着量这件事：起一个 `thread t1 { ... }` 再 `stop all`。量不出来 —— 线程体里的 `say` 走 `gui_say` 这个内建（`STMT_SAY` 在 `in_thread` 时的分支），不写 stdout，两种实现下 stdout 都只剩 `after` 一行或干脆空。**所以这条只记「名字与实现不一致」，不记「错」**：没有量到差异，就不该在文档里判它对错。而且本路线根本不接受带线程体的容器，这一条对它是不可达的形状。
3. **`OP_HALT` 的两个出口本来就分 main/容器**。共用那两个出口不等于「这两个 opcode 一样」：`OP_HALT` 是程序结束，`OP_STOP` 是线程结束，只是本路线只有一条线程，所以同一个出口同时是两者的答案。

### 六、边界

1. **带线程体的程序仍取不到这条**：容器里有线程段就在 `why=threads` 处被拒，`OP_STOP` 与「还有别的线程在跑」并存的那种形状永远不会到发射器。
2. **`stop t1` / `restart t1` / `yield` 仍在 not-yet**：`OP_THREAD_CTRL` 与 `OP_YIELD` 没有臂，`unknown` 里剩下的 18 个包含它们。
3. **没有新分配、没有新寄存器**：零掩码意味着这一格既不加寄存器也不动池。
4. **rc 语义**：`return 0;` 与 `exit(0)` 都给 0，与解释器停线程后的 0 相同；`exit` 前显式 `fflush(stdout)`，因为容器可能停在未刷新的输出之后。

## §10.135 约束声明：记录一张表，并且用它检查存储（aot-mir，A 类第二十四项）

### 一、来源与日期

判据 `099a1d8`（七例，在 `34600dc` 上先红）、实现 `aa53930`，2026-11-09。这是 `unknown` 从 18 降到 17 的那一格。

### 二、形状

`OP_BIND` 是 `name: set [= init]` 这条声明（唯一形状，解析器里 `looks_like_set_start` 的那个分支）。它在解释器里跨**两个**臂：

- 声明处 `L_BIND` 做两件事：把约束记成 `vm->global_bound[g] = set 的区域下标 + 1`（`0` 表示无约束），并检查初值 —— 不命中就抛 `initial value out of range`；
- **每一个存储**处 `L_STORE_GLOBAL` 查同一张表，不命中就抛 `type_mismatch`。

所以「发射了 `OP_BIND`」不等于这一格答对了：只记录不检查的路线会把 `x = 5` 印成 `5`，而解释器一个字都不印、退出码 1、在 stderr 上说 `type_mismatch`。这条差异正是 `mir_bind_out_of_range_store` 存在的理由，也是这一格真正的成本所在。

四个决定，每个都写成了可被读数推翻的形状：

1. **表里存解释器存的那个数**：区域下标 `+ 1`，`0` = 无约束。存下标本身会让 0 号集合与「无约束」撞在同一个值上；池的句柄从 1 起，`+1` 什么都不损失。检查因此是一次比较加一次成员测试，不需要 tag 判别。
2. **检查是程序级开关**（`g_bound_used`）：一个程序里任何一处声明，都会让这个程序里**所有**全局存储带上守卫 —— 这正是解释器的规则（表是全局的，不是逐条语句的），同时保证**没有声明的程序 C 字节一字不动**，覆盖测试的 `c-identical`/`c-diff` 分类不会因为这一格而移动。
3. **两条消息由来源决定，不由值决定**：同一个「不命中」，声明处说 `initial value out of range`，存储处说 `type_mismatch`。判据给每条消息一个正向子串**加**一条互斥禁止，所以一个常量消息满足不了两条。
4. **不自己发明成员规则**：检查直接调 `mv_set_has`，它是解释器 `set_contains` 的抄本，连非对称一起抄 —— INT 探针先比 int 半边、**再**扫非 int 元素（所以 `1` 是 `{1.0, 2.0}` 的成员），其它 tag 只看 items（所以 `1.0` 不是 `{1, 2, 3}` 的成员）。两个用例各钉一个方向：造一个「数值成员」统一规则的路线会过第一条、红在第二条。

初值寄存器 `r3` 是**编译期操作数**（`-1` 表示没有 `= value`），所以「有没有初值」在发射时就是 C 文本的分支，不是运行时的判断：无初值时按 `L_BIND` 的无初值路径把全局置成 `nil`。

表里存的句柄**不**经 `mv_arr_live` 解析，这是声明不是疏忽：本路线里集合没有写路径（`OP_SET_ADD` 未发射），而唯一会搬区域的路径 —— `mv_dict_set` 的非前沿分支 —— 把它被要求写的那本字典拷到**前沿**，其它区域原地不动，所以从集合上取到的句柄只要池还在就有效。哪天集合长出写路径，这张表就得开始解析（§10.129 修的 `mv_len` 正是同一个错误在另一个臂上）。

### 三、判据

| 用例 | 钉住什么 | 红读数（`34600dc`） | 绿读数（本树） |
|---|---|---|---|
| `mir_bind_constrained_store` | 存储前全局是 `nil`，成员赋值落地 | 拒译 `OP_BIND:1` | `nil 2`，6 字节 |
| `mir_bind_initial_value` | 初值臂存下给的值，之后的成员赋值仍落地 | 拒译 `OP_BIND:1` | `2 3`，4 字节 |
| `mir_bind_out_of_range_store` | `x = 5`：stdout 空、rc 1、stderr 有 `type_mismatch`、**不得**有另一条消息 | 拒译 `OP_BIND:1` | rc 1，0 字节 |
| `mir_bind_out_of_range_init` | `= 5`：另一条消息，且不得有 `type_mismatch` | 拒译 `OP_BIND:1` | rc 1，0 字节 |
| `mir_bind_int_probe_finds_float` | 非对称方向一：INT 探针仍是浮点集合的成员（`x = 1` 进 `{1.0, 2.0}`） | 拒译 `OP_BIND:1` | `2 1`，4 字节 |
| `mir_bind_float_probe_misses_int` | 方向二：FLOAT 探针不是整数集合的成员（`x = 1.0` 抛） | 拒译 `OP_BIND:1` | rc 1，0 字节 |
| `mir_bind_plain_constraint` | 约束不是集合时**不**记约束，存储全放行 | 拒译 `OP_BIND:1` | `9`，2 字节 |

七条红全是**拒译**（`why=unsupported-opcode`），与上一格同形：这一格也没有「被接受然后印错」的阶段 —— 但**这一格的三、四两条是成对的**，因为「被接受然后不检查」恰恰是一种能印错而不停的可能，判据按 stderr 的正向子串与互斥禁止把它钉住了。

### 四、读数（本树 `aa53930`）

```
IR-MIR-MIR-ONLY|case=mir_bind_constrained_store|mir=accepted|cc=ok|exec=same|rc=0|bytes=6|stdout-hex=6e696c0a320a
IR-MIR-MIR-ONLY|case=mir_bind_initial_value|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=320a330a
IR-MIR-MIR-ONLY|case=mir_bind_out_of_range_store|mir=accepted|cc=ok|exec=same|rc=1|bytes=0|stdout-hex=
IR-MIR-MIR-ONLY|case=mir_bind_out_of_range_init|mir=accepted|cc=ok|exec=same|rc=1|bytes=0|stdout-hex=
IR-MIR-MIR-ONLY|case=mir_bind_int_probe_finds_float|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=320a310a
IR-MIR-MIR-ONLY|case=mir_bind_float_probe_misses_int|mir=accepted|cc=ok|exec=same|rc=1|bytes=0|stdout-hex=
IR-MIR-MIR-ONLY|case=mir_bind_plain_constraint|mir=accepted|cc=ok|exec=same|rc=0|bytes=2|stdout-hex=390a
IR-MIR-STDERR|case=mir_bind_out_of_range_store|need=([exception] uncaught: type_mismatch)|forbidden=([exception] uncaught: initial value out of range)|missing=(none)|leaked=(none)|ok=yes
IR-MIR-STDERR|case=mir_bind_out_of_range_init|need=([exception] uncaught: initial value out of range)|forbidden=([exception] uncaught: type_mismatch)|missing=(none)|leaked=(none)|ok=yes
IR-MIR-ALLOCS|case=mir_bind_constrained_store|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_bind_initial_value|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_bind_int_probe_finds_float|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-ALLOCS|case=mir_bind_plain_constraint|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-COVER|...|mir-only-cases=92|mir-only-same-as-interpreter=92
IR-MIR-VERDICT|PASS|tree=aa53930...|accepted=86|refused=0|differ=0
```

三条抛异常的用例读数是 `IR-MIR-ALLOCS|reported=no|why=rc=1` —— 与 §10.124 的抛异常那一对同形：消息在 stderr 上，退出发生在运行中，没有计数可咨询。

发射器侧：`handles=50|of=67|unknown=17|agree=yes`、`accepted=178|same-as-interpreter=178`、`beyond-ast-denominator=21`（`OP_BIND` 进了「MIR 独有例子上解释器执行过的 opcode」那一栏）、`covered=29|missing=0` [obs: aa53930]。

### 五、写这条之前先量过的事

1. **约束不一定是字面量集合**。`s = 1, 2, 3` 之后 `x: s = 2` 也合法：解释器与 MIR 都印 `2`、随后 `x = 4` 都抛 `type_mismatch`（rc 1）[obs: aa53930]。这就是为什么「是不是集合」的判断在**运行时**（看 `v[setReg]` 的 tag）而不在发射时 —— 发射器看不到那个寄存器的值。
2. **`x: 5` 是「约束不是集合」，不是「约束写错」**。解释器把 `sidx` 记成 `-1`，于是没有任何存储被检查：`x = 9` 印 `9`。`mir_bind_plain_constraint` 钉住这条路不能因为「只认集合」就拒译这个程序。
3. **函数里对同名全局的赋值走的是局部变量**。实测 `x: 1, 2, 3` 之后 `func f(v) { x = v; say x }`，`f(2)` 印 2、`f(5)` 印 5，**都不抛**。所以本格覆盖的是「顶层存储」，函数内的那条路根本不经过这张表；这一格不声称覆盖它。
4. **浮点打印口径会让人把读数看错**。`x: 1.0, 2.0 = 2.0` 印出来是 `2`（`nv_fmt_double` 对整数值浮点就印整数形），`x = 1` 印 `1`；两个读数都是单字节 `32`/`31`，与 §10.133 钉的容器打印口径一致。
5. **非对称两个方向都实测过**：`x: 1, 2, 3` 后 `x = 1.5` 抛；`x: 1.0, 2.0` 后 `x = 1` 通过。这不是从 `set_contains` 的注释推出来的，是量出来的。

### 六、边界

1. **`OP_DECLARE`（资源上限声明）仍不在本格**：它不是 `OP_BIND`，记的是 `limit_mem`/`limit_threads` 这类东西，只能靠**触发**那个限才观测到，仍未发射。
2. **约束只是「成员检查」，不是类型的全部**：解释器还允许 `x: 1, 2, 3` 的全局存 `nil` 吗？本格没有量过这条，也不声称；判据只覆盖了上面七种形状。
3. **集合键与集合约束是两条规则**（§10.128 的字典键比较走 `mv_key_eq`，本格的成员检查走 `mv_set_has`）：两者都不许互相「借」对方的答案，判据各自钉住。
4. **`g_bound_used` 是程序级的**：这意味着一个只声明过一次约束的大程序，每个全局存储都多一次比较与一次分支。读数上没有可见成本（`mir_bind_plain_constraint` 是 0|0，其它三条的 256 字节是集合字面量本身），但没有为了省这几条指令去做逐全局的细粒度开关 —— 那会让「表是全局的」这条解释器的规则在发射器里变成第二个版本。

## §10.136 `yield`：这条路线里它是一个 no-op，而「什么都不发」会变成一次拒译（aot-mir，A 类第二十五项）

### 一、来源与日期

判据 `ca80f2f`（四例，在 `a7cfdac` 上先红）、实现 `a29fd74`，2026-11-09。这是 `unknown` 从 17 降到 16 的那一格。

### 二、形状

`yield` 在解释器里的全部工作是**一句带守卫的调用**：

```
if (t->is_task && t->fiber_sched) SwitchToFiber(t->fiber_sched);
```

也就是「把控制权交还给 fiber 调度器」——协作式的让出。而**两个条件**在本路线接受的程序里恒为假：本路线按名拒掉带线程段的容器（`why=threads`），所以它接受的每个程序都跑在一条普通线程上，`is_task` 是假，也没有调度器。于是这条 opcode 在本路线里做不出任何程序能看见的事。

**所以这一格真正要钉的不是它印什么，而是它不能等于什么**（四条判据各钉一条）：

- **不能是停**：`yield` 之后的 `say 2` 还得印（`mir_yield_at_toplevel`）；
- **不能是重跑**：`while` 里 `yield`，循环体只跑三次、`i` 最后是 3，而不是从某个地方重来（`mir_yield_mid_loop`）；
- **不能是换帧**：被调容器在 `yield` 之后仍要跑完，调用点也仍要继续（`mir_yield_inside_a_function` 印 `in`、`out`、`end`）；
- **不能白花**：`yield` 本身零分配（`mir_yield_around_allocating` 唯一的 256 字节是它自己建的那个数组字面量）。

### 三、几处「不写代码也得写点什么」

发射器的这一臂**不产生任何可执行语句**，但也不能什么都不写：`emit_container` 的每条指令都要求正文变长，否则报 `emitted nothing, so the conservation equation could not see it` 并**拒译**——也就是说，一个合法的 no-op 会因为这个守恒检查变成一次失败。所以这一臂写一行**注释**：

```
/* OP_YIELD: this thread is not a task, so the arm is the no-op the interpreter's guard already is */
```

这行注释不是装饰：它让指令对 `IR-MIR-CONSERVE` 可见（`emitted+not-yet=insns` 成立），同时不写任何可执行文本。注释里说明了「为什么这里是空的」，因为下一个人读到它第一反应会是「这是漏了吧」。

`op_regs` 给的是零掩码：编译器发它时三个字段都是 0，解释器的臂也从不碰 `R[]`。

### 四、判据

| 用例 | 钉住什么 | 红读数（`a7cfdac`） | 绿读数（本树） |
|---|---|---|---|
| `mir_yield_at_toplevel` | 不是停：`say 2` 仍执行 | 拒译 `OP_YIELD:1` | `1 2`，4 字节 |
| `mir_yield_mid_loop` | 不是重跑：三次迭代 | 拒译 `OP_YIELD:1` | `3`，2 字节 |
| `mir_yield_inside_a_function` | 不是换帧：容器跑完、调用点继续 | 拒译 `OP_YIELD:1` | `in out end`，11 字节 |
| `mir_yield_around_allocating` | 零分配：唯一一块是数组字面量 | 拒译 `OP_YIELD:2` | `2`，2 字节，`allocs=1` |

四条红都是**拒译**（`why=unsupported-opcode`），与最近几格同形；差别在于这一格的绿读数**四例 stdout 全都与「把 yield 那行删掉」的程序相同** —— 这正是它作为 no-op 的定义，也正是为什么这四条只能靠「不许等于什么」来钉。

### 五、读数（本树 `a29fd74`）

```
IR-MIR-MIR-ONLY|case=mir_yield_at_toplevel|mir=accepted|cc=ok|exec=same|rc=0|bytes=4|stdout-hex=310a320a
IR-MIR-MIR-ONLY|case=mir_yield_mid_loop|mir=accepted|cc=ok|exec=same|rc=0|bytes=2|stdout-hex=330a
IR-MIR-MIR-ONLY|case=mir_yield_inside_a_function|mir=accepted|cc=ok|exec=same|rc=0|bytes=11|stdout-hex=696e0a6f75740a656e640a
IR-MIR-MIR-ONLY|case=mir_yield_around_allocating|mir=accepted|cc=ok|exec=same|rc=0|bytes=2|stdout-hex=320a
IR-MIR-ALLOCS|case=mir_yield_at_toplevel|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_yield_mid_loop|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_yield_inside_a_function|reported=yes|allocs=0|freed=0|live=0|bytes=0|ok=yes
IR-MIR-ALLOCS|case=mir_yield_around_allocating|reported=yes|allocs=1|freed=1|live=0|bytes=256|ok=yes
IR-MIR-COVER|...|mir-only-cases=96|mir-only-same-as-interpreter=96
IR-MIR-VERDICT|PASS|tree=a29fd74...|accepted=86|refused=0|differ=0
```

发射器侧（合并前在本树上量的）：`handles=51|of=67|unknown=16|agree=yes`、`accepted=182|same-as-interpreter=182`、`beyond-ast-denominator=22`（`OP_YIELD` 进了「MIR 独有例子上解释器执行过的 opcode」那一栏）、`covered=29|missing=0` [obs: a29fd74]。

### 六、边界

1. **带线程体的程序仍取不到这条**：容器里有线程段就在 `why=threads` 处被拒，所以「`yield` 真的让出一个 task」那种形状永远不会到发射器——本格的 no-op 与解释器的 no-op 之所以相同，**靠的正是那道更早的拒绝**。
2. **`OP_WAIT` 与线程八件套仍在 not-yet**：它们与 `yield` 不同，`WAIT` 会真的等一个时长，本路线没有调度器可等。
3. **`OP_SET_INTERVAL` / `OP_RECORD` / `OP_DECLARE` 各有各的成本**：区间要一整套区间运行时，`RECORD` 是记录元数据子系统，`DECLARE` 只能靠触发资源限才观测到——这三个都不是「再一格 no-op」。

## §10.137 `try` / `catch` / `finally`：抛是一次置标志，帧是一个槽（aot-mir，A 类第二十六项）

### 一、来源与日期

判据 `4835034`（十一例先红、外加一条拒绝判据与一条退役）、实现 `575a9d5`、缓冲区修正 `7054191`，2026-11-09。这是 `unknown` 从 16 降到 14 的那一格——两个 opcode，一次。

### 二、形状

**解释器侧**：`try` 是一个栈。`OP_TRY_START` 先在字节码的 `try_entries` 里按起点查一条条目，查不到就什么都不做（纯 no-op），查到就把一帧 `{code, ip, sp, base, frame_count, catch_ip, var_idx, ignore}` 压进 `exc_stack`；`OP_TRY_END` 弹一层；抛的时候 `vm_throw` 取**栈顶**那帧、按它的 `frame_count` 回退帧、把错误值写进 `var_idx` 指的那个**全局**（`var_idx < 0` 的裸 `catch { }` 只吞掉并记一笔），然后跳到 `catch_ip`。容器返回时，属于它的那些帧按 `frame_count` 被弹掉。注意 `TRY_START` 的 `r2` 是**全局号**，不是寄存器——所以这一臂的掩码是零。

**本路线侧**：**不用 `setjmp`/`longjmp`**，用一条传播标志。这样选是实测逼出来的：在 `try` 里写一个数组元素、再从 `catch` 里读回，`longjmp` 之后读到的是 `setjmp` 那一刻的旧值（`111`，不是 `222`）——`longjmp` 回滚的正是寄存器，而这条路线把每一个值都放在寄存器 `v[]` 里。判据 `mir_try_keeps_a_register_written_in_the_body` 就是那条会抓住这件事的判据（解释器印 `2`）。改成 `volatile` 也不行：拼接路径要一个 `MirVal *`，`volatile MirVal *` 传不进去。

于是三个机制：

1. **抛 = 置标志 + 写变量**。`mv_throw(v)`：还有帧就把它写的那个变量（`mv_try_var[--mv_try_depth]`，裸 `catch { }` 是空指针）接住这个值、置 `mv_throwing`；没有帧就照旧未捕获。
2. **最内层 catch 在翻译期解析**。每个容器在发正文前算一张表：每条指令 → 覆盖它的条目里**起点最大**的那条（属性是数字，不是数组顺序；见四.2）。每个「可能抛」的指令之后就发一句 `if (mv_throwing) { mv_throwing = 0; goto L<catch_off>; }`。
3. **非 main 容器在返回时恢复深度**（`mv_try_mark`）：对应解释器返回时弹掉自己那些帧的那段循环。没有它，`try { return 5 }` 会把帧留给调用者。

**可捕获的家族**：`throw` 本身、下标类型错、两处 `OP_BIND` 的 stop、算术的 `division_by_zero` 与 `numeric_overflow`。后两者来自**共享前导**（两条路线必须逐字相同，`classify()` 读它），所以只能在发射点**前置检查**：`mv_try_*` 六个 wrapper 只在程序用到 `try` 时发射，没用到 `try` 的程序生成文本一字不动。它们的判据必须与共享前导一致：浮点除零是 `inf`、不抛；`%` 用整数化之后的零判；溢出用 `__builtin_*_overflow`；`+` 撞到字符串直接走拼接路径。

**按名拒绝的形状**：跳出或跳进 `try` 区，`why=try-escapes` / `why=try-enters`。这不是省事：`break` 跳出 `try` 时解释器**自己**漏帧（实测同一个程序里，那个过期的帧接住了循环之后才发生的 `throw`，先印 `2` 再印 `late`，rc 0）——本路线不照抄这个漏。

### 三、判据

十一例执行 + 一例拒绝，全部先在解释器上量出 oracle，再在 `489a674` 上先红。

| 用例 | 钉住什么 | 红读数（`489a674`） | 绿读数（本树） |
|---|---|---|---|
| `mir_try_catches_a_throw` | 抛出来、进 catch、之后继续 | 拒译 `OP_TRY_START:1,OP_TRY_END:1` | `a / boom / after`，13 字节 |
| `mir_try_body_finishes` | 不抛时 catch 不跑（**退役来的**，见五） | 同上 | `x / done`，7 字节 |
| `mir_try_catches_from_a_callee` | 被调容器里抛、调用者的 try 接住 | 同上 | `inner / after`，12 字节 |
| `mir_try_catches_an_arm_stop` | 绑定的 `type_mismatch` 也是可捕获的 | 同上 | `type_mismatch / after`，20 字节，`allocs=1`（是声明那块集合） |
| `mir_try_nested` | 内层抛进内层 catch、catch 里再抛进**外层** catch | `:2,:2` | `inner / mid / after`，16 字节 |
| `mir_try_bare_catch` | 裸 `catch { }` 没有变量也不许崩 | 同上 | `bare catch / after`，17 字节 |
| `mir_try_catches_a_non_string` | 抛一个数字也能接住、`say e` 印 `5` | 同上 | `5 / after`，8 字节 |
| `mir_try_finally_cleanup` | finally 在错误仍未捕获时照跑，进程仍 rc 1 | 同上 | `t / cleanup`，10 字节，rc 1，stderr 含 `[exception] uncaught: boom` |
| `mir_try_finally_keeps_return` | finally 跑完、`return` 的返回值还在 | 同上 | `t / cleanup / 1`，12 字节 |
| `mir_try_return_from_inside` | 从 try 里返回要把帧还回去 | 同上 | `5`，2 字节 |
| `mir_try_keeps_a_register_written_in_the_body` | catch 里读到 body 写进寄存器的值（`setjmp` 方案会印 1） | 同上 | `2`，2 字节 |
| `mir_try_escapes`（拒绝） | 跳出 try 区按名字拒，不按 `unsupported-opcode` | `why=unsupported-opcode` | `why=try-escapes`，`ok=yes` |

### 四、实现期才发现的四件事

1. **`mv_die_uncaught` 要先声明**。`mv_try_new`（深度超 64 就停在那里）写在 `mv_die_uncaught` 定义之前，生成的 `o.c` 先是隐式声明、随后撞上「静态声明跟在非静态声明之后」。两行前置声明解决。
2. **`try_entries` 不是嵌套顺序**。第一版按「后写的条目覆盖前面的」填那张表，于是 `try { try { ... } catch ... }` 的内层 `throw` 跳到了**外层** catch：`mir_try_nested` 印 `inner / after`，解释器印 `inner / mid / after`。改成逐格取「起点最大的 enclosing 条目」之后，内层 `goto L6`、内层 catch 里的再抛 `goto L12`。
3. **「什么都不发」等于拒译**（与 §10.136 同一条约束）：查不到条目的 `TRY_START`、以及没有 try 的程序里的两个臂，都只发一行注释，否则 `emit_container` 的守恒检查报 `emitted nothing, so the conservation equation could not see it`。
4. **那个 512 字节的缓冲区**。`aot_mir_subset_description` 把可发射 opcode 拼成一个字符串，长度上限写死 512；这一格多两个 opcode 之后它就超了，而循环在截断之后还按「想要的长度」前进，下一次调用拿到的是 `sizeof desc - o`（越过末尾后是个巨大的 `size_t`），于是写出界。**症状不在这个函数里**：`aot-mir subset` 死在 `*** buffer overflow detected ***`，覆盖仪器如实报 `REFUSED|the tool does not report its emittable set (IR-MIR-SUBSET)`，而不是猜一个分母。修成 2 KB + 遇截断即停，当前 53 个 opcode 的描述是 561 字符（`7054191`）。

### 五、读数（本树 `7054191`）

```
IR-MIR-COVER|...|mir-only-cases=107|mir-only-same-as-interpreter=107
IR-MIR-VERDICT|PASS|tree=7054191...|accepted=86|refused=0|differ=0|c-diff=86|perturb-stdout-changes=5
IR-MIR-SUBSET|emits=53|ops=(...)
IR-AOT-MIR|accepted=193|refused=0|same-as-interpreter=193|emittable=53|of-ops=67|not-emittable=14|beyond-ast-denominator=24
IR-AOT-MIR-LOWERING|handles=53|of=67|unknown=14|agree=yes
IR-AOT-MIR-VERDICT|PASS|...|covered=29|missing=0|same-as-interpreter=193
```

`OP_TRY_START`/`OP_TRY_END` 进了 `IR-AOT-MIR-BEYOND` 那一栏（解释器在 MIR 独有例子上真的执行过它们），`covered=29|missing=0` 不动。

**一条退役**：`mir_try_no_throw` 原本在拒绝清单里，注释写着「`throw` 那步之后它必须继续红」。这一步之后它不再是拒绝：它的程序搬进执行清单成为 `mir_try_body_finishes`，那条注释改写成了这次退役的记录——一个「先记下预测、后来被实测推翻」的判据，退回它该有的位置是执行比拒绝更强的断言，而把预测留在原地才是假装。

`ALLOCS_EXPECT` 里 `mir_try_catches_an_arm_stop` 那一行是**这一格唯一改过的期望值**：实现之前按「一条只印字再退出的臂不带块」猜成 `0|0`，实测 `1|256`，而那 256 字节不是 throw——是声明 `x: 1, 2, 3` 那块集合（与 §10.135 的那一行同形）。抛出去的值本身零分配：`mv_str` 交回的是**resident** 字符串（`ival = 1` 说「静态存储」）。改正写在那一行的注释里，不悄悄改。

### 六、边界与两笔债

1. **带线程体的容器仍拒**：`why=threads` 比 try 更早，本格的判据里没有一条起线程。
2. **`mv_try_var` 只有 64 格**：更深的嵌套会**大声停在那里**（`mv_die_uncaught`），不是静默截断。
3. **两笔既有的债，本格不修**（都不属于「照抄解释器与缺陷一致」那一类，而是本路线自己的洞）：①非数值操作数撞到 `mv_tag_to_nv` 的 `default: abort()`（`say "a" + 1` 在两路线里的 MIR 侧 rc 134，与 try 无关）；②`mv_arr_set` 的 `index_out_of_range` **不是**解释器的异常——`a[5] = 1` 在解释器里被静默忽略（实测只印 `after`、stderr 空、rc 0），所以它也不该被本格当成可捕获的东西。
4. **`finally` 的重抛路径没被判据覆盖到**：只有 `finally` 没有 `catch` 时，编译器在 finally 末尾重抛一个内部全局；本格的四条 finally 判据都带 catch。这是形状边界，不是未实现——重抛走的是同一条 `OP_THROW`。

## §10.138 `wait`：这条路线真的睡，而「负时长」是它唯一不复现的形状（aot-mir，A 类第二十七项）

### 一、来源与日期

判据 `9eb08a7`（六例先红）、实现 `39c7a00`，2026-11-09。这是 `unknown` 从 14 降到 13 的那一格。

### 二、形状

**解释器侧**：`wait` 的答案不是一个值，是一段时间。`src/vm/vm.c` 的 `L_WAIT` 先把操作数变成毫秒，用的是**它自己的类型阶梯**：`INT` 读 `ival`、`FLOAT` 读 `fval`、**其他任何类型一律 0.0**；然后 `ms = (int)(seconds * 1000)`。接着分四个去向：这个线程是 task 就交给调度器；`step_mode` 就交 `sleep_ms`；有 gui 泵或有逐帧回调就进循环泵帧；**剩下的（也就是这条路线接受的 CLI 形状）落到 `sleep_ms(ms)`**，即一次阻塞睡眠。睡完之后它还会做一次自动存档检查，而这条路线没有 `record`，那个检查是 no-op。

发射侧：`STMT_WAIT` 在 `src/compiler/compiler.c` 发 `OP_WAIT r, 0, 0`——只有 `r1` 是寄存器，`r2`/`r3` 为零，所以掩码是 `R1`、没有可校验的池索引或全局号。另外三处 `OP_WAIT` 属于 `worker.wait`（线程等待），本路线整段按 `why=threads` 拒。

**本路线侧**：**真的睡**。这不是「照抄一个 no-op」：判据能看到的只有字节，而字节在这里不携带延迟——如果只把这条指令当 no-op 放过去，生成的可执行文件就会印出同样的字节并且**立刻返回**，那是一个用「同样的输出」掩盖「不同的行为」的答案。所以发射一个条件 helper `mv_wait`（只在程序里出现 `wait` 时发出，`g_wait_used` 与 `g_bound_used`/`g_try_used` 同一层：没有 `wait` 的程序，生成文本一字不动）：

- 类型阶梯**逐字照抄**解释器：`MV_INT` 读 `ival`、`MV_FLT` 读 `fval`、其他 0.0。**故意不用 `mv_as_nv`**——它在字符串上会 `abort()`（`mv_tag_to_nv` 的 default），而解释器把字符串读成 0 秒然后照常往下走；`mir_wait_a_non_number_is_zero` 钉的就是这一条。
- 阻塞用平台的睡眠：`_WIN32` 走 `Sleep`，否则 `nanosleep`。`#include` 发在**共享前导之后**，因为 `aot_native_preamble()` 在两条路线必须逐字相同，MIR 独有的 include 不能进它。

### 三、判据

六例，全部先在解释器上量出 oracle，再在 `84f89d8` 上先红。

| 用例 | 钉住什么 | 红读数（`84f89d8`） | 绿读数（本树） |
|---|---|---|---|
| `mir_wait_at_toplevel` | 延迟不改变输出、不吞语句 | 拒译 `OP_WAIT:1` | `1 / 2`，4 字节 |
| `mir_wait_zero` | `wait 0` 是一次立即返回 | 同上 | `1 / 2`，4 字节 |
| `mir_wait_inside_a_function` | 容器里的等待不打断调用点 | 同上 | `7 / done / after`，13 字节 |
| `mir_wait_with_a_variable_delay` | 延迟可以来自寄存器 | 同上 | `1 / 2`，4 字节 |
| `mir_wait_in_a_loop` | 循环里反复等待后计数仍对 | 同上 | `3`，2 字节 |
| `mir_wait_a_non_number_is_zero` | 类型阶梯，不是 tag：字符串读成 0.0 且**不许 abort** | 同上 | `1 / 2`，4 字节 |

六行的 `ALLOCS` 全是 `allocs=0|bytes=0`（先量后写）：睡眠是 libc 调用，不是池里的块，而这一格没有任何容器构造。

### 四、实现期才发现的四件事

1. **`pause` 是关键字**。`mir_wait_inside_a_function` 的第一版把容器命名为 `pause`，解析器直接拒：`expected 'function name', but got 'pause' (type 45)`。改成 `hold`，并把这次改名写进注释——否则下一个人会以为是笔误。
2. **这条 opcode 没有 pre-scan 分支，而且不该有**。它不读池、不读全局、不读表，没有可校验的操作数；而覆盖仪器的操作数检测器把「有 pre-scan 分支却没有索引读者」的标签判成 dead label。所以 `g_wait_used` 来自一次**全程序扫描**（helper 在任何容器被降级之前就要发出去），不是 pre-scan。
3. **类型阶梯不能用 `mv_as_nv`**：见二。这是「同一件事在两条路线上的两种写法」里最容易写错的一处——`mv_as_nv` 是本路线别的臂的默认选择，在这里恰好是错的。
4. **负数是一处明知的分歧**（见六）。

### 五、读数（本树 `39c7a00`）

```
IR-MIR-COVER|...|mir-only-cases=113|mir-only-same-as-interpreter=113
IR-MIR-VERDICT|PASS|tree=9eb08a7...   ← 判据笔上的绿（实现前的那棵树）
IR-MIR-ALLOCS|case=mir_wait_at_toplevel|...|allocs=0|freed=0|live=0|bytes=0|ok=yes   （六例同形）
IR-AOT-MIR|accepted=199|of=29|covered=29|missing=0|refused=0|same-as-interpreter=199|beyond-ast-denominator=25
IR-AOT-MIR-LOWERING|handles=54|of=67|unknown=13|agree=yes
```

**手工计时**（同一台机器、各三次、逐次取整到毫秒；判据不做时间断言，见六.2）：

| 程序 | 本路线 | 解释器 |
|---|---|---|
| `say 1 / wait 0.3 / say 2` | 0.3096 / 0.3097 / 0.3075 | 0.3558 / 0.3526 / 0.3551 |
| `say 1 / wait 0 / say 2` | 0.0066 / 0.0073 / 0.0064 | 0.0468 / 0.0408 / 0.0452 |

那个 45 毫秒的差不是睡眠：它是解释器加载模块的启动开销。把两行相减，两条路线在同一台机器上花在睡眠上的都是 300 毫秒。

### 六、边界

1. **负时长是本格唯一不复现的形状**。解释器把 `wait -1` 变成 `ms = -1000` 再交给一个**无符号**的睡眠，于是一去不回——探针程序跑满两分钟仍未返回，被移到后台后杀掉。本路线在 `ms <= 0` 时立即返回，所以这一条是**明知的分歧**：复现一次挂死不是「与解释器一致」，而是一段无法被测的代码。判据里没有负时长，因为没有一条能把两条路线都跑完的 oracle。
2. **判据不测时间**。墙钟断言是抖动测试穿上了测量的衣服：同一台机器上 300 毫秒能读成 297 到 310。所以延迟只在本文档手工记录，判据只钉「不许改变输出 / 不许吞语句 / 不许分配」。
3. **Windows 那半个 helper 在本机没编过**。这里只编译并跑了 POSIX 分支（`nanosleep`）；`_WIN32` 分支（`Sleep`）在这台机器上没有编译器验证过，写在这里而不是假装它被测过。
4. **线程等待仍未降级**：`worker.wait` 的三处发射点都在线程语境里，随 `why=threads` 整段拒掉，与本格无关。

## §10.139 `record`：这条路线不存/不写/不生成存档，于是操作数就是答案（aot-mir，A 类第二十八项）

### 一、来源与日期

判据 `28a1a55`（五例先红）、实现 `4247cc4`，2026-11-09。这是 `unknown` 从 13 降到 12 的那一格，也是这条路线**最后一条便宜的格子**：剩下的十二个 opcode 每个都要一个子系统（见六.4）。

### 二、形状

**编译器侧**：`record name = value [, tags]` 不是一条赋值指令，是三条。名字第一次出现时它发 `OP_MOV rs, r0` → `OP_RECORD g, (meta << 16) | sidx, rs` → `OP_STORE_GLOBAL g, rs` → 记下这个全局已经是 record 全局；之后再写同名时**只发** `OP_STORE_GLOBAL`。`record default store = value` 是另一形，发 `OP_RECORD -1, store, 0`（`r1 = -1` 表示这条指令没有名字）。

⇒ **赋值永远是紧跟的那条 `OP_STORE_GLOBAL` 干的**；`OP_RECORD` 自己只写元数据（名字、store、scope、merge 四个字段）。

**解释器侧**：`src/vm/vm.c` 的 `L_RECORD` 先写元数据（按需 `realloc` 扩表、`strdup` 名字），然后有一个**分支**：只有在启动时载入过存档（`vm->record_loaded_dict > 0`）并且这个名字在存档里有，它才把**存档里的值**搬进 `r3` ——也就是把编译器刚放进寄存器的操作数**覆盖掉**。没有存档时这个分支整段跳过，寄存器保持 `MOV` 写进去的值。

**本路线侧**：把这条指令降级成**元数据 no-op**。它不写元数据、不载存档、不写存档、也不创建存档文件；`r3` 保留 `MOV` 放进去的值，随后的 `STORE_GLOBAL` 写入程序自己的值 ——**这正是解释器在干净目录里第一次跑时的行为**。

掩码是 `R3`、不是 `0`：`r1` 在本路线是全局号、`r2` 是打包的名字下标，两者都不是寄存器，所以都不进掩码；`r3` 在 IR 自己的语义里确实是寄存器（有存档时解释器写的就是它），所以它留在掩码里，而**将来真带存档的路线正好写在那里**。

### 三、判据

五例，都先在解释器上量出 oracle，再在 `23465c0` 的主树二进制上先红。

| 用例 | 钉住什么 | 红读数（`23465c0`） | 绿读数（本树 `4247cc4`） |
|---|---|---|---|
| `mir_record_assigns_the_value` | 值经 `STORE_GLOBAL` 落地 | 拒译 `OP_RECORD:1` | `5 / done`，7 字节 |
| `mir_record_two_names` | 两个名字各自独立 | 拒译 `OP_RECORD:2` | `1 / 2`，4 字节 |
| `mir_record_a_store_tag` | 带 `store:` 标签的形不影响值 | 拒译 `OP_RECORD:1` | `6`（`rtagged + 1`），2 字节 |
| `mir_record_default_store` | `default store` 形没有名字也没有副作用 | 拒译 `OP_RECORD:1` | `1`，2 字节 |
| `mir_record_then_overwrite` | record 之后普通赋值仍覆盖它 | 拒译 `OP_RECORD:1` | `5 / 9`，4 字节 |

五行的 `ALLOCS` 全是 `allocs=0|bytes=0`（先量后写）：本格没有构造任何容器，也没有 helper——元数据 no-op 的生成文本里只有一行注释。

### 四、读数

```
IR-MIR-MIR-ONLY|case=mir_record_assigns_the_value|mir=accepted|cc=ok|exec=same|rc=0|bytes=7|stdout-hex=350a646f6e650a
IR-MIR-MIR-ONLY|case=mir_record_two_names|...|bytes=4|stdout-hex=310a320a
IR-MIR-MIR-ONLY|case=mir_record_a_store_tag|...|bytes=2|stdout-hex=360a
IR-MIR-MIR-ONLY|case=mir_record_default_store|...|bytes=2|stdout-hex=310a
IR-MIR-MIR-ONLY|case=mir_record_then_overwrite|...|bytes=4|stdout-hex=350a390a
IR-MIR-COVER|...|mir-only-cases=118|mir-only-same-as-interpreter=118
IR-MIR-VERDICT|PASS
IR-AOT-MIR|accepted=204|of=29|covered=29|missing=0|refused=0|same-as-interpreter=204|beyond-ast-denominator=26
IR-AOT-MIR-LOWERING|handles=55|of=67|unknown=12|agree=yes
```

**`save.dat` 的实测转写**（同一目录、逐字节；这是本格的核心发现，不是脚注）：

| 步骤 | 程序 | 解释器印 | 存档文件 |
|---|---|---|---|
| ① 干净目录 | `record a = 5 / say a` | `5` | 生成 `{"a":5}` |
| ② 同一目录 | `record a = 9 / say a` | **`5`** | 仍是 `{"a":5}` |
| ③ 同一目录 | 没有 `record` 的程序 | 正常 | **不生成** |

② 那行是关键：解释器把**存档里的 5** 覆盖了程序写的 9。同一个程序在同一目录跑两次会给出不同答案；两次之间换个程序写存档也会改变答案。⇒ **`record` 是这条路线遇到的第一个「输出是宿主磁盘的函数、而不是程序的函数」的形状**。

### 五、实现期才发现的四件事

1. **harness 的目录是共享的**。`tools/aot_mir.test.py` 每次调用只 `mkdtemp` 一次，全部用例共用一个工作目录 ⇒ 解释器侧会在里面累积 `save.dat`。判据因此遵守两条稳定性规则，都写在判据组的注释里：**每个 record 名字在本文件里只出现一次**（存档不可能替一个它没来源的名字作答）、**每例写回的值正是它自己会读到的值**（同一个用例重跑读到的字节相同）。五条判据都满足这两条，所以它们在共享目录里仍然逐字节确定。
2. **`r1`/`r2` 在本路线不是寄存器**，而覆盖仪器的操作数检测器会把「有 pre-scan 分支却没有索引读者」的标签判成 dead label ⇒ 本格**故意不加 pre-scan case**（同一处理见 `OP_WAIT`、`OP_TRY_END`）。没有全局号范围校验也安全：本路线不使用那个全局，越界的全局号在解释器那边同样是「什么都不做」。
3. **生成文本里故意点名那个寄存器**。注释写成 `/* OP_RECORD: no record store on this route -- v[3] keeps what the preceding MOV put there */`，把 `r3` 写进文本：读者应该能看见这是**故意不动**，而不是忘了动。
4. **worktree 里没有 `build/` 时要先 configure**。`cmake --build build -j4 --target aot-mir` 在全新 worktree 上直接报 `Error: .../build is not a directory`，得先 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`。

### 六、边界

1. **存档是明知的分歧，不藏**。解释器在退出时写 `save.dat`（只要有 record 元数据），之后同一目录里的程序读回存档值；本路线不载、不写、不生成，于是同一目录里的第二个程序会印程序自己的值（`9` 而解释器印 `5`）。这条分歧记在这里，而不是靠删掉判据来回避。
2. **判据只覆盖「干净目录」那一块**。那是两条路线都确定的唯一区域；跨运行的存档语义没有判据，理由与一中的稳定性规则相同——它需要一个**顺序依赖**的测试，而顺序依赖的用例会把「哪条用例先跑」变成读数的一部分，那种读数不是关于程序的事实。
3. **函数体内的 `record` 形状不被判据覆盖**。实测解释器里函数内 `record a = 7` 之后顶层 `say a` 印 `nil`（名字在函数内成了另一个槽），这条形状的对应关系还没查清，所以没有为它写判据——不是实现拒绝它，是**没有 oracle**。
4. **这一格之后再没有便宜的格子**。剩余十二个 opcode：线程六件（`START`/`CTRL`/`JOIN`/`WAIT`/`STATE`/`GOTO`）+ `LOCK`/`SEND`/`RECV` 三件都随 `why=threads` 整段拒；`OP_DECLARE` 是资源限制子系统（观测点是「触限」而不是值）；`OP_SET_INTERVAL` 要命名内建集代数 + 区间谓词 + 打印；`OP_SET_ADD` 只有推导式能发，而推导式先发 `list` 内建、本路线按名拒；`OP_POP_REG` 已从解释器移除、不可达。**每一个都是子系统，没有一个是「再写一条臂」**。

## §10.140 区间集：池里装的是谓词，不是清单（aot-mir，A 类第二十九项）

### 一、来源与日期

判据 `cf29586`（八例先红）、实现 `07f744a`，2026-11-09。这是 `unknown` 从 12 降到 11 的那一格。

### 二、形状

**解释器侧**：`[0, 3)` 不是数组（那是 `[0, 3]`），也不是有限集。它是 `OP_SET_INTERVAL`，答案是 `SetObj.kind = 2`（`src/vm/vm.h:57-69` [obs: 07f744a]）：一个**定义域名**、两个 `double` 边界、两位包含性，以及**没有任何可枚举的东西**。全部行径向三个函数读来：

- **成员**：`set_contains` 的 kind-2 臂（`src/vm/vm.c:2046-2053` [obs: 07f744a]）先问域名（`builtin_contains`），再问边界。`R` 的域名判定是 `return 1`（`:1998` [obs: 07f744a]），所以对 R 区间而言剩下的只有：**非数字不是成员**（`builtin_contains` 的第一行，`VAL_BOOL` 也在这一行被拒——这就是为什么这里不能改用 `val_is_num`）、NaN 不是成员，以及两个边界与它们的包含位。
- **相等**：`set_equal` 的 kind-2 行（`:2225-2227` [obs: 07f744a]）比域名、边界、两个包含位；kind 0 与 kind 2 的交叉相等要求「空字面量恰好一个组件」，本路线不构造那种形状，所以区间对有限集答 **false** 而不是停。
- **长度**：`0`。`vm_set_to_array`（`:2313-2316` [obs: 07f744a]）对 kind 1 返回 −1（整域内建集是无限的），对 kind 2 走 `set_comp_enum`——R 区间枚举出来是空的。
- **打印**：`set(R interval)`（`:1330` [obs: 07f744a]），域名加一个词，**不是计数**。

**本路线侧**：同一个池，**四个槽**，头槽说清这是什么：

```
头槽 (ival-1) : MV_INT, ival == -1        ← −1 表示「区间」；>=0 表示有限集的元素数
ival + 0      : MV_FLT  lo
ival + 1      : MV_FLT  hi
ival + 2      : MV_INT  flags（1=下界含，2=上界含）
```

那个 −1 是**每个已有的集合读者本来就在看的槽**：它们全都以 `int n = (int)mv_arrmem[iv-1].ival;` 开头，而负的计数不是计数，所以没有任何读者会把区间当成有限集。它也不会和转发头混淆——转发头是 `MV_ARR`，同一个槽，同一个理由。

**不造第三个 tag 的理由**：tag 会是同一个池槽的第二种分类，而这份文件已经两次论证过「同一个答案不能有两个主人」（字典的哈希表、集合的两半边）。头槽本身就是「这个区域装的是什么」的答案，而它本来就在装计数。

### 三、第一个「寄存器不全在操作数里」的 opcode

`EXPR_SETINTERVAL` 的发射序列（`src/compiler/compiler.c`）是：先算 lo、hi 并各自 `OP_PUSH_REG`，再发 `OP_SET_INTERVAL r, (nameIdx & 0xFFFFFF) | (flags << 24), 0`。所以 **r1 是目标寄存器，r2 是打包立即数，r3 不用；两个边界只存在于参数栈上**。这是本路线第一次遇到这种形状：

- `op_regs` 只能给 `R1`——给 r2 一位会让通用的寄存器边界循环把一个字符串池下标当寄存器号查；
- `prescan` 必须**消耗两个栈槽**（两个边界都会被推，即使源码里省略，编译器也会用 ±1.0e308 补上）；
- 发射臂读 `mvargs[asp-2]` 与 `mvargs[asp-1]`。

**第一版把这两个写成 `v[asp-2]`／`v[asp-1]`**，于是每一次成员判定都答 false：`mir_interval_membership` 印 `false false false false false`，解释器印 `true true false false false`。判据抓到了它，改一个字母就对了——这就是「判据先红」在真实缺陷上的价值，不是流程装饰。

**域名不是 R 时按名拒绝**（`why=set-interval-domain`）：同一套语法既能到 `R[0~3)`（kind 2，本 opcode），也能到 `Z[0~3]`（**有限集**，kind 0 带一个组件，实测 `set(4)` 且成员可枚举）。把 Z 当 R 答，是错答而不是缺答。

**非数字边界不是拒译**：解释器对 `[nil, 3)` 的回答是**整个值变成 nil**（`L_SET_INTERVAL` 的 `!val_is_num` 臂），所以发射的是 `v[r1] = mv_nil()` 而不是停；随后 `1 in p` 自然为 false。

### 四、判据

八例，全部先在解释器上量出 oracle，再在 `49b0e0e` 上先红——红的形状与最近几格不同：**不是"没有这个 opcode 的臂"，而是 `OP_SET_INTERVAL` 的两个操作数没人读**，所以八条红全是 `illegal-operand ... leaves the argument stack unbalanced: asp=2`（相等那条 `asp=6`，函数里那条 `container=1`）。这个区别值得写下来：判据测的是参数栈那一半。

| 用例 | 钉住什么 | 绿读数（本树 `07f744a`） |
|---|---|---|
| `mir_interval_membership` | 定义域先于边界；bool/字符串不是成员；INT 与 FLOAT 都在界内 | `true true false false false`，28 字节 |
| `mir_interval_inclusive_upper` | `(0, 3]`：下界不含、上界含 | `false true true`，16 字节 |
| `mir_interval_open_upper_bound` | `[0, )`：上界开、`len` 为 0、打印域名 | `false true 0 set(R interval)`，29 字节 |
| `mir_interval_bounds_from_registers` | 边界来自变量（不是常量） | `true true false`，16 字节 |
| `mir_interval_len_and_print` | `len` 是 0、打印不是计数 | `set(R interval) 0`，18 字节 |
| `mir_interval_a_non_number_bound_makes_nil` | 非数字边界 → 整个值是 nil | `nil false`，10 字节 |
| `mir_interval_equality_by_bounds` | 相等按边界与包含位，区间对区间 | `true false true`，16 字节 |
| `mir_interval_inside_a_function` | 区间在容器里构造、返回后仍有效 | `true false 0`，13 字节 |

### 五、读数（本树 `07f744a`）

```
IR-MIR-COVER|...|mir-only-cases=126|mir-only-same-as-interpreter=126
IR-MIR-VERDICT|PASS
IR-AOT-MIR-LOWERING|handles=56|of=67|unknown=11|agree=yes
IR-AOT-MIR|accepted=212|same-as-interpreter=212|emittable=56|of-ops=67
  |not-emittable=11|beyond-ast-denominator=27
IR-AOT-MIR-VERDICT|PASS|covered=29|missing=0
```

`ALLOCS_EXPECT` 里**两行是量出来才改对的**，注释写明了哪两行、为什么：`a_non_number_bound` 是 `0|0`（值在调用 `mv_set_interval` 之前就变成了 nil，一个区域都不建），`equality_by_bounds` 是 `2|768`（程序建了三个区间，池长过第一个八槽块）。其余六例 `1|256`。三行都先按「一般形状」写成 1|256，然后被读数纠正——写在注释里而不是悄悄替换。

### 六、边界

1. **只有 R 域**。其余域名（N/Z/Z±/FloatN/floatN）在解释器里是**有限集或整域内建集**，形状不同（kind 0 + 组件，或 kind 1），本格按名拒译。要支持它们需要「内建集代数」这个子系统，不是再加一条臂。
2. **没有集合运算符**。`p | [5, 7)` 直接解析失败（`Error: expected 'expression', but got '|'`），所以并/交/差不可达，本格不需要回答它们。
3. **`OP_SET_ADD` 仍是 not-yet**：它只由推导式发射，而推导式先发 `list` 内建、本路线按名拒。
4. **区间与有限集的相等答 false**，这是解释器 `set_equal` 的规矩（交叉相等要求空字面量 + 恰好一个组件），不是本路线的简化——判据里没有这条，因为本路线构造不出那种字面量。

## §10.141 `lock`：成对的那种是一个 no-op，不成对的按名拒绝（aot-mir，A 类第三十项）

### 一、来源与日期

判据 `7507ffc`（四条执行 + 两条拒绝）、实现 `c6026be`，2026-11-09。这是 `unknown` 从 11 降到 10 的那一格。

### 二、形状

`lock m { ... }` 在字节码里是**同一条 opcode 出现两次**：`OP_LOCK midx, 0` 取锁、body、`OP_LOCK midx, 1` 放锁（`src/compiler/compiler.c`，`STMT_LOCK` 的 `isBlock == 1` 路径；`isBlock == 0` 是裸 `lock m`，`isBlock == 2` 是 `unlock m`）。解释器的臂（`src/vm/vm.c`，`L_LOCK`）按名字查 `vm->mutexes`，第一次用到就 `im_mutex_new()` 建出来，然后按 `r2` 调 `im_mutex_lock` 或 `im_mutex_unlock`。

**本路线只有一条线程**（带线程段的容器在更早的地方按 `why=threads` 拒掉），所以互斥量没有任何竞争者，两次调用都不可观测。于是这一臂是一个**注释**——与 §10.136 的 `yield` 同一类「诚实的 no-op」，而且必须是注释而不是沉默：`emit_container` 对「正文没变长」的指令会拒译。

`op_regs` 给的是零掩码：`r1` 是**互斥量下标**（容器 `mutexes` 表里的运行期对象，不是池槽），`r2` 是方向位，两个都不是寄存器。

### 三、不成对的那种：按名拒绝，而且理由是实测

**`lock m` 不带块会让解释器死锁。** 它取锁、然后永远不放。实测：块形式印 `a\nin\nb`，裸形式的探针**两分钟没有返回**，被 kill。

所以 prescan 对同一互斥量下标做**成对检查**，两个方向都拒，`why=lock-unpaired`：

- 取了不放（`lock m` 裸写）⇒ 拒绝；
- 放了没取（`unlock m` 单独出现）⇒ 拒绝，理由是它会放一把本路线从没取过的锁。

把挂死照抄过来不是「与解释器一致」，而是一段无法被测的行为；把裸形式答成「又一个 no-op」则会掩盖读者最需要被告知的那个形状。两条拒绝都进了 `MIR_ONLY_REFUSE`，所以拒绝是被测量的，不是被许诺的。

### 四、判据

| 用例 | 钉住什么 | 红读数（`793d0e4`） | 绿读数（本树 `c6026be`） |
|---|---|---|---|
| `mir_lock_block` | 块形式：body 跑了、前后语句都在 | `unsupported-opcode (OP_LOCK:2)` | `a / in / b`，7 字节 |
| `mir_lock_inside_a_loop` | 循环里反复取放不改变计数 | 同上 | `3`，2 字节 |
| `mir_lock_return_from_body` | body 里的 `return` 不被锁吞掉 | 同上 | `7`，2 字节 |
| `mir_lock_around_a_container` | 锁块里建容器、`len` 仍对 | 同上 | `2 / done`，7 字节，`allocs=1` |
| `mir_lock_bare_never_released`（拒绝） | 裸 `lock m` 按名拒 | `unsupported-opcode` | `why=lock-unpaired`，`ok=yes` |
| `mir_lock_unlock_alone`（拒绝） | 单独 `unlock m` 按名拒 | `unsupported-opcode` | `why=lock-unpaired`，`ok=yes` |

四条执行判据的 `ALLOCS`：三个 `0|0`（互斥量不是池区域，这三个程序也不建容器），`mir_lock_around_a_container` 是 `1|256`（它建了一个数组字面量）——**量出来的**，不是从「锁不分配」推出来的。

### 五、读数（本树 `c6026be`）

```
IR-MIR-COVER|...|mir-only-cases=130|mir-only-same-as-interpreter=130
IR-MIR-VERDICT|PASS
IR-AOT-MIR-LOWERING|handles=57|of=67|unknown=10|agree=yes
IR-AOT-MIR|accepted=216|same-as-interpreter=216|emittable=57|of-ops=67
  |not-emittable=10|beyond-ast-denominator=28
IR-AOT-MIR-VERDICT|PASS|covered=29|missing=0
```

### 六、实现期被两套仪器抓到的两件事

1. **把检查写成 `case OP_LOCK` 会被覆盖仪器判死标签。** 第一版把成对检查放进操作数 switch，`tools/aot_mir_cover.test.py` 直接报 `prescan checks OP_LOCK, which the emitter does not index`——因为它不读池、不读全局、不读表。移到 switch 外面写成 `if (op == OP_LOCK)` 才对，这也是 `OP_WAIT`、`OP_TRY_END` 已经在用的位置。
2. **移出去之后，`break` 跳出的是指令循环而不是 switch。** 于是每条指令剩下的那半段 pass 被跳过，生成的 C 里出现 `(null) = v[1];`——一个 pass 从没标记过「被引用」的全局。这次是**覆盖仪器**报出来的（`mir_lock_around_a_container: the generated C did not compile: 'null' undeclared`），修法是 `continue`，注释里写明了这两个词为什么不能互换。

第 2 条值得单独记：它不是因为逻辑写错，而是因为**移动一段代码改变了它所在的作用域**，而「break 跳出哪一层」正是那种一旦错了也不会有编译错误的决定。

### 七、边界

1. **线程仍在更早处被拒**：带线程段的容器根本到不了这一臂，所以「互斥量真的挡住了别人」这个形状在本路线不可测，也不声称。
2. **`send` / `recv` 仍需线程**：`send t1 5` 连解释器都答 `未定义的线程 't1'`（线程名要在 `thread` 段里定义），而线程段本身被拒；这两个 opcode 与前六个线程 opcode 一起留在 `unknown` 里。
3. **跨容器的取放不检查**：成对检查是**每个容器内**的，因为块在字节码里是连续的；跨容器的裸取（在一个函数里 `lock m`、在另一个里 `unlock m`）本路线不检查——解释器那种情形同样是死锁风险，但本路线的判据里没有它，所以不声称覆盖。

## §10.142 内建 `list`：集合的枚举顺序第一次变成可观测的（aot-mir，A 类第三十一项）

### 一、来源与日期

判据 `2a456e8`（五条先红，第六条是差分探测后补的）、实现 `62cc8cc`，2026-11-09。这是**这一系列里第一个不推动 `handles` 的格子**，理由写在六.1。

### 二、形状

`list(x)` 不是 opcode，是内建（`src/runtime/runtime_posix.c` 的 `posix_core_list`），语义只有三种回答：

| 输入 | 输出 |
|---|---|
| 数组 | **原样返回同一个句柄**（`pop` 再 `push` 回去，不是拷贝） |
| 有限集 | 枚举成一个新数组 |
| 其他任何东西（整数、浮点、布尔、nil、字符串、字典、区间集） | `nil` |

**枚举顺序是被观测到的，不是猜的**（`.scratch/iv/m1.im`，逐字节）：

```
s = 3, 1, 2      -> [1, 2, 3]        ← 整数半边升序
t = 1, 2.5, 3    -> [1, 3, 2.5]      ← 整数升序在前，其余保持压栈序
u = 2.5, 1, 3    -> [1, 3, 2.5]      ← 与 t 相同：整数半边永远先出，与压栈顺序无关
```

这来自 `vm_set_to_array`：先走 `s->i64`（解释器那半边是**排序插入**的），再走 `s->items`（插入序），最后才是 comps。所以降级必须是**两遍**而不是整体排序——整体排序会把 `1, 2.5, 3` 排成 `[1, 2.5, 3]`，而解释器给 `[1, 3, 2.5]`。`mir_list_mixed_set` 就是那条会把整体排序钉红的判据。

这是这条路线上**集合的内部顺序第一次可以被程序看见**：在此之前，集合只有 `len`、`in`、相等三种观测，三者都与顺序无关。

### 三、判据

六条，全部先在解释器上量出 oracle。

| 用例 | 钉住什么 | 绿读数 |
|---|---|---|
| `mir_list_of_a_set` | 三个乱序整数 → 升序 | `[1, 2, 3]`，10 字节 |
| `mir_list_mixed_set` | 两遍枚举（整体排序会红） | `[1, 3, 2.5]` 两次，24 字节 |
| `mir_list_of_an_array` | 数组原样（同一句柄，不是拷贝） | `true` / `[1, 2]`，12 字节 |
| `mir_list_of_scalars` | 五种标量 → 全 nil | `nil nil nil`，12 字节 |
| `mir_list_of_an_interval` | 区间集 → nil（不许被当成有限集枚举） | `nil`，4 字节 |
| `mir_list_narrows_wide_ints` | 见四节 | `2` / `[705032704]`，14 字节 |

### 四、差分探测才发现的一处真分歧：枚举时整数会被窄化到 32 位

前五条绿了之后做差分探测，发现 `vm_set_to_array` 枚举整数半边时写的是 **`(int)s->i64[i]`**——**窄化到 32 位**。窄化的后果不只是丢位，还可能**与另一个元素撞车**：

```
s = 5000000000, 705032704
解释器：len(s) = 2，list(s) = [705032704]
```

`5000000000` 窄化后是 `705032704`，与字面量里的第二个元素**相等**，于是被 `array_has` 式的去重挡掉，数组里只剩一个元素——而 `len` 仍是 2（`len` 数的是集合自己的两个半边计数，不看窄化后的值）。

实现照字面复现了这个顺序：先按升序排，再 `(int)` 窄化，再对已经发射过的值做一次去重。第一版在这里给了 `[1, 5000000000]`，是错的；这条用例是第六个加进来的，前五条全在 int32 内，抓不到它。

**这条分歧的性质**：它是解释器自己的行为，不是本路线的简化，所以照抄——但要写成「与解释器一致」，不写成「已答对」：`list` 一个含 `5000000000` 的集合给出 `[705032704]`，那不是任何一个心智模型会预期的答案。这一条与 §10.128（集合键）、§10.131（转递捕获）同属一类：「逐字节一致」这个目标有时候意味着复现对方的缺陷。

### 五、读数（合并树 `14887bc`）

```
IR-MIR-COVER|...|mir-only-cases=136|mir-only-same-as-interpreter=136
IR-MIR-VERDICT|PASS
IR-AOT-MIR-LOWERING|handles=57|of=67|unknown=10|agree=yes
IR-AOT-MIR|accepted=222|same-as-interpreter=222|emittable=57|of-ops=67
  |not-emittable=10|beyond-ast-denominator=28
IR-AOT-MIR-VERDICT|PASS|covered=29|missing=0
```

`ALLOCS_EXPECT` 六行都是**先跑出来再写**：`1|256`、`2|768`、`1|256`、`0|0`、`1|256`、`1|256`。其中 `mir_list_of_an_array` 是 `1|256` 而不是 `0|0`，尽管 `list()` 一个字节都不复制——因为程序自己的 `a = [1, 2]` 建了那个数组；`mir_list_of_an_interval` 同理（`p = [0, 3)` 是四槽区间区域，`list` 加的是零），这一行正是「nil 不花块」的读数。

### 六、边界

1. **这一格不推动 `handles`，而且不该推动。** `handles` 数的是 **opcode**，`list` 是内建；把它算进 `handles` 会让那个读数变成「实现了多少东西」的含糊指标，而不是「67 个 opcode 里答了几个」。它推动的是 `accepted`（216→222，两格合起来）与 `mir-only-cases`（130→136）。
2. **`list` 是推导式的前置，但不是推导式本身。** 推导式的第一条字节码就是 `OP_CALL_BUILTIN list`，所以以前整个程序停在那里；现在它推进一站，停在 `IR-MIR-NOT-YET|op=OP_SET_ADD|n=1`。两处说「停在 list」的旧注释已按实测改正。
3. **数组的「同一句柄」只在语言层可观测**：`list(a) == a` 为 true。本路线返回同一句柄时这条自然成立，拷贝会答 false——判据里有这一条（`mir_list_of_an_array` 的 `true` 行）。
4. **区间集的 `list` 是 nil 而不是空数组**：解释器的 `vm_set_to_array` 对 kind 2 会去枚举组件，但本路线根本不构造那种组件；实测 `list([0, 3))` 印 `nil`，所以按 nil 答。

## §10.143 `OP_SET_ADD`：推导式的写半边，以及集合第一次会长大（aot-mir，A 类第三十二项）

### 一、来源与日期

判据 `8aef037`（五条先红）、实现 `377ec38`，2026-11-09。这是 `unknown` 从 10 降到 9 的那一格，也是**推导式那条路的终点**：`list`（§10.142）把程序送过第一个内建，这一格送过第二个 opcode。

### 二、形状

`{ x in S | cond }` 的字节码是：建一个**空集合**（`OP_NEW_SET r, 0, 0`）→ 循环 `list(S)` → 每个满足条件的成员发一条 `OP_SET_ADD result, x`。所以这条 opcode 是**这条路线上第一次给数组以外的容器写入**。

解释器的臂只有两行（`L_SET_ADD`）：`if (sv->type == VAL_SET) vm_set_add(vm, sv->ival, &R[ins.r2]);`。真正的规则在 `vm_set_add`（`src/vm/vm.c`）里，而且是**非对称**的，与 `mv_set_has` 已经实现的那条相同：

- **整数**：在排序的 i64 半边二分查找，不存在就**插入**（`memmove` + 存），从不看 items；
- **其他任何 tag**：在 items 里用 `val_eq` 扫，不存在就**追加**，从不看 i64。

`mir_set_add_floats` 只加浮点，所以一个「统一相等」的实现也能过它——混合的情形才把两种规则分开，判据里那几条整数/浮点混合的正是为此。

### 三、结构上的那一半：集合现在会搬移

集合区域是 `[count][elem...]`，**没有容量槽**，所以「加一个元素」必须先腾地方。而一个不在池前沿的区域**不能原地扩展**——它上面的槽属于更晚建的区域。于是它按数组的老规矩长大（`mv_arr_set` 的规则）：

- 在池前沿 → 原地扩展，句柄不变；
- 不在前沿 → **拷到前沿**，旧句柄处留下一个**转发头**。

**这一步让所有集合读取函数原先的写法变成错的。** 在这一格之前集合永远不会搬移，所以 `mv_set_has` 与 `mv_set_eq` 直接拿 `ival` 当区域下标用；现在两者都先调 `mv_arr_live`。`mv_len` 与 `mv_list` 本来就有那一跳，因为**数组**早就会通过它们搬移。

写这一格时先量到的事实：`s = { ... }` 之后建一个数组，集合就不再位于前沿，它的第一次 `add` 必须拷贝到前沿并留下转发头；同一程序里把数组读回来，是「搬移有没有压到邻居」的第一处观测点，而 `say list(s)` 读两次则钉住「旧句柄搬移后仍然指向这个集合」。

### 四、判据

| 用例 | 钉住什么 | 绿读数 |
|---|---|---|
| `mir_set_add_dedups_and_orders` | 去重 + 枚举顺序 | `[1, 2, 3]` / `3`，12 字节 |
| `mir_set_add_filters` | 条件只留下合格成员 | `[2, 3]`，7 字节 |
| `mir_set_add_floats` | 浮点走 items 半边，去重 | `[1.5, 2.5]` / `2`，13 字节 |
| `mir_set_add_empty_source` | 空源仍然建集合：`set(0)`、`0`、`[]` | 12 字节 |
| `mir_set_add_moves_the_region` | 搬移后旧句柄仍可读、邻居未受损 | `[3, 5, 9]` 两次 / `7`，22 字节 |

五条红全是 `unsupported-opcode (OP_SET_ADD:1)`——这是本系列里少见的「红得干脆」：没有参数栈溢出之类的次要症状，判据测的就是这个 opcode 本身。

### 五、读数（本树 `377ec38`）

```
IR-MIR-COVER|...|mir-only-cases=141|mir-only-same-as-interpreter=141
IR-MIR-VERDICT|PASS
IR-AOT-MIR-LOWERING|handles=58|of=67|unknown=9|agree=yes
IR-AOT-MIR|accepted=227|same-as-interpreter=227|emittable=58|of-ops=67
  |not-emittable=9|beyond-ast-denominator=29
```

`ALLOCS_EXPECT` 五行：`1|256`、`2|768`、`2|768`、`2|768`、`3|1792`，全部 `ok=yes`。**其中两行是猜错之后被读数纠正的**，注释里写明哪两行、为什么：空源是 `1|256`（源 `[]` 是空数组，除了头槽不占区域，而且一次 add 都没跑），搬移那条是 `3|1792`（集合拷到前沿，`list` 的目标数组落在旁边，池翻了两次）。

### 六、边界

1. **`kind != 0` 的集合静默忽略 add**：解释器的 `if (!s || s->kind != 0) return;` 对区间集就是「什么都不做」，本臂照抄（用 `mv_set_is_interval` 判别那个 −1 头槽）。
2. **跨容器共用的集合也能长大**：`mv_set_add` 收到的是值的**拷贝**，搬移只改池、不改调用者的寄存器；调用者的句柄仍然指向转发头，`mv_arr_live` 会解析它。这正是「别名不受搬移影响」的实现方式，也是那段注释写明的理由。
3. **推导式到此为止**：`OP_SET_ADD` 之后这条路上没有剩下的 opcode 了。`IR-MIR-NOT-YET` 在推导式程序上现在是空的——这条路的下一步不再是「再实现一个 opcode」，而是「让更多程序形态通过预扫描」。
4. **`OP_SET_ADD` 不推动 `list` 那一格记下的判据**：两者是独立的判据组，`list` 是读、`add` 是写。

## §10.144 `recv`：没有线程能填的队列，答案只能是 nil（aot-mir，A 类第三十三项）

### 一、来源与日期

判据 `1b4a1c4`（四条先红）、实现 `38a8c58`，2026-11-09。这是 `unknown` 从 9 降到 8 的那一格。

### 二、形状

`recv name [, timeout]` 编译成 `OP_RECV r1, r2, 0`：`r1` 是**结果**寄存器，`r2` 是超时寄存器（源码没有超时就是 **−1**），`r3` 不用（`src/compiler/compiler.c`，`STMT_RECV`）。

解释器的臂（`src/vm/vm.c`，`L_RECV`）在 `t->msg_lock` 下轮询 `t->msg_q`。**这两个对象只在启动线程的路径上创建**（同文件 `4532-4533`：`ImMutex *ml = im_mutex_new(); nt->msg_lock = ml;`）。本路线接受的程序只有一条线程，而带线程段的容器在更早处就被按 `why=threads` 拒掉——所以 `t->msg_lock` 是 NULL，整段轮询被跳过，`out`（初始化为 `VAL_NIL`）原样返回。

**超时寄存器甚至不被读取**：解释器在没有队列可等时也不等。实测 `recv m, 0.5` 立刻返回 nil，而不是睡半秒。

### 三、判据

| 用例 | 钉住什么 | 绿读数 |
|---|---|---|
| `mir_recv_is_nil` | 立刻返回 nil，后续语句照常 | `nil / after`，10 字节 |
| `mir_recv_with_a_timeout` | 带超时也不等待、也是 nil | `nil`，4 字节 |
| `mir_recv_inside_a_function` | 容器里接收到的值可被 `return` 出来 | `nil / done`，9 字节 |
| `mir_recv_result_is_a_value` | 结果是一个**真 nil 值**，不是“没写过的寄存器” | `true / false / nil`，15 字节 |

最后一条是这一格的关键：一个**什么都不写**的臂也能让前三条通过（寄存器里恰好是 nil 的情况很多），但 `say m == nil` 会答 true 而 `say m == 0` 会答 false——只有真的写入了 nil 才两条都对，`x = m` 之后再 `say x` 也一样。**「恰好空着」和「答案是 nil」在前三条上不可区分，在第四条上可区分**。

### 四、读数（本树 `38a8c58`）

```
IR-MIR-COVER|...|mir-only-cases=145|mir-only-same-as-interpreter=145
IR-MIR-VERDICT|PASS
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|agree=yes
IR-AOT-MIR|accepted=231|same-as-interpreter=231|emittable=59|of-ops=67
  |not-emittable=8|beyond-ast-denominator=30
```

四条 `ALLOCS` 全是 `0|0`——队列读不到东西，也就没有可分配的块。这一行存在是为了让「把收到的值装箱」或「为接收缓冲向池要一块」的实现被读数抓住。

`op_regs` 只给 `R1`：`r2` 是超时寄存器、在源码没有超时时是 −1，给它一位会让通用的寄存器边界循环拒掉一个合法的 −1（`OP_BIND` 的初始化寄存器是同样的处理）。

### 五、边界

1. **`send` 仍然拒译**：`send t1 5` 连解释器都过不了——它答「未定义的线程 t1」，因为线程名必须在 `thread` 段里定义，而线程段本身被拒。所以 `send` 与线程六件一起留在 `unknown` 里。
2. **这一格不声称「消息传递可用」**：它只声称「在这些程序上，`recv` 的答案与解释器一致」。真正的消息传递需要线程段与调度器，那条路在本路线是**具名边界**（`why=threads`），不是缺口。
3. **`recv` 不阻塞，所以判据里没有时间断言**：与 §10.138 的 `wait` 相反——`wait` 真的睡（因此要手工记录延迟），`recv` 真的不睡（因此「立刻返回」由程序在合理时间内完成来体现，不需要计时）。

## §10.145 这条路线的边界：把 `unknown=8` 变成一个必须被论证的集合（A 类第三十四项）

### 一、来源与日期

实现 `fe59dc1`（检查器 + 两条 CTest）、`1be19eb`（`recon.ctest-count` 同步），2026-11-09。这一节没有新 opcode，`handles` 仍是 `59|of=67`——它做的是**把余数从数字变成论证**。

### 二、问题

`IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8` 是一个**计数**。计数回答不了「这 8 个为什么在」。而两种缺席会印出同一个数，却要求完全相反的回应：

- 还没有写臂的 opcode，那是**活**；
- 这条路线**决定不安**的 opcode，那是**边界**。

更糟的是：明天往 `src/compiler/bytecode.h` 末尾追加一个 opcode 而没有人写臂，它会**静默地**加入余数——`unknown` 加一，而没有任何东西说出这是哪一种缺席。读到 `unknown=8` 的人分不出「还剩 8 件事」和「有 8 件已声明不做」，而这个区别正是他要问的全部。

### 三、办法：把边界做成一个有两条性质的人工物

`tools/aot_mir_surface.py` 要求余数是一个**闭合且可复读**的集合：

- **闭合**：每个未发射的 opcode 都必须在 `BOUNDARY` 表里被归类；反过来，如果某行归类的 opcode **后来有了臂**，那行就报**陈旧**——一份描述已经不存在的树的边界，比没有边界更坏，因为它会**错误地回答**读者的问题。
- **有据**：每行都必须点名一个树里还在的东西（文件 + 里面的一个片段），所以一个分类活不过它所指的那段代码。

它**不判断**一条边界高不高明，只判断它完不完整、能不能被重读。**这是让人可以反驳它的前提**。

三个类，每个都用一句话说清「这条路为什么不答它」：

| 类 | 含义 | 本树 |
|---|---|---|
| `another-thread` | 答案要求存在第二条线程；本路线按 `why=threads` 拒掉带线程段的容器，所以**构造上不可达** | 7 |
| `measurement` | 可观测量是「资源上限在第几条指令被触发」；生成的 C 的指令流不是解释器的，那个计数在这里没有对应物 | 1 |
| `retired` | 解释器不再派发它，没有东西可以一致 | 1 |

### 四、读数（本树 `1be19eb`）

```
IR-AOT-MIR-SURFACE|declared=67|emitted=59|unemitted=8|classified=9|classes=3
IR-AOT-MIR-BOUNDARY-CLASS|class=another-thread|n=7|meaning=unreachable by construction: this route accepts no thread section
IR-AOT-MIR-BOUNDARY-CLASS|class=measurement|n=1|meaning=the observable is a stop position, not a value
IR-AOT-MIR-BOUNDARY-CLASS|class=retired|n=1|meaning=the interpreter no longer dispatches it
IR-AOT-MIR-SURFACE-VERDICT|PASS
```

`classified=9` 而 `unemitted=8`，因为 `OP_POP_REG` 也有一行：它**不是成员**（enum 的注释在编号空洞里提到它），但读者会问它，所以那一行说的是「为什么这里没有可答的东西」。

`declared=67` 与 `IR-MIR-SUBSET` 的分母一致，这一点是**先错后对的**，见六.1。

### 五、反向验证：两个方向都注入过并还原

```
把一行改名（= 一个 opcode 无人归类）
  → PROBLEM|the emitter has no case for OP_DECLARE, and the boundary does not
              classify it                                   VERDICT|FAIL
把某行的证据片段改掉
  → PROBLEM|OP_DECLARE: evidence 'no-such-line-anywhere' is no longer in
              src/vm/vm.c                                   VERDICT|FAIL
还原
  → VERDICT|PASS
```

五个自检格（闭合 / 未分类 / 陈旧 / 证据失效 / 未声明类）**注册成自己的 CTest**：一个从没被证明会红的检查器，是没有人能引用的检查器。

`EXP_CTEST` 194 → 196 与注册**同一笔提交**里改：注册与它蕴含的那个数必须同进同出，否则别人的门禁会为一个两者都不点名的理由变红。

### 六、这一格自己踩的两个坑

1. **注释里的名字不是成员。** 第一版用正则扫名字，把 enum 注释里那句「29 是空洞：`OP_POP_REG` 曾在这里」的**单词**当成了成员，于是报 `declared=68|emitted=60`——而 `aot-mir subset` 与 case 标签 grep 都说 **59/67**。多出来的那个「成员」是注释里的一个词，多出来的那个「发射」是对它做的算术。现在先剥注释再数，并且注释里写明了这件事：只存在于文字里的 opcode 不可能有派发器，把它算进余数会放一条**永远赢不了的条目**进去，而余数正是这个检查器存在的意义。
2. **一个 0 的类不是类。** 初版还有个 `scheduler` 类，实际使用数为 0。一个没人用的类是一个读者读不出含义的类——删掉了，而不是留着当「以后可能用上」。

### 七、边界

1. **这一节不增加 `handles`**，也不该：`handles` 数的是 opcode 实现，而这一格是把**已经存在的余数**固定成可论证的形状。
2. **它不判断边界明智不明智**。「线程六件应该实现吗」这个问题的答案是「如果这条路线开始接受线程段」，而那是一个与 `handles` 无关的决定；检查器只保证「如果你不答它，你得说清是哪一种不答」。
3. **`docs/` 里的 34 节论证没有被搬过来**。表里每行的 `why` 是一句话摘要，而 `BOUNDARY` 的存在理由是 `docs/` **装不了**的东西——散文允许「等等」，检查器不允许。两者是互补的：`docs/` 讲为什么，这个表保证**集合是闭的**。
4. **`OP_DECLARE` 的 `measurement` 类是唯一还在「理论上也许能做」的**：它可达、无线程依赖。它留在这个类里，是因为可观测量（触限发生在第几条指令）与生成的 C 没有共同刻度——真要支持它，得先定义「生成的 C 里的指令计数」是什么，而那是一个新决定，不是一条臂。

## §10.146 生成程序发现的三个缺陷，以及一个能继续发现它们的工具（A 类第三十五项）

### 一、来源与日期

判据 `755dbfb`（八条排序停止）、实现 `2016565`、判据与实现 `a9b73b1`（算术守卫 + fuzzer），2026-11-09。这一格**不增加 `handles`**（仍是 `59|of=67`）：它让已有的 opcode **答对**，并造了一个能持续找这类错的工具。

### 二、为什么需要生成器

这条路线原先只有手写语料：86 条共享用例 + 一条条 MIR_ONLY 列表。手写语料**只找得到作者已经怀疑的东西**。而这一周已经有两次例外：

- `list` 把集合的整数半边**窄化到 32 位**（§10.142），是子智能体做差分探测时发现的；
- 本文的三个缺陷，是一个下午的生成程序发现的，**当时所有手写用例都绿**。

`tools/aot_mir_fuzz.py` 生成程序，两边跑（解释器 vs `aot-mir translate` + `cc`），**逐字节**比。它把发现分成三类，因为三类要三种回应：

| 类 | 含义 |
|---|---|
| `DIVERGE` | 两边都跑完、字节不同 —— **错答** |
| `THREW` | 一边非零退出、另一边没有 —— **少停了一次或多停了一次** |
| `UNSOUND` | 本路线拒了解释器能跑的 —— 不一定是 bug（拒绝是设计），但**是覆盖事实**，所以打印它自己的 `why=` |

### 三、三个缺陷

**① 不可排序的一对会 abort。** 解释器四个比较臂的第一件事都是 `if (!val_orderable(...)) vm_throw_kind("type_mismatch")`，而 `val_orderable`（`src/vm/vm.c:442-445` [obs: a9b73b1]）只认两种形状：**两个字符串**、或**两个数字**（bool 算数字）。其余全部**拒绝**，且拒绝是**可捕获的异常**。本路线在这些形状上 `abort()`（rc 134、无消息）—— 那是读者唯一无法回应的一种停。

实测六种形状（`"a" < 1`、字符串变量对数字、集合/数组/字典对数字、`nil > 1`）解释器全部印 `[exception] uncaught: type_mismatch`、stdout 空、exit 1；而 `true > 1` 答 `false` —— 这是**必要的对照**，一个「凡不是 int/float 就拒」的实现会在那里红。

**② 守卫有了、检查没有，于是「停」变成了「没停」。** 四个排序臂加了 `mv_orderable` 之后，在 `try` 里仍然答 `false`：

```
try { say ("hi" < c) } catch (e) { say "caught" }
```

本路线印 `false`，解释器印 `caught`。原因是 `mv_throw` 只**置标志**，真正跳到 catch 的是**指令之后那句检查**——而这四个臂没带它。判据 `mir_order_is_catchable` 与「它会停」是**两条**，因为「停得对但捕不到」和「根本不停」是两个不同的 bug。

**③ 算术的同一族。** `1 + nil`、`1 * min(7)`、`-nil` 也全是 abort。`val_as_double`（`src/vm/vm.c:323-334` [obs: a9b73b1]）认 int/float/bool，其余拒绝，所以六个算术 helper 全部先问 `mv_numeric`。

### 四、生成器自己的五个 bug（它们伪装成发现）

每一个都是**语言**的真实限制，都先量再绕开：

| 生成器写了 | 实际情况 |
|---|---|
| `min(a, b)` | `min` 只收**一个**参数，多参写法是语法错误 |
| `len(1, 2)` | 同样，`len` 只收一个 |
| `0.5 * (1, 2)` | 裸集合列表**在表达式里根本不解析**（`Error: expected 'expression', but got ','`），两种运行时都一样：它是**语句**形状 |
| `c = -1, 2, 3` | 集合字面量**不能含负数**：前瞻不接受前导 `-`，重试时看到的是逗号 |

一个把这些当发现的 fuzzer 什么都教不了，还会把它真正找到的东西埋掉。所以生成器现在：把集合绑到**名字**上、不让集合进表达式操作数、集合字面量只用非负数。这些限制写在这里，因为下一个把集合接进表达式的人会撞上它们。

### 五、读数（本树 `a9b73b1`）

门禁自己那条调用（`--count 20 --seed 7`，固定种子 ⇒ 读数可复现）：

```
IR-AOT-MIR-FUZZ|seed=7|count=20|ran=15|diverge=0|threw=0|unsound=0
  |refused-by-name=5
IR-AOT-MIR-FUZZ-VERDICT|PASS
```

`refused-by-name` **单独计数、不并入 UNSOUND**：按名拒绝是本路线按设计工作（`tools/aot_mir_surface.py` 为每一条论证），并进去会让「今天覆盖变少了」看起来像「找到了 bug」。

判据侧：`mir-only-cases=145→154`（新增九条：七条停、一条 bool 对照、一条可捕获性）。`IR-MIR-VERDICT|PASS`、`accepted=239`、`IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|agree=yes`。

fuzzer 的比较器有自己的四个自检格（相同 / 差一个字节 / 引擎横幅被滤掉 / 只有横幅），因为一个从不说「不同」的比较器在任何树上都是绿的。

### 六、这一格踩的三次同名坑

`mv_throw`、`mv_str`、`mv_numeric` **三个函数**都在使用点之后定义，而 `kMirValBlock` 是按源码顺序编译的。三次都表现为「生成的 C 编译失败 ⇒ 全部共享用例红在编译器上而不是红在答案上」。前两次用前置声明解决，第三次是把 `mv_numeric` 移到所有算术 helper 之前。**这条规矩因此写进文件里**：凡是 kMirValBlock 内部新增的、被更早的 helper 调用的函数，要么前置声明、要么放在最前。

### 七、边界

1. **fuzzer 不生成**：线程段（按名拒）、`wait`（答案是时间）、`record`（答案是宿主磁盘）、故意的未捕获 throw、集合作字典键（§10.128 已知两边一致于缺陷）。每一条都是决定，不是遗漏。
2. **它比的是 stdout 与退出码**，不比 stderr。stderr 的差异已经由 `STDERR_EXPECT` 那一族判据逐条钉过，而且两条路线从没承诺过 stderr 逐字节相同（§10.124 的注释）。
3. **`handles` 不动是对的**：这一格没有新 opcode。`handles` 数的是「67 个 opcode 答了几个」，不数「答对了几个」——而后者的证据在 `same-as-interpreter` 与 `mir-only-cases` 上。

## §10.147 `+` 的四条分支：字符串不再是「这一边没有答案」（aot-mir，A 类第二十七项）

### 一、来源与日期

判据 `9f0eec2`（十三条先红），实现 `745ad88`（2 文件 +125/−4），2026-11-09。起点是 fuzzer 扩面之后的手工差分：十个形状里九个两边一致，第十个（`say "a" + 1`）本路线 **rc=134**——它没答错，它**用 abort 代替回答**。

### 二、形状：解释器在 `+` 上分支四次，本路线原来只答了两次

解释器臂先看 fatal，再看集合，再看字符串，再看数值（`src/vm/vm.c` 里 `L_ADD` 那一段，3276–3326）：

| 序 | 条件 | 解释器的答案 | 本格之前本路线的答案 |
|---|---|---|---|
| 1 | 任一侧是数组或字典 | `Error: '+' is not defined for arrays/dicts …`（**fatal**，`exit(1)`，try 捕不到） | `type_mismatch` 抛出（可捕，rc=1） |
| 2 | 两侧都是集合 | 并集；只有一侧是集合 | `nil` | 抛 `type_mismatch` |
| 3 | 任一侧是字符串 | **两侧都过 `value_to_string`**，再拼接（缓冲 128） | 混合对 **abort**（rc=134） |
| 4 | 两侧 intlike | int64 + 溢出检查 ⇒ 溢出抛 `numeric_overflow` | 同 |

第 3 行那句 abort 的注释写着「MIXED PAIR: no answer on this route」——而解释器有答案，而且有四种：`"a"+1`=`a1`、`"a"+1.5`=`a1.5`、`"a"+true`=`atrue`、`"a"+nil`=`anil`（反向 `1+"a"`=`1a`，变量 `x = 5` 后 `"v="+x`=`v=5`）。

### 三、判据（十三条，`9f0eec2`，在 `1b2114d` 上全红）

字符串五条 + 链式一条（`x = 5; say "v=" + x + "!"`，走 `OP_CONCAT` 链）；集合三条（不交、相交、与标量）；区间一条；fatal 两条（数组、字典，用 `STDERR_EXPECT` 钉那句致命话）。红读数形如：

```
IR-MIR-EXEC-DIFF|case=mir_add_string_and_int|interp b'a1\n' rc=0|mir b'' rc=-6
IR-MIR-EXEC-DIFF|case=mir_add_sets_is_a_union|interp b'set(4)\n4\n' rc=0|mir b'' rc=1
IR-MIR-STDERR|case=mir_add_arrays_is_a_fatal_error|does not contain "Error: '+' is not defined for arrays/dicts"
```

### 四、实现：按解释器的顺序重写，而不是按本路线的旧形状打补丁

`mv_add` 整段重写，四条分支与解释器同序；第 3 条把非字符串那侧先 `mv_vts` 成 128 字节的字符串再 `mv_str_concat`（**与解释器同一个缓冲区大小**，因为超长结果在两边都必须截在同一个位置）。集合那一支新建 `mv_set_union_of`：池上两趟 pass（先 a 后 b），分段序照 `set_union`（i64 半边在前、items 次之、comps 最后），**区间集不贡献成员**——解释器的 kind-2 集合两个计数都是 0，所以两个区间集求并是空集（实测 `p = (0, 3); q = (2, 5); say p + q` 印 `set(0)`、`len` 0）。`mv_try_add` 退化成「只保留第 4 条」，非数值对一律交回 `mv_add`，于是 try 里的数组加法仍然致命（与解释器一致）。读数（本树 `745ad88`）：

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86
  |c-identical=0|c-diff=86|perturb-stdout-changes=5
  |mir-only-cases=167|mir-only-same-as-interpreter=167
IR-MIR-VERDICT|PASS|tree=745ad88|accepted=86|refused=0|differ=0
IR-AOT-MIR|accepted=253|refused=0|same-as-interpreter=253|of=29|emittable=59|of-ops=67
  |not-emittable=8|corpus=inline-cases:86|mir-only-cases:167|beyond-ast-denominator=30
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-OPERANDS|indexed=14|prescanned=15|checked=14|missing=(none)|dead=(none)|agree=yes
```

### 五、十一行分配，第一遍全猜错

`ALLOCS_EXPECT` 的十一行第一遍写的是 `0|0`——「加法是算术，算术不分配」。第一次跑，十一行全红：第 3 条要先造字符串再拼，`mv_str_concat` 会注册缓冲，所以每条字符串例是 `1|n`（bytes = 结果长度 + 1），链式是 `2|9`；两个集合例是 `2|768` 与 `3|1792`（两个集合 + 并集 + `list` 的目标数组 ⇒ 池翻两次）；区间并集 `2|768`；与标量求并 `1|256`（只建结果集合）。**十一行都是先量后写的第二遍**，注释里留着第一遍的错法，因为它正是这一节的存在理由。

### 六、边界

1. **`+` 只答这四种解释器形状**：数组/字典致命、集合并集（区间集并集空集）、字符串混合拼接、数值加法。这些形状之外的组合（例如 `true + [1]`）先撞第 1 条，与解释器的 fatal 同形。
2. **字符串缓冲仍是 128 字节**，超长结果两边截在同一个位置——这是**照抄**，不是本路线另设的限制。
3. **`mv_try_sub/mul/div/mod` 仍可能 abort**：本格只修了 `mv_try_add` 的入口；其余三个 wrapper 对非数值 tag 直接 `mv_as_nv` ⇒ 撞 `abort()`。这一条记成债，不在本格。
4. **`mv_arr_set` 的 `index_out_of_range`** 仍在（解释器对越界写静默忽略），与本格无关。

## §10.148 集合的 `-` 与 `*`：先看类型，再看数（aot-mir，A 类第二十八项）

### 一、来源与日期

2026-11-11。判据 `b5b7438`（九条，先红），实现 `dd886ba`。

这一格**不推进 handles**：`OP_SUB` 与 `OP_MUL` 早就有臂，缺的是臂里的第一条分支。所以 `handles` 仍是 59（`unknown=8`），推进的是 `accepted` 与 `same-as-interpreter`（253 → 262，见第四节）。

### 二、形状

解释器的 `L_SUB`（`src/vm/vm.c` 的 3445 行起）与 `L_MUL`（3472 行起）读的是**同一条顺序**：

1. **两边都是 set** ⇒ `set_diff` / `set_intersect`，结果 `VAL_SET`；
2. **只有一边是 set** ⇒ 那个辅助函数返回 −1，臂把 −1 写成 `VAL_NIL`；
3. 这之后才轮到数值：两侧 intlike ⇒ int64 + `__builtin_*_overflow`（溢出抛 `numeric_overflow`），否则 double。

本路线原来的 `mv_sub` / `mv_mul` **没有第 1、2 步**，直接进数值守卫 ⇒ 每一个集合形状都印 `type_mismatch`、rc 1——不是「没有答案」，是**答错**。

`set_diff` 只接受**两侧都是有限集**：任一 `kind != 0`、或任一 `compCount > 0`，直接返回 −1。本路线能造出的非有限集只有区间（kind 2），所以这条整句拒绝压缩成一个 `mv_set_is_interval` 测试。

`set_intersect` 是四趟：a 的 i64 半边查 b、a 的 items 查 b、b 的 i64 半边查 a、b 的 items 查 a，全部经 `vm_set_add` 去重 ⇒ 结果与 `b * a` 相同。它还有一段**组件半趟**（把区间转成组件、`comp_intersect` 双层循环），本路线没有组件槽，这一段没有抄。

### 三、九条判据

| 判据 | 解释器 | 本格之前 | 现在 |
|---|---|---|---|
| `mir_sub_sets_is_a_difference` | `set(1)` / `1` | `type_mismatch` rc 1 | same |
| `mir_sub_lists_the_leftovers` | `[2]` | rc 1 | same |
| `mir_sub_keeps_a_float_out` | `[2]` | rc 1 | same |
| `mir_mul_sets_is_an_intersection` | `set(2)` / `2` / `[2, 3]` | rc 1 | same |
| `mir_mul_floats` | `set(1)` / `set(1)` | rc 1 | same |
| `mir_sub_a_set_and_a_scalar_is_nil` | `nil` / `nil` | rc 1 | same |
| `mir_sub_an_interval_is_nil` | `nil` / `set(0)` / `0` | rc 1 | same |
| `mir_mul_with_an_interval_keeps_the_members` | `set(2)` / `2` | rc 1 | same |
| `mir_mod_a_set_still_stops`（**控制组**） | `type_mismatch` rc 1 | rc 1（同） | same |

最后一条是控制组：`/` 与 `%` 的解释器臂**没有集合分支**（`L_DIV` 3501 行起直接走数值路径），所以集合落在那两条上必须仍然抛 `type_mismatch`。它钉的是「本格没有顺手把 `/` 与 `%` 也接上集合」。

### 四、读数

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=176|mir-only-same-as-interpreter=176
IR-MIR-VERDICT|PASS|tree=b5b743875d459472a76a95ce0d3073b328212590|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR|accepted=262|refused=0|same-as-interpreter=262|of=29|emittable=59|of-ops=67|not-emittable=8|corpus=inline-cases:86|mir-only-cases:176|beyond-ast-denominator=30
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=dd886baa06aab12d21f83c6145c5ce1451113cdf|accepted=262|of=29|covered=29|missing=0|refused=0|same-as-interpreter=262|beyond-ast-denominator=30
```

`IR-MIR-ALLOCS` 的九行（`mir_mod_a_set_still_stops` 是 `reported=no|why=rc=1`，与其余 rc=1 的用例同形）：`(2,768)`、`(2,768)`、`(2,768)`、`(3,1792)`、`(2,768)`、`(1,256)`、`(2,768)`、`(2,768)`。

**三行是猜的，三行全错，第一次绿跑把它们改掉了**：`mir_sub_lists_the_leftovers` 猜 `3|1024` 实测 `2|768`、`mir_mul_sets_is_an_intersection` 猜 `2|1024` 实测 `3|1792`、`mir_sub_a_set_and_a_scalar_is_nil` 猜 `1|512` 实测 `1|256`。表上方的注释写着「这些数在第一次绿跑后才定」——它就是在第一次绿跑后定的。

### 五、写这条之前量过的三件事

1. **空集合打印 `set(0)`、`list` 给 `[]`**（不是 nil）：`a = 1, 2; b = 2, 1; say a - b` 印 `set(0)`，`say list(a - b)` 印 `[]`。这一步决定了「差集为空」怎么写，也决定了它和**组件集合**的答案在 `list` 上分得开。
2. **两个区间相交仍印 `set(0)`、len 0，即使它们真的重叠**：`p = (0, 5); q = (2, 3)` 与 `p = (0, 3); q = (2, 5)` 印的是同一行 —— 打印只看 `iCount + count`，组件不计入。本路线因此把「区间 ∩ 区间」写成空有限集，打印与长度都对。
3. **i64 半边是窄着读的**：`a = 5000000000, 1; b = 1, 2; say list(a - b)` 解释器印 `[705032704]`（5000000000 截成 32 位）；`a = 5000000000, 1; b = 5000000000, 2; say a * b` 印 `set(0)`（两侧探针都先窄化，与对侧存的宽值不相等）。本路线的两个新函数照抄这个 `(long long)(int)` 转换。

### 六、边界

1. **区间 ∩ 区间的 `list` 与解释器不同**：解释器造的是组件集合，`list(p * q)` 给 `nil`（`vm_set_to_array` 拒绝组件集）；本路线给空有限集，`list` 是 `[]`。打印与 `len` 一致，`list` 不一致——这是本格唯一的一处已知分歧，选择它是因为没差的那两项更常被看见，且组件槽不在本路线的表示里。
2. **组件集合不算模型的一部分**：本路线没有 `compCount` 槽，因此任何依赖组件的形状（组件并、组件交、组件枚举）都不在本格；`p + q` 的「空集」答案来自 §10.147 的同一事实（区间集的 `iCount`/`count` 都是 0）。
3. **`/` 与 `%` 不动**：控制组那一条钉住它们仍然抛 `type_mismatch`，与解释器一致（解释器的 `/`、`%` 臂没有集合分支）。
4. **宽整数窄化是照抄，不是修复**：本格不修解释器的 32 位截断，只让它在本路线里同样发生。
5. **`mv_try_sub` / `mv_try_mul` 仍然可能 abort**：本格只改 `mv_sub` / `mv_mul` 的入口；带 `try` 的程序走 wrapper，集合在那里仍会落到 `mv_as_nv` ⇒ `abort()`。记成债，与 §10.147 的边界第 3 条同一笔。

## §10.149 算术包装器：先问是不是数字，再转换（aot-mir，A 类第二十八项）

### 一、来源与日期

- 判据：`2133490`（`test: eight criteria for arithmetic on a non-number, inside a program with a try`），2026-11-11。
- 实现：`810f849`（`aot-mir: the arithmetic wrappers ask whether it is a number before converting it`）。
- **本格不推进 handles**（`OP_SUB`/`OP_MUL`/`OP_DIV`/`OP_MOD`/`OP_NEG` 早就有臂，`handles` 仍是 `59|of=67|unknown=8`）：它推进的是 `accepted 262 → 270` 与 `mir-only-cases 176 → 184`，也就是**同一批 opcode 不再答崩**。

### 二、形状

带 `try` 的程序里，发射器把 `OP_ADD/SUB/MUL/DIV/MOD/NEG` 换成 `mv_try_*` 包装器（原因见 §10.137 的第二节：停止要能被 catch 住）。五个包装器的开头原本是：

`NV x = mv_as_nv(a), y = mv_as_nv(b);`

对**数字以外**的东西 `mv_as_nv` 是 `abort()`。于是：

| 表达式 | 解释器 | 本格之前（带 `try` 的程序） | 本格之后 |
|---|---|---|---|
| `"a" - 1` | 可捕获的 `type_mismatch` | `rc=134`、stdout 空 | `type_mismatch` |
| `"a" * 2` / `"a" / 2` / `-"a"` | 同上 | `rc=134` | 同上 |
| `[1] - 1` | 可捕获的 `type_mismatch` | `rc=134` | `type_mismatch` |
| `1, 2 - 5` | `nil` | `rc=134` | `nil` |
| `1, 2, 3 - 2, 3` | `set(1)` | `rc=134` | `set(1)` |

修法是让每个包装器**先问、后转**：`if (!mv_numeric(a) || !mv_numeric(b)) return mv_sub(a, b);` —— 把非数字对交还给被包装的函数（它的守卫会抛 `type_mismatch`，或像 §10.148 那样真的给出答案），包装器自己只留下「真正会停」的谓词（整数 `/ 0`、`% 0`、int64 溢出）。这与 §10.147 给 `mv_try_add` 定下的规则是同一条：**第 1 到 3 条属于 `mv_add`，包装器只为第 4 条存在**。

### 三、判据

| 判据 | 解释器（绿） | 本格之前（红） |
|---|---|---|
| `mir_try_arith_sub_on_a_string` | `type_mismatch\nafter` rc0 | `b''` rc=-6 |
| `mir_try_arith_mul_on_a_string` | `type_mismatch\nafter` rc0 | `b''` rc=-6 |
| `mir_try_arith_div_on_a_string` | `type_mismatch\nafter` rc0 | `b''` rc=-6 |
| `mir_try_arith_neg_on_a_string` | `type_mismatch\nafter` rc0 | `b''` rc=-6 |
| `mir_try_arith_on_a_set_and_a_scalar` | `nil\nafter` rc0 | `b''` rc=-6 |
| `mir_try_arith_on_two_sets` | `set(1)\nafter` rc0 | `b''` rc=-6 |
| `mir_arith_type_error_outside_a_try` | `t` rc1 + 未捕获 `type_mismatch` | `b''` rc=-6 |
| `mir_try_div_zero_is_still_caught`（**控制组**） | `division_by_zero\nafter` rc0 | 已经是绿的 |

控制组的作用写进注释：一个「把所有东西都交给 `mv_div`」的补丁会让前七条全绿、把它弄红。

### 四、读数

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-diff=86|mir-only-cases=184|mir-only-same-as-interpreter=184
IR-MIR-VERDICT|PASS|tree=21334907323bb8fe5a3014221d55dc854c0733b0|accepted=86|refused=0|differ=0
IR-AOT-MIR|accepted=270|refused=0|same-as-interpreter=270|emittable=59|of-ops=67|not-emittable=8
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=810f849|accepted=270|of=29|covered=29|missing=0|refused=0|same-as-interpreter=270
```

`ALLOCS` 两行是量出来的：字符串四例 `0|0`（不分配），`a = 1, 2` 那例 `1|256`，两个集合 `2|768`。第一遍表里没有这两行、按默认 `0|0` 判红，读数就是这样把「猜」纠成「量」的。

### 五、写这条之前量过的事

1. **只有带 `try` 的程序会炸**：同一句 `say "a" - 1` 在没有 `try` 的文件里，两边都是 `[exception] uncaught: type_mismatch` + rc1（`mv_sub` 自己的守卫先跑）。所以判据程序必须都带一个 `try`，否则量不到这个洞。
2. **集合对也被读在守卫之前**：`a = 1, 2 - 5` 在带 `try` 的程序里 `rc=134`，尽管 §10.148 刚让 `mv_sub` 能回答这个形状 —— 包装器是在 `mv_sub` 之前读的，顺序就是问题本身。
3. **控制组是真的**：`try { say 1 / 0 }` 两边都印 `division_by_zero`，所以包装器里必须留下自己的谓词，不能整体换成委托。

### 六、边界

1. 这只改了 `mv_try_*` 五个包装器；**不带 `try` 的程序生成的 C 文本一字未动**（同一棵树里 86 条语料仍 `c-diff=86`、`c-identical=0`）。
2. `mv_as_nv` 本身仍然是「非数字即 abort」——本格没有把它变宽，只是**不再在守卫之前调用它**。带 `try` 的程序里若还有别的路径提前转数值，仍会 abort（`mv_arr_set` 的越界写、`mv_vts` 之外的打印路径都不属于本格）。
3. 判据只覆盖 `-`、`*`、`/`、`%`、一元 `-` 与 `+`；比较类（`<`、`==`）对混合类型的处理是 §10.13x 系列的既有读数，本格没碰。
4. `mv_try_mod` 的谓词用的是 `nv_asi`（`%` 的解释器语义），与 `/` 的「非浮点且为 0」不同 —— 这是照抄，不是统一。

## §10.150 负下标的数组写：一条 throw，不是一次 exit（aot-mir，A 类第三十项）

### 一、来源与日期

判据 `2f27c0a`（2026-11-11，五条，其中两条在基线 `cc73482` 上红）；实现 `32ed584`。这一格**不推进 handles**（`OP_INDEX_SET` 早就有臂，`handles=59|of=67|unknown=8` 不动），推进的是 `mir-only-cases` 与「同一棵树上答案相同」这条指标。

### 二、形状

解释器的数组写（`src/vm/vm.c:3717-3733`）[obs: 32ed584]：

1. 容器不是 `VAL_ARRAY` 就直接返回 —— 字符串、字典、nil 上的写是**静默忽略**；
2. 下标 `i = (idxv->type == VAL_INT) ? idxv->ival : (int)val_as_double(vm, idxv)`；
3. `i < 0` ⇒ `vm_throw_kind(vm, "index_out_of_range")`（`src/vm/vm.c:3721-3724`）[obs: 32ed584] —— **一次 throw**，可被 `try/catch` 接住；
4. `i >= count` ⇒ 不是错误，是**扩展**：中间槽填 nil（`vm.c:3721-3724` 的 fill 段）。

本路线这三件事里第 1、2 早已一致；第 3 件之前是「同一句话、不同机制」：`mv_arr_set` 走 `fprintf(stderr, "[exception] uncaught: index_out_of_range")` + `exit(1)`，于是

- 没有 handler 的程序：两边一样（同一句话、都 rc 1）——旧判据 `mir_index_set_negative` 钉的正是这一半；
- 有 handler 的程序：解释器继续跑，本路线的进程直接消失。

### 三、判据

| 用例 | 形状 | 基线 `cc73482` |
|---|---|---|
| `mir_index_set_negative_is_caught` | `try { a[-1] = 9 } catch(e) { say e }` 后继续 | 红：`b'' rc=1` vs `index_out_of_range\nafter\n[1, 2]\n` rc=0 |
| `mir_index_set_negative_in_a_nested_array` | 嵌套数组里负下标写在 handler 里 | 红：`b'' rc=1` vs `index_out_of_range\n[1, 2]\n` rc=0 |
| `mir_index_set_negative_uncaught` | 同样的写，**没有** handler | 绿（控制组：故意钉住「未捕获仍 rc 1、仍印那句话」） |
| `mir_index_read_negative_is_nil` | `say a[-1]` | 绿（控制组：读路径的负下标是 nil，不是 throw） |
| `mir_dict_negative_key_is_nil` | `d[-1] = 9` 后 `say d[-1]` | 绿（控制组：字典的负键是键，不是下标） |

红读数原文（`IR-MIR-PROBLEM|mir_index_set_negative_is_caught: the MIR route printed b'' rc=1, the interpreter b'index_out_of_range\nafter\n[1, 2]\n' rc=0`、`IR-MIR-PROBLEM|mir_index_set_negative_in_a_nested_array: … b'index_out_of_range\n[1, 2]\n' rc=0`）。

### 四、实现与读数

`mv_arr_set` 的 `i < 0` 分支改成

```
        mv_throw(mv_str("index_out_of_range"));
        return;
```

机制换了一条，话语、未捕获形状的 rc、以及读路径全都没动。`OP_INDEX_SET` 的发射点早就有 `emit_try_check`（§10.137 加的），所以 throw 之后的那一跳自动成立。

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-diff=86|perturb-stdout-changes=5|mir-only-cases=189|mir-only-same-as-interpreter=189
IR-MIR-VERDICT|PASS|tree=32ed5848f21a7980072001fe547f2ae39bbfe1be|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
```

两条控制组与三条新判据全绿；`mir-only-cases` 184 → 189。

### 五、写这条之前量过的事

- **负下标的写 vs 读不是一回事**：读走 `vm_array_get` 的 `i < 0` 守卫（返回 nil），没有 throw；写才 throw。所以判据里专门留了一条读的控制组。
- **字典的负键是键**：`d[-1] = 9` 在两边都答 `nil`（`d[-1]` 查回来是 nil）—— 这一半是两边一致的「奇怪」，不在本格修。
- **扩展语义运行期已经一致**：`mv_arr_set` 的 `i >= count` 分支本来就会填 nil 长到 `i`，与解释器的 fill 段同形；真正挡路的是 **prescan 的常量子标检查**（字面量 2 元素 + `a[5]` 被判成越界并拒译）—— 见下节。

### 六、边界

1. 本格只把**机制**从 exit 改成 throw；`i >= count` 的扩展没有新判据（运行期早已一致），但**常量子标的翻译期拒绝仍在**：程序 `a = [1, 2]` 后写 `a[5] = 9` 会被 `why=index-out-of-range-constant` 拒掉，而解释器会扩展它。这是本路线的一处**过度拒绝**，需要单独一格处理（它不是「没有答案」，而是「答案需要运行期的长度」，而 present 的常数检查用的是字面量的长度）。
2. `mv_arr_set` 对非数组容器（字符串/字典/nil）的写仍是「静默忽略」，这条与解释器一致，没有判据（旧用例 `mir_index_set_edges` 覆盖了边界下标的一半）。
3. 本格不动 `mv_arr_get` 的负下标语义（nil），也不动字典键比较的任何规则。

## §10.151 常量子标的「生成前拒绝」退役：一个有答案的程序不该被拒译（aot-mir，A 类第二十八项）

### 一、来源与日期

判据 `5b9dcb0`，实现 `4f6c8e8`，2026-11-11。这一格**不推进 handles**（`handles=59|of=67|unknown=8` 不动），推进的是 `accepted 275→278`：三个语料程序此前被按名拒译，现在被执行并与解释器逐字节相同。

### 二、退役的是什么

`src/compilation/aot_mir.c` 曾有一个 `prescan_constant_subscripts` 趟：它按容器把「某寄存器里是长度为 N 的字面量数组」「某寄存器里是常量 k」这两件事沿直线指令传播（join 点清零、全局经写/读传播、调用清全局表），然后在 `OP_INDEX_GET` 上发现 `k >= N` 就**在发射之前**拒译，理由名 `index-out-of-range-constant`。全文件只此一处「生成前拒绝」，它自己的长注释也承认：**解释器会回答它（nil）**。

它退役的原因是两个实测，不是一个偏好：

1. **它有答案，而且答案不是 nil**。`a = [1, 2]` / `a[5] = 9` / `say a[5]` —— 解释器印 `9`（写会让数组**增长**，中间槽填 nil），而这条拒绝把它判成「不可能的下标」。一个答案非 nil 的程序被拒译，是比「答错」更贵的一类：它连比较的机会都没有。
2. **它记住的长度会过期**。增长是写路径干的，而那段知识不在写之后失效，所以 `a[5] = 9` 之后同一常量下标的**读**仍然落在过期长度上。这是策略层的硬伤：一份不可靠的知识用来拒绝，拒绝就会错。

### 三、判据

`mir_container_const_oob` 从 `MIR_ONLY_REFUSE` 退役（它要求「必须拒译」，而拒译正是被退役的东西），换成 `MIR_ONLY` 里的三条：

| 判据 | 程序 | 解释器 | 退役前本路线 |
|---|---|---|---|
| `mir_read_past_a_literal_is_nil` | `a = [1, 2, 3]` / `say a[3]` / `say "after"` | `nil`,`after` | 拒译（`insn=10 constant subscript 3 is outside the 3 element(s)`） |
| `mir_read_a_value_written_past_the_literal` | `a = [1, 2]` / `a[5] = 9` / `say a[5]` / `say len(a)` | `9`,`6` | 拒译（`insn=12 … 5 is outside the 2 element(s)`） |
| `mir_read_far_past_a_single_element_literal` | `a = [1]` / `say a[40]` | `nil`,`after` | 拒译（`insn=6 … 40 is outside the 1 element(s)`） |

三条都是 `exec=same`（`stdout-hex` 分别是 `6e696c0a61667465720a`、`390a360a`、`6e696c0a61667465720a`），`ALLOCS` 各 `1|256`（字面量自己的块，读不分配）——这两行是**绿跑之后量的**，第一遍按 `0|0` 写，被 `IR-MIR-ALLOCS` 判错。

### 四、读数

```
IR-MIR-COVER|...|mir-only-cases=192|mir-only-same-as-interpreter=192
IR-MIR-VERDICT|PASS|tree=ba568e9...|accepted=86|refused=0|differ=0
IR-AOT-MIR|accepted=278|refused=0|same-as-interpreter=278|of=29|emittable=59|of-ops=67|not-emittable=8|beyond-ast-denominator=30
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|table=59|emitter=59|reported=59|agree=yes
```

`accepted 275→278` 是这一格唯一动过的数：三个语料程序从「按名拒译」变成「执行且逐字节相同」。

### 五、写这条之前量过的事

1. 解释器对越界**读**答 `nil`（`src/vm/vm.c` 的数组读臂），本路线的 `mv_arr_get` 早就是同一个答案 —— 也就是说这条拒绝所拒绝的程序，运行期本来就能答对。
2. 越界**写**会让数组增长（`src/vm/vm.c` 的写路径按 `ni` 扩容并填 nil），所以「常量下标越界」在写侧根本没有拒绝的理由；在写之后再读同一个下标，答案就是写进去的值。
3. 负下标：读答 nil、写抛 `index_out_of_range`（§10.150 已把它改成可捕获的 throw）——两者都不在这条静态知识的范围里，退役没有碰到它们。
4. `KS_GLOBALS`（4096）这个界与 `0 < id < 4096` 的检查是这段知识的实现细节；删掉之后 `grep` 不再有 `ks_*` 残留，只剩两处注释点名「它已退役」。

### 六、边界

1. **删掉的是「生成前拒绝」，不是「运行期行为」**。越界读仍是 nil、越界写仍是增长、负下标写仍是可捕获的 throw；本格只删了「发射前把程序判死」的那一步 (168 行)。
2. **`MIR_ONLY_REFUSE` 仍然存在**，它现在装的是能力边界（线程、测不出的限制等），不再是这条。区别写在那里：能力边界可以拒，可回答的形状不能拒。
3. **本路线的「拒绝前已知」少了唯一的实例**。往后若有新检查想按同样理由拒译，得先回答本格这个问题：「解释器会答什么？如果它会答，我们就得答」。

## §10.152 增长的那些形状，从此有名字（aot-mir，A 类第二十九项）

### 一、来源与日期

判据与工具改动同一笔 `tools/aot_mir.test.py` + `tools/aot_mir_fuzz.py` + `CMakeLists.txt` + `tools/gate.sh` + `docs/BOARD.md`，2026-11-11。这一格**不推进 handles，也不推进 accepted 的语料**（都在 §10.151 之后没变：`handles=59|of=67|unknown=8`、`accepted=278`）；它推进的是**可复跑的宽度**：`mir-only-cases 192→198`，以及一族的生成器形状从「手测过」变成「有种子、有 CTest」。

§10.151 退役了那条「常量下标越界就拒译」的生成前拒绝。退役之后有一句话不能只停在结论上：**「我们不再拒绝它」不等于「我们答得和解释器一样」**。这一格就是补那句话的两半——先是六条手写判据，再是一个生成器族和它的种子。

### 二、六条判据钉住的是什么

| 判据 | 形状 | 解释器 |
|---|---|---|
| `mir_grow_an_array_past_its_literal` | `a = [1, 2]` / `a[5] = 9` / `say a` / `len` / `a[5]` / `a[3]` | `[1, 2, nil, nil, nil, 9]`,`6`,`9`,`nil` |
| `mir_grow_through_an_alias` | `a = [1]` / `b = a` / `a[3] = 7` / `say b` / `b[3]` / `len(b)` | `[1, nil, nil, 7]`,`7`,`4` |
| `mir_grow_inside_a_function` | 函数里 `a[4] = 7` 再返回 | 两边都是五槽，返回值与原值相同 |
| `mir_grow_a_nested_element` | `n = [[1]]` / `n[3] = 5` / `say n` | `[[1], nil, nil, 5]`,`5` |
| `mir_grow_then_compare` | 增长后比长度/相等 | `false`,`false` |
| `mir_grow_with_an_explicit_nil` | `a[4] = nil`（显式 nil 也要增长） | `[1, 2, nil, nil, nil]`,`5` |

顺手在同一台机器上另跑了八条**没进判据**的形状，全部 `SAME`：增长后比较（`a == b` / `len` 相等）、显式 nil 增长、嵌套别名增长（`m = n[0]; m[2] = 5`）、单步追加（`a[1] = 2`）、字符串越界写（静默忽略）、字典里的数组增长、函数内增长后别名可见、变量下标增长。它们不进口径，是因为每条都要一段固定程序，而六条已经把这族的三个承重点（增长、别名、显式 nil）钉住了。

### 三、生成器与种子

`tools/aot_mir_fuzz.py` 加了一个 `gw`/`gx` 族：`gw = [...]` → `gw[k] = v`（k 可能远超字面量长度）→ `say gw` / `len` / `say gw[k]` → 别名 `gx = gw` 读回 → 再读一个 k+0..2 的下标。登记成 `aot_mir_fuzz_regression_seed13`（`--count 20 --seed 13`），`EXP_CTEST 199→200`，`docs/BOARD.md` 的 `recon.ctest-count` 同步。

读数：`IR-AOT-MIR-FUZZ|seed=13|count=20|ran=18|diverge=0|threw=0|unsound=0|refused-by-name=2`、`IR-AOT-MIR-FUZZ-VERDICT|PASS`；`ctest -N` ⇒ `Total Tests: 200`；`check_reconciliation.py` ⇒ `VERDICT|PASS|measured=5`。

### 四、为什么这一族以前生成不出来

不是忘了写，是**写不出来**：`gw[5] = v` 在 `[a, b]` 这种两元素字面量上会被 `prescan_constant_subscripts` 在发射前判死（§10.151 之前），生成器即使产出这样的程序也只会拿到 `why=index-out-of-range-constant`，于是「这个形状我们答得对不对」这件事永远不进入读数。这条与 §10.146 的教训是同一个：**生成器对某个形状的沉默，与该形状正确，从内部看无法区分**。区别是这次沉默的原因是一条策略，而不是作者的疏忽。

### 五、边界

1. 这一格**没有新行为**：没有一行运行时或发射器的改动，交付物是判据、生成器分支与登记。它是 §10.151 的验收面，不是新能力。
2. 增长后的**搬移**语义仍靠 §10.148 的 `mv_arr_live` 转发头；这一格只是把「搬移之后读回」也纳入了可复跑的形状（`mir_grow_through_an_alias` 的 `b` 读的就是搬移后的区域）。
3. `refused-by-name=2` 在那 18 例里仍是 `why=threads` 一类的能力边界，与本族无关。

## §10.153 `min()`/`max()`：集合归约，而不是在集合上停住（aot-mir，A 类第三十项）

### 一、来源与日期

- 判据：`10b1a46`（七条先红）与 `11c6750`（第八条：函数操作数）。
- 实现：`521be6e`。
- 日期：2026-11-12。基线 `fe72f9f`。
- 本格不推进 `handles`（`OP_MIN`/`OP_MAX` 早有臂，59|of=67|unknown=8 不动）；推进的是**同一棵树上的答案**：`accepted 275→292`、`mir-only-cases 189→206`。

### 二、形状：`min` 是集合归约，不是二元函数

- 解释器把 `R[ins.r2]` 与 `R[ins.r1]` 交给 `set_minmax`，而它的**第一句**就是 `dst->type = VAL_NIL`：只有 `src->type == VAL_SET` 才继续往下折叠。所以 `min(1, 2)` 答 `nil`（没有二元 `min`），`x = 1, 2` 后 `min(x)` 答 `1`。
- 翻译期：`min(1, 2)` 编译成区间集（`OP_SET_INTERVAL`），`min(x)` 是 `OP_MIN` 带一个寄存器操作数。
- 本路线的旧臂只认标量 tag：标量答 `nil`（对），**集合 `abort()`**（错——解释器有答案）。旧臂的注释用「集合到不了这里」论证这件事，而那个前提被 `OP_NEW_SET`（§10.125）与 `OP_SET_INTERVAL`（§10.140）退役了。

### 三、判据：七条红读数 + 一条函数操作数

```
IR-MIR-PROBLEM|mir_set_min_max_ints: the MIR route printed b'' rc=-6, the interpreter b'1\n3\n' rc=0
IR-MIR-PROBLEM|mir_set_min_max_floats: … b'1.5\n2.5\n' rc=0
IR-MIR-PROBLEM|mir_set_min_max_strings: … b'a\nb\n' rc=0
IR-MIR-PROBLEM|mir_set_min_max_mixed_is_nil: … b'nil\nnil\n' rc=0
IR-MIR-PROBLEM|mir_set_min_max_of_an_interval: … b'nil\n' rc=0
IR-MIR-PROBLEM|mir_set_min_max_narrows_i64: … b'1\n705032704\n' rc=0
IR-MIR-PROBLEM|mir_set_min_max_mixed_numbers: … b'1\n2.5\n' rc=0
```

第八条（`mir_set_min_max_of_a_function`）钉的是旧臂的另一半：`f = () -> 42` 后 `min(f)` 在解释器是 `nil`，而旧臂对 `MV_FUN` 也是 `abort()`（它的注释把函数列成「本路线没有答案」）。现在两者都答 `nil`。

### 四、读数

```
IR-MIR-COVER|…|mir-only-cases=206|mir-only-same-as-interpreter=206
IR-MIR-VERDICT|PASS|tree=521be6e…
IR-AOT-MIR|accepted=292|same-as-interpreter=292|emittable=59|of-ops=67|not-emittable=8|beyond-ast-denominator=30
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|agree=yes
```

### 五、写这条之前先量过的事

1. **窄化规则**：`set_minmax` 把 i64 半边按 `(int)` 读，所以 `x = 5000000000, 1` 的 `max` 是 `705032704`。照抄这个缺陷是本格唯一诚实的做法——用完整 int64 比较会答 `5000000000`，正是我们要复现的那个缺陷的反面。
2. **字符串与数字混在一起是 `nil`**（解释器 `have = -1`），不是「按类型排序」：`x = 1, "a"` 的 `min`/`max` 都是 `nil`。
3. **空集合**在解释器里 `have` 停在 0 ⇒ `nil`；本路线的折叠同样以 `nil` 起手、以 `have` 收尾。
4. **区间没有离散极值**：`p = (0, 3)` 的 `min` 是 `nil`（解释器对 `nameIdx == 24` 直接返回），本路线对 kind 2 也答 `nil`。

### 六、边界（每条都是决定，不是遗漏）

- **命名内建集**（`Z`、`Z+`、…）仍按名拒（preset 全局），所以 kind 1 在本路线不可达；若将来可达，规则是 `set_minmax` 的 kind-1 答案（N 的最小 0、Z+ 的最小 1、Z- 的最大 −1，其余 `nil`）。
- **除 `R` 以外的整数区间**不可达（本路线只构建 `R` 区间），而 `R` 没有离散极值。
- **NaN 元素**只能从 `OP_CALL_BUILTIN` 进来，仍在子集之外。
- **零分配说的是这条臂**：折叠只回传它找到的元素或 `nil`，从不构造集合；判据里的 `(1, 256)` 是程序自己建的那个集合字面量。第一遍表把七行全填成 `(0, 0)`，第一次运行把它们全部推翻——照例「先量后写」。

## §10.154 生成器的两个盲区：集合运算符与到达 `+` 的字符串（tools，A 类外一项）

### 一、来源与日期

- 提交：`695258f`（生成器 + CMake 注册 + 计数同步同笔）。
- 日期：2026-11-12。基线 `120d336`。
- 本格不动 `src/`：推进的是**测量工具的覆盖面**，以及由此得到的「补上盲区之后仍然干净」这条证据。

### 二、形状：沉默的是生成器，不是路线

`tools/aot_mir_fuzz.py` 里 `SETOPS = ["+", "-", "<", ">", "==", "!=", "in"]` 从写下那天起就**没有被任何分支读过**；同时 `value_expr` 没有任何叶子返回裸字符串，所以「字符串挨着 `+`」这一族（§10.147 正是为它写的）在这台生成器里**不可表达**。

两处都是生成器的沉默，而**沉默与正确从内部无法区分**——所以它们不会被任何一次绿色运行暴露，只能靠人把生成器的能力对照路线的臂列表读出来（§10.146 的教训，本格是同一课的第二次）。

### 三、证据：从 0/2500 到 19+42/80

| 量 | 改动前 | 改动后 |
|---|---|---|
| 生成程序总数 | 2500（100 种子 × 25） | 80（4 个种子族 × 20） |
| 含集合运算符的程序 | **0** | **19** |
| 含「字符串 + 标量」的程序 | **0** | **42** |

生成的形状样例：`say sw - sv`、`say sw * sv`、`say sw == sv`、`say 5 in sw`、`say sp + 1`、`say sp + "hi"`、`say 1 + sp`（`sw`/`sv` 是集合、`sp` 是字符串，名字与语句形状都写进生成器注释）。

### 四、读数

四个种子族（含新注册的 `aot_mir_fuzz_regression_seed17`）：

```
IR-AOT-MIR-FUZZ|seed=7|count=20|ran=19|diverge=0|threw=0|unsound=0|refused-by-name=1
IR-AOT-MIR-FUZZ|seed=11|count=20|ran=14|diverge=0|threw=0|unsound=0|refused-by-name=6
IR-AOT-MIR-FUZZ|seed=13|count=20|ran=13|diverge=0|threw=0|unsound=0|refused-by-name=7
IR-AOT-MIR-FUZZ|seed=17|count=20|ran=17|diverge=0|threw=0|unsound=0|refused-by-name=3
IR-AOT-MIR-FUZZ-VERDICT|PASS
```

另跑了一次宽带手工扫描（**加宽后的**生成器，12 种子 × 30 例）：`ran=299|refused-by-name=61|diverge=0|threw=0|unsound=0`，十二个 `VERDICT|PASS`。计数同步：`EXP_CTEST 200 → 201`、`docs/BOARD.md` 的 `recon.ctest-count` 随同，`check_reconciliation.py` 报 `VERDICT|PASS|measured=5`。

### 五、写这条之前先量过的事

1. **改动前的 0 是真读数**，不是回忆：冻结生成器（md5 记录在案）跑的 2500 例里，`grep` 集合运算符与字符串 `+` 都是 0 命中；改成「加宽版」后同一测量的计数立刻变成 19 与 42。
2. **加宽后仍然零分歧**：四个注册族 + 12 种子宽带扫描都干净 ⇒ §10.147/§10.148/§10.153 修好的那三族在新形状下也站得住。零分歧**不是**「路线完美」，只是「在说得出的形状上一致」。
3. **计数必须同笔**：新增一个 CTest 而不改 `EXP_CTEST`/`recon.ctest-count`，门禁会在 reconciliation 阶段红并点名两个数打架（§10.145 的老账）。

### 六、边界（每条都是决定，不是遗漏）

- **仍然不生成**：线程段（按名拒）、时间或磁盘答案（`wait`/`record`）、故意的未捕获 `throw`、以及「集合作字典键」（§10.128 已记成「与缺陷达成一致」，每次重find 一遍没有新信息）。
- **生成的是语句而不是表达式**：裸集合字面量在两条路线里都不是合法表达式（实测 `Error: expected 'expression', but got ','`），所以集合族写成 `sw = <集合字面量>` 再 `say sw OP sv`，而不是把集合塞进表达式里。
- **本格不产生新判据**：生成器改动的「判据」是它注册的种子族与自检，覆盖面由每次运行的 `diverge`/`refused-by-name` 计数表达；若要它变成有牙的断言，得先有人写下「哪一族必须出现在报告里」——那是另一格。

## §10.155 交接文档的行号换成引文，并且有检查器看着它们（docs，A 类外一项）

### 一、来源与日期

- 内容提交 `dc59d7f`（把交接文档里的行号引用换成**内容锚点**）、`777db4c`（新检查器与两枚 CTest），2026-11-11，基树 `f477f39`，合并进主线的那一笔是 `8c20c9a`。
- 盘点是上一个手次的只读清扫交出来的：那份文档里 **45 处**引用里 14 处**当时是对的**、30 处**已经漂了**、1 处的目标**今天已不存在**。

### 二、形状

`docs/HANDOFF_INFIVERSE.md` 是写给**另一棵树**（INFIVERSE）的交接记录：它用 `文件:行号` 指认实现，而它引的那棵树不在这台机器上、也不会跟着这棵树长大。于是每一次上游改动都会让那些数字**静默地指向别的东西** —— 读者仍然看得见一个数字，只是那个数字在说另一个故事。本格之前没有任何机制看住它：行引用检查器的 TARGETS 只包含两个构建文件，这份文档根本不在它的视野里。

### 三、判据与读数

判据不是「这行对不对」（那要另一棵树），而是「**这句引文今天在不在它点名的文件里**」：`tools/check_handoff_anchors.py` 解析文档里的每个内容锚点，去对应文件里找那段引文。检查器自身的控制格有五枚（三枚压结论、一枚压配对规则），`--self-check` 报 `checks=5|verdict=PASS`。

```
HANDOFF-ANCHORS|anchors=33|found=33|missing=0|verdict=PASS
HANDOFF-ANCHORS-SELFCHECK|checks=5|verdict=PASS
```

替换的证据在文档自己身上：改动后 `grep` 该文件里 `文件:行号` 形状的引用是 **0**，留在原地的数字都带 `[obs: fe72f9f]` 或明确的复跑命令。

### 四、读数

合并树（`8c20c9a`）上的全量读数：`ctest 203/203`、`explicit delta +12 (expect +12)`、`unanchored delta -4 (ceiling +0)`、`reconciliation VERDICT|PASS|measured=5`，以及这一格最值得记的一处：

```
check_obs_markers: pointers 2457 -> 2430; pointer delta +223 -> +196 (ceiling 223)
```

### 五、写这一条之前量过的事

1. 这份文档**不在**行引用检查器的 TARGETS 里（它只看两个构建文件），所以它的行号从来没进过那枚 pin —— 改它不会动 `explicit`。
2. 把一处裸 `:NNN` 改成显式 `文件:行号` 会**新增**一个匹配 ⇒ `explicit` 变成 +13 而红。因此本格把每一处都改成了引文（`` `文件` 里 `引文` ``），而不是「把行号改对」。
3. 在同一行加 `[obs: <sha>]` 会把该行的引用从 pointer 变成 record，pointer delta 因此**下降**。
4. 那 27 处未标注引用是随着行号一起消失的 —— 这就是 ceiling 读数从 +223 掉到 +196 的全部原因，不是别处漏了 27 个。
5. 合并时两枚分支都把 CTest 计数写死（200 与 202），正解不是二选一，而是**在合并树上重量**：`ctest -N` 给 203，两边都改成 203。

### 六、边界

- 检查器证明的是「**引文存在**」，不是「引文周围那句话仍然成立」：一段引文可能还在文件里，而它上面的句子已经过时。这一条写进了检查器的输出尾注，而不是留给读者猜。
- 目标已消失的那几处**不重新指向**别的东西：它们在文档里被记成「这个对象今天不存在」，并给出替代职责落在哪（例如一个全局绑定判定）。
- 数字本身仍然需要 `[obs: <sha>]` 才有意义；本格没有给全部数字补观测标记，只补了重测过的那几处。
- 检查器读的是**当前这份 checkout**；它看不见那份文档真正描述的那棵树，因此「当年是否成立」永远不在它的判据里。
- 本格不动 `docs_headings.pin.json`：它因为新增小节而变旧，重取 pin 是**合并那一侧的活**（`--write-pin` 在脏树上拒绝写）。
- 本格不含 `src/` 改动：它一个字都没动运行期。

## §10.156 生成器自报族谱：一族沉默就是一次红（tools，A 类外一项）

### 一、来源与日期

- 提交：`0ba1164`（生成器自报 + 沉默即红；红、绿两条读数都逐字贴在那笔的提交信息里）。本节文档随后单独一笔。
- 日期：2026-11-11。基线 `8c20c9a`（与 `git ls-remote origin refs/heads/main` 同 sha）。
- 本格不动 `src/`：改的是生成器**关于自己的说法**。

### 二、形状：沉默与正确，从生成器内部无法区分

§10.154 靠人把生成器的代码对着路线的臂列表读，找出两族死代码，然后在自己的「六」里写下了下一格的条件——「若要它变成有牙的断言，得先有人写下『哪一族必须出现在报告里』」。本格就是那一格。

在那之前，生成器相信的族只活在 rng 的分支里：哪一族被摇到、摇到几次，跑完只知道总分（`ran`、`diverge`、`refused-by-name`），**报告里没有族名**。所以把某一族的概率改塌到零（写错分支条件、把骰子写死、常量折叠掉一种运算符）之后，输出与「这族全对」逐字节相同——2500 例 PASS 站在两族死代码前面，就是这么来的。这是同一课的第三次，前两次都只能靠人读出来。

三件东西现在都在生成器里，且都写在同一份源码上：

1. **声明**：`FAMILIES` 是工具对自己所相信的全部族名的声明，写在 `gen_program` 上方，reviewer 可以就着一处把声明与臂列表对着读。
2. **登记**：每族在自己的代码块里 `emitted.add(name)`，不在事后从生成文本里猜。块停止发程序，登记随之消失——这才是重点。
3. **红**：`audit_families` 把「声明了却零程序」变成 `FFAMILY-SILENT`；`verdict_of` 是**唯一**决定 PASS/FAIL 的地方，自检因此量的是真规则，不是它的拷贝。

### 三、判据与红读数

判据三条，方向都是「说出来的必须兑现」：

- 声明了、却零程序 → `FFAMILY-SILENT|family=<n>|programs=0|of=<count>` → `VERDICT|FAIL`，退出码 1。
- 登记了、却不在声明里 → `FFAMILY-UNDECLARED|...`，同样红：一份会过期的声明，是同一课高一层。
- `--suppress-family gwoth`（拼错的名字）→ `FFAMILY-SUPPRESS-UNKNOWN`，退出码 1。压不住的杆子只会打出绿色，而绿色读起来正像「这个控制不工作」，所以键集在这里是关死的。

红读数两条，都取 seed 7、`--count 20`。**(a) 旗子压族**（`--suppress-family growth`，退出码 1）；**(b) 源码级压族**（把 growth 的骰子 `0.4` 临时改成 `0.0`，不加旗子，退出码 1）——(b) 打印的 `suppressed=-` 说明这次红来自生成器自己的骰子，不是来自那根专门用来演示它的杆子。两条都在下节逐字；恢复骰子后同一命令回到 `VERDICT|PASS`、退出码 0。

### 四、读数

红 (a)，旗子压族：

```
FFAMILY-SILENT|family=growth|programs=0|of=20
IR-AOT-MIR-FUZZ-FAMILY|name=growth|programs=0|of=20|SILENT
IR-AOT-MIR-FUZZ-FAMILIES|declared=10|emitted=9|silent=1|undeclared=0|suppressed=growth
IR-AOT-MIR-FUZZ|seed=7|count=20|ran=17|diverge=0|threw=0|unsound=0|refused-by-name=3
IR-AOT-MIR-FUZZ-VERDICT|FAIL
exit=1
```

红 (b)，源码级压族（`0.4` → `0.0`，无旗子）：

```
FFAMILY-SILENT|family=growth|programs=0|of=20
IR-AOT-MIR-FUZZ-FAMILY|name=growth|programs=0|of=20|SILENT
IR-AOT-MIR-FUZZ-FAMILIES|declared=10|emitted=9|silent=1|undeclared=0|suppressed=-
IR-AOT-MIR-FUZZ-VERDICT|FAIL
exit=1
```

恢复后的绿，四个种子族（每族打印自己的普查行、总分与判决）：

```
IR-AOT-MIR-FUZZ-FAMILIES|declared=10|emitted=10|silent=0|undeclared=0|suppressed=-
IR-AOT-MIR-FUZZ|seed=7|count=20|ran=19|diverge=0|threw=0|unsound=0|refused-by-name=1|dir=/tmp/aot_mir_fuzz_es0mm021
IR-AOT-MIR-FUZZ-VERDICT|PASS
IR-AOT-MIR-FUZZ-FAMILIES|declared=10|emitted=10|silent=0|undeclared=0|suppressed=-
IR-AOT-MIR-FUZZ|seed=11|count=20|ran=14|diverge=0|threw=0|unsound=0|refused-by-name=6|dir=/tmp/aot_mir_fuzz_tbmayaef
IR-AOT-MIR-FUZZ-VERDICT|PASS
IR-AOT-MIR-FUZZ-FAMILIES|declared=10|emitted=10|silent=0|undeclared=0|suppressed=-
IR-AOT-MIR-FUZZ|seed=13|count=20|ran=13|diverge=0|threw=0|unsound=0|refused-by-name=7|dir=/tmp/aot_mir_fuzz_fdhsptc6
IR-AOT-MIR-FUZZ-VERDICT|PASS
IR-AOT-MIR-FUZZ-FAMILIES|declared=10|emitted=10|silent=0|undeclared=0|suppressed=-
IR-AOT-MIR-FUZZ|seed=17|count=20|ran=17|diverge=0|threw=0|unsound=0|refused-by-name=3|dir=/tmp/aot_mir_fuzz_5qxvho0k
IR-AOT-MIR-FUZZ-VERDICT|PASS
```

seed 7 的逐族普查：十族全部非零，且 `scalar-tail` 恰好等于 `count`（下一节说它为什么该等于）：

```
IR-AOT-MIR-FUZZ-FAMILY|name=container-len|programs=12|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=index-side|programs=8|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=growth|programs=5|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=nest-alias|programs=9|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=closure|programs=7|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=loop|programs=13|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=try-catch|programs=3|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=set-ops|programs=10|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=str-concat|programs=14|of=20|emitted
IR-AOT-MIR-FUZZ-FAMILY|name=scalar-tail|programs=20|of=20|emitted
```

自检（`--self-check`，退出码 0，取证那一格从 4 格加到 9 格）：

```
IR-AOT-MIR-FUZZ-SELFCHECK|what=families-all-emitted|silent=[]|undeclared=[]|ok
IR-AOT-MIR-FUZZ-SELFCHECK|what=family-silenced-is-named|silent=['growth']|ok
IR-AOT-MIR-FUZZ-SELFCHECK|what=silent-family-is-red|verdict=FAIL|ok
IR-AOT-MIR-FUZZ-SELFCHECK|what=undeclared-family-is-red|verdict=FAIL|ok
IR-AOT-MIR-FUZZ-SELFCHECK|what=scalar-tail-is-the-canary|scalar-tail=20|of=20|ok
IR-AOT-MIR-FUZZ-SELFCHECK|declared=container-len,index-side,growth,nest-alias,closure,loop,try-catch,set-ops,str-concat,scalar-tail
IR-AOT-MIR-FUZZ-SELFCHECK|checks=9|verdict=PASS
```

拼错族名的拒绝：

```
FFAMILY-SUPPRESS-UNKNOWN|name=gwoth|why=not-in-FAMILIES|declared=10
IR-AOT-MIR-FUZZ-FAMILY-DECLARED|names=container-len,index-side,growth,nest-alias,closure,loop,try-catch,set-ops,str-concat,scalar-tail
exit=1
```

### 五、写这条之前先量过的事

1. **两个方向的红都真的跑出来了，不是设计**：先按旗子压（退出码 1），再把 growth 的骰子改 0（退出码 1，且 `suppressed=-`），恢复后同一条命令回到 0。只写判据而不看它红，「这个控制工作」就只是一句话。
2. **本格没有改变路线上的任何读数**：四个种子族的 `ran`/`refused-by-name` 与 §10.154 记录的值逐个相同（19/1、14/6、13/7、17/3），分歧、抛出、不可靠都仍是 0。红只来自「族名零程序」这一条新判据，不是把旧判据调紧了。
3. **`scalar-tail` 每个程序都登记，所以它该等于 `count`**：它是普查自己的金丝雀——某族读 0 究竟是「骰子没摇到」还是「计数器坏了」，靠一个不可能沉默的族分开；实测 20/20。
4. **计数没有动**：本格不加 CTest，`ctest -N` 仍是 203（与 `EXP_CTEST`、`recon.ctest-count` 一致），`check_reconciliation.py` 仍 `VERDICT|PASS|measured=5`。要求「若加 CTest 必须同笔同步计数」因此未被触发。
5. **文档预算改前量过**：`explicit delta +12 (expect +12)`、`unanchored delta -4 (ceiling 0)`、`pointer delta +196 (ceiling 223)`。本节刻意不写任何新的带行号的路径引用（`EXP_LINE_REFS_DELTA` 是**等号**判据，多一条显式引用就变 +13），所以三项读数改后不变。

### 六、边界（每条都是决定，不是遗漏）

- **不加新 CTest**：四个已注册的种子族今天就端到端跑这条沉默检查（零程序族 → `FFAMILY-SILENT` → 退出码 1 → CTest 红），再加一个「专跑自检」的注册只会多一处计数要同步、少一分信号。代价写在这里：门禁里**没有**任何一步会去压族，压族这一步是人手动复现的。
- **`scalar-tail` 是无条件族，故意保留**：它不进任何 `if`，所以它衡量的是「登记是否还在发生」，不是「骰子是否友好」。它是金丝雀，不是覆盖面。
- **`index-side` 登记在 `container-len` 的块里**：它今天只在这块里可达（容器先被造出来，才谈得上取下标），所以压 `container-len` 会同时点名两族——那是代码形状的真话，不是重复计数；普查里 12 与 8 的差就是这个包含关系。
- **压族不保证其余部分的字节相同**：被跳过的块本来会抽自己的随机数，所以压族之后程序流整体位移（红 (a) 的 `ran=17` 与绿的 `ran=19` 就是这个位移）。杆子只承诺「零程序即红」，不假装被压的那次运行与原来同构；这一点写在 `_hit` 的 docstring 里，而不是靠读者去猜。
- **生成器的运行目录仍是每次运行新建的临时目录，不改**：那是工具的既有行为，单次进程内自足；本格的 scratch（配置日志、构建日志、四个种子的输出、提交信息草稿）全部落在 worktree 的 `.scratch/` 下。
- **本格不声称覆盖面**：普查说的是「这一族发了几个程序」，不是「这一族打到了路线的哪几条臂」；把族名对到臂列表仍是人读的活（§10.154 就是这么做的），本条只是让「族名一个都没有」这件事自己红。

## §10.157 实参个数对不上：cc 的报错不是本路线的答案（aot-mir，A 类第三十二项）

### 一、来源与日期

- 判据：`5001220`（九条先红：`mir-only-cases=215` 而 `mir-only-same-as-interpreter=206`，`IR-MIR-VERDICT|FAIL`）。
- 实现：`7cc9067`。
- 日期：2026-11-11。基线 `ec21ffe`。
- 本格**不推进 `handles`**（`59|of=67|unknown=8` 不动：没有新操作码），推进的是**同一棵树上的答案**：`accepted 292→301`、`mir-only-cases 206→215`。九条此前要么编译不过、要么运行期 `rc=-6` 的程序，现在执行且与解释器逐字节相同。

### 二、形状

一个用户函数的**签名由它自己的声明决定**：`func h(n)` 发射 `static MirVal fn_h(MirVal a0)`，与调用点无关。实参的个数却由程序决定。两者不一致时，报错的地方变成了 **C 编译器**：

```
func h(n) { return n }
say h(1, 2)
```

翻译本身是成功的——`aot-mir translate` 退出码 0，发射出

```
v[1] = fn_h(mvargs[asp-2], mvargs[asp-1]); asp -= 2;
```

紧接着 cc 拒绝这条程序：

```
error: too many arguments to function 'fn_h'; expected 1, have 2
  ... | v[1] = fn_h(mvargs[asp-2], mvargs[asp-1]); asp -= 2;
  ... | static MirVal fn_h(MirVal a0) {
```

反方向同形：`say h()` 得到 `too few arguments to function 'fn_h'; expected 1, have 0`。而解释器两题都答（`1` 与 `nil`，rc 0）。

这是本路线最贵的一类失败：不是答错，而是**没有答案**——程序连被比较的机会都没有，失败发生在 C 编译器里，而这条路线存在的全部理由就是把 `.im` 变成能跑的 C。同一个 bug 在函数值那条路上晚一层发作：`f = () -> 42` 配 `f(1, 2)` 能编过，然后在运行期 abort，`rc=-6`。

### 三、判据与红读数

九条判据**先写、先红**，红读数分两层：

| 判据 | 程序 | 解释器 | 红读数 |
|---|---|---|---|
| `mir_arity_extra_args_by_name` | `func h(n) { … }` / `say h(1, 2)` | `1` | cc failed：`too many arguments to function 'fn_h'; expected 1` |
| `mir_arity_missing_args_by_name` | `func g(a, b) { … }` / `g(7)` | `7`, `nil` | cc failed：`too few arguments to function 'fn_g'; expected 2` |
| `mir_arity_zero_args_by_name` | 同上 / `g()` | `nil`, `nil` | cc failed：`too few arguments … expected 2, have 0` |
| `mir_arity_extra_arg_is_evaluated` | `h(1, mark())`，`mark` 印字 | `mark`, `body`, `1` | cc failed：`too many arguments to function 'fn_h'` |
| `mir_arity_nested_mixed` | `three(one(1, 2), one(3), one(4, 5, 6))` | `8` | cc failed：`too many arguments to function 'fn_one'; expected 1, have 2` |
| `mir_arity_value_extra` | `f = () -> 42` / `f(1, 2)` | `42` | `rc=-6`（运行期 abort） |
| `mir_arity_value_missing` | `f = (a, b) -> a` / `f(7)`、`f()` | `7`, `nil` | `rc=-6` |
| `mir_arity_value_capture_extra` | 捕获闭包 / `f(1, 2)`、`f(1, 2, 3, 4)` | `13`, `13` | `rc=-6` |
| `mir_arity_value_through_param` | `apply(dbl, 3, 9)` | `6` | `rc=-6` |

前五条红在 **cc**：翻译接受（`mir=accepted`、`cc=ok` 都还没到），编不过。后四条红在**运行期**：C 编过了，`OP_CALL_VALUE` 用的那张分发表里逐函数发射的

```
if (nargs != 2) {
    fprintf(stderr, "error: function takes 2 argument(s), got %d\n", nargs);
    abort();
}
```

把程序停下。两条 red 是两个层次，不是同一条：一条是「这条路线产不出 C」，一条是「产出了 C 却在跑的时候停住」。`rc=-6` 是 abort 的退出码，`exec=diff`、`bytes=0`。

### 四、读数

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=215|mir-only-same-as-interpreter=215
IR-MIR-VERDICT|PASS|tree=7cc9067602f66fc715dd441f4e18719d039c62e1|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR|accepted=301|refused=0|same-as-interpreter=301|of=29|emittable=59|of-ops=67|not-emittable=8|corpus=inline-cases:86|mir-only-cases:215|beyond-ast-denominator=30|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=7cc9067602f66fc715dd441f4e18719d039c62e1|accepted=301|of=29|covered=29|missing=0|refused=0|same-as-interpreter=301|prelude-excluded=yes|beyond-ast-denominator=30
```

九条各自变绿之后逐条读到（`mir-only-same-as-interpreter` 与 `mir-only-cases` 同值就是它们）：

| 判据 | 绿读数（`mir=accepted|cc=ok|exec=same|rc=0` 之后的 `bytes=` 与 `stdout-hex=`） |
|---|---|
| `mir_arity_extra_args_by_name` | `bytes=7` / `6e3d0a310a310a` |
| `mir_arity_missing_args_by_name` | `bytes=6` / `370a6e696c0a` |
| `mir_arity_zero_args_by_name` | `bytes=8` / `6e696c0a6e696c0a` |
| `mir_arity_extra_arg_is_evaluated` | `bytes=12` / `6d61726b0a626f64790a310a` |
| `mir_arity_nested_mixed` | `bytes=2` / `380a` |
| `mir_arity_value_extra` | `bytes=3` / `34320a` |
| `mir_arity_value_missing` | `bytes=6` / `370a6e696c0a` |
| `mir_arity_value_capture_extra` | `bytes=6` / `31330a31330a` |
| `mir_arity_value_through_param` | `bytes=2` / `360a` |

`c-identical=0|c-diff=86` 与 `perturb-stdout-changes=5` 都**没有动**：这一格改的是调用点的实参，不是那 86 条 AST 对照的 C 文本形状，也不是扰动面。

### 五、写这条之前先量过的事

1. **解释器的规则是量出来的，不是推出来的**（判据写之前，`build/inimerse --no-mods`）：多出来的实参**会被求值**——`h(1, mark())` 先印 `mark`，再进被调者的身体，所以副作用按实参顺序发生，然后被当作实参丢掉；实参不够时参数**读到 nil**（不是 0，也不是上一个调用的残留）：`func g(a, b)` 的 `g(7)` 印 `7`、`nil`，`g()` 印 `nil`、`nil`，`g(7, 8, 9)` 印 `7`、`8`。
2. **机制只有一处**：`L_CALL_FUNC` 一个臂同时服务两个操作码（`fidx` 在 `call_fidx_override` 与 `ins.r1` 之间由 `ins.op == OP_CALL_VALUE` 选），它的三行是「先按 `max(declared, argc)` 把 FR 填 nil，再把压入的实参按倒序搬进 FR」。所以「少读 nil」是那三行里的第一行，「多者丢弃」是第二行——本条实现的是这两个已测事实，不是新政策。
3. **函数值那条路单独量过**：`f = () -> 42` 配 `say f(1, 2)` 印 `42`；`f = (a, b) -> a` 配 `f()` / `f(7)` 印 `nil` / `7`；捕获闭包 `mk = (n) -> (a, b) -> n + a + b` 配 `mk(10)` 后 `f(1, 2, 3, 4)` 印 `13`。两条路**同一条臂**，但预扫描与发射是两处，所以分别量，而不是从按名调用那条外推。
4. **正常形状的 C 文本没有动**（逐字节 diff：同一批程序分别用改前/改后的 `aot-mir` 翻译）。按名调用且实参个数**对得上**的那个程序，发射结果**逐字节相同**（`IDENTICAL`）。同一次对比也给出了「动了哪些形状」，写进 §六。
5. **分配数是量出来的**：九条里只有捕获闭包那条分配，它自己的绿 `IR-MIR-ALLOCS` 行读 `allocs=1|freed=1|live=0|bytes=32`（一次实例化、一个单槽环境），于是 `ALLOCS_EXPECT` 添 `(1, 32)`，其余八条留在默认 `(0, 0)`。这一行是**绿跑之后**才写的：第一遍绿跑对这条报的是 `ok=no|expect-allocs=0|expect-bytes=0`，补上之后重跑才是 `expect-allocs=1|expect-bytes=32|ok=yes`。
6. **文档预算改前量过**：`explicit delta +12 (expect +12)`、`unanchored delta -4 (ceiling 0)`、`pointer delta +196 (ceiling 223)`。本节刻意不写任何新的带行号引用（显式引用是**等号**判据，多一条就变 +13），所以三项读数改后不变，见 §六。
7. **计数没有动**：本格不加 CTest，`ctest -N` 仍是 203（与 `EXP_CTEST`、`recon.ctest-count` 一致），`check_reconciliation.py` 仍 `VERDICT|PASS|measured=5`。

### 六、边界（每条都是决定，不是遗漏）

- **不复制解释器的「陈旧寄存器」现象，而是点名**。解释器把多出来的实参搬进 `FR[k+1]`（`k < argc`）之后才把「超出声明个数」的部分丢掉，所以被调者**自己**那个位置的局部寄存器里会留着实参，而且同一帧深度的下一次调用仍然看得到它——因为 nil 填充只到 `max(declared, argc)` 宽：

  ```
  func g(a) { if false { y = 1 }  say y }
  g(1, 99, 88)     -> 88      （第三个实参，落在局部槽 y）
  g(1)             -> 88      （还在：这个深度没有被重新填过）
  ```

  本路线两个都答 `0`（它进帧时逐槽置 0）。要复现就得让调用点能写被调者的寄存器，也就是把实参向量塞进签名，而这是解释器寄存器文件的陈旧性，不是它调用约定的内容。**这是本格唯一没有对齐的读数**，写明而不是假装它不存在；判据注释里也带着这段转录。
- **不动 builtin 的实参个数**。`say len(1, 2)` 在解释器里真的印 `2`，但**不是它返回了实参个数**——它读的是最后一个实参（§10.159 量并钉住这条规则）；本路线当时按名拒译这条程序，那是另一条已经写下来的能力边界，本格一格都没碰。
- **`mv_callv` 的运行期检查是少了一条，不是全没了**。留下的是「非函数不可调用」（`error: value is not callable`，rc 1）与「函数索引没有容器」两条；被删掉的只有「实参个数对不上」，因为解释器对它有答案而前两条它有/没有答案的结论不同。
- **哪些形状的 C 文本动了**（实测，见 §五 第 4 条）：按名调用且个数对得上 → **逐字节不变**；按名调用且个数对不上 → 只动那一行调用（`fn_h(mvargs[asp-2])` 取代 `fn_h(mvargs[asp-2], mvargs[asp-1])`，弹出的宽度仍是 `argc`，所以多出来的实参照样被求值）；**任何用到函数值的模块** → 那张分发表 switch 的文本会动，因为守卫删了、实参改成带 `nargs > k` 短路的三元式。最后一条是刻意的，而且 `nargs > k` 必须在前：`argc == 0` 的调用点按字面量传的是空指针，先取 `args[0]` 就是空指针解引用——这一条有判据 `mir_arity_value_missing` 的 `f()` 一行钉着。
- **本格不改 `prescan` 的收窄**：`want` 从 `nodes[callee].argc` 取，只在被调者的函数下标能解析时才用；解析不到时退回 `argc`，`mv_missing_callee` 那条回退路径的文本因此一个字都没动。
- **文档预算三项读数改后**：`explicit delta +12 (expect +12)`、`unanchored delta -4 (ceiling 0)`、`pointer delta +196 (ceiling 223)`——与改前逐字相同，本节没有新增任何带行号的路径引用。


## §10.158 字典键是「哈希闸门 ∧ `val_eq`」，不是 `val_eq` 本身（aot-mir，A 类第三十三项）

### 一、来源与日期

- 判据：`e583c2c`（十五条：六条红、九条是对照组；红读数 `mir-only-cases=221` 而 `mir-only-same-as-interpreter=215`，`IR-MIR-VERDICT|FAIL`）。
- 实现：`df0e75a`。
- 合并：`106886a`（把 `89675b5` 合进来之后重跑：`mir-only-cases=230|mir-only-same-as-interpreter=230`、`IR-MIR-VERDICT|PASS`）。
- 日期：2026-11-11。基线 `ec21ffe`。
- 本格**不推进 `handles`**（`59|of=67|unknown=8` 不动：没有新操作码），推进的是**同一棵树上的答案**：`accepted 301→316`、`mir-only-cases 215→230`。
- 这一格是从一次**只读差分扫**（1820 个程序）里捞出来的静默分歧：两侧 `rc=0`、无拒译、`stdout` 不同——本路线最贵的一类。

### 二、形状

解释器的字典键不是「两个值相等」，而是**两件事同时成立**：桶里的 32 位哈希等于探针的哈希，**并且** `val_eq` 说相等。第二半落在 `dict_hash_find` 的收键条件里，第一半落在它的槽位选择里。

而 `dict_hash_key` 给布尔**单独一行**（`true` 是 `0x85ebca6b`、`false` 是 `0xc2b2ae35`），这两行都**不是**整数那一行；同一时刻，`val_eq` 的数值尾巴把布尔与整数放进同一个类。于是哈希这一半**拆开**了 `val_eq` 会合并的一对：

```
d = {}
d[1] = "a"
d[true] = "b"
say len(d)
```

解释器印 `2`，本路线印 `1`——因为它只做了「比较」那一半，没做「闸门」那一半。

真正把代价便宜的模型排除掉的是这一条：`d[4294967297]` 与 `d[4294967297.0]` 在解释器里是**两个键**。整数那一行把键截断到 32 位（`2^32+1` 恰好与 `1` 同哈希），而整值浮点只在 `[-2^31, 2^31)` 之内被改道走整数行、出了窗口就按 IEEE 位走自己那一行。所以「`val_eq` 再加一张 tag 类表（`{INT,FLOAT}` 一类、`{BOOL}` 一类）」这种模型会把这一条答成一——它对了其余各行，独错这一行；闸门不需要类表也复现得出来。

`==` **不是**这条规则：`1 == true` 仍然为真，而 `1` 与 `true` 是两个键。这正是修法不能落在 `mv_eq` 里的原因。

### 三、判据与红读数

十五条判据**先写、先红**。六条红在**同一层**：两侧 `rc=0`、无拒译，只是答案不同。

| 判据 | 一侧两键的写法 | 解释器 | 红读数（本路线） |
|---|---|---|---|
| `mir_boolkey_int_vs_bool` | `d[1]` 后 `d[true]` | `2`, `1`, `true` | `1`, `1`, `nil` |
| `mir_boolkey_bool_vs_int` | 顺序反过来 | `2`, `true`, `1` | `1`, `true`, `nil` |
| `mir_boolkey_false_vs_zero` | `d[0]` 后 `d[false]` | `2`, `false`, `0` | `1`, `false`, `nil` |
| `mir_boolkey_bool_vs_float` | `d[1.0]` 后 `d[true]` | `2`, `true`, `1` | `1`, `true`, `nil` |
| `mir_boolkey_int_float_hash_truncates` | `d[4294967297]` 后 `d[4294967297.0]` | `2` | `1` |
| `mir_boolkey_register_read` | 经变量写两键再读 | `b`, `2` | `a`, `1` |

读数里 `d[0]` 这种**位置**下标会答「第 0 对是哪一对」，所以每条同时钉住**对的数量**与**对的次序**：本路线那条唯一的对在第 0 位、键是整数；解释器整数对在前、布尔对在后。

红读数的原文（六条同形，这里贴两条）：

```
IR-MIR-EXEC-DIFF|case=mir_boolkey_int_vs_bool|interp-rc=0|mir-rc=0|interp-out=b'2\n1\ntrue\n'|mir-out=b'1\n1\nnil\n'
IR-MIR-EXEC-DIFF|case=mir_boolkey_int_float_hash_truncates|interp-rc=0|mir-rc=0|interp-out=b'2\n'|mir-out=b'1\n'
```

九条对照组在基线实现上**本来就绿**，所以一个「顺手多收几条」的修法会把它们弄红：整数与浮点在两个写序上都**合并成一键**（`d[1]` 与 `d[1.0]` 是同一个键，后写的值赢、键槽仍记 `1`）、两个数值相等的浮点、`0.0` 与 `-0.0`、`nil` 与 `0`、字符串 `"1"` 与整数 `1`，五条 `==` 的答案（`true`/`true`/`true`/`true`/`false`），以及两条**集合成员**判据——它们带着解释器那条非对称的整数半边规则（整数探针先二分整数半边再扫 items，布尔探针只看 items：`true in {1}` 为假、`1 in {true}` 为真）。集合那一套**故意不动**。

### 四、读数

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=230|mir-only-same-as-interpreter=230
IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=7|ok=7|expect=7
IR-MIR-VERDICT|PASS|tree=106886a92abeea70bab4fd26d044ffd0be54b0bd|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR|accepted=316|refused=0|same-as-interpreter=316|of=29|emittable=59|of-ops=67|not-emittable=8|corpus=inline-cases:86|mir-only-cases=230|beyond-ast-denominator=30|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=106886a92abeea70bab4fd26d044ffd0be54b0bd|accepted=316|of=29|covered=29|missing=0|refused=0|same-as-interpreter=316|prelude-excluded=yes|beyond-ast-denominator=30
```

`same-as-interpreter` 215→230，`differ` 仍是 0：没有任何一条原本一致的程序在这一格之后变得不一致。`of=29`、`covered=29`、`missing=0`、`beyond-ast-denominator=30` 全不动——修法移动的是**键的身份**，不是覆盖。

### 五、写这条之前量过的事

1. **键身份是合取，不是比较**：读 `dict_hash_find` 的收键条件（哈希相等 ∧ `val_eq`），再读 `dict_hash_key` 的三条行——布尔两行、整数一行（32 位截断）、浮点在内窗口改道整数行、出窗口按 IEEE 位。
2. **量了十三条形状**：布尔/整数两序、布尔/浮点、布尔/`false` 与 `0`、整数与整值浮点两序、两个数值相等的浮点、`0.0` 与 `-0.0`、`nil` 与 `0`、`"1"` 与 `1`、`4294967297` 与 `4294967297.0`、经变量写入再读、以及五条 `==`。**全部先量解释器、再量本路线**；六条不同、其余一致。
3. **`==` 与字典键是两条规则**：`1 == true` 为真而两键不同——所以修法只能落在键比较器里，不能改相等运算（改了会把 §10.127 的集合内容相等、§10.148 的集合 `-`/`*` 一起挪）。
4. **集合成员不参与**：那两条判据是**对照组**，照抄解释器的非对称半边规则，本格不允许顺手「统一」。
5. **行数是量出来的**：第一次绿跑把十四条新判据报成 `allocs=1|bytes=256, the criterion demands allocs=0|bytes=0`（每个程序起手一个空字典字面量占一块，修法本身零分配）；第十五条从不建字典，报 `allocs=0|bytes=0`。

### 六、边界

1. **字典键的闸门在本路线是「先查哈希再比」的线性对表**，不是桶表：解释器的两键/一键结论被逐条照抄，但**哈希碰撞**的处理路径不同（解释器在下标里碰撞、本路线在扫描里碰撞），两侧 `val_eq` 相同的碰撞对结论一致，两侧 `val_eq` 不同的碰撞对结论也一致——这一点是量过十条混写程序才写下的，不是推理。
2. **加一条必要条件只会把「命中」变「不命中」**：因此以前各格（尤其 §10.128 的「集合键恒不命中」）的答案方向全部保留，唯一的位移是「本来错误地并成一对的地方分开」。
3. **`==` 未动**，`mv_eq`/`mv_ne`/`mv_set_eq` 一字未改；集合成员那一侧也未动。
4. **没有新拒绝**：这一格没有引入任何 `why=`，十五条程序现在全部被接受并执行。

## §10.159 `len(x, y)` 读的是最后一个实参，不是实参个数；零参读的是残渣（aot-mir，A 类第三十四项）

### 一、来源与日期

- 判据：`6e49fa3`（八条：六条正、两条反；红读数 `mir-only-cases=236` 而 `mir-only-same-as-interpreter=231`，`IR-MIR-VERDICT|FAIL`，红跑在基线 `9dafc43` 的树上）。
- 实现：`58fcf79`。
- 日期：2026-11-11。基线 `9dafc43`。
- 本格**不推进 `handles`**（`59|of=67|unknown=8` 不动：没有新操作码），推进的是**同一棵树上的答案**：`accepted 316→322`、`mir-only-cases 230→236`、反向判据总数 `7→9`。
- 这一格是从一次**只读扫**（1820 个程序）里捞出来的：路线对 builtin 调用只收实参个数 1，其余一律拒译（`why=illegal-operand`），而解释器对多数的多参形状有答案。拒掉一个解释器会答的程序，等于把这条路线声称的面自己缩小。

### 二、形状

预扫描在 builtin 那个臂上收紧口是**一条按个数的检查**：解析到名字是 `len` 或 `list` 之后，若 `argc != 1` 就 `bad(...)`，`why` 落回默认的 `illegal-operand`；发射器也只有一个臂，弹一格（`asp -= 1;`）并从 `mvargs[asp-1]` 取值。于是每个多参形状都死在翻译期，程序从未变成 C。

解释器那边只有**栈顶**一条规则：`posix_core_len` 取 `vm_cur_stack(vm)[vm_cur_sp(vm)]`，算出长度，弹**一个**、压回一个，与指令里写的个数无关。所以多参调用里前面的实参被求值、留在实参栈上，答案是**最后一个实参**：

```
say len(1, 2)       -> 2        （最后一个实参）
say len(2, 1)       -> 1        （最后一个实参；若按个数答会是 2）
say len("ab", 5)    -> 5
say len(5, "ab")    -> 2        （最后一个实参；恰与个数同值，故必须与上一行配对才钉得住）
say len(9, 8, [1, 2, 3, 4]) -> 4（个数是 3，最后一个是长度 4 的数组）
```

「个数」模型、「首参」模型都在这些判别式上立刻输掉；`len(1, 2)` 印 2 只是**恰好**与个数同值，不是个数的证据——这也是 §10.157 §六 第二句把值读成个数的原因，那一句本格已经就地改正。

零参是另一件事：`len()` 没有实参可读，解释器读的是**目的寄存器**（空栈时 VM 直接把寄存器原样移过去）或**栈顶的残渣**（若外层刚压过实参）。于是：

```
say len()                 -> 0         （新程序：目的寄存器的初值）
x = 6 / say len()         -> 6         （同一个寄存器，还是 6）
x = "abcdef" / say list() -> abcdef    （残渣，原样是那个字符串）
say len("abc", len())     -> 3         （内层读到外层压上的 "abc"）
```

### 三、判据与红读数

八条判据**先写、先红**：六条被接受（含一条本来就在的对照），两条反向判据红在**另一个层次**（拒了，但理由不对）。

| 判据 | 写法 | 解释器 | 红读数（本路线） |
|---|---|---|---|
| `mir_builtin_argc_len_last` | `len(2, 1)`、`len(12345, 0)`、`len("abc", 1)`、`len(1, "abc")`、`len([1, 2, 3], [9])`、`len(1, [1, 2])`，再配 `len("ab", 5)` 与 `len(5, "ab")` 两条判别式 | `1`, `0`, `1`, `3`, `1`, `2`, `5`, `2` | 拒译（`why=illegal-operand`），程序未变成 C |
| `mir_builtin_argc_len_many` | `len(1, 2, 3)`、`len(9, 8, [1, 2, 3, 4])`、`len(1, 2, 3, 4, 5)` | `3`, `4`, `5` | 同上 |
| `mir_builtin_argc_len_args_evaluated` | `mark()` 在首位/次位：`len(mark(), 7)`、`len(1, mark())` | `m`, `7`, `m`, `5` | 同上 |
| `mir_builtin_argc_list_last` | `list(1, 2)`、`list(1, [5, 6])`、`list([5, 6], 1)`、`list(1, 2, 3)` | `nil`, `[5, 6]`, `nil`, `nil` | 同上 |
| `mir_builtin_argc_nested_expression_loop` | `len(len(1, 2), len([1, 2, 3], 9))`、`x = len(1, 2) + len(3, 4, 5)`、循环里 `len(n, 2)` | `9`, `7`, `2` | 同上 |
| `mir_builtin_argc_single_arg_control` | 单参四条（这条在基线上本来就绿，是做对照的） | `3`, `4`, `[1, 2]`, `1` | 绿（`bytes=13|stdout-hex=330a340a5b312c20325d0a310a`） |
| `mir_builtin_argc_zero_len`（反向） | `x = 6` 后 `say len()` | `6` | `why=illegal-operand`，期望 `builtin-stale-stack`，`ok=no` |
| `mir_builtin_argc_zero_list`（反向） | `x = "abcdef"` 后 `say list()` | `abcdef` | 同上 |

红读数的原文：

```
IR-MIR-MIR-ONLY|case=mir_builtin_argc_len_last|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_builtin_argc_len_many|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_builtin_argc_len_args_evaluated|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_builtin_argc_list_last|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY|case=mir_builtin_argc_nested_expression_loop|mir=refused|why=illegal-operand
IR-MIR-MIR-ONLY-REFUSED|case=mir_builtin_argc_zero_len|mir=refused|why=illegal-operand|expect=builtin-stale-stack|ok=no|interp-rc=0|interp-out=b'6\n'|stdout-bytes=2|stdout-hex=360a
IR-MIR-MIR-ONLY-REFUSED|case=mir_builtin_argc_zero_list|mir=refused|why=illegal-operand|expect=builtin-stale-stack|ok=no|interp-rc=0|interp-out=b'abcdef\n'|stdout-bytes=7|stdout-hex=6162636465660a
```

两条反向判据的红是「理由不对」而不是「答得不一样」：本路线拒了它们，但它当时给的理由是 `illegal-operand`——一个说「这个形状没有意义」的理由，而本格要的是「这个形状的答案不是程序的函数」，于是理由被具名成 `builtin-stale-stack`。

### 四、读数

```
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=236|mir-only-same-as-interpreter=236
IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=9|ok=9|expect=9
IR-MIR-VERDICT|PASS|tree=58fcf79066eb48cd92470260b52ced7e2f11ed6c|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR|accepted=322|refused=0|same-as-interpreter=322|of=29|emittable=59|of-ops=67|not-emittable=8|corpus=inline-cases:86|mir-only-cases=236|beyond-ast-denominator=30|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=58fcf79066eb48cd92470260b52ced7e2f11ed6c|accepted=322|of=29|covered=29|missing=0|refused=0|same-as-interpreter=322|prelude-excluded=yes|beyond-ast-denominator=30
```

`same-as-interpreter` 316→322，`differ` 仍是 0；`of=29`、`covered=29`、`missing=0`、`beyond-ast-denominator=30` 全不动——本格移动的是**同一棵树上被接受的程序**，不是覆盖；`handles` 也没有动。

六条被接受的判据逐条读到（`mir=accepted|cc=ok|exec=same|rc=0` 之后）：

| 判据 | `bytes` / `stdout-hex` | 分配（绿跑之后抄的） |
|---|---|---|
| `mir_builtin_argc_len_last` | `16` / `310a300a310a330a310a320a350a320a` | `allocs=2|bytes=768` |
| `mir_builtin_argc_len_many` | `6` / `330a340a350a` | `allocs=1|bytes=256` |
| `mir_builtin_argc_len_args_evaluated` | `8` / `6d0a370a6d0a350a` | `allocs=0|bytes=0` |
| `mir_builtin_argc_list_last` | `19` / `6e696c0a5b352c20365d0a6e696c0a6e696c0a` | `allocs=1|bytes=256` |
| `mir_builtin_argc_nested_expression_loop` | `6` / `390a370a320a` | `allocs=1|bytes=256` |
| `mir_builtin_argc_single_arg_control` | `13` / `330a340a5b312c20325d0a310a` | `allocs=1|bytes=256` |

两条反向判据现在是 `builtin-stale-stack|expect=builtin-stale-stack|ok=yes`。发射出来的 C 是这两行（照抄生成文本）：

```
    v[1] = mv_len(mvargs[asp-1]); asp -= 2;
    v[3] = mv_len(mvargs[asp-1]); asp -= 3;
    v[4] = mv_list(mvargs[asp-1]); asp -= 2;
```

### 五、写这条之前量过的事

1. **规则是量出来的，不是推出来的**（判据落笔之前，`build/inimerse --no-mods`）：`len(2, 1)` 印 1、`len(1, 2)` 印 2、`len("ab", 5)` 印 5、`len(5, "ab")` 印 2、`len(9, 8, [1, 2, 3, 4])` 印 4、`len([1, 2, 3], [9])` 印 1、`len(1, [1, 2])` 印 2、`len(1, 2, 3)` 印 3、`len(1, 2, 3, 4, 5)` 印 5——**最后一个实参**，不是个数、也不是首参。`list` 一并量过：`list(1, [5, 6])` 印 `[5, 6]`、`list([5, 6], 1)` 印 `nil`、`list(1, 2)` 印 `nil`。
2. **每个实参都被求值**：`mark()` 放在首位（`len(mark(), 7)`）先印 `m` 再印 `7`，放在次位（`len(1, mark())`）也是先 `m` 再 `5`；两侧 stdout 的次序就是求值次序。所以「接受」不是「只读最后一个就扔掉前面」，前面照样求值——本路线在发射里也只是不建残渣，不跳过求值。
3. **零参是残渣，量了四条程序**：新程序 `say len()` 印 0；`x = 6` 之后印 6；`x = "abcdef"` 之后 `say list()` 印 `abcdef`；`say len("abc", len())` 印 3（内层的零参调用读到的正是外层压上的 `"abc"`）。机制读自 `posix_core_len`/`posix_core_list` 与 VM 的 builtin 臂：它们只读栈顶、只弹一个，空栈时 VM 把目的寄存器原样移过去——所以零参答的是**寄存器/栈的上一状态**，不是程序请求的值。
4. **单参的 C 文本逐字节没有动**：同一批单参程序（`len(x)`、`len("abcd")`、`list(x)`、`len(1)`）分别用主树上未改的 `aot-mir` 和本格改后的 `aot-mir` 翻译，`diff` 结论 `IDENTICAL`。发射里 `asp -= %d` 在 `argc == 1` 时就是原来那行 `asp -= 1;`，这是逐字节对比量出来的，不是推理。
5. **分配数是绿跑之后抄的**：`len_last` 是 9 个字面量槽（4+2+3），池从 8 槽翻到 8+16 槽，`2|768`；`len_many` 一个 5 槽 `[1, 2, 3, 4]`、`list_last` 一个 3 槽 `[5, 6]`、`nested` 一个 4 槽 `[1, 2, 3]`、对照一个 4+3 槽，都是 `1|256`；`args_evaluated` 没有容器字面量，`0|0`。第一遍绿跑这五条的 `ALLOCS_EXPECT` 是默认 `(0, 0)`，报 `ok=no`，补上之后重跑才 `ok=yes`。
6. **文档预算改前量过**：`explicit delta +12 (expect +12)`、`unanchored delta -4 (ceiling 0)`、`pointer delta +196 (ceiling 223)`，`CITATIONS|base=1199|now=1199`。本节刻意不写任何新的带行号引用（显式引用是**等号**判据，多一条就变 +13），所以三项读数改后不变，见 §六。
7. **计数没有动**：本格不加 CTest，`ctest -N` 仍是 203（`EXP_CTEST` 与 `check_reconciliation.py` 的 `ctest-count` 都读 203），`check_reconciliation.py` 仍 `VERDICT|PASS|measured=5`。

### 六、边界（每条都是决定，不是遗漏）

1. **多参照答是一个决定，不是复制缺陷**。解释器的缺陷是「builtin 忽略个数、把压上的实参留在栈上」；它答的**值**是最后一个实参，而这个值是程序的函数：`len(1, 2)` 与 `len(2, 1)` 印不同、且两侧都由程序本身决定。零参答的值**不是**程序的函数：两个只差前置赋值的程序印不同，也没有任何程序能把那个残渣与「程序要的值」分开。所以「答多参、拒零参」是同一条规则的切分，不是双重标准；若无条件照抄零参，本格就开始复制缺陷。
2. **拒的是零参，具名 `builtin-stale-stack`**。明细句写明「答案是一个遗留物（若外圈压过实参则是栈顶，否则是目的寄存器的上一个值），不是程序请求的值」。`why` 词典从 `illegal-operand` 的默认值里分出这一条，因为「这个形状没有意义」不是本格的意思——这个形状有意义，只是答案不可复现。
3. **为什么拒而不照抄零参**。要复现它，C 侧就得维护一个「残余实参栈」并在零参调用时读它——那是解释器寄存器/栈的陈旧性，不是调用约定的内容（同 §10.157 §六 第一条的思路）。而且本路线**不留残渣**：它接受多参时每个实参都求值、都不留在栈上，唯一能读残渣的调用点（零参 builtin）被点名拒掉之后，残渣在任何被接受的程序里**不可观测**——这一点由预扫描的静态性保证（预扫描走遍所有容器，被接受的程序里没有零参 builtin 调用），不是靠运行时赌。
4. **不接受「个数」模型，也不接受「首参」模型**。三对判别式钉着：`len(2, 1)`/`len(1, 2)`（个数与首参都答 2，量到 1），`len("ab", 5)`/`len(5, "ab")`（首参答 5/5，个数答 2/2，量到 5/2），`len(9, 8, [1, 2, 3, 4])`（个数答 3，量到 4）。将来若有人改回 count，这三条直接红。
5. **单参一个字节都没动**。`asp -= %d` 在 `argc == 1` 时就是旧的 `asp -= 1;`；改前/改后的 `aot-mir` 对单参程序翻译出的 C `IDENTICAL`（逐字节 diff，见 §五 第 4 条）。86 条 AST 对照的 `c-identical=0|c-diff=86` 与 `perturb-stdout-changes=5` 也都没动。
6. **builtin 的名字面不变**：仍只发 `len`/`list`，其余名字仍 `builtin-call`；名字先判，所以 `str(1)` 这种零参/多参的形状仍报 `builtin-call`，不会落到 `builtin-stale-stack`。本格没有扩大 builtin 面。
7. **§10.157 §六 第二句就地改正**。那句说 `say len(1, 2)` 在解释器里「返回实参个数」，量过之后是错的：它读的是最后一个实参（`len(2, 1)` 印 1 就否掉了个数）。原文保留会把两处读数写成相反的，所以那一句改成「读的是最后一个实参」，并让本节的判据钉住它；§10.157 说的「本格没碰 builtin 实参个数」仍然是事实，只是那一格的描述口径不再被沿用。
8. **文档预算**：本节不新增任何带行号的路径引用，三项读数改后与改前逐字相同（见 §五 第 6 条与 §六 末尾的读数）。
## §10.160 三处两侧同错的同意：finally 的触发面、finally 里的 throw、集合迭代的 nil（aot-mir，A 类第三十五项）

### 一、来源与日期

- 判据：`40013db`（十二条：七条 finally、五条迭代；**首写即绿**）。
- 实现：无。本格一个字节都不动 `src/`：三处都不是本路线的缺陷——`try`/`catch`/`finally` 的降级是 §10.137 从解释器逐字搬下来的，集合遍历是 §10.125 之前就有的「下标读」形状。判据锁定的是**已经成立的同意**，不是一处修法。
- 日期：2026-11-11。基线 `9dafc43`（`9dafc433a02be7501205a7b2d19a256099d8c8b2`）。
- 本格**不推进 `handles`**：`handles=59|of=67|unknown=8` 一条不动——没有新操作码，`OP_INDEX_GET` 与 `OP_THROW` 都是既有的。推进的是 `mir-only-cases 230→242` 与 `accepted 316→328`（同一基线上量的）。
- 一句话：**这是等号，不是箭头**。§10.157/§10.158 是「先红后绿」；这一格相反——只读扫把三处同意扫出来，判据当场绿。绿不是「没测」，绿是结论本身：任何一侧在未来改掉这三处，判据会当场红，那才是真正的 drift。

### 二、形状

三处都只用最小程序量，两侧分别是 `build/inimerse --no-mods run` 与「`build/aot-mir translate` → `cc -O2 -std=c99` → 运行」；比较的是 stdout **字节**与退出码。

**其一，finally 只在「catch 体非空」时进入正常收尾路径。**

```wren
try {
    say "body"
} finally {
    say "fin"
}
say "after"
```

两侧都是 `body\nafter\n`（11 字节），rc 0，`fin` 一次也不印。中间加 `catch { }`、或加 `catch (e) { }`，读数一字不变；把 catch 体写上一句 `say "caught"`（哪怕一次都不执行），同一程序就变成 `body\nfin\nafter\n`（15 字节）——决定 finally 去不去的是 catch 体的**语句条数**，不是有没有 `catch` 关键字。

**异常路径不是这样**：`try { throw "boom" } finally { say "fin" }` 两侧印 `fin\n`（4 字节）、rc 1、stderr 是 `boom`。finally 在异常路径上会跑，跑完把原错误重新抛出。所以「没有 handler 就不跑 finally」的准确形状是：**正常收尾路径**跳过了它，异常路径没有。

机制在解析器与编译器里：`handlerCount` 只数 catch 体的语句，finally-only 的 try 会被注册一个幻影全局 `__inimerse_finally_error`，于是正常路径的 JUMP 被**补丁打了两次**——

```c
if (stmt->tryStmt.handlerCount == 0 && stmt->tryStmt.finallyCount > 0) {
    finally_error_idx = register_global(comp, "__inimerse_finally_error");
    varIdx = finally_error_idx;
}
...
patch_r2(comp->curBC, tjo, comp->curBC->count);   /* 先落在 catch 体尾，也就是 finally 开头 */
for (int i = 0; i < stmt->tryStmt.finallyCount; i++)
    compile_stmt(comp, stmt->tryStmt.finallyBody[i], break_list, break_count_ptr);
if (finally_error_idx >= 0) {
    int rethrow = alloc_reg();
    emit(comp->curBC, OP_LOAD_GLOBAL, rethrow, finally_error_idx, 0);
    emit(comp->curBC, OP_THROW, rethrow, 0, 0);
    /* Normal completion must skip both the cleanup exception rethrow
       and the exception-only finally entry. */
    patch_r2(comp->curBC, tjo, comp->curBC->count);  /* 第二次补丁把跳点挪到 finally 之后 */
}
```

字节码把这件事钉死了（`try { say "body" } finally { throw "fin-throw" } say "after"` 的 main 流）：

```text
57,0,23,0    OP_TRY_START   varIdx=23：幻影全局 __inimerse_finally_error
3,1,0,0      LOADK "body"    30,1,0,0  SAY    58,0,0,0  OP_TRY_END
24,0,9,0     OP_JUMP   正常路径 0→9：跳过的正是 finally 体与其重抛
3,1,1,0      LOADK "fin-throw"   59,1,0,0  THROW
22,1,23,0    LOAD_GLOBAL 23   59,1,0,0  THROW
9: LOADK "after"   SAY   HALT
```

本路线按字节码原样发 `goto`（跳点就是表里那个 9），于是两侧在同一个空档上对齐。`OP_TRY_START` 压的那一帧在解释器里是 `ExcFrame{catch_ip, var_idx, ignore}`；`vm_throw` 取栈顶帧、`var_idx >= 0` 时把错误值写进那个全局、`ignore` 时只记进调试槽，然后 `ip = f->catch_ip`——这正是异常路径「先跑 finally 再重抛」的实现，也是正常路径那条跳点被覆盖之后不再传上去的原因。

**其二，finally 里的 throw：运行则上浮，跳过则不可达。**

```wren
try {
    say "body"
} finally {
    throw "fin-throw"
}
say "after"
```

两侧印 `body\nafter\n`（11 字节）、rc 0、stderr 空。「throw 被吞掉」在这里的可观测形式只是：**它根本没执行**。

真让它执行，它就传上去：`try { throw "boom" } catch { say "caught" } finally { throw "fin-throw" }` 两侧 stdout `caught\n`（7 字节）、rc 1、stderr 是 `fin-throw`（原错误 `boom` 让位）；`try { throw "boom" } finally { say "fin" }` 两侧 `fin\n`、rc 1、stderr `boom`。

所以简报那句「finally 里的 throw 被吞掉」只在一个形状上为真：`break`/`continue` 把 finally 内联进循环控制流（此时异常帧还开着，throw 落回自己的帧里被静默）。而本路线对这种「跳出 try 区」的程序在 translate 阶段就按 `why=try-escapes` 整条拒译（§10.137 量的正是解释器在这里泄漏帧），那条形状因此不构成可锁的同意——它是边界，不是判据。

**其三，集合逐元素迭代恒给 `nil`。**

```wren
s = 1, 2, 3
for x in s {
    say x
}
say "end"
```

两侧：`nil\nnil\nnil\nend\n`（16 字节），rc 0——`len(s)` 是 3，所以循环跑三次、每次读出一个 nil。两元素 `s = 1, 2` 印两行；数组 `a = [1, 2, 3]` 印 `1\n2\n3`；字典 `d = {"a": 1, "b": 2}` 印键 `a\nb`；区间 `p = [0, 3)` 一行也不印（`len(p)` 是 0）。

机制是 `for x in <容器>` 的降级形状：索引从 0 起、`while i < len(container)`、每轮 `x = container[i]`——

```c
/* for x in <容器>：i=0; while i<len(arr): x=arr[i]; body; i++ */
emit(comp->curBC, OP_INDEX_GET, r_x, arr_reg, r_i);
```

而 `OP_INDEX_GET` 只认识数组、字典、字符串三族容器，其余全部落到最后那个分支：

```c
} else {
    value_set(&R[ins.r1], VAL_NIL, 0, 0, NULL, NULL);
}
```

集合没有臂，于是 `s[i]` 恒为 `nil`，而 `len(s)` 是元素**个数**：正好印 `len(s)` 行 nil。路线侧是同一个形状——读容器的辅助函数在数组/字典/字符串三臂之后原样抄了同一个收尾 `return mv_nil();`，两侧一起掉进同一个兜底。

### 三、判据（首写即绿）

十二行在 `MIR_ONLY` 末尾，读数逐字节相同：

| 判据 | 程序 | 读数（两侧相同） |
| --- | --- | --- |
| `mir_finally_only_skipped` | `try{say body}finally{say fin}say after` | `body\nafter`，rc 0 |
| `mir_finally_empty_catch_skipped` | 同上，中间 `catch { }` | `body\nafter`，rc 0 |
| `mir_finally_empty_catch_var_skipped` | 同上，中间 `catch (e) { }` | `body\nafter`，rc 0 |
| `mir_finally_after_catch_runs` | 同上，`catch { say "caught" }` | `body\nfin\nafter`，rc 0 |
| `mir_finally_throw_in_skipped_block` | `finally{throw "fin-throw"}` | `body\nafter`，rc 0，stderr 空 |
| `mir_finally_throw_after_catch_propagates` | `try{throw boom}catch{say caught}finally{throw fin-throw}` | `caught`，rc 1，stderr `fin-throw` |
| `mir_finally_runs_before_try_error` | `try{throw boom}finally{say fin}` | `fin`，rc 1，stderr `boom` |
| `mir_set_three_elements_three_nils` | `s=1,2,3` 循环 | `nil\nnil\nnil\nend`，rc 0 |
| `mir_set_two_elements_two_nils` | `s=1,2` 循环 | `nil\nnil\nend`，rc 0 |
| `mir_array_then_set_iteration` | 数组循环后接集合循环 | `1\n2\n3\nnil\nnil\nnil\nend`，rc 0 |
| `mir_dict_iteration_keys` | `d={"a":1,"b":2}` 循环 | `a\nb\nend`，rc 0 |
| `mir_interval_iteration_yields_nothing` | `p=[0,3)` 循环 | `set(R interval)\n0\nend`，rc 0 |

**它们第一次运行就是绿的，这一点要写清楚**：`mir-only-cases 230→242`、`mir-only-same-as-interpreter=242`，十二条全部 `mir=accepted|cc=ok|exec=same`。这不是「判据写松了」——写松的样子是「把红读成绿」；这十二个程序是先由只读扫量出来的，两侧读数逐字节相同，判据只是把**已经成立的等号**钉进可重复的报告里。等号的判据天然首绿；它的价值在第二次运行：只要解释器或路线动到这三处，十二条里必有一条红，而且读数里带 `bytes=` 与 `stdout-hex=`，红的时候能看出是哪一侧动了。

`ALLOCS_EXPECT` 里五行读数全是**先跑后抄**的：五个容器类判据一律 `allocs=1|bytes=256`（一块 8 槽池），finally 类一律 0|0。`STDERR_EXPECT`/`STDERR_EXCLUDE` 为两条抛错判据各加一行：需要 `fin-throw`/`boom` 出现，且 `boom` 前者不许漏出来——钉的是「哪一句错误留下来」，不是「有没有报错」。

### 四、读数

```text
IR-MIR-COVER|corpus=inline-cases:86|accepted=86|refused=0|differ=0|conserved=86|c-identical=0|c-diff=86|reasons=identical:0,mir-register-shape:82,mir-register-shape+mir-internal-globals:4|perturbed=5|perturb-c-changed-failures=0|perturb-stdout-changes=5|mir-only-cases=242|mir-only-same-as-interpreter=242
IR-MIR-VERDICT|PASS|tree=40013dba2e951d01e877b898d613e9dfd30c2a52|accepted=86|refused=0|differ=0|c-identical=0|c-diff=86|perturb-stdout-changes=5
IR-AOT-MIR|accepted=328|refused=0|same-as-interpreter=328|of=29|emittable=59|of-ops=67|not-emittable=8|corpus=inline-cases:86|mir-only-cases:242|beyond-ast-denominator=30|basis=mir-emitted-ops-union-over-the-corpus|coverage-claim=none
IR-AOT-MIR-LOWERING|handles=59|of=67|unknown=8|basis=distinct-case-labels-in-src/compilation/aot_mir.c's-op_regs-and-emit_one|table=59|emitter=59|reported=59|agree=yes
IR-AOT-MIR-VERDICT|PASS|tree=40013dba2e951d01e877b898d613e9dfd30c2a52|accepted=328|of=29|covered=29|missing=0|refused=0|same-as-interpreter=328|prelude-excluded=yes|beyond-ast-denominator=30
```

十二条判据的逐例读数（`IR-MIR-MIR-ONLY` 的 `rc=/bytes=/stdout-hex=` 字段）：

| 判据 | rc | bytes | stdout-hex |
| --- | --- | --- | --- |
| `mir_finally_only_skipped` | 0 | 11 | `626f64790a61667465720a` |
| `mir_finally_empty_catch_skipped` | 0 | 11 | `626f64790a61667465720a` |
| `mir_finally_empty_catch_var_skipped` | 0 | 11 | `626f64790a61667465720a` |
| `mir_finally_after_catch_runs` | 0 | 15 | `626f64790a66696e0a61667465720a` |
| `mir_finally_throw_in_skipped_block` | 0 | 11 | `626f64790a61667465720a` |
| `mir_finally_throw_after_catch_propagates` | 1 | 7 | `6361756768740a` |
| `mir_finally_runs_before_try_error` | 1 | 4 | `66696e0a` |
| `mir_set_three_elements_three_nils` | 0 | 16 | `6e696c0a6e696c0a6e696c0a656e640a` |
| `mir_set_two_elements_two_nils` | 0 | 12 | `6e696c0a6e696c0a656e640a` |
| `mir_array_then_set_iteration` | 0 | 22 | `310a320a330a6e696c0a6e696c0a6e696c0a656e640a` |
| `mir_dict_iteration_keys` | 0 | 8 | `610a620a656e640a` |
| `mir_interval_iteration_yields_nothing` | 0 | 22 | `736574285220696e74657276616c290a300a656e640a` |

`same-as-interpreter` 316→328，`differ` 仍是 0：没有任何一条原本一致的程序在这一格之后变得不一致。`of=29`、`covered=29`、`missing=0`、`beyond-ast-denominator=30` 全不动——这一格不换覆盖，只换**等号的证据形态**。

### 五、写这条之前量过的事

1. **空 catch 的三种写法都量了**：`catch { }`、`catch (e) { }`、以及只写注释的 catch 体——三者的 `handlerCount` 都是 0，finally 都不跑；catch 体里一句 `say` 是分水岭。`catch (e)` 只多绑一个名字，不改变条数。
2. **正常路径与异常路径分开量**：try 体 throw + 无 catch → finally 跑、原错误重抛（rc 1）；try 体不 throw + 无 catch → finally 不跑（rc 0）。同一个程序在不同路径上两种答案，判据必须挑一条路径钉——这一格钉的是正常路径，异常路径由另外两条钉住。
3. **throw 的位置矩阵量了**：finally 里（运行态）→ 上浮；finally 里（跳过态）→ 不可达；catch 体里 → 上浮；try 体里且 finally 会跑 → 清理先跑、原错误上浮；`break` 出 try 且 finally 内联了 throw → 被吞，但本路线按 `why=try-escapes` 拒译。最后一条量的是「为什么不能拿它当判据」。
4. **迭代四种容器都量了**：set（3 元素 3 nil、2 元素 2 nil）、array（元素）、dict（键）、`[0, 3)` 区间（零次，`len` 0）。`str()` 会被本路线按 `why=builtin-call` 拒译，所以判据里只出现 `say x` 与 `len`。
5. **`Z[0~3]` 有限域区间**：解释器印四行 nil，本路线按 `why=set-interval-domain` 拒译（它只发 R 区间）——能力边界，既不是同意也不是红。
6. **`R[0~3)` 显式域区间两侧不一致**：解释器 `set(0)`、`len` 0、`min` 0、`max` 3；本路线 `set(1)`、`len` 1、`min`/`max` 都答集合自己。这是**另一格**的真分歧，不写进本条判据，也不靠「取最后看的那一侧」圆掉——它和 `[0, 3)` 只差一个拼法，差的却是 `OP_SET_INTERVAL` 之后多出来的那条 `NEW_SET`。
7. **机制是读出来的，不是猜的**：解析器的 `handlerCount` 数语句、编译器的两次 `patch_r2`、`OP_TRY_START` 压 `ExcFrame`、`vm_throw` 存 `var_idx` 或按 `ignore` 静默、`OP_INDEX_GET` 的兜底分支、路线侧那行 `return mv_nil();`。每一条都先量两侧读数、再回到源码里找到发出它的那一行。
8. **ALLOCS 是量出来的**：五个容器类判据全 `allocs=1|bytes=256`（一块 8 槽池），finally 类全 0|0；两条 rc 1 的判据 `reported=no`，因为解释器只在 rc 0 时读那一行。

### 六、边界

1. **其一 的退场观察**：若解释器去掉 `tjo` 的第二次补丁（或让 finally-only 的 try 在正常路径上也落到 finally 开头），`mir_finally_only_skipped` 等四条会开始印 `fin` 而红，`mir_finally_throw_in_skipped_block` 会从 rc 0 变 rc 1——届时本路线必须同步，否则红就是 drift。这是这一格唯一「一句话就能推翻」的观察。
2. **其二 的退场观察**：若让「finally 里的 throw」在**运行态**消失（例如 `vm_throw` 在 finally 路径上只存不跳，或本路线在 finally 入口处再清一次 `mv_throwing`），`mir_finally_throw_after_catch_propagates` 会从 rc 1 变 rc 0、stderr 变空而红。反过来，若 `break` 出 try 被合法化并照抄泄漏帧，本路线会多接受一批程序，那时 §10.137 的 `try-escapes` 拒译与这条边界要一起重写。
3. **其三 的退场观察**：解释器若给 `OP_INDEX_GET` 补一个 `VAL_SET` 臂（第 i 个元素按枚举答），三条集合判据会印 `1 2 3` 而红；本路线必须补同一臂。若只改 `len` 的语义，nil 的**行数**会变，判据仍红。
4. **区间只担保它量过的部分**：`[0, 3)` 的同意是「len 0 ⇒ 零次」这一条，不是「区间的迭代语义正确」。`Z` 的拒绝是能力边界，`R[0~3)` 是另一格的真分歧。
5. **判据只钉 stdout 字节与退出码**：stderr 用包含式，因为本路线没有解释器的 `at ip=… frames=…` 账本；两条抛错判据钉的是「哪一句」而不是整行，`STDERR_EXCLUDE` 额外钉住「原错误不许冒出来」。
6. **不动 `src/`**：本格没有实现，也没有新 `why=`；`handles=59|of=67|unknown=8` 与 §10.158 相同，`of=29`、`covered=29`、`missing=0` 全不动。这是**同意**的形状，不是修复的形状。
7. **文档预算没有位移**：这一节不新增任何 `path:line` 引用（只引代码、只写文件名），所以 `check_line_refs` 的 `explicit delta` 仍是 `+12`；`citations` 的 1199 行与 `recon.*` 令牌一概没碰。
8. **与 §10.159 的关系**：兄弟格在另一个 worktree 里写同一段尾注，合并时两条尾部追加都保留；本格没有改 §10.159，也不依赖它。

## §10.161 继承裁定：闭包组已落地，`stream/mirval-closure` 是更早基线重做的重复（A 类第十九、二十项的后续）

### 一、来源与日期

接手会话（`session-f96c94eb` 因 bili preflight 502 死后由新会话接手），2026-11-12。交接包 §四.2 写「闭包第一格绿，但整格判据 `bc9862c` 还要 `OP_MAKE_FUNC` + `OP_CALL_VALUE` + `OP_LOAD_CAPTURE` + 转递捕获」，并要求在「真做」或「明写边界划掉」之间裁定。**按树重核后，这个前提与 `origin/main` 不符**，本节记录的是裁定与证据，不是一次实现。

### 二、裁定

**闭包组五条判据早已在 `origin/main` 上落齐**：§10.130（`c447c09`/`939c1dd`，无捕获函数值）与 §10.131（`1c9d664`，捕获读 + 高阶 + 返回并转递 + 零参调用堆字符串），日期 2026-11-09，两节都写明「五条判据全部落齐」。`stream/mirval-closure`（判据 `bc9862c`、实现 `71fd728`）是在旧基线 `2de6727` 上把同一组判据重做一遍 —— 它诞生时 `939c1dd` 还没进那棵树，所以「另外四条还红」在**那条旧分支上是真的**，在**今天的 main 上不成立**。处置：**不合并**（合并即回退，见三.3），分支保留在工作树 `.worktrees/mirval-closure` 作历史，不删。

### 三、证据（亲跑；第 2 条的计数表在 `564a147` 上复核重取）

1. `git show origin/main:tools/aot_mir.test.py | grep -c mir_closure_` = **11**：五条 `MIR_ONLY` 判据与它们的 `ALLOCS_EXPECT`/`STDERR_EXCLUDE` 行都在树上。
2. 捕获环境已经在实现里（`MV_FUN.ptr` 指向每次创建自己的槽数组）。**这条的计数方式必须写明，因为第一版把它写错了**：逐提交跑 `git show <sha>:src/compilation/aot_mir.c | grep -c mv_fun_cap`，得到下表（另一侧 `grep -c mv_capture` 一并列出，理由见下）：

   | commit | `mv_fun_cap` | `mv_capture` | 这棵树是什么 |
   |---|---|---|---|
   | `2de6727` | 0 | 0 | 闭包支的基点 |
   | `71fd728` | **0** | **0** | 闭包支 tip（本节的裁定对象） |
   | `c447c09` | 0 | 0 | §10.130 落地**之前** |
   | `939c1dd` | **9** | 4 | §10.130 落地（2026-11-09），捕获机制进 main |
   | `1c9d664` | 9 | 4 | §10.131 补全五条判据 |
   | `564a147` | **9** | 4 | 当前 main |

   全树交叉验证：`git grep -c mv_fun_cap 564a147 -- src` 只命中一个文件 —— `564a147:src/compilation/aot_mir.c:9`。
   **第一版这里的数写错过**：当时跑的是 `grep -c 'mv_fun_cap\|mv_capture'`（两个词合一）得到 13，却在正文里把它当成「`mv_fun_cap` 出现 13 次」写下来。13 是 9 + 4 的和，`mv_fun_cap` 本身在每一棵有它的树上都只有 **9**。结论不受影响（`0 vs 9` 已经足够），但数字必须能一条命令复现：一个复算到 9 的人如果照原文去数 13，会以为这条裁定是编的。
3. `git merge-base --is-ancestor 939c1dd 9dafc43` = 真；`939c1dd` 不是 `2de6727` 的祖先。上表第 2 行与第 5 行的 `0` 与 `9` ⇒ 合 `71fd728` 会删掉 main 已有的捕获支持。
4. 五条判据在 `363a3a1` 上亲测全绿：`IR-MIR-MIR-ONLY` 逐条 `mir=accepted|exec=same`，`ALLOCS` 读数 `2|64`（捕获读）、`1|32`（高阶）、`2|96`（返回并转递）、`2|40`（零参堆字符串）、`0|0`（无捕获），`ok=yes` 全命中。

### 四、边界

1. **本裁定不新增实现、不动 `src/`**：`handles=59|of=67|unknown=8` 与 §10.160 相同；`of=29|covered=29|missing=0` 不动。
2. **编号让位**：`.scratch/builtin-plan/PLAN.md` 的建议序列把 `size`/`count` 写作 §10.161，本节占用了这个号 ⇒ 该序列从本节的下一号起顺延一位；PLAN 是 scratch 里的建议，不是规范。
3. **没有剩余格**：闭包组的两格（A 类第十九、二十项）都已落在 §10.130/§10.131，不再从 A 类推进计划里保留条目。
4. **可复现**：裁定靠三段 `git show`/`merge-base`/`grep -c` 与 `tools/aot_mir.test.py --build build --tool build/aot-mir` 的 `IR-MIR-MIR-ONLY|case=mir_closure_*` 行；任何一条读数变化即本节需重写。
5. **一次更正记录（本节的数字被独立复核抓到过一次）**：三.2 初版把 `mv_fun_cap` 写成 13，实测是 9（13 是它与 `mv_capture` 的 4 之和，当时命令把两个词合成了一个正则却没在正文里说明）。已就地改成逐提交的计数表并写明命令，结论（`0 vs 9`）不变。这条留在这里而不是悄悄改掉：**裁定靠数字，数字就必须能一条命令复现**；一个复算到 9 的人若按原文去数 13，会以为整节裁定是编的。

## §10.162 内建 `size`/`count`：一条集合体、两条尾巴，区间两侧都 nil（A 类第三十五项）

### 一、来源与日期

接手会话，2026-11-12；§10.161 的裁定（不合并闭包旧支）之后的第一格实现。父 sha `a177c37`。`.scratch/builtin-plan/PLAN.md` 的建议序列把这一格写作 §10.161，§10.161 占用了那个号，本节按 §10.161 四.2 的让位规则改用 §10.162。

### 二、判据（先红）

两条 `MIR_ONLY` 判据：`mir_size_and_count_of_every_shape`（17 行输出）与 `mir_len_size_count_side_by_side`（8 行输出）。在 `a177c37` 上先红：两条都是 `mir=refused|why=builtin-call`（预扫的名字门只认 `len` 与 `list`），harness exit 1。解释器的逐行读数写在判据注释里，也就是这条臂要履约的合同：

1. **集合体上三者一致**：`[1, 2]`→2、`[]`→0、`"ab"`→2、`{a:1}`→1、`{}`→0、`1, 2`→2。
2. **区间是两侧同错的那一格**：`size([1~3])` 与 `count([1~3])` 都是 `nil`，而 `len([1~3])` 是 0 —— §10.129 给 len 的 0 不许漏进这两个名字。
3. **只有 size 保留强制转换尾**：`size(3)`=3、`size(-7)`=`nil`（解释器的 `if (n < 0)` 把负数结果读成「无答案」）、`size(2.5)`=2（向零截断）、`size(true)`/`size(nil)`=`nil`；`count(3)`=`nil`、`count(2.5)`=`nil`，但字符串仍在 `count` 的体内（`count("ab")`=2，不是 nil）。

第二条判据把三个名字并排放在一个操作数上：`len(x)`/`size(x)`/`count(x)` 对 `[1, 2]` 是 `2 2 2`，对 `[1~3]` 是 `0 nil nil`，`size(3)`=3 而 `count(3)`=`nil`。任何两个名字被别名化，至少一行会红。

### 三、实现

预扫与发射器两处名字门收下 `size`/`count`；运行时新增三个函数，`mv_len` 一字未动：

- `mv_region_len`：共享体，覆盖容器与字符串，对区间和非集合回答 0（不是数字，也不是 nil —— 两个调用者各自把这个 0 映射成 nil）。
- `mv_size`：共享体 + INT 自身 / FLOAT 向零截断 + `n < 0` 即 nil。
- `mv_count`：共享体，无强制转换尾（数字一律 nil）。

预扫的拒绝消息随之改成「this route emits len, list, size and count and no other builtin」，四个名字之外的调用仍按名字拒（`why=builtin-call`），零参调用仍先按名字拒（`builtin-stale-stack`）。

### 四、读数（亲跑）

- `IR-MIR-COVER`：`mir-only-cases=250|mir-only-same-as-interpreter=250`（判据落笔前是 250/248 —— 那两条正是被拒的两条），`inline-cases:86|accepted=86|refused=0|differ=0`，`c-diff=86|c-identical=0`，`perturb-stdout-changes=5`。
- `IR-AOT-MIR-VERDICT|PASS|accepted=336|of=29|covered=29|missing=0|refused=0|same-as-interpreter=336`（accepted 由 334 升到 336，就是这两条判据）。
- `ALLOCS` 两行按本格的形状钉住：`mir_size_and_count_of_every_shape` 是 `2|768`（数组、一对字典、集合三个字面量），`mir_len_size_count_side_by_side` 是 `1|256`（只有数组字面量）；两个答案本身在栈上算，不分配。
- `handles=59|of=67|unknown=8` **不动**：内建臂不是新 opcode 臂，`of=29|covered=29|missing=0` 也不动。

### 五、非对称注入（都在绿树上做，各只红一行）

1. **别名化**：把 `count` 发射成 `mv_size` ⇒ 两条判据都只红在 `count(3)` 那一行（印 3，解释器是 nil）。
2. **删掉尾上的 `n < 0`**：`size` 直接把 n 印出去 ⇒ 只红在 `size(-7)` 那一行（印 -7，解释器是 nil）。

两处都恢复后重跑，`250/250` 与 exit 0。

### 六、边界

1. **组件集合（`1, 2, Z[7~9]`）建不出来**：`OP_SET_INTERVAL` 的名字边界只认 `R`，所以解释器对组件集合枚举出的 5 在本路线**不可达**。这不是同意的形状，是能力边界——和 §10.159 的 `Z[7~9]` 同一条。
2. **区间的 nil 是本格量过的部分**：`*` 不等于区间的迭代语义正确；`R[0~3)` 的迭代仍归 §10.159 记录的真分歧，本格没有改它。
3. **判据钉 stdout 字节与退出码，外加 ALLOCS 行**；两个程序都不写 stderr，所以没有 `STDERR_EXCLUDE`。
4. **移动/陈旧容器句柄**：解释器对越界句柄给 `nil`，本路线也给 `nil`；这不是新 `why=`，也不改变预扫的接受面。
5. **文档预算没有位移**：本节不新增任何 `path:line` 引用（只引代码、只写文件名与 `§`），`check_line_refs` 的 `explicit delta` 仍是 `+12`；`citations` 的 1199 行不动。
6. **删除条件**：删掉 `size`/`count` 两个名字（预扫 + 发射器）与 `mv_region_len`/`mv_size`/`mv_count`，同时删掉两条判据；删一个名字而留另一条的尾会让判据红，所以两个名字不可单独删。

### 七、与兄弟格的关系

§10.159 的 argc 臂与 §10.160 的三处两侧同错都不动；三条尾部追加在同一文件末尾相邻，合并时都保留。

## §10.163 内建 `has`/`keys`：两套键规则必须并存，`keys` 交出新区域（A 类第三十六项）

### 一、来源与日期

接手会话，2026-11-12，§10.162 之后的第二格实现。父 sha `ea7b6b0`。编号承接 §10.161 的让位规则（PLAN 建议序列把这一格写作 §10.162）。

### 二、判据（先红）

两条 `MIR_ONLY` 判据，在 `ea7b6b0` 上都是 `mir=refused|why=builtin-call`，harness exit 1：

- `mir_has_uses_the_coercing_rule`（13 行输出）：`true false false false false false false false false` 之后跟两对。
- `mir_keys_returns_a_new_region`（10 行输出）：`[z, a, m]` 打印两次（中间夹一次 `m["z"] = 9`，证明顺序在覆盖后不变）、`keys(m)[0]`=z、`len(keys(m))`=3，然后 `[a, b]` / `[]` / `[]` / `[]` / `[]`，最后向返回数组写 `k[0] = "zzz"` 后再 `keys(d)` 仍是 `[a, b]`。

**两对是这一格的全部理由**：`n = {1: "x"}` 上 `has(n, true)` 是 **true** 而 `n[true]` 是 **nil**；`e = {true: "b"}` 上 `has(e, 1)` 是 **true** 而 `e[1]` 是 **nil**。`has` 用 `val_eq`（混合数值对提升为 double），字典读写用哈希门 `mv_key_eq`（§10.158），两条规则在这两对上结论相反。

### 三、实现

- 运行时新增 `mv_has(MirVal dict, MirVal key)`（非 `MV_DIC` 直接 `false`，线性扫对，比较器是 **`mv_eq`**）与 `mv_keys(MirVal a)`（非字典与空字典都答 `[]`，不是 nil；正常路径按插入序把键拷进一个**新**分配的区域）。
- 发射器：`keys` → `mv_keys(mvargs[asp-1])`；`has` → `mv_has(mvargs[asp-2], mvargs[asp-1])`（编译器先压容器后压键，容器在 `asp-2`）。
- 预扫：名字门扩到六个名字；**`has` 少于两个实参按名拒 `builtin-stale-stack`** —— 解释器的臂在栈不够深时返回 0（失败的 builtin），目标寄存器保持原值。实测三种形状：`say has(d)` 印字典、`w = "hello"` 之后印 `hello`、在 `len("abc", has(d))` 里印 0，所以这个形状没有「由表达式决定」的答案可抄，与 §10.159 的零参 `len`/`list` 同类。

### 四、读数（亲跑）

- `IR-MIR-COVER`：`mir-only-cases=252|mir-only-same-as-interpreter=252`（落笔前 252/250），`inline-cases:86|accepted=86|refused=0|differ=0`，`c-diff=86|c-identical=0`，`perturb-stdout-changes=5`。
- `IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=9|ok=9|expect=9` 不动：本格没有新增拒译判据，`has` 的单参拒绝是用 `aot-mir translate` 手验的（`IR-MIR-REFUSED|why=builtin-stale-stack`）。
- `ALLOCS`：`mir_has_uses_the_coercing_rule` = `2|768`（两个字典字面量，池翻一次；`has` 自身零分配），`mir_keys_returns_a_new_region` = `4|3840`（五次 `keys` 各建一个区域）。两行都是先量后写。
- `handles=59|of=67|unknown=8` 不动；`of=29|covered=29|missing=0` 不动。

### 五、非对称注入（绿树上各做一次，逐次恢复重建）

1. **`has` 借用 `mv_key_eq`**：只红那两对——`has(n, true)` 与 `has(e, 1)` 从 true 变 false，前九行一个字节都没动。这正是判据存在的理由。
2. **`mv_keys` 返回字典自己的区域**：红四行——两次 `keys(m)` 印成 `[z, 1, a, 2, m, 3]` / `[z, 9, a, 2, m, 3]`（对值也进了数组）、`len(keys(m))` 从 3 变 6、最后一行印成 `[zzz, 1, b, 2]`（往返回数组的写泄漏回了字典）；`ALLOCS` 同时从 `4|3840` 掉到 `3|1792`，是第二把锁。

（第一次注入写错了：`r` 先用后声明，症状是 `cc failed` 而不是答案不同——那是实现错误不是判据的读数，已重写并记录。）

### 六、边界

1. **`has` 的比较器与字典的键规则不同，是故意的**：这一节不统一它们。要统一得先改解释器（`val_eq` 或 `dict_hash_find`），而本路线的目标是逐字复现它。
2. **集合作键仍是既有债**（§10.158 只解决了 bool/int 那条）：`d[s] = 7` 之后 `d[s]` 是 `nil`、`has(d, s)` 是 `false`、`len(d)` 是 1 —— 本格没有动它，`mv_has` 走 `mv_eq` 而非 `mv_key_eq` 这一点与该债**无关**（`mv_eq` 有集合行、`mv_key_eq` 没有，所以 `has` 与 `d[s]` 在集合上也不一致，实测确认）。
3. **多参调用只读最后一个实参**：与 §10.159 的 `len`/`list` 同一条，本格没有新增机制（`has("a", d)` 等价于 `has(d)` 的顶层，实测 false）。
4. **`keys` 的顺序是插入序**，不是排序序；覆盖已有键不改位置——这正是判据里「打印两次、中间覆盖」那两行钉的。
5. **文档预算没有位移**：本节不新增 `path:line` 引用，`check_line_refs` 的 `explicit delta` 仍是 `+12`；`citations` 1199 行不动。
6. **删除条件**：两个名字与两个运行时函数必须同删；只删一个会让另一条的判据红。

## §10.164 内建 `str`：一次调用里两种浮点格式，答案是一个新堆字符串（A 类第三十七项）

### 一、来源与日期

接手会话，2026-11-12，§10.163 之后的第三格实现。父 sha `725d74d`。编号承接 §10.161 的让位规则。

### 二、判据（先红）

两条 `MIR_ONLY` 判据，在 `725d74d` 上都是 `mir=refused|why=builtin-call`：

- `mir_str_of_every_shape`（15 行）：`1 -7 2.5 0.10000000000000001 true nil ab (空行) [1, 2] [] {a: 1} {} <function> set(2) set(R interval)`。
- `mir_str_float_split_side_by_side`（10 行）：`0.1 / 0.10000000000000001 / 0.333333 / 0.33333333333333331 / 123456789.123457 / 123456789.12345679 / [0.1, 2] / 1 / 1 / 19`。

三条读数是这一格的全部理由：

1. **`str(0.1)` 是 `0.10000000000000001` 而 `say 0.1` 是 `0.1`**：`str` 用 `%.17g`，`say` 用 `value_to_string` 的短格式。
2. **`str([0.1, 2])` 是 `[0.1, 2]`**：容器臂即使从 `str` 进来也用**短**格式 —— 格式按**值的类型**选，不是按**调用**选。所以同一个函数里两种格式共存。
3. **`len(str(0.1))` = 19**：答案是真正的堆字符串，长度是字节数。

### 三、实现

运行时新增 `mv_str_of(MirVal a)`：INT 用 `%lld`、BOOL 用 `true`/`false`、STRING 复制自身（`strdup` 语义，**也分配**）、FLOAT 用 `%.17g`、其余（nil / 函数 / 容器）走 `mv_vts`（与 `say` 同一个格式化器）。所有臂都返回 `ival = 0` 的堆字符串，由 `atexit(mv_free_all)` 统一回收。

预扫与发射器的名字门扩到七个名字；`str()` 零参仍按名拒 `builtin-stale-stack`（实测：`x = 6` 后印 6、`y = "abcdef"` 后印 abcdef、函数后印 `<function>`，答案取残留寄存器）。

### 四、读数（亲跑）

- `IR-MIR-COVER`：`mir-only-cases=254|mir-only-same-as-interpreter=254`，`inline-cases:86|accepted=86|refused=0|differ=0`，`c-diff=86`，`perturb-stdout-changes=5`。
- `ALLOCS`：`mir_str_of_every_shape` = `17|864`、`mir_str_float_split_side_by_side` = `9|350`（每次调用一块，长度 = 答案字节数 + 1；先量后写）。
- `IR-MIR-MIR-ONLY-REFUSED-TOTAL|cases=9|ok=9|expect=9` 不变。
- `handles=59|of=67|unknown=8` 不动；`of=29|covered=29|missing=0` 不动。

### 五、非对称注入（绿树上各一次）

1. **FLOAT 改走 `nv_fmt_double`（短格式）**：两条判据的浮点行全红 —— `0.1` 取代 `0.10000000000000001`、`0.333333` 取代 `0.33333333333333331`、`123456789.123457` 取代 `123456789.12345679`、`len(str(0.1))` 从 19 变 3；非浮点行一个字节没动。
2. **字符串臂返回常驻指针而非拷贝**：**stdout 完全不变**，只有分配读数移动（`17|864 → 15|860`、`9|350 → 8|348`）。这一条是 ALLOCS 行存在的理由：字节相等看不见它。
   （第一次写这个注入时把返回语句放在 `mv_alloc` **之后**，属于死代码，判据照绿 —— 那不是判据的读数，是实现写错了，已重写并记录。）

### 六、边界

1. **`str(fn)` 是 `<function>`**：PLAN 的排序表里写的是 `nil`，实测是 `<function>`（`value_to_string` 的函数臂），本格照实测落地并在判据注释里写明「记录的是测量，不是预测」。
2. **零参调用仍按名拒**：与 §10.159/§10.162/§10.163 同一条，答案取残留寄存器，不由表达式决定。
3. **多参调用只读最后一个实参**：与既有各格同一条，本格没有新增机制。
4. **旧判据 `mir_builtin_call_with_arg` 换名不删**：它原来的程序是 `x = str(1)`，`str` 落地后它**正确地**变红（那条臂从拒绝侧走到了回答侧）。处置是把它指向仍不发射的 `trim`，就地写明原因 —— 判据测的类（带实参的内建调用按名拒）没变，只有它点的名字移动了。下一次 `trim` 落地时这张行要再往前挪一个名字。
5. **文档预算没有位移**：本节不新增 `path:line` 引用，`explicit delta` 仍是 `+12`；`citations` 1199 行不动。
6. **删除条件**：臂、预扫名字与两条判据同删；只删浮点臂会让 `str(0.1)` 等于 `say 0.1`，只删 `mv_vts` 臂会让容器行全红。

## §10.165 内建 `sum`：折、两个 nil 边界，以及它raise的两种方式（A 类第三十八项）

### 一、来源与日期

接手会话，2026-11-12，§10.164 之后的第四格实现。父 sha `37fb09e`。编号承接 §10.161 的让位规则。

### 二、判据（先红）

三条 `MIR_ONLY` 判据，在 `37fb09e` 上前两条都是 `mir=refused|why=builtin-call`（mir-only 256/254）：

- `mir_sum_fold_and_nil_boundaries`（10 行）：`6 0 3.5 3.5 nil nil nil nil 3.5 nil`。
- `mir_sum_non_numeric_throws`：`say "before"` 之后 `sum([1, "a", 2])`，stdout 恰好 `before\n`、rc 1、stderr 含 `[exception] uncaught: sum: non-numeric element`，且 `STDERR_EXCLUDE` 禁止 `numeric_overflow`。
- `mir_sum_bool_element_throws`（**第三条是注入实验之后补的**，见五.2）：`say "bool"` 之后 `sum([1, true])`，stdout `bool\n`、rc 1、同一条消息。

三件事各有一个失败面：折（int 与 float 的转换时机、集合走 `mv_list` 的枚举）、nil 边界（非容器 `3`/`"ab"`/`nil`、以及枚举不出来的 R 区间）、**抛**（第一个会 raise 的内建）。

### 三、实现

`mv_sum(MirVal a)`：ARRAY 自己就是源；SET 先走 `mv_list`（**复用**同一套枚举，所以区间的 nil 是顺着来的，不是第二条规则）；其余 nil。累加器是一个 int64（`__builtin_add_overflow`）加一个并行 double —— 这正是 `[1, 2.5]` 与 `[2.5, 1]` 都答 3.5 而都不答整数的原因。元素规则只有 `MV_INT`/`MV_FLT`，其他一律 `mv_throw(mv_str("sum: non-numeric element"))`；int 累加器回绕则 `mv_throw(mv_str("numeric_overflow"))`。

预扫与发射器的名字门扩到八个名字；`sum()` 零参仍按名拒 `builtin-stale-stack`。

### 四、读数（亲跑）

- `IR-MIR-COVER`：`mir-only-cases=257|mir-only-same-as-interpreter=257`，`inline-cases:86|accepted=86|refused=0|differ=0`，`c-diff=86`，`perturb-stdout-changes=5`。
- `ALLOCS`：`mir_sum_fold_and_nil_boundaries` = `4|1794`（程序自己的字面量；`sum` 零分配），两条抛错案例报 `reported=no|why=rc=1`（在运行中停下，没有 sweep 行 —— 与其他抛错判据同一处理）。
- `IR-MIR-STDERR|case=mir_sum_non_numeric_throws|…|ok=yes`：需要的那句在、禁止的那句不在。
- `handles=59|of=67|unknown=8` 不动；`of=29|covered=29|missing=0` 不动。

### 五、非对称注入（绿树上各一次）

1. **把 throw 臂换成 `return mv_nil()`**：三件同时红 —— rc 从 1 变 0、stdout 多印一行 `nil`、stderr 那句话消失（`missing=(...)`）。`sum` 的形状正是「三个通道一起看」才有判别力。
2. **把 `MV_BOO` 混进数字臂**：前两条判据**完全绿**，只有第三条（`mir_sum_bool_element_throws`）红 —— 印 2、rc 0、消息缺失。**这条判据是这次注入之后补的**：先有实现、先跑注入，才发现「让布尔落进整数臂」这个最合理的错法在前两条判据里看不见。这是「注入不是验收仪式的装饰」的一次实例。

### 六、边界

1. **组件集合（`1, 2, Z[7~9]`）建不出来**：与 §10.162 同一条边界，`sum` 的集合臂照 `mv_list` 的答案走，因此那条边界自动继承。
2. **集合整数半边的窄化照抄**：`mv_list` 把每个 int 窄成 `(int)` 再发射（那条 note 记了实测），所以 `s = 5000000000, 1` 的 `sum` 加的是窄化后的值 —— 与解释器一致，是照抄不是修正。
3. **`numeric_overflow` 是第二种 raise**：`sum([9223372036854775807, 1])` 在解释器上抛它（实测）。本格没有单列判据（`STDERR_EXCLUDE` 反向钉住它不出现在第一条抛错案例里），留作下一格的候选。
4. **零参与多参调用**：同既有各格，零参按名拒、多参读最后一个实参。
5. **文档预算没有位移**：本节不新增 `path:line` 引用，`explicit delta` 仍是 `+12`；`citations` 1199 行不动。
6. **删除条件**：臂、预扫名字与三条判据同删；只删 throw 臂会让静默 nil 复活，只放宽元素规则会让布尔通过。

## §10.166 内建 `remove`/`push`/`pop`：这条路线第一次改动区域长度（A 类第三十九项）

### 一、来源与日期

接手会话，2026-11-12，§10.165 之后的第五格实现。父 sha `be023b4`。

### 二、判据（先红）

五条 `MIR_ONLY`，在 `be023b4` 上四条 `mir=refused|why=builtin-call`、一条对照先绿（mir-only 262/258）：

| 判据 | oracle |
|---|---|
| `mir_remove_both_containers` | `true 1 2 true [10, 30] 10 30 2 false×5` |
| `mir_remove_aliases_two_rounds` | `true 1 1 true 0 0 3 1 true 0 false 0` |
| `mir_remove_then_grow` | `true 5 1 7 1` |
| `mir_grow_without_remove`（对照） | `5 2 7 2` |
| `mir_push_and_pop` | `1 [1,2,3] 3 1 [1,2] 1 1 3 [1,2] nil nil nil` |

判据里钉住三个只有字节比较看得见的细节：数组 remove 是**移位**而非清 nil（`[10, 30]`、`a[1]`=30，长度都是 2，只有 `say a` 分得开）；`push` **恒返回 1**，连 `push(nil,1)`/`push(5,1)` 也返回 1 且什么都不写（后面紧跟 `say b` 仍 `[1,2]` 证明变量真的没变）；第二轮 remove 才真正踩到 `mv_arr_live` 的多跳（第一轮后旧句柄是 head-to-head）。

### 三、实现

`mv_remove(obj, key)`：DICT 用 `mv_key_eq` 扫对、成对左移、头槽 `n-2`、尾两槽清 nil；ARRAY 下标走 `mv_index_i32`（非整数抛 `type_mismatch`）但**负数答 false 不抛**（写抛、remove 不抛，解释器 `:527-546` 的判据是 `idx >= 0 && idx < a->count`）；其余（含缺键、空容器、nil、字符串、集合）答 false。
`mv_push(obj, x)`：**恒返回 1**；数组经 `mv_arr_set(obj, mv_int(n), x)` 追加（`i == n` 正是扩展分支，复用增长臂而非另写一份）；非数组什么都不写、仍返回 1。
`mv_pop(obj)`：数组最后一个元素，头槽 `n-1`，尾槽清 nil；空数组/非数组答 nil。

`remove`/`push` 少于两参按名拒 `builtin-stale-stack`（`say remove({"a": 1})` 实测印字典自身）。

### 四、设计决策：缩短**不回收**池槽

`mv_remove`/`mv_pop` 只改头槽，`mv_arrused` 不回退。放弃的那条替代方案（区域恰好坐在前沿时把槽还回去）**不是更冒险，而是不可达**：增长臂的前沿判据是 `iv + n == mv_arrused`，而删完之后 `iv + n' < mv_arrused` 是构造性的（`n' = n - 2`）⇒ **回收能生效的时机与它需要生效的时机互补**。一个任何被接受程序都走不到的分支不是节省，是未测的代码。

代价是可测量的并已被钉住：缩短过的区域下一次写**必然**拷到前沿。解释器那边释放 `2 * sizeof(Value)` 的 `used_mem`（`src/vm/vm.c:1244`），本路线没有这本账、退出时整池 sweep —— 差异是记账，不是泄漏（五条判据全部 `live=0`）。

### 五、读数，以及**我的 ALLOCS 预测五条错三条**

真实读数（亲跑）：`mir_remove_both_containers` = `2|768`、`mir_remove_aliases_two_rounds` = `1|256`、`mir_remove_then_grow` = `2|768`、`mir_grow_without_remove` = `2|768`、`mir_push_and_pop` = `1|256`。

**预测错在哪（本轮唯一的实质发现）**：我把"前沿拷贝"算成「+1 块、+256 字节」，因为池首次用到 8 槽时正是 256 字节。**但池不是按块增量分配的**：`mv_arr_need` 一次性分配**双倍大小的整块**并把活的前缀拷进去，所以一次把前沿推过 8 槽的拷贝是**一块 512 字节**，而不是 256；而**放得下的拷贝成本为零**。判别对（同一程序加一行）实测：

```
d={"a":1}; d["z"]=5; a=[7, 8]; say d["z"]; say a[0]          -> allocs=1|bytes=256
同一程序，在写之前插一句 remove(d,"a")                          -> allocs=2|bytes=768   (256+512)
```

所以拷贝真实存在且可观测，错的是我算它的价钱的算术。

**一条不舒服的结论，照写不删**：按实测数字，`mir_remove_then_grow` 与它的对照组**读数相同**（`2|768`），因此**那一行不再能区分"拷了"与"没拷"**。它仍然钉住 `live=0` 与总量。一个看不见自己写作理由的判据，值得说出来。

**后续（同一轮，独立复核之后）**：这条结论没有停在散文里，两处都补了：
1. **判别对升格成两条具名判据**：`mir_remove_pushes_frontier_control` = `1|256`、`mir_remove_pushes_frontier` = `2|768`（两者只差一句 `say remove(d, "a")`），已进 corpus 并被 pin 钉住 —— 只写在正文里的对照会在下一次重钉时被当成叙述丢掉，而被 pin 钉住的东西才叫证据。
2. **pin 新增 `allocs-scope` 块**，把每一行"能钉什么"写在数字旁边：`mir_remove_then_grow` 与 `mir_grow_without_remove` 是 `totals+live`（总量与无泄漏，**不含**"是否拷贝"），`mir_remove_pushes_frontier` 是 `totals+live+the-copy`。这样按**名字**引用判据的人不会得出读数没支持的结论。
3. **块粒度写进判据注释**：池首次用到 8 槽 256 字节，超出时**双倍**分配整块并拷活前缀（`mv_arr_need`）⇒ 放得下的拷贝零成本、跨过边界的拷贝是一块 **512** 字节。这是我算错的那笔价钱，也是下一个人会算错的同一笔。

### 六、非对称注入（各只红自己那一行）

1. **数组 remove 清 nil 而不移位**：只红 `mir_remove_both_containers`，且只红 `[10, nil]` 与 `a[1]`=nil 两处（`len` 两边都是 2 —— 这就是程序里放 `say a` 的理由）。
2. **头槽单位错（`n-2` → `n-1`）**：只红两条字典判据 —— `len(d)` 0→1、1→2，且 `mir_remove_then_grow` 的 `d["z"]` 读成 nil。

两次都恢复并重跑到 `mir-only 262/262`、exit 0。

### 七、边界

1. **字典没有 push**：解释器里没有 `push(dict, x)` 这种东西，本路线不发明一个 —— 字典实参什么都不写、仍答 1。
2. **哈希索引不存在**：解释器删完重哈希（`src/vm/vm.c:1246`），本路线用线性扫描，没有可重建的东西；可观测的那半（存活对的顺序）两边相同。
3. **零参/单参**：按名拒，理由与前面各格同一条。
4. **文档预算**：本节不新增 `path:line` 引用，`explicit delta` 仍 `+12`；`citations` 1199 行不动。
5. **删除条件**：三个臂、预扫名字与五条判据同删；只删收缩臂会留下"能长不能缩"的路线，正是这批判据写作时所针对的状态。

## §10.167 内建 `substr`：第一个在池里长出字节的内建（A 类第四十项）

### 一、来源与日期

接手会话，2026-11-12，§10.166 之第六格实现。父 sha `8be1c2e`。按 PLAN 行 10 单独成格（不与 `split`/`join` 合捆：三者共用一个新东西，但判据形态不同，合捆会让 ALLOCS 的算术无处对照）。

### 二、判据（先红）

两条 `MIR_ONLY`，在 `8be1c2e` 上都是 `mir=refused|why=builtin-call`（mir-only 266/264）。只有 `tools/aot_mir.test.py` 变了 —— `src/`、`docs/` 未动。

`mir_substr_clamps_every_boundary` 的十一行 oracle（逐字节）：`bcd / ef / （空） / cdef / （空） / ab / （空） / ab / （空） / C3 A9 / 3`。

四条 clamp 规则各自有一行钉住，其中两条是这一格的理由：

1. **负 length 不从尾部数**：`("abcdef", 3, -1)` 是**空串**。看起来对称的那条规则是错的。
2. **字节级、不认 UTF-8**：`("héllo", 1, 2)` 返回两个原始字节 `C3 A9`（碰巧是一个字符）。一个按 rune 切的实现会在同一行给出别的答案。

其余：负 start 从尾部数（`-2` → `ef`）、起点越界给空串（不是 nil、不是整串）、超长截断（`(2,99)` → `cdef`）、负 start 越过开头 clamp 到 0（`-99` → `ab`）、空源、零长、`len(substr(...))`=3 证明答案是真字符串而非源串视图。

### 三、实现

`mv_substr(src, startv, lenv)`：按解释器 `src/runtime/runtime_posix.c:548-572` 的四条规则与**顺序**落地，`start`/`len` 是 `ival` 读（非强制转换，与解释器同）。答案一律是 `mv_alloc(len+1)` 的堆字符串。名字门扩到十个；`substr` 少于三参按名拒 `builtin-stale-stack`（实测两参时返回 `1`，是残留寄存器）。

### 四、读数（亲跑）

- `mir_substr_clamps_every_boundary` = `11|29`、`mir_substr_allocates_its_answer` = `3|10`；`IR-MIR-COVER|mir-only-cases=266|mir-only-same-as-interpreter=266`；cover `accepted 350→352`。
- **两个 ALLOCS 数字都是「先推导、后验证」，且这次预测成立**：十一次调用的 clamp 后长度 3,2,0,4,0,2,0,2,0,2,3 和为 18，加每答案一字节 = 29；字符串字面量是常驻的（`mv_str`，`ival=1`）不分配，所以十一个答案就是全部分配。上一格五条错三条的教训是"池不是按块增量分配"，这一格先把算术写下来再看读数。

### 五、非对称注入（各只红自己那个通道）

1. **负 length 改成镜像 start 规则**（从尾部数）：两条判据都红，`("abcdef",3,-1)` 从空串变 `def`；**分配读数完全不动**。
2. **按源串长度分配**（而非 clamp 后长度）：stdout **逐字节不变**，只有 ALLOCS 的字节数动（29→67、10→21）。这正是本格 ALLOCS 判据存在的理由：字节相等看不见它。

两次都恢复并重跑到 `266/266`、exit 0。

### 六、边界

1. **非字符串源读成空串**（`sv.type == VAL_STRING && sv.sval ? ... : ""`），所以 `substr(123, 0, 1)` 是空串不是抛错。照抄，且进判据。
2. **start/len 是 `ival` 读**：字符串或布尔落在那两个位置会读成 0。不钉 —— 没有被接受程序能达到可观测的形状，而没有判据的规则只是声明。
3. **两参调用按名拒**：答案取残留寄存器（实测 `substr("abc", 1)` 印 `1`），与前面各格同类。
4. **文档预算**：本节不新增 `path:line` 引用，`explicit delta` 仍 `+12`；`citations` 1199 行不动。
5. **删除条件**：臂、预扫名字与两条判据同删；只删堆答案（返回指进源串的指针）对十一行里十行都印同样字节，只有 `len(...)` 与 ALLOCS 行看得见。

### 七、与 PLAN 的关系

PLAN 行 10/11/12 把 `substr`/`split`/`join` 分成三格，本节按此执行。三者共享"堆字符串的产生"这一新东西，但判据形态不同：`substr` 是 `字符串 → 新字符串`（本格）、`split` 是 `字符串 → 字符串数组`（下一个）、`join` 是 `容器 → 新字符串`。分开跑，ALLOCS 的算术才能一条条对得上。

## §10.168 内建 `split`：第一个造出「字符串数组」的内建（A 类第四十一项）

### 一、来源与日期

接手会话，2026-11-12，§10.167 之后的第七格实现。父 sha `2215f78`。按 PLAN 行 11 单独成格。

### 二、判据（先红）

两条 `MIR_ONLY`，在 `2215f78` 上都是 `mir=refused|why=builtin-call`（mir-only 268/266）。只有 `tools/aot_mir.test.py` 变了。

`mir_split_edges_and_the_trailing_empty` 的十三行 oracle（逐字节）：`[a, b, c] / [] / [abc] / [] / 2 / [, a] / [a, b] / [] / [] / 1 / [, ] / [a, , b] / 0`。

**先量后写**，因为 PLAN 表只给了四个形状，而定夺形状的四个都不在表里：

| 形状 | 答案 | 意义 |
|---|---|---|
| `("a,b,", ",")` | `[a, b]` | **尾空丢弃** |
| `(",a", ",")` | `[, a]` | **首空保留** |
| `("a::b", "::")` | `[a, b]` | 分隔符按**整串**匹配（按字符切会得到 `[a,,b]`）|
| `(123, ",")` | `[]` | 非字符串源读成空串，不是 `["123"]` |

首尾不对称不是缺陷：解释器的循环是 `while (*p)` + `strstr`，位置 0 命中时仍 push 一个空片，而尾部分隔符只是把 `p` 推到 NUL、循环结束。

**打印相同而实质不同的一对**：`split(",", ",")` 印 `[]` 但 **len 是 1**（一个空字符串元素），`split("", ",")` 印 `[]` 且 **len 是 0**（真的空数组）。两者 `say` 出来是同样的三个字节，**只有 `len()` 分得开** —— 所以程序里 `say` 与 `len` 并排。

**两个方向相反的边界放在同一程序里**：空**源**给 `[]`，空**分隔符**给 `[整串]`（解释器把空分隔符替换成单个空格，而 `"abc"` 没有空格）。

### 三、实现

`mv_split(src, sepv)`：两条 pass（先数片数再一遍布局），分隔符按整串 `strstr` 匹配，空分隔符先替换成单空格，非字符串源/分隔符读成空串；**每个元素独占一块**（`mv_alloc(blen+1)`），区域一次性布局。名字门扩到十一个；`split` 少于两参按名拒 `builtin-stale-stack`。

### 四、读数，以及**预测 6|144 两半都错了**

实测：`mir_split_allocates_new_bytes_per_element` = `4|262`、`mir_split_edges_and_the_trailing_empty` = `20|1822`。`IR-MIR-COVER|mir-only-cases=268|mir-only-same-as-interpreter=268`；cover `accepted 352→354`。

预测错在两处，**两处都是我的错，不是实现的错**：

1. **我把解释器的两次 `strdup`（源与分隔符）算进了分配**。本路线不需要它们 —— 它读的是别人递进来的值、一个也不拥有 —— 所以 6 块变 4 块。
2. **我把数组区域按"它需要的槽数"计价**（4 槽 = 128 字节）。池分配的是**整块**：首次 8 槽 = 256 字节，所以 144 变 262。

**第 2 条与上一格的前沿拷贝是同一个错误的第二次**：把"增量"当"整块"。写在这里而不是悄悄改数，因为它一天之内在同一个文件里发生了两次。

判据本身没受影响：它仍然能抓住"共享字节而非拷贝"（0 个元素块）与"按字符而非按片分配"。

### 五、非对称注入

1. **只在计数那一遍多算一个尾空**（于是头槽与实体不一致）：`len(split("a,b,", ","))` 从 2 变 3，另有两行移动，并印出 `<unknown>`（元素没有字节）。
2. **元素改为共享源指针**（不拷贝）：两条判据都红成 `rc=-11`（段错误）—— 元素会指向一个已经消失的栈缓冲区。记下来是因为"共享更省"正是下一个人会伸手去做的优化。

两次都恢复并重跑到 `268/268`、exit 0。

### 六、边界

1. **自重叠分隔符**（`split("aaa","aa")`）跟随 `strstr` 的从左到右**不重叠**推进。不钉 —— 没有被接受程序能在不改动已钉行的前提下让它可观测。
2. **两次 `strdup` 不存在于本路线**：这是分配的实质差异，也是本格预测错的一半来源。
3. **单参按名拒**：答案取残留寄存器（实测 `split("a,b")` 把压入的实参印回来）。
4. **文档预算**：本节不新增 `path:line` 引用，`explicit delta` 仍 `+12`；`citations` 1199 行不动。
5. **删除条件**：臂、预扫名字与两条判据同删。只删尾空规则会印 `[a, b, ]` 并红一行；只删首空规则红另一行 —— 两者可分离，所以都钉。

### 七、与 PLAN 的关系

PLAN 行 10/11/12 三格（`substr`/`split`/`join`）共享"堆字符串的产生"，但判据形态不同：`substr` 是 `字符串 → 新字符串`（§10.167）、`split` 是 `字符串 → 字符串数组`（本节）、`join` 是 `容器 → 新字符串`（下一格）。分开跑的好处在这一格具体兑现了：预测错的时候，我能确定错的是"元素分配"这一件事，而不是三件事里的哪一件。

## §10.169 内建 `join`：容器变成一个新字符串，元素字节永不当格式串（A 类第四十二项）

### 一、来源与日期

接手会话，2026-11-12，§10.168 之后的第八格实现。父 sha `69b6cfb`。PLAN 行 12，单独成格。

### 二、判据（先红）

两条 `MIR_ONLY`，在 `69b6cfb` 上都是 `mir=refused|why=builtin-call`（mir-only 270/268）。只有 `tools/aot_mir.test.py` 变了。

**六条形状预先量定**（PLAN 表只给了四条）：

| 形状 | 答案 | 意义 |
|---|---|---|
| `join([1,2,3])` | `123` | 默认分隔符是**空串** |
| `join([1,2.5,true,nil],"-")` | `1-2.5-true-nil` | 布尔/nil 印各自的词，不跳过、不给空 |
| `join([[1,2],3],"-")` | `[1, 2]-3` | 嵌套容器**序列化**，不拒绝 |
| `join(nil\|"abc"\|{"a":1},"-")` | `""` | 非数组源给**空串**（非 nil、非抛错）|
| `join(["%s","%d"],"-")` | `%s-%d` | 字面输出：**不对元素字节施加 printf** |
| `join([1.0, 0.1+0.2, 1/3],"-")` | `1-0.3-0.333333` | **短格式** |

**最后一条是这一格存在的理由**：`0.1+0.2` 经 `join` 印 `0.3`，经 `str` 印 `0.30000000000000004`；`1/3` 经 `join` 印 `0.333333`，经 `str` 印 `0.33333333333333331`。判据把**同一个表达式**的两种写法并排印，否则只在整数字面上测是看不出差别的。

### 三、实现

`mv_join(arrv, has_sep, sepv)`：**最后一个参数是字符串才是分隔符**（这条读自解释器，`join([1,2,3])` 不是错误也不是残留寄存器）；非数组源给空串；每个元素走 `mv_vts`（短格式），**不对元素字节用 printf**；嵌套容器由 `mv_vts` 的深度规则序列化。名字门扩到十二个；零参按名拒 `builtin-stale-buffer`。

### 四、读数，以及**我的第一版实现过分配 5.7 倍**

实测：`mir_join_every_shape_and_the_default_separator` = `15|3895`、`mir_join_float_format_and_percent` = `8|838`。`IR-MIR-COVER|mir-only-cases=270|mir-only-same-as-interpreter=270`；cover `accepted 354→356`。

**第一版**按"每个元素预留 1024 字节"来定答案缓冲区大小，于是同一个程序读出 **`15|22294`** —— 一个五字节的答案花了将近 22 KB。改成**两遍**（先格式化进临时缓冲并累加长度，再按精确大小分配），同一程序变 `15|3895`，而 **stdout 逐字节不变**。两个数字都记下来：前一个就是"看起来没问题"的价钱，而 `IR-MIR-ALLOCS` 是这个文件里唯一能看见它的读数。

**我公布的预测 `2|262` 是范围错误，不是被推翻的论断**：它说的是**一次** `join` 调用，而读数覆盖十一行（每行有自己的数组字面量与答案块，池还随区域堆叠翻倍）。`blocks(n) = n<=8 ? 256 : 256*2^d` 这个闭式成立并保留。

### 五、非对称注入

1. **浮点元素改用 `%.17g`**（而非 `mv_vts`）：浮点那条判据红（并且 abort —— 更宽的文字撑破了定尺寸那一遍）。**这正是 `str` 与 `join` 的格式分裂能被抓到的唯一方式**。
2. **把元素字节当 printf 格式串**：`%s-%d` 变成 `(null`，只红那一行。一个基于 printf 的实现在这个文件的**其他每一行**都能通过。

两次都恢复并重跑到 `270/270`、exit 0。

### 六、边界

1. **嵌套容器序列化而非拒绝**：解释器答它，本路线的 `mv_vts` 已经会答它，拒绝会是"装成边界的能力倒退"。这是一个**真选择**，记下来因为它曾被认真考虑过。
2. **解释器每元素 256 字节的 part 缓冲不复制**：本路线的 `mv_vts` 拿到的缓冲按整个答案定尺寸，所以单个超过 255 字节的元素在这里完整格式化、在解释器会被截断。不钉 —— 没有被接受程序能达到那个长度，而给别人的限制写判据是替对方记录。
3. **单参是默认分隔符形式**（`join([1,2,3])` 印 `123`），所以**发射而不拒**；零参才按名拒。
4. **文档预算**：本节不新增 `path:line` 引用，`explicit delta` 仍 `+12`；`citations` 1199 行不动。
5. **删除条件**：臂、预扫名字与两条判据同删。把元素格式化从 `mv_vts` 换成 `%.17g` 只红两条浮点行；用格式串打印只红 `%s`/`%d` 那一行。

### 七、PLAN 行 10/11/12 三格的收束

`substr`（§10.167）是 `字符串 → 新字符串`、`split`（§10.168）是 `字符串 → 字符串数组`、`join`（本节）是 `容器 → 新字符串`。三格分开跑的决定在这一格再次兑现：过分配是**只有 join 这一格才有**的形状（它要把多个片段拼成一个答案），如果三格捆在一起，`split` 的"每元素一块"与本格的"一块装全部"会在同一张 ALLOCS 表里互相掩盖。

## §10.170 `chars`/`ord`/`chr`：字节模型，钉在能承载它的通道上（A 类第四十三项）

### 一、来源与日期

接手会话，2026-11-12，§10.169 之后的第九格实现。父 sha `e033fdb`。PLAN 行 13。

### 二、判据（先红），以及**这一格真正的风险是通道**

两条 `MIR_ONLY`，在 `e033fdb` 上都是 `mir=refused|why=builtin-call`（mir-only 272/270）。

**先量通道**：`say chr(200)` 印出的字节在任何终端上都不是文字，而一个**按文本**比较 stdout 的判据分不出"字节对"与"字节被吞"。所以模型劈成两条程序：

- `mir_bytes_ascii_roundtrip`：**只用可打印 ASCII**，stdout 是文本，逐字节比较名副其实。
- `mir_bytes_high_bytes_via_ord`：**一个高字节都不印**，全部经 `ord()` 读回 —— 整数通道。实测 `ord(chr(128))=128`、`ord(chr(200))=200`、`ord(chr(255))=255`：字节确实在，只是印不出来。

**按字节而非码点，两个方向都量了**：`len(chars("é"))` 是 **2**、`ord("é")` 是 **195**（0xC3，第一个字节）—— 不是 1 和 233。只量一条，一个按 rune 切的实现可能碰巧通过；量这一对则不能。

**非字符串源**（全实测）：`chars(nil)`/`chars(123)` 是 `[]`、`ord(nil)`/`ord(123)`/`ord("")` 是 `0`、`chr("65")`/`chr(1.5)`/`chr(true)` 都是空串（参数按 `ival` 读，这几种都不携带可用的整数）。

**NUL 一族每条都带 `len`**（兄弟格挣来的规矩，这里适用两次）：`chr(0)` 印空**且** `len` 为 0；`chr(-1)` 是裸 0xFF（len 1）、`chr(257)` 是 0x01（len 1）⇒ "`& 0xff` 然后遇 NUL 即止"这条规则**只有 len 看得见**。

### 三、实现

`mv_chars` 按字节切、每元素一块 2 字节；`mv_ord` 答**第一个字节**（非字符串/空串给 0）；`mv_chr` 把 `(char)(n & 0xff)` 写进两字节缓冲再当 C 字符串交出去 —— 这正是 `chr(0)`/`chr(256)` 是空串而 `chr(-1)`/`chr(257)` 是长度为 1 的裸字节的原因。名字门扩到十五个；三者零参均按名拒 `builtin-stale-stack`。

### 四、读数，以及**分母现在写进了判据**

实测：`mir_bytes_ascii_roundtrip` = `22|1830`、`mir_bytes_high_bytes_via_ord` = `12|281`。`IR-MIR-COVER|mir-only-cases=272|mir-only-same-as-interpreter=272`；cover `accepted 356→358`。

**分母是这一行的一部分**：22 块摊在 **21 次调用**上，不是"每次一块再加一块" —— 它是「每个字节一块答案字符串」**加上**「区域堆过 8、16、32、64 槽时的四次池翻倍」。把任一个总数读成"每调用的成本"就是同一个错误第四次出现。

### 五、非对称注入（各只红自己那条轴）

1. **`ord` 改答码点**：两条程序的 `ord` 行全红（65→233、97→233、128/255/200→233），**所有 `chars` 行一个字节没动**。
2. **`chars` 改按码点数**：只红 `len(chars("é"))` 从 2 变 1，在这条 ASCII 程序里，**其余行全不动**。

两次都恢复并重跑到 `272/272`、exit 0。

### 六、边界

1. **带嵌入 NUL 的源在本路线不可能存在**：每个字符串都是 NUL 结尾的 C 文本，`chr(0)` 是空串而非单字节值。`chars` 因此永远看不到 NUL 之后的字节 —— 与解释器自己的 `strlen` 驱动循环一致。
2. **`chr` 对大整数静默回绕**（`n & 0xff`），负数同理。两条都进判据，且只能用 `len` 钉，因为印出来是空的。
3. **三者的零参按名拒**，与前面各格同一条。
4. **文档预算**：本节不新增 `path:line` 引用，`explicit delta` 仍 `+12`；`citations` 1199 行不动。
5. **删除条件**：三个臂、预扫名字与两条判据同删。rune 化的 `chars` 红 `len(chars("é"))`；码点化的 `ord` 红 `ord("é")`；去掉 `& 0xff` 红 `chr(-1)` 与 `chr(257)` 的 len。

### 七、与兄弟格的交叉

`mir_bytes_high_bytes_via_ord` 里有一行 `join(chars(join(["a","b"],"")), "-")` —— **§10.169 与本节在这里交汇**：`chars` 把串切成字节、`join` 再拼回去，所以一个能往返的字节被证明了两次。这是复核建议放进判据的，采纳。

`chars` 与 §10.168 的 `split` 是同一族的镜像（都造字符串数组）：`split` 每片一块、`chars` 每字节一块，两格的块粒度闭式互相校验通过（`blocks(n) = n<=8 ? 256 : 256*2^d`）。

## §10.171 `replace`/`index`/`startswith`/`endswith`：四个纯扫描，以及 PLAN 表里被命名错的强制转换（A 类第四十四项）

### 一、来源与日期

接手会话，2026-11-12，§10.170 之后的第十格实现。父 sha `38eeeed`。PLAN 行 16。

### 二、判据（先红）

两条 `MIR_ONLY`，在 `38eeeed` 上都是 `mir=refused|why=builtin-call`（mir-only 274/272）：
- `mir_scan_empty_needle_and_overlap`（二十次调用，逐字节比）；
- `mir_replace_allocates_only_its_answer`（**只含 replace**，理由见四）。

### 三、**PLAN 的命名是错的，实测推翻**

PLAN 表把 `startswith("abc", 3) = true` 记成"非字符串参数**经 `say` 路径转成字符串**"。读 `src/runtime/runtime_posix.c:573-640` 并实测：这四个都按 `v.type == VAL_STRING && v.sval ? v.sval : ""` 读参数 —— 非字符串参数是**空串**，**从不**经过 `say` 的格式化路径。所以：

```
startswith("abc", 3) 是 true      （比较 0 个字节 → 恒真）
startswith("abc", 4) 也是 true    （与 3 同因，不是长度检查）
startswith("abc", 0) 是 true
index("abc", 3)      是 0         （空针在 0 处命中）
```

把 `3` 当**下标**读的人会预测长度检查，三条全错；判据把 `3`、`4`、`0` 都印出来，其中 **`4` 是分水岭** —— 当作下标它就越界。

### 四、实测钉住的六个形状

1. **空针族**：`index("abc","")`=0（`strstr` 返回串本身）；`replace("abc","","-")` **原样返回** `abc`（在进循环前就返回，**没有它会无限循环**）；`startswith`/`endswith` 空针为 true。
2. **重叠规则**：`replace("aaa","aa","b")` = **`ba`**（从左到右**非重叠**，命中后跳过整根针）；`replace("abcabc","bc","X")` = `aXaX` 再钉同一规则。
3. **字节偏移不是字符偏移**（与 §10.170 一致）：`index("héllo","l")` = **3**（不是 2）；`replace("hé","é","e")` = `he`（针是那两个字节）。
4. **非字符串源/补丁也读成空串**：`index(nil,"a")`=-1、`startswith(nil,"")`=true、`replace(nil,"a","b")`=空串、**`replace("abc","a",nil)` = `bc`** —— nil 补丁是空串，即**删除**，不是错误也不是字面 `nil`。这一条最容易漏量且改变 replace 的含义。
5. **首次命中**：`index("abcabc","bc")` = 1（不是 4）。
6. **分配轴单列**：三个扫描答 int/bool、**一条 `mv_alloc` 都没有**；`replace` 只在算好的长度上分配一次答案。

### 五、实现

共享读取器 `mv_sstr`（非字符串→空串）；`mv_index`=`strstr`；`mv_starts`=`strncmp(s,p,strlen(p))`；`mv_ends`=`memcmp(s+sl-pl,p,pl)`；`mv_replace` **先数命中**、按精确长度分配一次（**不做增长中的 scratch**），空针提前返回源串，命中后 `i += ol`（非重叠）。名字门扩到十九个；四者少于两参时按名拒 `builtin-argc`（探针读栈顶、源在其下，短调用会读残值）。

### 六、读数与分母

实测 `mir_scan_empty_needle_and_overlap` = `7|23`、`mir_replace_allocates_only_its_answer` = `2|9`。

**分母必须一起读**：replace-only 那行是干净的一行 —— **恰好 2 次分配对应恰好 2 个答案**（`aXaX` 5 字节 + 原样返回的 `aaa` 4 字节 = 9），所以 replace 只分配答案、别无他物。扫描行的 7 块**不是扫描**：每个到达某个臂的**字符串常量**各是一次 `mv_str` 拷贝（本路线会拷贝字面量），而这条程序三次到达 `"abc"`、还有 `"aaa"/"aa"/"b"`、`"abcabc"/"bc"/"X"`、`"héllo"/"l"/"hé"/"é"` —— **四个扫描本身加零**，所以块数跟着字面量走、不跟着调用数走。

`IR-MIR-COVER|mir-only-cases=274|mir-only-same-as-interpreter=274`；cover `accepted 358→360`。

### 七、非对称注入

1. **非字符串针当下标读**（`k >= 0 && k <= strlen(s)`）：**只红 `startswith("abc",4)`** 一行（interp `true` / mir `false`），其余各行一个字节不动。
2. **replace 空针交出源指针而非拷贝**：**stdout 逐字节不变**，只有 ALLOCS 动（`7|23→5|15`、`2|9→1|5`）—— 这正是"replace 单列一行"存在的意义。

两次恢复后重跑 `274/274`、exit 0。

### 八、边界

1. **空针的"原样返回"是本路线自己拷贝一份**（解释器在空针分支 `push_string(s)` 后 `free(s)`，语义相同：返回的是**新**串）。差异只在那一份拷贝是否可见 —— 判据用 ALLOCS 钉住它。
2. **大源串的容量**按精确长度一次分配；解释器的 `realloc` 倍增在这里没有对应物，**不钉**（接受程序达不到那个尺寸）。
3. **文档预算**：本节不新增 `path:line` 引用，`citations` 1199 行不动。
4. **删除条件**：四个臂、名字门与两条判据同删。强制转换改成"转字符串再比"红 `4`/`0` 两行；replace 从头重扫红 `ba` 与 `aXaX`；偏移改成字符计数红 `3` 与 `he`。

## §10.172 `upper`/`lower`/`trim`：ASCII 折叠，以及一个集合问题（A 类第四十五项）

### 一、来源与日期

接手会话，2026-11-12，§10.171 之后的第十一格实现。父 sha `891578f`（含主人账号的 `9cb892f` 测试超时修复）。PLAN 行 17。

### 二、判据（先红），以及写判据时量出来的两件事

三条 `MIR_ONLY`，在 `891578f` 上都是 `mir=refused|why=builtin-call`（mir-only 277/274）：`mir_case_ascii_only_and_nonstring`、`mir_trim_charset_is_an_enumeration`、`mir_trim_allocates_only_when_it_cuts`。

**量出来的第一件：`\v` 与 `\f` 不是这套语言的转义。** `len("\v x")` 是 **4** —— 反斜杠、v、空格、x —— 所以用转义写集合问题，测的是四个**可打印**字符，`isspace` 实现会因错误的原因通过。判据改用 `chr(11)`/`chr(12)` 放入真字节（实测 3 字节）。

**量出来的第二件**：判据字符串里放真实制表符/换行会让 Verim 字面量跨行而解析失败，harness 报 `refused` 却没有 REFUSED 行。程序文本里写成字面反斜杠序列。

### 三、实测的六条

1. **非字符串源**（上格实测是"读空串"，这格**重新量而不是沿用**）：`upper(nil)`、`upper(123)`、`trim(nil)`、`trim(123)` 全是空串。
2. **只折 ASCII、按字节**：`upper("é")` 不变（两字节都不动，**不是 locale 大小写**）；`upper("àbc")` = `àBC`（0xC3 0xA0 引导字节留着、ASCII 尾折叠）；`lower("ÀBC")` = `Àbc`。rune 或 locale 实现会红这三行。
3. **trim 的字符集是枚举 `" \t\r\n"`，不是 `isspace`**：`trim(" x ")`=1、`trim("\t\n x \r")`=1、**`trim(chr(11)+" x")`=3**、**`trim(chr(12)+" x")`=3**。后两条是分水岭 —— `isspace` 实现通过前两条、红后两条。
4. **只削两端**：`trim("a b  ")` = `a b`（内部空格不动）。
5. **空/全空白**：`trim("")` 与 `trim("   ")` 都是空串（len 0）。
6. **分配轴单列**：`trim("abc")`（无可削）**0 块**、`trim(" abc ")` 1 块 4 字节。

### 四、实现

`mv_case_of(a, up)` 只折 `a-z`/`A-Z`、逐字节；`mv_upper`/`mv_lower` 是它的两极。`mv_trim` 削两端四个枚举字符；**没有可削时把原字节直接交回去**（新 `MirVal` 指向同一片字节），有可削才分配精确长度。名字门扩到二十二个；三者零参按名拒 `builtin-stale-stack`。

### 五、读数与分母

实测 `mir_case_ascii_only_and_nonstring` = `10|31`、`mir_trim_charset_is_an_enumeration` = `10|24`、`mir_trim_allocates_only_when_it_cuts` = `1|4`。`IR-MIR-COVER|mir-only-cases=277|mir-only-same-as-interpreter=277`；cover `accepted 360→363`。

**分母一起读**：case 行是 9 次调用、每次 upper/lower 各分配一个答案；trim 行是 9 次调用 10 块 —— **两条无操作 trim 若也拷贝会变成 11 块**。**最干净的是第三行**：1 次切削对应 1 次分配（3+NUL=4 字节），`trim("abc")` **一块都不分配** —— 这条读数存在的意义就是抓住"无操作路径上多了拷贝"。

**记账义务已履行**：`mir_builtin_call_with_arg` 从 `trim` 前移到 **`sqrt`**（trim 已发射；§10.164 写下的义务是"锚必须跟着前沿走，既不许删也不许放宽"）。`type` 不是可调用内建（语法关键字），实测 `sqrt` 给 `why=builtin-call`。

### 六、非对称注入

1. **trim 改用 `isspace` 式范围**（`c==' ' || (c>=0x09 && c<=0x0D)`）：**只红两行** —— `3→1`、`3→1`（`\v`/`\f` 被削），其余各行一个字节不动。
2. **trim 无操作也拷贝**：**stdout 逐字节不变**，只有 ALLOCS 动（`1|4→2|8`）—— 正是"无操作 0 块"这条存在的意义。

两次恢复后重跑 `277/277`、exit 0。

### 七、边界

1. **无操作时交回源字节是本路线自己的选择**；解释器用 `malloc(n+1)` + `memcpy` 总会拷贝（`runtime_posix.c:625-631`）。语义相同（都返回新串），差异只在一次拷贝是否可见 —— 判据用 ALLOCS 钉住它，**并明写"这不是优化承诺，是代码现在的行为"**。
2. **`chr(11)`/`chr(12)` 经 `+` 拼串**：本路线支持字符串 `+`（该程序已被接受并逐字节对齐）。
3. **文档预算**：本节不新增 `path:line` 引用，`citations` 1199 行不动。
4. **删除条件**：三个臂、名字门与三条判据同删；`isspace` 化红 `\v`/`\f` 两行，locale 折叠红三行带重音字符，无操作拷贝红分配行。

## §10.173 `shl`/`shr`/`bit_and`/`bit_or`/`bit_xor`/`bit_not`：一个符号性与一个"不强制转换"（A 类第四十六项）

### 一、来源与日期

接手会话，2026-11-12，§10.172 之后的第十二格实现。父 sha `c753708`（§10.172 的合并 pin）。PLAN 行 23/24（score ~10，当前最高分）。

### 二、判据（先红），以及写判据时量出来的两处 PLAN 之误

三条 `MIR_ONLY`，在 `c753708` 上都是 `mir=refused|why=builtin-call`（mir-only 280/277）：`mir_bits_values_and_width`、`mir_shl_overflow_is_an_error`、`mir_bitwise_noninteger_throws`。抛错两条分开成程序，因为未捕获异常会结束程序 —— 一个程序只能演示一次抛错。

**PLAN 之误一：`shl(5,34)` 的答案。** PLAN 行 23 的例子写 `8589934592`（那是 `1<<33`）；实测 **`85899345920`**，十倍之差。照 PLAN 抄的判据会因与实现无关的原因先红。

**PLAN 之误二（方向相反）：位运算不强制转换。** 上两格实测"非字符串源读成空串"，那是**扫描/变换**的规则；`bit_and(1.9,3)`、`bit_and(-1.9,3)`、`bit_and("5",3)`、`bit_and(nil,3)`、`bit_and(true,3)` 与 `bit_not(nil)`、`bit_not("x")` **全部抛** `<name>: expected two integers` / `bit_not: expected an integer`。这条规则**不沿用到位运算**，正因为如此它是量出来的而不是假设的。

### 三、实测的七组

1. **右移的符号性（分水岭）**：`shr(-1,1)`=-1、`shr(-8,2)`=-2、`shr(-5,1)`=-3 ⇒ **算术右移**。逻辑右移实现只会红这三行，正数行全过。
2. **移位计数越界饱和**（`shr` 停、`shl` 不）：`shr(1,64)`=0、`shr(1,65)`=0、`shr(-8,65)`=-1（负→-1，正→0，**不绕回、不抛**）。
3. **左移溢出是错误不是绕回**：`shl(1,63)`、`shl(1,64)`、`shl(1,65)`、`shl(3,62)` 全抛 `numeric_overflow`；`shl(1,62)`=4611686018427387904、`shl(5,34)`=85899345920、`shl(-5,2)`=-20。
4. **计数本身必须是非负 INT**：`shl(1,-1)`、`shr(1,-1)` 也抛 `numeric_overflow`（与溢出同一条消息，值得知道）。
5. **补码**：`bit_not(3)`=-4、`bit_not(0)`=-1、`bit_not(-1)`=0；`bit_and(5,3)`=1、`bit_and(-5,3)`=3、`bit_or(4,2)`=6、`bit_xor(-1,5)`=-6、`bit_xor(6,3)`=5。
6. **打印形态**：大整数按精确十进制打印（`4611686018427387904`、`85899345920`），与 `mv_int` 的宽度一致 —— `say` 没有把答案折成浮点。
7. **分配轴是一个零**：六个内建全答整数 ⇒ 整个程序 **0 块 0 字节**，显式钉 `0|0`，抓"把答案绕成中间字符串 / 走 `mv_str`"的实现。

### 四、实现

`mv_bit_msg(name, tail)` 拼抛错条串；`mv_bit_two` 严格检查两个实参都是 `MV_INT` 否则抛；`mv_bitnot` 单独检查一个。`mv_shl` 用 `__builtin_mul_overflow` 逐步加倍——**检查过的乘法**，溢出即抛 `numeric_overflow`，负数计数直接抛；`mv_shr` 先按 `>=64` 饱和（正 0 / 负 -1），再用 `x >> y` 的算术移位。名字门扩到二十八个；`shl/shr/bit_and/bit_or/bit_xor` 的 `argc<2` 拒 `builtin-argc`，`bit_not` 的 `argc<1` 拒 `builtin-stale-stack`（与既有内建的按名拒法一致）。

### 五、读数与分母

`IR-MIR-COVER|mir-only-cases=280|mir-only-same-as-interpreter=280`；`IR-MIR-MIR-ONLY|case=mir_bits_values_and_width|mir=accepted|cc=ok|exec=same|bytes=71`；`IR-MIR-ALLOCS|allocs=0|freed=0|live=0|bytes=0|ok=yes`；两条抛错程序 `rc=1|stdout="before\n"`、stderr 逐字对齐。cover `accepted 363→369`（+6 个名字）。

**分母一起读**：值程序 17 行 = 3 算术右移 + 3 饱和 + 3 左移（含 1 个溢出边界内）+ 8 个按位；两条抛错程序各 1 行 ——`shl` 溢出与 `bit_and` 非整数是两条**不同的消息**，一条实现不能藏在另一条后面。

### 六、非对称注入

1. **`shr` 改逻辑右移**（`(unsigned long long)x >> y`）：**只红值程序**（`279/280`），负三行变 `9223372036854775807`/`4611686018427387902`/`9223372036854775805`，正数行与饱和行一个字节不动。
2. **位运算改向零截断**（`MV_FLT` 实参先 `mv_int`）：**只红 `mir_bitwise_noninteger_throws`**（`279/280`），stdout 从 `"before\n"` 变 `"before\n1\n"`、rc 1→0，其余两条程序不动。

两次恢复后重跑 `280/280`、exit 0。

### 七、边界

1. **`shr`/`shl` 计数语义是本路线按实测实现的**（`>=64` 饱和、`<0` 抛），不依赖 C 的移位未定义行为；`shl` 用检查过的乘法而不是 `x << y`，所以 `shl(1,63)` 是抛错而不是实现定义。
2. **`bit_not` 没有两参形态**，`argc` 门因此与其余五个不同（`builtin-stale-stack` vs `builtin-argc`）。
3. **文档预算**：本节不新增 `path:line` 引用，`citations` 1199 行不动。
4. **删除条件**：六个臂、名字门与三条判据同删；逻辑右移红负三行，绕回左移红溢出两条，截断强制转换红非整数程序。

### 八、同一轮里碰到的第二件事：`check_release_tags` 的红灯不是本格的，但它挡住了推送

位运算这支合并树上 gate 第 19 阶段红：`docs/STATUS.md:42` 等四处记的 `v0.5.1 -> 4e444dd` 被判为漂移，而 `v0.5.1` 仍指向 `4e444dd` —— **错的是比较，不是记录**：git 自动缩写的**打印宽度**随对象数从 7 涨到 8（`--short` 给 `4e444dde`），工具却在逐字符比较。干净主树 `c753708` 上单跑同一工具同样红，确认与本格无关。

裁定（主人会话）＝改工具、不改归档。落地在 `stream/release-tags-prefix`，**先写自测并跑出红**（2/4 cells，两条预期失败），再把比较从"字符串"换成"把记录解析成 commit"（`rev-parse --verify --quiet <recorded>^{commit}` 再比 tag 指向的 commit）；四条 cell 覆盖：不同 commit 红、歧义缩写必须**被 `rev-parse` 拒绝而不是悄悄加长**、README 基线落后于最新 tag 红、打印宽度变大但记录仍解析到同一 commit **绿**。注册进 ctest 后 `EXP_CTEST` 203→204、`docs/BOARD.md` 的 `recon.ctest-count` 同步（两个数与本节的注册在同一轮里改，否则下一个人的 gate 会红在一个不点名他的地方，这正是 `EXP_CTEST` 那行注释写下的规矩）。

**边界**：这只治"记录比今天打印的宽/窄但指向同一 commit"这一类；tag 真被移动、记录解析不到、歧义三种仍是红。**没有改动任何归档文档** —— `AUDIT.md` 与 `RELEASE_*.md` 记的是写下时为真的事，仓库规矩是归档不回改。
## §10.174 `bool`/`float`：两个全函数，以及读者会猜错的两行（A 类第四十七项）

### 一、来源与日期

接手会话，2026-11-12，§10.173 之后的下一格实现。父 sha `c753708`（与 §10.173 的 `2a143f0f` 分叉于同一父；合并时按 §10.173 在前、§10.174 在后的顺序保留两侧）。PLAN 行 25 与行 26（`bool`/`float`；`int`/`round` 按主人批注拆到下一格——`int` 的容器臂会泄漏容器自己的索引，见六）。

### 二、判据（先红）

两条 `MIR_ONLY`，在 `c753708` 上都是 `mir=refused|why=builtin-call`：

```
IR-MIR-MIR-ONLY|case=mir_bool_truthiness_and_zero_float|mir=refused|why=builtin-call
IR-MIR-MIR-ONLY|case=mir_float_converts_scalars_and_zeroes_containers|mir=refused|why=builtin-call
IR-MIR-COVER|...|mir-only-cases=279|mir-only-same-as-interpreter=277
```

**两个都是全函数**（每种形状都有答案、没有抛错行），所以整格的形状问题就是 stdout 本身；这使它们与 §10.171–§10.173 的"先红在名字上、绿在形状上"不同——红只说明名字没发射，绿要逐行对齐。

**写判据时量出来的两行**（读者会猜错的）：
- **空串为真**：这套语言没有 empty-is-false 规则，`bool("")` 是 `true`。
- **`-0.0` 为假**：浮点臂是"数值零"测试而非符号测试，`bool(-0.0)` 是 `false`。

### 三、实测的十五 + 十一行

`bool`（每行一个 `say`）：`1 true / 0 false / 2 true / -1 true / nil false / 1.5 true / 0.0 false / -0.0 false / true true / "" true / "x" true / f true / [] true / {} true / [1, 3) true`。

`float`：`3→3 / true→1 / nil→0 / "2.5"→2.5 / "x"→0 / []→0 / {}→0 / [1, 3)→0 / f→0 / 9223372036854775807→9.2233720368547758e+18 / 2.5→2.5`。

**最后倒数第二行是显示问题**：那是 `say` 的浮点格式（`nv_fmt_double`，与 §10.164 记下的 `str` 用 `%.17g` 相区分）。用 `str` 的格式会印 `9223372036854775808`；判据用 stdout 钉住它。

### 四、实现

`mv_bool`：NIL 假、INT 非零、FLOAT 非零（含 `-0.0`）、BOO 原样（`false` 保持 `false`）、其余真。`mv_float`：INT 加宽、FLOAT 原样、STRING `atof`、BOO 1/0、NIL/函数/容器 0。名字门扩到二十四个；emitter 各加一条分支；两者零参仍按名拒 `builtin-stale-stack`。

### 五、读数与分母

`mir-only-cases=279|mir-only-same-as-interpreter=279`、`IR-MIR-VERDICT|PASS`；ALLOCS 两案各 `1|256` —— 那一块是程序里的容器字面量（`[]` 或 `{}`），**先量后写**，不用默认 `0|0`：写 0 会让"把真值结果装箱"或"把 double 过一遍缓冲区"的实现多出的那一块看不见。

### 六、边界

1. **容器臂的 0 不是索引**：`float([])` 是 0；而 `int([])` 是 **1**、`int({})` 是 **1**、`int([1, 3))` 是 **22** —— 那是解释器把值当自己的表下标泄漏出来的数。两个转换**不共享**容器臂，这正是 `int` 必须单独一格的理由；读者若假设共享，`int([])` 会被印成 0。
2. **`bool` 的 BOO 臂与默认臂是两个答案**：注入 (B) 把默认臂答成假时，六行动而 `bool(true)` 不动 —— 这行就是"BOO 臂存在"的读数。
3. **显示层不是本节的证明对象**：浮点显示走共享 `nv_fmt_double`，本节只钉一行大整数；断言的是"答案类型对了之后显示自然对"。
4. **删除条件**：两个 helper、名字门两处、emitter 两分支与两条判据同删；注入 (A) 红 `float("2.5")` 一行，注入 (B) 红六行。

### 七、非对称注入

1. `float` 的字符串臂改答 0（丢掉 `atof`）：**只红一行**（`float("2.5")` 2.5→0），其余十行逐字节不变。
2. `mv_bool` 的默认臂改答假：**红六行**（`bool("")`、`bool("x")`、`bool(f)`、`bool([])`、`bool({})`、`bool([1, 3))`），INT/FLOAT 四行不动。两次恢复后重跑 `279/279`、exit 0。

---

## §10.175 `round`：一个身份函数，以及 `int` 为何被写成边界（A 类第四十八项）

### 一、来源与日期

接手会话，2026-11-13，§10.174 之后的下一格。父 sha `7ddd346c`（§10.174 的合并 pin）。PLAN 行 25 与行 26 把 `bool`/`float`/`int`/`round` 放在同一格，主人批注把 `int` 的容器臂拆出来——本节的裁量是：`round` 落地，`int` 写成边界。

### 二、判据（先红）

一条 `MIR_ONLY`（`mir_round_is_identity`，十五行）加两条 `MIR_ONLY_REFUSE`（`mir_round_zero_arg_is_not_lowered` 期望 `builtin-stale-stack`、`mir_round_multi_arg_is_not_lowered` 期望 `builtin-argc`）。在 `7ddd346c` 上：

    IR-MIR-MIR-ONLY|case=mir_round_is_identity|mir=refused|why=builtin-call
    IR-MIR-MIR-ONLY-REFUSED|case=mir_round_zero_arg_is_not_lowered|mir=refused|why=builtin-call|expect=builtin-stale-stack|ok=no
    IR-MIR-MIR-ONLY-REFUSED|case=mir_round_multi_arg_is_not_lowered|mir=refused|why=builtin-call|expect=builtin-argc|ok=no
    IR-MIR-COVER|...|mir-only-cases=283|mir-only-same-as-interpreter=282

两条拒绝判据在红态报的是**错的原因**（`builtin-call` 而不是名字），正是判据偏要测的那一面：允许“因为它没实现”作借口，等于没有测拒绝的形状。

### 三、实测：`round` 不圆整，它是身份

解释器逐行实测（`build/inimerse --no-mods run`）：`round(2.5)` 印 `2.5`、`round(2.6)` 印 `2.6`、`round(-2.5)` 印 `-2.5`、`round(0.5)` 印 `0.5`、`round(1)` 印 `1`、`round("2.5")` 印 `2.5`、`round("x")` 印 `x`、`round(true)` 印 `true`、`round(nil)` 印 `nil`、`round([1,2])` 印 `[1, 2]`、`round({"a":1})` 印 `{a: 1}`、`round(f)` 印 `<function>`。**没有 half-away，也没有 banker**——两条“看起来该进位”的行（2.5 与 0.5）是判据的锚。

### 四、实现

`src/compilation/aot_mir.c`：名字门两处加 `round`；emitter 臂 `v[r1] = mvargs[asp-1]`（不装箱、不格式化、不分配）；prescan 的两道 arity 门——`argc < 1` 答 `builtin-stale-stack`（解释器交回目标寄存器旧值，实测 `x = 6\nsay round()` 印 6），`argc > 1` 答 `builtin-argc`（位置相关的栈残值，实测 `(1,2)`→1、`(1,2,3)`→2、`("a",2)`→nil、`(nil,2)`→nil）。

### 五、非对称注入

1. emitter 改答 `mv_int(0)`：**十五行全红**（身份一次看全）。
2. `argc > 1` 门关掉：多参链被**接受**（判据要求拒绝），且它对 `round(1, 2)` 会印 2 而解释器印 1——**被接受本身就是失败**，不是输出错。恢复后重跑 `283/283`、exit 0。

### 六、边界（未落地）：`int`

`int` 的标量臂本来可发射（`int(2.5)`=2 向零截断、`int("4.9")`=4、`int("x")`=0、`int(nil)`=0、`int(true)`=1、`int(1.0e30)` 饱和到 `9223372036854775807`、`int(-1.0e30)` 饱和到 `-9223372036854775808`）。**但容器臂答的是容器自己的创建序号**：实测 `int([1,2])`=1、`a=[1,2]` 后 `int(a)`=2、`int({})`=3 —— 那是解释器运行时的区域编号，本路线没有等价的编号空间（字面量、`list()`、`keys()` 等各自建区域，顺序一挪就全错）。名字门只能按名拒，不能半发射，所以 `int` **整名保持拒绝**，这一条写在这里而不是藏在实现里。

### 七、文档预算与删除条件

本节不新增 `path:line` 引用，`citations` 1199 行不动。删除条件：`round` 臂与两道 arity 门、三条判据同删；身份改成任何圆整会红那两条“看起来该进位”的行，关掉 arity 门会红两条拒绝判据。
