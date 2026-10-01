# Inimerse AI Tooling
> **核对提示**：本文件为设计/规范文档，实现状态可能已变化；权威总览以 [docs/API.md](../docs/API.md) 为准，状态与版本裁定见 [docs/STATUS.md](../docs/STATUS.md)。


Four layers of agent tooling for this repository. The third is the one this
repository actively maintains; the fourth is what makes several agents safe to
run at the same time.

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

## 4. `gate.sh`, `stream.sh`, `check_links.py` — working with several agents at once

When more than one DSH conversation works on this repository, three scripts keep
them from stepping on each other. The board and the rules are in
[docs/BOARD.md](../docs/BOARD.md); these are the tools.

### `gate.sh` — the acceptance gate

Five stages; exit 0 only if all pass. A branch is mergable when this is green.

```bash
tools/gate.sh                 # build + ctest + economy + plugin + links
tools/gate.sh --fast          # reuse the existing build/ (skip configure)
tools/gate.sh --only links    # one stage: build|ctest|economy|plugin|links
```

| Stage | Expectation |
| --- | --- |
| build | Release build, 0 error |
| ctest | **85 / 85** |
| economy | `tools/economy_migration.test.py` — **39 / 39** |
| plugin | `node tools/dsh-inimerse/verify.mjs --live` — **55 / 55** |
| links | `tools/check_links.py` — **0 broken** (66 markdown files, 209 links) |

When one of those numbers changes, update this table *and* the baseline row in
[docs/STATUS.md](../docs/STATUS.md) §1 — otherwise the next session gates against
a stale expectation.

### `stream.sh` — one working tree per conversation

A shared working tree is how one session's `git add -A` swallows another's
half-finished work (it happened: §43.5's work-in-progress rode into `fdcdb10`).
Each stream gets its own worktree at `.worktrees/<slug>` and its own branch
`stream/<slug>` — **the branch existing is what "claimed" means**, so no lock
file is needed and every session sees it immediately via `git worktree list`.

```bash
tools/stream.sh new verse-upp     # .worktrees/verse-upp on branch stream/verse-upp
tools/stream.sh list              # worktrees, branches, dirty state, commits ahead
tools/stream.sh cd verse-upp      # prints the path: cd "$(tools/stream.sh cd verse-upp)"
tools/stream.sh sync verse-upp    # merge current main into the stream
tools/stream.sh rm verse-upp      # refuses if dirty; --force to discard
tools/stream.sh prune             # drop worktrees whose branch is already merged
```

Each worktree has its own `build/`, so streams can build concurrently.

### `check_links.py` — relative links in Markdown

Moved a document into `docs/archive/` and left a dozen links pointing at where it
used to be? That is the failure mode this catches. External URLs are never
fetched (the gate must not depend on the network); only links that have to
resolve inside the checkout are checked.

```bash
tools/check_links.py          # exit 1 if anything is broken
tools/check_links.py -v       # also list the links that resolve
tools/check_links.py --json   # machine-readable
```

It blanks fenced code blocks *and* inline code spans before parsing, because
`` `object["m"](...)` `` in prose is not a link.

## Claude Desktop registration

The sample config `claude_desktop_config.sample.json` registers the server:
copy the `inimerse` entry into `%APPDATA%\Claude\claude_desktop_config.json`
under `mcpServers`, then restart Claude Desktop.
