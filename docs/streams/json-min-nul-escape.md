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

## 7. 实施记录（本 worktree，实施者 `jsonmin` 追加）

### 7.1 交付物

| 文件 | 变更 |
| --- | --- |
| `src/verse/json_min.c` | `vj_read_hex4` / `vj_put_utf8` / `vj_put_replacement` / `vj_is_hi_surrogate` / `vj_is_lo_surrogate` 五个静态辅助；`case 'u':` 改为：合并代理对 → 4 字节 UTF-8；孤立代理 → U+FFFD；`\u0000` → `vj_fail(P, "\\u0000 is not representable")`（释放 `out`，不写任何字节） |
| `src/verse/json_min_probe.c` | 新增 76 项检查的离线探针；链接 `src/verse/upp.c`，往返用**已发布**的 `upp_json_write_string`，不是副本 |
| `CMakeLists.txt` | 注册 `verse_json_min_probe`（`add_test` + `TIMEOUT 30` + `LABELS "protocol;json"`），紧邻 `verse_upp_probe` |
| `tools/gate.sh` | `EXP_CTEST` 92 → 93（新增一个 ctest） |
| `tools/crp_engine_crosscheck.js` | 语料 101 → **107** 条（代理对、`\u0001`、`\u00e9\u4e2d`、字面 `\\u0000`）；比较仍是严格的 `a === b`，孤立代理不进语料（理由见 7.4） |

### 7.2 `P->p[0..3]` 前读：先证明，后加固

**结论：原有读在实测中不可证明会越界，但我还是改了。** 两者都写在这里。

- 实测：把输入放进**恰好 `strlen+1` 字节**的堆缓冲区（NUL 是最后一个字节），在 `-fsanitize=address,undefined` 下跑 `"\"\\u004"`、`"\"\\u0041"`、`"\"\\uD83D"` —— **无任何 ASan 报告**。原因是循环第一次无效读就是那个 NUL 本身，而 NUL 在界内：`p[3]` 是终结符、合法可读，随即走 `else` 报 `"bad \\u escape"`，`p[4]` 永不接触。
- 所以旧读**只在调用方保证 NUL 终止时**才成立；没有任何余地留给「少一格 slack 的调用方」。
- 改动：`vj_read_hex4()` 每读一格先判 `h == '\0'` → 立即 `vj_fail(P, "bad \\u escape")`。这个守卫**不改变任何现有输入的报错文本**（4 个十六进制位里出现 NUL 本来就走同一个 `else`），只是把「靠终结符兜底」变成「显式有界」。

### 7.3 三种缺陷的修后字节（同一驱动，与 §1 表逐行对照）

```
\uD83D\uDE00  F0 9F 98 80   （原 ED A0 BD ED B8 80，CESU-8 且非法 UTF-8）
\uD83D        EF BF BD      （原 ED A0 BD）
\uDE00        EF BF BD      （原 ED B8 80）
x\u0000y      NULL + err "\\u0000 is not representable at offset 8"（原 78，即 "x"）
alice\u0000A  NULL          （原 "alice"）
alice\u0000B  NULL          （原 "alice"，两者塌成同一个 C 串）
\u00e9 / \u4e2d / \u0001  不变
```

### 7.4 孤立代理**不进**交叉校验语料：它是固有不一致，不是可归一化的拼写差

第一版实施里我把它当成「同一值的两种拼写」加了 `canonicalRecord()` 归一化——**那是错的，已删除**。Lead 复核指出，事实是：

- 引擎的 `out` 里是 **3 个字节 `EF BF BD`**（原码点 U+FFFD）；
- 参考的 `out` 里是 **6 个 ASCII 字符 `\`,`u`,`d`,`8`,`3`,`d`**（`JSON.stringify` 对未配对代理的转义）。

**线上字节不同、记录文本不同、解码后的值也不同**（U+FFFD vs 未配对代理码元）。更根本的是：一个字符串模型是 UTF-8 字节的引擎**装不下**未配对代理，U+FFFD 是被迫的。所以这与 `\u0000` 拒绝同类——**固有的、要记录的分歧**，不是可以用比较器抹平的等式。

现在的处理：

- **代理对**（`\uD83D\uDE00`）保留在语料里，仍在**严格 `a === b`** 下逐字节一致——那才是真正的契约违规，也已修好。
- 同时保留 `\u0001`、`\u00e9\u4e2d`、`pair in key`、`two pairs`、以及**字面量 `"\\u0000"`（六个可打印字符，不含 NUL 字节）**。
- **删除**两条孤立代理条目，**删除 `canonicalRecord()`**，比较恢复为 `if (a === b) continue;`。
- 引擎对孤立代理的确切字节由 `src/verse/json_min_probe.c` 断言（`\uD83D`/`\uDE00` → `efbfbd`，含 key 位置），这才是钉住它的正确位置。
- 语料：101 → **107** 条（新增 6 条：代理对 ×3、`\u0001`、`\u00e9\u4e2d`、字面 `\\u0000`）。

精确的分歧事实（供 §10.6 记录）：

| | 输入（CRP 行） | 引擎 | 参考（Node） |
| --- | --- | --- | --- |
| 孤立代理 | `{"x":"\uD83D"}` | `ok:true`，`out` 内含 `EF BF BD`，字节 `…2278223a22efbfbd227d7d0a` | `ok:true`，`out` 内含 `\ud83d`，字节 `…2278223a225c7564383364227d7d0a` |
| `\u0000` | `{"x":"x\u0000y"}` | `ok:false`，`"\\u0000 is not representable at offset 48"`，`out:null` | `ok:true`，`out` 为 `{"x":"x\u0000y"}`（转义），解出 `780079` |

交叉校验**无法**覆盖这两条：参考实现 `crp_reference.js` 走 `JSON.parse`，两个输入它都接受。

### 7.5 给 Lead 的合并清单

1. **`docs/BOARD.md` §3 的 gate 表**：`ctest (expect 92/92)` → `93/93`，与 `tools/gate.sh:43` 已改的 `EXP_CTEST=93` 对齐（我不改 BOARD.md）。
2. **`docs/STATUS.md` §10.6**：请追加**两条**分歧（我按 §5 禁令没有改 STATUS.md，草稿如下；精确输入/输出见 7.4 的表）：
   - 「`\u0000`：引擎**拒绝**该行（`ok:false`，`\u0000 is not representable`），Node `JSON.parse` 产出含 NUL 的串并接受（`"x\u0000y"` → 字节 `78 00 79`）。引擎侧所有 writer 都按 `*p` 扫到 NUL 为止，最小只能产出 `\u0001`，故此分歧不缩小引擎可生成的范围。」
   - 「孤立代理：引擎在解码时映射为 **U+FFFD**（`EF BF BD`），Node `JSON.parse` 保留未配对代理码元、`JSON.stringify` 再把它转义成 **六个字符 `\ud83d`**。引擎的字符串模型是 UTF-8 字节，装不下未配对代理，U+FFFD 是被迫的；两边线上字节不同、记录文本不同。引擎侧确切字节由 `src/verse/json_min_probe.c` 钉住。」
3. 其余仍写着 92 的位置（我没动，供 Lead 决定）：`tools/README.md:103`、`docs/STATUS.md:41`、`:48`、`:57`、`:716`、`:729`、`docs/HYGIENE.md:321`、`docs/BOARD.md:54`。

### 7.6 复现命令

```sh
cd /home/sakiko/inimerse/.worktrees/json-min-nul-escape
gcc -std=gnu11 -I src/verse -I src/common -o /tmp/probe \
    src/verse/json_min_probe.c src/verse/json_min.c src/verse/upp.c && /tmp/probe
node tools/crp_engine_crosscheck.js build/verse_crp_probe
bash tools/gate.sh --jobs 4
```

回归必先失败的证据：`git stash push -- src/verse/json_min.c` 后同一探针输出
`json_min_probe: 74 checks, 20 failures`（exit 1），再 `git stash pop` 即恢复。

