# 作业单：`vverse-cli` —— 让 `.im` / CLI 产出的 `.vverse` 是**真正的** `.vverse`

**状态**：进行中
**冲突域（别人别碰）**：`src/mod/verse_dist_mod.c`、`src/common/vverse_pack.c`、`src/common/vverse_pack.h`、`CMakeLists.txt`、`tools/gate.sh`、`tools/vverse_*`、`tools/vverse_cli*`、`tools/vverse_cross.test.py`
**禁碰**：`docs/**`（协调者持有）、`src/verse/**`、`src/platform/**`、`Infiverse_standard/**`、`tools/node_suites/**`

---

## 1. 板上原话错在哪（协调者开工前实测）

`docs/BOARD.md` §5 原写「`vverse-cli`：无 `.im`/CLI 入口」。**这句话是错的，已更正。**

- `src/mod/verse_dist_mod.c:2693` 注册了内建：
  `vm_register_builtin_full(vm, "verse_pack", b_verse_pack, 1|CAP_VERSE|CAP_NET, 0)`
- `src/lobby_online_src.im:40` 早就在用：`r1 = verse_pack(".", "universe/room.vverse")`
- 实测一条 3 行 `.im`（`r = verse_pack("/tmp/vvtest/src", "/tmp/vvtest/out.vverse")` + `say r`）跑 `./inimerse` → **exit 0，产出 416 字节文件**。

⇒ **`.im` 能调用打包内建。真正的缺陷是格式。**

## 2. 真正的缺陷

`b_verse_pack`（`src/mod/verse_dist_mod.c:1109`）**自己手工拼 JSON**：拼 `"files":{…}`、用 sha256 **hex**，写出的是**裸 JSON**。实测产物头 32 字节：

```
{"id":"src","version":"1.0.0","publisher":"e4610a1d…
```

而真正的 `.vverse` 是 **gzip 的**：

```
gzip( {"format":"vverse-1","files":{"<path>":"<base64>",…}} )     # mtime = 0
```

契约由**只读参考实现**拥有（见 `src/common/vverse_pack.h` 的抬头注释，以及 `tools/vverse_pack.js:8`）。把内建产物喂给参考实现：

```
node tools/vverse_pack.js preview <pkg>   →  vverse pack: incorrect header check
node tools/vverse_pack.js unpack  <pkg>   →  exit 1
```

⇒ **内建是遗留的另一种格式，与整条工具链不兼容**：`.im` 能打包，但打出来的东西谁也读不了。

## 3. 已有资产（不要重写）

引擎侧**正确的**实现已经存在，且已被 `vverse-produce` 流验证过：

- `src/common/vverse_pack.{h,c}` —— `vverse_pack()` / `vverse_unpack()` / `vverse_validate()` / `vverse_sha256_file()`
- `src/common/gzip.{h,c}` —— 确定性 gzip（写侧 stored、`MTIME=0/XFL=0/OS=3`），使打包成为**树内容的纯函数**
- 双向交叉验证：`tools/vverse_cross.test.py`（48 checks，含 node `crypto.verify` 认可引擎产出的 DER SPKI）

**但这些文件目前只链进 `vverse_pack_probe` 这个可执行文件**（`CMakeLists.txt:131-136`），**没有链进 `inimerse` 主引擎**。这是本流唯一需要动 `CMakeLists.txt` 的原因。

## 4. 要做什么

1. 让 `b_verse_pack` **改走** `src/common/vverse_pack.c` 的容器，不再自己拼 JSON。
   - 把 `src/common/vverse_pack.c` 与 `src/common/gzip.c` 加进 `inimerse` 目标（**不要**复制代码、**不要**再写第二份打包器）。
   - 保持内建在 `.im` 里的调用形态与返回值语义**尽量不变**（`verse_pack(root, out)`；失败要能被脚本看见）。
   - 注意：参考实现「打包时**不改动**源目录」（JS 写者会往源目录里丢一份新的 `signatures/sha256.json`）——引擎侧同样**不得**改动源树，输出必须是树内容的纯函数。
2. **进树回归测试**（新的 `tools/vverse_cli.test.py` 或等价物，注册进 CTest）：
   - 用引擎跑一条 `.im` 脚本调用 `verse_pack`，产出 `.vverse`；
   - 断言该产物能被 `node tools/vverse_pack.js unpack <pkg> <dir>` 解开；
   - 断言解开后的目录能通过 `node tools/vverse_validate.js <dir> --strict --require-signature --require-complete-signature`；
   - 断言 `vverse_pack_probe`（引擎自己的读侧）也能读它（双向）。
3. `tools/gate.sh:43` 的 `EXP_CTEST` 同步 bump（当前 `94`）。
4. 修前必须失败的证据：先跑一次、贴出真实失败输出，再修。

## 5. 验收判据

- [ ] **修前必须失败**：在改动前的引擎上，新测试**必须失败**并贴出原始输出（禁止先改再补测试）。
- [ ] 一条 `.im` 脚本（或 CLI）能直接产出**通过 `node tools/vverse_validate.js <unpacked> --strict --require-signature --require-complete-signature`** 的 `.vverse`。
- [ ] `node tools/vverse_pack.js unpack` 能解开引擎产出的包（**双向**，不是单向）。
- [ ] 打包**不改动源目录**：修前修后对源树做 `find … -printf '%p %s %T@\n' | sort` 或 `sha256` 清单，必须逐字节相同。
- [ ] 打包是**纯函数**：同一棵树打两次，产物**逐字节相同**（确定性 gzip 的既有保证，别弄丢）。
- [ ] `rm -rf build && tools/gate.sh --jobs 4` → **七阶段全 PASS、`gate: OK`、exit 0**，并贴出 `ctest (expect N/N)` 那一行的实际计数。
- [ ] 交付说明必须写清：改了哪些文件、`EXP_CTEST` 的新值、以及**为什么没有写第二份打包器**。

## 6. 明确的边界（别越界）

- **不要**去改 `src/platform/http_posix.c`（超大包已由 `http-truncation-and-ports` 流修完，见 [STATUS.md](../STATUS.md) §10.10）。
- **不要**动 `docs/**` —— 文档由协调者在合并后统一同步（包括 `docs/BOARD.md` 与 `docs/STATUS.md`）。
- **不要** `push`、**不要** `merge` —— 交回分支与 commit 哈希即可。
- **不要**为了让测试变绿而放宽断言；如果发现参考实现与引擎在某个点上真的不一致，**停下并报告**，不要单方面改契约。
- Windows 侧不在范围（`src/headless_server.c` 的孪生问题另记）。
