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

## 2. 当前基线（2026-10-07 更新测试计数到 134；其余各项为 2026-10-01 实测）

| 项目 | 实测值 | 证据 |
| --- | --- | --- |
| 版本 | `0.5.0` | `CMakeLists.txt:8`；git tag `v0.5.0` |
| 干净构建 | configure / build 均退出码 0，**35 warnings / 0 error** | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| 全量测试 | **134 / 134 真通过**，无 `WILL_FAIL` 记账项；`-j12` 高争用单轮约 13 s | `ctest --test-dir build -j$(nproc)` |
| 高争用稳定性 | §2.9 的端口窗口**已关闭**：hub 一律用内核分配端口（`--port 0 --http-port 0`），不再由 harness 猜号。`tools/ports_race_probe.py` 实测 1224 次启动 **5 → 0**（对照格「已修引擎但仍猜端口」为 **6**，证明竞态在 harness 而非引擎）。本行原来的「80 轮失败 1 轮」是**内核分配之前**的数字，未复测 | `python3 tools/ports_race_probe.py`；`for i in $(seq 80); do ctest --test-dir build -j12; done` |
| 编译器诊断 | **35 条 warning，0 error**（§2.5 修复后干净重建日志） | 干净重建日志 |
| 引擎代码 | `src/` 101 个 `.c` + 49 个 `.h`，合计 48,753 行（`.c` 单独 46,120 行） | `find src -name '*.c' -o -name '*.h' \| xargs cat \| wc -l` |
| 内建函数注册 | 531 处 `vm_register_builtin*` 调用 | `grep -rho 'vm_register_builtin[a-z_]*' src \| wc -l` |
| 自举编译器 | `selfhost/` 48 个 `.im`、2,316 行 | `find selfhost -name '*.im'` |
| 脚本规模 | 仓库 316 个 `.im`（根目录 148 个为回归测试） | `find . -name '*.im' -not -path './build/*'` |
| 测试注册 | `CMakeLists.txt` 中 **134** 个 `add_test(`（`grep -c 'add_test(' CMakeLists.txt`；这个数字是**断言**，必须与 `tools/gate.sh:50` 的 `EXP_CTEST` 同步，历史增量见各 §10.x） | — |
| 工具 | `tools/` 98 个条目 | `ls tools \| wc -l` |
| 性能（`sum(1..2000000)`） | 解释器 88 ms = 1.00x · AOT 打包 = 与解释器**等同**（分布中位 **0.98x**） · Wasm MVP 58 ms = 1.51x | [SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) |

### 2.1 复现命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4      # 期望 100% tests passed, 0 failed out of 134
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
| 绑定生成与扫描工具 | CTest `bindgen_regression` / `scan_tools_regression`；`tools/bindgen.py`、`tools/cpp_scan.py`、`tools/python_scan.py`、`tools/migrate_report.py` |
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

**根因：两套索引约定被接在一起。** `VAL_ARRAY` **值**里存的是 **1-based 句柄**（构造点 `src/vm/vm.c:3240` 的 `value_set(&R[ins.r1], VAL_ARRAY, aidx + 1, 0, NULL, NULL)`；读者都减一，见 `src/vm/vm.c:3275`、`:3323`、`:2447`），而整个 `vm_array_*` **函数族**收的是 **raw 池下标**（`vm_array_len(vm, idx)`，`src/vm/vm.c:789-795`）。`vm_set_to_array`（`src/vm/vm.c:1966`）**两条造数组的分支都以 `return aidx + 1;` 收尾**，而它的三个调用方全部按 raw 解释：`src/runtime/runtime_posix.c:19`（`len` 的集合分支，`vm_array_len(vm, a)`）、`:236`（`list`，`{ VAL_ARRAY, idx + 1 }`）、`src/runtime/runtime.c:134`（WIN32 的 `list`，`ival = r + 1`）。⇒ `list(<集合>)` 交回的是**池里的下一个数组**（刚新建、通常是空的）。

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

**症状（流程缺陷，不是运行错误）**：仓库根的 `contract_test.im` 是 API/SPI 契约套件，头注释自称「每次引擎 API 变更必须跑本套件（回归门槛）」—— 但它**根本不在 CTest 里**。`grep -in contract CMakeLists.txt` 在本轮之前只命中两处无关注释。§1.8 的 `+` 链整数溢出与 §1.10 的 `list(<set>)` 句柄错位**都是它先抓到的**，只因要人手动跑，从没在门禁里红过。**一条不跑的套件等于没有套件。**

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

`docs/DECFY_DESIGN.md:76` 把 `Value` 的宽度**冻结**在 32 字节：它同时是 VM 寄存器、AOT 生成 C 的 `NV`、wasm 线性内存槽位的共同形状。`int ival` 直接改 `long long` 会让 `sizeof` 从 32 变成 40，是真正的 ABI 破坏。改用匿名 union：

```c
int type;
union { long long ival; double fval; };
char *sval;
void *ptr;
```

实测 `sizeof` 前后都是 32，`type`/`sval`/`ptr` 的偏移不变（`ival` 与 `fval` 共用偏移 8）。`CMakeLists.txt:19-20` 是 C11，匿名 union 合法。wasm 后端**本来就是这样**的（`src/compilation/wasm_backend.c:66` 的 `SLOT_BYTES 16` 注释即 `[tag i32 @+0][pad][i64 payload @+8]`），所以 AOT 的 `NV` 完全没动，只有 VM 和 wasm 要改。顺带一提，改完之后 `grep -n 'Value v; .*v\.fval = 0' src/` 这类「同时给 `ival` 和 `fval` 赋值」的位置全部失效（两者现在是同一块存储），全仓 28 个文件里那些 5 字段的位置初始化 `{VAL_NIL, 0, 0, NULL, NULL}` 都要去掉中间那个 `0` —— 这是本次 diff 里最大的一片机械改动。

### 四、逐条：加宽之后才暴露的六处

1. **字节码没有 int64 字面量的载体。** `RegInstruction` 只有 `r1/r2/r3`，`OP_LOADK_INT` 的 `r2` 是**符号扩展的 int32**，无法承载无符号低半。先试过把高低 32 位相加（`r2 + (r3<<32)`），**这是错的**：低半符号位为 1 时 `(int)` 会把它符号扩展，结果少了 2³²（实测 `9223372036854775807` 被读成 `9223372032559808511`）。最终**在 `OpCode` 枚举末尾追加 `OP_LOADK_I64`**（`src/compiler/bytecode.h` 自己的约定就是「追加在末尾以保持旧 opcode 编号、DLL ABI 兼容」），两半都按无符号拼装；旧字节码的 `OP_LOADK_INT` 语义不变，`INIM_BYTECODE_VERSION` 仍是 **3** —— 它在 `src/compiler/bytecode.c:351`、`:731` 是**严格相等**比较的，一升版所有既有 `.inim` 都读不了。
2. **`push_int` 的参数是 `int`。** `src/vm/vm.c:325` / `src/vm/vm.h:379` 原文 `void push_int(VM *vm, int v)`，而 `L_PUSH_REG` 对 `VAL_INT` 直接调它 —— **函数传参路径上每一个 64 位整数都被截成低 32 位**。实测 `func f(a) { return a } g = f(9007199254740993) say g` 解释器打印 `1`，AOT 和 wasm 都是 `9007199254740993`。这个缺陷一直潜伏：字面量以前都被降级成 double 走 `push_float`，`push_int` 根本见不到超过 int32 的值。
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

`if s` 认为空串为真，`not s`、`s or x` 与 `bool(s)` 认为它为假。而 `docs/DECFY_DESIGN.md:125` 要求的是「`OP_JUMP_IF_FALSE` / `OP_JUMP_IF_TRUE` 各对应一条 `W_IF` 映射，**真值产生点唯一**」，`:24` 写下的规则是那条三目链的尾句 `… : (va.type == VAL_NIL) ? 0 : 1`，即**非 nil 且非零为真，空串也算真**。

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

**为什么统一到「空串为真」而不是统一到 `bool()` 的「空串为假」。** ① 前者是 `docs/DECFY_DESIGN.md:24` 写下的规则，且六处里本来就有三处如此；② 前者**完全不动 `if` 的控制流**，对既有 `.im` 程序的爆炸半径为零 —— 反过来统一到「空串为假」会让每个 `if s` 在 `s` 为空串时静默换分支。代价是 **`bool("")` 从 `false` 变成 `true`**，这是本次唯一面向用户的语义变化，单独写在明处。

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

**根因。** v3.1 把 `Value` 的整数槽改成 64 位时，为守住 32 字节宽度契约（`docs/DECFY_DESIGN.md:76`）用的是匿名 union：`ival` 与 `fval` **共享存储**。于是所有「只分两种类型」的取值写法 `X.type == VAL_INT ? X.ival : (int)X.fval` 对 `VAL_BOOL` 都会去读 `fval`，读出来的是 `ival` 的位模式。`float(false)`/`float(nil)` 答 0 只是碰巧（位模式全零）。

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

③ **路径不能截断 —— 改成拒绝**：`src/main.c:247`、`src/mod/verse_dist_mod.c:384`、`:839`（两处）、`:1297` 四处换成 `im_platform_path_join`（`src/platform/platform.c:122-133`，装不下返回 **-1**，同时归一化分隔符、按平台选 `/` 或 `\`）。理由：静默截断的路径把文件写到**错误的位置**，那是安静的数据损坏；被拒绝的 join 只是一个被跳过的条目。调用方一律 `if (im_platform_path_join(...) != 0) continue;`。`src/lint_mod.c:181` 的 `lint_add` 没有「扩大」这个选项（`LintBuf` 已 12.8 KB、可能栈上），改成前缀 `snprintf` + 显式 clamp + `memcpy` 余量 + NUL，**`msg` 完全不再走 `%s`**。因为 join 自己按平台选分隔符，`src/main.c:247` 与 `src/mod/verse_dist_mod.c:384` 里那两段 `#ifdef _WIN32` 的 `/`→`\` 转换循环随之删掉。

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

**验证。** 直接调用 Windows 侧工具链 `/mnt/c/msys64/mingw64/bin/gcc.exe`（gcc 16.1.0）：对全部 `src/**/*.c` 做 `-fsyntax-only` 只筛 `implicit declaration`，修复前命中 5 处，修复后只剩 `src/platform/http_probe.c:93` 的 `setenv`，而它在 `CMakeLists.txt:202` 的 `if(NOT WIN32)` 里、不参与 Windows 构建。受影响编译单元逐个 PASS。发布会话在仓库外克隆上完成 164/164 干净重建、`inimerse.exe` 链接成功。

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

**双向验证**：新 pin `vtest/atomic_slot_type_contract_v06.im` + CTest **#132** `atomic_slot_type_contract_runtime`（`PASS_REGULAR_EXPRESSION` 钉整行，两平台**同一行**）。`git stash push -- src/runtime/runtime_posix.c` 重编 ⇒ 打出 `atomic-slot-ok g1=4609434218613702656 r1=4609434218613702657 y=4609434218613702657 r3=4609434218613702657 yz=4609434218613702657 g2=1 r2=2 s=2 g3=7 r4=12`、退出码 0；恢复 ⇒ `atomic-slot-ok g1=0 r1=0 y=1.5 r3=0 yz=1.5 g2=0 r2=0 s=abcdef g3=7 r4=12`。Windows 侧（ucrt64，CI 同款工具链，`Total Tests: 122`）同形 A/B：把新守卫换回 `type = VAL_INT; ival = 0;` 后重编 ⇒ 该测试打出 `atomic-slot-ok g1=0 r1=1 y=1 r3=1 yz=1 g2=0 r2=1 s=1`、**Failed**（`PREFIX_CTEST_RC=8`）；换回 ⇒ Passed。这一行把「修前 WIN32」从读码结论升为实测。计数 **131 → 132**（`tools/gate.sh` 的 `EXP_CTEST`）。

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
有示例但零注册、`docs/SYNTAX.md:500` 的「有 `vtest` 覆盖」对 `random` 不成立），以及
`docs/API.md:234` **早就记下 `gui_fullscreen` 重复却只当成计数问题**这一点，
见 [AUDIT.md](AUDIT.md) §1.53。`docs/SYNTAX.md:500` 已就地更正（`rand` 移出名单）。

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

### §10.85 关于实参类型的第一条通用规则：D14 落地

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

### §10.86 `--only` 打错阶段名：跑了个空，却打印绿灯

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
