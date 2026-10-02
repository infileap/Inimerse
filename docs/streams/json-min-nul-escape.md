# 流简报：`json-min-nul-escape`

> 这是给**一个 DSH 对话**的作业单。开工前先读 [BOARD.md](../BOARD.md) §1–§4。
> 分支 `stream/json-min-nul-escape`，冲突域 `src/verse/json_min.c`、`src/verse/json_min.h`。
>
> **状态：未开工。** 本文件的「证据」一节是**协调者在开工前跑出来的**，可复现；
> 「现状」记录的是**今天**的代码事实。判断某件事做了没有，以 `docs/STATUS.md` §10 与 `docs/BOARD.md` §5 为准。

## 0. 一句话

`src/verse/json_min.c` 的 `\u` 解码在三个输入上**静默产出错数据**，其中两个还**违反本仓库已有的
「与 Node `JSON.parse` 逐字节一致」契约**（`tools/crp_engine_crosscheck.js` 正在执行这个契约）。

## 1. 证据（协调者已复现，你应当能独立复现同样结果）

用 `gcc -std=gnu11 -I src/verse` 编译 `src/verse/json_min.c` 加一个 20 行 driver，与
`node -e 'JSON.parse(...)'` 对同一批输入比对：

| 输入（JSON 字符串字面量） | Node `JSON.parse` → UTF-8 字节 | 引擎 `vj_parse` → UTF-8 字节 | |
|---|---|---|---|
| `"\uD83D\uDE00"`（U+1F600 😀） | `F0 9F 98 80`（strlen 4） | `ED A0 BD ED B8 80`（strlen 6） | ✗ **CESU-8，不是合法 UTF-8** |
| `"\uD83D"`（孤立高代理） | `EF BF BD`（U+FFFD） | `ED A0 BD` | ✗ 未替换，且非法 UTF-8 |
| `"\uDE00"`（孤立低代理） | `EF BF BD` | `ED B8 80` | ✗ 未替换，且非法 UTF-8 |
| `"x\u0000y"` | `78 00 79`（strlen 3） | `78`（strlen 1） | ✗ **静默截断**，`y` 丢失 |
| `"alice\u0000A"` | — | `61 6C 69 63 65`（`alice`） | ✗ |
| `"alice\u0000B"` | — | `61 6C 69 63 65`（`alice`） | ✗ **两个不同 JSON 串塌缩成同一个 C 串** |
| `"\u00e9"` | `C3 A9` | `C3 A9` | ✓ |
| `"\u4e2d"` | `E4 B8 AD` | `E4 B8 AD` | ✓ |
| `"\u0001"` | `01` | `01` | ✓ |

**为什么 `\u0000` 会截断**：`src/verse/json_min.c:95` `if (cp < 0x80) out[len++] = (char)cp;` 写入**裸 NUL**；
`src/verse/json_min.h:19` 的字符串是 `char *s;`，**没有长度字段** ⇒ 之后每一个 `strlen` / `strcmp` / `%s`
消费者都只看到 NUL 之前的部分。写入本身没有越界，缓冲区增长检查 `if (len + 4 >= cap)` 也**足够覆盖 4 字节写入**——
问题纯粹是「值无法表示」。

**为什么这不是纸面问题**：
- 解析器被 `layer.c` / `eventlog.c` / `protocol.c` / `crp.c` / `upp.c` / `vverse_pack.c` **共用**（`src/` 下 63 处 `vj_str` 调用点）。
- `src/platform/http_posix.c:1760` 用 `vj_parse` 解析**来自网络的**经济域导入包；`:688`/`:705`/`:836` 用
  `vj_str(vj_get(b,"account"), NULL)` 取账户名去算 `balances_hash`。两个只差 NUL 之后内容的账户名
  （`alice\u0000A` / `alice\u0000B`）**解析后字节相同**，任何基于它们的摘要都相同。
- 线上形式是 6 个 ASCII 字节 `\`,`u`,`0`,`0`,`0`,`0`——**不含裸 NUL**，所以能穿过 HTTP 头部剥离与所有 `strstr`/`strlen`
  检查，一路到达解析器。

**为什么「拒绝」不是随意的收窄**（这条决定了修法，别跳过）：引擎自己的写出器**无法产出 `\u0000`**。
`src/verse/upp.c:166`、`src/verse/eventlog.c:119`、`src/common/vverse_pack.c:105`、`src/mod/json_mod.c:72`、
`src/mod/replay_mod.c:75` 都在 `*p < 0x20` 时写 `\u%04x`，但它们的循环都是 `for (p = s; *p; p++)`——
**裸 NUL 会先终止循环**，所以最小可产出转义是 `\u0001`。也就是说 `\u0001`–`\u001f` 必须继续正常往返，
而 `\u0000` **在引擎可生成的输入空间里根本不存在**。

## 2. 要交付什么

修 `vj_parse` 的 `\u` 解码，使**可表示的输入与 Node 逐字节一致**，不可表示的输入**显式报错**：

1. **代理对**：`\uD800`–`\uDBFF` 后紧跟 `\uDC00`–`\uDFFF` ⇒ 合成一个码点，按 4 字节 UTF-8 编码
   （`F0 9F 98 80`），**匹配 Node**。
2. **孤立代理**（高代理后面不是低代理，或低代理单独出现）⇒ 输出 U+FFFD `EF BF BD`，**匹配 Node**。
3. **`\u0000`** ⇒ **拒绝解析**，写明确错误（建议 `\u0000 is not representable`），**不写任何字节**。
   这是**有意与 Node 不同**的一处（Node 给 `78 00 79`），必须写进
   [STATUS.md §10.6](../STATUS.md) 已有的「与参考实现的分歧」清单，说明理由：`VjVal` 无长度字段，
   C 字符串无法表示内嵌 NUL，宁可显式拒绝也不静默截断。
4. 同一份解码逻辑**同时作用于对象键**（`vj_parse_object` 走的是同一个 `vj_parse_string_raw`），
   所以键上的 `\u0000`/代理对一并按上面处理，别只修值那一侧。

**不要**给 `VjVal` 加长度字段。那是 63 个调用点 + 全部写出器的连锁改动，而且**不解决代理对问题**
（CESU-8 依然是错的），收益/风险比远差于上面的方案。BOARD 上「两条路」的措辞是发现时的猜测，
协调者据此证据**选定了拒绝 + 配对**这条。

## 3. 验收判据（量化，别含糊）

1. **上面那张 9 行表**：6 个可表示的行与 Node **逐字节相同**；`\u0000` 行**返回 NULL 且错误非空**。
   写一个可复现的 driver 或测试，把「修之前失败」也证明一遍（`git stash` 回基线跑同一个 driver）。
2. **进树的回归**：把这三个缺陷固化成**会随 `ctest` 跑的**测试（现有 `src/verse/crp_probe.c`
   或新建 `json_min` 探针，你选，但要进 `CMakeLists.txt` 且有 `add_test(`）。
   要求：**在修复前该测试必须失败**——把失败输出贴回来。
3. **crosscheck 语料扩项**：`tools/crp_engine_crosscheck.js` 的 101 条语料加入代理对 / 孤立代理 /
   `\u0001` 三类用例，仍然**逐行文本一致**（`\u0000` 不进语料，它是文档化的分歧）。
   注意：require 参考实现前**必须冻结 `Date.now`**，这是上次踩过的坑。
4. **往返性质**：对引擎写出器**能产出**的全部转义（`\u0001`–`\u001f` 及 7 个短转义）验证
   `parse(write(s)) == s`。
5. **门禁**：`tools/gate.sh --jobs 4` 七阶段全 PASS。ctest 计数若变了，**必须同步**
   `tools/gate.sh` 的 `EXP_CTEST`（它现在是真断言，不是标签）与 `docs/BOARD.md` §3。
6. **内存**：新增的配对分支要过 ASan + UBSan（`-fsanitize=address,undefined`，`detect_leaks=1`），
   覆盖 `"\uD83D\uDE00"`、`"\uD83D"`（在字符串末尾）、以及**紧贴缓冲末尾**的 `\uXXXX`。

## 4. 已知的坑（都是踩过的）

- **`P->p[k]` 是无界前读**：`src/verse/json_min.c:88` 在验证 4 个 hex 位时读 `P->p[0..3]`。输入以 NUL 结尾，
  非 hex 会立刻报错，但若 4 位转义**正好贴着输入缓冲区末尾**，第 2–4 次读取可能越过 NUL 之后再读 2–3 字节。
  现在没炸是因为调用方缓冲区普遍很大（`http_posix.c` 的 `char req[65536]`）。**修的时候顺手确认这一点**：
  要么证明调用方缓冲总是留有余量并写进注释，要么改成不越界的逐字节推进。别默默留着。
- **`\u0000` 的错误文本要短**：错误串会经 `crp_decode_line` / `upp_decode_line` 冒泡到线上，
  保持与既有风格一致（小写、无标点尾巴，如 `bad \u escape`）。
- **`json_min` 只收整数**：遇到 `.`/`e`/`E` 直接 `non-integer number unsupported`。别顺手「修」这个。
- **别碰 `src/parser/parser.c`**：那是 `.im` **语言**的词法/转义写出器（`:22`），与 `json_min` 是两套东西。
- **`vj_str` 的 63 个调用点不要动签名**：本次修法不改 `VjVal` 布局，调用点应**零改动**。若你发现必须改，
  停下来在交回里说明，不要单方面扩大冲突域。
- **`\u0000` 被拒绝会改变一条线上行为**：任何含 `\u0000` 的 CRP/UPP 行现在会被拒。
  这是**期望的**（此前是静默截断），但要在交回里明确写出，并确认现有测试没有依赖旧行为。

## 5. 不要做

- 不要给 `VjVal` 加长度字段（见 §2 末）。
- 不要放宽或删除任何现有校验来「让测试过」。
- 不要改 `docs/STATUS.md` §10.6 里已有的四条 CRP 分歧；只**追加** `\u0000` 这一条。
- 不要在这个 worktree 里碰 `hub-large-package`（`src/platform/http_posix.c:312`）——那是另一条流。

## 6. 交回时给我

按 [BOARD.md](../BOARD.md) §4 的五项，第 4 项「没做什么 / 已知没解决什么」不能漏。
另外明确回答：**`P->p[k]` 的前读你是证明安全了，还是改了？**
