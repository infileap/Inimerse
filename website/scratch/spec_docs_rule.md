# 甲‴ · `spec.js` 那 11 项是【手抄】的 —— 而且今天没有任何一条规则在管它们

**规格与「加进去会动哪几个数」，不落地。**
**第五件：开工那一刻 `origin/main = 1a6ad2d1`。读法 = 工作树文件。`HEAD = 65ab646`。**

---

## ① 那 11 项是怎么来的 —— **可判形式**

`website/data/spec.js` 是**手写的**：`website/scratch/` 下五个脚本
（`check_citations.js` / `render_check.js` / `report_diff.js` / `run_snippets.js` / `run_tutorial.js`）
**没有一个是它的生成器**。所以第一层答案是「手抄」。★ 但这不够 —— 手抄也会**遵照**一条规则，得测。

**四条候选规则，逐条施加到那 11 项上：**

| 候选规则 | 11 项里满足 | 判决 |
|---|---|---|
| **R1** `file` 匹配 `docs/*.md`（**恰好一层目录**） | **11 / 11** | 存活 |
| **R3** 台账里有条目挂上去 | **7 / 11** | **falsified** |
| **R4** 有 `headers` | **11 / 11** | 存活（**但见下**） |
| **R5** 有 `role` | **11 / 11** | 存活（**但见下**） |

★ R3 被证伪的那 4 份：`docs/ERROR_CODES_V06.md`、`docs/SYNTAX.md`、`docs/THREADS_V06.md`、`docs/DECFY_DESIGN.md`
—— **它们在页面上站着，而底下一条裁定都没挂**。

### ★★ 可判形式（这是要交的那一件）

> 一条规则 R **真的在管**这张清单，当且仅当：
> **(i) R 在这 11 项上成立**，且 **(ii) R 能排除至少一份「本该被排除」的文档。**

按这条判据：

- **R4 / R5 无法排除任何东西** —— `headers` 与 `role` 是**条目自己的字段**，
  任何被写进 `docs[]` 的条目都带着它们。⇒ **它们不是规则，是同义反复。**
- **R1 能排除**（26 份全排除），但它排除的**理由是路径层数** ——
  一个与「该不该进规范页」**无关**的性质。

⇒ 判据：**一份由规则生成的清单与一份手抄的清单，在清单本身上同形；
把它们分开的不是清单，是【一条能被违反的规则】。而今天一条也没有。**

### ★ 顺带量到的一件事：字数不是约束

`spec.js` 的文件头逐字写着纪律：「`title` 与 `headers` 里的每一行都必须是那份文件里的**逐字**原文，
由 `check_citations.js` 钉住（改一个字必须红）」。⇒ **内容被钉住了，成员资格没有被钉住。**
★ **「每一份都是逐字对的」与「这一份该不该在这里」是两个读数** —— 而只有前者有守卫。

---

## ② 把 R1 施加到 `docs/streams/` 那 26 份 —— **0 / 26**

26 份**全部是两层**（`docs/streams/<名>.md`），而 R1 要的是**恰好一层**。逐份一行：

```
R1=0  docs/streams/agent-bridge.md                R1=0  docs/streams/i-json-user-statements.md
R1=0  docs/streams/aot-backend.md                 R1=0  docs/streams/json-min-nul-escape.md
R1=0  docs/streams/be-removal.md                  R1=0  docs/streams/repo-hygiene.md
R1=0  docs/streams/builtin-platform-census.md     R1=0  docs/streams/surgery-verify-report.md
R1=0  docs/streams/count-size.md                  R1=0  docs/streams/upp-in-engine.md
R1=0  docs/streams/crp-in-engine.md               R1=0  docs/streams/v06-decision-points-delegation.md
R1=0  docs/streams/crp-portal-auth.md             R1=0  docs/streams/v06-decision-points.md
R1=0  docs/streams/crp-portal-postcheck.md        R1=0  docs/streams/vverse-cli.md
R1=0  docs/streams/docs-audit.md                  R1=0  docs/streams/vverse-produce.md
R1=0  docs/streams/eidos-archive-crosscheck.md    R1=0  docs/streams/wasm-simd-gc.md
R1=0  docs/streams/eidos-diagnostic-fields.md     R1=0  docs/streams/win-source-attribution.md
R1=0  docs/streams/eidos-state.md                 R1=0  docs/streams/ws-client-coverage.md
R1=0  docs/streams/gate-hermeticity.md            R1=0  docs/streams/xlang-bridge.md
```

**分母 = 26；按 R1 本该进去的 = 0。** ★ 而 R1 **不是**一条真规则（见 ①`(ii)`），
所以这 0 **不是**「26 份都不该进」的读数 —— **它只是说「它们都是两层」。**

---

## ③ 加进去会移动哪几个读数（**改前的值逐个写出**）

| 读数 | 今天 | 加 26 份之后 | 谁在管它 |
|---|---|---|---|
| `spec.js` `docs[]` 项数 | **11** | 37 | — |
| 守卫印的「规范页元信息：N 份文档」（`check_citations.js:3689`） | **11** | 37 | 无判据，只是印 |
| 页面印的「文档元信息钉在 ref … 上：N 份文档」（`site.js:470`） | **11** | 37 | 无判据 |
| `render_check` 「渲染出的文档块数 = spec.js 里的文档数」（`:753`） | **11 / 11** | **37 / 37** | ★ **不会红** |
| `check_citations.js:264`「每份文档都声明了 `role`」 | 绿 | ★ **26 份缺字段就立刻红** | 有判据 |
| `check_citations.js:266` 逐份钉 `title`/`headers` 逐字 | 绿 | ★ **26 份每一份都要补齐 `title/lines/role/cite/citeQuote/headers`** | 有判据 |
| `check_citations.js:325` 与 `:473` 的 `specFiles.size` | **11** | 37 | 有判据 |
| **★ `check_citations.js:473`「落在规范页清单之外的 N 节」** | **7 节 / 6 份文档** | **会变小** | ★★ **见下** |
| `decisions.js` 条目数 | **58** | **58（不动）** | — |
| 「带身份标记的 docs：扫了 **88** 份 .md」 | **88** | **88（不动）** | 那是全仓扫描，与清单无关 |
| 守卫红数 | **3（已知保留项）** | 3（若补齐字段） | — |
| `render_check` | **✓ 全部通过** | ✓ | — |

### ★★★ ③ 里最要紧的那一格：这一对数是**同一个集合**决定的

`check_citations.js:473` 今天逐字印：

```
· ★ 其中落在规范页清单之外的：**7** 节，分布在 **6** 份文档里 ⇒ 「覆盖 11 份」是一个【射程声明】，不是一个【覆盖率】
```

★ 这一句的**第一半**（「7 节在清单之外」）与**第二半**（「覆盖 11 份」）
**由同一个集合 `specFiles` 决定**，而 `specFiles` 就是 `S.docs.map(x => x.file)`。

⇒ **把清单变大，反例那一格就变小，而【没有任何一节被真的覆盖】。**
⇒ 判据：**「射程声明」自己吃掉了自己的反例** ——
**一个数（覆盖几份）与它的反例（几节在覆盖之外）是同一个集合的两个函数；
把声明做大，反例自动变小，而这两个动作在页面上长得一样。**
★ 这一条**独立于本次要不要动**，请一并裁。

### ★ 还有一条：`render_check` 那条判据**量不出这件事**

`render_check.js:754` 是 `check('规范页：渲染出的文档块数 = spec.js 里的文档数', docs.length === S.docs.length, …)`
—— ★★ **我原先在这里写「等号两边都从 `spec.js` 来」，那是错的**（而且我是在没读那一行的时候就断言的）：
左边 `const docs = host.all((n) => n.tagName === 'ARTICLE' && /spec-doc/.test(n.className));` 是从
**渲染出来的 DOM 节点**里数的，**不是从 `spec.js` 读的**。真正让这个等号恒成立的是 `website/assets/site.js`
里 `S.docs.forEach(...)` 中 **`host.appendChild(box)` 是无条件执行的** ⇒ 一条数据必然产出一个
`<article class="spec-doc">`。⇒ **结论（它绿不出信息）成立，理由是【那个渲染循环的保长度性质】，不是【两边同源】。**
★ 判据：**「两边同源」与「一边是另一边的不变量」在输出里同形 —— 前者说的是【同一个数组读了两遍】
（可以被数据打破），后者说的是【一个转换过程保长度】（只可以被那个渲染器的循环打破）。**
★ 同族（已裁定过的那条）：**两个读数只有在不共享前提时才互为对照。**

---

## ④ 两个反方向对照 —— **★ 两个都退回同义反复，而这就是读数**

**(a) 造一份符合规则的文档，看它被收进。**
★ 今天**做不出来**，因为「符合规则」的定义就是待裁的那件事。
★ 若**假定 R1 就是规则**：(a) = 造一份 `docs/<一层>/x.md` 并把它写进 `spec.js` ⇒ 它被收进。
 **但这是同义反复：写进清单的人就是收进清单的人。** ⇒ **这是一个手工动作，不是一个对照。**

**(b) 造一份不符合规则的文档，看它不被收进。**
★ `docs/streams/` 那 26 份实测 **R1 = 0 / 26**（已实测）。
 **但它们「不被收进」不是因为被拦下 —— 是因为【没有任何机制在读 `docs/streams/`】。**
⇒ **(b) 也是同义反复。**

⇒ ★★★ **④ 的读数**：**一个没有机制在跑的清单，做不出反方向对照。**
你能造的只是「**我改了这个文件**」，不是「**规则放行/拦下了它**」——
**而这两件事在输出里同形。** 要让 ④ 成立，**先得有一条真的在跑的规则**（见 ①`(ii)`）。

---

## ⑤ 不落地 ✓

本件**没有动** `spec.js`、**没有动**页面、**没有建**任何规则或守卫。
唯一写进树里的是这一份 `.md`。

---

## ⑥ 三条路（供裁，我不选）

| | 做什么 | 代价 |
|---|---|---|
| **(i)** | 给规范页**一条真规则**：全扫 `docs/**/*.md` + 一条能被违反的判据（例如「只收带身份标记的」） | ★ 那时 26 份会被**真的筛一遍**，而筛选结果**可能不是 26 也不是 0** —— 今天没人知道是几 |
| **(ii)** | 把清单**改名**：不叫「规范页覆盖的文档」，叫「人工选入的文档」，并把页面上「覆盖」这个词撤掉 | ★ 因为 ③ 里那一对数目**今天在奖励把清单变大** |
| **(iii)** | 不动 | ★ 代价已逐条写在 ③：那 7 节会**永远**在清单之外，而清单每变大一次，它就少一节 |
