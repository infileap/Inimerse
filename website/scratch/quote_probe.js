// 己⁷ 探针：把「引号」做成一条可复核的线。
// 判据在 quote_criteria.md（第一节逐字写下了三个口径）。本件只量不写。
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = process.argv[2] || '.';
const SHELL_META_NAIVE = /[;&|<>$()`\n\\]/;   // 口径 ①：不认引号
const GIT_READONLY = new Set(['ls-files','ls-tree','rev-list','rev-parse','show','grep','cat-file','merge-base','count-objects','log','diff','status']);
const SAFE_HEAD = new Set(['grep','wc','ctest','git','sed','python3','node','cmake','cargo','bash','ls-files','ls-tree']);

// ---- 口径 ①「不认引号」----
function metaN(s) { const m = s.match(SHELL_META_NAIVE); return m ? m[0] : null; }

// ---- 口径 ②「认单引号与双引号」= 戊⁷ 那一版逐字 ----
function metaQ2(s) {
  let q = null, back = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q) { if (c === q) q = null; continue; }
    if (c === "'" || c === '"') { q = c; continue; }
    if (c === '\\') { back = back || c; continue; }
    if (';&|<>$()`\n'.includes(c)) return c;
  }
  return back;
}

// ---- 口径 ③「认单引号、双引号，且双引号里的 $ 与反引号仍算展开」----
// 判据 Q 第一节第 3/4/5 条：单引号内一律不命中；双引号内只有 $ ` \ 命中。
function metaQ3(s) {
  let q = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q === "'") { if (c === "'") q = null; continue; }        // 单引号内：什么都不算
    if (q === '"') {                                             // 双引号内：只有 $ ` \ 算
      if (c === '"') { q = null; continue; }
      if (c === '\\') { i++; continue; }
      if (c === '$' || c === '`') return c;
      continue;
    }
    if (c === "'" || c === '"') { q = c; continue; }
    if (c === '\\') { i++; return '\\'; }
    if (';&|<>$()`\n'.includes(c)) return c;
  }
  return null;
}
const META = { n: metaN, q2: metaQ2, q3: metaQ3 };
const LABEL = { n: '① 不认引号', q2: '② 认单双引号', q3: '③ 双引号里的 $/反引号仍算展开' };

const GIT_OPTS_WITH_ARG = new Set(['-C','-c','--git-dir','--work-tree','--namespace','--exec-path']);
function gitSub(cmd) {
  const t = cmd.trim().split(/\s+/);
  if (t[0] !== 'git') return null;
  for (let i = 1; i < t.length; i++) {
    const x = t[i];
    if (GIT_OPTS_WITH_ARG.has(x)) { i++; continue; }
    if (x.startsWith('--') && x.includes('=')) continue;
    if (x.startsWith('-')) continue;
    return x;
  }
  return null;
}

// ---- 五条规则（分母一律用【不认引号】那一版，判据 Q0.3）----
function rulesHit(cmd, key) {
  const s = cmd.trim();
  const hit = [];
  if (!s) { hit.push(['R0','空']); return hit; }
  const m = META[key](s);
  if (m) hit.push(['R1','含 shell 元字符：' + JSON.stringify(m)]);
  const head = s.split(/\s+/)[0];
  if (!SAFE_HEAD.has(head)) hit.push(['R2','可执行名不在允许表：' + head]);
  if (head === 'git') {
    const sub = gitSub(s);
    if (!sub) hit.push(['R3','git 后面没有子命令']);
    else if (!GIT_READONLY.has(sub)) hit.push(['R4','git 子命令非只读：' + sub]);
  }
  if (['bash','node','python3','cmake','cargo'].includes(head)) hit.push(['R5','本探针不跑这一族：' + head]);
  return hit;
}
const isMine = (hit) => hit.some(([k]) => k !== 'R0');

// ---- 载体 ----
const files = execFileSync('git', ['-c','core.quotepath=false','ls-files','-z','--','*.md'], { cwd: ROOT, encoding: 'utf8' }).split('\0').filter(Boolean);
const N1 = /\*\*(\d[\d,]*)\*\*/g, N2 = /\*\*([^*]*?\d[^*]*?)\*\*/g;
const s1 = (l) => [...l.matchAll(N1)].map((m) => m[1]);
const s2 = (l) => [...l.matchAll(N2)].map((m) => (m[1].match(/\d[\d,]*/) || [''])[0]).filter(Boolean);
const nums = (l) => { const a = s1(l); return a.length ? a : s2(l); };

const rows = [];
for (const f of files) {
  let t; try { t = fs.readFileSync(path.join(ROOT, f), 'utf8'); } catch { continue; }
  t.split('\n').forEach((line, i) => {
    const ns = nums(line);
    if (!ns.length) return;
    const spans = [...line.matchAll(/`([^`]*)`/g)].map((m) => m[1]);
    const cmds = spans.filter((s) => !isMine(rulesHit(s, 'n')) || SAFE_HEAD.has(s.trim().split(/\s+/)[0]));
    rows.push({ file: f, line: i + 1, num: ns[0], spans, cmds, text: line });
  });
}
const withCmd = rows.filter((r) => r.cmds.length > 0);
const five = [];
for (const r of withCmd) {
  const cmd = r.cmds[0];
  const hit = rulesHit(cmd, 'n');
  if (!isMine(hit)) continue;
  five.push({ ...r, cmd, hit });
}
const say = (s = '') => console.log(s);
const at = (r) => r.file + ':' + r.line;

say('=== Q0：载体与分母（一律承戊⁷ 的定义，不重定义）');
say('  载体（受跟踪 .md）        = ' + files.length);
say('  分母 B（带命令的处）      = ' + withCmd.length + '   ⇒ 戊⁷ 是 583');
say('  ⑤ 我的射程之外            = ' + five.length + '   ⇒ 戊⁷ 是 245');
say();

// ---- Q0.1：两个集合不是同一个 ----
const r1n = five.filter((r) => r.hit.some(([k]) => k === 'R1'));                       // 136
// ★ 戊⁷ 的 ③「根本不是命令」= 54，由一个分类脚本给出，而那个脚本在
//   website/scratch/.build/（第五档，gitignored）⇒ **本件不能重算它**。
//   这里【不重建】那个集合，只把「本件能算的两个集合」与它报的 54 并排。
const tier3 = null;
const stillR1 = r1n.filter((r) => META.q2(r.cmd));                                      // 59
say('=== Q0.1：派件说「那 59 处（③ 档）」—— 它把两个集合当成了一个');
say('  集合 A = R1 在①下命中                     = ' + r1n.length + '   （戊⁷ 报 136）');
say('  集合 B = R1 在①下命中且【在②下仍命中】     = ' + stillR1.length + '   （戊⁷ 报 59）');
say('  集合 C = 戊⁷ 五档里的 ③「根本不是命令」     = 54（戊⁷ 报的数，本件不能重算 —— 见下）');
const key = (r) => at(r);
const A = new Set(r1n.map(key)), B = new Set(stillR1.map(key));
const inter = (x, y) => [...x].filter((k) => y.has(k));
say('  ★ A∩B = ' + inter(A,B).length + '   |A\\B| = ' + (A.size - inter(A,B).length));
say('  ★ 戊⁷ 的 ③ 报的是 54，而它由 `website/scratch/.build/classify.js`（第五档、gitignored）给出');
say('    ⇒ **本件不能重算 54**（判据 Q6.5：第五档的数可以重读、不能重算）。');
say('    ⇒ 但【B = 59】与【C = 54】是不是同一个集合，**不需要重算就能否**：');
say('      B 的定义里【含 R1 命中】这一条是构造性的，C 的判词是「那段反引号里根本不是一条命令」；');
say('      两者的定义不同源 ⇒ 除非有人量过 |B∩C|，否则「59 与 54 是同一个集合」是一条**没有分母的断言**。');
say('  ⇒ 本件按【B = 那 59 处】为对象往下走（派件后面每一问都只能问在 B 上）。');
say();

// ---- Q1：三个口径并排 ----
say('=== Q1：三个口径并排（对象 = 那 59 处）');
say('  口径                        R1 命中数');
for (const k of ['n','q2','q3']) {
  say('  ' + LABEL[k].padEnd(28) + stillR1.filter((r) => META[k](r.cmd)).length);
}
say('  ★★★ 三个数都是 59 —— 而这不是一条读数，是一个【恒等式】：');
say('      B 的定义就是「在①下命中【且】在②下仍命中」；③ ⊇ ② 成立（下面量到 0 处反例）⇒ 在 B 上必然同判。');
say('      ⇒ **问「B 在三个口径下变成几」，答案只能有一个：|B|。** 本件照实报，并把它与下一节的数并排。');
const onlyQ3 = stillR1.filter((r) => !META.q2(r.cmd) && META.q3(r.cmd));
say('  ★ 只有口径 ③ 收的 = ' + onlyQ3.length + ' 处（恒等于 0，同上）。');
say();
say('=== Q1b：同样三个口径，用在全部 ' + r1n.length + ' 处（集合 A）上（并排，不许合）');
for (const k of ['n','q2','q3']) say('  ' + LABEL[k].padEnd(28) + r1n.filter((r) => META[k](r.cmd)).length);
const q2q3 = r1n.filter((r) => !META.q2(r.cmd) && META.q3(r.cmd));
say('  ★★★ ② 与 ③ 的差 = ' + q2q3.length + ' 处 —— **这才是派件问的那个问题的真读数**（不是 59 上那个恒等式）：');
for (const r of q2q3) say('    ' + at(r).padEnd(46) + '  ②=null ③=' + JSON.stringify(META.q3(r.cmd)) + '   cmd=`' + r.cmd + '`');
const q1q2 = r1n.filter((r) => META.n(r.cmd) && !META.q2(r.cmd));
say('  ★ ① 与 ② 的差 = ' + q1q2.length + ' 处（引号里的字面量）—— 逐处已列在戊⁷ 的读数里，本件不重复。');
say('  ★ ② 与 ③ 的差【不是】① 与 ② 的差的子集吗？ 交 = ' + q2q3.filter((r) => !META.q2(r.cmd)).length + '（构造上全在 ①\\② 里）');
say();

// ---- Q1c：那 6 处「两者都无」 ----
say('=== Q1c：那 6 处「两者都无」，在三个口径下各落什么');
const both = (s) => ({ pipe: /[|]/.test(s), hole: /<[^=]/.test(s) });
const six = stillR1.filter((r) => !both(r.cmd).pipe && !both(r.cmd).hole);
say('  命中 < > 或 | 的：真管道 = ' + stillR1.filter((r) => both(r.cmd).pipe).length + ' ／ 含占位符 = ' + stillR1.filter((r) => both(r.cmd).hole).length + ' ／ 两者都无 = ' + six.length);
for (const r of six) {
  say('  ' + at(r).padEnd(46) + '  ①=' + JSON.stringify(META.n(r.cmd)) + ' ②=' + JSON.stringify(META.q2(r.cmd)) + ' ③=' + JSON.stringify(META.q3(r.cmd)) + '   cmd=`' + r.cmd + '`');
}
say('  ★ 在口径 ③ 下【仍落 ③】的 = ' + six.filter((r) => META.q3(r.cmd)).length + ' / ' + six.length);
say();

// ---- Q3：四档（对象 = 59）----
say('=== Q3：四档（对象 = 那 59 处，和必须 = 59）');
const t1 = [], t2 = [], t3 = [], t4 = [];
for (const r of stillR1) {
  const a = !!META.n(r.cmd), b = !!META.q2(r.cmd), c = !!META.q3(r.cmd);
  if (a === b && b === c) (a ? t1 : t3).push(r);
  else if (a === b && b !== c) t2.push(r);
  else t4.push(r);
}
say('  ① 三个口径同判（都命中）   = ' + t1.length);
say('  ② 只有第三个口径改判        = ' + t2.length);
say('  ③ 三个口径都不收            = ' + t3.length);
say('  ④ 判不出                    = ' + t4.length);
say('  ★ 和 = ' + (t1.length + t2.length + t3.length + t4.length) + '   ⇒ 分母 59  ' + ((t1.length+t2.length+t3.length+t4.length) === stillR1.length ? '✓' : '✗ 红'));
say('  --- ② 逐处（这些就是「双引号里的 $／反引号」）');
for (const r of t2) say('    ' + at(r).padEnd(46) + '  ①=' + JSON.stringify(META.n(r.cmd)) + ' ②=' + JSON.stringify(META.q2(r.cmd)) + ' ③=' + JSON.stringify(META.q3(r.cmd)) + '   cmd=`' + r.cmd + '`');
say('  --- ④ 逐处（带理由）');
for (const r of t4) say('    ' + at(r).padEnd(46) + '  ①=' + JSON.stringify(META.n(r.cmd)) + ' ②=' + JSON.stringify(META.q2(r.cmd)) + ' ③=' + JSON.stringify(META.q3(r.cmd)) + '   cmd=`' + r.cmd + '`');
say();

// ---- Q4：「关于命令的期望」能不能与「命令」分开 ----
say('=== Q4：「关于命令的期望」能不能与【命令】分开');
const EXEC = /^\s*([A-Za-z_][\w.-]*)\b/;
const noExec = stillR1.filter((r) => !EXEC.test(r.cmd));
const withExec = stillR1.filter((r) => EXEC.test(r.cmd));
const noExecNoMeta = noExec.filter((r) => !META.q3(r.cmd));
say('  ★ 候选判据：「一条反引号里的文本，若不含任何可执行名、也不含任何 shell 元字符，它就不能是一条命令」');
say('    方向 1（无头名 ⇒ 不是命令）：命中 = ' + noExec.length + ' / 59');
say('    方向 2（有头名 ⇒ 是命令）：命中 = ' + withExec.length + ' / 59');
say('    两者同时为真（无头名且无元字符）= ' + noExecNoMeta.length);
say('  ★ 反向不成立的反例（有可执行名、却是关于命令的期望）：');
const expectLike = stillR1.filter((r) => /\bexpect\b/i.test(r.cmd) || /\(\s*expect/.test(r.cmd));
for (const r of expectLike) say('    ' + at(r).padEnd(46) + '   cmd=`' + r.cmd + '`');
say('    ⇒ 共 ' + expectLike.length + ' 处。含可执行名 ≠ 是一条会跑的命令。');
say();
say('  ★ 第二条候选判据（**形状判据**，不逐处读）：「第二个 token 以 ( 开头」');
const shape = stillR1.filter((r) => /^\S+\s+\(/.test(r.cmd.trim()));
const shapeAll = five.filter((r) => /^\S+\s+\(/.test(r.cmd.trim()));
say('    在 59 上命中 = ' + shape.length + ' ／ 在全部 ' + five.length + ' 处上命中 = ' + shapeAll.length);
for (const r of shapeAll) say('      ' + at(r).padEnd(46) + '   cmd=`' + r.cmd + '`');
say('    ★ 这条判据【可判】（只读形状，不读语义），而它命中的全部都在 59 里。');
const fp = shape.filter((r) => !expectLike.includes(r));
say('    ★ 假阳性（形状命中但不含 expect）= ' + fp.length + ' 处');
for (const r of fp) say('      ' + at(r).padEnd(46) + '   cmd=`' + r.cmd + '`');
say();

// ---- Q5：两个反方向对照 ----
say('=== Q5：两个反方向对照');
const ctlOutside = { cmd: "git ls-files | wc -l" };
const ctlInside = { cmd: "grep -c 'add_test(' CMakeLists.txt" };
for (const [nm, c] of [['(a) 元字符在引号外', ctlOutside], ['(b) 元字符在引号里', ctlInside]]) {
  const a = META.n(c.cmd), b = META.q2(c.cmd), d = META.q3(c.cmd);
  say('  ' + nm + '  cmd=`' + c.cmd + '`');
  say('     ①=' + JSON.stringify(a) + '  ②=' + JSON.stringify(b) + '  ③=' + JSON.stringify(d));
}
say('  ★ (a) 该三个都命中 ⇒ ' + (META.n(ctlOutside.cmd) && META.q2(ctlOutside.cmd) && META.q3(ctlOutside.cmd) ? '✓' : '✗ 红'));
say('  ★ (b) 该 ① 命中、②③ 不命中 ⇒ ' + (META.n(ctlInside.cmd) && !META.q2(ctlInside.cmd) && !META.q3(ctlInside.cmd) ? '✓' : '✗ 红'));
say();

// ---- 空集对照 ----
say('=== Q5b：空集对照 —— 对象换成空集，四档必须全 0');
function tiersOf(list) {
  const a=[],b=[],c=[],d=[];
  for (const r of list) {
    const x = !!META.n(r.cmd), y = !!META.q2(r.cmd), z = !!META.q3(r.cmd);
    if (x===y && y===z) (x?a:c).push(r);
    else if (x===y && y!==z) b.push(r);
    else d.push(r);
  }
  return [a.length,b.length,c.length,d.length];
}
const t = tiersOf([]);
say('  空集：① ' + t[0] + ' ／ ② ' + t[1] + ' ／ ③ ' + t[2] + ' ／ ④ ' + t[3] + '  ⇒ ' + (t.every((x)=>x===0) ? '✓' : '✗ 红'));
const t59 = tiersOf(stillR1);
say('  那 59 处：① ' + t59[0] + ' ／ ② ' + t59[1] + ' ／ ③ ' + t59[2] + ' ／ ④ ' + t59[3] + '  ⇒ 和 ' + t59.reduce((x,y)=>x+y,0) + (t59.reduce((x,y)=>x+y,0)===stillR1.length ? ' ✓' : ' ✗ 红'));
const tA = tiersOf(r1n);
say('  那 ' + r1n.length + ' 处（集合 A）：① ' + tA[0] + ' ／ ② ' + tA[1] + ' ／ ③ ' + tA[2] + ' ／ ④ ' + tA[3] + '  ⇒ 和 ' + tA.reduce((x,y)=>x+y,0) + (tA.reduce((x,y)=>x+y,0)===r1n.length ? ' ✓' : ' ✗ 红'));
say('  ★ 空集下四个数都是 0 —— 这不是「判不出」，是【没有对象】。');
say('  ★★★ 而 ② 那一档在 A 上 = ' + tA[1] + ' 处，在 B 上 = ' + t59[1] + ' 处 ——');
say('      同一个【档】的名字，在两个对象上给出两个不同的数，而两个都不是错的。');
say();
say('=== Q5c：★★★ 那个 ④ = 78 不是「判不出」—— 是【那个四档划分漏了一格】');
say('  ★ 三个布尔值的【八种】可能组合，逐格（**枚举出来的，不是从链条推出来的**）：');
const combos = [['F','F','F'],['F','F','T'],['F','T','F'],['F','T','T'],['T','F','F'],['T','F','T'],['T','T','F'],['T','T','T']];
const names = { 'FFF': '三个都不收', 'TFF': '★ 只有①收（引号里的字面量）', 'TTF': '②③收、③不改判… 实际是 ①②收、③不收', 'TTT': '三个都收' };
for (const c of combos) {
  const k = c.join('');
  const inB = stillR1.filter((r) => [META.n(r.cmd),META.q2(r.cmd),META.q3(r.cmd)].map((x)=>x?'T':'F').join('') === k).length;
  const inA = r1n.filter((r) => [META.n(r.cmd),META.q2(r.cmd),META.q3(r.cmd)].map((x)=>x?'T':'F').join('') === k).length;
  say('    (' + c.join(',') + ')   在 B 上 = ' + String(inB).padStart(3) + '   在 A 上 = ' + String(inA).padStart(3) + '     ' + (names[k] || ''));
}
say('  ★★★ 派件的四档把 (T,F,F) 那一格 —— 「元字符在引号里，是字面量」—— **放进了 ④「判不出」**。');
say('      而它恰恰是最大的一格（**76 处**，加上 (T,F,T) 那 2 处 = ④ 报的 78），也是戊⁷ 第三条头条说的那一格。');
say('  ⇒ 判据：**一个四档划分如果只按【第几把收】写，就会把「①收了、②③没收」这一格挤进「判不出」。**');
say('      与壬⁶ 那条同族：**一个划分的定义域漏了一格，而落进那一格的那一处恰好是刚刚被救回来的那一个。**');
say();
say('=== Q5d：★★★ 本件自己也漏了一格 —— 而漏的那一格，正是派件选对象时排除掉的那一格');
const inK = (r) => [META.n(r.cmd),META.q2(r.cmd),META.q3(r.cmd)].map((x)=>x?'T':'F').join('');
// ③ ⊇ ② 的反例：② 命中而 ③ 不命中
const notWiden = r1n.filter((r) => META.q2(r.cmd) && !META.q3(r.cmd));
say('  ★ 先量 ③ ⊇ ② 是否成立：② 命中而 ③ 不命中的 = ' + notWiden.length + ' 处 ⇒ ' + (notWiden.length===0 ? '成立 ✓（③ 确实是 ② 的加宽）' : '不成立 ✗'));
const assumed = ['FFF','TFF','TTF','TTT'];
const offGrid = r1n.filter((r) => !assumed.includes(inK(r)));
say('  本件最初【从链条 ① ⊇ ③ ⊇ ② 推】出 4 格 = ' + assumed.join('/') + ' ⇒ 那 4 格之和 = 135，而 |A| = ' + r1n.length + ' ⇒ **少 ' + (r1n.length - 135) + ' 处**。');
say('  ★ 而那 4 格里有【一格是空的】：TTF（①②收、③不收）在 ③ ⊇ ② 之下【不可能存在】。');
say('  ★ 真实的第 4 格是 **TFT** —— 我把它写成了 TTF，于是真正有东西的那一格在我列的清单里不存在。');
say('  ⇒ 那 ' + offGrid.length + ' 处落在 TFT 上：');
for (const r of offGrid) {
  say('    ' + at(r).padEnd(46) + '  ①=' + JSON.stringify(META.n(r.cmd)) + ' ②=' + JSON.stringify(META.q2(r.cmd)) + ' ③=' + JSON.stringify(META.q3(r.cmd)) + '   cmd=`' + r.cmd + '`');
}
say('  ★ 形状 = (T,F,T)：**① 收、② 不收、③ 收** —— 这两处的 $ 在**双引号里**。');
say('  ⇒ ★★★ 判据：**一个划分的成员，如果是从【一条链】推出来的而不是【枚举】出来的，那么写错的那一个成员，恰好是那条链所禁止的那一格 —— '
  + '而真正装着东西的那一格，会在清单里不存在。**（与派件那个 ④ = 78 是同一族的第二次。）');
say();
say('  ★★★ 而更值钱的是：**那 2 处 TFT 里，有 0 处落进 B。**');
say('     B 的定义 = 「① 命中【且】② 命中」；而口径 ③ 的**全部效果**，恰好只发生在【② 不命中】的地方。');
say('     ⇒ **派件把对象选成了「② 命中的那些」，而第三个口径的作用域正是「② 不命中的那些」—— 两者的交是空集。**');
say('     ⇒ 派件问「这 59 处在三个口径下变成几」，答案在【选对象的那一刻】就已经被定成了 59 —— 与测量无关。');
say('     ⇒ ★★★ 判据：**一个口径的效果如果全部落在【上一个口径不收】的地方，那么在一个由【上一个口径收了】定义的对象上量它，'
  + '得到的 0 不是「它没效果」，是【对象与它的作用域不相交】。**');
