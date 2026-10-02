'use strict';
/*
 * Infiverse desktop: the eight EXISTING modules, fixed by DOM assertion.
 *
 * WHY THIS SUITE EXISTS, AND WHAT IT DELIBERATELY DOES NOT CLAIM
 * --------------------------------------------------------------
 * BOARD row `forge-panels` originally asked for "Verse Forge 第一批时空 / 物理 /
 * 蓝图面板 | 面板可用 + 截图或录屏证据", on the stated premise that the panels
 * live in `Infiverse_standard/src/ui/`.  Measured: they do not exist.  `grep -i
 * forge` over the repository hits documentation only; `grep -i
 * 'forge|spacetime|蓝图|blueprint'` over every `.rs`/`.js`/`.html`/`.json`/`.md`
 * under `Infiverse_standard/` returns nothing.  So that criterion was
 * unverifiable in principle -- there was nothing to photograph.
 *
 * The row's criterion was therefore narrowed (with the narrowing stated on the
 * BOARD, not applied silently) to the modules that DO exist: the eight entries
 * of `MODULES` at `Infiverse_standard/src/ui/app.js:7-16`.
 *
 * What passing here proves: those eight modules render, switching between them
 * swaps the view, and each one issues the IPC commands it is supposed to issue.
 * What it does NOT prove: that the real Tauri shell works, or that any panel is
 * "usable" in the original sense -- that still needs the shell, and the shell
 * still cannot be built here (no webkit2gtk-4.1 / libsoup-3.0 / gtk+-3.0 / even
 * pkg-config, and no passwordless sudo).
 *
 * DOM dependency: jsdom is required, and is NOT vendored into this repository
 * (there is no package.json here, and every other suite under tools/ is
 * dependency-free).  Rather than silently depend on it, this suite SELF-SKIPS
 * with an explicit message when jsdom is absent, exactly like
 * tools/wasm_host.test.js does when the wasm module has not been built.  A skip
 * is visible in the output on purpose; it is never a silent pass.
 */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const UI_DIR = path.resolve(__dirname, '..', 'Infiverse_standard', 'src', 'ui');

// Top-level `function name() {}` declarations, in source order.  Used below to
// assert the file declares no name twice.
function topLevelFunctionNames(src) {
  return [...src.matchAll(/^function\s+([A-Za-z_$][\w$]*)\s*\(/gm)].map((m) => m[1]);
}

let JSDOM;
try {
  ({ JSDOM } = require('jsdom'));
} catch {
  console.log('infiverse panels test skipped (jsdom not installed -- npm i jsdom)');
  process.exit(0);
}

// The eight modules, asserted against the source of truth rather than a
// hand-copied list.  Reading app.js for MODULES keys means adding a ninth
// module fails this suite until the expectation below is updated -- the list
// cannot drift away from the app.
function modulesFromSource(src) {
  const block = src.slice(src.indexOf('const MODULES = {'));
  const body = block.slice(0, block.indexOf('};'));
  const ids = [];
  for (const m of body.matchAll(/^\s*([a-z]+):\s*\{/gm)) ids.push(m[1]);
  return ids;
}

const EXPECTED_MODULES = ['home', 'chat', 'browse', 'workbench', 'inimerse', 'toolbox', 'plugins', 'settings'];

// IPC command sequences observed from a real load.  These are pinned so that a
// refactor which stops calling an engine command cannot pass unnoticed.
const EXPECTED_IPC = [
  'record_run',
  'get_identity',
  'get_local_ip',
  'get_engine_info',
  'get_identity',
  'get_public_ip',
  'get_qr_svg',
  'get_achievements',
];

(async () => {
  const appJs = fs.readFileSync(path.join(UI_DIR, 'app.js'), 'utf8');
  const html = fs.readFileSync(path.join(UI_DIR, 'index.html'), 'utf8');

  // (1) The module table and our expectation must agree.
  const ids = modulesFromSource(appJs);
  assert.deepEqual(
    ids,
    EXPECTED_MODULES,
    `app.js MODULES changed: got ${JSON.stringify(ids)} -- update this suite and docs/BOARD.md row forge-panels`,
  );

  // (2) The activity bar must actually offer all eight.  Declared modules that
  //     the user cannot click are not "rendered panels".
  const buttons = [...html.matchAll(/data-mod="([a-z]+)"/g)].map((m) => m[1]);
  assert.deepEqual(
    buttons,
    EXPECTED_MODULES,
    `index.html activitybar buttons (${buttons.length}) do not match the eight modules`,
  );

  // (3) Load the real page with the real app.js and a recording IPC stub.
  const calls = [];
  const dom = new JSDOM(html, {
    runScripts: 'dangerously',
    url: 'http://127.0.0.1/',
    pretendToBeVisual: true,
    beforeParse(win) {
      // The stub records every command the app issues.  app.js:48 already has a
      // no-Tauri path (app.js:70 tauriOk) that keeps placeholder data, so this
      // does not fabricate behaviour the browser preview lacks.
      win.__TAURI__ = {
        core: {
          invoke(cmd, args) {
            calls.push({ cmd, args });
            if (cmd === 'get_identity') return Promise.resolve({ name: 'Infiverse 用户', uid: 'UID-0001' });
            // app.js:83 does `ips.join(', ')`, so this one is a LIST, not a
            // string.  Returning a string here throws inside the app's promise
            // chain and is exactly the kind of shape error this suite exists to
            // catch -- it was caught this way while writing the suite.
            if (cmd === 'get_local_ip') return Promise.resolve(['192.168.1.100']);
            if (cmd === 'get_public_ip') return Promise.resolve('203.0.113.7');
            // app.js:85 only uses `eng.path`; ENGINE.path is assigned from it.
            if (cmd === 'get_engine_info') return Promise.resolve({ path: 'D:\\inimerse_stable\\inimerse.exe', version: 'v0.9.2', status: '正常' });
            if (cmd === 'get_qr_svg') return Promise.resolve('<svg></svg>');
            if (cmd === 'get_achievements') return Promise.resolve([]);
            // app.js:513 does `files.map(...)` on this one, and app.js:94 does
            // `ach.map(...)` on get_achievements above -- both are LISTS.  A
            // bare `null` here throws inside the app's own promise chain, which
            // rejects unhandled and kills the process before any assertion
            // runs.  Returning list shapes keeps the failure inside this suite
            // where it can be reported usefully.
            if (cmd === 'tool_files') return Promise.resolve([]);
            // The OAuth link flow (BOARD row 101).  These shapes are the ones
            // lib.rs actually returns, so the UI is exercised against its real
            // contract rather than a convenient one.
            if (cmd === 'oauth_status') return Promise.resolve({ linked: false, provider: args && args.provider });
            if (cmd === 'oauth_start_callback') return Promise.resolve({ ok: true, addr: '127.0.0.1:8765' });
            if (cmd === 'oauth_pkce_start') return Promise.resolve({ ok: true, challenge: 'E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM', method: 'S256' });
            if (cmd === 'oauth_authorize') return Promise.resolve('https://github.com/login/oauth/authorize?client_id=CID');
            if (cmd === 'oauth_open') return Promise.resolve({ ok: true });
            if (cmd === 'oauth_poll_callback') return Promise.resolve('code=THECODE&state=THESTATE');
            if (cmd === 'oauth_bind') return Promise.resolve({ ok: true, provider: args && args.provider });
            if (cmd === 'list_projects') return Promise.resolve([]);
            if (cmd === 'list_downloads') return Promise.resolve([]);
            if (cmd === 'plugins_list') return Promise.resolve([]);
            // Unknown commands still resolve null, matching app.js's own
            // no-Tauri behaviour, but they are recorded so a new command shows
            // up in `cmds` rather than silently vanishing.
            return Promise.resolve(null);
          },
        },
      };
      // localStorage is written during init; jsdom provides a real one as long
      // as the URL above is not opaque.
    },
  });

  // (0) No top-level function may be declared twice.
  //
  //     This is not a style rule.  app.js declared `function bindBrowse()`
  //     twice (once with the OAuth wiring, once without); because declarations
  //     hoist and the last wins, the FIRST body was dead code and the OAuth
  //     authorize button did nothing when clicked -- for as long as that
  //     duplicate existed.  Nothing caught it, because the panel suite only
  //     asked whether modules render and switch, and every module still did.
  //     A shadowed function is invisible in exactly the way a broken one is
  //     not, so it gets its own assertion.
  const names = topLevelFunctionNames(appJs);
  const dupes = [...new Set(names.filter((n, i) => names.indexOf(n) !== i))];
  assert.deepEqual(
    dupes,
    [],
    `app.js declares these top-level functions more than once: ${dupes.join(', ')}. ` +
      'The later declaration silently replaces the earlier one at runtime.',
  );

  const script = dom.window.document.createElement('script');
  script.textContent = appJs;
  dom.window.document.body.appendChild(script);
  await new Promise((r) => setTimeout(r, 300));

  const doc = dom.window.document;
  const $ = (s) => doc.querySelector(s);

  // (4) The shell rendered its regions.
  assert.ok($('#activitybar'), '#activitybar must exist');
  assert.ok($('#view-content'), '#view-content must exist');
  // Count MODULE buttons, not every .ab-item: the activity bar also carries
  // #theme-toggle (index.html), which is a control, not a panel.  Asserting on
  // `.ab-item` alone would fail for the wrong reason and, worse, would have to
  // be loosened to 9 -- hiding the distinction this suite cares about.
  const moduleButtons = doc.querySelectorAll('#activitybar [data-mod]');
  assert.equal(moduleButtons.length, 8, `expected 8 module buttons, got ${moduleButtons.length}`);
  assert.ok(
    doc.querySelector('#activitybar #theme-toggle'),
    'the theme toggle is expected to still be present; its disappearance is a UI regression',
  );

  // (5) The default module is home and its title is the declared one.
  assert.equal(
    $('#view-title') && $('#view-title').textContent.trim(),
    '主页',
    'the app must open on the home module ("主页")',
  );

  // (6) Every module must be switchable, and switching must change the view.
  //     This is the core of "the panels render" -- a button that does not
  //     change the view is a dead panel.
  const seenTitles = new Set();
  for (const id of EXPECTED_MODULES) {
    const btn = doc.querySelector(`#activitybar [data-mod="${id}"]`);
    assert.ok(btn, `no activitybar button for module "${id}"`);
    btn.dispatchEvent(new dom.window.MouseEvent('click', { bubbles: true }));
    await new Promise((r) => setTimeout(r, 20));
    const title = $('#view-title') && $('#view-title').textContent.trim();
    assert.ok(title && title.length > 0, `module "${id}" produced an empty title`);
    seenTitles.add(title);
    assert.ok(
      $('#view-content') && $('#view-content').children.length > 0,
      `module "${id}" rendered no content`,
    );
  }
  assert.equal(seenTitles.size, 8, `expected 8 distinct module titles, saw ${seenTitles.size}`);

  // (7) Back to home, and then the IPC sequence.
  doc.querySelector('#activitybar [data-mod="home"]').dispatchEvent(
    new dom.window.MouseEvent('click', { bubbles: true }),
  );
  await new Promise((r) => setTimeout(r, 50));

  const cmds = calls.map((c) => c.cmd);
  for (const expected of EXPECTED_IPC) {
    assert.ok(
      cmds.includes(expected),
      `expected IPC command "${expected}" to be invoked; saw ${JSON.stringify(cmds)}`,
    );
  }

  // (8) The OAuth link flow must go through PKCE and end at oauth_bind.
  //
  //     What this pins is the WIRING, not the cryptography: `app.js` has to ask
  //     for a challenge, put it in the authorize URL, and only then call
  //     oauth_bind -- the sequence that replaced the copy which promised a
  //     binding that no command performed.  The state/token logic itself is
  //     covered by the `oauth_loop` crate's own 36 tests, which can call it
  //     directly; a DOM test cannot reach into Rust.
  // The link card is rendered by `renderBrowse` (app.js:283-285), not by
  // settings -- verified by locating the enclosing renderer, not assumed.
  doc.querySelector('#activitybar [data-mod="browse"]').dispatchEvent(
    new dom.window.MouseEvent('click', { bubbles: true }),
  );
  await new Promise((r) => setTimeout(r, 50));
  const clientInput = $('#oauth-client');
  const ghBtn = $('#oauth-gh');
  assert.ok(clientInput, 'the browse module must still render the OAuth client-id field');
  assert.ok(ghBtn, 'the browse module must still render the GitHub authorize button');
  clientInput.value = 'CID';
  ghBtn.dispatchEvent(new dom.window.MouseEvent('click', { bubbles: true }));

  // The flow polls once a second; give it enough turns for one poll cycle.
  await new Promise((r) => setTimeout(r, 1400));

  const oauthCmds = calls.map((c) => c.cmd).filter((c) => c.startsWith('oauth_'));
  assert.ok(
    oauthCmds.includes('oauth_pkce_start'),
    `the link flow must request a PKCE challenge; saw ${JSON.stringify(oauthCmds)}`,
  );
  assert.ok(
    oauthCmds.indexOf('oauth_pkce_start') < oauthCmds.indexOf('oauth_authorize'),
    'the challenge must be obtained BEFORE the authorize URL is built',
  );
  assert.ok(
    oauthCmds.includes('oauth_bind'),
    `a received callback must reach oauth_bind; saw ${JSON.stringify(oauthCmds)}`,
  );
  // The challenge has to actually travel in the URL, or the token exchange
  // will be refused by the provider.
  const authCall = calls.find((c) => c.cmd === 'oauth_authorize');
  assert.ok(
    authCall && authCall.args && authCall.args.codeChallenge,
    'oauth_authorize must receive the PKCE codeChallenge',
  );
  const msg = $('#links-msg');
  assert.ok(
    msg && /已关联/.test(msg.textContent),
    `a successful bind must be reported to the user; got ${JSON.stringify(msg && msg.textContent)}`,
  );

  console.log(
    `infiverse panels: ok (8/${EXPECTED_MODULES.length} modules rendered+switchable, ${calls.length} IPC calls, oauth link flow wired)`,
  );
  dom.window.close();
})().catch((err) => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
