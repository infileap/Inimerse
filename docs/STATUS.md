# Inimerse / Infiverse 状态、路线图与版本裁定

> **本文件是项目状态、路线图和版本裁定的唯一权威来源。**
> 语言与 API 事实见 [API.md](API.md)；需求分析与治理裁决见 [REQUIREMENTS_ANALYSIS.md](REQUIREMENTS_ANALYSIS.md)；历史版本说明与已废止文档见 [archive/](archive/README.md)。
>
> 口径核实日期：**2026-10-01**。核实时使用的方法与原始输出见 §2。

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

## 2. 当前基线（2026-10-03 更新测试计数到 104；其余各项为 2026-10-01 实测）

| 项目 | 实测值 | 证据 |
| --- | --- | --- |
| 版本 | `0.5.0` | `CMakeLists.txt:8`；git tag `v0.5.0` |
| 干净构建 | configure / build 均退出码 0，**35 warnings / 0 error** | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| 全量测试 | **104 / 104 真通过**，无 `WILL_FAIL` 记账项；`-j12` 高争用单轮约 13 s | `ctest --test-dir build -j$(nproc)` |
| 高争用稳定性 | §2.9 的端口窗口**已关闭**：hub 一律用内核分配端口（`--port 0 --http-port 0`），不再由 harness 猜号。`tools/ports_race_probe.py` 实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」为 **6**，证明竞态在 harness 而非引擎）。本行原来的「80 轮失败 1 轮」是**内核分配之前**的数字，未复测 | `python3 tools/ports_race_probe.py`；`for i in $(seq 80); do ctest --test-dir build -j12; done` |
| 编译器诊断 | **35 条 warning，0 error**（§2.5 修复后干净重建日志） | 干净重建日志 |
| 引擎代码 | `src/` 101 个 `.c` + 49 个 `.h`，合计 48,753 行（`.c` 单独 46,120 行） | `find src -name '*.c' -o -name '*.h' \| xargs cat \| wc -l` |
| 内建函数注册 | 531 处 `vm_register_builtin*` 调用 | `grep -rho 'vm_register_builtin[a-z_]*' src \| wc -l` |
| 自举编译器 | `selfhost/` 48 个 `.im`、2,316 行 | `find selfhost -name '*.im'` |
| 脚本规模 | 仓库 316 个 `.im`（根目录 148 个为回归测试） | `find . -name '*.im' -not -path './build/*'` |
| 测试注册 | `CMakeLists.txt` 中 **104** 个 `add_test(`（原有 101 + 自举 codegen 等价 1 + AOT 原生 1 + 坏函数索引 1 = **104**，见 §10.1/§10.2/§10.6/§10.7/§10.10/§10.11/§10.17/§10.33/§10.40） | — |
| 工具 | `tools/` 98 个条目 | `ls tools \| wc -l` |
| 性能（`sum(1..2000000)`） | 解释器 88 ms = 1.00x · AOT 打包 = 与解释器**等同**（分布中位 **0.98x**） · Wasm MVP 58 ms = 1.51x | [SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) |

### 2.1 复现命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4      # 期望 100% tests passed, 0 failed out of 104
node tools/node_suites/run_all.js                    # JS 侧协议套件 11 个（不在 CTest 内）
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
| 绑定生成与扫描工具 | CTest `bindgen_regression` / `scan_tools_regression`；`tools/bindgen.py`、`tools/cpp_scan.py`、`tools/python_scan.py`、`tools/migrate_report.py` |
| 语言语法糖（`?`、`\|>`、`case try`、lambda、`>>`、集合推导、`?.`、`??`） | CTest `pipeline_runtime` / `try_finally_runtime` / `lambda_*_runtime` / `case_*_runtime` / `null_coalesce_runtime` / `optional_member_runtime` |
| POSIX 运行时对等 | CTest `posix_core_api_runtime` / `posix_runtime_parity` / `headless_probe` |
| 协议与回放（CRP 会话、节点发现、权威交接、断线重连、确定性回放、包签名） | CTest `crp_session_flow_regression` / `node_discovery_regression` / `lease_handoff_regression` / `reconnect_generation_regression` / `replay_closure_regression` / `verse_pack_regression` / `hub_dist_regression` / `protocol_regression` |
| 经济域与可审计结算 | CTest `economy_domain_regression`；`tools/economy_domain.test.py` |

**声称与现状的对照** —— `docs/archive/RELEASE_0.5.0.md` 的下列表述：前两条**今天仍不成立**，后三条**已被后续交付推翻**（保留在此是为了让「当初为什么不成立」有据可查）。行号按 2026-10-02 的该文件正文计 —— 更正横幅插在文件顶部，此前的行号全部漂移了 9~11 行：

| 该文件声称 | 核实结果 |
| --- | --- |
| 「AOT compilation … outperform the interpreter by at least 2x」（第 52 行） | 实测与解释器**等同**——12 次试验中位 **0.98x**（0.85x..1.33x），与「解释器对自己」的对照区间（0.77x..1.36x）**12/12 重叠**，1.09x 只是其中一个噪声样本。AOT 通道是**打包**通道（引擎副本 + 嵌入规范化字节码），复用同一个 C 解释器，不自生成原生代码。原文 `SELFHOST_BENCHMARK.md` 已自述「不满足 ≥2x 目标」。 |
| 「WebAssembly output … supporting SIMD optimizations and WebAssembly GC」（第 47 行） | **它把三件不同的事写在了一起，`wasm-simd-gc`（`3fadc8b`）逐件给了结论，见 §10.16**：**heaps 已做**（模块自己的线性内存 arena，确定性引用计数回收，耗尽显式 `heap_exhausted (code 4)`）；**v128 SIMD 已实现、已测量（本机 ~1.5–2.5×）、但生成器不选路**（16 字节装箱槽让向量存储无收益、32 位溢出提升为 float、浮点归约重结合会改变结果 —— 与「逐字节等价」判据冲突）；**wasm-GC 明确未实现**，且它本来就是另一件事（需 `--enable-gc` 与 struct/array 引用类型）。`wasm_backend.h` 那句 `future work` 已改写。 |
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

**`0.4.x` → `0.5.0`（tag `v0.5.0`）**，配合 `docs/archive/CHANGELOG_0.5.0.md` 的 `[Unreleased]` 段落。其余版本号表述（例如根 `README.md` 曾写"当前发布基线：0.5.0"却链到 `RELEASE_0.4.1.md`）属于笔误，已修正。

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

> 进展：平台时钟/休眠已接入 VM 兼容层；跨平台互斥锁接口已建立，VM 现有 `CRITICAL_SECTION` 调用仍待逐步替换；原生 Verse 线程和调度器线程的启动、join/close 已接入 `src/platform/thread.h`，取消与超时强制终止语义仍待迁移。Fiber 调用已接入 `src/platform/fiber.h`（Windows Fiber / POSIX `ucontext`）。主程序自身路径解析已迁移到 `im_platform_executable_path`。VM 全局/分片/消息/脚本锁已迁移到 `ImMutex` 平台接口，Windows 构建回归通过。`child_proc` 注册表锁和计时已迁移；进程创建/终止后端仍待 POSIX 实现。`src/platform/process.h` 已提供跨平台进程 API，并有 `process_probe` 验证程序。`child_proc` 已迁移到 `ImProcess`，下一步处理 `isolate_mod` 的输出捕获与超时。`server_mod` 已移除固定盘符，路径默认随可执行文件目录推导并支持环境变量覆盖；房间目录创建/删除已脱离 Win32 文件调用，目录枚举仍待平台目录迭代器。

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

- 集合化类型系统（⚠ 集合化：`Z`/`Z+` 集合关系与 `be` 语法）→ v3.1，见 [archive/ROADMAP_3.1.md](archive/ROADMAP_3.1.md)。
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
| `src/verse/json_min.c` 加入 4 处目标 | `CMakeLists.txt:127`(`http_probe`) / `:130`(`hub_probe`) / `:133`(`websocket_probe`) / `:240`（主 `inimerse` POSIX 分支） |
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
2. CRP `FIND` / `PORTAL` 回环和签名校验。**仍未实现**；`upp-in-engine` 合入 `main` 后已解阻塞（见 [BOARD.md](BOARD.md) 的 `crp-in-engine` 行）。
3. ~~`.vverse` 打包、预览、下载与启动。~~ **引擎侧打包器已实现**（§10.2）；剩余是 `.im`/CLI 入口（`vverse-cli`）与 hub 对 >64 KiB 包的静默截断（`hub-large-package`）。
4. GitHub/Bilibili OAuth token 交换和资料绑定。
5. Verse Forge 第一批时空/物理/蓝图面板。

### 9.3 多智能体协调桥接层（`设计未实现`）

设计文档 [`future/multi-agent-coordination-bridge.md`](../future/multi-agent-coordination-bridge.md)（624 行，2026-10-02 复核更新，作业单 [streams/agent-bridge.md](streams/agent-bridge.md)）：在既有的 CRP（能力路由 / 授权平面）、Verse Layer（历史与提交）、UPP（打包与校验）之上，定义智能体之间如何**声明意图、达成承诺、留下可审计的证据**。它**不新增第五套机制**，只做三层映射：G²CP 的 performative 叠在 CRP 的 `SIGNAL` 上、承诺状态落成 Verse Layer 的一个 `commitment:<id>` cell、身份与生命周期沿用 UPP 的监督。

- 状态：**`设计未实现` / E1** —— 只有设计，没有实现。判定依据：`grep -rn "commitment" src/ --include=*.c --include=*.h -i` 零命中（rc=1），`grep -rniE "\bagent" src/` 同样零命中。
- 代码改动：**零**。本项不需要 `CMakeLists.txt`，也不需要动 `src/**`。
- 挂靠点：§4 阶段四「AI 居民与训练沙盒」的 `强制 AI 标识与权限边界，避免将 AI 伪装成人类玩家。`（阶段出口条件见该节末）。
- 已知前提缺口（**不是**「已具备」）：CRP `PORTAL` 的签发侧只有**调用方认证**、没有 per-`(verse, peer)` **授权**（文档 §7.7，`crp-portal-auth` `40f6094` 之后），因此 `UPDATE = PORTAL + SIGNAL` 的映射**今天仍只是形式上的**。
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

**缺陷（实测）**：`tools/check_links.py:57-74` 的 `iter_markdown_files()` 用 `os.walk(REPO_ROOT)` 遍历**工作区**，只靠 `SKIP_DIRS`/`SKIP_PREFIXES` 黑名单挡杂物。另一个会话在仓库根留下的暂存目录 `.verify/`（未跟踪）里有一份引用 GitHub 社区健康文件（`SECURITY.md`/`GOVERNANCE.md`/`MAINTAINERS.md`）的 README，于是本仓库的门禁被判红：

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
**未修（发现但不在本次写域内）：** `tools/perf_compare.py:131` 与 `tools/selfhost_bench.py:121` 的 `--write-docs` 把报告写到 `docs/` 下的 `SELFHOST_BENCHMARK.md` —— 少了 `archive/` 这一级，那条路径**不存在**，脚本会新建一个文件；真实文件在 `docs/archive/SELFHOST_BENCHMARK.md`。修它会牵动发布流程，单独记账。

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
- **v128 SIMD：实现了、测量了，但生成器不选它。** 导出 `bench_sum_scalar(n)` / `bench_sum_simd(n)`（f64x2 lane 并行，两者结果**逐位相等**），`node tools/wasm_run.js --bench <mod> <n>`。协调者独立复跑：n=2e7 → **2.158 / 2.342 / 2.008**（该流自报 1.870 / 2.136 / 1.791），n=5e7 → **1.509**（自报 1.888）—— 同量级，机器上有波动。**不选路的三条理由是承重的**：16 字节装箱槽让向量存储没有收益、32 位溢出提升为 float 使整型 lane 语义不同、浮点归约重结合会改变结果（与「逐字节等价」判据直接冲突）。所以口径是**「已实现、已测量、未选路」，不是「已用 SIMD 优化」**。
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

**§7 是重点，共 31 条，每条都有实测输出或源码行号。** 10 条「危险·静默」（不报错但结果错）：D1 `push(list, <用户函数调用>)` 误编译、D2 GUI 动词无 arity/类型检查（`sprite 42` 与 `sprite "x"` 一样「成功」）、D3 类型标注纯装饰（`int x = "hello"` 通过）、D4 参数个数不校验（`h(1,2)`→`1`、`h()`→`nil`，两个方向都不报错）、D5 未声明变量求值为 `nil`、D6 字符串里的 `\0` 截断（`len("a\0b")` = 1）、D7 `(1,2)` 是区间不是元组、D8 `a[1~2]` 是集合区间不是切片、D9 `type` 已注册为内建却是保留字（`type(x)` 是解析错误，唯一途径 `x.type`）、D10 `N`/`Z`/`Z+`/`Z-`/`Float1..9` 被硬编码为集合前缀（`func Z(a,b)` 后 `Z(1,5)` 得到 `set(Z interval)`，函数根本没被调用）。11 条「危险·误导」：M1 `\|>` 与 `>>` 不能混用（两个顺序循环而非统一循环，报错是 `expected ')'`）、M2 `->` 既是 lambda 又是类型转换、M3 命名实参不支持且报错落在别处、M4 保留字可作成员名、M5 `until`/`till` 只在 `do…until`、M6 `..` 不是通用运算符、M7 `show(` 变函数调用、M8 `join` 双重身份、M9 `match` 上下文敏感（`no_infix_match`）、M10 后缀条件的行敏感规则、M11 `--lint` 恒 0。7 条冗余：R1 八组关键字别名、R2 后置 `with` 子句**四段代码不可达**（`src/parser/parser.c:1690`/`:1692`/`:1693`/`:1722` 用 `peek(p).type == TOK_IDENT && sv_eq_cstr(text,"with")` 判断，而 `with` 是 `TOK_WITH`）、R3 `src/parser/parser.c:1286`/`:1287` 是逐字相同的一行、R4 十六进制有两条扫描路径、R5 约 30 个 GUI 关键字没有自己的语法（只有 `parse_gui_stmt` 的「原文当字符串」机制）、R6 `TOK_UNKNOWN` 无人处理、R7 完全没有按位运算符。3 条卫生：H1 14 个受版本控制文件含 U+FFFD（C 注释是损坏的 GBK，`src/mod/gui_mod.c.bak2_20260808_221050` 1485、`mods/debug/debug_mod.c` 619、`src/compiler/bytecode.c` 409…）、H2 上述 `.bak2_` 备份文件被入库、H3 `ai_browser_diag.js` 是唯一**非法 UTF-8** 文件（偏移 478）。

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
- `src/main.c.bak`（未被版本控制）里还有一份 `inim_load_text()`；它不参与构建，没有动。

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

本轮最严重的一条 —— `and` / `or` 在两个后端返回不同的东西 —— **不是手写探针找到的**。解释器的编译器把 `and`/`or` 编译成短路跳转并返回操作数（`src/compiler/compiler.c:648-670`），AOT 编译成布尔（`src/compilation/aot_native.c:334-339`）；实测 `31 or 1` 解释器给 `31`、AOT 给 `true`。

**最难察觉的地方在于**：VM 的 `L_AND`/`L_OR`（`src/vm/vm.c:3166-3179`）**确实是布尔语义的**，但编译器根本不发它们 —— `OP_OR` 声明在 `src/compiler/bytecode.h:12` 却**全仓从未被 emit**，`OP_AND` 只在 `src/compiler/compiler.c:827` 用于链式比较。所以「去读 VM 代码」会得出与运行时**相反**的结论。而 `docs/API.md:90` 只说这两个是「逻辑」，没说返回操作数还是布尔 ⇒ **两边都符合文档，都不报错**。

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

（`OP_CALL_FUNC` = 34、`OP_HALT` = 33，见 `src/compiler/bytecode.h:8-23`；`funcs` 为空 ⇒ `fidx = 999` 全面越界。）

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

- `selfhost/compiler.im:254-266` —— `and`/`or` **值语义**（与 C 侧 `src/compiler/compiler.c:648-670` 同形：`OP_JUMP_IF_FALSE`/`OP_JUMP_IF_TRUE` + `OP_MOV result, right`）；
- `selfhost/eval.im:114-123` —— 同一对运算符 **布尔语义**（`if !truthy(l) { return false }`）。

⇒ **「用 `.im` 重写」本身不消除分歧，只是改变分歧发生在哪两个文件之间。** 同语言内部同样分裂：C 侧 `compiler.c`（值）对 `vm.c`/`aot_native.c`/`wasm_backend.c`（布尔）。

**六个决定点**（全部逐行读过，`[读码]`）：`src/compiler/compiler.c:648-670`（C·值）、`selfhost/compiler.im:254-266`（`.im`·值）、`src/vm/vm.c:3166-3179`（C·布尔）、`src/compilation/aot_native.c:334-339`（C·布尔）、`src/compilation/wasm_backend.c:769-786`（C·布尔）、`selfhost/eval.im:114-123`（`.im`·布尔）= **2 值 : 4 布尔**。`%` 有 **3 个**：`src/vm/vm.c:3815-3824`（32 位）、`src/compilation/aot_native.c:208-211`（64 位）、`src/compilation/wasm_backend.c:938`/`:960`（64 位）。

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

**顺带抓出第二个既有真缺陷：wasm 的 `not` 是反的。** `cg_cond` 的 `TOK_NOT` 分支已经做了一次 `i32.eqz`，`cg_expr` 的 `TOK_NOT` 分支又调 `cg_cond` 再 `eqz` 一次 ⇒ 双重否定。六条 `not` 用例**全部恰好相反**（`not 0` 在 wasm 上打 `false`，而解释器与 AOT 都打 `true`）。`git diff` 证明该分支未被本次改动触及，**是既有缺陷**，只是从来没人同时跑两个后端所以没被发现。

**O1（`%`）**：`src/vm/vm.c` 的 `L_MOD` 有三重问题 —— 把两操作数窄化过 32 位 `int`（`2147483648 % 7` → **-2**，AOT `2`、wasm `1`）、**在截断之后**才判零（`7 % 0.5` 误报 `division_by_zero`，而真值 0 完全合法）、以及 `-2147483648 % -1` 实测 **rc=136 `Floating point exception`**（真崩溃，不是干净拒绝）。AOT 侧 `nv_div` 除零静默给 `inf` —— **AOT 此前完全没有错误机制**（`grep nv_error|nv_throw|nv_panic|abort()` 在 `src/compilation/aot_native.c` 零命中）。修法：VM 新增饱和转换 `im_dbl_to_i64()`、零检查**移到窄化之前**、`y == -1` 特判、结果超出 int32 时提升为 float（照抄 `L_NEG` 对 `INT_MIN` 的既有先例）；AOT 新增 `nv_die_division_by_zero()`；wasm 用 `e_trunc_sat_i64`（`0xFC 0x06`）替代 `e_trunc_sat_i32`。

**O2(a)（整数静默提升可诊断）**：三处提升点 `src/vm/vm.c:2950`（`L_ADD`）/`:3073`（`L_SUB`）/`:3097`（`L_MUL`）形状相同且完全静默。配套缺陷是 `src/main.c:1242-1247` 的 `--lint` **恒返回 0** —— 而 `lint_check` 本身是真的（`src/lint_mod.c:558` 委托 `lint_scan`，599 行模块），所以 `vtest/lint_case_*_v04.im` 五个 fixture 里有四个「即使 lint 报告再多也永远绿」。修法：退出码承载判据（**1 = 有发现 / 2 = 文件不可读 / 0 = 干净**）。

### 判据：三后端逐格比对，不是「值对了」

新增两个进树回归，形状相同 —— 同一份源码在**解释器 / AOT / wasm 三个后端各跑一遍，然后互相比对**：

- `tools/logic_semantics.test.py`（**34 例**：值位置 8 / 真值性 4 / 优先级 2 / 链式嵌套 3 / `not` 7 / 条件位置 6 / 短路 4）；
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

