# marketplace-entry-draft — 投稿入口的一页

本页只回答一件事：**把 `tools/dsh-inimerse` 投到 `awesome-dsh-plugin`，人要做的是哪两下，贴什么。**
它不重新论证要不要上架（那件事在 `docs/streams/marketplace-watch-facts.md`：PR #1 是被关闭的、
checks 一个没跑、reviews 0，「等待维护者点 Approve and run」描述的是一个不存在的等待状态）。

## 0. 边界

- 采集方式：只读 `gh api` 读 `awesome-dsh-plugin/awesome-dsh-plugin` 的 `contributing.md`、`data/`、
  `scripts/lib/entries.mjs`、`scripts/check-submission.mjs`；只读本仓库 `tools/dsh-inimerse/` 的
  `package.json`、`index.js`、`README.md`；只读 GitHub 上 `infileap/Inimerse` 的 manifest、topics、目录清单。
- 没有改动 `tools/dsh-inimerse/` 任何字节，没有改 `package.json`，没有碰 DSH profile，没有跑任何 install/upgrade。
- 上游仓库的克隆不在本机：**没有跑过 `awesome-lint`、没有跑过它的站点构建**，本页给的是按文档与校验器源码对齐的形状，不是「CI 已绿」。

## 1. 两下

1. 给 `infileap/Inimerse` 加 topic `dsh-plugin`。这是收录的硬性要求，`contributing.md` 明写，且今天实测仍是空的（`GET /repos/infileap/Inimerse/topics` → `{"names": []}`）。
2. 在 `awesome-dsh-plugin` 上开 PR，**只加一个文件** `data/plugins/infileap__Inimerse--tools-dsh-inimerse.yml`，内容是 §3。不要动那两个 README（它们是生成的），不要动别人的条目。

其余全部由 CI 与维护者承接：不需要跑任何命令，不需要在条目里写 npm 字段，不需要写截图字段。

## 2. 文件名是算出来的，不是起的

`scripts/lib/entries.mjs:60` 逐字：

```js
export function slugFor(url) {
  const p = url.replace(/^https:\/\/github\.com\//, '').replace(/\/+$/, '')
  const repo = p.split('/').slice(0, 2).join('/')
  const sub = p.includes('/tree/') ? p.split('/tree/')[1].replace(/^[^/]+\//, '') : null
  const base = repo.replaceAll('/', '__')
  return sub ? `${base}--${sub.replaceAll('/', '-')}` : base
}
```

代入 `url = https://github.com/infileap/Inimerse/tree/main/tools/dsh-inimerse`：

| 步 | 值 |
| --- | --- |
| `p` | `infileap/Inimerse/tree/main/tools/dsh-inimerse` |
| `repo` | `infileap/Inimerse` |
| `sub`（去掉 `main/` 后） | `tools/dsh-inimerse` |
| `base` | `infileap__Inimerse` |
| 结果 | **`infileap__Inimerse--tools-dsh-inimerse`** |

`entries.mjs:191` 会拿 `path.basename(file,'.yml')` 与它比对，不等就报
`filename must match the url — expected …yml`。维护者举例给的文件名与这个算法逐字相同，
所以这一格不是猜的。

## 3. 候选 YAML（可直贴）

```yaml
url: https://github.com/infileap/Inimerse/tree/main/tools/dsh-inimerse
name: infileap/Inimerse#tools/dsh-inimerse
category: tools
description:
  en: Five DSH tools that build, test and run the Inimerse engine, and drive its Verse layers over the server's line protocol.
  zh: 五个 DSH 工具，用于构建、测试与运行 Inimerse 引擎，并通过服务端行协议驱动其 Verse 层。
# icon:                     # 见 §5.2 —— 今天写不出来，故意留空
# tarball:                  # 不需要：已发 npm（见 §5.4）
```

三点写法上的硬约束，来自 `contributing.md` 与校验器：

- `description.en` 是**唯一必填**的描述；`zh` 可省，缺翻译算维护者的活。
- 描述里含 `: `（半角冒号加空格）**必须加引号**，否则 YAML 当成嵌套键。上面两条都不含，故不加引号。
- `description` 必须**单行**（`entries.mjs:221` 检查 `\n`），且会被维护者**逐字对着代码核**：写「五个工具」就应该正好有五个，写某个命令名它就应该存在。

`name` 的一格是被校验的：`entries.mjs:202` 只在 `name` 含 `/` 时比对 `name.split('#')[0]` 与该 url 的 `owner/repo`。
`infileap/Inimerse#…` 的 `#` 之前正是那个仓库，逐字相等（比较时两边都小写）；`#` 之后是 monorepo 子路径的自由标签。

## 4. 逐字段：值、出处、是否已定

| 字段 | 取值 | 出处 | 今天是否已定 |
| --- | --- | --- | --- |
| `url` | `https://github.com/infileap/Inimerse/tree/main/tools/dsh-inimerse` | monorepo 子包规则；默认分支实测 `main` | 已定 |
| `name` | `infileap/Inimerse#tools/dsh-inimerse` | `contributing.md` 的 `owner/repo#subname`；`entries.mjs:202` 的比对规则 | 已定 |
| `category` | `tools` | 见 §5.3 | 已定（可被维护者改） |
| `description.en` | 见 §3 | 见 §4.1 的逐条核对 | 已定 |
| `description.zh` | 见 §3 | 同上 | 已定（可省） |
| `icon` | **不写** | 见 §5.2 | **不写** |
| `tarball` | **不写** | 见 §5.4 | **不写** |
| 文件名 | `infileap__Inimerse--tools-dsh-inimerse.yml` | §2 | 已定 |

`ENTRY_KEYS` 白名单（`entries.mjs:166`）逐字是
`url` / `name` / `category` / `description` / `icon` / `tarball`（`file` 由读入器加，不是作者写的）。
**白名单之外任何键都会被拒**，且拒绝文案明写「Nothing reads anything else」。

### 4.1 「五个工具」这句是对着代码数出来的

`tools/dsh-inimerse/index.js` 里 `ctx.tools.register` 恰好五次，名字逐字为
`inim_status`、`inim_build`、`inim_test`、`inim_run`、`inim_verse`。
`README.md` 的 Tools 一节同样是五条，且声明「the five tools below」。
描述里的「build, test and run」对应 `inim_build`／`inim_test`／`inim_run`；
「drive its Verse layers over the server's line protocol」对应 `inim_verse`（经 `inim-server` 的换行分隔 JSON 行协议）。
`inim_status` 未被单列，因为它是只读快照，不构成「build/test/run」之一 —— 这一点是有意不说满的：描述宁可少说，不可多说。

## 5. 今天确定不了的东西（照实列）

### 5.1 `verified` —— **它不是字段**

`contributing.md` 里没有 `verified`，`ENTRY_KEYS` 里也没有。
它出现在 `check-submission.mjs` 的输出语汇里（`unverified` / `nothing checked — …`），
那是**校验器自报「这一项没核成」的状态**，不是条目可以声明的属性。
所以这一格不需要「等确定」——它根本不该被写进 YAML，写进去会被 `unknown field` 拒。

### 5.2 `icon` —— 今天写不出来，原因是现成图标是 SVG

`contributing.md` 的图标一节只收**位图**：`png` / `jpg` / `jpeg` / `webp`，
且明写「SVG 与第三方图床会被拒绝」；`entries.mjs:117` 的 `ICON_EXTENSIONS` 逐字就是这个四种。

实测：`tools/dsh-inimerse/` 目录下唯一的图像是 `icon.svg`（297 字节），
**没有** png / jpg / jpeg / webp；也没有 `screenshots.json`。
所以 `icon` 今天要么不写（市场回退到作者 GitHub 头像），要么先往仓库里加一张方图再写。
两条路都不属于本页能替人做的决定 —— 尤其「往 `tools/dsh-inimerse/` 加文件」被派工明令禁止。

### 5.3 `category` —— 选 `tools`，不选 `dev`

`contributing.md` 说分类「选贴合插件实际做的事，不贴合维护者会改，不会打回」，且分类体系本身在漂移
（`security` / `browser` / `git` / `docs` / `remote` / `voice` 都是从 `tools`、`ui`、`dev` 里拆出来的）。
这个插件做的是「给引擎的构建、测试、运行与协议会话各造一个一等工具」，落在 `tools` 最接近。
`dev` 也能自圆其说，但那是「开发流程本身」那一类；此处不如 `tools` 直白。
**这一格不承担正确性风险：选错不被打回，维护者直接改。**

### 5.4 npm 关联 —— 不需要字段，但有一处规则没写清

- 条目里**没有任何 npm 字段**；手写 `npm:` 会被校验器直接拒（`contributing.md` 明写）。
- 采集是自动的：包已发布 `dsh-inimerse@0.1.0`，`repository` 为
  `git+https://github.com/infileap/Inimerse.git` 且 `directory: tools/dsh-inimerse`。
- **没写清的一处**：规则说「已发布包的 `repository` 字段必须指回本列表收录的那个仓库」。
  本条目收录的 `url` 是**子目录**，而包的 `repository` 指回**根仓库**。原文的「那个仓库」在
  monorepo 条目上指根还是指子目录，文档没有交代；`directory` 字段是否被采信也没有交代。
  ⇒ 这属于「可能被判定为未关联（后果只是没有下载量数字，不影响收录）」，不是「会导致被拒」，所以不阻塞提交。

### 5.5 `screenshots` —— 可选，今天没有

推荐但不必须；声明方式是在**自己仓库**里放 `screenshots.json`（monorepo 放子目录里）。
实测 `tools/dsh-inimerse/screenshots.json` 为 404。不写不影响收录（市场会从 README 抽图）。
本页不做，理由同 §5.2：往那个目录加文件不在授权范围内。

## 6. 文档与代码的一处不一致（照实写）

`contributing.md` 说 manifest 从「root, or a `packages/` · `plugins/` · `apps/` subpackage」读，
字面上**不包含 `tools/`**，而本条的 manifest 恰好住在 `tools/dsh-inimerse/`。

但校验器源码是另一回事：`check-submission.mjs:284` 先直接读
`repos/<repo>/contents/<sub>/package.json`，`sub` 来自条目 url 的 `tree/` 之后那段；
读到就判 `dsh.bundle` 在不在（`:291`），**不限定子目录名叫什么**。
它自己的文件头注释（`:3`）写的是「monorepos put it in `packages/`, `plugins/`, `extensions/`, `bundle/`, `npm/`, ...」。

⇒ 结论：**`tools/` 会被接受，`contributing.md` 那一行的枚举只是没跟上代码。**
证据不是读注释得来的，是实测的：`GET /repos/infileap/Inimerse/contents/tools/dsh-inimerse/package.json` 返回 200，
内容含 `"dsh": {"bundle": {"patch": "./cordis.patch.yml"}}`。
若 CI 在这一格失败，那说明线上跑的版本比本页读到的源码旧或新，届时错误文案会指名要改什么（§7）。

## 7. 提上去之后 CI 依次做什么

`contributing.md` 的「What CI checks」四项，顺序固定：

1. **条目数** 每个 PR 最多 3 条，最先检查、早于任何网络请求。本条是 1 条。
2. **`dsh.bundle`** 见 §6。
3. **仓库年龄** 创建满 1 天。实测 `created_at = 2026-08-23T10:40:22Z`，远超门槛。
4. **`awesome-lint` 与站点构建** 双语一致性、分隔符、日期、截图。本页给的两条描述都非空、单行，`zh` 存在即不触发回退。

失败时在同一分支推修复即可，不必重开 PR。

## 8. 边界与未核项

- **未跑**：`awesome-lint`、`node scripts/generate-readme.mjs`、站点构建。它们的仓库不在本机。
- **未核**：`data/plugins/` 里是否已有覆盖同一件事的条目（去重是维护者评审项之一，不是 CI 项）。
  抽查只做了「maintainer 说社区层是既定入口」这一层的确认。
- **未做**：加 topic（需要人类账号权限）、开 PR（需要人类决定）。
- **不做**：任何对 `tools/dsh-inimerse/`、`package.json`、DSH profile 的改动；任何 install/upgrade。
- 上游 `contributing.md` 会变；本页所有行号取自 2026-10-23 读到的 `contributing.md`（30906 字节）与
  `scripts/lib/entries.mjs`、`scripts/check-submission.mjs`。行号会漂，**引用时以形状（函数名、白名单、拒绝文案）为准**，
  必要时重取。
