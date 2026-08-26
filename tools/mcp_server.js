#!/usr/bin/env node
/* inimerse-mcp: Model Context Protocol (stdio) server exposing the Inimerse
 * sandbox to LLM agents. Tools: run_im (execute .im source safely).
 *
 * Protocol: MCP 2024-11-05, JSON-RPC 2.0 over stdio.
 * Every run uses: --safe (24 dangerous builtins blocked) --err-json
 * (structured single-line error JSON) and a default 10s time limit, so the
 * AI gets back {stdout, error, exit_code} and can fix the script in a loop.
 */
'use strict';
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const ENGINE = process.env.INIMERSE_EXE || 'D:/inimerse_stable/inimerse.exe';
const TMP = path.join(os.tmpdir(), 'inimerse_mcp');
try { fs.mkdirSync(TMP, { recursive: true }); } catch (e) {}

function send(msg) { process.stdout.write(JSON.stringify(msg) + '\n'); }

function runIm(scriptText, timeoutSec) {
  const file = path.join(TMP, 'mcp_' + process.pid + '_' + Date.now() + '.im');
  fs.writeFileSync(file, scriptText, 'utf8');
  const args = ['--safe', '--err-json', '--time-limit', String(timeoutSec || 10), file];
  let stdout = '', errText = '', exitCode = 0;
  try {
    const out = execFileSync(ENGINE, args, { encoding: 'buffer', timeout: (timeoutSec || 10) * 1000 + 5000 });
    stdout = out.toString('utf8');
  } catch (e) {
    if (e.stdout) stdout = Buffer.isBuffer(e.stdout) ? e.stdout.toString('utf8') : String(e.stdout);
    if (e.stderr) errText = Buffer.isBuffer(e.stderr) ? e.stderr.toString('utf8') : String(e.stderr);
    exitCode = e.status == null ? (e.killed ? 124 : 1) : e.status;
    if (e.killed && exitCode === 124) errText += '[timeout] killed\n';
  } finally {
    try { fs.unlinkSync(file); } catch (e2) {}
  }
  // the structured error (if any) is the last JSON line on stderr
  let errJson = null;
  for (const line of (errText + stdout).split('\n')) {
    const t = line.trim();
    if (t.startsWith('{"error"')) { try { errJson = JSON.parse(t); } catch (e3) {} }
  }
  return { stdout, stderr: errText, exit_code: exitCode, error: errJson };
}

let pending = new Map(); // id -> {resolve}
process.stdin.setEncoding('utf8');
let buf = '';
process.stdin.on('data', (chunk) => {
  buf += chunk;
  let idx;
  while ((idx = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, idx).trim();
    buf = buf.slice(idx + 1);
    if (!line) continue;
    let msg;
    try { msg = JSON.parse(line); } catch (e) { continue; }
    handle(msg);
  }
});

function handle(msg) {
  const { id, method, params } = msg;
  if (method === 'initialize') {
    send({ jsonrpc: '2.0', id, result: {
      protocolVersion: '2024-11-05',
      capabilities: { tools: { listChanged: false } },
      serverInfo: { name: 'inimerse-mcp', version: '1.0.0' }
    }});
    return;
  }
  if (method === 'notifications/initialized' || method === 'notifications/cancelled') return;
  if (method === 'ping') { send({ jsonrpc: '2.0', id, result: {} }); return; }
  if (method === 'tools/list') {
    send({ jsonrpc: '2.0', id, result: { tools: [
      {
        name: 'run_im',
        description: 'Execute an Inimerse (.im) script in the safe sandbox ' +
          '(--safe + --time-limit + --err-json). Returns stdout, stderr, exit ' +
          'code and the structured error JSON. Use it to test/fix generated .im code.',
        inputSchema: {
          type: 'object',
          properties: {
            script: { type: 'string', description: 'Full .im source code to run' },
            timeout: { type: 'number', description: 'Time limit in seconds (default 10, max 120)' }
          },
          required: ['script']
        }
      }
    ]}});
    return;
  }
  if (method === 'tools/call') {
    const name = params && params.name;
    const args = (params && params.arguments) || {};
    if (name !== 'run_im' || typeof args.script !== 'string') {
      send({ jsonrpc: '2.0', id, result: { content: [{ type: 'text', text: 'usage: run_im({script, timeout?})' }], isError: true } });
      return;
    }
    try {
      const t = Math.min(120, Math.max(1, Math.floor(args.timeout || 10)));
      const res = runIm(args.script, t);
      const text = [
        'exit_code: ' + res.exit_code,
        '--- stdout ---',
        res.stdout.trimEnd(),
        '--- stderr ---',
        res.stderr.trimEnd(),
        res.error ? ('--- structured error ---\n' + JSON.stringify(res.error)) : ''
      ].join('\n');
      send({ jsonrpc: '2.0', id, result: { content: [{ type: 'text', text }], isError: res.exit_code !== 0 } });
    } catch (e) {
      send({ jsonrpc: '2.0', id, result: { content: [{ type: 'text', text: 'mcp internal error: ' + e.message }], isError: true } });
    }
    return;
  }
  send({ jsonrpc: '2.0', id, result: {} });
}
