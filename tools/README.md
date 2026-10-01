# Inimerse AI Tooling
> **核对提示**：本文件为设计/规范文档，实现状态可能已变化；权威总览以 [docs/API.md](../docs/API.md) 为准，状态与版本裁定见 [docs/STATUS.md](../docs/STATUS.md)。


Three ways for an LLM agent to execute and fix Inimerse code safely. The third is
the one this repository actively maintains.

## 1. `ai_run.ps1` — one-shot sandboxed runner

```
powershell -File D:\inimerse_stable\tools\ai_run.ps1 test.im            # safe, 10s
powershell -File D:\inimerse_stable\tools\ai_run.ps1 test.im -Timeout 30
powershell -File D:\inimerse_stable\tools\ai_run.ps1 test.im -Unsafe    # allow dangerous builtins
```

Always adds `--safe --err-json` unless `-Unsafe`. Errors come back as one
JSON line: `{"error":"parse|exception|io","line","col","expect","got","fix"}`.
Exit code is 1 on failure (uncaught exception / compile error), 0 on success.

## 2. `mcp_server.js` — MCP (Model Context Protocol) stdio server

Speaks MCP 2024-11-05 over stdio; register it in any MCP client
(Claude Desktop, Cursor, custom agents) as:

```
node D:\inimerse_stable\tools\mcp_server.js
```

Exposes one tool:

### `run_im(script, timeout?)`
- `script`: full .im source (UTF-8)
- `timeout`: seconds, default 10, max 120

Returns `stdout`, `stderr`, `exit_code` and the parsed `error` object
(`{error,line,col,expect,got,fix}` or `{error:"exception",message,ip,frames}`).
`isError` is true when the run failed — drive your fix loop off it.

The engine binary can be overridden with `INIMERSE_EXE`.

## 3. `dsh-inimerse/` — DSH harness bridge (the current path)

A **DSH Host-only Cordis bundle**. Where the two tools above hand an agent one
narrow verb (`run_im`) over stdio, this one makes the engine a **native
capability of the agent session** and returns structured JSON for the whole
development loop — not just script execution.

| Tool | What it reports |
| --- | --- |
| `inim_status` | engine version, which binaries exist, git branch/commit/dirty, declared CTest count |
| `inim_build` | configure + build, with exit codes, warning/error counts and the log tail |
| `inim_test` | CTest totals **and the names of the failing tests** |
| `inim_run` | script or inline source, with stdout/stderr/exit code |
| `inim_verse` | a full `inim-server` session: hello, ops, drain, **verbatim transcript** |

It owns no engine logic: every mutation goes through the real binaries, so the
boundary rules the engine enforces — the server assigns `seq`/`rev`, a client may
not assert final state, an unanchored log refuses a session — apply unchanged,
and several tools exist to make those refusals *visible* rather than to hide
them.

```bash
node tools/dsh-inimerse/verify.mjs                   # offline: schemas + error paths
node tools/dsh-inimerse/verify.mjs --live            # also runs a script and a Verse round trip
node tools/dsh-inimerse/verify.mjs --live --build     # also rebuilds (minutes)
```

See [dsh-inimerse/README.md](dsh-inimerse/README.md) for the install steps, the
config keys and the design notes. Install it with the DSH Plugin Manager as a
local bundle pointed at this directory — never by hand-editing the profile.

## Typical agent loop

1. Write `.im` code following the language reference in [docs/API.md](../docs/API.md).
2. Run it — `inim_run` under DSH, or `run_im` over MCP.
3. Read the structured `error` → patch the code.
4. Repeat until `exit_code == 0` and stdout matches expectations.
5. `inim_test` before claiming the change is safe; `inim_build` to reproduce the
   release gate from scratch when it is not.


## Claude Desktop registration

The sample config `claude_desktop_config.sample.json` registers the server:
copy the `inimerse` entry into `%APPDATA%\Claude\claude_desktop_config.json`
under `mcpServers`, then restart Claude Desktop.
