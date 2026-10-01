# PR to `iasiv5/dsh-m`

Title:

```
registry: 收录 dsh-inimerse（dsh-inimerse@0.1.0，infileap），官方清单条目数断言 23→24
```

Body:

---

### 收录条目

| 字段 | 值 |
| --- | --- |
| `id` / `npm` | `dsh-inimerse` |
| `name` | Inimerse Bridge |
| `category` | `tools` |
| `source` | `npm` |
| `github` | `infileap/Inimerse` |
| `verified` | `0.2.0-rc.2` |

**这是什么**：Inimerse 是一个 C 写的嵌入式脚本引擎，自带 Verse 事件日志层。
这个插件把它作为 5 个 DSH 工具接进 agent 会话——`inim_status`（读仓库与构建状态）、
`inim_build`（CMake 配置与构建）、`inim_test`（跑 CTest 并**只转述 CTest 自己的判定**）、
`inim_run`（跑 `.im` 脚本）、`inim_verse`（跨真实 client/server 边界驱动 Verse 协议）。
插件只调用引擎自己的二进制与协议，**不复制引擎逻辑**；`inim_verse` 永不发送
`seq`/`rev`/`head`/`balance`/`state_hash`/`committed`，这些一律由服务端指派。

**为什么 `source` 只能是 npm**：`github` 源会把**仓库根**当成包来安装，而
`infileap/Inimerse` 的仓库根是引擎本身（C / CMake），插件位于
`tools/dsh-inimerse/` 子目录，dsh-m 会跳过 monorepo 子包条目。因此包已发布到 npm：
`dsh-inimerse@0.1.0`。

### 变更

| 文件 | 改动 |
| --- | --- |
| `registry.json` | 追加一条，19 行，纯追加 |
| `tests/registry.test.mjs` | `parsed.registry.plugins.length` `23` → `24` |

### 自查

- `npm ci` → `npm run build` → `[dsh-m] build ok: lib/host.js + lib/client.js`
- `node scripts/validate-registry.mjs` → 本条目 `npm` / `github` / `homepage` 三项全过
- `npm test` → 只剩 `tests/run-command-error-digest.test.mjs` 两个既有的、依赖环境的
  用例；在**未改动的** `f18fc81` 上检出后单跑该文件，复现同一断言，确认与本 PR 无关
- 未使用 `icon` 字段：`raw.githubusercontent.com` 在本机不可达（`curl --noproxy '*'`
  与 Node `fetch` 均连接重置），无法自证其可达性；上游 23 条**也都没有用 `icon`**，
  且 dsh-m 会回退到 GitHub 拥有者头像
  （`lib/client.js`：`entry.icon || https://github.com/<owner>.png?size=64`）。
  如需要，可在能访问 `raw.githubusercontent.com` 的环境里另行补一条。

---

## How this branch was prepared

`/home/sakiko/inimerse/.dshm-pr` — clone of upstream `f18fc81`, branch
`add-dsh-inimerse`, commit `67a16c9`, pushed to `infileap/dsh-m`.

```console
$ git -C /home/sakiko/inimerse/.dshm-pr diff --stat f18fc81
 registry.json           | 19 +++++++++++++++++++
 tests/registry.test.mjs |  2 +-
 2 files changed, 20 insertions(+), 1 deletion(-)

$ cd /home/sakiko/inimerse/.dshm-pr && node scripts/validate-registry.mjs
…
✓ [dsh-inimerse] npm 包存在：dsh-inimerse
✓ [dsh-inimerse] GitHub 仓库存在：infileap/Inimerse
✓ [dsh-inimerse] homepage 可达
registry 校验通过
```

Opened as `iasiv5/dsh-m#1`. The `Registry check` run is `action_required` with
zero jobs — GitHub's first-time-contributor gate, waiting on a maintainer's
*Approve and run*.
