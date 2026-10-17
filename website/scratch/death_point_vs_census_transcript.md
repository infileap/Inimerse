# 庚⁶ 逐字留档

## 一、对照 (a)：`D = undefined` ⇒ `check_citations.js` 死在哪

最小输入 = 把 `website/data/decisions.js` 里的 `INFIVERSE_DECISIONS` 改名一处（不碰受跟踪文件的原树；在临时 worktree 内改）。

```
$ node website/scratch/check_citations.js
rc=1   输出行数 = 0   FAIL 行 = 0
--- stderr
/home/sakiko/inimerse/.worktrees/wt-geng6/website/scratch/check_citations.js:87
for (const e of D.entries) {
                  ^

TypeError: Cannot read properties of undefined (reading 'entries')
    at Object.<anonymous> (/home/sakiko/inimerse/.worktrees/wt-geng6/website/scratch/check_citations.js:87:19)
```

## 二、对照 (b)：同一个输入 ⇒ `render_check.js` 死在哪

```
$ node website/scratch/render_check.js
rc=1   输出行数 = 0
--- stderr
/home/sakiko/inimerse/.worktrees/wt-geng6/website/scratch/render_check.js:661
    host.children.length === D.entries.length, `${host.children.length} / ${D.entries.length}`);
                               ^

TypeError: Cannot read properties of undefined (reading 'entries')
```

## 三、两个死点在己⁶ 的普查里出现几次（逐字 grep）

```
check_citations.js:81   ⇒ 报告 0  留档 0
check_citations.js:82   ⇒ 报告 0  留档 0
check_citations.js:87   ⇒ 报告 0  留档 0
check_citations.js:208  ⇒ 报告 0  留档 0
render_check.js:661     ⇒ 报告 0  留档 0
```

## 四、那四行逐字（它们满足己⁶ 公布的每一条形状）

```
:81   function doc(file, ref) { return docAtRef(ref || D.ref, file); }
:82   function readAtOf(e) { return e.readAt || D.ref; }
:87   for (const e of D.entries) {
:208  for (const e of D.entries) {
```

## 五、逐变量的来源（`grep` 逐字）

```
website/scratch/check_citations.js:44:const D = sandbox.window.INFIVERSE_DECISIONS;
website/scratch/check_citations.js:259:const S = specBox.window.INFIVERSE_SPEC;
website/scratch/check_citations.js:345:const MSR_SHA = execFileSync('git', ['rev-parse', MSR_SPEC.resolve], { cwd: ROOT, encoding: 'utf8' }).trim();
website/scratch/check_citations.js:357:const ATT_SHA = execFileSync('git', ['rev-parse', ATT_SPEC.resolve], { cwd: ROOT, encoding: 'utf8' }).trim();
website/scratch/check_citations.js:517:const RULING_ROWS = [
website/scratch/check_citations.js:686:const A_ROWS = RULING_ROWS.filter((r) => r.tier === 'A');
website/scratch/check_citations.js:3534:  const CENSUS = (() => {
website/scratch/render_check.js:654:  const D = st.sandbox.INFIVERSE_DECISIONS;
website/scratch/run_snippets.js:192:const manifest = require(path.join(SNIP, 'manifest.js'));
```

## 六、切一条规则，分母就动（逐字输出）

```
跳过含声明关键字的行 ⇒ 命中 = 49
不跳               ⇒ 命中 = 105

=== 只被「跳过声明关键字」这条规则吃掉的行（前 20 处）
  check_citations.js:81   [D]  function doc(file, ref) { return docAtRef(ref || D.ref, file); }
  check_citations.js:82   [D]  function readAtOf(e) { return e.readAt || D.ref; }
  check_citations.js:87   [D]  for (const e of D.entries) {
  check_citations.js:208  [D]  for (const e of D.entries) {
  check_citations.js:238  [D]  const d = doc(D.humanRuled.source.file, D.humanRuled.source.readAt || …
  check_citations.js:266  [S]  for (const sd of (S && S.docs) || []) {
  check_citations.js:268  [S]  const d = docAt(sd.ref || S.ref, sd.file);
  check_citations.js:308  [D]  const d = doc(D.planV07Note.file);
  check_citations.js:320  [S]  const specFiles = new Set(((S && S.docs) || []).map((x) => x.file));
  check_citations.js:321  [D]  const missing = [...new Set(D.entries.map((e) => e.source && e.source.…
  check_citations.js:344  [D]  const MSR_SPEC = D.markerScanRef || { kind: 'pin', resolve: D.ref };
  check_citations.js:355  [D]  const ATT_SPEC = (D.attribution && D.attribution.tree)
  check_citations.js:364  [D]  const at = D.attribution || {};
  check_citations.js:371  [D]  const ledgerFiles = new Set(D.entries.map((e) => e.source && e.source.…
  check_citations.js:606  [RULING_ROWS]  const tableKeys = new Set(RULING_ROWS.map(keyOf));
  check_citations.js:616  [RULING_ROWS]  for (const rel of new Set(RULING_ROWS.map((r) => r.file))) {
  check_citations.js:619  [RULING_ROWS]  const notVerbatim = RULING_ROWS.filter((r) => {
  check_citations.js:686  [RULING_ROWS]  const A_ROWS = RULING_ROWS.filter((r) => r.tier === 'A');
  check_citations.js:687  [A_ROWS]  const missingRead = A_ROWS.filter((r) => !r.auditRead);
  check_citations.js:742  [RULING_ROWS]  const famRows = RULING_ROWS.map((r) => {
```

## 七、己⁶ 的扫描器源码**不在树上**（逐字）

```
$ grep -n '```' website/scratch/unprotected_deref_census_transcript.md
12:```
85:```
```

⇒ 只有第 12~85 行那一段**输出**，**没有源码**。那把尺子落在 `website/scratch/.build/`（第五档）。
⇒ **那 54 的分母可以重读，不能重算。**
