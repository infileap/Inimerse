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

Fifteen stages; exit 0 only if all pass. A branch is mergable when this is green.
Run it **serially** — two gates at once bind overlapping ports and manufacture the
failures [docs/STATUS.md](../docs/STATUS.md) §2.9 records.

```bash
tools/gate.sh                 # all fifteen stages
tools/gate.sh --fast          # reuse the existing build/ (skip configure)
tools/gate.sh --only links    # one stage: build|ctest|fuzz|economy|node|plugin|oauth-loop
                              #            |ignored-credentials|links|doc-paths
                              #            |text-integrity|orphan-fixtures|orphan-targets
                              #            |line-refs|release-tags
                              # `orphan-targets` asks whether a target is run, not
                              # whether it was compiled here — a target can be run by a
                              # CTest and still be named only inside a platform branch
                              # ([docs/AUDIT.md](../docs/AUDIT.md) §1.71).
                              # `line-refs` asserts the denominators of the sweep over
                              # line numbers `docs/` cites in `CMakeLists.txt` and
                              # `gate.sh`: how many are written in each of the four
                              # forms, and how many have nothing holding them. It does
                              # not assert that any number is right.
                              # `release-tags` asks whether the sha a document records
                              # for a tag is still the sha that tag points at. A tag is
                              # a moving pointer and moving one leaves no trace locally
                              # (`git reflog show <tag>` prints nothing), so a number
                              # that was true when written and is false now cannot be
                              # caught by reading ([docs/AUDIT.md](../docs/AUDIT.md)
                              # §1.74 E).
tools/gate.sh --required-for <base>..<head>|staged|worktree|<rev>|<path>
                              # print the stages that change forces, one selector per
                              # line, then `required: N stage(s)` and a bracket saying
                              # which question was answered — a range is committed
                              # history and does not read the working tree, a single
                              # rev does, and a bare path is the unstaged changes to
                              # that path. All three print the same `0 stage(s)`, so
                              # the bracket is what tells a mistyped path from a change
                              # that needs nothing. `staged` and `worktree` name the two
                              # questions git's own syntax has no spelling for. Runs
                              # nothing, so a green answer here is not a green gate.
                              # The stage list is the same registry `--only` reads;
                              # which path forces which stage comes from each
                              # `stage_*` function's own call sites, from the scopes
                              # written in `gate.sh` (each row carries the reason it
                              # cannot be derived), and from `check_text_integrity.py`'s
                              # own suffix/name lists. The scopes table and the registry
                              # watch each other, and all three directions can go red: a
                              # scopes row naming a stage the registry does not declare
                              # exits 2; a registered stage that no scope row and no
                              # `all` row can reach exits 2; and a registered stage that
                              # ONLY the `all` rows reach exits 2 as well -- an `all`
                              # row fires only for `tools/gate.sh` and `CMakeLists.txt`,
                              # so a stage nothing else reaches is reachable on paper and
                              # excused in silence for every other change there is.
```

| Stage | Expectation |
| --- | --- |
| build | Release build, 0 error |
| ctest | **148 / 148** |
| economy | `tools/economy_migration.test.py` — **39 / 39** |
| fuzz | `tools/im_diff_fuzz.py` — 3 seeds x 120 programs, **0 findings** |
| node | `node tools/node_suites/run_all.js` — **12 / 12** |
| plugin | `node tools/dsh-inimerse/verify.mjs --live` — **55 / 55** |
| oauth-loop | `oauth_loop` crate — **75 / 75** |
| ignored-credentials | `userdata/` ignore rules — default deny |
| links | `tools/check_links.py` — **0 broken** |
| doc-paths | `tools/check_doc_paths.py` — **0 broken** |
| text-integrity | `tools/check_text_integrity.py` — **0 files with NUL**；清单里每个条目要么命中一个受管文件、要么在 `NO_FILE_TODAY` 里被点名（今天 13 条），新出现的死条目与「活过来」的已声明条目都红 |
| orphan-fixtures | `tools/check_orphan_fixtures.py` — **0 orphans** |
| orphan-targets | `tools/check_orphan_targets.py` — **0 orphans**, where a target is measured by the target some `add_test( )` runs and never by the test's name. It answers *is it run*, not *was it compiled here*: a target can be run by a CTest and still be named only inside a platform branch, and then the PASS has no relation to the change ([docs/AUDIT.md](../docs/AUDIT.md) §1.71) |
| line-refs | `tools/check_line_refs.py` — **pins held**: the input set is non-empty, all four writings are present, the explicit-form count *equals* its pin (an equality, not a floor: the house rule writes citations as `file:N`, so a growing repository must grow this number, and the equality forces every growth to say why), and the unanchored count has not grown past its ceiling. It asserts denominators, not an anchor rate, and it does not assert that any number is right: a number still there that now points at something else is not caught ([docs/AUDIT.md](../docs/AUDIT.md) §1.72). The input set is hermetic: **only a file git tracks can hold a number**, because an untracked build artefact can hold one -- before that fix the same commit read 520 held / 515 unanchored in a tree carrying `build/` and 514 / 521 in a depth-1 clone of it, and six numbers were held by artefacts the clone does not contain. The pins are denominators taken on a git-tracked-only tree, re-taken after merging `origin/main` = `68248e4` `[obs: c3f7857]`, and **writing that row is not the only thing that expires them -- it is not even the last thing**: six later commits on this branch moved neither number, and a merge moved both. `56bf4c5` read 720 / 521, `6de3d9a` (this branch's pre-merge tip) read 720 / 521, main at the merge read 730 / 506, and the first merge read 734 / 530 -- +14 explicit and +9 unanchored, of which 4 explicit are this branch's own row examples and 10 are main's later documentation commits; 9 of those 10 were written for main's 850-line `tools/gate.sh` while the merged tree carries this branch's 1045-line one, so they have nothing holding them. **A second shift came from T2 and it moved the other file**: at `main` = `fa8247e`, `CMakeLists.txt` had grown by 17 lines (an insert near line 1357, `add_test(NAME xrange_t2_runtime ...)`) while `tools/gate.sh` changed one line and **kept its line count**, so none of that loss is gate.sh's doing. The T2-merged tree read **750 / 548**; merging `origin/main` = `fb18bd7` took it to **762 / 551** -- all twelve explicit on ONE appended `docs/BOARD.md` row, while `docs/AUDIT.md` grew 39 lines and added none -- rewriting nine `docs/DECFY_DESIGN.md:<N>` citations as section numbers took it to **758 / 549**, and merging `origin/main` = `2d0ecd8`, which brought a new file (`docs/streams/builtin-platform-census.md`, 3 references of which 2 have no anchor), took it to **758 / 551**; the merge of `origin/main` = `68248e4` was neutral, and writing this account moved it to **761 / 552**: +15 explicit arrive with T2 (13 in `docs/BOARD.md`, 2 in the new `docs/streams/win-source-attribution.md`) and +20 unanchored split by target into 18 into `CMakeLists.txt` and 2 into `tools/gate.sh` -- and those 2 are the most instructive: they were held before T2 **not by the file the report labels as their target** (those numbers name `src/parser/parser.c`, 1832 lines, in range; the label is a fallback, see the honesty boundaries) **but by `src/compiler/compiler.c`, named on the same line**, which T2 edited. So "does this reference have an anchor" and "did the file it names move" are two different questions, and the three causes (gate.sh shift / CMakeLists shift / anchor living elsewhere) are told apart by reading `hits`, not by diffing. The pins were re-taken after the last merge and after this account was written, and **the count is moved not only by whoever writes that row but by everyone afterwards who cites either of the two files** -- so the row and the numbers in it are one action, and re-taking them is part of editing it. What has to be enumerated is not the diff: it is **every line in `docs/` that cites the file that moved** -- here 30 references lost their anchor, 29 of them into `tools/gate.sh`, and 20 of those sit in `docs/AUDIT.md`, a file this branch never edited. |
| release-tags | `tools/check_release_tags.py` — **every `tag → sha` binding in `docs/` agrees with `git rev-parse`**, and every version named in backticks is a tag that exists. The tag names come from `git tag -l`, never from the file, so the next release adds one without anyone editing it. It says the documents and the tags agree *right now*: it cannot see a tag that moves after the run, and it cannot tell where a tag was meant to point. **An exemption is a shape, not a file**: a fenced block, a quotation of someone else's document, or a recollection -- a line that says in its own words that the binding is a memory (`记录`, `初稿写过`, `曾经的`, `[obs:`) *and* carries its falsifier on the same or an adjacent line (`失效`, `不再`, `已移到`, `旧值`). Exempting a whole file by name was the old rule, and it printed "the tag moved and the document did not" even when the document had followed the move: that is "I did not recognise a recollection" written as "the document did not follow". **Every exemption class must have at least one member or the run is red** -- an empty class is a mechanism that never runs while its docstring says it is on watch, and the correct fix is to delete the class (which is what happened to the blockquote class: zero members repository-wide); a class that excused something without being declared is red too. The floor `EXP_TAG_BINDINGS` is 2 ([docs/AUDIT.md](../docs/AUDIT.md) §1.74 E) |

When one of those numbers changes, update this table *and* the baseline row in
[docs/STATUS.md](../docs/STATUS.md) §1 — otherwise the next session gates against
a stale expectation. That is not hypothetical: `upp-in-engine` and
`vverse-produce` each saw only **87** tests on their own branch (85 + their own
two probes) and the merged tree has **89**.

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
