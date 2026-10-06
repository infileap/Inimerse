/* render_check.js —— 在 Node 里用最小 DOM 桩执行 assets/site.js 的渲染路径。
   目的：静态 HTML 里 video-list / toy-list 是空容器，靠 JS 填。
   不开浏览器（本机没装 bsk）时，这是唯一能证明「空状态 + 有记录两种分支都不炸」的办法。

   用法：node website/scratch/render_check.js   （在仓库根或任意目录都能跑）
*/
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');

/* ---------- 最小 DOM ---------- */
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
  set textContent(v) {
    this.children = [];
    this._text = v == null ? '' : String(v);
  }
  appendChild(c) { c.parent = this; this.children.push(c); return c; }
  removeChild(c) { this.children = this.children.filter((x) => x !== c); return c; }
  replaceWith(n) {
    const p = this.parent;
    if (p) { const i = p.children.indexOf(this); p.children[i] = n; }
    n.parent = p;
  }
  remove() {
    const p = this.parent;
    if (p) p.children = p.children.filter((x) => x !== this);
  }
  setAttribute(k, v) { this.attrs[k] = String(v); }
  getAttribute(k) { return this.attrs[k]; }
  addEventListener(t, fn) { (this.listeners[t] = this.listeners[t] || []).push(fn); }
  click() { (this.listeners.click || []).forEach((f) => f()); }
  /* 结构化遍历：断言用 */
  walk(fn) { fn(this); this.children.forEach((c) => c.walk(fn)); }
  find(pred) {
    let hit = null;
    this.walk((n) => { if (!hit && pred(n)) hit = n; });
    return hit;
  }
  html() {
    const cls = this.className ? ` class="${this.className}"` : '';
    const at = Object.keys(this.attrs).map((k) => ` ${k}="${this.attrs[k]}"`).join('');
    const href = this.href ? ` href="${this.href}"` : '';
    const src = this.src ? ` src="${this.src}"` : '';
    if (this.children.length === 0) {
      return `<${this.tagName.toLowerCase()}${cls}${at}${href}${src}>${this._text}</${this.tagName.toLowerCase()}>`;
    }
    return `<${this.tagName.toLowerCase()}${cls}${at}${href}${src}>${this.children.map((c) => c.html()).join('')}</${this.tagName.toLowerCase()}>`;
  }
}

const HOSTS = {};
['video-list', 'toy-list', 'year'].forEach((id) => { HOSTS[id] = new El('div'); });
HOSTS.year.textContent = '';

const document = {
  readyState: 'complete',
  createElement: (t) => new El(t),
  getElementById: (id) => HOSTS[id] || null,
  addEventListener: () => {},
};

const sandbox = {
  document,
  console,
  Date,
  String,
  parseInt,
  encodeURIComponent,
};
sandbox.window = sandbox;
vm.createContext(sandbox);

/* ---------- 按页面真实顺序加载 data/*.js 再加载 assets/site.js ---------- */
function run(file) {
  vm.runInContext(fs.readFileSync(path.join(ROOT, file), 'utf8'), sandbox, { filename: file });
}

const results = [];
function check(name, cond, detail) {
  results.push({ name, ok: !!cond, detail: detail || '' });
}

/* ===== 第一轮：空记录（仓库当前的真实状态）===== */
sandbox.INFIVERSE_VIDEOS = [];
sandbox.INFIVERSE_TOYS = [];
run('data/site.js');
run('data/videos.js');
run('data/toys.js');
run('assets/site.js');

check('空状态：视频列表显示提示文案',
  /还没有视频记录/.test(HOSTS['video-list'].textContent),
  HOSTS['video-list'].textContent.slice(0, 80));
check('空状态：作品位显示提示文案',
  /还没有已发布的作品/.test(HOSTS['toy-list'].textContent),
  HOSTS['toy-list'].textContent.slice(0, 80));
check('页脚年份被填入',
  /^\d{4}$/.test(HOSTS.year.textContent),
  HOSTS.year.textContent);
check('data/site.js 提供了仓库地址',
  /github\.com\/infileap\/Inimerse/.test(sandbox.INFIVERSE_SITE.repoUrl),
  sandbox.INFIVERSE_SITE.repoUrl);
check('空状态无脚本抛错', true, '');

/* ===== 第二轮：未投稿的记录 ===== */
HOSTS['video-list'] = new El('div');
sandbox.INFIVERSE_VIDEOS = [{
  id: 'demo-01', title: '演示一', bvid: '未投稿', duration: '03:42',
  resolution: '1920x1080', cover: '', summary: '一句话。',
  ref: 'f1dfe62583b7a89e2395ddc502fcf2ac7b60055e', date: '2026-10-06',
}];
const src2 = fs.readFileSync(path.join(ROOT, 'assets/site.js'), 'utf8');
vm.runInContext(src2, sandbox, { filename: 'assets/site.js#2' });
const vl2 = HOSTS['video-list'];
check('未投稿：显示「待投稿后补 BV 号」徽章',
  /待投稿后补 BV 号/.test(vl2.textContent), vl2.textContent.slice(0, 120));
check('未投稿：元信息写「B站 未投稿」',
  /B站 未投稿/.test(vl2.textContent));
check('未投稿：封面缺失时给占位而不是坏图',
  /封面待交付/.test(vl2.textContent));
check('未投稿：ref 只显示前 7 位',
  /ref f1dfe62/.test(vl2.textContent) && !/f1dfe62583b7a89e/.test(vl2.textContent));
check('未投稿：不生成外链（没有可点的 a[href]）',
  vl2.find((n) => n.tagName === 'A' && n.href) === null);

/* ===== 第三轮：已投稿 + featured 的记录，并点一次「在站内播放」===== */
HOSTS['video-list'] = new El('div');
sandbox.INFIVERSE_VIDEOS = [
  {
    id: 'demo-02', title: '演示二', bvid: 'BV1xx411c7mD', duration: '10:00',
    resolution: '2560x1440', cover: 'assets/covers/demo-02.jpg', summary: '两句话。',
    ref: 'f1dfe62583b7a89e2395ddc502fcf2ac7b60055e', date: '2026-10-01', featured: true,
  },
  { id: 'demo-03', title: '竖屏演示', bvid: 'BV1yy411c7mE', orientation: 'portrait', cover: 'x.jpg' },
];
vm.runInContext(src2, sandbox, { filename: 'assets/site.js#3' });
const vl3 = HOSTS['video-list'];
check('已投稿：生成 B站 观看外链并指向正确 BV',
  !!vl3.find((n) => n.tagName === 'A' && /bilibili\.com\/video\/BV1xx411c7mD\//.test(n.href || '')),
  vl3.children[0].html().slice(0, 200));
check('已投稿：元信息写「B站 BV1xx411c7mD」', /B站 BV1xx411c7mD/.test(vl3.textContent));
check('已投稿：featured 首条带 video--featured 类',
  /video--featured/.test(vl3.children[0].className), vl3.children[0].className);
check('竖屏：封面容器带 portrait 类',
  !!vl3.find((n) => /video__cover--portrait/.test(n.className)));
check('列表：两条记录渲染成两张卡', vl3.children.length === 2, String(vl3.children.length));

const embedBtn = vl3.children[0].find((n) => n.tagName === 'BUTTON' && /在站内播放/.test(n.textContent));
check('已投稿：有「在站内播放」按钮', !!embedBtn);
if (embedBtn) {
  embedBtn.click();
  const iframe = vl3.find((n) => n.tagName === 'IFRAME');
  check('点击后在原封面位置插入 iframe', !!iframe);
  check('iframe 指向 B站播放器且带正确 bvid',
    !!iframe && /player\.bilibili\.com\/player\.html\?bvid=BV1xx411c7mD&page=1&autoplay=0/.test(iframe.src),
    iframe ? iframe.src : '(无 iframe)');
  check('插入后封面容器被替换掉（不会留下两张封面）',
    vl3.children[0].find((n) => /video__cover/.test(n.className)) === null);
}

/* ===== 第四轮：作品位有记录 ===== */
HOSTS['toy-list'] = new El('div');
sandbox.INFIVERSE_TOYS = [{
  slug: 'inimerse-demo', title: '语法演示', summary: '一句话。',
  url: 'https://www.bilibili.com/toy/inimerse-demo/index.html', status: 'live', channel: 'toy',
}];
vm.runInContext(src2, sandbox, { filename: 'assets/site.js#4' });
const tl = HOSTS['toy-list'];
check('作品位：渲染成卡并带上 Toy 链接',
  /bilibili\.com\/toy\/inimerse-demo\/index\.html/.test(tl.children[0].href || ''),
  tl.children[0].href);
check('作品位：live 状态显示「今天可玩」', /今天可玩/.test(tl.textContent));
check('作品位：标注「B站 Toy 托管」', /B站 Toy 托管/.test(tl.textContent));

/* ---------- 结果 ---------- */
let bad = 0;
results.forEach((r) => {
  if (!r.ok) bad++;
  console.log(`${r.ok ? 'PASS' : 'FAIL'}  ${r.name}${r.ok ? '' : '   ← ' + r.detail}`);
});
console.log(`\n${results.length - bad}/${results.length} 通过`);
process.exit(bad ? 1 : 0);
