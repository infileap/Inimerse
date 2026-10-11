/* render_check.js —— 五页渲染验证（Node + 最小 DOM 桩）
 *
 * 跑法（**从仓库根、从 website/、从任何目录都可以**，路径按 __dirname 解析）：
 *     node website/scratch/render_check.js
 * 退出码：全部通过 = 0；有 FAIL = 1（可以直接接进 CI / gate）。
 *
 * 本机没装 bsk（BrowserSkill 未安装）⇒ 开不了浏览器。这个桩是唯一能证明
 * 「页面渲染分支不炸」的办法，所以它必须覆盖全部五页，不能只盯一页。
 *
 * ---------------------------------------------------------------------------
 * 红对照（变异测试）怎么复现 —— 全绿不等于它有用，要证明它会红：
 *
 *   # 0. 先确认基线干净，否则分不清是你的变异还是原有改动
 *   git diff --numstat
 *   # 1. 变异 M1：把 videos/index.html 的 id 改名（JS 静默 early-return 的典型形状）
 *   sed -i 's/id="video-list"/id="video-list-typo"/' website/videos/index.html
 *   git diff --numstat website/videos/index.html     # ← 非空才叫落地
 *   node website/scratch/render_check.js; echo "exit=$?"   # 必须红、exit=1
 *   git checkout -- website/videos/index.html
 *   # 2. 变异 M2：往 data/videos.js 里塞一条缺 ref、bvid 非法的记录
 *   #    注意：**先看 numstat 非空再读结论**。这条变异第一次做时 replace 目标串
 *   #    没匹配上、根本没落地，而结果照样「全绿」—— 空 numstat 是唯一的警报。
 *   ...
 *
 * 变异必须在**读结果之前**证明已落盘（numstat 非空 / diff 正文可见）。
 * ---------------------------------------------------------------------------
 *
 * 它验证两层，缺一不可：
 *
 *   A. 结构层 —— 读 HTML 文件本身：页面引用的 js/css 是否真的存在、id 是否唯一、
 *      有没有绝对路径、内容位注释在不在、每个页面该有的结构在不在。
 *
 *   B. 运行层 —— 用**从该页 HTML 里抽出来的 id** 搭 DOM 桩，按页面真实顺序
 *      跑 data/*.js + assets/site.js，断言「它渲染出了什么」。
 *      每页都跑两遍：数据为空 / 数据有值。
 *
 * ⚠ 为什么运行层的 id 必须从 HTML 抽、绝不能手抄：
 *   手抄的桩在「HTML 把 id 改名了、JS 还在找旧名」时**依然会全绿** —— 而那正是
 *   最该抓的 bug（JS 静默 early-return，页面缺一块，控制台不报错）。
 *   从 HTML 抽 ⇒ 两边一旦不一致，getElementById 返回 null，断言立刻红。
 *   同样，锚点的 href / hidden 初值也从 HTML 里读，不手抄。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');        // = website/
const PAGES = [
  { key: 'index',  label: '首页',     file: 'index.html',        slots: 2, containers: [] },
  { key: 'games',  label: '在线游戏', file: 'games/index.html',  slots: 2, containers: ['toy-list'] },
  { key: 'videos', label: '实机演示', file: 'videos/index.html', slots: 1, containers: ['video-list', 'bili-account'] },
  { key: 'tools',  label: '小工具',   file: 'tools/index.html',  slots: 1, containers: [] },
  { key: 'about',  label: '关于',     file: 'about/index.html',  slots: 0, containers: [] },
  { key: 'spec',     label: '规范', file: 'spec/index.html',     slots: 1, containers: ['spec-list', 'spec-ref'] },
  { key: 'decisions', label: '台账', file: 'decisions/index.html', slots: 2,
    containers: ['decision-list', 'decision-filter', 'decisions-legend', 'decisions-summary', 'decisions-ref'] },
];
const DATA_FILES = ['data/site.js', 'data/videos.js', 'data/toys.js'];

/* ============================ 最小 DOM 桩 ============================ */
class El {
  constructor(tag) {
    this.tagName = String(tag).toUpperCase();
    this.className = '';
    this.children = [];
    this.attrs = {};
    this._text = '';
    this.listeners = {};
  }
  get textContent() {
    if (this.children.length === 0) return this._text;
    return this.children.map((c) => c.textContent).join('');
  }
  set textContent(v) { this.children = []; this._text = v == null ? '' : String(v); }
  /* ★ 桩不解析 HTML。`innerHTML = ''` 只清空（与浏览器一致）；
     赋一个**非空**字符串在桩里不会被解析 ⇒ 它只会静默地什么都不发生，
     所以这里把它记下来，由断言判红（见「innerHTML 只被赋空串」那一条）。 */
  set innerHTML(v) {
    this.children = [];
    this._text = '';
    if (v) this._unparsedHTML = String(v);
  }
  appendChild(c) { c.parent = this; this.children.push(c); return c; }
  removeChild(c) { this.children = this.children.filter((x) => x !== c); return c; }
  replaceWith(n) {
    const p = this.parent;
    if (p) p.children[p.children.indexOf(this)] = n;
    n.parent = p;
  }
  remove() { if (this.parent) this.parent.children = this.parent.children.filter((x) => x !== this); }
  setAttribute(k, v) { this.attrs[k] = String(v); }
  getAttribute(k) { return this.attrs[k]; }
  addEventListener(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
  click() { (this.listeners.click || []).forEach((f) => f()); }
  walk(fn) { fn(this); this.children.forEach((c) => c.walk(fn)); }
  find(pred) {
    let hit = null;
    this.walk((n) => { if (!hit && pred(n)) hit = n; });
    return hit;
  }
  all(pred) { const out = []; this.walk((n) => { if (pred(n)) out.push(n); }); return out; }
  html() {
    const cls = this.className ? ` class="${this.className}"` : '';
    const at = Object.keys(this.attrs).map((k) => ` ${k}="${this.attrs[k]}"`).join('');
    const href = this.href ? ` href="${this.href}"` : '';
    const src = this.src ? ` src="${this.src}"` : '';
    const t = this.tagName.toLowerCase();
    const inner = this.children.length ? this.children.map((c) => c.html()).join('') : this._text;
    return `<${t}${cls}${at}${href}${src}>${inner}</${t}>`;
  }
}

/* ============================ HTML 解析（只做够用的部分）================ */
const idsIn = (html) => {
  const out = [];
  const re = /\sid="([^"]+)"/g;
  let m;
  while ((m = re.exec(html))) out.push(m[1]);
  return out;
};

/** 取含 id="X" 那个标签的属性（用于把 href / hidden 初值也从 HTML 读出来） */
function attrsForId(html, id) {
  const re = new RegExp(`<[a-zA-Z][^>]*\\sid="${id}"[^>]*>`);
  const m = html.match(re);
  if (!m) return null;
  const tag = m[0];
  const href = (tag.match(/\shref="([^"]*)"/) || [])[1];
  return { href: href === undefined ? undefined : href, hidden: /\shidden(\s|>)/.test(tag) };
}

/* 按浏览器的规则，把一个页面里的相对引用解析成站点内的绝对路径（站点根 = '/'）。
   桩必须自己会这一步：否则「数据里的路径写对了」和「浏览器真能取到」永远是两件事。 */
function resolveFrom(pageFile, ref) {
  if (/^[a-z][a-z0-9+.-]*:/i.test(ref) || ref.charAt(0) === '/') return ref;
  const baseDir = pageFile.replace(/[^/]+$/, '');
  const out = [];
  for (const p of (baseDir + ref).split('/')) {
    if (p === '' || p === '.') continue;
    if (p === '..') out.pop();
    else out.push(p);
  }
  return '/' + out.join('/');
}
const ORIGIN = 'https://example.invalid';
/* 站点被托管在哪个子路径下。
   GitHub Pages 把它放在 /Inimerse/ 下，B站 Toy 会放在 /toy/<slug>/ 下 ——
   而「资源必须相对路径」这条设计正是为这两种情况写的。默认空 = 站点在根。
   用法：SITE_PREFIX=/Inimerse node website/scratch/render_check.js */
const PREFIX = process.env.SITE_PREFIX || '';

const refsIn = (html) => {
  const out = [];
  for (const re of [/<script[^>]*\ssrc="([^"]+)"/g, /<link[^>]*\shref="([^"]+\.css)"/g]) {
    let m;
    while ((m = re.exec(html))) out.push(m[1]);
  }
  return out;
};

/* ============================ 断言收集 ============================ */
const results = [];
let currentPage = '(全局)';
let pageRuns = 0;
function check(name, cond, detail) {
  results.push({ page: currentPage, name, ok: !!cond, detail: detail === undefined ? '' : String(detail) });
}
/* 只是「报事实」的行，不参与判定 —— 让看报告的人不用自己跑一遍才知道解析结果 */
const infos = [];
function info(s) { infos.push(s); }

/* ============================ 运行一页 ============================ */
/**
 * @param page   PAGES 里的条目
 * @param data   { videos: [], toys: [] } —— 要在加载 site.js 之前注入的数据
 * @param html   该页 HTML 源文
 */
function runPage(page, data, html) {
  pageRuns++;
  const ids = idsIn(html);

  // 用该页真实存在的 id 建容器；不存在的 id 一律返回 null（这是关键：不手抄、不补造）
  const hosts = {};
  ids.forEach((id) => {
    const e = new El('div');
    e.id = id;
    const a = attrsForId(html, id);
    if (a && a.href !== undefined) e.href = a.href;
    if (a && a.hidden) e.hidden = true;
    hosts[id] = e;
  });

  // 这一页自己引用的 assets/site.js 是写成 "../assets/site.js" 还是 "assets/site.js"，
  // 从 HTML 里读；再按浏览器规则解析成绝对 URL —— currentScript.src 在浏览器里就是
  // 这个解析后的值，桩不能手写一个假的。
  const scriptRef = refsIn(html).find((r) => /assets\/site\.js(\?|$)/.test(r)) || 'assets/site.js';

  const document = {
    readyState: 'complete',
    createElement: (t) => new El(t),
    getElementById: (id) => (Object.prototype.hasOwnProperty.call(hosts, id) ? hosts[id] : null),
    addEventListener: () => {},
    currentScript: { src: ORIGIN + PREFIX + resolveFrom(page.file, scriptRef) },
    getElementsByTagName: () => [],
  };

  const sandbox = { document, console, Date, String, parseInt, encodeURIComponent };
  sandbox.window = sandbox;
  vm.createContext(sandbox);

  const run = (rel) => vm.runInContext(fs.readFileSync(path.join(ROOT, rel), 'utf8'), sandbox, { filename: rel });

  /* 按**页面自己引用的 script 顺序**跑 —— 浏览器就是这么做的。
     ★ 原来是写死的 DATA_FILES 列表；那意味着「页面引了一个桩不知道的数据文件」
     与「那个文件不存在」在桩里长得一样（都是没跑）。新页面引 decisions.js / spec.js，
     写死的列表会静默漏掉它们。 */
  for (const ref of refsIn(html).filter((r) => /\.js$/.test(r))) {
    if (/assets\/site\.js$/.test(ref)) {
      if (data) {                            // data === null ⇒ 不注入，用 data/*.js 自己现在的内容
        sandbox.INFIVERSE_VIDEOS = data.videos;
        sandbox.INFIVERSE_TOYS = data.toys;
      }
    }
    run(resolveFrom(page.file, ref).replace(/^\//, ''));
  }

  return { hosts, sandbox, ids };
}

/* ============================ 测试数据 ============================ */
const EMPTY = { videos: [], toys: [] };

const ONE_VIDEO_UNPUBLISHED = {
  videos: [{
    id: 'demo-01', title: '演示一', bvid: '未投稿', duration: '03:42',
    resolution: '1920x1080', cover: '', summary: '一句话。',
    ref: 'f1dfe62583b7a89e2395ddc502fcf2ac7b60055e', date: '2026-10-06',
  }],
  toys: [],
};

const FULL = {
  videos: [
    {
      id: 'demo-02', title: '演示二', bvid: 'BV1xx411c7mD', duration: '10:00',
      resolution: '2560x1440', cover: 'assets/covers/demo-02.jpg', summary: '两句话。',
      ref: 'f1dfe62583b7a89e2395ddc502fcf2ac7b60055e', date: '2026-10-01', featured: true,
    },
    { id: 'demo-03', title: '竖屏演示', bvid: 'BV1yy411c7mE', orientation: 'portrait', cover: 'x.jpg' },
  ],
  toys: [{
    slug: 'inimerse-demo', title: '语法演示', summary: '一句话。',
    url: 'https://www.bilibili.com/toy/inimerse-demo/index.html', status: 'live', channel: 'toy',
    poster: 'assets/covers/demo-02.jpg',
  }],
};

/* ============================ 开始：结构层 ============================ */
const allRefs = [];

PAGES.forEach((page) => {
  currentPage = page.label;
  const abs = path.join(ROOT, page.file);
  const html = fs.readFileSync(abs, 'utf8');

  // 1. 引用到的 js/css 是否真的存在（每条引用算一项检查）
  const refs = refsIn(html);
  check(`${page.file}：引用了 ${refs.length} 个 js/css`, refs.length === 5, refs.length);
  refs.forEach((r) => {
    const target = path.resolve(path.dirname(abs), r);
    const exists = fs.existsSync(target);
    allRefs.push({ page: page.file, ref: r, exists });
    check(`${page.file}：资源存在 → ${r}`, exists, path.relative(ROOT, target));
  });

  // 2. id 唯一（重复 id 是静默 bug：getElementById 只给第一个）
  const ids = idsIn(html);
  const dup = ids.filter((v, i) => ids.indexOf(v) !== i);
  check(`${page.file}：id 不重复`, dup.length === 0, dup.join(','));

  // 3. 没有绝对路径（Toy 托管 / 子目录部署的硬要求）
  const absPath = html.match(/(?:src|href)="\//g);
  check(`${page.file}：没有绝对路径`, absPath === null, absPath ? absPath.join(' ') : '无');

  // 4. 内容位注释数量（视频/作品/工具将来插在哪一块，靠这些注释定位）
  const slots = (html.match(/内容位/g) || []).length;
  check(`${page.file}：内容位注释 ${page.slots} 处`, slots === page.slots, slots);

  // 5. 页脚年份容器（五个页面都该有）
  check(`${page.file}：有 #year`, ids.includes('year'));

  /* 5b. 该页**必须存在**的 JS 容器。
     这一条不能写成「如果存在就检查」—— 容器被改名/删掉时，那种写法会静默跳过、
     整页断言集体失效，而页面只是少一块、控制台不报错。这里是无条件断言。 */
  page.containers.forEach((cid) => {
    check(`${page.file}：必须有 #${cid}（JS 靠它渲染，缺了会静默少一块）`,
      ids.includes(cid));
  });

  // 6. 页面必须能本地直接打开：不依赖 fetch / 模块
  check(`${page.file}：没有 fetch(`,
    !/\bfetch\s*\(/.test(html), '');
  check(`${page.file}：script 不带 type="module"`,
    !/<script[^>]*type="module"/.test(html));
});

/* ============================ 开始：运行层（每页 × 空/有数据）============ */
PAGES.forEach((page) => {
  const html = fs.readFileSync(path.join(ROOT, page.file), 'utf8');

  /* ---- 空数据 ---- */
  currentPage = `${page.label}（空数据）`;
  let st;
  try {
    st = runPage(page, EMPTY, html);
    check('不抛异常', true);
  } catch (e) {
    check('不抛异常', false, e.message);
    return;
  }
  check('页脚年份被填入', /^\d{4}$/.test(st.hosts.year.textContent), st.hosts.year.textContent);
  check('日期不为空字符串', st.hosts.year.textContent.length === 4);

  /* 无条件：本页该有的容器必须都在。下面的渲染断言都写成「如果容器存在就检查」，
     所以必须在这里补一条无条件的 —— 否则容器一旦消失，整页断言集体被跳过。 */
  page.containers.forEach((cid) => {
    check(`容器 #${cid} 真的在 DOM 里`, !!st.hosts[cid]);
  });

  if (st.hosts['video-list']) {
    check('视频位显示空状态文案',
      /还没有视频记录/.test(st.hosts['video-list'].textContent),
      st.hosts['video-list'].textContent.slice(0, 60));
    check('空状态不是一片空白（有 <p> 提示）',
      st.hosts['video-list'].find((n) => n.tagName === 'P') !== null);
  }
  if (st.hosts['toy-list']) {
    check('作品位显示空状态文案',
      /还没有已发布的作品/.test(st.hosts['toy-list'].textContent),
      st.hosts['toy-list'].textContent.slice(0, 60));
    check('空状态提到内测资格（不能只说"暂无"）',
      /内测/.test(st.hosts['toy-list'].textContent));
  }
  if (st.hosts['bili-account']) {
    check('B站 按钮：bilibiliUrl 留空时仍是初始 href（不编 UID）',
      st.hosts['bili-account'].href === '#',
      String(st.hosts['bili-account'].href));
    check('B站 按钮：留空时保持 hidden',
      st.hosts['bili-account'].hidden === true,
      String(st.hosts['bili-account'].hidden));
  }
  /* 没有 JS 容器的页面：断言它「渲染出了什么」落在 HTML 结构上 */
  if (page.key === 'index') {
    const cards = (html.match(/class="card"/g) || []).length;
    check('首页：三张入口卡都在', cards >= 3, cards);
    check('首页：有「栏目状态一览」总表', /栏目状态一览/.test(html));
    check('首页：总表六行', (html.match(/<tr>\s*<td>[\s\S]*?<\/tr>/g) || []).length >= 6,
      (html.match(/<tr>\s*<td>[\s\S]*?<\/tr>/g) || []).length);
  }
  if (page.key === 'tools') {
    const toolCards = (html.match(/class="card"/g) || []).length;
    check('小工具：六张计划卡都在', toolCards === 6, toolCards);
    check('小工具：每张都标「未开始」（不假装做完了）',
      (html.match(/未开始/g) || []).length === 6,
      (html.match(/未开始/g) || []).length);
  }
  if (page.key === 'about') {
    check('关于：四条「不要做」都在', (html.match(/<tr><td>/g) || []).length === 4,
      (html.match(/<tr><td>/g) || []).length);
    check('关于：三条技术约束都在', (html.match(/<li><b>/g) || []).length === 3,
      (html.match(/<li><b>/g) || []).length);
  }
  if (page.key === 'games') {
    check('游戏页：逐字引用「当前 Toy 发布面向邀请 UP 主逐步开放」这句',
      /当前 Toy 发布面向邀请 UP 主逐步开放/.test(html));
    check('游戏页：写明「没有集成路径」（判定 2 的逐字证据在页面上）',
      /没有集成路径/.test(html));
  }
  if (page.key === 'videos') {
    check('演示页：交付要求表在（给录制那边的接口）', /交付要求/.test(html));
  }

  /* ---- 有数据：未投稿一条 ---- */
  currentPage = `${page.label}（有数据/未投稿）`;
  let s2;
  try {
    s2 = runPage(page, ONE_VIDEO_UNPUBLISHED, html);
    check('不抛异常', true);
  } catch (e) {
    check('不抛异常', false, e.message);
    return;
  }
  check('页脚年份仍被填入', /^\d{4}$/.test(s2.hosts.year.textContent));
  if (s2.hosts['video-list']) {
    check('未投稿：显示「待投稿后补 BV 号」徽章',
      /待投稿后补 BV 号/.test(s2.hosts['video-list'].textContent));
    check('未投稿：元信息写「B站 未投稿」，不留空',
      /B站 未投稿/.test(s2.hosts['video-list'].textContent));
    check('未投稿：封面缺失给占位，不放坏图',
      /封面待交付/.test(s2.hosts['video-list'].textContent));
    check('未投稿：ref 只显示前 7 位',
      /ref f1dfe62/.test(s2.hosts['video-list'].textContent) &&
      !/f1dfe62583b7a89e/.test(s2.hosts['video-list'].textContent));
    check('未投稿：不生成任何可点外链',
      s2.hosts['video-list'].find((n) => n.tagName === 'A' && n.href) === null);
  }

  /* ---- 有数据：已投稿 + featured + 竖屏 + 作品 ---- */
  currentPage = `${page.label}（有数据/完整）`;
  let s3;
  try {
    s3 = runPage(page, FULL, html);
    check('不抛异常', true);
  } catch (e) {
    check('不抛异常', false, e.message);
    return;
  }
  check('页脚年份仍被填入', /^\d{4}$/.test(s3.hosts.year.textContent));

  if (s3.hosts['video-list']) {
    const vl = s3.hosts['video-list'];
    check('已投稿：渲染成两张卡', vl.children.length === 2, vl.children.length);
    check('已投稿：生成 B站 外链且 BV 号正确',
      !!vl.find((n) => n.tagName === 'A' && /bilibili\.com\/video\/BV1xx411c7mD\//.test(n.href || '')));
    check('已投稿：元信息写「B站 BV1xx411c7mD」', /B站 BV1xx411c7mD/.test(vl.textContent));
    check('已投稿：featured 首条带 video--featured 类',
      /video--featured/.test(vl.children[0].className), vl.children[0].className);
    check('竖屏：封面容器带 portrait 类',
      !!vl.find((n) => /video__cover--portrait/.test(n.className)));
    const btn = vl.children[0].find((n) => n.tagName === 'BUTTON' && /在站内播放/.test(n.textContent));
    check('已投稿：有「在站内播放」按钮', !!btn);
    if (btn) {
      btn.click();
      const iframe = vl.children[0].find((n) => n.tagName === 'IFRAME');
      check('点击后插入 iframe', !!iframe);
      check('iframe 指向 B站播放器且 bvid 正确',
        !!iframe && /player\.bilibili\.com\/player\.html\?bvid=BV1xx411c7mD&page=1&autoplay=0/.test(iframe.src),
        iframe ? iframe.src : '(无)');
      check('插入后原封面容器被替换（不留两张封面）',
        vl.children[0].find((n) => /video__cover/.test(n.className)) === null);
    }
  }
  if (s3.hosts['toy-list']) {
    const tl = s3.hosts['toy-list'];
    check('作品位：渲染成卡', tl.children.length === 1, tl.children.length);
    check('作品位：卡上带 Toy 链接',
      /bilibili\.com\/toy\/inimerse-demo\/index\.html/.test(tl.children[0].href || ''),
      tl.children[0].href);
    check('作品位：live 状态显示「今天可玩」', /今天可玩/.test(tl.textContent));
    check('作品位：标注「B站 Toy 托管」', /B站 Toy 托管/.test(tl.textContent));
    /* 作品封面走的是同一个 asset()。这条在**在线游戏页**上再验一次：
       它是个子页，如果 asset() 没生效，这里会解析成 /games/assets/... 而不是 /assets/... */
    const pimg = tl.children[0].find((n) => n.tagName === 'IMG' && n.src);
    check('作品位：poster 解析成站点根下的绝对 URL（不是 /games/assets/...）',
      !!pimg && pimg.src === ORIGIN + PREFIX + '/assets/covers/demo-02.jpg',
      pimg ? pimg.src : '(没有 img)');
  }
  if (s3.hosts['bili-account']) {
    check('B站 按钮：留空时仍不指向任何地址', s3.hosts['bili-account'].href === '#');
  }
});

/* ============================ 第五轮：填了 bilibiliUrl ============================ */
currentPage = '实机演示（bilibiliUrl 有值）';
{
  const page = PAGES.find((p) => p.key === 'videos');
  const html = fs.readFileSync(path.join(ROOT, page.file), 'utf8');
  const st = runPage(page, EMPTY, html);
  st.sandbox.INFIVERSE_SITE = Object.assign({}, st.sandbox.INFIVERSE_SITE, {
    // 故意用一个明显不是 B站 的地址：**不在这里放任何看起来像真 UID 的数字**。
    // 桩只需要「一个非空字符串被原样搬进 href」，不需要一个能被人误认成事实的号。
    bilibiliUrl: 'https://example.invalid/bili-test',
  });
  vm.runInContext(fs.readFileSync(path.join(ROOT, 'assets/site.js'), 'utf8'), st.sandbox,
    { filename: 'assets/site.js#bili' });
  const b = st.hosts['bili-account'];
  check('填了地址后按钮指向它', b.href === 'https://example.invalid/bili-test', String(b.href));
  check('填了地址后不再 hidden', b.hidden === false, String(b.hidden));
}

/* ============================ 数据文件契约 ============================
 * 上面运行层的数据是**桩自带的 fixture**，所以「真实数据文件里字段写错」它看不见。
 * 这一段补上这个缺口：直接把 data/videos.js / data/toys.js 求值，逐条校验字段。
 * 记录为空时它不喊 —— 但 glad-badger 交付第一条的那一刻，它就开始工作。
 */
function loadDataFile(rel) {
  const sb = { console };
  sb.window = sb;
  vm.createContext(sb);
  vm.runInContext(fs.readFileSync(path.join(ROOT, rel), 'utf8'), sb, { filename: rel });
  return sb;
}
const BV_RE = /^BV[0-9A-Za-z]{8,12}$/;
const HEX40 = /^[0-9a-f]{40}$/;

currentPage = '数据文件 data/videos.js';
{
  const sb = loadDataFile('data/videos.js');
  const vids = sb.INFIVERSE_VIDEOS;
  check('导出 INFIVERSE_VIDEOS 且是数组', Array.isArray(vids));
  (vids || []).forEach((v, i) => {
    const tag = `videos[${i}] ${v && v.id ? v.id : '(缺 id)'}`;
    ['id', 'title', 'bvid', 'duration', 'resolution', 'summary', 'ref', 'date'].forEach((k) => {
      check(`${tag}：${k} 是非空字符串`, !!v && typeof v[k] === 'string' && v[k].length > 0);
    });
    check(`${tag}：bvid 是 BV 号或「未投稿」`,
      !v || v.bvid === '未投稿' || BV_RE.test(v.bvid || ''), v && v.bvid);
    check(`${tag}：ref 是 40 位十六进制`, !v || HEX40.test(v.ref || ''), v && v.ref);
    check(`${tag}：date 形如 YYYY-MM-DD（月 01-12、日 01-31）`,
      !v || /^\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])$/.test(v.date || ''), v && v.date);
    check(`${tag}：orientation 合法`,
      !v || v.orientation === undefined || ['landscape', 'portrait'].indexOf(v.orientation) !== -1,
      v && v.orientation);
    /* 这一条**故意不是**「cover 可以是字符串」那种恒真断言（恒真断言只会撑大分母）：
       已投稿的记录必须有封面 —— 否则 B站 上挂着、站内却是一块占位，看起来像没做完。
       未投稿允许留空，那时页面显示「封面待交付」占位。 */
    check(`${tag}：已投稿的记录必须有封面`,
      !v || v.bvid === '未投稿' || (typeof v.cover === 'string' && v.cover.length > 0),
      v && v.cover);
  });
}

currentPage = '数据文件 data/toys.js';
{
  const sb = loadDataFile('data/toys.js');
  const toys = sb.INFIVERSE_TOYS;
  check('导出 INFIVERSE_TOYS 且是数组', Array.isArray(toys));
  (toys || []).forEach((t, i) => {
    const tag = `toys[${i}] ${t && (t.slug || t.title) ? (t.slug || t.title) : '(缺 slug)'}`;
    ['slug', 'title', 'summary'].forEach((k) => {
      check(`${tag}：${k} 是非空字符串`, !!t && typeof t[k] === 'string' && t[k].length > 0);
    });
    check(`${tag}：url 是 http(s) 地址`, !t || /^https?:\/\//.test(t.url || ''), t && t.url);
    check(`${tag}：status 合法`, !t || ['live', 'pending'].indexOf(t.status) !== -1, t && t.status);
    check(`${tag}：channel 合法`,
      !t || t.channel === undefined || ['toy', 'self'].indexOf(t.channel) !== -1, t && t.channel);
    /* poster 是可选的；一旦填了就必须能取到 —— 要么是外链，要么是站点根下真实存在的文件。
       「填了个取不到的路径」和「没填」在页面上看起来一样（都是没有封面），
       但在数据里是两种不同的错误，所以这里分开判。 */
    if (t && t.poster) {
      const ok = /^https?:\/\//.test(t.poster) || fs.existsSync(path.join(ROOT, t.poster));
      check(`${tag}：poster 指向真实存在的文件（或外链）`, ok, t.poster);
    }
  });
}

/* ============================ 真实数据轮 ============================
 * ★ 上面运行层用的是**桩自带的 fixture** —— 也就是说，真实 data/*.js 里现在
 *   躺着什么、渲染出来是什么，它会一路绿灯地不知道。这一轮补上这个缺口：
 *   不注入任何东西，直接跑 data/*.js 现在的内容，看页面渲染成什么。
 *   这一轮才是「今天打开网站看到的东西」的断言。
 */
currentPage = '实机演示（真实数据 data/videos.js）';
{
  const page = PAGES.find((p) => p.key === 'videos');
  const html = fs.readFileSync(path.join(ROOT, page.file), 'utf8');
  const st = runPage(page, null, html);
  const real = st.sandbox.INFIVERSE_VIDEOS || [];
  const vl = st.hosts['video-list'];

  check('真实 data/videos.js 至少渲染出一条记录', real.length >= 1, `${real.length} 条`);
  if (real.length) {
    check('真实第一条渲染成卡（不是空状态）', vl.children.length === real.length, vl.children.length);
    check('真实记录标题出现在页面上', vl.textContent.indexOf(real[0].title) !== -1);
    check('真实记录：封面文件真的在磁盘上',
      fs.existsSync(path.resolve(ROOT, real[0].cover)), real[0].cover);
    /* ★ 上面那条只说明「文件在磁盘上」；它**不说明浏览器取得到**。
       数据里的路径是相对站点根写的，而浏览器按**页面目录**解析 —— 从 /videos/ 打开时
       "assets/covers/x.jpg" 会变成 /videos/assets/covers/x.jpg ⇒ 404。
       所以这条按浏览器规则走一遍：拿到渲染后的 img.src，解析成站点内路径，再看文件在不在。 */
    const img = vl.find((n) => n.tagName === 'IMG' && n.src);
    const raw = img ? img.src : '';
    // 站点根反推成功时 img.src 已经是绝对 URL（站点根 + 数据里的路径）⇒ 剥掉 origin
    // 再当站点内路径用；反推失败时它还是个页面相对路径 ⇒ 按浏览器规则解析（这正是 404 的形态）。
    let asPath = /^[a-z][a-z0-9+.-]*:\/\//i.test(raw)
      ? raw.replace(/^[a-z][a-z0-9+.-]*:\/\/[^/]+/i, '')
      : (raw.charAt(0) === '/' ? raw : resolveFrom(page.file, raw));
    if (PREFIX && asPath.indexOf(PREFIX + '/') === 0) asPath = asPath.slice(PREFIX.length);
    check('真实记录：封面按浏览器规则解析后仍落在站点内、且文件存在',
      !!img && fs.existsSync(path.join(ROOT, asPath.split('?')[0])),
      `${raw || '(没有 img)'} → ${asPath}`);
    info(`真实记录封面：数据里写 "${real[0].cover}"，页面渲染出 "${raw}"（浏览器会请求 ${asPath}）`);
    /* ★ 子路径不变式：站点被托管在 PREFIX 之下时，站内资源 URL 必须以 ORIGIN+PREFIX 开头。
       它抓的是「把站点根当成域根」这一类 bug —— 那种写法在根路径下能跑，在 /Inimerse/ 下 404。
       根路径那一遍也照跑（此时要求以 ORIGIN+'/' 开头，等于顺带禁掉了硬写的绝对路径）。 */
    check(`真实记录：封面 URL 落在托管路径 ${PREFIX || '/'} 之下（不是域根的绝对路径）`,
      raw.indexOf(ORIGIN + PREFIX + '/') === 0,
      `${raw} 应以 ${ORIGIN + PREFIX}/ 开头`);
    check('真实记录：未投稿时页面写「B站 未投稿」，不是空白',
      real[0].bvid !== '未投稿' || /B站 未投稿/.test(vl.textContent));
    check('真实记录：ref 在页面上只显示前 7 位',
      vl.textContent.indexOf(real[0].ref.slice(0, 7)) !== -1 &&
      vl.textContent.indexOf(real[0].ref) === -1);
    /* ref 必须是一棵**真实存在过**的树。这不是「格式对不对」，是「有没有出处」——
       一个查不到的 ref 和编造的数字是同一件事。 */
    let gitSays = '';
    let refOk = false;
    try {
      require('child_process').execSync(
        `git rev-parse --quiet --verify ${JSON.stringify(real[0].ref + '^{commit}')}`,
        { cwd: path.resolve(ROOT, '..'), stdio: ['ignore', 'pipe', 'pipe'] });
      refOk = true;
    } catch (e) {
      gitSays = String((e && e.stderr) || (e && e.message) || e).trim().slice(0, 120);
    }
    check(`真实记录：ref 指向一个仓库里真实存在的提交（${real[0].ref.slice(0, 7)}）`,
      refOk, gitSays);
  }
}

currentPage = '在线游戏（真实数据 data/toys.js）';
{
  const page = PAGES.find((p) => p.key === 'games');
  const html = fs.readFileSync(path.join(ROOT, page.file), 'utf8');
  const st = runPage(page, null, html);
  const real = st.sandbox.INFIVERSE_TOYS || [];
  const tl = st.hosts['toy-list'];
  if (real.length === 0) {
    check('还没有 Toy 记录，页面必须显示空状态（含内测说明）',
      /还没有已发布的作品/.test(tl.textContent) && /内测/.test(tl.textContent));
  } else {
    check('有 Toy 记录时渲染成卡', tl.children.length === real.length, tl.children.length);
  }
}

/* ============================ 规范页 / 台账页（真实数据）============================
 * ★ 这两页是这一笔的交付物本身。没有这一段，它们就是「没测到」，而不是「没问题」。
 */
currentPage = '台账（真实数据 data/decisions.js）';
{
  const page = PAGES.find((p) => p.key === 'decisions');
  const html = fs.readFileSync(path.join(ROOT, page.file), 'utf8');
  const st = runPage(page, null, html);
  const D = st.sandbox.INFIVERSE_DECISIONS;
  const host = st.hosts['decision-list'];
  const bar = st.hosts['decision-filter'];
  const legend = st.hosts['decisions-legend'];
  const summary = st.hosts['decisions-summary'];

  check('台账页：渲染出的条目数 = 数据里的条目数',
    host.children.length === D.entries.length, `${host.children.length} / ${D.entries.length}`);
  check('台账页：图例覆盖了状态词表里的每一个状态',
    legend.children.length === Object.keys(D.statuses).length,
    `${legend.children.length} / ${Object.keys(D.statuses).length}`);
  check('台账页：摘要写出了总条数',
    summary.textContent.indexOf('共 ' + D.entries.length + ' 条') !== -1,
    summary.textContent.slice(0, 40));
  const cards = host.all((n) => n.tagName === 'ARTICLE');
  check('台账页：每一条都带自己的 id 锚点（反馈要能落到具体一条上）',
    cards.length === D.entries.length && cards.every((n) => !!n.id),
    `${cards.filter((n) => n.id).length} / ${cards.length} 带 id`);
  /* ★ 「docs 未记录备选」是一个零，而一个零必须带归因 —— 否则它与「我没找到」长得一样 */
  const zeros = D.entries.filter((e) => !e.alternative);
  if (zeros.length) {
    const c = host.find((n) => n.id === zeros[0].id);
    check(`台账页：${zeros.length} 条没有备选记录的，写「docs 未记录备选」并给出查过哪些文件`,
      !!c && /docs 未记录备选/.test(c.textContent) && /PLAN_V06/.test(c.textContent),
      c ? c.textContent.slice(0, 60) : '(找不到那张卡)');
  }
  /* ★ 引 PLAN_V07 的数字必须同时显示 §0.3 */
  const v7 = D.entries.filter((e) => e.source && e.source.file === 'docs/PLAN_V07.md');
  if (v7.length) {
    const c = host.find((n) => n.id === v7[0].id);
    check('台账页：引 PLAN_V07 的条目同时显示了 §0.3「数字都不是读数」',
      !!c && /没有观测点/.test(c.textContent), c ? c.textContent.slice(0, 40) : '(找不到)');
  }
  /* ★ 出处分级：四条人类选的必须与其余「已裁」分得开 */
  const marked = host.all((n) => /badge--human/.test(n.className)).length;
  check('台账页：四条人类选定的带「人类选的」标记，与其余已裁分得开',
    marked === D.humanRuled.ids.length, `${marked} / ${D.humanRuled.ids.length}`);
  check('台账页：摘要里明说「已裁」不是一种、是两种',
    /已裁」不是一种，是两种/.test(summary.textContent));

  /* ★★ 状态转移：不是「按钮在了」，是「点了之后渲染出来的东西真的变了」 */
  const k = Object.keys(D.statuses).find((s) => D.entries.some((e) => e.status === s));
  const btn = bar.children.find((b) => b.textContent.indexOf(D.statuses[k].label) === 0);
  check(`台账页：筛选栏里有「${D.statuses[k].label}」这个按钮`, !!btn);
  if (btn) {
    btn.click();
    const want = D.entries.filter((e) => e.status === k).length;
    const got = host.all((n) => n.tagName === 'ARTICLE');
    check(`台账页：点「${D.statuses[k].label}」后只剩 ${want} 条，且每一条都是那个状态`,
      got.length === want && got.every((n) => n.className.indexOf('decision--' + k) !== -1),
      `渲染出 ${got.length} 条`);
    bar.children[0].click();
    check('台账页：点回「全部」后条数复原',
      host.children.length === D.entries.length, host.children.length);
  }
}

currentPage = '规范（真实数据 data/spec.js + data/decisions.js）';
{
  const page = PAGES.find((p) => p.key === 'spec');
  const html = fs.readFileSync(path.join(ROOT, page.file), 'utf8');
  const st = runPage(page, null, html);
  const S = st.sandbox.INFIVERSE_SPEC;
  const D = st.sandbox.INFIVERSE_DECISIONS;
  const host = st.hosts['spec-list'];
  const docs = host.all((n) => n.tagName === 'ARTICLE' && /spec-doc/.test(n.className));

  check('规范页：渲染出的文档块数 = spec.js 里的文档数',
    docs.length === S.docs.length, `${docs.length} / ${S.docs.length}`);
  check('规范页：每份文档都写出了自己的标题',
    S.docs.every((d) => host.textContent.indexOf(d.title.replace(/^#+\s*/, '')) !== -1));
  check('规范页：每份文档都标出了它自己那条引用规矩（节号 / 行号）',
    S.docs.every((d) => host.textContent.indexOf(d.cite) !== -1));
  check('规范页：文档头的逐字引文真的在页面上',
    S.docs.every((d) => d.headers.every((h) => host.textContent.indexOf(h.replace(/^>\s?/, '')) !== -1)));
  check('规范页：本站概括与逐字引文是分开显示的（各有标签）',
    /本站概括/.test(host.textContent) && /文件头逐字/.test(host.textContent));
  const shown = host.all((n) => n.tagName === 'ARTICLE' && /decision--/.test(n.className)).length;
  check(`规范页：${D.entries.length} 条裁定全部挂到了各自文档下面`,
    shown === D.entries.length, `${shown} / ${D.entries.length}`);
}

currentPage = '站点脚本（结构）';
{
  const js = fs.readFileSync(path.join(ROOT, 'assets/site.js'), 'utf8');
  const all = js.match(/\.innerHTML\s*=\s*[^;]+/g) || [];
  const empty = all.filter((m) => /\.innerHTML\s*=\s*(''|"")\s*$/.test(m));
  check('assets/site.js：innerHTML 只被赋空串（桩不解析 HTML，赋非空串在桩里会静默什么都不发生）',
    all.length === empty.length,
    all.filter((m) => empty.indexOf(m) === -1).join(' | '));
}

/* ============================ 报告 ============================ */
const byPage = new Map();
results.forEach((r) => {
  if (!byPage.has(r.page)) byPage.set(r.page, { pass: 0, fail: 0 });
  const s = byPage.get(r.page);
  r.ok ? s.pass++ : s.fail++;
});

let bad = 0;
for (const [pageName, s] of byPage) {
  if (s.fail) { /* 失败的逐条打在下面 */ }
}
results.forEach((r) => {
  if (!r.ok) {
    bad++;
    console.log(`FAIL  [${r.page}] ${r.name}${r.detail ? '   ← ' + r.detail : ''}`);
  }
});

const uniqRefs = new Set(allRefs.map((r) => path.resolve(ROOT, path.dirname(PAGES.find((p) => p.file === r.page).file), r.ref)));
console.log('');
console.log(`页面覆盖：${PAGES.length}/${PAGES.length} 页（${PAGES.map((p) => p.label).join('、')}）`);
console.log(`运行次数：${pageRuns} 次页面求值（${PAGES.length} 页 × 3 轮夹具 + 真实数据轮 + 按钮两种初值轮）`);
console.log(`资源引用：${allRefs.length} 条，去重后 ${uniqRefs.size} 个文件，全部存在=${allRefs.every((r) => r.exists)}`);
console.log(`托管路径：${PREFIX || '/'}（SITE_PREFIX 环境变量；空 = 站点在根）`);
console.log(`检查项：${results.length - bad}/${results.length} 通过`);
infos.forEach((s) => console.log(`  · ${s}`));
if (bad) {
  console.log(`\n✗ 有 ${bad} 项失败`);
  process.exit(1);
}
console.log('\n✓ 全部通过');
