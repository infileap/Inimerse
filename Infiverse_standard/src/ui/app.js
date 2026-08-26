/* Infiverse 桌面应用 — UI 骨架（B0）
 * 8 模块活动栏 + 一期三屏闭环：主页 → 工作台 → Inimerse 管理
 * Tauri 壳就绪后：window.__invoke = Tauri IPC；当前为本地占位实现
 */
'use strict';

const MODULES = {
  home:     { name: '主页', sub: '账号 · 成就 · 下载' },
  chat:     { name: '聊天', sub: '好友 · 群聊 · AI 好友' },
  browse:   { name: '资源与联机', sub: '项目下载 · 热/冷联机' },
  workbench:{ name: '工作台', sub: '项目 · declare 参数面板' },
  inimerse: { name: 'Inimerse 管理', sub: '引擎 · 一键更新 · 组件化安装' },
  toolbox:  { name: '工具箱', sub: '常用工具' },
  plugins:  { name: '插件库', sub: '插件浏览与安装' },
  settings: { name: '设置', sub: '外观 · 路径 · 关于' },
};

/* ---- 占位数据（后续接 identity_mod / verse / engine） ---- */
const PROFILE = {
  avatar: 'I', name: 'Infiverse 用户', uid: 'UID-0001',
  verse: 'verse://hub/0001', ip: '192.168.1.100', credit: 128,
};
const DOWNLOADS = [
  { name: 'Inimerse 标准版', size: '7.2 MB', time: '2026-08-16', state: '已安装' },
  { name: '桌面应用 B0 骨架', size: '0.6 MB', time: '2026-08-16', state: '最新' },
];
const ACHIEVEMENTS = [
  { icon: '🚀', name: '首次运行', desc: '启动 Inimerse 引擎', done: true },
  { icon: '🧩', name: '模组作者', desc: '通过契约测试 65 项', done: true },
  { icon: '🌐', name: '联机先锋', desc: '完成一次 verse 联机', done: false },
];
const PROJECTS = [
  { name: 'test_im.im', engine: 'v0.9.2', params: [['mem', '64MB'], ['time', '10s'], ['fps', '60']] },
  { name: 'game_demo.im', engine: 'v0.9.2', params: [['mem', '128MB'], ['world', '0']] },
];
const ENGINE = {
  path: 'D:\\inimerse_stable\\inimerse.exe', version: 'v0.9.2 (2026-08-16)', status: '正常',
};
const COMPONENTS = [
  { name: '引擎核心', size: '586 KB', state: '已安装' },
  { name: '标准扩展（GUI/联机）', size: '—', state: '已安装' },
  { name: 'AI 模型（Qwen2.5-7B）', size: '3.7 GB', state: '未安装' },
];

const $ = (s) => document.querySelector(s);

/* ---- Tauri IPC（withGlobalTauri） ---- */
async function invoke(cmd, args) {
  const t = window.__TAURI__;
  if (t && t.core) {
    try { return await t.core.invoke(cmd, args || {}); }
    catch (e) { console.error('invoke ' + cmd + ' failed:', e); return null; }
  }
  return null; // 浏览器预览模式：返回 null，保持占位数据
}

/* ---- 主页真实数据（B1：identity_mod / 本机 IP / 引擎探测） ---- */
async function loadHomeData() {
  const tauriOk = !!(window.__TAURI__ && window.__TAURI__.core);
  const diag = $('#hp-engine');
  if (diag) diag.textContent = tauriOk ? 'IPC: 已连接（Tauri）' : 'IPC: 未连接（浏览器预览）';
  const [id, ips, eng] = await Promise.all([
    invoke('get_identity'), invoke('get_local_ip'), invoke('get_engine_info'),
  ]);
  if (id) {
    $('#hp-avatar').textContent = (id.name || 'I').slice(0, 1);
    $('#hp-name').textContent = id.name || PROFILE.name;
    $('#hp-uid').textContent = (id.id || PROFILE.uid) + ' · 信用 ' + PROFILE.credit;
    $('#hp-verse').textContent = id.verse || PROFILE.verse;
    if (id.bio) $('#hp-uid').textContent += ' — ' + id.bio;
  }
  if (ips && ips.length) $('#hp-ip').textContent = '本机 IP：' + ips.join(', ');
  const pub = await invoke('get_public_ip');
  if (pub) $('#hp-ip').textContent += ' · 公网 IP：' + pub;
  if (eng && eng.path) {
    const mb = eng.size ? (eng.size / 1048576).toFixed(1) : '?';
    $('#hp-engine').textContent = '引擎：' + eng.path + ' (' + mb + ' MB)';
    ENGINE.path = eng.path;
  }
  const qr = await invoke('get_qr_svg', { text: (id && id.verse) || PROFILE.verse });
  const qbox = $('#hp-qr');
  if (qr && qbox) qbox.innerHTML = qr;
  const ach = await invoke('get_achievements');
  const abox = $('#hp-ach');
  if (ach && ach.items && abox) {
    abox.innerHTML = ach.items.map(a => `<div class="step"><span class="dot">${a.done ? '✔' : a.icon}</span><div><b>${a.name}</b><div class="muted">${a.desc}（累计 ${ach.runs} 次）</div></div></div>`).join('');
  }
}

/* ---- 渲染 ---- */
function el(html) { const d = document.createElement('div'); d.innerHTML = html.trim(); return d.firstChild; }

function achBlock() {
  return `<div class="card"><h3>🏅 成就墙</h3><div id="hp-ach">${ACHIEVEMENTS.map(a => `
    <div class="step"><span class="dot">${a.done ? '✔' : a.icon}</span>
    <div><b>${a.name}</b><div class="muted">${a.desc}</div></div></div>`).join('')}</div></div>`;
}
function dlBlock() {
  return `<div class="card"><h3>⬇️ 我的下载</h3><table><tr><th>名称</th><th>大小</th><th>状态</th></tr>
    ${DOWNLOADS.map(d => `<tr><td>${d.name}</td><td>${d.size}</td><td><span class="tag ${d.state === '已安装' ? 'ok' : ''}">${d.state}</span></td></tr>`).join('')}
  </table></div>`;
}
function renderHome() {
  const idCard = `
  <div class="row" style="gap:20px;margin-bottom:18px">
    <div class="avatar" id="hp-avatar">${PROFILE.avatar}</div>
    <div>
      <div style="font-size:20px;font-weight:600" id="hp-name">${PROFILE.name}</div>
      <div class="muted" id="hp-uid">${PROFILE.uid} · 信用 ${PROFILE.credit}</div>
      <div class="muted" id="hp-verse">${PROFILE.verse}</div>
      <div class="muted" id="hp-ip">本机 IP：${PROFILE.ip}</div>
      <div class="muted" id="hp-engine" style="margin-top:6px"></div>
      <div id="hp-qr" style="margin-top:8px"></div>
    </div>
  </div>`;
  const edit = `<div class="card"><h3>✎ 编辑资料</h3><div class="row"><input id="profile-name" placeholder="昵称" style="padding:8px;flex:1"><input id="profile-avatar" placeholder="头像字符" style="padding:8px;width:120px"></div><textarea id="profile-bio" rows="2" placeholder="简介" style="width:100%;padding:8px;margin-top:8px"></textarea><button class="btn primary" id="profile-save" style="margin-top:8px">保存资料</button><span id="profile-msg" class="muted" style="margin-left:8px"></span></div>`;
  if (homeTab === 'achievements') return idCard + achBlock();
  if (homeTab === 'downloads') return idCard + dlBlock();
  return idCard + edit + '<div class="grid">' + achBlock() + dlBlock() + '</div>' +
    `<button class="btn primary" data-go="workbench">进入工作台 →</button>`;
}

function bindProfileEditor() {
  const save = $('#profile-save'); if (!save) return;
  invoke('get_identity').then(id => { if (!id) return; $('#profile-name').value=id.name||''; $('#profile-avatar').value=id.avatar||''; $('#profile-bio').value=id.bio||''; });
  save.addEventListener('click', async () => { const r=await invoke('update_identity',{name:$('#profile-name').value,avatar:$('#profile-avatar').value,bio:$('#profile-bio').value}); const msg=$('#profile-msg'); if(msg) msg.textContent=r&&r.ok?'✅ 已保存':'❌ '+((r&&r.error)||'保存失败'); if(r&&r.ok) loadHomeData(); });
}

function renderWorkbench() {
  return `
  <div class="grid">
    <div class="card"><h3>📁 项目 / 脚本</h3>
      <div id="wb-files"><div class="muted">加载中…</div></div>
    </div>
    <div class="card"><h3>🧮 declare 参数面板</h3>
      <div id="wb-panel"><div class="muted">← 点击左侧文件加载 declare 参数（只显示可变参数）</div></div>
    </div>
  </div>`;
}
async function bindWorkbench() {
  const files = await invoke('workbench_load');
  const list = $('#wb-files');
  const panel = $('#wb-panel');
  if (!list || !panel) return;
  if (!files || !files.length) { list.innerHTML = '<div class="muted">未找到 .im 文件</div>'; return; }
  list.innerHTML = files.map((f, i) => `<div class="sb-item" data-wb="${i}">${f.file.replace(/.*[\\\\/]/, '')} <span class="muted">(${(f.declares || []).length} 参数)</span></div>`).join('');
  const render = (f) => {
    const ds = f.declares || [];
    if (!ds.length) { panel.innerHTML = '<div class="muted">该文件无 declare 参数</div>'; return; }
    panel.innerHTML = `<div class="muted" style="margin-bottom:8px">${f.file}</div>
      ${ds.map((d, i) => `<div class="step"><span style="width:90px">${d.key}</span>
        <input data-wb-val="${i}" value="${d.val}" style="flex:1;padding:6px;border:1px solid var(--border);border-radius:6px;background:var(--bg);color:var(--fg)">
        <span class="muted" style="width:40px">${d.unit || ''}</span></div>`).join('')}
      <div class="row" style="margin-top:10px">
        <button class="btn primary" id="wb-apply">💾 应用参数</button>
        <span class="muted" id="wb-msg"></span>
      </div>`;
    panel.querySelectorAll('[data-wb-val]').forEach(inp => {});
    const apply = $('#wb-apply');
    if (apply) apply.addEventListener('click', async () => {
      const items = ds.map((d, i) => {
        const inp = panel.querySelector('[data-wb-val="' + i + '"]');
        let v = inp ? parseInt(inp.value) : d.val;
        if (isNaN(v)) v = d.val;
        return { key: d.key, val: v, unit: d.unit };
      });
      const r = await invoke('workbench_apply', { file: f.file, items });
      const msg = $('#wb-msg');
      if (msg) msg.textContent = r && r.ok ? '✅ ' + r.msg : '❌ ' + (r ? r.msg : '失败');
    });
  };
  list.querySelectorAll('[data-wb]').forEach(el => el.addEventListener('click', () => {
    render(files[parseInt(el.dataset.wb)]);
  }));
  // 默认选第一个有参数的
  const first = files.find(f => (f.declares || []).length > 0);
  if (first) { const idx = files.indexOf(first); const el = list.querySelector('[data-wb="' + idx + '"]'); if (el) { el.classList.add('active'); render(first); } }
}


function renderWorkbenchIDE() {
  return `<div class="workbench-layout">
    <aside class="card wb-files"><h3>📁 项目文件</h3><div class="row" style="margin-bottom:10px"><input id="wb-new-name" placeholder="new_project" style="width:100%;padding:7px"><button class="btn" id="wb-new">新建</button></div><div id="wb-ide-files"><div class="muted">加载中…</div></div></aside>
    <section class="card wb-editor"><div class="wb-toolbar"><div><h3 id="wb-file-title">未选择文件</h3><span id="wb-state" class="muted">选择一个 .im 文件开始编辑</span></div><div class="row"><button class="btn" id="wb-save" disabled>保存</button><button class="btn primary" id="wb-run" disabled>运行</button></div></div><div class="row wb-find"><input id="wb-find" class="code" placeholder="查找" disabled><input id="wb-replace" class="code" placeholder="替换为" disabled><button class="btn" id="wb-find-next" disabled>查找下一个</button><button class="btn" id="wb-replace-all" disabled>全部替换</button></div><textarea id="wb-code" spellcheck="false" placeholder="选择左侧文件开始编辑…" disabled></textarea><pre id="wb-output" class="code hidden"></pre></section>
    <aside class="card wb-params"><h3>🧮 declare 参数</h3><div id="wb-ide-panel"><div class="muted">选择文件后显示可变参数</div></div><hr class="wb-rule"><h3>✦ AI 编程</h3><textarea id="wb-ai-prompt" rows="4" placeholder="描述要生成或修复的代码…"></textarea><button class="btn primary" id="wb-ai">生成建议</button><button class="btn" id="wb-ai-apply" disabled>应用到编辑器</button><pre id="wb-ai-out" class="code hidden"></pre></aside>
  </div>`;
}
async function bindWorkbenchIDE() {
  const files = await invoke('workbench_load');
  const list = $('#wb-ide-files'), panel = $('#wb-ide-panel'), code = $('#wb-code'), save = $('#wb-save'), run = $('#wb-run'), state = $('#wb-state'), output = $('#wb-output');
  const find = $('#wb-find'), replace = $('#wb-replace'), findNext = $('#wb-find-next'), replaceAll = $('#wb-replace-all');
  if (!list || !panel || !code || !save || !run) return;
  if (!files || !files.length) { const root = await invoke('workbench_root'); list.innerHTML = '<div class="muted">未找到 .im 文件<br><br>扫描目录：<br>' + escapeHtml(root || 'unknown') + '/plugins<br>' + escapeHtml(root || 'unknown') + '/projects</div>'; return; }
  list.innerHTML = files.map((f, i) => `<div class="sb-item" data-wb-ide="${i}">${escapeHtml(f.file.replace(/.*[\\/]/, ''))}<span class="muted"> (${(f.declares || []).length})</span></div>`).join('');
  let currentFile = null, dirty = false;
  const setState = (text) => { if (state) state.textContent = text; };
  const setDirty = (value) => { dirty = value; setState(value ? '未保存的更改' : (currentFile ? '已保存' : '选择一个 .im 文件开始编辑')); };
  const renderParams = (f) => {
    const ds = f.declares || [];
    panel.innerHTML = ds.length ? ds.map(d => `<div class="step"><span style="width:78px">${escapeHtml(d.key)}</span><span class="code" style="padding:4px 7px">${d.val}${escapeHtml(d.unit || '')}</span></div>`).join('') : '<div class="muted">此文件没有 declare 参数</div>';
  };
  const openFile = async (f, el) => {
    const r = await invoke('workbench_read', { file: f.file });
    if (!r || !r.ok) { setState('读取失败：' + ((r && r.error) || 'unknown')); return; }
    currentFile = f; code.value = r.content || ''; code.disabled = false; save.disabled = false; run.disabled = false; [find, replace, findNext, replaceAll].forEach(x => { if (x) x.disabled = false; });
    $('#wb-file-title').textContent = f.file.replace(/.*[\\/]/, '');
    list.querySelectorAll('[data-wb-ide]').forEach(x => x.classList.remove('active')); el.classList.add('active'); setDirty(false); renderParams(f);
  };
  list.querySelectorAll('[data-wb-ide]').forEach(el => el.addEventListener('click', () => openFile(files[parseInt(el.dataset.wbIde)], el)));
  code.addEventListener('input', () => setDirty(true));
  if (findNext) findNext.addEventListener('click', () => { const q = find.value; if (!q) return; const i = code.value.indexOf(q, code.selectionEnd || 0); const j = i < 0 ? code.value.indexOf(q) : i; if (j >= 0) { code.focus(); code.setSelectionRange(j, j + q.length); } });
  if (replaceAll) replaceAll.addEventListener('click', () => { const q = find.value; if (!q) return; const before = code.value; code.value = before.split(q).join(replace.value || ''); if (code.value !== before) setDirty(true); });
  code.addEventListener('keydown', e => { if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'f' && find) { e.preventDefault(); find.focus(); find.select(); } });
  code.addEventListener('keydown', e => { if (e.key === 'Tab') { e.preventDefault(); const a=code.selectionStart,b=code.selectionEnd; code.setRangeText('    ',a,b,'end'); setDirty(true); } if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); save.click(); } });
  save.addEventListener('click', async () => { if (!currentFile) return; const r = await invoke('workbench_save', { file: currentFile.file, content: code.value }); if (r && r.ok) setDirty(false); else setState('保存失败：' + ((r && r.error) || 'unknown')); });
  run.addEventListener('click', async () => { if (!currentFile || run.disabled) return; if (dirty) await save.click(); run.disabled = true; save.disabled = true; output.classList.remove('hidden'); output.textContent = '运行中…'; const r = await invoke('workbench_run', { file: currentFile.file }); const text = r ? ((r.stdout || '') + (r.stderr ? '\n' + r.stderr : '')) : ''; output.textContent = text || (r && r.error) || '无输出'; setState(r && r.ok ? '运行完成' : '运行失败（查看输出）'); run.disabled = false; save.disabled = false; if (r && !r.ok) { const m = text.match(/(?:line|行)\s*[:#]?\s*(\d+)/i); if (m) { const line = Number(m[1]); const lines = code.value.split('\n'); let pos = 0; for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1; code.focus(); code.setSelectionRange(pos, pos + (lines[line - 1] || '').length); } } });
  const create = $('#wb-new');
  if (create) create.addEventListener('click', async () => { const name = ($('#wb-new-name').value || '').trim(); const r = await invoke('workbench_create_project', { name }); if (r && r.ok) switchModule('workbench'); else setState('创建失败：' + ((r && r.error) || 'unknown')); });
  const ai = $('#wb-ai'), aiOut = $('#wb-ai-out'), aiApply = $('#wb-ai-apply');
  if (ai) ai.addEventListener('click', async () => { const prompt = ($('#wb-ai-prompt').value || '').trim(); if (!prompt) return; ai.disabled = true; aiOut.classList.remove('hidden'); aiOut.textContent = '生成中…'; const context = currentFile ? '\n\nCurrent code:\n' + code.value.slice(0, 6000) : ''; const answer = await invoke('ai_chat', { text: prompt + context }); aiOut.textContent = answer || 'AI 服务未返回内容'; ai.disabled = false; aiApply.disabled = !answer; });
  if (aiApply) aiApply.addEventListener('click', () => { if (!aiOut.textContent || aiOut.textContent === '生成中…') return; code.value = aiOut.textContent.replace(/^```(?:im)?\s*/i, '').replace(/\s*```$/, ''); setDirty(true); });
}

let chatMsgs = {};
const BROWSE = [
  { name: 'Inimerse 标准版', ver: '0.9.6', size: '7.2 MB', state: '已安装', desc: '引擎 + 标准模组 + 契约测试' },
  { name: 'Inimerse 桌面应用', ver: '0.1.0-B2', size: '12.5 MB', state: '运行中', desc: 'Tauri 壳 + 8 模块（当前项目）' },
  { name: '示例项目·贪吃蛇', ver: '0.2.0', size: '1.1 MB', state: '可下载', desc: '精灵 + 实体 + 键盘输入教学' },
  { name: '示例项目·平台跳跃', ver: '0.1.0', size: '1.8 MB', state: '可下载', desc: '物理 + 关卡 + 音效教学' },
  { name: 'AI 编译辅助规则包', ver: '1.0.0', size: '64 KB', state: '可下载', desc: '--lint 规则 + ai_code_check 提示词' },
];
function renderBrowse() {
  return `
  <div class="card"><h3>📦 项目下载</h3>
    ${BROWSE.map(p => `<div class="b-card">
      <div class="b-name">${p.name} <span class="muted">v${p.ver} · ${p.size}</span>
        <span class="tag ${p.state === '已安装' || p.state === '运行中' ? 'ok' : 'danger'}">${p.state}</span></div>
      <div class="muted">${p.desc}</div>
      <button class="btn primary" data-browse="${p.name}">${p.state === '已安装' ? '重新下载' : p.state === '运行中' ? '打开' : '下载'}</button>
    </div>`).join('')}
  </div>
  <div class="card"><h3>🌐 联机</h3>
    <div class="step"><span class="dot">🔗</span><div>本机地址：<span class="code" style="display:inline;padding:2px 8px">${PROFILE.verse}</span></div></div>
    <div class="step"><span class="dot">📡</span><div>模式：${['单机', '热联机', '冷联机'].map((m, i) => `<label class="radio"><input type="radio" name="netmode" ${i === 0 ? 'checked' : ''}> ${m}</label>`).join('')}</div></div>
    <div class="step"><span class="dot">🛰️</span><div>节点探测：<input id="net-addr" class="code" style="width:180px" value="127.0.0.1:11460">
      <button class="btn" id="btn-ping">探测</button> <span id="net-msg" class="muted"></span></div></div>
    <div class="muted">热联机 = 实时同步（verse 节点）；冷联机 = 离线交换存档/模组（.vverse 文件）</div>
  </div>
  <div class="card"><h3>👥 好友节点配置</h3><div id="friend-node-list" class="muted">加载中…</div>
    <button class="btn primary" id="btn-save-nodes" style="margin-top:6px">保存节点地址</button>
    <span class="muted" id="node-msg"></span></div>
  <div class="card"><h3>🔗 关联</h3>
    <div class="step"><span class="dot">🐙</span><div><b>GitHub</b> <span id="oauth-gh-status" class="muted">未关联</span><br><input id="oauth-client" class="code" style="width:180px" placeholder="OAuth Client ID"><input id="oauth-redirect" class="code" style="width:240px" value="http://127.0.0.1:8765/callback"><button class="btn" id="oauth-gh">授权</button></div></div>
    <div class="step"><span class="dot">📺</span><div><b>Bilibili</b> <span id="oauth-bili-status" class="muted">未关联</span><br><button class="btn" id="oauth-bili">打开授权页</button></div></div>
    <div class="muted" id="links-msg">授权后由回调服务交换 code，再调用 oauth_bind 保存资料。</div>
  </div>
  <button class="btn" data-go="workbench">→ 去工作台</button>`;
}
function bindBrowse() {
  document.querySelectorAll('[data-browse]').forEach(btn => btn.addEventListener('click', async () => {
    const name = btn.dataset.browse;
    const item = BROWSE.find(b => b.name === name);
    if (!item || item.state === '下载中') return;
    item.state = '下载中';
    $('#view-content').innerHTML = renderBrowse();
    bindBrowse();
    const r = await invoke('browse_download', { name });
    if (r && r.state) {
      item.state = r.state;
      $('#view-content').innerHTML = renderBrowse();
      bindBrowse();
      const out = document.createElement('div');
      out.className = 'muted code';
      out.textContent = '校验：' + (r.out || '');
      const card = document.querySelector('.b-card');
      if (card) card.appendChild(out);
    }
  }));
  const ping = $('#btn-ping');
  if (ping) ping.addEventListener('click', async () => {
    const addr = $('#net-addr').value || '127.0.0.1:11460';
    const msg = $('#net-msg');
    if (msg) msg.textContent = '探测中...';
    const r = await invoke('verse_ping', { addr });
    if (msg) msg.textContent = r.ok ? ('✅ 可达 ' + r.ms + 'ms') : ('❌ 不可达（' + r.ms + 'ms；端口无服务：引擎 11460(HTTP 11470)需 start_all.ps1，桌面 verse 服务 11480）');
  });
  const ghStatus = $('#oauth-gh-status'), biStatus = $('#oauth-bili-status');
  invoke('oauth_status', { provider: 'github' }).then(r => { if (ghStatus && r && r.linked) ghStatus.textContent = '已关联 · ' + (r.display_name || r.user_id || 'GitHub'); });
  invoke('oauth_status', { provider: 'bilibili' }).then(r => { if (biStatus && r && r.linked) biStatus.textContent = '已关联 · ' + (r.display_name || r.user_id || 'Bilibili'); });
  const openAuth = async (provider) => {
    const cid = ($('#oauth-client') && $('#oauth-client').value) || localStorage.getItem('oauth_client_id') || '';
    const red = ($('#oauth-redirect') && $('#oauth-redirect').value) || 'http://127.0.0.1:8765/callback';
    if (!cid) { if ($('#links-msg')) $('#links-msg').textContent = '请先填写 GitHub OAuth Client ID'; return; }
    localStorage.setItem('oauth_client_id', cid);
    const state = crypto.randomUUID ? crypto.randomUUID() : String(Date.now());
    await invoke('oauth_start_callback');
    const url = await invoke('oauth_authorize', { provider, clientId: cid, redirectUri: red, state });
    if (url) {
      const opened = await invoke('oauth_open', { url });
      if (!opened || !opened.ok) { try { window.open(url, '_blank'); } catch (e) {} }
      if ($('#links-msg')) $('#links-msg').textContent = opened && opened.ok ? '✅ 已在默认浏览器打开授权页' : '请复制授权地址到浏览器打开';
      let tries = 0; const timer = setInterval(async () => { const q = await invoke('oauth_poll_callback'); if (q || ++tries > 60) { clearInterval(timer); if (q && $('#links-msg')) $('#links-msg').textContent = '✅ 收到授权回调：' + q; } }, 1000);
    } else if ($('#links-msg')) $('#links-msg').textContent = '无法生成授权地址';
  };
  const ghBtn = $('#oauth-gh'), biBtn = $('#oauth-bili');
  if (ghBtn) ghBtn.addEventListener('click', () => openAuth('github'));
  if (biBtn) biBtn.addEventListener('click', () => openAuth('bilibili'));
}

function renderChat() {
  return `
  <div class="chat-wrap">
    <div class="chat-list" id="chat-list">
      <div class="sb-title">会话</div>
      ${CHAT_SESSIONS.map(s => `
        <div class="sb-item ${chatActive === s.id ? 'active' : ''}" data-chat="${s.id}">${s.avatar} ${s.name}
          <div class="muted" style="font-size:11px">${s.hint}</div></div>`).join('') || '<div class="muted">加载中…</div>'}
    </div>
    <div class="chat-main">
      <div class="chat-head" id="chat-head">${CHAT_SESSIONS.find(s => s.id === chatActive)?.name || '选择会话'}</div>
      <div class="chat-msgs" id="chat-msgs">${renderMsgs()}</div>
      <div class="chat-input">
        <input id="chat-text" placeholder="输入消息，回车发送" />
        <button class="btn primary" id="chat-send">发送</button>
      </div>
    </div>
  </div>`;
}
function renderMsgs() {
  const list = chatMsgs[chatActive] || [];
  return list.map(m => `
    <div class="msg ${m.who === 'me' ? 'msg-me' : 'msg-other'}">
      <div class="msg-bubble">${escapeHtml(m.text)}</div>
    </div>`).join('');
}
function escapeHtml(s) { return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); }
function saveChat() {
  invoke('chat_save', { session: chatActive, messages: JSON.stringify(chatMsgs[chatActive] || []) });
}
async function loadChat(session) {
  const saved = await invoke('chat_load', { session });
  if (saved) {
    try {
      const arr = JSON.parse(saved);
      if (Array.isArray(arr)) { chatMsgs[session] = arr; $('#chat-msgs').innerHTML = renderMsgs(); }
    } catch (e) {}
  }
}

async function bindChat() {
  invoke('verse_start');
  const list = $('#chat-list');
  if (list) list.querySelectorAll('[data-chat]').forEach(el => el.addEventListener('click', () => {
    chatActive = el.dataset.chat;
    renderSidebar();
    $('#view-content').innerHTML = renderChat();
    bindChat();
    $('#chat-text').focus();
    if (!AI_SESSIONS.has(chatActive)) loadChat(chatActive);
  }));
  const send = () => {
    const input = $('#chat-text');
    const text = (input.value || '').trim();
    if (!text) return;
    if (chatActive === 'g1' && !(chatMsgs.g1 || []).length) chatMsgs.g1 = [{ who: 'me', text: '大家好～' }];
    (chatMsgs[chatActive] = chatMsgs[chatActive] || []).push({ who: 'me', text });
    $('#chat-msgs').innerHTML = renderMsgs();
    input.value = '';
    saveChat();
    if (!AI_SESSIONS.has(chatActive) && chatActive !== 'g1') {
      const head = $('#chat-head');
      const nm = CHAT_SESSIONS.find(s => s.id === chatActive)?.name || '';
      if (head) head.textContent = nm + ' · verse 推送中…';
      const fr = CHAT_SESSIONS.find(s => s.id === chatActive);
      invoke('verse_send', { addr: (fr && fr.node) || '127.0.0.1:11480', text }).then(r => {
        if (head) head.textContent = nm + (r && r.ok ? ' · ✅ 已推送' : ' · ⚠ 节点离线，仅本地记录');
        setTimeout(() => { if (head) head.textContent = nm; }, 2500);
      });
    }
    if (AI_SESSIONS.has(chatActive)) {
      if (!chatMsgs[chatActive]) chatMsgs[chatActive] = [];
      (chatMsgs[chatActive]).push({ who: 'sys', text: '…' });
      $('#chat-msgs').innerHTML = renderMsgs();
      invoke('ai_chat', { text }).then(reply => {
        (chatMsgs[chatActive] || []).pop();
        (chatMsgs[chatActive] = chatMsgs[chatActive] || []).push({ who: 'ai', text: reply || '（无回复）' });
        $('#chat-msgs').innerHTML = renderMsgs();
        saveChat();
      });
    } else {
      (chatMsgs[chatActive] = chatMsgs[chatActive] || []).push({ who: 'ai', text: '（消息已记录，联机回复待 B3）' });
      $('#chat-msgs').innerHTML = renderMsgs();
      saveChat();
    }
  };
  const btn = $('#chat-send');
  if (btn) btn.addEventListener('click', send);
  const input = $('#chat-text');
  if (input) input.addEventListener('keydown', e => { if (e.key === 'Enter') send(); });
  const friends = await invoke('friends_list');
  if (friends && friends.length) {
    CHAT_SESSIONS = friends.map(f => ({ id: f.id, name: f.name, avatar: f.avatar, hint: f.hint, is_ai: !!f.is_ai }))
      .concat([{ id: 'g1', name: '我的群聊', avatar: '👥', hint: '好友群聊（待 B3 联机）' }]);
    AI_SESSIONS = new Set(CHAT_SESSIONS.filter(s => s.is_ai).map(s => s.id));
    if (!CHAT_SESSIONS.find(s => s.id === chatActive)) chatActive = CHAT_SESSIONS[0].id;
    const head = $('#chat-head');
    if (head) head.textContent = CHAT_SESSIONS.find(s => s.id === chatActive)?.name || '选择会话';
    const lst = $('#chat-list');
    if (lst) {
      lst.innerHTML = CHAT_SESSIONS.map(s => `<div class="sb-item ${chatActive === s.id ? 'active' : ''}" data-chat="${s.id}">${s.avatar} ${s.name}
        <div class="muted" style="font-size:11px">${s.hint}</div></div>`).join('');
      lst.querySelectorAll('[data-chat]').forEach(el => el.addEventListener('click', () => {
        chatActive = el.dataset.chat;
        renderSidebar();
        $('#view-content').innerHTML = renderChat();
        bindChat();
        $('#chat-text').focus();
        if (!AI_SESSIONS.has(chatActive)) loadChat(chatActive);
      }));
    }
    $('#chat-msgs').innerHTML = renderMsgs();
  }
  if (window._chatPoll) clearInterval(window._chatPoll);
  window._chatPoll = setInterval(() => {
    if (chatActive && !AI_SESSIONS.has(chatActive) && chatActive !== 'g1') loadChat(chatActive);
  }, 3000);
}

function renderTools() {
  return `
  <div class="card"><h3>🧰 工具箱</h3>
    <div class="step"><span class="dot">📄</span><div>脚本：<select id="tool-file" class="code"></select></div></div>
    <div class="step"><span class="dot">🔍</span><button class="btn primary" data-tool="lint">lint 检查</button>
      <button class="btn" data-tool="stats">代码统计</button>
      <button class="btn" data-tool="desugar">一键脱糖</button></div>
    <div class="step"><span class="dot">💾</span><button class="btn" data-tool="backup">备份列表</button>
      <span class="muted" id="tool-bk"></span></div>
    <pre class="code-block" id="tool-out">选择脚本后运行工具</pre>
  </div>
  <div class="card"><h3>📡 节点探测</h3>
    <div class="step"><span class="dot">🖥</span><div>地址：<input id="tool-ping" class="code" value="127.0.0.1:11460" />
      <button class="btn primary" data-tool="ping">探测</button>
      <span class="muted" id="tool-ping-out"></span></div></div>
  </div>`;
}
function bindTools() {
  const sel = $('#tool-file');
  if (sel) invoke('tool_files').then(files => {
    sel.innerHTML = files.map(f => '<option value="' + escapeHtml(f) + '">' + escapeHtml(f.split('/').pop()) + '</option>').join('');
  });
  document.querySelectorAll('[data-tool]').forEach(b => b.addEventListener('click', () => {
    const act = b.dataset.tool;
    const out = $('#tool-out');
    const path = sel ? sel.value : '';
    if (act === 'lint') { out.textContent = '运行中…'; invoke('tool_lint', { path }).then(r => out.textContent = r || '（无警告）'); }
    else if (act === 'stats') { invoke('tool_stats', { path }).then(r => out.textContent = r ? '行数=' + r.lines + ' 代码=' + r.code + ' 注释=' + r.comments + ' 函数/任务=' + r.fns : '（无）'); }
    else if (act === 'desugar') { out.textContent = '运行中…'; invoke('tool_desugar', { path }).then(r => out.textContent = r || '（空）'); }
    else if (act === 'backup') { invoke('backup_list').then(r => $('#tool-bk').textContent = r && r.length ? '最新：' + r[0] + '（共' + r.length + '个）' : '（无备份）'); }
    else if (act === 'ping') {
      const po = $('#tool-ping-out');
      po.textContent = '探测中…';
      invoke('verse_ping', { addr: ($('#tool-ping').value || '127.0.0.1:11460') }).then(r => {
        po.textContent = r && r.ok ? '✅ 可达 ' + r.ms + 'ms' : '❌ 不可达（端口无服务：引擎 11460(HTTP 11470)需 start_all.ps1，桌面 verse 服务 11480）';
      });
    }
  }));
}
function renderPlugins() {
  return `
  <div class="card"><h3>📥 安装插件</h3>
    <div class="step"><span class="dot">📄</span><div>
      <select id="pl-install-src" class="code" style="width:280px"></select>
      <button class="btn primary" id="pl-install-btn">安装到 plugins/</button>
      <span id="pl-install-msg" class="muted"></span>
    </div></div>
  </div>
  <div class="card"><h3>🧩 插件列表</h3><input id="pl-search" placeholder="搜索插件名称或路径…" style="width:100%;padding:9px;margin-bottom:10px"><div id="pl-list">加载中…</div></div>`;
}
function bindPlugins() {
  const src = $('#pl-install-src');
  if (src) {
    invoke('tool_files').then(files => {
      if (files && files.length) {
        src.innerHTML = files.map(f => `<option value="${f}">${f.split('/').pop()}</option>`).join('');
      }
    });
    const btn = $('#pl-install-btn');
    if (btn) btn.addEventListener('click', async () => {
      const msg = $('#pl-install-msg');
      const r = await invoke('plugin_install', { path: src.value });
      if (r && r.ok) { msg.textContent = '✅ 已安装: ' + r.name; refreshPluginList(); }
      else msg.textContent = '❌ 失败: ' + (r && r.err ? r.err : '');
    });
  }
  refreshPluginList();
}
function refreshPluginList() {
  const list = $('#pl-list');
  if (!list) return;
  invoke('plugin_list').then(plugins => {
    const q = (($('#pl-search') && $('#pl-search').value) || '').trim().toLowerCase();
    const visible = (plugins || []).filter(pl => !q || (pl.name + ' ' + pl.path).toLowerCase().indexOf(q) >= 0);
    if (!visible.length) { list.innerHTML = '<div class="muted">' + (q ? '没有匹配的插件' : '暂无插件（可安装或查看 mods/projects 脚本）') + '</div>'; return; }
    list.innerHTML = visible.map(pl => {
      const name = pl.name;
      const dis = pl.path && pl.path.indexOf('.disabled') >= 0;
      const stars = localStorage.getItem('pl-rate-' + name) || '0';
      return `<div class="step" style="margin:6px 0"><span class="dot">${dis ? '⏸️' : '✅'}</span><div>
        <b>${name}</b> <span class="muted">${pl.path}</span><br>
        <button class="btn" data-pl-toggle="${name}" data-pl-dis="${dis ? 1 : 0}">${dis ? '启用' : '停用'}</button>
        ${pl.path && /[\\/]plugins[\\/]/i.test(pl.path) ? `<button class="btn danger" data-pl-remove="${name}">移除</button>` : ''}
        <span class="muted">评分:</span>
        ` + [1,2,3,4,5].map(s => `<button class="btn" data-pl-rate="${name}" data-pl-s="${s}" style="padding:2px 6px">${s <= +stars ? '★' : '☆'}</button>`).join('') + `
      </div></div>`;
    }).join('');
    list.querySelectorAll('[data-pl-toggle]').forEach(b => b.addEventListener('click', async () => {
      await invoke('plugin_toggle', { name: b.dataset.plToggle });
      refreshPluginList();
    }));
    list.querySelectorAll('[data-pl-rate]').forEach(b => b.addEventListener('click', () => {
      localStorage.setItem('pl-rate-' + b.dataset.plRate, b.dataset.plS);
      refreshPluginList();
    }));
    list.querySelectorAll('[data-pl-remove]').forEach(b => b.addEventListener('click', async () => { if (!confirm('确认移除 ' + b.dataset.plRemove + '？')) return; const r = await invoke('package_remove', { name: b.dataset.plRemove }); if (r && r.ok) refreshPluginList(); }));
  });
  const search = $('#pl-search');
  if (search && !search.dataset.bound) { search.dataset.bound = '1'; search.addEventListener('input', refreshPluginList); }
}
function bindBrowse() {
  document.querySelectorAll('[data-browse]').forEach(b => b.addEventListener('click', () => {
    const name = b.dataset.browse;
    b.textContent = '下载中…';
    invoke('browse_download', { name }).then(r => {
      const card = b.closest('.b-card');
      const st = card ? card.querySelector('.tag') : null;
      if (st) st.textContent = (r && r.state) || '';
      if (card) {
        let tip = card.querySelector('.b-tip');
        if (!tip) { tip = document.createElement('div'); tip.className = 'muted b-tip'; card.appendChild(tip); }
        tip.textContent = r && r.out ? ('校验：' + String(r.out).slice(0, 90)) : '';
      }
      b.textContent = '重新下载';
    });
  }));
  const ns = $('#net-addr');
  const pm = $('#net-msg');
  const pb = $('#btn-ping');
  if (pb) pb.addEventListener('click', () => {
    pm.textContent = '探测中…';
    invoke('verse_ping', { addr: (ns ? ns.value : '127.0.0.1:11460') }).then(r => {
      pm.textContent = r && r.ok ? '✅ 可达 ' + r.ms + 'ms' : '❌ 不可达（端口无服务：引擎 11460(HTTP 11470)需 start_all.ps1，桌面 verse 服务 11480）';
    });
  });
  // 好友节点配置
  const fl = $('#friend-node-list');
  if (fl) invoke('friends_list').then(friends => {
    const real = (friends || []).filter(f => !f.is_ai && f.id);
    fl.innerHTML = real.length ? real.map(f => '<div class="step"><span class="dot">👤</span><div>' + escapeHtml(f.name) + ' <span class="muted">' + escapeHtml(f.id) + '</span><br><input data-fid="' + escapeHtml(f.id) + '" class="code" value="' + escapeHtml(f.node || '') + '" placeholder="节点地址，如 192.168.1.5:11480" /></div></div>').join('') : '<div class="muted">暂无 verse 好友（friends.json）</div>';
  });
  const sb = $('#btn-save-nodes');
  if (sb) sb.addEventListener('click', () => {
    document.querySelectorAll('[data-fid]').forEach(inp => {
      invoke('verse_save_node', { friendId: inp.dataset.fid, node: inp.value.trim() });
    });
    const msg = $('#node-msg');
    if (msg) { msg.textContent = '✅ 节点配置已保存（friends.json）'; setTimeout(() => { msg.textContent = ''; }, 2500); }
  });
}
function renderPluginsAndBind() { bindPlugins(); }

function renderInimerse() {
  const steps = ['git pull', 'build.ps1', '备份旧 exe', '替换', '重启'];
  return `
  <div class="card"><h3>🧩 引擎</h3>
    <div class="step"><span class="dot">📍</span><div>路径：<span class="code" style="display:inline;padding:2px 8px">${ENGINE.path}</span>
      <span class="muted">（inimerse where）</span></div></div>
    <div class="step"><span class="dot">ℹ️</span><div>版本：${ENGINE.version} <span class="tag ok">${ENGINE.status}</span></div></div>
    <div class="row" style="margin-top:10px">
      <button class="btn primary" id="btn-update">🔄 一键更新</button>
      <button class="btn" id="btn-which">🔎 重新发现路径</button>
      <select id="update-channel" class="code"><option value="stable">Stable</option><option value="preview">Preview</option><option value="source">Source</option></select>
    </div>
    <div id="update-log" class="code hidden" style="margin-top:10px"></div>
  </div>
  <div class="card"><h3>🔎 引擎发现（inimerse where）</h3><div id="where-list" class="muted">探测中…</div></div>
  <div class="card"><h3>🗂️ 引擎版本</h3><div id="engine-versions" class="muted">扫描版本中…</div></div>
  <div class="card" id="engine-components"><h3>📦 组件化安装</h3><div id="components-list">加载中…</div></div>
  <div class="card"><h3>🛠️ Repair 自检</h3><button class="btn" id="btn-repair">扫描安装完整性</button><span id="repair-msg" class="muted" style="margin-left:8px"></span><div id="repair-list" class="code hidden" style="margin-top:10px"></div></div>
  <div class="card" id="engine-changes"><h3>📜 变更日志</h3><div id="changes-box" class="code" style="max-height:220px;overflow:auto">加载中…</div></div>
  <button class="btn" data-go="home">← 返回主页</button>`;
}

/* Inimerse 管理：一键更新真实流水线 */
function bindEngineActions() {
  const btn = $('#btn-update');
  if (!btn) return;
  invoke('inimerse_where').then(r => {
    const el = $('#where-list');
    if (!el) return;
    if (r && r.length) {
      el.innerHTML = r.map(x => `<div>📍 ${x.path} <span class="muted">(${x.size} B)</span></div>`).join('');
    } else el.textContent = '未找到 inimerse.exe';
  });
  invoke('read_changes').then(c => {
    const el = $('#changes-box');
    if (el) el.textContent = c || '（无变更日志）';
  });
  invoke('engine_versions').then(list => {
    const el = $('#engine-versions'); if (!el) return;
    el.innerHTML = list && list.length ? list.map(x => `<div class="step"><span class="dot">${x.selected ? '✅' : '⚙️'}</span><div style="flex:1"><span class="code" style="padding:3px 7px">${escapeHtml(x.path)}</span><span class="muted"> ${x.size} B</span></div><button class="btn" data-engine-select="${escapeHtml(x.path)}" ${x.selected ? 'disabled' : ''}>${x.selected ? '当前版本' : '设为默认'}</button></div>`).join('') : '<div class="muted">未发现引擎版本。可将版本放入 engines/ 或 versions/ 目录。</div>';
    el.querySelectorAll('[data-engine-select]').forEach(b => b.addEventListener('click', async () => { const r = await invoke('engine_select', { path: b.dataset.engineSelect }); if (r && r.ok) bindEngineActions(); }));
  });
  invoke('components_list').then(list => {
    const el=$('#components-list'); if(!el) return;
    el.innerHTML=(list||[]).map(c => `<div class="step"><span class="dot">${c.installed?'✅':'⬇️'}</span><div style="flex:1"><b>${escapeHtml(c.name)}</b><span class="muted"> · ${(c.size/1048576).toFixed(2)} MB${c.required?' · 必需':''}</span></div><button class="btn" data-component="${c.id}" data-installed="${c.installed?1:0}" ${c.required?'disabled':''}>${c.installed?'卸载':'安装'}</button></div>`).join('');
    el.querySelectorAll('[data-component]').forEach(b=>b.addEventListener('click',async()=>{const r=await invoke('component_set',{id:b.dataset.component,installed:b.dataset.installed!=='1'}); if(r&&r.ok) bindEngineActions();}));
  });
  const channel = $('#update-channel');
  if (channel) {
    invoke('update_channel_get').then(v => { channel.value = v || 'stable'; });
    channel.addEventListener('change', async () => { const r = await invoke('update_channel_set', { channel: channel.value }); const log = $('#update-log'); if (log) log.textContent = r && r.ok ? '更新通道已设为 ' + channel.value : '通道设置失败'; });
  }
  const repair = $('#btn-repair');
  if (repair) repair.addEventListener('click', async () => { repair.disabled=true; const r=await invoke('repair_scan'); const msg=$('#repair-msg'), box=$('#repair-list'); if(msg) msg.textContent=r&&r.ok?'✅ 核心文件完整':'⚠ 发现缺失或异常文件'; if(box){box.classList.remove('hidden'); box.textContent=(r&&r.checks||[]).map(x=>(x.ok?'✅ ':'❌ ')+x.name).join('\n');} repair.disabled=false; });
  const which = $('#btn-which');
  if (which) which.addEventListener('click', async () => {
    const r = await invoke('inimerse_where');
    const el = $('#where-list');
    if (el) el.innerHTML = r && r.length
      ? r.map(x => `<div>📍 ${escapeHtml(x.path)} <span class="muted">(${x.size} B)</span></div>`).join('')
      : '未找到 inimerse.exe';
  });
  btn.addEventListener('click', async () => {
    const logEl = $('#update-log');
    logEl.classList.remove('hidden');
    logEl.textContent = '启动流水线...';
    await invoke('update_engine');
    // 轮询日志
    const poll = setInterval(async () => {
      const log = await invoke('get_update_log');
      if (logEl) logEl.textContent = log || '（等待日志...）';
      if (log && log.includes('5/5 完成')) {
        clearInterval(poll);
        btn.textContent = '✅ 已更新（重启服务生效）';
      }
    }, 1200);
  });
}

const PLACEHOLDERS = {
  chat: `<div class="card"><h3>💬 聊天</h3><div class="muted">好友 / 群聊 / AI 好友（对接 identity_mod + ai_mod，Discord 式布局）— B2</div></div>`,
  browse: `<div class="card"><h3>🌐 资源与联机</h3><div class="muted">Inimerse 项目下载 · GitHub/B站关联 · 单机/热联机/冷联机（verse_dist + server_mod）— B3</div></div>`,
  toolbox: `<div class="card"><h3>🛠️ 工具箱</h3><div class="muted">常用工具 — B6</div></div>`,
  plugins: `<div class="card"><h3>🧲 插件库</h3><div class="muted">插件浏览与安装 — B7</div></div>`,
  settings: `<div class="card"><h3>⚡ 设置</h3>
    <div class="step"><span style="width:120px">外观</span><button class="btn" id="theme-toggle-2">切换深/浅色</button></div>
    <div class="step"><span style="width:120px">引擎路径</span><span class="code" style="display:inline;padding:2px 8px">${ENGINE.path}</span></div>
    <div class="muted" style="margin-top:8px">Infiverse 桌面应用 — B0 骨架（Tauri 壳待接）</div>
  </div>`,
};

/* ---- 模块切换 ---- */
let homeTab = 'overview';
let chatActive = '';
let CHAT_SESSIONS = [];
let AI_SESSIONS = new Set();
let current = 'home';
function renderSidebar() {
  const s = $('#sidebar-content');
  if (current === 'workbench') {
    s.innerHTML = `<div class="sb-title">项目</div>
      ${PROJECTS.map(p => `<div class="sb-item active">${p.name}</div>`).join('')}
      <div class="sb-meta">工作台 — 参数面板只读 declare 可变参数</div>`;
  } else if (current === 'inimerse') {
    s.innerHTML = `<div class="sb-title">Inimerse</div>
      <div class="sb-item active" data-engine-section="top">引擎管理</div><div class="sb-item" data-engine-section="engine-components">组件安装</div><div class="sb-item" data-engine-section="engine-changes">变更日志</div>
      <div class="sb-meta">source 一键更新：git pull → build.ps1 → 替换 → 重启</div>`;
    s.querySelectorAll('[data-engine-section]').forEach(el => el.addEventListener('click', () => {
      const id = el.dataset.engineSection;
      if (id === 'top') { $('#view-content').scrollTo({ top: 0, behavior: 'smooth' }); return; }
      const target = document.getElementById(id);
      if (target) target.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }));
  } else if (current === 'home') {
    s.innerHTML = `<div class="sb-title">主页</div>
      <div class="sb-item ${homeTab === 'overview' ? 'active' : ''}" data-home-tab="overview">概览</div>
      <div class="sb-item ${homeTab === 'achievements' ? 'active' : ''}" data-home-tab="achievements">成就</div>
      <div class="sb-item ${homeTab === 'downloads' ? 'active' : ''}" data-home-tab="downloads">下载记录</div>`;
    s.querySelectorAll('[data-home-tab]').forEach(el => el.addEventListener('click', () => {
      homeTab = el.dataset.homeTab;
      renderSidebar();
      $('#view-content').innerHTML = renderHome();
      loadHomeData();
    }));
  } else {
    s.innerHTML = `<div class="sb-title">${MODULES[current].name}</div><div class="sb-item active">${MODULES[current].name}</div>`;
  }
}
function switchModule(name) {
  current = name;
  document.querySelectorAll('.ab-item[data-mod]').forEach(b => b.classList.toggle('active', b.dataset.mod === name));
  $('#view-title').textContent = MODULES[name].name;
  $('#view-sub').textContent = MODULES[name].sub;
  $('#view-content').innerHTML = name === 'chat' ? renderChat() : name === 'browse' ? renderBrowse()
    : name === 'home' ? renderHome()
    : name === 'workbench' ? renderWorkbenchIDE()
    : name === 'inimerse' ? renderInimerse()
    : name === 'toolbox' ? renderTools()
    : name === 'plugins' ? renderPlugins()
    : name === 'settings' ? renderSettings()
    : PLACEHOLDERS[name];
  renderSidebar();
  if (name === 'chat') bindChat();
  if (name === 'workbench') bindWorkbenchIDE();
  if (name === 'toolbox') bindTools();
  if (name === 'plugins') bindPlugins();
  if (name === 'inimerse') bindEngineActions();
  if (name === 'settings') bindSettings();
  if (name === 'browse') bindBrowse();
  location.hash = name;
  if (name === 'home') loadHomeData();
  if (name === 'home') bindProfileEditor();
}

/* ---- 事件 ---- */
document.querySelectorAll('.ab-item[data-mod]').forEach(b =>
  b.addEventListener('click', () => switchModule(b.dataset.mod)));

/* 启动埋点：record_run（成就统计；等 Tauri 全局就绪） */
setTimeout(() => {
  invoke('record_run').then(v => {
    if (v && v.runs) {
      const el2 = $('#hp-engine');
      if (el2) el2.textContent = '引擎：' + (ENGINE.path || '?') + ' · 已记录 ' + v.runs + ' 次运行';
    }
  });
}, 1200);

/* 启动即启动 verse 消息服务（127.0.0.1:11480，供好友消息跨端同步） */
setTimeout(() => invoke('verse_start'), 800);

/* /�˹record_run1ߡ	 */
invoke('record_run');

$('#view-content').addEventListener('click', (e) => {
  const go = e.target.closest('[data-go]');
  if (go) { switchModule(go.dataset.go); return; }
  if (e.target.closest('[data-run]')) {
    $('#view-sub').textContent = `运行 ${e.target.closest('[data-run]').dataset.run}…（对接引擎）`;
  }
  if (e.target.closest('[data-open]')) { /* 项目打开占位 */ }
});


/* 主题切换 */
function toggleTheme() {
  const cur = document.documentElement.getAttribute('data-theme');
  document.documentElement.setAttribute('data-theme', cur === 'dark' ? 'light' : 'dark');
}
$('#theme-toggle').addEventListener('click', toggleTheme);
$('#view-content').addEventListener('click', (e) => { if (e.target.closest('#theme-toggle-2')) toggleTheme(); });

/* 启动 */

function renderSettings() {
  const net = JSON.parse(localStorage.getItem('net-cfg') || '{}');
  return `
  <div class="card"><h3>🌐 网络</h3>
    <div class="step"><span class="dot">📡</span><div>引擎地址：
      <input id="set-net-addr" class="code" style="width:180px" value="${net.addr || '127.0.0.1:11460'}">
      <span class="muted">（探测/联机默认）</span></div></div>
    <button class="btn primary" id="set-net-save">保存网络设置</button> <span id="set-net-msg" class="muted"></span>
  </div>
  <div class="card"><h3>🗣️ 语言</h3>
    <div class="step"><span class="dot">🌏</span><div>
      <select id="set-lang" class="code"><option value="zh">中文</option><option value="en">English</option></select>
      <span class="muted">（界面文案切换开发中，先记录偏好）</span></div></div>
  </div>
  <div class="card"><h3>📚 教程</h3>
    <div class="step"><span class="dot">📖</span><div><button class="btn" id="set-tut">打开 API_REFERENCE.md</button>
      <button class="btn" id="set-tut2">打开 ROADMAP</button></div></div>
  </div>
  <div class="card"><h3>🧹 清除缓存</h3>
    <div class="step"><span class="dot">🗑️</span><div><button class="btn danger" id="set-clear">清除本地缓存（聊天记录/脱糖输出）</button>
      <span id="set-clear-msg" class="muted"></span></div></div>
  </div>`;
}
function bindSettings() {
  const lang = $('#set-lang');
  if (lang) lang.value = localStorage.getItem('lang') || 'zh';
  if (lang) lang.addEventListener('change', () => localStorage.setItem('lang', lang.value));
  const sa = $('#set-net-save');
  if (sa) sa.addEventListener('click', () => {
    const addr = $('#set-net-addr').value || '127.0.0.1:11460';
    localStorage.setItem('net-cfg', JSON.stringify({ addr }));
    const msg = $('#set-net-msg'); if (msg) msg.textContent = '✅ 已保存（探测默认改为 ' + addr + '）';
  });
  const tut = $('#set-tut');
  if (tut) tut.addEventListener('click', () => { try { window.open('file:///D:/inimerse_stable/docs/API_REFERENCE.md'); } catch (e) {} });
  const tut2 = $('#set-tut2');
  if (tut2) tut2.addEventListener('click', () => { try { window.open('file:///D:/inimerse_stable/docs/ROADMAP.md'); } catch (e) {} });
  const clr = $('#set-clear');
  if (clr) clr.addEventListener('click', async () => {
    const r = await invoke('clear_userdata_cache');
    localStorage.clear();
    const msg = $('#set-clear-msg'); if (msg) msg.textContent = '✅ 已清除（缓存文件 ' + (r && r.removed ? r.removed : 0) + ' 个）';
  });
}

const fromHash = location.hash.replace('#', '');
switchModule(MODULES[fromHash] ? fromHash : 'home');
