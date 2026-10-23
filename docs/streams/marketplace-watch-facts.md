# 作业单：`marketplace-watch-facts` —— 上架 dsh-m 这件事今天到底在哪

**状态**：只读核查完成（**未改任何代码，未动 `main` 工作树**；本轮产物仅本文档一份）
**冲突域**：`docs/streams/marketplace-watch-facts.md`（仅此一份）
**禁碰**：`docs/BOARD.md`、`docs/STATUS.md`、`tools/dsh-inimerse/**`、`src/**`、`tools/**`

---

## 0. 一句话结论

**主板 L102 的阻塞理由已经过期。** 它写的是「PR #1 等待维护者点 *Approve and run*」——而这一个 PR **不是仍在等待，是被维护者关闭了**（`2026-10-03T14:15:28Z`，不是合并、不是关闭后重开），关闭时他在同一条评论里直接给了另一条入口。所以「等」这个动作已经没有对象：要么照他给的入口改投，要么把这条标成「不走这条路」。

---

## 1. 仓库侧

起点 `a16dae2`（`docs: the decfy-design criterion-2 bracket records what the column grew into, not 'none'`）。

`tools/dsh-inimerse/` 的文件清单（11 项）：

```
.gitignore  LICENSE  README.md  cordis.patch.yml  icon.svg  index.js
locale/{en,zh}.json  package.json  verify.mjs
marketplace/{PR_BODY.md, README.md, entry.json, registry.json}
```

**npm 包里的 8 个文件**（由 `package.json` 的 `files` 白名单决定：`["index.js","cordis.patch.yml","README.md","icon.svg","locale/*.json"]`）：

```
LICENSE  README.md  cordis.patch.yml  icon.svg  index.js  locale/{en,zh}.json  package.json
```

**仓库有、包内没有（6 项）**：`.gitignore`、`verify.mjs`、`marketplace/PR_BODY.md`、`marketplace/README.md`、`marketplace/entry.json`、`marketplace/registry.json`。
⇒ 这不是漏打包：`files` 白名单逐字如此，且**仓库与发布包的 `package.json` 逐字相同**（我做了整份 JSON 相等比较，`True`）。`verify.mjs` 是自检脚本，本来就不该进行时依赖。

**逐文件 sha256 比对（共有 8 项）**：7 项 `SAME`；**唯一 `DIFF` 是 `README.md`**：

| | bytes | 行数 |
|---|---|---|
| npm 包 `README.md` | 7476 | 159 |
| 仓库 `README.md` | 7957 | 166 |

**发布时间（`2026-10-01T11:30:27Z`）之后，碰过 `tools/dsh-inimerse/` 的提交共 4 笔**：

```
9e7097e 2026-10-05 06:30:39 +0800  tools: three places asserted a conclusion without its denominator
765a104 2026-10-01 20:19:59 +0800  tools: record the open dsh-m PR and the verified npm artifact
9caf58c 2026-10-01 19:38:22 +0800  tools: write the dsh-m PR body ahead of the fork
714fbc7 2026-10-01 19:37:17 +0800  tools: dsh-inimerse is published on npm; drop the unverifiable icon
```

⇒ **仓库已向前走了 4 笔，npm 上那一份一个字节都没动。** 漂移是真实的，但漂移量很小：只有 `README.md` 差 481 字节 / 7 行，运行时代码 `index.js`（39735 B）与 `cordis.patch.yml` 逐字节相同。

---

## 2. npm 侧

`dsh-inimerse` 在 npm 上**只有一个版本**：

| 项 | 值 |
|---|---|
| `dist-tags.latest` | `0.1.0` |
| 版本列表 | `["0.1.0"]` |
| 最后修改 | `2026-10-01T11:30:27.977Z` |
| tarball shasum (sha1) | `9038702626b5501c2908f743f31ccd6fef047aa5` |
| tarball sha256（我下载后自算） | `073399edc7973a41be0c1a4bbea114f41bc9004cad35e3a63cfd49107a69b0b0` |

`registry.npmjs.org` 与 `registry.npmmirror.com` 的 `shasum` 相同，镜像已同步。
（`npm view` 在本机直接失败：`EROFS: /home/sakiko/.npm/_cacache`，缓存目录不可写；以上全部走 registry HTTP API 读，并用 curl + `sha256sum` 自算校验。）

**结论**：npm 上就是「旧 `README.md` + 无 `verify.mjs`」的那一版，没有第二版可拉。

---

## 3. 本机实际装的是哪一份

`~/.dsh/profiles/web/package.json` 的依赖里写的是：

```json
"dsh-inimerse": "file:/home/sakiko/inimerse/tools/dsh-inimerse"
```

`~/.dsh/profiles/web/node_modules/dsh-inimerse/package.json` 的 `version` = `0.1.0`。

⇒ **本机跑的是仓库工作树，不是 npm 那一份。** 两者版本号同为 `0.1.0`，靠版本号区分不出来；要靠上面 §1 的 `README.md` 差异或 `verify.mjs` 是否存在来分辨。
⇒ 这一条的实际后果：**在本机验收通过，不等于 npm 上那一份通过**。若要验 npm 那一份，得单独装 registry 版本。

---

## 4. 那两条「兼容性阻断」

检查器（`dsh-compat-vet`）报 36 个条目、**阻断 2**：`@deepseek-ai/dsh-base` 与 `@deepseek-ai/dsh-web-app`「bundles 里引用了但 dsh 安装目录里没有」，并说 dsh 启动会打印 `skipping profile bundle` 并跳过它。

**我逐条复核，两条都不成立。**

它找的路径是：

```
…/.pnpm/@deepseek-ai+dsh@0.2.1-rc.2_…/node_modules/@deepseek-ai/dsh/node_modules/@deepseek-ai/dsh-base/package.json
```

该路径**确实不存在**（`MISSING`，两个都 MISSING）—— 检查器没说错它在自己的路径上找不到东西。

但：

1. **dsh 自己的 `package.json` 把两者都声明为 `dependencies`**，版本都钉在 `0.2.1-rc.2`（与运行时同版）。
2. 两者在 pnpm store 里**都有实体**：`@deepseek-ai+dsh-base@0.2.1-rc.2`、`@deepseek-ai+dsh-web-app@0.2.1-rc.2`。
3. **从 dsh 安装目录 `require.resolve` 两个都成功**，分别解析到 store 里的实体路径。
4. `~/.dsh/logs/` 全部日志里，`skipping profile bundle` **只出现过 `dsh-opencode-xdbridge`（5 次）**，**没有任何一次是这两个**。
5. 最近一次启动日志（`dsh-up-20261023-185002.log`）正常打印 `dsh web: http://127.0.0.1:3080/?token=…`。

⇒ **判定：这是检查器的「安装目录」路径假设在 pnpm 布局上失效（它假设依赖会平铺在 `dsh/node_modules/` 下），不是真的缺包，也没有被跳过。** 两条「阻断」应当撤销。

**但有一条是真的、只是不是问题**：这两个包出现在 profile 的 `dsh.profile.bundles`（34 项里头的第 1、2 项），而 profile 的 `dependencies` 里没有它们。这是「由 dsh 安装提供」的正常形态（`scope=@deepseek-ai`），不是缺依赖。

另：`cordis.patch.yml` 里 `@deepseek-ai/dsh-web-app` 只在**一行注释**里出现（`# == @deepseek-ai/dsh-web-app, patched by billion-context`），不是引用。

### 4.1 更正：「解析得到」这一列的口径必须写清起点是符号链接还是真身

本文档初版写的是「从 dsh 安装目录 `require.resolve` 两个都成功」——**这句话口径不明，已被 `ivory-zephyr` 判为复现不了，他判得对**。同一个包、同一台机器，起点不同则结论相反：

| 起点 | 结果 |
|---|---|
| 真身路径 `….pnpm/@deepseek-ai+dsh@0.2.1-rc.2_<hash>/node_modules/@deepseek-ai/dsh` + 裸 `require.resolve` | **成功** |
| 符号链接路径 `…/node_modules/@deepseek-ai/dsh` + `require.resolve(…, {paths:[<该路径>]})` | **`MODULE_NOT_FOUND`** |

原因是 pnpm 的隔离布局：这两个包的真身在 `.pnpm/@deepseek-ai+dsh@0.2.1-rc.2_<hash>/node_modules/@deepseek-ai/` 下以**符号链接**存在，而这一层是 `@deepseek-ai` 的**兄弟**目录、不是它的祖先。`paths` 选项不解析符号链接，只按给定路径逐级向上找 `node_modules`，因此永远不会进入 `.pnpm/<pkg>@<hash>/node_modules` 这一层；从真身进入时，Node 的查找链天然落在那里。

逐条实测的搜索路径（`createRequire(installAnchor).resolve.paths('@deepseek-ai/dsh-base')` 前五项），第 3 项就是落点：

```
$V/.pnpm/@deepseek-ai+dsh@0.2.1-rc.2_<hash>/node_modules/@deepseek-ai/dsh/node_modules
$V/.pnpm/@deepseek-ai+dsh@0.2.1-rc.2_<hash>/node_modules/@deepseek-ai/node_modules
$V/.pnpm/@deepseek-ai+dsh@0.2.1-rc.2_<hash>/node_modules          ← 落点（含 dsh-base / dsh-web-app 两个符号链接）
$V/.pnpm/node_modules
$V
```

### 4.2 dsh 加载 bundle 的真实机制（不是 `require.resolve`）

代码在 `@deepseek-ai/dsh-app-boot/lib/index.js`：

- `:995 resolveBundleDir(binName, packageName, installAnchor, profileDir)` — 依次用两个锚：**先安装锚**（`installAnchor`＝dsh 应用包自己的 `package.json`），后 profile 目录；仍找不到就 **throw**：`cannot resolve profile bundle "<name>" from the dsh installation or <profileDir>; run 'dsh plugin --profile <p> install' if its dependency is not installed`。
- `:976 packageDirFromAnchor(anchor, packageName)` — **不用 `require.resolve`**（注释逐字：`without depending on the package exporting ./package.json (require.resolve would need that)`），而是 `createRequire(anchor).resolve.paths(pkg)` 拿到搜索路径，逐项 `existsSync(join(candidate, "package.json"))`；注释同时写明 `existsSync` **会跟随 pnpm 隔离布局的符号链接**。
- `:499` 起的设计注释逐字：`Module resolution is two-anchor by construction: a bundle name resolves first from the dsh installation (the launcher's own package), then from the profile directory.` —— 「安装优先」是契约，保证 `@deepseek-ai/dsh-base` 一类 in-box bundle **永远与运行的 dsh 同源**，不会取到 profile 里的副本。
- `:538 reportSkippedBundles` — 只负责把 `profile.skippedBundles` 逐条写到 stderr；`skippedBundles` 的成因是**不兼容／已退役／不可读**，且加载期「nothing is printed」，由启动器每次启动调一次。

复跑 `packageDirFromAnchor` 的等价实现（锚＝`@deepseek-ai/dsh` 的真身 `package.json`）：`dsh-base` 与 `dsh-web-app` 都解析到 `.pnpm/@deepseek-ai+dsh@…/node_modules/<包名>`；而**故意给一个不存在的包名**得到 `undefined`，即会走 throw 那条路。

⇒ **「不打印 ≠ 加载成功」这个疑点因此闭合，而且闭合方式是两条不同的报错通道**：bundle **解析不到**是 `throw`（启动直接失败，根本不进 `skippedBundles`）；只有**解析得到但不兼容／退役／不可读**才会进 `skippedBundles`、才会打印 `skipping profile bundle`。所以「两个包既没打印 skip、又启动成功」在同一台机器上不可能同时由「解析失败」造成 —— 那会是一次启动崩溃，而不是一条静默。

---

## 5. PR #1 逐字事实

| 字段 | 值 |
|---|---|
| url | https://github.com/iasiv5/dsh-m/pull/1 |
| title | `registry: 收录 dsh-inimerse（dsh-inimerse@0.1.0，infileap），官方清单条目数断言 23→24` |
| author | `infileap`（月识） |
| head → base | `add-dsh-inimerse` → `main` |
| created | `2026-10-01T12:13:13Z` |
| **state** | **`CLOSED`** |
| mergedAt | `null` |
| closedAt | **`2026-10-03T14:15:28Z`** |
| reviews | **0** |
| checks | **`[]`（一个都没跑）** |

⇒ 主板写「等待维护者点 *Approve and run*」—— 但**这个 PR 从来没跑过任何 check，也从来没等到 review**，它是被直接关掉的。

**维护者（`iasiv5`）在关闭的那一刻留下的那条评论，逐字要点**：

- 先肯定了实现（结构化 JSON 输出、预期失败按值返回、`verify.mjs` 用激活期同一套 schema 断言自检）。
- 说明 dsh-m 是**「社区 + 精选」双层**：`registry.json`（精选）是他手工维护的小集合，主要用来示意自定义清单的用法；**社区目录 `awesome-dsh-plugin`（经 npm 包 `dsh-plugin-catalog` 分发）才是第三方插件的既定收录入口**。
- 给出建议：投 `awesome-dsh-plugin`，按其 `contributing.md` 提交一个新增 YAML 文件的 PR，例如 `data/plugins/infileap__Inimerse--tools-dsh-inimerse.yml`。
- 给出投稿前自查四项，其中**三项他判「已达标」，一项标 ⚠️「待添加（收录硬性要求）」= 仓库加 `dsh-plugin` topic**。
- 补充：精选清单可整体覆盖 —— 托管在任意 HTTPS 地址或本机路径，填进 dsh-m 设置页即生效，社区清单层照常叠加。
- 「这个 PR 我先关闭了」。

**今天复核那个硬性要求**：`GET /repos/infileap/Inimerse/topics` → `{"names": []}`。
⇒ **`dsh-plugin` topic 至今没加**（距离他提出已 20 天）。仓库 `created 2026-08-23`、`pushed 2026-10-23`、`archived false`，活跃度达标。
⇒ npm 包的 `dsh.bundle` manifest 也已具备：`{"bundle": {"patch": "./cordis.patch.yml"}}`。

**所以那条硬性门槛是唯一没做的一件事，而且是一件能在 GitHub 网页上点两下完成的事。**

---

## 6. 建议（供协调者裁决，我没有权限也不打算自己动 BOARD）

三个选项，按我看到的成本排序：

1. **加 topic，改投 `awesome-dsh-plugin`** —— 唯一一个维护者明确指认的入口，且他说的三项自查已达标。剩余工作量：加 topic（网页操作）+ 一个 YAML 文件的 PR。
2. **自维护 `registry.json`** —— 维护者明说精选层可被整体覆盖。适合「只想让 Inimerse 用户看到」的场景，但那就不是「上架 dsh-m」，而是「自己发一份清单」。
3. **标死这条流** —— 如果判断不做分发也成立，就把 L102 从「阻塞」改成「不开」，并在台账里写明理由，别让它无限期挂着当阻塞。

无论选哪个，**L102 现在的措辞都需要改**：它描述的是一个不存在的等待状态。

---

## 7. 边界（我做了什么、没做什么）

- 只读：`git log`／`git show`／`sha256sum`／`curl`（registry HTTP API 与 GitHub API）／`require.resolve`／`grep ~/.dsh/logs`。
- **没有**安装或卸载任何插件，没有 `dsh plugin add/remove`，没有升级 dsh-m，没有改 profile 的 `package.json`。
- **没有**碰 `main` 工作树的任何文件；本文档写在 `stream/marketplace-watch-facts` 工作树里。
- `npm view` 因本机 npm 缓存目录 `EROFS` 不可用，版本数据全部取自 registry HTTP API；tarball 的 sha256 是我下载后自算的，未与任何第三方声明比对（npm 只公布 sha1 `shasum`）。
- 兼容性那两条的结论只覆盖「这两个包被解析得到、且没有被 skip」；**没有**验证它们在 0.2.1-rc.2 下的运行行为是否正常（本会话的工具调用都正常工作，但那不等于这两个 bundle 内部无问题）。
