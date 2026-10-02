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

## 2. 当前基线（2026-10-01 实测）

| 项目 | 实测值 | 证据 |
| --- | --- | --- |
| 版本 | `0.5.0` | `CMakeLists.txt:8`；git tag `v0.5.0` |
| 干净构建 | configure / build 均退出码 0，**35 warnings / 0 error** | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| 全量测试 | **95 / 95 真通过**，无 `WILL_FAIL` 记账项；`-j12` 高争用单轮约 13 s | `ctest --test-dir build -j$(nproc)` |
| 高争用稳定性 | §2.9 的端口窗口**已关闭**：hub 一律用内核分配端口（`--port 0 --http-port 0`），不再由 harness 猜号。`tools/ports_race_probe.py` 实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」为 **6**，证明竞态在 harness 而非引擎）。本行原来的「80 轮失败 1 轮」是**内核分配之前**的数字，未复测 | `python3 tools/ports_race_probe.py`；`for i in $(seq 80); do ctest --test-dir build -j12; done` |
| 编译器诊断 | **35 条 warning，0 error**（§2.5 修复后干净重建日志） | 干净重建日志 |
| 引擎代码 | `src/` 101 个 `.c` + 49 个 `.h`，合计 48,753 行（`.c` 单独 46,120 行） | `find src -name '*.c' -o -name '*.h' \| xargs cat \| wc -l` |
| 内建函数注册 | 531 处 `vm_register_builtin*` 调用 | `grep -rho 'vm_register_builtin[a-z_]*' src \| wc -l` |
| 自举编译器 | `selfhost/` 48 个 `.im`、2,316 行 | `find selfhost -name '*.im'` |
| 脚本规模 | 仓库 316 个 `.im`（根目录 148 个为回归测试） | `find . -name '*.im' -not -path './build/*'` |
| 测试注册 | `CMakeLists.txt` 中 95 个 `add_test(`（85 + UPP 2 + `.vverse` 2 + CRP 3 + json_min 1 + 超大包 1 + `.im` 打包往返 1，见 §10.1/§10.2/§10.6/§10.7/§10.10/§10.11） | — |
| 工具 | `tools/` 98 个条目 | `ls tools \| wc -l` |
| 性能（`sum(1..2000000)`） | 解释器 88 ms = 1.00x · AOT 打包 = 与解释器**等同**（分布中位 **0.98x**） · Wasm MVP 58 ms = 1.51x | [SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) |

### 2.1 复现命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4      # 期望 100% tests passed, 0 failed out of 95
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

**声称但未交付（设计未实现）** —— `docs/archive/RELEASE_0.5.0.md` 的下列表述不成立：

| 该文件声称 | 核实结果 |
| --- | --- |
| 「AOT compilation … outperform the interpreter by at least 2x」（第 52 行） | 实测与解释器**等同**——12 次试验中位 **0.98x**（0.85x..1.33x），与「解释器对自己」的对照区间（0.77x..1.36x）**12/12 重叠**，1.09x 只是其中一个噪声样本。AOT 通道是**打包**通道（引擎副本 + 嵌入规范化字节码），复用同一个 C 解释器，不自生成原生代码。原文 `SELFHOST_BENCHMARK.md` 已自述「不满足 ≥2x 目标」。 |
| 「WebAssembly output … supporting SIMD optimizations and WebAssembly GC」（第 38 行） | `src/compilation/wasm_backend.h:10` 原文：`SIMD/GC/heaps are future work.` 当前为数值子集 MVP。 |
| 「Python extension bridge: `inimerse_extension.c` implementing `PyInit_inimerse()`」（第 26 行） | 仓库中不存在 `inimerse_extension.c`，全仓无 `PyInit_inimerse` 实现。 |
| 「Java native bridge: `InimerseBridge.java`」（第 27 行） | 仓库中不存在 `InimerseBridge.java`。 |
| 发布产物 `.whl` / `.jar` / `inimerse-aot`（第 123–130 行） | 仓库中不存在任何 `.whl` / `.jar` / `.aot` / `libinimerse.so` 产物。 |

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
- selfhost：`compiler.im --dump lexer.im`、`eval.im`、`parser.im`。
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
tools/gate.sh          # 七个阶段，串行；不要并发跑，§2.9 的端口窗口会假失败
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
