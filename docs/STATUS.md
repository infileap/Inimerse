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

---

## 2. 当前基线（2026-10-01 实测）

| 项目 | 实测值 | 证据 |
| --- | --- | --- |
| 版本 | `0.5.0` | `CMakeLists.txt:8`；git tag `v0.5.0` |
| 干净构建 | configure / build 均退出码 0，**35 warnings / 0 error** | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` |
| 全量测试 | **85 / 85 真通过**，无 `WILL_FAIL` 记账项；`-j12` 高争用单轮约 13 s | `ctest --test-dir build -j$(nproc)` |
| 高争用稳定性 | `-j12` 连续 80 轮**失败 1 轮**（§2.9 残余的端口窗口；改前失败更密，见 §2.6–§2.9） | `for i in $(seq 80); do ctest --test-dir build -j12; done` |
| 编译器诊断 | **35 条 warning，0 error**（§2.5 修复后干净重建日志） | 干净重建日志 |
| 引擎代码 | `src/` 81 个 `.c` + 41 个 `.h`，合计 36,836 行（`.c` 单独 35,062 行） | `find src -name '*.c' -o -name '*.h' \| xargs cat \| wc -l` |
| 内建函数注册 | 531 处 `vm_register_builtin*` 调用 | `grep -rho 'vm_register_builtin[a-z_]*' src \| wc -l` |
| 自举编译器 | `selfhost/` 48 个 `.im`、2,316 行 | `find selfhost -name '*.im'` |
| 脚本规模 | 仓库 316 个 `.im`（根目录 167 个为回归测试） | `find . -name '*.im' -not -path './build/*'` |
| 测试注册 | `CMakeLists.txt` 中 85 个 `add_test(` | — |
| 工具 | `tools/` 80 个条目 | `ls tools \| wc -l` |
| 性能（`sum(1..2000000)`） | 解释器 88 ms = 1.00x · AOT 打包 81 ms = **1.09x** · Wasm MVP 58 ms = 1.51x | [SELFHOST_BENCHMARK.md](archive/SELFHOST_BENCHMARK.md) |

### 2.1 复现命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -j4      # 期望 100% tests passed, 0 failed out of 85
node tools/upp_reference.test.js                     # JS 侧协议测试（不在 CTest 内）
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

1. **`free_port()` 是 TOCTOU**（`tools/testports.py:49-56`）：`bind(0)` 取号后立刻关闭，返回瞬间即释放。
   实测单次连续分配 20 个时约 **23/300** 批次出现重复。全部 hub 端口点已改用 `distinct_ports`。
2. **`distinct_ports()` 的保证只在单次调用内成立**（`tools/testports.py:214`）：两次独立调用各建一个池，
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

**残余（诚实记录）**：端口在「池归还」到「子进程 bind」之间仍有一个窗口，**另一个套件的池**可以在
这个窗口里抢到同一个号码。这是 `distinct_ports` 机制固有的，实测约 **1 轮 / 80 轮**全量 `ctest -j12`
会因此失败。彻底的修法有两种：让引擎支持 `--http-port 0` 由内核分配并把真实端口打到 stderr（当前
`verse_http_start` 拒绝 `port < 1`，故不支持），或让套件在就绪失败时用新端口重试。**两者都尚未实施**，
当前依靠 §2.8 的响亮报错把这类失败从「神秘超时」变成「一行可读原因」。

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
| 「AOT compilation … outperform the interpreter by at least 2x」（第 43 行） | 实测 **1.09x**。AOT 通道是**打包**通道（引擎副本 + 嵌入规范化字节码），复用同一个 C 解释器，不自生成原生代码。原文 `SELFHOST_BENCHMARK.md` 已自述「不满足 ≥2x 目标」。 |
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

1. UPP 本地参考协议与 Verse manifest。
2. CRP `FIND` / `PORTAL` 回环和签名校验。
3. `.vverse` 打包、预览、下载与启动。
4. GitHub/Bilibili OAuth token 交换和资料绑定。
5. Verse Forge 第一批时空/物理/蓝图面板。
